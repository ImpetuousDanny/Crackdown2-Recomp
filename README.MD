## Requirements:
- [Rexglue SDK 0.10.0](https://github.com/rexglue/rexglue-sdk)
- CMake
- Ninja
- Clang++/LLVM
- Crackdown 2 game disc
- Crackdown 2 Title Update 5

# Hashes
- Default.xex
 ```SHA256: 8959C449E382661D29B8EA479C685F506CD5BD3ECA40B3D73A51CC10F281069F```
- Crackdown 2 Title Update 5 xexp
 ```SHA256: A5E49FC1FF7F6F65E6F95A7B5C4B9392BB9E38674D1237AC0E2F0149D05929D7```

## To build:
 - Compile and install the rexglue SDK
 - Extract game disc and latest title update into the assets folder. (Including its xexp file)
 - Run the codegen by using the rexglue binary in the rexglue sdk install folder, with the jump table limit raised
   (rexglue ignores `max_jump_table_entries` in the manifest; the game has a 518-entry switch table in
   `Anim_cProfileDataHand2HandFull_HandleAttributeData` and the default limit of 512 truncates it, which traps on load).
   The CMake presets export `REX_MAX_JUMP_TABLE_ENTRIES=519`, and with SDK 0.10.0 codegen runs as part of the build, so
   building through a preset is enough. To run it by hand:
   ```rexglue codegen crackdown2_manifest.toml --max_jump_table_entries=519``` (0.10.0; on 0.9.0 the CLI rejects the
   flag, use the environment variable instead).
 - Windows: ```cmake --preset win-amd64-relwithdebinfo; cmake --build --preset win-amd64-relwithdebinfo``` in the main folder
   (see the Windows section below for prerequisites and the run script)
 - Linux (clang 20 and the SDK install prefix on hand):
   ```cmake --preset linux-amd64-relwithdebinfo -DCMAKE_PREFIX_PATH=<sdk-prefix> -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld```
   then ```cmake --build --preset linux-amd64-relwithdebinfo```
 - Run with the GPU plugin enabled (it is off by default) and the extracted title update mounted as `update:`:
   ```crackdown2 --game_data_root=<assets> --update_data_root=<extracted TU folder> --gpu_plugin=xenos```
   On Linux put the SDK `lib` folder on `LD_LIBRARY_PATH`; the xenos plugin is staged next to the executable by CMake.

 # Status: In-game but not playable yet.

## Windows build

Prerequisites (all machines, including laptops/iGPU boxes):
 - [Rexglue SDK 0.10.0](https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.0) `rexglue-sdk-0.10.0-win-amd64.zip`, extracted anywhere (its `bin\rexglue.exe` and `lib\cmake` are what the build needs).
 - LLVM/Clang 20 or newer for Windows (official installer; `clang`/`clang++` on PATH), CMake 3.25+, Ninja, and Visual Studio 2022 Build Tools with the Windows 10/11 SDK (for the system headers and `link.exe`).
 - Python 3 for the extraction scripts in `tools/` (they are pure Python and work the same on Windows).

Steps (PowerShell, from the repo folder):
 1. Extract the disc and title update into `assets\` as described above, e.g.
    ```python tools\xdvdfs_extract.py <game.iso> assets``` and ```python tools\stfs_extract.py <TU5 file> assets```.
 2. Configure, pointing CMake at the SDK:
    ```cmake --preset win-amd64-relwithdebinfo -DCMAKE_PREFIX_PATH=<sdk-folder>```
 3. Build (codegen runs as the first build step; the preset already exports `REX_MAX_JUMP_TABLE_ENTRIES=519`):
    ```cmake --build --preset win-amd64-relwithdebinfo```
 4. Run: ```.\tools\run_cd2.ps1``` (add `-Update <extracted TU folder>` to mount it as `update:`; `-Backend vulkan` or `-Backend d3d12`
    picks the GPU backend, the default lets the plugin choose D3D12 on Windows). The xenos GPU plugin DLL is staged next to the
    executable by CMake, and so is the SDK's `rexruntime` DLL (`rexruntimerd.dll` for RelWithDebInfo).

Verified 2026-10-28 on Windows 11 24H2 (Ryzen 9 7940HS / Radeon 780M) with LLVM 20.1.8, CMake 4.4.5, Ninja 1.13.2 and
VS 2022 Build Tools 17.x (MSVC 14.44, Windows SDK 10.0.26100): run the configure/build from a `vcvars64.bat` prompt so clang
finds the MSVC headers; a clean build takes about five minutes. The game must be started from an interactive desktop session:
launched from a remote shell (WinRM/SSH, session 0) it logs `D3D12Presenter: Failed to create a swap chain for the HWND`.

## Status (2026-10-28)

| Platform / backend | Result |
| --- | --- |
| Windows 11, Radeon 780M, D3D12 (`--c2_gpu_backend=d3d12`, the Windows default) | Playable: title, menus, Agent Setup and co-op lobby at 63-80 FPS with `--vsync=false` (the 30 FPS seen before was the vsync cap); the intro cutscene at ~68 FPS; the opening Agency tutorial room with full HUD at ~35 FPS. Keyboard/mouse via `--mnk_mode --mnk_mouse=true` (Enter = Start, Space = A, Tab = Back, WASD / arrows = sticks). |
| Linux (Mint 22.3), Intel Iris Xe, Vulkan (Mesa ANV) | Boots to the Agency screen and the real-time intro at ~28 FPS, but all UI text and solid fills are missing and 3D scenes come out as single-channel tints (pure red / pure blue). |

The Windows run proves the recompiled CPU side (same `generated/` code on both platforms) is fine; the Linux defects are in the
GPU plugin's Vulkan path or the Intel driver. The SDK's prebuilt Windows plugin only contains the D3D12 backend, so the Vulkan
backend cannot be cross-checked on Windows without building the SDK from source. Debug aids in this repo: `--c2_gfx_draw_log`
(per-60-frame Scaleform draw counters, texture updates) and `--c2_force_texture_invalidate` (forces GPU texture-cache
invalidation on GFx texture updates; had no effect, so the missing text is not a stale glyph atlas).

## Menus and settings

* **F4** opens the SDK's settings overlay (every cvar by category); **F3** the debug overlay.
* **F6** toggles mouse look. With `--mnk_mouse=true` the mouse drives the right stick from startup; F6 releases the
  cursor for the overlays and grabs it again.
* **F5** opens this project's ImGui Graphics menu (`src/graphics_menu.h`): v-sync, fullscreen, FPS counter, 2x MSAA,
  upscaling filter, resolution, internal render scale, GPU backend.
* **Native rows in the game's own Options > Display screen** (`src/native_menu.cpp`): V-SYNC, FULLSCREEN, FPS COUNTER,
  UPSCALING, 2X MSAA and INTERNAL RESOLUTION are appended after HUD OPACITY and GAMMA, drawn by the game's Scaleform menu
  with its font, cursor, tooltips and sounds. Press A on a row to cycle its value; it writes the same cvars as the F5 menu.
  The row framework is documented in `docs/native_menu.md` (component ids, vtable slots, item/text object layout, the
  bctrl call sites used by the midasm hook). No game assets are modified.

## Stability notes

* The runtime's `InputSystem::RefreshDevices()` rebuilds its device list without locking on every XInput call, and the
  game polls input from several guest threads; a Windows run died during a city load inside that vector's destructor
  (STATUS_ILLEGAL_INSTRUCTION in rexruntimerd.dll). `src/hooks.cpp` now binds the game's `XamInputGetState` /
  `GetCapabilities` / `SetState` / `GetKeystrokeEx` calls to wrappers that serialize them through one mutex before
  forwarding to the runtime's originals.
