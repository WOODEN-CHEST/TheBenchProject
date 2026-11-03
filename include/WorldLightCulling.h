#pragma once
#include <stdbool.h>
#include <stddef.h>
// Vector3 is a Raylib type used for positions; it is already the transform type across the world data layer, so
// this include is unavoidable here.
#include "raylib/raylib.h"


/**
 * @file WorldLightCulling.h
 * @brief Selects the handful of point and spot lights that most affect a given object, for forward shading.
 *
 * A world may hold hundreds of lights, but a forward renderer can only shade a small fixed number per draw. This module
 * does the "reach culling": given an object's bounding sphere, it walks a list of lights, keeps only those that reach
 * the sphere (a point light's reach is a sphere; a spot's is the part of that sphere inside its cone), ranks them by
 * influence (brighter and closer wins), and returns the strongest up to a fixed cap. The renderer builds the light list
 * once per frame and calls this once per drawn object, so the cost is O(lights) per object, independent of the object
 * count.
 *
 * This module is pure selection logic with no rendering or world dependency, so it can be exercised headlessly.
 */


// Macros.
/**
 * @brief Maximum number of lights forward-shaded on a single object.
 *
 * The lit fragment shaders declare their forward-light uniform arrays with MAX_FORWARD_LIGHTS entries
 * (shaders/common/lighting.glsl), which should equal this value. The shaders clamp their loops to their own array size
 * and the renderer checks the arrays when it loads a shader, uploading at most as many lights as both allow, so a
 * mismatch costs lights, never memory safety.
 */
#define WORLD_MAX_FORWARD_LIGHTS 8


// Types.
/**
 * @brief A light as the culling sees it: where it is, how far it reaches, how bright it is and, for a spot, its cone.
 *
 * A plain value type; the caller keeps any other per-light data in a parallel array addressed by the same index. A
 * zero-initialized light of some position, range and intensity is a point light.
 */
typedef struct WorldCullingLightStruct
{
    /** @brief Position of the light, in the same space as the spheres it is culled against (the renderer's render space:
     *  relative to the camera). */
    Vector3 Position;
    /** @brief Reach radius (the light component's range); a light with Range <= 0 is never selected. */
    float Range;
    /** @brief Emission intensity; a light with Intensity <= 0 is never selected. */
    float Intensity;
    /** @brief Whether it is a spot (its reach the part of its reach sphere inside its cone); false for a point. */
    bool IsSpot;
    /** @brief A spot's axis, the unit direction it shines along (unused for a point). */
    Vector3 Direction;
    /** @brief The cosine of a spot's outer angle, in (0, 1): spots never reach 90 degrees off their axis (unused for a
     *  point). */
    float CosOuter;
} WorldCullingLight;


// Functions.
/**
 * @brief Returns whether a light's reach touches a sphere: a point's reach sphere overlaps it (the distance from the
 *        light to the sphere's surface is less than the range), and for a spot the sphere also reaches into its cone
 *        and in front of it.
 *
 * Conservative: it may keep a sphere that only nears the cone (it tests the infinite cone, and the apex's region as the
 * cone), never drop one the light reaches. Ignores the intensity. O(1), pure.
 * @param light The light; must not be NULL.
 * @param center Centre of the sphere, in the light's space.
 * @param radius Radius of the sphere; negative values are treated as 0.
 * @returns true if the light may light some part of the sphere; false for a light with Range <= 0.
 */
bool WorldLightCulling_DoesLightReachSphere(const WorldCullingLight* light, Vector3 center, float radius);

/**
 * @brief Selects the most influential lights that reach a bounding sphere.
 *
 * Considers every light in @p lights with positive Range and Intensity and keeps those that reach the sphere at
 * (@p center, @p radius) (WorldLightCulling_DoesLightReachSphere). The kept lights are ranked by influence (intensity
 * scaled by how deeply the reach sphere penetrates the sphere) and the indices of the strongest, up to @p capacity, are
 * written to @p outIndices in descending-influence order. Does not allocate; O(lightCount * min(capacity,
 * WORLD_MAX_FORWARD_LIGHTS)).
 * @param lights The candidate lights; may be NULL only when @p lightCount is 0.
 * @param lightCount Number of entries in @p lights.
 * @param center Centre of the object's bounding sphere, in the lights' space.
 * @param radius Radius of the object's bounding sphere; negative values are treated as 0.
 * @param outIndices [out] Receives indices into @p lights, most influential first. Must not be NULL and must have
 *        room for at least @p capacity entries.
 * @param capacity Maximum lights to select (the buffer length). Values above WORLD_MAX_FORWARD_LIGHTS are clamped
 *        to it. If 0, no lights are selected.
 * @param outReachingCount [out] Receives how many lights reached the sphere, selected or not (more than the count
 *        returned when the sphere has more lights than it takes); may be NULL.
 * @returns The number of indices written to @p outIndices (0 to min(@p capacity, WORLD_MAX_FORWARD_LIGHTS)).
 */
size_t WorldLightCulling_SelectForSphere(const WorldCullingLight* lights, size_t lightCount, Vector3 center,
    float radius, size_t* outIndices, size_t capacity, size_t* outReachingCount);
