#pragma once
#include "GameCamera.h"
#include "raylib/raylib.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): render space, the space the world renderer draws in (T32), as pure maths tested headless
 * (tests/WorldRenderSpaceTests.c).
 *
 * RENDER SPACE is the world's orientation with the camera at (0, 0, 0). Each frame takes the camera's world position, in
 * double, as its ORIGIN; every position the GPU sees is that position minus the origin, subtracted in double and rounded
 * to float once (GameMath_ToVector3Relative, GameMath_ToMatrixRelative; the render cache does it for every entry when it
 * syncs). A float holds a world position far from the world's origin only to millimetres or worse (its step is 1 mm past
 * 8 km), so a renderer working in world floats shakes out there; an offset from the camera keeps a float's full
 * precision near the camera, however far out the camera is.
 *
 * THE VIEW (WorldRenderSpace_BuildView). raylib's camera sits at the origin and looks along the camera's forward
 * (GameCamera_ToRenderCamera), so the view matrix is a pure rotation and the view-projection and its inverse hold no
 * large numbers: the sky's rays come from the rotation alone (WorldRenderSpace_GetSkyRay is the C mirror of the sky
 * shader's ray), and the stars and the sun's place on screen are directions (w = 0). Since T39 (decision G41) the
 * projection may be REVERSED and infinite (WorldRenderSpace_GetReversedProjection): the near plane at depth 1, infinity
 * at 0, no far plane, for the camera's passes where the GPU has clip control (GLDepth.h); otherwise raylib's, with its far
 * plane.
 *
 * THE SHADOW CASCADES (WorldRenderSpace_GetCascade, WorldRenderSpace_StepCascade). Each cascade is a fixed-size
 * orthographic box around the camera, looking along the shadow light. Its centre is kept on the cascade's texel grid in
 * the light's frame, in double: each frame it moves from where it was (its anchor, which the cascade's
 * WorldRenderCascadeGrid keeps from frame to frame) to the camera by whole texels along the light's right and up axes.
 * While the light holds still the grid therefore stands still in the world, however the camera moves, so shadow edges
 * do not crawl. While the light turns (the clock moves the sun), the grid turns about the anchor, which is within half a
 * texel of the camera: a point near the camera slides as it would under a box centred on the camera, wherever the
 * camera is. (A grid fixed to the world's origin would turn about that origin and slide by the camera's distance from
 * it times the angle: far out, shadow edges would jump by a fraction of a texel every frame.) In render space the
 * centre lies within half a texel of the origin along the light's right and up axes.
 *
 * THE RULE FOR SHADERS. "World space" in every shader means render space: fragment positions, light positions and the
 * shadow matrices are relative to the camera, which is at (0, 0, 0). A shader never receives the absolute origin as
 * floats; a pattern anchored to the world (noise, waves, rain) gets the origin wrapped by the pattern's period, computed
 * in double on the CPU (agents/WorldRuntime.md, "Render space").
 */


// Types.
/** @brief The camera's view of one frame in render space (WorldRenderSpace_BuildView). A plain value type. */
typedef struct WorldRenderViewStruct
{
    /** @brief The frame's origin: the camera's world position, in double. Render space's (0, 0, 0). */
    Vector3Double Origin;
    /** @brief raylib's camera for BeginMode3D: at (0, 0, 0), looking along the camera's forward, with its up and its
     *  field of view (GameCamera_ToRenderCamera). */
    Camera3D Camera;
    /** @brief Render space -> view space: a pure rotation (the matrix BeginMode3D loads for Camera). */
    Matrix View;
    /** @brief The perspective projection (BeginMode3D's for the same aspect and planes), and its inverse. */
    Matrix Projection;
    Matrix InvProjection;
    /** @brief View x projection (raymath's order, MatrixMultiply(View, Projection)): render space -> clip; and its
     *  inverse, clip -> render space (the sky's rays). */
    Matrix ViewProjection;
    Matrix InvViewProjection;
} WorldRenderView;

/** @brief A shadow cascade's size (WorldRenderSpace_GetCascade). A plain value type. */
typedef struct WorldRenderCascadeShapeStruct
{
    /** @brief The box's width and height, in metres: the area it shadows around the camera. */
    float Size;
    /** @brief How far the light's eye stands from the box's centre, along the light, in metres. */
    float LightDistance;
    /** @brief The box's near and far planes, in metres from the light's eye. */
    float NearPlane;
    float FarPlane;
    /** @brief The shadow map's texels along each side (above 0): a texel is Size / MapSize wide, the grid the box's
     *  centre snaps to. */
    int MapSize;
} WorldRenderCascadeShape;

/** @brief One shadow cascade's light matrices for a frame, in render space (WorldRenderSpace_GetCascade). */
typedef struct WorldRenderCascadeStruct
{
    /** @brief The light's view (from LightDistance along the light, looking at Center) and its orthographic
     *  projection. */
    Matrix View;
    Matrix Projection;
    /** @brief Render space -> the light's clip space (raymath's order, MatrixMultiply(View, Projection)). */
    Matrix ViewProjection;
    /** @brief The box's centre in render space: within half a texel of (0, 0, 0) along the light's right and up axes, at
     *  the camera's depth along the light. */
    Vector3 Center;
    /** @brief The box's centre in the world, in double: the anchor to pass to the next frame's
     *  WorldRenderSpace_GetCascade for this cascade. */
    Vector3Double Anchor;
} WorldRenderCascade;

/** @brief One shadow cascade's texel grid from frame to frame (WorldRenderSpace_StepCascade): where its box's centre
 *  was. Zeroed, it has no centre yet. A plain value type; the renderer keeps one per cascade. */
typedef struct WorldRenderCascadeGridStruct
{
    /** @brief The box's centre in the world, in double, from the last frame the cascade ran (that frame's
     *  WorldRenderCascade.Anchor). Meaningful only while HasAnchor. */
    Vector3Double Anchor;
    /** @brief Whether Anchor holds a centre: false until the cascade's first frame, which starts the grid at that
     *  frame's origin. */
    bool HasAnchor;
} WorldRenderCascadeGrid;


// Functions.
/**
 * @brief Returns the reversed infinite perspective projection (T39): x and y as MatrixPerspective's, clip z the near
 *        plane's distance and clip w the view distance, so with clip control's 0-to-1 depth a point at the near plane
 *        has depth 1, one twice as far 0.5, infinity 0; nothing is clipped far away. In raymath's layout.
 * @param fovY The vertical field of view, in radians (above 0, below pi).
 * @param aspect The target's width over its height (above 0).
 * @param nearPlane The near plane, in metres (above 0).
 * @param outInverse [out] Receives its inverse (exact: clip back to view space); may be NULL.
 * @returns The projection.
 */
Matrix WorldRenderSpace_GetReversedProjection(double fovY, double aspect, double nearPlane, Matrix* outInverse);

/**
 * @brief Builds a frame's view in render space: the origin (the camera's position), raylib's camera at (0, 0, 0), the
 *        rotation-only view and the projection, with their product and the inverses.
 *
 * The view is MatrixLookAt of the render camera, which is what BeginMode3D loads for it, and the projection is
 * MatrixPerspective of the camera's field of view, which matches BeginMode3D's for a target of @p aspect (or, for
 * reversed depth, WorldRenderSpace_GetReversedProjection, which the camera's passes load in its place), so the passes
 * that reconstruct positions or rays from pixels line up with the geometry. Constant time; no allocation.
 * @param camera The camera; must not be NULL.
 * @param aspect The target's width over its height (above 0).
 * @param nearPlane The projection's near plane, in metres (raylib's cull distance for the scene).
 * @param farPlane The projection's far plane, in metres (unused for reversed depth: no far plane).
 * @param isDepthReversed Whether the projection is the reversed infinite one (T39).
 * @returns The view.
 */
WorldRenderView WorldRenderSpace_BuildView(const GameCamera* camera, double aspect, double nearPlane, double farPlane,
    bool isDepthReversed);

/**
 * @brief The view ray through a point of the target as the sky shader reconstructs it (its C mirror, in float): the point
 *        at clip depth 1 there (the far plane's, or with reversed depth the near plane's) through the inverse
 *        view-projection, divided by its w and made unit. The camera is at the origin, so the point is the ray.
 * @param invViewProjection The frame's inverse view-projection (WorldRenderView.InvViewProjection); must not be NULL.
 * @param u The point's place across the target, 0 at its left edge and 1 at its right (gl_FragCoord.x / width).
 * @param v The point's place up the target, 0 at its bottom edge and 1 at its top (gl_FragCoord.y / height).
 * @returns The ray's unit direction, in the world's orientation.
 */
Vector3 WorldRenderSpace_GetSkyRay(const Matrix* invViewProjection, float u, float v);

/**
 * @brief Builds one shadow cascade's light matrices for a frame: a box of the shape's size around the camera, looking
 *        along the light, with its centre on the shadow map's texel grid.
 *
 * The light's right and up axes are MatrixLookAt's for the light's view (up is world +Y, or +Z for a light within about
 * 8 degrees of straight up), computed in double. The camera's coordinates relative to @p anchor along them are rounded
 * to the nearest multiple of the texel's width (Size / MapSize): the box's new centre (Anchor) is the anchor moved by
 * those whole texels along the right and up axes, and to the camera's depth along the light. In render space the centre
 * is the rounding's remainder (Center), at most half a texel from the origin along each axis.
 *
 * A texel's edges lie at whole multiples of the texel's width from the anchor along the light's axes, so moving the
 * anchor by whole texels keeps them where they are: with the light holding still, a point that stays put keeps its
 * texel, and its texel coordinates change by whole texels when the centre steps, wherever the camera goes. With the
 * light turning, the grid turns about @p anchor, so pass each frame's Anchor to the next frame's call: the grid then
 * pivots within half a texel of the camera (as a box centred on the camera would), never about a point far away.
 * Constant time; no allocation.
 * @param origin The frame's origin (the camera's world position).
 * @param anchor This cascade's previous Anchor; on its first frame any point (the origin is the natural one: only the
 *        grid's phase depends on it).
 * @param towardLight The unit direction toward the shadow light.
 * @param shape The cascade's size; must not be NULL.
 * @returns The cascade's matrices and centre in render space, and its new anchor.
 */
WorldRenderCascade WorldRenderSpace_GetCascade(Vector3Double origin, Vector3Double anchor, Vector3 towardLight,
    const WorldRenderCascadeShape* shape);

/**
 * @brief A cascade's frame, as the renderer runs it: WorldRenderSpace_GetCascade from the grid's last centre (from the
 *        frame's origin on the grid's first frame), then that frame's centre kept in the grid for the next one.
 *
 * Call once a frame for each cascade with the same grid, so the grid stands still while the light does and turns about
 * the camera while the light turns (see WorldRenderSpace_GetCascade). Constant time; no allocation.
 * @param grid The cascade's grid (zeroed before its first frame); must not be NULL. Updated.
 * @param origin The frame's origin (the camera's world position).
 * @param towardLight The unit direction toward the shadow light.
 * @param shape The cascade's size; must not be NULL.
 * @returns The cascade's matrices and centre in render space, and its new anchor (also in @p grid).
 */
WorldRenderCascade WorldRenderSpace_StepCascade(WorldRenderCascadeGrid* grid, Vector3Double origin, Vector3 towardLight,
    const WorldRenderCascadeShape* shape);
