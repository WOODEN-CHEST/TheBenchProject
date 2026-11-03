#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "AssetManager.h"
#include "GameInput.h"
#include "Logger.h"
#include "World.h"
#include "WorldRenderer.h"

/*
 * Private header (source/) of the world test frame's far terrain content: the coast around the town, and the switches
 * of the picture's curve and the terrain's far shadows. Test content only: WorldTestFrame.c calls it from its tick.
 *
 * The far terrain is every terrain definition named tbp:test_far_terrain_<a>_<b> (written by
 * asset_source/scripts/placeholder_far_terrain.py: hills and mountains to the north of the town, out to 42 km, and the
 * sea floor to the south), each placed at ((a - 4) x WORLD_TEST_FAR_TERRAIN_PITCH, 0, (b - 4) x
 * WORLD_TEST_FAR_TERRAIN_PITCH) under a root object at the town's terrain's object. They are runtime objects (never
 * saved), made when the far terrain is first shown; together with the sea (Shift + keypad +: WorldTestWater.h) they make
 * a coast, and at sunset the mountains' shadows reach the town.
 *
 * Keys (raw debug keys, read in Update after the UI had first refusal): Shift with keypad * shows and hides the far
 * terrain (keypad * alone is the shadow levels' debug view: WorldTestShadows.h); Scroll Lock switches the world's curve
 * (the environment's is_curvature_enabled); Pause switches the terrain's far shadows (WorldRenderer_SetFarShadowsEnabled:
 * the A/B). Each is logged.
 */


// Macros.
/** The pitch between the far terrain's tiles' origins, metres (the script's), and the index offset in their names. */
#define WORLD_TEST_FAR_TERRAIN_PITCH 14000.0
#define WORLD_TEST_FAR_TERRAIN_INDEX_OFFSET 4


// Types.
/** The test frame's far terrain content. Borrowed pointers stay valid while the frame lives. */
typedef struct WorldTestFarTerrainStruct
{
    World* _world;
    WorldRenderer* _renderer;
    AssetManager* _assets;
    AssetTypeID _terrainType;
    Logger* _logger;
    /** The far terrain's root object (0 until it is first shown), how many tiles it holds, and whether it is shown. */
    uint64_t _rootId;
    uint32_t _tileCount;
    bool _isShown;
} WorldTestFarTerrain;


// Functions.
/** Sets up the far terrain content over a world, its renderer and the asset manager the terrains are listed in (all
 *  borrowed, as the logger is). Nothing is made until the far terrain is first shown. */
void WorldTestFarTerrain_Construct(WorldTestFarTerrain* self, World* world, WorldRenderer* renderer, AssetManager* assets,
    AssetTypeID terrainType, Logger* logger);

/** The tick's debug keys (Shift + keypad * the far terrain, Scroll Lock the curve, Pause the far shadows). */
void WorldTestFarTerrain_HandleInput(WorldTestFarTerrain* self, const GameInput* input);

/** Logs one line: the far terrain, the curve and the far shadows' switch (with the renderer's status, I). */
void WorldTestFarTerrain_LogStatus(const WorldTestFarTerrain* self);
