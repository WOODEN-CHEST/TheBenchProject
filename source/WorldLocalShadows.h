#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WorldRenderCache.h"
#include "WorldVisibility.h"
#include "raylib/raylib.h"

/*
 * Private header (source/): the local lights' shadows as pure maths, tested headless (tests/WorldLocalShadowsTests.c).
 * The shadow passes render them (WorldRendererShadows.c) and common/lighting reads them; WorldLocalShadows_ProjectSpot is
 * the C mirror of that read's projection.
 *
 * WHICH LIGHTS (WorldLocalShadows_Choose). Two kinds of local light cast shadows, each only while it is on, casts
 * shadows, and has a range and an intensity above 0:
 * - the point light nearest the camera, into the cube shadow (six faces around it);
 * - the WORLD_LOCAL_SHADOWS_MAX_SPOTS strongest spots, into a map each: a spot's strength is its intensity over its
 *   squared distance from the camera (at least 1 m: lamps on top of the camera tie at their intensity), among the spots
 *   whose light can reach the camera's view (the sphere around its cone, WorldLocalShadows_GetSpotBounds, touches the
 *   camera's frustum). A spot shadowed last frame counts WORLD_LOCAL_SHADOWS_SPOT_HYSTERESIS times as strong, so two
 *   lamps of about the same strength do not hand their shadows back and forth as the camera moves between them. Every
 *   other light lights without a shadow (the forward lights).
 *
 * A SPOT'S MAP (WorldLocalShadows_GetSpotFrame). A perspective depth map from the light along its axis, square, its
 * field of view the outer cone and a margin of a few texels (so a lookup's filter at the cone's edge stays on the map),
 * at most twice WORLD_LOCAL_SHADOWS_MAX_SPOT_HALF_ANGLE (a wider spot casts shadows within that angle of its axis only),
 * from WORLD_LOCAL_SHADOWS_SPOT_NEAR metres to its range. Its depth is OpenGL's (0 at the near plane, 1 at the far; the
 * maps are not drawn with the camera's reversed depth), compared by the hardware.
 *
 * THE LOOKUP (common/lighting's SpotShadowAmount). A texel of the map is TexelScale x the depth along the axis wide
 * there; the lookup moves off its surface along the surface's normal by WORLD_LOCAL_SHADOWS_NORMAL_OFFSET_TEXELS of them
 * times the slant (the sine of the surface's angle to the light), and compares its depth less a bias of
 * WORLD_LOCAL_SHADOWS_DEPTH_BIAS_TEXELS of them along the axis (DepthBiasScale over the depth, in the map's depth: the
 * perspective depth's slope at that depth). The maps hold the casters' faces turned away from the light (the passes cull
 * the faces toward it), as the sun's levels do, so a lit surface never meets itself in them.
 */


// Macros.
/** @brief The most spot lights that cast shadows in one render: a layer each of the spot shadow maps. */
#define WORLD_LOCAL_SHADOWS_MAX_SPOTS 2U
/** @brief The size of a spot's shadow map, texels a side. */
#define WORLD_LOCAL_SHADOWS_SPOT_MAP_SIZE 1024
/** @brief The near plane of a spot's shadow map, metres from the light. */
#define WORLD_LOCAL_SHADOWS_SPOT_NEAR 0.05f
/** @brief The widest half field of view a spot's shadow map takes, radians (80 degrees). */
#define WORLD_LOCAL_SHADOWS_MAX_SPOT_HALF_ANGLE 1.3962634f
/** @brief The map's margin past the outer cone, in texels at its edge. */
#define WORLD_LOCAL_SHADOWS_SPOT_MARGIN_TEXELS 4.0f
/** @brief How much stronger a spot shadowed last frame counts in the choice. */
#define WORLD_LOCAL_SHADOWS_SPOT_HYSTERESIS 1.25f
/** @brief The lookup's move off its surface at grazing incidence and its depth bias, in texels of the map where it reads. */
#define WORLD_LOCAL_SHADOWS_NORMAL_OFFSET_TEXELS 1.5f
#define WORLD_LOCAL_SHADOWS_DEPTH_BIAS_TEXELS 0.5f


// Types.
/** @brief The local lights that cast shadows this frame, as indices into the render cache's lights. A plain value. */
typedef struct WorldLocalShadowChoiceStruct
{
    /** @brief The point light with the cube shadow, or SIZE_MAX for none. */
    size_t PointLight;
    /** @brief How many spots cast shadows, and which, strongest first. */
    uint32_t SpotCount;
    size_t Spots[WORLD_LOCAL_SHADOWS_MAX_SPOTS];
} WorldLocalShadowChoice;

/** @brief A spot's shadow map in render space (WorldLocalShadows_GetSpotFrame). A plain value. */
typedef struct WorldSpotShadowFrameStruct
{
    /** @brief The light's view (from its position along its axis), the map's projection and their product: render space
     *  to the map's clip space. */
    Matrix View;
    Matrix Projection;
    Matrix ViewProjection;
    /** @brief The map's half field of view, radians. */
    float HalfAngle;
    /** @brief Its near and far planes, metres along the axis. */
    float Near;
    float Far;
    /** @brief A texel's width per metre of depth along the axis (2 tan(HalfAngle) / the map's size). */
    float TexelScale;
    /** @brief The lookup's move off its surface at grazing incidence per metre of depth
     *  (WORLD_LOCAL_SHADOWS_NORMAL_OFFSET_TEXELS x TexelScale). */
    float NormalOffsetScale;
    /** @brief The depth bias times the depth along the axis, in the map's depth: WORLD_LOCAL_SHADOWS_DEPTH_BIAS_TEXELS
     *  texels along the axis, through the perspective depth's slope (Far Near / ((Far - Near) d^2)). */
    float DepthBiasScale;
} WorldSpotShadowFrame;


// Functions.
/**
 * @brief Chooses the frame's shadowed local lights (the file comment): the cube shadow's point light and the strongest
 *        spots.
 * @param lights The render cache's lights this frame (render space: the camera at the origin).
 * @param lightCount Their number.
 * @param cameraFrustum The camera's frustum (a spot whose light cannot reach the view casts nothing), or NULL to take
 *        every spot.
 * @param previousSpotIds The object ids of the spots that cast shadows last frame (0: none), WORLD_LOCAL_SHADOWS_MAX_SPOTS
 *        of them, for the hysteresis; may be NULL.
 * @param maxSpots How many spots may cast (clamped to WORLD_LOCAL_SHADOWS_MAX_SPOTS; 0: none).
 * @param isPointWanted Whether the cube shadow runs at all (false: PointLight is SIZE_MAX).
 * @returns The choice.
 */
WorldLocalShadowChoice WorldLocalShadows_Choose(const WorldRenderLight* lights, size_t lightCount,
    const WorldFrustum* cameraFrustum, const uint64_t* previousSpotIds, uint32_t maxSpots, bool isPointWanted);

/**
 * @brief The smallest sphere around a spot's reach: the part of its range's sphere within its outer cone. With a cone
 *        narrower than 45 degrees, the sphere through its apex and its rim; wider, the sphere around its rim's circle.
 * @param light The spot (its render-space position, axis, cone and range).
 * @param outCenter [out] The sphere's centre.
 * @param outRadius [out] Its radius.
 */
void WorldLocalShadows_GetSpotBounds(const WorldRenderLight* light, Vector3* outCenter, float* outRadius);

/**
 * @brief A spot's shadow map in render space (the file comment).
 * @param position The light's render-space position.
 * @param direction Its axis (unit).
 * @param outerAngle Its outer angle, radians.
 * @param range Its range, metres (the far plane: at least twice the near plane).
 * @param mapSize The map's size, texels a side.
 * @returns The map's frame.
 */
WorldSpotShadowFrame WorldLocalShadows_GetSpotFrame(Vector3 position, Vector3 direction, float outerAngle, float range,
    int mapSize);

/**
 * @brief Where a render-space point lies on a spot's map: the C mirror of common/lighting's projection.
 * @param frame The map's frame.
 * @param point The point.
 * @param outMap [out] x and y: its place on the map in [0, 1] while it is within the map's field of view; z: its depth,
 *        0 at the near plane and 1 at the far.
 * @returns false for a point at or behind the light's plane (nothing of the map holds it).
 */
bool WorldLocalShadows_ProjectSpot(const WorldSpotShadowFrame* frame, Vector3 point, Vector3* outMap);
