#include "TestSuites.h"
#include "WorldInstancing.h"
#include "WorldLod.h"
#include "WorldTransform.h"
#include "WorldVisibility.h"
#include "GameMath.h"
#include "raylib/raymath.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** How many copies the seeded scenes hold. */
#define TEST_COPY_COUNT 600U
/** Distance steps of the band sweeps, per metre. */
#define TEST_STEPS_PER_METRE 50U


// Static functions: helpers.
/* A small linear congruential generator, so every run builds the same scene. */
static uint32_t NextRandom(uint32_t* state)
{
    *state = (*state * 1664525U) + 1013904223U;
    return *state >> 8;
}

/* A uniform value in [minimum, maximum). */
static double RandomBetween(uint32_t* state, double minimum, double maximum)
{
    return minimum + ((maximum - minimum) * ((double)NextRandom(state) / 16777216.0));
}

/* Copies scattered over [-100, 100) m around the object in X and Z (exact cell edges among them), up to 20 m in Y,
 * turned every way, scaled evenly and unevenly, tinted. */
static void MakeCopies(PropertyInstanceEntry* outCopies, size_t count, uint32_t seed)
{
    uint32_t State = seed;
    for (size_t Index = 0U; Index < count; Index++)
    {
        PropertyInstanceEntry* Copy = &outCopies[Index];
        *Copy = PropertyInstanceEntry_CreateDefault();
        Copy->Position = Vector3Double_Create(RandomBetween(&State, -100.0, 100.0), RandomBetween(&State, -2.0, 20.0),
            RandomBetween(&State, -100.0, 100.0));
        if ((Index % 7U) == 0U)
        {
            // On a cell's edge, exactly.
            Copy->Position.X = WORLD_INSTANCE_CELL_SIZE * (double)((int)(Index % 5U) - 2);
        }
        Copy->Rotation = (Vector3){ (float)RandomBetween(&State, -3.0, 3.0), (float)RandomBetween(&State, -3.0, 3.0),
            (float)RandomBetween(&State, -3.0, 3.0) };
        float Even = (float)RandomBetween(&State, 0.25, 3.0);
        Copy->Scale = ((Index % 3U) == 0U) ? (Vector3){ Even, Even, Even }
            : (Vector3){ Even, (float)RandomBetween(&State, 0.2, 4.0), (float)RandomBetween(&State, 0.5, 2.0) };
        Copy->Tint = (Color){ (unsigned char)(NextRandom(&State) & 255U), 180U, 90U, (unsigned char)(255U - (Index % 4U)) };
    }
}

/* The world model's sphere in a copy's space for the tests: off its origin, as a tree's is. */
static WorldInstanceBounds MakeBounds(void)
{
    return (WorldInstanceBounds){ .Center = { 0.2f, 1.5f, -0.1f }, .Radius = 1.75f };
}

/* Three LODs switching at 10 and 30 m (the last one's 90 m is its default draw distance), with hysteresis @p hysteresis. */
static WorldModelLodSettings MakeSettings(float hysteresis)
{
    return (WorldModelLodSettings)
    {
        .LodCount = 3U, .SwitchDistances = { 10.0f, 30.0f, 90.0f }, .Hysteresis = hysteresis, .FadeSeconds = 0.4f,
        .IsLodForced = false, .ForcedLod = 0U
    };
}

/* A view of a component with an even bounds sphere at a copy's origin (so a copy's LOD distance is its distance), no
 * opacity loss and @p settings' bands. */
static WorldInstanceView MakeView(const WorldModelLodSettings* settings, float drawDistance)
{
    WorldInstanceView View = { .ZoomFactor = 1.0f, .ObjectOpacity = 1.0f, .BoundsCenter = { 0.0f, 0.0f, 0.0f },
        .BoundsRadius = 0.5f };
    WorldInstancing_SetDrawDistance(&View, drawDistance);
    WorldInstancing_GetLodBands(settings, 1.0f, &View.Bands);
    return View;
}

/* A copy at the cell's centre, unturned, at size 1, opaque white. */
static WorldInstanceRecord MakePlainRecord(void)
{
    PropertyInstanceEntry Copy = PropertyInstanceEntry_CreateDefault();
    Matrix4Double Composed = WorldTransform_Compose(Copy.Position, Copy.Rotation, Copy.Scale);
    WorldInstanceRecord Record = { .Tint = Copy.Tint, .Random = 0.5f };
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        for (size_t Column = 0U; Column < 4U; Column++)
        {
            Record.Rows[(Row * 4U) + Column] = (float)Composed.M[(Column * 4U) + Row];
        }
    }
    return Record;
}

/* The state of a plain copy @p distance metres in front of the camera. */
static WorldInstanceState EvaluateAt(const WorldInstanceView* view, float distance)
{
    WorldInstanceRecord Record = MakePlainRecord();
    Matrix Cell = MatrixTranslate(0.0f, 0.0f, distance);
    return WorldInstancing_Evaluate(view, &Cell, &Record);
}

/* Which LOD the lit pass draws each cell of the 4x4 pattern with (SIZE_MAX where none, SIZE_MAX - 1 where two do). */
static void GetPatternLods(const WorldInstanceState* state, size_t lodCount, size_t outLods[WORLD_VISIBILITY_DITHER_LEVELS])
{
    for (uint32_t Cell = 0U; Cell < WORLD_VISIBILITY_DITHER_LEVELS; Cell++)
    {
        outLods[Cell] = SIZE_MAX;
        for (size_t Lod = 0U; Lod < lodCount; Lod++)
        {
            WorldInstanceDraw Draw = WorldInstancing_GetDraw(state, WorldInstancePass_Lit, Lod);
            if (Draw.IsDrawn && !WorldVisibility_IsDitherDiscarded(Draw.Fade, Draw.Progress, Draw.IsIncoming,
                Cell % WORLD_VISIBILITY_DITHER_SIZE, Cell / WORLD_VISIBILITY_DITHER_SIZE))
            {
                outLods[Cell] = (outLods[Cell] == SIZE_MAX) ? Lod : (SIZE_MAX - 1U);
            }
        }
    }
}


// Tests: cells.
/* Every copy lands in exactly one cell, the one whose square holds its position; cells come in grid order and keep the
 * list's order inside; a cell's centre is its square's middle in X and Z and its copies' middle height. */
static void EveryCopyLandsInExactlyOneCell(TestRun* run)
{
    PropertyInstanceEntry* Copies = Memory_Allocate(TEST_COPY_COUNT * sizeof(PropertyInstanceEntry));
    MakeCopies(Copies, TEST_COPY_COUNT, 7U);
    WorldInstanceCells Cells;
    WorldInstanceCells_Construct(&Cells);
    WorldInstanceBounds Bounds = MakeBounds();
    if (TEST_CHECK_SUCCESS(run, WorldInstanceCells_Build(&Cells, Copies, TEST_COPY_COUNT, 42U, &Bounds)))
    {
        size_t CellCount = 0U;
        size_t RecordCount = 0U;
        size_t SourceCount = 0U;
        const WorldInstanceCell* List = WorldInstanceCells_GetCells(&Cells, &CellCount);
        WorldInstanceCells_GetRecords(&Cells, &RecordCount);
        const size_t* Sources = WorldInstanceCells_GetSources(&Cells, &SourceCount);
        TEST_CHECK(run, (RecordCount == TEST_COPY_COUNT) && (SourceCount == TEST_COPY_COUNT) && (CellCount > 20U)
            && (CellCount <= 64U));
        uint8_t* Seen = Memory_Allocate(TEST_COPY_COUNT);
        Memory_Zero(Seen, TEST_COPY_COUNT);
        size_t Next = 0U;
        bool AreAllInTheirSquare = true;
        bool AreInGridOrder = true;
        bool AreInListOrder = true;
        bool AreCentresRight = true;
        for (size_t CellIndex = 0U; CellIndex < CellCount; CellIndex++)
        {
            const WorldInstanceCell* Cell = &List[CellIndex];
            AreInGridOrder = AreInGridOrder && (Cell->FirstRecord == Next) && (Cell->RecordCount > 0U);
            double SquareX = floor(Cell->Center.X / WORLD_INSTANCE_CELL_SIZE);
            double SquareZ = floor(Cell->Center.Z / WORLD_INSTANCE_CELL_SIZE);
            AreCentresRight = AreCentresRight && (Cell->Center.X == ((SquareX + 0.5) * WORLD_INSTANCE_CELL_SIZE))
                && (Cell->Center.Z == ((SquareZ + 0.5) * WORLD_INSTANCE_CELL_SIZE));
            if (CellIndex > 0U)
            {
                const WorldInstanceCell* Before = &List[CellIndex - 1U];
                double BeforeX = floor(Before->Center.X / WORLD_INSTANCE_CELL_SIZE);
                double BeforeZ = floor(Before->Center.Z / WORLD_INSTANCE_CELL_SIZE);
                AreInGridOrder = AreInGridOrder && ((BeforeX < SquareX) || ((BeforeX == SquareX) && (BeforeZ < SquareZ)));
            }
            double MinimumY = DBL_MAX;
            double MaximumY = -DBL_MAX;
            for (size_t Record = Cell->FirstRecord; Record < (Cell->FirstRecord + Cell->RecordCount); Record++)
            {
                size_t Source = Sources[Record];
                Seen[Source]++;
                const PropertyInstanceEntry* Copy = &Copies[Source];
                AreAllInTheirSquare = AreAllInTheirSquare && (floor(Copy->Position.X / WORLD_INSTANCE_CELL_SIZE) == SquareX)
                    && (floor(Copy->Position.Z / WORLD_INSTANCE_CELL_SIZE) == SquareZ);
                AreInListOrder = AreInListOrder && ((Record == Cell->FirstRecord) || (Sources[Record - 1U] < Source));
                MinimumY = fmin(MinimumY, Copy->Position.Y);
                MaximumY = fmax(MaximumY, Copy->Position.Y);
            }
            AreCentresRight = AreCentresRight && (Cell->Center.Y == ((MinimumY + MaximumY) * 0.5));
            Next += Cell->RecordCount;
        }
        bool IsEachOnce = true;
        for (size_t Index = 0U; Index < TEST_COPY_COUNT; Index++)
        {
            IsEachOnce = IsEachOnce && (Seen[Index] == 1U);
        }
        TEST_CHECK(run, IsEachOnce && AreAllInTheirSquare && AreInGridOrder && AreInListOrder && AreCentresRight
            && (Next == TEST_COPY_COUNT));
        Memory_Free(Seen);

        // A second build of other copies replaces the first; an empty one leaves nothing.
        TEST_CHECK_SUCCESS(run, WorldInstanceCells_Build(&Cells, Copies, 3U, 42U, &Bounds));
        WorldInstanceCells_GetRecords(&Cells, &RecordCount);
        TEST_CHECK(run, RecordCount == 3U);
        TEST_CHECK_SUCCESS(run, WorldInstanceCells_Build(&Cells, NULL, 0U, 42U, &Bounds));
        TEST_CHECK(run, (WorldInstanceCells_GetCells(&Cells, &CellCount) == NULL) && (CellCount == 0U));
        TEST_CHECK_ERROR(run, WorldInstanceCells_Build(&Cells, NULL, 2U, 42U, &Bounds), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldInstanceCells_Build(&Cells, Copies, 2U, 42U, NULL), ErrorCode_IllegalArgument);
    }
    WorldInstanceCells_Deconstruct(&Cells);
    Memory_Free(Copies);
}

/* A record's matrix, relative to its cell's centre, composed back in double with the centre gives the copy's own
 * matrix: the position to within a float's step of the offset, the rotation and scale to float precision. The tint
 * and the random value travel with it. */
static void CellMatricesComposeBackToTheCopies(TestRun* run)
{
    PropertyInstanceEntry* Copies = Memory_Allocate(TEST_COPY_COUNT * sizeof(PropertyInstanceEntry));
    MakeCopies(Copies, TEST_COPY_COUNT, 11U);
    WorldInstanceCells Cells;
    WorldInstanceCells_Construct(&Cells);
    WorldInstanceBounds Bounds = MakeBounds();
    if (TEST_CHECK_SUCCESS(run, WorldInstanceCells_Build(&Cells, Copies, TEST_COPY_COUNT, 9U, &Bounds)))
    {
        size_t CellCount = 0U;
        size_t RecordCount = 0U;
        size_t SourceCount = 0U;
        const WorldInstanceCell* List = WorldInstanceCells_GetCells(&Cells, &CellCount);
        const WorldInstanceRecord* Records = WorldInstanceCells_GetRecords(&Cells, &RecordCount);
        const size_t* Sources = WorldInstanceCells_GetSources(&Cells, &SourceCount);
        double WorstPosition = 0.0;
        double WorstLinear = 0.0;
        bool AreExtrasKept = true;
        for (size_t CellIndex = 0U; CellIndex < CellCount; CellIndex++)
        {
            for (size_t Index = List[CellIndex].FirstRecord; Index < (List[CellIndex].FirstRecord
                + List[CellIndex].RecordCount); Index++)
            {
                const PropertyInstanceEntry* Copy = &Copies[Sources[Index]];
                Matrix4Double Expected = WorldTransform_Compose(Copy->Position, Copy->Rotation, Copy->Scale);
                Matrix Relative = WorldInstanceRecord_GetMatrix(&Records[Index]);
                Matrix4Double Back = GameMath_FromMatrix(Relative);
                Back.M[12] += List[CellIndex].Center.X;
                Back.M[13] += List[CellIndex].Center.Y;
                Back.M[14] += List[CellIndex].Center.Z;
                for (size_t Entry = 0U; Entry < 16U; Entry++)
                {
                    double Difference = fabs(Back.M[Entry] - Expected.M[Entry]);
                    if (Entry >= 12U)
                    {
                        WorstPosition = fmax(WorstPosition, Difference);
                    }
                    else
                    {
                        WorstLinear = fmax(WorstLinear, Difference / fmax(1.0, fabs(Expected.M[Entry])));
                    }
                }
                AreExtrasKept = AreExtrasKept && (Records[Index].Tint.r == Copy->Tint.r)
                    && (Records[Index].Tint.a == Copy->Tint.a)
                    && (Records[Index].Random == WorldInstancing_GetRandom(9U, Sources[Index]));
            }
        }
        // Offsets within a cell are under 64 m (half a square and the heights), where a float's step is 4 um.
        TEST_CHECK(run, (WorstPosition < 4e-6) && (WorstLinear < 1e-6) && AreExtrasKept);
        if (!TEST_CHECK(run, WorstPosition < 4e-6))
        {
            printf("      worst position %.3g m, worst linear %.3g\n", WorstPosition, WorstLinear);
        }
    }
    WorldInstanceCells_Deconstruct(&Cells);
    Memory_Free(Copies);
}

/* A cell's sphere holds every copy's sphere: points on the bounds' surface in 26 directions under each copy's matrix
 * (turned, scaled evenly and unevenly) lie inside it. */
static void CellBoundsHoldEveryCopy(TestRun* run)
{
    PropertyInstanceEntry* Copies = Memory_Allocate(TEST_COPY_COUNT * sizeof(PropertyInstanceEntry));
    MakeCopies(Copies, TEST_COPY_COUNT, 5U);
    WorldInstanceCells Cells;
    WorldInstanceCells_Construct(&Cells);
    WorldInstanceBounds Bounds = MakeBounds();
    if (TEST_CHECK_SUCCESS(run, WorldInstanceCells_Build(&Cells, Copies, TEST_COPY_COUNT, 3U, &Bounds)))
    {
        size_t CellCount = 0U;
        size_t SourceCount = 0U;
        const WorldInstanceCell* List = WorldInstanceCells_GetCells(&Cells, &CellCount);
        const size_t* Sources = WorldInstanceCells_GetSources(&Cells, &SourceCount);
        bool IsHeld = true;
        double Tightest = DBL_MAX;
        for (size_t CellIndex = 0U; CellIndex < CellCount; CellIndex++)
        {
            const WorldInstanceCell* Cell = &List[CellIndex];
            double Farthest = 0.0;
            for (size_t Index = Cell->FirstRecord; Index < (Cell->FirstRecord + Cell->RecordCount); Index++)
            {
                const PropertyInstanceEntry* Copy = &Copies[Sources[Index]];
                Matrix4Double Composed = WorldTransform_Compose(Copy->Position, Copy->Rotation, Copy->Scale);
                for (int X = -1; X <= 1; X++)
                {
                    for (int Y = -1; Y <= 1; Y++)
                    {
                        for (int Z = -1; Z <= 1; Z++)
                        {
                            if ((X == 0) && (Y == 0) && (Z == 0))
                            {
                                continue;
                            }
                            Vector3Double Direction = Vector3Double_NormalizeOr(Vector3Double_Create(X, Y, Z),
                                Vector3Double_Create(1.0, 0.0, 0.0));
                            Vector3Double Point = Vector3Double_Add(Vector3Double_Create(Bounds.Center.x, Bounds.Center.y,
                                Bounds.Center.z), Vector3Double_Scale(Direction, (double)Bounds.Radius));
                            double Distance = Vector3Double_Distance(Matrix4Double_TransformPoint(&Composed, Point),
                                Cell->SphereCenter);
                            Farthest = fmax(Farthest, Distance);
                            IsHeld = IsHeld && (Distance <= (Cell->SphereRadius + 1e-9));
                        }
                    }
                }
            }
            Tightest = fmin(Tightest, Cell->SphereRadius - Farthest);
        }
        TEST_CHECK(run, IsHeld);
        // The sphere is not wildly loose either: some point of some copy comes within its uneven scale's slack.
        TEST_CHECK(run, Tightest < 2.0);
    }
    WorldInstanceCells_Deconstruct(&Cells);
    Memory_Free(Copies);
}

/* The random value is the same for the same object and copy, differs between neighbours, lies in [0, 1) and spreads. */
static void TheRandomValueIsStable(TestRun* run)
{
    TEST_CHECK(run, WorldInstancing_GetRandom(7U, 3U) == WorldInstancing_GetRandom(7U, 3U));
    size_t Low = 0U;
    size_t Same = 0U;
    bool IsInRange = true;
    for (size_t Index = 0U; Index < 4096U; Index++)
    {
        float Value = WorldInstancing_GetRandom(51U, Index);
        IsInRange = IsInRange && (Value >= 0.0f) && (Value < 1.0f);
        Low += (Value < 0.5f) ? 1U : 0U;
        Same += (Value == WorldInstancing_GetRandom(52U, Index)) ? 1U : 0U;
    }
    TEST_CHECK(run, IsInRange && (Low > 1900U) && (Low < 2200U) && (Same < 4U));
}


// Tests: fades and bands.
/* The bands lie around each switch distance by the hysteresis, stop at the middle between two switch distances, follow
 * the lod_distance_scale (0: the last LOD everywhere), and a forced LOD is clamped. */
static void BandsLieAroundTheSwitchDistances(TestRun* run)
{
    WorldModelLodSettings Settings = MakeSettings(0.2f);
    WorldInstanceLodBands Bands;
    WorldInstancing_GetLodBands(&Settings, 1.0f, &Bands);
    TEST_CHECK(run, (Bands.LodCount == 3U) && !Bands.IsForced && (Bands.Start[0] == 8.0f) && (Bands.End[0] == 12.0f)
        && (Bands.Start[1] == 24.0f) && (Bands.End[1] == 36.0f));
    WorldInstancing_GetLodBands(&Settings, 2.0f, &Bands);
    TEST_CHECK(run, (Bands.Start[0] == 16.0f) && (Bands.End[1] == 72.0f));
    WorldInstancing_GetLodBands(&Settings, NAN, &Bands);
    TEST_CHECK(run, Bands.Start[0] == 8.0f);

    // Wide bands stop at the middle: switches at 10 and 12 with a hysteresis of 0.5 meet at 11.
    WorldModelLodSettings Close = { .LodCount = 3U, .SwitchDistances = { 10.0f, 12.0f, 40.0f }, .Hysteresis = 0.5f };
    WorldInstancing_GetLodBands(&Close, 1.0f, &Bands);
    TEST_CHECK(run, (Bands.Start[0] == 5.0f) && (Bands.End[0] == 11.0f) && (Bands.Start[1] == 11.0f)
        && (Bands.End[1] == 18.0f));

    // No hysteresis (or none that counts): empty bands, a hard switch.
    WorldModelLodSettings Hard = MakeSettings(1.5f);
    WorldInstancing_GetLodBands(&Hard, 1.0f, &Bands);
    TEST_CHECK(run, (Bands.Start[0] == 10.0f) && (Bands.End[0] == 10.0f));

    // A scale of 0 gives the last LOD at any distance, also with a switch distance that overflowed to infinity (a huge
    // lod_distance_multiplier): every band lies at 0, never NaN, and every cell can show the last LOD; a forced LOD is
    // clamped to the last.
    WorldInstanceView View = MakeView(&Settings, 1000.0f);
    WorldInstancing_GetLodBands(&Settings, 0.0f, &View.Bands);
    TEST_CHECK(run, (EvaluateAt(&View, 0.5f).Lod == 2U) && !EvaluateAt(&View, 0.5f).IsInBand);
    WorldModelLodSettings Overflowed = Settings;
    Overflowed.SwitchDistances[0] = INFINITY;
    WorldInstancing_GetLodBands(&Overflowed, 0.0f, &View.Bands);
    TEST_CHECK(run, (View.Bands.Start[0] == 0.0f) && (View.Bands.End[0] == 0.0f) && (View.Bands.End[1] == 0.0f)
        && (EvaluateAt(&View, 0.5f).Lod == 2U) && (EvaluateAt(&View, 70.0f).Lod == 2U)
        && ((WorldInstancing_GetCellLodMask(&View.Bands, 3.0f, 9.0f) & 4U) != 0U));
    WorldInstancing_GetLodBands(&Overflowed, 1.0f, &View.Bands);
    TEST_CHECK(run, (EvaluateAt(&View, 500.0f).Lod == 0U)
        && ((WorldInstancing_GetCellLodMask(&View.Bands, 3.0f, 9.0f) & 1U) != 0U));
    Settings.IsLodForced = true;
    Settings.ForcedLod = 7U;
    WorldInstancing_GetLodBands(&Settings, 1.0f, &View.Bands);
    TEST_CHECK(run, View.Bands.IsForced && (View.Bands.ForcedLod == 2U) && (EvaluateAt(&View, 0.5f).Lod == 2U)
        && (EvaluateAt(&View, 500.0f).Lod == 2U));
    Settings.ForcedLod = 1U;
    WorldInstancing_GetLodBands(&Settings, 1.0f, &View.Bands);
    TEST_CHECK(run, (EvaluateAt(&View, 0.5f).Lod == 1U) && (EvaluateAt(&View, 50.0f).Lod == 1U));

    // One LOD, or none: no bands.
    WorldModelLodSettings Single = { .LodCount = 0U };
    WorldInstancing_GetLodBands(&Single, 1.0f, &Bands);
    TEST_CHECK(run, (Bands.LodCount == 1U) && !Bands.IsForced);
}

/* Across a band the two LODs keep exactly complementary pixels at every distance; the incoming share rises steadily
 * from 0 to 1, so the picture at a band's edges is the picture beside them (continuity); outside bands one LOD draws
 * every pixel. */
static void BandsAreComplementaryAndContinuous(TestRun* run)
{
    WorldModelLodSettings Settings = MakeSettings(0.2f);
    WorldInstanceView View = MakeView(&Settings, 1000.0f);
    bool IsComplementary = true;
    bool IsSteady = true;
    float LastProgress = 0.0f;
    size_t LastLod = 0U;
    size_t Changes = 0U;
    for (size_t Step = 0U; Step < (60U * TEST_STEPS_PER_METRE); Step++)
    {
        float Distance = 0.01f + ((float)Step / (float)TEST_STEPS_PER_METRE);
        WorldInstanceState State = EvaluateAt(&View, Distance);
        size_t Lods[WORLD_VISIBILITY_DITHER_LEVELS];
        GetPatternLods(&State, 3U, Lods);
        size_t IncomingPixels = 0U;
        for (uint32_t Cell = 0U; Cell < WORLD_VISIBILITY_DITHER_LEVELS; Cell++)
        {
            IsComplementary = IsComplementary && (Lods[Cell] < 3U);
            IncomingPixels += (State.IsInBand && (Lods[Cell] == State.IncomingLod)) ? 1U : 0U;
        }
        if (State.IsInBand)
        {
            IsSteady = IsSteady && ((State.Lod != LastLod) || (State.Progress >= LastProgress));
            IsComplementary = IsComplementary
                && (IncomingPixels == (size_t)floorf((State.Progress * (float)WORLD_VISIBILITY_DITHER_LEVELS) + 0.5f));
            LastProgress = State.Progress;
        }
        else
        {
            LastProgress = 0.0f;
        }
        // The shown LOD (the solid one) changes only twice on the way out, in the middle of each band.
        size_t Solid = WorldInstancing_GetSolidLod(&State);
        Changes += (Solid != LastLod) ? 1U : 0U;
        LastLod = Solid;
    }
    TEST_CHECK(run, IsComplementary && IsSteady && (Changes == 2U));

    // Continuity at the edges: just inside a band's start the incoming LOD has almost no pixels; just inside its end the
    // outgoing one has almost none.
    WorldInstanceState Early = EvaluateAt(&View, 8.001f);
    WorldInstanceState Late = EvaluateAt(&View, 11.999f);
    WorldInstanceState Before = EvaluateAt(&View, 7.999f);
    WorldInstanceState After = EvaluateAt(&View, 12.001f);
    TEST_CHECK(run, Early.IsInBand && (Early.Progress < 0.001f) && Late.IsInBand && (Late.Progress > 0.999f)
        && !Before.IsInBand && (Before.Lod == 0U) && !After.IsInBand && (After.Lod == 1U));
    TEST_CHECK(run, (WorldInstancing_GetSolidLod(&Early) == 0U) && (WorldInstancing_GetSolidLod(&Late) == 1U));
    WorldInstanceState Middle = EvaluateAt(&View, 10.0f);
    TEST_CHECK(run, Middle.IsInBand && (fabsf(Middle.Progress - 0.5f) < 1e-5f) && (Middle.IncomingLod == 1U)
        && (WorldInstancing_GetSolidLod(&Middle) == 1U));
}

/* The zoom factor and the switch distances' multipliers move the bands; the copy is measured to its sphere's centre for
 * its LOD and to its nearest point for its fade. */
static void TheZoomAndTheSphereMeasureTheCopy(TestRun* run)
{
    WorldModelLodSettings Settings = MakeSettings(0.2f);
    WorldInstanceView View = MakeView(&Settings, 1000.0f);
    TEST_CHECK(run, EvaluateAt(&View, 15.0f).Lod == 1U);
    View.ZoomFactor = 0.5f;
    TEST_CHECK(run, (EvaluateAt(&View, 15.0f).Lod == 0U) && (EvaluateAt(&View, 15.0f).IsInBand == false));
    View.ZoomFactor = 1.0f;
    Settings.SwitchDistances[0] *= 2.0f;
    Settings.SwitchDistances[1] *= 2.0f;
    WorldInstancing_GetLodBands(&Settings, 1.0f, &View.Bands);
    TEST_CHECK(run, EvaluateAt(&View, 15.0f).Lod == 0U);

    // A sphere 3 m up and 2 m wide, on a copy scaled 2 on its longest axis: the LOD by the centre's distance, the fade
    // by its nearest point's.
    WorldModelLodSettings Plain = MakeSettings(0.0f);
    WorldInstanceView Tree = MakeView(&Plain, 20.0f);
    Tree.BoundsCenter = (Vector3){ 0.0f, 3.0f, 0.0f };
    Tree.BoundsRadius = 1.0f;
    PropertyInstanceEntry Copy = PropertyInstanceEntry_CreateDefault();
    Copy.Scale = (Vector3){ 1.0f, 2.0f, 1.0f };
    Matrix4Double Composed = WorldTransform_Compose(Copy.Position, Copy.Rotation, Copy.Scale);
    WorldInstanceRecord Record = { .Tint = Copy.Tint };
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        for (size_t Column = 0U; Column < 4U; Column++)
        {
            Record.Rows[(Row * 4U) + Column] = (float)Composed.M[(Column * 4U) + Row];
        }
    }
    // The camera 8 m in front of the copy's base and 6 m below its sphere's centre (6 m up from the base): 10 m away.
    Matrix Cell = MatrixTranslate(0.0f, 0.0f, 8.0f);
    WorldInstanceState State = WorldInstancing_Evaluate(&Tree, &Cell, &Record);
    TEST_CHECK(run, (State.Lod == 1U) && (State.Fade == 0.0f));
    Matrix Far = MatrixTranslate(0.0f, 0.0f, 19.0f);
    // 19.92 m to the centre, 17.92 m to the nearest point: inside the last 3 m of the 20 m draw distance.
    State = WorldInstancing_Evaluate(&Tree, &Far, &Record);
    float Expected = WorldVisibility_GetDistanceFade(sqrtf((19.0f * 19.0f) + 36.0f) - 2.0f, 20.0f);
    TEST_CHECK(run, (State.Fade > 0.0f) && (fabsf(State.Fade - Expected) < 1e-5f));
}

/* A copy's draw-distance fade is T07's rule exactly (the models' and sprites'), its opacity the object's times its own,
 * and the larger fade counts; nothing reaches a draw distance of infinity; none above 0 fades everything out. */
static void FadesFollowTheModelsRule(TestRun* run)
{
    const float DrawDistances[] = { 2.0f, 10.0f, 45.5f, 600.0f };
    bool IsSame = true;
    for (size_t Index = 0U; Index < (sizeof(DrawDistances) / sizeof(DrawDistances[0])); Index++)
    {
        WorldInstanceView View = { .ZoomFactor = 1.0f, .ObjectOpacity = 1.0f };
        WorldInstancing_SetDrawDistance(&View, DrawDistances[Index]);
        for (size_t Step = 0U; Step < 4000U; Step++)
        {
            float Distance = DrawDistances[Index] * 1.2f * ((float)Step / 4000.0f);
            IsSame = IsSame && (WorldInstancing_GetDistanceFade(&View, Distance)
                == WorldVisibility_GetDistanceFade(Distance, DrawDistances[Index]));
        }
        IsSame = IsSame && (WorldInstancing_GetDistanceFade(&View, NAN) == 1.0f);
    }
    TEST_CHECK(run, IsSame);
    WorldInstanceView Endless = { .ZoomFactor = 1.0f, .ObjectOpacity = 1.0f };
    WorldInstancing_SetDrawDistance(&Endless, INFINITY);
    TEST_CHECK(run, (WorldInstancing_GetDistanceFade(&Endless, 1e30f) == 0.0f));
    WorldInstancing_SetDrawDistance(&Endless, 0.0f);
    TEST_CHECK(run, (WorldInstancing_GetDistanceFade(&Endless, 0.0f) == 1.0f) && (Endless.FadeBand > 0.0f));

    // Opacity: the object's times the copy's.
    WorldModelLodSettings Settings = MakeSettings(0.2f);
    WorldInstanceView View = MakeView(&Settings, 1000.0f);
    View.ObjectOpacity = 0.5f;
    WorldInstanceRecord Record = MakePlainRecord();
    Record.Tint.a = 128U;
    Matrix Cell = MatrixTranslate(0.0f, 0.0f, 5.0f);
    WorldInstanceState State = WorldInstancing_Evaluate(&View, &Cell, &Record);
    TEST_CHECK(run, fabsf(State.Fade - (1.0f - (0.5f * (128.0f / 255.0f)))) < 1e-6f);
    View.ObjectOpacity = 0.0f;
    TEST_CHECK(run, WorldInstancing_Evaluate(&View, &Cell, &Record).Fade == 1.0f);
}

/* The passes' rules: the lit pass draws both LODs of a band with the copy's fade; the G-buffer the solid LOD, solid,
 * while the fade is below half; the shadows the solid LOD with the copy's fade alone; the unlit fallback the solid LOD;
 * a copy faded out entirely is drawn by none. */
static void ThePassesDrawByTheirRules(TestRun* run)
{
    WorldInstanceState Band = { .Fade = 0.25f, .Lod = 1U, .IncomingLod = 2U, .Progress = 0.75f, .IsInBand = true };
    WorldInstanceDraw Outgoing = WorldInstancing_GetDraw(&Band, WorldInstancePass_Lit, 1U);
    WorldInstanceDraw Incoming = WorldInstancing_GetDraw(&Band, WorldInstancePass_Lit, 2U);
    TEST_CHECK(run, Outgoing.IsDrawn && !Outgoing.IsIncoming && (Outgoing.Progress == 0.75f) && (Outgoing.Fade == 0.25f)
        && Incoming.IsDrawn && Incoming.IsIncoming && (Incoming.Progress == 0.75f) && (Incoming.Fade == 0.25f)
        && !WorldInstancing_GetDraw(&Band, WorldInstancePass_Lit, 0U).IsDrawn);
    WorldInstanceDraw GBuffer = WorldInstancing_GetDraw(&Band, WorldInstancePass_GBuffer, 2U);
    TEST_CHECK(run, GBuffer.IsDrawn && (GBuffer.Fade == 0.0f) && (GBuffer.Progress == 0.0f) && !GBuffer.IsIncoming
        && !WorldInstancing_GetDraw(&Band, WorldInstancePass_GBuffer, 1U).IsDrawn);
    WorldInstanceDraw Shadow = WorldInstancing_GetDraw(&Band, WorldInstancePass_Shadow, 2U);
    TEST_CHECK(run, Shadow.IsDrawn && (Shadow.Fade == 0.25f) && (Shadow.Progress == 0.0f) && !Shadow.IsIncoming
        && !WorldInstancing_GetDraw(&Band, WorldInstancePass_Shadow, 1U).IsDrawn);
    TEST_CHECK(run, WorldInstancing_GetDraw(&Band, WorldInstancePass_Unlit, 2U).IsDrawn
        && !WorldInstancing_GetDraw(&Band, WorldInstancePass_Unlit, 1U).IsDrawn);
    // Below half the band, the outgoing LOD is the solid one.
    Band.Progress = 0.4f;
    TEST_CHECK(run, WorldInstancing_GetDraw(&Band, WorldInstancePass_Shadow, 1U).IsDrawn
        && !WorldInstancing_GetDraw(&Band, WorldInstancePass_Shadow, 2U).IsDrawn);
    // From half its fade on, a copy leaves the G-buffer; at a fade of 1 it leaves every pass.
    Band.Fade = 0.5f;
    TEST_CHECK(run, !WorldInstancing_GetDraw(&Band, WorldInstancePass_GBuffer, 1U).IsDrawn
        && WorldInstancing_GetDraw(&Band, WorldInstancePass_Shadow, 1U).IsDrawn);
    Band.Fade = 1.0f;
    for (int Pass = 0; Pass <= (int)WorldInstancePass_Unlit; Pass++)
    {
        for (size_t Lod = 0U; Lod < 3U; Lod++)
        {
            TEST_CHECK(run, !WorldInstancing_GetDraw(&Band, (WorldInstancePass)Pass, Lod).IsDrawn);
        }
    }
    // Outside a band the lit pass draws one LOD with no part of a cross-fade.
    WorldInstanceState Alone = { .Fade = 0.0f, .Lod = 0U, .IncomingLod = 0U, .Progress = 0.0f, .IsInBand = false };
    WorldInstanceDraw Single = WorldInstancing_GetDraw(&Alone, WorldInstancePass_Lit, 0U);
    TEST_CHECK(run, Single.IsDrawn && (Single.Progress == 0.0f) && !Single.IsIncoming
        && !WorldInstancing_GetDraw(&Alone, WorldInstancePass_Lit, 1U).IsDrawn);
}

/* A cell's LOD mask holds every LOD any copy whose LOD distance lies in its range shows (both of a band); a forced LOD
 * is its only bit; a NaN range gives every LOD. */
static void CellLodMasksHoldTheirCopiesLods(TestRun* run)
{
    WorldModelLodSettings Settings = MakeSettings(0.2f);
    WorldInstanceView View = MakeView(&Settings, 1000.0f);
    uint32_t State = 99U;
    bool AreHeld = true;
    bool IsTightSomewhere = false;
    for (int Round = 0; Round < 2000; Round++)
    {
        float Nearest = (float)RandomBetween(&State, 0.0, 80.0);
        float Farthest = Nearest + (float)RandomBetween(&State, 0.0, 30.0);
        uint32_t Mask = WorldInstancing_GetCellLodMask(&View.Bands, Nearest, Farthest);
        IsTightSomewhere = IsTightSomewhere || (Mask != 7U);
        for (int Sample = 0; Sample <= 20; Sample++)
        {
            float Distance = Nearest + ((Farthest - Nearest) * ((float)Sample / 20.0f));
            WorldInstanceState Copy = EvaluateAt(&View, Distance);
            AreHeld = AreHeld && ((Mask & (1U << Copy.Lod)) != 0U)
                && (!Copy.IsInBand || ((Mask & (1U << Copy.IncomingLod)) != 0U));
        }
    }
    TEST_CHECK(run, AreHeld && IsTightSomewhere);
    TEST_CHECK(run, (WorldInstancing_GetCellLodMask(&View.Bands, 0.0f, 5.0f) == 1U)
        && (WorldInstancing_GetCellLodMask(&View.Bands, 13.0f, 20.0f) == 2U)
        && (WorldInstancing_GetCellLodMask(&View.Bands, 40.0f, 400.0f) == 4U)
        && (WorldInstancing_GetCellLodMask(&View.Bands, 9.0f, 9.5f) == 3U)
        && (WorldInstancing_GetCellLodMask(&View.Bands, NAN, 3.0f) == 7U));
    // The range is widened for the GPU's rounding of a copy's distance: a cell ending just short of the first band (8 m)
    // or starting just past its end (12 m) keeps the band's other LOD too. Infinitely far shows the last LOD.
    TEST_CHECK(run, (WorldInstancing_GetCellLodMask(&View.Bands, 7.0f, 7.9995f) == 3U)
        && (WorldInstancing_GetCellLodMask(&View.Bands, 12.0005f, 13.0f) == 3U)
        && (WorldInstancing_GetCellLodMask(&View.Bands, 7.0f, 7.99f) == 1U)
        && (WorldInstancing_GetCellLodMask(&View.Bands, INFINITY, INFINITY) == 4U));
    Settings.IsLodForced = true;
    Settings.ForcedLod = 1U;
    WorldInstancing_GetLodBands(&Settings, 1.0f, &View.Bands);
    TEST_CHECK(run, WorldInstancing_GetCellLodMask(&View.Bands, 0.0f, 500.0f) == 2U);
}


// Public functions.
void WorldInstancingTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "every_copy_lands_in_exactly_one_cell", EveryCopyLandsInExactlyOneCell },
        { "cell_matrices_compose_back_to_the_copies", CellMatricesComposeBackToTheCopies },
        { "cell_bounds_hold_every_copy", CellBoundsHoldEveryCopy },
        { "the_random_value_is_stable", TheRandomValueIsStable },
        { "bands_lie_around_the_switch_distances", BandsLieAroundTheSwitchDistances },
        { "bands_are_complementary_and_continuous", BandsAreComplementaryAndContinuous },
        { "the_zoom_and_the_sphere_measure_the_copy", TheZoomAndTheSphereMeasureTheCopy },
        { "fades_follow_the_models_rule", FadesFollowTheModelsRule },
        { "the_passes_draw_by_their_rules", ThePassesDrawByTheirRules },
        { "cell_lod_masks_hold_their_copies_lods", CellLodMasksHoldTheirCopiesLods },
    };
    TestRun_RunSuite(run, "WorldInstancing", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
