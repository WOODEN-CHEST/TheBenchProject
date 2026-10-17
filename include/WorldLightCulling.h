#pragma once
#include <stddef.h>
// Vector3 is a Raylib type used for positions; it is already the transform type across the world data layer, so
// this include is unavoidable here.
#include "raylib/raylib.h"


/**
 * @file WorldLightCulling.h
 * @brief Selects the handful of point lights that most affect a given object, for forward shading.
 *
 * A world may hold hundreds of point lights, but a forward renderer can only shade a small fixed number per
 * draw. This module does the "reach culling": given an object's bounding sphere, it walks a list of lights,
 * keeps only those whose reach actually overlaps the sphere, ranks them by influence (brighter and closer
 * wins), and returns the strongest up to a fixed cap. The renderer builds the light list once per frame and
 * calls this once per drawn object, so the cost is O(lights) per object, independent of the object count.
 *
 * This module is pure selection logic with no rendering or world dependency, so it can be exercised headlessly.
 */


// Macros.
/**
 * @brief Maximum number of point lights forward-shaded on a single object.
 *
 * The PBR fragment shader declares its point-light uniform arrays with MAX_POINT_LIGHTS entries, which should
 * equal this value. The shader clamps its loop to its own array size and the renderer checks the arrays when it
 * loads the shader, uploading at most as many lights as both allow, so a mismatch costs lights, never memory
 * safety.
 */
#define WORLD_MAX_FORWARD_LIGHTS 8


// Types.
/**
 * @brief A point light as the culling sees it: where it is, how far it reaches and how bright it is.
 *
 * A plain value type; the caller keeps any other per-light data in a parallel array addressed by the same index.
 */
typedef struct WorldCullingLightStruct
{
    /** @brief World-space position of the light. */
    Vector3 Position;
    /** @brief Reach radius (the WorldLight size); a light with Range <= 0 is never selected. */
    float Range;
    /** @brief Emission intensity; a light with Intensity <= 0 is never selected. */
    float Intensity;
} WorldCullingLight;


// Functions.
/**
 * @brief Selects the most influential lights whose reach overlaps a bounding sphere.
 *
 * Considers every light in @p lights with positive Range and Intensity and keeps those whose reach sphere
 * overlaps the sphere at (@p center, @p radius), i.e. the distance from the light to the sphere's surface is
 * less than the light's Range. The kept lights are ranked by influence (intensity scaled by how deeply the reach
 * penetrates the sphere) and the indices of the strongest, up to @p capacity, are written to @p outIndices in
 * descending-influence order. Does not allocate; O(lightCount * min(capacity, WORLD_MAX_FORWARD_LIGHTS)).
 * @param lights The candidate lights; may be NULL only when @p lightCount is 0.
 * @param lightCount Number of entries in @p lights.
 * @param center World-space centre of the object's bounding sphere.
 * @param radius Radius of the object's bounding sphere; negative values are treated as 0.
 * @param outIndices [out] Receives indices into @p lights, most influential first. Must not be NULL and must have
 *        room for at least @p capacity entries.
 * @param capacity Maximum lights to select (the buffer length). Values above WORLD_MAX_FORWARD_LIGHTS are clamped
 *        to it. If 0, no lights are selected.
 * @returns The number of indices written to @p outIndices (0 to min(@p capacity, WORLD_MAX_FORWARD_LIGHTS)).
 */
size_t WorldLightCulling_SelectForSphere(const WorldCullingLight* lights, size_t lightCount, Vector3 center,
    float radius, size_t* outIndices, size_t capacity);
