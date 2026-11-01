#pragma once
#include <stdbool.h>
#include <stdint.h>
// Vector3 is raylib's: the light's direction.
#include "raylib/raylib.h"
#include "WorldEnvironment.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): the clouds as the world renderer draws them, pure maths tested headless
 * (tests/WorldCloudsTests.c). The cloud pass (WorldRendererClouds.c) builds one WorldCloudsFrame a frame and uploads it
 * to shaders/common/clouds.glsl, whose geometry and layers this file mirrors (keep the two in step;
 * tests/ShaderSourceTests.c pins the shared constants).
 *
 * THE LAYERS (heights above the atmosphere's ground, sea level, as the sky's):
 *  - LOW: the weather's clouds. cloud_coverage, cloud_type and cloud_density (the weather's values) between CloudBase and
 *    CloudBase + CloudThickness; cloud_density sets its extinction, WORLD_CLOUDS_LOW_EXTINCTION per metre at 1 (a dense
 *    cloud: light loses two thirds in its first 7 m, and about 430 m of it are as deep as the weather's even deck,
 *    WorldWeatherLight.h's 64 at density 1);
 *  - MID: CloudMidCoverage of altocumulus or altostratus (CloudMidType) between CloudMidBase and its thickness;
 *  - HIGH: CloudHighCoverage of cirrus or cirrocumulus (CloudHighType) as a sheet at CloudHighHeight, with an optical
 *    depth of WORLD_CLOUDS_HIGH_DEPTH straight through at CloudHighDensity 1.
 * A layer without coverage, or with no density, is not drawn at all.
 *
 * THE PATTERNS. Each layer's patterns (the shape, the erosion, the coverage map; CloudNoise.h's textures) tile every so
 * many metres (times CloudScale) and DRIFT downwind: the weather's wind times CloudWindScale (the mid layer 1.5
 * times that, the high layer 2.5 times); with CloudEvolution the shape and the erosion drift a little faster than the
 * coverage and rise, so the clouds change as they move. The drift is kept in double (WorldCloudsDrift, the renderer's
 * own state, advanced by the real seconds of each frame: the wind is in metres per real second). A shader never gets a
 * position: each pattern's ANCHOR is the frame's origin less its drift, wrapped in double by a whole number of its tiles
 * (the shaders read some patterns at whole multiples of a tile's frequency, the high layer's fibres at 2.7 times it, so
 * an anchor wraps where all of its pattern's reads repeat), and a render-space point plus the anchor is its place in the
 * pattern: a feature of the pattern stands at its place plus the drift, so the clouds move with the wind
 * (agents/WorldRuntime.md, "Render space").
 *
 * THE SHADOW MAP. The light's share that passes the clouds, for points of a level plane at the low layer's base: a square
 * of WORLD_CLOUDS_SHADOW_EXTENT metres around where the camera's ray toward the light crosses that plane, its centre on
 * the map's texel grid in the world (snapped in double), so the map's texels stand still while the camera moves. It keeps
 * the low layer's share and the upper layers' apart, so a surface inside or above the low layer takes only the part of
 * it above itself (shaders/common/lighting.glsl's CloudShadowAt).
 */


// Macros.
/** @brief The low layer's extinction per metre where the weather's cloud_density and the shape's density are both 1. */
#define WORLD_CLOUDS_LOW_EXTINCTION 0.15f
/** @brief The mid layer's extinction per metre at CloudMidDensity 1 and density 1. */
#define WORLD_CLOUDS_MID_EXTINCTION 0.04f
/** @brief The high layer's optical depth straight through at CloudHighDensity 1 and density 1. */
#define WORLD_CLOUDS_HIGH_DEPTH 1.0f
/** @brief The patterns' tiles at CloudScale 1, metres per repeat: the low layer's shape, its erosion, its coverage map, the
 *  mid layer's shape, its map, the high layer's map. */
#define WORLD_CLOUDS_SHAPE_TILE 5000.0
#define WORLD_CLOUDS_DETAIL_TILE 700.0
#define WORLD_CLOUDS_COVERAGE_TILE 80000.0
#define WORLD_CLOUDS_MID_SHAPE_TILE 2400.0
#define WORLD_CLOUDS_MID_MAP_TILE 12000.0
#define WORLD_CLOUDS_HIGH_MAP_TILE 40000.0
/** @brief The mid and the high layers' anchors wrap by this many of their maps' tiles: every scale at which the shaders read
 *  their patterns (shaders/common/clouds.glsl: the mid layer's patches at half its map's frequency, its cells at it, its
 *  shape at 5 times it; the high layer's fibres at CLOUD_HIGH_FIBRE_SCALE, 2.7 times it, its puffs at 3 times it) repeats
 *  a whole number of times over it (tests/ShaderSourceTests.c checks the shaders' scales against these). */
#define WORLD_CLOUDS_MID_ANCHOR_TILES 4
#define WORLD_CLOUDS_HIGH_ANCHOR_TILES 10
/** @brief How much faster than the low layer the mid and the high layers drift. */
#define WORLD_CLOUDS_MID_DRIFT 1.5
#define WORLD_CLOUDS_HIGH_DRIFT 2.5
/** @brief At CloudEvolution 1: how much faster than the coverage the shape and the erosion drift along the wind (a share
 *  of its drift), and how fast they rise, metres per second. */
#define WORLD_CLOUDS_SHAPE_DRIFT_SHARE 0.15
#define WORLD_CLOUDS_DETAIL_DRIFT_SHARE 0.4
#define WORLD_CLOUDS_SHAPE_RISE 0.5
#define WORLD_CLOUDS_DETAIL_RISE 1.5
/** @brief The shadow map's side, metres. */
#define WORLD_CLOUDS_SHADOW_EXTENT 24000.0
/** @brief The least a light's ray climbs per metre for the shadow map (lower lights take this: their light through the air
 *  is nearly gone). */
#define WORLD_CLOUDS_SHADOW_MIN_UP 0.02f


// Types.
/** @brief One volumetric layer as drawn: its base and top (metres above the atmosphere's ground), its coverage and type
 *  (0 .. 1) and its extinction per metre at density 1 (0: not drawn). */
typedef struct WorldCloudLayerStruct
{
    float Base;
    float Top;
    float Coverage;
    float Type;
    float Extinction;
} WorldCloudLayer;

/** @brief How far each layer's patterns have drifted with the wind, metres, in double (they grow without bound). The
 *  renderer's own state: start it with WorldCloudsDrift_Reset, advance it every frame. */
typedef struct WorldCloudsDriftStruct
{
    /** @brief The low layer's coverage map, its shape and its erosion. */
    Vector3Double LowCoverage;
    Vector3Double LowShape;
    Vector3Double LowDetail;
    /** @brief The mid layer's and the high layer's patterns. */
    Vector3Double Mid;
    Vector3Double High;
} WorldCloudsDrift;

/** @brief The frame's clouds as the shaders take them (shaders/common/clouds.glsl). A plain value; WorldClouds_GetFrame
 *  fills it. */
typedef struct WorldCloudsFrameStruct
{
    /** @brief The two volumetric layers. */
    WorldCloudLayer Low;
    WorldCloudLayer Mid;
    /** @brief The high layer: its height, coverage, type and optical depth straight through at density 1 (0: not drawn). */
    float HighHeight;
    float HighCoverage;
    float HighType;
    float HighDepth;
    /** @brief The low layer's erosion strength (0 .. 2). */
    float DetailStrength;
    /** @brief The forward lobe's asymmetry (0 .. 0.95), the clouds' colour (linear) and the town's light's multiplier. */
    float Forward;
    float Tint[3];
    float LightPollution;
    /** @brief The patterns' tiles, metres: the low layer's shape, erosion and coverage map, the mid layer's shape; the
     *  mid layer's map and the high layer's map. */
    float Tiles[4];
    float MapTiles[2];
    /** @brief The patterns' anchors for the frame's origin, metres (render-space point + anchor = place in the pattern;
     *  the origin less the drift, wrapped): the low layer's shape, erosion and coverage map, the mid layer's, the high
     *  layer's. */
    float ShapeAnchor[3];
    float DetailAnchor[3];
    float CoverageAnchor[2];
    float MidAnchor[3];
    float HighAnchor[2];
} WorldCloudsFrame;

/** @brief The shadow map's frame: the centre of its square on the plane (render space x and z), its side, the plane's
 *  height in render space (the low layer's base minus the camera's height) and the low layer's top's (the share of the
 *  low layer a surface above the plane takes is the part of the layer's room above it). */
typedef struct WorldCloudShadowFrameStruct
{
    float Center[2];
    float Extent;
    float Plane;
    float Top;
} WorldCloudShadowFrame;


// Functions.
/** @brief Starts a drift at nothing. */
void WorldCloudsDrift_Reset(WorldCloudsDrift* self);

/**
 * @brief Moves every layer's patterns with the wind for @p seconds (real time): the environment's wind (WindDirection,
 *        WindSpeed and WindGust) times CloudWindScale, the mid and high layers faster, the shape and the erosion faster
 *        still and rising by CloudEvolution. A negative or non-finite time, or a non-finite wind, moves nothing.
 */
void WorldCloudsDrift_Advance(WorldCloudsDrift* self, const WorldEnvironment* environment, double seconds);

/**
 * @brief Fills the frame's clouds from the environment (the weather's values and the clouds' settings) and the drift,
 *        for the frame's origin (the camera's world position): the layers (a layer without coverage or density gets no
 *        extinction), the look, the tiles and the anchors (the origin less the drift, wrapped in double).
 * @param environment The world's environment; must not be NULL.
 * @param drift The drift so far; must not be NULL.
 * @param origin The frame's origin.
 * @param outFrame [out] The frame's clouds.
 */
void WorldClouds_GetFrame(const WorldEnvironment* environment, const WorldCloudsDrift* drift, Vector3Double origin,
    WorldCloudsFrame* outFrame);

/** @brief Whether any layer is drawn. */
bool WorldClouds_HasAny(const WorldCloudsFrame* self);

/**
 * @brief The shadow map's frame for a light: centred where the camera's ray toward the light crosses the low layer's base
 *        (its climb at least WORLD_CLOUDS_SHADOW_MIN_UP), on the map's texel grid of @p mapSize texels in the world, with
 *        the plane's and the low layer's top's heights in render space.
 * @param self The frame's clouds.
 * @param origin The frame's origin.
 * @param viewHeight The camera's height above the atmosphere's ground (the shaders' atmosphereViewHeight).
 * @param lightDirection The light's unit direction.
 * @param mapSize The map's side in texels (at least 1).
 * @param outFrame [out] The map's frame.
 */
void WorldClouds_GetShadowFrame(const WorldCloudsFrame* self, Vector3Double origin, float viewHeight,
    Vector3 lightDirection, int32_t mapSize, WorldCloudShadowFrame* outFrame);

/**
 * @brief The first stretch of a ray from the camera inside the shell between two heights (the C mirror of
 *        shaders/common/clouds.glsl's cloudShellSegment, in double): its start and end distance.
 * @param viewHeight The camera's height above the atmosphere's ground, metres.
 * @param cosZenith The ray's zenith cosine.
 * @param base The shell's lower height.
 * @param top Its upper height (above @p base).
 * @param outStart [out] Where the ray enters it (0 inside it).
 * @param outEnd [out] Where it leaves it.
 * @returns false when the ray does not meet the shell in front of the camera, or the ground hides it first.
 */
bool WorldClouds_GetShellSegment(double viewHeight, double cosZenith, double base, double top, double* outStart,
    double* outEnd);
