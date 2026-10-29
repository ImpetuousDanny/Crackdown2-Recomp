#include "game_console.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_STRING(c2_console_startup, "", "Game",
                      "Developer console commands to run once a few seconds after boot, separated by ';' "
                      "(for example \"perfgraph;gputiming\"). See docs/console_commands.md.");

extern "C" {
void sub_825CE628(PPCContext& ctx, uint8_t* base);        // Console_Execute(console, wchar_t* line, context)
void __imp__RWS_MainLoop_Poll(PPCContext& ctx, uint8_t* base);
}

namespace {

// Core globals live at 0x83800000; +2804 holds the console object pointer
// (Core_Init executes its "minp press ..." input-map commands through it).
constexpr uint32_t kConsoleObjectPtr = 0x83800000 + 2804;
// Local player object (Core globals + 3976). The game's own console callers
// pass `player + 1352` as the command context; player commands (TeleportPlayer,
// god, loaded, ...) check it and silently do nothing when it is 0.
constexpr uint32_t kLocalPlayerPtr = 0x83800000 + 3976;
constexpr uint32_t kPlayerContextOffset = 1352;
REXCVAR_DEFINE_INT32(c2_console_startup_delay, 4, "Game",
                     "Seconds after boot before --c2_console_startup runs (the front end is up after ~4; "
                     "a loaded save needs 60+). F8 runs the same commands again at any time.");

std::mutex g_mutex;
std::deque<std::string> g_queue;
uint32_t g_ticks = 0;
bool g_startup_done = false;
std::mutex g_quit_mutex;
std::function<void()> g_quit_handler;

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
  const uint32_t player = ld<uint32_t>(base, kLocalPlayerPtr);
  ctx.r5.u32 = player ? player + kPlayerContextOffset : 0;
  sub_825CE628(ctx, base);
  rex::ppc::stack_pop(ctx, uint32_t(line.size()));
  ctx = saved;
  REXLOG_INFO("console: {}", command);
  return true;
}

void RunStartupCommands() {
  std::string startup = REXCVAR_GET(c2_console_startup);
  std::stringstream ss(startup);
  std::string cmd;
  while (std::getline(ss, cmd, ';')) {
    if (!cmd.empty()) QueueConsoleCommand(cmd);
  }
}

void QueueConsoleCommand(const std::string& command) {
  if (command.empty()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_queue.push_back(command);
}

void SetQuitHandler(std::function<void()> handler) {
  std::lock_guard<std::mutex> lock(g_quit_mutex);
  g_quit_handler = std::move(handler);
}

void RequestQuit() {
  std::function<void()> handler;
  {
    std::lock_guard<std::mutex> lock(g_quit_mutex);
    handler = g_quit_handler;
  }
  if (handler) {
    REXLOG_INFO("console: quit requested from menu; closing window.");
    handler();
  } else {
    REXLOG_WARN("console: quit requested but no quit handler installed.");
  }
}

void RequestRestart() {
#if defined(_WIN32)
  wchar_t exe[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    REXLOG_ERROR("restart: GetModuleFileName failed; quitting instead.");
    RequestQuit();
    return;
  }
  std::filesystem::path exe_path(exe);

  // Persist the changed cvars (resolution, render scale, ...) so the relaunched
  // instance loads them from <exe>/crackdown2.toml at startup.
  try {
    rex::cvar::SaveConfig(exe_path.parent_path() / "crackdown2.toml");
    REXLOG_INFO("restart: saved display settings to crackdown2.toml");
  } catch (const std::exception& e) {
    REXLOG_ERROR("restart: SaveConfig failed: {}", e.what());
  }

  // Relaunch only after this process has fully exited, so the exclusively-locked
  // shader/pipeline cache is released first. A tiny cmd stub polls for our PID to
  // disappear, then re-runs us with the exact same command line.
  //
  // If we were started inside a kill-on-job-close job object (e.g. from a
  // scheduled task), a detached child is killed when the job is torn down as we
  // exit, so the relaunch never happens - and such jobs usually forbid
  // CREATE_BREAKAWAY_FROM_JOB too. In that case, hand the relaunch to the Task
  // Scheduler service instead: it runs the stub outside our job. Otherwise (the
  // common double-click / launcher-script case) spawn the stub directly, which is
  // self-contained and leaves no task behind.
  const DWORD pid = GetCurrentProcessId();
  const std::wstring game_cmd = GetCommandLineW();
  constexpr wchar_t kTaskName[] = L"cd2_apply_restart";

  BOOL in_job = FALSE;
  IsProcessInJob(GetCurrentProcess(), nullptr, &in_job);
  bool via_task = in_job != FALSE;

  wchar_t tmp[MAX_PATH];
  if (GetTempPathW(MAX_PATH, tmp) == 0) {
    REXLOG_ERROR("restart: GetTempPath failed; quitting instead.");
    RequestQuit();
    return;
  }
  std::filesystem::path stub = std::filesystem::path(tmp) / "crackdown2_restart.cmd";
  std::filesystem::path vbs = std::filesystem::path(tmp) / "crackdown2_restart.vbs";
  {
    std::wofstream f(stub);
    f << L"@echo off\r\n";
    f << L":w\r\n";
    f << L"tasklist /fi \"PID eq " << pid << L"\" 2>nul | find \"" << pid
      << L"\" >nul && (ping -n 2 127.0.0.1 >nul & goto w)\r\n";
    if (via_task) {
      // Run the game synchronously (no `start`) so the hosting task stays alive
      // (and its job open) for as long as the game runs, then clean up on exit.
      f << game_cmd << L"\r\n";
      f << L"schtasks /delete /tn " << kTaskName << L" /f >nul 2>&1\r\n";
      f << L"del \"" << vbs.wstring() << L"\"\r\n";
    } else {
      f << L"start \"\" " << game_cmd << L"\r\n";
    }
    f << L"del \"%~f0\"\r\n";
  }
  if (via_task) {
    // The task runs this .vbs via wscript (a windowless host), which runs the cmd
    // stub with a hidden window and waits for it - so no console flashes on screen
    // while the game is restarting or running. ("" doubles to one quote in VBScript.)
    std::wofstream v(vbs);
    v << L"CreateObject(\"WScript.Shell\").Run \"cmd /c \"\"" << stub.wstring()
      << L"\"\"\", 0, True\r\n";
  }

  auto run_sync = [](const std::wstring& command) -> bool {
    std::vector<wchar_t> buf(command.begin(), command.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
      return false;
    }
    WaitForSingleObject(pi.hProcess, 10000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
  };

  auto spawn_direct = [&]() -> bool {
    std::wstring cmdline = L"cmd.exe /c \"" + stub.wstring() + L"\"";
    const DWORD base_flags = CREATE_NO_WINDOW | DETACHED_PROCESS;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    auto try_flags = [&](DWORD flags) -> bool {
      std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
      buf.push_back(L'\0');
      if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags, nullptr,
                          exe_path.parent_path().wstring().c_str(), &si, &pi)) {
        return false;
      }
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      return true;
    };
    return try_flags(base_flags | CREATE_BREAKAWAY_FROM_JOB) || try_flags(base_flags);
  };

  auto spawn_via_task = [&]() -> bool {
    // A one-shot task whose action runs the stub; /run triggers it now and the
    // Task Scheduler service hosts it outside our job. /st is a dummy time.
    std::wstring create = std::wstring(L"schtasks /create /f /tn ") + kTaskName +
                          L" /sc once /st 23:59 /tr \"wscript \\\"" + vbs.wstring() + L"\\\"\"";
    std::wstring run = std::wstring(L"schtasks /run /tn ") + kTaskName;
    return run_sync(create) && run_sync(run);
  };

  bool launched = via_task ? (spawn_via_task() || spawn_direct())
                           : (spawn_direct() || spawn_via_task());
  if (launched) {
    REXLOG_INFO("restart: relaunch scheduled ({}); quitting to apply settings.",
                via_task ? "task" : "direct");
  } else {
    REXLOG_ERROR("restart: could not start relaunch ({}); quitting without relaunch.",
                 static_cast<unsigned long>(GetLastError()));
  }
#else
  REXLOG_WARN("restart: self-restart is only implemented on Windows; quitting.");
#endif
  RequestQuit();
}

}  // namespace c2

// RWS_MainLoop_Poll runs once per frame on the game thread (it also drives
// UI_Update, so menu actions and console commands share a thread): drain the queue
// there so commands from the UI thread execute with a proper guest context.
extern "C" void RWS_MainLoop_Poll(PPCContext& ctx, uint8_t* base) {
  __imp__RWS_MainLoop_Poll(ctx, base);

  if (g_ticks == 0) REXLOG_INFO("console: main loop poll running (console {})", c2::ConsoleReady() ? "ready" : "not ready");
  if (!g_startup_done && ++g_ticks >= uint32_t(std::max(1, REXCVAR_GET(c2_console_startup_delay))) * 60u) {
    g_startup_done = true;
    c2::RunStartupCommands();
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
