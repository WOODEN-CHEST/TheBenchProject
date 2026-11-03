// The world test frame's water content (WorldTestWater.h): the sea. Test content only.

#include <stdarg.h>
#include <stdio.h>
#include "WorldTestWater.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "wr/WRMemory.h"


// Macros.
/** The sea's world model (the water plane, WATER_PLANE_SIZE metres a side, with the sea's water material) and its
 *  object's name. */
#define SEA_WORLD_MODEL ((const unsigned char*)u8"tbp:test_sea")
#define WATER_PLANE_SIZE 1000.0
#define SEA_OBJECT_NAME ((const unsigned char*)u8"sea")


// Static functions.
static void Log(const WorldTestWater* self, const char* format, ...)
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

/* Logs a failed call (test content: nothing else to do about it); true when it succeeded. */
static bool Check(const WorldTestWater* self, Error result, const char* what)
{
    if (result.Code == ErrorCode_Success)
    {
        return true;
    }
    Log(self, "%s failed: %s", what, (result.Message != NULL) ? (const char*)result.Message : "no details");
    Error_Deconstruct(&result);
    return false;
}

/* Where the sea lies: under the terrain's object (moved with it by the far shift), or under the world's origin. */
static Vector3Double GetSeaPosition(const WorldTestWater* self)
{
    size_t Count = 0U;
    WorldComponent* const* Terrains = World_GetComponents(self->_world, WorldComponentType_Terrain, &Count);
    Vector3Double Origin = (Count > 0U) ? WorldObject_GetPosition(WorldComponent_GetOwner(Terrains[0]))
        : Vector3Double_Zero();
    return Vector3Double_Create(Origin.X, Origin.Y + WORLD_TEST_SEA_LEVEL, Origin.Z);
}

/* Makes the sea: a runtime object with the sea's world model, scaled to its size. */
static void MakeSea(WorldTestWater* self)
{
    WorldObject* Sea = NULL;
    WorldComponent* Component = NULL;
    if (!Check(self, World_CreateObject(self->_world, SEA_OBJECT_NAME, &Sea), "creating the sea"))
    {
        return;
    }
    WorldObject_SetPersistent(Sea, false);
    self->_seaId = WorldObject_GetId(Sea);
    float Scale = (float)(WORLD_TEST_SEA_SIZE / WATER_PLANE_SIZE);
    if (Check(self, WorldObject_SetPosition(Sea, GetSeaPosition(self)), "placing the sea")
        && Check(self, WorldObject_SetScale(Sea, (Vector3){ Scale, 1.0f, Scale }), "scaling the sea")
        && Check(self, World_AddComponent(self->_world, self->_seaId, WorldComponentType_Model, &Component),
            "giving the sea its model")
        && Check(self, WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(Component), SEA_WORLD_MODEL),
            "naming the sea's world model"))
    {
        Log(self, "the sea: %.0f km a side, %.1f m under the terrain's object (Shift + keypad + hides it).",
            WORLD_TEST_SEA_SIZE / 1000.0, -WORLD_TEST_SEA_LEVEL);
    }
}

/* Shows or hides the sea, making it when it is first shown. */
static void ToggleSea(WorldTestWater* self)
{
    if (!self->_isSeaShown && (self->_seaId == 0U))
    {
        MakeSea(self);
    }
    WorldObject* Sea = NULL;
    if ((self->_seaId == 0U) || !World_TryGetObject(self->_world, self->_seaId, &Sea))
    {
        return;
    }
    if (Check(self, WorldObject_SetVisible(Sea, !self->_isSeaShown), "showing or hiding the sea"))
    {
        self->_isSeaShown = !self->_isSeaShown;
        Log(self, "the sea %s.", self->_isSeaShown ? "shown" : "hidden");
    }
}


// Functions.
void WorldTestWater_Construct(WorldTestWater* self, World* world, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    self->_world = world;
    self->_logger = logger;
}

void WorldTestWater_HandleInput(WorldTestWater* self, const GameInput* input)
{
    bool IsShiftDown = GameInput_IsKeyDown(input, GameKey_LeftShift) || GameInput_IsKeyDown(input, GameKey_RightShift);
    if (IsShiftDown && GameInput_IsKeyPressed(input, GameKey_KeypadAdd))
    {
        ToggleSea(self);
    }
}

void WorldTestWater_LogStatus(const WorldTestWater* self)
{
    Log(self, "water: the sea %s (Shift + keypad +).",
        (self->_seaId == 0U) ? "not made yet" : (self->_isSeaShown ? "shown" : "hidden"));
}
