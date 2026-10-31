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
 * config, the per-tick input, the key registry, the root registry, and a GHDF object pool for world save/load.
 * Services aggregates BORROWED pointers to them so a frame or subsystem can be handed one Services* rather than a
 * growing argument list.
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
/** @brief The per-tick input; full type in GameInput.h. Borrowed. */
typedef struct GameInputStruct GameInput;
/** @brief The program's namespaced key registry; full type in NamespacedKey.h. Borrowed. */
typedef struct KeyRegistryStruct KeyRegistry;
/** @brief The program's root registry (the registry of registries); full type in Registry.h. Borrowed. */
typedef struct RegistryRootStruct RegistryRoot;


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
    /** @brief The input, polled by the frame manager at the start of every update tick. Frames read keys, actions,
     *  the mouse and typed text through it (never through raylib), after the frames above them had first refusal.
     *  Borrowed. */
    GameInput* Input;
    /** @brief A GHDF object pool for encoding/decoding world documents. Borrowed. */
    GHDFObjectPool* GHDFPool;
    /** @brief The program's one key registry: namespaced keys (tags, object data keys) interned to ids. Every World
     *  borrows it, so it outlives every frame. Borrowed. */
    KeyRegistry* Keys;
    /** @brief The program's root registry: every system's registries under their keys (`tbp:input_action`, ...), for
     *  tools that work on every kind (the terminal, autocomplete, the editor's pickers). Systems add their registries
     *  at startup and keep direct pointers to them; nothing looks a registry up per tick. It borrows Keys. Borrowed. */
    RegistryRoot* Registries;
    /** @brief Absolute UTF-8 directory that game files (config, assets, logs, the asset cache) are resolved
     *  against: the executable's directory, or the working directory if the OS could not report it. Borrowed. */
    const unsigned char* BaseDirectory;
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
