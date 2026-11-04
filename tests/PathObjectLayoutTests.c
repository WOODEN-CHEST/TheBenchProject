#include "TestSuites.h"
#include "PathCurve.h"
#include "PathObject.h"
#include "PathObjectLayout.h"
#include "wr/WRMemory.h"
#include "wr/WRVectorMath.h"
#include <math.h>
#include <stdio.h>


// Macros.
#define NEAR(a, b, tolerance) (fabs((double)(a) - (double)(b)) <= (double)(tolerance))
/** The tolerance of a straight line's distances, in metres. */
#define TOLERANCE 1.0e-3


// Types.
/* A curve and its nodes, a layout and what it is laid out from. */
typedef struct FixtureStruct
{
    PathCurve Curve;
    PathNode Nodes[64];
    size_t NodeCount;
    PathObjectLayout Layout;
    PathObjectLayoutInput Input;
    PathObjectSettings Settings;
    PathObjectLayoutModel Models[PathObjectSlot_Count][4];
} Fixture;


// Static functions: helpers.
static void Fixture_Begin(Fixture* self)
{
    Memory_Zero(self, sizeof(*self));
    PathCurve_Construct(&self->Curve);
    PathObjectLayout_Construct(&self->Layout);
    self->Settings = PathObject_ResolveSettings(NULL, NULL);
    self->Input.Curve = &self->Curve;
    self->Input.Nodes = self->Nodes;
    self->Input.Settings = &self->Settings;
    self->Input.MaxPieces = 100000U;
    for (uint32_t Slot = 0U; Slot < (uint32_t)PathObjectSlot_Count; Slot++)
    {
        self->Input.Slots[Slot].Models = self->Models[Slot];
        self->Input.Slots[Slot].IsRigid = true;
        self->Input.Slots[Slot].Stretches = (Slot == (uint32_t)PathObjectSlot_Middle)
            || (Slot == (uint32_t)PathObjectSlot_Single);
    }
}

static void Fixture_End(Fixture* self)
{
    PathCurve_Deconstruct(&self->Curve);
    PathObjectLayout_Deconstruct(&self->Layout);
}

/* A slot of one model of a length, in use. */
static void SetSlot(Fixture* self, PathObjectSlot slot, float length)
{
    self->Models[slot][0] = (PathObjectLayoutModel){ .Length = length, .Weight = 1.0f };
    self->Input.Slots[slot].ModelCount = 1U;
    self->Input.Slots[slot].IsUsed = true;
}

static void AddNode(Fixture* self, double x, double y, double z, PathNodeMode mode)
{
    PathNode Node = PathNode_CreateDefault();
    Node.Position = Vector3Double_Create(x, y, z);
    Node.Mode = mode;
    self->Nodes[self->NodeCount++] = Node;
}

static bool BuildCurve(TestRun* run, Fixture* self, bool isClosed)
{
    self->Input.NodeCount = self->NodeCount;
    return TEST_CHECK_SUCCESS(run, PathCurve_Build(&self->Curve, self->Nodes, self->NodeCount, isClosed, PathUpMode_Upright));
}

/* A straight line of a length, along X. */
static bool BuildLine(TestRun* run, Fixture* self, double length)
{
    self->NodeCount = 0U;
    AddNode(self, 0.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(self, length, 0.0, 0.0, PathNodeMode_Corner);
    return BuildCurve(run, self, false);
}

static const PathObjectPiece* Lay(TestRun* run, Fixture* self, size_t* outCount)
{
    *outCount = 0U;
    if (!TEST_CHECK_SUCCESS(run, PathObjectLayout_Build(&self->Layout, &self->Input)))
    {
        return NULL;
    }
    return PathObjectLayout_GetPieces(&self->Layout, outCount);
}

/* The pieces of one slot only, in order: their starts into an array. */
static size_t StartsOf(const PathObjectPiece* pieces, size_t count, PathObjectSlot slot, double* outStarts, size_t capacity)
{
    size_t Found = 0U;
    for (size_t Index = 0U; Index < count; Index++)
    {
        if ((pieces[Index].Slot == slot) && (Found < capacity))
        {
            outStarts[Found++] = pieces[Index].Start;
        }
    }
    return Found;
}


// Tests: the stretch fit.
static void StretchScalesAWholeNumberOfMiddlesToFill(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    struct { double Length; PathObjectRounding Rounding; size_t Expected; } Cases[] =
    {
        { 10.0, PathObjectRounding_Nearest, 5U }, { 10.9, PathObjectRounding_Nearest, 5U },
        { 11.1, PathObjectRounding_Nearest, 6U }, { 10.9, PathObjectRounding_Up, 6U },
        { 10.9, PathObjectRounding_Down, 5U }, { 11.9, PathObjectRounding_Down, 5U }, { 10.0, PathObjectRounding_Up, 5U },
    };
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        F.Settings.Rounding = Cases[Index].Rounding;
        if (!BuildLine(run, &F, Cases[Index].Length))
        {
            break;
        }
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, Count == Cases[Index].Expected);
        double Each = Cases[Index].Length / (double)Cases[Index].Expected;
        for (size_t Piece = 0U; (Piece < Count) && (Pieces != NULL); Piece++)
        {
            TEST_CHECK(run, NEAR(Pieces[Piece].Start, Each * (double)Piece, TOLERANCE)
                && NEAR(Pieces[Piece].End, Each * (double)(Piece + 1U), TOLERANCE));
            TEST_CHECK(run, (Pieces[Piece].Slot == PathObjectSlot_Middle) && (Pieces[Piece].Index == Piece)
                && (Pieces[Piece].Nominal == 2.0f) && !Pieces[Piece].IsJoint);
        }
        // The last piece lands exactly on the end.
        TEST_CHECK(run, (Count > 0U) && (Pieces[Count - 1U].End == Cases[Index].Length));
    }
    Fixture_End(&F);
}

static void CapsAndSeparatorsLieEndToEndByTheirLengths(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    SetSlot(&F, PathObjectSlot_StartCap, 0.5f);
    SetSlot(&F, PathObjectSlot_EndCap, 0.25f);
    SetSlot(&F, PathObjectSlot_Separator, 0.1f);
    // 3 middles, 2 separators and the caps: 0.5 + 6 + 0.2 + 0.25 = 6.95; 0.05 over: the middles stretch by 6.05 / 6.
    if (BuildLine(run, &F, 7.0))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, Count == 7U);
        if (Count == 7U)
        {
            double Middle = 2.0 * (6.05 / 6.0);
            const PathObjectSlot Slots[7] = { PathObjectSlot_StartCap, PathObjectSlot_Middle, PathObjectSlot_Separator,
                PathObjectSlot_Middle, PathObjectSlot_Separator, PathObjectSlot_Middle, PathObjectSlot_EndCap };
            const double Lengths[7] = { 0.5, Middle, 0.1, Middle, 0.1, Middle, 0.25 };
            double Here = 0.0;
            for (size_t Index = 0U; Index < 7U; Index++)
            {
                TEST_CHECK(run, (Pieces[Index].Slot == Slots[Index]) && NEAR(Pieces[Index].Start, Here, TOLERANCE)
                    && NEAR(Pieces[Index].End, Here + Lengths[Index], TOLERANCE));
                Here += Lengths[Index];
            }
        }
    }
    // A separator every two middles, and none after the last.
    F.Settings.SeparatorEvery = 2U;
    if (BuildLine(run, &F, 20.0))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        size_t Middles = 0U;
        for (size_t Index = 0U; Index < Count; Index++)
        {
            Middles += (Pieces[Index].Slot == PathObjectSlot_Middle) ? 1U : 0U;
            if (Pieces[Index].Slot == PathObjectSlot_Separator)
            {
                TEST_CHECK(run, (Middles % 2U) == 0U);
            }
        }
        TEST_CHECK(run, (Middles >= 8U) && (Pieces[Count - 2U].Slot == PathObjectSlot_Middle));
    }
    Fixture_End(&F);
}

static void ACountIsExactAsFarAsItFits(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    F.Settings.UsesCount = true;
    F.Settings.Count = 3U;
    if (BuildLine(run, &F, 10.0))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 3U) && NEAR(Pieces[1].Start, 10.0 / 3.0, TOLERANCE));
    }
    // Twenty panels cannot fit in 10 m above the minimum stretch: the count comes down until they do.
    F.Settings.Count = 40U;
    F.Settings.MinimumStretch = 0.5f;
    if (BuildLine(run, &F, 10.0))
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        TEST_CHECK(run, Count == 10U);
    }
    Fixture_End(&F);
}

/* A count of many more than can ever fit comes down at once to what fits, for the fits that stretch all their pieces and those
 * that resize their ends alike. */
static void AHugeCountComesDownToWhatFits(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    F.Settings.UsesCount = true;
    F.Settings.Count = PATH_OBJECT_MAX_COUNT;
    F.Input.MaxPieces = PATH_OBJECT_MAX_COUNT;
    if (BuildLine(run, &F, 10.0))
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 20U) && !PathObjectLayout_WasLimited(&F.Layout));
        // At a smaller smallest size: more, and more than a try per piece could come down to.
        F.Settings.MinimumStretch = 0.1f;
        Lay(run, &F, &Count);
        TEST_CHECK(run, Count == 50U);
        F.Settings.MinimumStretch = PATH_OBJECT_DEFAULT_MINIMUM_STRETCH;
        F.Settings.Fit = PathObjectFit_ResizeEnds;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count >= 5U) && (Count <= 6U) && (Pieces != NULL) && NEAR(Pieces[Count - 1U].End, 10.0, TOLERANCE));
        F.Settings.ResizeMethod = PathObjectResizeMethod_Cut;
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count >= 5U) && (Count <= 6U) && (Pieces != NULL) && NEAR(Pieces[Count - 1U].End, 10.0, TOLERANCE));
    }
    Fixture_End(&F);
}

/* A path whose chords walk much farther than their lengths say overruns by many pieces: the count comes down by as many in one
 * go. On this one every hairpin outgrows a chord at the same stretch, so no x lands the count the arcs say: a lower count must.
 */
static void ALongWindingPathStillFills(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 3.0f);
    for (int Node = 0; Node < 64; Node++)
    {
        AddNode(&F, 4.0 * (double)Node, 0.0, ((Node % 2) == 0) ? 5.0 : -5.0, PathNodeMode_Smooth);
    }
    if (BuildCurve(run, &F, false))
    {
        const PathObjectFit Fits[3] = { PathObjectFit_Fixed, PathObjectFit_ResizeEnds, PathObjectFit_Stretch };
        const float Lengths[8] = { 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f };
        for (size_t Fit = 0U; Fit < 3U; Fit++)
        {
            F.Settings.Fit = Fits[Fit];
            for (size_t Length = 0U; Length < 8U; Length++)
            {
                F.Models[PathObjectSlot_Middle][0].Length = Lengths[Length];
                size_t Count = 0U;
                const PathObjectPiece* Pieces = Lay(run, &F, &Count);
                size_t SpanCount = 0U;
                const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
                TEST_CHECK(run, (Count > 50U) && (SpanCount == 1U));
                if (Count > 50U)
                {
                    // It holds as many as fit: less than a piece and its arc stays free at the end, and no piece lies past it.
                    TEST_CHECK(run, (Spans[0].Length - Pieces[Count - 1U].End) < (1.4 * (double)Lengths[Length]));
                    bool AreAllLong = true;
                    for (size_t Index = 0U; Index < Count; Index++)
                    {
                        AreAllLong = AreAllLong && ((Pieces[Index].End - Pieces[Index].Start) > (0.2 * (double)Lengths[Length]));
                    }
                    TEST_CHECK(run, AreAllLong);
                }
            }
        }
    }
    Fixture_End(&F);
}

/* Where a walk jumps (chords outgrowing a path's bends), no x lands exactly: what ends short by less than a piece is kept, and
 * what cannot land comes down by what it overran. The counts are the most that fit each of these paths. */
static void AWalkThatJumpsStillLands(TestRun* run)
{
    typedef struct CaseStruct
    {
        double Across;
        double Along;
        float Length;
        PathObjectFit Fit;
        size_t Pieces;
        double Gap;
    } Case;
    static const Case Cases[5] =
    {
        { 1.0, 4.0, 4.0f, PathObjectFit_Fixed, 63U, 1.0 },
        { 1.0, 3.0, 3.0f, PathObjectFit_Fixed, 63U, 1.0 },
        { 2.0, 1.0, 2.0f, PathObjectFit_Stretch, 126U, 0.1 },
        { 2.0, 1.0, 1.5f, PathObjectFit_Stretch, 126U, 0.1 },
        { 1.0, 1.0, 2.5f, PathObjectFit_Stretch, 28U, 0.1 },
    };
    for (size_t Index = 0U; Index < 5U; Index++)
    {
        const Case* Held = &Cases[Index];
        Fixture F;
        Fixture_Begin(&F);
        SetSlot(&F, PathObjectSlot_Middle, Held->Length);
        F.Settings.Fit = Held->Fit;
        F.Settings.Align = PathObjectAlign_Evenly;
        for (int Node = 0; Node < 64; Node++)
        {
            AddNode(&F, Held->Along * (double)Node, 0.0, ((Node % 2) == 0) ? Held->Across : -Held->Across, PathNodeMode_Smooth);
        }
        if (BuildCurve(run, &F, false))
        {
            size_t Count = 0U;
            const PathObjectPiece* Pieces = Lay(run, &F, &Count);
            size_t SpanCount = 0U;
            const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
            TEST_CHECK(run, (Count == Held->Pieces) && (SpanCount == 1U));
            if ((Count > 0U) && (SpanCount == 1U))
            {
                TEST_CHECK(run, (Spans[0].Length - Pieces[Count - 1U].End) < Held->Gap);
                bool AreAllLong = true;
                for (size_t Piece = 0U; Piece < Count; Piece++)
                {
                    AreAllLong = AreAllLong && ((Pieces[Piece].End - Pieces[Piece].Start) > (0.2 * (double)Held->Length));
                }
                TEST_CHECK(run, AreAllLong);
            }
        }
        Fixture_End(&F);
    }
}

/* A hostile count with long separators fits nothing in any span, try after try, and emits next to nothing: the walks themselves
 * have a limit, or a path of many sections would take minutes. */
static void AHostileCountStopsWalking(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    SetSlot(&F, PathObjectSlot_Separator, 1000.0f);
    SetSlot(&F, PathObjectSlot_Corner, 0.0f); // so the corners leave the spans their length
    F.Settings.UsesCount = true;
    F.Settings.Count = PATH_OBJECT_MAX_COUNT;
    F.Settings.MinimumStretch = 1.0e-30f;
    F.Input.MaxPieces = PATH_OBJECT_MAX_COUNT;
    for (int Node = 0; Node < 64; Node++)
    {
        AddNode(&F, 10.0 * (double)Node, 0.0, ((Node % 2) == 0) ? 3.0 : -3.0, PathNodeMode_Corner);
    }
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        TEST_CHECK(run, PathObjectLayout_WasLimited(&F.Layout));
        TEST_CHECK(run, F.Layout._walkedItems < (PATH_OBJECT_LAYOUT_MAX_WALKED_ITEMS + (3U * (size_t)PATH_OBJECT_MAX_COUNT)));
    }
    Fixture_End(&F);
}

/* A span knows the sections that meet it at its ends: a run's inside and a closed run's closing node, not its open ends. */
static void ASpanKnowsTheSectionsAtItsEnds(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 1.0f);
    AddNode(&F, 0.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 10.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 10.0, 0.0, 10.0, PathNodeMode_Corner);
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        size_t SpanCount = 0U;
        const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        TEST_CHECK(run,
            (SpanCount == 2U) && !Spans[0].HasSectionBefore && Spans[0].HasSectionAfter && (Spans[0].SectionAfter == 1U)
            && Spans[1].HasSectionBefore && (Spans[1].SectionBefore == 0U) && !Spans[1].HasSectionAfter);
        F.Settings.FitsEachSection = false;
        Lay(run, &F, &Count);
        Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        TEST_CHECK(run, (SpanCount == 1U) && !Spans[0].HasSectionBefore && !Spans[0].HasSectionAfter);
        F.Settings.FitsEachSection = true;
    }
    AddNode(&F, 0.0, 0.0, 10.0, PathNodeMode_Corner);
    if (BuildCurve(run, &F, true))
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        size_t SpanCount = 0U;
        const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        TEST_CHECK(run,
            (SpanCount == 4U) && Spans[0].HasSectionBefore && (Spans[0].SectionBefore == 3U) && Spans[3].HasSectionAfter
            && (Spans[3].SectionAfter == 0U));
        F.Settings.FitsEachSection = false;
        Lay(run, &F, &Count);
        Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        TEST_CHECK(run,
            (SpanCount == 1U) && Spans[0].HasSectionBefore && (Spans[0].SectionBefore == 3U) && Spans[0].HasSectionAfter
            && (Spans[0].SectionAfter == 0U));
    }
    Fixture_End(&F);
}

/* A layout that has reached its limit walks no more of the path. */
static void AFullLayoutWalksNoMoreSpans(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 1.0f);
    F.Input.MaxPieces = 5U;
    for (int Node = 0; Node < 40; Node++)
    {
        AddNode(&F, 10.0 * (double)Node, 0.0, ((Node % 2) == 0) ? 3.0 : -3.0, PathNodeMode_Corner);
    }
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        size_t SpanCount = 0U;
        PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        TEST_CHECK(run, (Count <= 5U) && PathObjectLayout_WasLimited(&F.Layout) && (SpanCount <= 2U));
    }
    Fixture_End(&F);
}


// Tests: on a curve.
/* A circle's quarter arcs: corner-free, and every chord is the same length exactly when walked by chords. */
static bool BuildCircle(TestRun* run, Fixture* self, double radius, bool isClosed)
{
    self->NodeCount = 0U;
    for (int Node = 0; Node < 16; Node++)
    {
        double Angle = ((double)Node / 16.0) * 6.283185307179586;
        AddNode(self, radius * cos(Angle), 0.0, radius * sin(Angle), PathNodeMode_Smooth);
    }
    return BuildCurve(run, self, isClosed);
}

/* The solve stops within its tolerance: the last piece still ends exactly on the span's end, in both kinds of walk. */
static void TheLastPieceLandsExactly(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 0.7f);
    if (BuildCircle(run, &F, 20.0, false))
    {
        for (int Kind = 0; Kind < 2; Kind++)
        {
            F.Input.Slots[PathObjectSlot_Middle].IsRigid = Kind == 0;
            size_t Count = 0U;
            const PathObjectPiece* Pieces = Lay(run, &F, &Count);
            size_t SpanCount = 0U;
            const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
            TEST_CHECK(run, (Count > 100U) && (SpanCount == 1U) && (Pieces[Count - 1U].End == Spans[0].Length));
        }
    }
    Fixture_End(&F);
}

static void RigidPiecesHaveTheirEndsOnTheCurveAChordApart(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 3.0f);
    if (BuildCircle(run, &F, 20.0, false))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        size_t SpanCount = 0U;
        const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        TEST_CHECK(run, (Count > 20U) && (SpanCount == 1U));
        double FirstChord = 0.0;
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL); Index++)
        {
            PathCurvePoint Start;
            PathCurvePoint End;
            TEST_CHECK(run, PathObjectLayout_GetSpanPoint(&F.Curve, &Spans[0], Pieces[Index].Start, &Start)
                && PathObjectLayout_GetSpanPoint(&F.Curve, &Spans[0], Pieces[Index].End, &End));
            double Chord = Vector3Double_Distance(Start.Position, End.Position);
            FirstChord = (Index == 0U) ? Chord : FirstChord;
            // Every chord equal, though arcs and chords differ on a circle.
            TEST_CHECK(run, NEAR(Chord, FirstChord, 1.0e-3));
            if ((Index + 1U) < Count)
            {
                TEST_CHECK(run, Pieces[Index + 1U].Start == Pieces[Index].End);
            }
        }
        // It lands on the path's end, and its arc is longer than its chords add up to.
        TEST_CHECK(run, (Count > 0U) && NEAR(Pieces[Count - 1U].End, PathCurve_GetLength(&F.Curve), 1.0e-9));
        TEST_CHECK(run, ((double)Count * FirstChord) < PathCurve_GetLength(&F.Curve));
    }
    // Bent pieces are measured along the curve: equal arcs instead.
    F.Input.Slots[PathObjectSlot_Middle].IsRigid = false;
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        double Each = PathCurve_GetLength(&F.Curve) / (double)Count;
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL); Index++)
        {
            TEST_CHECK(run, NEAR(Pieces[Index].End - Pieces[Index].Start, Each, 1.0e-3));
        }
    }
    Fixture_End(&F);
}

static void ANodesScaleMakesPiecesLonger(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    AddNode(&F, 0.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 12.0, 0.0, 0.0, PathNodeMode_Corner);
    F.Nodes[0].Scale = 2.0f;
    F.Nodes[1].Scale = 2.0f;
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 3U) && NEAR(Pieces[0].End, 4.0, TOLERANCE) && Pieces[0].IsResized);
        // A fit that does not stretch shows it plainly: pieces twice as long, and the rest left over.
        F.Settings.Fit = PathObjectFit_Fixed;
        F.Models[PathObjectSlot_Middle][0].Length = 2.5f;
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 2U) && NEAR(Pieces[0].End, 5.0, TOLERANCE) && NEAR(Pieces[1].End, 10.0,
            TOLERANCE) && !Pieces[0].IsResized);
        F.Settings.Fit = PathObjectFit_Stretch;
        F.Models[PathObjectSlot_Middle][0].Length = 2.0f;
    }
    // A scale of 0 still moves the walk on: no endless loop, and a limited number of pieces.
    F.Nodes[0].Scale = 0.0f;
    F.Nodes[1].Scale = 0.0f;
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count > 0U) && (Pieces != NULL) && (Pieces[Count - 1U].End <= 12.0));
    }
    Fixture_End(&F);
}


// Tests: the fixed fit.
static void FixedPutsTheLeftoverWhereTheAlignmentSays(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    F.Settings.Fit = PathObjectFit_Fixed;
    struct { PathObjectAlign Align; double First; double Pitch; } Cases[] =
    {
        { PathObjectAlign_Start, 0.0, 2.0 },
        { PathObjectAlign_End, 1.0, 2.0 },
        { PathObjectAlign_Center, 0.5, 2.0 },
        { PathObjectAlign_Between, 0.0, 2.25 },
        { PathObjectAlign_Around, 0.1, 2.2 },
        { PathObjectAlign_Evenly, 1.0 / 6.0, 2.0 + (1.0 / 6.0) },
    };
    if (BuildLine(run, &F, 11.0))
    {
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            F.Settings.Align = Cases[Index].Align;
            size_t Count = 0U;
            const PathObjectPiece* Pieces = Lay(run, &F, &Count);
            TEST_CHECK(run, Count == 5U);
            for (size_t Piece = 0U; (Piece < Count) && (Pieces != NULL); Piece++)
            {
                TEST_CHECK(run, NEAR(Pieces[Piece].Start, Cases[Index].First + (Cases[Index].Pitch * (double)Piece), TOLERANCE)
                    && NEAR(Pieces[Piece].End - Pieces[Piece].Start, 2.0, TOLERANCE));
            }
        }
    }
    // One piece cannot be flush at both ends: between puts it in the middle.
    F.Settings.Align = PathObjectAlign_Between;
    if (BuildLine(run, &F, 3.0))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 1U) && NEAR(Pieces[0].Start, 0.5, TOLERANCE));
    }
    Fixture_End(&F);
}

static void ARowOfLampsIsAMiddleWithGaps(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 0.0f);
    F.Settings.Fit = PathObjectFit_Fixed;
    F.Settings.UsesGap = true;
    F.Settings.Gap = 25.0f;
    double Starts[16];
    if (BuildLine(run, &F, 100.0))
    {
        F.Settings.Align = PathObjectAlign_Between;
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 5U) && (StartsOf(Pieces, Count, PathObjectSlot_Middle, Starts, 16U) == 5U));
        for (size_t Index = 0U; Index < Count; Index++)
        {
            TEST_CHECK(run, NEAR(Starts[Index], 25.0 * (double)Index, TOLERANCE));
        }
    }
    // The gap is what it keeps at least: between shares what is left on top of it; start leaves it at the end.
    if (BuildLine(run, &F, 110.0))
    {
        F.Settings.Align = PathObjectAlign_Between;
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 5U) && NEAR(Pieces[1].Start, 27.5, TOLERANCE));
        F.Settings.Align = PathObjectAlign_Start;
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 5U) && NEAR(Pieces[1].Start, 25.0, TOLERANCE) && NEAR(Pieces[4].Start, 100.0, TOLERANCE));
        F.Settings.Align = PathObjectAlign_Center;
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 5U) && NEAR(Pieces[0].Start, 5.0, TOLERANCE));
    }
    // A count instead of a gap.
    F.Settings.UsesGap = false;
    F.Settings.UsesCount = true;
    F.Settings.Count = 3U;
    F.Settings.Align = PathObjectAlign_Between;
    if (BuildLine(run, &F, 10.0))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 3U) && NEAR(Pieces[1].Start, 5.0, TOLERANCE) && NEAR(Pieces[2].Start, 10.0, TOLERANCE));
    }
    // Without a gap or a count nothing says how many points to make: the build says so instead of making thousands.
    F.Settings.UsesCount = false;
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 0U) && PathObjectLayout_HasZeroLengthMiddle(&F.Layout));
    }
    Fixture_End(&F);
}

static void CapsSitWhereTheSettingSays(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    SetSlot(&F, PathObjectSlot_StartCap, 0.5f);
    SetSlot(&F, PathObjectSlot_EndCap, 0.5f);
    F.Settings.Fit = PathObjectFit_Fixed;
    F.Settings.Align = PathObjectAlign_Center;
    if (BuildLine(run, &F, 10.0))
    {
        // 4 middles take 8; the caps 1; 1 m is left, half on each side.
        F.Settings.CapsAt = PathObjectCapsAt_Path;
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 6U) && NEAR(Pieces[0].Start, 0.0, TOLERANCE) && NEAR(Pieces[1].Start, 1.0, TOLERANCE)
            && NEAR(Pieces[5].End, 10.0, TOLERANCE));
        F.Settings.CapsAt = PathObjectCapsAt_Run;
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 6U) && NEAR(Pieces[0].Start, 0.5, TOLERANCE) && NEAR(Pieces[1].Start, 1.0, TOLERANCE)
            && NEAR(Pieces[5].End, 9.5, TOLERANCE));
    }
    Fixture_End(&F);
}

static void SeparatorsStandInTheMiddleOfAGap(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    SetSlot(&F, PathObjectSlot_Separator, 0.2f);
    F.Settings.Fit = PathObjectFit_Fixed;
    F.Settings.Align = PathObjectAlign_Between;
    F.Settings.UsesCount = true;
    F.Settings.Count = 2U;
    if (BuildLine(run, &F, 6.2))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 3U) && (Pieces[1].Slot == PathObjectSlot_Separator) && NEAR(Pieces[0].End, 2.0, TOLERANCE)
            && NEAR(Pieces[1].Start, 3.0, TOLERANCE) && NEAR(Pieces[2].Start, 4.2, TOLERANCE));
    }
    Fixture_End(&F);
}


// Tests: resizing the ends.
static void ResizeEndsKeepsWholePiecesBetweenThem(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    F.Settings.Fit = PathObjectFit_ResizeEnds;
    F.Settings.Rounding = PathObjectRounding_Up;
    if (BuildLine(run, &F, 11.0))
    {
        // Six pieces: four whole and two at 3 / 4 of their length each.
        F.Settings.ResizedEnds = PathObjectResizedEnds_Both;
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 6U) && NEAR(Pieces[0].End, 1.5, TOLERANCE) && NEAR(Pieces[1].End, 3.5, TOLERANCE)
            && NEAR(Pieces[5].Start, 9.5, TOLERANCE) && NEAR(Pieces[5].End, 11.0, TOLERANCE));
        TEST_CHECK(run, (Pieces[0].CutStart == 0.0f) && (Pieces[0].CutEnd == 0.0f));
        // Only the first: five whole and one of 1 m.
        F.Settings.ResizedEnds = PathObjectResizedEnds_Start;
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 6U) && NEAR(Pieces[0].End, 1.0, TOLERANCE) && NEAR(Pieces[1].End, 3.0, TOLERANCE));
        // Only the last.
        F.Settings.ResizedEnds = PathObjectResizedEnds_End;
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 6U) && NEAR(Pieces[0].End, 2.0, TOLERANCE) && NEAR(Pieces[5].Start, 10.0, TOLERANCE));
    }
    // Rounding down, the ends grow instead.
    F.Settings.Rounding = PathObjectRounding_Down;
    F.Settings.ResizedEnds = PathObjectResizedEnds_Both;
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 5U) && NEAR(Pieces[0].End, 2.5, TOLERANCE));
    }
    Fixture_End(&F);
}

static void ACutSaysHowMuchOfThePieceItTakesOff(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    F.Settings.Fit = PathObjectFit_ResizeEnds;
    F.Settings.ResizeMethod = PathObjectResizeMethod_Cut;
    F.Settings.Rounding = PathObjectRounding_Down; // a cut only shortens: it rounds up whatever the setting says
    if (BuildLine(run, &F, 11.0))
    {
        F.Settings.ResizedEnds = PathObjectResizedEnds_Both;
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 6U) && NEAR(Pieces[0].CutStart, 0.25, 1.0e-3) && (Pieces[0].CutEnd == 0.0f)
            && NEAR(Pieces[5].CutEnd, 0.25, 1.0e-3) && (Pieces[5].CutStart == 0.0f) && (Pieces[2].CutStart == 0.0f));
        F.Settings.ResizedEnds = PathObjectResizedEnds_End;
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 6U) && (Pieces[0].CutStart == 0.0f) && NEAR(Pieces[5].CutEnd, 0.5, 1.0e-3)
            && NEAR(Pieces[5].End - Pieces[5].Start, 1.0, TOLERANCE));
    }
    // One piece cut at both ends takes half of the cut off each.
    F.Settings.ResizedEnds = PathObjectResizedEnds_Both;
    if (BuildLine(run, &F, 1.5))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 1U) && NEAR(Pieces[0].CutStart, 0.125, 1.0e-3) && NEAR(Pieces[0].CutEnd, 0.125, 1.0e-3));
    }
    // An exact fit cuts nothing, and no sliver is left over a hair past it.
    if (BuildLine(run, &F, 10.000001))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 5U) && NEAR(Pieces[4].CutEnd, 0.0, 1.0e-3));
    }
    Fixture_End(&F);
}


// Tests: too short.
static void ASpanTooShortDoesWhatTheSettingSays(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    SetSlot(&F, PathObjectSlot_StartCap, 0.1f);
    SetSlot(&F, PathObjectSlot_EndCap, 0.1f);
    if (BuildLine(run, &F, 1.2))
    {
        // Squeezed to fill what the caps leave.
        F.Settings.WhenTooShort = PathObjectTooShort_Squeeze;
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 3U) && (Pieces[1].Slot == PathObjectSlot_Middle) && NEAR(Pieces[1].End - Pieces[1].Start, 1.0,
            TOLERANCE));
        F.Settings.WhenTooShort = PathObjectTooShort_CapsOnly;
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 2U) && (Pieces[0].Slot == PathObjectSlot_StartCap) && (Pieces[1].Slot == PathObjectSlot_EndCap)
            && NEAR(Pieces[0].Start, 0.0, TOLERANCE) && NEAR(Pieces[1].End, 1.2, TOLERANCE));
        F.Settings.WhenTooShort = PathObjectTooShort_Nothing;
        Lay(run, &F, &Count);
        TEST_CHECK(run, Count == 0U);
        // The single slot, with nothing else: without one, as caps only.
        F.Settings.WhenTooShort = PathObjectTooShort_Single;
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 2U) && (Pieces[0].Slot == PathObjectSlot_StartCap));
        SetSlot(&F, PathObjectSlot_Single, 3.0f);
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 1U) && (Pieces[0].Slot == PathObjectSlot_Single) && NEAR(Pieces[0].End, 1.2, TOLERANCE));
        F.Input.Slots[PathObjectSlot_Single].IsUsed = false;
    }
    // Below the minimum stretch a squeeze gives up: caps only.
    F.Settings.WhenTooShort = PathObjectTooShort_Squeeze;
    F.Settings.MinimumStretch = 0.5f;
    if (BuildLine(run, &F, 1.0))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 2U) && (Pieces[0].Slot == PathObjectSlot_StartCap));
    }
    // Caps longer than the span: nothing fits, nothing is drawn over the end.
    if (BuildLine(run, &F, 0.15))
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        TEST_CHECK(run, Count == 0U);
    }
    Fixture_End(&F);
}

static void ExactlyOneMiddleDrawsTheSingleSlot(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    SetSlot(&F, PathObjectSlot_StartCap, 0.1f);
    SetSlot(&F, PathObjectSlot_EndCap, 0.1f);
    SetSlot(&F, PathObjectSlot_Single, 3.0f);
    if (BuildLine(run, &F, 2.6))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 1U) && (Pieces[0].Slot == PathObjectSlot_Single) && NEAR(Pieces[0].Start, 0.0, TOLERANCE)
            && NEAR(Pieces[0].End, 2.6, TOLERANCE));
        // A single that does not stretch lies in the middle, at its own length.
        F.Input.Slots[PathObjectSlot_Single].Stretches = false;
        F.Models[PathObjectSlot_Single][0].Length = 1.0f;
        Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 1U) && NEAR(Pieces[0].Start, 0.8, TOLERANCE) && NEAR(Pieces[0].End, 1.8, TOLERANCE));
    }
    // The fixed fit too: one middle fits, and the single takes the span.
    F.Settings.Fit = PathObjectFit_Fixed;
    F.Models[PathObjectSlot_Single][0].Length = 1.0f;
    if (BuildLine(run, &F, 2.6))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 1U) && (Pieces[0].Slot == PathObjectSlot_Single));
    }
    // A span too short for a middle at all, on squeeze: the single too, as the fits that resize draw it.
    if (BuildLine(run, &F, 1.5))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 1U) && (Pieces[0].Slot == PathObjectSlot_Single));
    }
    F.Settings.Fit = PathObjectFit_Stretch;
    F.Input.Slots[PathObjectSlot_Single].Stretches = true;
    // Two middles: no single.
    if (BuildLine(run, &F, 5.0))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 4U) && (Pieces[1].Slot == PathObjectSlot_Middle));
    }
    Fixture_End(&F);
}


// Tests: runs, sections and joints.
static void EachSectionFitsOnItsOwnWithAJointBetween(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    SetSlot(&F, PathObjectSlot_Separator, 0.2f);
    SetSlot(&F, PathObjectSlot_StartCap, 0.0f);
    SetSlot(&F, PathObjectSlot_EndCap, 0.0f);
    AddNode(&F, 0.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 6.1, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 6.1, 0.0, 8.1, PathNodeMode_Corner);
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        size_t SpanCount = 0U;
        PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        TEST_CHECK(run, SpanCount == 2U);
        // The joint is one piece at the first span's end, half of it on each side: its spans fill 6.1 - 0.1 and 8.1 - 0.1 m.
        size_t Joints = 0U;
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL); Index++)
        {
            if (Pieces[Index].IsJoint)
            {
                Joints++;
                TEST_CHECK(run, (Pieces[Index].Slot == PathObjectSlot_Separator) && (Pieces[Index].Span == 0U)
                    && NEAR(Pieces[Index].Start, 6.1, TOLERANCE) && (Pieces[Index].Start == Pieces[Index].End));
            }
            if (Pieces[Index].Slot == PathObjectSlot_Middle)
            {
                // 3 panels and 2 separators fill 6 m; 4 panels and 3 separators 8 m.
                TEST_CHECK(run, NEAR(Pieces[Index].End - Pieces[Index].Start,
                    (Pieces[Index].Span == 0U) ? (5.6 / 3.0) : (7.4 / 4.0),
                    TOLERANCE));
            }
        }
        TEST_CHECK(run, Joints == 1U);
        // The second span's first middle starts half a joint past its node.
        bool IsFound = false;
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL) && !IsFound; Index++)
        {
            if ((Pieces[Index].Span == 1U) && (Pieces[Index].Slot == PathObjectSlot_Middle))
            {
                IsFound = true;
                TEST_CHECK(run, NEAR(Pieces[Index].Start, 0.1, TOLERANCE));
            }
        }
        TEST_CHECK(run, IsFound);
    }
    // A corner slot takes over at corner nodes, a separator stays at other section starts.
    SetSlot(&F, PathObjectSlot_Corner, 0.0f);
    F.Nodes[1].Mode = PathNodeMode_Corner;
    AddNode(&F, 6.1, 0.0, 20.0, PathNodeMode_Smooth);
    F.Nodes[2].Mode = PathNodeMode_Smooth;
    F.Nodes[2].StartsSection = true;
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        PathObjectSlot Kinds[2] = { PathObjectSlot_Count, PathObjectSlot_Count };
        size_t Found = 0U;
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL); Index++)
        {
            if (Pieces[Index].IsJoint && (Found < 2U))
            {
                Kinds[Found++] = Pieces[Index].Slot;
            }
        }
        TEST_CHECK(run, (Found == 2U) && (Kinds[0] == PathObjectSlot_Corner) && (Kinds[1] == PathObjectSlot_Separator));
    }
    Fixture_End(&F);
}

static void ASkippedSectionEndsARunWithItsCaps(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    SetSlot(&F, PathObjectSlot_StartCap, 0.5f);
    SetSlot(&F, PathObjectSlot_EndCap, 0.5f);
    AddNode(&F, 0.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 5.0, 0.0, 0.0, PathNodeMode_Corner);
    F.Nodes[1].SkipsSection = false;
    AddNode(&F, 8.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 13.0, 0.0, 0.0, PathNodeMode_Corner);
    F.Nodes[1].SkipsSection = true; // the section from node 1 to node 2 is the gate
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        size_t Starts = 0U;
        size_t Ends = 0U;
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL); Index++)
        {
            Starts += (Pieces[Index].Slot == PathObjectSlot_StartCap) ? 1U : 0U;
            Ends += (Pieces[Index].Slot == PathObjectSlot_EndCap) ? 1U : 0U;
            // Nothing lies in the gate: the spans' own distances say which side a piece is on.
            TEST_CHECK(run, Pieces[Index].End <= 5.0 + TOLERANCE);
        }
        TEST_CHECK(run, (Starts == 2U) && (Ends == 2U));
        size_t SpanCount = 0U;
        const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        TEST_CHECK(run, (SpanCount == 2U) && NEAR(Spans[1].Start, 8.0, TOLERANCE));
    }
    Fixture_End(&F);
}

static void AClosedPathIsARunWithoutEnds(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    SetSlot(&F, PathObjectSlot_StartCap, 0.5f);
    SetSlot(&F, PathObjectSlot_EndCap, 0.5f);
    SetSlot(&F, PathObjectSlot_Separator, 0.0f);
    if (BuildCircle(run, &F, 10.0, true))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        size_t Middles = 0U;
        size_t Separators = 0U;
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL); Index++)
        {
            TEST_CHECK(run, (Pieces[Index].Slot != PathObjectSlot_StartCap) && (Pieces[Index].Slot != PathObjectSlot_EndCap));
            Middles += (Pieces[Index].Slot == PathObjectSlot_Middle) ? 1U : 0U;
            Separators += (Pieces[Index].Slot == PathObjectSlot_Separator) ? 1U : 0U;
        }
        // A separator after every middle, the last where the path closes: as many as middles.
        TEST_CHECK(run, (Middles > 20U) && (Separators == Middles));
        TEST_CHECK(run, Pieces[Count - 1U].IsJoint && NEAR(Pieces[Count - 2U].End, PathCurve_GetLength(&F.Curve), 1.0e-9));
    }
    // A skipped section opens it: one run wrapping past the end of the list, its caps at the opening's two sides.
    F.Nodes[4].StartsSection = true;
    F.Nodes[6].StartsSection = true;
    F.Nodes[4].SkipsSection = true;
    if (BuildCurve(run, &F, true))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        size_t SpanCount = 0U;
        const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        TEST_CHECK(run, (Count > 2U) && (SpanCount == 1U) && (Pieces[0].Slot == PathObjectSlot_StartCap)
            && (Pieces[Count - 1U].Slot == PathObjectSlot_EndCap));
        TEST_CHECK(run, (SpanCount == 1U) && (Spans[0].Start + Spans[0].Length) > PathCurve_GetLength(&F.Curve));
    }
    Fixture_End(&F);
}

static void OneFitOverAWholeRunCrossesItsCorners(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    SetSlot(&F, PathObjectSlot_Separator, 0.0f);
    F.Settings.FitsEachSection = false;
    F.Input.Slots[PathObjectSlot_Middle].IsRigid = false;
    AddNode(&F, 0.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 5.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 5.0, 0.0, 5.0, PathNodeMode_Corner);
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        size_t SpanCount = 0U;
        const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        TEST_CHECK(run, (SpanCount == 1U) && (Spans[0].FirstSection == 0U) && (Spans[0].LastSection == 1U)
            && NEAR(Spans[0].Length, 10.0, TOLERANCE));
        size_t Joints = 0U;
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL); Index++)
        {
            Joints += Pieces[Index].IsJoint ? 1U : 0U;
        }
        TEST_CHECK(run, (Joints == 0U) && (Count == 9U));
        // A span's point keeps its last section's arriving tangent at its end, its first's at its start.
        PathCurvePoint Point;
        TEST_CHECK(run, PathObjectLayout_GetSpanPoint(&F.Curve, &Spans[0], 10.0, &Point) && NEAR(Point.Tangent.z, 1.0, 1.0e-5));
        TEST_CHECK(run, PathObjectLayout_GetSpanPoint(&F.Curve, &Spans[0], 0.0, &Point) && NEAR(Point.Tangent.x, 1.0, 1.0e-5));
    }
    Fixture_End(&F);
}

/* The two spans a joint stands between agree on which model it is: each leaves half of that model's length to it. */
static void BothSpansOfAJointAgreeOnItsModel(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 1.0f);
    SetSlot(&F, PathObjectSlot_Separator, 0.2f);
    F.Models[PathObjectSlot_Separator][1] = (PathObjectLayoutModel){ .Length = 1.0f, .Weight = 1.0f };
    F.Input.Slots[PathObjectSlot_Separator].ModelCount = 2U;
    F.Settings.Seed = 5U;
    for (int Node = 0; Node < 24; Node++)
    {
        AddNode(&F, 10.0 * (double)Node, 0.0, ((Node % 2) == 0) ? 2.0 : -2.0, PathNodeMode_Corner);
    }
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        size_t SpanCount = 0U;
        const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        TEST_CHECK(run, SpanCount == 23U);
        bool SawEach[2] = { false, false };
        for (size_t Index = 1U; (Index + 1U) < Count; Index++)
        {
            if (!Pieces[Index].IsJoint || Pieces[Index].Slot != PathObjectSlot_Separator)
            {
                continue;
            }
            SawEach[Pieces[Index].Model] = true;
            double Half = 0.5 * (double)Pieces[Index].Nominal;
            const PathObjectPiece* Before = &Pieces[Index - 1U];
            const PathObjectPiece* After = &Pieces[Index + 1U];
            TEST_CHECK(run, NEAR(Before->End, Spans[Before->Span].Length - Half, TOLERANCE));
            TEST_CHECK(run, NEAR(After->Start, Half, TOLERANCE));
        }
        TEST_CHECK(run, SawEach[0] && SawEach[1]);
    }
    Fixture_End(&F);
}

static void ASectionOfNoLengthHoldsNothing(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 2.0f);
    SetSlot(&F, PathObjectSlot_Separator, 0.0f);
    AddNode(&F, 0.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 4.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 4.0, 0.0, 0.0, PathNodeMode_Corner);
    AddNode(&F, 4.0, 0.0, 4.0, PathNodeMode_Corner);
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        size_t SpanCount = 0U;
        PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        size_t Joints = 0U;
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL); Index++)
        {
            Joints += Pieces[Index].IsJoint ? 1U : 0U;
        }
        TEST_CHECK(run, (SpanCount == 2U) && (Joints == 1U));
    }
    Fixture_End(&F);
}


// Tests: models.
static void ASlotChoosesAmongItsModels(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 1.0f);
    F.Models[PathObjectSlot_Middle][1] = (PathObjectLayoutModel){ .Length = 1.0f, .Weight = 3.0f };
    F.Input.Slots[PathObjectSlot_Middle].ModelCount = 2U;
    if (BuildLine(run, &F, 4000.0))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        size_t Seconds = 0U;
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL); Index++)
        {
            Seconds += Pieces[Index].Model;
        }
        // By their weights, within what chance leaves.
        TEST_CHECK(run, (Count == 4000U) && (Seconds > 2850U) && (Seconds < 3150U));
        uint32_t First[8];
        for (size_t Index = 0U; (Index < 8U) && (Pieces != NULL); Index++)
        {
            First[Index] = Pieces[Index].Model;
        }
        // The same seed, the same choices; another seed, others.
        Pieces = Lay(run, &F, &Count);
        bool IsSame = Pieces != NULL;
        for (size_t Index = 0U; (Index < 8U) && IsSame; Index++)
        {
            IsSame = First[Index] == Pieces[Index].Model;
        }
        TEST_CHECK(run, IsSame);
        F.Settings.Seed = 12345U;
        Pieces = Lay(run, &F, &Count);
        size_t Differing = 0U;
        for (size_t Index = 0U; (Index < 200U) && (Pieces != NULL); Index++)
        {
            Differing += (PathObjectLayout_GetRandom(0U, PathObjectSlot_Middle, 0U, (uint32_t)Index, 1U) < 0.25f)
                != (Pieces[Index].Model == 0U) ? 1U : 0U;
        }
        TEST_CHECK(run, Differing > 10U);
    }
    // In the list's order.
    F.Input.Slots[PathObjectSlot_Middle].Pick = PathObjectPick_InOrder;
    if (BuildLine(run, &F, 7.0))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL); Index++)
        {
            TEST_CHECK(run, Pieces[Index].Model == (Index % 2U));
        }
        TEST_CHECK(run, Count == 7U);
    }
    // Models of different lengths fill by their own lengths.
    F.Models[PathObjectSlot_Middle][1].Length = 3.0f;
    if (BuildLine(run, &F, 8.0))
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 4U) && NEAR(Pieces[0].End, 1.0, TOLERANCE) && NEAR(Pieces[1].End, 4.0, TOLERANCE)
            && NEAR(Pieces[3].End, 8.0, TOLERANCE));
    }
    Fixture_End(&F);
}

static void ARandomValueIsStableAndSpread(TestRun* run)
{
    float First = PathObjectLayout_GetRandom(1U, PathObjectSlot_Middle, 2U, 3U, 4U);
    TEST_CHECK(run, First == PathObjectLayout_GetRandom(1U, PathObjectSlot_Middle, 2U, 3U, 4U));
    TEST_CHECK(run, First != PathObjectLayout_GetRandom(2U, PathObjectSlot_Middle, 2U, 3U, 4U));
    TEST_CHECK(run, First != PathObjectLayout_GetRandom(1U, PathObjectSlot_Separator, 2U, 3U, 4U));
    TEST_CHECK(run, First != PathObjectLayout_GetRandom(1U, PathObjectSlot_Middle, 3U, 3U, 4U));
    TEST_CHECK(run, First != PathObjectLayout_GetRandom(1U, PathObjectSlot_Middle, 2U, 4U, 4U));
    TEST_CHECK(run, First != PathObjectLayout_GetRandom(1U, PathObjectSlot_Middle, 2U, 3U, 5U));
    double Sum = 0.0;
    for (uint32_t Index = 0U; Index < 10000U; Index++)
    {
        float Value = PathObjectLayout_GetRandom(9U, PathObjectSlot_Corner, 0U, Index, 1U);
        TEST_CHECK(run, (Value >= 0.0f) && (Value < 1.0f));
        Sum += (double)Value;
    }
    TEST_CHECK(run, fabs((Sum / 10000.0) - 0.5) < 0.02);
}


// Tests: limits.
static void ALayoutStopsAtItsLimit(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    SetSlot(&F, PathObjectSlot_Middle, 1.0f);
    F.Input.MaxPieces = 10U;
    if (BuildLine(run, &F, 1000.0))
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        TEST_CHECK(run, (Count <= 10U) && PathObjectLayout_WasLimited(&F.Layout));
        F.Input.MaxPieces = 100000U;
        Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 1000U) && !PathObjectLayout_WasLimited(&F.Layout));
    }
    // Seven middles fit a limit of ten, but not with their six separators.
    SetSlot(&F, PathObjectSlot_Separator, 0.0f);
    F.Input.MaxPieces = 10U;
    if (BuildLine(run, &F, 7.0))
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        TEST_CHECK(run, (Count == 10U) && PathObjectLayout_WasLimited(&F.Layout));
    }
    F.Input.Slots[PathObjectSlot_Separator].IsUsed = false;
    F.Input.MaxPieces = 100000U;
    // A middle of no length fills nothing but its caps, and says so.
    F.Models[PathObjectSlot_Middle][0].Length = 0.0f;
    SetSlot(&F, PathObjectSlot_StartCap, 1.0f);
    {
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        TEST_CHECK(run,
            (Count == 1U) && (Pieces[0].Slot == PathObjectSlot_StartCap) && PathObjectLayout_HasZeroLengthMiddle(&F.Layout));
    }
    // No middle at all, no curve at all: nothing, and no error.
    F.Input.Slots[PathObjectSlot_Middle].ModelCount = 0U;
    F.Input.Slots[PathObjectSlot_StartCap].IsUsed = false;
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        TEST_CHECK(run, Count == 0U);
    }
    F.Input.Slots[PathObjectSlot_Middle].ModelCount = 1U;
    F.NodeCount = 0U;
    if (BuildCurve(run, &F, false))
    {
        size_t Count = 0U;
        Lay(run, &F, &Count);
        TEST_CHECK(run, Count == 0U);
    }
    Fixture_End(&F);
    TEST_CHECK_ERROR(run, PathObjectLayout_Build(NULL, NULL), ErrorCode_IllegalArgument);
}

/* A small generator, the same on every machine. */
static uint32_t NextRandom(uint32_t* state)
{
    *state = (*state * 1664525U) + 1013904223U;
    return *state >> 8U;
}

static float RandomIn(uint32_t* state, float minimum, float maximum)
{
    return minimum + ((maximum - minimum) * ((float)(NextRandom(state) % 65536U) / 65536.0f));
}

static void AnyLayoutHoldsItsPiecesInOrderInsideTheirSpans(TestRun* run)
{
    Fixture F;
    Fixture_Begin(&F);
    uint32_t State = 2024U;
    for (int Round = 0; Round < 400; Round++)
    {
        F.NodeCount = 0U;
        size_t Nodes = 2U + (NextRandom(&State) % 10U);
        for (size_t Node = 0U; Node < Nodes; Node++)
        {
            uint32_t Kind = NextRandom(&State) % 3U;
            AddNode(&F, (double)RandomIn(&State, -40.0f, 40.0f), (double)RandomIn(&State, -5.0f, 5.0f),
                (double)RandomIn(&State, -40.0f, 40.0f), (PathNodeMode)Kind);
            F.Nodes[Node].StartsSection = (NextRandom(&State) % 4U) == 0U;
            F.Nodes[Node].SkipsSection = (NextRandom(&State) % 6U) == 0U;
            F.Nodes[Node].Scale = ((NextRandom(&State) % 5U) == 0U) ? RandomIn(&State, 0.0f, 3.0f) : 1.0f;
        }
        if (!BuildCurve(run, &F, (NextRandom(&State) % 3U) == 0U))
        {
            break;
        }
        F.Settings = PathObject_ResolveSettings(NULL, NULL);
        F.Settings.Fit = (PathObjectFit)(NextRandom(&State) % 3U);
        F.Settings.Align = (PathObjectAlign)(NextRandom(&State) % 6U);
        F.Settings.Rounding = (PathObjectRounding)(NextRandom(&State) % 3U);
        F.Settings.ResizedEnds = (PathObjectResizedEnds)(NextRandom(&State) % 3U);
        F.Settings.ResizeMethod = (PathObjectResizeMethod)(NextRandom(&State) % 2U);
        F.Settings.WhenTooShort = (PathObjectTooShort)(NextRandom(&State) % 4U);
        F.Settings.CapsAt = (PathObjectCapsAt)(NextRandom(&State) % 2U);
        F.Settings.UsesCount = (NextRandom(&State) % 4U) == 0U;
        F.Settings.Count = 1U + (NextRandom(&State) % 9U);
        F.Settings.UsesGap = (NextRandom(&State) % 4U) == 0U;
        F.Settings.Gap = RandomIn(&State, 0.0f, 5.0f);
        F.Settings.FitsEachSection = (NextRandom(&State) % 4U) != 0U;
        F.Settings.SeparatorEvery = 1U + (NextRandom(&State) % 3U);
        for (uint32_t Slot = 0U; Slot < (uint32_t)PathObjectSlot_Count; Slot++)
        {
            SetSlot(&F, (PathObjectSlot)Slot, ((NextRandom(&State) % 4U) == 0U) ? 0.0f : RandomIn(&State, 0.1f, 4.0f));
            F.Input.Slots[Slot].IsUsed = (Slot == (uint32_t)PathObjectSlot_Middle) || ((NextRandom(&State) % 2U) == 0U);
            F.Input.Slots[Slot].IsRigid = (NextRandom(&State) % 2U) == 0U;
            F.Input.Slots[Slot].Stretches = (NextRandom(&State) % 2U) == 0U;
        }
        size_t Count = 0U;
        const PathObjectPiece* Pieces = Lay(run, &F, &Count);
        size_t SpanCount = 0U;
        const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&F.Layout, &SpanCount);
        double Previous = -1.0;
        uint32_t PreviousSpan = 0U;
        for (size_t Index = 0U; (Index < Count) && (Pieces != NULL); Index++)
        {
            const PathObjectPiece* Piece = &Pieces[Index];
            TEST_CHECK(run, Piece->Span < SpanCount);
            double Length = Spans[Piece->Span].Length;
            TEST_CHECK(run,
                isfinite(Piece->Start) && isfinite(Piece->End) && (Piece->Start >= 0.0) && (Piece->End >= Piece->Start)
                && (Piece->End <= (Length + 1.0e-9)) && (Piece->Model < F.Input.Slots[Piece->Slot].ModelCount));
            if (Piece->Span != PreviousSpan)
            {
                Previous = -1.0;
                PreviousSpan = Piece->Span;
            }
            // In order, a hair's rounding aside; a joint sits on the span's end, past nothing.
            TEST_CHECK(run, Piece->Start >= (Previous - 1.0e-6));
            Previous = Piece->End;
            TEST_CHECK(run, (Piece->CutStart >= 0.0f) && (Piece->CutEnd >= 0.0f) && ((Piece->CutStart + Piece->CutEnd) <= 1.0f));
        }
    }
    Fixture_End(&F);
}


// Public functions.
void PathObjectLayoutTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "stretch_scales_a_whole_number_of_middles_to_fill", StretchScalesAWholeNumberOfMiddlesToFill },
        { "caps_and_separators_lie_end_to_end_by_their_lengths", CapsAndSeparatorsLieEndToEndByTheirLengths },
        { "a_count_is_exact_as_far_as_it_fits", ACountIsExactAsFarAsItFits },
        { "a_huge_count_comes_down_to_what_fits", AHugeCountComesDownToWhatFits },
        { "the_last_piece_lands_exactly", TheLastPieceLandsExactly },
        { "a_long_winding_path_still_fills", ALongWindingPathStillFills },
        { "a_walk_that_jumps_still_lands", AWalkThatJumpsStillLands },
        { "a_hostile_count_stops_walking", AHostileCountStopsWalking },
        { "a_span_knows_the_sections_at_its_ends", ASpanKnowsTheSectionsAtItsEnds },
        { "a_full_layout_walks_no_more_spans", AFullLayoutWalksNoMoreSpans },
        { "rigid_pieces_have_their_ends_on_the_curve_a_chord_apart", RigidPiecesHaveTheirEndsOnTheCurveAChordApart },
        { "a_nodes_scale_makes_pieces_longer", ANodesScaleMakesPiecesLonger },
        { "fixed_puts_the_leftover_where_the_alignment_says", FixedPutsTheLeftoverWhereTheAlignmentSays },
        { "a_row_of_lamps_is_a_middle_with_gaps", ARowOfLampsIsAMiddleWithGaps },
        { "caps_sit_where_the_setting_says", CapsSitWhereTheSettingSays },
        { "separators_stand_in_the_middle_of_a_gap", SeparatorsStandInTheMiddleOfAGap },
        { "resize_ends_keeps_whole_pieces_between_them", ResizeEndsKeepsWholePiecesBetweenThem },
        { "a_cut_says_how_much_of_the_piece_it_takes_off", ACutSaysHowMuchOfThePieceItTakesOff },
        { "a_span_too_short_does_what_the_setting_says", ASpanTooShortDoesWhatTheSettingSays },
        { "exactly_one_middle_draws_the_single_slot", ExactlyOneMiddleDrawsTheSingleSlot },
        { "each_section_fits_on_its_own_with_a_joint_between", EachSectionFitsOnItsOwnWithAJointBetween },
        { "a_skipped_section_ends_a_run_with_its_caps", ASkippedSectionEndsARunWithItsCaps },
        { "a_closed_path_is_a_run_without_ends", AClosedPathIsARunWithoutEnds },
        { "one_fit_over_a_whole_run_crosses_its_corners", OneFitOverAWholeRunCrossesItsCorners },
        { "both_spans_of_a_joint_agree_on_its_model", BothSpansOfAJointAgreeOnItsModel },
        { "a_section_of_no_length_holds_nothing", ASectionOfNoLengthHoldsNothing },
        { "a_slot_chooses_among_its_models", ASlotChoosesAmongItsModels },
        { "a_random_value_is_stable_and_spread", ARandomValueIsStableAndSpread },
        { "a_layout_stops_at_its_limit", ALayoutStopsAtItsLimit },
        { "any_layout_holds_its_pieces_in_order_inside_their_spans", AnyLayoutHoldsItsPiecesInOrderInsideTheirSpans },
    };
    TestRun_RunSuite(run, "PathObjectLayout", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
