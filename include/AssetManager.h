#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"
#include "wr/WRIO.h"


/**
 * @file AssetManager.h
 * @brief Dynamic-type, definition-driven asset manager: loads, shares, and unloads game assets.
 *
 * OVERVIEW
 * --------
 * The asset manager stores asset DEFINITIONS (recipes describing where an asset's resources live and how
 * to build it) and, on demand, LOADS the assets those recipes describe. A loaded asset is STATIC-STATE:
 * it holds no per-instance state and may be used freely and concurrently by any part of the program
 * without one user's use affecting another's. The manager owns all asset memory; callers only ever
 * borrow it.
 *
 * ASSET TYPES ARE DYNAMIC. There are no hard-coded asset-type enums. A type is registered at runtime with
 * AssetManager_CreateAssetType and identified thereafter by an opaque AssetTypeID. This lets new asset
 * types (including modded ones) be added without recompiling the manager. The common built-in types are
 * registered together by AssetManager_CreateStandardAssetTypes.
 *
 * DEFINITIONS. Each definition has a name that is unique within its type (names may repeat across types).
 * A name is a namespaced key, "namespace:path" (NamespacedKey.h's rules, at most NAMESPACED_KEY_MAX_LENGTH bytes in
 * full), so two packs' assets never collide: "mymod:desk" and "tbp:desk" are two assets. Written without a namespace
 * it is the default namespace's: every function that takes a name accepts "desk" for "tbp:desk"
 * (AssetManager_ParseAssetName), and everything the manager stores, logs and returns is the full name. Definitions
 * may be added or removed at any time; removing a definition
 * does NOT unload an asset already built from it: while that asset stays loaded it keeps being shared with
 * new users, and only once it has been unloaded can it no longer be built (until the definition is re-added). Definitions are usually read from files at startup with AssetManager_ReadDefinitions (a bad
 * definition file is logged and skipped, never fatal), but they can also be built in code and registered
 * with AssetManager_SetDefinition, or built from raw bytes with AssetManager_BuildDefinition (e.g. by a
 * custom, non-filesystem source such as a server download).
 *
 * RESOURCE FILES. A File location names a resource WITHOUT its extension. Loaders pass the extensions they
 * can decode (an AssetExtensionFilter); in each search root, in priority order, the candidates are the
 * files in the location's directory whose stem equals the location's last entry name, that are not
 * definition files and whose extension the filter accepts. The first root with any candidate decides: one
 * candidate resolves, two or more is an ambiguity error listing them, none moves on to the next root.
 *
 * FAILED LOADS. When building an asset fails, the failure is logged once and remembered on its definition.
 * Later loads of that asset return the same error code immediately, without touching the disk, until
 * definitions, types, search roots or reference resources change, or AssetManager_ClearLoadFailures is
 * called.
 *
 * LOGGING. The manager borrows an optional Logger (given to AssetManager_Construct1) and reports
 * recoverable problems through it: skipped definition files and first-time load failures. With a NULL
 * logger nothing is logged; the same information is still available through return values.
 *
 * NOT TIED TO JSON. The manager core only ever moves opaque byte buffers; it never parses them. The
 * format of a definition blob is entirely the concern of that type's AssetDefinitionConstructor. The
 * standard types parse JSON (via WRJSON) inside their constructors, but a custom type may use any format.
 *
 * RESOURCE PACKS / OVERRIDES. Assets are resolved through an ORDERED list of search roots. Index 0 is the
 * highest priority: when two roots provide a definition or resource file with the same relative
 * identity, the one in the higher-priority root wins. A resource pack or mod inserts its root at the
 * front to override base assets, or appends to only add new ones.
 *
 * USERS AND AUTO-UNLOAD. Every load is attributed to an AssetUserID (a subsystem, scene, mod, etc.). Holds
 * are presence-based: a user that loads the same asset several times is still a single holder. A user
 * releases one asset with AssetManager_ReleaseAsset or all of its assets at once with
 * AssetManager_ReleaseAllAssetsForUser. When an asset's holder set becomes empty it is unloaded
 * automatically.
 *
 * THREADING. Raylib asset/GPU loading is not safe off the main thread, so ALL asset manager calls that
 * load, unload, mutate definitions/types/roots, or otherwise touch manager state MUST be made from the
 * main thread. The manager performs no internal locking. To keep the frame responsive during large
 * loads, a bulk operation is STEPPED: it loads one asset per AssetBulkOperation_CompleteStep call so the
 * game loop can render a loading screen between steps.
 *
 * MEMORY REUSE. The manager owns buffer pools (AssetManager_BorrowGenericBuffer /
 * AssetManager_ReturnGenericBuffer) so loaders can reuse scratch storage instead of reallocating.
 */


// Macros.
/** @brief Sentinel AssetTypeID meaning "no/invalid type". Returned/expected where a type is absent. */
#define ASSET_TYPE_ID_INVALID ((AssetTypeID)0)
/** @brief Sentinel AssetUserID meaning "no/invalid user". */
#define ASSET_USER_ID_INVALID ((AssetUserID)0)


// Forward declarations.
/** @brief Opaque handle to a JSON object pool (from WRJSON); used only by the standard-types helper so
 *         this header need not include WRJSON. */
typedef struct JSONObjectPoolStruct JSONObjectPool;

/** @brief The logger; full type in Logger.h. The manager only borrows it (see AssetManager_Construct1). */
typedef struct LoggerStruct Logger;

/** @brief The asset manager. Opaque; construct with AssetManager_Construct1. */
typedef struct AssetManagerStruct AssetManager;

/* Standard wrapper types, forward-declared so the typed convenience loaders can name them without pulling
 * Raylib into this header. Their full definitions live in their own module headers. */
typedef struct SpriteSheetStruct SpriteSheet;
typedef struct SpriteAnimationStruct SpriteAnimation;
typedef struct GameSoundStruct GameSound;
typedef struct GameFontStruct GameFont;
typedef struct GameShaderStruct GameShader;
typedef struct GameModelStruct GameModel;
typedef struct GameTextureStruct GameTexture;
typedef struct WorldModelStruct WorldModel;
typedef struct WorldSpriteStruct WorldSprite;
typedef struct CharacterAssetStruct CharacterAsset;
typedef struct AnimationLibraryStruct AnimationLibrary;
/** @brief An animation graph asset (AnimationGraph.h). */
typedef struct AnimationGraphStruct AnimationGraph;
/** @brief A terrain asset (TerrainAsset.h). */
typedef struct TerrainAssetStruct TerrainAsset;
/** @brief A terrain material asset (TerrainMaterial.h). */
typedef struct TerrainMaterialStruct TerrainMaterial;
/** @brief A climate asset (Climate.h). */
typedef struct ClimateStruct Climate;
/** @brief An input actions asset (InputActions.h). */
typedef struct InputActionsStruct InputActions;
/** @brief A texture asset's image as CPU pixels (GameTexture.h), filled by AssetManager_LoadTexturePixels. */
typedef struct TexturePixelsStruct TexturePixels;


// Core identifier types.
/** @brief Opaque handle to a registered asset type. ASSET_TYPE_ID_INVALID (0) denotes no type. */
typedef uint64_t AssetTypeID;

/** @brief Opaque handle to an asset user (a subsystem/scene/mod that keeps assets alive).
 *         ASSET_USER_ID_INVALID (0) denotes no user. */
typedef uint64_t AssetUserID;


// Asset locations.
/**
 * @brief Discriminates where an asset resource is pulled from.
 */
typedef enum AssetLocationTypeEnum
{
    /** @brief A file: an extension-less relative path resolved across the search roots. */
    AssetLocationType_File,
    /** @brief A reference: a name resolved via registered in-memory bytes or a reference resolver. */
    AssetLocationType_Reference
} AssetLocationType;

/**
 * @brief Where a single asset resource lives.
 *
 * Loaders never open resources directly; they pass an AssetLocation to AssetManager_OpenResource (for
 * stream-capable loaders) or AssetManager_AcquireResourcePath (for loaders that need a real file), so
 * search-root override and reference resolution happen in exactly one place.
 */
typedef struct AssetLocationStruct
{
    /** @brief Which kind of location this is. */
    AssetLocationType Type;
    /** @brief Borrowed, NUL-terminated UTF-8 value: an extension-less relative path (File) or a
     *         reference name (Reference). Not retained past the call it is passed to. */
    const unsigned char* Value;
} AssetLocation;

/**
 * @brief The file extensions a loader accepts when a File location is resolved to a real file.
 *
 * A borrowed view built by the caller (usually a constant per loader); nothing is copied or retained past
 * the call it is passed to. Extensions are written WITHOUT the dot (u8"png") and are compared
 * case-insensitively for ASCII letters. A filter whose Extensions is NULL, or whose Count is 0, accepts
 * every extension (including files without one). See AssetManager_OpenResource for how the filter takes
 * part in choosing a file.
 */
typedef struct AssetExtensionFilterStruct
{
    /** @brief Borrowed array of Count NUL-terminated extensions without the dot, matched
     *         case-insensitively; NULL accepts any extension. */
    const unsigned char* const* Extensions;
    /** @brief Number of entries in Extensions; 0 accepts any extension. */
    size_t Count;
} AssetExtensionFilter;

/**
 * @brief Produces a readable stream for a named (non-file) resource.
 *
 * Invoked by AssetManager_OpenResource when a location is AssetLocationType_Reference and no in-memory
 * bytes were registered for the name. Must be callable on the main thread and must set @p outStream to a
 * stream the caller will close, or return a non-success Error and leave @p outStream NULL.
 * @param manager The asset manager making the request; borrowed.
 * @param userData The user data supplied at registration; borrowed, valid only for the call.
 * @param referenceName The reference name to resolve; borrowed, NUL-terminated UTF-8.
 * @param outStream [out] Receives the opened stream on success, NULL on failure. Never NULL.
 * @returns Success with @p *outStream set, or a non-success Error describing why the reference could not
 *          be resolved.
 */
typedef Error (*AssetReferenceResolver)(AssetManager* manager,
    const UserData* userData,
    const unsigned char* referenceName,
    IOStream** outStream);

/**
 * @brief Opaque handle to a resolved resource path.
 *
 * Obtained from AssetManager_AcquireResourcePath and released with AssetManager_ReleaseResourcePath. It
 * remembers whether it wraps a real resolved file path or a temp file the manager materialized, so
 * release deletes the temp (and is a no-op for a real path). Read the path with AssetResourcePath_Get.
 */
typedef struct AssetResourcePathStruct AssetResourcePath;


// Loaded assets.
struct LoadedAssetStruct;

/**
 * @brief Virtual table for a loaded asset, letting the manager tear one down without knowing its type.
 */
typedef struct LoadedAssetVTableStruct
{
    /**
     * @brief Frees the loaded asset and every resource it owns.
     *
     * Called by the manager when the asset's holder set becomes empty. Must release the wrapper (Asset),
     * any GPU/Raylib objects, and return any borrowed pool buffers. It must NOT depend on the definition
     * that produced the asset still existing. Dependency assets held under the asset's dependency user
     * are released by the manager separately, so this need not touch them.
     * @param self The LoadedAsset being destroyed; borrowed.
     * @param manager The owning manager (e.g. to return pooled buffers); borrowed.
     */
    void (*Destroy)(struct LoadedAssetStruct* self, AssetManager* manager);
} LoadedAssetVTable;

/**
 * @brief The product of loading a definition: the caller-facing wrapper plus how to tear it down.
 *
 * An AssetDefinition's LoadAsset fills one of these. The manager stores it in its loaded-asset table and
 * hands Asset back to callers. Because it carries its own destructor, an asset can be unloaded
 * independently of the definition it came from.
 */
typedef struct LoadedAssetStruct
{
    /** @brief The custom wrapper handed back to callers (e.g. SpriteSheet*, GameModel*). Manager-owned. */
    void* Asset;
    /** @brief Teardown behavior for this asset; must be set by the loader. */
    const LoadedAssetVTable* VTable;
    /** @brief Loader-owned context the destructor needs (owned buffers, Raylib handles, etc.). May be NULL. */
    void* DestroyContext;
} LoadedAsset;


// Asset definitions.
struct AssetDefinitionStruct;

/**
 * @brief Virtual table for an asset definition (the recipe for building one asset).
 */
typedef struct AssetDefinitionVTableStruct
{
    /**
     * @brief Builds a concrete asset from this recipe.
     *
     * Opens the definition's resources via the manager (AssetManager_OpenResource /
     * AssetManager_AcquireResourcePath, passing the extensions the loader can decode so a File location
     * picks the right file), builds the custom wrapper (drawing scratch/backing storage from
     * the manager's buffer pools where useful), and fills @p outLoaded. To pull in dependency assets
     * (e.g. a sprite animation depending on a sprite sheet), load them via AssetManager_LoadAssetSingle
     * attributed to @p dependencyUser so they stay alive exactly as long as this asset does. Runs on the
     * main thread.
     * @param self The definition (its concrete object; equals the AssetDefinition* since the base is the
     *        first member); borrowed.
     * @param manager The owning manager; borrowed.
     * @param dependencyUser A manager-provided user id representing THIS asset; attribute any dependency
     *        loads to it so they are released when this asset unloads.
     * @param outLoaded [out] Receives the built asset and its teardown info. Never NULL.
     * @returns Success with @p outLoaded populated, or a non-success Error (typically
     *          ErrorCode_InvalidAssetData / ErrorCode_IO) on failure.
     */
    Error (*LoadAsset)(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded);

    /**
     * @brief Frees the definition object itself (not any asset built from it).
     * @param self The definition to destroy; borrowed.
     */
    void (*Destroy)(void* self);
} AssetDefinitionVTable;

/**
 * @brief Abstract base of every asset definition (a build recipe for one named asset of one type).
 *
 * Concrete definitions embed this as their FIRST member, so a concrete pointer converts to
 * AssetDefinition*. The manager sets Type when the definition is registered; the constructor sets Name
 * (parsed from the blob). Create via a type's AssetDefinitionConstructor (from bytes) or a per-type
 * builder (in code), then hand ownership to the manager with AssetManager_SetDefinition.
 */
typedef struct AssetDefinitionStruct
{
    /** @brief Behavior (load/destroy) for this definition; set by whoever constructs it. */
    const AssetDefinitionVTable* VTable;
    /** @brief The type this definition belongs to; set by the manager at registration. */
    AssetTypeID Type;
    /** @brief Owned, NUL-terminated asset name, unique within the type, allocated with Memory_Allocate; set by the
     *         constructor (AssetJSON_ReadName gives the full name). Registration rejects a name that fails
     *         AssetManager_IsValidAssetName and replaces one written without its namespace with its full form (freeing
     *         the old string with Memory_Free). */
    unsigned char* Name;
} AssetDefinition;

/**
 * @brief Builds a typed AssetDefinition from a raw definition blob.
 *
 * This is the sole place a type's on-disk/in-memory format is interpreted; the manager never parses. The
 * constructor parses @p rawData (e.g. UTF-8 JSON), validates it, reads out the asset name (storing it in
 * the produced definition's Name), allocates a concrete definition, and returns it upcast to
 * AssetDefinition*. Ownership passes to the manager when the definition is registered.
 * @param manager The owning manager; borrowed. May be used to reach shared parsing state.
 * @param userData The type's ConstructorUserData (e.g. a shared JSONObjectPool*); borrowed.
 * @param rawData The raw definition bytes; borrowed, not retained past the call.
 * @param sourceDescription Human-readable origin (file path or reference name) for error messages; borrowed.
 * @param outDefinition [out] Receives the new definition on success, NULL on failure. Never NULL.
 * @returns Success, or a non-success Error (typically ErrorCode_InvalidAssetDefinition) on malformed input.
 */
typedef Error (*AssetDefinitionConstructor)(AssetManager* manager,
    const UserData* userData,
    const GenericBuffer* rawData,
    const unsigned char* sourceDescription,
    AssetDefinition** outDefinition);


// Asset type registration.
/**
 * @brief Everything needed to register a new asset type.
 *
 * The strings are copied by AssetManager_CreateAssetType, so the caller need not keep them alive.
 */
typedef struct AssetTypeInfoStruct
{
    /** @brief Unique, NUL-terminated UTF-8 type name (e.g. u8"sprite_sheet"). Copied. */
    const unsigned char* Name;
    /** @brief NUL-terminated UTF-8 sub-directory name under each search root for this type's resources
     *         and definitions (e.g. u8"sprite_sheets"). Copied. */
    const unsigned char* DirectoryName;
    /** @brief OPTIONAL definition-file extension WITHOUT the dot (e.g. u8"json"). When set,
     *         AssetManager_ReadDefinitions only treats files with this extension as definitions and skips
     *         all others (so resource files — models, textures, etc. — may live alongside definitions in
     *         this type's directory). Conversely, file-location resolution never returns a file with this
     *         extension, so a definition may share its stem with the resource it defines (test.json +
     *         test.png). When NULL, every file in the directory is parsed as a definition and no file is
     *         excluded from resolution. Copied; matched case-insensitively. */
    const unsigned char* DefinitionFileExtension;
    /** @brief Builds definitions of this type from raw bytes. Must not be NULL. */
    AssetDefinitionConstructor Constructor;
    /** @brief Context passed to Constructor on every call (e.g. a shared JSONObjectPool*). Copied by value. */
    UserData ConstructorUserData;
} AssetTypeInfo;

/**
 * @brief The type ids of the standard built-in asset types, filled by AssetManager_CreateStandardAssetTypes.
 */
typedef struct StandardAssetTypesStruct
{
    /** @brief Sprite sheet type id. */
    AssetTypeID SpriteSheet;
    /** @brief 2D sprite animation type id. */
    AssetTypeID SpriteAnimation;
    /** @brief Sound (and music) type id. */
    AssetTypeID Sound;
    /** @brief Font type id. */
    AssetTypeID Font;
    /** @brief Shader type id. */
    AssetTypeID Shader;
    /** @brief Model type id (static meshes; see the model module for the current feature scope). */
    AssetTypeID Model;
    /** @brief Standalone texture type id (sky panoramas, the moon, noise, normal maps, masks). */
    AssetTypeID Texture;
    /** @brief World model type id: a placeable thing's LOD models, draw distance, render defaults and variants, built
     *  on model and texture assets (WorldModel.h). */
    AssetTypeID WorldModel;
    /** @brief World sprite type id: how a sprite animation stands, is lit and is seen in the world, built on a sprite
     *  animation asset (WorldSprite.h). */
    AssetTypeID WorldSprite;
    /** @brief Character type id: the defaults of a kind of character (a human, a dog, a pigeon): its body and how it
     *  moves (CharacterAsset.h). */
    AssetTypeID Character;
    /** @brief Animation library type id: the skeletal animation clips of a glTF file, which world models name for
     *  their placements to play (AnimationLibrary.h). */
    AssetTypeID AnimationLibrary;
    /** @brief Animation graph type id: which clip an animated object plays when (layers, states, transitions, special
     *  actions), which world models name next to their animation libraries (AnimationGraph.h). */
    AssetTypeID AnimationGraph;
    /** @brief Terrain type id: a terrain's free-form mesh with its painted weights and densities, its palette and its
     *  placements' defaults (TerrainAsset.h). */
    AssetTypeID Terrain;
    /** @brief Terrain material type id: one material of a terrain's palette, its maps' pixels (built on texture assets'
     *  images) and its settings (TerrainMaterial.h). */
    AssetTypeID TerrainMaterial;
    /** @brief Climate type id: how a world's weather behaves over game time: its presets, their seasons, links and rates
     *  (Climate.h). */
    AssetTypeID Climate;
    /** @brief Input actions type id: one action category with its actions, and overrides of registered actions and
     *  categories, which the input registers at startup (InputActions.h). */
    AssetTypeID InputActions;
} StandardAssetTypes;


// Bulk loading.
/** @brief Opaque handle to a bulk load operation. Created by AssetManager_CreateAssetBulkOperation. */
typedef struct AssetBulkOperationStruct AssetBulkOperation;

/** @brief Opaque handle to one entry's future result within a bulk operation. */
typedef struct PromisedAssetStruct PromisedAsset;

/** @brief Opaque, pollable progress of a bulk operation. Read via AssetLoadProgress_* accessors. */
typedef struct AssetLoadProgressStruct AssetLoadProgress;


// ---- Lifecycle ----
/**
 * @brief Allocates and initializes an empty asset manager.
 *
 * The new manager has no types, definitions, search roots, or loaded assets, and a default cache
 * directory (see AssetManager_SetCacheDirectory). Construction does not touch the filesystem: the cache
 * directory is only created when it is first needed. Release with AssetManager_Deconstruct.
 *
 * The manager reports recoverable problems through @p logger: definition files skipped by
 * AssetManager_ReadDefinitions (one ERROR each plus a WARNING summary) and the first failure of each asset
 * load (ERROR). It never logs per frame. The logger is BORROWED: it must stay valid until
 * AssetManager_Deconstruct returns. Pass NULL (e.g. in tests) to log nothing; return values carry the same
 * information.
 * @param logger The logger to report through; borrowed, may be NULL.
 * @param outSelf [out] Receives the new manager on success, NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p outSelf is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_Construct1(Logger* logger, AssetManager** outSelf);

/**
 * @brief Unloads everything and frees the manager.
 *
 * Unloads all loaded assets (regardless of remaining holders), destroys all definitions and types, clears
 * the cache directory if it was used this session, and frees the manager itself. Teardown is best-effort: the first error is returned
 * and later errors are released so none leak. Safe to call with NULL (returns success).
 * @param self The manager to deconstruct; may be NULL.
 * @returns Success, or the first non-success Error encountered while tearing down.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_Deconstruct(AssetManager* self);

/**
 * @brief Returns the logger the manager reports through (the one given to AssetManager_Construct1).
 *
 * For asset loaders that meet a recoverable content problem while building an asset that still loads (for example
 * a font file lacking requested glyphs): they log one WARNING through it at load time. Never log per frame.
 * @param self The manager; must not be NULL.
 * @returns The borrowed logger, or NULL when the manager logs nothing.
 */
Logger* AssetManager_GetLogger(const AssetManager* self);


// ---- Types ----
/**
 * @brief Registers a new asset type and returns its id.
 *
 * Copies the names from @p info. The type name and directory name must each be unique among registered
 * types. Clears all cached load failures (see AssetManager_ClearLoadFailures) before adding the type,
 * since a loader may have failed for want of it.
 * @param self The manager; must not be NULL.
 * @param info The type description; must not be NULL, with a non-NULL Name, DirectoryName, and Constructor.
 * @param outID [out] Receives the new type id on success, ASSET_TYPE_ID_INVALID on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL/empty fields; ErrorCode_InvalidOperation if a type
 *          with the same name or directory already exists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_CreateAssetType(AssetManager* self, const AssetTypeInfo* info, AssetTypeID* outID);

/**
 * @brief Registers all standard asset types and a shared JSON pool used to parse their definitions.
 *
 * Registers the sixteen standard types (sprite sheet, sprite animation, sound, font, shader, model, texture, world
 * model, world sprite, character, animation library, animation graph, terrain, terrain material, climate and input
 * actions), writing their ids into @p outTypes. Creates one JSONObjectPool and wires it into every standard type's
 * ConstructorUserData so all standard definition parsing recycles JSON compounds/arrays/strings instead of
 * reallocating. The caller OWNS @p *outDefinitionPool and must free it with JSONObjectPool_Deconstruct after (or
 * alongside) the manager. The same pool may be reused as the ConstructorUserData of a custom JSON-based type to
 * extend the recycling to it. The manager also remembers these ids internally so the typed convenience
 * loaders (AssetManager_LoadSpriteSheet, etc.) work.
 * @param self The manager; must not be NULL.
 * @param outTypes [out] Receives the standard type ids. Must not be NULL.
 * @param outDefinitionPool [out] Receives the caller-owned shared JSON pool. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_InvalidOperation if a standard
 *          type is already registered.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_CreateStandardAssetTypes(AssetManager* self, StandardAssetTypes* outTypes,
    JSONObjectPool** outDefinitionPool);

/**
 * @brief Unregisters an asset type.
 *
 * Fails if any definition of the type is still registered or any asset of the type is still loaded; remove
 * those first. Once those checks pass, all cached load failures are cleared (see
 * AssetManager_ClearLoadFailures) and the type is removed.
 * @param self The manager; must not be NULL.
 * @param id The type to remove.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p id is invalid/unknown;
 *          ErrorCode_InvalidOperation if definitions or loaded assets of the type remain.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_RemoveAssetType(AssetManager* self, AssetTypeID id);

/**
 * @brief Returns a type's directory name (borrowed, manager-owned; valid until the type is removed).
 * @param self The manager; must not be NULL.
 * @param id The type id.
 * @param outName [out] Receives the borrowed directory name. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an unknown id.
 */
Error AssetManager_GetAssetTypeDirectoryName(AssetManager* self, AssetTypeID id, const unsigned char** outName);

/**
 * @brief Returns a type's name (borrowed, manager-owned; valid until the type is removed).
 * @param self The manager; must not be NULL.
 * @param id The type id.
 * @param outName [out] Receives the borrowed type name. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an unknown id.
 */
Error AssetManager_GetAssetTypeName(AssetManager* self, AssetTypeID id, const unsigned char** outName);

/**
 * @brief Looks up a type id by name.
 * @param self The manager; must not be NULL.
 * @param name The type name to find; NUL-terminated UTF-8, must not be NULL.
 * @param outID [out] Receives the id, or ASSET_TYPE_ID_INVALID if no such type. Must not be NULL.
 * @returns Success (whether or not found); ErrorCode_IllegalArgument for NULL args.
 */
Error AssetManager_GetAssetTypeByName(AssetManager* self, const unsigned char* name, AssetTypeID* outID);


// ---- Search roots / resource packs ----
// Changing the root list clears all cached load failures first (see AssetManager_ClearLoadFailures),
// because a different file may now resolve.
/**
 * @brief Appends a search root at the lowest priority.
 *
 * Clears all cached load failures before adding the root.
 * @param self The manager; must not be NULL.
 * @param directory NUL-terminated UTF-8 directory path; copied. Must not be NULL/empty.
 * @returns Success; ErrorCode_IllegalArgument for NULL/empty args.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_AddSearchRoot(AssetManager* self, const unsigned char* directory);

/**
 * @brief Inserts a search root at a given priority index (0 = highest priority / override winner).
 *
 * Clears all cached load failures before inserting the root.
 * @param self The manager; must not be NULL.
 * @param index Insertion index in [0, current count]. Out-of-range fails.
 * @param directory NUL-terminated UTF-8 directory path; copied. Must not be NULL/empty.
 * @returns Success; ErrorCode_IllegalArgument for NULL/empty args; ErrorCode_IndexOutOfBounds if @p index
 *          exceeds the current count.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_InsertSearchRoot(AssetManager* self, size_t index, const unsigned char* directory);

/**
 * @brief Removes the first search root equal to @p directory.
 *
 * Clears all cached load failures before removing the root (not when the root is absent).
 * @param self The manager; must not be NULL.
 * @param directory NUL-terminated UTF-8 directory path to remove. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_InvalidOperation if not present.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_RemoveSearchRoot(AssetManager* self, const unsigned char* directory);

/**
 * @brief Returns the number of search roots.
 * @param self The manager; may be NULL (returns 0).
 * @returns The search-root count.
 */
size_t AssetManager_GetSearchRootCount(AssetManager* self);

/**
 * @brief Returns the search root at a priority index (borrowed, valid until the root list changes).
 * @param self The manager; must not be NULL.
 * @param index Index in [0, count).
 * @param outDirectory [out] Receives the borrowed directory string. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_IndexOutOfBounds if out of range.
 */
Error AssetManager_GetSearchRoot(AssetManager* self, size_t index, const unsigned char** outDirectory);


// ---- Reference resolution (in-memory / server-fetched resources) ----
/**
 * @brief Sets the fallback resolver used for reference locations with no registered in-memory bytes.
 *
 * Replaces any previous resolver. Passing a NULL resolver clears it. Clears all cached load failures
 * first.
 * @param self The manager; must not be NULL.
 * @param resolver The resolver callback, or NULL to clear.
 * @param userData Context stored by value and handed to the resolver on each call; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_SetReferenceResolver(AssetManager* self, AssetReferenceResolver resolver, const UserData* userData);

/**
 * @brief Registers a fixed in-memory blob under a reference name (the manager copies the bytes).
 *
 * A later reference location with this name resolves to these bytes (taking precedence over the resolver).
 * Registering the same name again replaces the bytes. Clears all cached load failures before storing
 * the bytes.
 * @param self The manager; must not be NULL.
 * @param referenceName NUL-terminated UTF-8 reference name; copied. Must not be NULL/empty.
 * @param bytes The resource bytes; copied into pooled storage. May be NULL only if @p byteCount is 0.
 * @param byteCount Number of bytes to copy.
 * @returns Success; ErrorCode_IllegalArgument for NULL/empty name or NULL @p bytes with non-zero count.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_SetReferenceBytes(AssetManager* self, const unsigned char* referenceName,
    const unsigned char* bytes, size_t byteCount);

/**
 * @brief Reports whether an extension filter accepts a file extension.
 *
 * Compares @p extension against each entry case-insensitively (ASCII letters only; other bytes must match
 * exactly). An empty filter (NULL @p self, NULL Extensions or Count 0) accepts everything.
 * @param self The filter; may be NULL (accepts everything).
 * @param extension The extension WITHOUT the dot (u8"png"), or an empty string for a file without one;
 *        NUL-terminated. NULL is treated as an empty string.
 * @returns true if the extension is accepted, false otherwise.
 */
bool AssetExtensionFilter_Accepts(const AssetExtensionFilter* self, const unsigned char* extension);

/**
 * @brief Appends a human-readable list of a filter's extensions (e.g. "png, bmp, gif") for messages.
 *
 * Treats @p destination as one growing string: an existing trailing NUL is dropped, the list is appended
 * and the result is NUL-terminated. An empty filter appends "any extension". Does not clear
 * @p destination first.
 * @param self The filter; may be NULL (appends "any extension").
 * @param destination The byte buffer (element size 1) to append to; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p destination is NULL or not a byte buffer;
 *          ErrorCode_BufferTooSmall if the buffer cannot grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetExtensionFilter_AppendList(const AssetExtensionFilter* self, GenericBuffer* destination);

/**
 * @brief Opens a resource location as a readable stream.
 *
 * For a File location, @p location->Value is a relative path WITHOUT an extension, resolved under
 * @p assetType's directory. The search roots are tried in priority order. In a root, the candidates are
 * the files in the location's directory whose stem (name minus its last extension) equals the location's
 * last entry name, that do not carry the type's definition-file extension, and whose extension
 * @p extensions accepts. The first root with at least one candidate decides: exactly one candidate is
 * opened; two or more fail with ErrorCode_InvalidAssetDefinition naming every candidate (so leftover
 * files never make the choice depend on directory order); none moves on to the next root. Example: with
 * the model filter, "ships/ship" picks "ships/ship.obj" over "ships/ship.mtl"; "a/b.c" only matches
 * files whose stem is "b.c" (such as "a/b.c.png"), never "a/b.png".
 *
 * For a Reference location, returns a stream over registered in-memory bytes if present, else dispatches
 * to the reference resolver; @p extensions is not used.
 *
 * The returned stream is owned by the manager; the caller must close it with AssetManager_CloseResource
 * (NOT IOStream_Deconstruct, which would not free the manager's heap wrapper and any backing buffer).
 * @param self The manager; must not be NULL.
 * @param assetType The type whose directory to resolve File locations under.
 * @param location The location to open; must not be NULL.
 * @param extensions The extensions the caller can decode; borrowed. NULL (or an empty filter) accepts
 *        any extension.
 * @param outStream [out] Receives the opened stream on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an unknown type; ErrorCode_FileNotFound if
 *          no root contains a file for the location; ErrorCode_InvalidAssetData if files with the right
 *          stem exist but none has an accepted extension (the message lists them and the accepted set);
 *          ErrorCode_InvalidAssetDefinition if the deciding root holds several candidates;
 *          ErrorCode_InvalidOperation if a reference cannot be resolved.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_OpenResource(AssetManager* self, AssetTypeID assetType,
    const AssetLocation* location, const AssetExtensionFilter* extensions, IOStream** outStream);

/**
 * @brief Closes and frees a stream returned by AssetManager_OpenResource.
 *
 * Closes the underlying file/memory stream, frees any backing buffer the manager allocated for it, and
 * frees the manager's heap wrapper. Must be paired with each successful AssetManager_OpenResource call.
 * @param self The manager; must not be NULL.
 * @param stream The stream returned by AssetManager_OpenResource; must not be NULL and must belong to
 *        @p self.
 * @returns Success; ErrorCode_IllegalArgument for NULL args.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_CloseResource(AssetManager* self, IOStream* stream);


// ---- Path resolution (loaders that need a real file, e.g. models) ----
/**
 * @brief Resolves a location to a usable filesystem path (for loaders that cannot take a stream).
 *
 * A File location resolves to its real path under the search roots (no copy), choosing the file exactly
 * as AssetManager_OpenResource does (including the ambiguity error). A Reference location is written to a
 * temp file in the cache directory named with @p preferredExtension, and that path is returned. Release
 * the handle with AssetManager_ReleaseResourcePath (which deletes any temp file). Reference resources used
 * this way must be self-contained single files, since a temp file cannot resolve external sibling files.
 * @param self The manager; must not be NULL.
 * @param assetType The type whose directory to resolve File locations under.
 * @param location The location to resolve; must not be NULL.
 * @param extensions The extensions the caller can decode, used for File locations; borrowed. NULL (or an
 *        empty filter) accepts any extension. A loader with a definition "format" passes a filter holding
 *        only that format.
 * @param preferredExtension Extension WITHOUT the dot (e.g. u8"glb") used to name a materialized temp
 *        file; ignored for File locations. May be NULL only for File locations.
 * @param outHandle [out] Receives the path handle on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an unknown type; ErrorCode_FileNotFound,
 *          ErrorCode_InvalidAssetData or ErrorCode_InvalidAssetDefinition when a File location does not
 *          resolve to exactly one file (see AssetManager_OpenResource); ErrorCode_InvalidOperation if a
 *          reference cannot be resolved.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_AcquireResourcePath(AssetManager* self, AssetTypeID assetType,
    const AssetLocation* location, const AssetExtensionFilter* extensions, const unsigned char* preferredExtension,
    AssetResourcePath** outHandle);

/**
 * @brief Returns the NUL-terminated filesystem path a handle resolved to (borrowed; valid until release).
 * @param self The path handle; must not be NULL.
 * @returns The path string, or NULL if @p self is NULL.
 */
const unsigned char* AssetResourcePath_Get(AssetResourcePath* self);

/**
 * @brief Releases a resource-path handle, deleting its temp file if one was materialized.
 * @param self The manager; must not be NULL.
 * @param handle The handle to release; must not be NULL and must belong to @p self.
 * @returns Success; ErrorCode_IllegalArgument for NULL args.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_ReleaseResourcePath(AssetManager* self, AssetResourcePath* handle);

/**
 * @brief Sets the directory where temp files (for reference resources needing a path) are materialized.
 *
 * Defaults to ".wr_asset_cache" relative to the working directory; the game sets it next to the executable.
 * This call only stores the path and does not touch the filesystem. The directory is created the first time
 * a reference resource needs a temp file, and at that first use the manager's own leftover temp files (regular
 * files named "temp_*" from a previous, e.g. crashed, run) are deleted; AssetManager_Deconstruct deletes them
 * again if the directory was used. Other files in the directory are never touched. Temp files already created in
 * a previous directory are still deleted when their resource paths are released.
 * @param self The manager; must not be NULL.
 * @param directory NUL-terminated UTF-8 directory path; copied. Must not be NULL/empty.
 * @returns Success; ErrorCode_IllegalArgument for NULL/empty args.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_SetCacheDirectory(AssetManager* self, const unsigned char* directory);


// ---- Definitions ----
/**
 * @brief Reads and registers every definition found under the search roots (filesystem source).
 *
 * For each search root (in priority order) and each registered type, walks the root's type
 * sub-directory recursively for definition files, reads each file's bytes, builds a definition with the
 * type's constructor, and registers it with the file as its source.
 *
 * Each file is handled on its own. A file that cannot be read, does not parse, fails its constructor or
 * cannot be registered (invalid name, duplicate) is SKIPPED: it is logged as an ERROR
 * ("Skipped <type> definition \"<path>\": <reason>"), counted, and the scan continues with the next file.
 * If any file was skipped, one WARNING with the count follows. A directory that cannot be enumerated
 * aborts only that directory (logged as an ERROR); the scan continues with the next type and root.
 *
 * Name collisions within a type:
 *  - Same search root: the definition found first stays registered and every later one is skipped as an
 *    error naming both files, because the winner would otherwise depend on directory order.
 *  - Different roots: the higher-priority (lower-index) root wins and the lower one is ignored silently;
 *    this is how resource packs override base assets.
 *  - A definition read by an EARLIER call is replaced by this call's winner, so calling this again after
 *    changing the roots applies the new override order. A definition registered in code with
 *    AssetManager_SetDefinition is never replaced by a file.
 *
 * Clears every cached load failure (see AssetManager_ClearLoadFailures).
 * @param self The manager; must not be NULL.
 * @param outFailedFileCount [out] Receives the number of skipped definition files (0 when all were
 *        registered). May be NULL. Set to 0 first, also on failure.
 * @returns Success when every directory could be scanned, even if individual files were skipped (see
 *          @p outFailedFileCount); ErrorCode_IllegalArgument if @p self is NULL; otherwise the FIRST
 *          directory-enumeration or structural error (later ones are logged and released), after the
 *          rest of the scan has still run.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_ReadDefinitions(AssetManager* self, size_t* outFailedFileCount);

/**
 * @brief Builds a definition from raw bytes using a type's constructor, WITHOUT registering it.
 *
 * Lets a custom (non-filesystem) source reuse per-type parsing. The returned definition is owned by the
 * caller until handed to AssetManager_SetDefinition (which takes ownership) or destroyed via its vtable.
 * @param self The manager; must not be NULL.
 * @param type The type whose constructor to use.
 * @param rawData The raw definition bytes; borrowed. Must not be NULL.
 * @param sourceDescription Origin string for error messages; borrowed. May be NULL.
 * @param outDefinition [out] Receives the new definition on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an unknown type; forwards the constructor's
 *          error on malformed input.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_BuildDefinition(AssetManager* self, AssetTypeID type,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/**
 * @brief Registers (or overrides) a definition by its (Type, Name); the manager ALWAYS takes ownership.
 *
 * The definition's Type must be set to a registered type (a builder/constructor result already carries it
 * via the type used) and Name must pass AssetManager_IsValidAssetName; a Name written without its namespace is
 * replaced with its full form. If a definition with the same (Type, full Name) exists it is destroyed and replaced,
 * whatever its source. This is the entry point for
 * runtime-created definitions and custom sources; such a definition has no source file and is never
 * replaced by AssetManager_ReadDefinitions.
 *
 * OWNERSHIP. Ownership of @p definition transfers on every call: on success the manager keeps it, and on
 * EVERY failure (including a NULL @p self) it is destroyed through its vtable before this returns, so the
 * caller must not touch it afterwards. The one exception is a definition whose VTable or VTable->Destroy
 * is NULL: it cannot be destroyed, so the call fails and leaves it with the caller.
 *
 * Clears all cached load failures before registering (when @p self and @p definition are usable).
 * @param self The manager; must not be NULL.
 * @param definition The definition to register; must not be NULL. Ownership transfers (see above).
 * @returns Success; ErrorCode_IllegalArgument for NULL args, an unset/unregistered Type, or a definition
 *          without a destroy function; ErrorCode_InvalidAssetDefinition if the name is invalid.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_SetDefinition(AssetManager* self, AssetDefinition* definition);

/**
 * @brief Removes a definition. Does not unload any asset already built from it.
 *
 * Clears all cached load failures before removing the definition (not when it does not exist).
 * @param self The manager; must not be NULL.
 * @param type The definition's type.
 * @param name The definition's name, with or without its namespace; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_ArgumentOutOfRange for an invalid name;
 *          ErrorCode_InvalidOperation if no such definition exists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_RemoveDefinition(AssetManager* self, AssetTypeID type, const unsigned char* name);

/**
 * @brief Reports whether a string is a valid asset name as written: a namespaced key, with or without its namespace.
 *
 * "tbp:desk", "mymod:furniture/desk" and "desk" (the default namespace's) are valid; uppercase letters, spaces, a
 * second colon, an empty namespace or path, any non-ASCII byte, and a name over NAMESPACED_KEY_MAX_LENGTH bytes in
 * full are not. Registration applies this check to every definition, whatever its type or source. Pure function.
 * @param name The candidate name; NUL-terminated. NULL is invalid.
 * @returns true if @p name is a valid asset name, false otherwise.
 */
bool AssetManager_IsValidAssetName(const unsigned char* name);

/**
 * @brief Describes what is wrong with an asset name as written, for messages ("... is not a valid asset name: %s.").
 * @param name The candidate name; NUL-terminated. NULL is described as empty.
 * @returns NULL for a valid asset name (AssetManager_IsValidAssetName); otherwise a static description such as
 *          "uppercase letters are not allowed (keys are lowercase)".
 */
const char* AssetManager_DescribeAssetNameProblem(const unsigned char* name);

/**
 * @brief Reads an asset name as written, with or without its namespace, and appends its full form.
 *
 * "desk" becomes "tbp:desk" (NAMESPACED_KEY_DEFAULT_NAMESPACE); "mymod:desk" stays as it is. The destination is one
 * growing string: a trailing NUL is dropped before appending, and the result is NUL-terminated. A buffer of
 * NAMESPACED_KEY_BUFFER_SIZE bytes always suffices (on the stack through GenericBuffer_CreateConstant). Nothing is
 * appended on failure.
 * @param name The name (NUL-terminated); must not be NULL.
 * @param outFullName The byte buffer to append the full name to; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_ArgumentOutOfRange when @p name is not
 *          a valid asset name (the message names the problem); ErrorCode_BufferTooSmall if @p outFullName cannot hold
 *          the name.
 */
Error AssetManager_ParseAssetName(const unsigned char* name, GenericBuffer* outFullName);

/**
 * @brief Lists the full names of a type's definitions that start with a prefix, in byte order.
 *
 * For loading every definition of a type and for autocomplete: "mymod:" lists a mod's definitions, the empty prefix
 * every one. The prefix is compared with the full names as it is ("desk" finds nothing, "tbp:d" finds "tbp:desk").
 * Each name is appended to @p outNames as a NUL-terminated text and its byte offset there to @p outOffsets (offsets,
 * not pointers: the names buffer may move as it grows); the earlier contents of both buffers are untouched. A linear
 * scan over every definition; allocates only to grow the buffers and a pooled scratch buffer.
 * @param self The manager; must not be NULL.
 * @param type The asset type.
 * @param prefix The prefix (NUL-terminated); must not be NULL.
 * @param outNames A byte buffer the names are appended to; must not be NULL.
 * @param outOffsets A buffer of size_t the names' offsets are appended to; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments, buffers of the wrong element size or an unknown
 *          type; ErrorCode_BufferTooSmall if a buffer could not grow (nothing appended then).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error AssetManager_GetDefinitionNames(AssetManager* self, AssetTypeID type, const unsigned char* prefix,
    GenericBuffer* outNames, GenericBuffer* outOffsets);

/**
 * @brief Forgets every remembered load failure, so the next load of each such asset is attempted again.
 *
 * When building an asset fails, AssetManager_LoadAssetSingle remembers the failure on the asset's
 * definition and later loads return it at once. The manager clears these automatically whenever
 * definitions, types, search roots or reference resources change; call this for a manual retry (e.g.
 * after fixing a file on disk while the game runs). Cheap when nothing failed.
 * @param self The manager; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_ClearLoadFailures(AssetManager* self);

/**
 * @brief Reports whether a definition with the given (type, name) is registered.
 * @param self The manager; must not be NULL.
 * @param type The type to check.
 * @param name The name to check, with or without its namespace; must not be NULL. An invalid name is not registered.
 * @param outExists [out] Receives the result. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args.
 */
Error AssetManager_HasDefinition(AssetManager* self, AssetTypeID type, const unsigned char* name, bool* outExists);


// ---- Users ----
/**
 * @brief Mints a new, unique asset user id.
 * @param self The manager; must not be NULL.
 * @param outID [out] Receives the new id (never ASSET_USER_ID_INVALID). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_GetNewUserID(AssetManager* self, AssetUserID* outID);

/**
 * @brief Drops one user's hold on one asset; unloads the asset if it was the last holder.
 * @param self The manager; must not be NULL.
 * @param assetType The asset's type.
 * @param name The asset's name, with or without its namespace; must not be NULL.
 * @param user The releasing user.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_ArgumentOutOfRange for an invalid name;
 *          ErrorCode_InvalidOperation if the asset is not loaded or the user does not hold it.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_ReleaseAsset(AssetManager* self, AssetTypeID assetType, const unsigned char* name, AssetUserID user);

/**
 * @brief Drops a user's hold on every asset it holds; each asset that reaches zero holders is unloaded.
 *
 * The user id remains valid for reuse.
 * @param self The manager; must not be NULL.
 * @param user The user to release.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an invalid user.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_ReleaseAllAssetsForUser(AssetManager* self, AssetUserID user);

/**
 * @brief Releases all of a user's assets and retires the id (it will not be handed out again).
 * @param self The manager; must not be NULL.
 * @param user The user to retire.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an invalid user.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_RetireUser(AssetManager* self, AssetUserID user);


// ---- Single load (main thread, synchronous) ----
/**
 * @brief Loads an asset (or shares an already-loaded one) and records a hold for @p user.
 *
 * In order: if the (assetType, name) asset is already loaded, returns the same wrapper and adds @p user to
 * its holder set (idempotent if already a holder). Otherwise looks up the definition. If an earlier build
 * of this definition failed and the failure is still remembered, returns that failure's error code at
 * once with a fresh message that quotes the original one; no file is touched and nothing is allocated
 * apart from the message. Otherwise builds the asset and inserts it. When the build fails, the failure is
 * remembered on the definition, logged once as an ERROR (type, name, definition file, reason), and the
 * loader's error is returned. Remembered failures are cleared when definitions, types, search roots or
 * reference resources change, and by AssetManager_ClearLoadFailures.
 *
 * The returned pointer is BORROWED, manager-owned, and valid until the asset's holder set empties (after
 * every holder releases it).
 * @param self The manager; must not be NULL.
 * @param assetType The asset's type.
 * @param name The asset's name, with or without its namespace ("desk" is "tbp:desk"); must not be NULL.
 * @param user The user taking a hold; must be a valid id.
 * @param outAsset [out] Receives the borrowed wrapper pointer on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an unknown type/invalid user;
 *          ErrorCode_ArgumentOutOfRange for an invalid name (AssetManager_IsValidAssetName);
 *          ErrorCode_InvalidOperation if no definition exists for (assetType, name); forwards the loader's
 *          error on a build failure, and that same error code (with a new message) while the failure is
 *          remembered.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_LoadAssetSingle(AssetManager* self, AssetTypeID assetType,
    const unsigned char* name, AssetUserID user, void** outAsset);


// ---- Typed convenience loaders (standard types; require AssetManager_CreateStandardAssetTypes) ----
/**
 * @brief Loads a sprite sheet by name. Thin wrapper over AssetManager_LoadAssetSingle with the standard
 *        sprite-sheet type id remembered by the manager.
 * @param self The manager; must not be NULL.
 * @param name The asset name; must not be NULL.
 * @param user The user taking a hold.
 * @param outAsset [out] Receives the sprite sheet. Must not be NULL.
 * @returns As AssetManager_LoadAssetSingle; ErrorCode_InvalidState if standard types were not registered.
 */
Error AssetManager_LoadSpriteSheet(AssetManager* self, const unsigned char* name, AssetUserID user, SpriteSheet** outAsset);

/**
 * @brief Loads a sprite animation by name. See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadSpriteAnimation(AssetManager* self, const unsigned char* name, AssetUserID user, SpriteAnimation** outAsset);

/**
 * @brief Loads a sound by name. See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadSound(AssetManager* self, const unsigned char* name, AssetUserID user, GameSound** outAsset);

/**
 * @brief Loads a font by name. See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadFont(AssetManager* self, const unsigned char* name, AssetUserID user, GameFont** outAsset);

/**
 * @brief Loads a shader by name. See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadShader(AssetManager* self, const unsigned char* name, AssetUserID user, GameShader** outAsset);

/**
 * @brief Loads a model by name. See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadModel(AssetManager* self, const unsigned char* name, AssetUserID user, GameModel** outAsset);

/**
 * @brief Loads a standalone texture by name. See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadTexture(AssetManager* self, const unsigned char* name, AssetUserID user, GameTexture** outAsset);

/**
 * @brief Loads a world model by name (WorldModel.h), with its LOD models and its variants' textures as dependencies.
 *        See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadWorldModel(AssetManager* self, const unsigned char* name, AssetUserID user,
    WorldModel** outAsset);

/**
 * @brief Loads a world sprite by name (WorldSprite.h), with its sprite animation as a dependency. See
 *        AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadWorldSprite(AssetManager* self, const unsigned char* name, AssetUserID user,
    WorldSprite** outAsset);

/**
 * @brief Loads a character by name (CharacterAsset.h). See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadCharacter(AssetManager* self, const unsigned char* name, AssetUserID user, CharacterAsset** outAsset);

/**
 * @brief Loads an animation library by name (AnimationLibrary.h): the clips of the glTF file its definition names. See
 *        AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadAnimationLibrary(AssetManager* self, const unsigned char* name, AssetUserID user,
    AnimationLibrary** outAsset);

/**
 * @brief Loads an animation graph by name (AnimationGraph.h): a copy of the graph its definition holds, checked when the
 *        definitions were read. See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadAnimationGraph(AssetManager* self, const unsigned char* name, AssetUserID user,
    AnimationGraph** outAsset);

/**
 * @brief Loads a terrain by name (TerrainAsset.h): its mesh, imported from the .glb its definition names or read from its
 *        binary form, cut into chunks, its rules, and its palette's terrain materials as its dependencies (their
 *        maps of one kind must share one size). Requires the standard types (AssetManager_CreateStandardAssetTypes) and
 *        that definitions were read. See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadTerrain(AssetManager* self, const unsigned char* name, AssetUserID user, TerrainAsset** outAsset);

/**
 * @brief Loads a terrain material by name (TerrainMaterial.h): its settings and the pixels of its maps, read from the
 *        image files of the texture assets it names (AssetManager_LoadTexturePixels). A terrain loads its palette's
 *        materials this way, as its dependencies. See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadTerrainMaterial(AssetManager* self, const unsigned char* name, AssetUserID user,
    TerrainMaterial** outAsset);

/**
 * @brief Loads a climate by name (Climate.h): its presets and settings, plain data with no dependencies. See
 *        AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadClimate(AssetManager* self, const unsigned char* name, AssetUserID user, Climate** outAsset);

/**
 * @brief Loads an input actions definition by name (InputActions.h): a category, its actions and its overrides, plain
 *        data with no dependencies. See AssetManager_LoadSpriteSheet for semantics.
 */
Error AssetManager_LoadInputActions(AssetManager* self, const unsigned char* name, AssetUserID user,
    InputActions** outAsset);

/**
 * @brief Decodes the image file a texture asset's definition names into 8-bit RGBA pixels on the CPU, for a loader that
 *        needs the pixels rather than the GPU texture (the terrain materials, whose maps become texture arrays).
 *
 * The texture asset is not loaded and no hold is taken: the definition registered under @p name (a pack's override wins,
 * as for loads) is read, its file resolved and decoded each call, and nothing is cached, neither the pixels nor a
 * failure. The definition's texture_properties do not apply (they describe the GPU texture); its color_space is
 * reported. Needs no GL context.
 *
 * DDS files are refused before they are decoded: raylib's DDS reader copies as many bytes as the file's header claims,
 * past the end of a short file (a hostile pack's), so the pixels come from png, bmp, gif and qoi images only. The size is
 * checked before the image is converted to RGBA.
 * @param self The manager; must not be NULL.
 * @param name The texture asset's name, with or without its namespace; must not be NULL.
 * @param maxSide The largest width or height to read, in pixels (the caller's limit; at least 1, and the texture type's
 *        own limit, 8192, applies above it).
 * @param outPixels [out] Receives the pixels; the caller frees them with TexturePixels_Deconstruct. Zeroed on failure.
 *        Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument or a @p maxSide below 1; ErrorCode_ArgumentOutOfRange
 *          for an invalid name; ErrorCode_InvalidState if the standard types were not registered;
 *          ErrorCode_InvalidOperation when no texture of that name is defined, or
 *          its definition is not read from an image file (one built in code); the file's errors as
 *          AssetManager_AcquireResourcePath gives them; ErrorCode_InvalidAssetData for a DDS file, an image that cannot
 *          be decoded, one with no pixels or more than @p maxSide per side (the message names both), or a compressed
 *          one.
 */
Error AssetManager_LoadTexturePixels(AssetManager* self, const unsigned char* name, int32_t maxSide,
    TexturePixels* outPixels);


// ---- Bulk load (stepped, main thread) ----
/**
 * @brief Creates a bulk load operation whose loaded assets are all attributed to @p user.
 *
 * Building the operation is cheap (no I/O). Add entries with AssetBulkOperation_AddEntry, then drive it
 * with AssetBulkOperation_CompleteStep.
 * @param self The manager; must not be NULL.
 * @param user The user that will hold every asset the operation loads; must be valid.
 * @param outOp [out] Receives the new operation on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an invalid user.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_CreateAssetBulkOperation(AssetManager* self, AssetUserID user, AssetBulkOperation** outOp);

/**
 * @brief Queues one asset in a bulk operation and returns its promise immediately (before loading).
 * @param self The operation; must not be NULL.
 * @param assetType The asset's type.
 * @param name The asset's name, with or without its namespace; must not be NULL (its full form is copied).
 * @param outPromise [out] Receives the promise (owned by the operation). May be NULL if not needed.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_ArgumentOutOfRange for an invalid name;
 *          ErrorCode_InvalidOperation if the operation has already started stepping.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetBulkOperation_AddEntry(AssetBulkOperation* self, AssetTypeID assetType,
    const unsigned char* name, PromisedAsset** outPromise);

/**
 * @brief Loads exactly one pending entry and returns; a no-op once complete.
 *
 * Call on the main thread (once or a few times per frame), rendering a loading bar in between. A FAILED
 * asset load does not fail this call: the failure is recorded on that entry's promise
 * (PromisedAsset_GetError) and the step still counts as done.
 * @param self The operation; must not be NULL.
 * @param outDidWork [out] Set true if an entry was processed, false if the operation was already complete.
 *        May be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetBulkOperation_CompleteStep(AssetBulkOperation* self, bool* outDidWork);

/**
 * @brief Reports whether every entry has been processed (succeeded or failed).
 * @param self The operation; may be NULL (returns true).
 * @returns true if complete (or @p self is NULL), false otherwise.
 */
bool AssetBulkOperation_IsComplete(AssetBulkOperation* self);

/**
 * @brief Frees a bulk operation and its promises. Loaded assets are unaffected (held by the user).
 *
 * Read the resolved assets out of the promises before deconstructing if you need them.
 * @param self The operation; may be NULL (returns success).
 * @returns Success, or the first non-success Error encountered while tearing down.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetBulkOperation_Deconstruct(AssetBulkOperation* self);

/**
 * @brief Returns the operation's progress object (borrowed, valid for the operation's lifetime).
 * @param self The operation; must not be NULL.
 * @returns The progress object, or NULL if @p self is NULL.
 */
AssetLoadProgress* AssetBulkOperation_GetProgress(AssetBulkOperation* self);

/**
 * @brief Returns processed/total as a factor in [0.0, 1.0] (1.0 when there are no entries).
 * @param self The progress object; must not be NULL.
 * @param outFactor [out] Receives the factor. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args.
 */
Error AssetLoadProgress_GetProgressFactor(AssetLoadProgress* self, double* outFactor);

/**
 * @brief Returns the total number of entries in the operation.
 * @param self The progress object; must not be NULL.
 * @param outItemCountTotal [out] Receives the total. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args.
 */
Error AssetLoadProgress_GetItemCountTotal(AssetLoadProgress* self, size_t* outItemCountTotal);

/**
 * @brief Returns the number of entries processed so far.
 * @param self The progress object; must not be NULL.
 * @param outItemCountProcessed [out] Receives the processed count. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args.
 */
Error AssetLoadProgress_GetItemCountProcessed(AssetLoadProgress* self, size_t* outItemCountProcessed);

/**
 * @brief Reports whether a promise has been resolved (its entry has been processed).
 * @param self The promise; may be NULL (returns false).
 * @returns true if resolved, false otherwise.
 */
bool PromisedAsset_IsResolved(PromisedAsset* self);

/**
 * @brief Returns the resolved asset wrapper for a promise.
 * @param self The promise; must not be NULL.
 * @param outAsset [out] Receives the borrowed wrapper pointer. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_InvalidState if not yet resolved;
 *          ErrorCode_InvalidOperation if the entry failed to load (see PromisedAsset_GetError).
 */
Error PromisedAsset_GetAsset(PromisedAsset* self, void** outAsset);

/**
 * @brief Copies the load error recorded for a promise (a success Error if it loaded fine).
 *
 * The written Error is owned by the caller; release it with Error_Deconstruct.
 * @param self The promise; must not be NULL.
 * @param outError [out] Receives a copy of the entry's load error (success if none). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_InvalidState if not yet resolved.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error PromisedAsset_GetError(PromisedAsset* self, Error* outError);


// ---- Buffer pools (manager-owned scratch/backing reuse) ----
/**
 * @brief Borrows a cleared, growable GenericBuffer with the given element size from a manager pool.
 *
 * Reuses a previously returned buffer of the same element size when available. Return it with
 * AssetManager_ReturnGenericBuffer; do not free or deconstruct it yourself, and do not use it after
 * returning.
 * @param self The manager; must not be NULL.
 * @param elementSize Bytes per element; must be greater than 0.
 * @param outBuffer [out] Receives the borrowed buffer on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_ArgumentOutOfRange if
 *          @p elementSize is 0.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_BorrowGenericBuffer(AssetManager* self, size_t elementSize, GenericBuffer** outBuffer);

/**
 * @brief Returns a buffer previously borrowed from this manager, retaining its capacity for reuse.
 * @param self The manager; must not be NULL.
 * @param buffer The buffer to return; must have come from AssetManager_BorrowGenericBuffer. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or a foreign buffer.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_ReturnGenericBuffer(AssetManager* self, GenericBuffer* buffer);
