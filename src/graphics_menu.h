// crackdown2 - custom in-game Graphics settings overlay.
//
// Demonstrates hooking the SDK's ImGui overlay system (rex::ui::ImGuiDrawer /
// ImGuiDialog) to add a brand-new menu of our own, separate from the game's
// Scaleform UI and from the SDK's built-in catch-all settings overlay (F4).
//
// Toggle with F5 (bind name "bind_graphics_menu"). Every control reads and
// writes real cvars through the runtime's by-name API, so it drives both the
// SDK's own graphics cvars (vsync, resolution, present_effect, ...) and this
// project's c2_* cvars across the DLL boundary. Changes that the engine can
// apply live take effect immediately; the rest are flagged as needing a
// restart, mirroring how the SDK's settings overlay reports them.

#pragma once

#include <rex/cvar.h>
#include <rex/rex_app.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>

#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

class GraphicsMenu : public rex::ui::ImGuiDialog {
 public:
  explicit GraphicsMenu(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

  void ToggleVisible() { visible_ = !visible_; }
  void SetVisible(bool v) { visible_ = v; }
  bool visible() const { return visible_; }

 protected:
  void OnDraw(ImGuiIO& /*io*/) override {
    if (!visible_) {
      return;
    }
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    if (!ImGui::Begin("Crackdown 2 - Graphics##c2gfx", &visible_,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::End();
      return;
    }

    ImGui::TextDisabled("Press F5 to close. Items marked * need a restart.");
    ImGui::Separator();

    BoolRow("V-Sync", "vsync");
    BoolRow("Fullscreen", "fullscreen");
    BoolRow("Show FPS overlay", "c2_fps_display");
    BoolRow("Native 2x MSAA", "native_2x_msaa");

    ImGui::Separator();

    ComboRow("Upscaling / output", "present_effect",
             {"bilinear", "cas", "fsr", "fsr2", "fsr3"});
    ComboRow("Resolution", "resolution",
             {"", "1280x720", "1920x1080", "2560x1440", "3840x2160"});
    IntSliderRow("Internal res scale *", "draw_resolution_scale_x", 1, 4);
    ComboRow("GPU backend *", "c2_gpu_backend", {"any", "d3d12", "vulkan"});

    ImGui::Separator();
    if (ImGui::Button("Reset these to defaults")) {
      for (const char* n : {"vsync", "fullscreen", "c2_fps_display", "native_2x_msaa",
                            "present_effect", "resolution", "draw_resolution_scale_x",
                            "draw_resolution_scale_y", "c2_gpu_backend"}) {
        if (rex::cvar::GetFlagInfo(n)) {
          rex::cvar::ResetToDefault(n);
        }
      }
    }

    auto pending = rex::cvar::GetPendingRestartFlags();
    if (!pending.empty()) {
      ImGui::Separator();
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                         "Restart required for %zu change(s).", pending.size());
    }

    ImGui::End();
  }

 private:
  static bool Exists(const char* name) { return rex::cvar::GetFlagInfo(name) != nullptr; }

  void BoolRow(const char* label, const char* cvar) {
    if (!Exists(cvar)) {
      return;
    }
    bool v = rex::cvar::Query<bool>(cvar);
    if (ImGui::Checkbox(label, &v)) {
      rex::cvar::SetFlagByName(cvar, v ? "true" : "false");
    }
  }

  void IntSliderRow(const char* label, const char* cvar, int lo, int hi) {
    if (!Exists(cvar)) {
      return;
    }
    int v = rex::cvar::Query<int32_t>(cvar);
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderInt(label, &v, lo, hi)) {
      rex::cvar::SetFlagByName(cvar, std::to_string(v));
      // keep X and Y scale in lockstep for a single "internal res" control.
      if (std::strcmp(cvar, "draw_resolution_scale_x") == 0 &&
          Exists("draw_resolution_scale_y")) {
        rex::cvar::SetFlagByName("draw_resolution_scale_y", std::to_string(v));
      }
    }
  }

  void ComboRow(const char* label, const char* cvar, std::vector<const char*> options) {
    if (!Exists(cvar)) {
      return;
    }
    std::string cur = rex::cvar::Query<std::string>(cvar);
    const char* preview = cur.empty() ? "(auto)" : cur.c_str();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo(label, preview)) {
      for (const char* opt : options) {
        bool selected = (cur == opt);
        const char* shown = (opt[0] == '\0') ? "(auto)" : opt;
        if (ImGui::Selectable(shown, selected)) {
          rex::cvar::SetFlagByName(cvar, opt);
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
  }

  bool visible_ = false;
};
