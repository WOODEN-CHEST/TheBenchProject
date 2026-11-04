// What a laid-out piece turns into: see PathObjectPlacement.h.

#include <float.h>
#include <math.h>
#include "PathObjectPlacement.h"
#include "WorldTransform.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** A polygon clipped by one slab: a triangle cut by two planes has at most five corners; each further plane adds at most one. */
#define MAX_POLYGON_CORNERS 16U
/** A chord shorter than this, in metres, has no direction of its own. */
#define MIN_CHORD_LENGTH 1.0e-6


// Types.
/* One corner of a polygon being clipped: every attribute in floats. */
typedef struct BendCornerStruct
{
    float Position[3];
    float Normal[3];
    float Tangent[4];
    float Texcoord[2];
    float Texcoord2[2];
    float Color[4];
} BendCorner;

/* A polygon being clipped. */
typedef struct BendPolygonStruct
{
    BendCorner Corners[MAX_POLYGON_CORNERS];
    uint32_t Count;
} BendPolygon;

/* The frame a slab's point goes through: its position at the slab's plane and its axes. */
typedef struct BendFrameStruct
{
    Vector3 Position;
    Vector3 AxisX;
    Vector3 AxisY;
    Vector3 AxisZ;
} BendFrame;


// Static functions: vectors in double.
static Vector3Double FromFloats(Vector3 vector)
{
    return Vector3Double_Create((double)vector.x, (double)vector.y, (double)vector.z);
}

static bool IsVectorFinite(Vector3 vector)
{
    return isfinite(vector.x) && isfinite(vector.y) && isfinite(vector.z);
}


// Static functions: rigid copies.
/* A piece's three axes and how its length climbs, turned as the slope says. Axis X runs along the piece, Y up, Z right; Y is
 * unit, Z is unit, and a point at distance x along the piece lies at Along * x along X plus Rise * x along Y. */
typedef struct CopyFrameStruct
{
    Vector3Double AxisX;
    Vector3Double AxisY;
    Vector3Double AxisZ;
    double Along;
    double Rise;
} CopyFrame;

/* A frame from a direction and an up: X along the direction as far as the up lets it, Y the up, Z their right. */
static CopyFrame MakeUprightFrame(Vector3Double direction, Vector3Double up, Vector3Double fallbackRight)
{
    CopyFrame Frame;
    Frame.AxisY = up;
    Frame.AxisZ = Vector3Double_NormalizeOr(Vector3Double_Cross(direction, up), fallbackRight);
    Frame.AxisX = Vector3Double_Cross(Frame.AxisY, Frame.AxisZ);
    Frame.Along = 1.0;
    Frame.Rise = 0.0;
    return Frame;
}

/* The frame the slope's rule gives a chord of the piece's: Along and Rise are the chord's parts along X and Y. */
static CopyFrame GetCopyFrame(const PathObjectCopyInput* input, Vector3Double chord, Vector3Double up, Vector3Double tangent,
    Vector3Double right)
{
    double Length = Vector3Double_Length(chord);
    bool HasChord = Length > MIN_CHORD_LENGTH;
    Vector3Double Direction = HasChord ? Vector3Double_Scale(chord, 1.0 / Length) : tangent;
    PathObjectSlope Slope = input->Slot->Slope;
    if (input->Slot->AlignedTo == PathObjectAlignedTo_World)
    {
        return (CopyFrame){ .AxisX = Vector3Double_Create(1.0, 0.0, 0.0), .AxisY = Vector3Double_Create(0.0, 1.0, 0.0),
            .AxisZ = Vector3Double_Create(0.0, 0.0, 1.0), .Along = 0.0, .Rise = 0.0 };
    }
    double Climb = Vector3Double_Dot(Direction, up);
    double Across = sqrt(fmax(0.0, 1.0 - (Climb * Climb)));
    if ((Slope == PathObjectSlope_Shear) && (fabs(Climb) > ((double)PATH_OBJECT_MAX_SHEAR_SLOPE * Across)))
    {
        Slope = PathObjectSlope_Tilt; // nearly along the up: a shear would flatten the piece
    }
    CopyFrame Frame = MakeUprightFrame(Direction, up, right);
    switch (Slope)
    {
        case PathObjectSlope_Shear:
            Frame.Along = Vector3Double_Dot(chord, Frame.AxisX);
            Frame.Rise = Vector3Double_Dot(chord, Frame.AxisY);
            return Frame;
        case PathObjectSlope_Tilt:
            Frame.AxisX = Direction;
            Frame.AxisY = Vector3Double_Cross(Frame.AxisZ, Direction);
            Frame.Along = Length;
            return Frame;
        case PathObjectSlope_Step:
            Frame.Along = Vector3Double_Dot(chord, Frame.AxisX);
            return Frame;
    }
    return Frame;
}

/* A level piece's height along the up, as the anchor says, relative to the chord's middle. */
static double GetStepLift(PathObjectStepAnchor anchor, double startHeight, double endHeight)
{
    double Middle = 0.5 * (startHeight + endHeight);
    switch (anchor)
    {
        case PathObjectStepAnchor_Middle: return 0.0;
        case PathObjectStepAnchor_Higher: return fmax(startHeight, endHeight) - Middle;
        case PathObjectStepAnchor_Lower: return fmin(startHeight, endHeight) - Middle;
    }
    return 0.0;
}

/* The Euler rotation of a frame with @p offset (Euler XYZ, on top of it), and a yaw about its Y, on top of that. */
static Vector3 GetFrameRotation(const CopyFrame* frame, Vector3 offset, float yaw)
{
    Matrix4Double Axes = Matrix4Double_Identity();
    const Vector3Double Columns[3] = { frame->AxisX, frame->AxisY, frame->AxisZ };
    for (size_t Column = 0U; Column < 3U; Column++)
    {
        Axes.M[Column * 4U] = Columns[Column].X;
        Axes.M[(Column * 4U) + 1U] = Columns[Column].Y;
        Axes.M[(Column * 4U) + 2U] = Columns[Column].Z;
    }
    Matrix4Double Offset = WorldTransform_Compose(Vector3Double_Create(0.0, 0.0, 0.0), offset, (Vector3){ 1.0f, 1.0f, 1.0f });
    Matrix4Double Yaw = Matrix4Double_FromRotationY((double)yaw);
    Matrix4Double Turned = Matrix4Double_Multiply(&Offset, &Yaw);
    Matrix4Double Whole = Matrix4Double_Multiply(&Turned, &Axes);
    Vector3Double Position;
    Vector3 Rotation;
    Vector3 Scale;
    WorldTransform_Decompose(&Whole, (Vector3){ 1.0f, 1.0f, 1.0f }, &Position, &Rotation, &Scale);
    return Rotation;
}


// Static functions: bending.
static void GetFrameAt(const PathObjectBendSlice* slices, size_t sliceCount, size_t slab, float x, BendFrame* outFrame,
    float* outLocalX)
{
    // Slab i lies between slices i - 1 and i; slab 0 and slab sliceCount reach past the ends, along the end's own X axis.
    if (slab == 0U)
    {
        *outFrame = (BendFrame){ slices[0].Position, slices[0].AxisX, slices[0].AxisY, slices[0].AxisZ };
        *outLocalX = x - slices[0].MeshX;
        return;
    }
    if (slab >= sliceCount)
    {
        const PathObjectBendSlice* Last = &slices[sliceCount - 1U];
        *outFrame = (BendFrame){ Last->Position, Last->AxisX, Last->AxisY, Last->AxisZ };
        *outLocalX = x - Last->MeshX;
        return;
    }
    const PathObjectBendSlice* A = &slices[slab - 1U];
    const PathObjectBendSlice* B = &slices[slab];
    float Span = B->MeshX - A->MeshX;
    float T = (Span > 0.0f) ? Clamp((x - A->MeshX) / Span, 0.0f, 1.0f) : 0.0f;
    outFrame->Position = Vector3Lerp(A->Position, B->Position, T);
    outFrame->AxisX = Vector3Lerp(A->AxisX, B->AxisX, T);
    outFrame->AxisY = Vector3Lerp(A->AxisY, B->AxisY, T);
    outFrame->AxisZ = Vector3Lerp(A->AxisZ, B->AxisZ, T);
    *outLocalX = 0.0f;
}

/* The corner of a split between two corners, on the plane @p x. */
static BendCorner LerpCorners(const BendCorner* a, const BendCorner* b, float x)
{
    float Span = b->Position[0] - a->Position[0];
    float T = (Span != 0.0f) ? ((x - a->Position[0]) / Span) : 0.0f;
    BendCorner Result;
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Result.Position[Index] = Lerp(a->Position[Index], b->Position[Index], T);
        Result.Normal[Index] = Lerp(a->Normal[Index], b->Normal[Index], T);
    }
    for (size_t Index = 0U; Index < 4U; Index++)
    {
        Result.Tangent[Index] = Lerp(a->Tangent[Index], b->Tangent[Index], T);
        Result.Color[Index] = Lerp(a->Color[Index], b->Color[Index], T);
    }
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        Result.Texcoord[Index] = Lerp(a->Texcoord[Index], b->Texcoord[Index], T);
        Result.Texcoord2[Index] = Lerp(a->Texcoord2[Index], b->Texcoord2[Index], T);
    }
    Result.Position[0] = x; // exactly on the plane, so the slabs on either side agree
    return Result;
}

/* Splits a polygon at the plane @p x: what lies at or below it into @p outBelow, at or above it into @p outAbove. */
static void SplitPolygon(const BendPolygon* polygon, float x, BendPolygon* outBelow, BendPolygon* outAbove)
{
    outBelow->Count = 0U;
    outAbove->Count = 0U;
    for (uint32_t Index = 0U; Index < polygon->Count; Index++)
    {
        const BendCorner* Here = &polygon->Corners[Index];
        const BendCorner* Next = &polygon->Corners[(Index + 1U) % polygon->Count];
        bool IsHereBelow = Here->Position[0] <= x;
        bool IsNextBelow = Next->Position[0] <= x;
        if (IsHereBelow && (outBelow->Count < MAX_POLYGON_CORNERS))
        {
            outBelow->Corners[outBelow->Count++] = *Here;
        }
        if ((Here->Position[0] >= x) && (outAbove->Count < MAX_POLYGON_CORNERS))
        {
            outAbove->Corners[outAbove->Count++] = *Here;
        }
        bool IsCrossing = (IsHereBelow != IsNextBelow) && (Here->Position[0] != x) && (Next->Position[0] != x);
        if (IsCrossing)
        {
            BendCorner Cut = LerpCorners(Here, Next, x);
            if (outBelow->Count < MAX_POLYGON_CORNERS)
            {
                outBelow->Corners[outBelow->Count++] = Cut;
            }
            if (outAbove->Count < MAX_POLYGON_CORNERS)
            {
                outAbove->Corners[outAbove->Count++] = Cut;
            }
        }
    }
}

/* The slab a mesh X lies in (see GetFrameAt): the number of slices at or below it. */
static size_t FindSlab(const PathObjectBendSlice* slices, size_t sliceCount, float x)
{
    size_t Low = 0U;
    size_t High = sliceCount;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (slices[Middle].MeshX <= x)
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

/* A corner through its slab's frame, appended to the mesh's arrays. */
static bool AppendCorner(PathObjectBentMesh* self, const PathObjectSourceMesh* source, const BendCorner* corner,
    const PathObjectBendSlice* slices, size_t sliceCount, size_t slab)
{
    BendFrame Frame;
    float LocalX = 0.0f;
    GetFrameAt(slices, sliceCount, slab, corner->Position[0], &Frame, &LocalX);
    float Y = corner->Position[1];
    float Z = corner->Position[2];
    Vector3 Position = Vector3Add(Frame.Position, Vector3Add(Vector3Scale(Frame.AxisX, LocalX),
        Vector3Add(Vector3Scale(Frame.AxisY, Y), Vector3Scale(Frame.AxisZ, Z))));
    float Positions[3] = { Position.x, Position.y, Position.z };
    self->BoxMin = Vector3Min(self->BoxMin, Position);
    self->BoxMax = Vector3Max(self->BoxMax, Position);

    Vector3 CofactorX = Vector3CrossProduct(Frame.AxisY, Frame.AxisZ);
    Vector3 CofactorY = Vector3CrossProduct(Frame.AxisZ, Frame.AxisX);
    Vector3 CofactorZ = Vector3CrossProduct(Frame.AxisX, Frame.AxisY);
    Vector3 Normal = Vector3Add(Vector3Scale(CofactorX, corner->Normal[0]), Vector3Add(Vector3Scale(CofactorY,
        corner->Normal[1]), Vector3Scale(CofactorZ, corner->Normal[2])));
    Normal = Vector3Normalize(Normal);
    if (Vector3LengthSqr(Normal) == 0.0f)
    {
        Normal = (Vector3){ corner->Normal[0], corner->Normal[1], corner->Normal[2] };
    }
    float Normals[3] = { Normal.x, Normal.y, Normal.z };
    bool IsAdded = GenericBuffer_AddLastRange(&self->Positions, Positions, 3U)
        && GenericBuffer_AddLastRange(&self->Normals, Normals, 3U);
    if (IsAdded && (source->Tangents != NULL))
    {
        Vector3 Along = Vector3Add(Vector3Scale(Frame.AxisX, corner->Tangent[0]), Vector3Add(Vector3Scale(Frame.AxisY,
            corner->Tangent[1]), Vector3Scale(Frame.AxisZ, corner->Tangent[2])));
        Along = Vector3Normalize(Along);
        float Tangents[4] = { Along.x, Along.y, Along.z, corner->Tangent[3] };
        IsAdded = GenericBuffer_AddLastRange(&self->Tangents, Tangents, 4U);
    }
    if (IsAdded && (source->Texcoords != NULL))
    {
        float Texcoords[2] = { corner->Texcoord[0], corner->Texcoord[1] };
        IsAdded = GenericBuffer_AddLastRange(&self->Texcoords, Texcoords, 2U);
    }
    if (IsAdded && (source->Texcoords2 != NULL))
    {
        float Texcoords[2] = { corner->Texcoord2[0], corner->Texcoord2[1] };
        IsAdded = GenericBuffer_AddLastRange(&self->Texcoords2, Texcoords, 2U);
    }
    if (IsAdded && (source->Colors != NULL))
    {
        unsigned char Colors[4];
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            Colors[Index] = (unsigned char)Clamp(corner->Color[Index] + 0.5f, 0.0f, 255.0f);
        }
        IsAdded = GenericBuffer_AddLastRange(&self->Colors, Colors, 4U);
    }
    return IsAdded;
}

/* A polygon of one slab, as a fan of triangles. */
static Error AppendPolygon(PathObjectBentMesh* self, const PathObjectSourceMesh* source, const BendPolygon* polygon,
    const PathObjectBendSlice* slices, size_t sliceCount, size_t slab, size_t maxVertexCount)
{
    if (polygon->Count < 3U)
    {
        return Error_CreateSuccess();
    }
    for (uint32_t Corner = 1U; (Corner + 1U) < polygon->Count; Corner++)
    {
        if ((PathObjectBentMesh_GetVertexCount(self) + 3U) > maxVertexCount)
        {
            return Error_Construct2(ErrorCode_ArgumentOutOfRange, "PathObjectBend_AppendMesh: the mesh would pass its limit.");
        }
        if (!AppendCorner(self, source, &polygon->Corners[0], slices, sliceCount, slab)
            || !AppendCorner(self, source, &polygon->Corners[Corner], slices, sliceCount, slab)
            || !AppendCorner(self, source, &polygon->Corners[Corner + 1U], slices, sliceCount, slab))
        {
            return Error_Construct2(ErrorCode_BufferTooSmall, "PathObjectBend_AppendMesh: a buffer could not grow.");
        }
    }
    return Error_CreateSuccess();
}

/* One vertex of the source, under its import transform. */
static void ReadCorner(const PathObjectSourceMesh* source, uint32_t vertex, const Matrix* normalMatrix, BendCorner* outCorner)
{
    Memory_Zero(outCorner, sizeof(*outCorner));
    const float* Read = &source->Positions[vertex * 3U];
    Vector3 Position = Vector3Transform((Vector3){ Read[0], Read[1], Read[2] }, source->Transform);
    outCorner->Position[0] = Position.x;
    outCorner->Position[1] = Position.y;
    outCorner->Position[2] = Position.z;
    if (source->Normals != NULL)
    {
        const float* Normals = &source->Normals[vertex * 3U];
        Vector3 Normal = Vector3Transform((Vector3){ Normals[0], Normals[1], Normals[2] }, *normalMatrix);
        Normal = Vector3Normalize(Normal);
        // An import transform that flattens the mesh has no inverse: such a normal says nothing, and up will do.
        bool IsUsable = IsVectorFinite(Normal) && (Vector3LengthSqr(Normal) > 0.0f);
        outCorner->Normal[0] = IsUsable ? Normal.x : 0.0f;
        outCorner->Normal[1] = IsUsable ? Normal.y : 1.0f;
        outCorner->Normal[2] = IsUsable ? Normal.z : 0.0f;
    }
    else
    {
        outCorner->Normal[1] = 1.0f;
    }
    if (source->Tangents != NULL)
    {
        const float* Tangents = &source->Tangents[vertex * 4U];
        Matrix Axes = source->Transform;
        Axes.m12 = 0.0f;
        Axes.m13 = 0.0f;
        Axes.m14 = 0.0f;
        Vector3 Tangent = Vector3Normalize(Vector3Transform((Vector3){ Tangents[0], Tangents[1], Tangents[2] }, Axes));
        outCorner->Tangent[0] = Tangent.x;
        outCorner->Tangent[1] = Tangent.y;
        outCorner->Tangent[2] = Tangent.z;
        outCorner->Tangent[3] = Tangents[3];
    }
    if (source->Texcoords != NULL)
    {
        outCorner->Texcoord[0] = source->Texcoords[vertex * 2U];
        outCorner->Texcoord[1] = source->Texcoords[(vertex * 2U) + 1U];
    }
    if (source->Texcoords2 != NULL)
    {
        outCorner->Texcoord2[0] = source->Texcoords2[vertex * 2U];
        outCorner->Texcoord2[1] = source->Texcoords2[(vertex * 2U) + 1U];
    }
    if (source->Colors != NULL)
    {
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            outCorner->Color[Index] = (float)source->Colors[(vertex * 4U) + Index];
        }
    }
}

/* The smallest and largest X of a polygon. */
static void GetPolygonRange(const BendPolygon* polygon, float* outMin, float* outMax)
{
    *outMin = polygon->Corners[0].Position[0];
    *outMax = *outMin;
    for (uint32_t Index = 1U; Index < polygon->Count; Index++)
    {
        *outMin = fminf(*outMin, polygon->Corners[Index].Position[0]);
        *outMax = fmaxf(*outMax, polygon->Corners[Index].Position[0]);
    }
}

/* One triangle of the source: cut to the range, then at each slice's plane it crosses. */
static Error AppendTriangle(PathObjectBentMesh* self, const PathObjectSourceMesh* source, uint32_t a, uint32_t b, uint32_t c,
    const Matrix* normalMatrix, const PathObjectBendSlice* slices, size_t sliceCount, float cutMinX, float cutMaxX,
    size_t maxVertexCount)
{
    BendPolygon Rest;
    Rest.Count = 3U;
    ReadCorner(source, a, normalMatrix, &Rest.Corners[0]);
    ReadCorner(source, b, normalMatrix, &Rest.Corners[1]);
    ReadCorner(source, c, normalMatrix, &Rest.Corners[2]);
    BendPolygon Below;
    BendPolygon Above;
    float Min = 0.0f;
    float Max = 0.0f;
    GetPolygonRange(&Rest, &Min, &Max);
    if ((Max <= cutMinX) || (Min >= cutMaxX))
    {
        return Error_CreateSuccess();
    }
    if (Min < cutMinX)
    {
        SplitPolygon(&Rest, cutMinX, &Below, &Above);
        Rest = Above;
    }
    if (Max > cutMaxX)
    {
        SplitPolygon(&Rest, cutMaxX, &Below, &Above);
        Rest = Below;
    }
    if (Rest.Count < 3U)
    {
        return Error_CreateSuccess();
    }
    GetPolygonRange(&Rest, &Min, &Max);
    size_t Slab = FindSlab(slices, sliceCount, Min);
    while (Slab < sliceCount)
    {
        float Plane = slices[Slab].MeshX;
        if (Max <= Plane)
        {
            break;
        }
        SplitPolygon(&Rest, Plane, &Below, &Above);
        Error Result = AppendPolygon(self, source, &Below, slices, sliceCount, Slab, maxVertexCount);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Rest = Above;
        Slab++;
    }
    return AppendPolygon(self, source, &Rest, slices, sliceCount, Slab, maxVertexCount);
}


// Functions: measuring.
void PathObjectMeasure_AddVertices(PathObjectMeasure* measure, const float* positions, size_t vertexCount, Matrix transform)
{
    for (size_t Index = 0U; Index < vertexCount; Index++)
    {
        const float* Read = &positions[Index * 3U];
        Vector3 Point = Vector3Transform((Vector3){ Read[0], Read[1], Read[2] }, transform);
        measure->Min = Vector3Min(measure->Min, Point);
        measure->Max = Vector3Max(measure->Max, Point);
    }
}

void PathObjectMeasure_AddBox(PathObjectMeasure* measure, BoundingBox box, Matrix transform)
{
    for (int Corner = 0; Corner < 8; Corner++)
    {
        Vector3 Point = { (Corner & 1) ? box.max.x : box.min.x, (Corner & 2) ? box.max.y : box.min.y,
            (Corner & 4) ? box.max.z : box.min.z };
        Point = Vector3Transform(Point, transform);
        measure->Min = Vector3Min(measure->Min, Point);
        measure->Max = Vector3Max(measure->Max, Point);
    }
}

float PathObjectMeasure_GetLength(const PathObjectMeasure* measure, const PathObjectSlotRecord* slot)
{
    if (slot->OverridesLength)
    {
        return slot->Length;
    }
    if (measure->Max.x < measure->Min.x)
    {
        return 0.0f;
    }
    return fmaxf((measure->Max.x - measure->Min.x) - slot->EntryDepthStart - slot->EntryDepthEnd, 0.0f);
}

float PathObjectMeasure_GetBodyCenter(const PathObjectMeasure* measure, const PathObjectSlotRecord* slot)
{
    if (measure->Max.x < measure->Min.x)
    {
        return 0.0f;
    }
    return 0.5f * ((measure->Min.x + slot->EntryDepthStart) + (measure->Max.x - slot->EntryDepthEnd));
}


// Functions: rigid copies.
bool PathObjectPlacement_MakeCopy(const PathObjectCopyInput* input, PropertyInstanceEntry* outCopy)
{
    const PathObjectSlotRecord* Slot = input->Slot;
    Vector3Double Chord = Vector3Double_Subtract(input->End.Position, input->Start.Position);
    Vector3Double Middle = Vector3Double_Scale(Vector3Double_Add(input->Start.Position, input->End.Position), 0.5);
    Vector3Double Up = Vector3Double_NormalizeOr(Vector3Double_Add(FromFloats(input->Start.Up), FromFloats(input->End.Up)),
        FromFloats(input->Start.Up));
    Vector3Double Tangent = FromFloats(input->Start.Tangent);
    Vector3Double Right = FromFloats(input->Start.Right);
    CopyFrame Frame = GetCopyFrame(input, Chord, Up, Tangent, Right);
    if ((Slot->Slope == PathObjectSlope_Step) && (Slot->AlignedTo == PathObjectAlignedTo_Path))
    {
        double Lift = GetStepLift(Slot->StepAnchor, Vector3Double_Dot(input->Start.Position, Up),
            Vector3Double_Dot(input->End.Position, Up));
        Middle = Vector3Double_AddScaled(Middle, Up, Lift);
    }

    float Size = 0.5f * (input->Start.Scale + input->End.Scale);
    float Width = 0.5f * (input->Start.Width + input->End.Width);
    float Nominal = PathObjectMeasure_GetLength(&input->Measure, Slot);
    float BodyCenter = PathObjectMeasure_GetBodyCenter(&input->Measure, Slot);
    float JitteredSize = Size * (1.0f + (Slot->ScaleJitter * ((2.0f * input->Jitter[4]) - 1.0f)));
    double ScaleX = (Nominal > 0.0f) ? (Frame.Along / (double)Nominal) : (double)JitteredSize;
    double ShearValue = (fabs(Frame.Along) > MIN_CHORD_LENGTH) ? (Frame.Rise / Frame.Along) : 0.0;
    if ((Slot->AlignedTo == PathObjectAlignedTo_World) || input->KeepsLength)
    {
        ScaleX = (double)JitteredSize;
    }
    Vector3 Offset = Slot->PositionOffset;
    Vector3 Jitter = { Slot->PositionJitter.x * ((2.0f * input->Jitter[0]) - 1.0f),
        Slot->PositionJitter.y * ((2.0f * input->Jitter[1]) - 1.0f),
        Slot->PositionJitter.z * ((2.0f * input->Jitter[2]) - 1.0f) };
    if (input->IsTurned)
    {
        Frame.AxisX = Vector3Double_Negate(Frame.AxisX);
        Frame.AxisZ = Vector3Double_Negate(Frame.AxisZ);
        ShearValue = -ShearValue;
    }
    double Lengthwise = (double)Offset.x + (double)Jitter.x;
    double Upward = (double)Offset.y + (double)Jitter.y;
    double Sideways = (double)Offset.z + (double)Jitter.z;
    Vector3Double Position = Vector3Double_AddScaled(Middle, Frame.AxisX, Lengthwise);
    Position = Vector3Double_AddScaled(Position, Frame.AxisY, Upward);
    Position = Vector3Double_AddScaled(Position, Frame.AxisZ, Sideways);

    float Yaw = Slot->YawJitter * ((2.0f * input->Jitter[3]) - 1.0f);
    Vector3 Rotation = GetFrameRotation(&Frame, Slot->RotationOffset, Yaw);
    Vector3 Scale = { (float)ScaleX, JitteredSize, JitteredSize * Width };
    if (!Vector3Double_IsFinite(Position) || !IsVectorFinite(Rotation) || !IsVectorFinite(Scale)
        || !isfinite((float)ShearValue))
    {
        return false;
    }
    // The body's middle lands on the piece's, however it is turned: the mesh's origin lies back from there along the copy's own
    // axes.
    Matrix4Double Axes = WorldTransform_ComposeSheared(Vector3Double_Create(0.0, 0.0, 0.0), Rotation, Scale, (float)ShearValue);
    Position = Vector3Double_Subtract(Position, Matrix4Double_TransformPoint(&Axes, Vector3Double_Create((double)BodyCenter, 0.0,
        0.0)));
    *outCopy = PropertyInstanceEntry_CreateDefault();
    outCopy->Position = Position;
    outCopy->Rotation = Rotation;
    outCopy->Scale = Scale;
    outCopy->Shear = (float)ShearValue;
    outCopy->Tint = input->Tint;
    return true;
}


// Functions: bent pieces.
void PathObjectBentMesh_Construct(PathObjectBentMesh* self)
{
    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->Positions, 0U, sizeof(float));
    GenericBuffer_AllocateVariable(&self->Normals, 0U, sizeof(float));
    GenericBuffer_AllocateVariable(&self->Tangents, 0U, sizeof(float));
    GenericBuffer_AllocateVariable(&self->Texcoords, 0U, sizeof(float));
    GenericBuffer_AllocateVariable(&self->Texcoords2, 0U, sizeof(float));
    GenericBuffer_AllocateVariable(&self->Colors, 0U, sizeof(unsigned char));
    PathObjectBentMesh_Clear(self);
}

void PathObjectBentMesh_Deconstruct(PathObjectBentMesh* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->Positions._data);
    Memory_Free(self->Normals._data);
    Memory_Free(self->Tangents._data);
    Memory_Free(self->Texcoords._data);
    Memory_Free(self->Texcoords2._data);
    Memory_Free(self->Colors._data);
    Memory_Zero(self, sizeof(*self));
}

void PathObjectBentMesh_Clear(PathObjectBentMesh* self)
{
    GenericBuffer_Clear(&self->Positions);
    GenericBuffer_Clear(&self->Normals);
    GenericBuffer_Clear(&self->Tangents);
    GenericBuffer_Clear(&self->Texcoords);
    GenericBuffer_Clear(&self->Texcoords2);
    GenericBuffer_Clear(&self->Colors);
    self->BoxMin = (Vector3){ FLT_MAX, FLT_MAX, FLT_MAX };
    self->BoxMax = (Vector3){ -FLT_MAX, -FLT_MAX, -FLT_MAX };
}

Error PathObjectBend_AppendMesh(PathObjectBentMesh* self, const PathObjectSourceMesh* source,
    const PathObjectBendSlice* slices, size_t sliceCount, float cutMinX, float cutMaxX, size_t maxVertexCount)
{
    if ((self == NULL) || (source == NULL) || (slices == NULL) || (source->Positions == NULL) || (sliceCount == 0U)
        || (sliceCount > PATH_OBJECT_MAX_SLICES))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "PathObjectBend_AppendMesh: the mesh, its source, its positions and 1 to 4096 slices are required.");
    }
    Matrix NormalMatrix = source->Transform;
    NormalMatrix.m12 = 0.0f;
    NormalMatrix.m13 = 0.0f;
    NormalMatrix.m14 = 0.0f;
    NormalMatrix = MatrixTranspose(MatrixInvert(NormalMatrix));
    uint32_t TriangleCount = (source->Indices != NULL) ? source->TriangleCount : (source->VertexCount / 3U);
    Error Result = Error_CreateSuccess();
    for (uint32_t Triangle = 0U; (Triangle < TriangleCount) && (Result.Code == ErrorCode_Success); Triangle++)
    {
        uint32_t A = (source->Indices != NULL) ? source->Indices[Triangle * 3U] : (Triangle * 3U);
        uint32_t B = (source->Indices != NULL) ? source->Indices[(Triangle * 3U) + 1U] : ((Triangle * 3U) + 1U);
        uint32_t C = (source->Indices != NULL) ? source->Indices[(Triangle * 3U) + 2U] : ((Triangle * 3U) + 2U);
        if ((A >= source->VertexCount) || (B >= source->VertexCount) || (C >= source->VertexCount))
        {
            continue;
        }
        Result = AppendTriangle(self, source, A, B, C, &NormalMatrix, slices, sliceCount, cutMinX, cutMaxX, maxVertexCount);
    }
    return Result;
}
