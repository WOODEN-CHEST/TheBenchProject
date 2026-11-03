// The world test frame's far terrain content (WorldTestFarTerrain.h): the coast around the town, the picture's curve and
// the far shadows' switch. Test content only.

#include <stdarg.h>
#include <stdio.h>
#include "WorldTestFarTerrain.h"
#include "WorldObject.h"
#include "WorldTerrainComponent.h"
#include "wr/WRMemory.h"


// Macros.
/** The tiles' definitions' common prefix (its text and its length), and the far terrain's root object's name. */
#define FAR_TERRAIN_PREFIX_TEXT u8"tbp:test_far_terrain_"
#define FAR_TERRAIN_PREFIX ((const unsigned char*)FAR_TERRAIN_PREFIX_TEXT)
#define FAR_TERRAIN_PREFIX_LENGTH (sizeof(FAR_TERRAIN_PREFIX_TEXT) - 1U)
#define FAR_TERRAIN_ROOT_NAME ((const unsigned char*)u8"far_terrain")
/** A tile's object's name: "far_terrain_" and its indices. */
#define TILE_NAME_CAPACITY 64


// Static functions.
static void Log(const WorldTestFarTerrain* self, const char* format, ...)
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
static bool Check(const WorldTestFarTerrain* self, Error result, const char* what)
{
    if (result.Code == ErrorCode_Success)
    {
        return true;
    }
    Log(self, "%s failed: %s", what, (result.Message != NULL) ? (const char*)result.Message : "no details");
    Error_Deconstruct(&result);
    return false;
}

/* Where the far terrain lies: at the town's terrain's object (moved with it by the far shift), or at the world's origin. */
static Vector3Double GetAnchor(const WorldTestFarTerrain* self)
{
    size_t Count = 0U;
    WorldComponent* const* Terrains = World_GetComponents(self->_world, WorldComponentType_Terrain, &Count);
    return (Count > 0U) ? WorldObject_GetPosition(WorldComponent_GetOwner(Terrains[0])) : Vector3Double_Zero();
}

/* One tile: an object under the root at its place, with a terrain component naming its definition. */
static bool MakeTile(WorldTestFarTerrain* self, const unsigned char* name, int column, int row)
{
    char ObjectName[TILE_NAME_CAPACITY];
    snprintf(ObjectName, sizeof(ObjectName), "far_terrain_%d_%d", column, row);
    WorldObject* Tile = NULL;
    WorldComponent* Component = NULL;
    if (!Check(self, World_CreateObject(self->_world, (const unsigned char*)ObjectName, &Tile), "creating a far tile"))
    {
        return false;
    }
    WorldObject_SetPersistent(Tile, false);
    uint64_t Id = WorldObject_GetId(Tile);
    Vector3Double Place = Vector3Double_Create(
        (double)(column - WORLD_TEST_FAR_TERRAIN_INDEX_OFFSET) * WORLD_TEST_FAR_TERRAIN_PITCH, 0.0,
        (double)(row - WORLD_TEST_FAR_TERRAIN_INDEX_OFFSET) * WORLD_TEST_FAR_TERRAIN_PITCH);
    return Check(self, World_SetParent(self->_world, Id, self->_rootId, false), "placing a far tile under its root")
        && Check(self, WorldObject_SetPosition(Tile, Place), "placing a far tile")
        && Check(self, World_AddComponent(self->_world, Id, WorldComponentType_Terrain, &Component),
            "giving a far tile its terrain")
        && Check(self, WorldTerrainComponent_SetTerrain(WorldTerrainComponent_FromComponent(Component), name),
            "naming a far tile's terrain");
}

/* Makes the far terrain: a root at the town's terrain's object and a tile for every far terrain definition. */
static void MakeFarTerrain(WorldTestFarTerrain* self)
{
    WorldObject* Root = NULL;
    if (!Check(self, World_CreateObject(self->_world, FAR_TERRAIN_ROOT_NAME, &Root), "creating the far terrain"))
    {
        return;
    }
    WorldObject_SetPersistent(Root, false);
    self->_rootId = WorldObject_GetId(Root);
    if (!Check(self, WorldObject_SetPosition(Root, GetAnchor(self)), "placing the far terrain"))
    {
        return;
    }
    GenericBuffer Names;
    GenericBuffer Offsets;
    GenericBuffer_AllocateVariable(&Names, 256U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&Offsets, 32U, sizeof(size_t));
    if (Check(self, AssetManager_GetDefinitionNames(self->_assets, self->_terrainType, FAR_TERRAIN_PREFIX, &Names,
        &Offsets), "listing the far terrain"))
    {
        const size_t* Starts = (const size_t*)Offsets._data;
        for (size_t Index = 0U; Index < Offsets._count; Index++)
        {
            const unsigned char* Name = (const unsigned char*)Names._data + Starts[Index];
            int Column = 0;
            int Row = 0;
            const char* Indices = (const char*)Name + FAR_TERRAIN_PREFIX_LENGTH;
            if ((sscanf(Indices, "%d_%d", &Column, &Row) == 2) && MakeTile(self, Name, Column, Row))
            {
                self->_tileCount++;
            }
        }
    }
    Memory_Free(Names._data);
    Memory_Free(Offsets._data);
    Log(self, "the far terrain: %u tiles of the coast (hills from 3 km, mountains 10 to 40 km out to the north, the sea "
        "floor to the south; Shift + keypad * hides it; Shift + keypad + shows the sea).", (unsigned int)self->_tileCount);
}

/* Shows or hides the far terrain, making it when it is first shown. */
static void ToggleFarTerrain(WorldTestFarTerrain* self)
{
    if (!self->_isShown && (self->_rootId == 0U))
    {
        MakeFarTerrain(self);
    }
    WorldObject* Root = NULL;
    if ((self->_rootId == 0U) || !World_TryGetObject(self->_world, self->_rootId, &Root))
    {
        return;
    }
    if (Check(self, WorldObject_SetVisible(Root, !self->_isShown), "showing or hiding the far terrain"))
    {
        self->_isShown = !self->_isShown;
        Log(self, "the far terrain %s.", self->_isShown ? "shown" : "hidden");
    }
}


// Functions.
void WorldTestFarTerrain_Construct(WorldTestFarTerrain* self, World* world, WorldRenderer* renderer, AssetManager* assets,
    AssetTypeID terrainType, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    self->_world = world;
    self->_renderer = renderer;
    self->_assets = assets;
    self->_terrainType = terrainType;
    self->_logger = logger;
}

void WorldTestFarTerrain_HandleInput(WorldTestFarTerrain* self, const GameInput* input)
{
    bool IsShiftDown = GameInput_IsKeyDown(input, GameKey_LeftShift) || GameInput_IsKeyDown(input, GameKey_RightShift);
    if (IsShiftDown && GameInput_IsKeyPressed(input, GameKey_KeypadMultiply))
    {
        ToggleFarTerrain(self);
    }
    if (GameInput_IsKeyPressed(input, GameKey_ScrollLock))
    {
        WorldEnvironment* Environment = World_GetEnvironment(self->_world);
        Environment->IsCurvatureEnabled = !Environment->IsCurvatureEnabled;
        Log(self, "the picture's curve %s.", Environment->IsCurvatureEnabled ? "ON (far things lie lower with the "
            "planet's curve: 8 m at 10 km, 126 m at 40 km)" : "OFF (the picture as flat as the world)");
    }
    if (GameInput_IsKeyPressed(input, GameKey_Pause))
    {
        bool Enabled = !WorldRenderer_AreFarShadowsEnabled(self->_renderer);
        WorldRenderer_SetFarShadowsEnabled(self->_renderer, Enabled);
        Log(self, "the terrain's far shadows %s.", Enabled ? "ON (terrain shades at any distance)"
            : "OFF (terrain casts only within the shadow levels' reach)");
    }
}

void WorldTestFarTerrain_LogStatus(const WorldTestFarTerrain* self)
{
    Log(self, "far terrain: %s (Shift + keypad *); the picture's curve %s (Scroll Lock); far shadows %s (Pause).",
        (self->_rootId == 0U) ? "not made yet" : (self->_isShown ? "shown" : "hidden"),
        World_GetEnvironment(self->_world)->IsCurvatureEnabled ? "on" : "off",
        WorldRenderer_AreFarShadowsEnabled(self->_renderer) ? "on" : "off");
}
