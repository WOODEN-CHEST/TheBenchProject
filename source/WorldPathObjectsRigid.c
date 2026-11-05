// The path objects system's rigid pieces (WorldPathObjectsInternal.h): where a piece's points lie, on the ground, and the copies
// and the collision they come to.

#include <math.h>
#include "WorldPathObjectsInternal.h"
#include "CollisionShape.h"
#include "GameMath.h"
#include "WorldModelInstancesComponent.h"
#include "WorldTransform.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** The salts of a piece's random choices (PathObjectLayout_GetRandom). */
#define SALT_FLIP 2U
#define SALT_JITTER 3U
/** The jitter channel after a copy's five that a prefab's piece picks its enum options from. */
#define PREFAB_PICK_CHANNEL 5U
/** A value in [0, 1) as 32 bits. */
#define PICK_RANGE 4294967296.0f


// Static functions: the ground.
/* A point of the object's space in the world. */
static Vector3Double ToWorld(const PathObjectRebuild* rebuild, Vector3Double local)
{
    return Matrix4Double_TransformPoint(&rebuild->ToWorld, local);
}

/* A point in the world in the object's space (the translation off first: exact far out). */
static Vector3Double ToLocal(const PathObjectRebuild* rebuild, Vector3Double world)
{
    Vector3Double Offset = Vector3Double_Subtract(world, Matrix4Double_GetTranslation(&rebuild->ToWorld));
    return Matrix4Double_TransformDirection(&rebuild->ToLocalAxes, Offset);
}

/* Whether a point of a section snaps, and where it goes: @p inOutPosition moves onto the ground when it finds one. */
static void SnapPoint(PathObjectRebuild* rebuild, uint32_t section, Vector3Double* inOutPosition)
{
    WorldPathObjects* System = rebuild->System;
    if ((System->_collision == NULL)
        || !PathCurveSection_ResolveSnapsToTerrain(PathCurve_GetSection(rebuild->Curve, section),
            rebuild->Settings.SnapsToTerrain))
    {
        return;
    }
    // It counts as snapped from here on, whatever the ray finds: an object of no size looks again when it has one.
    rebuild->State->HasSnapped = true;
    Vector3Double InWorld = ToWorld(rebuild, *inOutPosition);
    rebuild->State->GroundArea = BoundingBoxDouble_UnionPoint(rebuild->State->GroundArea, InWorld);
    if (!rebuild->HasInverse)
    {
        return;
    }
    if (rebuild->GroundWorkLeft < rebuild->GroundCost)
    {
        if (!rebuild->State->HasWarnedGround)
        {
            rebuild->State->HasWarnedGround = true;
            WorldPathObjectsInternal_Warn(System, rebuild->Owner,
                "looks for the ground more than it may, over its own colliders: the rest stay off it.");
        }
        return;
    }
    rebuild->GroundWorkLeft -= rebuild->GroundCost;
    PathGroundHit Hit;
    if (PathGround_Project(System->_collision, InWorld, &rebuild->Ground, &Hit))
    {
        *inOutPosition = ToLocal(rebuild, Hit.Position);
    }
}

/* The up a piece stands by at a point: the object's +Y as near as the frame's right lets it. The curve's own up is square to its
 * tangent, so on a climbing path it leans along the path: a post would lean with it, and a shear would be a tilt. Only a roll
 * leans this one. Where the right lies along +Y it says nothing, and the curve's stays. */
static Vector3 GetStandingUp(const PathCurvePoint* point)
{
    Vector3 Standing = { -point->Right.y * point->Right.x, 1.0f - (point->Right.y * point->Right.y),
        -point->Right.y * point->Right.z };
    return (Vector3LengthSqr(Standing) > 1.0e-6f) ? Vector3Normalize(Standing) : point->Up;
}


// Static functions: corners.
/* A corner between two sections, into the list. */
static void AddCorner(PathObjectRebuild* rebuild, double distance, uint32_t arrivingSection, uint32_t leavingSection)
{
    PathCurvePoint Arriving;
    PathCurvePoint Leaving;
    if (!PathCurve_GetSectionPoint(rebuild->Curve, arrivingSection, PathCurve_GetSection(rebuild->Curve,
        arrivingSection)->Length, &Arriving)
        || !PathCurve_GetSectionPoint(rebuild->Curve, leavingSection, 0.0, &Leaving))
    {
        return;
    }
    Vector3 Sum = Vector3Add(Arriving.Right, Leaving.Right);
    Vector3 Mitre = Arriving.Right; // doubling straight back has no middle
    if (Vector3LengthSqr(Sum) > 1.0e-8f)
    {
        Vector3 Middle = Vector3Normalize(Sum);
        float Cosine = Vector3DotProduct(Middle, Arriving.Right);
        float Stretch = (Cosine > (float)(1.0 / WORLD_PATH_OBJECTS_MAX_CORNER_STRETCH)) ? (1.0f / Cosine)
            : (float)WORLD_PATH_OBJECTS_MAX_CORNER_STRETCH;
        Mitre = Vector3Scale(Middle, Stretch);
    }
    PathObjectSpanCorner Corner = { .Distance = distance, .Mitre = Mitre, .Arriving = Arriving.Tangent,
        .Leaving = Leaving.Tangent,
        .Width = Arriving.Width };
    GenericBuffer_AddLast(&rebuild->System->_corners, &Corner);
}

/* The span's corners, listed when the span changes: its two ends where a section of its run meets it, and between its sections.
 */
static const PathObjectSpanCorner* GetCorners(PathObjectRebuild* rebuild, const PathObjectSpan* span, size_t* outCount)
{
    WorldPathObjects* System = rebuild->System;
    if (System->_cornersSpan != span)
    {
        System->_cornersSpan = span;
        GenericBuffer_Clear(&System->_corners);
        if (span->HasSectionBefore)
        {
            AddCorner(rebuild, 0.0, span->SectionBefore, span->FirstSection);
        }
        uint32_t SectionCount = PathCurve_GetSectionCount(rebuild->Curve);
        uint32_t Section = span->FirstSection;
        uint32_t LastHolding = Section;
        double Offset = PathCurve_GetSection(rebuild->Curve, Section)->Length;
        for (uint32_t Step = 0U; (Step < SectionCount) && (Section != span->LastSection); Step++)
        {
            Section = (Section + 1U) % SectionCount;
            double Length = PathCurve_GetSection(rebuild->Curve, Section)->Length;
            if (Length > 0.0)
            {
                AddCorner(rebuild, Offset, LastHolding, Section);
                LastHolding = Section;
                Offset += Length;
            }
        }
        if (span->HasSectionAfter)
        {
            AddCorner(rebuild, span->Length, span->LastSection, span->SectionAfter);
        }
    }
    *outCount = System->_corners._count;
    return (const PathObjectSpanCorner*)System->_corners._data;
}

/* Which corner comes first at or after a distance: its index, or the count. */
static size_t FindCornerAtOrAfter(const PathObjectSpanCorner* corners, size_t count, double distance)
{
    size_t Low = 0U;
    size_t High = count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (corners[Middle].Distance < distance)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    return Low;
}


// Functions: corners.
const PathObjectSpanCorner* WorldPathObjectsRigid_FindCorner(PathObjectRebuild* rebuild, const PathObjectSpan* span,
    double distance)
{
    size_t Count = 0U;
    const PathObjectSpanCorner* Corners = GetCorners(rebuild, span, &Count);
    size_t Index = FindCornerAtOrAfter(Corners, Count, distance - WORLD_PATH_OBJECTS_CORNER_TOLERANCE);
    return ((Index < Count)
        && (Corners[Index].Distance <= (distance + WORLD_PATH_OBJECTS_CORNER_TOLERANCE))) ? &Corners[Index] : NULL;
}

bool WorldPathObjectsRigid_IsPastInnerCorner(PathObjectRebuild* rebuild, const PathObjectSpan* span, double distance, double side)
{
    size_t Count = 0U;
    const PathObjectSpanCorner* Corners = GetCorners(rebuild, span, &Count);
    size_t After = FindCornerAtOrAfter(Corners, Count, distance - WORLD_PATH_OBJECTS_CORNER_TOLERANCE);
    if ((After < Count) && (Corners[After].Distance <= (distance + WORLD_PATH_OBJECTS_CORNER_TOLERANCE)))
    {
        return false; // at a corner itself: where the two legs meet
    }
    if (After < Count)
    {
        // Coming to a corner: on its inside, the side's line ends this far before it.
        const PathObjectSpanCorner* Corner = &Corners[After];
        double Along = side * (double)Corner->Width * (double)Vector3DotProduct(Corner->Mitre, Corner->Arriving);
        if ((Along < 0.0) && (distance > (Corner->Distance + Along)))
        {
            return true;
        }
    }
    if (After > 0U)
    {
        // Leaving one: on its inside, the side's line starts this far after it.
        const PathObjectSpanCorner* Corner = &Corners[After - 1U];
        double Along = side * (double)Corner->Width * (double)Vector3DotProduct(Corner->Mitre, Corner->Leaving);
        if ((Along > 0.0) && (distance < (Corner->Distance + Along)))
        {
            return true;
        }
    }
    return false;
}


// Functions: a piece's points.
/* A point along a span, off to its side and on the ground, with its frame there. */
bool WorldPathObjectsRigid_MakeEnd(PathObjectRebuild* rebuild, const PathObjectSpan* span, double distance, float sideSign,
    float sideOffset,
    PathObjectPieceEnd* outEnd)
{
    PathCurvePoint Point;
    if (!PathObjectLayout_GetSpanPoint(rebuild->Curve, span, distance, &Point))
    {
        return false;
    }
    double Side = (double)sideSign * (double)sideOffset * (double)Point.Width;
    Vector3 Across = Point.Right;
    if (Side != 0.0)
    {
        // At a corner a side turns by its mitre, so what stands on either side of it meets.
        const PathObjectSpanCorner* Corner = WorldPathObjectsRigid_FindCorner(rebuild, span, distance);
        Across = (Corner != NULL) ? Corner->Mitre : Across;
    }
    Vector3Double Position = Point.Position;
    Position.X += Side * (double)Across.x;
    Position.Y += Side * (double)Across.y;
    Position.Z += Side * (double)Across.z;
    SnapPoint(rebuild, Point.Section, &Position);
    *outEnd = (PathObjectPieceEnd){ .Position = Position, .Tangent = Point.Tangent, .Up = GetStandingUp(&Point),
        .Right = Point.Right, .Scale = Point.Scale, .Width = Point.Width };
    return true;
}


// Functions: a piece's choices.
bool WorldPathObjectsRigid_IsPieceFlipped(const PathObjectRebuild* rebuild, const PathObjectPiece* piece,
    const PathObjectSpan* span,
    const PathObjectSlotRecord* slot)
{
    switch (slot->Flip)
    {
        case PathObjectFlip_None: return false;
        case PathObjectFlip_Always: return true;
        case PathObjectFlip_Alternating: return (piece->Index % 2U) == 1U;
        case PathObjectFlip_Random:
            return PathObjectLayout_GetRandom(rebuild->Settings.Seed, piece->Slot, span->FirstSection, piece->Index,
                SALT_FLIP) < 0.5f;
    }
    return false;
}

/* The sides a piece stands on: their signs, +1 right and -1 left, 0 on the path. */
uint32_t WorldPathObjectsRigid_GetPieceSides(const PathObjectPiece* piece, const PathObjectSlotRecord* slot,
    float outSigns[WORLD_PATH_OBJECTS_MAX_PIECE_SIDES])
{
    switch (slot->Side)
    {
        case PathObjectSide_OnPath: outSigns[0] = 0.0f; return 1U;
        case PathObjectSide_Left: outSigns[0] = -1.0f; return 1U;
        case PathObjectSide_Right: outSigns[0] = 1.0f; return 1U;
        case PathObjectSide_Both: outSigns[0] = 1.0f; outSigns[1] = -1.0f; return 2U;
        case PathObjectSide_Alternating: outSigns[0] = ((piece->Index % 2U) == 0U) ? 1.0f : -1.0f; return 1U;
    }
    outSigns[0] = 0.0f;
    return 1U;
}

float WorldPathObjectsRigid_GetJitter(const PathObjectRebuild* rebuild, const PathObjectPiece* piece, const PathObjectSpan* span,
    uint32_t side,
    uint32_t channel)
{
    return PathObjectLayout_GetRandom(rebuild->Settings.Seed, piece->Slot, span->FirstSection, piece->Index,
        SALT_JITTER + (side * 16U) + channel);
}


// Static functions: placed collision.
/* Whether one more placed shape is allowed: by its child's limit and the world's; the first refusal is warned about. */
static bool CanAddShape(PathObjectRebuild* rebuild, const PathObjectCopyList* list)
{
    if ((list->Shapes._count < WORLD_PATH_OBJECTS_MAX_PLACED_SHAPES) && (rebuild->ShapeBudget > 0U))
    {
        return true;
    }
    if (!rebuild->State->HasWarnedShapes)
    {
        rebuild->State->HasWarnedShapes = true;
        WorldPathObjectsInternal_Warn(rebuild->System, rebuild->Owner,
            "makes more collision shapes than it may hold: the rest do not collide.");
    }
    return false;
}


// Functions: placed collision.
/* Appends the collision a copy has by its slot: the world model's set under the copy's matrix, or one box around it. */
void WorldPathObjectsRigid_AddCopyCollision(PathObjectRebuild* rebuild, PathObjectSlot slot, uint32_t modelIndex,
    const PropertyInstanceEntry* copy,
    const PathObjectSlotRecord* settings)
{
    const PathObjectSlotModel* ModelBuild = &rebuild->Slots[slot].Models[modelIndex];
    const PathObjectSlotRecord* Settings = settings;
    if (Settings->Collision == PathObjectCollision_None)
    {
        return;
    }
    PathObjectCopyList* List = WorldPathObjectsInternal_GetCopyList(rebuild->System, slot, modelIndex);
    if (!CanAddShape(rebuild, List))
    {
        return;
    }
    Matrix4Double Placement = WorldTransform_ComposeSheared(copy->Position, copy->Rotation, copy->Scale, copy->Shear);
    if (Settings->Collision == PathObjectCollision_Boxes)
    {
        Vector3 Min = ModelBuild->Measure.Min;
        Vector3 Max = ModelBuild->Measure.Max;
        if (Max.x < Min.x)
        {
            return;
        }
        CollisionShape Box = CollisionShape_Create(CollisionShapeType_Box);
        float BodyCenter = PathObjectMeasure_GetBodyCenter(&ModelBuild->Measure, Settings);
        Vector3 Size = Settings->OverridesBoxSize ? Settings->BoxSize : (Vector3){ 0.0f, Max.y - Min.y, Max.z - Min.z };
        float BoxLength = PathObjectMeasure_GetLength(&ModelBuild->Measure, Settings);
        Box.HalfExtents = (Vector3){ fmaxf(0.5f * (BoxLength + Size.x), 0.005f), fmaxf(0.5f * Size.y, 0.005f),
            fmaxf(0.5f * Size.z, 0.005f) };
        Box.Center = (Vector3){ BodyCenter, 0.5f * (Min.y + Max.y), 0.5f * (Min.z + Max.z) };
        if (Settings->OverridesBoxOffset)
        {
            Box.Center = Vector3Add(Box.Center, Settings->BoxOffset);
        }
        GenericBuffer_AddLast(&List->Shapes, &Box);
        GenericBuffer_AddLast(&List->Placements, &Placement);
        rebuild->ShapeBudget--;
        return;
    }
    const WorldModelCollision* Collision = WorldModel_GetCollision(ModelBuild->Asset);
    if (Collision == NULL)
    {
        return;
    }
    uint32_t SetIndex = WorldModelCollision_GetDefaultSetIndex(Collision);
    if (Settings->OverridesCollisionSet && !WorldModelCollision_TryFindSet(Collision, Settings->CollisionSet, &SetIndex))
    {
        return; // warned once per rebuild is too often: a set a model lacks simply does not collide
    }
    const CollisionSet* Set = WorldModelCollision_GetSet(Collision, SetIndex);
    for (uint32_t Shape = 0U; (Set != NULL) && (Shape < CollisionSet_GetShapeCount(Set)); Shape++)
    {
        if (!CanAddShape(rebuild, List))
        {
            return;
        }
        GenericBuffer_AddLast(&List->Shapes, (void*)CollisionSet_GetShape(Set, Shape));
        GenericBuffer_AddLast(&List->Placements, &Placement);
        rebuild->ShapeBudget--;
    }
}


// Functions: rigid pieces.
/* Fills the ends of a rigid piece: both on the curve, or a joint's around its node along the way it arrives. */
bool WorldPathObjectsRigid_MakePieceEnds(PathObjectRebuild* rebuild, const PathObjectPiece* piece, const PathObjectSpan* span,
    const PathObjectSlotBuild* slot,
    float sideSign, PathObjectPieceEnd* outStart, PathObjectPieceEnd* outEnd)
{
    float SideOffset = slot->Settings.SideOffset;
    if (piece->IsJoint)
    {
        PathObjectPieceEnd Node;
        if (!WorldPathObjectsRigid_MakeEnd(rebuild, span, piece->End, sideSign, SideOffset, &Node))
        {
            return false;
        }
        double Half = 0.5 * (double)slot->Models[piece->Model].Length * (double)Node.Scale;
        Vector3Double Tangent = GameMath_FromVector3(Node.Tangent);
        *outStart = Node;
        *outEnd = Node;
        outStart->Position = Vector3Double_AddScaled(Node.Position, Tangent, -Half);
        outEnd->Position = Vector3Double_AddScaled(Node.Position, Tangent, Half);
        return true;
    }
    return WorldPathObjectsRigid_MakeEnd(rebuild, span, piece->Start, sideSign, SideOffset, outStart)
        && WorldPathObjectsRigid_MakeEnd(rebuild, span, piece->End, sideSign, SideOffset, outEnd);
}

/* A rigid copy of a piece on one of its sides, into its list. */
void WorldPathObjectsRigid_AddCopy(PathObjectRebuild* rebuild, const PathObjectPiece* piece, const PathObjectSpan* span,
    const PathObjectSlotBuild* slot,
    uint32_t sideIndex, float sideSign)
{
    const PathObjectSlotModel* ModelBuild = &slot->Models[piece->Model];
    PathObjectCopyInput Input;
    Memory_Zero(&Input, sizeof(Input));
    if (!WorldPathObjectsRigid_MakePieceEnds(rebuild, piece, span, slot, sideSign, &Input.Start, &Input.End))
    {
        return;
    }
    Input.Measure = ModelBuild->Measure;
    // An object has no shear, as a copy has: a prefab's piece stands level instead.
    PathObjectSlotRecord PrefabSettings = slot->Settings;
    PrefabSettings.Slope = (slot->Settings.Slope == PathObjectSlope_Shear) ? PathObjectSlope_Step : slot->Settings.Slope;
    Input.Slot = (ModelBuild->Prefab != NULL) ? &PrefabSettings : &slot->Settings;
    Input.IsTurned = WorldPathObjectsRigid_IsPieceFlipped(rebuild, piece, span, &slot->Settings) != (sideSign < 0.0f);
    Input.KeepsLength = piece->IsJoint || ((Input.Slot->Slope == PathObjectSlope_Step) && !slot->Settings.Stretches
        && !piece->IsResized);
    for (uint32_t Channel = 0U; Channel < 5U; Channel++)
    {
        Input.Jitter[Channel] = WorldPathObjectsRigid_GetJitter(rebuild, piece, span, sideIndex, Channel);
    }
    Input.Tint = ModelBuild->Record->Tint;
    PropertyInstanceEntry Copy;
    if (!PathObjectPlacement_MakeCopy(&Input, &Copy))
    {
        return;
    }
    if (ModelBuild->Prefab != NULL)
    {
        // An object's scale stays even: stretched or widened, it would shear whatever is turned inside it.
        Copy.Scale = (Vector3){ Copy.Scale.y, Copy.Scale.y, Copy.Scale.y };
    }
    // A copy's list holds so many, and its rows refuse a whole list for one copy too far out: such a copy is left out alone.
    PathObjectCopyList* List = WorldPathObjectsInternal_GetCopyList(rebuild->System, piece->Slot, piece->Model);
    if ((List->Copies._count >= WORLD_MODEL_INSTANCES_MAX_COUNT) || (fabs(Copy.Position.X) > WORLD_POSITION_LIMIT)
        || (fabs(Copy.Position.Y) > WORLD_POSITION_LIMIT) || (fabs(Copy.Position.Z) > WORLD_POSITION_LIMIT))
    {
        if (!rebuild->State->HasWarnedCopies)
        {
            rebuild->State->HasWarnedCopies = true;
            WorldPathObjectsInternal_Warn(rebuild->System, rebuild->Owner,
                "makes a copy its list cannot hold (too many, or too far out): it is left out.");
        }
        return;
    }
    if (!GenericBuffer_AddLast(&List->Copies, &Copy))
    {
        return;
    }
    if (ModelBuild->Prefab != NULL)
    {
        // What it picks its enums from, for this copy: its objects collide for themselves.
        uint32_t Pick = (uint32_t)(WorldPathObjectsRigid_GetJitter(rebuild, piece, span, sideIndex, PREFAB_PICK_CHANNEL) * PICK_RANGE);
        GenericBuffer_AddLast(&List->Picks, &Pick);
        return;
    }
    WorldPathObjectsRigid_AddCopyCollision(rebuild, piece->Slot, piece->Model, &Copy, &slot->Settings);
}
