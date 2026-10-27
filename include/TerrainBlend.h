#pragma once
#include <stdbool.h>
#include <stdint.h>
// TerrainAsset.h for the rules, the palette's materials and the mesh a point lies on; WRVectorMath.h for positions in
// double and the terrain object's world matrix; raylib.h for the float vectors (normals).
#include "TerrainAsset.h"
#include "TerrainMaterial.h"
#include "raylib/raylib.h"
#include "wr/WRVectorMath.h"


/**
 * @file TerrainBlend.h
 * @brief The terrain's material blend in C: what the terrain shader (shaders/terrain/fragment.frag) works out per
 *        pixel, mirrored as the dither is (WorldVisibility.h), so tests, the scattering's limits (not built yet),
 *        footsteps and anything else that asks "what is the ground here" get the shader's answer.
 *
 * Per pixel, in order:
 * 1. **The painted weights**: a vertex's sixteen weight bytes (one per palette material, byte / 255), interpolated over
 *    its triangle and divided by their sum; a point without any weight (a sum below TERRAIN_BLEND_WEIGHT_EPSILON) is the
 *    first material alone. A face with a face material is that material alone, and nothing below changes it.
 * 2. **The rules** (TerrainRule, the terrain's "rules"), in order: each one's strength is its opacity times its slope's,
 *    its height's and its noise's factors (TerrainBlend_GetRuleStrength); every material it covers gives up that share of
 *    its weight to the rule's material. The slope is the angle between the ground's normal in the world and straight up
 *    (0 flat, 90 a wall, 180 an overhang's underside); the height is the world's Y (absolute: a snow line stays where it
 *    is when a terrain moves); the noise is value noise in the terrain's own space (TerrainBlend_GetValueNoise) that
 *    repeats every TERRAIN_BLEND_NOISE_PERIOD cells.
 * 3. **Height blending** (TerrainBlend_GetHeightShares): each material with weight left scores its weight plus its height
 *    times its height_blend_strength; those within TERRAIN_BLEND_HEIGHT_BAND of the best score share the pixel by how far
 *    above the band's floor they are, the rest drop out. Where a stony material meets grass, its stones' tops win first:
 *    crisp, natural transitions at pixel scale.
 * 4. **Sampling**: a material's maps are laid along the terrain's own axes (its object's turn and place carry them, as
 *    a model's textures go with the model), tile_size metres a repeat, from above (planar) or from three sides
 *    (triplanar: TerrainBlend_GetTriplanarWeights, the normal's components to the fourth power, so the sides hand over
 *    quickly; "steep" from its triplanar_slope, ramped in over TERRAIN_BLEND_TRIPLANAR_RAMP_DEGREES).
 *    TerrainBlend_GetProjectionCoordinates gives each side's texture coordinates, as the shader's.
 *
 * WHERE A POINT IS (TerrainBlendPlace): the textures and the noise read the point in the terrain's space (within 8 km of
 * its origin: the renderer wraps each chunk's centre by every period in double, and the shader adds its exact offset from
 * the centre), the rules' slope and height read the world's. TerrainBlend_GetPlace works the place out from a point of a
 * triangle and the terrain object's world matrix.
 *
 * The C side samples the base level of the height maps (bilinear, TerrainMaterial_SampleHeight), where the GPU blends mip
 * levels with distance: the answer near the camera is the same, and far away the C side is the exact one. It always
 * uses the materials' real heights: a renderer without texture arrays shows the mean colours blended on flat heights, so
 * its picture's dominant material can differ there from this answer, which never depends on the GPU (gameplay asks
 * here). Pure functions; no allocation; thread-safe.
 */


// Macros.
/** @brief The band below the best score within which materials share a pixel (height blending). The terrain shader's
 *  HEIGHT_BLEND_BAND is the same value (a test reads it from the file). */
#define TERRAIN_BLEND_HEIGHT_BAND 0.1f
/** @brief A weight below this is none (half of one weight byte's step): such a material is not sampled, and a point whose
 *  weights sum below it is the first material alone. The shader's WEIGHT_EPSILON is the same value. */
#define TERRAIN_BLEND_WEIGHT_EPSILON (0.5f / 255.0f)
/** @brief The noise repeats every this many cells along each axis (a power of two): the shader's VALUE_NOISE_PERIOD. */
#define TERRAIN_BLEND_NOISE_PERIOD 256U
/** @brief Over how many degrees around its triplanar_slope a "steep" material turns from planar to triplanar (centred on
 *  it), so no seam shows. The shader's TRIPLANAR_RAMP_DEGREES. */
#define TERRAIN_BLEND_TRIPLANAR_RAMP_DEGREES 10.0f


// Types.
/**
 * @brief Where a point of a terrain is, as the blend reads it. A plain value type; TerrainBlend_GetPlace fills one.
 */
typedef struct TerrainBlendPlaceStruct
{
    /** @brief The point in the terrain's own space (metres): its textures and its rules' noise. */
    Vector3Double TerrainPosition;
    /** @brief The ground's unit normal in the terrain's space: its projections (triplanar weights, each side's frame). */
    Vector3 TerrainNormal;
    /** @brief The point's height in the world (its Y, metres): the rules' height. */
    double WorldHeight;
    /** @brief The ground's unit normal in the world: the rules' slope. */
    Vector3 WorldNormal;
} TerrainBlendPlace;

/**
 * @brief The three projections of a triplanar material, each a texture coordinate pair (in repeats of its maps).
 *
 * Each side's image stands upright and unmirrored as seen from outside (its top toward +Y on the walls; on the ground its
 * top toward +Z), which fixes each side's tangent frame (the shader's Whiteout frames), all in the terrain's space.
 */
typedef struct TerrainProjectionCoordinatesStruct
{
    /** @brief The side facing along X: u along -Z (+Z on the -X side), v down the terrain's Y. */
    Vector2 X;
    /** @brief The top (and bottom): u along -X (+X seen from below), v along -Z. */
    Vector2 Y;
    /** @brief The side facing along Z: u along +X (-X on the -Z side), v down the terrain's Y. */
    Vector2 Z;
} TerrainProjectionCoordinates;

/**
 * @brief What the blend gives at one point of a terrain (TerrainBlend_EvaluatePoint). A plain value type.
 */
typedef struct TerrainBlendPointStruct
{
    /** @brief Whether the point's triangle has a face material (then every weight below is that material alone). */
    bool IsFaceMaterial;
    /** @brief The painted weights, interpolated and divided by their sum (one per palette index; 0 past the palette). */
    float PaintedWeights[TERRAIN_MAX_MATERIALS];
    /** @brief The weights after the rules. */
    float RuleWeights[TERRAIN_MAX_MATERIALS];
    /** @brief Each material's share of the point after height blending (they sum to 1). */
    float Shares[TERRAIN_MAX_MATERIALS];
    /** @brief The material with the largest share (the lowest index among equal ones): what the ground is here. */
    uint32_t DominantMaterial;
} TerrainBlendPoint;


// Functions: the steps.
/**
 * @brief Turns a vertex's weight bytes into weights: byte / 255 for the palette's materials, 0 past it.
 * @param bytes TERRAIN_MAX_MATERIALS bytes (TerrainMesh_GetWeights; on the GPU four normalized u8 x 4 attributes, bytes
 *        4k to 4k + 3 the k-th); must not be NULL.
 * @param materialCount The palette's size (1 to TERRAIN_MAX_MATERIALS; larger is clamped).
 * @param outWeights [out] TERRAIN_MAX_MATERIALS weights.
 */
void TerrainBlend_UnpackWeights(const uint8_t* bytes, uint32_t materialCount, float* outWeights);

/**
 * @brief Divides weights by their sum; weights summing below TERRAIN_BLEND_WEIGHT_EPSILON become the first material
 *        alone (an unpainted vertex shows the palette's first material).
 * @param ioWeights TERRAIN_MAX_MATERIALS weights (those past @p materialCount are left as they are).
 * @param materialCount The palette's size.
 * @returns false when the weights summed to nothing (and the first material was given the point).
 */
bool TerrainBlend_NormalizeWeights(float* ioWeights, uint32_t materialCount);

/**
 * @brief A rule condition's factor for a value: 1 inside the range, a smooth step (3t^2 - 2t^3) to 0 across Blend
 *        centred on each bound, a hard step without a blend (the bound inside); 1 for a condition the rule does not have.
 * @param range The condition; must not be NULL.
 * @param value The ground's value (degrees of slope, metres of height).
 * @returns The factor, 0 to 1.
 */
float TerrainBlend_GetRangeFactor(const TerrainRuleRange* range, float value);

/**
 * @brief The slope of ground with this normal: the angle from straight up, in degrees.
 * @param normal The ground's unit normal.
 * @returns 0 for flat ground, 90 for a wall, 180 for a flat underside.
 */
float TerrainBlend_GetSlopeDegrees(Vector3 normal);

/**
 * @brief Smooth value noise at a point: random values at the integer cells, blended with a quintic fade, repeating
 *        every TERRAIN_BLEND_NOISE_PERIOD cells along each axis. The shader's ValueNoise (shaders/common/value_noise.glsl)
 *        hashes the same integers (pcg3d), so the two agree within float rounding.
 * @param cellPosition The point in noise cells (a position divided by the cell size).
 * @param seed Picks the pattern.
 * @returns The noise, 0 to 1.
 */
float TerrainBlend_GetValueNoise(Vector3 cellPosition, uint32_t seed);

/**
 * @brief Wraps a coordinate into [0, @p period): how the renderer hands a chunk's place to a periodic pattern (texture
 *        coordinates, the noise) so the shader's float sum stays precise (agents/WorldRuntime.md, "Render space").
 * @param value The coordinate (in double).
 * @param period The pattern's period (above 0).
 * @returns value - period x floor(value / period).
 */
double TerrainBlend_WrapCoordinate(double value, double period);

/**
 * @brief A rule's strength at a point: its opacity times its slope's, height's and noise's factors.
 * @param rule The rule; must not be NULL.
 * @param place Where the point is (the slope from its world normal, the height from its world height, the noise from
 *        its position in the terrain's space); must not be NULL.
 * @returns The strength, 0 to 1.
 */
float TerrainBlend_GetRuleStrength(const TerrainRule* rule, const TerrainBlendPlace* place);

/**
 * @brief Applies rules to normalized weights, in order: each moves its strength's share of the weight of every material
 *        it covers (other than its own) to its material. The sum stays 1.
 * @param rules The rules; may be NULL when @p ruleCount is 0.
 * @param ruleCount How many.
 * @param materialCount The palette's size (rules naming a material past it do nothing).
 * @param place Where the point is; must not be NULL.
 * @param ioWeights TERRAIN_MAX_MATERIALS normalized weights, changed in place.
 */
void TerrainBlend_ApplyRules(const TerrainRule* rules, uint32_t ruleCount, uint32_t materialCount,
    const TerrainBlendPlace* place, float* ioWeights);

/**
 * @brief Height blending: each material whose weight is at least TERRAIN_BLEND_WEIGHT_EPSILON scores its weight plus its
 *        height times its strength; those within TERRAIN_BLEND_HEIGHT_BAND of the best share the point in proportion to
 *        how far they reach above the band's floor (best - band), the others get nothing.
 *
 * A single material takes the point alone; equal scores share it equally.
 * @param weights TERRAIN_MAX_MATERIALS weights (after the rules).
 * @param heights TERRAIN_MAX_MATERIALS heights, 0 to 1 (each material's height map at the point; only those with weight
 *        are read).
 * @param strengths TERRAIN_MAX_MATERIALS height blend strengths.
 * @param materialCount The palette's size.
 * @param outShares [out] TERRAIN_MAX_MATERIALS shares, summing to 1 (0 past the palette). With no weight at all, the
 *        first material takes the point.
 */
void TerrainBlend_GetHeightShares(const float* weights, const float* heights, const float* strengths,
    uint32_t materialCount, float* outShares);

/**
 * @brief The material with the largest share (the lowest palette index among equal ones).
 * @param shares TERRAIN_MAX_MATERIALS shares.
 * @param materialCount The palette's size (at least 1).
 * @returns Its palette index.
 */
uint32_t TerrainBlend_GetDominantMaterial(const float* shares, uint32_t materialCount);

/**
 * @brief How much each of a material's projections counts at ground with this normal: the side along X, the top and
 *        bottom, the side along Z (summing to 1).
 * @param normal The ground's unit normal in the terrain's space.
 * @param mode The material's triplanar mode.
 * @param triplanarSlope Its triplanar slope, in radians (for TerrainTriplanarMode_Steep).
 * @returns The weights: (0, 1, 0) when planar; the normal's components to the fourth power, normalized, when triplanar; for
 *          "steep" a smooth step between the two across TERRAIN_BLEND_TRIPLANAR_RAMP_DEGREES around the slope.
 */
Vector3 TerrainBlend_GetTriplanarWeights(Vector3 normal, TerrainTriplanarMode mode, float triplanarSlope);

/**
 * @brief A point's texture coordinates on each of a material's projections.
 * @param terrainPosition The point in the terrain's space.
 * @param terrainNormal The ground's unit normal there (which side each projection is seen from).
 * @param tileSize The material's tile size, in metres (above 0).
 * @returns The coordinates, in repeats (wrapped into one repeat in double first).
 */
TerrainProjectionCoordinates TerrainBlend_GetProjectionCoordinates(Vector3Double terrainPosition, Vector3 terrainNormal,
    float tileSize);

/**
 * @brief A material's height at a point, sampled on its projections as the shader samples them (the base level).
 * @param material The material; NULL is flat (TERRAIN_MATERIAL_FLAT_HEIGHT / 255).
 * @param place Where the point is (its terrain position and normal); must not be NULL.
 * @returns The height, 0 to 1.
 */
float TerrainBlend_SampleMaterialHeight(const TerrainMaterial* material, const TerrainBlendPlace* place);


// Functions: a point of a terrain.
/**
 * @brief Where a point of one of a terrain's triangles is: its position and normal in the terrain's space (the
 *        triangle's vertices and their normals, interpolated by @p barycentrics, the normal made unit length, as the GPU
 *        interpolates them), and its height and normal in the world through the terrain object's world matrix.
 * @param terrain The terrain; must not be NULL.
 * @param triangle A triangle of its mesh.
 * @param barycentrics The point's weights of the triangle's three vertices (summing to 1); must not be NULL.
 * @param worldMatrix The terrain object's world matrix (WorldObject's, in double); must not be NULL.
 * @param outPlace [out] The place. Must not be NULL.
 * @returns false (and @p outPlace zeroed) for a triangle index past the mesh.
 */
bool TerrainBlend_GetPlace(const TerrainAsset* terrain, uint32_t triangle, const float* barycentrics,
    const Matrix4Double* worldMatrix, TerrainBlendPlace* outPlace);

/**
 * @brief The blend at a point of one of a terrain's triangles: its painted weights (or its face material), the rules,
 *        height blending with the materials' height maps, and the dominant material.
 *
 * What a collision hit on the terrain (its chunk and SourceTriangle, with barycentrics), the scattering or footsteps
 * ask; TerrainBlend_GetPlace gives @p place from the same triangle and barycentrics.
 * @param terrain The terrain; must not be NULL. Materials it was built without (headless tests) are flat.
 * @param triangle A triangle of its mesh (a hole answers as painted ground).
 * @param barycentrics The point's weights of the triangle's three vertices (summing to 1); must not be NULL.
 * @param place Where the point is; must not be NULL.
 * @param outPoint [out] The weights, shares and dominant material. Must not be NULL.
 * @returns false (and @p outPoint zeroed) for a triangle index past the mesh.
 */
bool TerrainBlend_EvaluatePoint(const TerrainAsset* terrain, uint32_t triangle, const float* barycentrics,
    const TerrainBlendPlace* place, TerrainBlendPoint* outPoint);
