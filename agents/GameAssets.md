# Game asset wrappers (GameFont, GameModel, GameShader, GameSound, GameTexture)

Files: `include/GameFont.h`, `include/GameModel.h`, `include/GameShader.h`, `include/GameTexture.h`, and `GameSound`
in `include/SoundEngine.h`. Their loaders: `source/FontDefinition.c`, `source/ModelDefinition.c`,
`source/ShaderDefinition.c` (with the private `#include` expander `source/ShaderSource.{h,c}`),
`source/SoundDefinition.c`, `source/TextureDefinition.c`.

## Purpose

The wrapper types the AssetManager hands out for the standard font, model, shader, sound and texture types. Each wraps
one raylib (or audio-engine) object **by value**. Sprite sheets and sprite animations have their own docs
([SpriteSheet.md](SpriteSheet.md), [SpriteAnimation.md](SpriteAnimation.md)); the definition JSON is in
`references/asset_structure.md`.

## Rules for all five

- **Get one** with `AssetManager_LoadFont`, `AssetManager_LoadModel`, `AssetManager_LoadShader`,
  `AssetManager_LoadSound` or `AssetManager_LoadTexture`, under your own `AssetUserID` ([AssetManager.md](AssetManager.md)).
- **Borrowed:** the manager owns the wrapper and everything in it. The pointer is valid until the last holder releases
  the asset (or `AssetManager_Deconstruct` runs); then the wrapper memory itself is freed.
- **Never unload or free** anything reached through a wrapper: no `UnloadFont`, `UnloadModel`, `UnloadMaterial`,
  `UnloadTexture`, `UnloadShader`, `UnloadWaveSamples` or `GameSound_Deconstruct` on it, and no `SetTextureFilter` /
  `SetTextureWrap` on its textures (every holder shares them).
- **Don't keep it past your user:** neither the pointer nor a by-value copy of the wrapper or of the raylib struct in
  it. A copy still uses the asset's GPU ids and heap arrays.
- **Shared and read-only:** every holder sees the same object, so writing through a copy's pointers (material maps,
  shader `locs`, font glyphs) changes it for everyone.
- Main thread only. Fonts, models, shaders and textures need the window's GL context to load; sounds do not.

## GameFont (`GameFont.h`, `source/GameFont.c`)

- `GameFont { Font _rayFont; bool _areGlyphsSorted; int32_t _fallbackGlyphIndex; Vector2 _shadowOffset; }`. Code reads
  `_rayFont` for the raylib calls that take a font. `GameFont_GetShadowOffset` is where text in the font draws its
  shadow when the text sets no offset (a fraction of the text size): the definition's `shadow_offset`, else
  `GAME_FONT_DEFAULT_SHADOW_OFFSET_X/Y` ([-0.1, 0.1], also raylib's built-in font's).
- Lookups: `GameFont_FindGlyphIndex(font, codepoint)` returns the glyph that draws a codepoint, or the `?` glyph when
  the font lacks it (-1 without either). It binary-searches when the glyphs are sorted by codepoint, which every font
  asset's are; `GameFont_HasGlyph` tells whether the font has the codepoint itself; `GameFont_GetGlyphAdvance` is the
  pen advance in font pixels, measured the way raylib measures text; `GameFont_HasGlyphs` is false for an empty font.
  `GameFont_Wrap(font)` builds the lookup data for a raylib font (the loader uses it, and `TextDocument` uses it for
  raylib's built-in font).
- Loading (`FontDefinition.c`): `LoadFontEx(path, size, codepoints, count)` with the definition's `size` (default 32 px)
  and codepoint set (default: Basic Latin, Latin-1 Supplement and Latin Extended-A, `?` and the space always included; parsed by the
  private `source/FontCodepoints.c`), then `SetTextureFilter` from its `texture_properties` (default bilinear), and the
  definition's `shadow_offset` (validated like a component's: each part within +-10). Files:
  `ttf`, `otf`. The definition keys are in `references/asset_structure.md`. When the file lacks requested codepoints,
  the loader logs one WARNING through `AssetManager_GetLogger` naming how many and the first few (they draw as `?`).
  Unloaded with `UnloadFont`.
- raylib's built-in font (`GetFontDefault()`) is not an asset: raylib owns it until `CloseWindow`, it must not be
  unloaded, and before `InitWindow` it is empty.
- Text never holds a `GameFont`: components name fonts and their `TextDocument` holds them under its own asset user
  ([TextDocument.md](TextDocument.md)).
- Shipped fonts: `fonts/test/newscrash.otf` (`test`; has only `š` and `ž` of the Latvian letters and no Cyrillic) and
  `fonts/noto_sans/NotoSans-Regular.ttf` (`noto_sans`: Noto Sans Regular from Ubuntu's fonts-noto-core 20201225, SIL
  Open Font License 1.1, licence in `OFL.txt` next to it; 48 px with Basic Latin, Latin-1, Latin Extended-A and
  Cyrillic). A test checks that the Noto file covers every codepoint its definition asks for.

## GameModel (`GameModel.h`)

- `GameModel { Model _rayModel; }`. `GameModel_GetRaylibModel(self)` returns the raylib `Model` **by value**, but its
  `meshes`, `materials`, `meshMaterial` and each material's `maps` still point into the asset: changing the copy's
  `transform` is local, writing through those pointers changes the shared asset.
- Loading (`ModelDefinition.c`): `LoadModel` on the resolved file (`obj` `gltf` `glb` `iqm` `vox` `m3d`); a result with no
  meshes fails the load. Then:
  - the definition's `transform` (scale, `rotation_euler` in degrees, translation) is baked into `model.transform`;
  - every material gets PBR defaults where the importer left a value at or below 0: roughness 0.5, occlusion 1.0;
  - `material_overrides` apply by material **index** (`slot`, 0 to `INT32_MAX`); a slot beyond the model's material
    count is skipped silently. Override maps go to `albedo` -> `MATERIAL_MAP_ALBEDO`, `normal` ->
    `MATERIAL_MAP_NORMAL`, `mra` (packed occlusion/roughness/metallic) -> `MATERIAL_MAP_METALNESS`, `emissive` ->
    `MATERIAL_MAP_EMISSION`, loaded from the models directory with the texture extensions. Scalars: `tint`, `metallic`
    and `roughness` (in [0, 1]), `emissive_color`, `emissive_intensity` (>= 0). The slot's `texture_properties`
    (filtering, wrap, mipmaps; `TextureProperties_Apply`) apply to each of its maps, so distant textured models can use
    mipmaps and shimmer less.
  - **The factors are shared with world model variants** (T10, [WorldModels.md](WorldModels.md)): the slot and the
    scalars are read by `AssetJSON_ReadMaterialSlot` and `AssetJSON_ReadMaterialFactors`
    (`source/AssetDefinitionCommon.{h,c}`) into a `GameMaterialFactors` (`GameModel.h`: each factor with its own
    `Has...` flag), and applied by `GameMaterialFactors_Apply(factors, hasEmissiveMap, maps)` (`source/GameModel.c`):
    here to the model's own materials while it loads, for a variant to the renderer's per-draw copy. An override with an
    emissive map but no emissive colour or intensity shows the map at white x 1. Since T10 the shared parser enforces
    the ranges the reference always gave (metallic and roughness in [0, 1], a non-negative intensity, finite numbers
    that fit a float).
- **Ownership:** the asset owns the meshes, the materials and their map arrays, and every texture its materials
  referenced at load (the importer's textures plus the overrides; raylib's default texture is skipped). Importer
  textures that an override replaced are unloaded during the load. On unload each tracked texture id is unloaded
  (`rlUnloadTexture`), then `UnloadModel` runs. The asset never owns a material's **shader** and never calls
  `UnloadMaterial`, which would also unload the material's shader, and a texture shared by two materials twice.
- **Read-only:** never write into the materials (shader, tint, maps). The WorldRenderer draws each mesh with a
  `Material` built on the stack (the asset's maps copied, its own pass shader, the tint applied to the copy).
- **Local bounds:** `GameModel_GetLocalBounds` returns the mesh-space bounding box (before the import transform),
  computed once at load, so users never walk the vertices themselves.
- **Imported textured models shade wrong:** glTF's metallic texture and OBJ's `map_Ks` land in the METALNESS slot,
  which the PBR shader reads as packed ORM (red = occlusion). Normalising imported materials is deferred
  (`handoff/2026-10_code_review/FIX_PLAN.md`, "Not in this pass").
- No skeletal animation; the `rig` key in the reference is not read.
- `GameMesh { Mesh _rayMesh; }` is declared in the same header, but the manager does not produce it and nothing uses
  it yet. Whoever builds one releases it with `UnloadMesh`.

## GameShader (`GameShader.h`)

- `GameShader { Shader _rayShader; }`. `GameShader_GetRaylibShader(self)` returns the `Shader` by value; its `locs`
  array belongs to the asset.
- Loading (`ShaderDefinition.c`): `vertex_location` and/or `fragment_location`, at least one (a bare `location` is the
  fragment stage when `fragment_location` is absent). The sources are read as text through `AssetManager_OpenResource`
  (stage extensions `vert` `frag` `vs` `fs` `glsl`, so the two stage files need distinct stems), their `#include`
  lines are expanded (below), and they are compiled with `LoadShaderFromMemory`; a missing stage uses raylib's
  default. Unloaded with `UnloadShader`.
- A stage that fails to compile, and a program that fails to link (raylib would hand back its default shader), both
  fail the load (`ErrorCode_InvalidAssetData`); the compiler output is in raylib's own log. raylib's location array is
  freed on that path (`UnloadShader`), so a failure leaks nothing.
- **`#include` (`source/ShaderSource.{h,c}`, private, tested in `tests/ShaderSourceTests.c`).** A line
  `#include "common/atmosphere"` is replaced by that file's text: a file location under `shaders/` resolved like every
  other (so packs can override it), with the stage extensions. Shared files sit in `shaders/common/` without a JSON
  definition. Rules: at most 8 levels deep; a file included twice in one stage is skipped (implicit include guards;
  the main file counts); no `#version` in an included file and no `#include` before the main file's `#version`;
  every other line passes through; it works line by line before the GLSL preprocessor, so it does not see comments or
  `#if` blocks (keep includes out of them); a NUL byte is an error. Each included file is a GLSL source string of its
  own: the expander writes `#line 1 <n>` before its text and `#line <next line> <parent>` after it (GLSL 3.30
  semantics), and a skipped repeat becomes an empty line, so `1(27)` / `1:27(26)` in the compiler output means line 27
  of file 1. When such a stage fails to compile, the load error lists which number is which file. A missing include
  fails with `ErrorCode_InvalidAssetData` naming the include, the including file and the line (cached like every
  failure). Shared files today: `common/math` (`PI`), `common/atmosphere` (the sky and the lit shaders' fog share it),
  `common/night_sky` (air mass, extinction, the skyglow profile: the sky, the stars and the lit shaders' fog),
  `common/night_vision` (the eye's dim-light vision: the sky and the lit shaders), `common/lighting` (the world's
  lighting: the PBR and the sprite shaders; it includes the four before it) and `common/distance_packing` (the
  point-light shadow cube's packed distances). A stage file may be shared too: the four sprite shaders' definitions all
  name `sprite/vertex`.
- Uniform values live in the GL program, which every holder shares: set the values you rely on before each use.
  Don't write into `locs`: copy the table if you need different entries (the WorldRenderer keeps its own copy for
  `world_pbr`, `_pbr._locs`, with the material map samplers patched).

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

## GameTexture (`GameTexture.h`)

- `GameTexture { Texture2D _rayTexture; bool _isSrgb; }`. `GameTexture_GetRaylibTexture(self)` returns the `Texture2D`
  by value (its id, size, mip count and format); `GameTexture_IsSrgb(self)` tells colour (`color_space` `srgb`, the
  default) from linear data (`linear`: normal maps, masks, noise). The flag is metadata for the shader: the GPU format
  is the same, and shaders linearize sRGB textures themselves.
- For standalone textures: sky panoramas, the moon, noise, normal maps, masks. Sprites keep coming from sprite sheets
  and sprite animations, model maps from model definitions.
- Loading (`TextureDefinition.c`): the file location (`png` `bmp` `gif` `qoi` `dds`; references are rejected when the
  definition is read) is decoded with `LoadImage` first, so an image over 8192 pixels per side fails before anything
  reaches the GPU; then `LoadTextureFromImage` and `TextureProperties_Apply` (`source/AssetDefinitionCommon.c`):
  mipmaps generated when `texture_properties.mipmaps` is true and the file brought none (a compressed dds without its
  own mip levels fails the load: raylib would count a mip chain the GPU never built), then the wrap mode, then the
  filter (last, because raylib picks the mipmapped GL filter from the mip count). Unloaded with `UnloadTexture`.
- Shipped: `sky_milky_way` (`textures/sky/milky_way.json`), a generated placeholder panorama
  (`placeholder_milky_way.png`, 1024x512, trilinear, mipmaps) until the owner's art replaces it; the sky pass draws it
  ([WorldRuntime.md](WorldRuntime.md), "Night sky").
