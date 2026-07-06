#pragma once
#include <stddef.h>
// Vector3 is a Raylib type used for the query centre; it is already the transform type across the world data
// layer, so this include is unavoidable here.
#include "raylib/raylib.h"


/**
 * @file WorldLightCulling.h
 * @brief Selects the handful of point lights that most affect a given object, for forward shading.
 *
 * A world may hold hundreds of point lights, but a forward renderer can only shade a small fixed number per
 * draw. This module does the "reach culling": given an object's bounding sphere, it walks the world's lights,
 * keeps only those whose reach (WorldLight size/radius) actually overlaps the sphere, ranks them by influence
 * (brighter and closer wins), and returns the strongest up to a fixed cap. The renderer calls this once per
 * drawn object and uploads the result to the lighting shader.
 *
 * This module is pure selection logic with no rendering dependency, so it can be exercised headlessly.
 */


// Forward declarations (referenced only by pointer).
/** @brief A live world; full type in World.h. */
typedef struct WorldStruct World;
/** @brief A point light; full type in WorldLight.h. */
typedef struct WorldLightStruct WorldLight;


// Macros.
/**
 * @brief Maximum number of point lights forward-shaded on a single object.
 *
 * The lighting shader declares point-light uniform arrays of exactly this length, so the value MUST match the
 * MAX_POINT_LIGHTS constant in the PBR fragment shader. Selecting more than this per object is impossible;
 * the culling keeps the most influential ones.
 */
#define WORLD_MAX_FORWARD_LIGHTS 8


// Functions.
/**
 * @brief Selects the most influential point lights whose reach overlaps a bounding sphere.
 *
 * Walks every object in @p world, considers only WorldObjectType_Light objects with positive intensity and
 * size, and keeps those whose reach sphere overlaps the sphere at (@p center, @p radius) — i.e. the distance
 * from the light to the sphere's surface is less than the light's size. The kept lights are ranked by
 * influence (intensity scaled by how deeply the reach penetrates the sphere) and the strongest up to
 * @p capacity are written to @p outLights in descending-influence order. Lights that do not reach the sphere
 * are excluded. Borrowed pointers into @p world are returned; they are valid only until the world's object
 * set changes.
 * @param world The world whose lights to consider. If NULL, no lights are selected (returns 0).
 * @param center World-space centre of the object's bounding sphere.
 * @param radius Radius of the object's bounding sphere; negative values are treated as 0.
 * @param outLights [out] Buffer receiving the selected borrowed light pointers, most influential first. Must
 *        not be NULL and must have room for at least @p capacity entries.
 * @param capacity Maximum lights to select (the buffer length). Values above WORLD_MAX_FORWARD_LIGHTS are
 *        clamped to it. If 0, no lights are selected.
 * @returns The number of lights written to @p outLights (0 to min(@p capacity, WORLD_MAX_FORWARD_LIGHTS)).
 */
size_t WorldLightCulling_SelectForSphere(World* world, Vector3 center, float radius,
    const WorldLight** outLights, size_t capacity);
