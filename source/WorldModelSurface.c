// A world model's surfaces as pure data: the lookup by name, the texture coordinates at a point of one of a surface's
// triangles, where on the picture a point of the glass is (through a CRT's curvature), and the ray that finds where on a
// placed surface the player points. The "surfaces" section's parser and loader are in WorldModelSurfaceDefinition.c.

#include <math.h>
#include "WorldModel.h"
#include "CollisionGeometry.h"
#include "GameMath.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** Corners per triangle. */
#define TRIANGLE_CORNERS 3U


// Fields.
/* The kinds' names, as definitions write them, indexed by WorldModelSurfaceKind. */
static const unsigned char* const SurfaceKindNames[WorldModelSurfaceKind_Count] =
{
    [WorldModelSurfaceKind_Screen] = u8"screen",
    [WorldModelSurfaceKind_Paper] = u8"paper",
    [WorldModelSurfaceKind_Crt] = u8"crt"
};


// Public functions.
bool WorldModel_TryFindSurface(const WorldModel* self, const unsigned char* name, uint32_t* outIndex)
{
    if (name == NULL)
    {
        return false;
    }
    size_t Length = StringUTF8_GetByteLength(name);
    for (uint32_t Index = 0U; Index < self->_surfaceCount; Index++)
    {
        const unsigned char* Candidate = self->_surfaces[Index]._name;
        if ((StringUTF8_GetByteLength(Candidate) == Length) && Memory_IsEqual(Candidate, name, Length))
        {
            if (outIndex != NULL)
            {
                *outIndex = Index;
            }
            return true;
        }
    }
    return false;
}

bool WorldModelSurface_GetUv(const WorldModelSurface* self, uint32_t triangle, Vector3 barycentric, Vector2* outUv)
{
    *outUv = (Vector2){ 0.0f, 0.0f };
    if ((self->_mesh == NULL) || (triangle >= self->_mesh->_triangleCount))
    {
        return false;
    }
    const Vector2* Corners = &self->_triangleUvs[(size_t)triangle * TRIANGLE_CORNERS];
    Vector2 Uv =
    {
        (Corners[0].x * barycentric.x) + (Corners[1].x * barycentric.y) + (Corners[2].x * barycentric.z),
        (Corners[0].y * barycentric.x) + (Corners[1].y * barycentric.y) + (Corners[2].y * barycentric.z)
    };
    *outUv = Uv;
    return (Uv.x >= 0.0f) && (Uv.x <= 1.0f) && (Uv.y >= 0.0f) && (Uv.y <= 1.0f);
}

bool WorldModelSurface_GetPicturePosition(const WorldModelSurface* self, Vector2 uv, Vector2* outPicture)
{
    Vector2 Picture = uv;
    if (self->_kind == WorldModelSurfaceKind_Crt)
    {
        float Aspect = (self->_height > 0) ? ((float)self->_width / (float)self->_height) : 1.0f;
        Picture = CrtScreen_Distort(uv, self->_crt.Curvature, Aspect);
    }
    *outPicture = Picture;
    return (Picture.x >= 0.0f) && (Picture.x <= 1.0f) && (Picture.y >= 0.0f) && (Picture.y <= 1.0f);
}

bool WorldModelSurface_GetContinuedPicturePosition(const WorldModelSurface* self, Vector2 uv, Vector2* outPicture)
{
    Vector2 OnGlass = { Math_ClampFloat(uv.x, 0.0f, 1.0f), Math_ClampFloat(uv.y, 0.0f, 1.0f) };
    bool IsOnGlass = (OnGlass.x == uv.x) && (OnGlass.y == uv.y);
    if (IsOnGlass || (self->_kind != WorldModelSurfaceKind_Crt))
    {
        return WorldModelSurface_GetPicturePosition(self, uv, outPicture);
    }
    // The curvature grows with the distance from the middle, and would run the other axis away: past the glass it stays as its edge has it.
    Vector2 AtEdge;
    WorldModelSurface_GetPicturePosition(self, OnGlass, &AtEdge);
    *outPicture = (Vector2){ AtEdge.x + (uv.x - OnGlass.x), AtEdge.y + (uv.y - OnGlass.y) };
    return false;
}

bool WorldModelSurface_Raycast(const WorldModelSurface* self, const Matrix4Double* objectToWorld, Vector3Double origin,
    Vector3 direction, float maxDistance, WorldModelSurfaceHit* outHit)
{
    float Length = CollisionMath_Length(direction);
    if ((self->_mesh == NULL) || !(Length > 0.0f) || !isfinite(Length) || !(maxDistance > 0.0f))
    {
        return false;
    }
    Vector3 Unit = CollisionMath_Scale(direction, 1.0f / Length);

    // Into the object's own space: the origin relative to the object's position first (in double, so a placement far
    // from the world's origin keeps its precision), then the inverse of the matrix's turn and scale. The direction goes
    // through the same inverse unnormalised, so the ray's parameter there stays the distance in metres.
    Vector3Double Position = Matrix4Double_GetTranslation(objectToWorld);
    Vector3 Relative = GameMath_ToVector3Relative(origin, Position);
    CollisionAffine ToWorld = CollisionAffine_FromMatrix(GameMath_ToMatrixRelative(objectToWorld, Position));
    CollisionAffine ToObject;
    if (!CollisionAffine_TryInvert(&ToWorld, &ToObject))
    {
        return false;
    }
    Vector3 LocalOrigin = CollisionAffine_TransformPoint(&ToObject, Relative);
    Vector3 LocalDirection = CollisionAffine_TransformVector(&ToObject, Unit);
    CollisionRayHit Hit;
    if (!CollisionGeometry_RaycastMesh(self->_mesh, LocalOrigin, LocalDirection, maxDistance, false, &Hit))
    {
        return false;
    }
    outHit->Distance = Hit.T;
    outHit->Triangle = Hit.TriangleIndex;
    outHit->Barycentric = Hit.Barycentric;
    WorldModelSurface_GetUv(self, Hit.TriangleIndex, Hit.Barycentric, &outHit->Uv);
    outHit->IsOnPicture = WorldModelSurface_GetPicturePosition(self, outHit->Uv, &outHit->Picture);
    return true;
}

const unsigned char* WorldModelSurfaceKind_GetName(WorldModelSurfaceKind kind)
{
    return (((int32_t)kind >= 0) && ((int32_t)kind < (int32_t)WorldModelSurfaceKind_Count))
        ? SurfaceKindNames[kind] : NULL;
}

bool WorldModelSurface_ContinueRay(const WorldModelSurface* self, const Matrix4Double* objectToWorld, Vector3Double origin,
    Vector3 direction, Vector2* outUv, Vector3Double* outPoint)
{
    Vector3Double Center = Matrix4Double_TransformPoint(objectToWorld, GameMath_FromVector3(self->_center));
    Vector3Double AxisU = Matrix4Double_TransformDirection(objectToWorld, GameMath_FromVector3(self->_axisU));
    Vector3Double AxisV = Matrix4Double_TransformDirection(objectToWorld, GameMath_FromVector3(self->_axisV));
    Vector3Double Normal = Vector3Double_Cross(AxisU, AxisV);
    Vector3Double Direction = GameMath_FromVector3(direction);
    double NormalSquared = Vector3Double_LengthSquared(Normal);
    if (!(NormalSquared > 0.0) || !isfinite(NormalSquared) || !Vector3Double_IsFinite(Direction) || !Vector3Double_IsFinite(origin))
    {
        return false;
    }

    // From the centre to where the ray meets the plane, or, when it never does in front of it, along where it points.
    double Facing = Vector3Double_Dot(Direction, Normal);
    double Distance = Vector3Double_Dot(Vector3Double_Subtract(Center, origin), Normal) / Facing; // Infinite or no number when it runs along the plane.
    bool IsMet = (Distance > 0.0) && isfinite(Distance);
    Vector3Double Offset = IsMet ? Vector3Double_Subtract(Vector3Double_AddScaled(origin, Direction, Distance), Center) : Direction;
    // On the plane's two axes, which need not be square to each other (their dual basis).
    double U = Vector3Double_Dot(Offset, Vector3Double_Cross(AxisV, Normal)) / NormalSquared;
    double V = Vector3Double_Dot(Offset, Vector3Double_Cross(Normal, AxisU)) / NormalSquared;
    double Largest = fmax(fabs(U), fabs(V));
    if (!IsMet && !(Largest > 0.0))
    {
        return false;
    }
    if (!IsMet || (Largest > WORLD_MODEL_SURFACE_MAX_CONTINUED_REACH))
    {
        U *= WORLD_MODEL_SURFACE_MAX_CONTINUED_REACH / Largest;
        V *= WORLD_MODEL_SURFACE_MAX_CONTINUED_REACH / Largest;
    }
    *outUv = (Vector2){ (float)(U + (double)self->_centerUv.x), (float)(V + (double)self->_centerUv.y) };
    *outPoint = Vector3Double_AddScaled(Vector3Double_AddScaled(Center, AxisU, U), AxisV, V);
    return true;
}
