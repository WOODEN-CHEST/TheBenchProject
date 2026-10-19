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
| WorldRenderCache | `source/WorldRenderCache.h`, `source/WorldRenderCache.c` (private) | The renderer's per-component cache and per-frame draw lists (models with resolved assets, sprites, lights), built from the world's per-type component arrays. |
| NightVision | `source/NightVision.h`, `source/NightVision.c` (private) | The eye's dim-light vision as pure maths: its calibration on the game's light scale, the rods' share, the rods' grey (see "Night vision"). |
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
  still uses, so models no component uses any more are released (asset holds are not counted, so this swap is
  how unused models get released). Call it after building or loading a world, after large changes, and
  whenever a different World is (re)built at the address of the previous one.
- Never writes to the world or to shared assets (AGENTS.md rule 3):
  - meshes draw with a `Material` built on the stack (`WorldRendererGeometry`): the asset's 12 material maps
    copied, the pass shader, the object tint multiplied into the copy's albedo colour;
  - the PBR shader's location table is a renderer-owned copy (`_pbr._locs`) whose material map samplers point
    at `mraMap` / `normalMap` / `emissiveMap`, so `DrawMesh` binds each material's textures without touching the
    shared shader asset;
  - sprite animations belong to the World (its sprite components bind them under the world's asset user); the
    renderer only reads the current frame.

### Per-component render cache

`WorldRenderCache_Sync` runs at the start of every render and walks the world's per-type component arrays once
(`World_GetComponents`), never the objects:
- **Models:** one entry per model component, in the world's order, remembering the owner's id and the component. An
  entry's model asset is resolved once (`AssetManager_LoadModel` under the model user); when the component's revision
  changes the name is hashed again, and the model is resolved again only if the name changed. A failed resolution is
  logged once (`WorldRenderer: object N ("name") draws nothing: ...`) and not retried until the name changes. Each
  frame the entry copies the component's settings (outline, crisp, shadow tier, casts shadows), gets the final
  transform of its object (import transform inside scale → Euler XYZ → translation, raylib's `DrawModelEx` convention)
  and a conservative world bounding sphere from `GameModel_GetLocalBounds` (computed once at load), and `IsDrawn`:
  its model resolved and its object enabled and visible. Passes draw only the `IsDrawn` entries; a hidden object keeps
  its entry and its model.
- **Sprites:** the sprites of enabled and visible objects whose playback instance has a frame, with their current
  frame, position, size, tint, outline and crisp settings.
- **Lights:** the lights of enabled and visible objects, with position, reach (the range), intensity, colour and
  shadow flag.

Entries follow the components across additions and removals: when the world's component-set version changes, entries
are re-matched by owner id, and an entry is carried over only if its component still names the model it was
resolved for (an id can come back with another object, e.g. an editor undo or a re-applied save). A different
World resets the cache, and the per-frame walk also checks each entry's component pointer and owner id, so a world
rebuilt at the same address is caught as soon as a component differs. Components added at runtime appear without
calling `PrepareWorld`; a model that failed to load is retried only after its name changes or `PrepareWorld` (call it
after asset definitions change).

### Pipeline (one `WorldRenderer_RenderToTarget`)

1. Sync the cache; make sure the targets match the window size and pixelation setting
   (`WorldRendererScene_EnsureTargets`).
2. Build the frame context (`WorldRenderFrame`): camera, the scene's view/projection matrices (the projection
   `BeginMode3D` builds for the scene target, shared by every pass that reconstructs positions or rays), the sky
   (`WorldEnvironment_GetSkyLights`: the sun's and the moon's directions, daylight, the moon's phase and lit fraction,
   the two lights' intensities and the shadow light with its fade), the stars' rotation (`StarsToWorld`), the render
   clock (`TimeSeconds`), sizes, the night sky values (`WorldRendererSky_PrepareFrame`: limiting magnitude, Milky Way
   radiance, skyglow, the moonlit sky's strength and the moon's disc; first the star catalogue is rebuilt if the
   world's seed or density changed, and the sky textures reloaded if the world names others), and every pass decision.
3. Lighting values for the frame (`WorldRendererLighting_PrepareFrame`): the sun's and the moon's linear colours,
   the ambient colour (with the moonlit sky's fill), the scene luminance for eye adaptation, fog density, shadow
   strength, and the forward-light candidates (every light except the cube-shadow light) with their linear radiance.
   Then the eye's night vision for the frame (`DecideNightVision`: its strength, the field luminance, the rods' grey).
4. The shadow cascades of the shadow light (the sun or the moon), then the point-light cube shadow (only casters
   within the light's reach).
5. The normal/flag G-buffer and the AO pre-pass, when AO or outlines run.
6. The scene pass: clear, atmospheric sky (with the moonlit sky, the moon's disc, the Milky Way and light pollution's
   skyglow), the stars (none behind the moon), PBR models (the sun and the moon; per object: up to
   `WORLD_MAX_FORWARD_LIGHTS` reach-culled point lights; per material: PBR factors and map flags), sprites, debug grid
   and light gizmos. The sky's glow and the models' reflected light go through the eye's night vision.
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

**PBR.** `world_pbr` (vertex + fragment pair): Cook-Torrance GGX with two directional lights, the sun and the moon
(one `DirectionalLight` function; see "Moon" for the moonlight), a sky-tracking ambient (blue by day, warm near the
horizon, a dim cool floor at night, the `NIGHT DARKNESS KNOB`s in `WorldRendererLighting.c`, raised by the moonlit
sky's fill), forward point lights, per-material metallic/roughness/occlusion/emissive factors and
optional albedo / normal / packed ORM (in the METALNESS slot) / emissive maps from the model definition's
`material_overrides`, derivative-based normal mapping, and atmospheric distance fog that fades into the same sky
the sky pass draws (both shaders `#include "common/atmosphere"`, so the atmosphere maths are one file; the sky pass's
below-horizon mirror is still repeated in the PBR shader's `fogSkyColor`). Without it models draw unlit with raylib's
default shader.

**Sky.** `world_sky`: Rayleigh + Mie single scattering (`shaders/common/atmosphere.glsl`) lit by the sun and, at
night, by the moon; the sun disc, the moon's disc, the Milky Way and light pollution's skyglow, drawn as a full-screen
pass before the geometry (it writes no depth). Each pixel's ray comes from the inverse of the frame's view-projection,
so the sky lines up with the 3D camera. Falls back to a CPU gradient clear. Its textures are the ones the world's
environment names (`MilkyWayTexture`, default `sky_milky_way`; `MoonTexture`, default `sky_moon`): each sky texture
slot holds its texture under its own asset user, loads it when the environment names it (at `PrepareWorld` or the
first render after a change) and releases it when the environment names another; a name that fails is logged once
and not retried until the name changes (`WorldRendererSky_SyncTextures`).

**Night sky.** The pure maths is in the private `source/NightSky.{h,c}` (tested in `tests/NightSkyTests.c`); the GLSL
the passes share is `shaders/common/night_sky.glsl`.
- **The sky turns.** `WorldEnvironment_GetStarsToWorld` turns everything fixed among the stars once a day around the
  celestial pole, which stays at world (0, sin SunAngle, -cos SunAngle): low toward -Z, at the height SunAngle (the
  latitude). The stars also drift through the year: the star frame is the celestial frame of the June solstice (where
  `NightSky_GetCelestialToGalactic` places the galaxy), turned ahead of the sun's frame by the sun's progress through
  the tropical year (continuous, no jump at New Year). So at the same time of day the stars stand about 0.99 degrees
  further along each day and rise about 4 minutes earlier: the galactic centre is high at midnight in June, set by
  midnight in late September, below the horizon all night in December. `WorldEnvironment_GetCelestialToWorld` is the
  sun's own frame (the sun at (1,0,0)), which the moon is placed in.
- **Stars** (`WorldRendererStars.c`, shader `world_stars`). A catalogue generated from `StarSeed`: `StarDensity` 1 is
  an Earth-like sky (about 9000 stars, at most 20000), magnitudes -1..7 by the real count law (2.8 times as many per
  magnitude), B-V colours (mostly white to yellow, some blue and orange), up to twice as dense along the galactic
  plane. Each star has its own random stream, so a higher density only adds stars. It becomes one mesh of one-pixel
  quads (six vertices per star, unindexed: raylib's 16-bit indices would cap an indexed mesh at 16384 stars), rebuilt
  only when the seed or the star count changes. The vertex shader projects each star as a point at infinity just
  inside the far plane and widens it to exactly one scene pixel, so a star lights one pixel at a steady brightness and
  never flickers. Drawn right after the sky: additive (ONE, ONE) with alpha 0 (the scene's alpha reaches the frame
  target), depth-tested, writing no depth (the bloom prefilter and the sun shafts read far depth as sky), culling off.
- **What shows.** The limiting magnitude (`NightSky_GetLimitingMagnitude`, one value per frame): 6.5 at a dark site,
  minus 3.5 x `LightPollution`, minus the moonlit sky's share (a high full moon at the default moonlight takes 1.75,
  so the faint stars and most of the Milky Way go; it grows with the logarithm of the moon's light, so a quarter moon
  still takes about 0.6, and it shrinks with light pollution, which already brightened the sky), and in twilight -1.5
  with the sun up, rising 0.5 per degree below the horizon (magnitude 1.5 at -6 degrees, full night at -18); stars fade
  out over the last 0.5 magnitude, so at dusk the brightest appear first. The moon's disc hides the stars behind it
  (the star shader drops them). Near the horizon stars and the Milky Way dim by extinction (Kasten-Young air mass, 0.25
  magnitudes per air mass); below it nothing is drawn. Stars twinkle by `StarTwinkle` x noise, more near the horizon,
  on `WorldRenderFrame.TimeSeconds` (the renderer's own clock, wrapped hourly).
- **Milky Way.** The texture the world names (`MilkyWayTexture`, default `sky_milky_way`: sRGB, trilinear with
  mipmaps), sampled once per pixel in galactic coordinates: `NightSky_GetCelestialToGalactic` places the galaxy as on
  Earth (plane 62.9 degrees to the equator, centre at declination -29) in the star frame, which the date turns (above).
  The panorama's longitude grows to the left, as in astronomical all-sky maps. Explicit mip level from the pixel's angular size
  (automatic derivatives would draw a seam line where the longitude wraps). Brightness: `MilkyWayBrightness` x full
  at a limiting magnitude of 6 or more, gone at 4.5 or less x extinction. A missing texture is logged once and the
  sky draws no Milky Way.
- **Light pollution.** At night the sky adds a skyglow of `LightPollutionColor` x `LightPollution`, strongest at the
  horizon and fading to 6% of that overhead, faded out by daylight. The PBR fog adds the same glow, so fogged
  silhouettes match the glowing sky behind them. The night ambient gains a small share of the colour
  (`NIGHT_AMBIENT_LIGHT_POLLUTION_SHARE`); the `NIGHT_AMBIENT_*` floor stays the dark-site look.
- **Brightness knobs** (tuned for the night exposure, the eye adaptation's maximum of 24): `STAR_RADIANCE_SCALE` in
  `WorldRendererStars.c`, `MILKY_WAY_BASE_RADIANCE` and `SKYGLOW_BASE_RADIANCE` in `WorldRendererSky.c`; per world
  `StarBrightness` and `MilkyWayBrightness` scale them.

**Moon.** The sky's model is pure maths in `WorldEnvironment` (`agents/WorldData.md`); the renderer draws it.
- **Phase and position.** The phase comes from the date (`WorldEnvironment_GetMoonPhase`: the real moon's by default,
  shifted by `MoonPhaseOffsetDays`, cycling every `MoonCycleDays`) or is fixed (`MoonPhaseSource`). The moon stands on
  the sun's path, east of it by 2pi x phase and `MoonOrbitTilt` x sin(2pi x phase) north of it, so the new moon is with
  the sun, the full moon opposite, the first quarter rises at noon, and it rises about 50 minutes later each day.
- **The disc** (`world_sky`, `WorldRendererMoonDisc` from `WorldRendererSky_PrepareFrame`): angular radius 0.26 degrees
  x `MoonSizeMultiplier` (at most 45 degrees), its axes toward the sky's pole (the face turns as it crosses the sky),
  shaded as a sphere lit by the sun with Lommel-Seeliger (normalized to 1 at full moon, which looks flat as the real one
  does; the terminator and the crescent face the sun), plus earthshine on the dark side (`MOON_EARTHSHINE_SHARE` x
  (1 - illumination)). The face is the texture the world names (`MoonTexture`, default `sky_moon`, point-filtered for
  crisp pixel art; a flat grey without it), tinted by `MoonColor`, at `MOON_DISC_BASE_RADIANCE` (the "MOON DISC KNOB") x
  `MoonBrightness`. The disc covers what lies behind it (the sun disc, the Milky Way, the stars) and is dimmed by the
  view ray's transmittance (an out parameter of the shared `atmosphere()`), with the sky's in-scatter added in front: so
  by day it is pale (a quarter moon is findable in the afternoon), at night bright, and reddened low over the horizon.
  It fades out within 6 to 12 degrees of the sun (lost in the glare; this also keeps the virtual moon from eclipsing
  the sun) and is skipped once below the horizon.
- **The moonlit sky.** The same atmosphere lit by the moon: x `MoonIntensity` x the phase's brightness x
  `MOON_SKY_SHARE` (the "MOONLIT SKY KNOB", 0.3: a dark slate-blue sky with a soft halo at full moon), faded out while
  the sun is above about 3 degrees and the moon below about -3 to -6 (outside that its raymarch is skipped). The PBR fog
  adds the same moonlit sky, so fogged silhouettes match the sky behind them.
- **Moonlight.** The second directional light of the PBR shader, with the sun's BRDF: `MoonIntensity` (default 0.02 of
  the sun, `WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY`: tuned on rendered frames so a full-moon night is clearly lighter
  than a new-moon one and still reads as night; 8% lit the test world nearly like day at the night exposure) x the
  phase's brightness (`WorldEnvironment_GetMoonBrightness`, the opposition-surge fit of Krisciunas and Schaefer: a half
  moon gives about a tenth of the full moon's light) x its height (full from about 11 degrees) x (1 - daylight), in
  `MoonColor`. The ambient gains the moonlit sky's fill (`MOON_AMBIENT_SHARE` 0.3 of the moonlight), the eye
  adaptation counts the moonlight like the sunlight. New-moon nights look as before.

**Night vision.** The eye's dim-light vision: in dim light the rods take over from the cones and colour fades into a
grey with a faint blue cast. A low moon's sky is as orange as a sunset's (moonlight is sunlight, scattered by the same
air; a long exposure shows it), but the eye sees it grey-blue, and a moonlit field loses its greens. The maths is the
private `source/NightVision.{h,c}` (tested in `tests/NightVisionTests.c`; its header explains the model), the GLSL
`shaders/common/night_vision.glsl`.
- **The model.** Hunt's rod sensitivity as Krawczyk, Myszkowski and Seidel use it (2005): the rods' share
  sigma = 0.04 / (0.04 + L) for a luminance L in cd/m^2, half and half at 0.04 (a bright moonlit scene). The game's
  light scale is compressed (its noon, a scene luminance of about 0.74, stands for 5000 cd/m^2, its moonless night,
  about 0.006, for 0.001 cd/m^2), so a game luminance Y is read as 5000 x (Y / 0.74)^3.2 cd/m^2, which makes
  sigma = 1 / (1 + (Y / 0.019)^3.2). A pixel is blended by sigma toward its luminance in the rods' grey: white shifted
  toward the night blue (CIE x, y = 0.25, 0.25, Jensen et al. 2001) by the world's `NightVisionBlueShift` (default
  0.5; the full blue looks like a film's day-for-night filter, Krawczyk's own tint reads violet), at the same luminance,
  so the night's tuned brightness does not change.
- **Which luminance.** The larger of the pixel's own and the field luminance: the frame's `BaseSceneLuminance`, the
  light the sky gives the whole scene (the sun, the moon, the ambient), without the nearby lamps eye adaptation adds.
  By day the field is far above 0.165, where sigma is below 1/1024, and the shaders return the colour untouched: day
  frames are bit for bit what they were. At night each pixel is seen by its own light: a lamp's bright pool keeps its
  colour, the dark beyond it greys. Lamps lower the exposure but do not raise the field: the rods adapt locally, so a
  lamp beside the camera darkens the dim sky but gives it no colour.
- **What goes through it.** The sky pass's glow (the in-scatter of the sun and the moon, the skyglow, the Milky Way;
  before the discs are added) and the PBR shader's reflected light (ambient, the sun and the moon, point lights) and
  its fog's sky. Not the emitters: the sun's and the moon's discs, the stars and emissive materials (the game draws
  emitters far dimmer than they are, and a real emitter that bright is seen in colour: the orange rising moon, a lit
  screen at night). Not yet the sprites (see below).
- **Switches.** The world's `NightVisionStrength` (1 the eye, 0 colourful nights: a long-exposure photo) and
  `NightVisionBlueShift`; the renderer toggle `WorldRenderer_SetNightVisionEnabled` (V in the test frame) for A/B.
  Cost: a `pow` per sky pixel and per lit fragment.
- **Not modelled.** The rods' slow dark adaptation (minutes; the vision follows the sky's light at once), a lit
  interior at night (its dark corners would grey: its light is not part of the field; revisit with interiors), the
  loss of sharpness in the dark. Sprites keep their colour for now: lit sprites (T06) must pass their reflected light
  through `nightVision` too.

**Sun and moon shadows.** Two camera-centred orthographic cascades (2048² depth textures): NEAR (small extent, sharp)
and FAR (large extent, coarse, optional). Each model component's `WorldShadowTier` (Both / Near / Far) routes which
cascade(s) it casts into, and one with `casts_shadows` off casts into none (nor into the point-light shadow). They are rendered from the frame's shadow light (`WorldEnvironment_GetSkyLights`): the
brighter of the sun and the moon by luminance, once it stands above 0.05 (the sine of its elevation; full shadows from
0.15), faded out while the two are about as bright (0 where they are equal, full once one is 4 times the other) and for
a light too dim for a shadow to show (a thin crescent). So dusk with the moon up goes from sun shadows through none to
moon shadows without a jump. The PBR shader samples near first, then far, with 2×2 PCF and a slope-scaled bias along
the shadow light, darkening only that light's direct term (`sunShadowFade` / `moonShadowFade`; both 0, and no map
sampled, while the cascades do not run). Active when shadows are enabled, the strength is above 0 and there is a
shadow light.

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
  applicable this frame) / unavailable (missing shader or target), and object counts (the star count included). The
  "stars" feature is inactive by day, while twilight, light pollution and the moonlit sky hide every star, and at
  density 0; the "Milky Way" is unavailable without its texture and inactive while it cannot be seen; "sun shadows" and
  "moon shadows" are the cascades cast by either light; "moon" is the disc (inactive below the horizon, near the sun or
  switched off; drawn flat grey without its texture), "moonlight" the moon's light on the world, "night vision" the
  eye's night vision (active while it changes something: dusk and night, with a strength above 0). The renderer logs the status
  once after its first render; `WorldRenderer_LogStatus` logs it again (the I key in the test frame).

### Renderer structure (keep it clean)

| File | What it holds |
|---|---|
| `WorldRenderer.c` | Public API: create/destroy, `PrepareWorld`, `RenderToTarget` (frame context, pass decisions, pass order), status. Orchestration only. |
| `WorldRendererInternal.h` | The renderer state (one state struct per pass), `WorldRenderFrame`, fixed texture units, shared constants, and every internal function, documented. |
| `WorldRendererCommon.c` | Config multipliers, colour conversion, shader loading, one-time warnings, render-target constructors. |
| `WorldRendererScene.c` | Scene target size, HDR tiers and recreation on resize (asks each pass to recreate its targets); the scene pass. |
| `WorldRendererGeometry.c` | Model meshes with per-draw materials, sprite billboards, light gizmos, the pixelation filter. |
| `WorldRendererLighting.c` | The PBR shader, its location table and uniforms (the night vision's among them), forward point lights, per-material factors, eye adaptation. |
| `WorldRendererShadows.c` | The shadow cascades of the sun or the moon, and the point-light cube shadow. |
| `WorldRendererGBuffer.c` | The normal/flag G-buffer and the AO pre-pass. |
| `WorldRendererSky.c` | The atmospheric sky with the moon, the moonlit sky, the Milky Way and light pollution's skyglow; the sky textures the world names; the night sky values and the moon's disc for the frame. |
| `WorldRendererStars.c` | The star field: the catalogue's mesh (rebuilt when the seed or density changes) and its draw (none behind the moon). |
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
is a second such pair. Each renderer shader lives in its own directory under `asset/shaders/`. GLSL shared between
shaders lives in `asset/shaders/common/` and is pulled in with `#include "common/<name>"` (no `#version` in shared
files; rules in GameAssets.md, "GameShader"): `common/math` (`PI`), `common/atmosphere` (Rayleigh + Mie single
scattering lit by the sun or the moon, with an overload that also gives the view ray's transmittance; used by
`world_sky` and the PBR fog), `common/night_sky` (air mass, extinction and the skyglow profile,
used by `world_sky`, `world_stars` and the PBR fog) and `common/night_vision` (the eye's night vision: the rods' share
and the blend into their grey; used by `world_sky` and `world_pbr`). Put new shared lighting, dithering or noise code there instead of
copying it between shaders.

## WorldTestFrame

Concrete `GameFrame` (embeds `GameFrame` first). Owns a `World` built with `World_Construct2` (so its sprite
components bind their animations themselves), a `GameClock` ([GameClock.md](GameClock.md)), a `GameCamera`, and a
`WorldRenderer`. `BuildTestWorld` builds the test world through the object API: `World_CreateObject`, the transform
setters, `World_AddComponent` and the components' setters (a model component per cube and for the ground slab, a sprite
component, two light components). Its `Update` runs the tick's animation step: it advances the sprites of enabled
objects (`World_GetComponents(world, WorldComponentType_Sprite)`).
**Game time:** the clock starts from the world environment's date and time of day (noon, 2026-10-18) at time scale
720 (a 2-minute day, as the test world had before T23; the clock's own default is 72). Every `Update` advances it,
logs a new day when it crosses midnight, and writes its time of day and date into the environment, so the sky shows
the clock.

All input runs in `Update` at the fixed timestep and comes from `Services.Input` ([GameInput.md](GameInput.md)), after
the UI test frame above had first refusal: movement through the actions (`move_*`, default WASD + SPACE/CTRL, `sprint`
default SHIFT), mouse-look (`GameInput_GetMouseDelta`, this tick's movement, only while the cursor is captured) and the
debug keys as raw keys:
- **the clock** (each change is logged with the date and time, e.g. `WorldTest: dawn (paused): Sun 2026-10-18
  06:00:00.`): P pause/resume the clock; 1–4 jump to dawn/noon/dusk/midnight of the clock's day (06:00, 12:00, 18:00,
  00:00) and pause; `[` `]` scrub the time while held (a tenth of a day per real second, across midnights, logged when
  let go); `,` and `.` the previous and next day, `/` a week ahead (the day keys keep the time of day);
- **the moon and the seasons:** F pins the moon full (`MoonPhaseSource` fixed at 0.5) and gives it back to the
  calendar; Y switches the seasons (`AreSeasonsEnabled`). Every clock and moon log line carries the moon: `WorldTest:
  next day: Mon 2026-10-19 12:00:00; first quarter (phase 0.27, 59% lit, from the calendar).`;
- **the renderer and the sky:** O post effects, B bloom, G sun shafts, C crisp overlay, V night vision (logged), L
  light gizmos, N cycle the
  light pollution (0, 0.35, 0.7, 1; logged), I log the renderer status, then the game time (`WorldTest: game time
  Sun 2026-10-18 12:00:00, running, 720 game seconds per real second (a day takes 2 real minutes); seasons off.`) and
  the moon;
- T toggles the text sample.

M frees the cursor for the UI (and captures it again); a left click the UI did not take captures it again. An
unconsumed `pause` (Escape once the UI is not using it) quits through `GameFrameManager_RequestStop`. `Render` handles
no input. Captures the cursor in `Start`, releases it in `End`/`Destroy`. The world and the clock are built in `Create`;
`BeginLoad` calls `WorldRenderer_PrepareWorld` (shaders and models), so the stepped load/unload slots are trivial.
`Create` builds everything up front so `Destroy` is always safe (even on forced teardown); it destroys the renderer,
then the world, the clock and the text sample.

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
