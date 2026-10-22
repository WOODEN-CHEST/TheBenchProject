// Render space, the space the world renderer draws in: the frame's view with the camera at the origin, the sky shader's
// ray in C, and the shadow cascades snapped to their texels. See WorldRenderSpace.h.

#include <math.h>
#include "WorldRenderSpace.h"
#include "GameMath.h"
#include "raylib/raymath.h"


// Macros.
/** A light this close to straight up (the y of its unit direction) looks at its box with +Z as its up instead of +Y, so
 *  the light's view never degenerates. */
#define CASCADE_STRAIGHT_UP_Y 0.99f


// Static functions.
/* The up vector the light's view is built with (MatrixLookAt's third argument). */
static Vector3 GetCascadeUp(Vector3 towardLight)
{
    return (fabsf(towardLight.y) > CASCADE_STRAIGHT_UP_Y) ? (Vector3){ 0.0f, 0.0f, 1.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };
}

/* @p coordinate rounded to the nearest whole multiple of @p step. */
static double SnapToGrid(double coordinate, double step)
{
    return floor((coordinate / step) + 0.5) * step;
}


// Internal functions (declared in WorldRenderSpace.h).
WorldRenderView WorldRenderSpace_BuildView(const GameCamera* camera, double aspect, double nearPlane, double farPlane)
{
    WorldRenderView View;
    View.Origin = camera->Position;
    View.Camera = GameCamera_ToRenderCamera(camera);
    View.View = MatrixLookAt(View.Camera.position, View.Camera.target, View.Camera.up);
    View.Projection = MatrixPerspective((double)View.Camera.fovy * DEG2RAD, aspect, nearPlane, farPlane);
    View.InvProjection = MatrixInvert(View.Projection);
    View.ViewProjection = MatrixMultiply(View.View, View.Projection);
    View.InvViewProjection = MatrixInvert(View.ViewProjection);
    return View;
}

Vector3 WorldRenderSpace_GetSkyRay(const Matrix* invViewProjection, float u, float v)
{
    // sky/fragment.frag: clip = (uv * 2 - 1, 1, 1), world = invViewProj * clip, ray = normalize(world.xyz / world.w).
    const Matrix* M = invViewProjection;
    float ClipX = (u * 2.0f) - 1.0f;
    float ClipY = (v * 2.0f) - 1.0f;
    float X = (M->m0 * ClipX) + (M->m4 * ClipY) + M->m8 + M->m12;
    float Y = (M->m1 * ClipX) + (M->m5 * ClipY) + M->m9 + M->m13;
    float Z = (M->m2 * ClipX) + (M->m6 * ClipY) + M->m10 + M->m14;
    float W = (M->m3 * ClipX) + (M->m7 * ClipY) + M->m11 + M->m15;
    return Vector3Normalize((Vector3){ X / W, Y / W, Z / W });
}

WorldRenderCascade WorldRenderSpace_GetCascade(Vector3Double origin, Vector3Double anchor, Vector3 towardLight,
    const WorldRenderCascadeShape* shape)
{
    // The light's frame as MatrixLookAt builds it from the eye toward the box: Z toward the light, X = up x Z, Y = Z x X.
    // In double, so the camera's coordinates along X and Y keep their precision however far out the camera is.
    Vector3 Up = GetCascadeUp(towardLight);
    Vector3Double AxisZ = Vector3Double_NormalizeOr(GameMath_FromVector3(towardLight), Vector3Double_Create(0.0, 1.0, 0.0));
    Vector3Double AxisX = Vector3Double_NormalizeOr(Vector3Double_Cross(GameMath_FromVector3(Up), AxisZ),
        Vector3Double_Create(1.0, 0.0, 0.0));
    Vector3Double AxisY = Vector3Double_Cross(AxisZ, AxisX);

    // The camera seen from the anchor along the light's axes: the box's centre moves there by whole texels along the
    // right and up axes (which keeps the texel grid where it is) and all the way along the light (depth is not snapped).
    // In render space the centre is what the rounding left over.
    double Texel = (double)shape->Size / (double)shape->MapSize;
    Vector3Double FromAnchor = Vector3Double_Subtract(origin, anchor);
    double AlongX = Vector3Double_Dot(FromAnchor, AxisX);
    double AlongY = Vector3Double_Dot(FromAnchor, AxisY);
    double StepX = SnapToGrid(AlongX, Texel);
    double StepY = SnapToGrid(AlongY, Texel);
    Vector3Double Offset = Vector3Double_Add(Vector3Double_Scale(AxisX, StepX - AlongX),
        Vector3Double_Scale(AxisY, StepY - AlongY));

    // An explicit tight orthographic box around the centre (rather than BeginMode3D, which would force raylib's global
    // cull distances and make the depth bias unintuitive): depth is linear across the near and far planes, so the
    // shaders' bias maps to a small, world-relative offset.
    WorldRenderCascade Cascade;
    Vector3Double Moved = Vector3Double_AddScaled(anchor, AxisX, StepX);
    Moved = Vector3Double_AddScaled(Moved, AxisY, StepY);
    Cascade.Anchor = Vector3Double_AddScaled(Moved, AxisZ, Vector3Double_Dot(FromAnchor, AxisZ));
    Cascade.Center = GameMath_ToVector3(Offset);
    Vector3 Eye = Vector3Add(Cascade.Center, Vector3Scale(GameMath_ToVector3(AxisZ), shape->LightDistance));
    Cascade.View = MatrixLookAt(Eye, Cascade.Center, Up);
    double HalfSize = (double)shape->Size * 0.5;
    Cascade.Projection = MatrixOrtho(-HalfSize, HalfSize, -HalfSize, HalfSize, (double)shape->NearPlane,
        (double)shape->FarPlane);
    Cascade.ViewProjection = MatrixMultiply(Cascade.View, Cascade.Projection);
    return Cascade;
}

WorldRenderCascade WorldRenderSpace_StepCascade(WorldRenderCascadeGrid* grid, Vector3Double origin, Vector3 towardLight,
    const WorldRenderCascadeShape* shape)
{
    Vector3Double Anchor = grid->HasAnchor ? grid->Anchor : origin;
    WorldRenderCascade Cascade = WorldRenderSpace_GetCascade(origin, Anchor, towardLight, shape);
    grid->Anchor = Cascade.Anchor;
    grid->HasAnchor = true;
    return Cascade;
}
