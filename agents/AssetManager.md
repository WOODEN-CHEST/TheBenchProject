# AssetManager module

Files: `include/AssetManager.h`, `source/AssetManager.c`. Standard asset types: `source/AssetTypesStandard.h`
(type names and directories), `source/AssetDefinitionCommon.h/.c` (shared JSON helpers and the accepted extension
sets), and one loader per type: `source/SpriteSheetDefinition.c`, `source/SpriteAnimationDefinition.c`,
`source/SoundDefinition.c`, `source/FontDefinition.c`, `source/ShaderDefinition.c` (with its `#include` expander
`source/ShaderSource.{h,c}`), `source/ModelDefinition.c`, `source/TextureDefinition.c`, `source/WorldModelDefinition.c`.
The definition JSON format is specified in `references/asset_structure.md`.

## Purpose

Loads, shares and unloads game assets. The manager stores **definitions** (named recipes, normally JSON files
under the asset search roots) and builds an asset from its definition the first time someone loads it. Asset
types are registered at runtime (`AssetManager_CreateAssetType`); the twelve standard types (sprite sheet, sprite
animation, sound, font, shader, model, texture, world model, world sprite, character, animation library, animation
graph) are
registered together by `AssetManager_CreateStandardAssetTypes`. The wrappers it hands out are documented in
[SpriteSheet.md](SpriteSheet.md), [SpriteAnimation.md](SpriteAnimation.md), [GameAssets.md](GameAssets.md),
[WorldModels.md](WorldModels.md), [WorldSprites.md](WorldSprites.md), [Characters.md](Characters.md) and
[Animation.md](Animation.md).

## Lifetime rules (read first)

### How assets are handed out
- A load returns a **raw borrowed pointer** to a manager-owned wrapper (`GameModel*`, `SpriteSheet*`, ...). Every
  holder of the same asset gets the same pointer.
- Holds are **per `AssetUserID` and presence-based**: a user holds an asset or it does not. Loading the same asset
  twice under one user is still one hold, and one `AssetManager_ReleaseAsset` drops it.
- When the **last holder releases**, the asset is unloaded **synchronously**, inside that release call: its GPU
  objects are unloaded and its wrapper memory is freed. Every pointer or by-value copy anyone kept now dangles.
- There are **no handles and no generation checks**: nothing detects a stale pointer.
- There is **no reload**. While an asset is loaded, loads keep returning it unchanged, even after its definition is
  replaced or removed. New data appears only after every holder released it and it is loaded again.
- `AssetManager_Deconstruct` unloads every asset regardless of holders, so every asset user must be finished first
  (`main.c` tears down the frame manager before the asset manager).

### The project rule (handoff/2026-10_code_review/FIX_PLAN.md, decision D3)
**Whatever keeps asset data owns its own `AssetUserID`, and nothing stores asset pointers or by-value copies
(a `GameFont`, a raylib `Model`/`Texture2D`/`Font`/`Shader`, a `SpriteAnimationInstance` source) past that user.**
- Give each owning object (a frame, the world renderer, a World built with an asset manager, each TextDocument;
  later the audio engine) its own user. Mint it when the owner is created and retire it when the owner is destroyed, also on forced teardown.
- Load what you keep under your own user, even if another user already loaded it: that only adds your hold, with
  no I/O.
- Hand an asset to another object only for the duration of a call. A receiver that keeps it must load its own hold
  (by name).

How the world code follows it: a `World` built with `World_Construct2` owns a user and binds its sprites' animations
under it when they are added (the renderer only reads frames); the `WorldRenderer` holds its shaders under one user
and the world's models under another, which `WorldRenderer_PrepareWorld` swaps to release unused models.

Text follows it the same way: components only name fonts and sprite animations, and each `TextDocument` loads them
under its own user and keeps them until it is destroyed ([TextDocument.md](TextDocument.md)). (Until FIX_PLAN Phase 4
the text code copied fonts by value into components; that is gone.)

### Assets are shared and read-only
- Never write to an asset, including through the arrays a by-value copy still points at (`Model.materials`,
  `Material.maps`, `Model.meshes`, `Shader.locs`, `Font.glyphs`). Put per-use state (tint, transform, pass shader)
  into your own copies, for example a `Material` built on the stack.
- The WorldRenderer draws every mesh with a `Material` built on the stack and keeps its own copy of the PBR shader's
  location table, so it writes neither. `RenderContext_RenderModel` (raylib's `DrawModelEx`, used only by the dormant
  UI) still writes the tint into the shared maps for the duration of a draw; it is marked `// TODO:`.

### Threading
- **Main thread only.** The manager does no locking, and raylib loading must run on the GL thread.
- Loading sprite sheets, sprite animations, fonts, shaders, models and textures needs the window (GL context); reading
  definitions does not. Sounds decode without the GL context or the audio device.

## Users

| Call | Effect |
|---|---|
| `AssetManager_GetNewUserID` | Mints a new id (never `ASSET_USER_ID_INVALID`, never reused). |
| `AssetManager_ReleaseAsset(self, type, name, user)` | Drops that user's hold on one asset and unloads it if no holder is left. `ErrorCode_InvalidOperation` if the asset is not loaded or the user does not hold it. |
| `AssetManager_ReleaseAllAssetsForUser(self, user)` | Drops every hold of the user; each asset left without holders is unloaded. The id stays valid for new loads. |
| `AssetManager_RetireUser(self, user)` | The same release, then deactivates the id: later loads, release-alls, retires and bulk operations with it fail with `ErrorCode_IllegalArgument`. Call it when the owner is destroyed. |

- Loads and release-alls reject an id that was never minted or was retired (`ErrorCode_IllegalArgument`).
- Because holds are not counted, two parts of one owner that load and release the same asset independently release
  it for each other. Give each independently releasing owner its own user.
- Each loaded asset also gets an internal **dependency user**. The assets it depends on (a sprite animation's sprite
  sheets) are held under that user and released right after the asset itself is destroyed.

## Usage

Setup (`SetUpAssetManager` in `main.c`):
1. `AssetManager_Construct1(logger, &Manager)`. The logger is borrowed and may be NULL.
2. `AssetManager_CreateStandardAssetTypes(Manager, &Types, &JsonPool)`. The caller owns `JsonPool` and deconstructs
   it after the manager (`JSONObjectPool_Deconstruct`).
3. `AssetManager_SetCacheDirectory(Manager, "<base>/.wr_asset_cache")` (see "Cache directory").
4. `AssetManager_AddSearchRoot(Manager, "<base>/asset")`.
5. `AssetManager_ReadDefinitions(Manager, NULL)`.

`<base>` is `Services.BaseDirectory`: the executable's directory (`FileSystem_GetExecutableDirectory`), or the
working directory if the OS cannot report it. Frames reach the manager and the standard type ids through
`Services.Assets` and `Services.AssetTypes`.

An owner:

```c
AssetUserID User = ASSET_USER_ID_INVALID;
Error Result = AssetManager_GetNewUserID(Assets, &User);
if (Result.Code != ErrorCode_Success)
{
    return Result;
}

GameModel* TestModel = NULL;
Result = AssetManager_LoadModel(Assets, u8"test", User, &TestModel);
if (Result.Code != ErrorCode_Success)
{
    // Already logged once by the manager. Degrade (skip drawing) instead of failing the frame.
    Error_Deconstruct(&Result);
    TestModel = NULL;
}

// ... use TestModel while User holds it; never store it anywhere that outlives User ...

Error RetireResult = AssetManager_RetireUser(Assets, User); // releases every hold of User
Error_Deconstruct(&RetireResult);
```

The typed loaders (`AssetManager_LoadSpriteSheet`, `LoadSpriteAnimation`, `LoadSound`, `LoadFont`, `LoadShader`,
`LoadModel`, `LoadTexture`, `LoadWorldModel`, `LoadWorldSprite`, `LoadCharacter`, `LoadAnimationLibrary`) return
`ErrorCode_InvalidState` if the standard types were never
registered. Custom types use
`AssetManager_LoadAssetSingle(self, typeId, name, user, &AssetPointer)` with a `void*` result.

## Definitions

- **Names** match `[a-z][a-z0-9_.-]*` (`AssetManager_IsValidAssetName`), checked at registration whatever the source.
  A name is unique within its type; different types may reuse it.
- **Search roots** form an ordered list; index 0 has the highest priority. `AssetManager_AddSearchRoot` appends,
  `AssetManager_InsertSearchRoot` inserts at an index, `AssetManager_RemoveSearchRoot` removes. A resource pack or mod
  that overrides base assets inserts its root at index 0.
- **`AssetManager_ReadDefinitions(self, &FailedFileCount)`** walks `<root>/<type directory>/` recursively for each root
  (in priority order) and each registered type. When the type has a definition-file extension (`json` for the
  standard types), only files with it are definitions and everything else there is a resource; without one, every
  file is parsed as a definition.
  - Each file is handled on its own. A file that cannot be read, does not parse, fails its constructor or cannot be
    registered is **skipped**: one ERROR (`Skipped <type> definition "<path>": <reason>`), counted in
    `*outFailedFileCount`, and the scan goes on. If any were skipped, one WARNING with the count follows.
  - It returns success even when files were skipped. A returned error means some directory could not be scanned
    (logged as `Could not scan <type> definitions in "<dir>": ...`) or an internal failure; the rest of the scan still
    ran. A root without a directory for some type is not an error.
  - Same type and name **in one root**: the first file found stays registered, and every later one is skipped with an
    error naming both files. Directory order decides which is first, so fix the duplicate.
  - Same type and name **in different roots**: the higher-priority root wins silently (the override mechanism).
  - **Calling it again** replaces definitions read by an earlier call with this call's winners, so a newly inserted
    root's overrides apply. It never removes a definition whose file is gone. Definitions registered in code always
    win: a file never replaces one (the file's definition is dropped silently).
- **`AssetManager_SetDefinition`** registers a definition built in code (its `Type` must be a registered type) and
  replaces any existing definition with the same type and name, whatever its source. It **always takes ownership**:
  on every failure (invalid name, unregistered type, NULL manager) the definition is destroyed before the call
  returns. The one exception is a definition whose `VTable` or `VTable->Destroy` is NULL: it cannot be destroyed, so
  it stays with the caller (`ErrorCode_IllegalArgument`).
- `AssetManager_BuildDefinition` runs a type's constructor on raw bytes without registering (for non-filesystem
  sources). The caller owns the result until `AssetManager_SetDefinition` or `definition->VTable->Destroy(definition)`.
- `AssetManager_RemoveDefinition` removes a definition. An asset already built from it stays loaded and keeps being
  returned by loads until its last holder releases it. `AssetManager_HasDefinition` reports presence.

## Resolving resources

Loaders never open files themselves. They call `AssetManager_OpenResource` (a stream; close it with
`AssetManager_CloseResource`, not `IOStream_Deconstruct`) or `AssetManager_AcquireResourcePath` (a real path for
raylib's file loaders; release it with `AssetManager_ReleaseResourcePath`), passing the extensions they can decode as
an `AssetExtensionFilter`.

### File locations
A file location is a relative path **without extension**, resolved under `<root>/<type directory>/`. One leading `/`
or `\` is ignored. `references/asset_structure.md` forbids `.` and `..` segments, but the resolver does not check it.

The target is the location's **last entry name**, dots included: `ships/ship` looks for `ship.*`, and `a/b.c` looks
for `b.c.*` and never matches `b.png`. In each root, in priority order, the candidates are the files directly in the
location's directory (no recursion) that:
- have the target as their stem (file name minus its last extension);
- do not carry the type's definition-file extension (so `test.json` and `test.png` can share a stem);
- have an extension the loader's filter accepts (ASCII case-insensitive).

The **first root with any candidate decides**:
- 1 candidate: that file is used.
- 2 or more: `ErrorCode_InvalidAssetDefinition` ("Location ... is ambiguous"), listing them.
- 0: the next root is tried. If no root has one, the error is `ErrorCode_InvalidAssetData` when files with the right
  stem but unaccepted extensions exist (it lists them and the accepted set), otherwise `ErrorCode_FileNotFound`.

Accepted extensions (`source/AssetDefinitionCommon.c`):

| Filter | Used for | Extensions |
|---|---|---|
| `ASSET_EXTENSIONS_TEXTURE` | standalone textures, sprite sheet images, texture animation frames, model material override maps | `png` `bmp` `gif` `qoi` `dds` |
| `ASSET_EXTENSIONS_MODEL` | models | `obj` `gltf` `glb` `iqm` `vox` `m3d` |
| `ASSET_EXTENSIONS_FONT` | fonts | `ttf` `otf` |
| `ASSET_EXTENSIONS_SOUND` | sounds | `wav` `ogg` `mp3` `qoa` |
| `ASSET_EXTENSIONS_SHADER_STAGE` | both shader stages and the files they `#include` | `vert` `frag` `vs` `fs` `glsl` |

These are what the shipped raylib decodes through each loader's call (it has no jpg, tga, psd or hdr readers; `.fnt`
and `xm`/`mod` are only read by raylib functions the loaders do not use). Both shader stages accept every stage
extension, so a shader's two source files need distinct stems (`pbr/vertex` + `pbr/fragment`).

**`format`** (models, fonts and sounds only) is an optional extension without the dot. It is validated when the
definition is read; anything outside the loader's set rejects the definition. For a file location it narrows the
filter to that one extension (`AssetFormat_SelectFilter`). For a reference location it is required.

### Reference locations
A reference location names in-memory data instead of a file: the bytes registered with
`AssetManager_SetReferenceBytes` (copied; registering a name again replaces them), otherwise the stream returned by
the resolver set with `AssetManager_SetReferenceResolver`, otherwise `ErrorCode_InvalidOperation`.
- `AssetManager_OpenResource` returns a memory stream over a copy of the bytes. Shader stages load this way, so their
  references need no `format`.
- `AssetManager_AcquireResourcePath` writes the bytes to a temp file `<cache directory>/temp_<n>.<ext>`, where `<ext>`
  is its `preferredExtension` argument (the standard loaders pass the definition's `format`); releasing the path
  deletes the file. The data must be one self-contained file (a temp
  file cannot find siblings such as `.mtl` or `.bin`). Models, fonts and sounds load this way, so a reference there
  needs `format`, which is checked when the definition is read.
- Texture locations (standalone textures, sheet images, texture frames, material override maps) have no `format` key,
  so they must be file paths; a reference is rejected when the definition is read.

## Failed-load cache

- **What is cached:** when a definition's `LoadAsset` fails, its error (code and message) is stored on the definition
  record and logged once as an ERROR: `Failed to load <type> "<name>" (defined in "<path>"): <reason>` (or
  `(defined in code)`). Nothing else is cached: a missing definition, an invalid user, or a failure after a successful
  build is reported on every call.
- **A cached hit** returns the same error code with a new message that quotes the original ("failed to load earlier
  (not retried until definitions or roots change)"), with no disk I/O and no log line.
- **What clears it** (for every definition at once): `AssetManager_CreateAssetType` (so also
  `CreateStandardAssetTypes`), `AssetManager_RemoveAssetType`, `AssetManager_AddSearchRoot`, `InsertSearchRoot`,
  `RemoveSearchRoot`, `AssetManager_SetReferenceResolver`, `SetReferenceBytes`, `AssetManager_ReadDefinitions`,
  `AssetManager_SetDefinition`, `AssetManager_RemoveDefinition`, and `AssetManager_ClearLoadFailures` (a manual retry,
  e.g. after fixing a file on disk while the game runs). A replaced definition also drops its own entry.
  `AssetManager_SetCacheDirectory` and edits on disk do not clear it.
- A failed dependency fails both assets: when a sprite animation's sheet fails, both are cached and logged.

## Cache directory

Only reference resources loaded through `AssetManager_AcquireResourcePath` use it.
- **Construction does not touch the filesystem.** The default path is `.wr_asset_cache`, relative to the working
  directory.
- `main.c` points it at `<base>/.wr_asset_cache` right after construction with `AssetManager_SetCacheDirectory`, which
  **only stores the path**.
- The directory is **created on first use**, the first time a reference resource needs a temp file. At that first use
  the manager's own leftover temp files (regular files named `temp_*`, from an earlier or crashed run) are deleted.
  Nothing else in the directory is touched, but a dedicated directory is still the right choice.
- `AssetManager_Deconstruct` clears the directory only if it was used this session. After a later
  `AssetManager_SetCacheDirectory`, the new directory is prepared on its own first use, and temp files still held in
  the old one are deleted when their paths are released.
- **Known gap:** two game instances sharing one directory can collide on temp file names (each manager counts
  `temp_0`, `temp_1`, ... from 0), and one instance's first-use cleanup can delete the other's live temp files.

## Standard types

All ten read JSON definitions (`.json`) and parse them with the shared `JSONObjectPool` from
`AssetManager_CreateStandardAssetTypes`. Every `texture_properties` key goes through one parser,
`AssetJSON_ReadTextureProperties` (filtering, wrap, mipmaps); the texture type and model material maps apply all three
with `TextureProperties_Apply`, the sprite sheet, sprite animation and font loaders only the filter (and reject
`trilinear`).

| Type (directory) | Typed loader | Wrapper | The loaded asset owns, and frees on unload |
|---|---|---|---|
| `sprite_sheet` (`sprite_sheets`) | `AssetManager_LoadSpriteSheet` | `SpriteSheet` | The packed atlas `Texture2D` (`UnloadTexture`) and the entry buffer (borrowed from the manager's pool, returned). |
| `sprite_animation` (`sprite_animations`) | `AssetManager_LoadSpriteAnimation` | `SpriteAnimation` | Its standalone frame textures (`UnloadTexture` each) and the frame buffer (pool, returned). Sheet frames borrow the sheet's atlas; the sheet is held under the animation's dependency user. |
| `sound` (`sounds`) | `AssetManager_LoadSound` | `GameSound` | The decoded float samples (`UnloadWaveSamples`); the `GameSound` borrows them. |
| `font` (`fonts`) | `AssetManager_LoadFont` | `GameFont` | The raylib `Font` (`UnloadFont`). |
| `shader` (`shaders`) | `AssetManager_LoadShader` | `GameShader` | The raylib `Shader` (`UnloadShader`). Each stage's `#include` lines are expanded first (shared files in `shaders/common/`, no definitions). |
| `model` (`models`) | `AssetManager_LoadModel` | `GameModel` | Meshes, materials and map arrays (`UnloadModel`), plus every texture its materials referenced at load, tracked by id and unloaded first (`rlUnloadTexture`). Never `UnloadMaterial`: it would also unload the material's shader, and a texture shared by two materials twice. |
| `texture` (`textures`) | `AssetManager_LoadTexture` | `GameTexture` | One GPU texture (`UnloadTexture`), at most 8192 px per side, with its filtering, wrap and optional mipmaps applied, and its `color_space` flag. |
| `world_model` (`world_models`) | `AssetManager_LoadWorldModel` | `WorldModel` | Only its own data: the variants, their overrides and names in one block. Its LOD models and its variants' textures are held under its dependency user and unload right after it. Unknown keys are refused anywhere in its definition. |
| `world_sprite` (`world_sprites`) | `AssetManager_LoadWorldSprite` | `WorldSprite` | Only its own data (the settings). Its sprite animation is held under its dependency user and unloads right after it. Its settings are read through property tables whose rows the sprite component shares; unknown keys are refused in the root and in `render`. |
| `character` (`characters`) | `AssetManager_LoadCharacter` | `CharacterAsset` | Only its own data (the settings); no dependencies. Its settings are read through a property table whose rows the character component shares; unknown keys are refused, and the heights must fit the radius ([Characters.md](Characters.md)). |

Details per type: [SpriteSheet.md](SpriteSheet.md), [SpriteAnimation.md](SpriteAnimation.md), [GameAssets.md](GameAssets.md),
[WorldModels.md](WorldModels.md), [WorldSprites.md](WorldSprites.md).

## Bulk operations (stepped loading)

`AssetManager_CreateAssetBulkOperation(self, user, &Operation)`, then `AssetBulkOperation_AddEntry` per asset (only
before the first step; it returns a `PromisedAsset*` owned by the operation), then `AssetBulkOperation_CompleteStep`
once or a few times per frame until `AssetBulkOperation_IsComplete`, then `PromisedAsset_GetAsset` /
`PromisedAsset_GetError`, then `AssetBulkOperation_Deconstruct` (frees the promises; the assets stay held by the user).
- Each step loads one entry (with its dependencies). A failed entry does not fail the step: its error is on the
  promise, and `PromisedAsset_GetAsset` returns `ErrorCode_InvalidOperation` for it.
- Progress: `AssetBulkOperation_GetProgress` with `AssetLoadProgress_GetProgressFactor`, `GetItemCountTotal` and
  `GetItemCountProcessed`.
- Designed for a frame's stepped `LoadStep` (skeleton in [GameFrame.md](GameFrame.md)), but **no frame uses it yet**:
  `WorldTestFrame` loads synchronously through `WorldRenderer_PrepareWorld`.

## Writing a new asset type

1. **Register** an `AssetTypeInfo` with `AssetManager_CreateAssetType`, before `AssetManager_ReadDefinitions`: a unique
   `Name` and `DirectoryName`, an optional `DefinitionFileExtension` (without the dot), the `Constructor`, and
   `ConstructorUserData` (a JSON-based type can reuse the standard pool: `UserData_FromPointer(JsonPool)`).
2. **Constructor** (`AssetDefinitionConstructor`): parse `rawData` (borrowed, never kept), allocate a concrete
   definition with `AssetDefinition Base` as its first member, and set `Base.VTable` and an owned `Base.Name`
   (`Base.Type` is set by the manager). Validate everything you can here, so a bad file is skipped at startup instead
   of failing at load. On failure free everything, leave `*outDefinition` NULL and return an error (normally
   `ErrorCode_InvalidAssetDefinition`; value-shape errors from the shared GameJSON parsers arrive as
   `ErrorCode_InvalidJSON`). Helpers for the standard JSON shapes are in `source/AssetDefinitionCommon.h`.
3. **`LoadAsset`**:
   - Open resources only through `AssetManager_OpenResource` / `AssetManager_AcquireResourcePath`, with your extension
     filter (narrowed with `AssetFormat_SelectFilter` if the type has a `format`). Close or release them right after use.
   - Load dependencies with `AssetManager_LoadAssetSingle(..., dependencyUser, ...)` and never release them yourself:
     the manager releases that user when the asset unloads or when `LoadAsset` fails.
   - Take scratch and backing buffers from `AssetManager_BorrowGenericBuffer` and return them.
   - Fill `outLoaded`: `Asset` (the pointer callers get; it must not move until destroyed), `VTable` (a static
     `LoadedAssetVTable`) and `DestroyContext`.
   - **On every failure path, unload and free everything you created** (GPU objects, raylib allocations, pool buffers,
     open streams and paths), then return the error. The manager caches and logs it.
   - Keep no pointer to the definition in the loaded asset: definitions can be replaced or removed while it is loaded.
4. **`LoadedAssetVTable.Destroy`**: free the wrapper and everything it owns, and return pool buffers. Never touch
   dependency assets. A normal unload destroys an asset before its dependencies, but `AssetManager_Deconstruct` unloads
   everything in no particular order, so a dependency may already be gone.
5. **`AssetDefinitionVTable.Destroy`**: free the definition object only, never an asset built from it.

Templates: `TextureDefinition.c` (the smallest: path-based, one GPU object), `FontDefinition.c` (path-based, with
`format`), `ShaderDefinition.c` (stream-based, pooled buffers),
`SpriteAnimationDefinition.c` (dependencies), `ModelDefinition.c` (tracking GPU objects to unload),
`WorldModelDefinition.c` (built only from other assets, by name: dependencies of two types, no resource files; refuses
unknown keys with `AssetJSON_RejectUnknownKeys`, which a new type should use too).
`AssetManager_RemoveAssetType` refuses while definitions or loaded assets of the type remain.

## Other notes

- **Logging** goes through the borrowed logger (NULL logs nothing; return values carry the same information): skipped
  definition files, their count, unscannable directories, and the first failure of each load. It never logs per frame.
  Loaders reach the same logger through `AssetManager_GetLogger` for content problems of an asset that still loads,
  logged once at load time (the font loader's warning about codepoints its file lacks).
- **Buffer pools:** `AssetManager_BorrowGenericBuffer(self, elementSize, &Buffer)` lends a cleared, growable buffer;
  give it back with `AssetManager_ReturnGenericBuffer` and never free it yourself.
- Type lookups: `AssetManager_GetAssetTypeByName` (returns `ASSET_TYPE_ID_INVALID` when unknown),
  `AssetManager_GetAssetTypeName`, `AssetManager_GetAssetTypeDirectoryName`. The sprite animation loader finds the
  sheet type by its name, `sprite_sheet`.
- `AssetManager_Deconstruct` is best-effort (the first error is returned, later ones are released) and accepts NULL.
- **Tests:** `tests/AssetManagerTests.c` (run with `sh tests/run_linux.sh`) covers skipped and counted definition
  files, name validation, same-root duplicates, file matching (material libraries, ambiguity, the whole last name,
  unsupported extensions) and the failed-load cache, all without a window. Extend it when you change that logic.
  `tests/TextureTests.c` covers the texture type and the `texture_properties` parser (including the size limit, which
  fails before the GPU upload), `tests/ShaderSourceTests.c` the shader `#include` expander, `tests/WorldModelTests.c`
  the world model type (with code-built models and textures, `tests/TestAssets.h`, so it loads without GL) and
  `tests/WorldSpriteTests.c` the world sprite type (over code-built sprite animations).
- **Reload (planned, built with the editor; T10 designed the consumers for it).** Today an asset never changes while it
  is loaded. The editor will edit a working copy of a definition, write its JSON into the pack and ask for that one
  definition to be reloaded. The plan:
  - `AssetManager_ReloadDefinition(self, type, name)` reads the definition's file again and replaces the definition
    (as `AssetManager_ReadDefinitions` does for one file);
  - if the asset is loaded, it is rebuilt **in place**: the new build replaces the old one's contents behind the same
    wrapper pointer, so holders keep a valid pointer (a failed rebuild keeps the old asset and reports the error);
  - every loaded asset gets a **revision** that advances on each rebuild; consumers that derived data from it (the
    world renderer's cache: a world model's LOD model pointers, the variant index, the bounds) compare it and derive
    again. Consumers already keep names, not data, and re-resolve by name (the render cache by the component's
    revision), so only the per-asset revision is new for them; a world sprite's settings are read every frame, and its
    placements' playback instances must be rebound when its animation changes;
  - dependencies: an asset built on another (a world model on its models and textures, a world sprite on its animation,
    a sprite animation on its sheets) is rebuilt when one it depends on is.
  This is a light version of the fix plan's deferred decision D3 (c).
