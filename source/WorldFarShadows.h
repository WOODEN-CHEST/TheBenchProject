#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): the terrain's shadows at any distance as pure maths, tested headless
 * (tests/WorldFarShadowsTests.c). The renderer's pass is WorldRendererFarShadows.c; the lit shaders' lookup is
 * shaders/common/far_shadow.glsl, whose C mirror WorldFarShadows_GetLookup is; the sweep's passes are
 * shaders/far_shadow/sweep.frag, whose C mirror WorldFarShadows_Sweep is.
 *
 * THE MAPS. Every terrain's top surface is drawn from above into WORLD_FAR_SHADOW_MAP_COUNT nested square height maps
 * around the camera, each WORLD_FAR_SHADOW_MAP_FACTOR times as wide as the one before (8, 32 and 128 km with the
 * player's default 1024 texels a side: 8, 32 and 128 m texels, about a pixel of the picture at the distances each
 * serves). All three share one FRAME (WorldFarShadowFrame): a centre in the world, on the coarsest map's texel grid
 * (so every map's texels lie on the world's grid of their size and a map drawn again holds the same texels), and the
 * planet's curve: every height is kept relative to the centre's level and lowered by its drop from the centre (d^2 / 2R,
 * the picture's curve: WorldRenderSpace.h), so the light's rays stay straight in the maps and a far ridge stands as low
 * as the picture draws it. The maps are drawn again when the camera has moved an eighth of the finest map from the
 * centre, when a terrain changes, and when the curve or the player's settings do (WorldFarShadows_BeginFrame).
 *
 * THE SWEEP. For every texel x of a map and the light's two disc edges (the top and the bottom of the sun's or the
 * moon's disc: WorldFarShadows_GetLight), the height below which x lies in the terrain's shadow:
 *     S(x) = max over t >= t0 of ( h(x + t d) - t s )
 * with d the light's horizontal direction, s the edge's slope (the tangent of its elevation) and t0 the exclusion
 * distance, WORLD_FAR_SHADOW_EXCLUSION_TEXELS of the map's texels: casters that near are the shadow levels' (and a
 * point's own texel would shade it). A point at height y is lit by the whole disc above S_bottom, by none below S_top,
 * by a share of it between (WorldFarShadows_GetDiscShare: a disc's area above a line): the real penumbra. The max is
 * built by doubling: the first pass takes one sample at t0, pass k the max of pass k - 1's at x and at x moved 2^(k-1)
 * texels along the light (lowered by that run's climb), so WorldFarShadows_GetPassesPerMap passes cover the map's
 * diagonal. A ray that leaves the map continues in the next coarser map: the first pass adds the coarser map's finished S
 * from the ray's exit point on (from t0 if it leaves sooner), read at the exit pulled back by the coarser map's own
 * exclusion, which turns its S into one from the exit point itself. So the coarsest map is swept first. Samples past a
 * map's edge count as no ground in its own doubling.
 *
 * THE SCHEDULE (WorldFarShadows_BeginFrame). A sweep runs over several frames, WORLD_FAR_SHADOW_PASSES_PER_FRAME passes
 * a frame (the three maps' passes, then the copies into the maps the lit shaders read, all three in the frame that
 * finishes the sweep: the lit shaders read the maps at once, with the finished sweep's frame); the lit shaders keep
 * reading the last finished sweep meanwhile. A sweep starts when none runs and the maps were drawn again (or the sweep
 * of the maps as drawn was dropped), or the shadow light has turned by WORLD_FAR_SHADOW_TURN_LIMIT since the finished
 * one's (also a change from the sun to the moon), and is drawn with the light and the maps it started with. A frame
 * without the far shadows drops a sweep that runs (WorldFarShadows_SkipFrame): the next frame with them starts afresh,
 * for its own light. The maps are drawn again only between sweeps: a camera moving faster than a sweep takes reads the
 * last finished maps, each read where it was drawn.
 *
 * THE LOOKUP (WorldFarShadows_GetLookup, common/far_shadow's FarShadowAt). A point in render space (the flat world's:
 * the camera at the origin) is moved into the finished frame (the camera's offset from its centre) and lowered by its
 * drop from the centre; it reads the finest map that holds it (up to WorldFarShadows_GetEdge of its half width), and
 * across the outer WORLD_FAR_SHADOW_BLEND_SHARE of that blends into the next map's (the last map's into none). The
 * shadow is 1 - the disc's share above the point's line of sight.
 */


// Macros.
/** @brief The nested maps. */
#define WORLD_FAR_SHADOW_MAP_COUNT 3U
/** @brief The finest map's side, metres. */
#define WORLD_FAR_SHADOW_FINEST_WIDTH 8192.0
/** @brief Each map's side over the one before it. */
#define WORLD_FAR_SHADOW_MAP_FACTOR 4.0
/** @brief The maps' texels a side: the player's default and the bounds of their setting (a power of two). */
#define WORLD_FAR_SHADOW_DEFAULT_MAP_SIZE 1024
#define WORLD_FAR_SHADOW_MIN_MAP_SIZE 256
#define WORLD_FAR_SHADOW_MAX_MAP_SIZE 2048
/** @brief Casters within this many of a map's texels of a point are left out of its shadow there (the shadow levels draw
 *  those, and a point's own texel would shade it). */
#define WORLD_FAR_SHADOW_EXCLUSION_TEXELS 3.0
/** @brief The share of the finest map's side the camera may move from the frame's centre (along x or z) before the maps
 *  are drawn again (an eighth: 1 km at the default 8 km). */
#define WORLD_FAR_SHADOW_RECENTRE_SHARE 0.125
/** @brief The frame's centre's height is the camera's when the maps are drawn, rounded to a multiple of this (metres):
 *  only the heights' float range depends on it. */
#define WORLD_FAR_SHADOW_HEIGHT_STEP 1024.0
/** @brief The heights the maps hold, metres around the frame's level (the depth range they are drawn with). */
#define WORLD_FAR_SHADOW_HEIGHT_RANGE 30000.0f
/** @brief A texel no terrain covers holds this height (the cleared value): far below every ray. */
#define WORLD_FAR_SHADOW_NO_GROUND (-1.0e9f)
/** @brief How far the shadow light may turn (radians) before a finished sweep is swept again: 0.1 degrees. */
#define WORLD_FAR_SHADOW_TURN_LIMIT 0.0017453293
/** @brief The most passes a frame runs (about 0.05 ms each at the default size on a GTX 980 Ti: a sweep in five frames). */
#define WORLD_FAR_SHADOW_PASSES_PER_FRAME 8U
/** @brief A lookup's margin at its map's edge, in its texels: the exclusion's distance and the bilinear filter's. */
#define WORLD_FAR_SHADOW_EDGE_MARGIN_TEXELS (WORLD_FAR_SHADOW_EXCLUSION_TEXELS + 2.0)
/** @brief The outer share of a map's read area over which its shadow blends into the next map's (or, past the last, into
 *  none). */
#define WORLD_FAR_SHADOW_BLEND_SHARE 0.15f
/** @brief The sun's angular radius (radians): half the atmosphere's light diameter. */
#define WORLD_FAR_SHADOW_SUN_RADIUS 0.00465f


// Types.
/** @brief The frame a set of maps is drawn in and read with. A plain value type. */
typedef struct WorldFarShadowFrameStruct
{
    /** @brief The centre in the world: x and z on the coarsest map's texel grid, y on WORLD_FAR_SHADOW_HEIGHT_STEP. */
    Vector3Double Center;
    /** @brief The maps' texels a side. */
    int MapSize;
    /** @brief The curve's scale the heights were lowered with (WorldRenderFrame.CurvatureScale: 0 for a flat world). */
    float CurvatureScale;
} WorldFarShadowFrame;

/** @brief The shadow light's two disc edges as the sweep takes them. A plain value type. */
typedef struct WorldFarShadowLightStruct
{
    /** @brief The unit horizontal direction toward the light (x, z: the world's), (1, 0) for a light straight up. */
    Vector2 Toward;
    /** @brief The tangents of the elevations of the disc's top and bottom edges (the rays' climb per metre). */
    float TopSlope;
    float BottomSlope;
} WorldFarShadowLight;

/** @brief One pass of a sweep, as the sweep shader takes it (uv: a map's texture coordinates, x along the world's +x,
 *  y along +z, 0 to 1 over its side; texel centres at (i + 0.5) / size). A plain value type. */
typedef struct WorldFarShadowPassStruct
{
    /** @brief The map it sweeps (or copies) and its place among that map's passes (0: the first, from the heights; k:
     *  the doubling over 2^(k-1) texels). */
    uint32_t Map;
    uint32_t Step;
    /** @brief Whether it copies the map's finished sweep into the maps the lit shaders read (the passes after every
     *  map's sweep), else a sweep pass. */
    bool IsCopy;
    /** @brief Whether it writes the map's own target (its last pass does) or the shared scratch target: the passes take
     *  turns, so each reads what the one before wrote. */
    bool WritesMapTarget;
    /** @brief The map's side and the run along the light its sample lies at, metres (the first pass: the exclusion,
     *  WORLD_FAR_SHADOW_EXCLUSION_TEXELS of its texels; pass k: 2^(k-1) texels). */
    float Width;
    float Run;
    /** @brief The first pass's continuation into the next coarser map (none for the coarsest map): whether there is one,
     *  that map's side and its exclusion's distance (metres). */
    bool HasCoarser;
    float CoarserWidth;
    float CoarserExclusion;
} WorldFarShadowPass;
/** @brief The far shadows' schedule across frames (WorldFarShadows_BeginFrame). Zero it before the first frame; nothing
 *  to release. Read-only (underscore-prefixed) fields. */
typedef struct WorldFarShadowsStruct
{
    /** @brief Whether the lit shaders have a finished sweep to read, and its frame and light. */
    bool _hasResult;
    WorldFarShadowFrame _resultFrame;
    Vector3 _resultTowardLight;
    /** @brief Whether the maps are drawn (the heights a sweep reads), their frame, and what they were drawn for: the
     *  terrains' listing count, the player's draw and shadow distance scales. */
    bool _hasMaps;
    WorldFarShadowFrame _mapsFrame;
    uint64_t _mapsListingCount;
    float _mapsDrawDistanceScale;
    float _mapsShadowDistanceScale;
    /** @brief Whether the maps as drawn wait for a sweep to finish (they were drawn since the finished sweep started,
     *  their own sweep perhaps dropped): one is due whatever the light does. */
    bool _isSweepDue;
    /** @brief The sweep that runs: whether one does, how many of its passes ran, and the light it is swept for. */
    bool _isSweeping;
    uint32_t _sweepPassesDone;
    Vector3 _sweepTowardLight;
    float _sweepDiscRadius;
} WorldFarShadows;

/** @brief What one frame asks of the far shadows (WorldFarShadows_BeginFrame). A plain value type. */
typedef struct WorldFarShadowsInputStruct
{
    /** @brief The frame's origin (the camera's world position). */
    Vector3Double Origin;
    /** @brief The unit direction toward the shadow light, and its disc's angular radius (radians). */
    Vector3 TowardLight;
    float DiscRadius;
    /** @brief The player's map size (texels a side; sanitized: WorldFarShadows_SanitizeMapSize) and the frame's curve. */
    int MapSize;
    float CurvatureScale;
    /** @brief The terrains' listing count (WorldRenderCache_GetTerrainListingCount: a terrain changed when it moves on)
     *  and the player's draw and shadow distance scales (the maps hold the casters within their shadow distances, which
     *  both scale). */
    uint64_t TerrainListingCount;
    float DrawDistanceScale;
    float ShadowDistanceScale;
} WorldFarShadowsInput;

/** @brief What one frame does (WorldFarShadows_BeginFrame). A plain value type. */
typedef struct WorldFarShadowsWorkStruct
{
    /** @brief Whether the maps are drawn this frame (before its passes), in DrawFrame. */
    bool DrawsMaps;
    WorldFarShadowFrame DrawFrame;
    /** @brief The sweep's passes this frame: [FirstPass, FirstPass + PassCount) of WorldFarShadows_GetSweepPassCount for
     *  the maps' size (DrawFrame's, or the frame the maps were last drawn in), for the light SweepLight (valid while
     *  PassCount > 0). */
    uint32_t FirstPass;
    uint32_t PassCount;
    WorldFarShadowLight SweepLight;
    /** @brief Whether the sweep finishes this frame: the maps the lit shaders read hold it from this frame's passes on. */
    bool FinishesSweep;
} WorldFarShadowsWork;

/** @brief Where a point's far shadow comes from (WorldFarShadows_GetLookup). A plain value type. */
typedef struct WorldFarShadowLookupStruct
{
    /** @brief The map read (WORLD_FAR_SHADOW_MAP_COUNT: none holds the point) and the next map's share across its band
     *  (at the last map, the share that fades to none). */
    uint32_t Map;
    float NextWeight;
    /** @brief The point's place in the frame: x and z relative to the centre (metres), and its height there, lowered by
     *  its drop from the centre. */
    Vector2 Place;
    float Height;
} WorldFarShadowLookup;


// Functions.
/** @brief A map's side, metres: WORLD_FAR_SHADOW_FINEST_WIDTH x WORLD_FAR_SHADOW_MAP_FACTOR^map. */
double WorldFarShadows_GetMapWidth(uint32_t map);

/** @brief A map's texel, metres, for a map size. */
double WorldFarShadows_GetTexel(uint32_t map, int mapSize);

/** @brief The player's map size, sanitized: a power of two in [WORLD_FAR_SHADOW_MIN_MAP_SIZE,
 *  WORLD_FAR_SHADOW_MAX_MAP_SIZE] (another value: the nearest power of two below it, clamped). */
int WorldFarShadows_SanitizeMapSize(int mapSize);

/** @brief The passes one map's sweep takes for a map size: the first, from the heights, and enough doublings for 2^k
 *  texels to cover the map's diagonal (12 at 1024 texels). */
uint32_t WorldFarShadows_GetPassesPerMap(int mapSize);

/** @brief A whole sweep's passes for a map size: every map's (WorldFarShadows_GetPassesPerMap), then one copy per map into
 *  the maps the lit shaders read. */
uint32_t WorldFarShadows_GetSweepPassCount(int mapSize);

/**
 * @brief The frame maps drawn for a camera take: its centre at the camera's x and z rounded to the coarsest map's texel
 *        grid (in double) and its y rounded to WORLD_FAR_SHADOW_HEIGHT_STEP.
 * @param origin The camera's world position.
 * @param mapSize The maps' texels a side (sanitized).
 * @param curvatureScale The frame's curve.
 * @returns The frame.
 */
WorldFarShadowFrame WorldFarShadows_GetFrame(Vector3Double origin, int mapSize, float curvatureScale);

/**
 * @brief The view and orthographic projection that draw a map from above, for draws whose model matrices are relative to
 *        the frame's centre: view x = world x, view y = world z, view z = world y (a reflection: draw both faces), the
 *        projection's clip x and y the map's side, its depth WORLD_FAR_SHADOW_HEIGHT_RANGE either way (higher is
 *        nearer). Its clip-space up is WorldRenderSpace_GetClipUp of MatrixMultiply(view, projection).
 * @param map The map, below WORLD_FAR_SHADOW_MAP_COUNT.
 * @param outView [out] The view; must not be NULL.
 * @param outProjection [out] The projection; must not be NULL.
 */
void WorldFarShadows_GetMapMatrices(uint32_t map, Matrix* outView, Matrix* outProjection);

/**
 * @brief The light's two disc edges for a sweep.
 * @param towardLight The unit direction toward the light (any length is normalized; a zero one points up).
 * @param discRadius The disc's angular radius, radians (>= 0).
 * @returns The horizontal direction and the edges' slopes (a light within 1e-6 of straight up: slopes of 1e6, so every
 *          ray clears every ridge).
 */
WorldFarShadowLight WorldFarShadows_GetLight(Vector3 towardLight, float discRadius);

/**
 * @brief One pass of a sweep, for the shader.
 * @param pass The pass, below WorldFarShadows_GetSweepPassCount: the coarsest map's WorldFarShadows_GetPassesPerMap
 *        first, then the next finer map's, then the copies (finest last).
 * @param mapSize The maps' texels a side.
 * @returns The pass (the light's direction and slopes are the sweep's own: WorldFarShadowsWork.SweepLight).
 */
WorldFarShadowPass WorldFarShadows_GetPass(uint32_t pass, int mapSize);

/**
 * @brief Plans one frame: draws the maps when they are due and no sweep runs, starts a sweep when the maps as drawn wait
 *        for one or the light turned, and runs the next WORLD_FAR_SHADOW_PASSES_PER_FRAME passes of the sweep that runs
 *        (fewer when the copies would not all fit: they run together, in the frame that finishes the sweep).
 *
 * The maps are due when there are none, when the camera stands more than WORLD_FAR_SHADOW_RECENTRE_SHARE of the finest
 * map's side from their centre along x or z, when the map size, the curve, the terrains' listing count or the draw or
 * shadow distance scale differ from theirs. The light turned when its direction lies more than
 * WORLD_FAR_SHADOW_TURN_LIMIT from the finished sweep's. Updates @p self as though the frame's work runs.
 * @param self The schedule; must not be NULL.
 * @param input The frame's input; must not be NULL.
 * @param outWork [out] The frame's work; must not be NULL.
 */
void WorldFarShadows_BeginFrame(WorldFarShadows* self, const WorldFarShadowsInput* input, WorldFarShadowsWork* outWork);

/** @brief Notes a frame without the far shadows (switched off, no shadow light, a fade of 0): a sweep that runs is
 *  dropped, so the next frame with them starts one afresh for its own light (after drawing the maps when they are due),
 *  never going on with a light from before. The maps and the finished sweep are kept. */
void WorldFarShadows_SkipFrame(WorldFarShadows* self);

/** @brief Forgets everything (the maps, the sweep, the result): the next frame draws the maps and sweeps afresh. Call it
 *  when the textures are lost or another world is drawn. */
void WorldFarShadows_Invalidate(WorldFarShadows* self);

/** @brief A map's read area's edge, as a share of its half side: 1 less its margin (WORLD_FAR_SHADOW_EDGE_MARGIN_TEXELS
 *  of its texels). */
float WorldFarShadows_GetEdge(int mapSize);

/**
 * @brief Where a point's far shadow comes from: the C mirror of common/far_shadow's choice.
 * @param frame The finished sweep's frame; must not be NULL.
 * @param origin The frame's origin (the camera's world position): the point's render space.
 * @param renderPosition The point in render space (the flat world's).
 * @returns The map, the blend and the point's place and height in the frame.
 */
WorldFarShadowLookup WorldFarShadows_GetLookup(const WorldFarShadowFrame* frame, Vector3Double origin,
    Vector3 renderPosition);

/**
 * @brief The share of a light's disc a point sees, from where its height lies between the edges' shadow heights.
 * @param fraction 0 at the top edge's shadow height (the disc's top just shows: nothing of it) to 1 at the bottom edge's
 *        (all of it); clamped.
 * @returns The disc's area above the point's horizon line there, 0 to 1: (acos c - c sqrt(1 - c^2)) / pi with
 *          c = 1 - 2 x fraction.
 */
float WorldFarShadows_GetDiscShare(float fraction);

/**
 * @brief The shadow a pair of a texel's shadow heights casts on a point there: 1 - the disc's share it sees.
 * @param height The point's height in the frame.
 * @param topHeight The top edge's shadow height (below it nothing of the disc shows).
 * @param bottomHeight The bottom edge's (above it all of the disc shows).
 * @returns 0 (lit) to 1 (in the whole shadow).
 */
float WorldFarShadows_GetShadow(float height, float topHeight, float bottomHeight);

/**
 * @brief Sweeps one map on the CPU as the passes do on the GPU (bilinear reads clamped at the edges as GL's, in exact
 *        float weights where the GPU's filter rounds its own): the C mirror of shaders/far_shadow/sweep.frag, which the
 *        tests compare with a brute-force march.
 * @param heights The map's heights, row by row (rows along z), mapSize x mapSize; must not be NULL.
 * @param coarser The next coarser map's finished shadow heights (top, bottom per texel, its rows), or NULL for none.
 * @param map The map's index (its texel, its exclusion and the coarser map's).
 * @param mapSize Texels a side.
 * @param light The light.
 * @param scratch Room for 2 x mapSize^2 floats; must not be NULL.
 * @param outShadowHeights [out] The map's shadow heights (top, bottom per texel), 2 x mapSize^2 floats; must not be NULL.
 */
void WorldFarShadows_Sweep(const float* heights, const float* coarser, uint32_t map, int mapSize,
    const WorldFarShadowLight* light, float* scratch, float* outShadowHeights);
