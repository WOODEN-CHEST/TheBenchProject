#pragma once
#include <stdint.h>
// AssetManager.h is needed for StandardAssetTypes (held by value). The other services are held by
// pointer and only forward-declared to keep this header light.
#include "AssetManager.h"


/**
 * @file Services.h
 * @brief A bundle of the shared, game-wide services, passed around instead of many separate arguments.
 *
 * As the game grows, most subsystems need the same handful of long-lived services: the logger, the loaded
 * Unicode database, the asset manager (and the standard asset-type ids), the game-frame manager, the game
 * config, and a GHDF object pool for world save/load. Services aggregates BORROWED pointers to them so a
 * frame or subsystem can be handed one Services* rather than a growing argument list.
 *
 * OWNERSHIP. Services owns NONE of what it points to. The composition root (main) constructs each service,
 * fills a Services with pointers to them, and tears them down itself in the correct order. Services_Deconstruct
 * is therefore a no-op. This keeps teardown ordering (which matters — GPU assets and render targets must be
 * released while the window/GL context is alive) explicit in one place.
 *
 * It is deliberately expandable: add a field for a new service and populate it in the bootstrap.
 */


// Forward declarations (these services are referenced only by pointer).
/** @brief The logger; full type in Logger.h. Borrowed. */
typedef struct LoggerStruct Logger;
/** @brief The game config; full type in Config.h. Borrowed. */
typedef struct GameConfigStruct GameConfig;
/** @brief The loaded Unicode database; full type in wr/WRUnicode.h. Borrowed. */
typedef struct UnicodeDataStruct UnicodeData;
/** @brief The game-frame manager; full type in GameFrameManager.h. Borrowed. */
typedef struct GameFrameManagerStruct GameFrameManager;
/** @brief A GHDF object pool (for world save/load); full type in wr/WRGHDF.h. Borrowed. */
typedef struct GHDFObjectPoolStruct GHDFObjectPool;


// Types.
/**
 * @brief Aggregated borrowed handles to the game's shared services.
 *
 * A plain bundle owning nothing. Populate it in the bootstrap and pass its address to subsystems that need
 * services. All pointer fields are borrowed and must outlive every user of the Services.
 */
typedef struct ServicesStruct
{
    /** @brief The logger for all program output. Borrowed. */
    Logger* Logger;
    /** @brief The loaded game configuration. Borrowed. */
    const GameConfig* Config;
    /** @brief The loaded Unicode database (for Unicode-aware string operations). Borrowed. */
    UnicodeData* Unicode;
    /** @brief The asset manager. Borrowed. */
    AssetManager* Assets;
    /** @brief The ids of the standard asset types (by value; valid once the manager registered them). */
    StandardAssetTypes AssetTypes;
    /** @brief The game-frame manager. Borrowed. */
    GameFrameManager* FrameManager;
    /** @brief A GHDF object pool for encoding/decoding world documents. Borrowed. */
    GHDFObjectPool* GHDFPool;
    /** @brief The process working directory as an absolute UTF-8 path; base for building absolute paths. Borrowed. */
    const unsigned char* WorkingDirectory;
} Services;


// Functions.
/**
 * @brief Zero-initializes a services bundle so every handle starts NULL.
 *
 * Call before populating the fields, so a partially built bundle has well-defined (NULL) contents.
 * @param self The bundle to initialize. May be NULL, in which case the call does nothing.
 */
void Services_Construct(Services* self);

/**
 * @brief Releases a services bundle.
 *
 * A no-op: Services owns none of what it references. Present for API symmetry and future-proofing.
 * @param self The bundle to release. May be NULL.
 */
void Services_Deconstruct(Services* self);
