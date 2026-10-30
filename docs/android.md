# Android build (experimental)

Work in progress on the `feature/android` branch: Crackdown 2 Recomp as an
arm64-v8a APK, built with the Android NDK and hosted by SDL3's activity, using
the rexglue Vulkan backend. Research notes first, then the state of the port.

## What the port is made of

| Piece | Status |
| --- | --- |
| Recompiled game code (`generated/`, 256 translation units, ~270 MB of C++) | Portable. It uses the SDK's `simde`-based PowerPC vector helpers, which map SSE to NEON on arm64. One guest symbol (`__errno`) clashed with Bionic and is renamed `cd2___errno` in `default_functions.toml`. |
| rexglue SDK runtime (`librexruntime.so`) | Had Android hooks (platform macro, ashmem memory, Vulkan Android surface) but no build path. Patched: see *SDK changes*. |
| GPU plugin (`librexgpu-xenos.so`, Vulkan) | Builds for Android. The same Vulkan backend renders with a colour tint and missing UI text on Intel Linux, so correctness on mobile GPUs is unknown until tested. |
| Windowing, input, lifecycle | SDL3 for Android (Java `SDLActivity` + `libSDL3.so`); game `GameActivity` passes data paths as arguments. Touch controls do not exist yet: use a Bluetooth or USB gamepad. |
| Audio | SDL3 audio on Android (AAudio/OpenSL inside SDL). Untested. |
| Build | `tools/build_android.sh` runs CMake with the NDK toolchain (SDK in-tree via `REXSDK_DIR`), copies the `.so` files into `android/app/src/main/jniLibs/arm64-v8a` and runs Gradle. Codegen stays a host step: `generated/default` must exist from a PC build. |

## Prior art

* [NFSMW Android Evolved](https://github.com/codepdbh/nfsmw-android) and its
  [NFS Carbon sibling](https://github.com/codepdbh/NFS-CARBON-360-ANDROID-RECOMP)
  carry rexglue 0.10.0 to Android (GPL-3.0). Their notes were the map for which
  SDK pieces need Android branches (SDL main, Bionic fibers, libc++ gaps,
  logcat sink, app folder, plugin loading by soname, surface lifecycle). The
  code here is written against the same rexglue sources rather than copied.
  Their key finding: the emulated Xenos GPU path ran at 1-3 FPS on Switch, so
  they wrote a native renderer for their game. Crackdown 2 has no such
  renderer; this port stands or falls with the Xenos Vulkan backend on mobile.
* Xenia Android ports ([xeo](https://github.com/libaerto/xeo), successor of
  xenia-android) run the same GPU emulation approach on Adreno; Pixel 10's
  PowerVR driver has had Vulkan issues in emulators.

## Expectations

* CPU: the Windows build is bound by single-thread draw submission (~12k draws
  per frame in dense city). A Tensor G5 or Snapdragon 8-class core scores
  about as high per thread as the Ryzen 9 5900HS laptop used for the Windows
  figures, so the city dip would be similar if everything else were equal;
  thermals and the Vulkan backend's own cost decide the rest. Tutorial and
  menus should be fine.
* GPU: needs Vulkan 1.1+ with the features the Xenos backend asks for
  (fragment shader interlock is optional). Mali and PowerVR are untested.
* Memory: the runtime reserves the 4 GB guest address space; 64-bit Android
  has the virtual space, and ashmem/ASharedMemory backs the mapped views.
  A device with 8 GB RAM or more is advisable.

## SDK changes (patch 0003, Android)

* CMake: `android-arm64` platform, no host tools (codegen/CLI) when
  cross-compiling, 16 KiB page alignment, SDL3 shared, no X11/Wayland,
  `-fexperimental-library` + `libc++experimental` for `std::jthread`.
* `fiber_android.cpp`: AAPCS64 context switch (Bionic has no
  `makecontext`/`swapcontext`); guest fibers need it.
* libc++ gaps: float `std::from_chars` fallback, `clock_time_conversion`.
* `surface_android.cpp`: ANativeWindow surface from SDL; window reattaches the
  presenter after the activity returns from the background.
* Entry point: `SDL_main` through SDL's activity; the single registered app
  is picked (`WindowedApp::GetSoleCreator`).
* `GetExecutableFolder()` honours `REX_APP_FOLDER` (settings, logs, pipeline
  cache live in the app's files directory); GPU plugin loaded by soname;
  logcat sink (tag `rexglue`); `AndroidInitialize` for memory and threads.

## First runs (Pixel 10 Pro, 2026-10-30)

* The runtime starts, maps the guest address space, loads the XEX and the title
  update, creates a Vulkan swapchain on the PowerVR driver and runs guest code
  (file I/O, audio endpoint, shader pipelines) from `SDLActivity`.
* First blocker: a `brk` trap while loading the game database. The animation
  attribute loader's switch table has 518 entries but the recompiler capped
  jump tables at 512 (`max_jump_table_entries` is a codegen cvar that SDK
  0.10.0 does not read from the manifest; SDK patch 0005 fixes that, and
  `tools/build_android.sh` also passes it through `REX_MAX_JUMP_TABLE_ENTRIES`).
  Attribute ids 512-516 occur in the game data, so the Windows build carries
  the same hole and should be regenerated with the fix.
* Pointer tagging: the manifest sets `allowNativeHeapPointerTagging="false"`
  so host pointers keep a clean top byte (the runtime mixes host pointers and
  guest addresses in places); not yet shown to matter.
* Orientation: SDL re-applies its own orientation when the window is created,
  which turned the phone to portrait; `GameActivity` overrides
  `setOrientationBis` to force landscape.

## Building and installing

```
tools/build_android.sh            # configure + natives + APK
tools/build_android.sh install    # adb install
```

Game data goes to the app's external files directory (no permission needed):

```
adb push assets        /sdcard/Android/data/com.crackdown2.recomp/files/assets
adb push tu5_extracted /sdcard/Android/data/com.crackdown2.recomp/files/tu5
```

Extra command-line flags: one per line in `.../files/args.txt`. Logs:
`.../files/user/crackdown2.log` and `adb logcat -s rexglue Crackdown2 SDL`.
