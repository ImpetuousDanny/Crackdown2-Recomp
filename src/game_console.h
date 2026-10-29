// crackdown2 - bridge to the game's own developer console.
//
// Crackdown 2 ships its retail build with the Ruffian developer console intact:
// ~730 commands ("god", "infiniteammo", "TeleportPlayer x y z", "summon <proto>",
// "todsetphase", "setfps", "perfgraph", ...). docs/console_commands.md lists
// them. The console object lives at Core+2804 and commands are executed by
// Console_Execute(console, const wchar_t* line, flags) = sub_825CE628.
//
// Commands must run on a guest thread with a live PPCContext, so host code
// queues them and the override of RWS_MainLoop_Poll (the game's per-frame poll)
// drains the queue on the game thread.

#pragma once

#include <cstdint>
#include <string>

#include <rex/ppc.h>

namespace c2 {

// Run a console command right now on the calling guest thread.
bool ExecConsoleCommand(PPCContext& ctx, uint8_t* base, const std::string& command);

// Queue a command to run on the game thread at the next tick. Safe from any
// thread (ImGui overlays, key binds, ...).
void QueueConsoleCommand(const std::string& command);

// True once the game has created its console (after Core_Init).
bool ConsoleReady();

}  // namespace c2
