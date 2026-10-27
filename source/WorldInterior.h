#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WorldAmbientZoneComponent.h"
#include "WorldSkyLight.h"
#include "raylib/raylib.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): interior lighting as pure maths, no GL: what keeps the sky's light out of rooms and what
 * lights them instead. The renderer and shaders/common/interior.glsl compute the same things on the GPU; this is their C
 * mirror (tests/ShaderSourceTests.c pins the shared constants), tested in tests/WorldInteriorTests.c.
 *
 * THE SKY'S VISIBILITY. A top-down depth map around the camera holds the topmost surface of everything that keeps the sky
 * out (WorldInterior_GetSkyMap: WORLD_INTERIOR_SKY_MAP_EXTENT metres a side in WORLD_INTERIOR_SKY_MAP_RESOLUTION texels,
 * its centre snapped to whole texels in double, so the map stands still in the world while the camera moves). A surface
 * reads it around the place it faces (WorldInterior_GetSkyRingCenter: WORLD_INTERIOR_SKY_RING_RADIUS in front of a
 * steep surface, whose sky lies on its side; the point itself for a flat one): a centre tap with WORLD_INTERIOR_SKY_INNER_TAPS
 * taps WORLD_INTERIOR_SKY_INNER_RADIUS around it, and a ring of WORLD_INTERIOR_SKY_RING_TAPS taps WORLD_INTERIOR_SKY_RING_RADIUS
 * around it (WorldInterior_GetSkyTap). A tap is covered when the map holds something above the surface there: above both
 * the surface's own height and its tangent plane extended to the tap, plus WORLD_INTERIOR_SKY_BIAS
 * (WorldInterior_GetSkyTapReference), so the uphill side of a slope does not read as a roof, nor does a curb; a surface
 * facing down is covered by anything at or above its own height, itself too (a roof's underside lies under the roof).
 * The visibility (WorldInterior_GetSkyVisibility) is the open share of the inner taps (with the centre) times the open
 * share of the ring: the ring tells how much sky lies around the place, the inner taps whether the place itself is
 * open. So a room's floor, under its roof, keeps none of the sky beyond its walls (the map cannot tell a wall from an
 * open side under the same roof), while a facade under its eaves, whose place lies in front of them, keeps most of it;
 * at a roof's edge the inner taps soften the step. The map sees only what is overhead: light that comes in sideways,
 * through doors and windows, is no map's to see. The sky's ambient then takes WorldInterior_GetSkyShare of the
 * visibility, never less than WORLD_INTERIOR_SKY_FLOOR (that light from doors and windows), and so do the ground's bounce
 * and the night's own light: a roof keeps out the light from the sunlit ground around it as much as the sky's. Toward the
 * map's edge the visibility fades to open (WorldInterior_GetSkyEdgeFade): the map moves with the camera, and no hard edge
 * sweeps over shaded ground.
 *
 * AMBIENT ZONES (WorldAmbientZoneComponent.h) in render space (WorldInterior_MakeZone): a box or a sphere with a centre,
 * three unit axes and its sizes in metres, the object's scale folded in. A point's weight in a zone
 * (WorldInterior_GetZoneWeight) rises inward from 0 at its surface to 1 at blend_distance, and a surface on or inside it
 * that faces into it (toward the zone from its nearest face) takes it whole: a room's walls, floor and ceiling lie on its
 * zone's surface and are lit by it, while a wall's outside face, beyond the zone, takes none. Beyond its surface (more
 * than WORLD_INTERIOR_ZONE_SURFACE_TOLERANCE) the weight is 0. Zones sorted by
 * priority, lowest first (WorldInterior_SelectZones' order), are laid over each other (WorldInterior_ComposeZones): each
 * group of equal priority, its members blended by their weights, covers what lies under it by its strongest member's
 * weight, so the higher priority wins where it is whole and equal ones blend. A draw takes at most
 * WORLD_INTERIOR_MAX_DRAW_ZONES zones, those its sphere touches, the highest priorities first.
 *
 * THE HAZE INSIDE. The part of the view from the camera to a point that lies inside a zone is hazed by the zone's haze
 * (WorldInterior_GetHazeDistance): the haze is read as if the point were that much nearer (a room's still air has none).
 *
 * THE CAMERA. How open the camera's surroundings are (the game measures it: WorldCollision_GetSkyOpenness) and the zones
 * at the camera ease over WORLD_INTERIOR_CAMERA_EASE_RATE (WorldInteriorCamera), and give the light the eye is in
 * (WorldInterior_GetCameraLuminance): the sky's light at the camera with its ambient taken by the openness as a surface's
 * by its visibility and the sun's and the moon's by the openness, plus the zones' own light, which counts as daylight on
 * the real scale (it is lamp light). The eye's anchor and night vision's field follow it; the zones' exposure bias
 * multiplies the exposure.
 */


// Macros.
/** @brief The most ambient zones one draw takes (the shaders' MAX_AMBIENT_ZONES). */
#define WORLD_INTERIOR_MAX_DRAW_ZONES 4U
/** @brief The sky's visibility map: its texels a side, its side in metres, and its depth range in metres (from half
 *  of it above the camera to half below). */
#define WORLD_INTERIOR_SKY_MAP_RESOLUTION 1024
#define WORLD_INTERIOR_SKY_MAP_EXTENT 96.0
#define WORLD_INTERIOR_SKY_MAP_DEPTH 1000.0
/** @brief The taps a surface reads the map at, around the place it faces: the centre, a ring of WORLD_INTERIOR_SKY_INNER_TAPS
 *  this many metres around it (is the place itself open?), and a ring of WORLD_INTERIOR_SKY_RING_TAPS this many metres
 *  around it (how much sky lies around?). */
#define WORLD_INTERIOR_SKY_INNER_TAPS 4
#define WORLD_INTERIOR_SKY_INNER_RADIUS 0.25f
#define WORLD_INTERIOR_SKY_RING_TAPS 8
#define WORLD_INTERIOR_SKY_RING_RADIUS 1.5f
/** @brief Every tap, in WorldInterior_GetSkyTap's order: the centre (0), the inner ring (1 to INNER_TAPS), the outer
 *  ring. */
#define WORLD_INTERIOR_SKY_TAP_COUNT (1 + WORLD_INTERIOR_SKY_INNER_TAPS + WORLD_INTERIOR_SKY_RING_TAPS)
/** @brief How far above a surface (and its tangent plane) the map's surface must lie to cover a tap, in metres: for a
 *  surface facing up (curbs, pebbles and a slope's own bumps do not cover it), and for an upright one, whose taps lie in
 *  front of it, away from itself (the top of a wall under a thin roof is as roofed as the rest of it); between them by
 *  the normal's upward part. */
#define WORLD_INTERIOR_SKY_BIAS 0.25f
#define WORLD_INTERIOR_SKY_UPRIGHT_BIAS 0.05f
/** @brief The share of the sky's ambient a surface keeps under a roof that covers every tap: the light that gets in
 *  through windows and doors no map shows. */
#define WORLD_INTERIOR_SKY_FLOOR 0.15f
/** @brief Toward the map's edge a surface's visibility fades to open, over this many metres from the camera
 *  (horizontally) up to WORLD_INTERIOR_SKY_FADE_END, where every tap still lies inside the map (half its extent less the
 *  taps' reach): the map moves with the camera, and no hard edge sweeps over shaded ground. */
#define WORLD_INTERIOR_SKY_FADE_END ((float)(WORLD_INTERIOR_SKY_MAP_EXTENT * 0.5) - 2.0f * WORLD_INTERIOR_SKY_RING_RADIUS)
#define WORLD_INTERIOR_SKY_FADE_WIDTH 12.0f
/** @brief The smallest upward (or downward) part of a normal the tangent plane is extended with: steeper surfaces count as
 *  walls. */
#define WORLD_INTERIOR_SKY_MIN_UP 0.001f
/** @brief A surface facing down is covered by anything higher than this below its own height (itself too). It faces
 *  down wholly from a normal 30 degrees below the horizontal (y -0.5; WORLD_INTERIOR_SKY_DOWN_SCALE), and the rule
 *  blends from an upright surface's toward it. */
#define WORLD_INTERIOR_SKY_SELF 0.02f
#define WORLD_INTERIOR_SKY_DOWN_SCALE 2.0f
/** @brief How far beyond a zone's surface a surface still counts as on it, in metres: a room's walls, built on its zone's
 *  surface, lie on it within float rounding. */
#define WORLD_INTERIOR_ZONE_SURFACE_TOLERANCE 0.01f
/** @brief How fast the camera's openness and zones ease toward where the camera is, per second (the share
 *  1 - e^(-rate x seconds) of the step a frame): a ray slipping through a door or a zone's edge never pops the eye or
 *  night vision. */
#define WORLD_INTERIOR_CAMERA_EASE_RATE 4.0f
/** @brief The most of one frame's time the camera's easing counts (one long stall does not jump it). */
#define WORLD_INTERIOR_CAMERA_MAX_DELTA_SECONDS 0.1f


// Types.
/**
 * @brief An ambient zone in render space, as the shaders evaluate it. A plain value (WorldInterior_MakeZone).
 */
typedef struct WorldInteriorZoneStruct
{
    /** @brief A box or a sphere. */
    WorldAmbientZoneShape Shape;
    /** @brief Its centre, in render space (relative to the frame's origin). */
    Vector3 Center;
    /** @brief Its three axes, unit and at right angles, in render space (the object's, without their scale). */
    Vector3 Axes[3];
    /** @brief A box's half sizes along Axes, in metres (the object's scale folded in). */
    Vector3 HalfExtents;
    /** @brief A sphere's radius, in metres (times the object's largest scale). */
    float Radius;
    /** @brief The radius of the sphere around it (a box's half diagonal), for the draws' selection. */
    float BoundsRadius;
    /** @brief How far inward from its surface it fades in, in metres (>= 0). */
    float BlendDistance;
    /** @brief Its own ambient light, linear RGB, per unit of a surface's albedo (colour x intensity). */
    float Ambient[3];
    /** @brief The sky's ambient scale, the exposure bias (stops) and the haze multiplier inside it. */
    float SkyAmbientScale;
    float ExposureBias;
    float Haze;
    /** @brief Its priority where zones overlap. */
    int32_t Priority;
} WorldInteriorZone;

/**
 * @brief What the zones give a point (WorldInterior_ComposeZones). A plain value.
 */
typedef struct WorldInteriorZoneLightStruct
{
    /** @brief The zones' own ambient light, linear RGB, per unit of albedo. */
    float Ambient[3];
    /** @brief What the sky's ambient is multiplied by (1 outside every zone). */
    float SkyAmbientScale;
    /** @brief The stops the exposure takes (0 outside every zone). */
    float ExposureBias;
    /** @brief How much of the point the zones cover, 0 (none) to 1. */
    float Weight;
} WorldInteriorZoneLight;

/**
 * @brief The sky's visibility map for a frame (WorldInterior_GetSkyMap). A plain value.
 */
typedef struct WorldInteriorSkyMapStruct
{
    /** @brief Its centre, in double: the camera's X and Z snapped to whole texels, the camera's height. */
    Vector3Double Center;
    /** @brief The map's view (looking straight down from above its centre), its orthographic projection and their
     *  product (render space -> its clip space), in render space. */
    Matrix View;
    Matrix Projection;
    Matrix ViewProjection;
} WorldInteriorSkyMap;

/**
 * @brief Returns the topmost surface's height above a point of the ground plane, the map's C stand-in for the tests:
 *        NaN where nothing is.
 * @param context What the callback reads (its heights).
 * @param x The point's X, render space.
 * @param z Its Z.
 * @returns The height (render space Y), or NaN.
 */
typedef float (*WorldInteriorHeightFunction)(const void* context, float x, float z);

/**
 * @brief The camera's interior: how open its surroundings are and the zones at it, eased over frames. A plain value:
 *        construct with WorldInteriorCamera_Construct; nothing to release.
 */
typedef struct WorldInteriorCameraStruct
{
    /** @brief How open the camera's surroundings are to the sky: 0 enclosed .. 1 the open (eased). */
    float Openness;
    /** @brief The zones at the camera (eased; WorldInterior_ComposeZones at the camera's position). */
    WorldInteriorZoneLight Zones;
    /** @brief Whether the values have been set (the first update sets them at once). */
    bool IsStarted;
} WorldInteriorCamera;


// Functions: zones.
/**
 * @brief Places an ambient zone in render space: its object's world matrix moves, turns and scales it (the matrix's
 *        columns taken apart into unit axes and their lengths; a sheared matrix's axes made square, the first kept).
 * @param component The zone; must not be NULL.
 * @param worldMatrix Its object's world matrix (in double); must not be NULL.
 * @param origin The frame's origin (render space's (0, 0, 0)).
 * @returns The zone (a zero size when the matrix collapses an axis).
 */
WorldInteriorZone WorldInterior_MakeZone(const WorldAmbientZoneComponent* component, const Matrix4Double* worldMatrix,
    Vector3Double origin);

/**
 * @brief Returns whether any point is inside a zone: a box's every half size, or a sphere's radius, above 0. A zone
 *        without an inside (a zero size, a collapsed scale) lights nothing, not even the surfaces on it: the renderer
 *        leaves it out.
 * @param zone The zone; must not be NULL.
 * @returns Whether it has an inside.
 */
bool WorldInterior_HasInside(const WorldInteriorZone* zone);

/**
 * @brief Returns a point's signed distance from a zone's surface: below 0 inside, 0 on it, above 0 outside (exact for a
 *        box and a sphere).
 * @param zone The zone; must not be NULL.
 * @param point The point, render space.
 * @returns The distance, in metres.
 */
float WorldInterior_GetZoneDistance(const WorldInteriorZone* zone, Vector3 point);

/**
 * @brief Returns the direction into a zone from a point: away from its nearest face (a box), toward its centre (a
 *        sphere); zero at a sphere's centre.
 * @param zone The zone; must not be NULL.
 * @param point The point, render space.
 * @returns The unit direction, or zero.
 */
Vector3 WorldInterior_GetZoneInward(const WorldInteriorZone* zone, Vector3 point);

/**
 * @brief Returns how much a zone weighs for a surface at a point: 0 beyond its surface (by more than
 *        WORLD_INTERIOR_ZONE_SURFACE_TOLERANCE); otherwise the larger of the fade (rising linearly inward from 0 at the
 *        surface to 1 at its blend distance; a blend distance of 0: 1 anywhere inside) and how much the surface faces into
 *        the zone (dot(normal, WorldInterior_GetZoneInward), at least 0).
 * @param zone The zone; must not be NULL.
 * @param point The point, render space.
 * @param normal The surface's unit normal, or zero for a point that is no surface (the camera: the fade alone).
 * @returns The weight, in [0, 1].
 */
float WorldInterior_GetZoneWeight(const WorldInteriorZone* zone, Vector3 point, Vector3 normal);

/**
 * @brief Picks the zones a draw takes: those whose bounds sphere its sphere touches and that reach it (their box or
 *        sphere within its sphere), at most WORLD_INTERIOR_MAX_DRAW_ZONES of them, the highest priorities first, then the
 *        nearest (the draw's centre's distance from the zone), then the first listed; written in the order the shaders
 *        lay them over each other: priority ascending, equal priorities in their listed order.
 * @param zones The zones; may be NULL when @p zoneCount is 0.
 * @param zoneCount How many.
 * @param center The draw's sphere's centre, render space.
 * @param radius Its radius (>= 0).
 * @param outSelected [out] Receives the chosen zones' indices; must hold WORLD_INTERIOR_MAX_DRAW_ZONES.
 * @returns How many were chosen.
 */
size_t WorldInterior_SelectZones(const WorldInteriorZone* zones, size_t zoneCount, Vector3 center, float radius,
    size_t* outSelected);

/**
 * @brief Returns what zones give a surface at a point, laid over each other in the given order (priority ascending,
 *        equal ones adjacent: WorldInterior_SelectZones' order): each group of equal priority, its members blended by
 *        their weights (WorldInterior_GetZoneWeight), covers what lies under it by its strongest member's weight.
 * @param zones The zones; may be NULL when @p count is 0.
 * @param order The zones to lay, by index, in order; may be NULL when @p count is 0.
 * @param count How many.
 * @param point The point, render space.
 * @param normal The surface's unit normal, or zero (the camera).
 * @returns The light (outside every zone: no ambient, sky scale 1, no bias, weight 0).
 */
WorldInteriorZoneLight WorldInterior_ComposeZones(const WorldInteriorZone* zones, const size_t* order, size_t count,
    Vector3 point, Vector3 normal);

/**
 * @brief Returns how long the camera's line of sight to a point is inside a zone: the camera at render space's origin,
 *        the point at @p distance along @p direction.
 * @param zone The zone; must not be NULL.
 * @param direction The line's direction, unit, render space.
 * @param distance The point's distance (>= 0).
 * @returns The length inside, in [0, @p distance].
 */
float WorldInterior_GetZonePathLength(const WorldInteriorZone* zone, Vector3 direction, float distance);

/**
 * @brief Returns the distance the haze in front of a point is read at: its distance from the camera, less each zone's
 *        part of the line of sight times (1 - its haze) (a zone hazier than the air adds), at least 0.
 * @param zones The zones; may be NULL when @p count is 0.
 * @param order The zones to count, by index; may be NULL when @p count is 0.
 * @param count How many.
 * @param point The point, render space (the camera at its origin).
 * @returns The distance, in metres.
 */
float WorldInterior_GetHazeDistance(const WorldInteriorZone* zones, const size_t* order, size_t count, Vector3 point);


// Functions: the sky's visibility.
/**
 * @brief Returns the sky's visibility map for a camera: its centre on the texel grid nearest the camera (X and Z
 *        rounded to whole texels in double, the camera's height), looking straight down from half its depth above, its
 *        orthographic box WORLD_INTERIOR_SKY_MAP_EXTENT metres a side and WORLD_INTERIOR_SKY_MAP_DEPTH deep, in render
 *        space.
 * @param origin The frame's origin (the camera's position).
 * @returns The map.
 */
WorldInteriorSkyMap WorldInterior_GetSkyMap(Vector3Double origin);

/**
 * @brief Returns the centre of a surface's ring of taps: the point moved RING_RADIUS times its normal's horizontal part
 *        (in front of a wall, a little downhill on a slope, the point itself on flat ground).
 * @param position The surface's point, render space.
 * @param normal Its unit normal.
 * @returns The ring's centre (its height the point's).
 */
Vector3 WorldInterior_GetSkyRingCenter(Vector3 position, Vector3 normal);

/**
 * @brief Returns where one of a surface's taps lies on the ground plane: tap 0 the place it faces
 *        (WorldInterior_GetSkyRingCenter), taps 1 to INNER_TAPS on the inner ring (the diagonals, from +X+Z turning
 *        toward -X+Z), the rest on the outer ring (from +X toward +Z, every 360 / RING_TAPS degrees).
 * @param position The surface's point, render space.
 * @param normal Its unit normal.
 * @param tap The tap, 0 to WORLD_INTERIOR_SKY_TAP_COUNT - 1 (another counts as 0).
 * @returns The tap's X and Z (as x and y).
 */
Vector2 WorldInterior_GetSkyTap(Vector3 position, Vector3 normal, int tap);

/**
 * @brief Returns the height the map's surface must top at a tap to cover a surface: for a surface facing up or upright
 *        the higher of its own height and its tangent plane extended to the tap (a normal whose upward or downward part is
 *        less than WORLD_INTERIOR_SKY_MIN_UP counts as that steep), plus a bias from WORLD_INTERIOR_SKY_UPRIGHT_BIAS
 *        (upright) to WORLD_INTERIOR_SKY_BIAS (facing up) by the normal's upward part; blended toward
 *        WORLD_INTERIOR_SKY_SELF below its own height as it faces down (wholly from a normal y of
 *        -1 / WORLD_INTERIOR_SKY_DOWN_SCALE).
 * @param position The surface's point, render space.
 * @param normal Its unit normal.
 * @param tap The tap's X and Z (WorldInterior_GetSkyTap).
 * @returns The height, render space Y.
 */
float WorldInterior_GetSkyTapReference(Vector3 position, Vector3 normal, Vector2 tap);

/**
 * @brief Returns a surface's visibility of the sky from a height function standing in for the map: the open share of
 *        the centre and the inner taps times the open share of the outer ring, a tap covered when the height there is
 *        above its reference (WorldInterior_GetSkyTapReference); a tap with no height (NaN) is open; faded to open toward
 *        the map's edge (WorldInterior_GetSkyEdgeFade). Exactly 1 when every tap is open.
 * @param heights The height function; must not be NULL.
 * @param context What it reads.
 * @param position The surface's point, render space.
 * @param normal Its unit normal.
 * @returns The visibility, in [0, 1].
 */
float WorldInterior_GetSkyVisibility(WorldInteriorHeightFunction heights, const void* context, Vector3 position,
    Vector3 normal);

/**
 * @brief Returns how far a surface's visibility is faded to open toward the sky's visibility map's edge: 0 within
 *        WORLD_INTERIOR_SKY_FADE_END - WORLD_INTERIOR_SKY_FADE_WIDTH metres of the camera (horizontally), rising linearly
 *        to 1 at WORLD_INTERIOR_SKY_FADE_END.
 * @param position The surface's point, render space (the camera at its origin).
 * @returns The fade, in [0, 1] (1 for a broken position).
 */
float WorldInterior_GetSkyEdgeFade(Vector3 position);

/**
 * @brief Returns the share of the sky's light a surface takes at a visibility: 1 - (1 - FLOOR) x (1 - visibility), so
 *        an open surface takes exactly all of it and a covered one WORLD_INTERIOR_SKY_FLOOR.
 * @param visibility The visibility, in [0, 1] (clamped).
 * @returns The share.
 */
float WorldInterior_GetSkyShare(float visibility);


// Functions: the camera.
/**
 * @brief Makes a camera's interior that has not started (its first update sets it at once).
 * @param self The state (overwritten); must not be NULL.
 */
void WorldInteriorCamera_Construct(WorldInteriorCamera* self);

/**
 * @brief Moves the camera's interior on by a frame, toward an openness and the zones at the camera: the share
 *        1 - e^(-WORLD_INTERIOR_CAMERA_EASE_RATE x seconds) of the step (at most WORLD_INTERIOR_CAMERA_MAX_DELTA_SECONDS
 *        counted); the first update sets them at once.
 * @param self The state; must not be NULL.
 * @param openness How open the surroundings are, clamped to [0, 1] (NaN counts as 1).
 * @param zones The zones at the camera; must not be NULL.
 * @param deltaSeconds The frame's time; one that is not positive (or NaN) moves nothing.
 */
void WorldInteriorCamera_Update(WorldInteriorCamera* self, float openness, const WorldInteriorZoneLight* zones,
    float deltaSeconds);

/**
 * @brief Returns the light the eye is in at the camera: the sky's light there, its ambient (the sky, the ground's bounce,
 *        the night's own light) times the share an openness gives a surface by its visibility (WorldInterior_GetSkyShare)
 *        and the zones' sky scale, the sun's and the moon's light times the openness, as a scene luminance
 *        (WorldSkyLight_GetSceneLuminance), plus the zones' own light; and the daylight's part of it
 *        (WorldSkyLight_GetDayLuminance of the same), plus the zones' light, which is lamp light on the real scale.
 *        Without the camera started, or open (1) and outside every zone, exactly the sky's.
 * @param light The sky's light at the camera; must not be NULL.
 * @param camera The camera's interior; must not be NULL.
 * @param outScene [out] Receives the scene luminance (the game's units); must not be NULL.
 * @param outDay [out] Receives its daylight part; must not be NULL.
 */
void WorldInterior_GetCameraLuminance(const WorldSkyLightFrame* light, const WorldInteriorCamera* camera,
    float* outScene, float* outDay);
