// Debug wireframes of the collision world's shapes (WorldRenderer_SetCollisionDebug): each shape of the collision world's
// debug list drawn in its own space under its world transform (uneven scales and shear included, so an ellipsoid looks
// like one), coloured by layer. Rule 3: it only reads the list; nothing here touches the world or the collision world.

#include "WorldRendererInternal.h"
#include "WorldCollision.h"
#include "raylib/raymath.h"


// Macros.
/** How finely the round shapes are drawn. */
#define SPHERE_RINGS 8
#define SPHERE_SLICES 12
#define CAPSULE_SLICES 12
#define CAPSULE_RINGS 4
#define CYLINDER_SIDES 16
/** The length of the tick showing a plane's front (its local +Y), in its own units. */
#define PLANE_NORMAL_LENGTH 0.25f
/** An unbounded plane is drawn as a grid this far around its origin, with lines this far apart (its own units). */
#define UNBOUNDED_PLANE_HALF_SIZE 20.0f
#define UNBOUNDED_PLANE_STEP 2.0f
/** How much darker static shapes and a mesh's inactive edges are drawn. */
#define STATIC_SHADE 0.6f
#define INACTIVE_EDGE_SHADE 0.45f
/** The opacity of the parts behind something (drawn without the depth test, under the visible parts). */
#define HIDDEN_ALPHA 90


// Static functions.
static Color Shade(Color color, float factor)
{
    return (Color){ (unsigned char)((float)color.r * factor), (unsigned char)((float)color.g * factor),
        (unsigned char)((float)color.b * factor), color.a };
}

/* A layer's colour: triggers violet whatever their layer, static shapes darker. */
static Color GetShapeColor(const WorldCollisionDebugShape* shape)
{
    Color Base = { 255, 255, 255, 255 };
    switch (shape->Layer)
    {
        case CollisionLayer_World:
            Base = (Color){ 190, 190, 190, 255 };
            break;
        case CollisionLayer_Prop:
            Base = (Color){ 255, 160, 40, 255 };
            break;
        case CollisionLayer_Character:
            Base = (Color){ 60, 230, 90, 255 };
            break;
        case CollisionLayer_Vehicle:
            Base = (Color){ 70, 140, 255, 255 };
            break;
        case CollisionLayer_Dynamic:
            Base = (Color){ 255, 230, 60, 255 };
            break;
        case CollisionLayer_Trigger:
            Base = (Color){ 200, 90, 255, 255 };
            break;
        case CollisionLayer_Interaction:
            Base = (Color){ 40, 220, 220, 255 };
            break;
        case CollisionLayer_Count:
            break;
    }
    if (shape->IsTrigger)
    {
        Base = (Color){ 200, 90, 255, 255 };
    }
    return shape->IsStatic ? Shade(Base, STATIC_SHADE) : Base;
}

static void DrawHullEdges(const CollisionHull* hull, Color color)
{
    if (hull == NULL)
    {
        return;
    }
    for (uint32_t Edge = 0U; Edge < hull->_edgeCount; Edge++)
    {
        DrawLine3D(hull->_vertices[hull->_edgeVertexIndices[Edge * 2U]],
            hull->_vertices[hull->_edgeVertexIndices[(Edge * 2U) + 1U]], color);
    }
}

/* Every triangle's edges; the inactive ones (flat or concave seams, where contacts take the face normal) darker. */
static void DrawMeshEdges(const CollisionMesh* mesh, Color color)
{
    if (mesh == NULL)
    {
        return;
    }
    Color Inactive = Shade(color, INACTIVE_EDGE_SHADE);
    for (uint32_t Index = 0U; Index < mesh->_triangleCount; Index++)
    {
        const CollisionMeshTriangle* Triangle = &mesh->_triangles[Index];
        for (uint32_t Edge = 0U; Edge < 3U; Edge++)
        {
            bool IsActive = (Triangle->_activeEdges & (1U << Edge)) != 0U;
            DrawLine3D(mesh->_vertices[Triangle->_indices[Edge]], mesh->_vertices[Triangle->_indices[(Edge + 1U) % 3U]],
                IsActive ? color : Inactive);
        }
    }
}

/* A plane: its rectangle (and the slab behind it), or a grid around its origin when it is unbounded; a tick shows the
 * front. */
static void DrawPlaneShape(const CollisionShape* shape, Color color)
{
    Vector3 Origin = { 0.0f, 0.0f, 0.0f };
    if (shape->IsBounded)
    {
        float X = shape->PlaneHalfExtents.x;
        float Z = shape->PlaneHalfExtents.y;
        DrawLine3D((Vector3){ -X, 0.0f, -Z }, (Vector3){ X, 0.0f, -Z }, color);
        DrawLine3D((Vector3){ X, 0.0f, -Z }, (Vector3){ X, 0.0f, Z }, color);
        DrawLine3D((Vector3){ X, 0.0f, Z }, (Vector3){ -X, 0.0f, Z }, color);
        DrawLine3D((Vector3){ -X, 0.0f, Z }, (Vector3){ -X, 0.0f, -Z }, color);
        if (shape->Depth > 0.0f)
        {
            DrawCubeWires((Vector3){ 0.0f, -0.5f * shape->Depth, 0.0f }, 2.0f * X, shape->Depth, 2.0f * Z, color);
        }
    }
    else
    {
        for (float At = -UNBOUNDED_PLANE_HALF_SIZE; At <= UNBOUNDED_PLANE_HALF_SIZE; At += UNBOUNDED_PLANE_STEP)
        {
            DrawLine3D((Vector3){ At, 0.0f, -UNBOUNDED_PLANE_HALF_SIZE }, (Vector3){ At, 0.0f, UNBOUNDED_PLANE_HALF_SIZE },
                color);
            DrawLine3D((Vector3){ -UNBOUNDED_PLANE_HALF_SIZE, 0.0f, At }, (Vector3){ UNBOUNDED_PLANE_HALF_SIZE, 0.0f, At },
                color);
        }
    }
    DrawLine3D(Origin, (Vector3){ 0.0f, PLANE_NORMAL_LENGTH, 0.0f }, color);
}

/* One shape in its own space (the caller set its transform). Capsules and cylinders lie along local Y. */
static void DrawShape(const CollisionShape* shape, Color color)
{
    Vector3 Origin = { 0.0f, 0.0f, 0.0f };
    Vector3 Top = { 0.0f, shape->HalfHeight, 0.0f };
    Vector3 Bottom = { 0.0f, -shape->HalfHeight, 0.0f };
    switch (shape->Type)
    {
        case CollisionShapeType_Sphere:
            DrawSphereWires(Origin, shape->Radius, SPHERE_RINGS, SPHERE_SLICES, color);
            break;
        case CollisionShapeType_Capsule:
            if (shape->HalfHeight > 0.0f)
            {
                DrawCapsuleWires(Bottom, Top, shape->Radius, CAPSULE_SLICES, CAPSULE_RINGS, color);
            }
            else
            {
                DrawSphereWires(Origin, shape->Radius, SPHERE_RINGS, SPHERE_SLICES, color);
            }
            break;
        case CollisionShapeType_Box:
            DrawCubeWires(Origin, 2.0f * shape->HalfExtents.x, 2.0f * shape->HalfExtents.y, 2.0f * shape->HalfExtents.z,
                color);
            break;
        case CollisionShapeType_Cylinder:
            DrawCylinderWiresEx(Bottom, Top, shape->Radius, shape->Radius, CYLINDER_SIDES, color);
            break;
        case CollisionShapeType_ConvexHull:
            DrawHullEdges(shape->Hull, color);
            break;
        case CollisionShapeType_Plane:
            DrawPlaneShape(shape, color);
            break;
        case CollisionShapeType_Mesh:
            DrawMeshEdges(shape->Mesh, color);
            break;
        case CollisionShapeType_Count:
            break;
    }
}


static void DrawShapes(const WorldCollisionDebugShape* shapes, size_t count, unsigned char alpha)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        Color ShapeColor = GetShapeColor(&shapes[Index]);
        ShapeColor.a = alpha;
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(shapes[Index].Transform));
        DrawShape(shapes[Index].Shape, ShapeColor);
        rlPopMatrix();
    }
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererCollision_Draw(WorldRenderer* self)
{
    size_t Count = 0U;
    const WorldCollisionDebugShape* Shapes = WorldCollision_GetDebugShapes(self->_collisionDebug, &Count);
    if (Count == 0U)
    {
        return;
    }
    // Twice: faint without the depth test (what is hidden, also the parts of a line that lie on a surface and would
    // fight its depth), then solid with it. rlgl batches its lines, so each depth state is drawn out before it changes.
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    DrawShapes(Shapes, Count, HIDDEN_ALPHA);
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    DrawShapes(Shapes, Count, 255);
}
