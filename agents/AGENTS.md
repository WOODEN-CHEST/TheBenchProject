This is for AI agents:
The project uses the exact same development tech stack and project guidelines as the main library it uses.
The library is WRFramework. In the same directory as this file ("agents" directory under root directory)
exists WRFramework.md.
That file is a copy-paste of the AGENTS.md file from the WRFramework library.
You MUST read that file before doing anything further.

This project follows the same style guide and tech stack as that framework, so use that file as reference.
You **must** look at that library's functions when deciding to implement a feature to see if it already has
some library functions to help implement it, the library has been added here so you wouldn't need to duplicate
code for each feature.

If you require to write code which requires platform-specific code AND the library doesn't provide a cross-platform way
of doing that, stop what you're doing and let that be know. This project should be cross-platform to windows and linux,
and all the cross-platform code should be in the WRFramework library. So if you require such absent functions,
let it be known so they can be added to WRFramework. If you are told to continue anyway, make sure that the code
you write works for all targeted platforms (same platforms as WRFramework).

When writing code, keep it modular and clean, do not cram everything into main.c.
You are a senior developer.

Try to keep memory allocation count to a minimum by reusing buffers or passing reusable buffer pools
throughout the application, this is a game after all. Performance matters.

The references/ directory contains various reference files for various things. It contains:
* ghdf.txt - The full specification of the binary file format GHDF.
* unicode_data.txt The unicode data loaded into the game at runtime.
* asset_structure.md - A file which describes the JSON asset structure that assets must have.
* text_component_structure.md - The JSON structure of text components.

BUILDING.md (repository root) records the exact raylib and WRFramework versions and the required raylib
configuration: raylib is built with SUPPORT_CUSTOM_FRAME_CONTROL, so EndDrawing neither swaps buffers nor polls
input (the game does both itself). Read it before touching frame timing, input polling or buffer swapping.

For storing BINARY game data, use the GHDF module, for human-readable game data, JSON module.

When you implement a new module, create an md file in the agents directory describing the module and
its usage, then in this file, in the modules list, add the module, the md file reference and a short
description of what the module is for.

Current module list:
* Config (Config.md) - Loads the game's JSON config file (window/render settings) into a plain, expandable GameConfig struct.
* Logger (Logger.md) - Writes timestamped, level-tagged messages to <base>/logs/latest.log and stdout, and rotates the previous log into <base>/logs/archived/ (keeping the newest 30).
* AssetManager (AssetManager.md) - Loads, shares and unloads assets built from JSON definitions in prioritized search
  roots; hands out borrowed pointers held per AssetUserID (presence-based, unloaded at once on the last release),
  caches failed loads, and registers the six standard types. Read its lifetime rules before touching assets.
* Game asset wrappers (GameAssets.md) - GameFont, GameModel, GameShader and GameSound: the manager-owned wrappers for
  the standard font, model, shader and sound types, with their ownership rules, accessors and pitfalls.
* SpriteSheet (SpriteSheet.md) - Texture atlas with named pixel regions; the asset loader shelf-packs padded source
  images into one texture (padding at most 64 px, atlas at most 8192 px).
* SpriteAnimation (SpriteAnimation.md) - 2D frame animations: SpriteAnimation (shared asset data: frames plus default
  playback) and SpriteAnimationInstance (per-consumer playback state, Update, end/loop events; borrows its source and
  must never be moved or copied by value).
* Renderer (Renderer.md) - RenderContext, the low-level aspect-fitted 2D drawing device (pixel/relative/fitted units,
  RenderColor, textures, text, primitives, model/mesh passthrough) used by the UI and text renderers; not the world
  renderer.
* ProgramTime (ProgramTime.md) - Tiny value type carrying a tick's total time and passed (delta) time, in seconds.
* GameFrame (GameFrame.md) - Abstract base for a game "scene"/section (menu, level, loading screen), with stepped load/unload, logic lifecycle, and per-target rendering.
* GameFrameManager (GameFrameManager.md) - Owns a z-stack of game frames, drives their lifecycle and update loop, and composites them to the screen; crashes gracefully on frame errors.
* World data layer (WorldData.md) - The rendering-independent 3D world model. Covers eight modules:
  * WorldObject - Abstract base of every world object (id, name, transform, tint; validating setters).
  * WorldModelObject - 3D model object referencing a model asset by name (+ outline / pixelation toggles).
  * WorldSpriteObject - 2D sprite object referencing a sprite animation by name (+ outline / pixelation toggles).
  * WorldLight - Point light (color, intensity, size, casts-shadows toggle).
  * WorldEnvironment - Per-world sky/sun/fog/effect settings (plain, expandable data).
  * World - Owns the objects, mints the shared uint64 id counter, holds the environment.
  * WorldDTO - Flat persistent snapshot of a world + World<->DTO conversions.
  * WorldEncoder - Encodes a WorldDTO into a GHDF compound tree (no binary write yet).
* World runtime layer (WorldRuntime.md) - Camera, renderer, services bundle and bootstrap for viewing a world:
  * GameCamera - 3D camera (pos/fov/yaw/pitch/roll; +Y up, +Z forward), converted to a raylib camera at render time.
  * Services - Bundle of borrowed shared services (logger, config, unicode, asset manager, frame manager, GHDF pool).
  * WorldRenderer - Renders a World's model objects through a GameCamera; separate from the world data.
  * WorldTestFrame - Concrete GameFrame: WASD + mouse-look flycam over a world with the test model centred.
* World light culling (WorldLightCulling.md) - Reach-culls a world's point lights to the strongest few (up to WORLD_MAX_FORWARD_LIGHTS) overlapping an object's bounding sphere, ranked by influence, for the renderer's per-object forward shading.
* UI framework (UIFramework.md) - The in-game UI. A screen context object owns a widget factory (pooled, capability-registered widget types), routes input to the top-most/focused widget, drives keyboard navigation, hosts keyframed property animations, and renders a z-ordered tree of abstract widgets through per-widget local render contexts. Covers six modules:
  * UIInput - Raylib-free input value types (UIKey/UIMouseButton enums, hover/mouse/keyboard argument structs) shared by widgets and the screen.
  * UIAnimation - Keyframed interpolation of a typed property: easing methods (constant/linear/exp in-out pow 2-5/sine/custom), keyframes, and evaluation. Pure data + math.
  * UIRenderContext - The widget-local drawing device: wraps the renderer's RenderContext with the widget's absolute box + accumulated tint, and maps widget-local [0;1] draws onto the screen.
  * UIWidgetFactory - Registers capabilities and widget types (struct size + constructor + capability resolvers), owns per-type object pools, and constructs/recycles widget instances.
  * UIWidget - The abstract widget base (vtable, lifecycle, geometry/flags/state, subwidgets, capabilities, state-change events, generic property get/set); concrete widgets embed it first.
  * UIScreen - The context: widget registry, roots, focus/z-order, input routing, keyboard navigation + outlines, animation host, and the reused per-tick update snapshot. The widget factory is program-wide (one shared across all screens), passed into each screen rather than owned by it.
  * LabelWidget - The first concrete widget: displays a (possibly multi-line) TextComponent with per-line alignment and bound handling (cut/wrap/resize/wrap-then-resize/wrap-then-cut). Auto-fits its widget box to the rendered text. Built on the text component system (TextComponents.md); adds an optional scissor to TextComponentRenderer for cutting.
  Also adds 2D rectangle/outline/line primitives to the Renderer module.
* Text components (TextComponents.md) - Renderer-independent, Minecraft-style rich text. Abstract mutable
  TextComponent tree (String/Sprite/Empty types) with ordered subcomponents and cycle-checked composition;
  a pooling TextComponentFactory that is the sole creator/cloner/returner (plus number/codepoint/special-char
  helpers over caller-owned buffers); a presence-guarded TextStyle applied onto components; and a
  TextComponentRenderer that measures and draws component trees inline (bottom-aligned lines, shadows,
  underline/strikethrough, sprites) through a RenderContext. Covers four modules:
  * TextComponent - Abstract base + StringComponent/SpriteComponent/EmptyComponent + subcomponent management.
    String/sprite components also carry a borrowed asset reference NAME (_fontName / _animationName) for
    serialization and binding.
  * TextComponentFactory - Pooled create/clone/return; number/codepoint/space/tab/newline shorthands.
  * TextStyle - Optional, presence-guarded style properties applied to a component by type.
  * TextComponentRenderer - Inline layout, measuring and drawing of a component tree (normalized-fitted units).
* GameJSON (GameJSON.md) - Shared parsers for the game's common JSON value shapes (numbers, booleans,
  strings, colors, render colors, vectors); the asset-definition helpers and the text component JSON parser
  both use it so the value conventions live in one place. Raises ErrorCode_InvalidJSON on wrong-shape values.
* Text component serialization (TextComponentSerialization.md) - Serializes text component trees to/from the
  game's three formats and binds their asset references. JSON is the human-facing form
  (references/text_component_structure.md); GHDF is compact binary storage; plain text is a lossy strings-only
  extraction. Structured forms convert to/from the format's STRUCT tree (JSONCompound/GHDFCompound), not
  bytes. Serializers take all working memory from the caller (object pools, factory, a WRBufferPool for
  stable deserialized strings). Covers four modules:
  * TextComponentText - Component -> plain UTF-8 string (no deserializer).
  * TextComponentJSON - Component <-> WRJSON value tree.
  * TextComponentGHDF - Component <-> GHDF compound tree.
  * TextComponentResolver - Binds a parsed tree's font/animation NAMES to live GameFont/SpriteAnimationInstance
    handles via the AssetManager (call after parsing; instances owned by a caller ObjectPool).
* SoundEngine (SoundEngine.md) - Custom audio mixer (track tree, effects, scheduled commands) on raylib's audio
  stream. DISABLED: AudioEngine_Construct1 always fails until its threading redesign; SoundEngine.md lists the
  defects and is the brief for that redesign (ask the owner its open decisions before starting).

Logging:
Use the Logger module for all program output. Do NOT print to the standard streams directly (no
printf / fprintf(stdout|stderr, ...) or other direct stdout/stderr writes) in game code. Route
messages through a Logger (passed by pointer) at the appropriate level instead. The only allowed
exception is the bootstrap fallback in main when the logger itself fails to initialize and therefore
cannot be used.

Project rules (added after the 2026-10 code review; handoff/FIX_PLAN.md has the reasoning):
1. Asset lifetime: whatever keeps asset data owns its own AssetUserID, and nothing stores asset pointers or by-value
   copies (GameFont, Model, Texture2D, sprite animation sources) past that user. See AssetManager.md. Two known
   violations are scheduled to be fixed: world sprites bound under the renderer's user, and text components copying
   fonts by value.
2. Never hold a Widget* or text component pointer across a call that can run hooks, events or user callbacks;
   re-resolve it by id afterwards. Never destroy a widget from inside its own event (until the UI's deferred
   destruction exists).
3. Render reads state, it never writes it: no mutating world objects, widgets or shared assets from render code.
   Per-object render data belongs in a renderer-side cache.
4. Switches over project enums that should handle every value have no `default:`. -Wswitch (on through -Wall, an
   error under -Werror) then fails the build wherever a newly added value is not handled. Handle corrupted values
   after the switch with an explicit error or neutral return, not unreachable().
5. Every recursive walk over data that came from a file or the network has a depth limit.
6. Never log per tick from the update loop, and never log from a non-main thread (the Logger is not thread-safe).
7. A frame returns an error only when an invariant is broken: that error shuts the game down. A recoverable failure
   (missing asset, bad keyframe) changes state so it is not retried every tick (mark it failed, stop the animation,
   fall back) and is logged once, when it happens.
8. Mark every intentionally incomplete piece with `// TODO:`.

Testing and tooling:
* Unlike what WRFramework.md says, this project has a test step. `sh tests/run_linux.sh` builds and runs the headless
  tests in tests/ (add `--sanitize` for AddressSanitizer + UndefinedBehaviorSanitizer). Add or extend tests for any
  logic that does not need a window or GL context, and run them before finishing a feature. tests/TestFramework.h
  explains how to write a suite. Tests are tooling, not game code, so they may print to stdout.
* `compile/compile_linux_debug.sh` then `compile/run_linux_x11_debug.sh` build and run the game with the same
  sanitizers. Use it after changes to lifetime-sensitive code (assets, UI, text components, audio).
* The build uses `-Wconversion -Wshadow` on top of `-Wall -Wextra -Wpedantic -Werror`; keep the code clean under them.
* Game files (config, assets, logs, the asset cache) are resolved against the executable's directory, never the
  working directory: use `Services.BaseDirectory`.

If you cannot compile because a WRFramework or other library header file exists, but there is no such defined function
in the library file, then this is an error on my part and should be flagged.
The headers are correct, the function missing from the library is just a version mismatch on my end.

You should not edit the WRFramework.md file, it is regularly pulled from the actual framework repository when
changes are made to it, so making changes to this file is useless as they will be overwritten. If more info
is needed, make changes to either a module file or this file.

**IMPORTANT: I try to architecture the project myself as much as I can, but the reality is that using Claude to
write all code leads to me not fully understanding the codebase. If when I request a feature you see a roadblock or
potential required refactor, please let me know before proceeding and say what is needed, you can even ask questions so you
can plan out what the refactor should be (but don't instantly perform it, I need to know first and decide myself).
The codebase should be kept clean, so try to avoid band-aid patches. Refactors are fine and expected, just don't pile on already
broken code.**


This is pretty much all that I can write here, read WRFramework.md for the rest.