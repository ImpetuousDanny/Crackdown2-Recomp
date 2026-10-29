# rexglue SDK patches

Optional performance patches against rexglue SDK v0.10.0. The prebuilt SDK works without them; apply them only if you
build the SDK from source (`git clone --recursive --branch v0.10.0 https://github.com/rexglue/rexglue-sdk.git`, then
`git apply <this repo>/patches/rexglue-sdk/*.patch` inside the clone and build with its `win-amd64` / `linux-amd64`
preset). Drop the resulting `rexgpu-xenos*.dll`/`.so` **and** the matching `rexruntime*` library beside the game
executable together: the runtime and the GPU plugin from different builds are not ABI compatible.

| Patch | What it does | Measured effect (NUC, Radeon 780M, tutorial room) |
| --- | --- | --- |
| 0001 | `ExecutePacketType0` writes register ranges through `WriteRegisterRangeFromRing` (one `copy_and_swap` per shader-constant block instead of one virtual call per dword, as upstream Xenia does); caches the `RegisterFile::GetRegisterInfo` lookup; adds `gpu_invalidation_widen_kb` (CPU write-watch invalidation window, default unchanged at 256 KB) and `gpu_log_shared_memory_stats` (logs upload and invalidation rates every 5 s). | Type-0 register handling drops from 29% to 11% of the "GPU Commands" thread. The widening cvar made no measurable difference for this game; it is there for diagnosis. |

The big win needs no patch: pass `--clear_memory_page_state=false` (see the README's Performance section).
