#pragma once
#include <stdbool.h>
#include "WaterMaterial.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): water as pure maths, the C mirror of shaders/common/water.glsl (the renderer's water pass,
 * source/WorldRendererWater.c; agents/WorldRuntime.md, "Water"). tests/WaterTests.c checks it and
 * tests/ShaderSourceTests.c pins the shader's constants to these.
 *
 * THE DRIFT. A water material's patterns move along its flow: with the wind's flow at a share of the wind's speed, with
 * the fixed and uv flows at a set speed. The renderer keeps one WorldWaterDrift (in double, growing without bound) for
 * every water: the seconds the flows have run and how far the wind has moved the air (its velocity summed over every
 * frame, gusts in), so a change of wind changes how fast the waves go from then on and never where they are. A material's
 * drift is a fixed multiple of one of the two, so no material needs a state of its own.
 *
 * THE MAPS' FRAMES. Each map (the two normal maps, the foam) is laid in its own frame: turned by its turn (the second
 * normal map's), repeating every tile. A render-space point p (or, with the uv flow, the mesh's texture coordinate)
 * reads the map at (turn(p) + offset) / tile, where turn(p) is (c px + s pz, -s px + c pz) and the offset is the frame's
 * origin turned the same way less the map's drift, wrapped by the tile in double on the CPU (agents/WorldRuntime.md,
 * "Render space": a shader never gets the origin itself). So the pattern is anchored to the world and as exact 10 000 km
 * out as at the origin, and a scaled mesh (the sea) never stretches it.
 *
 * THE STRENGTH. Wind-driven seas' slopes grow with the wind: Cox and Munk's mean square slope is 0.003 + 0.00512 U
 * (U the wind's speed, m/s), so a wave's slope goes as its square root, about a third in still air of what it is in a
 * 5 m/s breeze and twice that in a 20 m/s gale. A material's wave strength is its strength in that breeze; its wind
 * response mixes between no change (0) and that growth (1), and beyond.
 *
 * UNDER THE SURFACE. The bottom is dimmed by exp(-absorption x path) over the view's path through the water (no
 * refraction: the bottom straight behind the pixel), and the water's colour scatters in over the rest. Foam shows where
 * the water is shallow: a texel of the foam's pattern shows foam while the depth below the surface is under the foam
 * depth times its value, softened over WORLD_WATER_FOAM_EDGE of the pattern's range.
 */


// Macros.
/** With the wind's flow, the waves drift at this share of the wind's speed times the material's wind response: water's
 *  surface drift, about 3 % of the wind. */
#define WORLD_WATER_WIND_DRIFT_SHARE 0.03
/** The breeze in which a material's waves have their own strength, metres a second. */
#define WORLD_WATER_REFERENCE_WIND 5.0
/** Cox and Munk's mean square slope of the sea: CALM + PER_WIND x the wind's speed (m/s). */
#define WORLD_WATER_CALM_SLOPE 0.003
#define WORLD_WATER_SLOPE_PER_WIND 0.00512
/** The fastest wind the waves' strength follows, metres a second (a hurricane). */
#define WORLD_WATER_MAX_WIND 40.0
/** The foam threshold's soft edge, a share of its pattern's range (common/water's WATER_FOAM_EDGE). */
#define WORLD_WATER_FOAM_EDGE 0.05f
/** The least roughness of the water's surface (the lit shaders' floor; common/water's WATER_MIN_ROUGHNESS). */
#define WORLD_WATER_MIN_ROUGHNESS 0.045f


// Types.
/** @brief How far every water's patterns have run: the seconds the flows have run and how far the wind has moved the
 *  air (metres along x and z, gusts in), in double (they grow without bound). The renderer's own state: start it with
 *  WorldWaterDrift_Reset, advance it every frame. */
typedef struct WorldWaterDriftStruct
{
    double Seconds;
    Vector2Double Wind;
} WorldWaterDrift;

/** @brief One map's frame for a frame's origin, as the shader takes it: the offset (the origin turned and less the
 *  map's drift, wrapped by the tile), the turn's cosine and sine, and one over the tile. */
typedef struct WorldWaterMapFrameStruct
{
    float Offset[2];
    float Cos;
    float Sin;
    float InverseTile;
} WorldWaterMapFrame;

/** @brief A water material's moving parts for one frame (WorldWater_GetFrame). */
typedef struct WorldWaterFrameStruct
{
    /** @brief The maps' frames, by WaterMaterialMap (A, B, the foam). */
    WorldWaterMapFrame Maps[WaterMaterialMap_Count];
    /** @brief Whether the maps lie on the mesh's texture coordinates (the uv flow) rather than the world's x and z. */
    bool IsAlongUv;
    /** @brief The waves' strength with the wind in (WorldWater_GetWaveStrength). */
    float WaveStrength;
} WorldWaterFrame;


// Functions.
/** @brief Starts a drift at nothing. */
void WorldWaterDrift_Reset(WorldWaterDrift* self);

/**
 * @brief Runs the flows for @p seconds (real time) and moves the air by the wind: toward (sin, 0, cos) of
 *        @p windDirection at @p windSpeed (its gust in). A negative or non-finite time moves nothing; a non-finite wind
 *        or a negative speed moves the air not at all (the flows still run).
 */
void WorldWaterDrift_Advance(WorldWaterDrift* self, float windDirection, float windSpeed, double seconds);

/**
 * @brief How far a map moving at @p speedShare of a material's flow has drifted, in the flow's frame: metres along the
 *        world's x and z for the wind's and the fixed flows, texture coordinates (u, v) for the uv flow.
 * @param settings The material's settings; must not be NULL.
 * @param drift The drift so far; must not be NULL.
 * @param speedShare The map's share of the flow's speed.
 * @returns The drift (0 for a value outside the flows or anything not finite).
 */
Vector2Double WorldWater_GetDrift(const WaterMaterialSettings* settings, const WorldWaterDrift* drift, double speedShare);

/**
 * @brief The waves' strength with the wind in: the material's strength times a mix, by its wind response, of 1 and the
 *        square root of Cox and Munk's slope at the wind's speed over the one at WORLD_WATER_REFERENCE_WIND; at least 0.
 * @param waveStrength The material's wave strength.
 * @param windResponse The material's wind response.
 * @param windSpeed The wind's speed, gust in (m/s; clamped to 0 .. WORLD_WATER_MAX_WIND, non-finite as 0).
 * @returns The strength.
 */
float WorldWater_GetWaveStrength(float waveStrength, float windResponse, float windSpeed);

/**
 * @brief A material's moving parts for a frame: each map's frame for the frame's origin (the uv flow ignores it), and the
 *        waves' strength in the frame's wind.
 * @param settings The material's settings; must not be NULL.
 * @param drift The drift so far; must not be NULL.
 * @param origin The frame's origin (the camera's world position).
 * @param windSpeed The wind's speed, gust in (m/s).
 * @param outFrame [out] The frame. Must not be NULL.
 */
void WorldWater_GetFrame(const WaterMaterialSettings* settings, const WorldWaterDrift* drift, Vector3Double origin,
    float windSpeed, WorldWaterFrame* outFrame);

/**
 * @brief Where a place lies on a map: (turn(place) + offset) x one over the tile (common/water's WaterMapCoordinate).
 * @param frame The map's frame; must not be NULL.
 * @param place A render-space point's x and z, or a texture coordinate with the uv flow.
 * @returns The map's coordinate (1 a repeat).
 */
Vector2 WorldWater_GetMapCoordinate(const WorldWaterMapFrame* frame, Vector2 place);

/**
 * @brief How much of what lies under the water still comes through over a path through it: exp(-absorption x path) per
 *        channel (common/water's WaterTransmittance). An infinite path (nothing under the water) gives 0.
 * @param absorption Per metre, per channel (at least 0).
 * @param path The view's path through the water, metres (at least 0).
 * @returns The share per channel.
 */
Vector3 WorldWater_GetTransmittance(Vector3 absorption, float path);

/**
 * @brief How much foam covers a point (common/water's WaterFoam): the foam's texel shows foam while the depth below the
 *        surface is under @p foamDepth times its value, softened over WORLD_WATER_FOAM_EDGE, times the strength.
 * @param texel The foam pattern's value there, 0 to 1.
 * @param depth The water's depth below the surface there, metres (an infinite one: none).
 * @param foamDepth The material's foam depth (0: no foam).
 * @param foamStrength The material's foam strength, 0 to 1.
 * @returns The foam's cover, 0 to 1.
 */
float WorldWater_GetFoam(float texel, float depth, float foamDepth, float foamStrength);
