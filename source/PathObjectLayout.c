// Where a path object's pieces lie along a path: see PathObjectLayout.h.

#include <math.h>
#include <stdint.h>
#include "PathObjectLayout.h"
#include "wr/WRMemory.h"


// Macros.
/** The most times the walk is run to solve x, and for a span of very many pieces. */
#define MAX_SOLVE_WALKS 10
#define MAX_SOLVE_WALKS_LONG 3
/** How many items make a span of very many pieces. */
#define LONG_SPAN_ITEMS ((size_t)4096)
/** The most steps toward one chord's end. */
#define MAX_CHORD_STEPS 16
/** How close a chord comes to its length before the walk moves on, in metres. */
#define CHORD_TOLERANCE 1.0e-5
/** How near the range's end the last piece is taken to land on it, in metres; also how far a sequence may overrun by rounding. */
#define LANDING_TOLERANCE 0.01
/** A span shorter than this, in metres, holds nothing. */
#define MIN_SPAN_LENGTH 1.0e-6
/** The most times a span lowers its count of middles, by as much as each try says, before it shows only its caps. */
#define MAX_COUNT_RETRIES 16
/** The salt of a model's pick (PathObjectLayout_GetRandom). */
#define SALT_PICK 1U
/** The splitmix64 constants (Steele, Lea and Flood): an increment by the golden ratio, then two multiply-xorshift rounds. */
#define SPLITMIX_INCREMENT 0x9E3779B97F4A7C15ULL
#define SPLITMIX_MULTIPLIER_A 0xBF58476D1CE4E5B9ULL
#define SPLITMIX_MULTIPLIER_B 0x94D049BB133111EBULL
/** The random value keeps this many bits of its hash: as many as a float's significand holds exactly below 1. */
#define RANDOM_BITS 24U


// Types.
/* One item of a span's sequence: a piece or a gap, its length A + B * x, measured by a chord or along the curve. */
typedef struct LayoutItemStruct
{
    PathObjectSlot Slot;
    uint32_t Model;
    uint32_t Index;
    float Nominal;
    /* The length that does not change with x, and the length x multiplies, in metres, before the node's scale (a gap takes no
     * scale). */
    float A;
    float B;
    bool IsGap;
    bool IsRigid;
    /* A piece a cut resizes: x is the share of its length left, off its start, its end or both alike. */
    bool IsCutAtStart;
    bool IsCutAtEnd;
    /* Filled by the walk: where it starts and ends, from the span's start. */
    double Start;
    double End;
} LayoutItem;

/* What the build is working on. */
typedef struct LayoutContextStruct
{
    PathObjectLayout* Self;
    const PathObjectLayoutInput* Input;
    const PathCurve* Curve;
    const PathObjectSettings* Settings;
    /* The span being fitted, its index among the spans, and the range the sequence may fill. */
    const PathObjectSpan* Span;
    uint32_t SpanIndex;
    double RangeStart;
    double RangeEnd;
    /* Whether the sequence holds the run's caps. */
    bool HasStartCap;
    bool HasEndCap;
    /* A buffer failed to grow. */
    bool HasFailed;
} LayoutContext;

/* How many middles a range holds at their nominal lengths. */
typedef struct MiddleCountStruct
{
    /* The whole middles that fit. */
    uint32_t Whole;
    /* How much of the next one fits, in [0, 1). */
    float Fraction;
} MiddleCount;


// Static functions: random.
static uint64_t Mix(uint64_t state)
{
    uint64_t Value = state + SPLITMIX_INCREMENT;
    Value = (Value ^ (Value >> 30)) * SPLITMIX_MULTIPLIER_A;
    Value = (Value ^ (Value >> 27)) * SPLITMIX_MULTIPLIER_B;
    return Value ^ (Value >> 31);
}


// Static functions: the curve.
/* A section's end, as a distance from its span's start. */
static bool GetPointInSpan(const LayoutContext* context, double distance, PathCurvePoint* outPoint)
{
    return PathObjectLayout_GetSpanPoint(context->Curve, context->Span, distance, outPoint);
}

/* The node's scale factor at a distance from the span's start, with its floor. */
static float GetScaleAt(const LayoutContext* context, double distance)
{
    PathCurvePoint Point;
    if (!GetPointInSpan(context, distance, &Point))
    {
        return 1.0f;
    }
    return fmaxf(Point.Scale, PATH_OBJECT_LAYOUT_MIN_SCALE);
}

/* Where a chord of @p length from @p start ends, along the span; past the span's end by what it lacks there, so a walk that
 * overruns still says by how much. */
static double AdvanceByChord(const LayoutContext* context, double start, double length)
{
    if (length <= 0.0)
    {
        return start;
    }
    double SpanEnd = context->Span->Length;
    PathCurvePoint From;
    if ((start >= SpanEnd) || !GetPointInSpan(context, start, &From))
    {
        return start + length; // past the end already: the walk keeps counting by how much it overruns
    }
    double Here = start + length; // an arc is never shorter than its chord
    for (int Step = 0; Step < MAX_CHORD_STEPS; Step++)
    {
        PathCurvePoint To;
        double At = fmin(Here, SpanEnd);
        if (!GetPointInSpan(context, At, &To))
        {
            return Here;
        }
        double Chord = Vector3Double_Distance(From.Position, To.Position);
        double Lack = length - Chord;
        if (Here >= SpanEnd)
        {
            if (Lack >= 0.0)
            {
                return SpanEnd + Lack; // the span ends before the chord does
            }
            Here = SpanEnd;
        }
        if (fabs(Lack) < CHORD_TOLERANCE)
        {
            return At;
        }
        Here = At + Lack;
    }
    return Here;
}


// Static functions: the sequence.
/* The model piece @p index of a slot picks, in the stretch @p span names. */
static uint32_t PickModelIn(const LayoutContext* context, PathObjectSlot slot, uint32_t span, uint32_t index)
{
    const PathObjectLayoutSlot* Slot = &context->Input->Slots[slot];
    if (Slot->ModelCount <= 1U)
    {
        return 0U;
    }
    if (Slot->Pick == PathObjectPick_InOrder)
    {
        return index % Slot->ModelCount;
    }
    double Total = 0.0;
    for (uint32_t ModelIndex = 0U; ModelIndex < Slot->ModelCount; ModelIndex++)
    {
        Total += (double)Slot->Models[ModelIndex].Weight;
    }
    double Choice = (double)PathObjectLayout_GetRandom(context->Settings->Seed, slot, span, index, SALT_PICK) * Total;
    double Sum = 0.0;
    for (uint32_t ModelIndex = 0U; ModelIndex < Slot->ModelCount; ModelIndex++)
    {
        Sum += (double)Slot->Models[ModelIndex].Weight;
        if (Choice < Sum)
        {
            return ModelIndex;
        }
    }
    return Slot->ModelCount - 1U; // only rounding reaches here
}

/* The model a piece of the span being fitted picks. */
static uint32_t PickModel(const LayoutContext* context, PathObjectSlot slot, uint32_t index)
{
    return PickModelIn(context, slot, context->Span->FirstSection, index);
}

static bool IsSlotUsable(const LayoutContext* context, PathObjectSlot slot)
{
    const PathObjectLayoutSlot* Slot = &context->Input->Slots[slot];
    return Slot->IsUsed && (Slot->ModelCount > 0U);
}

static float GetModelLength(const LayoutContext* context, PathObjectSlot slot, uint32_t model)
{
    return context->Input->Slots[slot].Models[model].Length;
}

static void AddItem(LayoutContext* context, LayoutItem* item)
{
    if (!GenericBuffer_AddLast(&context->Self->_items, item))
    {
        context->HasFailed = true;
    }
}

static void AddGap(LayoutContext* context, float fixed, float share)
{
    if ((fixed > 0.0f) || (share > 0.0f))
    {
        LayoutItem Item = { .Slot = PathObjectSlot_Middle, .A = fixed, .B = share, .IsGap = true };
        AddItem(context, &Item);
    }
}

/* A piece of a slot: its whole length following x, or none of it. */
static void AddPiece(LayoutContext* context, PathObjectSlot slot, uint32_t index, bool followsX)
{
    uint32_t ModelIndex = PickModel(context, slot, index);
    float Length = GetModelLength(context, slot, ModelIndex);
    LayoutItem Item =
    {
        .Slot = slot, .Model = ModelIndex, .Index = index, .Nominal = Length, .A = followsX ? 0.0f : Length,
        .B = followsX ? Length : 0.0f, .IsRigid = context->Input->Slots[slot].IsRigid
    };
    AddItem(context, &Item);
}

static float GetCapLength(const LayoutContext* context, PathObjectSlot slot, bool isHeld)
{
    if (!isHeld || !IsSlotUsable(context, slot))
    {
        return 0.0f;
    }
    return GetModelLength(context, slot, PickModel(context, slot, 0U));
}

static bool IsSeparatorAfter(const LayoutContext* context, uint32_t middle, uint32_t count)
{
    if (!IsSlotUsable(context, PathObjectSlot_Separator) || ((middle + 1U) >= count))
    {
        return false;
    }
    return ((middle + 1U) % context->Settings->SeparatorEvery) == 0U;
}

/* How many middles the range holds at their nominal lengths, with their separators and caps, measured along the curve. */
static MiddleCount CountMiddles(const LayoutContext* context, float gap)
{
    MiddleCount Count = { .Whole = 0U, .Fraction = 0.0f };
    double StartCap = (double)GetCapLength(context, PathObjectSlot_StartCap, context->HasStartCap);
    double EndCap = (double)GetCapLength(context, PathObjectSlot_EndCap, context->HasEndCap);
    double Here = context->RangeStart + (StartCap * (double)GetScaleAt(context, context->RangeStart));
    double End = context->RangeEnd - (EndCap * (double)GetScaleAt(context, context->RangeEnd));
    uint32_t SeparatorIndex = 0U;
    size_t Limit = context->Input->MaxPieces;
    if (context->Settings->UsesCount && (context->Settings->Count < Limit))
    {
        Limit = context->Settings->Count;
    }
    while (Count.Whole < Limit)
    {
        context->Self->_walkedItems++;
        uint32_t ModelIndex = PickModel(context, PathObjectSlot_Middle, Count.Whole);
        double Length = (double)GetModelLength(context, PathObjectSlot_Middle, ModelIndex) * (double)GetScaleAt(context, Here);
        if ((Here + Length) > (End + PATH_OBJECT_LAYOUT_END_TOLERANCE))
        {
            double Left = End - Here;
            bool IsAnythingLeft = (Length > 0.0) && (Left > PATH_OBJECT_LAYOUT_END_TOLERANCE);
            Count.Fraction = IsAnythingLeft ? (float)fmin(Left / Length, 0.999999) : 0.0f;
            return Count;
        }
        Here += Length;
        Count.Whole++;
        if (IsSeparatorAfter(context, Count.Whole - 1U, Count.Whole + 1U))
        {
            uint32_t Separator = PickModel(context, PathObjectSlot_Separator, SeparatorIndex);
            Here += (double)GetModelLength(context, PathObjectSlot_Separator, Separator) * (double)GetScaleAt(context, Here);
            SeparatorIndex++;
        }
        Here += (double)gap;
    }
    return Count;
}

/* The count a fit that resizes takes, by the rounding. */
static uint32_t RoundCount(MiddleCount count, PathObjectRounding rounding)
{
    switch (rounding)
    {
        case PathObjectRounding_Nearest: return count.Whole + ((count.Fraction >= 0.5f) ? 1U : 0U);
        case PathObjectRounding_Up: return count.Whole + ((count.Fraction > 0.0f) ? 1U : 0U);
        case PathObjectRounding_Down: return count.Whole;
    }
    return count.Whole;
}


// Static functions: the walk.
/* Walks the items with @p x, filling their places; returns where the sequence ends (past the span's end by what it overruns). */
static double Walk(const LayoutContext* context, float x)
{
    LayoutItem* Items = (LayoutItem*)context->Self->_items._data;
    size_t Count = context->Self->_items._count;
    context->Self->_walkedItems += Count;
    double Here = context->RangeStart;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        LayoutItem* Item = &Items[Index];
        double Length = (double)Item->A + ((double)Item->B * (double)x);
        Item->Start = Here;
        if (Item->IsGap)
        {
            Here += Length;
        }
        else
        {
            double Scaled = Length * (double)GetScaleAt(context, Here);
            Here = Item->IsRigid ? AdvanceByChord(context, Here, Scaled) : (Here + Scaled);
        }
        Item->End = Here;
    }
    return Here;
}

/* The sum of the items' two kinds of length, at the node's scale where the range starts. */
static void SumItems(const LayoutContext* context, double* outFixed, double* outFollowing)
{
    const LayoutItem* Items = (const LayoutItem*)context->Self->_items._data;
    double Scale = (double)GetScaleAt(context, 0.5 * (context->RangeStart + context->RangeEnd));
    *outFixed = 0.0;
    *outFollowing = 0.0;
    for (size_t Index = 0U; Index < context->Self->_items._count; Index++)
    {
        double Factor = Items[Index].IsGap ? 1.0 : Scale;
        *outFixed += (double)Items[Index].A * Factor;
        *outFollowing += (double)Items[Index].B * Factor;
    }
}

/* Solves x so the sequence ends at the range's end; a sequence nothing follows x in just walks. A walk's end rises with x, though
 * not always smoothly: a chord that outgrows a hairpin jumps past it, and on a regular path all of them at once. So the secant
 * stays between the x known to end short and the x known to overrun. It lands when it ends within LANDING_TOLERANCE past the end,
 * or short by less than an item's share of the range. Where no x lands, *outFits is false and the items lie as they lay at the x
 * that overran, so the caller can see by how much and try a lower count; with @p acceptsShort it takes the x that ended short
 * instead. Gives x; false too when the sequence overruns with x at 0. */
static float SolveX(const LayoutContext* context, bool acceptsShort, bool* outFits)
{
    double Fixed = 0.0;
    double Following = 0.0;
    SumItems(context, &Fixed, &Following);
    double Target = context->RangeEnd;
    if (Following <= 0.0)
    {
        double End = Walk(context, 1.0f);
        *outFits = End <= (Target + PATH_OBJECT_LAYOUT_END_TOLERANCE);
        return 1.0f;
    }
    size_t ItemCount = context->Self->_items._count;
    int MaxWalks = (ItemCount > LONG_SPAN_ITEMS) ? MAX_SOLVE_WALKS_LONG : MAX_SOLVE_WALKS;
    double ShortAllowed = (Target - context->RangeStart) / (double)ItemCount;
    double X = fmax(0.0, ((Target - context->RangeStart) - Fixed) / Following);
    double Over = Walk(context, (float)X) - Target;
    double Slope = Following;
    double Short = -1.0;
    double ShortOver = 0.0;
    double Past = -1.0;
    for (int Attempt = 1; (Attempt < MaxWalks) && (fabs(Over) > PATH_OBJECT_LAYOUT_END_TOLERANCE); Attempt++)
    {
        if (Over < 0.0)
        {
            Short = X;
            ShortOver = Over;
        }
        else
        {
            Past = X;
        }
        double Next = fmax(0.0, X - (Over / Slope));
        if ((Short >= 0.0) && (Past >= 0.0) && ((Next <= Short) || (Next >= Past)))
        {
            Next = 0.5 * (Short + Past);
        }
        if (Next == X)
        {
            break; // x stays at 0: the sequence overruns without it
        }
        double NextOver = Walk(context, (float)Next) - Target;
        double Measured = (NextOver - Over) / (Next - X);
        Slope = (isfinite(Measured) && (Measured > 1.0e-9)) ? Measured : Slope;
        X = Next;
        Over = NextOver;
    }
    if ((Over <= LANDING_TOLERANCE) && (Over >= -ShortAllowed))
    {
        *outFits = true;
        return (float)X;
    }
    if (Over < 0.0)
    {
        Short = X;
        ShortOver = Over;
    }
    else
    {
        Past = X;
        if ((Short < 0.0) && (X > 0.0))
        {
            // Every try overran, by a little after so few of them: twice the step ends short of the end.
            X = fmax(0.0, X - ((2.0 * Over) / Slope));
            Over = Walk(context, (float)X) - Target;
            if (Over < 0.0)
            {
                Short = X;
                ShortOver = Over;
            }
        }
    }
    if ((Short >= 0.0) && (acceptsShort || (ShortOver >= -ShortAllowed) || (Past < 0.0)))
    {
        if (X != Short)
        {
            Walk(context, (float)Short);
        }
        *outFits = true;
        return (float)Short;
    }
    if (X != Past)
    {
        Walk(context, (float)Past);
    }
    *outFits = false;
    return (float)Past;
}

/* Hands the walked items over as pieces. A resized piece's cut comes from x; the last piece ends at the range's end when nothing
 * follows it. */
static void EmitItems(LayoutContext* context, float x)
{
    PathObjectLayout* Self = context->Self;
    const LayoutItem* Items = (const LayoutItem*)Self->_items._data;
    size_t Count = Self->_items._count;
    size_t Last = Count;
    while ((Last > 0U) && Items[Last - 1U].IsGap && (Items[Last - 1U].B == 0.0f) && (Items[Last - 1U].A == 0.0f))
    {
        Last--;
    }
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const LayoutItem* Item = &Items[Index];
        if (Item->IsGap)
        {
            continue;
        }
        if (Self->_pieces._count >= context->Input->MaxPieces)
        {
            Self->_wasLimited = true;
            return;
        }
        PathObjectPiece Piece =
        {
            .Slot = Item->Slot, .Model = Item->Model, .Index = Item->Index, .Span = context->SpanIndex,
            .Start = fmin(fmax(Item->Start, 0.0), context->Span->Length), .End = fmin(Item->End, context->Span->Length),
            .Nominal = Item->Nominal, .IsJoint = false, .IsResized = Item->B > 0.0f
        };
        if (((Index + 1U) == Last) && (Piece.End > (context->RangeEnd - LANDING_TOLERANCE)))
        {
            Piece.End = context->RangeEnd; // the solve stops within its tolerance: the last piece lands exactly
        }
        Piece.End = fmax(Piece.End, Piece.Start);
        if (Item->IsCutAtStart || Item->IsCutAtEnd)
        {
            float Taken = 1.0f - fminf(fmaxf(x, 0.0f), 1.0f);
            float Shares = (Item->IsCutAtStart && Item->IsCutAtEnd) ? 0.5f : 1.0f;
            Piece.CutStart = Item->IsCutAtStart ? (Taken * Shares) : 0.0f;
            Piece.CutEnd = Item->IsCutAtEnd ? (Taken * Shares) : 0.0f;
        }
        if (!GenericBuffer_AddLast(&Self->_pieces, &Piece))
        {
            context->HasFailed = true;
            return;
        }
    }
}


// Static functions: the fits.
static void AddStartCap(LayoutContext* context)
{
    if (context->HasStartCap && IsSlotUsable(context, PathObjectSlot_StartCap))
    {
        AddPiece(context, PathObjectSlot_StartCap, 0U, false);
    }
}

static void AddEndCap(LayoutContext* context)
{
    if (context->HasEndCap && IsSlotUsable(context, PathObjectSlot_EndCap))
    {
        AddPiece(context, PathObjectSlot_EndCap, 0U, false);
    }
}

/* Whether a build has walked as much as it may: it says so and stops. */
static bool HasWalkedTooFar(const LayoutContext* context)
{
    if (context->Self->_walkedItems < PATH_OBJECT_LAYOUT_MAX_WALKED_ITEMS)
    {
        return false;
    }
    context->Self->_wasLimited = true;
    return true;
}

/* A span that shows only its caps, where they fit. */
static void FillCapsOnly(LayoutContext* context)
{
    GenericBuffer_Clear(&context->Self->_items);
    AddStartCap(context);
    AddGap(context, 0.0f, 1.0f);
    AddEndCap(context);
    bool Fits = false;
    float X = SolveX(context, true, &Fits);
    if (Fits && (X >= 0.0f))
    {
        EmitItems(context, X);
    }
}

/* A span the single slot fills: scaled to the span while it stretches, otherwise in its middle. */
static void FillSingle(LayoutContext* context)
{
    GenericBuffer_Clear(&context->Self->_items);
    const PathObjectLayoutSlot* Slot = &context->Input->Slots[PathObjectSlot_Single];
    if (Slot->Stretches)
    {
        AddPiece(context, PathObjectSlot_Single, 0U, true);
    }
    else
    {
        AddGap(context, 0.0f, 0.5f);
        AddPiece(context, PathObjectSlot_Single, 0U, false);
        AddGap(context, 0.0f, 0.5f);
    }
    bool Fits = false;
    float X = SolveX(context, true, &Fits);
    if (Fits)
    {
        EmitItems(context, X);
    }
}

/* What a span does where one middle does not fit: true when it was handled and nothing more is to be done. */
static bool FillTooShort(LayoutContext* context)
{
    switch (context->Settings->WhenTooShort)
    {
        case PathObjectTooShort_Squeeze:
            return false;
        case PathObjectTooShort_Single:
            if (IsSlotUsable(context, PathObjectSlot_Single))
            {
                FillSingle(context);
            }
            else
            {
                FillCapsOnly(context);
            }
            return true;
        case PathObjectTooShort_CapsOnly:
            FillCapsOnly(context);
            return true;
        case PathObjectTooShort_Nothing:
            return true;
    }
    return true;
}

/* Whether the middle @p index of @p count is one the fit resizes. */
static bool IsMiddleResized(const LayoutContext* context, uint32_t index, uint32_t count)
{
    switch (context->Settings->Fit)
    {
        case PathObjectFit_Stretch:
            return context->Input->Slots[PathObjectSlot_Middle].Stretches;
        case PathObjectFit_ResizeEnds:
        {
            bool IsFirst = index == 0U;
            bool IsLast = (index + 1U) == count;
            switch (context->Settings->ResizedEnds)
            {
                case PathObjectResizedEnds_Start: return IsFirst;
                case PathObjectResizedEnds_End: return IsLast;
                case PathObjectResizedEnds_Both: return IsFirst || IsLast;
            }
            return false;
        }
        case PathObjectFit_Fixed:
            return false;
    }
    return false;
}

/* The items of a fit that resizes, for @p count middles. */
static void BuildResizingItems(LayoutContext* context, uint32_t count)
{
    GenericBuffer_Clear(&context->Self->_items);
    bool IsStretch = context->Settings->Fit == PathObjectFit_Stretch;
    bool IsCut = !IsStretch && (context->Settings->ResizeMethod == PathObjectResizeMethod_Cut);
    PathObjectResizedEnds Ends = context->Settings->ResizedEnds;
    if (context->HasStartCap && IsSlotUsable(context, PathObjectSlot_StartCap))
    {
        AddPiece(context, PathObjectSlot_StartCap, 0U, IsStretch && context->Input->Slots[PathObjectSlot_StartCap].Stretches);
    }
    uint32_t SeparatorIndex = 0U;
    for (uint32_t Middle = 0U; (Middle < count) && !context->HasFailed; Middle++)
    {
        bool IsResized = IsMiddleResized(context, Middle, count);
        AddPiece(context, PathObjectSlot_Middle, Middle, IsResized);
        if (IsCut && IsResized)
        {
            LayoutItem* Item = (LayoutItem*)GenericBuffer_GetPointerToLast(&context->Self->_items);
            bool IsFirst = Middle == 0U;
            bool IsLast = (Middle + 1U) == count;
            Item->IsCutAtStart = IsFirst && (Ends != PathObjectResizedEnds_End);
            Item->IsCutAtEnd = IsLast && (Ends != PathObjectResizedEnds_Start);
        }
        if (IsSeparatorAfter(context, Middle, count))
        {
            AddPiece(context, PathObjectSlot_Separator, SeparatorIndex,
                IsStretch && context->Input->Slots[PathObjectSlot_Separator].Stretches);
            SeparatorIndex++;
        }
    }
    if (context->HasEndCap && IsSlotUsable(context, PathObjectSlot_EndCap))
    {
        AddPiece(context, PathObjectSlot_EndCap, 0U, IsStretch && context->Input->Slots[PathObjectSlot_EndCap].Stretches);
    }
}

/* The next count to try after the sequence of @p count middles overran the range: as far lower as the overrun says (a long
 * winding span's chords overrun by many pieces), at least one lower. */
static uint32_t LowerCountByOverrun(const LayoutContext* context, uint32_t count)
{
    const LayoutItem* Items = (const LayoutItem*)context->Self->_items._data;
    size_t ItemCount = context->Self->_items._count;
    double Overrun = (ItemCount > 0U) ? fmax(Items[ItemCount - 1U].End - context->RangeEnd, 0.0) : 0.0;
    double Walked = (context->RangeEnd - context->RangeStart) + Overrun;
    double Drop = (Walked > 0.0) ? floor((Overrun * (double)count) / Walked) : 0.0;
    uint32_t Lower = (Drop < (double)count) ? (count - (uint32_t)Drop) : 1U;
    Lower = (Lower < count) ? Lower : (count - 1U);
    return (Lower > 0U) ? Lower : 1U;
}

/* The most middles a fit that resizes can hold, whatever it is asked: each at the smallest size it may take when it stretches,
 * and at its own size otherwise (only the end pieces resize). A count of many more would walk pieces that can never fit. */
static uint32_t GetMostResizedMiddles(const LayoutContext* context, MiddleCount measured, float minimumX)
{
    double Whole = (double)measured.Whole + 3.0;
    bool DoAllResize = (context->Settings->Fit == PathObjectFit_Stretch)
        && context->Input->Slots[PathObjectSlot_Middle].Stretches;
    double Most = (DoAllResize && (minimumX > 0.0f)) ? (Whole / (double)minimumX) : Whole;
    return (Most < (double)UINT32_MAX) ? (uint32_t)Most : UINT32_MAX;
}

/* The stretch and resize_ends fits. */
static void FillResizing(LayoutContext* context)
{
    const PathObjectSettings* Settings = context->Settings;
    bool IsCut = (Settings->Fit == PathObjectFit_ResizeEnds) && (Settings->ResizeMethod == PathObjectResizeMethod_Cut);
    MiddleCount Measured = CountMiddles(context, 0.0f);
    uint32_t Count = Settings->UsesCount ? Settings->Count
        : RoundCount(Measured, IsCut ? PathObjectRounding_Up : Settings->Rounding);
    if (Count > context->Input->MaxPieces)
    {
        Count = (uint32_t)context->Input->MaxPieces;
        context->Self->_wasLimited = true;
    }
    if ((Measured.Whole == 0U) && FillTooShort(context))
    {
        return;
    }
    Count = (Count > 0U) ? Count : 1U;
    if (!Settings->UsesCount && (Measured.Whole >= context->Input->MaxPieces))
    {
        // As many as a layout may hold cannot fill it: they lie at their own lengths, not stretched out over the rest.
        context->Self->_wasLimited = true;
        BuildResizingItems(context, Count);
        Walk(context, 1.0f);
        EmitItems(context, 1.0f);
        return;
    }
    float MinimumX = IsCut ? 0.0f : Settings->MinimumStretch;
    uint32_t Most = GetMostResizedMiddles(context, Measured, MinimumX);
    Count = (Count < Most) ? Count : Most;
    bool DoAllStretch = (Settings->Fit == PathObjectFit_Stretch) && context->Input->Slots[PathObjectSlot_Middle].Stretches;
    for (int Retry = 0; Retry < MAX_COUNT_RETRIES; Retry++)
    {
        if ((Count == 1U) && IsSlotUsable(context, PathObjectSlot_Single))
        {
            FillSingle(context);
            return;
        }
        BuildResizingItems(context, Count);
        if (context->HasFailed || HasWalkedTooFar(context))
        {
            return;
        }
        bool Fits = false;
        float X = SolveX(context, (Retry + 1) == MAX_COUNT_RETRIES, &Fits);
        // A cut piece of hardly any length is no piece: a count one lower ends flush instead.
        bool IsSliver = IsCut && (X < 0.01f);
        if (Fits && !IsSliver && (X >= MinimumX))
        {
            EmitItems(context, X);
            return;
        }
        if (Count == 1U)
        {
            break;
        }
        if (!Fits)
        {
            Count = LowerCountByOverrun(context, Count);
        }
        else if (DoAllStretch && (MinimumX > 0.0f))
        {
            // Every middle shrinks alike: how far under its smallest size x came says how many fit.
            double AtSmallest = floor(((double)Count * (double)X / (double)MinimumX) + 1.0e-6);
            uint32_t Lower = (AtSmallest >= 1.0) ? (uint32_t)AtSmallest : 1U;
            Count = (Lower < Count) ? Lower : (Count - 1U);
        }
        else
        {
            Count--;
        }
    }
    FillCapsOnly(context);
}

/* The shares of the fixed fit's leftover: before the first piece, between pieces, after the last. */
static void GetAlignShares(PathObjectAlign align, uint32_t count, float* outBefore, float* outBetween, float* outAfter)
{
    *outBefore = 0.0f;
    *outBetween = 0.0f;
    *outAfter = 0.0f;
    switch (align)
    {
        case PathObjectAlign_Start: *outAfter = 1.0f; return;
        case PathObjectAlign_End: *outBefore = 1.0f; return;
        case PathObjectAlign_Center: *outBefore = 0.5f; *outAfter = 0.5f; return;
        case PathObjectAlign_Between:
            *outBetween = 1.0f;
            if (count <= 1U)
            {
                *outBefore = 0.5f; // one piece cannot be flush at both ends: it lies in the middle
                *outAfter = 0.5f;
            }
            return;
        case PathObjectAlign_Around: *outBefore = 0.5f; *outBetween = 1.0f; *outAfter = 0.5f; return;
        case PathObjectAlign_Evenly: *outBefore = 1.0f; *outBetween = 1.0f; *outAfter = 1.0f; return;
    }
}

/* The items of the fixed fit, for @p count middles. */
static void BuildFixedItems(LayoutContext* context, uint32_t count, float gap)
{
    GenericBuffer_Clear(&context->Self->_items);
    float Before = 0.0f;
    float Between = 0.0f;
    float After = 0.0f;
    GetAlignShares(context->Settings->Align, count, &Before, &Between, &After);
    bool AreCapsAtPath = context->Settings->CapsAt == PathObjectCapsAt_Path;
    if (AreCapsAtPath)
    {
        AddStartCap(context);
    }
    AddGap(context, 0.0f, Before);
    if (!AreCapsAtPath)
    {
        AddStartCap(context);
    }
    uint32_t SeparatorIndex = 0U;
    for (uint32_t Middle = 0U; (Middle < count) && !context->HasFailed; Middle++)
    {
        AddPiece(context, PathObjectSlot_Middle, Middle, false);
        if ((Middle + 1U) >= count)
        {
            break;
        }
        if (IsSeparatorAfter(context, Middle, count))
        {
            AddGap(context, 0.5f * gap, 0.5f * Between);
            AddPiece(context, PathObjectSlot_Separator, SeparatorIndex, false);
            SeparatorIndex++;
            AddGap(context, 0.5f * gap, 0.5f * Between);
        }
        else
        {
            AddGap(context, gap, Between);
        }
    }
    if (!AreCapsAtPath)
    {
        AddEndCap(context);
    }
    AddGap(context, 0.0f, After);
    if (AreCapsAtPath)
    {
        AddEndCap(context);
    }
}

/* The fixed fit. */
static void FillFixed(LayoutContext* context)
{
    const PathObjectSettings* Settings = context->Settings;
    float Gap = Settings->UsesGap ? Settings->Gap : 0.0f;
    MiddleCount Measured = CountMiddles(context, Gap);
    uint32_t Count = Measured.Whole; // at most the count asked for: the measure stops there
    if ((Measured.Whole >= context->Input->MaxPieces) && !Settings->UsesCount)
    {
        context->Self->_wasLimited = true;
    }
    if (Count == 0U)
    {
        if (FillTooShort(context))
        {
            return;
        }
        if (IsSlotUsable(context, PathObjectSlot_Single))
        {
            FillSingle(context); // a span of one middle draws the single, as the fits that resize do
            return;
        }
        // Squeezed: one middle scaled down to fill what is left between the caps.
        GenericBuffer_Clear(&context->Self->_items);
        AddStartCap(context);
        AddPiece(context, PathObjectSlot_Middle, 0U, true);
        AddEndCap(context);
        bool Fits = false;
        float X = SolveX(context, true, &Fits);
        if (Fits && (X >= Settings->MinimumStretch))
        {
            EmitItems(context, X);
        }
        else
        {
            FillCapsOnly(context);
        }
        return;
    }
    if ((Count == 1U) && IsSlotUsable(context, PathObjectSlot_Single))
    {
        FillSingle(context);
        return;
    }
    for (int Retry = 0; Retry < MAX_COUNT_RETRIES; Retry++)
    {
        BuildFixedItems(context, Count, Gap);
        if (context->HasFailed || HasWalkedTooFar(context))
        {
            return;
        }
        bool Fits = false;
        float X = SolveX(context, (Retry + 1) == MAX_COUNT_RETRIES, &Fits);
        if (Fits)
        {
            EmitItems(context, X);
            return;
        }
        if (Count == 1U)
        {
            break;
        }
        Count = LowerCountByOverrun(context, Count); // chords walk farther along a bend than their lengths say
    }
    FillCapsOnly(context);
}

/* Fills the range of the context's span. */
static void FillSpan(LayoutContext* context)
{
    if (!IsSlotUsable(context, PathObjectSlot_Middle))
    {
        FillCapsOnly(context);
        return;
    }
    const PathObjectLayoutSlot* Middle = &context->Input->Slots[PathObjectSlot_Middle];
    // A middle of no length stands on a point: a row of lamps, spaced by its gap or its count; nothing else can tell how
    // many of them to make.
    const PathObjectSettings* Settings = context->Settings;
    bool IsSpacedApart = (Settings->Fit == PathObjectFit_Fixed) && (Settings->UsesCount
        || (Settings->UsesGap && (Settings->Gap >= PATH_OBJECT_LAYOUT_MIN_MIDDLE_LENGTH)));
    for (uint32_t ModelIndex = 0U; (ModelIndex < Middle->ModelCount) && !IsSpacedApart; ModelIndex++)
    {
        if (Middle->Models[ModelIndex].Length < PATH_OBJECT_LAYOUT_MIN_MIDDLE_LENGTH)
        {
            context->Self->_hasZeroLengthMiddle = true;
            FillCapsOnly(context);
            return;
        }
    }
    switch (context->Settings->Fit)
    {
        case PathObjectFit_Stretch:
        case PathObjectFit_ResizeEnds:
            FillResizing(context);
            return;
        case PathObjectFit_Fixed:
            FillFixed(context);
            return;
    }
}


// Static functions: runs.
/* What stands at the start of a section where two spans meet: the corner slot at a corner node, else the separator; false
 * for nothing. */
static bool GetJointSlot(const LayoutContext* context, uint32_t section, PathObjectSlot* outSlot)
{
    const PathCurveSection* Section = PathCurve_GetSection(context->Curve, section);
    bool IsCorner = (Section->FirstNode < context->Input->NodeCount)
        && (context->Input->Nodes[Section->FirstNode].Mode == PathNodeMode_Corner);
    if (IsCorner && IsSlotUsable(context, PathObjectSlot_Corner))
    {
        *outSlot = PathObjectSlot_Corner;
        return true;
    }
    if (IsSlotUsable(context, PathObjectSlot_Separator))
    {
        *outSlot = PathObjectSlot_Separator;
        return true;
    }
    return false;
}

/* The model of the joint at the start of a section: a choice of the section's, so the two spans it stands between agree. */
static uint32_t PickJointModel(const LayoutContext* context, PathObjectSlot slot, uint32_t section)
{
    return PickModelIn(context, slot, section, section);
}

/* The length of the joint at the start of a section, in metres at its node's scale; 0 without one. */
static double GetJointLength(const LayoutContext* context, uint32_t section)
{
    PathObjectSlot Slot = PathObjectSlot_Separator;
    if (!GetJointSlot(context, section, &Slot))
    {
        return 0.0;
    }
    PathCurvePoint Point;
    double Scale = 1.0;
    if (PathCurve_GetSectionPoint(context->Curve, section, 0.0, &Point))
    {
        Scale = (double)fmaxf(Point.Scale, PATH_OBJECT_LAYOUT_MIN_SCALE);
    }
    return (double)GetModelLength(context, Slot, PickJointModel(context, Slot, section)) * Scale;
}

/* Puts the joint at the start of @p section down as a piece at the end of the context's span. */
static void EmitJoint(LayoutContext* context, uint32_t section, uint32_t index)
{
    PathObjectSlot Slot = PathObjectSlot_Separator;
    if (!GetJointSlot(context, section, &Slot))
    {
        return;
    }
    if (context->Self->_pieces._count >= context->Input->MaxPieces)
    {
        context->Self->_wasLimited = true;
        return;
    }
    uint32_t ModelIndex = PickJointModel(context, Slot, section);
    PathObjectPiece Piece =
    {
        .Slot = Slot, .Model = ModelIndex, .Index = index, .Span = context->SpanIndex, .Start = context->Span->Length,
        .End = context->Span->Length, .Nominal = GetModelLength(context, Slot, ModelIndex), .IsJoint = true
    };
    if (!GenericBuffer_AddLast(&context->Self->_pieces, &Piece))
    {
        context->HasFailed = true;
    }
}

/* Lays out one span of a run: its sequence between the joints on either side, then the joint after it. */
static void LayOutSpan(LayoutContext* context, PathObjectSpan* span, uint32_t section, uint32_t nextSection, bool hasJointBefore,
    bool hasJointAfter, bool isRunStart, bool isRunEnd, uint32_t jointIndex)
{
    PathObjectLayout* Self = context->Self;
    if (Self->_wasLimited)
    {
        return; // full: a hostile path's other sections are not walked at all
    }
    if (!GenericBuffer_AddLast(&Self->_spans, span))
    {
        context->HasFailed = true;
        return;
    }
    const PathObjectSpan* Stored = (const PathObjectSpan*)GenericBuffer_GetPointerToLast(&Self->_spans);
    context->Span = Stored;
    context->SpanIndex = (uint32_t)(Self->_spans._count - 1U);
    context->HasStartCap = isRunStart;
    context->HasEndCap = isRunEnd;
    double Before = hasJointBefore ? GetJointLength(context, section) : 0.0;
    double After = hasJointAfter ? GetJointLength(context, nextSection) : 0.0;
    context->RangeStart = 0.5 * Before;
    context->RangeEnd = span->Length - (0.5 * After);
    if (context->RangeEnd > context->RangeStart)
    {
        FillSpan(context);
    }
    if (hasJointAfter && !context->HasFailed)
    {
        EmitJoint(context, nextSection, jointIndex);
    }
}

/* Lays out one run: @p count sections from @p first (wrapping), those of no length left out. */
static void LayOutRun(LayoutContext* context, uint32_t first, uint32_t count, bool isLoop)
{
    const PathCurve* Curve = context->Curve;
    uint32_t SectionCount = PathCurve_GetSectionCount(Curve);
    // The sections that hold anything, by their offsets in the run.
    uint32_t FirstHolding = count;
    uint32_t LastHolding = 0U;
    for (uint32_t Offset = 0U; Offset < count; Offset++)
    {
        if (PathCurve_GetSection(Curve, (first + Offset) % SectionCount)->Length >= MIN_SPAN_LENGTH)
        {
            FirstHolding = (FirstHolding == count) ? Offset : FirstHolding;
            LastHolding = Offset;
        }
    }
    if (FirstHolding == count)
    {
        return;
    }
    if (!context->Settings->FitsEachSection)
    {
        const PathCurveSection* Start = PathCurve_GetSection(Curve, (first + FirstHolding) % SectionCount);
        PathObjectSpan Span = { .FirstSection = (first + FirstHolding) % SectionCount,
            .LastSection = (first + LastHolding) % SectionCount, .Start = Start->StartDistance, .Length = 0.0 };
        for (uint32_t Offset = FirstHolding; Offset <= LastHolding; Offset++)
        {
            Span.Length += PathCurve_GetSection(Curve, (first + Offset) % SectionCount)->Length;
        }
        // A loop's one span meets itself where it closes.
        Span.HasSectionBefore = isLoop;
        Span.SectionBefore = Span.LastSection;
        Span.HasSectionAfter = isLoop;
        Span.SectionAfter = Span.FirstSection;
        LayOutSpan(context, &Span, Span.FirstSection, Span.FirstSection, isLoop, isLoop, !isLoop, !isLoop, 0U);
        return;
    }
    uint32_t Joint = 0U;
    uint32_t PreviousSection = (first + LastHolding) % SectionCount; // a loop's first span follows its last
    for (uint32_t Offset = FirstHolding; (Offset <= LastHolding) && !context->HasFailed; Offset++)
    {
        uint32_t Section = (first + Offset) % SectionCount;
        const PathCurveSection* Held = PathCurve_GetSection(Curve, Section);
        if (Held->Length < MIN_SPAN_LENGTH)
        {
            continue;
        }
        // The next section that holds anything: a corner pair at one place shows one joint.
        uint32_t NextOffset = Offset + 1U;
        while ((NextOffset <= LastHolding)
            && (PathCurve_GetSection(Curve, (first + NextOffset) % SectionCount)->Length < MIN_SPAN_LENGTH))
        {
            NextOffset++;
        }
        bool IsLast = NextOffset > LastHolding;
        uint32_t NextSection = (first + (IsLast ? FirstHolding : NextOffset)) % SectionCount;
        bool HasBefore = isLoop || (Offset != FirstHolding);
        bool HasAfter = isLoop || !IsLast;
        PathObjectSpan Span = { .FirstSection = Section, .LastSection = Section, .Start = Held->StartDistance,
            .Length = Held->Length, .HasSectionBefore = HasBefore, .SectionBefore = PreviousSection, .HasSectionAfter = HasAfter,
            .SectionAfter = NextSection };
        LayOutSpan(context, &Span, Section, NextSection, HasBefore, HasAfter, !isLoop && (Offset == FirstHolding),
            !isLoop && IsLast, Joint);
        PreviousSection = Section;
        Joint++;
    }
}

/* Every run of the curve. */
static void LayOutRuns(LayoutContext* context)
{
    const PathCurve* Curve = context->Curve;
    uint32_t SectionCount = PathCurve_GetSectionCount(Curve);
    if (SectionCount == 0U)
    {
        return;
    }
    if (!PathCurve_IsClosed(Curve))
    {
        uint32_t Index = 0U;
        while ((Index < SectionCount) && !context->HasFailed)
        {
            if (PathCurve_GetSection(Curve, Index)->IsSkipped)
            {
                Index++;
                continue;
            }
            uint32_t End = Index;
            while ((End < SectionCount) && !PathCurve_GetSection(Curve, End)->IsSkipped)
            {
                End++;
            }
            LayOutRun(context, Index, End - Index, false);
            Index = End;
        }
        return;
    }
    // A closed path's runs start after a skipped section; with none skipped, the one run is a loop from section 0.
    uint32_t FirstSkipped = SectionCount;
    for (uint32_t Index = 0U; Index < SectionCount; Index++)
    {
        if (PathCurve_GetSection(Curve, Index)->IsSkipped)
        {
            FirstSkipped = Index;
            break;
        }
    }
    if (FirstSkipped == SectionCount)
    {
        LayOutRun(context, 0U, SectionCount, true);
        return;
    }
    uint32_t Offset = 1U;
    while ((Offset < SectionCount) && !context->HasFailed)
    {
        if (PathCurve_GetSection(Curve, (FirstSkipped + Offset) % SectionCount)->IsSkipped)
        {
            Offset++;
            continue;
        }
        uint32_t End = Offset;
        while ((End < SectionCount) && !PathCurve_GetSection(Curve, (FirstSkipped + End) % SectionCount)->IsSkipped)
        {
            End++;
        }
        LayOutRun(context, (FirstSkipped + Offset) % SectionCount, End - Offset, false);
        Offset = End;
    }
}


// Functions.
void PathObjectLayout_Construct(PathObjectLayout* self)
{
    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->_pieces, 0U, sizeof(PathObjectPiece));
    GenericBuffer_AllocateVariable(&self->_spans, 0U, sizeof(PathObjectSpan));
    GenericBuffer_AllocateVariable(&self->_items, 0U, sizeof(LayoutItem));
}

void PathObjectLayout_Deconstruct(PathObjectLayout* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->_pieces._data);
    Memory_Free(self->_spans._data);
    Memory_Free(self->_items._data);
    Memory_Zero(self, sizeof(*self));
}

Error PathObjectLayout_Build(PathObjectLayout* self, const PathObjectLayoutInput* input)
{
    if ((self == NULL) || (input == NULL) || (input->Curve == NULL) || (input->Settings == NULL) || (input->MaxPieces == 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "PathObjectLayout_Build: the layout, the input, its curve, its settings and a limit are required.");
    }
    GenericBuffer_Clear(&self->_pieces);
    GenericBuffer_Clear(&self->_spans);
    GenericBuffer_Clear(&self->_items);
    self->_walkedItems = 0U;
    self->_wasLimited = false;
    self->_hasZeroLengthMiddle = false;
    // Spans stay where they are while a build runs: a context points at the last one, and the buffer may move only as it
    // grows, before the next one is stored and pointed at.
    LayoutContext Context =
    {
        .Self = self, .Input = input, .Curve = input->Curve, .Settings = input->Settings, .Span = NULL, .SpanIndex = 0U,
        .RangeStart = 0.0, .RangeEnd = 0.0, .HasStartCap = false, .HasEndCap = false, .HasFailed = false
    };
    LayOutRuns(&Context);
    if (Context.HasFailed)
    {
        GenericBuffer_Clear(&self->_pieces);
        GenericBuffer_Clear(&self->_spans);
        return Error_Construct2(ErrorCode_BufferTooSmall, "PathObjectLayout_Build: a buffer could not grow.");
    }
    return Error_CreateSuccess();
}

bool PathObjectLayout_GetSpanPoint(const PathCurve* curve, const PathObjectSpan* span, double distance,
    PathCurvePoint* outPoint)
{
    if (distance <= 0.0)
    {
        return PathCurve_GetSectionPoint(curve, span->FirstSection, 0.0, outPoint);
    }
    if (distance >= span->Length)
    {
        uint32_t Last = span->LastSection;
        if (Last >= PathCurve_GetSectionCount(curve))
        {
            return false;
        }
        return PathCurve_GetSectionPoint(curve, Last, PathCurve_GetSection(curve, Last)->Length, outPoint);
    }
    if (span->FirstSection == span->LastSection)
    {
        return PathCurve_GetSectionPoint(curve, span->FirstSection, distance, outPoint);
    }
    return PathCurve_GetPointAt(curve, span->Start + distance, outPoint);
}

float PathObjectLayout_GetRandom(uint32_t seed, PathObjectSlot slot, uint32_t span, uint32_t index, uint32_t salt)
{
    uint64_t Value = Mix(((uint64_t)seed << 32) | (uint64_t)index);
    Value = Mix(Value ^ (((uint64_t)span << 32) | (((uint64_t)slot & 0xFFFFU) << 16) | ((uint64_t)salt & 0xFFFFU)));
    return (float)(Value >> (64U - RANDOM_BITS)) / (float)(1U << RANDOM_BITS);
}
