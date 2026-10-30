// The world test frame's shadow content (WorldTestShadows.h): the far field and the shadow levels' debug view. Test
// content only.

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldTestShadows.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "wr/WRMemory.h"


// Macros.
/** The world model of the plain and the towers (a cube CUBE_SIZE metres a side), and the objects' names. */
#define CUBE_WORLD_MODEL ((const unsigned char*)u8"test")
#define CUBE_SIZE 0.5
#define FIELD_OBJECT_NAME ((const unsigned char*)u8"far_field")
#define PLAIN_OBJECT_NAME ((const unsigned char*)u8"far_field_plain")
#define TOWER_OBJECT_NAME ((const unsigned char*)u8"far_field_tower")
/** The plain's thickness and where its top lies, metres above the terrain's object (just under the slab's top). */
#define PLAIN_THICKNESS 1.0
#define PLAIN_TOP (-0.15)
/** How far the plain and the towers are seen (past the 600 m of the test cube's own draw distance). */
#define FIELD_DRAW_DISTANCE 20000.0f
/** Pi, in double. */
#define DOUBLE_PI 3.14159265358979323846


// Fields.
/* Each ring of towers: its distance from the slab, the towers' width and their height, metres. */
static const double TowerRings[WORLD_TEST_FAR_TOWER_RING_COUNT][3] =
{
    { 300.0, 6.0, 40.0 },
    { 1000.0, 15.0, 100.0 },
    { 3000.0, 40.0, 250.0 },
};


// Static functions.
static void Log(const WorldTestShadows* self, const char* format, ...)
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
static bool Check(const WorldTestShadows* self, Error result, const char* what)
{
    if (result.Code == ErrorCode_Success)
    {
        return true;
    }
    Log(self, "%s failed: %s", what, (result.Message != NULL) ? (const char*)result.Message : "no details");
    Error_Deconstruct(&result);
    return false;
}

/* Where the field stands: at the terrain's object (moved with it by the far shift), or the world's origin. */
static Vector3Double GetFieldOrigin(const WorldTestShadows* self)
{
    size_t Count = 0U;
    WorldComponent* const* Terrains = World_GetComponents(self->_world, WorldComponentType_Terrain, &Count);
    return (Count > 0U) ? WorldObject_GetPosition(WorldComponent_GetOwner(Terrains[0])) : Vector3Double_Zero();
}

/* Makes one cube of the field under its root: a runtime object at a place relative to the root, of a size in metres,
 * drawn to FIELD_DRAW_DISTANCE, casting shadows or not. */
static bool MakeCube(WorldTestShadows* self, uint64_t rootId, const unsigned char* name, Vector3Double position,
    Vector3 size, bool castsShadows)
{
    Vector3 Scale = { size.x / (float)CUBE_SIZE, size.y / (float)CUBE_SIZE, size.z / (float)CUBE_SIZE };
    WorldObject* Cube = NULL;
    WorldComponent* Component = NULL;
    if (!Check(self, World_CreateObject(self->_world, name, &Cube), "creating a far field cube"))
    {
        return false;
    }
    WorldObject_SetPersistent(Cube, false);
    uint64_t Id = WorldObject_GetId(Cube);
    if (!Check(self, World_SetParent(self->_world, Id, rootId, false), "placing a far field cube in its field")
        || !Check(self, WorldObject_SetPosition(Cube, position), "placing a far field cube")
        || !Check(self, WorldObject_SetScale(Cube, Scale), "scaling a far field cube")
        || !Check(self, World_AddComponent(self->_world, Id, WorldComponentType_Model, &Component),
            "giving a far field cube its model"))
    {
        return false;
    }
    WorldModelComponent* Placement = WorldModelComponent_FromComponent(Component);
    return Check(self, WorldModelComponent_SetWorldModel(Placement, CUBE_WORLD_MODEL), "naming the far field's cube")
        && Check(self, WorldModelComponent_SetOverridden(Placement, WorldModelSetting_DrawDistance, true),
            "overriding a far field cube's draw distance")
        && Check(self, WorldModelComponent_SetDrawDistance(Placement, FIELD_DRAW_DISTANCE),
            "setting a far field cube's draw distance")
        && Check(self, WorldModelComponent_SetOverridden(Placement, WorldModelSetting_CastsShadows, true),
            "overriding a far field cube's shadows")
        && Check(self, WorldModelComponent_SetCastsShadows(Placement, castsShadows), "setting a far field cube's shadows");
}

/* Makes the field: its root at the terrain's object, the plain and every ring's towers. */
static void MakeField(WorldTestShadows* self)
{
    WorldObject* Root = NULL;
    if (!Check(self, World_CreateObject(self->_world, FIELD_OBJECT_NAME, &Root), "creating the far field"))
    {
        return;
    }
    WorldObject_SetPersistent(Root, false);
    self->_fieldId = WorldObject_GetId(Root);
    if (!Check(self, WorldObject_SetPosition(Root, GetFieldOrigin(self)), "placing the far field"))
    {
        return;
    }
    bool IsMade = MakeCube(self, self->_fieldId, PLAIN_OBJECT_NAME,
        Vector3Double_Create(0.0, PLAIN_TOP - (PLAIN_THICKNESS * 0.5), 0.0),
        (Vector3){ (float)WORLD_TEST_FAR_FIELD_SIZE, (float)PLAIN_THICKNESS, (float)WORLD_TEST_FAR_FIELD_SIZE }, false);
    uint32_t TowerCount = 0U;
    for (uint32_t Ring = 0U; IsMade && (Ring < WORLD_TEST_FAR_TOWER_RING_COUNT); Ring++)
    {
        double Distance = TowerRings[Ring][0];
        double Width = TowerRings[Ring][1];
        double Height = TowerRings[Ring][2];
        for (uint32_t Direction = 0U; IsMade && (Direction < WORLD_TEST_FAR_TOWER_DIRECTION_COUNT); Direction++)
        {
            // Each ring a little turned from the one before, so no tower stands in another's shadow from the slab.
            double Angle = ((2.0 * DOUBLE_PI * (double)Direction) / (double)WORLD_TEST_FAR_TOWER_DIRECTION_COUNT)
                + (0.25 * (double)Ring);
            Vector3Double Position = Vector3Double_Create(Distance * sin(Angle), PLAIN_TOP + (Height * 0.5),
                Distance * cos(Angle));
            IsMade = MakeCube(self, self->_fieldId, TOWER_OBJECT_NAME, Position,
                (Vector3){ (float)Width, (float)Height, (float)Width }, true);
            TowerCount += IsMade ? 1U : 0U;
        }
    }
    if (IsMade)
    {
        Log(self, "the far field: a plain of %.0f m and %u towers at 300 m, 1 km and 3 km made (keypad + hides it).",
            WORLD_TEST_FAR_FIELD_SIZE, TowerCount);
    }
}

/* Shows or hides the field, making it when it is first shown. */
static void ToggleField(WorldTestShadows* self)
{
    if (!self->_isFieldShown && (self->_fieldId == 0U))
    {
        MakeField(self);
    }
    WorldObject* Field = NULL;
    if ((self->_fieldId == 0U) || !World_TryGetObject(self->_world, self->_fieldId, &Field))
    {
        return;
    }
    if (Check(self, WorldObject_SetVisible(Field, !self->_isFieldShown), "showing or hiding the far field"))
    {
        self->_isFieldShown = !self->_isFieldShown;
        Log(self, "the far field %s.", self->_isFieldShown ? "shown" : "hidden");
    }
}


// Functions.
void WorldTestShadows_Construct(WorldTestShadows* self, World* world, WorldRenderer* renderer, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    self->_world = world;
    self->_renderer = renderer;
    self->_logger = logger;
}

void WorldTestShadows_HandleInput(WorldTestShadows* self, const GameInput* input)
{
    if (GameInput_IsKeyPressed(input, GameKey_KeypadAdd))
    {
        ToggleField(self);
    }
    if (GameInput_IsKeyPressed(input, GameKey_KeypadMultiply))
    {
        bool Enabled = !WorldRenderer_IsShadowLevelViewEnabled(self->_renderer);
        WorldRenderer_SetShadowLevelViewEnabled(self->_renderer, Enabled);
        Log(self, "shadow levels' view %s.", Enabled ? "ON (every lit surface tinted by its shadow's level: red, green, "
            "blue, yellow, magenta, cyan, orange, white from the nearest out; dark beyond the reach)" : "OFF");
    }
}

void WorldTestShadows_LogStatus(const WorldTestShadows* self)
{
    Log(self, "shadows: the far field %s (keypad +), the shadow levels' view %s (keypad *).",
        (self->_fieldId == 0U) ? "not made yet" : (self->_isFieldShown ? "shown" : "hidden"),
        WorldRenderer_IsShadowLevelViewEnabled(self->_renderer) ? "on" : "off");
}
