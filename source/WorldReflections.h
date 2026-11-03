#pragma once
#include <stdbool.h>
#include <stdint.h>
// Vector2, Vector3 and Matrix are raylib's: render-space directions and the frame's view-projection.
#include "raylib/raylib.h"

/*
 * Private header (source/): the screen-space reflections as the world renderer makes them, pure maths tested headless
 * (tests/WorldReflectionsTests.c). The lit shaders hand each mirroring pixel's data to the reflection pass
 * (shaders/common/reflections.glsl, shaders/common/reflection_output.glsl), which marches it over the scene's depth
 * (shaders/reflections/); this file mirrors their formulas (keep them in step; tests/ShaderSourceTests.c pins the shared
 * constants).
 *
 * WHAT A PIXEL HANDS OVER. A lit surface that mirrors (water, a wet film or a puddle, an object with has_reflections)
 * writes four things into the scene target's three reflection attachments: the WEIGHT (the share of the mirrored light
 * that reaches the eye: its Fresnel reflectance, times the share of it that mirrors, times the haze's transmittance in
 * front of it), the SCREEN SHARE (how much of its reflection the screen may answer: 1 a sharp mirror, falling to 0 by
 * WORLD_REFLECTIONS_ROUGH_ROUGHNESS, where a reflection is the sky's alone: WorldReflections_GetScreenShare), the SKY (what
 * it mirrors where the screen has no answer: the sky and the ground's bounce as the surface's own shader works them out,
 * through the eye's night vision) and the DIRECTION it mirrors along (render space, packed in two numbers:
 * WorldReflections_EncodeDirection). A surface that does not mirror writes a weight of 0. The pass adds weight x (the
 * screen's answer, mixed with the sky by the answer's confidence times the screen share) to the scene's colour.
 *
 * THE MARCH (WorldReflections_March). The reflected ray from the pixel's surface, in render space, projected onto the
 * screen: its start and its end through the frame's view-projection, the end WORLD_REFLECTIONS_RAY_LENGTH metres along
 * it (as good as infinity: its point on the screen is the direction's vanishing point), or, for a ray toward the camera,
 * just short of the near plane. The screen segment between them is clipped to the screen and walked from
 * WORLD_REFLECTIONS_START_OFFSET pixels off the start, BLOCK BY BLOCK: the screen in blocks of WORLD_REFLECTIONS_BLOCK_SIZE
 * pixels a side, each with the depth of the nearest surface in it (WorldReflections_GetBlockNearest: the pass's block
 * image). One over the ray's distance in front of the camera is linear along the screen, so the walk knows the ray's
 * distance anywhere along it exactly. Where the ray stays in front of a block's nearest surface all through the block,
 * nothing there can stop it, and the walk jumps to the block's far side; otherwise it walks every pixel the ray's line
 * crosses in the block: a pixel where the ray's distances across it reach behind the scene's surface (farther than the
 * distance its depth holds) without all lying farther behind it than the thickness (WorldReflections_GetThickness:
 * WORLD_REFLECTIONS_THICKNESS plus WORLD_REFLECTIONS_THICKNESS_SHARE of the surface's distance) is a HIT; all farther
 * behind, the ray passes behind something thin and goes on. So no thin thing is stepped over, however thin on screen,
 * and empty screen costs a step a block. The sky (nothing drawn: no distance) never hides a ray. A ray that
 * reaches its vanishing point on the screen, behind nothing, where the screen shows the sky, is answered by the SKY'S
 * PIXEL there (the drawn clouds, the moon and the sun's glow in it; the pass leaves out the discs, which the lit shaders'
 * glints mirror already). Anything else (it leaves the screen, it runs toward the camera to the near plane, it ends on
 * something past its length, it visits more blocks than the quality allows) has no answer: the sky the surface handed
 * over stands.
 *
 * CONFIDENCE. An answer fades toward the screen's edges (WorldReflections_GetEdgeFade: over WORLD_REFLECTIONS_EDGE_FADE
 * of the screen at each side), and for rays toward the camera (WorldReflections_GetFacingFade, by the ray's cosine to the
 * camera's forward): the screen holds the faces turned toward the camera, so such a ray would find a thing's front where
 * a mirror shows its back.
 *
 * DEPTH. The scene's depth as the camera's passes store it (shaders/common/depth.glsl): reversed (1 at the near plane, 0
 * at infinity, the sky exactly 0) where the GPU has clip control, raylib's standard otherwise (0 at the near plane, 1 at
 * its far plane, the sky at 1). WorldReflections_GetDistance turns either into the distance in front of the camera.
 */


// Macros.
/** @brief The roughness up to which a surface mirrors the screen in full, and the roughness from which its reflection is
 *  the sky's alone (WorldReflections_GetScreenShare: a smooth step between them). */
#define WORLD_REFLECTIONS_SHARP_ROUGHNESS 0.1f
#define WORLD_REFLECTIONS_ROUGH_ROUGHNESS 0.4f
/** @brief The share of the screen's width (and height) at each side over which an answer fades out toward the edge. */
#define WORLD_REFLECTIONS_EDGE_FADE 0.08f
/** @brief The ray's cosine to the camera's forward at which an answer is gone, and from which it is whole. */
#define WORLD_REFLECTIONS_FACING_FADE_START (-0.35f)
#define WORLD_REFLECTIONS_FACING_FADE_END (-0.05f)
/** @brief How far behind a surface a ray may lie and still hit it: this many metres plus WORLD_REFLECTIONS_THICKNESS_SHARE
 *  of the surface's distance in front of the camera (things are taken this thick). */
#define WORLD_REFLECTIONS_THICKNESS 0.3f
#define WORLD_REFLECTIONS_THICKNESS_SHARE 0.03f
/** @brief How far a ray away from the camera runs, in metres: its end on the screen is as good as its vanishing point. */
#define WORLD_REFLECTIONS_RAY_LENGTH 100000.0f
/** @brief A ray toward the camera ends where its distance in front of the camera is this many times the near plane's. */
#define WORLD_REFLECTIONS_NEAR_MARGIN 1.5f
/** @brief How many pixels off its start the march begins (it must not find its own surface). */
#define WORLD_REFLECTIONS_START_OFFSET 1.5f
/** @brief The pixels a side of the march's blocks (the pass's block image holds each one's nearest surface). */
#define WORLD_REFLECTIONS_BLOCK_SIZE 4
/** @brief How far past a block's side the walk enters the next, in pixels along the ray. */
#define WORLD_REFLECTIONS_BLOCK_NUDGE 0.01f
/** @brief The most pixels a walk through one block crosses beyond its first (two sides of a block: a line crosses at most
 *  that many pixels in it). */
#define WORLD_REFLECTIONS_BLOCK_WALK (2 * WORLD_REFLECTIONS_BLOCK_SIZE)
/** @brief The most blocks any quality's ray visits (the shader's loop bound). */
#define WORLD_REFLECTIONS_MAX_BLOCKS 256
/** @brief The blocks a ray visits at most at the player's low and high quality (a ray across the whole screen visits
 *  about 250 at 640 x 360), and how many scene pixels a side of the pass's pixel spans there. */
#define WORLD_REFLECTIONS_LOW_BLOCKS 128
#define WORLD_REFLECTIONS_HIGH_BLOCKS 256
#define WORLD_REFLECTIONS_LOW_DIVISOR 2
#define WORLD_REFLECTIONS_HIGH_DIVISOR 1
/** @brief How far past the sun's disc, in degrees, the sky's pixels count as the disc (the sky's answer leaves them out):
 *  about two pixels of a 640-pixel-wide screen. */
#define WORLD_REFLECTIONS_DISC_MARGIN_DEGREES 0.35f


// Types.
/**
 * @brief How the pass marches at one quality (WorldReflections_GetMarchSettings). A plain value type.
 */
typedef struct WorldReflectionsMarchSettingsStruct
{
    /** @brief The most blocks a ray visits, 1 to WORLD_REFLECTIONS_MAX_BLOCKS. */
    int32_t MaxBlocks;
    /** @brief How many scene pixels a side of the pass's pixel spans (1 the scene's resolution, 2 half of it). */
    int32_t ResolutionDivisor;
} WorldReflectionsMarchSettings;

/**
 * @brief The camera and the scene's depth a march runs over. A plain value type; the renderer fills it from the frame.
 */
typedef struct WorldReflectionsViewStruct
{
    /** @brief Render space -> clip (raymath's layout: the frame's ViewProjection); clip w is the distance in front of the
     *  camera. */
    Matrix ViewProjection;
    /** @brief The camera's forward, unit, in render space. */
    Vector3 Forward;
    /** @brief The projection's near plane and (standard depth only) its far plane, in metres. */
    float NearPlane;
    float FarPlane;
    /** @brief Whether the depth is reversed (1 at the near plane, 0 at infinity) or raylib's standard. */
    bool IsDepthReversed;
    /** @brief The scene's depth size in pixels (above 0). */
    int32_t Width;
    int32_t Height;
} WorldReflectionsView;

/**
 * @brief Reads the scene's depth as stored at a pixel (0 <= x < width, 0 <= y < height; y up from the bottom row, as GL
 *        stores it).
 * @param context The caller's depth (borrowed).
 * @returns The stored depth.
 */
typedef float (*WorldReflectionsDepthReader)(const void* context, int32_t x, int32_t y);

/**
 * @brief What a march found (WorldReflections_March). A plain value type.
 */
typedef struct WorldReflectionsAnswerStruct
{
    /** @brief Whether the screen answers the ray: a hit, or the sky at its vanishing point. */
    bool IsFound;
    /** @brief Whether the answer is the sky's pixel at the ray's vanishing point (false: a surface the ray hit). */
    bool IsSky;
    /** @brief The answer's pixel (x right, y up from the bottom row), meaningful while IsFound. */
    int32_t X;
    int32_t Y;
    /** @brief How much the answer counts, 0..1: its fades (the screen's edges, rays toward the camera); 0 without one. */
    float Confidence;
} WorldReflectionsAnswer;


// Functions.
/**
 * @brief The march's settings at a quality (GameConfig.ReflectionQuality): low visits WORLD_REFLECTIONS_LOW_BLOCKS at
 *        half the scene's resolution, high WORLD_REFLECTIONS_HIGH_BLOCKS at the scene's. Off, or another value, gives
 *        high's (the renderer does not run the pass while it is off).
 * @param quality CONFIG_REFLECTION_QUALITY_*.
 * @returns The settings.
 */
WorldReflectionsMarchSettings WorldReflections_GetMarchSettings(int32_t quality);

/**
 * @brief How much of a surface's reflection the screen may answer by its roughness: 1 up to
 *        WORLD_REFLECTIONS_SHARP_ROUGHNESS, a smooth step down to 0 at WORLD_REFLECTIONS_ROUGH_ROUGHNESS and beyond
 *        (common/reflections' ReflectionScreenShare). A NaN gives 0.
 * @param roughness The surface's roughness.
 * @returns 0..1.
 */
float WorldReflections_GetScreenShare(float roughness);

/**
 * @brief Packs a unit direction into two numbers in [-1, 1] (an octahedral map: common/reflections'
 *        ReflectionEncodeDirection).
 * @param direction A unit direction (any length above 0 is made unit; zero gives +Z's).
 * @returns The packed direction.
 */
Vector2 WorldReflections_EncodeDirection(Vector3 direction);

/**
 * @brief Unpacks a direction WorldReflections_EncodeDirection packed (common/reflections' ReflectionDecodeDirection).
 * @param encoded The packed direction, each part in [-1, 1].
 * @returns The unit direction.
 */
Vector3 WorldReflections_DecodeDirection(Vector2 encoded);

/**
 * @brief How much an answer at a place on the screen counts by its distance to the screen's nearest edge: 0 at the edge,
 *        rising to 1 at WORLD_REFLECTIONS_EDGE_FADE of the screen in from it.
 * @param u The place across the screen, 0 at its left edge, 1 at its right.
 * @param v The place up the screen, 0 at its bottom, 1 at its top.
 * @returns 0..1 (0 outside the screen).
 */
float WorldReflections_GetEdgeFade(float u, float v);

/**
 * @brief How much an answer counts by its ray's cosine to the camera's forward: 0 up to WORLD_REFLECTIONS_FACING_FADE_START,
 *        a smooth step to 1 at WORLD_REFLECTIONS_FACING_FADE_END and beyond.
 * @param cosine The ray's unit direction dotted with the camera's forward.
 * @returns 0..1.
 */
float WorldReflections_GetFacingFade(float cosine);

/**
 * @brief How far behind a surface at a distance a ray may lie and still hit it: WORLD_REFLECTIONS_THICKNESS plus
 *        WORLD_REFLECTIONS_THICKNESS_SHARE of the distance.
 * @param distance The surface's distance in front of the camera, in metres.
 * @returns Metres.
 */
float WorldReflections_GetThickness(float distance);

/**
 * @brief The distance in front of the camera a stored depth stands for (shaders/common/depth.glsl's conventions):
 *        reversed, the near plane over the depth; standard, from its normalized device z through raylib's perspective.
 * @param view The view (its near and far planes and its convention); must not be NULL.
 * @param depth The depth as stored.
 * @returns Metres; +infinity for the sky (nothing drawn: 0 reversed, 1 or more standard, up to the shader's tolerance).
 */
float WorldReflections_GetDistance(const WorldReflectionsView* view, float depth);

/**
 * @brief The depth of the nearest surface in a block of the screen as stored (the largest reversed, the smallest
 *        standard), as the reflection pass's block image holds it: over the block's pixels that lie on the screen; the
 *        sky's depth where the block shows only the sky.
 * @param view The view (its convention and size); must not be NULL.
 * @param blockX The block's column (its pixels from blockX x WORLD_REFLECTIONS_BLOCK_SIZE).
 * @param blockY The block's row, from the bottom.
 * @param read Reads the stored depth at a pixel; must not be NULL.
 * @param context Handed to @p read.
 * @returns The stored depth.
 */
float WorldReflections_GetBlockNearest(const WorldReflectionsView* view, int32_t blockX, int32_t blockY,
    WorldReflectionsDepthReader read, const void* context);

/**
 * @brief Marches a reflected ray over the scene's depth as the reflection pass does (see the header's MARCH), its blocks'
 *        nearest surfaces from WorldReflections_GetBlockNearest.
 * @param view The camera and the depth's size and convention; must not be NULL.
 * @param settings The quality's settings (its MaxBlocks); must not be NULL.
 * @param origin Where the ray starts: the mirroring pixel's surface, in render space (in front of the camera).
 * @param direction The ray's unit direction, in render space.
 * @param read Reads the stored depth at a pixel; must not be NULL.
 * @param context Handed to @p read.
 * @returns The answer; IsFound false (and confidence 0) without one.
 */
WorldReflectionsAnswer WorldReflections_March(const WorldReflectionsView* view,
    const WorldReflectionsMarchSettings* settings, Vector3 origin, Vector3 direction, WorldReflectionsDepthReader read,
    const void* context);
