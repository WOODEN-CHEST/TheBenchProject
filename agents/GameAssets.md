# Game asset wrappers (GameFont, GameModel, GameShader, GameSound)

Files: `include/GameFont.h`, `include/GameModel.h`, `include/GameShader.h`, and `GameSound` in `include/SoundEngine.h`.
Their loaders: `source/FontDefinition.c`, `source/ModelDefinition.c`, `source/ShaderDefinition.c`,
`source/SoundDefinition.c`.

## Purpose

The wrapper types the AssetManager hands out for the standard font, model, shader and sound types. Each wraps one
raylib (or audio-engine) object **by value**. Sprite sheets and sprite animations have their own docs
([SpriteSheet.md](SpriteSheet.md), [SpriteAnimation.md](SpriteAnimation.md)); the definition JSON is in
`references/asset_structure.md`.

## Rules for all four

- **Get one** with `AssetManager_LoadFont`, `AssetManager_LoadModel`, `AssetManager_LoadShader` or
  `AssetManager_LoadSound`, under your own `AssetUserID` ([AssetManager.md](AssetManager.md)).
- **Borrowed:** the manager owns the wrapper and everything in it. The pointer is valid until the last holder releases
  the asset (or `AssetManager_Deconstruct` runs); then the wrapper memory itself is freed.
- **Never unload or free** anything reached through a wrapper: no `UnloadFont`, `UnloadModel`, `UnloadMaterial`,
  `UnloadTexture`, `UnloadShader`, `UnloadWaveSamples` or `GameSound_Deconstruct` on it.
- **Don't keep it past your user:** neither the pointer nor a by-value copy of the wrapper or of the raylib struct in
  it. A copy still uses the asset's GPU ids and heap arrays.
- **Shared and read-only:** every holder sees the same object, so writing through a copy's pointers (material maps,
  shader `locs`, font glyphs) changes it for everyone.
- Main thread only. Fonts, models and shaders need the window's GL context to load; sounds do not.

## GameFont (`GameFont.h`)

- `typedef struct GameFontStruct { Font _rayFont; } GameFont;`. The header has no accessor and no doc comments; code
  reads `_rayFont` directly (for example `TextRenderArguments.TargetFont = font._rayFont`).
- Loaded with `LoadFontEx(path, 32, NULL, 0)`: a 32 px base size and raylib's default glyph set, the 95 printable ASCII
  codepoints (32-126). Any other character draws as `?`. Files: `ttf`, `otf`. No texture filter is set. Unloaded with
  `UnloadFont`.
- The text code also builds `GameFont` values around `GetFontDefault()`, raylib's built-in font, which raylib owns
  until `CloseWindow`. Those are not assets and must not be unloaded either. Before `InitWindow`, `GetFontDefault()`
  returns an empty font.
- **Known problem (Phase 4):** text components keep fonts **by value**. `TextComponentResolver_ResolveTree` copies the
  loaded font into `StringComponent._font` (`StringComponent_SetFont(component, *Loaded)`), and `TextStyle._font`
  stores one too. These copies dangle once the resolving user releases the font. Until Phase 4 replaces them with a
  `TextDocument` and names-only references, keep that user's holds for as long as any component or style holds a copy.

## GameModel (`GameModel.h`)

- `GameModel { Model _rayModel; }`. `GameModel_GetRaylibModel(self)` returns the raylib `Model` **by value**, but its
  `meshes`, `materials`, `meshMaterial` and each material's `maps` still point into the asset: changing the copy's
  `transform` is local, writing through those pointers changes the shared asset.
- Loading (`ModelDefinition.c`): `LoadModel` on the resolved file (`obj` `gltf` `glb` `iqm` `vox` `m3d`); a result with no
  meshes fails the load. Then:
  - the definition's `transform` (scale, `rotation_euler` in degrees, translation) is baked into `model.transform`;
  - every material gets PBR defaults where the importer left a value at or below 0: roughness 0.5, occlusion 1.0;
  - `material_overrides` apply by material **index** (`slot`); a slot beyond the model's material count is skipped
    silently. Override maps go to `albedo` -> `MATERIAL_MAP_ALBEDO`, `normal` -> `MATERIAL_MAP_NORMAL`, `mra` (packed
    occlusion/roughness/metallic) -> `MATERIAL_MAP_METALNESS`, `emissive` -> `MATERIAL_MAP_EMISSION`, loaded from the
    models directory with the texture extensions. Scalars: `tint`, `metallic`, `roughness`, `emissive_color`,
    `emissive_intensity`.
- **Ownership:** the asset owns the meshes, the materials and their map arrays, and every texture its materials
  referenced at load (the importer's textures plus the overrides; raylib's default texture is skipped). Importer
  textures that an override replaced are unloaded during the load. On unload each tracked texture id is unloaded
  (`rlUnloadTexture`), then `UnloadModel` runs. The asset never owns a material's **shader** and never calls
  `UnloadMaterial`, which would also unload the shader the renderer bound.
- **Known violation (Phase 3):** the WorldRenderer writes its pass shader into the shared materials and the binding
  persists, so other code drawing the same model (for example `RenderContext_RenderModel`) shades with whichever pass
  shader was bound last.
- **Imported textured models shade wrong:** glTF's metallic texture and OBJ's `map_Ks` land in the METALNESS slot,
  which the PBR shader reads as packed ORM (red = occlusion). Normalising imported materials is deferred
  (`handoff/FIX_PLAN.md`, "Not in this pass").
- No skeletal animation; the `rig` key in the reference is not read.
- `GameMesh { Mesh _rayMesh; }` is declared in the same header, but the manager does not produce it and nothing uses
  it yet. Whoever builds one releases it with `UnloadMesh`.

## GameShader (`GameShader.h`)

- `GameShader { Shader _rayShader; }`. `GameShader_GetRaylibShader(self)` returns the `Shader` by value; its `locs`
  array belongs to the asset.
- Loading (`ShaderDefinition.c`): `vertex_location` and/or `fragment_location`, at least one (a bare `location` is the
  fragment stage when `fragment_location` is absent). The sources are read as text through `AssetManager_OpenResource`
  (stage extensions `vert` `frag` `vs` `fs` `glsl`, so the two stage files need distinct stems) and compiled with
  `LoadShaderFromMemory`; a missing stage uses raylib's default. Unloaded with `UnloadShader`.
- A stage that fails to compile fails the load (`ErrorCode_InvalidAssetData`). A program that compiles but fails to
  **link** comes back from raylib as raylib's default shader, so the load succeeds and only raylib's own trace log
  mentions it.
- Uniform values live in the GL program, which every holder shares: set the values you rely on before each use.
  Don't write into `locs` (the WorldRenderer's `EnsureShaders` still does for `world_pbr`, a known violation for
  Phase 3).
- Known leak: on a compile failure the loader does not call `UnloadShader`, so the small `locs` array raylib allocated
  for the failed shader is lost (once per failure, since failures are cached).

## GameSound (`SoundEngine.h`)

- `GameSound { _samples, _sampleCount, _format }`: borrowed interleaved float samples, the total sample count (frames x
  channels), and an `AudioFormat` (`SampleRate`, `ChannelCount` within [1, `MAX_AUDIO_CHANNELS`] = [1, 2]).
  `GameSound_Construct1` validates and borrows the samples; `GameSound_Deconstruct` frees nothing.
- Loading (`SoundDefinition.c`): `LoadWave` (`wav` `ogg` `mp3` `qoa`), 1 or 2 channels, decoded to normalized floats
  with `LoadWaveSamples`. The loaded asset owns the samples (`UnloadWaveSamples` on unload) and the `GameSound` borrows
  them. Decoding does not touch the audio device.
- The audio engine is disabled (`AudioEngine_Construct1` always fails) and nothing loads sounds yet; see
  [SoundEngine.md](SoundEngine.md). Releasing a sound frees its samples immediately, which is why the planned engine
  holds sounds under its own asset user.
