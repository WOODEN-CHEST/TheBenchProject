// The world test frame's prefabs: see WorldTestPrefabs.h.

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "WorldTestPrefabs.h"
#include "DataValue.h"
#include "WorldObject.h"
#include "WorldPrefabComponent.h"
#include "wr/WRString.h"


// Macros.
#define BUS_STOP_NAME "bus_stop_plain"
#define HOUSE_NAME "house_plain"
#define LOG_LINE_CAPACITY ((size_t)512)
/** How far the moved bench lies off, in metres. */
#define BENCH_MOVE 0.8


// Fields.
static const char* const LayoutNames[] = { "open", "windy", "closed" };
#define LAYOUT_COUNT ((int)(sizeof(LayoutNames) / sizeof(LayoutNames[0])))
static const char* const RoofColors[] = { "#7a3b2e", "#2e4a7a", "#3a6a3a", "#c0a040" };
#define ROOF_COUNT ((int)(sizeof(RoofColors) / sizeof(RoofColors[0])))
static const char* const WallColors[] = { "#f0e6d2", "#b0c8e0", "#d8a890", "#90b090" };
#define WALL_COUNT ((int)(sizeof(WallColors) / sizeof(WallColors[0])))


// Static functions.
static void Log(const WorldTestPrefabs* self, const char* format, ...)
{
    if (self->_logger == NULL)
    {
        return;
    }
    char Text[LOG_LINE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length >= 0)
    {
        Error Result = Logger_LogFormatted(self->_logger, LogLevel_Info, (const unsigned char*)u8"Prefabs: %s", Text);
        Error_Deconstruct(&Result);
    }
}

/* The copy of an object by its name in the test world, or NULL. */
static WorldPrefabComponent* FindCopy(const WorldTestPrefabs* self, const char* name)
{
    size_t Count = World_GetObjectCount(self->_world);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error Result = World_GetObjectByIndex(self->_world, Index, &Object);
        Error_Deconstruct(&Result);
        const unsigned char* Name = (Object != NULL) ? WorldObject_GetName(Object) : NULL;
        if ((Name == NULL) || (strcmp((const char*)Name, name) != 0))
        {
            continue;
        }
        return WorldPrefabComponent_FromComponent(WorldObject_GetComponent(Object, WorldComponentType_Prefab));
    }
    return NULL;
}

/* Sets a text value of a copy. */
static void SetTextValue(const WorldTestPrefabs* self, const char* copy, const char* name, const char* text)
{
    WorldPrefabComponent* Component = FindCopy(self, copy);
    DataValue Value = DataValue_CreateTag();
    Error Result = (Component != NULL) ? DataValue_CreateString((const unsigned char*)text, &Value)
        : Error_Construct2(ErrorCode_InvalidOperation, "the test world has no such copy.");
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldPrefabComponent_SetValue(Component, (const unsigned char*)name, &Value);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Log(self, "could not set %s of %s: %s", name, copy, (Result.Message != NULL) ? (const char*)Result.Message : "");
    }
    Error_Deconstruct(&Result);
    DataValue_Deconstruct(&Value);
}

static void MoveBench(WorldTestPrefabs* self)
{
    WorldPrefabComponent* Component = FindCopy(self, BUS_STOP_NAME);
    if (Component == NULL)
    {
        return;
    }
    self->_isBenchMoved = !self->_isBenchMoved;
    if (!self->_isBenchMoved)
    {
        Error Result = WorldPrefabComponent_ClearObjectOverride(Component, (const unsigned char*)u8"bench");
        Error_Deconstruct(&Result);
        return;
    }
    DataValue List = DataValue_CreateTag();
    Error Result = DataValue_CreateList(DataType_Float, &List);
    const double Parts[3] = { BENCH_MOVE, 0.0, 0.55 };
    for (size_t Index = 0U; (Index < 3U) && (Result.Code == ErrorCode_Success); Index++)
    {
        DataValue Part = DataValue_FromFloat(Parts[Index]);
        Result = DataValue_ListAdd(&List, &Part);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldPrefabComponent_SetObjectRow(Component, (const unsigned char*)u8"bench", NULL, (const unsigned char*)u8"position", &List);
    }
    Error_Deconstruct(&Result);
    DataValue_Deconstruct(&List);
}

/* Reads the files again and makes every copy and path object anew. */
static void ReloadPrefabs(WorldTestPrefabs* self)
{
    size_t Failed = 0U;
    Error Result = AssetManager_ReadDefinitions(self->_assets, &Failed);
    if (Result.Code != ErrorCode_Success)
    {
        Log(self, "the definitions could not be read: %s", (Result.Message != NULL) ? (const char*)Result.Message : "");
    }
    Error_Deconstruct(&Result);
    WorldPrefabs_Rebuild(self->_system);
    WorldPathObjects_Rebuild(self->_pathObjects);
    Log(self, "read the definitions again (%" PRIu64 " skipped) and made every copy and path object anew.", (uint64_t)Failed);
}


// Functions.
void WorldTestPrefabs_Construct(WorldTestPrefabs* self, World* world, WorldPrefabs* system, WorldPathObjects* pathObjects, AssetManager* assets, Logger* logger)
{
    *self = (WorldTestPrefabs){ ._world = world, ._system = system, ._pathObjects = pathObjects, ._assets = assets, ._logger = logger };
}

void WorldTestPrefabs_HandleInput(WorldTestPrefabs* self, GameInput* input)
{
    if (!GameInput_IsKeyDown(input, GameKey_LeftControl) && !GameInput_IsKeyDown(input, GameKey_RightControl))
    {
        return;
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad1))
    {
        GameInput_ConsumeKey(input, GameKey_Keypad1);
        self->_layout = (self->_layout + 1) % LAYOUT_COUNT;
        SetTextValue(self, BUS_STOP_NAME, "layout", LayoutNames[self->_layout]);
        Log(self, "the plain bus stop's layout is %s.", LayoutNames[self->_layout]);
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad2))
    {
        GameInput_ConsumeKey(input, GameKey_Keypad2);
        self->_roof = (self->_roof + 1) % ROOF_COUNT;
        SetTextValue(self, BUS_STOP_NAME, "roof_color", RoofColors[self->_roof]);
        Log(self, "the plain bus stop's roof is %s.", RoofColors[self->_roof]);
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad3))
    {
        GameInput_ConsumeKey(input, GameKey_Keypad3);
        MoveBench(self);
        Log(self, "the plain bus stop's bench is %s.", self->_isBenchMoved ? "moved" : "where the prefab has it");
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad4))
    {
        GameInput_ConsumeKey(input, GameKey_Keypad4);
        WorldPrefabComponent* Component = FindCopy(self, BUS_STOP_NAME);
        if (Component != NULL)
        {
            self->_isBinRemoved = !self->_isBinRemoved;
            Error Result = WorldPrefabComponent_SetObjectRemoved(Component, (const unsigned char*)u8"bin", self->_isBinRemoved);
            Error_Deconstruct(&Result);
            Log(self, "the plain bus stop's bin is %s.", self->_isBinRemoved ? "removed" : "back");
        }
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad5))
    {
        GameInput_ConsumeKey(input, GameKey_Keypad5);
        self->_walls = (self->_walls + 1) % WALL_COUNT;
        SetTextValue(self, HOUSE_NAME, "wall_color", WallColors[self->_walls]);
        Log(self, "the plain house's walls are %s.", WallColors[self->_walls]);
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad6))
    {
        GameInput_ConsumeKey(input, GameKey_Keypad6);
        ReloadPrefabs(self);
    }
}

void WorldTestPrefabs_LogStatus(WorldTestPrefabs* self)
{
    WorldPrefabsStats Stats = WorldPrefabs_GetStats(self->_system);
    Log(self, "%" PRIu64 " copies holding %" PRIu64 " objects; %" PRIu64 " applied, %" PRIu64 " rows written (Control + the keypad's 1 to 6).",
        (uint64_t)Stats.CopyCount, (uint64_t)Stats.ObjectCount, Stats.ApplyCount, Stats.WriteCount);
}
