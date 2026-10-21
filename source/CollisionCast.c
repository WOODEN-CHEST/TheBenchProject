#include <float.h>
#include <math.h>
#include "CollisionGeometry.h"
#include "wr/WRMemory.h"

/*
 * Casts against planes, and casts that rotate (source/CollisionGeometry.h).
 *
 * A linear cast against an unbounded plane has a closed form: the shape's deepest point toward the plane reaches it at
 * a fraction of the motion. Against a bounded plane it is the GJK cast against the slab, with the one-sided rules.
 *
 * A ROTATING cast (a shape whose pose turns while it moves) has no closed form, so it uses conservative advancement
 * (Mirtich 1996): at the current time the shapes are some distance apart along a normal, and no point of the moving
 * shape closes along that normal faster than the motion's translation along it plus the turn times the core's
 * largest distance from the turn's axis times the sine between the axis and the normal (a slerp turns at a constant
 * rate about one fixed axis; a turn about the normal itself moves nothing toward the other shape, and the rounding of a
 * capsule or sphere turns with nothing). So the shapes cannot touch before distance / that bound has passed: stepping
 * by exactly that never skips a contact, whatever the speed. The steps shrink as the shapes close, and the cast stops
 * COLLISION_TOI_TARGET short of touching (or at half the gap it starts in, when it starts closer: a cast from where
 * the last one stopped may still move away or along). A step that lands in an overlap (rounding) falls back to the
 * last pose that was apart, so a hit always has a real normal and real points.
 */


// Macros.
/** A bounded plane is crossed from behind: a hit whose normal (from the shape into the plane) points this much along
 *  the plane's front is from behind and ignored. */
#define PLANE_BEHIND_COSINE 0.5f


// Types.
/* What a rotating cast's motion can do: its translation, its total turn about one fixed world axis (a slerp keeps
 * it), and the moving core's largest distance from that axis through its pose's origin. */
typedef struct MotionBoundStruct
{
    Vector3 Translation;
    float Turn;
    bool HasAxis;
    Vector3 Axis;
    float AxisRadius;
} MotionBound;

/* The distance from the moving shape (placed at a pose) to what it is cast against; false when it cannot be touched
 * from there (behind a bounded plane). */
typedef bool (*TargetDistanceFunction)(const void* target, const CollisionConvex* placed, CollisionDistance* outDistance);


// Static functions.
/* The view of the moving shape at pose @p pose. */
static void PlaceAtPose(const CollisionConvex* inPose, const CollisionPose* pose, CollisionConvex* outPlaced)
{
    *outPlaced = *inPose;
    CollisionAffine Pose = CollisionPose_ToAffine(pose);
    outPlaced->Transform = CollisionAffine_Multiply(&Pose, &inPose->Transform);
    CollisionConvex_Prepare(outPlaced);
}

static float GetAxisDistance(Vector3 point, Vector3 axis)
{
    return CollisionMath_Length(CollisionMath_AddScaled(point, axis, -CollisionMath_Dot(point, axis)));
}

/* The largest distance of the moving shape's core from the line through its pose's origin along the unit @p axis
 * (both in the pose's space): how far a turn about that line can carry it. A similarity's rounding turns with nothing;
 * an uneven scale's rounding is part of the core and bounded by its sphere. */
static float GetAxisRadius(const CollisionConvex* inPose, Vector3 axis)
{
    const CollisionAffine* Placement = &inPose->Transform;
    float Largest = 0.0f;
    switch (inPose->Kind)
    {
        case CollisionConvexKind_Point:
            Largest = GetAxisDistance(Placement->Translation, axis);
            break;
        case CollisionConvexKind_Segment:
        case CollisionConvexKind_Cylinder:
            for (int32_t Side = -1; Side <= 1; Side += 2)
            {
                Largest = fmaxf(Largest, GetAxisDistance(CollisionMath_AddScaled(Placement->Translation, Placement->Y,
                    (float)Side * inPose->HalfHeight), axis));
            }
            if (inPose->Kind == CollisionConvexKind_Cylinder)
            {
                Largest += inPose->CylinderRadius * CollisionAffine_GetMaxStretch(Placement);
            }
            break;
        case CollisionConvexKind_Box:
            for (uint32_t Corner = 0U; Corner < 8U; Corner++)
            {
                Vector3 Local = CollisionMath_Vector(((Corner & 1U) != 0U) ? inPose->HalfExtents.x : -inPose->HalfExtents.x,
                    ((Corner & 2U) != 0U) ? inPose->HalfExtents.y : -inPose->HalfExtents.y,
                    ((Corner & 4U) != 0U) ? inPose->HalfExtents.z : -inPose->HalfExtents.z);
                Largest = fmaxf(Largest, GetAxisDistance(CollisionAffine_TransformPoint(Placement, Local), axis));
            }
            break;
        case CollisionConvexKind_Hull:
            for (uint32_t Vertex = 0U; Vertex < inPose->Hull->_vertexCount; Vertex++)
            {
                Largest = fmaxf(Largest, GetAxisDistance(CollisionAffine_TransformPoint(Placement,
                    inPose->Hull->_vertices[Vertex]), axis));
            }
            break;
        case CollisionConvexKind_Triangle:
            for (uint32_t Corner = 0U; Corner < 3U; Corner++)
            {
                Largest = fmaxf(Largest, GetAxisDistance(inPose->Vertices[Corner], axis));
            }
            break;
    }
    if (!inPose->IsSimilarity && (inPose->Radius > 0.0f))
    {
        Largest += inPose->Radius * CollisionAffine_GetMaxStretch(Placement);
    }
    return Largest;
}

static void GetMotionBound(const CollisionConvex* inPose, const CollisionPose* from, const CollisionPose* to,
    MotionBound* outBound)
{
    outBound->Translation = CollisionMath_Subtract(to->Position, from->Position);
    outBound->Turn = CollisionPose_GetRotationAngle(from, to);
    outBound->HasAxis = false;
    outBound->Axis = CollisionMath_Vector(0.0f, 1.0f, 0.0f);
    outBound->AxisRadius = 0.0f;
    if (!(outBound->Turn > 0.0f))
    {
        return;
    }
    // The turn's axis in the start pose's frame is the relative rotation's (from^-1 to); in the world, that turned by
    // the start pose. Too small a turn to give an axis falls back to the core's distance from the origin itself.
    Quaternion Relative = QuaternionMultiply(QuaternionInvert(from->Rotation), to->Rotation);
    Vector3 Local = CollisionMath_Vector(Relative.x, Relative.y, Relative.z);
    float Length = CollisionMath_Length(Local);
    if (!(Length > 1e-6f))
    {
        outBound->AxisRadius = CollisionMath_Length(inPose->Transform.Translation) + inPose->BoundingRadius;
        return;
    }
    Local = CollisionMath_Scale(Local, 1.0f / Length);
    outBound->HasAxis = true;
    outBound->Axis = Vector3RotateByQuaternion(Local, from->Rotation);
    outBound->AxisRadius = GetAxisRadius(inPose, Local);
}

/* How fast (per unit of the motion's fraction) any point of the moving shape can close along the unit @p normal (from
 * it toward the target). */
static float GetApproachBound(const MotionBound* bound, Vector3 normal)
{
    float Spin = bound->Turn * bound->AxisRadius;
    if (bound->HasAxis)
    {
        Spin *= CollisionMath_Length(CollisionMath_Cross(normal, bound->Axis));
    }
    return CollisionMath_Dot(bound->Translation, normal) + Spin;
}

/* Fills a start-of-motion hit from a distance query that found the shapes touching or overlapping. */
static void SetStartHit(const CollisionConvex* a, const CollisionConvex* b, const CollisionDistance* distance,
    CollisionCastHit* outHit)
{
    outHit->Fraction = 0.0f;
    if (distance->AreCoresOverlapping)
    {
        CollisionPenetration Penetration;
        CollisionGeometry_GetPenetration(a, b, &Penetration);
        outHit->Normal = Penetration.Normal;
        outHit->PenetrationDepth = Penetration.Depth;
        outHit->PointA = Penetration.PointA;
        outHit->PointB = Penetration.PointB;
        outHit->IsStartPenetrating = true;
        return;
    }
    outHit->Normal = distance->Normal;
    outHit->PointA = distance->PointA;
    outHit->PointB = distance->PointB;
    outHit->PenetrationDepth = fmaxf(0.0f, -distance->Distance);
    outHit->IsStartPenetrating = distance->Distance <= 0.0f;
}

/* The distance and normal (from the shape to the plane) of a shape at rest against a plane; false when the plane
 * cannot be touched (behind a bounded plane's back face). */
static bool GetPlaneDistance(const CollisionConvex* a, const CollisionPlaneView* plane, CollisionDistance* outDistance)
{
    Memory_Zero(outDistance, sizeof(*outDistance));
    Vector3 Normal = plane->Normal;
    if (!plane->IsBounded)
    {
        Vector3 Deepest = CollisionConvex_SupportFull(a, CollisionMath_Negate(Normal));
        float Distance = CollisionMath_Dot(CollisionMath_Subtract(Deepest, plane->Origin), Normal);
        outDistance->Distance = Distance;
        outDistance->Normal = CollisionMath_Negate(Normal);
        outDistance->PointA = Deepest;
        outDistance->PointB = CollisionMath_AddScaled(Deepest, Normal, -Distance);
        return true;
    }
    if (CollisionMath_Dot(CollisionMath_Subtract(a->Center, plane->Origin), Normal) < -plane->BackDistance)
    {
        return false;
    }
    CollisionGeometry_GetDistance(a, &plane->Slab, outDistance);
    if (!outDistance->AreCoresOverlapping && (CollisionMath_Dot(outDistance->Normal, Normal) > PLANE_BEHIND_COSINE))
    {
        return false; // approaching the slab from behind
    }
    return true;
}


static bool GetConvexTargetDistance(const void* target, const CollisionConvex* placed, CollisionDistance* outDistance)
{
    CollisionGeometry_GetDistance(placed, target, outDistance);
    return true;
}

static bool GetPlaneTargetDistance(const void* target, const CollisionConvex* placed, CollisionDistance* outDistance)
{
    return GetPlaneDistance(placed, target, outDistance);
}

/* Conservative advancement from a start pose where the shapes are apart (@p start, its distance above 0) to the first
 * pose within the stopping gap. True with the hit at the last pose found apart; false when the motion never brings the
 * shape that close. */
static bool Advance(const CollisionConvex* inPose, const CollisionPose* from, const CollisionPose* to, const void* target,
    TargetDistanceFunction getDistance, const CollisionDistance* start, CollisionCastHit* outHit)
{
    MotionBound Bound;
    GetMotionBound(inPose, from, to, &Bound);
    float Target = fminf(COLLISION_TOI_TARGET, 0.5f * start->Distance);
    float Tolerance = fminf(COLLISION_CAST_TOLERANCE, 0.5f * Target);
    float T = 0.0f;
    CollisionDistance Current = *start;
    for (uint32_t Iteration = 0U; Iteration < COLLISION_TOI_MAX_ITERATIONS; Iteration++)
    {
        float Speed = GetApproachBound(&Bound, Current.Normal);
        if (!(Speed > 0.0f))
        {
            return false; // nothing of the shape closes along this normal: the separating plane holds to the end
        }
        float Next = T + ((Current.Distance - Target) / Speed);
        if (!(Next <= 1.0f))
        {
            return false; // the motion ends before the shapes can come that close
        }
        CollisionPose Pose = CollisionPose_Interpolate(from, to, Next);
        CollisionConvex Placed;
        PlaceAtPose(inPose, &Pose, &Placed);
        CollisionDistance Distance;
        if (!getDistance(target, &Placed, &Distance))
        {
            return false; // moved to where the target cannot be touched (beside and behind a bounded plane)
        }
        if (Distance.AreCoresOverlapping || (Distance.Distance <= 0.0f))
        {
            break; // the step overshot (rounding): the last pose found apart is the hit
        }
        T = Next;
        Current = Distance;
        if (Current.Distance <= (Target + Tolerance))
        {
            break;
        }
    }
    // Within the gap, after an overshoot, or out of iterations while still closing (never a missed contact).
    outHit->Fraction = T;
    outHit->Normal = Current.Normal;
    outHit->PointA = Current.PointA;
    outHit->PointB = Current.PointB;
    return true;
}


/* A start hit that a shallow overlap keeps only when the turning motion goes deeper (COLLISION_SHALLOW_START_DEPTH). */
static bool KeepsStartHit(const CollisionConvex* inPose, const CollisionPose* from, const CollisionPose* to,
    CollisionCastHit* hit)
{
    if (hit->PenetrationDepth > COLLISION_SHALLOW_START_DEPTH)
    {
        return true;
    }
    MotionBound Bound;
    GetMotionBound(inPose, from, to, &Bound);
    if (GetApproachBound(&Bound, hit->Normal) > 0.0f)
    {
        return true;
    }
    Memory_Zero(hit, sizeof(*hit));
    return false; // resting where the last cast left it, and moving away or along: free
}

/* The start of a cast touching or inside a plane: the plane's own push (to its front within the rectangle). False when
 * it touches nothing there (behind a bounded plane's back face, or apart). */
static bool GetPlaneStartHit(const CollisionConvex* a, const CollisionPlaneView* plane, CollisionCastHit* outHit)
{
    Memory_Zero(outHit, sizeof(*outHit));
    Vector3 Normal = plane->Normal;
    if (!plane->IsBounded)
    {
        Vector3 Deepest = CollisionConvex_SupportFull(a, CollisionMath_Negate(Normal));
        float Height = CollisionMath_Dot(CollisionMath_Subtract(Deepest, plane->Origin), Normal);
        if (Height > 0.0f)
        {
            return false;
        }
        outHit->Normal = CollisionMath_Negate(Normal);
        outHit->IsStartPenetrating = true;
        outHit->PenetrationDepth = -Height;
        outHit->PointA = Deepest;
        outHit->PointB = CollisionMath_AddScaled(Deepest, Normal, -Height);
        return true;
    }
    if (CollisionMath_Dot(CollisionMath_Subtract(a->Center, plane->Origin), Normal) < -plane->BackDistance)
    {
        return false;
    }
    CollisionContact Contact;
    if (!CollisionGeometry_CollidePlane(plane, a, 0.0f, &Contact))
    {
        return false;
    }
    outHit->IsStartPenetrating = Contact.Separation <= 0.0f;
    outHit->PenetrationDepth = fmaxf(0.0f, -Contact.Separation);
    outHit->Normal = CollisionMath_Negate(Contact.Normal);
    outHit->PointA = Contact.Points[0].PositionB;
    outHit->PointB = Contact.Points[0].PositionA;
    return true;
}


// Public functions.
bool CollisionGeometry_CastPlane(const CollisionConvex* a, Vector3 translation, const CollisionPlaneView* plane,
    CollisionCastHit* outHit)
{
    Memory_Zero(outHit, sizeof(*outHit));
    Vector3 Normal = plane->Normal;
    if (GetPlaneStartHit(a, plane, outHit))
    {
        // A shallow start (COLLISION_SHALLOW_START_DEPTH) blocks only a motion that goes deeper.
        bool IsDeeper = CollisionMath_Dot(translation, outHit->Normal)
            > (COLLISION_GJK_RELATIVE_TOLERANCE * CollisionMath_Length(translation));
        if ((outHit->PenetrationDepth <= COLLISION_SHALLOW_START_DEPTH) && !IsDeeper)
        {
            Memory_Zero(outHit, sizeof(*outHit));
            return false;
        }
        return true;
    }
    if (!plane->IsBounded)
    {
        Vector3 Deepest = CollisionConvex_SupportFull(a, CollisionMath_Negate(Normal));
        float Height = CollisionMath_Dot(CollisionMath_Subtract(Deepest, plane->Origin), Normal);
        outHit->Normal = CollisionMath_Negate(Normal);
        float Approach = CollisionMath_Dot(translation, Normal);
        if (Approach >= 0.0f)
        {
            return false;
        }
        float Fraction = Height / -Approach;
        if (Fraction > 1.0f)
        {
            return false;
        }
        outHit->Fraction = Fraction;
        outHit->PointA = CollisionMath_AddScaled(Deepest, translation, Fraction);
        outHit->PointB = outHit->PointA;
        return true;
    }
    if (CollisionMath_Dot(CollisionMath_Subtract(a->Center, plane->Origin), Normal) < -plane->BackDistance)
    {
        return false; // behind the slab: a bounded plane is passed from behind
    }
    if (!CollisionGeometry_Cast(a, translation, &plane->Slab, outHit))
    {
        return false;
    }
    if (!outHit->IsStartPenetrating && (CollisionMath_Dot(outHit->Normal, Normal) > PLANE_BEHIND_COSINE))
    {
        Memory_Zero(outHit, sizeof(*outHit));
        return false;
    }
    return true;
}

bool CollisionGeometry_CastMotion(const CollisionConvex* aInPose, const CollisionPose* from, const CollisionPose* to,
    const CollisionConvex* b, CollisionCastHit* outHit)
{
    Memory_Zero(outHit, sizeof(*outHit));
    CollisionConvex Placed;
    PlaceAtPose(aInPose, from, &Placed);
    if (CollisionConvex_IsBehindOneSided(b, Placed.Center))
    {
        return false;
    }
    CollisionDistance Start;
    CollisionGeometry_GetDistance(&Placed, b, &Start);
    if (Start.AreCoresOverlapping || (Start.Distance <= 0.0f))
    {
        SetStartHit(&Placed, b, &Start, outHit);
        CollisionConvex_FaceOneSided(b, outHit);
        return KeepsStartHit(aInPose, from, to, outHit);
    }
    if (!Advance(aInPose, from, to, b, GetConvexTargetDistance, &Start, outHit))
    {
        return false;
    }
    CollisionConvex_FaceOneSided(b, outHit);
    return true;
}

bool CollisionGeometry_CastMotionPlane(const CollisionConvex* aInPose, const CollisionPose* from,
    const CollisionPose* to, const CollisionPlaneView* plane, CollisionCastHit* outHit)
{
    Memory_Zero(outHit, sizeof(*outHit));
    CollisionConvex Placed;
    PlaceAtPose(aInPose, from, &Placed);
    CollisionDistance Start;
    if (!GetPlaneDistance(&Placed, plane, &Start))
    {
        return false;
    }
    if (Start.AreCoresOverlapping || (Start.Distance <= 0.0f))
    {
        // Touching or inside at the start: the plane's own push.
        return GetPlaneStartHit(&Placed, plane, outHit) && KeepsStartHit(aInPose, from, to, outHit);
    }
    return Advance(aInPose, from, to, plane, GetPlaneTargetDistance, &Start, outHit);
}
