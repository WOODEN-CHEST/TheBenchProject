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
* controls_structure.md - The JSON structure of the player's controls file (action bindings).

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
  caches failed loads, and registers the seven standard types. Read its lifetime rules before touching assets.
* Game asset wrappers (GameAssets.md) - GameFont, GameModel, GameShader, GameSound and GameTexture: the manager-owned
  wrappers for the standard font, model, shader, sound and texture types, with their ownership rules, accessors and
  pitfalls. GameFont (source/GameFont.c) adds sorted glyph lookup with the '?' fallback; fonts load at a
  definition-chosen size and codepoint set (source/FontCodepoints.c, private) and warn once about glyphs their file
  lacks. Shader stages may `#include "common/..."` shared GLSL from shaders/common/, expanded with #line directives so
  compiler errors name the right file (source/ShaderSource.c, private). GameTexture is a standalone texture (sky
  panoramas, the moon, noise, masks) with filtering, wrap, optional mipmaps and an sRGB/linear flag.
* SpriteSheet (SpriteSheet.md) - Texture atlas with named pixel regions; the asset loader shelf-packs padded source
  images into one texture (padding at most 64 px, atlas at most 8192 px).
* SpriteAnimation (SpriteAnimation.md) - 2D frame animations: SpriteAnimation (shared asset data: frames plus default
  playback) and SpriteAnimationInstance (per-consumer playback state, Update, end/loop events; borrows its source and
  must never be moved or copied by value).
* Renderer (Renderer.md) - RenderContext, the low-level aspect-fitted 2D drawing device (pixel/relative/fitted units,
  RenderColor, textures, text, primitives, model/mesh passthrough) used by the UI and text renderers; not the world
  renderer.
* ProgramTime (ProgramTime.md) - Tiny value type carrying a tick's total time and passed (delta) time, in seconds.
* Input (GameInput.md) - How the game reads input. Covers two modules:
  * GameKey - The game-wide key identity (keyboard keys and mouse buttons, room for gamepad buttons) with stable id
    strings for binding files and display names; the raylib codes stay private to the input layer.
  * GameInput - The per-tick input snapshot (keys, mouse, wheel, typed text drained once), polled by the frame manager
    right after PollInputEvents, with consumption (a consumed key reads as not pressed while held) and the action layer
    (GameAction: named actions bound to keys with modifiers; JSON controls file load/save).
* GameFrame (GameFrame.md) - Abstract base for a game "scene"/section (menu, level, loading screen), with stepped load/unload, logic lifecycle, and per-target rendering.
* GameFrameManager (GameFrameManager.md) - Owns a z-stack of game frames, drives their lifecycle and update loop (polls the input, then updates frames top-down so the top frame gets input first), and composites them to the screen; crashes gracefully on frame errors.
* World data layer (WorldData.md) - The rendering-independent 3D world model. Covers eight modules:
  * WorldObject - Abstract base of every world object (id, name, transform, tint; validating setters).
  * WorldModelObject - 3D model object referencing a model asset by name (+ outline / pixelation / shadow-tier
    toggles; an asset revision that changes on rename).
  * WorldSpriteObject - 2D sprite object referencing a sprite animation by name, owning its playback instance
    (+ outline / pixelation toggles); the World binds its animation.
  * WorldLight - Point light (color, intensity, size, casts-shadows toggle).
  * WorldEnvironment - Per-world sky/sun/fog/effect settings, described by its property table (defaults,
    validation, copy, JSON and GHDF all come from it; see PropertyDescriptors.md).
  * World - Owns the objects, mints the shared uint64 id counter, holds the environment; with an asset manager
    (World_Construct2) it owns an asset user and binds its sprites' animations; an object-set version.
  * WorldDTO - Flat persistent snapshot of a world + World<->DTO conversions.
  * WorldEncoder - Encodes a WorldDTO into a GHDF compound tree (no binary write yet).
* World runtime layer (WorldRuntime.md) - Camera, renderer, services bundle and bootstrap for viewing a world:
  * GameCamera - 3D camera (pos/fov/yaw/pitch/roll; +Y up, +Z forward), converted to a raylib camera at render time.
  * Services - Bundle of borrowed shared services (logger, config, unicode, asset manager, frame manager, GHDF pool).
  * WorldRenderer - Renders a World through a GameCamera (PBR, HDR, sky with stars, Milky Way and light pollution,
    shadows, AO, outlines, bloom, sun shafts, crisp overlay); separate from the world data and never writes to it. Split by render pass into
    source/WorldRenderer*.c behind the private source/WorldRendererInternal.h; read "Renderer structure" in
    WorldRuntime.md before adding to it. WorldRenderer_GetStatus reports which features run.
  * WorldRenderCache - The renderer's private per-object cache (source/WorldRenderCache.h): one world walk per frame
    builds the model/sprite/light lists; model assets are resolved once per object and again only on rename.
  * NightSky - The night sky's pure maths (private, source/NightSky.h): the seeded star catalogue, the limiting
    magnitude (light pollution, twilight), the air mass, star colours and the galaxy's place among the stars; used
    by the sky and star passes (WorldRendererSky.c, WorldRendererStars.c).
  * WorldTestFrame - Concrete GameFrame: WASD + mouse-look flycam over a test world, with debug keys and a text sample
    (T toggles it) drawn with TextDocument + TextLayout over the world.
* PropertyDescriptor (PropertyDescriptors.md) - Property tables: one static table per data type describes each field
  once (key, GHDF wire id, type, default, rules, flags, editor text), and generic functions driven by it do defaults,
  validation (errors name the table, key and rule), deep copy, equality, JSON and GHDF read (all or nothing, unknown
  keys by mode, migration hook) and write (defaults left out by default), and get/set by key. The one module allowed to
  write `_` fields by offset. WorldEnvironment is the first client; new data types get a table, not hand-written codecs.
* World light culling (WorldLightCulling.md) - Reach-culls a list of point lights to the strongest few (up to WORLD_MAX_FORWARD_LIGHTS) overlapping an object's bounding sphere, ranked by influence, for the renderer's per-object forward shading.
* UI framework (UIFramework.md) - The in-game UI, on any surface (the window or a texture in the world: the host passes
  the pointer, the keyboard and the viewport). A screen holds a tree of widgets with slot + generation ids, routes the
  tick's GameInput (the focused widget gets every key first, consumed input does not reach the game), drives keyboard
  navigation through actions, runs typed property animations, queues notifications to the end of its update, lays
  widgets out in update and draws them with nested clipping. Widget_Destroy is deferred and takes the whole subtree.
  Covers these modules (UIScreen is split by job behind the private source/UIInternal.h):
  * UIInput - The argument structs handed to a widget's input hooks (keys and buttons are GameKeys).
  * UIAnimation - Typed property values (UIPropertyValue), easing methods, keyframes, keyframe validation and
    evaluation. Pure data + math.
  * UIRenderContext - The widget-local drawing device: wraps the renderer's RenderContext with the widget's absolute box + accumulated tint, and maps widget-local [0;1] draws onto the screen.
  * UIWidgetFactory - Registers capabilities and widget types (a descriptor: struct size, vtable, constructor,
    capabilities, typed properties, focusability), owns per-type pools, creates widgets (base first, id reserved,
    constructor, then alive: a failed constructor leaves nothing) and counts live widgets. One per program.
  * UIWidget - The abstract widget base (vtable incl. Layout and keyboard first refusal, geometry/flags/state, subwidgets
    with a separate draw order, capabilities, typed properties, deferred destroy); concrete widgets embed it first.
  * UIScreen - The context: id registry, roots, focus, input routing, keyboard navigation + outlines, animations,
    notifications (subscriptions; queued, delivered at the end of UIScreen_Update), layout pass and rendering.
  * UITestFrame - A debug game frame over the world test that exercises the UI and the input sharing.
  * LabelWidget - The first concrete widget: displays a (possibly multi-line) rich text with per-line alignment and bound handling (cut/wrap/resize/wrap-then-resize/wrap-then-cut). Embeds a TextDocument and a TextLayout (TextDocument.md), lays out in Update, draws in Render. Auto-fits its widget box to the rendered text.
  Also adds 2D rectangle/outline/line primitives to the Renderer module.
* Text components (TextComponents.md) - Renderer-independent, Minecraft-style rich text as a STRICT TREE: a parent owns
  its children, a tree has at most 64 levels, components own copies of their strings, and fonts and sprite animations
  are named, never held. Every change gives the component and its ancestors a new revision. Covers three modules (plus
  the private source/TextComponentInternal.h):
  * TextComponent - The base type, the four concrete types (String, Translate (a key plus a fallback, styled like a
    string), Sprite, Empty), tree operations (add/insert a root, detach, destroy) and validating property setters.
  * TextComponentFactory - Pooled create/clone/return of components and their strings and child lists; number,
    codepoint and special-character shorthands.
  * TextStyle - Optional, presence-guarded style properties applied to a component by type.
* Text documents and layout (TextDocument.md) - Showing a component tree. Covers two modules:
  * TextDocument - The single owner of one rich text: the tree, its own asset user, the name->asset bindings (each
    failing name logged once and remembered), one playback instance per sprite-animation name (TextDocument_Update
    advances them), the default font name and the translation hook (no string tables yet).
  * TextLayout - Measures and arranges a document once (lines, wrap at spaces, shrink to width, alignment,
    bottom-aligned runs, tabs) into cached glyph and sprite runs; TextLayout_Refresh rebuilds only when the document,
    its tree or the options changed; TextLayout_Draw draws the runs (shadows first) through RenderContext_RenderGlyph.
* GameJSON (GameJSON.md) - Shared parsers and writers for the game's common JSON value shapes (numbers, booleans,
  strings, colors, render colors, vectors); the asset-definition helpers, the text component codec and the property
  tables all use it so the value conventions live in one place. Raises ErrorCode_InvalidJSON for every bad value.
* Text component serialization (TextComponentSerialization.md) - Text component trees to and from the game's three
  formats. JSON (references/text_component_structure.md) and GHDF share ONE codec (source/TextComponentCodec.c,
  private) that lists every type's fields once, validates every value, names the key and the component's path in
  errors (InvalidJSON / Deserialize) and limits nesting to 64 levels. Structured forms convert to/from the format's
  value tree, not bytes. Covers three modules:
  * TextComponentText - Component -> plain UTF-8 string (no deserializer).
  * TextComponentJSON - Component <-> WRJSON value tree (writes shorthands and only non-default keys).
  * TextComponentGHDF - Component <-> GHDF compound tree (explicit type wire ids).
* SoundEngine (SoundEngine.md) - Custom audio mixer (track tree, effects, scheduled commands) on raylib's audio
  stream. DISABLED: AudioEngine_Construct1 always fails until its threading redesign; SoundEngine.md lists the
  defects and is the brief for that redesign (ask the owner its open decisions before starting).

Logging:
Use the Logger module for all program output. Do NOT print to the standard streams directly (no
printf / fprintf(stdout|stderr, ...) or other direct stdout/stderr writes) in game code. Route
messages through a Logger (passed by pointer) at the appropriate level instead. The only allowed
exception is the bootstrap fallback in main when the logger itself fails to initialize and therefore
cannot be used.

Project rules (added after the 2026-10 code review; handoff/2026-10_code_review/FIX_PLAN.md has the reasoning):
1. Asset lifetime: whatever keeps asset data owns its own AssetUserID, and nothing stores asset pointers or by-value
   copies (GameFont, Model, Texture2D, sprite animation sources) past that user. See AssetManager.md. Text follows it
   through TextDocument (since FIX_PLAN Phase 4 components only name their assets).
2. Never hold a Widget* or text component pointer across a call that can run hooks, events or user callbacks;
   re-resolve it by id afterwards (UIScreen_TryGetWidgetById). Destroying widgets from hooks and notification handlers
   is safe: Widget_Destroy is deferred to the end of the screen's update.
3. Render reads state, it never writes it: no mutating world objects, widgets or shared assets from render code.
   Per-object render data belongs in a renderer-side cache (the WorldRenderer's is WorldRenderCache), and meshes draw
   with per-draw Material copies, never by writing shaders or tints into a model asset's materials.
4. Switches over project enums that should handle every value have no `default:`. -Wswitch (on through -Wall, an
   error under -Werror) then fails the build wherever a newly added value is not handled. Handle corrupted values
   after the switch with an explicit error or neutral return, not unreachable().
5. Every recursive walk over data that came from a file or the network has a depth limit.
6. Never log per tick from the update loop, and never log from a non-main thread (the Logger is not thread-safe).
7. A frame returns an error only when an invariant is broken: that error shuts the game down. A recoverable failure
   (missing asset, bad keyframe) changes state so it is not retried every tick (mark it failed, stop the animation,
   fall back) and is logged once, when it happens.
8. Mark every intentionally incomplete piece with `// TODO:`.
9. Read input only through GameInput (Services.Input) and only in Update; never call raylib input functions
   (IsKeyPressed, GetMouseDelta, GetCharPressed, ...) above the input layer. Ask for actions rather than keys for
   anything a player could rebind, and respect consumption: frames are updated top-down and the UI reads first.

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