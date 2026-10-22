# Building TheBenchProject

The game links two prebuilt static libraries from `lib/`, which is gitignored: `libraylib.a` and `libwr.a`.
This file records exactly where they come from and how to rebuild them, because the game depends on a
non-default raylib configuration.

## Toolchain

- **GCC 13 or newer** (C23). Older GCCs type `u8"..."` literals as `char[]`, not `unsigned char[]`, which
  breaks the `-Werror` build at every `u8` string passed as `const unsigned char*`.
- Linux build: `cd compile && sh compile_linux_x11.sh` (it writes `compile/out/TheBenchProject`). Run the
  game with `sh run_linux_x11.sh`, also from `compile/`. The game finds `config.json`, `asset/`, `logs/` and
  its temp-file cache next to the executable (`compile/out`), whatever the working directory is.
- Debug build with AddressSanitizer + UndefinedBehaviorSanitizer: `sh compile_linux_debug.sh`, then
  `sh run_linux_x11_debug.sh` (both from `compile/`). `lsan_suppressions.txt` hides leaks inside GL/X11 system
  libraries only.
- Tests: `sh tests/run_linux.sh` from anywhere (`--sanitize` adds the sanitizers). Headless; see
  `tests/TestFramework.h`.
- Windows build (`compile_windows.bat`, MinGW-w64): not set up or tested yet. See the Windows notes below.

## Library versions

| Library | Source | Version | Build |
|---|---|---|---|
| raylib | https://github.com/raysan5/raylib (local copy: `../raylib/6.0`) | commit `65abee1` (2026-07-04), reports `6.1-dev` | CMake, see below |
| WRFramework | `../WRFramework` | commit `e6085fead5630602416729efe00dc4e60d60fa54` | its `compile/compile_linux_x11.sh` → `compile/libwr.a` |

`lib/libwr.a`, `include/wr/` and `agents/WRFramework.md` match the commit above, `c341ca5` (T31's `WRVectorMath`, on
top of `8c9af0b`'s `WRArena`), plus `Vector2Double` in the same module, not yet committed in WRFramework (below). Once
the owner commits it there, this table names that commit.

The project's `include/raylib/` and `include/wr/` headers must come from the same versions as the libraries.
To update WRFramework: build it with its own script, then copy its `include/*.h` into `include/wr/`,
`compile/libwr.a` into `lib/`, and its `AGENTS.md` over `agents/WRFramework.md`.

## raylib: required configuration

1. **`SUPPORT_CUSTOM_FRAME_CONTROL` must be enabled.**
   The game polls input once per update tick (`GameFrameManager_Update`, which then takes the tick's `GameInput`
   snapshot) and swaps buffers itself after `EndDrawing` (`GameFrameManager_Render`). A stock raylib's `EndDrawing` also swaps and polls, which gives
   double swaps (flicker, roughly half the frame rate with vsync) and double polls (key presses and mouse
   movement arriving during the second poll are lost). Either:
   - edit `src/config.h`: `#define SUPPORT_CUSTOM_FRAME_CONTROL 1` (what the current library was built
     with), or
   - leave `config.h` untouched and configure CMake with
     `-DCUSTOMIZE_BUILD=ON -DSUPPORT_CUSTOM_FRAME_CONTROL=ON`. `CUSTOMIZE_BUILD` turns every `config.h` flag
     into a CMake option that defaults to its `config.h` value, so nothing else changes.
2. **Build type `Release`: pass `-DCMAKE_BUILD_TYPE=Release`.**
   raylib's CMake only picks a default build type (Debug) when `src/` is configured as the top-level
   project. Configured from the repository root (the normal way) it prints *"Default build type is not set
   (CMAKE_BUILD_TYPE)"* and builds with **no optimisation flags at all**. raylib's own Makefile defaults to a
   release build; the CMake path does not.
3. **Keep the other defaults.** In particular:
   - `SUPPORT_WINMM_HIGHRES_TIMER` (Windows): sets the 1 ms timer resolution the main loop's idle sleeps
     rely on.
   - The default image formats: png, bmp, gif, qoi and dds. jpg, tga, psd and hdr are off, and the asset
     loaders only accept enabled formats.
   - `MAX_MATERIAL_MAPS` 12 (defined in `src/rmodels.c`, not exported by `raylib.h`). The world renderer copies
     exactly that many maps into each per-draw material and keeps its own samplers on texture units 12–15,
     above raylib's material units: `WORLD_RENDERER_MATERIAL_MAP_COUNT` in `source/WorldRendererInternal.h`
     must match it.

Linux commands (from the raylib source directory):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DPLATFORM=Desktop \
      -DGLFW_BUILD_X11=ON -DGLFW_BUILD_WAYLAND=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF \
      -DCUSTOMIZE_BUILD=ON -DSUPPORT_CUSTOM_FRAME_CONTROL=ON   # or omit this line if config.h is edited
cmake --build build -j
cp build/raylib/libraylib.a /path/to/TheBenchProject/lib/
```

Reusing an existing build directory works too: re-run the `cmake -B build ...` line with the new options
(they update the cache), then `cmake --build build`.

### Checking a libraylib.a

- **Custom frame control:** this must print nothing (a stock build's `EndDrawing` calls both functions):

  ```sh
  objdump -dr lib/libraylib.a | awk '/<EndDrawing>:/{f=1} f{print} f&&/^$/{exit}' \
      | grep -E "SwapScreenBuffer|PollInputEvents"
  ```

- **Optimisation:** check the build directory, not the archive: `grep CMAKE_BUILD_TYPE build/CMakeCache.txt`
  should say `Release`, and `build/raylib/CMakeFiles/raylib.dir/flags.make` should contain `-O3`.

### Current state

`lib/libraylib.a` was rebuilt on 2026-10-17 as **Release** (`-O3 -DNDEBUG`) from `../raylib/6.0` (commit
`65abee1` with the `config.h` edit), reusing its existing `build/` directory: `cmake -B build
-DCMAKE_BUILD_TYPE=Release`, then `cmake --build build --target raylib`. The check above confirms custom frame
control. Until then it had been built with an empty build type, i.e. unoptimised.

## Windows notes (for when the Windows build is set up)

None of this has been run on Windows yet.

- **raylib:** the same CMake options as above, with `-G "MinGW Makefiles"`.
- **libwr.a:** WRFramework's `compile_windows.bat`.
- **Non-ASCII paths:** raylib and GLFW open files and query the working directory with narrow (ANSI)
  Windows APIs, so a path like `C:\Users\Jānis\...` fails. Fix: embed an application manifest with
  `<activeCodePage>UTF-8</activeCodePage>` (Windows 10 1903+) via a `windres`-compiled resource.
- **Stack size:** Windows gives the main thread 1 MB by default (Linux: 8 MB). Link with
  `-Wl,--stack,8388608` so recursion limits match Linux.
