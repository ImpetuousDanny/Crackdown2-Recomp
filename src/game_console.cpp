#include "game_console.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <deque>
#include <mutex>
#include <sstream>
#include <vector>

REXCVAR_DEFINE_STRING(c2_console_startup, "", "Game",
                      "Developer console commands to run once a few seconds after boot, separated by ';' "
                      "(for example \"perfgraph;gputiming\"). See docs/console_commands.md.");

extern "C" {
void sub_825CE628(PPCContext& ctx, uint8_t* base);        // Console_Execute(console, wchar_t* line, flags)
void __imp__RWS_MainLoop_Poll(PPCContext& ctx, uint8_t* base);
}

namespace {

// Core globals live at 0x83800000; +2804 holds the console object pointer
// (Core_Init executes its "minp press ..." input-map commands through it).
constexpr uint32_t kConsoleObjectPtr = 0x83800000 + 2804;
// Ticks to wait before running --c2_console_startup (the front end is up by then).
constexpr uint32_t kStartupDelayTicks = 240;

std::mutex g_mutex;
std::deque<std::string> g_queue;
uint32_t g_ticks = 0;
bool g_startup_done = false;

template <typename T>
T ld(uint8_t* base, uint32_t addr) {
  return static_cast<T>(*rex::memory::GuestPtr<rex::be<T>*>(base, addr));
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

}  // namespace

namespace c2 {

bool ConsoleReady() {
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  return ld<uint32_t>(base, kConsoleObjectPtr) != 0;
}

bool ExecConsoleCommand(PPCContext& ctx, uint8_t* base, const std::string& command) {
  const uint32_t console = ld<uint32_t>(base, kConsoleObjectPtr);
  if (!console || command.empty()) {
    REXLOG_WARN("console: cannot run '{}' (console object {:#x})", command, console);
    return false;
  }
  REXLOG_INFO("console: executing '{}'", command);

  // The caller's volatile registers are live (we run after the original
  // function returned, before its caller resumes), so keep a copy.
  PPCContext saved = ctx;
  std::vector<uint8_t> line = encode_utf16(command);
  const uint32_t guest_line = rex::ppc::stack_push(ctx, base, line.data(), uint32_t(line.size()));
  ctx.r3.u32 = console;
  ctx.r4.u32 = guest_line;
  ctx.r5.u32 = 0;
  sub_825CE628(ctx, base);
  rex::ppc::stack_pop(ctx, uint32_t(line.size()));
  ctx = saved;
  REXLOG_INFO("console: {}", command);
  return true;
}

void QueueConsoleCommand(const std::string& command) {
  if (command.empty()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_queue.push_back(command);
}

}  // namespace c2

// RWS_MainLoop_Poll runs once per frame on the game thread (it also drives
// UI_Update, so menu actions and console commands share a thread): drain the queue
// there so commands from the UI thread execute with a proper guest context.
extern "C" void RWS_MainLoop_Poll(PPCContext& ctx, uint8_t* base) {
  __imp__RWS_MainLoop_Poll(ctx, base);

  if (g_ticks == 0) REXLOG_INFO("console: main loop poll running (console {})", c2::ConsoleReady() ? "ready" : "not ready");
  if (!g_startup_done && ++g_ticks >= kStartupDelayTicks) {
    g_startup_done = true;
    std::string startup = REXCVAR_GET(c2_console_startup);
    std::stringstream ss(startup);
    std::string cmd;
    while (std::getline(ss, cmd, ';')) {
      if (!cmd.empty()) c2::QueueConsoleCommand(cmd);
    }
  }

  std::deque<std::string> pending;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    pending.swap(g_queue);
  }
  for (const std::string& cmd : pending) {
    c2::ExecConsoleCommand(ctx, base, cmd);
  }
}
