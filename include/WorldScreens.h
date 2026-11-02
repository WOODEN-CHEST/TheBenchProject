#pragma once
#include <stdbool.h>
#include <stdint.h>
// Vector2 and Vector3 are raylib types in the hits (the picture's position, the ray's direction); this include is
// unavoidable here. Positions are WRVectorMath's doubles.
#include "raylib/raylib.h"
#include "wr/WRVectorMath.h"
#include "wr/WRError.h"
#include "AssetManager.h"
#include "WorldCollision.h"
#include "WorldModel.h"

// Forward declarations: the screens borrow a world (World.h) and a logger (Logger.h).
typedef struct WorldStruct World;
typedef struct LoggerStruct Logger;


/**
 * @file WorldScreens.h
 * @brief The screens: every screen component of a world (WorldScreenComponent.h) with the surface it shows on resolved
 *        on its object's world model, and the ray that finds which screen the player points at and where on its picture.
 *
 * WHAT IT HOLDS. One entry per screen component: its object, the world model its object's model component names
 * (resolved by name under the screens' own asset user, AGENTS.md rule 1) and the index of the surface the component
 * names in it (WorldModel_TryFindSurface). An entry is resolved again only when the surface the screen component names
 * changes (or the component is replaced), or the model component's world model does; a screen that cannot show anything (no model component, a world
 * model that does not load, no surface of that name) is logged once, when it happens, and points at nothing until it
 * changes.
 *
 * SYNC. WorldScreens_Sync once a tick (after the tick's moves and World_UpdateTransforms, before anything points at a
 * screen): it follows the world's screen components across additions and removals and resolves what changed. Queries
 * read each screen's placement from its object's world matrix as the last World_UpdateTransforms left it. Between syncs
 * a screen whose object or component the world removed is skipped (a query never reads a removed one), and one added
 * since is not there yet.
 *
 * POINTING. WorldScreens_Raycast finds the nearest screen a ray meets from the front (WorldModelSurface_Raycast on
 * every enabled, visible screen's surface: LOD 0's triangles of its slot) and where on its picture: (0, 0) the picture's
 * top-left, a CRT's through its curvature (the point of its bulging picture the glass shows there), the position a UI
 * on it takes as its pointer. With a collision world it
 * also asks whether anything else is in front of it: a collider of another object between the ray's origin and the
 * screen hides it (the screen's own colliders never do, so a monitor whose box encloses its glass is pointed at
 * through its box).
 *
 * POWER. WorldScreens_Advance, once a tick, moves every screen component's power transition toward its power state
 * (WorldScreenComponent.h, "POWER"): a CRT's over its surface's power-on and power-off times, any other screen's (and
 * one that shows on no surface) at once. A screen the sync meets for the first time starts settled at its power state,
 * so one added and switched off before it was ever shown does not play its power-off animation.
 *
 * THREADS AND LIFETIME. Not thread-safe. The screens borrow their world, which must outlive them. Pointers they return
 * (surfaces, world models) are valid until the next WorldScreens_Sync.
 */


// Macros.
/** @brief A collider no more than this in front of a screen (metres) does not hide it: a frame or a glass flush with
 *  the picture. */
#define WORLD_SCREENS_OCCLUSION_TOLERANCE 0.01f


// Types.
/** @brief The screens of one world. Created with WorldScreens_Create, released with WorldScreens_Destroy. */
typedef struct WorldScreensStruct WorldScreens;

/**
 * @brief One screen as the screens resolved it (WorldScreens_TryGetScreen). A plain value type; its pointers are
 *        borrowed until the next WorldScreens_Sync.
 */
typedef struct WorldScreenInfoStruct
{
    /** @brief The world model the screen's object draws. */
    const WorldModel* WorldModel;
    /** @brief The surface the screen shows on, and its index in the world model. */
    const WorldModelSurface* Surface;
    uint32_t SurfaceIndex;
} WorldScreenInfo;

/**
 * @brief Where a ray points at a screen (WorldScreens_Raycast). A plain value type; Surface is borrowed until the next
 *        WorldScreens_Sync.
 */
typedef struct WorldScreenHitStruct
{
    /** @brief The screen's object. */
    uint64_t ObjectId;
    /** @brief The surface met, and its index in the object's world model. */
    const WorldModelSurface* Surface;
    uint32_t SurfaceIndex;
    /** @brief Where on the surface: its texture coordinates, (0, 0) the glass's top-left, (1, 1) its bottom-right. */
    Vector2 Uv;
    /** @brief Where on the picture: (0, 0) its top-left, (1, 1) its bottom-right; Uv itself, but a CRT's through its
     *  curvature (WorldModelSurface_GetPicturePosition). The pointer a UI on the screen takes. */
    Vector2 Picture;
    /** @brief Whether Picture lies on the picture ([0, 1] on both axes): the ray may meet the slot's triangles beyond it
     *  (a CRT's glass beyond its curved picture). */
    bool IsOnPicture;
    /** @brief The point met, in the world (double). */
    Vector3Double Point;
    /** @brief How far from the ray's origin, in metres. */
    float Distance;
} WorldScreenHit;


// Functions.
/**
 * @brief Creates the screens of a world.
 * @param world The world; borrowed, must outlive the screens. Must not be NULL.
 * @param assets The asset manager the world models load from (under the screens' own asset user); NULL resolves none
 *        (every screen points at nothing).
 * @param logger Where the screens that cannot show anything are reported, once each; may be NULL.
 * @param outScreens [out] Receives the screens (owned by the caller: WorldScreens_Destroy). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL world or output.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error WorldScreens_Create(World* world, AssetManager* assets, Logger* logger, WorldScreens** outScreens);

/**
 * @brief Destroys the screens and releases the world models they held.
 * @param self The screens; NULL does nothing.
 */
void WorldScreens_Destroy(WorldScreens* self);

/**
 * @brief Brings the screens up to date with the world's screen components (see the file comment, "SYNC").
 * @param self The screens; must not be NULL.
 */
void WorldScreens_Sync(WorldScreens* self);

/**
 * @brief Moves every screen's power transition toward its power state by one step of time (see the file comment,
 *        "POWER"); call it once a tick, after WorldScreens_Sync.
 * @param self The screens; must not be NULL.
 * @param seconds The tick's real seconds (0, negative or NaN: only screens that change at once change).
 */
void WorldScreens_Advance(WorldScreens* self, float seconds);

/**
 * @brief Returns how one object's screen resolved.
 * @param self The screens; must not be NULL.
 * @param objectId The screen's object.
 * @param outInfo [out] Receives its world model and surface. Must not be NULL.
 * @returns true when the object has a screen component that shows on a surface of its world model (as of the last
 *          sync, and the object and that component are still in the world).
 */
bool WorldScreens_TryGetScreen(const WorldScreens* self, uint64_t objectId, WorldScreenInfo* outInfo);

/**
 * @brief Finds the nearest screen a ray points at, and where on it (see the file comment, "POINTING").
 * @param self The screens; must not be NULL.
 * @param origin Where the ray starts, in the world (the eyes, the camera).
 * @param direction Its direction (any length but 0; it is normalised).
 * @param maxDistance How far it reaches, in metres (above 0).
 * @param collision The collision world whose colliders may hide a screen, or NULL: nothing hides one.
 * @param filter What hides a screen (the colliders the ray sees; NULL: every layer, triggers ignored); its ignored
 *        object is the one pointing (the player).
 * @param outHit [out] Receives the screen and where on it. Must not be NULL.
 * @returns true when the ray meets a screen that nothing hides (also off its picture: see IsOnPicture).
 */
bool WorldScreens_Raycast(WorldScreens* self, Vector3Double origin, Vector3 direction, float maxDistance,
    WorldCollision* collision, const WorldCollisionFilter* filter, WorldScreenHit* outHit);
