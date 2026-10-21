// The character capsule's motion against the collision world: sweeps, support, move and slide, steps, following the
// ground and the room to stand up (WorldCharacterInternal.h). Used by the character system (WorldCharacters.c).

#include <math.h>
#include "WorldCharacterInternal.h"
#include "WorldCharacters.h"
#include "raylib/raymath.h"
#include "wr/WRMath.h"


// Macros.
/** A dot product below minus this goes into a plane. */
#define PLANE_EPSILON 1e-6f
/** Squared lengths below this are zero. */
#define LENGTH_SQUARED_EPSILON 1e-12f


// Static functions.
static const Vector3 WorldUp = { 0.0f, 1.0f, 0.0f };

/* The capsule's centre over its feet. */
static Vector3 GetCenter(const CharacterBody* body, Vector3 feet)
{
    return (Vector3){ feet.x, feet.y + (0.5f * body->Height), feet.z };
}

/* How far along a move of @p direction (unit) to go toward a contact @p distance away (where the sweep's wider capsule
 * touches it) so the gap left along the contact's normal is CHARACTER_REST_GAP beyond the skin: the rest gap over how
 * directly the move closes on it, at most the rest gap over CHARACTER_MIN_APPROACH for grazing contacts. */
static float GetAdvance(Vector3 direction, Vector3 normal, float distance)
{
    float Approach = Math_MaxFloat(-Vector3DotProduct(direction, normal), CHARACTER_MIN_APPROACH);
    return Math_MaxFloat(0.0f, distance - (CHARACTER_REST_GAP / Approach));
}

/* @p feet after going @p advance along a sweep's @p direction (unit) toward its hit, backed out along @p away (the
 * hit's normal, or a walker's upright wall's) when the gap left to it along the hit's normal is short of the one
 * GetAdvance leaves, but never short of CHARACTER_MIN_REST_GAP: it started closer (a slide along a wall drifts toward
 * it by rounding, a little every tick). Nothing else gives the gap back, the collision step pushing only within the
 * skin, and a capsule left touching stalls on moves that only roll along a contact. A walker backs away from a wall
 * level, not up the edge it cannot climb. */
static Vector3 KeepRestGap(Vector3 feet, Vector3 direction, float advance, const WorldCastHit* hit, Vector3 away)
{
    float Approach = -Vector3DotProduct(direction, hit->Normal);
    if (Approach <= 0.0f)
    {
        return feet;
    }
    float Gap = (hit->Distance - advance) * Approach;
    float Wanted = Math_MaxFloat(CHARACTER_REST_GAP * Math_MinFloat(1.0f, Approach / CHARACTER_MIN_APPROACH),
        CHARACTER_MIN_REST_GAP);
    if (Gap >= Wanted)
    {
        return feet;
    }
    float Share = Math_MaxFloat(Vector3DotProduct(away, hit->Normal), CHARACTER_MIN_APPROACH);
    return Vector3Add(feet, Vector3Scale(away, (Wanted - Gap) / Share));
}

/* Whether @p vector goes into none of @p planes by more than @p tolerance. */
static bool IsClearOfPlanes(Vector3 vector, const Vector3* planes, size_t planeCount, float tolerance)
{
    for (size_t Index = 0U; Index < planeCount; Index++)
    {
        if (Vector3DotProduct(vector, planes[Index]) < -tolerance)
        {
            return false;
        }
    }
    return true;
}

/* @p vector without its part into @p plane, when it goes into it. */
static Vector3 ClipToPlane(Vector3 vector, Vector3 plane)
{
    float Into = Vector3DotProduct(vector, plane);
    return (Into < 0.0f) ? Vector3Subtract(vector, Vector3Scale(plane, Into)) : vector;
}

/* A sweep for something to stand on, with pushables (a crate to stand on). Starting inside something whose push-out
 * points up is ground it sank into (a crate it fell onto, a lift that rose into it): the caller gets out upward. Starting
 * inside something from the side, it looks again without pushables: a side-long overlap with a pushable (another
 * character it walks into, which the collision step will push) is neither ground nor a reason to move, and
 * @p outIsHidden says that pushable may have hidden the ground. Only an overlap with what does not give way is then
 * handed back, for the caller to get out of. */
static bool SweepForGround(const CharacterBody* body, Vector3 from, Vector3 to, WorldCastHit* outHit, bool* outIsHidden)
{
    if (!CharacterMotion_Sweep(body, from, to, &body->GroundFilter, outHit))
    {
        return false;
    }
    if (!outHit->IsStartPenetrating || (outHit->Normal.y >= body->WalkableNormalY))
    {
        return true;
    }
    WorldCastHit Without;
    bool IsHit = CharacterMotion_Sweep(body, from, to, &body->MoveFilter, &Without);
    if (!IsHit || !Without.IsStartPenetrating)
    {
        *outIsHidden = true;
    }
    if (IsHit)
    {
        *outHit = Without;
    }
    return IsHit;
}

/* The face under a contact on the capsule's round bottom: a short ray down just inside the contact (the contact's
 * normal points from it toward the capsule, so inside is the other way). False when the contact is straight below the
 * capsule's axis (no inside to look at), or nothing is found there. */
static bool ProbeFaceUnder(const CharacterBody* body, const WorldCastHit* hit, WorldRayHit* outFace)
{
    Vector3 Inward = { -hit->Normal.x, 0.0f, -hit->Normal.z };
    float InwardLength = Vector3Length(Inward);
    if (InwardLength < 1e-6f)
    {
        return false;
    }
    Inward = Vector3Scale(Inward, 1.0f / InwardLength);
    Vector3 Origin = Vector3Add(hit->Point, Vector3Scale(Inward, CHARACTER_EDGE_PROBE_INSET));
    Origin.y += CHARACTER_EDGE_PROBE_LIFT;
    return WorldCollision_Raycast(body->Collision, Origin, (Vector3){ 0.0f, -1.0f, 0.0f },
        CHARACTER_EDGE_PROBE_LIFT + CHARACTER_EDGE_PROBE_DEPTH, &body->GroundFilter, outFace) && !outFace->IsStartInside;
}

/* How far @p point is above the plane of @p ground (through its contact, along its normal), measured straight up: the
 * height of a step above the ground a walker walks on, a slope's or a crest's edge on that plane being none. */
static float GetHeightAboveGround(const CharacterSupport* ground, Vector3 point)
{
    Vector3 Offset = Vector3Subtract(point, ground->Point);
    if (ground->Normal.y < 1e-3f)
    {
        return Offset.y;
    }
    return Vector3DotProduct(ground->Normal, Offset) / ground->Normal.y;
}

/* Whether a contact is an edge higher than the step height above the plane of the @p ground a walker walks on: the
 * round bottom would otherwise roll over any edge up to radius x (1 - cos slope_limit) whatever the step height (8.8 cm
 * for a human), the slide's contacts and the ground's alike. A contact on a face (the face under it turned as the
 * contact) is no edge: a ramp's foot is no step. */
static bool IsEdgeAboveStep(const CharacterBody* body, const CharacterSupport* ground, const WorldCastHit* hit)
{
    if (GetHeightAboveGround(ground, hit->Point) <= (body->StepHeight + CHARACTER_SKIN))
    {
        return false;
    }
    WorldRayHit Face;
    if (!ProbeFaceUnder(body, hit, &Face))
    {
        return hit->Normal.y < 1.0f - 1e-6f;
    }
    return Vector3DotProduct(Face.Normal, hit->Normal) < CHARACTER_SAME_FACE_COSINE;
}

/* The walkable ground a walker at @p feet rests on now: a ray from its bottom cap's centre along its @p ground's normal,
 * reaching as far as the cap rests from that ground (its radius, the skin and the rest gap) and a skin more. */
static bool FindRestingGround(const CharacterBody* body, Vector3 feet, const CharacterSupport* ground,
    CharacterSupport* outSupport)
{
    Vector3 Origin = { feet.x, feet.y + body->Radius, feet.z };
    float Reach = body->Radius + (2.0f * CHARACTER_SKIN) + CHARACTER_REST_GAP;
    WorldRayHit Hit;
    if (!WorldCollision_Raycast(body->Collision, Origin, Vector3Negate(ground->Normal), Reach, &body->GroundFilter, &Hit)
        || Hit.IsStartInside || (Hit.Normal.y < body->WalkableNormalY))
    {
        return false;
    }
    *outSupport = (CharacterSupport){ .IsSupported = true, .ObjectId = Hit.Shape.ObjectId, .Normal = Hit.Normal,
        .Point = Hit.Point };
    return true;
}

/* Tries to climb a step after a walking move along @p direction (unit) was blocked at @p feet, @p gap short of where
 * the sweep's wider capsule touches the riser (the rest gap the slide kept) with @p remaining metres of the move left:
 * up by the step height (less a ceiling), forward by what was left of the move but at least past the gap
 * (CHARACTER_STEP_ENGAGE_DISTANCE further: otherwise a riser met with less than the gap of the move left, or by a slow
 * walker, would never be climbed), then back down onto
 * something to stand on within the climb. Accepted only when it lands supported, higher than it started, on something
 * no higher than the step height above the @p ground it walks on: a step's edge counts (the round bottom then rolls
 * over it in the following ticks), a taller step's edge does not. */
static bool TryStepUp(const CharacterBody* body, const CharacterSupport* ground, Vector3 feet, Vector3 direction,
    float remaining, float gap, Vector3* outFeet, CharacterSupport* outSupport)
{
    Vector3 Flat = { direction.x, 0.0f, direction.z };
    float FlatShare = Vector3Length(Flat);
    if ((FlatShare < 1e-4f) || (body->StepHeight < CHARACTER_MIN_STEP_CLIMB))
    {
        return false;
    }
    float HorizontalLength = Math_MaxFloat(remaining * FlatShare, (gap * FlatShare) + CHARACTER_STEP_ENGAGE_DISTANCE);
    Vector3 Horizontal = Vector3Scale(Flat, HorizontalLength / FlatShare);

    // Up, stopping under a ceiling.
    WorldCastHit Hit;
    Vector3 Raised = Vector3Add(feet, (Vector3){ 0.0f, body->StepHeight, 0.0f });
    if (CharacterMotion_Sweep(body, feet, Raised, &body->MoveFilter, &Hit))
    {
        if (Hit.IsStartPenetrating)
        {
            return false;
        }
        Raised = Vector3Add(feet, (Vector3){ 0.0f, GetAdvance(WorldUp, Hit.Normal, Hit.Distance), 0.0f });
    }
    float Climb = Raised.y - feet.y;
    if (Climb < CHARACTER_MIN_STEP_CLIMB)
    {
        return false;
    }

    // Forward, as far as the move had left (looking further, as the slide does).
    Vector3 Direction = Vector3Scale(Horizontal, 1.0f / HorizontalLength);
    Vector3 Forward = Vector3Add(Raised, Horizontal);
    Vector3 Probe = Vector3Add(Forward, Vector3Scale(Direction, CHARACTER_REST_GAP / CHARACTER_MIN_APPROACH));
    if (CharacterMotion_Sweep(body, Raised, Probe, &body->MoveFilter, &Hit))
    {
        if (Hit.IsStartPenetrating)
        {
            return false;
        }
        float Advance = Math_MinFloat(HorizontalLength, GetAdvance(Direction, Hit.Normal, Hit.Distance));
        if (Advance < CHARACTER_MIN_MOVE)
        {
            return false;
        }
        Forward = Vector3Add(Raised, Vector3Scale(Direction, Advance));
    }

    // Down onto the step: back to the starting height, and a skin or two further.
    Vector3 Lowered = Vector3Subtract(Forward, (Vector3){ 0.0f, Climb + (2.0f * CHARACTER_SKIN), 0.0f });
    bool IsHidden = false;
    if (!SweepForGround(body, Forward, Lowered, &Hit, &IsHidden) || Hit.IsStartPenetrating)
    {
        return false;
    }
    Vector3 Landed = Vector3Subtract(Forward, (Vector3){ 0.0f, GetAdvance((Vector3){ 0.0f, -1.0f, 0.0f }, Hit.Normal,
        Hit.Distance), 0.0f });
    if (Landed.y < (feet.y + CHARACTER_MIN_STEP_RISE))
    {
        // Back on the level it started from: no step there, only a wall.
        return false;
    }
    CharacterSupport Support = CharacterMotion_EvaluateSupport(body, &Hit);
    if (!Support.IsSupported || (GetHeightAboveGround(ground, Support.Point) > (body->StepHeight + CHARACTER_SKIN)))
    {
        return false;
    }
    *outFeet = Landed;
    *outSupport = Support;
    return true;
}


// Functions (WorldCharacterInternal.h).
CharacterSupport CharacterSupport_CreateNone(void)
{
    return (CharacterSupport){ .IsSupported = false, .ObjectId = 0U, .Normal = WorldUp, .Point = { 0.0f, 0.0f, 0.0f } };
}

bool CharacterMotion_Sweep(const CharacterBody* body, Vector3 from, Vector3 to, const WorldCollisionFilter* filter,
    WorldCastHit* outHit)
{
    return WorldCollision_SweepCapsule(body->Collision, GetCenter(body, from), GetCenter(body, to),
        body->Radius + CHARACTER_SKIN, body->Height + (2.0f * CHARACTER_SKIN), filter, outHit);
}

CharacterSupport CharacterMotion_EvaluateSupport(const CharacterBody* body, const WorldCastHit* hit)
{
    CharacterSupport Support = CharacterSupport_CreateNone();
    Vector3 Normal = hit->Normal;
    if (Normal.y >= body->WalkableNormalY)
    {
        Support.IsSupported = true;
        Support.ObjectId = hit->Shape.ObjectId;
        Support.Normal = Normal;
        Support.Point = hit->Point;
        return Support;
    }
    if (Normal.y <= CHARACTER_EDGE_MIN_NORMAL_Y)
    {
        return Support;
    }

    // An edge (or a steep face) under the round bottom: the face just inside the contact decides.
    WorldRayHit Face;
    if (!ProbeFaceUnder(body, hit, &Face) || (Face.Normal.y < body->WalkableNormalY))
    {
        return Support;
    }
    Support.IsSupported = true;
    Support.ObjectId = hit->Shape.ObjectId;
    Support.Normal = Face.Normal;
    Support.Point = hit->Point;
    return Support;
}

Vector3 CharacterMotion_ClipToPlanes(Vector3 vector, const Vector3* planes, size_t planeCount)
{
    // A vector clipped against a plane keeps a rounding error along it of the order of the vector it was clipped from,
    // however short the clipped one is: the tolerance grows with that (at 10 m/s a fall almost straight into a slope,
    // read against the short slide's own length, came back as no slide at all).
    float Tolerance = PLANE_EPSILON * Math_MaxFloat(1.0f, Vector3Length(vector));
    if (IsClearOfPlanes(vector, planes, planeCount, Tolerance))
    {
        return vector;
    }
    // One plane may be enough.
    for (size_t Index = 0U; Index < planeCount; Index++)
    {
        Vector3 Clipped = ClipToPlane(vector, planes[Index]);
        if (IsClearOfPlanes(Clipped, planes, planeCount, Tolerance))
        {
            return Clipped;
        }
    }
    // Along the crease of two.
    for (size_t First = 0U; First < planeCount; First++)
    {
        for (size_t Second = First + 1U; Second < planeCount; Second++)
        {
            Vector3 Crease = Vector3CrossProduct(planes[First], planes[Second]);
            float LengthSquared = Vector3DotProduct(Crease, Crease);
            if (LengthSquared < LENGTH_SQUARED_EPSILON)
            {
                continue;
            }
            Crease = Vector3Scale(Crease, 1.0f / sqrtf(LengthSquared));
            Vector3 Along = Vector3Scale(Crease, Vector3DotProduct(vector, Crease));
            if (IsClearOfPlanes(Along, planes, planeCount, Tolerance))
            {
                return Along;
            }
        }
    }
    // A corner of three or more.
    return (Vector3){ 0.0f, 0.0f, 0.0f };
}

Vector3 CharacterMotion_ProjectOnGround(Vector3 move, Vector3 normal)
{
    if (normal.y < 1e-4f)
    {
        return move;
    }
    float Rise = -((normal.x * move.x) + (normal.z * move.z)) / normal.y;
    return (Vector3){ move.x, Rise, move.z };
}

CharacterMove CharacterMotion_MoveAndSlide(const CharacterBody* body, Vector3 feet, Vector3 displacement,
    Vector3* velocity, const CharacterSupport* ground)
{
    CharacterMove Move = { .Feet = feet, .HasStepped = false, .StepSupport = CharacterSupport_CreateNone(),
        .HasHit = false };
    Vector3 Planes[CHARACTER_MAX_PLANES];
    size_t PlaneCount = 0U;
    bool HasTriedStep = false;
    Vector3 Remaining = displacement;

    for (uint32_t Iteration = 0U; Iteration < CHARACTER_MAX_SLIDE_ITERATIONS; Iteration++)
    {
        float Length = Vector3Length(Remaining);
        if (Length < CHARACTER_MIN_MOVE)
        {
            break;
        }
        // The sweep looks past the move as far as GetAdvance can back off: whatever the move would take within the rest
        // gap of is hit, a wall the gap away (where the last move left it) included, so the velocity loses its part
        // into it instead of creeping into the gap. A hit further on does not stop the move.
        WorldCastHit Hit;
        Vector3 Direction = Vector3Scale(Remaining, 1.0f / Length);
        Vector3 Target = Vector3Add(Move.Feet, Remaining);
        Vector3 Probe = Vector3Add(Target, Vector3Scale(Direction, CHARACTER_REST_GAP / CHARACTER_MIN_APPROACH));
        if (!CharacterMotion_Sweep(body, Move.Feet, Probe, &body->MoveFilter, &Hit))
        {
            Move.Feet = Target;
            break;
        }
        if (Hit.IsStartPenetrating)
        {
            // Within the skin of something already (a spawn, something moved into it): out first, then the rest.
            Move.HasHit = true;
            Move.Feet = Vector3Add(Move.Feet, Vector3Scale(Hit.Normal, Hit.PenetrationDepth + CHARACTER_REST_GAP));
            continue;
        }

        Vector3 Normal = Hit.Normal;
        float Advance = GetAdvance(Direction, Normal, Hit.Distance);
        if (Advance >= Length)
        {
            Move.Feet = Target;
            break;
        }
        Move.HasHit = true;
        Move.Feet = Vector3Add(Move.Feet, Vector3Scale(Direction, Advance));
        Remaining = Vector3Scale(Direction, Length - Advance);
        bool IsSteep = (Normal.y < body->WalkableNormalY)
            || ((ground != NULL) && IsEdgeAboveStep(body, ground, &Hit));

        if ((ground != NULL) && IsSteep)
        {
            // A wall, a riser or steep ground in a walker's way: a step first (once a move), then a wall.
            if (!HasTriedStep && (Normal.y > -CHARACTER_EDGE_MIN_NORMAL_Y))
            {
                HasTriedStep = true;
                Vector3 Stepped;
                CharacterSupport StepSupport;
                if (TryStepUp(body, ground, Move.Feet, Direction, Length - Advance,
                    Math_MaxFloat(0.0f, Hit.Distance - Advance), &Stepped, &StepSupport))
                {
                    Move.Feet = Stepped;
                    Move.HasStepped = true;
                    Move.StepSupport = StepSupport;
                    break;
                }
            }
            // Steep ground does not lift a walker: it holds it like an upright wall.
            Vector3 Flat = { Normal.x, 0.0f, Normal.z };
            float FlatLength = Vector3Length(Flat);
            if ((Normal.y > 0.0f) && (FlatLength > 1e-4f))
            {
                Normal = Vector3Scale(Flat, 1.0f / FlatLength);
            }
        }

        Move.Feet = KeepRestGap(Move.Feet, Direction, Advance, &Hit, Normal);
        if (PlaneCount < CHARACTER_MAX_PLANES)
        {
            Planes[PlaneCount] = Normal;
            PlaneCount++;
        }
        Remaining = CharacterMotion_ClipToPlanes(Remaining, Planes, PlaneCount);
        *velocity = CharacterMotion_ClipToPlanes(*velocity, Planes, PlaneCount);
    }
    return Move;
}

CharacterGround CharacterMotion_FollowGround(const CharacterBody* body, Vector3 feet, float distance,
    const CharacterSupport* ground, Vector3* outFeet, CharacterSupport* outSupport)
{
    Vector3 Position = feet;
    Vector3 Remaining = { 0.0f, -distance, 0.0f };
    bool IsHidden = false;
    bool IsBlockedByStep = false;
    for (uint32_t Iteration = 0U; Iteration < CHARACTER_MAX_GROUND_ITERATIONS; Iteration++)
    {
        float Length = Vector3Length(Remaining);
        if (Length < CHARACTER_MIN_MOVE)
        {
            break;
        }
        WorldCastHit Hit;
        if (!SweepForGround(body, Position, Vector3Add(Position, Remaining), &Hit, &IsHidden))
        {
            break;
        }
        if (Hit.IsStartPenetrating)
        {
            // Within the skin of ground it sank into (out upward) or of something that does not give way: out first.
            Position = Vector3Add(Position, Vector3Scale(Hit.Normal, Hit.PenetrationDepth + CHARACTER_REST_GAP));
            continue;
        }
        Vector3 Direction = Vector3Scale(Remaining, 1.0f / Length);
        float Advance = GetAdvance(Direction, Hit.Normal, Hit.Distance);
        Position = Vector3Add(Position, Vector3Scale(Direction, Advance));
        // A walker's ground holds it only within its step height above the ground it walked on: a taller step's edge
        // under its round bottom is a rim like any other (otherwise it would walk up the edge as up a slope).
        CharacterSupport Support = CharacterMotion_EvaluateSupport(body, &Hit);
        bool IsStepEdge = Support.IsSupported && (ground != NULL) && IsEdgeAboveStep(body, ground, &Hit);
        if (Support.IsSupported && !IsStepEdge)
        {
            // Resting the gap above what holds it (backing out of it, when it was already closer); a rim it slides off
            // gets no such room.
            *outFeet = KeepRestGap(Position, Direction, Advance, &Hit, Hit.Normal);
            *outSupport = Support;
            return CharacterGround_Supported;
        }
        IsBlockedByStep = IsBlockedByStep || IsStepEdge;
        // A rim or steep ground: slide the rest down along it, looking for support further down.
        Remaining = Vector3Scale(Direction, Length - Advance);
        Remaining = ClipToPlane(Remaining, Hit.Normal);
        if (Remaining.y > -CHARACTER_MIN_MOVE)
        {
            break;
        }
    }
    // An edge above the step leaning on the round bottom (a curb a walker presses against) can stop the look before the
    // ground it rests on: a near-flat edge's rim leaves a short look almost no way down. The ground it rests on (resting
    // distance from the bottom cap's centre, along the ground's normal) still holds it, where it is.
    if (IsBlockedByStep && FindRestingGround(body, feet, ground, outSupport))
    {
        *outFeet = feet;
        return CharacterGround_Supported;
    }
    return IsHidden ? CharacterGround_Unsure : CharacterGround_Unsupported;
}

bool CharacterMotion_DoesFit(const CharacterBody* body, Vector3 feet, float height, GenericBuffer* scratchIds)
{
    // The capsule a skin narrower, its bottom two skins above the feet: resting on the ground (a skin below the feet)
    // does not count, anything else it would touch does.
    float Radius = body->Radius - CHARACTER_SKIN;
    float TestHeight = height - (2.0f * CHARACTER_SKIN);
    if ((Radius <= 0.0f) || (TestHeight < (2.0f * Radius)))
    {
        return true;
    }
    Vector3 Center = { feet.x, feet.y + (0.5f * height) + CHARACTER_SKIN, feet.z };
    GenericBuffer_Clear(scratchIds);
    return WorldCollision_OverlapCapsule(body->Collision, Center, Radius, TestHeight, &body->GroundFilter,
        scratchIds) == 0U;
}
