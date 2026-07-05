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

Owns a borrowed `AssetManager` + `Logger` and its own `AssetUserID`; resolves each object's model asset by
name (idempotent `AssetManager_LoadModel` under that user) and releases them all on deconstruct.
`WorldRenderer_Render(world, camera, RenderContext*)` is called INSIDE an active render pass (the frame
opens the target pass): it `ClearBackground(sky)`, enters 3D mode with the camera, draws a debug grid (if
enabled) and every model object, then leaves 3D mode. It does NOT open/close the target pass or composite.

Model draw: composes the object's world transform (scale → Euler XYZ → translate) OVER the model's baked
import transform (`RayModel.transform = MatrixMultiply(RayModel.transform, world)`), matching raylib's own
`DrawModelEx` convention, then `DrawModel(..., tint)`. Uses raylib directly (not `RenderContext_RenderModel`,
which only supports single-axis rotation). `GameModel_GetRaylibModel` returns a by-value copy, so mutating
`.transform` is local and does not touch the asset.

SCOPE this step: models only. Sprites, lights, and the full effect pipeline (atmospheric sky/sun, PBR,
sun shadow map, light culling, bloom, sunshafts, fog, outlines, pixelation) are marked TODO and layer on
later. Renders 3D straight into the frame's depth-backed target; the frame manager flips + composites to
screen.

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
