// A world model's surfaces as pure data: the lookup by name, the texture coordinates at a point of one of a surface's
// triangles, and the ray that finds where on a placed surface the player points. The "surfaces" section's parser and
// loader are in WorldModelSurfaceDefinition.c.

#include <math.h>
#include "WorldModel.h"
#include "CollisionGeometry.h"
#include "GameMath.h"
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
    [WorldModelSurfaceKind_Paper] = u8"paper"
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
    outHit->IsOnPicture = WorldModelSurface_GetUv(self, Hit.TriangleIndex, Hit.Barycentric, &outHit->Uv);
    return true;
}

const unsigned char* WorldModelSurfaceKind_GetName(WorldModelSurfaceKind kind)
{
    return (((int32_t)kind >= 0) && ((int32_t)kind < (int32_t)WorldModelSurfaceKind_Count))
        ? SurfaceKindNames[kind] : NULL;
}
