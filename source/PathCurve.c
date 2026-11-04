// A line through nodes as pure maths (PathCurve.h): its shape as lines and cubic Beziers, arc length by Gauss-Legendre over
// a table per cubic, frames, values, sections, samples, the nearest point and the region polygon.

#include <math.h>
#include "PathCurve.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Macros.
/** How far a cubic's table of chords may leave the curve, in metres, by Wang's bound: what the region polygon holds to. */
#define TABLE_FLATNESS 0.01
/** A cubic's table has this many intervals at the least and the most: the most bounds a hostile path's memory. */
#define TABLE_MIN_INTERVALS 4U
#define TABLE_MAX_INTERVALS 32U
/** Wang's bound for a cubic, n^2 >= (3 * 2 / 8) * M / tolerance, without the tolerance. */
#define WANG_CUBIC_FACTOR 0.75
/** Solving a parameter for a distance: how close counts, in metres, and how many steps at the most. */
#define ARC_TOLERANCE 1e-9
#define ARC_MAX_STEPS 32
/** How near zero a tangent's X and Z may lie before the upright frame counts it as vertical (squared, of a unit vector). */
#define UPRIGHT_VERTICAL_SQUARED 1e-12
/** How near zero a cubic's derivative may lie before its tangent comes from a secant instead (squared, in metres). */
#define TANGENT_SPEED_SQUARED 1e-16
/** The secant's half width in the parameter. */
#define TANGENT_SECANT 1e-4
/** What is left of a vector along an axis counts as nothing below this share of it (squared): it is rounding, with no
 *  direction. */
#define SQUARE_REST_SQUARED 1e-12
/** Golden section: how many steps refine the nearest point on a cubic (0.618^48 of the bracket), and the ratio. */
#define NEAREST_REFINE_STEPS 48
#define GOLDEN_RATIO 0.6180339887498949
/** Sampling: how many times one spacing step may be halved, and the shortest step worth halving. */
#define SAMPLE_MAX_DEPTH 12
#define SAMPLE_MIN_STEP 1e-4
/** Values between nodes closer than this, in metres, do not interpolate: a slope over it would be no slope at all. */
#define VALUE_MIN_LENGTH 1e-9
/** How far in from a segment's end its upright right is looked for, in its parameter, before its table's steps. */
#define END_RIGHT_PROBE_COUNT 4U


// Types.
/* One segment: a line (IsStraight, P0 to P3) or a cubic Bezier, and where it lies along the path. */
struct PathCurveSegmentStruct
{
    Vector3Double P0;
    Vector3Double P1;
    Vector3Double P2;
    Vector3Double P3;
    /** The distance of its start from node 0, and its length. */
    double StartDistance;
    double Length;
    /** The tangent where the shape gives none (a zero-length segment, a cubic with all its control points in a line
     *  doubling back): the chord's or a neighbour's. */
    Vector3 FallbackTangent;
    /** The upright frame's right on a vertical run: what came before it. */
    Vector3 FallbackRight;
    /** Its entries in the table, IntervalCount + 1 of them from FirstEntry (even steps of the parameter). */
    uint32_t FirstEntry;
    uint32_t IntervalCount;
    /** The section it belongs to, and the nodes at its ends. */
    uint32_t Section;
    uint32_t Node;
    uint32_t NextNode;
    bool IsStraight;
};

/* One node's values and their slopes per metre, for the monotone cubic between nodes. */
struct PathCurveNodeValuesStruct
{
    float Roll;
    float Width;
    float Scale;
    double RollSlope;
    double WidthSlope;
    double ScaleSlope;
};

/* What a first pass over the nodes works out, so the block is allocated once at its size. */
typedef struct BuildCountsStruct
{
    uint32_t SegmentCount;
    uint32_t SectionCount;
    uint32_t EntryCount;
    uint32_t RegionPointCount;
} BuildCounts;

/* Sampling's state over one call. */
typedef struct SampleWriterStruct
{
    const PathCurve* Curve;
    GenericBuffer* Out;
    const PathSampleOptions* Options;
    double CosMaxTurn;
    /** Where this call's samples start in Out, and the most elements Out may hold. */
    size_t First;
    size_t Limit;
} SampleWriter;


// Fields.
/* Five-point Gauss-Legendre over [-1, 1]: exact for a polynomial speed up to degree 9, and a cubic's speed over one table
 * interval is very nearly one. */
static const double GaussNodes[5] = { 0.0, -0.5384693101056831, 0.5384693101056831, -0.9061798459386640,
    0.9061798459386640 };
static const double GaussWeights[5] = { 0.5688888888888889, 0.4786286704993665, 0.4786286704993665, 0.2369268850561891,
    0.2369268850561891 };
static const double EndRightProbes[END_RIGHT_PROBE_COUNT] = { 0.0, 1e-6, 1e-4, 1e-2 };


// Static functions: small vector helpers.
static Vector3 ToFloats(Vector3Double value)
{
    return (Vector3){ (float)value.X, (float)value.Y, (float)value.Z };
}

static Vector3Double FromFloats(Vector3 value)
{
    return Vector3Double_Create((double)value.x, (double)value.y, (double)value.z);
}

static Vector3Double GetUnitOrZero(Vector3Double value)
{
    return Vector3Double_NormalizeOr(value, Vector3Double_Zero());
}

/* @p vector with its part along the unit @p axis taken out, made unit; false when nothing but rounding is left of it. */
static bool TryGetSquareTo(Vector3Double vector, Vector3Double axis, Vector3Double* outUnit)
{
    Vector3Double Rest = Vector3Double_AddScaled(vector, axis, -Vector3Double_Dot(vector, axis));
    if (Vector3Double_LengthSquared(Rest) <= (SQUARE_REST_SQUARED * Vector3Double_LengthSquared(vector)))
    {
        return false;
    }
    *outUnit = GetUnitOrZero(Rest);
    return true;
}

/* @p vector made unit and square to the unit @p axis; @p fallback the same, or any perpendicular, when nothing is left. */
static Vector3Double Orthonormalize(Vector3Double vector, Vector3Double axis, Vector3Double fallback)
{
    Vector3Double Unit;
    if (TryGetSquareTo(vector, axis, &Unit) || TryGetSquareTo(fallback, axis, &Unit))
    {
        return Unit;
    }
    return GetUnitOrZero(Vector3Double_Perpendicular(axis));
}


// Static functions: a segment's shape.
static Vector3Double GetPosition(const PathCurveSegment* segment, double u)
{
    double T = 1.0 - u;
    if (segment->IsStraight)
    {
        return Vector3Double_Add(Vector3Double_Scale(segment->P0, T), Vector3Double_Scale(segment->P3, u));
    }
    double A = T * T * T;
    double B = 3.0 * T * T * u;
    double C = 3.0 * T * u * u;
    double D = u * u * u;
    return Vector3Double_Create((A * segment->P0.X) + (B * segment->P1.X) + (C * segment->P2.X) + (D * segment->P3.X),
        (A * segment->P0.Y) + (B * segment->P1.Y) + (C * segment->P2.Y) + (D * segment->P3.Y),
        (A * segment->P0.Z) + (B * segment->P1.Z) + (C * segment->P2.Z) + (D * segment->P3.Z));
}

static Vector3Double GetDerivative(const PathCurveSegment* segment, double u)
{
    if (segment->IsStraight)
    {
        return Vector3Double_Subtract(segment->P3, segment->P0);
    }
    double T = 1.0 - u;
    Vector3Double D0 = Vector3Double_Subtract(segment->P1, segment->P0);
    Vector3Double D1 = Vector3Double_Subtract(segment->P2, segment->P1);
    Vector3Double D2 = Vector3Double_Subtract(segment->P3, segment->P2);
    Vector3Double Sum = Vector3Double_Scale(D0, 3.0 * T * T);
    Sum = Vector3Double_AddScaled(Sum, D1, 6.0 * T * u);
    return Vector3Double_AddScaled(Sum, D2, 3.0 * u * u);
}

static double GetSpeed(const PathCurveSegment* segment, double u)
{
    return Vector3Double_Length(GetDerivative(segment, u));
}

/* A point of a cubic relative to its start. Far from the object's origin a position cancels what a retracted handle leaves
 * of the curve's first steps (they are a millionth of a micrometre); differences of the control points do not. */
static Vector3Double GetOffsetFromStart(const PathCurveSegment* segment, double u)
{
    double T = 1.0 - u;
    Vector3Double Offset = Vector3Double_Scale(Vector3Double_Subtract(segment->P1, segment->P0), 3.0 * T * T * u);
    Offset = Vector3Double_AddScaled(Offset, Vector3Double_Subtract(segment->P2, segment->P0), 3.0 * T * u * u);
    return Vector3Double_AddScaled(Offset, Vector3Double_Subtract(segment->P3, segment->P0), u * u * u);
}

/* The unit tangent: the derivative, where a cusp or a retracted handle stops it a secant around the point, then the
 * segment's fallback. */
static Vector3Double GetTangent(const PathCurveSegment* segment, double u)
{
    Vector3Double Derivative = GetDerivative(segment, u);
    if (Vector3Double_LengthSquared(Derivative) > TANGENT_SPEED_SQUARED)
    {
        return GetUnitOrZero(Derivative);
    }
    Vector3Double Secant = Vector3Double_Subtract(GetOffsetFromStart(segment, Math_MinDouble(u + TANGENT_SECANT, 1.0)),
        GetOffsetFromStart(segment, Math_MaxDouble(u - TANGENT_SECANT, 0.0)));
    Vector3Double Unit = GetUnitOrZero(Secant);
    return (Vector3Double_LengthSquared(Unit) > 0.5) ? Unit : FromFloats(segment->FallbackTangent);
}

static double IntegrateSpeed(const PathCurveSegment* segment, double from, double to)
{
    double Half = 0.5 * (to - from);
    double Middle = 0.5 * (to + from);
    double Sum = 0.0;
    for (int Index = 0; Index < 5; Index++)
    {
        Sum += GaussWeights[Index] * GetSpeed(segment, Middle + (Half * GaussNodes[Index]));
    }
    return Sum * Half;
}

/* The table interval a parameter lies in. */
static uint32_t GetIntervalOf(const PathCurveSegment* segment, double u)
{
    double Scaled = u * (double)segment->IntervalCount;
    if (Scaled <= 0.0)
    {
        return 0U;
    }
    uint32_t Interval = (uint32_t)Scaled;
    return (Interval >= segment->IntervalCount) ? (segment->IntervalCount - 1U) : Interval;
}

/* The distance from a segment's start of a parameter. */
static double GetLocalOfParameter(const PathCurve* self, const PathCurveSegment* segment, double u)
{
    if (segment->IsStraight)
    {
        return u * segment->Length;
    }
    uint32_t Interval = GetIntervalOf(segment, u);
    double From = (double)Interval / (double)segment->IntervalCount;
    return self->_tableDistances[segment->FirstEntry + Interval] + IntegrateSpeed(segment, From, u);
}

/* The parameter at a distance from a segment's start: the table's interval, then Newton inside it, kept in its bracket. */
static double GetParameterOfLocal(const PathCurve* self, const PathCurveSegment* segment, double local)
{
    if (segment->Length <= 0.0)
    {
        return 0.0;
    }
    if (local >= segment->Length)
    {
        return 1.0;
    }
    if (local <= 0.0)
    {
        return 0.0;
    }
    if (segment->IsStraight)
    {
        return local / segment->Length;
    }

    const double* Distances = &self->_tableDistances[segment->FirstEntry];
    uint32_t Low = 0U;
    uint32_t High = segment->IntervalCount;
    while ((High - Low) > 1U)
    {
        uint32_t Middle = Low + ((High - Low) / 2U);
        if (Distances[Middle] <= local)
        {
            Low = Middle;
        }
        else
        {
            High = Middle;
        }
    }
    double Count = (double)segment->IntervalCount;
    double From = (double)Low / Count;
    double BracketLow = From;
    double BracketHigh = (double)(Low + 1U) / Count;
    double Target = local - Distances[Low];
    double Span = Distances[Low + 1U] - Distances[Low];
    double U = (Span > 0.0) ? (From + ((BracketHigh - From) * (Target / Span))) : From;
    for (int Step = 0; Step < ARC_MAX_STEPS; Step++)
    {
        double Excess = IntegrateSpeed(segment, From, U) - Target;
        if (fabs(Excess) <= ARC_TOLERANCE)
        {
            break;
        }
        if (Excess > 0.0)
        {
            BracketHigh = U;
        }
        else
        {
            BracketLow = U;
        }
        double Speed = GetSpeed(segment, U);
        double Next = (Speed > 0.0) ? (U - (Excess / Speed)) : BracketLow;
        U = ((Next > BracketLow) && (Next < BracketHigh)) ? Next : (0.5 * (BracketLow + BracketHigh));
    }
    return U;
}


// Static functions: frames and values.
/* Hermite between two values over a segment's length, kept between them (the slopes are monotone, rounding is not). */
static float GetValue(float from, float to, double fromSlope, double toSlope, double length, double local)
{
    if (length <= VALUE_MIN_LENGTH)
    {
        return to;
    }
    double X = local / length;
    double X2 = X * X;
    double X3 = X2 * X;
    double Value = (((2.0 * X3) - (3.0 * X2) + 1.0) * (double)from) + ((X3 - (2.0 * X2) + X) * length * fromSlope)
        + (((-2.0 * X3) + (3.0 * X2)) * (double)to) + ((X3 - X2) * length * toSlope);
    return (float)Math_ClampDouble(Value, (double)Math_MinFloat(from, to), (double)Math_MaxFloat(from, to));
}

/* The right of an upright frame for a unit tangent: Tangent x +Y; false for a vertical tangent, which has none. */
static bool TryGetUprightRight(Vector3Double tangent, Vector3Double* outRight)
{
    double Across = (tangent.X * tangent.X) + (tangent.Z * tangent.Z);
    if (Across < UPRIGHT_VERTICAL_SQUARED)
    {
        return false;
    }
    double Scale = 1.0 / sqrt(Across);
    *outRight = Vector3Double_Create(-tangent.Z * Scale, 0.0, tangent.X * Scale);
    return true;
}

/* The right of a frame on a segment, before the roll. */
static Vector3Double GetFrameRight(const PathCurve* self, const PathCurveSegment* segment, double u, Vector3Double tangent)
{
    Vector3Double Fallback = FromFloats(segment->FallbackRight);
    if (self->_tableRights == NULL)
    {
        Vector3Double Right;
        return TryGetUprightRight(tangent, &Right) ? Right : Orthonormalize(Fallback, tangent, Fallback);
    }
    uint32_t Interval = GetIntervalOf(segment, u);
    double Amount = (u * (double)segment->IntervalCount) - (double)Interval;
    Vector3Double From = FromFloats(self->_tableRights[segment->FirstEntry + Interval]);
    Vector3Double To = FromFloats(self->_tableRights[segment->FirstEntry + Interval + 1U]);
    return Orthonormalize(Vector3Double_Lerp(From, To, Amount), tangent, From);
}

/* The point on a segment at a parameter and its distance from the segment's start. */
static void FillPoint(const PathCurve* self, uint32_t segmentIndex, double u, double local, double distance,
    PathCurvePoint* outPoint)
{
    const PathCurveSegment* Segment = &self->_segments[segmentIndex];
    const PathCurveNodeValues* From = &self->_nodeValues[Segment->Node];
    const PathCurveNodeValues* To = &self->_nodeValues[Segment->NextNode];
    Vector3Double Tangent = GetTangent(Segment, u);
    Vector3Double Right = GetFrameRight(self, Segment, u, Tangent);
    Vector3Double Up = Vector3Double_Cross(Right, Tangent);
    float Roll = GetValue(From->Roll, To->Roll, From->RollSlope, To->RollSlope, Segment->Length, local);
    double Cos = cos((double)Roll);
    double Sin = sin((double)Roll);
    Vector3Double RolledUp = Vector3Double_Add(Vector3Double_Scale(Up, Cos), Vector3Double_Scale(Right, Sin));
    Vector3Double RolledRight = Vector3Double_Add(Vector3Double_Scale(Right, Cos), Vector3Double_Scale(Up, -Sin));
    *outPoint = (PathCurvePoint)
    {
        .Position = GetPosition(Segment, u), .Tangent = ToFloats(Tangent), .Up = ToFloats(RolledUp),
        .Right = ToFloats(RolledRight), .Roll = Roll,
        .Width = GetValue(From->Width, To->Width, From->WidthSlope, To->WidthSlope, Segment->Length, local),
        .Scale = GetValue(From->Scale, To->Scale, From->ScaleSlope, To->ScaleSlope, Segment->Length, local),
        .Distance = distance, .Segment = segmentIndex, .Section = Segment->Section
    };
}

/* The point of a curve with no segment: its one node, upright along +Z. */
static void FillSingleNodePoint(const PathCurve* self, PathCurvePoint* outPoint)
{
    const PathCurveNodeValues* Values = &self->_nodeValues[0];
    double Cos = cos((double)Values->Roll);
    double Sin = sin((double)Values->Roll);
    *outPoint = (PathCurvePoint)
    {
        .Position = self->_bounds.Min, .Tangent = { 0.0f, 0.0f, 1.0f }, .Up = { (float)(-Sin), (float)Cos, 0.0f },
        .Right = { (float)(-Cos), (float)(-Sin), 0.0f }, .Roll = Values->Roll, .Width = Values->Width,
        .Scale = Values->Scale, .Distance = 0.0, .Segment = 0U, .Section = 0U
    };
}


// Static functions: building the shape.
/* A segment touching a corner is straight, unless a bezier neighbour's handle shapes it. */
static bool IsSegmentStraight(const PathNode* from, const PathNode* to)
{
    return ((from->Mode == PathNodeMode_Corner) || (to->Mode == PathNodeMode_Corner))
        && (from->Mode != PathNodeMode_Bezier) && (to->Mode != PathNodeMode_Bezier);
}

/* The offset of a smooth node's control point on the side of its segment toward its next node (@p isLeaving) or its
 * previous one. Next to a straight the node takes the straight's direction, so it has no kink; otherwise its tangent is
 * the centripetal Catmull-Rom spline's derivative in the knot parameter (the same on both sides, so no kink either), and
 * an open end reflects its neighbour. */
static Vector3Double GetSmoothControl(const PathNode* nodes, uint32_t count, bool isClosed, uint32_t node, bool isLeaving)
{
    bool HasPrevious = isClosed || (node > 0U);
    bool HasNext = isClosed || ((node + 1U) < count);
    Vector3Double Here = nodes[node].Position;
    const PathNode* Previous = HasPrevious ? &nodes[(node + count - 1U) % count] : NULL;
    const PathNode* Next = HasNext ? &nodes[(node + 1U) % count] : NULL;
    Vector3Double ToPrevious = HasPrevious ? Vector3Double_Subtract(Here, Previous->Position) : Vector3Double_Zero();
    Vector3Double ToNext = HasNext ? Vector3Double_Subtract(Next->Position, Here) : Vector3Double_Zero();
    double PreviousLength = Vector3Double_Length(ToPrevious);
    double NextLength = Vector3Double_Length(ToNext);
    double Sign = isLeaving ? 1.0 : -1.0;

    bool IsPreviousStraight = HasPrevious && IsSegmentStraight(Previous, &nodes[node]);
    bool IsNextStraight = HasNext && IsSegmentStraight(&nodes[node], Next);
    double Reach = isLeaving ? NextLength : PreviousLength;
    if (IsPreviousStraight && (PreviousLength > 0.0))
    {
        return Vector3Double_Scale(GetUnitOrZero(ToPrevious), Sign * Reach / 3.0);
    }
    if (IsNextStraight && (NextLength > 0.0))
    {
        return Vector3Double_Scale(GetUnitOrZero(ToNext), Sign * Reach / 3.0);
    }

    double PreviousGap = sqrt(PreviousLength);
    double NextGap = sqrt(NextLength);
    Vector3Double Tangent = Vector3Double_Zero();
    if ((PreviousGap > 0.0) && (NextGap > 0.0))
    {
        Vector3Double Across = Vector3Double_Add(ToPrevious, ToNext);
        Tangent = Vector3Double_Scale(ToPrevious, 1.0 / PreviousGap);
        Tangent = Vector3Double_AddScaled(Tangent, Across, -1.0 / (PreviousGap + NextGap));
        Tangent = Vector3Double_AddScaled(Tangent, ToNext, 1.0 / NextGap);
    }
    else if (NextGap > 0.0)
    {
        Tangent = Vector3Double_Scale(ToNext, 1.0 / NextGap);
    }
    else if (PreviousGap > 0.0)
    {
        Tangent = Vector3Double_Scale(ToPrevious, 1.0 / PreviousGap);
    }
    return Vector3Double_Scale(Tangent, Sign * (isLeaving ? NextGap : PreviousGap) / 3.0);
}

/* A node's control point toward one side, as a position. */
static Vector3Double GetControlPoint(const PathNode* nodes, uint32_t count, bool isClosed, uint32_t node, bool isLeaving)
{
    const PathNode* Node = &nodes[node];
    switch (Node->Mode)
    {
        case PathNodeMode_Corner:
            return Node->Position;
        case PathNodeMode_Bezier:
            return Vector3Double_Add(Node->Position, FromFloats(isLeaving ? Node->OutHandle : Node->InHandle));
        case PathNodeMode_Smooth:
            return Vector3Double_Add(Node->Position, GetSmoothControl(nodes, count, isClosed, node, isLeaving));
    }
    return Node->Position;
}

/* The largest second difference of a cubic's control points, which Wang's bound takes. */
static double GetSegmentBend(const PathCurveSegment* segment)
{
    Vector3Double FirstBend = Vector3Double_Add(Vector3Double_Subtract(segment->P0, segment->P1),
        Vector3Double_Subtract(segment->P2, segment->P1));
    Vector3Double SecondBend = Vector3Double_Add(Vector3Double_Subtract(segment->P1, segment->P2),
        Vector3Double_Subtract(segment->P3, segment->P2));
    return Math_MaxDouble(Vector3Double_Length(FirstBend), Vector3Double_Length(SecondBend));
}

/* A segment's shape from its nodes: everything but its place along the path, its frames and its section. */
static PathCurveSegment MakeSegmentShape(const PathNode* nodes, uint32_t count, bool isClosed, uint32_t index)
{
    uint32_t Next = (index + 1U) % count;
    PathCurveSegment Segment = { .P0 = nodes[index].Position, .P3 = nodes[Next].Position, .Node = index, .NextNode = Next,
        .IntervalCount = 1U };
    Segment.IsStraight = IsSegmentStraight(&nodes[index], &nodes[Next]);
    if (Segment.IsStraight)
    {
        Segment.P1 = Segment.P0;
        Segment.P2 = Segment.P3;
        return Segment;
    }
    Segment.P1 = GetControlPoint(nodes, count, isClosed, index, true);
    Segment.P2 = GetControlPoint(nodes, count, isClosed, Next, false);
    bool IsAtOnePlace = Vector3Double_EqualsClose(Segment.P0, Segment.P3, 0.0)
        && Vector3Double_EqualsClose(Segment.P0, Segment.P1, 0.0) && Vector3Double_EqualsClose(Segment.P0, Segment.P2, 0.0);
    if (IsAtOnePlace)
    {
        Segment.IsStraight = true;
        return Segment;
    }
    double Count = ceil(sqrt(WANG_CUBIC_FACTOR * GetSegmentBend(&Segment) / TABLE_FLATNESS));
    Segment.IntervalCount = (uint32_t)Math_ClampDouble(Count, (double)TABLE_MIN_INTERVALS, (double)TABLE_MAX_INTERVALS);
    return Segment;
}

static bool DoesNodeStartSection(const PathNode* node)
{
    return node->StartsSection || (node->Mode == PathNodeMode_Corner);
}

/* The sizes of everything the block holds. */
static BuildCounts CountBuild(const PathNode* nodes, uint32_t count, bool isClosed)
{
    BuildCounts Counts = { .SegmentCount = isClosed ? ((count > 1U) ? count : 0U) : ((count > 0U) ? (count - 1U) : 0U) };
    bool HasStart = false;
    for (uint32_t Index = 0U; Index < Counts.SegmentCount; Index++)
    {
        PathCurveSegment Shape = MakeSegmentShape(nodes, count, isClosed, Index);
        Counts.EntryCount += Shape.IntervalCount + 1U;
        Counts.RegionPointCount += Shape.IsStraight ? 1U : Shape.IntervalCount;
        if (((Index == 0U) && !isClosed) || DoesNodeStartSection(&nodes[Index]))
        {
            Counts.SectionCount++;
            HasStart = true;
        }
    }
    if ((count > 0U) && !HasStart)
    {
        Counts.SectionCount = 1U;
    }
    if (!isClosed)
    {
        Counts.RegionPointCount = 0U;
    }
    return Counts;
}

/* Each segment's shape, place along the path and arc-length table. */
static void BuildSegments(PathCurve* self, PathCurveSegment* segments, double* distances, const PathNode* nodes,
    uint32_t count, bool isClosed)
{
    double Distance = 0.0;
    uint32_t Entry = 0U;
    for (uint32_t Index = 0U; Index < self->_segmentCount; Index++)
    {
        PathCurveSegment Segment = MakeSegmentShape(nodes, count, isClosed, Index);
        Segment.StartDistance = Distance;
        Segment.FirstEntry = Entry;
        distances[Entry] = 0.0;
        if (Segment.IsStraight)
        {
            Segment.Length = Vector3Double_Distance(Segment.P0, Segment.P3);
            distances[Entry + 1U] = Segment.Length;
        }
        else
        {
            double Sum = 0.0;
            for (uint32_t Interval = 0U; Interval < Segment.IntervalCount; Interval++)
            {
                double Span = (double)Segment.IntervalCount;
                Sum += IntegrateSpeed(&Segment, (double)Interval / Span, (double)(Interval + 1U) / Span);
                distances[Entry + Interval + 1U] = Sum;
            }
            Segment.Length = Sum;
        }
        Entry += Segment.IntervalCount + 1U;
        Distance += Segment.Length;
        segments[Index] = Segment;
    }
    self->_length = Distance;
}

/* The chord's direction, or where a closed cubic's chord has none, toward its first control point off its start. */
static Vector3Double GetShapeDirection(const PathCurveSegment* segment)
{
    Vector3Double Candidates[3] = { Vector3Double_Subtract(segment->P3, segment->P0),
        Vector3Double_Subtract(segment->P1, segment->P0), Vector3Double_Subtract(segment->P2, segment->P0) };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Vector3Double Unit = GetUnitOrZero(Candidates[Index]);
        if (Vector3Double_LengthSquared(Unit) > 0.5)
        {
            return Unit;
        }
    }
    return Vector3Double_Zero();
}

/* A fallback tangent per segment: its own direction, or the nearest neighbour's that has one (those before it first). A
 * path all at one place runs along +Z. */
static void BuildFallbackTangents(const PathCurve* self, PathCurveSegment* segments)
{
    Vector3Double Last = Vector3Double_Zero();
    for (uint32_t Index = 0U; Index < self->_segmentCount; Index++)
    {
        Vector3Double Own = GetShapeDirection(&segments[Index]);
        Last = (Vector3Double_LengthSquared(Own) > 0.5) ? Own : Last;
        segments[Index].FallbackTangent = ToFloats(Last);
    }
    Last = Vector3Double_Create(0.0, 0.0, 1.0);
    for (uint32_t Index = self->_segmentCount; Index > 0U; Index--)
    {
        PathCurveSegment* Segment = &segments[Index - 1U];
        if (Vector3Double_LengthSquared(FromFloats(Segment->FallbackTangent)) > 0.5)
        {
            Last = FromFloats(Segment->FallbackTangent);
        }
        Segment->FallbackTangent = ToFloats(Last);
    }
}

/* The upright right nearest one end of a segment: at the end itself, or where the curve has left the vertical, stepping in
 * from that end. A curve that ends vertical has a right in the limit, which the closest probes reach. */
static bool TryGetEndRight(const PathCurveSegment* segment, bool isEnd, Vector3Double* outRight)
{
    for (uint32_t Index = 0U; Index < END_RIGHT_PROBE_COUNT; Index++)
    {
        double Along = EndRightProbes[Index];
        if (TryGetUprightRight(GetTangent(segment, isEnd ? (1.0 - Along) : Along), outRight))
        {
            return true;
        }
    }
    for (uint32_t Step = 1U; Step < segment->IntervalCount; Step++)
    {
        double Along = (double)Step / (double)segment->IntervalCount;
        if (TryGetUprightRight(GetTangent(segment, isEnd ? (1.0 - Along) : Along), outRight))
        {
            return true;
        }
    }
    return false;
}

/* A fallback right per segment: the upright right at its start, or on a vertical start what the last segment ended with
 * (what comes after for a path that starts vertical, +X for one that stays so). */
static void BuildFallbackRights(const PathCurve* self, PathCurveSegment* segments)
{
    Vector3Double Last = Vector3Double_Create(1.0, 0.0, 0.0);
    for (uint32_t Index = 0U; Index < self->_segmentCount; Index++)
    {
        if (TryGetEndRight(&segments[Index], false, &Last))
        {
            break;
        }
    }
    for (uint32_t Index = 0U; Index < self->_segmentCount; Index++)
    {
        Vector3Double Right;
        if (TryGetUprightRight(GetTangent(&segments[Index], 0.0), &Right))
        {
            Last = Right;
        }
        segments[Index].FallbackRight = ToFloats(Last);
        if (TryGetEndRight(&segments[Index], true, &Right))
        {
            Last = Right;
        }
    }
}

/* A monotone slope at a node from the secants on its sides (Steffen): the smaller secant's size at the most, zero at a
 * peak or a dip, so the cubic between nodes never passes either value. */
static double GetSteffenSlope(float before, float here, float after, double lengthBefore, double lengthAfter)
{
    if ((lengthBefore <= VALUE_MIN_LENGTH) || (lengthAfter <= VALUE_MIN_LENGTH))
    {
        return 0.0;
    }
    double SecantBefore = ((double)here - (double)before) / lengthBefore;
    double SecantAfter = ((double)after - (double)here) / lengthAfter;
    if ((SecantBefore * SecantAfter) <= 0.0)
    {
        return 0.0;
    }
    double Middle = ((SecantBefore * lengthAfter) + (SecantAfter * lengthBefore)) / (lengthBefore + lengthAfter);
    double Sign = (SecantBefore > 0.0) ? 1.0 : -1.0;
    double Size = Math_MinDouble(Math_MinDouble(fabs(SecantBefore), fabs(SecantAfter)), 0.5 * fabs(Middle));
    return 2.0 * Sign * Size;
}

static double GetEndSlope(float from, float to, double length)
{
    return (length > VALUE_MIN_LENGTH) ? (((double)to - (double)from) / length) : 0.0;
}

static void BuildNodeValues(const PathCurve* self, const PathCurveSegment* segments, PathCurveNodeValues* values,
    const PathNode* nodes, uint32_t count, bool isClosed)
{
    for (uint32_t Index = 0U; Index < count; Index++)
    {
        values[Index] = (PathCurveNodeValues){ .Roll = nodes[Index].Roll, .Width = nodes[Index].Width,
            .Scale = nodes[Index].Scale };
    }
    if (self->_segmentCount == 0U)
    {
        return;
    }
    for (uint32_t Index = 0U; Index < count; Index++)
    {
        bool HasBefore = isClosed || (Index > 0U);
        bool HasAfter = isClosed || ((Index + 1U) < count);
        uint32_t Before = (Index + count - 1U) % count;
        uint32_t After = (Index + 1U) % count;
        const PathNode* Node = &nodes[Index];
        if (HasBefore && HasAfter)
        {
            double LengthBefore = segments[Before].Length;
            double LengthAfter = segments[Index].Length;
            values[Index].RollSlope = GetSteffenSlope(nodes[Before].Roll, Node->Roll, nodes[After].Roll, LengthBefore,
                LengthAfter);
            values[Index].WidthSlope = GetSteffenSlope(nodes[Before].Width, Node->Width, nodes[After].Width, LengthBefore,
                LengthAfter);
            values[Index].ScaleSlope = GetSteffenSlope(nodes[Before].Scale, Node->Scale, nodes[After].Scale, LengthBefore,
                LengthAfter);
        }
        else if (HasAfter)
        {
            double Length = segments[Index].Length;
            values[Index].RollSlope = GetEndSlope(Node->Roll, nodes[After].Roll, Length);
            values[Index].WidthSlope = GetEndSlope(Node->Width, nodes[After].Width, Length);
            values[Index].ScaleSlope = GetEndSlope(Node->Scale, nodes[After].Scale, Length);
        }
        else if (HasBefore)
        {
            double Length = segments[Before].Length;
            values[Index].RollSlope = GetEndSlope(nodes[Before].Roll, Node->Roll, Length);
            values[Index].WidthSlope = GetEndSlope(nodes[Before].Width, Node->Width, Length);
            values[Index].ScaleSlope = GetEndSlope(nodes[Before].Scale, Node->Scale, Length);
        }
    }
}

static PathCurveSection MakeSection(const PathNode* node, uint32_t index, double startDistance)
{
    return (PathCurveSection){ .FirstNode = index, .StartDistance = startDistance, .IsSkipped = node->SkipsSection,
        .OverridesSnapsToTerrain = node->OverridesSnapsToTerrain, .SnapsToTerrain = node->SnapsToTerrain };
}

/* The sections and each segment's section. A closed path's last section reaches through the end to the first start. */
static void BuildSections(PathCurve* self, PathCurveSegment* segments, PathCurveSection* sections, const PathNode* nodes,
    bool isClosed)
{
    uint32_t SegmentCount = self->_segmentCount;
    uint32_t Count = 0U;
    for (uint32_t Index = 0U; Index < SegmentCount; Index++)
    {
        if (((Index == 0U) && !isClosed) || DoesNodeStartSection(&nodes[Index]))
        {
            sections[Count] = MakeSection(&nodes[Index], Index, segments[Index].StartDistance);
            Count++;
        }
    }
    if (Count == 0U)
    {
        sections[0] = MakeSection(&nodes[0], 0U, 0.0);
        Count = 1U;
    }
    self->_sectionCount = Count;
    if (SegmentCount == 0U)
    {
        return; // one node: a section of no segments, so a point always names a section that exists
    }
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        PathCurveSection* Section = &sections[Index];
        bool IsLast = (Index + 1U) == Count;
        uint32_t Ending = IsLast ? (isClosed ? (sections[0].FirstNode + SegmentCount) : SegmentCount)
            : sections[Index + 1U].FirstNode;
        double EndDistance = IsLast ? (isClosed ? (sections[0].StartDistance + self->_length) : self->_length)
            : sections[Index + 1U].StartDistance;
        Section->SegmentCount = Ending - Section->FirstNode;
        Section->Length = EndDistance - Section->StartDistance;
        for (uint32_t Step = 0U; Step < Section->SegmentCount; Step++)
        {
            segments[(Section->FirstNode + Step) % SegmentCount].Section = Index;
        }
    }
}

/* @p vector turned by the smallest rotation that takes the unit @p from to the unit @p to. Doubling straight back has no
 * one smallest rotation: it turns about the up, so the up stays up. */
static Vector3Double RotateMinimally(Vector3Double vector, Vector3Double from, Vector3Double to)
{
    Vector3Double Axis = Vector3Double_Cross(from, to);
    double Sin = Vector3Double_Length(Axis);
    double Cos = Vector3Double_Dot(from, to);
    if (Sin < 1e-12)
    {
        return (Cos > 0.0) ? vector : Vector3Double_Negate(vector);
    }
    Vector3Double Unit = Vector3Double_Scale(Axis, 1.0 / Sin);
    Vector3Double Rotated = Vector3Double_Scale(vector, Cos);
    Rotated = Vector3Double_AddScaled(Rotated, Vector3Double_Cross(Unit, vector), Sin);
    return Vector3Double_AddScaled(Rotated, Unit, Vector3Double_Dot(Unit, vector) * (1.0 - Cos));
}

/* One step of the double reflection method (Wang, Juettler, Zheng, Liu 2008), which follows a rotation-minimizing frame to
 * fourth order: a reflection across the plane between the two points, then one that takes the reflected tangent to the
 * new one. */
static Vector3Double StepDoubleReflection(Vector3Double right, Vector3Double fromPoint, Vector3Double fromTangent,
    Vector3Double toPoint, Vector3Double toTangent)
{
    Vector3Double Move = Vector3Double_Subtract(toPoint, fromPoint);
    double MoveSquared = Vector3Double_LengthSquared(Move);
    if (MoveSquared <= 0.0)
    {
        return RotateMinimally(right, fromTangent, toTangent);
    }
    double Factor = 2.0 / MoveSquared;
    Vector3Double ReflectedRight = Vector3Double_AddScaled(right, Move, -Factor * Vector3Double_Dot(Move, right));
    Vector3Double ReflectedTangent = Vector3Double_AddScaled(fromTangent, Move,
        -Factor * Vector3Double_Dot(Move, fromTangent));
    Vector3Double Second = Vector3Double_Subtract(toTangent, ReflectedTangent);
    double SecondSquared = Vector3Double_LengthSquared(Second);
    if (SecondSquared <= 0.0)
    {
        return ReflectedRight;
    }
    return Vector3Double_AddScaled(ReflectedRight, Second,
        (-2.0 / SecondSquared) * Vector3Double_Dot(Second, ReflectedRight));
}

/* The rotation-minimizing rights over the table, from an upright start, a smallest rotation across every kink between
 * segments, and on a closed path the leftover twist spread along the length so the ends meet. */
static void BuildRotationMinimizingRights(const PathCurve* self, const PathCurveSegment* segments, Vector3* rights,
    bool isClosed)
{
    if (self->_segmentCount == 0U)
    {
        return;
    }
    Vector3Double FirstTangent = GetTangent(&segments[0], 0.0);
    Vector3Double Right = Orthonormalize(FromFloats(segments[0].FallbackRight), FirstTangent,
        FromFloats(segments[0].FallbackRight));
    Vector3Double FirstRight = Right;
    Vector3Double LastTangent = FirstTangent;
    for (uint32_t Index = 0U; Index < self->_segmentCount; Index++)
    {
        const PathCurveSegment* Segment = &segments[Index];
        double Count = (double)Segment->IntervalCount;
        Vector3Double Tangent = GetTangent(Segment, 0.0);
        Vector3Double Point = GetPosition(Segment, 0.0);
        Right = Orthonormalize(RotateMinimally(Right, LastTangent, Tangent), Tangent, Right);
        rights[Segment->FirstEntry] = ToFloats(Right);
        for (uint32_t Step = 1U; Step <= Segment->IntervalCount; Step++)
        {
            double U = (double)Step / Count;
            Vector3Double NextPoint = GetPosition(Segment, U);
            Vector3Double NextTangent = GetTangent(Segment, U);
            Right = StepDoubleReflection(Right, Point, Tangent, NextPoint, NextTangent);
            Right = Orthonormalize(Right, NextTangent, Right);
            rights[Segment->FirstEntry + Step] = ToFloats(Right);
            Point = NextPoint;
            Tangent = NextTangent;
        }
        LastTangent = Tangent;
    }
    if (!isClosed || (self->_length <= 0.0))
    {
        return;
    }
    // Back at the start the frame has turned about the first tangent; each entry takes its share of turning it back.
    Vector3Double Arrived = Orthonormalize(RotateMinimally(Right, LastTangent, FirstTangent), FirstTangent, FirstRight);
    double Twist = atan2(Vector3Double_Dot(Vector3Double_Cross(Arrived, FirstRight), FirstTangent),
        Vector3Double_Dot(Arrived, FirstRight));
    for (uint32_t Index = 0U; Index < self->_segmentCount; Index++)
    {
        const PathCurveSegment* Segment = &segments[Index];
        for (uint32_t Step = 0U; Step <= Segment->IntervalCount; Step++)
        {
            double Along = Segment->StartDistance + self->_tableDistances[Segment->FirstEntry + Step];
            double Angle = Twist * (Along / self->_length);
            Vector3Double Tangent = GetTangent(Segment, (double)Step / (double)Segment->IntervalCount);
            Vector3Double Turned = Vector3Double_RotateByAxisAngle(FromFloats(rights[Segment->FirstEntry + Step]), Tangent,
                Angle);
            rights[Segment->FirstEntry + Step] = ToFloats(Turned);
        }
    }
}

/* A closed path's polygon in X and Z: a straight segment's start, a cubic's table points. */
static void BuildRegionPoints(const PathCurve* self, const PathCurveSegment* segments, Vector2Double* points)
{
    uint32_t Count = 0U;
    for (uint32_t Index = 0U; Index < self->_segmentCount; Index++)
    {
        const PathCurveSegment* Segment = &segments[Index];
        uint32_t Steps = Segment->IsStraight ? 1U : Segment->IntervalCount;
        for (uint32_t Step = 0U; Step < Steps; Step++)
        {
            Vector3Double Point = GetPosition(Segment, (double)Step / (double)Segment->IntervalCount);
            points[Count] = Vector2Double_Create(Point.X, Point.Z);
            Count++;
        }
    }
}

static BoundingBoxDouble GetSegmentBounds(const PathCurveSegment* segment)
{
    BoundingBoxDouble Bounds = BoundingBoxDouble_FromCorners(segment->P0, segment->P3);
    Bounds = BoundingBoxDouble_UnionPoint(Bounds, segment->P1);
    return BoundingBoxDouble_UnionPoint(Bounds, segment->P2);
}

static bool IsFloatFinite(float value)
{
    return isfinite(value) != 0;
}

/* Finite and small enough that squares of differences of it cannot overflow. */
static bool IsCoordinateValid(double value)
{
    return isfinite(value) && (fabs(value) <= PATH_CURVE_COORDINATE_LIMIT);
}

static bool IsVectorValid(Vector3Double value)
{
    return IsCoordinateValid(value.X) && IsCoordinateValid(value.Y) && IsCoordinateValid(value.Z);
}

static bool IsNodeValid(const PathNode* node)
{
    bool IsModeKnown = (node->Mode == PathNodeMode_Smooth) || (node->Mode == PathNodeMode_Corner)
        || (node->Mode == PathNodeMode_Bezier);
    return IsModeKnown && IsVectorValid(node->Position) && IsVectorValid(FromFloats(node->InHandle))
        && IsVectorValid(FromFloats(node->OutHandle)) && IsFloatFinite(node->Roll) && IsFloatFinite(node->Width)
        && IsFloatFinite(node->Scale);
}

/* Reserves @p bytes of the block and moves the cursor past them. */
static void* TakeBytes(unsigned char** cursor, size_t bytes)
{
    void* Start = *cursor;
    *cursor += bytes;
    return Start;
}


// Static functions: finding segments.
static double WrapDistance(const PathCurve* self, double distance)
{
    double Length = self->_length;
    if (!self->_isClosed || (Length <= 0.0))
    {
        return Math_ClampDouble(distance, 0.0, Length);
    }
    double Wrapped = fmod(distance, Length);
    if (Wrapped < 0.0)
    {
        Wrapped += Length;
    }
    return (Wrapped >= Length) ? 0.0 : Wrapped;
}

/* The last segment starting at or before a distance; at a node, the one leaving it. A zero-length segment is only ever the
 * answer when nothing before it has a length. */
static uint32_t FindSegment(const PathCurve* self, double distance)
{
    uint32_t Low = 0U;
    uint32_t High = self->_segmentCount;
    while ((High - Low) > 1U)
    {
        uint32_t Middle = Low + ((High - Low) / 2U);
        if (self->_segments[Middle].StartDistance <= distance)
        {
            Low = Middle;
        }
        else
        {
            High = Middle;
        }
    }
    while ((Low > 0U) && (self->_segments[Low].Length <= 0.0))
    {
        Low--;
    }
    return Low;
}

/* Where the Nth segment of a section starts, from the section's start. */
static double GetSectionLocalStart(const PathCurve* self, const PathCurveSection* section, uint32_t step)
{
    const PathCurveSegment* Segment = &self->_segments[(section->FirstNode + step) % self->_segmentCount];
    double Start = Segment->StartDistance;
    bool HasWrapped = (section->FirstNode + step) >= self->_segmentCount;
    return (HasWrapped ? (Start + self->_length) : Start) - section->StartDistance;
}


// Static functions: the nearest point.
static double GetSeparationSquared(Vector3Double a, Vector3Double b, bool ignoresHeight)
{
    double X = a.X - b.X;
    double Y = ignoresHeight ? 0.0 : (a.Y - b.Y);
    double Z = a.Z - b.Z;
    return (X * X) + (Y * Y) + (Z * Z);
}

/* The parameter of a point's nearest spot on the chord from @p a to @p b, by the same measure. */
static double GetChordParameter(Vector3Double a, Vector3Double b, Vector3Double point, bool ignoresHeight)
{
    Vector3Double Along = Vector3Double_Subtract(b, a);
    Vector3Double Toward = Vector3Double_Subtract(point, a);
    if (ignoresHeight)
    {
        Along.Y = 0.0;
        Toward.Y = 0.0;
    }
    double Squared = Vector3Double_LengthSquared(Along);
    return (Squared > 0.0) ? Math_ClampDouble(Vector3Double_Dot(Toward, Along) / Squared, 0.0, 1.0) : 0.0;
}

/* The squared distance from a point to a box, a lower bound for anything inside it. */
static double GetBoxSeparationSquared(BoundingBoxDouble box, Vector3Double point, bool ignoresHeight)
{
    double X = Math_MaxDouble(Math_MaxDouble(box.Min.X - point.X, point.X - box.Max.X), 0.0);
    double Y = ignoresHeight ? 0.0 : Math_MaxDouble(Math_MaxDouble(box.Min.Y - point.Y, point.Y - box.Max.Y), 0.0);
    double Z = Math_MaxDouble(Math_MaxDouble(box.Min.Z - point.Z, point.Z - box.Max.Z), 0.0);
    return (X * X) + (Y * Y) + (Z * Z);
}

/* A golden section for the nearest spot to a point between two parameters. */
static double RefineNearestParameter(const PathCurveSegment* segment, double low, double high, Vector3Double point,
    bool ignoresHeight)
{
    double Left = high - (GOLDEN_RATIO * (high - low));
    double Right = low + (GOLDEN_RATIO * (high - low));
    double LeftSquared = GetSeparationSquared(GetPosition(segment, Left), point, ignoresHeight);
    double RightSquared = GetSeparationSquared(GetPosition(segment, Right), point, ignoresHeight);
    for (int Step = 0; Step < NEAREST_REFINE_STEPS; Step++)
    {
        if (LeftSquared <= RightSquared)
        {
            high = Right;
            Right = Left;
            RightSquared = LeftSquared;
            Left = high - (GOLDEN_RATIO * (high - low));
            LeftSquared = GetSeparationSquared(GetPosition(segment, Left), point, ignoresHeight);
        }
        else
        {
            low = Left;
            Left = Right;
            LeftSquared = RightSquared;
            Right = low + (GOLDEN_RATIO * (high - low));
            RightSquared = GetSeparationSquared(GetPosition(segment, Right), point, ignoresHeight);
        }
    }
    return 0.5 * (low + high);
}

/* How far from a point one chord of a cubic's table lies, squared. */
static double GetChordSeparationSquared(const PathCurveSegment* segment, uint32_t interval, Vector3Double point,
    bool ignoresHeight)
{
    double Count = (double)segment->IntervalCount;
    Vector3Double From = GetPosition(segment, (double)interval / Count);
    Vector3Double To = GetPosition(segment, (double)(interval + 1U) / Count);
    double Along = GetChordParameter(From, To, point, ignoresHeight);
    return GetSeparationSquared(Vector3Double_Lerp(From, To, Along), point, ignoresHeight);
}

/* The parameter of a cubic's nearest spot to a point. A cubic can pass near a point up to three times, and the nearest chord
 * need not hold the nearest pass: every chord nearer than its neighbours, that the table's flatness (Wang's bound) leaves in
 * reach of the best, gets a golden section around it, and the best of those answers. */
static double FindNearestParameter(const PathCurveSegment* segment, Vector3Double point, bool ignoresHeight)
{
    uint32_t Count = segment->IntervalCount;
    double CountValue = (double)Count;
    double BestChord = INFINITY;
    for (uint32_t Interval = 0U; Interval < Count; Interval++)
    {
        BestChord = Math_MinDouble(BestChord, GetChordSeparationSquared(segment, Interval, point, ignoresHeight));
    }
    double Flatness = WANG_CUBIC_FACTOR * GetSegmentBend(segment) / (CountValue * CountValue);
    double Reach = sqrt(BestChord) + (2.0 * Flatness);

    double BestSquared = INFINITY;
    double BestU = 0.0;
    double Before = INFINITY;
    double Here = GetChordSeparationSquared(segment, 0U, point, ignoresHeight);
    for (uint32_t Interval = 0U; Interval < Count; Interval++)
    {
        double After = ((Interval + 1U) < Count) ? GetChordSeparationSquared(segment, Interval + 1U, point, ignoresHeight)
            : INFINITY;
        // By the root, as Reach was made: the best chord's squared distance can round above the square of its own root.
        if ((Here <= Before) && (Here <= After) && (sqrt(Here) <= Reach))
        {
            double Low = (Interval > 0U) ? ((double)(Interval - 1U) / CountValue) : 0.0;
            double High = Math_MinDouble((double)(Interval + 2U) / CountValue, 1.0);
            double U = RefineNearestParameter(segment, Low, High, point, ignoresHeight);
            double Squared = GetSeparationSquared(GetPosition(segment, U), point, ignoresHeight);
            if (Squared < BestSquared)
            {
                BestSquared = Squared;
                BestU = U;
            }
        }
        Before = Here;
        Here = After;
    }
    return BestU;
}


// Static functions: samples.
/* Appends a distance, unless the sums that made it did not reach past the last one (a segment too short to move them). */
static Error AppendSample(SampleWriter* writer, double distance)
{
    size_t Count = writer->Out->_count;
    if ((Count > writer->First) && (distance <= ((const double*)writer->Out->_data)[Count - 1U]))
    {
        return Error_CreateSuccess();
    }
    if (Count >= writer->Limit)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "PathCurve_WriteSamples: the path needs more samples than PATH_CURVE_MAX_SAMPLES at these options.");
    }
    if (!GenericBuffer_AddLast(writer->Out, &distance))
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "PathCurve_WriteSamples: the buffer could not grow.");
    }
    return Error_CreateSuccess();
}

/* How far a point lies from the chord between two others. */
static double GetChordDeviation(Vector3Double point, Vector3Double from, Vector3Double to)
{
    double Along = GetChordParameter(from, to, point, false);
    return Vector3Double_Distance(point, Vector3Double_Lerp(from, to, Along));
}

/* Whether one step of a cubic needs no halving. An S has parallel ends and its middle on its chord, so the turn is read on
 * both halves, and the deviation at the quarters too. */
static bool IsStepFlat(const SampleWriter* writer, const PathCurveSegment* segment, double fromLocal, double toLocal,
    const Vector3Double fromAndTo[2], const Vector3Double tangents[3], Vector3Double middlePoint)
{
    double CosMaxTurn = writer->CosMaxTurn;
    if ((Vector3Double_Dot(tangents[0], tangents[2]) < CosMaxTurn)
        || (Vector3Double_Dot(tangents[0], tangents[1]) < CosMaxTurn)
        || (Vector3Double_Dot(tangents[1], tangents[2]) < CosMaxTurn))
    {
        return false;
    }
    double MaxDeviation = writer->Options->MaxDeviation;
    if (GetChordDeviation(middlePoint, fromAndTo[0], fromAndTo[1]) > MaxDeviation)
    {
        return false;
    }
    double Quarter = 0.25 * (toLocal - fromLocal);
    Vector3Double First = GetPosition(segment, GetParameterOfLocal(writer->Curve, segment, fromLocal + Quarter));
    if (GetChordDeviation(First, fromAndTo[0], fromAndTo[1]) > MaxDeviation)
    {
        return false;
    }
    Vector3Double Third = GetPosition(segment, GetParameterOfLocal(writer->Curve, segment, toLocal - Quarter));
    return GetChordDeviation(Third, fromAndTo[0], fromAndTo[1]) <= MaxDeviation;
}

/* Appends what lies after @p fromLocal up to and including @p toLocal on a cubic, halving while the curve leaves the chord
 * or turns too far. */
static Error SubdivideCubic(SampleWriter* writer, const PathCurveSegment* segment, double base, double fromLocal,
    double toLocal, Vector3Double fromPoint, Vector3Double fromTangent, Vector3Double toPoint, Vector3Double toTangent,
    int depth)
{
    double MiddleLocal = 0.5 * (fromLocal + toLocal);
    double MiddleU = GetParameterOfLocal(writer->Curve, segment, MiddleLocal);
    Vector3Double MiddlePoint = GetPosition(segment, MiddleU);
    Vector3Double MiddleTangent = GetTangent(segment, MiddleU);
    const Vector3Double Ends[2] = { fromPoint, toPoint };
    const Vector3Double Tangents[3] = { fromTangent, MiddleTangent, toTangent };
    if ((depth >= SAMPLE_MAX_DEPTH) || ((toLocal - fromLocal) <= SAMPLE_MIN_STEP)
        || IsStepFlat(writer, segment, fromLocal, toLocal, Ends, Tangents, MiddlePoint))
    {
        return AppendSample(writer, base + toLocal);
    }
    Error Result = SubdivideCubic(writer, segment, base, fromLocal, MiddleLocal, fromPoint, fromTangent, MiddlePoint,
        MiddleTangent, depth + 1);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SubdivideCubic(writer, segment, base, MiddleLocal, toLocal, MiddlePoint, MiddleTangent, toPoint, toTangent,
        depth + 1);
}

/* Appends one segment's samples after its start, @p base being the output's distance of its start. */
static Error WriteSegmentSamples(SampleWriter* writer, const PathCurveSegment* segment, double base)
{
    if (segment->Length <= 0.0)
    {
        return Error_CreateSuccess();
    }
    double Steps = ceil(segment->Length / writer->Options->Spacing);
    if (Steps > (double)PATH_CURVE_MAX_SAMPLES)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "PathCurve_WriteSamples: the path needs more samples than PATH_CURVE_MAX_SAMPLES at these options.");
    }
    size_t Count = (size_t)Math_MaxDouble(Steps, 1.0);
    double Step = segment->Length / (double)Count;
    Error Result = Error_CreateSuccess();
    if (segment->IsStraight)
    {
        for (size_t Index = 1U; (Index <= Count) && (Result.Code == ErrorCode_Success); Index++)
        {
            Result = AppendSample(writer, base + ((Index == Count) ? segment->Length : ((double)Index * Step)));
        }
        return Result;
    }
    double FromLocal = 0.0;
    Vector3Double FromPoint = GetPosition(segment, 0.0);
    Vector3Double FromTangent = GetTangent(segment, 0.0);
    for (size_t Index = 1U; (Index <= Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        double ToLocal = (Index == Count) ? segment->Length : ((double)Index * Step);
        double ToU = GetParameterOfLocal(writer->Curve, segment, ToLocal);
        Vector3Double ToPoint = GetPosition(segment, ToU);
        Vector3Double ToTangent = GetTangent(segment, ToU);
        Result = SubdivideCubic(writer, segment, base, FromLocal, ToLocal, FromPoint, FromTangent, ToPoint, ToTangent, 0);
        FromLocal = ToLocal;
        FromPoint = ToPoint;
        FromTangent = ToTangent;
    }
    return Result;
}

/* Ends a section's samples on exactly its length, whatever the sums of its segments rounded to: samples at or past it, or a
 * rounding short of it, give way to it. */
static Error EndSectionSamples(SampleWriter* writer, double length)
{
    const double* Distances = (const double*)writer->Out->_data;
    size_t Count = writer->Out->_count;
    while ((Count > (writer->First + 1U))
        && ((Distances[Count - 1U] >= length) || ((length - Distances[Count - 1U]) <= SAMPLE_MIN_STEP)))
    {
        Count--;
    }
    GenericBuffer_SetCount(writer->Out, Count);
    return (length > Distances[Count - 1U]) ? AppendSample(writer, length) : Error_CreateSuccess();
}

/* Checks a call's arguments and prepares its writer. */
static Error BeginSamples(const PathCurve* self, const PathSampleOptions* options, GenericBuffer* outDistances,
    const char* function, SampleWriter* outWriter)
{
    if ((self == NULL) || (options == NULL) || (outDistances == NULL) || (outDistances->_elementSize != sizeof(double)))
    {
        return Error_Construct4(ErrorCode_IllegalArgument,
            "%s: self, options and outDistances (a buffer of doubles) must not be NULL.", function);
    }
    bool AreOptionsValid = isfinite(options->Spacing) && (options->Spacing > 0.0) && isfinite(options->MaxDeviation)
        && (options->MaxDeviation > 0.0) && isfinite(options->MaxTurn) && (options->MaxTurn > 0.0f);
    if (!AreOptionsValid)
    {
        return Error_Construct4(ErrorCode_IllegalArgument, "%s: the spacing, deviation and turn must be above 0.", function);
    }
    // A tangent cannot turn further than back on itself: more than that asks for nothing more.
    double MaxTurn = Math_MinDouble((double)options->MaxTurn, (double)PI);
    *outWriter = (SampleWriter){ .Curve = self, .Out = outDistances, .Options = options, .CosMaxTurn = cos(MaxTurn),
        .First = outDistances->_count, .Limit = outDistances->_count + PATH_CURVE_MAX_SAMPLES };
    return Error_CreateSuccess();
}


// Public functions: lifetime.
void PathCurve_Construct(PathCurve* self)
{
    Memory_Zero(self, sizeof(*self));
}

void PathCurve_Deconstruct(PathCurve* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->_block);
    Memory_Zero(self, sizeof(*self));
}

Error PathCurve_Build(PathCurve* self, const PathNode* nodes, size_t nodeCount, bool isClosed, PathUpMode upMode)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "PathCurve_Build: self must not be NULL.");
    }
    PathCurve_Deconstruct(self);
    bool IsUpModeKnown = (upMode == PathUpMode_Upright) || (upMode == PathUpMode_RotationMinimizing);
    if (((nodes == NULL) && (nodeCount > 0U)) || (nodeCount > PATH_CURVE_MAX_NODES) || !IsUpModeKnown)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "PathCurve_Build: the nodes must not be NULL, at most "
            "PATH_CURVE_MAX_NODES of them, with a known up mode.");
    }
    for (size_t Index = 0U; Index < nodeCount; Index++)
    {
        if (!IsNodeValid(&nodes[Index]))
        {
            return Error_Construct2(ErrorCode_IllegalArgument, "PathCurve_Build: a node has an unknown mode or a value "
                "that is not finite.");
        }
    }

    uint32_t Count = (uint32_t)nodeCount;
    self->_isClosed = isClosed;
    self->_nodeCount = Count;
    if (Count == 0U)
    {
        return Error_CreateSuccess();
    }
    BuildCounts Counts = CountBuild(nodes, Count, isClosed);
    self->_segmentCount = Counts.SegmentCount;
    self->_regionPointCount = Counts.RegionPointCount;

    // Largest alignment first. Bounded by the limit on nodes and the table's limit per segment, so no size can overflow.
    bool IsRotationMinimizing = upMode == PathUpMode_RotationMinimizing;
    size_t ByteCount = (Counts.SegmentCount * sizeof(PathCurveSegment)) + (Counts.SectionCount * sizeof(PathCurveSection))
        + (Counts.EntryCount * sizeof(double)) + (Counts.RegionPointCount * sizeof(Vector2Double))
        + (Count * sizeof(PathCurveNodeValues)) + (IsRotationMinimizing ? (Counts.EntryCount * sizeof(Vector3)) : 0U);
    unsigned char* Cursor = Memory_Allocate(ByteCount);
    self->_block = Cursor;
    PathCurveSegment* Segments = TakeBytes(&Cursor, Counts.SegmentCount * sizeof(PathCurveSegment));
    PathCurveSection* Sections = TakeBytes(&Cursor, Counts.SectionCount * sizeof(PathCurveSection));
    double* Distances = TakeBytes(&Cursor, Counts.EntryCount * sizeof(double));
    Vector2Double* RegionPoints = TakeBytes(&Cursor, Counts.RegionPointCount * sizeof(Vector2Double));
    PathCurveNodeValues* Values = TakeBytes(&Cursor, Count * sizeof(PathCurveNodeValues));
    Vector3* Rights = IsRotationMinimizing ? TakeBytes(&Cursor, Counts.EntryCount * sizeof(Vector3)) : NULL;
    self->_segments = Segments;
    self->_sections = Sections;
    self->_tableDistances = Distances;
    self->_regionPoints = isClosed ? RegionPoints : NULL;
    self->_nodeValues = Values;
    self->_tableRights = Rights;

    BuildSegments(self, Segments, Distances, nodes, Count, isClosed);
    BuildFallbackTangents(self, Segments);
    BuildFallbackRights(self, Segments);
    BuildNodeValues(self, Segments, Values, nodes, Count, isClosed);
    BuildSections(self, Segments, Sections, nodes, isClosed);
    if (IsRotationMinimizing)
    {
        BuildRotationMinimizingRights(self, Segments, Rights, isClosed);
    }
    if (isClosed)
    {
        BuildRegionPoints(self, Segments, RegionPoints);
    }
    self->_bounds = BoundingBoxDouble_FromCorners(nodes[0].Position, nodes[0].Position);
    for (uint32_t Index = 0U; Index < self->_segmentCount; Index++)
    {
        self->_bounds = BoundingBoxDouble_Union(self->_bounds, GetSegmentBounds(&Segments[Index]));
    }
    return Error_CreateSuccess();
}


// Public functions: reading.
double PathCurve_GetNodeDistance(const PathCurve* self, uint32_t nodeIndex)
{
    if (nodeIndex >= self->_nodeCount)
    {
        return 0.0;
    }
    return (nodeIndex < self->_segmentCount) ? self->_segments[nodeIndex].StartDistance : self->_length;
}

bool PathCurve_GetPointAt(const PathCurve* self, double distance, PathCurvePoint* outPoint)
{
    if ((self->_nodeCount == 0U) || !isfinite(distance))
    {
        return false;
    }
    if (self->_segmentCount == 0U)
    {
        FillSingleNodePoint(self, outPoint);
        return true;
    }
    double Wrapped = WrapDistance(self, distance);
    uint32_t Index = FindSegment(self, Wrapped);
    const PathCurveSegment* Segment = &self->_segments[Index];
    double Local = Math_ClampDouble(Wrapped - Segment->StartDistance, 0.0, Segment->Length);
    FillPoint(self, Index, GetParameterOfLocal(self, Segment, Local), Local, Wrapped, outPoint);
    return true;
}

bool PathCurve_GetSectionPoint(const PathCurve* self, uint32_t sectionIndex, double localDistance,
    PathCurvePoint* outPoint)
{
    if ((sectionIndex >= self->_sectionCount) || !isfinite(localDistance))
    {
        return false;
    }
    if (self->_segmentCount == 0U)
    {
        FillSingleNodePoint(self, outPoint);
        return true;
    }
    const PathCurveSection* Section = &self->_sections[sectionIndex];
    double Local = Math_ClampDouble(localDistance, 0.0, Section->Length);
    uint32_t Low = 0U;
    uint32_t High = Section->SegmentCount;
    while ((High - Low) > 1U)
    {
        uint32_t Middle = Low + ((High - Low) / 2U);
        if (GetSectionLocalStart(self, Section, Middle) <= Local)
        {
            Low = Middle;
        }
        else
        {
            High = Middle;
        }
    }
    while ((Low > 0U) && (self->_segments[(Section->FirstNode + Low) % self->_segmentCount].Length <= 0.0))
    {
        Low--;
    }
    uint32_t Index = (Section->FirstNode + Low) % self->_segmentCount;
    const PathCurveSegment* Segment = &self->_segments[Index];
    double SegmentLocal = Math_ClampDouble(Local - GetSectionLocalStart(self, Section, Low), 0.0, Segment->Length);
    double Distance = Section->StartDistance + Local;
    if (Distance >= self->_length)
    {
        Distance = self->_isClosed ? (Distance - self->_length) : self->_length;
    }
    FillPoint(self, Index, GetParameterOfLocal(self, Segment, SegmentLocal), SegmentLocal, Distance, outPoint);
    return true;
}

bool PathCurve_FindNearest(const PathCurve* self, Vector3Double point, bool ignoresHeight, PathCurvePoint* outPoint,
    double* outSeparation)
{
    if ((self->_nodeCount == 0U) || !Vector3Double_IsFinite(point))
    {
        return false;
    }
    if (self->_segmentCount == 0U)
    {
        FillSingleNodePoint(self, outPoint);
        if (outSeparation != NULL)
        {
            *outSeparation = sqrt(GetSeparationSquared(outPoint->Position, point, ignoresHeight));
        }
        return true;
    }
    double BestSquared = INFINITY;
    uint32_t BestSegment = 0U;
    double BestU = 0.0;
    for (uint32_t Index = 0U; Index < self->_segmentCount; Index++)
    {
        const PathCurveSegment* Segment = &self->_segments[Index];
        if (GetBoxSeparationSquared(GetSegmentBounds(Segment), point, ignoresHeight) >= BestSquared)
        {
            continue;
        }
        double U = Segment->IsStraight ? GetChordParameter(Segment->P0, Segment->P3, point, ignoresHeight)
            : FindNearestParameter(Segment, point, ignoresHeight);
        double Squared = GetSeparationSquared(GetPosition(Segment, U), point, ignoresHeight);
        if (Squared < BestSquared)
        {
            BestSquared = Squared;
            BestSegment = Index;
            BestU = U;
        }
    }
    const PathCurveSegment* Best = &self->_segments[BestSegment];
    double Local = GetLocalOfParameter(self, Best, BestU);
    FillPoint(self, BestSegment, BestU, Local, WrapDistance(self, Best->StartDistance + Local), outPoint);
    if (outSeparation != NULL)
    {
        *outSeparation = sqrt(BestSquared);
    }
    return true;
}


// Public functions: samples.
Error PathCurve_WriteSamples(const PathCurve* self, const PathSampleOptions* options, GenericBuffer* outDistances)
{
    SampleWriter Writer;
    Error Result = BeginSamples(self, options, outDistances, "PathCurve_WriteSamples", &Writer);
    if ((Result.Code != ErrorCode_Success) || (self->_nodeCount == 0U))
    {
        return Result;
    }
    Result = AppendSample(&Writer, 0.0);
    for (uint32_t Index = 0U; (Index < self->_segmentCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        const PathCurveSegment* Segment = &self->_segments[Index];
        Result = WriteSegmentSamples(&Writer, Segment, Segment->StartDistance);
    }
    return Result;
}

Error PathCurve_WriteSectionSamples(const PathCurve* self, uint32_t sectionIndex, const PathSampleOptions* options,
    GenericBuffer* outDistances)
{
    SampleWriter Writer;
    Error Result = BeginSamples(self, options, outDistances, "PathCurve_WriteSectionSamples", &Writer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (sectionIndex >= self->_sectionCount)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "PathCurve_WriteSectionSamples: no such section.");
    }
    const PathCurveSection* Section = &self->_sections[sectionIndex];
    Result = AppendSample(&Writer, 0.0);
    for (uint32_t Step = 0U; (Step < Section->SegmentCount) && (Result.Code == ErrorCode_Success); Step++)
    {
        const PathCurveSegment* Segment = &self->_segments[(Section->FirstNode + Step) % self->_segmentCount];
        Result = WriteSegmentSamples(&Writer, Segment, GetSectionLocalStart(self, Section, Step));
    }
    return (Result.Code == ErrorCode_Success) ? EndSectionSamples(&Writer, Section->Length) : Result;
}


// Public functions: regions.
bool PathCurve_IsPointInside(const PathCurve* self, double x, double z)
{
    uint32_t Count = self->_regionPointCount;
    if ((self->_regionPoints == NULL) || (Count < 3U) || !isfinite(x) || !isfinite(z)
        || (x < self->_bounds.Min.X) || (x > self->_bounds.Max.X) || (z < self->_bounds.Min.Z)
        || (z > self->_bounds.Max.Z))
    {
        return false;
    }
    // Winding by crossings of the horizontal through the point: up and to its left +1, down and to its left -1.
    int64_t Winding = 0;
    Vector2Double From = self->_regionPoints[Count - 1U];
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        Vector2Double To = self->_regionPoints[Index];
        double Side = ((To.X - From.X) * (z - From.Y)) - ((x - From.X) * (To.Y - From.Y));
        if (From.Y <= z)
        {
            if ((To.Y > z) && (Side > 0.0))
            {
                Winding++;
            }
        }
        else if ((To.Y <= z) && (Side < 0.0))
        {
            Winding--;
        }
        From = To;
    }
    return Winding != 0;
}
