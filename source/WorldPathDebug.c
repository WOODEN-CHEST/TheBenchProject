// What the paths' debug view draws, as a list of lines (WorldPathDebug.h).

#include <math.h>
#include "WorldPathDebug.h"
#include "PathGround.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldPathComponent.h"
#include "wr/WRMath.h"


// Macros.
/** How large a node's cross and a section start's tick are, in metres. */
#define NODE_CROSS_SIZE 0.3f
#define HANDLE_CROSS_SIZE 0.12f
#define SECTION_TICK_HEIGHT 2.0f
/** How long a sample's tick is to each side, in metres. */
#define SAMPLE_TICK_SIZE 0.08f
/** Metres between two frames and the ground under them, and a frame's arm at a width and scale of 1. */
#define FRAME_SPACING 2.0
#define FRAME_ARM 0.6f
/** The fewest metres between samples, by the path's length, so a very long path stays within the list. */
#define MAX_SAMPLES_PER_PATH 20000.0
/** A section turning snapping off shows this long a tick at each sample. */
#define NO_SNAP_TICK 0.4f
/** The ground's cross, in metres. */
#define GROUND_CROSS_SIZE 0.1f
/** Metres between the hatching's lines, and the most lines an area takes. */
#define HATCH_SPACING 2.0
#define HATCH_MAX_LINES 256.0
/** The most crossings one hatching line takes. */
#define HATCH_MAX_CROSSINGS 256U


// Types.
/* Where a hatching line crosses the outline, and which way the outline runs there. */
typedef struct HatchCrossingStruct
{
    double X;
    int Direction;
} HatchCrossing;


// Fields.
static const Color SectionColors[] =
{
    { 255, 150, 40, 255 },
    { 60, 220, 180, 255 },
    { 240, 90, 200, 255 },
    { 170, 230, 60, 255 },
    { 110, 150, 255, 255 },
};
static const Color SkippedColor = { 90, 90, 90, 255 };
static const Color SmoothNodeColor = { 255, 255, 255, 255 };
static const Color CornerNodeColor = { 255, 230, 40, 255 };
static const Color BezierNodeColor = { 40, 220, 255, 255 };
static const Color TangentColor = { 70, 110, 255, 255 };
static const Color UpColor = { 60, 230, 80, 255 };
static const Color RightColor = { 240, 60, 50, 255 };
static const Color SampleColor = { 220, 220, 220, 255 };
static const Color GroundColor = { 150, 150, 150, 255 };
static const Color NoSnapColor = { 170, 90, 255, 255 };
static const Color HatchColor = { 90, 230, 130, 130 };


// Static functions.
static void AddLine(GenericBuffer* lines, Vector3Double from, Vector3Double to, Color color)
{
    if (lines->_count >= WORLD_PATH_DEBUG_MAX_LINES)
    {
        return;
    }
    WorldDebugLine Line = { .From = from, .To = to, .LineColor = color };
    GenericBuffer_AddLast(lines, &Line);
}

static void AddCross(GenericBuffer* lines, Vector3Double at, float size, Color color)
{
    double Size = (double)size;
    AddLine(lines, Vector3Double_Create(at.X - Size, at.Y, at.Z), Vector3Double_Create(at.X + Size, at.Y, at.Z), color);
    AddLine(lines, Vector3Double_Create(at.X, at.Y - Size, at.Z), Vector3Double_Create(at.X, at.Y + Size, at.Z), color);
    AddLine(lines, Vector3Double_Create(at.X, at.Y, at.Z - Size), Vector3Double_Create(at.X, at.Y, at.Z + Size), color);
}

static Vector3Double Offset(Vector3Double from, Vector3 direction, float length)
{
    return Vector3Double_Create(from.X + (double)(direction.x * length), from.Y + (double)(direction.y * length),
        from.Z + (double)(direction.z * length));
}

static Color GetSectionColor(const PathCurve* curve, uint32_t section)
{
    if ((section < PathCurve_GetSectionCount(curve)) && PathCurve_GetSection(curve, section)->IsSkipped)
    {
        return SkippedColor;
    }
    return SectionColors[section % (sizeof(SectionColors) / sizeof(SectionColors[0]))];
}

static Color GetNodeColor(const PathNode* node)
{
    switch (node->Mode)
    {
        case PathNodeMode_Smooth:
            return SmoothNodeColor;
        case PathNodeMode_Corner:
            return CornerNodeColor;
        case PathNodeMode_Bezier:
            return BezierNodeColor;
    }
    return SmoothNodeColor;
}

/* The nodes: crosses by mode, a bezier node's handles, a tall tick where a section starts. */
static void AddNodes(const WorldPathComponent* path, const Matrix4Double* placement, GenericBuffer* lines)
{
    size_t Count = 0U;
    const PathNode* Nodes = WorldPathComponent_GetNodes(path, &Count);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const PathNode* Node = &Nodes[Index];
        Vector3Double At = Matrix4Double_TransformPoint(placement, Node->Position);
        Color NodeColor = GetNodeColor(Node);
        AddCross(lines, At, NODE_CROSS_SIZE, NodeColor);
        if (Node->Mode == PathNodeMode_Bezier)
        {
            Vector3Double In = Vector3Double_Create(Node->Position.X + (double)Node->InHandle.x,
                Node->Position.Y + (double)Node->InHandle.y, Node->Position.Z + (double)Node->InHandle.z);
            Vector3Double Out = Vector3Double_Create(Node->Position.X + (double)Node->OutHandle.x,
                Node->Position.Y + (double)Node->OutHandle.y, Node->Position.Z + (double)Node->OutHandle.z);
            Vector3Double InWorld = Matrix4Double_TransformPoint(placement, In);
            Vector3Double OutWorld = Matrix4Double_TransformPoint(placement, Out);
            AddLine(lines, At, InWorld, NodeColor);
            AddLine(lines, At, OutWorld, NodeColor);
            AddCross(lines, InWorld, HANDLE_CROSS_SIZE, NodeColor);
            AddCross(lines, OutWorld, HANDLE_CROSS_SIZE, NodeColor);
        }
    }
    const PathCurve* Curve = WorldPathComponent_GetCurve(path);
    for (uint32_t Index = 0U; Index < PathCurve_GetSectionCount(Curve); Index++)
    {
        const PathCurveSection* Section = PathCurve_GetSection(Curve, Index);
        Vector3Double At = Matrix4Double_TransformPoint(placement, Nodes[Section->FirstNode].Position);
        AddLine(lines, At, Vector3Double_Create(At.X, At.Y + (double)SECTION_TICK_HEIGHT, At.Z),
            GetSectionColor(Curve, Index));
    }
}

/* The ground under a point as the shared projection finds it, or a tick up where its section turns snapping off. */
static void AddGround(const WorldPathComponent* path, const PathCurvePoint* point, const WorldPathDebugOptions* options,
    GenericBuffer* lines)
{
    if (options->Collision == NULL)
    {
        return;
    }
    const PathCurve* Curve = WorldPathComponent_GetCurve(path);
    const PathCurveSection* Section = PathCurve_GetSection(Curve, point->Section);
    if (!PathCurveSection_ResolveSnapsToTerrain(Section, true))
    {
        AddLine(lines, point->Position, Vector3Double_Create(point->Position.X, point->Position.Y + (double)NO_SNAP_TICK,
            point->Position.Z), NoSnapColor);
        return;
    }
    PathGroundSettings Settings = PathGroundSettings_CreateDefault();
    PathGroundHit Hit;
    if (PathGround_Project(options->Collision, point->Position, &Settings, &Hit))
    {
        AddLine(lines, point->Position, Hit.Position, GroundColor);
        AddCross(lines, Hit.Position, GROUND_CROSS_SIZE, GroundColor);
    }
}

/* The frame at a point: the tangent, the up at the scale's size, the right across the width. */
static void AddFrame(const PathCurvePoint* point, GenericBuffer* lines)
{
    AddLine(lines, point->Position, Offset(point->Position, point->Tangent, FRAME_ARM), TangentColor);
    AddLine(lines, point->Position, Offset(point->Position, point->Up, FRAME_ARM * point->Scale), UpColor);
    float Across = FRAME_ARM * point->Width;
    AddLine(lines, Offset(point->Position, point->Right, -Across), Offset(point->Position, point->Right, Across),
        RightColor);
}

/* The curve as its samples' chords, coloured by section, and with the detail when asked. */
static void AddCurve(const WorldPathComponent* path, const WorldPathDebugOptions* options, GenericBuffer* samples,
    GenericBuffer* lines)
{
    const PathCurve* Curve = WorldPathComponent_GetCurve(path);
    double Length = PathCurve_GetLength(Curve);
    double Spacing = Math_MaxDouble((double)WorldPathComponent_GetSampleSpacing(path), Length / MAX_SAMPLES_PER_PATH);
    PathSampleOptions SampleOptions = PathSampleOptions_Create(Spacing);
    for (uint32_t Section = 0U; (Section < PathCurve_GetSectionCount(Curve)) && (lines->_count < WORLD_PATH_DEBUG_MAX_LINES);
        Section++)
    {
        GenericBuffer_Clear(samples);
        Error Result = PathCurve_WriteSectionSamples(Curve, Section, &SampleOptions, samples);
        Error_Deconstruct(&Result); // what it wrote before a limit is still worth drawing
        const double* Distances = (const double*)samples->_data;
        Color SectionColor = GetSectionColor(Curve, Section);
        Vector3Double Last = Vector3Double_Zero();
        double NextFrame = 0.0;
        for (size_t Index = 0U; Index < samples->_count; Index++)
        {
            PathCurvePoint Point;
            if (!PathCurve_GetSectionPoint(Curve, Section, Distances[Index], &Point))
            {
                continue;
            }
            WorldPathComponent_PointToWorld(path, &Point, &Point);
            if (Index > 0U)
            {
                AddLine(lines, Last, Point.Position, SectionColor);
            }
            if (options->ShowsDetail)
            {
                AddLine(lines, Offset(Point.Position, Point.Right, -SAMPLE_TICK_SIZE),
                    Offset(Point.Position, Point.Right, SAMPLE_TICK_SIZE), SampleColor);
                if (Distances[Index] >= NextFrame)
                {
                    AddFrame(&Point, lines);
                    AddGround(path, &Point, options, lines);
                    NextFrame = Distances[Index] + FRAME_SPACING;
                }
            }
            Last = Point.Position;
        }
    }
}

/* The outline's crossings of the line at @p z, sorted by X (at most HATCH_MAX_CROSSINGS). */
static uint32_t FindHatchCrossings(const Vector2Double* points, uint32_t count, double z, HatchCrossing* outCrossings)
{
    uint32_t CrossingCount = 0U;
    for (uint32_t Index = 0U; (Index < count) && (CrossingCount < HATCH_MAX_CROSSINGS); Index++)
    {
        Vector2Double From = points[Index];
        Vector2Double To = points[(Index + 1U) % count];
        if ((From.Y <= z) == (To.Y <= z))
        {
            continue;
        }
        double X = From.X + ((To.X - From.X) * ((z - From.Y) / (To.Y - From.Y)));
        HatchCrossing Crossing = { .X = X, .Direction = (To.Y > From.Y) ? 1 : -1 };
        uint32_t Place = CrossingCount;
        while ((Place > 0U) && (outCrossings[Place - 1U].X > X))
        {
            outCrossings[Place] = outCrossings[Place - 1U];
            Place--;
        }
        outCrossings[Place] = Crossing;
        CrossingCount++;
    }
    return CrossingCount;
}

/* A closed path's area: lines along X at even steps of Z, wherever the winding is not zero (what the region tests call
 * inside), at the curve's middle height. */
static void AddRegionHatching(const WorldPathComponent* path, const Matrix4Double* placement, GenericBuffer* lines)
{
    const PathCurve* Curve = WorldPathComponent_GetCurve(path);
    uint32_t Count = 0U;
    const Vector2Double* Points = PathCurve_GetRegionPoints(Curve, &Count);
    if ((Points == NULL) || (Count < 3U))
    {
        return;
    }
    BoundingBoxDouble Bounds = PathCurve_GetBounds(Curve);
    double Height = 0.5 * (Bounds.Min.Y + Bounds.Max.Y);
    double Step = Math_MaxDouble(HATCH_SPACING, (Bounds.Max.Z - Bounds.Min.Z) / HATCH_MAX_LINES);
    HatchCrossing Crossings[HATCH_MAX_CROSSINGS];
    for (double Z = Bounds.Min.Z + (0.5 * Step); Z < Bounds.Max.Z; Z += Step)
    {
        uint32_t CrossingCount = FindHatchCrossings(Points, Count, Z, Crossings);
        int Winding = 0;
        for (uint32_t Index = 0U; (Index + 1U) < CrossingCount; Index++)
        {
            Winding += Crossings[Index].Direction;
            if (Winding != 0)
            {
                AddLine(lines,
                    Matrix4Double_TransformPoint(placement, Vector3Double_Create(Crossings[Index].X, Height, Z)),
                    Matrix4Double_TransformPoint(placement, Vector3Double_Create(Crossings[Index + 1U].X, Height, Z)),
                    HatchColor);
            }
        }
    }
}


// Functions.
void WorldPathDebug_Build(const World* world, const WorldPathDebugOptions* options, GenericBuffer* outLines)
{
    size_t Count = 0U;
    WorldComponent* const* Paths = World_GetComponents(world, WorldComponentType_Path, &Count);
    GenericBuffer Samples;
    GenericBuffer_AllocateVariable(&Samples, 0U, sizeof(double));
    for (size_t Index = 0U; (Index < Count) && (outLines->_count < WORLD_PATH_DEBUG_MAX_LINES); Index++)
    {
        const WorldPathComponent* Path = WorldPathComponent_FromComponent(Paths[Index]);
        if (!WorldObject_IsEffectivelyEnabled(Path->Base._owner))
        {
            continue;
        }
        Matrix4Double Placement = WorldObject_GetWorldMatrix(Path->Base._owner);
        AddCurve(Path, options, &Samples, outLines);
        AddNodes(Path, &Placement, outLines);
        AddRegionHatching(Path, &Placement, outLines);
    }
    Memory_Free(Samples._data);
}
