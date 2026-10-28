// crackdown2 - native "Graphics" rows inside the game's own Display Options screen.
//
// Crackdown 2's front-end menus are built in C++ on top of a generic row
// framework (Scaleform GFx only renders the rows). Reverse-engineering notes
// live in docs/native_menu.md. The short version:
//
//   * A screen (component) owns menus; menu+44..+48 is the item pointer array.
//   * Menu_AddItem (sub_828F3260) allocates a 308-byte item: +36 kind, +40
//     action (component id to push for kind 2), +178/+179/+180 tooltip.
//   * Item_SetText (sub_828F3400, item, slot0, slot1) attaches a 652-byte text
//     object: +1 / +2 "has text" flags, +12 UTF-16BE wchar[64] slot 0 (the
//     centred value column), +140 wchar[64] slot 1 (the left-aligned label).
//     The stock rows pass their label as slot 1; the Controls screen keeps its
//     YES/NO in slot 0 and rewrites that on toggle, which is what we copy.
//   * Pressing A on a kind-3 row makes the framework call
//     screen->OnMenuEvent(0, menuIdx, rowIdx) through a bctrl at 0x828F1014.
//     Display Options uses the shared no-op for that slot, so we intercept the
//     call site with a midasm hook and handle our own rows there.
//
// Result: the rows render with the game's font, cursor, sounds and legend,
// scroll with the stock rows, and toggle real engine cvars when activated.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Recompiled guest routines. `__imp__name` is always the original body; the
// plain symbol is a weak alias that a REX_FUNC definition may replace.
extern "C" {
void __imp__sub_828B5128(PPCContext& ctx, uint8_t* base);  // FeDisplayOptions::Init
void sub_828F3260(PPCContext& ctx, uint8_t* base);         // Menu_AddItem(menu) -> item
void sub_828F3400(PPCContext& ctx, uint8_t* base);         // Item_SetText(item, slot0, slot1)
}

namespace {

constexpr uint32_t kDisplayOptionsVtable = 0x82164968;

// Menu object
constexpr uint32_t kMenuItemsBegin = 44;
constexpr uint32_t kMenuItemsEnd = 48;
// Item object
constexpr uint32_t kItemDirty = 10;
constexpr uint32_t kItemTextIndex = 12;
constexpr uint32_t kItemTexts = 24;
constexpr uint32_t kItemKind = 36;
constexpr uint32_t kItemAction = 40;
constexpr uint32_t kItemHasTip = 178;
constexpr uint32_t kItemTipEnabled = 179;
constexpr uint32_t kItemTip = 180;
constexpr uint32_t kItemKindNotifyOnly = 3;
// Text object
constexpr uint32_t kTextHasSlot0 = 1;
constexpr uint32_t kTextSlot0 = 12;
constexpr uint32_t kTextChars = 64;

template <typename T>
T ld(uint8_t* base, uint32_t addr) {
  return static_cast<T>(*rex::memory::GuestPtr<rex::be<T>*>(base, addr));
}
template <typename T>
void st(uint8_t* base, uint32_t addr, T value) {
  *rex::memory::GuestPtr<rex::be<T>*>(base, addr) = value;
}

uint8_t* guest_base() { return rex::system::kernel_state()->memory()->virtual_membase(); }

// Write an ASCII string as a NUL-terminated UTF-16BE wchar[max_chars] buffer.
void put_utf16(uint8_t* base, uint32_t dst, const std::string& s, size_t max_chars) {
  size_t n = std::min(s.size(), max_chars - 1);
  for (size_t i = 0; i < n; ++i) {
    st<uint16_t>(base, dst + uint32_t(i * 2), uint16_t(uint8_t(s[i])));
  }
  st<uint16_t>(base, dst + uint32_t(n * 2), 0);
}

std::vector<uint8_t> encode_utf16(const std::string& s) {
  std::vector<uint8_t> out;
  out.reserve(s.size() * 2 + 2);
  for (unsigned char c : s) {
    out.push_back(0);
    out.push_back(c);
  }
  out.push_back(0);
  out.push_back(0);
  return out;
}

struct Choice {
  const char* value;  // cvar value
  const char* shown;  // text on the row
};

struct Row {
  const char* label;
  const char* tip;
  const char* cvar;
  std::vector<Choice> choices;
  const char* mirror_cvar = nullptr;  // second cvar kept in lockstep (resolution scale y)
};

const Choice kOnOff[] = {{"false", "OFF"}, {"true", "ON"}};

std::vector<Row>& rows() {
  static std::vector<Row> r = {
      {"V-SYNC", "TOGGLE VERTICAL SYNC", "vsync", {kOnOff[0], kOnOff[1]}},
      {"FULLSCREEN", "SWITCH BETWEEN WINDOWED AND FULLSCREEN", "fullscreen", {kOnOff[0], kOnOff[1]}},
      {"FPS COUNTER", "SHOW THE FRAME RATE OVERLAY", "c2_fps_display", {kOnOff[0], kOnOff[1]}},
      {"UPSCALING FILTER", "OUTPUT FILTER: BILINEAR, CAS SHARPEN OR FSR",
       "present_effect",
       {{"bilinear", "BILINEAR"}, {"cas", "CAS"}, {"fsr", "FSR"}, {"fsr2", "FSR 2"}, {"fsr3", "FSR 3"}}},
      {"2X MSAA", "NATIVE 2X MSAA (TAKES EFFECT AFTER RESTART)", "native_2x_msaa",
       {kOnOff[0], kOnOff[1]}},
      {"RENDER SCALE", "INTERNAL RESOLUTION MULTIPLIER (TAKES EFFECT AFTER RESTART)",
       "draw_resolution_scale_x",
       {{"1", "1X"}, {"2", "2X"}, {"3", "3X"}},
       "draw_resolution_scale_y"},
  };
  return r;
}

size_t current_choice(const Row& row) {
  if (!rex::cvar::GetFlagInfo(row.cvar)) return 0;
  std::string cur = rex::cvar::GetFlagByName(row.cvar);
  for (size_t i = 0; i < row.choices.size(); ++i) {
    if (cur == row.choices[i].value) return i;
  }
  // bool cvars may serialize as 0/1
  if (cur == "1" || cur == "0") return cur == "1" ? 1 : 0;
  return 0;
}

std::string row_value(const Row& row) { return row.choices[current_choice(row)].shown; }

// Screen instance whose Display Options menu currently carries our rows, the
// index of our first row within that menu, and the item pointers.
uint32_t g_screen = 0;
uint32_t g_first_row = 0;
struct AddedRow { uint32_t item; size_t row; };
std::vector<AddedRow> g_items;

}  // namespace

// Replaces FeDisplayOptions::Init: run the original (loads OptionsScreen.gfx,
// creates the menu, adds HUD OPACITY and GAMMA), then append our rows.
extern "C" void sub_828B5128(PPCContext& ctx, uint8_t* base) {
  const uint32_t screen = ctx.r3.u32;
  __imp__sub_828B5128(ctx, base);

  uint32_t menus = ld<uint32_t>(base, screen + 48);
  uint32_t menu = menus ? ld<uint32_t>(base, menus) : 0;
  if (!menu) return;

  g_screen = screen;
  g_items.clear();
  g_first_row = (ld<uint32_t>(base, menu + kMenuItemsEnd) - ld<uint32_t>(base, menu + kMenuItemsBegin)) / 4;

  for (Row& row : rows()) {
    const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(row.cvar);
    if (!info) continue;
    // Keep only the values this runtime build accepts (e.g. present_effect is
    // bilinear-only when the SDK was built without FidelityFX), and drop rows
    // that would have nothing to cycle through.
    if (info->constraints.HasAllowedValues()) {
      const auto& allowed = info->constraints.allowed_values;
      std::vector<Choice> kept;
      for (const Choice& c : row.choices) {
        if (std::find(allowed.begin(), allowed.end(), c.value) != allowed.end()) kept.push_back(c);
      }
      row.choices = std::move(kept);
    }
    if (row.choices.size() < 2) continue;

    ctx.r3.u32 = menu;
    sub_828F3260(ctx, base);
    const uint32_t item = ctx.r3.u32;
    if (!item) break;

    std::vector<uint8_t> value = encode_utf16(row_value(row));
    std::vector<uint8_t> label = encode_utf16(row.label);
    uint32_t guest_value = rex::ppc::stack_push(ctx, base, value.data(), uint32_t(value.size()));
    uint32_t guest_label = rex::ppc::stack_push(ctx, base, label.data(), uint32_t(label.size()));
    ctx.r3.u32 = item;
    ctx.r4.u32 = guest_value;  // slot 0: value column
    ctx.r5.u32 = guest_label;  // slot 1: label column (what HUD OPACITY / GAMMA use)
    sub_828F3400(ctx, base);
    // Each push is aligned on its own, so pop them one at a time in reverse.
    rex::ppc::stack_pop(ctx, uint32_t(label.size()));
    rex::ppc::stack_pop(ctx, uint32_t(value.size()));

    base[item + kItemHasTip] = 1;
    base[item + kItemTipEnabled] = 1;
    put_utf16(base, item + kItemTip, row.tip, kTextChars);
    st<uint32_t>(base, item + kItemKind, kItemKindNotifyOnly);
    st<int32_t>(base, item + kItemAction, -1);
    g_items.push_back({item, size_t(&row - rows().data())});
  }
}

// Midasm hook just before the framework's screen->OnMenuEvent(0, menu, row)
// call for an activated row (default_hooks.toml, 0x828F1014). r3 = screen,
// r6 = selected row index, r26 = menu object.
void C2MenuItemActivated(PPCRegister& r3, PPCRegister& r5, PPCRegister& r6, PPCRegister& r26) {
  (void)r5;
  uint8_t* base = guest_base();
  const uint32_t screen = r3.u32;
  if (!screen || screen != g_screen) return;
  if (ld<uint32_t>(base, screen) != kDisplayOptionsVtable) return;

  const int32_t selected = int32_t(r6.u32);
  const int32_t index = selected - int32_t(g_first_row);
  if (index < 0 || size_t(index) >= g_items.size()) return;

  const uint32_t menu = r26.u32;
  const uint32_t item = ld<uint32_t>(base, ld<uint32_t>(base, menu + kMenuItemsBegin) + uint32_t(selected) * 4);
  if (item != g_items[size_t(index)].item) return;

  Row& row = rows()[g_items[size_t(index)].row];
  const size_t next = (current_choice(row) + 1) % row.choices.size();
  rex::cvar::SetFlagByName(row.cvar, row.choices[next].value);
  if (row.mirror_cvar && rex::cvar::GetFlagInfo(row.mirror_cvar)) {
    rex::cvar::SetFlagByName(row.mirror_cvar, row.choices[next].value);
  }

  const uint32_t texts = ld<uint32_t>(base, item + kItemTexts);
  const uint32_t text_index = ld<uint32_t>(base, item + kItemTextIndex);
  const uint32_t text_obj = texts ? ld<uint32_t>(base, texts + text_index * 4) : 0;
  if (!text_obj) return;
  put_utf16(base, text_obj + kTextSlot0, row_value(row), kTextChars);
  base[text_obj + kTextHasSlot0] = 1;
  base[item + kItemDirty] = 1;
}
