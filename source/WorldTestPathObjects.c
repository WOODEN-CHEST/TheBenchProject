// The world test frame's path objects (WorldTestPathObjects.h): the walk's fence changed by keys, through its table's rows. Test
// content only.

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldTestPathObjects.h"
#include "PathObject.h"
#include "WorldPathObjectComponent.h"
#include "wr/WRMemory.h"


// Macros.
/** The test world's fence along the walk. */
#define FENCE_OBJECT_ID 99U
/** How many fits and slopes the keys step through. */
#define FIT_COUNT 4
#define SLOPE_COUNT 3


// Types.
/* One of the fits the key steps through. */
typedef struct FitStepStruct
{
    const char* Name;
    PathObjectFit Fit;
    PathObjectResizeMethod Method;
    PathObjectAlign Align;
} FitStep;


// Fields.
static const FitStep FitSteps[FIT_COUNT] =
{
    { "stretch", PathObjectFit_Stretch, PathObjectResizeMethod_Scale, PathObjectAlign_Start },
    { "resize_ends, scaled", PathObjectFit_ResizeEnds, PathObjectResizeMethod_Scale, PathObjectAlign_Start },
    { "resize_ends, cut", PathObjectFit_ResizeEnds, PathObjectResizeMethod_Cut, PathObjectAlign_Start },
    { "fixed, between", PathObjectFit_Fixed, PathObjectResizeMethod_Scale, PathObjectAlign_Between },
};

static const char* const SlopeNames[SLOPE_COUNT] = { "shear", "tilt", "step" };
static const PathObjectSlope Slopes[SLOPE_COUNT] = { PathObjectSlope_Shear, PathObjectSlope_Tilt, PathObjectSlope_Step };


// Static functions.
static void Log(const WorldTestPathObjects* self, const char* format, ...)
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

static WorldPathObjectComponent* GetFence(const WorldTestPathObjects* self)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(self->_world, FENCE_OBJECT_ID, WorldComponentType_PathObject, &Component)
        ? WorldPathObjectComponent_FromComponent(Component) : NULL;
}

/* One row of the fence, as the editor will write it. */
static void SetRow(WorldPathObjectComponent* fence, const char* key, PropertyValue value)
{
    Error Result = Properties_Set(WorldPathObjectComponent_GetPropertyTable(), fence, (const unsigned char*)key, &value);
    Error_Deconstruct(&Result); // a known row and a valid value
}

static void ApplyFit(const WorldTestPathObjects* self)
{
    WorldPathObjectComponent* Fence = GetFence(self);
    if (Fence == NULL)
    {
        return;
    }
    const FitStep* Step = &FitSteps[self->_fit];
    SetRow(Fence, "overrides_fit", PropertyValue_FromBool(true));
    SetRow(Fence, "fit", PropertyValue_FromEnum((int32_t)Step->Fit));
    SetRow(Fence, "overrides_resize_method", PropertyValue_FromBool(true));
    SetRow(Fence, "resize_method", PropertyValue_FromEnum((int32_t)Step->Method));
    SetRow(Fence, "overrides_align", PropertyValue_FromBool(true));
    SetRow(Fence, "align", PropertyValue_FromEnum((int32_t)Step->Align));
}

static void ApplySlope(const WorldTestPathObjects* self)
{
    WorldPathObjectComponent* Fence = GetFence(self);
    if (Fence == NULL)
    {
        return;
    }
    PathObjectSlotRecord Record = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
    Record.OverridesSlope = true;
    Record.Slope = Slopes[self->_slope];
    PropertyRecordList* List = NULL;
    Error Result = PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), &Record, 1U, &List);
    Error_Deconstruct(&Result); // a record of its table's defaults
    SetRow(Fence, "slots", PropertyValue_FromRecordList(List));
    PropertyRecordList_Destroy(List);
}


// Functions.
void WorldTestPathObjects_Construct(WorldTestPathObjects* self, World* world, WorldPathObjects* system, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    self->_world = world;
    self->_system = system;
    self->_logger = logger;
}

void WorldTestPathObjects_HandleInput(WorldTestPathObjects* self, GameInput* input)
{
    if (!GameInput_IsKeyPressed(input, GameKey_Backspace)
        || (!GameInput_IsKeyDown(input, GameKey_LeftControl) && !GameInput_IsKeyDown(input, GameKey_RightControl)))
    {
        return;
    }
    GameInput_ConsumeKey(input, GameKey_Backspace);
    if (GameInput_IsKeyDown(input, GameKey_LeftShift) || GameInput_IsKeyDown(input, GameKey_RightShift))
    {
        self->_slope = (self->_slope + 1) % SLOPE_COUNT;
        ApplySlope(self);
        Log(self, "the walk's fence's panels take a slope by %s.", SlopeNames[self->_slope]);
        return;
    }
    self->_fit = (self->_fit + 1) % FIT_COUNT;
    ApplyFit(self);
    Log(self, "the walk's fence fits by %s.", FitSteps[self->_fit].Name);
}

void WorldTestPathObjects_LogStatus(WorldTestPathObjects* self)
{
    WorldPathObjectsStats Stats = WorldPathObjects_GetStats(self->_system);
    Log(self, "path objects: %" PRIu64 " holding %" PRIu64 " pieces: %" PRIu64 " copies, %" PRIu64 " placed shapes, %" PRIu64
        " bent stretches of %" PRIu64 " triangles, on %" PRIu64 " children; %" PRIu64 " rebuilds, %" PRIu64 " changing nothing; "
            "the "
        "walk's fence fits by %s (Control + Backspace), its panels by %s (with Shift).", (uint64_t)Stats.ComponentCount,
            (uint64_t)Stats.PieceCount,
        (uint64_t)Stats.CopyCount, (uint64_t)Stats.PlacedShapeCount, (uint64_t)Stats.GeneratedModelCount,
        (uint64_t)Stats.GeneratedTriangleCount, (uint64_t)Stats.ChildCount, Stats.RebuildCount, Stats.UnchangedRebuildCount,
        FitSteps[self->_fit].Name, SlopeNames[self->_slope]);
}
