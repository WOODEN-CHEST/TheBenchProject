// Instanced drawing as pure maths: a component's cells and records, its LOD bands, and every copy's fade and LOD (the C
// mirror of shaders/common/instancing.glsl). See WorldInstancing.h.

#include <float.h>
#include <math.h>
#include "WorldInstancing.h"
#include "WorldLod.h"
#include "WorldTransform.h"
#include "WorldVisibility.h"
#include "wr/WRComparator.h"


// Macros.
/** Initial capacity (in copies) of the cells' buffers; they grow with the largest list built. */
#define INITIAL_CAPACITY 16U
/** The random value keeps this many bits of its hash: as many as a float's significand holds exactly below 1. */
#define RANDOM_BITS 24U
/** The splitmix64 constants (Steele, Lea and Flood): an increment by the golden ratio, then two multiply-xorshift rounds. */
#define SPLITMIX_INCREMENT 0x9E3779B97F4A7C15ULL
#define SPLITMIX_MULTIPLIER_A 0xBF58476D1CE4E5B9ULL
#define SPLITMIX_MULTIPLIER_B 0x94D049BB133111EBULL


// Types.
/* A copy's grid square and its index in the list, while sorting. */
typedef struct InstanceKeyStruct
{
    int64_t X;
    int64_t Z;
    size_t Index;
} InstanceKey;


// Static functions: cells.
/* The grid square a coordinate lies in; 0 for anything not finite (the component's rules keep positions finite). */
static int64_t GetSquare(double coordinate)
{
    double Square = floor(coordinate / WORLD_INSTANCE_CELL_SIZE);
    if (!isfinite(Square) || (Square > (double)INT32_MAX) || (Square < (double)INT32_MIN))
    {
        return 0;
    }
    return (int64_t)Square;
}

/* Grid order (X, then Z), and the list's order within a square, so a build is the same on every machine. */
static ComparisonResult CompareKeys(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    const InstanceKey* First = a._element;
    const InstanceKey* Second = b._element;
    if (First->X != Second->X)
    {
        return (First->X < Second->X) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
    }
    if (First->Z != Second->Z)
    {
        return (First->Z < Second->Z) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
    }
    if (First->Index != Second->Index)
    {
        return (First->Index < Second->Index) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
    }
    return ComparisonResult_AEqualsB;
}

/* Empties every buffer (a build replaces the cells). */
static void Clear(WorldInstanceCells* self)
{
    GenericBuffer_Clear(&self->_cells);
    GenericBuffer_Clear(&self->_records);
    GenericBuffer_Clear(&self->_sources);
    GenericBuffer_Clear(&self->_keys);
}

/* Fills the keys of @p count copies and sorts them into grid order. */
static bool SortKeys(WorldInstanceCells* self, const PropertyInstanceEntry* instances, size_t count)
{
    if (!GenericBuffer_EnsureTotalCapacity(&self->_keys, count))
    {
        return false;
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        InstanceKey Key = { .X = GetSquare(instances[Index].Position.X), .Z = GetSquare(instances[Index].Position.Z),
            .Index = Index };
        if (!GenericBuffer_AddLast(&self->_keys, &Key))
        {
            return false;
        }
    }
    size_t ScratchSize = GenericBuffer_GetSortScratchSize(&self->_keys);
    if (!GenericBuffer_EnsureTotalCapacity(&self->_sortScratch, ScratchSize))
    {
        return false;
    }
    return (count < 2U) || GenericBuffer_SortAscending(&self->_keys, CompareKeys, NULL, self->_sortScratch._data);
}

/* A record from a copy's matrix relative to its cell's centre (rounded once from double). */
static WorldInstanceRecord MakeRecord(const Matrix4Double* relative, Color tint, float random)
{
    WorldInstanceRecord Record = { .Tint = tint, .Random = random };
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        for (size_t Column = 0U; Column < 4U; Column++)
        {
            Record.Rows[(Row * 4U) + Column] = (float)relative->M[(Column * 4U) + Row];
        }
    }
    return Record;
}

/* One cell from the sorted keys [first, end): its centre, its records and sources, and its sphere around every copy's. */
static bool BuildCell(WorldInstanceCells* self, const PropertyInstanceEntry* instances, const InstanceKey* keys,
    size_t first, size_t end, uint64_t objectId, const WorldInstanceBounds* bounds)
{
    double MinimumY = instances[keys[first].Index].Position.Y;
    double MaximumY = MinimumY;
    for (size_t Position = first + 1U; Position < end; Position++)
    {
        double Y = instances[keys[Position].Index].Position.Y;
        MinimumY = fmin(MinimumY, Y);
        MaximumY = fmax(MaximumY, Y);
    }
    WorldInstanceCell Cell =
    {
        .Center = Vector3Double_Create(((double)keys[first].X + 0.5) * WORLD_INSTANCE_CELL_SIZE,
            (MinimumY + MaximumY) * 0.5, ((double)keys[first].Z + 0.5) * WORLD_INSTANCE_CELL_SIZE),
        .FirstRecord = self->_records._count,
        .RecordCount = end - first
    };

    // The records, and each copy's sphere (in the cell's space, in double) for the cell's bounds.
    Vector3Double BoundsCenter = Vector3Double_Create((double)bounds->Center.x, (double)bounds->Center.y,
        (double)bounds->Center.z);
    Vector3Double Minimum = Vector3Double_Create(DBL_MAX, DBL_MAX, DBL_MAX);
    Vector3Double Maximum = Vector3Double_Create(-DBL_MAX, -DBL_MAX, -DBL_MAX);
    for (size_t Position = first; Position < end; Position++)
    {
        size_t Index = keys[Position].Index;
        const PropertyInstanceEntry* Instance = &instances[Index];
        Matrix4Double Relative = WorldTransform_Compose(Vector3Double_Subtract(Instance->Position, Cell.Center),
            Instance->Rotation, Instance->Scale);
        WorldInstanceRecord Record = MakeRecord(&Relative, Instance->Tint, WorldInstancing_GetRandom(objectId, Index));
        if (!GenericBuffer_AddLast(&self->_records, &Record) || !GenericBuffer_AddLast(&self->_sources, &Index))
        {
            return false;
        }
        Vector3Double SphereCenter = Matrix4Double_TransformPoint(&Relative, BoundsCenter);
        double Radius = (double)bounds->Radius * WorldTransform_GetMaxScale(&Relative);
        Minimum = Vector3Double_Min(Minimum, Vector3Double_SubtractValue(SphereCenter, Radius));
        Maximum = Vector3Double_Max(Maximum, Vector3Double_AddValue(SphereCenter, Radius));
    }
    Vector3Double Middle = Vector3Double_Scale(Vector3Double_Add(Minimum, Maximum), 0.5);
    double SphereRadius = 0.0;
    for (size_t Position = first; Position < end; Position++)
    {
        const PropertyInstanceEntry* Instance = &instances[keys[Position].Index];
        Matrix4Double Relative = WorldTransform_Compose(Vector3Double_Subtract(Instance->Position, Cell.Center),
            Instance->Rotation, Instance->Scale);
        Vector3Double SphereCenter = Matrix4Double_TransformPoint(&Relative, BoundsCenter);
        double Radius = (double)bounds->Radius * WorldTransform_GetMaxScale(&Relative);
        SphereRadius = fmax(SphereRadius, Vector3Double_Distance(Middle, SphereCenter) + Radius);
    }
    Cell.SphereCenter = Vector3Double_Add(Middle, Cell.Center);
    Cell.SphereRadius = SphereRadius;
    return GenericBuffer_AddLast(&self->_cells, &Cell);
}


// Static functions: fades and bands.
/* The scale to use: anything not finite or below 0 counts as 1. */
static float SanitizeScale(float scale)
{
    return (isfinite(scale) && (scale >= 0.0f)) ? scale : 1.0f;
}

/* The hysteresis to use: a share in [0, 1); anything else counts as none. */
static float SanitizeHysteresis(float hysteresis)
{
    return ((hysteresis >= 0.0f) && (hysteresis < 1.0f)) ? hysteresis : 0.0f;
}

/* Switch distance @p band of @p settings times the scale: 0 for a scale of 0, whatever the distance (an infinite one
 * times 0 would be NaN), as WorldLod_Select puts every switch at 0 then. */
static float GetSwitch(const WorldModelLodSettings* settings, size_t band, float scale)
{
    return (scale == 0.0f) ? 0.0f : (settings->SwitchDistances[band] * scale);
}

/* Row r of a record applied to the point p = (x, y, z, 1). */
static float ApplyRow(const WorldInstanceRecord* record, size_t row, Vector3 point)
{
    const float* Row = &record->Rows[row * 4U];
    return (Row[0] * point.x) + (Row[1] * point.y) + (Row[2] * point.z) + Row[3];
}

/* The longest axis of a record's matrix: a copy's sphere's radius is the bounds' times it. */
static float GetLongestAxis(const WorldInstanceRecord* record)
{
    float Longest = 0.0f;
    for (size_t Column = 0U; Column < 3U; Column++)
    {
        float X = record->Rows[Column];
        float Y = record->Rows[4U + Column];
        float Z = record->Rows[8U + Column];
        Longest = fmaxf(Longest, sqrtf((X * X) + (Y * Y) + (Z * Z)));
    }
    return Longest;
}


// Internal functions (declared in WorldInstancing.h).
void WorldInstanceCells_Construct(WorldInstanceCells* self)
{
    GenericBuffer_AllocateVariable(&self->_cells, INITIAL_CAPACITY, sizeof(WorldInstanceCell));
    GenericBuffer_AllocateVariable(&self->_records, INITIAL_CAPACITY, sizeof(WorldInstanceRecord));
    GenericBuffer_AllocateVariable(&self->_sources, INITIAL_CAPACITY, sizeof(size_t));
    GenericBuffer_AllocateVariable(&self->_keys, INITIAL_CAPACITY, sizeof(InstanceKey));
    GenericBuffer_AllocateVariable(&self->_sortScratch, 2U * sizeof(InstanceKey), sizeof(unsigned char));
}

void WorldInstanceCells_Deconstruct(WorldInstanceCells* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->_cells._data);
    Memory_Free(self->_records._data);
    Memory_Free(self->_sources._data);
    Memory_Free(self->_keys._data);
    Memory_Free(self->_sortScratch._data);
}

Error WorldInstanceCells_Build(WorldInstanceCells* self, const PropertyInstanceEntry* instances, size_t count,
    uint64_t objectId, const WorldInstanceBounds* bounds)
{
    if ((self == NULL) || (bounds == NULL) || ((instances == NULL) && (count > 0U)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldInstanceCells_Build: self and bounds must not be NULL, nor instances while count is above 0.");
    }
    Clear(self);
    if (count == 0U)
    {
        return Error_CreateSuccess();
    }
    bool IsBuilt = SortKeys(self, instances, count) && GenericBuffer_EnsureTotalCapacity(&self->_records, count)
        && GenericBuffer_EnsureTotalCapacity(&self->_sources, count);
    const void* KeyData = self->_keys._data;
    const InstanceKey* Keys = KeyData;
    size_t First = 0U;
    while (IsBuilt && (First < count))
    {
        size_t End = First + 1U;
        while ((End < count) && (Keys[End].X == Keys[First].X) && (Keys[End].Z == Keys[First].Z))
        {
            End++;
        }
        IsBuilt = BuildCell(self, instances, Keys, First, End, objectId, bounds);
        First = End;
    }
    GenericBuffer_Clear(&self->_keys);
    if (!IsBuilt)
    {
        Clear(self);
        return Error_Construct2(ErrorCode_InvalidOperation, "WorldInstanceCells_Build: a buffer could not grow.");
    }
    return Error_CreateSuccess();
}

float WorldInstancing_GetRandom(uint64_t objectId, size_t index)
{
    uint64_t Value = (objectId * SPLITMIX_INCREMENT) + (uint64_t)index + SPLITMIX_INCREMENT;
    Value = (Value ^ (Value >> 30U)) * SPLITMIX_MULTIPLIER_A;
    Value = (Value ^ (Value >> 27U)) * SPLITMIX_MULTIPLIER_B;
    Value ^= Value >> 31U;
    return (float)(Value >> (64U - RANDOM_BITS)) / (float)(1U << RANDOM_BITS);
}

Matrix WorldInstanceRecord_GetMatrix(const WorldInstanceRecord* record)
{
    const float* R = record->Rows;
    return (Matrix)
    {
        .m0 = R[0], .m4 = R[1], .m8 = R[2], .m12 = R[3],
        .m1 = R[4], .m5 = R[5], .m9 = R[6], .m13 = R[7],
        .m2 = R[8], .m6 = R[9], .m10 = R[10], .m14 = R[11],
        .m3 = 0.0f, .m7 = 0.0f, .m11 = 0.0f, .m15 = 1.0f
    };
}

void WorldInstancing_GetLodBands(const WorldModelLodSettings* settings, float distanceScale,
    WorldInstanceLodBands* outBands)
{
    WorldInstanceLodBands Bands = { .LodCount = 1U, .IsForced = false, .ForcedLod = 0U };
    size_t Count = (settings->LodCount < WORLD_MODEL_MAX_LODS) ? settings->LodCount : WORLD_MODEL_MAX_LODS;
    Bands.LodCount = (Count > 0U) ? Count : 1U;
    if (settings->IsLodForced)
    {
        Bands.IsForced = true;
        Bands.ForcedLod = (settings->ForcedLod < Bands.LodCount) ? settings->ForcedLod : (Bands.LodCount - 1U);
    }
    float Scale = SanitizeScale(distanceScale);
    float Hysteresis = SanitizeHysteresis(settings->Hysteresis);
    for (size_t Band = 0U; (Band + 1U) < Bands.LodCount; Band++)
    {
        float Switch = GetSwitch(settings, Band, Scale);
        float Start = Switch * (1.0f - Hysteresis);
        float End = Switch * (1.0f + Hysteresis);
        if (Band > 0U)
        {
            Start = fmaxf(Start, 0.5f * (GetSwitch(settings, Band - 1U, Scale) + Switch));
        }
        if ((Band + 2U) < Bands.LodCount)
        {
            End = fminf(End, 0.5f * (Switch + GetSwitch(settings, Band + 1U, Scale)));
        }
        Bands.Start[Band] = Start;
        Bands.End[Band] = fmaxf(End, Start);
    }
    *outBands = Bands;
}

void WorldInstancing_SetDrawDistance(WorldInstanceView* self, float drawDistance)
{
    if (!(drawDistance > 0.0f))
    {
        self->DrawDistance = 0.0f; // nothing is nearer: every copy fades out
        self->FadeStart = 0.0f;
        self->FadeBand = 1.0f;
        return;
    }
    if (isinf(drawDistance))
    {
        self->DrawDistance = FLT_MAX;
        self->FadeStart = FLT_MAX;
        self->FadeBand = 1.0f;
        return;
    }
    float Band = WorldVisibility_GetFadeBand(drawDistance);
    self->DrawDistance = drawDistance;
    self->FadeStart = drawDistance - Band;
    self->FadeBand = Band;
}

float WorldInstancing_GetDistanceFade(const WorldInstanceView* view, float distance)
{
    if (!(distance < view->DrawDistance))
    {
        return 1.0f; // beyond the draw distance, or a NaN
    }
    if (distance <= view->FadeStart)
    {
        return 0.0f;
    }
    return fminf((distance - view->FadeStart) / view->FadeBand, 1.0f);
}

WorldInstanceState WorldInstancing_Evaluate(const WorldInstanceView* view, const Matrix* cellRenderMatrix,
    const WorldInstanceRecord* record)
{
    // The copy's sphere: the bounds under its matrix (in the cell's space), then the cell's matrix into render space.
    Vector3 Local = { ApplyRow(record, 0U, view->BoundsCenter), ApplyRow(record, 1U, view->BoundsCenter),
        ApplyRow(record, 2U, view->BoundsCenter) };
    const Matrix* M = cellRenderMatrix;
    Vector3 Render =
    {
        (M->m0 * Local.x) + (M->m4 * Local.y) + (M->m8 * Local.z) + M->m12,
        (M->m1 * Local.x) + (M->m5 * Local.y) + (M->m9 * Local.z) + M->m13,
        (M->m2 * Local.x) + (M->m6 * Local.y) + (M->m10 * Local.z) + M->m14
    };
    float CenterDistance = sqrtf((Render.x * Render.x) + (Render.y * Render.y) + (Render.z * Render.z));
    float Radius = view->BoundsRadius * GetLongestAxis(record);
    float LodDistance = CenterDistance * view->ZoomFactor;
    float FadeDistance = fmaxf(CenterDistance - Radius, 0.0f) * view->ZoomFactor;

    float Opacity = fminf(fmaxf(view->ObjectOpacity * ((float)record->Tint.a / 255.0f), 0.0f), 1.0f);
    WorldInstanceState State =
    {
        .Fade = fmaxf(WorldInstancing_GetDistanceFade(view, FadeDistance), 1.0f - Opacity),
        .Lod = view->Bands.LodCount - 1U, .IncomingLod = view->Bands.LodCount - 1U, .Progress = 0.0f,
        .IsInBand = false
    };
    if (view->Bands.IsForced)
    {
        State.Lod = view->Bands.ForcedLod;
        State.IncomingLod = State.Lod;
        return State;
    }
    for (size_t Band = 0U; (Band + 1U) < view->Bands.LodCount; Band++)
    {
        if (LodDistance < view->Bands.Start[Band])
        {
            State.Lod = Band;
            State.IncomingLod = Band;
            break;
        }
        if (LodDistance < view->Bands.End[Band])
        {
            State.Lod = Band;
            State.IncomingLod = Band + 1U;
            State.IsInBand = true;
            State.Progress = (LodDistance - view->Bands.Start[Band]) / (view->Bands.End[Band] - view->Bands.Start[Band]);
            break;
        }
    }
    return State;
}

size_t WorldInstancing_GetSolidLod(const WorldInstanceState* state)
{
    return (state->IsInBand && (state->Progress >= WORLD_LOD_SOLID_SHARE)) ? state->IncomingLod : state->Lod;
}

WorldInstanceDraw WorldInstancing_GetDraw(const WorldInstanceState* state, WorldInstancePass pass, size_t lod)
{
    WorldInstanceDraw Draw = { .IsDrawn = false, .Fade = 0.0f, .Progress = 0.0f, .IsIncoming = false };
    if (!(state->Fade < 1.0f))
    {
        return Draw;
    }
    switch (pass)
    {
        case WorldInstancePass_Lit:
            if (lod == state->Lod)
            {
                Draw = (WorldInstanceDraw){ .IsDrawn = true, .Fade = state->Fade, .Progress = state->Progress,
                    .IsIncoming = false };
            }
            else if (state->IsInBand && (lod == state->IncomingLod))
            {
                Draw = (WorldInstanceDraw){ .IsDrawn = true, .Fade = state->Fade, .Progress = state->Progress,
                    .IsIncoming = true };
            }
            return Draw;
        case WorldInstancePass_GBuffer:
            Draw.IsDrawn = (lod == WorldInstancing_GetSolidLod(state))
                && (state->Fade < WORLD_VISIBILITY_GBUFFER_FADE_LIMIT);
            return Draw;
        case WorldInstancePass_Shadow:
            Draw.IsDrawn = lod == WorldInstancing_GetSolidLod(state);
            Draw.Fade = state->Fade;
            return Draw;
        case WorldInstancePass_Unlit:
            Draw.IsDrawn = lod == WorldInstancing_GetSolidLod(state);
            return Draw;
    }
    // Only reachable with a corrupted pass value: drawing nothing is the safe choice.
    return Draw;
}

uint32_t WorldInstancing_GetCellLodMask(const WorldInstanceLodBands* bands, float nearest, float farthest)
{
    if (bands->IsForced)
    {
        return (uint32_t)1U << bands->ForcedLod;
    }
    uint32_t Every = ((uint32_t)1U << bands->LodCount) - 1U;
    if (isnan(nearest) || isnan(farthest))
    {
        return Every;
    }
    float Nearest = (nearest * (1.0f - WORLD_INSTANCE_CELL_RANGE_SHARE)) - WORLD_INSTANCE_CELL_RANGE_MARGIN;
    float Farthest = (farthest * (1.0f + WORLD_INSTANCE_CELL_RANGE_SHARE)) + WORLD_INSTANCE_CELL_RANGE_MARGIN;
    uint32_t Mask = 0U;
    for (size_t Lod = 0U; Lod < bands->LodCount; Lod++)
    {
        // LOD k shows from the start of the band before it (as its incoming LOD) to the end of the band after it.
        float Start = (Lod == 0U) ? -FLT_MAX : bands->Start[Lod - 1U];
        float End = ((Lod + 1U) == bands->LodCount) ? FLT_MAX : bands->End[Lod];
        bool IsLast = (Lod + 1U) == bands->LodCount;
        if (((Nearest < End) || IsLast) && (Farthest >= Start))
        {
            Mask |= (uint32_t)1U << Lod;
        }
    }
    return Mask;
}
