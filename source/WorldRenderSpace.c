// Render space, the space the world renderer draws in: the frame's view with the camera at the origin, the sky shader's
// ray in C, and the shadow cascades snapped to their texels. See WorldRenderSpace.h.

#include <math.h>
#include "WorldRenderSpace.h"
#include "GameMath.h"
#include "raylib/raymath.h"


// Static functions.
/* The up vector the light's view is built with (MatrixLookAt's third argument). */
static Vector3 GetCascadeUp(Vector3 towardLight)
{
    return (fabsf(towardLight.y) > WORLD_RENDER_SPACE_CASCADE_STRAIGHT_UP_Y) ? (Vector3){ 0.0f, 0.0f, 1.0f }
        : (Vector3){ 0.0f, 1.0f, 0.0f };
}

/* @p coordinate rounded to the nearest whole multiple of @p step. */
static double SnapToGrid(double coordinate, double step)
{
    return floor((coordinate / step) + 0.5) * step;
}

/* The light's unit direction in double, as the axes are built from it. */
static Vector3Double GetCascadeAxisZ(Vector3 towardLight)
{
    return Vector3Double_NormalizeOr(GameMath_FromVector3(towardLight), Vector3Double_Create(0.0, 1.0, 0.0));
}

/* A cascade's matrices around its render-space centre (cascade->Center, set by the caller): an explicit tight
 * orthographic box around the centre (rather than BeginMode3D, which would force raylib's global cull distances and make
 * the depth bias unintuitive), so depth is linear across the near and far planes and the shaders' bias maps to a small,
 * world-relative offset. */
static void BuildCascadeMatrices(WorldRenderCascade* cascade, Vector3Double axisZ, Vector3 up,
    const WorldRenderCascadeShape* shape)
{
    Vector3 Eye = Vector3Add(cascade->Center, Vector3Scale(GameMath_ToVector3(axisZ), shape->LightDistance));
    cascade->View = MatrixLookAt(Eye, cascade->Center, up);
    double HalfSize = (double)shape->Size * 0.5;
    cascade->Projection = MatrixOrtho(-HalfSize, HalfSize, -HalfSize, HalfSize, (double)shape->NearPlane,
        (double)shape->FarPlane);
    cascade->ViewProjection = MatrixMultiply(cascade->View, cascade->Projection);
}


// Internal functions (declared in WorldRenderSpace.h).
Matrix WorldRenderSpace_GetReversedProjection(double fovY, double aspect, double nearPlane, Matrix* outInverse)
{
    // MatrixPerspective's x and y; clip z = the near plane's distance, clip w = -z (the distance in front): depth
    // z/w = near / distance after clip control's 0-to-1 mapping.
    double Focal = 1.0 / tan(fovY * 0.5);
    Matrix Projection = { 0 };
    Projection.m0 = (float)(Focal / aspect);
    Projection.m5 = (float)Focal;
    Projection.m11 = -1.0f;
    Projection.m14 = (float)nearPlane;
    if (outInverse != NULL)
    {
        // x = clip x aspect / focal, y = clip y / focal, z = -clip w, w = clip z / near.
        Matrix Inverse = { 0 };
        Inverse.m0 = (float)(aspect / Focal);
        Inverse.m5 = (float)(1.0 / Focal);
        Inverse.m11 = (float)(1.0 / nearPlane);
        Inverse.m14 = -1.0f;
        *outInverse = Inverse;
    }
    return Projection;
}

WorldRenderView WorldRenderSpace_BuildView(const GameCamera* camera, double aspect, double nearPlane, double farPlane,
    bool isDepthReversed)
{
    WorldRenderView View;
    View.Origin = camera->Position;
    View.Camera = GameCamera_ToRenderCamera(camera);
    View.View = MatrixLookAt(View.Camera.position, View.Camera.target, View.Camera.up);
    if (isDepthReversed)
    {
        View.Projection = WorldRenderSpace_GetReversedProjection((double)View.Camera.fovy * DEG2RAD, aspect, nearPlane,
            &View.InvProjection);
    }
    else
    {
        View.Projection = MatrixPerspective((double)View.Camera.fovy * DEG2RAD, aspect, nearPlane, farPlane);
        View.InvProjection = MatrixInvert(View.Projection);
    }
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
    Vector3Double AxisZ = GetCascadeAxisZ(towardLight);
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

    WorldRenderCascade Cascade;
    Vector3Double Moved = Vector3Double_AddScaled(anchor, AxisX, StepX);
    Moved = Vector3Double_AddScaled(Moved, AxisY, StepY);
    Cascade.Anchor = Vector3Double_AddScaled(Moved, AxisZ, Vector3Double_Dot(FromAnchor, AxisZ));
    Cascade.Center = GameMath_ToVector3(Offset);
    BuildCascadeMatrices(&Cascade, AxisZ, Up, shape);
    return Cascade;
}

bool WorldRenderSpace_HasSameCascadeUp(Vector3 a, Vector3 b)
{
    bool IsAUp = fabsf(a.y) > WORLD_RENDER_SPACE_CASCADE_STRAIGHT_UP_Y;
    bool IsBUp = fabsf(b.y) > WORLD_RENDER_SPACE_CASCADE_STRAIGHT_UP_Y;
    return IsAUp == IsBUp;
}

WorldRenderCascade WorldRenderSpace_GetCascadeAt(Vector3Double origin, Vector3Double center, Vector3 towardLight,
    const WorldRenderCascadeShape* shape)
{
    WorldRenderCascade Cascade;
    Cascade.Anchor = center;
    Cascade.Center = GameMath_ToVector3Relative(center, origin);
    BuildCascadeMatrices(&Cascade, GetCascadeAxisZ(towardLight), GetCascadeUp(towardLight), shape);
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
