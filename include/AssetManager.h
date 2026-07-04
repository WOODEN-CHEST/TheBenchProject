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
 * Definitions may be added or removed at any time; removing a definition does NOT unload an asset already
 * built from it, it only prevents future loads until the definition is re-added. Definitions are usually
 * read from files at startup with AssetManager_ReadDefinitions, but they can also be built in code and
 * registered with AssetManager_SetDefinition, or built from raw bytes with AssetManager_BuildDefinition
 * (e.g. by a custom, non-filesystem source such as a server download).
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
     * AssetManager_AcquireResourcePath), builds the custom wrapper (drawing scratch/backing storage from
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
    /** @brief Owned, NUL-terminated UTF-8 asset name, unique within the type; set by the constructor. */
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
 * directory (see AssetManager_SetCacheDirectory). Release with AssetManager_Deconstruct.
 * @param outSelf [out] Receives the new manager on success, NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p outSelf is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_Construct1(AssetManager** outSelf);

/**
 * @brief Unloads everything and frees the manager.
 *
 * Unloads all loaded assets (regardless of remaining holders), destroys all definitions and types, clears
 * the cache directory, and frees the manager itself. Teardown is best-effort: the first error is returned
 * and later errors are released so none leak. Safe to call with NULL (returns success).
 * @param self The manager to deconstruct; may be NULL.
 * @returns Success, or the first non-success Error encountered while tearing down.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_Deconstruct(AssetManager* self);


// ---- Types ----
/**
 * @brief Registers a new asset type and returns its id.
 *
 * Copies the names from @p info. The type name and directory name must each be unique among registered
 * types.
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
 * Registers the sprite sheet, sprite animation, sound, font, shader, and model types, writing their ids
 * into @p outTypes. Creates one JSONObjectPool and wires it into every standard type's ConstructorUserData
 * so all standard definition parsing recycles JSON compounds/arrays/strings instead of reallocating. The
 * caller OWNS @p *outDefinitionPool and must free it with JSONObjectPool_Deconstruct after (or alongside)
 * the manager. The same pool may be reused as the ConstructorUserData of a custom JSON-based type to
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
 * those first.
 * @param self The manager; must not be NULL.
 * @param id The type to remove.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p id is invalid/unknown;
 *          ErrorCode_InvalidOperation if definitions or loaded assets of the type remain.
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
/**
 * @brief Appends a search root at the lowest priority.
 * @param self The manager; must not be NULL.
 * @param directory NUL-terminated UTF-8 directory path; copied. Must not be NULL/empty.
 * @returns Success; ErrorCode_IllegalArgument for NULL/empty args.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_AddSearchRoot(AssetManager* self, const unsigned char* directory);

/**
 * @brief Inserts a search root at a given priority index (0 = highest priority / override winner).
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
 * Replaces any previous resolver. Passing a NULL resolver clears it.
 * @param self The manager; must not be NULL.
 * @param resolver The resolver callback, or NULL to clear.
 * @param userData Context stored by value and handed to the resolver on each call; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error AssetManager_SetReferenceResolver(AssetManager* self, AssetReferenceResolver resolver, const UserData* userData);

/**
 * @brief Registers a fixed in-memory blob under a reference name (the manager copies the bytes).
 *
 * A later reference location with this name resolves to these bytes (taking precedence over the resolver).
 * Registering the same name again replaces the bytes.
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
 * @brief Opens a resource location as a readable stream.
 *
 * For a File location: resolves @p location->Value (an extension-less relative path) under
 * @p assetType's directory across the search roots in priority order, opening the first match (the file's
 * extension is discovered automatically). For a Reference location: returns a stream over registered
 * in-memory bytes if present, else dispatches to the reference resolver. The returned stream is owned by
 * the manager; the caller must close it with AssetManager_CloseResource (NOT IOStream_Deconstruct, which
 * would not free the manager's heap wrapper and any backing buffer).
 * @param self The manager; must not be NULL.
 * @param assetType The type whose directory to resolve File locations under.
 * @param location The location to open; must not be NULL.
 * @param outStream [out] Receives the opened stream on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an unknown type; ErrorCode_FileNotFound if
 *          no root contains the file; ErrorCode_InvalidOperation if a reference cannot be resolved.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_OpenResource(AssetManager* self, AssetTypeID assetType,
    const AssetLocation* location, IOStream** outStream);

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
 * A File location resolves to its real path under the search roots (no copy). A Reference location is
 * written to a temp file in the cache directory named with @p preferredExtension, and that path is
 * returned. Release the handle with AssetManager_ReleaseResourcePath (which deletes any temp file).
 * Reference resources used this way must be self-contained single files, since a temp file cannot resolve
 * external sibling files.
 * @param self The manager; must not be NULL.
 * @param assetType The type whose directory to resolve File locations under.
 * @param location The location to resolve; must not be NULL.
 * @param preferredExtension Extension WITHOUT the dot (e.g. u8"glb") used to name a materialized temp
 *        file; ignored for File locations. May be NULL only for File locations.
 * @param outHandle [out] Receives the path handle on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an unknown type; ErrorCode_FileNotFound if
 *          no root contains the file; ErrorCode_InvalidOperation if a reference cannot be resolved.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_AcquireResourcePath(AssetManager* self, AssetTypeID assetType,
    const AssetLocation* location, const unsigned char* preferredExtension, AssetResourcePath** outHandle);

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
 * Defaults to a hidden subdirectory of the working directory. The cache directory is cleared on this
 * call, and on manager deconstruct, so a crash mid-load cannot leave orphaned temp files.
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
 * For each registered type, walks each search root's type sub-directory for definition files, reads each
 * file's bytes, builds a definition with the type's constructor, and registers it. On a (type, name)
 * collision across roots, the higher-priority (lower-index) root wins.
 * @param self The manager; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL. Individual malformed definitions may be
 *          reported per the manager's error policy.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_ReadDefinitions(AssetManager* self);

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
 * @brief Registers (or overrides) a definition by its (Type, Name); the manager takes ownership.
 *
 * The definition's Type must be set to a registered type (a builder/constructor result already carries it
 * via the type used) and Name must be non-empty. If a definition with the same (Type, Name) exists it is
 * destroyed and replaced. This is the entry point for runtime-created definitions and custom sources.
 * @param self The manager; must not be NULL.
 * @param definition The definition to register; must not be NULL, ownership transfers to the manager.
 * @returns Success; ErrorCode_IllegalArgument for NULL args, an unset/invalid Type, or an empty Name.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AssetManager_SetDefinition(AssetManager* self, AssetDefinition* definition);

/**
 * @brief Removes a definition. Does not unload any asset already built from it.
 * @param self The manager; must not be NULL.
 * @param type The definition's type.
 * @param name The definition's name; NUL-terminated UTF-8, must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_InvalidOperation if no such
 *          definition exists.
 */
Error AssetManager_RemoveDefinition(AssetManager* self, AssetTypeID type, const unsigned char* name);

/**
 * @brief Reports whether a definition with the given (type, name) is registered.
 * @param self The manager; must not be NULL.
 * @param type The type to check.
 * @param name The name to check; NUL-terminated UTF-8, must not be NULL.
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
 * @param name The asset's name; NUL-terminated UTF-8, must not be NULL.
 * @param user The releasing user.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_InvalidOperation if the asset is
 *          not loaded or the user does not hold it.
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
 * If the (assetType, name) asset is already loaded, returns the same wrapper and adds @p user to its
 * holder set (idempotent if already a holder). Otherwise resolves the definition, builds the asset, and
 * inserts it. The returned pointer is BORROWED, manager-owned, and valid until the asset's holder set
 * empties (after every holder releases it).
 * @param self The manager; must not be NULL.
 * @param assetType The asset's type.
 * @param name The asset's name; NUL-terminated UTF-8, must not be NULL.
 * @param user The user taking a hold; must be a valid id.
 * @param outAsset [out] Receives the borrowed wrapper pointer on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL args or an unknown type/invalid user;
 *          ErrorCode_InvalidOperation if no definition exists for (assetType, name); forwards the loader's
 *          error on a build failure.
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
 * @param name The asset's name; NUL-terminated UTF-8, must not be NULL (copied).
 * @param outPromise [out] Receives the promise (owned by the operation). May be NULL if not needed.
 * @returns Success; ErrorCode_IllegalArgument for NULL args; ErrorCode_InvalidOperation if the operation
 *          has already started stepping.
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
