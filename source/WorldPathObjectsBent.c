// The path objects system's bent pieces (WorldPathObjectsInternal.h): a piece's slices, the stretches of about a part's length
// they are bent into, and the generated models those come to.

#include <math.h>
#include "WorldPathObjectsInternal.h"
#include "GameMath.h"
#include "GameModel.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** A cut plane that lies past everything. */
#define NO_CUT 1.0e30f
/** How far to either side of a piece's end the ground's way is read, in metres: the same for the two pieces that share the end.
 */
#define GROUND_STEP 0.25
/** A piece's slice distances are thinned once they hold this many, so a hostile spacing costs no memory. */
#define MAX_HELD_DISTANCES (2U * PATH_OBJECT_MAX_SLICES)
/** The part a stretch at a hostile distance can go to, at most. */
#define MAX_PART_INDEX 4.0e9


// Static functions: parts.
/* The part of a piece's stretch, by its distance along the path. */
static uint32_t GetPartAt(double pathDistance)
{
    return (uint32_t)fmin(fmax(0.0, floor(pathDistance / WORLD_PATH_OBJECTS_PART_LENGTH)), MAX_PART_INDEX);
}

/* The stretch a slot's and model's bent pieces at a part are built in, started at @p center when it is new. */
static PathObjectBentPart* FindOrMakePart(PathObjectRebuild* rebuild, PathObjectSlot slot, uint32_t modelIndex, uint32_t part,
    Vector3Double center)
{
    WorldPathObjects* System = rebuild->System;
    PathObjectSlotModel* ModelBuild = &rebuild->Slots[slot].Models[modelIndex];
    if (ModelBuild->HasParts && (part <= ModelBuild->LastPart))
    {
        // An earlier one: parts come in rising order along the path, so this is rare and near the end (a piece's other side).
        PathObjectBentPart* Parts = (PathObjectBentPart*)System->_parts._data;
        for (size_t Index = System->_parts._count; Index > 0U; Index--)
        {
            PathObjectBentPart* Held = &Parts[Index - 1U];
            if ((Held->Slot == slot) && (Held->Model == modelIndex) && (Held->Part == part))
            {
                return Held;
            }
        }
    }
    PathObjectBentPart Fresh;
    Memory_Zero(&Fresh, sizeof(Fresh));
    Fresh.Slot = slot;
    Fresh.Model = modelIndex;
    Fresh.Part = part;
    Fresh.Center = center;
    const WorldModel* Asset = rebuild->Slots[slot].Models[modelIndex].Asset;
    for (size_t Lod = 0U; Lod < WorldModel_GetLodCount(Asset); Lod++)
    {
        Model Source = GameModel_GetRaylibModel(WorldModel_GetLodModel(Asset, Lod));
        uint32_t MeshCount = (Source.meshCount > 0) ? (uint32_t)Source.meshCount : 0U;
        Fresh.MeshCounts[Lod] = MeshCount;
        if (MeshCount > 0U)
        {
            Fresh.Meshes[Lod] = Memory_Allocate((size_t)MeshCount * sizeof(PathObjectBentMesh));
            for (uint32_t MeshIndex = 0U; MeshIndex < MeshCount; MeshIndex++)
            {
                PathObjectBentMesh_Construct(&Fresh.Meshes[Lod][MeshIndex]);
            }
        }
    }
    if (!GenericBuffer_AddLast(&System->_parts, &Fresh))
    {
        return NULL;
    }
    ModelBuild->HasParts = true;
    ModelBuild->LastPart = ModelBuild->LastPart > part ? ModelBuild->LastPart : part;
    return (PathObjectBentPart*)GenericBuffer_GetPointerToLast(&System->_parts);
}


// Static functions: slices.
/* Takes a section's samples at a spacing for the slot: kept, for many pieces walk along one section. A tinier spacing than a
 * section can hold is coarsened to what it can. What the samples cost comes off the rebuild's work. */
static const double* GetSectionSamples(PathObjectRebuild* rebuild, PathObjectSlot slot, uint32_t section, float spacing,
    size_t* outCount)
{
    PathObjectSampleCache* Cache = &rebuild->System->_sampleCaches[slot];
    if (Cache->Section != section)
    {
        GenericBuffer_Clear(&Cache->Samples);
        double Length = PathCurve_GetSection(rebuild->Curve, section)->Length;
        double Coarsest = Length / (double)WORLD_PATH_OBJECTS_MAX_SECTION_SAMPLES;
        PathSampleOptions Options = PathSampleOptions_Create(fmax((double)spacing, Coarsest));
        Error Result = PathCurve_WriteSectionSamples(rebuild->Curve, section, &Options, &Cache->Samples);
        if (Result.Code != ErrorCode_Success)
        {
            GenericBuffer_Clear(&Cache->Samples); // the piece then has its ends alone
        }
        Error_Deconstruct(&Result);
        Cache->Section = section;
        size_t Cost = Cache->Samples._count;
        rebuild->BentWorkLeft -= (Cost < rebuild->BentWorkLeft) ? Cost : rebuild->BentWorkLeft;
    }
    *outCount = Cache->Samples._count;
    return (const double*)Cache->Samples._data;
}

/* Halves the distances held, keeping every other one, the piece's start among them. */
static void ThinDistances(GenericBuffer* distances)
{
    double* Values = (double*)distances->_data;
    size_t Kept = 0U;
    for (size_t Index = 0U; Index < distances->_count; Index += 2U)
    {
        Values[Kept++] = Values[Index];
    }
    distances->_count = Kept;
}

/* Appends the distances a bent piece is sliced at: its ends, and the span's samples between them, at most the slice spacing apart
 * and closer where the path bends. Every section of the span is sampled: a corner inside it comes out as a sample. However many
 * there are, it holds at most MAX_HELD_DISTANCES of them, thinned evenly. */
static bool WritePieceDistances(PathObjectRebuild* rebuild, const PathObjectSpan* span, const PathObjectPiece* piece,
    float spacing)
{
    WorldPathObjects* System = rebuild->System;
    GenericBuffer* Out = &System->_distances;
    GenericBuffer_Clear(Out);
    double Start = piece->Start;
    double End = piece->End;
    if (!GenericBuffer_AddLast(Out, &Start))
    {
        return false;
    }
    uint32_t SectionCount = PathCurve_GetSectionCount(rebuild->Curve);
    uint32_t Section = span->FirstSection;
    double Offset = 0.0;
    size_t Stride = 1U;
    size_t Seen = 0U;
    for (uint32_t Step = 0U; (Step < SectionCount) && (Offset < End); Step++)
    {
        const PathCurveSection* Held = PathCurve_GetSection(rebuild->Curve, Section);
        if ((Offset + Held->Length) > Start)
        {
            size_t SampleCount = 0U;
            const double* Values = GetSectionSamples(rebuild, piece->Slot, Section, spacing, &SampleCount);
            // The first sample past the piece's start: samples rise.
            size_t Low = 0U;
            size_t High = SampleCount;
            while (Low < High)
            {
                size_t Middle = Low + ((High - Low) / 2U);
                if ((Offset + Values[Middle]) <= (Start + 1.0e-6))
                {
                    Low = Middle + 1U;
                }
                else
                {
                    High = Middle;
                }
            }
            for (size_t Index = Low; Index < SampleCount; Index++)
            {
                double Here = Offset + Values[Index];
                if (Here >= (End - 1.0e-6))
                {
                    break;
                }
                double Last = *(const double*)GenericBuffer_GetPointerToLast(Out);
                if ((Here > (Last + 1.0e-6)) && ((Seen++ % Stride) == 0U))
                {
                    GenericBuffer_AddLast(Out, &Here);
                    if (Out->_count >= MAX_HELD_DISTANCES)
                    {
                        ThinDistances(Out);
                        Stride *= 2U;
                    }
                }
            }
        }
        Offset += Held->Length;
        if (Section == span->LastSection)
        {
            break;
        }
        Section = (Section + 1U) % SectionCount;
    }
    if (End > (*(const double*)GenericBuffer_GetPointerToLast(Out)))
    {
        GenericBuffer_AddLast(Out, &End);
    }
    return Out->_count >= 2U;
}

/* The way the ground runs at a distance along the piece's span, off to its side: by a step to either side of it. Two pieces that
 * share a slice read it alike there, so their frames agree and they meet. */
static Vector3 GetGroundTangent(PathObjectRebuild* rebuild, const PathObjectBentPieceInfo* info, double distance,
    Vector3 fallback)
{
    float SideOffset = info->Settings->SideOffset;
    PathObjectPieceEnd Before;
    PathObjectPieceEnd After;
    if (!WorldPathObjectsRigid_MakeEnd(rebuild, info->Span, distance - GROUND_STEP, info->SideSign, SideOffset, &Before)
        || !WorldPathObjectsRigid_MakeEnd(rebuild, info->Span, distance + GROUND_STEP, info->SideSign, SideOffset, &After))
    {
        return fallback;
    }
    Vector3Double Unit = Vector3Double_NormalizeOr(Vector3Double_Subtract(After.Position, Before.Position),
        GameMath_FromVector3(fallback));
    return (Vector3){ (float)Unit.X, (float)Unit.Y, (float)Unit.Z };
}

/* Appends one kept distance's slice to the places: the point on the ground, its frame by the slope's rule, where the mesh's X
 * lands. */
static bool AddBentPlace(PathObjectRebuild* rebuild, const PathObjectBentPieceInfo* info, const PathObjectBentEnd* ends,
    size_t index, size_t count)
{
    const PathObjectBentEnd* End = &ends[index];
    const PathObjectSlotRecord* Settings = info->Settings;
    // The tangent the points themselves make while they lie on the ground, or the curve's.
    Vector3 Tangent = End->End.Tangent;
    if (rebuild->State->HasSnapped && (count > 1U))
    {
        if ((index == 0U) || ((index + 1U) == count))
        {
            Tangent = GetGroundTangent(rebuild, info, End->Distance, Tangent);
        }
        else
        {
            Vector3Double Along = Vector3Double_Subtract(ends[index + 1U].End.Position, ends[index - 1U].End.Position);
            Vector3Double Unit = Vector3Double_NormalizeOr(Along, GameMath_FromVector3(Tangent));
            Tangent = (Vector3){ (float)Unit.X, (float)Unit.Y, (float)Unit.Z };
        }
    }
    Vector3 Right = End->End.Right;
    const PathObjectSpanCorner* Corner = WorldPathObjectsRigid_FindCorner(rebuild, info->Span, End->Distance);
    if (Corner != NULL)
    {
        // A mitre: the slice lies across the turn's middle, wide enough for the two sides to meet.
        Tangent = Vector3Normalize(Vector3Add(Corner->Arriving, Tangent));
        Right = Corner->Mitre;
    }
    else
    {
        Vector3 Crossed = Vector3CrossProduct(Tangent, End->End.Up);
        if (Vector3LengthSqr(Crossed) > 1.0e-10f)
        {
            Right = Vector3Normalize(Crossed);
        }
    }
    Vector3 Up = End->End.Up;
    if (Settings->Slope == PathObjectSlope_Tilt)
    {
        Vector3 Leaning = Vector3CrossProduct(Vector3Normalize(Right), Tangent);
        if (Vector3LengthSqr(Leaning) > 1.0e-10f)
        {
            Up = Vector3Normalize(Leaning);
        }
    }
    float Size = End->End.Scale * info->SizeJitter;
    float Share = (info->PieceLength > 0.0) ? (float)((ends[index].Distance - info->Piece->Start) / info->PieceLength) : 0.0f;
    float Sign = info->IsTurned ? -1.0f : 1.0f;
    Vector3Double Place = Vector3Double_AddScaled(End->End.Position, GameMath_FromVector3(Tangent),
        (double)(info->Offset.x * Sign));
    Place = Vector3Double_AddScaled(Place, GameMath_FromVector3(Up), (double)info->Offset.y);
    Place = Vector3Double_AddScaled(Place, GameMath_FromVector3(Right), (double)(info->Offset.z * Sign));
    PathObjectBentPlace Made =
    {
        .Position = Place,
        .MeshX = info->IsTurned ? (info->KeptStart + ((1.0f - Share) * info->KeptLength))
            : (info->KeptStart + (Share * info->KeptLength)),
        .AxisX = Vector3Scale(Tangent, info->Stretch * Sign),
        .AxisY = Vector3Scale(Up, Size),
        .AxisZ = Vector3Scale(Right, Size * End->End.Width * Sign)
    };
    return GenericBuffer_AddLast(&rebuild->System->_places, &Made);
}

/* The places of a bent piece's slices, along the path: every distance kept (a very long piece keeps every n-th slice, a limit
 * against a hostile spacing), each off to its side and on the ground. Past the inside of a corner a side has no place to lie:
 * such slices are left out, and the side's two legs meet at the corner's own slice. */
static bool MakeBentPlaces(PathObjectRebuild* rebuild, const PathObjectBentPieceInfo* info)
{
    WorldPathObjects* System = rebuild->System;
    GenericBuffer* Places = &System->_places;
    GenericBuffer* Ends = &System->_ends;
    GenericBuffer_Clear(Places);
    GenericBuffer_Clear(Ends);
    size_t Count = System->_distances._count;
    const double* Distances = (const double*)System->_distances._data;
    size_t Keep = (Count > PATH_OBJECT_MAX_SLICES) ? ((Count / PATH_OBJECT_MAX_SLICES) + 1U) : 1U;
    double Side = (double)info->SideSign * (double)info->Settings->SideOffset;
    for (size_t Index = 0U; ; )
    {
        bool IsEnd = (Index == 0U) || ((Index + 1U) >= Count);
        if (IsEnd || (Side == 0.0) || !WorldPathObjectsRigid_IsPastInnerCorner(rebuild, info->Span, Distances[Index], Side))
        {
            PathObjectBentEnd End = { .Distance = Distances[Index] };
            if (!WorldPathObjectsRigid_MakeEnd(rebuild, info->Span, Distances[Index], info->SideSign, info->Settings->SideOffset,
                &End.End)
                || !GenericBuffer_AddLast(Ends, &End))
            {
                return false;
            }
        }
        if ((Index + 1U) >= Count)
        {
            break;
        }
        size_t Next = Index + Keep;
        Index = (Next < Count) ? Next : (Count - 1U);
    }
    size_t Cost = Ends->_count;
    rebuild->BentWorkLeft -= (Cost < rebuild->BentWorkLeft) ? Cost : rebuild->BentWorkLeft;
    const PathObjectBentEnd* Held = (const PathObjectBentEnd*)Ends->_data;
    for (size_t Index = 0U; Index < Ends->_count; Index++)
    {
        if (!AddBentPlace(rebuild, info, Held, Index, Ends->_count))
        {
            return false;
        }
    }
    return Places->_count >= 2U;
}

/* Writes a stretch's slices of one LOD (every @p stride-th, the last always), relative to its part's origin and rising along the
 * mesh's X. Returns how many. */
static size_t WriteStretchSlices(const WorldPathObjects* self, const PathObjectBentPart* part, size_t first, size_t last,
    size_t stride,
    bool isTurned, PathObjectBendSlice* outSlices)
{
    const PathObjectBentPlace* Places = (const PathObjectBentPlace*)self->_places._data;
    size_t Written = 0U;
    for (size_t Index = first; ; Index += stride)
    {
        size_t Taken = (Index < last) ? Index : last;
        const PathObjectBentPlace* Place = &Places[Taken];
        Vector3Double Relative = Vector3Double_Subtract(Place->Position, part->Center);
        outSlices[Written++] = (PathObjectBendSlice){ .MeshX = Place->MeshX,
            .Position = { (float)Relative.X, (float)Relative.Y, (float)Relative.Z }, .AxisX = Place->AxisX,
            .AxisY = Place->AxisY, .AxisZ = Place->AxisZ };
        if (Taken == last)
        {
            break;
        }
    }
    if (isTurned)
    {
        for (size_t Low = 0U, High = Written - 1U; Low < High; Low++, High--)
        {
            PathObjectBendSlice Swap = outSlices[Low];
            outSlices[Low] = outSlices[High];
            outSlices[High] = Swap;
        }
    }
    return Written;
}

/* Bends one LOD's meshes along a stretch's slices into its part. */
static void BendLod(PathObjectRebuild* rebuild, PathObjectBentPart* part, const WorldModel* asset, size_t lod,
    const PathObjectBendSlice* slices,
    size_t sliceCount, float cutMin, float cutMax)
{
    Model Source = GameModel_GetRaylibModel(WorldModel_GetLodModel(asset, lod));
    for (int MeshIndex = 0; MeshIndex < Source.meshCount; MeshIndex++)
    {
        const Mesh* Held = &Source.meshes[MeshIndex];
        PathObjectSourceMesh Input =
        {
            .Positions = Held->vertices, .Normals = Held->normals, .Tangents = Held->tangents,
            .Texcoords = Held->texcoords, .Texcoords2 = Held->texcoords2, .Colors = Held->colors,
            .VertexCount = (Held->vertexCount > 0) ? (uint32_t)Held->vertexCount : 0U, .Indices = Held->indices,
            .TriangleCount = (Held->triangleCount > 0) ? (uint32_t)Held->triangleCount : 0U, .Transform = Source.transform
        };
        PathObjectBentMesh* Target = &part->Meshes[lod][MeshIndex];
        size_t BeforeCount = PathObjectBentMesh_GetVertexCount(Target);
        size_t Limit = (rebuild->VertexBudget < WORLD_PATH_OBJECTS_MAX_MESH_VERTICES)
            ? (BeforeCount + rebuild->VertexBudget) : WORLD_PATH_OBJECTS_MAX_MESH_VERTICES;
        Error Result = PathObjectBend_AppendMesh(Target, &Input, slices, sliceCount, cutMin, cutMax, Limit);
        size_t Added = PathObjectBentMesh_GetVertexCount(Target) - BeforeCount;
        rebuild->VertexBudget -= (Added < rebuild->VertexBudget) ? Added : rebuild->VertexBudget;
        if ((Result.Code != ErrorCode_Success) && !rebuild->State->HasWarnedBudget)
        {
            rebuild->State->HasWarnedBudget = true;
            WorldPathObjectsInternal_Warn(rebuild->System, rebuild->Owner, "bends more than it may: the rest is left out.");
        }
        Error_Deconstruct(&Result);
    }
}

/* Bends a piece's places into stretches of at most a part's length, each LOD over every 2^LOD-th slice of its stretch. */
static void BendPiece(PathObjectRebuild* rebuild, const PathObjectBentPieceInfo* info)
{
    WorldPathObjects* System = rebuild->System;
    const PathObjectPiece* Piece = info->Piece;
    const WorldModel* Asset = info->Model->Asset;
    const PathObjectBentPlace* Places = (const PathObjectBentPlace*)System->_places._data;
    size_t PlaceCount = System->_places._count;
    double PathStart = info->Span->Start + Piece->Start;
    // As many stretches as its length asks, but each holds a slab at least: a part with nothing to hold is no part.
    double Wanted = ceil(info->PieceLength / WORLD_PATH_OBJECTS_PART_LENGTH);
    uint32_t StretchCount = (uint32_t)fmax(1.0, fmin(Wanted, (double)(PlaceCount - 1U)));
    uint32_t FirstPart = GetPartAt(PathStart + (0.5 * fmin(info->PieceLength, WORLD_PATH_OBJECTS_PART_LENGTH)));
    PathObjectBendSlice* Slices = Memory_Allocate(PlaceCount * sizeof(PathObjectBendSlice));
    for (uint32_t Stretch = 0U; Stretch < StretchCount; Stretch++)
    {
        size_t First = (size_t)((double)(PlaceCount - 1U) * (double)Stretch / (double)StretchCount);
        size_t Last = (size_t)((double)(PlaceCount - 1U) * (double)(Stretch + 1U) / (double)StretchCount);
        PathObjectBentPart* Part = FindOrMakePart(rebuild, Piece->Slot, Piece->Model, FirstPart + Stretch,
            Places[First].Position);
        if (Part == NULL)
        {
            continue;
        }
        for (size_t Lod = 0U; Lod < WorldModel_GetLodCount(Asset); Lod++)
        {
            size_t Written = WriteStretchSlices(System, Part, First, Last, (size_t)1U << Lod, info->IsTurned, Slices);
            // A cut lies at the piece's own end along the path: at the other end of the mesh, on a turned piece.
            float CutAtMeshStart = info->IsTurned ? Piece->CutEnd : Piece->CutStart;
            float CutAtMeshEnd = info->IsTurned ? Piece->CutStart : Piece->CutEnd;
            float CutMin = (CutAtMeshStart > 0.0f) ? info->KeptStart : -NO_CUT;
            float CutMax = (CutAtMeshEnd > 0.0f) ? (info->KeptStart + info->KeptLength) : NO_CUT;
            // A stretch inside a long piece stops at its own slices; only the piece's own ends reach on past theirs.
            if ((info->IsTurned ? ((Stretch + 1U) < StretchCount) : (Stretch > 0U)))
            {
                CutMin = Slices[0].MeshX;
            }
            if ((info->IsTurned ? (Stretch > 0U) : ((Stretch + 1U) < StretchCount)))
            {
                CutMax = Slices[Written - 1U].MeshX;
            }
            BendLod(rebuild, Part, Asset, Lod, Slices, Written, CutMin, CutMax);
        }
    }
    Memory_Free(Slices);
}

/* The collision of a bent piece that does not come from its mesh: the model's set along its chord, or boxes along it, a stand-in
 * piece of a box's length under a rigid piece's rules. */
static void AddBentCollision(PathObjectRebuild* rebuild, const PathObjectBentPieceInfo* info, const PathObjectSlotBuild* slot)
{
    const PathObjectSlotRecord* Settings = info->Settings;
    const PathObjectPiece* Piece = info->Piece;
    if ((Settings->Collision != PathObjectCollision_Model) && (Settings->Collision != PathObjectCollision_Boxes))
    {
        return;
    }
    size_t Held = WorldPathObjectsInternal_GetCopyList(rebuild->System, Piece->Slot, Piece->Model)->Shapes._count;
    size_t Room = (Held < WORLD_PATH_OBJECTS_MAX_PLACED_SHAPES) ? (WORLD_PATH_OBJECTS_MAX_PLACED_SHAPES - Held) : 0U;
    Room = (Room < rebuild->ShapeBudget) ? Room : rebuild->ShapeBudget;
    bool IsBoxes = Settings->Collision == PathObjectCollision_Boxes;
    double Wanted = IsBoxes ? ceil(info->PieceLength / (double)Settings->BoxSpacing) : 1.0;
    // Past what it may hold, one more says so: a hostile spacing's other boxes are not even placed.
    uint32_t Count = (uint32_t)fmin(fmax(1.0, Wanted), (double)fmax((double)Room, 1.0));
    // It lies as the mesh does: a bent piece's frames take no rotation offset or yaw jitter, and its jitter is the piece's own.
    PathObjectSlotRecord ChordSettings = *Settings;
    ChordSettings.RotationOffset = (Vector3){ 0.0f, 0.0f, 0.0f };
    ChordSettings.YawJitter = 0.0f;
    if (IsBoxes)
    {
        ChordSettings.OverridesLength = true;
        ChordSettings.Length = (float)(info->PieceLength / (double)Count);
    }
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        PathObjectPiece Chord = *Piece;
        Chord.Start = Piece->Start + (info->PieceLength * (double)Index / (double)Count);
        Chord.End = Piece->Start + (info->PieceLength * (double)(Index + 1U) / (double)Count);
        PathObjectCopyInput Input;
        Memory_Zero(&Input, sizeof(Input));
        if (!WorldPathObjectsRigid_MakePieceEnds(rebuild, &Chord, info->Span, slot, info->SideSign, &Input.Start, &Input.End))
        {
            continue;
        }
        Input.Measure = info->Model->Measure;
        Input.Slot = &ChordSettings;
        Input.IsTurned = info->IsTurned;
        Input.Tint = info->Model->Record->Tint;
        for (uint32_t Channel = 0U; Channel < 5U; Channel++)
        {
            Input.Jitter[Channel] = WorldPathObjectsRigid_GetJitter(rebuild, Piece, info->Span, info->SideIndex, Channel);
        }
        PropertyInstanceEntry Copy;
        if (PathObjectPlacement_MakeCopy(&Input, &Copy))
        {
            WorldPathObjectsRigid_AddCopyCollision(rebuild, Piece->Slot, Piece->Model, &Copy, &ChordSettings);
        }
    }
}


// Functions.
void WorldPathObjectsBent_FreeParts(WorldPathObjects* self)
{
    PathObjectBentPart* Parts = (PathObjectBentPart*)self->_parts._data;
    for (size_t Index = 0U; Index < self->_parts._count; Index++)
    {
        for (size_t Lod = 0U; Lod < WORLD_MODEL_MAX_LODS; Lod++)
        {
            for (uint32_t MeshIndex = 0U; MeshIndex < Parts[Index].MeshCounts[Lod]; MeshIndex++)
            {
                PathObjectBentMesh_Deconstruct(&Parts[Index].Meshes[Lod][MeshIndex]);
            }
            Memory_Free(Parts[Index].Meshes[Lod]);
        }
    }
    GenericBuffer_Clear(&self->_parts);
}

/* One bent copy of a piece. */
bool WorldPathObjectsBent_IsPartEmpty(const PathObjectBentPart* part)
{
    for (size_t Lod = 0U; Lod < WORLD_MODEL_MAX_LODS; Lod++)
    {
        for (uint32_t MeshIndex = 0U; MeshIndex < part->MeshCounts[Lod]; MeshIndex++)
        {
            if (PathObjectBentMesh_GetVertexCount(&part->Meshes[Lod][MeshIndex]) > 0U)
            {
                return false;
            }
        }
    }
    return true;
}

void WorldPathObjectsBent_AddCopy(PathObjectRebuild* rebuild, const PathObjectPiece* piece, const PathObjectSpan* span,
    const PathObjectSlotBuild* slot,
    uint32_t sideIndex, float sideSign)
{
    const PathObjectSlotModel* ModelBuild = &slot->Models[piece->Model];
    const PathObjectSlotRecord* Settings = &slot->Settings;
    float Nominal = PathObjectMeasure_GetLength(&ModelBuild->Measure, Settings);
    float BodyStart = PathObjectMeasure_GetBodyCenter(&ModelBuild->Measure, Settings) - (0.5f * Nominal);
    bool IsTurned = WorldPathObjectsRigid_IsPieceFlipped(rebuild, piece, span, Settings) != (sideSign < 0.0f);
    float CutAtMeshStart = IsTurned ? piece->CutEnd : piece->CutStart;
    PathObjectBentPieceInfo Info =
    {
        .Piece = piece, .Span = span, .Settings = Settings, .Model = ModelBuild,
        .IsTurned = IsTurned,
        .SideIndex = sideIndex,
        .SideSign = sideSign,
        .PieceLength = piece->End - piece->Start,
        .KeptStart = BodyStart + (CutAtMeshStart * Nominal),
        .KeptLength = Nominal * (1.0f - piece->CutStart - piece->CutEnd),
        .SizeJitter = 1.0f + (Settings->ScaleJitter * ((2.0f * WorldPathObjectsRigid_GetJitter(rebuild, piece, span, sideIndex,
            4U)) - 1.0f)),
        .Offset =
        {
            Settings->PositionOffset.x + (Settings->PositionJitter.x * ((2.0f * WorldPathObjectsRigid_GetJitter(rebuild, piece,
                span, sideIndex, 0U)) - 1.0f)),
            Settings->PositionOffset.y + (Settings->PositionJitter.y * ((2.0f * WorldPathObjectsRigid_GetJitter(rebuild, piece,
                span, sideIndex, 1U)) - 1.0f)),
            Settings->PositionOffset.z + (Settings->PositionJitter.z * ((2.0f * WorldPathObjectsRigid_GetJitter(rebuild, piece,
                span, sideIndex, 2U)) - 1.0f))
        }
    };
    Info.Stretch = (Info.KeptLength > 0.0f) ? (float)(Info.PieceLength / (double)Info.KeptLength) : 1.0f;
    if ((rebuild->VertexBudget == 0U) || (rebuild->BentWorkLeft == 0U))
    {
        if (!rebuild->State->HasWarnedBudget)
        {
            rebuild->State->HasWarnedBudget = true;
            WorldPathObjectsInternal_Warn(rebuild->System, rebuild->Owner, "bends more than it may: the rest is left out.");
        }
    }
    else if (WritePieceDistances(rebuild, span, piece, Settings->SliceSpacing) && MakeBentPlaces(rebuild, &Info))
    {
        BendPiece(rebuild, &Info);
    }
    AddBentCollision(rebuild, &Info, slot);
}

WorldGeneratedModel* WorldPathObjectsBent_MakeGeneratedModel(const PathObjectRebuild* rebuild, const PathObjectBentPart* part)
{
    const WorldModel* Asset = rebuild->Slots[part->Slot].Models[part->Model].Asset;
    size_t LodCount = WorldModel_GetLodCount(Asset);
    WorldGeneratedModel* Generated = WorldGeneratedModel_Create(LodCount);
    for (size_t Lod = 0U; Lod < LodCount; Lod++)
    {
        Model Source = GameModel_GetRaylibModel(WorldModel_GetLodModel(Asset, Lod));
        for (uint32_t MeshIndex = 0U; MeshIndex < part->MeshCounts[Lod]; MeshIndex++)
        {
            PathObjectBentMesh* Built = &part->Meshes[Lod][MeshIndex];
            size_t Count = PathObjectBentMesh_GetVertexCount(Built);
            if (Count == 0U)
            {
                continue;
            }
            // The builder's arrays are handed over whole: their capacity is released with them.
            WorldGeneratedMesh Made =
            {
                .MaterialSlot = (Source.meshMaterial != NULL) ? Source.meshMaterial[MeshIndex] : 0,
                .VertexCount = (uint32_t)Count,
                .Positions = (float*)Built->Positions._data,
                .Normals = (float*)Built->Normals._data,
                .Tangents = (Built->Tangents._count == (Count * 4U)) ? (float*)Built->Tangents._data : NULL,
                .Texcoords = (Built->Texcoords._count == (Count * 2U)) ? (float*)Built->Texcoords._data : NULL,
                .Texcoords2 = (Built->Texcoords2._count == (Count * 2U)) ? (float*)Built->Texcoords2._data : NULL,
                .Colors = (Built->Colors._count == (Count * 4U)) ? (unsigned char*)Built->Colors._data : NULL
            };
            if (WorldGeneratedModel_AddMesh(Generated, Lod, &Made))
            {
                // What the model took is no longer the builder's; what it left stays to be freed with the builder.
                GenericBuffer_AllocateVariable(&Built->Positions, 0U, sizeof(float));
                GenericBuffer_AllocateVariable(&Built->Normals, 0U, sizeof(float));
                if (Made.Tangents != NULL)
                {
                    GenericBuffer_AllocateVariable(&Built->Tangents, 0U, sizeof(float));
                }
                if (Made.Texcoords != NULL)
                {
                    GenericBuffer_AllocateVariable(&Built->Texcoords, 0U, sizeof(float));
                }
                if (Made.Texcoords2 != NULL)
                {
                    GenericBuffer_AllocateVariable(&Built->Texcoords2, 0U, sizeof(float));
                }
                if (Made.Colors != NULL)
                {
                    GenericBuffer_AllocateVariable(&Built->Colors, 0U, sizeof(unsigned char));
                }
            }
        }
    }
    WorldGeneratedModel_UpdateBounds(Generated);
    return Generated;
}
