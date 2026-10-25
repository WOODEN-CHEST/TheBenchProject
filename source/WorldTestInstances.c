// The world test frame's instanced copies (B03, WorldTestInstances.h): the pebble field and the instancing A/B. Test
// content only.

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldTestInstances.h"
#include "CollisionLayer.h"
#include "WorldModelInstancesComponent.h"
#include "WorldObject.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** The pebbles' world model and the field's object name. */
#define PEBBLE_WORLD_MODEL ((const unsigned char*)u8"test_pebble")
#define FIELD_OBJECT_NAME ((const unsigned char*)u8"pebble_field")
/** The field's area in front of the slab (the start view looks along +Z), metres from the terrain's object. */
#define FIELD_MIN_X (-50.0)
#define FIELD_MAX_X 50.0
#define FIELD_MIN_Z 10.5
#define FIELD_MAX_Z 60.0
/** How high above the field the ground is looked for, and how far down. */
#define GROUND_SEARCH_HEIGHT 60.0
#define GROUND_SEARCH_DISTANCE 120.0f
/** The field's seed. */
#define FIELD_SEED 20000U
/** Every how many pebbles one is stretched unevenly. */
#define UNEVEN_EVERY 4U


// Static functions.
static void Log(const WorldTestInstances* self, const char* format, ...)
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
static bool Check(const WorldTestInstances* self, Error result, const char* what)
{
    if (result.Code == ErrorCode_Success)
    {
        return true;
    }
    Log(self, "%s failed: %s", what, (result.Message != NULL) ? (const char*)result.Message : "no details");
    Error_Deconstruct(&result);
    return false;
}

/* The next value of the field's generator, in [0, 1). */
static double NextRandom(uint32_t* state)
{
    *state = (*state * 1664525U) + 1013904223U;
    return (double)(*state >> 8) / (double)(1U << 24);
}

/* The ground's height under a point of the world (the world layer: the slab and the terrain), or @p fallback. */
static double FindGroundHeight(const WorldTestInstances* self, Vector3Double above, double fallback)
{
    WorldCollisionFilter Filter = WorldCollisionFilter_Create(COLLISION_LAYER_BIT(CollisionLayer_World),
        WorldCollisionTriggerMode_Ignore);
    WorldRayHit Hit;
    if (WorldCollision_Raycast(self->_collision, above, (Vector3){ 0.0f, -1.0f, 0.0f }, GROUND_SEARCH_DISTANCE, &Filter,
        &Hit) && !Hit.IsStartInside)
    {
        return Hit.Point.Y;
    }
    return fallback;
}

/* Where the field's object stands: at the terrain's object (moved with it by the far shift), or the world's origin. */
static Vector3Double GetFieldOrigin(const WorldTestInstances* self)
{
    size_t Count = 0U;
    WorldComponent* const* Terrains = World_GetComponents(self->_world, WorldComponentType_Terrain, &Count);
    return (Count > 0U) ? WorldObject_GetPosition(WorldComponent_GetOwner(Terrains[0])) : Vector3Double_Zero();
}

/* Makes the field: its copies, then its runtime object with their model instances component. */
static void MakeField(WorldTestInstances* self)
{
    Vector3Double Origin = GetFieldOrigin(self);
    PropertyInstanceEntry* Copies = Memory_Allocate(WORLD_TEST_PEBBLE_COUNT * sizeof(PropertyInstanceEntry));
    uint32_t State = FIELD_SEED;
    for (uint32_t Index = 0U; Index < WORLD_TEST_PEBBLE_COUNT; Index++)
    {
        double X = FIELD_MIN_X + ((FIELD_MAX_X - FIELD_MIN_X) * NextRandom(&State));
        double Z = FIELD_MIN_Z + ((FIELD_MAX_Z - FIELD_MIN_Z) * NextRandom(&State));
        Vector3Double Above = Vector3Double_Add(Origin, Vector3Double_Create(X, GROUND_SEARCH_HEIGHT, Z));
        double Ground = FindGroundHeight(self, Above, Origin.Y);
        float Size = 0.6f + (0.9f * (float)NextRandom(&State));
        float Grey = 150.0f + (105.0f * (float)NextRandom(&State));
        float Warmth = 30.0f * (float)NextRandom(&State);
        PropertyInstanceEntry* Copy = &Copies[Index];
        *Copy = PropertyInstanceEntry_CreateDefault();
        Copy->Position = Vector3Double_Create(X, Ground - Origin.Y, Z);
        Copy->Rotation = (Vector3){ (float)(NextRandom(&State) - 0.5) * 0.5f, (float)NextRandom(&State) * 2.0f * PI,
            (float)(NextRandom(&State) - 0.5) * 0.5f };
        Copy->Scale = ((Index % UNEVEN_EVERY) == 0U) ? (Vector3){ Size * 1.3f, Size * 0.8f, Size }
            : (Vector3){ Size, Size, Size };
        Copy->Tint = (Color){ (unsigned char)Grey, (unsigned char)(Grey - (Warmth * 0.5f)),
            (unsigned char)(Grey - Warmth), 255 };
    }

    WorldObject* Field = NULL;
    WorldComponent* Component = NULL;
    bool IsMade = Check(self, World_CreateObject(self->_world, FIELD_OBJECT_NAME, &Field), "creating the pebble field");
    if (IsMade)
    {
        // A runtime object: never saved.
        WorldObject_SetPersistent(Field, false);
        IsMade = Check(self, WorldObject_SetPosition(Field, Origin), "placing the pebble field")
            && Check(self, World_AddComponent(self->_world, WorldObject_GetId(Field), WorldComponentType_ModelInstances,
                &Component), "giving the pebble field its copies")
            && Check(self, WorldModelInstancesComponent_SetWorldModel(
                WorldModelInstancesComponent_FromComponent(Component), PEBBLE_WORLD_MODEL), "naming the pebble")
            && Check(self, WorldModelInstancesComponent_SetInstances(WorldModelInstancesComponent_FromComponent(Component),
                Copies, WORLD_TEST_PEBBLE_COUNT), "setting the pebbles");
        self->_fieldId = WorldObject_GetId(Field);
    }
    Memory_Free(Copies);
    if (IsMade)
    {
        Log(self, "the pebble field: %u copies of %s made (the apostrophe hides it).", WORLD_TEST_PEBBLE_COUNT,
            (const char*)PEBBLE_WORLD_MODEL);
    }
}

/* Shows or hides the field, making it when it is first shown. */
static void ToggleField(WorldTestInstances* self)
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
    if (Check(self, WorldObject_SetVisible(Field, !self->_isFieldShown), "showing or hiding the pebble field"))
    {
        self->_isFieldShown = !self->_isFieldShown;
        Log(self, "the pebble field %s.", self->_isFieldShown ? "shown" : "hidden");
    }
}


// Functions.
void WorldTestInstances_Construct(WorldTestInstances* self, World* world, WorldCollision* collision,
    WorldRenderer* renderer, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    self->_world = world;
    self->_collision = collision;
    self->_renderer = renderer;
    self->_logger = logger;
}

void WorldTestInstances_HandleInput(WorldTestInstances* self, const GameInput* input)
{
    if (GameInput_IsKeyPressed(input, GameKey_Apostrophe))
    {
        ToggleField(self);
    }
    if (GameInput_IsKeyPressed(input, GameKey_Semicolon))
    {
        bool Enabled = !WorldRenderer_IsInstancingEnabled(self->_renderer);
        WorldRenderer_SetInstancingEnabled(self->_renderer, Enabled);
        Log(self, "instanced drawing %s.", Enabled ? "ON" : "OFF (every copy drawn on its own: the picture must not "
            "change)");
    }
}

void WorldTestInstances_LogStatus(const WorldTestInstances* self)
{
    Log(self, "instances: the pebble field %s (the apostrophe), instanced drawing %s (the semicolon).",
        (self->_fieldId == 0U) ? "not made yet" : (self->_isFieldShown ? "shown" : "hidden"),
        WorldRenderer_IsInstancingEnabled(self->_renderer) ? "on" : "off");
}
