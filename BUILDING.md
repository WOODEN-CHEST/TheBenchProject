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

`lib/libwr.a`, `include/wr/` and `agents/WRFramework.md` match the commit above, `e6085fe`, file for file: the
version with `WRVectorMath` (`Vector2Double` and raymath's other vector operations included) and `WRArena`.

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
2. **`SUPPORT_GPU_SKINNING` must be enabled.** Skinned models bend on the GPU: the world renderer's skinned
   vertex stages read each vertex's bone indices and weights, which raylib uploads (vertex buffers 7 and 8) only when
   built with it, and every object's pose stays its own (a palette per draw), where raylib's CPU skinning would pose
   the shared model in place. Without it skinned models load, but their meshes draw as bound and never bend; the game
   logs that once per model (`...: its meshes have bone weights the GPU does not have (raylib was built without
   SUPPORT_GPU_SKINNING, see BUILDING.md); they draw as they are bound and do not bend.`). The same two ways as above:
   `#define SUPPORT_GPU_SKINNING 1` in `src/config.h` (what the current library was built with), or
   `-DCUSTOMIZE_BUILD=ON -DSUPPORT_GPU_SKINNING=ON` (raylib's CMake reads every option's default from `config.h`, so
   with `CUSTOMIZE_BUILD` an edited `config.h` still counts).
3. **Build type `Release`: pass `-DCMAKE_BUILD_TYPE=Release`.**
   raylib's CMake only picks a default build type (Debug) when `src/` is configured as the top-level
   project. Configured from the repository root (the normal way) it prints *"Default build type is not set
   (CMAKE_BUILD_TYPE)"* and builds with **no optimisation flags at all**. raylib's own Makefile defaults to a
   release build; the CMake path does not.
4. **Keep the other defaults.** In particular:
   - `SUPPORT_WINMM_HIGHRES_TIMER` (Windows): sets the 1 ms timer resolution the main loop's idle sleeps
     rely on.
   - The default image formats: png, bmp, gif, qoi and dds. jpg, tga, psd and hdr are off, and the asset
     loaders only accept enabled formats.
   - `MAX_MATERIAL_MAPS` 12 (defined in `src/rmodels.c`, not exported by `raylib.h`). The world renderer copies
     exactly that many maps into each per-draw material and keeps its own samplers on texture units 12–15,
     above raylib's material units: `WORLD_RENDERER_MATERIAL_MAP_COUNT` in `source/WorldRendererInternal.h`
     must match it.
   - glTF support (`SUPPORT_FILEFORMAT_GLTF`): the animation library reader (`source/AnimationLibraryGltf.c`) and the
     terrain's import (`source/TerrainMeshGltf.c`, both checked by `source/GltfChecks.c`) call the cgltf that raylib
     compiles into `libraylib.a`, through a copy of raylib's `src/external/cgltf.h` in
     `include/raylib/cgltf.h` (cgltf 1.15, MIT: `licenses/cgltf/`). Keep the two the same version: copy the header
     again whenever raylib is updated.

Linux commands (from the raylib source directory):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DPLATFORM=Desktop \
      -DGLFW_BUILD_X11=ON -DGLFW_BUILD_WAYLAND=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF \
      -DCUSTOMIZE_BUILD=ON -DSUPPORT_CUSTOM_FRAME_CONTROL=ON -DSUPPORT_GPU_SKINNING=ON   # or omit if config.h is edited
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
- **GPU skinning:** run the game with the test world: the log must not carry the
  "bone weights the GPU does not have" warning above, and `WorldRenderer_LogStatus` (the I key) lists `skinning` as
  active.

### Current state

`lib/libraylib.a` is a **Release** build (`-O3 -DNDEBUG`) of `../raylib/6.0` at commit `65abee1` with both `config.h`
edits (custom frame control and GPU skinning), built in that copy's existing `build/` directory
(`cmake --build build --target raylib`) and copied to `lib/`. It passes the checks above: custom frame control, and GPU
skinning (the test world's figures bend, no warning).

## Direct OpenGL calls

Everything draws through raylib, with three exceptions, each in a private module of its own and nowhere else:
- **texture arrays** (`GL_TEXTURE_2D_ARRAY`, `source/GLTextureArray.{h,c}`), which raylib does not have and the
  terrain's materials need (up to sixteen materials blended per pixel, one array layer each);
- **readbacks that do not wait** (`source/GLReadback.{h,c}`): raylib reads a texture back only by stalling until
  the GPU has finished everything before it; the eye's meter copies its target into a pixel-pack buffer, fences the
  copy and takes the values a frame or two later (`glGenBuffers`, `glDeleteBuffers`, `glBindBuffer`, `glBufferData`,
  `glGetBufferSubData`, `glReadPixels`, `glPixelStorei`, `glGetIntegerv`, `glFenceSync`, `glClientWaitSync`,
  `glDeleteSync`, `glGetError`: OpenGL 3.2 core at the newest, the sync objects; a missing one leaves the eye adapting
  to the sky's light at the camera, logged once);
- **reversed depth** (`source/GLDepth.{h,c}`): raylib's camera depth runs from 0 at the near plane to
  1 at its fixed 4 km far plane, in a depth buffer whose format the driver picks (24 bits here), so nothing past 4 km
  draws; the camera's passes instead draw with depth 1 at the near plane falling toward 0 at infinity, into 32-bit
  float depth buffers, which keeps about the same relative precision at every distance (`glClipControl` for the 0-to-1
  depth range, `glDepthFunc`, `glClearDepth`, `glGenTextures`, `glDeleteTextures`, `glBindTexture`, `glTexImage2D`,
  `glTexParameteri`, `glGenRenderbuffers`, `glDeleteRenderbuffers`, `glBindRenderbuffer`, `glRenderbufferStorage`,
  `glGetIntegerv`, `glGetStringi`, `glGetError`). Clip control is OpenGL 4.5 or `GL_ARB_clip_control` (most desktop
  GPUs since 2014; NVIDIA reports a 3.3 core context as 3.3 and has the extension, macOS has neither): the module
  checks the version, then the extension list, and without it the renderer keeps raylib's projection and its 4 km,
  logged once. The reversed state is switched on only around the camera's passes and switched back after them, so
  raylib's own state is what every other pass and the UI see.

The texture arrays' module, as the pattern all three follow:
- the function pointers (`glGenTextures`, `glDeleteTextures`, `glBindTexture`, `glTexImage3D`, `glTexSubImage3D`,
  `glGenerateMipmap`, `glTexParameteri`, `glTexParameterf`, `glPixelStorei`, `glGetIntegerv`, `glGetFloatv`,
  `glGetError`) come through raylib's `rlGetProcAddress`, the loader raylib itself was initialized with (GLFW's on the
  desktop), once, when the renderer loads its terrain shader: **raylib is not rebuilt** and no GL header or loader
  library is added;
- the GL constants it uses are defined in `GLTextureArray.c`; the function pointer types carry GL's calling convention
  (`__stdcall` on Windows, which matters for 32-bit builds);
- all of them are OpenGL 3.0 core, which raylib's GL 3.3 already requires; anisotropic filtering is used where the
  driver has the extension (`EXT_texture_filter_anisotropic`, core in 4.6);
- a failure degrades with one log line: a function the loader cannot find, or a GL error after making an array (a
  loader may hand out a stub for a missing function, so the error check is what catches it), leaves the terrain showing
  its materials' mean colours.

Anything else that needs GL raylib lacks belongs in one of these modules or one like them (function pointers through
`rlGetProcAddress`, constants defined there, one log line on failure), never in a render pass.

## Windows notes (for when the Windows build is set up)

None of this has been run on Windows yet.

- **raylib:** the same CMake options as above, with `-G "MinGW Makefiles"`.
- **libwr.a:** WRFramework's `compile_windows.bat`.
- **Non-ASCII paths:** raylib and GLFW open files and query the working directory with narrow (ANSI)
  Windows APIs, so a path like `C:\Users\Jānis\...` fails. Fix: embed an application manifest with
  `<activeCodePage>UTF-8</activeCodePage>` (Windows 10 1903+) via a `windres`-compiled resource.
- **Stack size:** Windows gives the main thread 1 MB by default (Linux: 8 MB). Link with
  `-Wl,--stack,8388608` so recursion limits match Linux.
