// crackdown2 - optional profiling aids.
//
// The rexglue SDK compiles Tracy zones and a per-frame perf-counter registry
// into every non-Release build (REXGLUE_ENABLE_PROFILING /
// REXGLUE_ENABLE_PERF_COUNTERS come from the SDK's CMake interface), but the
// runtime never starts the Tracy listener and never opens the counter CSV.
// These two cvars expose both so a RelWithDebInfo build can be profiled with
// stock Tracy tools (tracy-capture / Tracy.exe on port 8086) and frame times
// can be logged without screenshots. Both are no-ops in Release builds.

#include "profiling.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>

#include <string>

REXCVAR_DEFINE_BOOL(c2_profile, false, "Debug",
                    "Start the SDK's built-in Tracy profiler listener (TCP 8086) so tracy-capture or the "
                    "Tracy UI can attach. Debug/RelWithDebInfo builds only.");
REXCVAR_DEFINE_STRING(c2_perf_csv, "", "Debug",
                      "Write the SDK's per-frame perf counters (frame time, draw calls, command buffer "
                      "stalls, texture/pipeline cache hits) to this CSV file. Debug/RelWithDebInfo builds only.");

namespace c2 {

void StartProfilingIfRequested() {
  if (REXCVAR_GET(c2_profile)) {
#ifdef REXGLUE_ENABLE_PROFILING
    rex::perf::Profiler::Startup();
    REXLOG_INFO("profiling: Tracy listener started (--c2_profile)");
#else
    REXLOG_WARN("profiling: --c2_profile has no effect in a Release build");
#endif
  }
  const std::string csv = REXCVAR_GET(c2_perf_csv);
  if (!csv.empty()) {
#ifdef REXGLUE_ENABLE_PERF_COUNTERS
    rex::perf::SetCsvLogPath(csv);
    REXLOG_INFO("profiling: per-frame perf counters -> {}", csv);
#else
    REXLOG_WARN("profiling: --c2_perf_csv has no effect in a Release build");
#endif
  }
}

}  // namespace c2
