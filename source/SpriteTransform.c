// The maths of a world sprite's quad: its world size, its matrix for a viewer, its bounds and the back-to-front order of
// a transparent list. See SpriteTransform.h.

#include <math.h>
#include "SpriteTransform.h"
#include "raylib/raymath.h"
#include "wr/WRComparator.h"
#include "wr/WRCompile.h"


// Macros.
/** Squared length below which a direction counts as none (a viewer at the pivot, an up along the normal). */
#define DEGENERATE_LENGTH_SQUARED 1e-10f


// Static functions.
/* @p vector made unit, or false when it is (nearly) zero or not finite. */
static bool TryNormalize(Vector3 vector, Vector3* outUnit)
{
    float LengthSquared = Vector3DotProduct(vector, vector);
    if (!(LengthSquared > DEGENERATE_LENGTH_SQUARED) || !isfinite(LengthSquared))
    {
        return false;
    }
    *outUnit = Vector3Scale(vector, 1.0f / sqrtf(LengthSquared));
    return true;
}

/* A unit vector perpendicular to the unit @p normal, as close to @p up as possible: @p up without its part along the
 * normal. When @p up is (nearly) along the normal, world +Z, then world +X stand in; at least one of those two is far
 * from any normal. */
static Vector3 GetPerpendicularUp(Vector3 normal, Vector3 up)
{
    const Vector3 Candidates[3] = { up, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } };
    for (size_t Index = 0; Index < (sizeof(Candidates) / sizeof(Candidates[0])); Index++)
    {
        Vector3 Candidate = Candidates[Index];
        Vector3 Perpendicular = Vector3Subtract(Candidate,
            Vector3Scale(normal, Vector3DotProduct(Candidate, normal)));
        Vector3 Unit;
        if (TryNormalize(Perpendicular, &Unit))
        {
            return Unit;
        }
    }
    return (Vector3){ 0.0f, 1.0f, 0.0f }; // only for a non-finite normal
}

/* The direction from the sprite toward its viewer (not unit; zero when a point viewer sits at the pivot). */
static Vector3 GetTowardViewer(const SpriteShape* shape, const SpriteViewer* viewer)
{
    return viewer->IsDirectional ? viewer->Direction : Vector3Subtract(viewer->Position, shape->Position);
}

/* The billboard's basis: the normal toward the viewer, the up leaning toward the viewer's up. */
static void GetBillboardBasis(const SpriteShape* shape, const SpriteViewer* viewer, Vector3* outRight, Vector3* outUp,
    Vector3* outNormal)
{
    Vector3 Normal;
    if (!TryNormalize(GetTowardViewer(shape, viewer), &Normal))
    {
        Normal = shape->AxisZ;
    }
    Vector3 Up = GetPerpendicularUp(Normal, viewer->Up);
    *outRight = Vector3CrossProduct(Up, Normal); // right x up = normal: right-handed, the image unmirrored to the viewer
    *outUp = Up;
    *outNormal = Normal;
}

/* The axis billboard's basis: the up is the object's Y axis, the normal the direction to the viewer without its part
 * along that axis (the object's Z when the viewer looks straight along the axis). */
static void GetAxisBillboardBasis(const SpriteShape* shape, const SpriteViewer* viewer, Vector3* outRight,
    Vector3* outUp, Vector3* outNormal)
{
    Vector3 Axis = shape->AxisY;
    Vector3 Toward = GetTowardViewer(shape, viewer);
    Vector3 AroundAxis = Vector3Subtract(Toward, Vector3Scale(Axis, Vector3DotProduct(Toward, Axis)));
    Vector3 Normal;
    if (!TryNormalize(AroundAxis, &Normal))
    {
        Normal = shape->AxisZ;
    }
    *outRight = Vector3CrossProduct(Axis, Normal);
    *outUp = Axis;
    *outNormal = Normal;
}

/* Draw order for the framework's ascending sort: farther first, then lower index first. */
static ComparisonResult CompareDrawOrder(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    UNUSED(buffer);
    UNUSED(userData);
    const SpriteDrawOrder* First = a._element;
    const SpriteDrawOrder* Second = b._element;
    if (First->Depth > Second->Depth)
    {
        return ComparisonResult_ALessThanB;
    }
    if (First->Depth < Second->Depth)
    {
        return ComparisonResult_AGreaterThanB;
    }
    if (First->Index != Second->Index)
    {
        return (First->Index < Second->Index) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
    }
    return ComparisonResult_AEqualsB;
}


// Internal functions (declared in SpriteTransform.h).
SpriteViewer SpriteViewer_FromPoint(Vector3 position, Vector3 up)
{
    return (SpriteViewer){ .IsDirectional = false, .Position = position, .Direction = { 0.0f, 0.0f, 0.0f }, .Up = up };
}

SpriteViewer SpriteViewer_FromDirection(Vector3 towardLight, Vector3 up)
{
    return (SpriteViewer){ .IsDirectional = true, .Position = { 0.0f, 0.0f, 0.0f }, .Direction = towardLight, .Up = up };
}

Vector2 SpriteTransform_GetWorldSize(Rectangle frameArea, float pixelsPerUnit, Vector3 objectScale)
{
    if (!(pixelsPerUnit > 0.0f) || !isfinite(pixelsPerUnit))
    {
        return (Vector2){ 0.0f, 0.0f };
    }
    return (Vector2)
    {
        frameArea.width / pixelsPerUnit * objectScale.x,
        frameArea.height / pixelsPerUnit * objectScale.y
    };
}

float SpriteTransform_GetBoundingRadius(Vector2 size, Vector2 pivot)
{
    float FarX = fmaxf(fabsf(pivot.x), fabsf(1.0f - pivot.x)) * fabsf(size.x);
    float FarY = fmaxf(fabsf(pivot.y), fabsf(1.0f - pivot.y)) * fabsf(size.y);
    return sqrtf(FarX * FarX + FarY * FarY);
}

Matrix SpriteTransform_GetMatrix(const SpriteShape* shape, const SpriteViewer* viewer)
{
    Vector3 Right = shape->AxisX;
    Vector3 Up = shape->AxisY;
    Vector3 Normal = shape->AxisZ;
    switch (shape->Orientation)
    {
        case WorldSpriteOrientation_Billboard:
            GetBillboardBasis(shape, viewer, &Right, &Up, &Normal);
            break;
        case WorldSpriteOrientation_AxisBillboard:
            GetAxisBillboardBasis(shape, viewer, &Right, &Up, &Normal);
            break;
        case WorldSpriteOrientation_Fixed:
            break;
    }

    // The image's axes scaled by the size and mirrored by the flips; the pivot's point of the image lands on Position.
    Vector3 Width = Vector3Scale(Right, shape->IsFlippedX ? -shape->Size.x : shape->Size.x);
    Vector3 Height = Vector3Scale(Up, shape->IsFlippedY ? -shape->Size.y : shape->Size.y);
    Vector3 Origin = Vector3Subtract(Vector3Subtract(shape->Position, Vector3Scale(Width, shape->Pivot.x)),
        Vector3Scale(Height, shape->Pivot.y));

    Matrix Result = MatrixIdentity();
    Result.m0 = Width.x;
    Result.m1 = Width.y;
    Result.m2 = Width.z;
    Result.m4 = Height.x;
    Result.m5 = Height.y;
    Result.m6 = Height.z;
    Result.m8 = Normal.x;
    Result.m9 = Normal.y;
    Result.m10 = Normal.z;
    Result.m12 = Origin.x;
    Result.m13 = Origin.y;
    Result.m14 = Origin.z;
    return Result;
}

Vector3 SpriteTransform_GetNormal(Matrix matrix)
{
    return (Vector3){ matrix.m8, matrix.m9, matrix.m10 };
}

Vector3 SpriteTransform_GetCenter(Matrix matrix)
{
    return Vector3Transform((Vector3){ 0.5f, 0.5f, 0.0f }, matrix);
}

bool SpriteTransform_SortBackToFront(GenericBuffer* drawOrder)
{
    if (drawOrder->_elementSize != sizeof(SpriteDrawOrder))
    {
        return false;
    }
    if (drawOrder->_count < 2)
    {
        return true;
    }
    SpriteDrawOrder Scratch[2];
    return GenericBuffer_SortAscending(drawOrder, CompareDrawOrder, NULL, Scratch);
}
