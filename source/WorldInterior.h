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
 * THE SKY'S VISIBILITY. An occupancy volume around the camera holds where things that keep the sky out stand
 * (WorldInterior_GetSkyMap: WORLD_INTERIOR_SKY_MAP_EXTENT metres a side in WORLD_INTERIOR_SKY_MAP_RESOLUTION texels, its
 * centre snapped to whole texels in double, so it stands still in the world while the camera moves). Each texel is a
 * column of WORLD_INTERIOR_SKY_SLICE_COUNT height slices, one bit each, in a window from WORLD_INTERIOR_SKY_WINDOW_BELOW
 * metres below the camera (WorldInterior_GetSkyWindowBottom, also snapped). The renderer draws everything that keeps the
 * sky out into it from above, twice: once toggling the slices whose centre lies below each surface (an XOR: a slice is
 * solid where an odd number of surfaces lie above it, WorldInterior_GetSliceParityMask), so walls under a roof are solid
 * and the air under it is not; once setting the slice each surface passes through (an OR,
 * WorldInterior_GetSlicePresenceMask), so a slab thinner than a slice is not lost (above the window, the top slice: a
 * column solid at the window's top counts as going on up). A pyramid of it holds, at level k, the union of 2^k x 2^k
 * columns.
 *
 * A surface reads it in directions around it (the ambient pre-pass reads WORLD_INTERIOR_SKY_PATTERN_AZIMUTHS, turned by
 * its pixel's pattern, and blurs): from WORLD_INTERIOR_SKY_MARCH_START metres out to WORLD_INTERIOR_SKY_MARCH_END, through
 * every texel the direction crosses, one after another, each at the level whose texels are WORLD_INTERIOR_SKY_CELL_RATIO
 * of the distance wide (WorldInterior_GetSkyLevelAt: finer near, coarser far; no wall between two reads, never the
 * surface's own column). Each texel stands for the distances the direction spends in it, and tests
 * WORLD_INTERIOR_SKY_BANDS bands of elevation, each WORLD_INTERIOR_SKY_BAND_DEGREES high, against the slices their rays
 * pass over those distances (WorldInterior_GetSliceRangeMask). What it finds solid hides the elevations from its bottom
 * seen from the farthest of them to its top seen from the nearest; a band keeps open the share its blockers leave below
 * and above them. The bands start a lift above the surface (its own slices are not its roof; curbs and bumps lower than
 * it are none); a surface facing up goes on around itself as its plane (a slope's own ground is no roof); and what is
 * solid in the surface's own slice alone, solid below it too, is what it stands over (the ground at a wall's foot), no
 * wall. The zenith's cap above the bands is read in the surface's own column (an upright surface's just in front of it).
 * The visibility (WorldInterior_GetSkyVisibility) is the bands' and the cap's open share, each weighed by its solid angle
 * and the surface's cosine to it: a pebble hides a sliver of sky, a wall half of it at its foot, a roof all of it, a
 * doorway lets in the sky below its lintel, and a surface facing down is covered by what lies above it, itself too. The
 * sky's ambient then takes WorldInterior_GetSkyShare of the visibility, never less than WORLD_INTERIOR_SKY_FLOOR, and so
 * do the ground's bounce and the night's own light. Toward the map's edge, and the window's top and bottom, the
 * visibility fades to open (WorldInterior_GetSkyEdgeFade): no hard edge sweeps over shaded ground.
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
/** @brief The sky's map, the occupancy volume's grid from above: its texels a side, its side in metres, and its depth
 *  range in metres (from half of it above the camera to half below). */
#define WORLD_INTERIOR_SKY_MAP_RESOLUTION 1024
#define WORLD_INTERIOR_SKY_MAP_EXTENT 96.0
#define WORLD_INTERIOR_SKY_MAP_DEPTH 1000.0
/** @brief The occupancy volume: the height slices of a column (the bits of one RGBA32UI texel: slice k is word k / 32's
 *  bit k % 32), their height in metres, how far below the camera the window starts (snapped to whole slices), and the
 *  levels of its pyramid (level k: 2^k x 2^k texels of level 0 in one). */
#define WORLD_INTERIOR_SKY_SLICE_COUNT 128
#define WORLD_INTERIOR_SKY_SLICE_WORDS 4
#define WORLD_INTERIOR_SKY_SLICE_HEIGHT 0.25
#define WORLD_INTERIOR_SKY_WINDOW_BELOW 12.0
#define WORLD_INTERIOR_SKY_LEVEL_COUNT 8
/** @brief A surface's march: the most directions around it a read takes, the ambient pre-pass's (turned by its pixel's
 *  pattern, and blurred); how far out it reads (from past the surface's own level-0 texel, whose diagonal is shorter, to
 *  its end, in metres), through at most so many texels, each at the level whose texels are at least the ratio of the
 *  distance wide; and the bands of elevation it tests (each this many degrees high, from the horizontal; the zenith's
 *  cap above them is read in the surface's own column). */
#define WORLD_INTERIOR_SKY_MAX_AZIMUTHS 8
#define WORLD_INTERIOR_SKY_PATTERN_AZIMUTHS 2
#define WORLD_INTERIOR_SKY_MARCH_START 0.14f
#define WORLD_INTERIOR_SKY_MARCH_END 24.0f
#define WORLD_INTERIOR_SKY_MAX_CELLS 24
#define WORLD_INTERIOR_SKY_CELL_RATIO 0.35f
/** @brief How many runs of solid slices a texel's read takes one by one; the rest count as one. */
#define WORLD_INTERIOR_SKY_MAX_RUNS 4
#define WORLD_INTERIOR_SKY_BANDS 5
#define WORLD_INTERIOR_SKY_BAND_DEGREES 15.0f
/** @brief How far above a surface its bands start, in metres: upright, and facing up (a slice and a hair: the surface's
 *  own slices are not its roof, nor are curbs and bumps lower than that); between them by the normal's upward part. */
#define WORLD_INTERIOR_SKY_UPRIGHT_LIFT 0.05f
#define WORLD_INTERIOR_SKY_LIFT 0.3f
/** @brief A surface facing up more than this (a normal's upward part) is taken to go on around it as its own plane: each
 *  band is tested only above that plane at its step, raised by the plane's rise across the step's texel (at most
 *  WORLD_INTERIOR_SKY_MAX_RISE a metre), so the uphill side of a slope, which a coarse texel holds at its highest
 *  corner, is no roof. */
#define WORLD_INTERIOR_SKY_SLOPE_UP 0.5f
#define WORLD_INTERIOR_SKY_MAX_RISE 1.732f
/** @brief The zenith's weight a surface takes beside its bands', per direction, whatever it faces: a surface facing down,
 *  with no band in its hemisphere, takes the zenith's openness (an underside is covered by what lies above it). */
#define WORLD_INTERIOR_SKY_ZENITH_FLOOR 0.02f
/** @brief The share of the sky's ambient a surface keeps where every band is closed: the light that still finds its way
 *  in. */
#define WORLD_INTERIOR_SKY_FLOOR 0.15f
/** @brief Toward the map's edge a surface's visibility fades to open, over this many metres from the camera
 *  (horizontally) up to WORLD_INTERIOR_SKY_FADE_END; and over WORLD_INTERIOR_SKY_WINDOW_FADE metres toward the window's
 *  top and bottom. The volume moves with the camera, and no hard edge sweeps over shaded ground. */
#define WORLD_INTERIOR_SKY_FADE_END ((float)(WORLD_INTERIOR_SKY_MAP_EXTENT * 0.5) - 3.0f)
#define WORLD_INTERIOR_SKY_FADE_WIDTH 12.0f
#define WORLD_INTERIOR_SKY_WINDOW_FADE 2.0f
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
 * @brief The sky's map for a frame, the occupancy volume's grid (WorldInterior_GetSkyMap). A plain value.
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
 * @brief Gives one column of the occupancy volume, the C stand-in for the GPU's texture in the tests: its slices' bits at a
 *        texel of a level (all clear outside the volume).
 * @param context What the callback reads.
 * @param level The pyramid's level, 0 to WORLD_INTERIOR_SKY_LEVEL_COUNT - 1.
 * @param x The texel's column at that level (from WorldInterior_GetSkyTexel).
 * @param y Its row.
 * @param outColumn [out] Receives the column's WORLD_INTERIOR_SKY_SLICE_WORDS words.
 */
typedef void (*WorldInteriorColumnFunction)(const void* context, int level, int x, int y, uint32_t* outColumn);

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
 * @brief Returns the sky's map for a camera (the occupancy volume's grid): its centre on the texel grid nearest the
 *        camera (X and Z rounded to whole texels in double, the camera's height), looking straight down from half its
 *        depth above, its
 *        orthographic box WORLD_INTERIOR_SKY_MAP_EXTENT metres a side and WORLD_INTERIOR_SKY_MAP_DEPTH deep, in render
 *        space.
 * @param origin The frame's origin (the camera's position).
 * @returns The map.
 */
WorldInteriorSkyMap WorldInterior_GetSkyMap(Vector3Double origin);

/**
 * @brief Returns the occupancy volume's window's bottom for a camera, in render space: WORLD_INTERIOR_SKY_WINDOW_BELOW
 *        metres below the camera, on the slices' grid (whole slices from the world's height 0, in double).
 * @param origin The frame's origin (the camera's position).
 * @returns The bottom's height relative to the camera (between -WINDOW_BELOW - SLICE_HEIGHT and -WINDOW_BELOW).
 */
float WorldInterior_GetSkyWindowBottom(Vector3Double origin);

/**
 * @brief Gives the slices a surface at a height toggles in the volume's first drawing: those whose centre lies below it.
 * @param height The surface's height, render space.
 * @param windowBottom The window's bottom (WorldInterior_GetSkyWindowBottom).
 * @param outMask [out] Receives WORLD_INTERIOR_SKY_SLICE_WORDS words; must not be NULL.
 */
void WorldInterior_GetSliceParityMask(float height, float windowBottom, uint32_t* outMask);

/**
 * @brief Gives the slice a surface at a height passes through, which the volume's second drawing sets: the top one for a
 *        surface above the window (what lies above keeps the sky out of the column too), none below it.
 * @param height The surface's height, render space.
 * @param windowBottom The window's bottom.
 * @param outMask [out] Receives WORLD_INTERIOR_SKY_SLICE_WORDS words; must not be NULL.
 */
void WorldInterior_GetSlicePresenceMask(float height, float windowBottom, uint32_t* outMask);

/**
 * @brief Gives the slices a range of heights passes through: every slice from the one holding the lower height to the
 *        one holding the higher (clamped to the window), and the top slice whenever the range reaches above the window
 *        (a column solid at its top goes on up); none for a range wholly below it, or a broken one.
 * @param low The range's lower height, render space.
 * @param high Its higher height (an infinity reaches the top).
 * @param windowBottom The window's bottom.
 * @param outMask [out] Receives WORLD_INTERIOR_SKY_SLICE_WORDS words; must not be NULL.
 */
void WorldInterior_GetSliceRangeMask(float low, float high, float windowBottom, uint32_t* outMask);

/**
 * @brief Returns the pyramid's level a surface's march reads at a distance: the finest whose texels are at least
 *        WORLD_INTERIOR_SKY_CELL_RATIO of the distance wide (level 0 nearer, the coarsest farther).
 * @param distance The distance from the surface, metres.
 * @returns The level, 0 to WORLD_INTERIOR_SKY_LEVEL_COUNT - 1.
 */
int WorldInterior_GetSkyLevelAt(float distance);

/**
 * @brief Gives the volume's texel a render-space point lands on at a level, as the shaders find it: the point through the
 *        map's view-projection, its clip coordinates' texel at level 0, shifted down by the level.
 * @param map The map; must not be NULL.
 * @param x The point's X, render space.
 * @param z Its Z.
 * @param level The level, 0 to WORLD_INTERIOR_SKY_LEVEL_COUNT - 1.
 * @param outX [out] Receives the texel's column; must not be NULL.
 * @param outY [out] Receives its row; must not be NULL.
 * @returns Whether the point lies on the map (outside, nothing is known: open).
 */
bool WorldInterior_GetSkyTexel(const WorldInteriorSkyMap* map, float x, float z, int level, int* outX, int* outY);

/**
 * @brief Returns a surface's visibility of the sky from a column function standing in for the volume: the open shares of
 *        its march's bands (and of the zenith's cap, read in its own column or an upright surface's just in front of
 *        it), weighed by their solid angle and its cosine to them, as a share of all of them; faded to open toward the
 *        map's edge and the window's top and bottom (WorldInterior_GetSkyEdgeFade). Exactly 1 when nothing hides any of
 *        them.
 * @param columns The column function; must not be NULL.
 * @param context What it reads.
 * @param map The map; must not be NULL.
 * @param windowBottom The window's bottom.
 * @param position The surface's point, render space.
 * @param normal Its unit normal (the geometric one).
 * @param turn How far its directions are turned, in [0, 1) of the step between two (the pre-pass's pattern).
 * @param azimuthCount Its directions, 1 to WORLD_INTERIOR_SKY_MAX_AZIMUTHS (another counts as the most).
 * @returns The visibility, in [0, 1].
 */
float WorldInterior_GetSkyVisibility(WorldInteriorColumnFunction columns, const void* context,
    const WorldInteriorSkyMap* map, float windowBottom, Vector3 position, Vector3 normal, float turn, int azimuthCount);

/**
 * @brief Returns a surface's visibility of the sky as a lit shader reads it where the pre-pass holds none of its own
 *        (blended sprites, the crisp overlay, a frame without the pre-pass): 1 when nothing the volume holds lies
 *        straight above it (read as the zenith's cap is: in its own column, an upright surface's just in front of it),
 *        else covered; faded to open toward the volume's edges as the march is.
 * @param columns The column function; must not be NULL.
 * @param context What it reads.
 * @param map The map; must not be NULL.
 * @param windowBottom The window's bottom.
 * @param position The surface's point, render space.
 * @param normal Its unit normal (the geometric one).
 * @returns 1, or the fade (0 well inside the volume).
 */
float WorldInterior_GetSkyOverhead(WorldInteriorColumnFunction columns, const void* context,
    const WorldInteriorSkyMap* map, float windowBottom, Vector3 position, Vector3 normal);

/**
 * @brief Returns how far a surface's visibility is faded to open toward the volume's edges: 0 within
 *        WORLD_INTERIOR_SKY_FADE_END - WORLD_INTERIOR_SKY_FADE_WIDTH metres of the camera (horizontally), rising linearly
 *        to 1 at WORLD_INTERIOR_SKY_FADE_END; and rising linearly to 1 over WORLD_INTERIOR_SKY_WINDOW_FADE metres toward
 *        the window's top and bottom (and beyond them); the larger of the two.
 * @param position The surface's point, render space (the camera at its origin).
 * @param windowBottom The window's bottom.
 * @returns The fade, in [0, 1] (1 for a broken position).
 */
float WorldInterior_GetSkyEdgeFade(Vector3 position, float windowBottom);

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
