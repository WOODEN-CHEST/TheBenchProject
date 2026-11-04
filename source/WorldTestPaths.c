// The world test frame's paths (WorldTestPaths.h): their debug view, a node that moves, the camera's region. Test content
// only.

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldTestPaths.h"
#include "WorldObject.h"
#include "WorldPathComponent.h"
#include "WorldPathDebug.h"
#include "WorldQuery.h"
#include "wr/WRMemory.h"


// Macros.
/** The test world's walk, the node of it that moves, and how. */
#define WALK_OBJECT_ID 95U
#define WAVE_NODE 2U
#define WAVE_AMPLITUDE 3.0
#define WAVE_RADIANS_PER_SECOND 1.5
/** The park's tag. */
#define PARK_TAG ((const unsigned char*)u8"tbp:test_park")
/** How many views Backspace steps through. */
#define VIEW_COUNT 3


// Static functions.
static void Log(const WorldTestPaths* self, const char* format, ...)
{
    if (self->_logger == NULL)
    {
        return;
    }
    char Text[512];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        return;
    }
    Error LogResult = Logger_LogInfoFormatted(self->_logger, (const unsigned char*)u8"WorldTest: %s", Text);
    Error_Deconstruct(&LogResult);
}

static WorldPathComponent* GetWalk(const WorldTestPaths* self)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(self->_world, WALK_OBJECT_ID, WorldComponentType_Path, &Component)
        ? WorldPathComponent_FromComponent(Component) : NULL;
}

/* The walk's node off its place by a sine, up and down, as a script or the editor would move it. */
static void MoveWave(WorldTestPaths* self, double seconds)
{
    WorldPathComponent* Walk = GetWalk(self);
    size_t Count = 0U;
    const PathNode* Nodes = (Walk != NULL) ? WorldPathComponent_GetNodes(Walk, &Count) : NULL;
    if (Count <= WAVE_NODE)
    {
        return;
    }
    double Before = sin(self->_waveSeconds * WAVE_RADIANS_PER_SECOND) * WAVE_AMPLITUDE;
    self->_waveSeconds += seconds;
    double After = sin(self->_waveSeconds * WAVE_RADIANS_PER_SECOND) * WAVE_AMPLITUDE;
    PathNode Moved = Nodes[WAVE_NODE];
    Moved.Position.Y += After - Before;
    Error Result = WorldPathComponent_SetNode(Walk, WAVE_NODE, &Moved);
    Error_Deconstruct(&Result); // a small move of a valid node
}

/* How many objects a query by the park's tag finds, in the world or at the camera. */
static size_t CountInPark(const WorldTestPaths* self)
{
    if (self->_parkTag == NAMESPACED_KEY_ID_NONE)
    {
        return 0U;
    }
    WorldQuery Query = WorldQuery_Create();
    Query.InsideRegionTag = self->_parkTag;
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 0U, sizeof(uint64_t));
    Error Result = World_Query(self->_world, &Query, &Ids);
    Error_Deconstruct(&Result); // a well formed query
    size_t Count = Ids._count;
    Memory_Free(Ids._data);
    return Count;
}

/* Whether a point lies in a region carrying the park's tag. The camera is no object, so this asks the paths directly. */
static bool IsInPark(const WorldTestPaths* self, Vector3Double point)
{
    size_t Count = 0U;
    WorldComponent* const* Paths = World_GetComponents(self->_world, WorldComponentType_Path, &Count);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const WorldPathComponent* Path = WorldPathComponent_FromComponent(Paths[Index]);
        if (WorldObject_HasTag(Path->Base._owner, self->_parkTag) && WorldPathComponent_IsWorldPointInside(Path, point))
        {
            return true;
        }
    }
    return false;
}

static void ShowLines(WorldTestPaths* self)
{
    if (self->_view == 0)
    {
        WorldRenderer_SetDebugLines(self->_renderer, NULL, 0U);
        return;
    }
    GenericBuffer_Clear(&self->_lines);
    WorldPathDebugOptions Options = { .ShowsDetail = self->_view == 2, .Collision = self->_collision };
    WorldPathDebug_Build(self->_world, &Options, &self->_lines);
    WorldRenderer_SetDebugLines(self->_renderer, (const WorldDebugLine*)self->_lines._data, self->_lines._count);
}


// Functions.
void WorldTestPaths_Construct(WorldTestPaths* self, World* world, WorldCollision* collision, WorldRenderer* renderer,
    Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    self->_world = world;
    self->_collision = collision;
    self->_renderer = renderer;
    self->_logger = logger;
    GenericBuffer_AllocateVariable(&self->_lines, 0U, sizeof(WorldDebugLine));
    uint32_t Tag = NAMESPACED_KEY_ID_NONE;
    if (KeyRegistry_TryFind(World_GetKeys(world), PARK_TAG, &Tag))
    {
        self->_parkTag = Tag;
    }
}

void WorldTestPaths_Deconstruct(WorldTestPaths* self)
{
    WorldRenderer_SetDebugLines(self->_renderer, NULL, 0U);
    Memory_Free(self->_lines._data);
    Memory_Zero(self, sizeof(*self));
}

void WorldTestPaths_HandleInput(WorldTestPaths* self, const GameInput* input)
{
    if (!GameInput_IsKeyPressed(input, GameKey_Backspace))
    {
        return;
    }
    if (GameInput_IsKeyDown(input, GameKey_LeftShift) || GameInput_IsKeyDown(input, GameKey_RightShift))
    {
        self->_isWaving = !self->_isWaving;
        Log(self, "the walk's node %u %s.", (unsigned int)WAVE_NODE, self->_isWaving ? "MOVES" : "stands still");
        return;
    }
    self->_view = (self->_view + 1) % VIEW_COUNT;
    static const char* const ViewNames[VIEW_COUNT] = { "off", "the paths", "the paths, their samples, frames and ground" };
    Log(self, "paths' view: %s.", ViewNames[self->_view]);
}

void WorldTestPaths_PrepareRender(WorldTestPaths* self, double seconds, Vector3Double cameraPosition)
{
    if (self->_isWaving)
    {
        MoveWave(self, seconds);
    }
    ShowLines(self);
    if (self->_parkTag == NAMESPACED_KEY_ID_NONE)
    {
        return;
    }
    bool IsInside = IsInPark(self, cameraPosition);
    if (IsInside != self->_wasCameraInside)
    {
        Log(self, "the camera %s the park.", IsInside ? "entered" : "left");
        self->_wasCameraInside = IsInside;
    }
}

void WorldTestPaths_LogStatus(WorldTestPaths* self)
{
    size_t Count = 0U;
    World_GetComponents(self->_world, WorldComponentType_Path, &Count);
    Log(self, "paths: %" PRIu64 ", view %d (Backspace), the walk's node %s (Shift + Backspace), %" PRIu64
        " objects in the park, the camera %s it; the view's list holds %" PRIu64 " lines.", (uint64_t)Count, self->_view,
        self->_isWaving ? "moving" : "still", (uint64_t)CountInPark(self), self->_wasCameraInside ? "in" : "not in",
        (uint64_t)self->_lines._count);
}
