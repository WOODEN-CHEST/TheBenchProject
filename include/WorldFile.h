#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "World.h"

// Forward declaration: warnings go to a borrowed logger (Logger.h).
typedef struct LoggerStruct Logger;


/**
 * @file WorldFile.h
 * @brief World files: a World saved and loaded as JSON (authored worlds: readable, diffable) or GHDF (saves, packed
 *        stories, worlds sent over the network), both from the property tables.
 *
 * A world file holds the format version, the world's name, its next object id, its environment and its objects; each
 * object holds its id, its properties (the object table: name, parent, transform, flags, tags and data) and its
 * components (each from its type's table). Both formats are the same tree: JSON keys are the tables' keys, GHDF entry
 * ids their wire ids, so this module writes the world's structure and its loading rules once for both. Only values that
 * differ from their defaults are written. references/world_structure.md documents the format.
 *
 * LOADING IS ALL OR NOTHING. A reader builds the world in the caller's memory (@p outWorld, not constructed yet) and
 * leaves it constructed only on success; on any failure nothing is left there to release, the key registry forgets the
 * keys the read interned, and the error names the object id and the key. A caller that replaces a live world loads into
 * a second World and switches over on success, so a failed load never touches the world in use. The rules, which hold
 * for hostile input (downloaded worlds use this):
 * - the input is at most WORLD_FILE_MAX_BYTE_COUNT bytes and the format version at most WORLD_FILE_FORMAT_VERSION;
 * - the parsed tree may take at most the options' MaxTreeByteCount (WORLD_FILE_MAX_TREE_BYTES by default; the JSON and
 *   GHDF readers' estimate, charged while they parse), so a small hostile file cannot take gigabytes of memory;
 * - at most WORLD_FILE_MAX_OBJECT_COUNT objects; ids are unique, from 1 to WORLD_OBJECT_ID_MAX;
 * - every parent exists and parents form no cycle and no hierarchy deeper than WORLD_HIERARCHY_MAX_DEPTH (every object
 *   is created first, then each one's table is read and the world's own checks refuse a bad parent);
 * - every value passes its table's rules (string lengths, ranges, the tags and data limits), and the objects' data
 *   together stays within WORLD_DATA_MAX_BYTES (checked after each object);
 * - nesting is capped by the JSON and GHDF readers, and a GHDF string can declare no more than the data holds;
 * - unknown keys (JSON), unknown entries (GHDF, except those the tables read) and unknown component types are skipped,
 *   with one warning per name for the whole file, so a world from a newer game version still loads without the parts
 *   this version does not know;
 * - a next object id that is not above every object id is raised to one that is, with a warning.
 *
 * WRITING validates every object and component before anything is written (an invalid world writes nothing) and lists
 * the objects parents first: the roots in id order, each followed by its subtree depth first, children in the order
 * they were attached (which a load restores). Runtime objects (WorldObject_IsPersistent false: the player, spawned NPCs)
 * are left out with their subtrees. JSON is pretty-printed with arrays of numbers on one line and ends with a
 * newline. Saves to a path are atomic: the bytes go to a temporary file next to the target, which then replaces it.
 *
 * Paths are the caller's; the game resolves them against Services.BaseDirectory (WorldFile_BuildPath).
 *
 * Network use: the memory versions read and write byte buffers. Compressing them is possible later (raylib's
 * CompressData), but untrusted compressed data needs its own size limit before it is decompressed.
 */


// Macros.
/** @brief The world file format this game writes and the newest it reads. Raise it only for changes older games cannot
 *  read by skipping what they do not know (a renamed key, a value read another way). */
#define WORLD_FILE_FORMAT_VERSION ((uint32_t)1)
/** @brief The most objects a world file may hold. */
#define WORLD_FILE_MAX_OBJECT_COUNT ((size_t)100000)
/** @brief The most bytes a world file may have (64 MiB), checked before anything is parsed. */
#define WORLD_FILE_MAX_BYTE_COUNT ((size_t)67108864)
/**
 * @brief The most memory a world file's parsed tree may take (1 GiB), as the JSON and GHDF readers estimate it while
 *        they parse (JSONReadLimits, GHDFReadLimits).
 *
 * A document's size bounds its tree only loosely: an empty JSON object is 3 bytes and over a kilobyte parsed, an empty
 * GHDF compound in an array 1 byte and as much, so without this a 64 MiB file could ask for tens of gigabytes (and
 * running out of memory aborts the game). A typical object (a name, a transform and a component) is estimated at about
 * 6 KiB in either format, so this leaves room for WORLD_FILE_MAX_OBJECT_COUNT objects with more on them.
 */
#define WORLD_FILE_MAX_TREE_BYTES ((size_t)1073741824)
/** @brief The directory under the game's base directory where worlds live until story packs exist. */
#define WORLD_FILE_DIRECTORY ((const unsigned char*)u8"worlds")
/** @brief The file name ending of a JSON world ("test" -> "test.world.json"). */
#define WORLD_FILE_JSON_EXTENSION ((const unsigned char*)u8".world.json")
/** @brief The file name ending of a GHDF world ("test" -> "test.world.ghdf"). */
#define WORLD_FILE_GHDF_EXTENSION ((const unsigned char*)u8".world.ghdf")


// Types.
/**
 * @brief The two forms of a world file.
 */
typedef enum WorldFileFormatEnum
{
    /** @brief JSON text: authored worlds, readable and diffable (WORLD_FILE_JSON_EXTENSION). */
    WorldFileFormat_JSON,
    /** @brief GHDF binary: saves, packed stories, worlds sent over the network (WORLD_FILE_GHDF_EXTENSION). */
    WorldFileFormat_GHDF
} WorldFileFormat;

/**
 * @brief The entry ids of a GHDF world file's root compound: the save schema, never renumbered.
 */
typedef enum WorldFileRootEntryEnum
{
    /** @brief The format version (an unsigned integer). */
    WorldFileRootEntry_FormatVersion = 1,
    /** @brief The world's name (a string; absent when the world has none). */
    WorldFileRootEntry_Name = 2,
    /** @brief The next object id (an unsigned integer). */
    WorldFileRootEntry_NextObjectId = 3,
    /** @brief The environment (a compound keyed by the environment table's wire ids). */
    WorldFileRootEntry_Environment = 4,
    /** @brief The objects (an array of compounds, see WorldFileObjectEntry). */
    WorldFileRootEntry_Objects = 5
} WorldFileRootEntry;

/**
 * @brief The entry ids of a GHDF object compound that are the world file's own; the object table's wire ids start at
 *        3 to leave room for them.
 */
typedef enum WorldFileObjectEntryEnum
{
    /** @brief The object's id (an unsigned integer). */
    WorldFileObjectEntry_Id = 1,
    /** @brief The components: a compound whose entry ids are the component types' values + 1 (GHDF ids start at 1),
     *  each a compound keyed by the type table's wire ids. */
    WorldFileObjectEntry_Components = 2
} WorldFileObjectEntry;

/**
 * @brief What a world file reader needs besides the bytes. Build with WorldFileReadOptions_Create.
 */
typedef struct WorldFileReadOptionsStruct
{
    /** @brief The program's key registry, which the world borrows (its tags and data keys); required. */
    KeyRegistry* Keys;
    /** @brief The asset manager the world's components load from (World_Construct2); NULL for a world without assets
     *  (tools, tests, a headless server). */
    AssetManager* Assets;
    /** @brief The world's logger, which also hears the reader's warnings (unknown names, a raised next id); may be
     *  NULL. */
    Logger* Logger;
    /** @brief The namespace JSON keys written without one get (the story's or pack's id); NULL means
     *  NAMESPACED_KEY_DEFAULT_NAMESPACE. GHDF always holds full keys. */
    const unsigned char* DefaultNamespace;
    /** @brief The most memory the file's parsed tree may take, as the JSON and GHDF readers estimate it while they
     *  parse: WORLD_FILE_MAX_TREE_BYTES from WorldFileReadOptions_Create. Lower it for files from strangers when the
     *  worlds expected are small. Must be greater than zero. */
    size_t MaxTreeByteCount;
} WorldFileReadOptions;


// Functions.
/**
 * @brief Makes read options with the default namespace and the default tree limit (WORLD_FILE_MAX_TREE_BYTES).
 * @param keys The program's key registry (required by the readers).
 * @param assets The asset manager, or NULL.
 * @param logger The logger, or NULL.
 * @returns The options.
 */
static inline WorldFileReadOptions WorldFileReadOptions_Create(KeyRegistry* keys, AssetManager* assets, Logger* logger)
{
    return (WorldFileReadOptions)
    {
        .Keys = keys, .Assets = assets, .Logger = logger, .DefaultNamespace = NULL,
        .MaxTreeByteCount = WORLD_FILE_MAX_TREE_BYTES
    };
}

/**
 * @brief Builds a world's file path: "<baseDirectory>/worlds/<name><extension>", e.g. ".../worlds/test.world.json".
 *
 * Until story packs exist (architecture A5) every world lives in the game's worlds directory.
 * @param baseDirectory The game's base directory (Services.BaseDirectory); must not be NULL.
 * @param name The world's file name without its ending, e.g. u8"test"; must not be NULL or empty.
 * @param format The file's form, which gives its ending: WORLD_FILE_JSON_EXTENSION or WORLD_FILE_GHDF_EXTENSION.
 * @param outPath [out] A byte buffer the path is appended to, NUL-terminated. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument or an empty name; a path error from WRPath.
 */
Error WorldFile_BuildPath(const unsigned char* baseDirectory, const unsigned char* name, WorldFileFormat format,
    GenericBuffer* outPath);

/**
 * @brief Loads a world from JSON text in memory: all or nothing (see the file comment).
 * @param bytes The text (UTF-8, no terminator needed); must not be NULL unless @p byteCount is 0.
 * @param byteCount The text's length, at most WORLD_FILE_MAX_BYTE_COUNT.
 * @param options The options; must not be NULL, with Keys set.
 * @param outWorld [out] Memory for the world: not constructed; constructed holding the file's world on success, left
 *        unconstructed on failure. Must not be NULL, and must not move once constructed (World.h).
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument or Keys; ErrorCode_InvalidJSON for text that is not
 *          JSON, a document that is too long, or any broken rule (the message names the object and the key).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error WorldFile_ReadJSON(const unsigned char* bytes, size_t byteCount, const WorldFileReadOptions* options,
    World* outWorld);

/**
 * @brief Loads a world from GHDF bytes in memory: all or nothing (see the file comment).
 * @param bytes The bytes; must not be NULL unless @p byteCount is 0.
 * @param byteCount Their count, at most WORLD_FILE_MAX_BYTE_COUNT.
 * @param options The options; must not be NULL, with Keys set.
 * @param outWorld [out] Memory for the world, as for WorldFile_ReadJSON.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument or Keys; ErrorCode_Deserialize for bytes that are
 *          not a GHDF document, a document that is too long, or any broken rule (the message names the object and the
 *          entry).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error WorldFile_ReadGHDF(const unsigned char* bytes, size_t byteCount, const WorldFileReadOptions* options,
    World* outWorld);

/**
 * @brief Writes a world as JSON text, appended to a byte buffer (not terminated; it ends with a newline).
 *
 * Nothing is appended on failure.
 * @param world The world; must not be NULL. Not changed.
 * @param outBytes [out] A byte buffer the text is appended to. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument; ErrorCode_ArgumentOutOfRange for an object or
 *          component that breaks its table's rules; a serialize error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error WorldFile_WriteJSON(const World* world, GenericBuffer* outBytes);

/**
 * @brief Writes a world as a GHDF document, appended to a byte buffer.
 *
 * Nothing is appended on failure.
 * @param world The world; must not be NULL. Not changed.
 * @param outBytes [out] A byte buffer the document is appended to. Must not be NULL.
 * @returns As WorldFile_WriteJSON.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error WorldFile_WriteGHDF(const World* world, GenericBuffer* outBytes);

/**
 * @brief Loads a JSON world file: reads at most WORLD_FILE_MAX_BYTE_COUNT bytes, then WorldFile_ReadJSON.
 * @param path The file's path; must not be NULL.
 * @param options The options; must not be NULL, with Keys set.
 * @param outWorld [out] Memory for the world, as for WorldFile_ReadJSON.
 * @returns Success; a file error (ErrorCode_FileNotFound for a missing file); ErrorCode_InvalidJSON for a file that is
 *          too long; the errors of WorldFile_ReadJSON.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error WorldFile_LoadJSON(const unsigned char* path, const WorldFileReadOptions* options, World* outWorld);

/**
 * @brief Loads a GHDF world file: reads at most WORLD_FILE_MAX_BYTE_COUNT bytes, then WorldFile_ReadGHDF.
 * @param path The file's path; must not be NULL.
 * @param options The options; must not be NULL, with Keys set.
 * @param outWorld [out] Memory for the world, as for WorldFile_ReadJSON.
 * @returns Success; a file error (ErrorCode_FileNotFound for a missing file); ErrorCode_Deserialize for a file that is
 *          too long; the errors of WorldFile_ReadGHDF.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error WorldFile_LoadGHDF(const unsigned char* path, const WorldFileReadOptions* options, World* outWorld);

/**
 * @brief Saves a world as a JSON file, atomically: the text goes to "<path>.tmp", which then replaces @p path. Missing
 *        parent directories are created.
 *
 * On failure the file at @p path is unchanged (the temporary file is removed when it can be).
 * @param world The world; must not be NULL. Not changed.
 * @param path The file's path; must not be NULL.
 * @returns Success; the errors of WorldFile_WriteJSON; a file error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error WorldFile_SaveJSON(const World* world, const unsigned char* path);

/**
 * @brief Saves a world as a GHDF file, atomically, as WorldFile_SaveJSON does.
 * @param world The world; must not be NULL. Not changed.
 * @param path The file's path; must not be NULL.
 * @returns Success; the errors of WorldFile_WriteGHDF; a file error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error WorldFile_SaveGHDF(const World* world, const unsigned char* path);
