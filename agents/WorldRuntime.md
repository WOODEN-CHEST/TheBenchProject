# World runtime layer (camera, renderer, services, bootstrap)

The runtime pieces that turn the (rendering-independent) world data layer into something you can see and
fly around. This is the "runnable milestone": a minimal but correctly-architected renderer plus the
bootstrap that wires the whole game together. See [WorldData.md](WorldData.md) for the data model it draws.

## Modules

| Module | Files | What it is |
|---|---|---|
| GameCamera | `include/GameCamera.h`, `source/GameCamera.c` | The 3D camera (position/fov/yaw/pitch/roll), converted to a raylib Camera3D only at render time. |
| Services | `include/Services.h`, `source/Services.c` | A bundle of BORROWED pointers to the shared game services (logger, config, unicode, asset manager + standard type ids, frame manager, GHDF pool, working dir). |
| WorldRenderer | `include/WorldRenderer.h`, `source/WorldRenderer.c` | Renders a World through a GameCamera; fully separate from the world data. |
| WorldTestFrame | `include/WorldTestFrame.h`, `source/WorldTestFrame.c` | A concrete GameFrame: a WASD + mouse-look flycam over a world with the test model at the centre. |

`source/main.c` is the composition root (bootstrap), rewritten this step.

## GameCamera

Coordinate convention: **+Y up, +Z forward** (yaw=pitch=roll=0 looks toward +Z). Plain value type (no
Construct/Deconstruct — like ProgramTime); build with `GameCamera_Create`, mutate public fields directly.
`GameCamera_GetForward` derives the view dir from yaw/pitch; `GetRight` is the horizontal strafe axis
(`cross(worldUp, forward)`, roll-independent); `GetUp` rotates world up around forward by roll.
`GameCamera_ToRaylibCamera` is the single place the raylib `Camera3D` (target = pos+forward, perspective)
is produced. Instance/runtime data — never saved to a world.

## WorldRenderer

Owns a borrowed `AssetManager` + `Logger`, its own `AssetUserID`, and an internal `RenderTexture2D` scene
target. Resolves each object's model asset by name (idempotent `AssetManager_LoadModel`) and releases them
all on deconstruct. `WorldRenderer_RenderToTarget(world, camera, RenderTexture2D target)` runs the whole
pipeline and opens/closes ALL its own passes (so it must NOT be called inside an active pass): it draws the
3D world (sky clear, camera, debug grid, model objects) into the scene target, then blits that into the
frame `target`. **Pixelation pass** (Step 3 increment 1): when enabled (default), the scene target is sized
to the low pixel resolution (`ComputeSceneSize`: longer window axis pinned to 640, shorter derived → square
pixels; 16:9 = 640x360) with `TEXTURE_FILTER_POINT`, and point-upscaled to the window = chunky pixels. The
scene→frame blit uses a **negative source height** (same flip convention the frame manager uses on composite
— verified: every RT-to-RT/screen hop uses negative source). Toggle via `WorldRenderer_SetPixelationEnabled`.

Model draw: composes the object's world transform (scale → Euler XYZ → translate) OVER the model's baked
import transform (`RayModel.transform = MatrixMultiply(RayModel.transform, world)`), matching raylib's own
`DrawModelEx` convention, then `DrawModel(..., tint)`. Uses raylib directly (not `RenderContext_RenderModel`,
which only supports single-axis rotation). `GameModel_GetRaylibModel` returns a by-value copy, so mutating
`.transform` is local and does not touch the asset.

**PBR lighting** (Step 3 increment 3): the renderer loads the `world_pbr` shader asset once (idempotent
`EnsurePbrShader`, under its own asset user), caches its uniform locations, and binds it onto every model
material before `DrawModel`. Per frame, `UpdateLightingUniforms` uploads the camera position plus the sun and
ambient-skylight colours/intensities from the world's `WorldEnvironment` (colours converted sRGB→linear on
the CPU; the sun direction comes from `WorldEnvironment_GetSunDirection`). The shader is a full metallic/roughness
Cook-Torrance model with a single directional sun + a flat ambient term, outputting **linear HDR** (no
tonemap/gamma in the shader — that is the post-pass, below); material metallic/roughness/ao are global scalar
uniforms for now (per-material PBR maps land with a later step). If the shader fails to load the renderer logs
a warning and falls back to raylib's default (unlit) shader so models still draw. NOTE: binding the shader
mutates the *shared* asset material array (intended — the renderer is the sole model consumer); a future
second consumer would need per-draw material copies instead.

**HDR pipeline** (Step 3 increment 4): the scene render target is a floating-point **RGBA16F** framebuffer
(`CreateSceneTarget`, built via rlgl since raylib's `LoadRenderTexture` is 8-bit only; 8-bit fallback + a
one-time log if the GPU can't make the float FBO complete). All scene content is composited in linear HDR: the
PBR models write linear HDR and the sky clear is linearized (`LinearizeColorBytes`). Tonemapping is a **post
pass** — the `world_tonemap` fragment shader (ACES + gamma) is applied via `BeginShaderMode` around the
scene→frame blit, so it runs AFTER the pixelation point-upscale. Result: HDR/realistic colour compositing
that still reads as chunky pixels, mapped to display [0,1] only at the very end. Bloom/sunshafts later plug
into the HDR buffer before this tonemap.

**Atmospheric sky** (Step 3 increment 5): a full-screen `world_sky` shader (Rayleigh + Mie single-scattering,
sun disc, night stars) drawn behind the geometry in `DrawSky` — a 2D pass that writes no depth, so the
depth-tested 3D geometry draws over it. Each pixel's world ray is reconstructed from the inverse
view-projection (`MatrixInvert(MatrixMultiply(GetCameraMatrix, MatrixPerspective(...)))`, matching
`BeginMode3D`), so the sky lines up with the camera. Outputs linear HDR (tonemapped by the post-pass). Driven
entirely by `WorldEnvironment` (turbidity, sky tint, sun colour/intensity/size, star seed/density/brightness);
falls back to the linearized CPU gradient clear if `world_sky` is unavailable. GPU look unverified/untuned.

**Sun shadows + outlines** (Step 3 increments 7–10 + the outline/shadow refactor): a directional sun shadow
map plus a low-res post pass give the stylised look.
* **Shadow map**: `RenderShadowMap` renders model depth (via the trivial `world_depth` shader) from a tight,
  camera-following orthographic sun frustum into a 2048² depth texture, storing the world→light-clip matrix.
  The `world_pbr` fragment samples it with a **2×2 PCF + slope-scaled bias** for CRISP, hard-edged pixel-art
  shadows (the old soft 3×3 PCF + frustum edge-fade were dropped), darkening only the sun's direct term
  (ambient still lights shadows). Gated on shadows-enabled + strength > 0 + sun above the horizon.
* **Normal G-buffer + outlines**: `RenderNormalBuffer` draws model objects through the `world_normal` shader
  (a vertex+fragment pair) into a low-res 8-bit RGBA target with blending disabled — RGB = view-space normal,
  A = surface/outline flag (0 = sky/grid, 0.5 = surface, 1.0 = surface with per-object `HasOutline`). The
  `world_postfx` pass then edge-detects in the style of the three.js RenderPixelatedPass / Godot 3D-pixel-art
  shader: **depth/surface-edge silhouettes darken** the near object's rim, and **view-normal-edge creases** are
  recoloured **sun-aware** (darken on the sun-lit side, brighten on the shadowed side, using the sun direction
  transformed into view space). The sky (flag 0) is never a surface, so it is never outlined. The same pass
  also does tangent-plane SSAO (gated to surfaces via the flag). Outlines are code-configurable (renderer
  toggle + built-in strength); AO/shadow strengths are config × world multipliers. This G-buffer replaced the
  old flag-only `world_mask`.

SCOPE now: models are PBR-lit with crisp sun shadows, the sky is the atmospheric-scattering shader, the post
pass adds SSAO + depth/normal-edge outlines, and the whole scene is HDR→tonemapped. Sprites, lights, and the
remaining effect pipeline (point-light culling, fog, bloom, sunshafts, per-object omit-pixelation) are marked
TODO and layer on later.

## Shader assets (vertex + fragment)

`source/ShaderDefinition.c` loads an optional vertex stage and an optional fragment stage
(`vertex_location` / `fragment_location`; a bare `location` is a back-compat alias for the fragment stage).
Either may be omitted to fall back to raylib's built-in stage. Because the asset resource resolver matches
files by **stem** (ignoring extension), a paired shader's two source files must have DISTINCT stems — the
`world_pbr` shader uses `shaders/pbr/vertex.vert` + `shaders/pbr/fragment.frag`, referenced as `"pbr/vertex"`
and `"pbr/fragment"`. The `.vert`/`.frag` files sit beside `pbr.json` and are skipped by `ReadDefinitions`
via the per-type `json` definition-file extension. The `world_normal` G-buffer shader is a second such pair
(`normal/vertex` + `normal/fragment`), whose vertex stage emits view-space normals for the outline pass.

## WorldTestFrame

Concrete `GameFrame` (embeds `GameFrame` first). Owns a `World` (test model at origin), a `GameCamera`, and
a `WorldRenderer`. Movement (WASD + SPACE/CTRL, SHIFT to sprint) runs in `Update` at the fixed timestep;
mouse-look runs in `Render` (once per real frame — avoids the fixed-step over-applying the per-frame mouse
delta). Captures the cursor (`DisableCursor`) in `Start`, releases it in `End`/`Destroy`. Loading is
synchronous (assets preloaded in `BeginLoad` via `WorldRenderer_PrepareWorld`), so the stepped load/unload
slots are trivial. `Create` builds everything up front so `Destroy` is always safe (even on forced teardown).

## Bootstrap (main.c)

Order: Logger → copy `GetWorkingDirectory()` once (absolute base) → load config (absolute path) → load
unicode DB (`asset/text/unicode_data.txt`, empty-DB fallback) → `InitWindow` + `SetExitKey(KEY_ESCAPE)`
(so the test is quittable) → AssetManager (`Construct1`, `CreateStandardAssetTypes`, add absolute `asset`
search root, `ReadDefinitions`) → GHDF pool → GameFrameManager → build `Services` → create + add
`WorldTestFrame` → run loop. Teardown (reverse, ordering matters): frame manager (releases assets + GPU
targets, needs asset manager + GL alive) → asset manager → JSON pool → GHDF pool → `CloseWindow` → unicode
→ config → working-dir buffer → logger.

Paths are absolute (built via `Path_Append(workingDir, relative)`). The Logger is the exception: it has no
path parameter and resolves its `logs/` relative to CWD (runtime CWD is `compile/out`); left as-is.

## Asset-pipeline fix (this step)

The provided test model couldn't load via `AssetManager_ReadDefinitions` (it parsed every file under the
type dir, so the `.obj` resource aborted the read; plus the dir was `model` not `models` and the `.json`/`.obj`
shared a stem). Resolved with a **per-type definition-file extension**: `AssetTypeInfo` (and the internal
`TypeRecord`) gained an optional `DefinitionFileExtension` (without the dot; NULL = parse all, back-compat).
`ReadDefinitions` now skips files whose extension doesn't match (case-insensitive, via `Path_GetExtension` +
a small `ExtensionMatches` helper). The standard types set it to `json` (`ASSET_TYPE_DEFINITION_EXTENSION` in
`AssetTypesStandard.h`), so resource files (models, textures, ...) can live beside their JSON definitions.
On-disk: renamed `asset/model`→`asset/models` and `test_model.json`→`test.json` (distinct stem from
`test_model.obj`). Verified headlessly: `ReadDefinitions` succeeds and `HasDefinition(model,"test")` is true.

## Status

Builds clean (full project, incl. `-Wconversion`). The renderer/frame path is not runtime-verifiable
headlessly (needs a real GL display — no `xvfb` here), but the camera controls and the asset pipeline were
verified with standalone scratch tests. The test model should now load and render at runtime.
