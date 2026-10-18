# World runtime layer (camera, renderer, services, bootstrap)

The runtime pieces that turn the (rendering-independent) world data layer into something you can see and
fly around: the camera, the world renderer, the services bundle, the test frame and the bootstrap in `main.c`.
See [WorldData.md](WorldData.md) for the data model it draws.

## Modules

| Module | Files | What it is |
|---|---|---|
| GameCamera | `include/GameCamera.h`, `source/GameCamera.c` | The 3D camera (position/fov/yaw/pitch/roll), converted to a raylib Camera3D only at render time. |
| Services | `include/Services.h`, `source/Services.c` | A bundle of BORROWED pointers to the shared game services (logger, config, unicode, asset manager + standard type ids, frame manager, per-tick input, GHDF pool, base directory). |
| WorldRenderer | `include/WorldRenderer.h`, `source/WorldRenderer*.c`, `source/WorldRendererInternal.h` | Renders a World through a GameCamera; fully separate from the world data. Split by render pass, see "Renderer structure". |
| WorldRenderCache | `source/WorldRenderCache.h`, `source/WorldRenderCache.c` (private) | The renderer's per-object cache and per-frame draw lists (models with resolved assets, sprites, lights). |
| WorldTestFrame | `include/WorldTestFrame.h`, `source/WorldTestFrame.c` | A concrete GameFrame: a WASD + mouse-look flycam over a test world. |

`source/main.c` is the composition root (bootstrap).

## GameCamera

Coordinate convention: **+Y up, +Z forward** (yaw=pitch=roll=0 looks toward +Z). Plain value type (no
Construct/Deconstruct — like ProgramTime); build with `GameCamera_Create`, mutate public fields directly.
`GameCamera_GetForward` derives the view dir from yaw/pitch; `GetRight` is the horizontal strafe axis
(`cross(worldUp, forward)`, roll-independent); `GetUp` rotates world up around forward by roll.
`GameCamera_ToRaylibCamera` is the single place the raylib `Camera3D` (target = pos+forward, perspective)
is produced. Instance/runtime data — never saved to a world. With +Z forward and +Y up in raylib's
right-handed pipeline, world +X renders on the camera's LEFT; the owner confirmed this is wanted.

## WorldRenderer

### Ownership and lifetime

- Borrows the `AssetManager`, the `Logger` and the `GameConfig` (config-side effect multipliers).
- Owns two asset users: one holds the pass shaders for its lifetime, the other the world's models.
  `WorldRenderer_PrepareWorld` replaces the model user with a fresh one after resolving every model the world
  still uses, so models no object uses any more are released (asset holds are not counted, so this swap is
  how unused models get released). Call it after building or loading a world, after large changes, and
  whenever a different World is (re)built at the address of the previous one.
- Never writes to the world or to shared assets (AGENTS.md rule 3):
  - meshes draw with a `Material` built on the stack (`WorldRendererGeometry`): the asset's 12 material maps
    copied, the pass shader, the object tint multiplied into the copy's albedo colour;
  - the PBR shader's location table is a renderer-owned copy (`_pbr._locs`) whose material map samplers point
    at `mraMap` / `normalMap` / `emissiveMap`, so `DrawMesh` binds each material's textures without touching the
    shared shader asset;
  - sprite animations belong to the World (`World_Construct2` binds them); the renderer only reads the current
    frame.

### Per-object render cache

`WorldRenderCache_Sync` runs at the start of every render and walks the world once:
- **Models:** one entry per model object, matched by object id. An entry's model asset is resolved once
  (`AssetManager_LoadModel` under the model user) and again only when the object's asset revision changes
  (`WorldModelObject_SetModelAssetName`). A failed resolution is logged once (`WorldRenderer: model object N
  ("name") draws nothing: ...`) and not retried until the next rename. Each frame the entry gets the final
  transform (import transform inside scale → Euler XYZ → translation, raylib's `DrawModelEx` convention) and a
  conservative world bounding sphere from `GameModel_GetLocalBounds` (computed once at load).
- **Sprites:** the sprites whose playback instance has a frame, with their current frame, position, size and
  tint.
- **Lights:** every light with position, reach, intensity, colour and shadow flag.

Entries follow the objects across additions and removals: when the world's object-set version changes, entries
are re-matched by object id, and an entry is carried over only if its object still names the model it was
resolved for (an id can come back with another object, e.g. an editor undo or a re-applied save). A different
World resets the cache, and the per-frame walk also checks each entry's object pointer, so a world rebuilt at the
same address is caught as soon as an object differs. Objects spawned at runtime appear without calling
`PrepareWorld`; a model that failed to load is retried only after a rename or `PrepareWorld` (call it after asset
definitions change).

### Pipeline (one `WorldRenderer_RenderToTarget`)

1. Sync the cache; make sure the targets match the window size and pixelation setting
   (`WorldRendererScene_EnsureTargets`).
2. Build the frame context (`WorldRenderFrame`): camera, the scene's view/projection matrices (the projection
   `BeginMode3D` builds for the scene target, shared by every pass that reconstructs positions or rays), sun
   direction and daylight, sizes, and every pass decision.
3. Lighting values for the frame (`WorldRendererLighting_PrepareFrame`): linear sun colour and daylight-scaled
   intensity, the ambient colour, the scene luminance for eye adaptation, fog density, shadow strength, and the
   forward-light candidates (every light except the cube-shadow light) with their linear radiance.
4. Sun shadow cascades, then the point-light cube shadow (only casters within the light's reach).
5. The normal/flag G-buffer and the AO pre-pass, when AO or outlines run.
6. The scene pass: clear, atmospheric sky, PBR models (per object: up to `WORLD_MAX_FORWARD_LIGHTS` reach-culled
   point lights; per material: PBR factors and map flags), sprites, debug grid and light gizmos.
7. Outlines, then bloom and sun shafts from the (outlined) HDR scene.
8. Eye adaptation, then the tonemapped point-upscale into the frame target.
9. The crisp overlay for OmitPixelation objects, composited over the finished image.

### Effects

**Pixelation.** When enabled (default), the scene target is sized to the low pixel resolution (longer window
axis pinned to 640, shorter derived → square pixels; 16:9 = 640x360) with point filtering, and point-upscaled to
the window. The scene→frame blit uses a **negative source height** (the flip convention every render-texture hop
uses). Toggle with `WorldRenderer_SetPixelationEnabled`.

**HDR.** The scene target is RGBA16F (built with rlgl; `LoadRenderTexture` is 8-bit only), with three tiers:
HDR + samplable depth (everything available), HDR + depth renderbuffer (AO, outlines, sun shafts and the crisp
overlay unavailable), 8-bit (last resort). The tier is logged once. Everything is composited in linear HDR and
tonemapped (ACES + gamma, `world_tonemap`) only in the final blit, after the pixelation upscale.

**PBR.** `world_pbr` (vertex + fragment pair): Cook-Torrance GGX with a directional sun, a sky-tracking ambient
(blue by day, warm near the horizon, a dim cool floor at night; the `NIGHT DARKNESS KNOB`s in
`WorldRendererLighting.c`), forward point lights, per-material metallic/roughness/occlusion/emissive factors and
optional albedo / normal / packed ORM (in the METALNESS slot) / emissive maps from the model definition's
`material_overrides`, derivative-based normal mapping, and atmospheric distance fog that fades into the same sky
the sky pass draws (the sky constants in `pbr/fragment.frag` must stay identical to `sky/fragment.frag`). Without
it models draw unlit with raylib's default shader.

**Sky.** `world_sky`: Rayleigh + Mie single scattering, sun disc and a seeded star field, drawn as a full-screen
pass before the geometry (it writes no depth). Each pixel's ray comes from the inverse of the frame's
view-projection, so the sky lines up with the 3D camera. Falls back to a CPU gradient clear.

**Sun shadows.** Two camera-centred orthographic cascades (2048² depth textures): NEAR (small extent, sharp) and
FAR (large extent, coarse, optional). Each model object's `WorldShadowTier` (Both / Near / Far) routes which
cascade(s) it casts into. The PBR shader samples near first, then far, with 2×2 PCF and a slope-scaled bias,
darkening only the sun's direct term. Active when shadows are enabled, the strength is above 0 and the sun is
above the horizon.

**Point-light shadow.** One cube shadow for the shadow-casting light nearest the camera (`CastsShadows`): six
90° faces of packed linear distance (RGBA8 cube), sampled with 20-tap PCF. That light is shaded separately and is
excluded from the forward candidates, so an object can still get eight other point lights.

**G-buffer, AO and outlines.** `world_normal` (models) and `world_normal_sprite` (alpha-discarded sprites) write
view-space normals (RGB) and a surface/outline flag (A: 0 = nothing, 0.5 = surface, 1 = surface with outline)
into a low-res 8-bit target with blending disabled. `world_ao` turns it into an AO multiplier that the PBR pass
applies to its ambient term only (so AO never dims direct light). `world_postfx` draws outlines after the scene:
depth/surface silhouettes darken, view-normal creases are recoloured sun-aware; the sky is never outlined. Both
are under the post-effects toggle (`WorldRenderer_SetPostEffectsEnabled`, O key in the test frame).

**Bloom and sun shafts.** Half-resolution HDR passes added by the tonemap: a depth-masked bright pass (the sky
does not bloom) with a separable blur, and a radial blur of the unoccluded sky toward the sun's screen position
that fades as the sun nears the screen margin or the horizon.

**Crisp overlay.** OmitPixelation models and sprites render lit at window resolution and are composited over the
pixelated image, depth-tested against the scene so they are occluded correctly (e.g. a readable in-world
screen). When the overlay cannot run they fall back to the pixelated passes, so they never vanish. Crisp objects
get no AO and no outlines.

**Effect strengths.** Bloom, sun shafts, shadows, AO and fog combine the world's multiplier
(`WorldEnvironment`), the config's (`GameConfig`) and a built-in base. Outlines are a code-side toggle.

### Failures and status

- A shader that fails to load is logged once (warning) and switches its feature off. A GPU target that cannot be
  created is logged once and switches its pass off. The only fatal failure is a scene target that cannot be
  created at all: `WorldRenderer_RenderToTarget` returns `ErrorCode_InvalidState`.
- `WorldRenderer_GetStatus` reports the scene tier, each feature as active / inactive (switched off or not
  applicable this frame) / unavailable (missing shader or target), and object counts. The renderer logs it once
  after its first render; `WorldRenderer_LogStatus` logs it again (the I key in the test frame).

### Renderer structure (keep it clean)

| File | What it holds |
|---|---|
| `WorldRenderer.c` | Public API: create/destroy, `PrepareWorld`, `RenderToTarget` (frame context, pass decisions, pass order), status. Orchestration only. |
| `WorldRendererInternal.h` | The renderer state (one state struct per pass), `WorldRenderFrame`, fixed texture units, shared constants, and every internal function, documented. |
| `WorldRendererCommon.c` | Config multipliers, colour conversion, shader loading, one-time warnings, render-target constructors. |
| `WorldRendererScene.c` | Scene target size, HDR tiers and recreation on resize (asks each pass to recreate its targets); the scene pass. |
| `WorldRendererGeometry.c` | Model meshes with per-draw materials, sprite billboards, light gizmos, the pixelation filter. |
| `WorldRendererLighting.c` | The PBR shader, its location table and uniforms, forward point lights, per-material factors, eye adaptation. |
| `WorldRendererShadows.c` | Sun cascades and the point-light cube shadow. |
| `WorldRendererGBuffer.c` | The normal/flag G-buffer and the AO pre-pass. |
| `WorldRendererSky.c` | The atmospheric sky. |
| `WorldRendererPost.c` | Outlines, bloom, sun shafts and the tonemap. |
| `WorldRendererCrisp.c` | The crisp overlay. |

The renderer keeps receiving features, so:

- **One pass, one place.** A new pass gets its own file (or a clearly separated part of the closest one), its
  own state struct in `WorldRendererInternal.h`, and its own `LoadShaders` / `CreateTargets` / `ReleaseTargets`
  hooks called from `EnsureShaders`, `WorldRendererScene_EnsureTargets` and `_ReleaseTargets`. Do not grow
  `WorldRenderer.c` beyond orchestration.
- **Decide once, read the frame.** Per-frame values and pass decisions go into `WorldRenderFrame`, built in
  `RenderToTarget`; passes read it instead of recomputing camera matrices or re-deciding.
- **Draw from the cache.** Passes iterate the render cache's lists; never walk the world or resolve assets by
  name in a pass.
- **Render reads, it never writes.** No writes to world objects or shared assets: per-draw material copies, the
  renderer's own location tables, and per-object render data in the cache.
- **Fixed texture units.** Renderer-owned samplers use `WorldRendererTextureUnit` (units 12–15, above raylib's
  material-map units 0–11 and the 2D batch's 0–4; `WORLD_RENDERER_MATERIAL_MAP_COUNT` must match raylib's
  `MAX_MATERIAL_MAPS`, see BUILDING.md). Point each sampler at its unit once at load and only bind textures per
  frame. Never leave a sampler on unit 0: OpenGL forbids samplers of different types on one unit. For 2D
  (`DrawTexturePro`) passes bind extra samplers with `SetShaderValueTexture` after `BeginShaderMode`; manual
  `rlActiveTextureSlot` binds only work for 3D `DrawMesh` draws.
- **Constants shared with GLSL.** `MAX_POINT_LIGHTS` in `pbr/fragment.frag` should equal
  `WORLD_MAX_FORWARD_LIGHTS`; the shader clamps its loop and the renderer checks the arrays at load (and logs if
  the shader holds fewer), so a mismatch costs lights, never memory safety.
- **Failures degrade loudly once.** A missing shader or target falls back with one log line, never silently
  and never per frame.

## Shader assets (vertex + fragment)

`source/ShaderDefinition.c` loads an optional vertex stage and an optional fragment stage
(`vertex_location` / `fragment_location`; a bare `location` is a back-compat alias for the fragment stage).
Either may be omitted to fall back to raylib's built-in stage. Both stages accept every shader-stage extension,
so a paired shader's two source files need DISTINCT stems: `world_pbr` uses `shaders/pbr/vertex.vert` +
`shaders/pbr/fragment.frag`, referenced as `"pbr/vertex"` and `"pbr/fragment"`. The `world_normal` G-buffer shader
is a second such pair. Each renderer shader lives in its own directory under `asset/shaders/`.

## WorldTestFrame

Concrete `GameFrame` (embeds `GameFrame` first). Owns a `World` built with `World_Construct2` (so it binds its
sprites' animations itself), a `GameCamera`, and a `WorldRenderer`. All input runs in `Update` at the fixed
timestep and comes from `Services.Input` ([GameInput.md](GameInput.md)), after the UI test frame above had first
refusal: movement through the actions (`move_*`, default WASD + SPACE/CTRL, `sprint` default SHIFT), mouse-look
(`GameInput_GetMouseDelta`, this tick's movement, only while the cursor is captured) and the debug keys as raw keys: P
pause/resume the day-night cycle, 1–4 jump to dawn/noon/dusk/midnight, `[` `]` scrub the time, O post effects, B bloom,
G sun shafts, C crisp overlay, L light gizmos, I log the renderer status, T toggle the text sample. M frees the cursor
for the UI (and captures it again); a left click the UI did not take captures it again. An unconsumed `pause` (Escape
once the UI is not using it) quits through `GameFrameManager_RequestStop`. `Render` handles no input. Captures the cursor
in `Start`, releases it in `End`/`Destroy`. The world is built in `Create`; `BeginLoad` calls
`WorldRenderer_PrepareWorld` (shaders and models), so the stepped load/unload slots are trivial. `Create` builds
everything up front so `Destroy` is always safe (even on forced teardown); it destroys the renderer, then the world,
then the text sample.

**Text sample** (shown by default, T toggles it): a `TextDocument` built from the `TEXT_SAMPLE_JSON` text component
JSON in `Create` (so the real game runs the codec), with `noto_sans` as its default font, laid out in `Update`
(`TextLayout_Refresh`) and drawn in `Render` over the final image, through a `RenderContext` on the frame target. It
shows Latvian and Cyrillic letters, colors, underline, strikethrough, a translate fallback, the `test_sprite`
animation inline, and one line in the `test` font (its missing letters draw as `?`, and its load logs which
codepoints the file lacks). A sample that fails to load is logged once and leaves the sample empty.

## UITestFrame

A debug `GameFrame` added on top of `WorldTestFrame` (`include/UITestFrame.h`, `source/UITestFrame.c`), so it gets the
input first. It owns a widget factory, a `UIScreen` (the window's size as its viewport, `UIScreenInput_CreateFromWindow`
as its input) and a few labels in `noto_sans`: a hint at the bottom left (input-disabled) and a focusable "menu" label at
the top right with three focusable item labels inside it. Hover and focus notifications tint the labels (yellow /
blue) and gaining focus plays a short size animation; Delete destroys the focused item, R rebuilds the menu. Tab or the
arrows start keyboard navigation, Enter opens the menu, Escape closes it and then lets go of the UI, after which Escape
reaches the world test (which quits). With the cursor captured (mouse look) the screen gets no pointer; M in the world
test frees it. It renders into its own transparent target. Known limitation: frames are composited with plain alpha
blending, so the antialiased edges of text on a transparent frame come out thinner than on an opaque one (fixing it
means premultiplied compositing in the frame manager).

## Bootstrap (main.c)

Order: base directory (`FileSystem_GetExecutableDirectory`) → Logger (`<base>/logs`) → if the executable's
directory was unavailable, warn and fall back to a copy of `GetWorkingDirectory()` → load config
(`<base>/config.json`) → load unicode DB (`<base>/asset/text/unicode_data.txt`, empty-DB fallback) →
`InitWindow` (fullscreen/vsync flags from the config) + `SetExitKey(KEY_NULL)` (Escape is the UI's `ui_back` and the
game's `pause` action, never raylib's quit key) → AssetManager (`Construct1` with the logger, `CreateStandardAssetTypes`,
cache directory `<base>/.wr_asset_cache`, absolute `<base>/asset` search root, `ReadDefinitions`) → GHDF pool →
`GameInput` (defaults, then `<base>/controls.json` if present) → GameFrameManager (polls the input) → build `Services`
(`Services.BaseDirectory`, `Services.Input`) → create + add `WorldTestFrame`, then `UITestFrame` on top → run loop.
Teardown (reverse, ordering matters): frame manager (releases assets + GPU targets, needs asset manager + GL alive) →
asset manager → JSON pool → GHDF pool → `CloseWindow` → input → unicode → config → base-directory buffer → logger.

Every game file is resolved against the executable's directory (built via `Path_Append(base, relative)`), so
the game works no matter where it is started from.

## Asset files next to their definitions

Within a type's directory a `.json` file is a definition and never a resource, so a definition may share a stem
with the file it loads (`test.json` + `test.png`). A location matches files by their whole last name and the
loader's accepted extensions; more than one candidate is an error. See [AssetManager.md](AssetManager.md).

## Status

Builds clean under the project's warning set. Nothing that needs a GL context can be checked headlessly:
agents verify the cache, culling and world logic with the tests in `tests/`, and the owner runs the game to check
the rendering (start with the status line the renderer logs after its first frame).
