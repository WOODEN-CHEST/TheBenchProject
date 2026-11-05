#pragma once
#include <stdbool.h>
#include "AssetManager.h"
#include "GameInput.h"
#include "Logger.h"
#include "World.h"
#include "WorldPathObjects.h"
#include "WorldPrefabs.h"

/*
 * Private header (source/) of the world test frame's prefabs: the keys that change copies while the game runs, as the editor will,
 * through their components' rows. Test content only: WorldTestFrame.c calls it. The objects themselves (111 to 118 of the test world)
 * are ordinary world-file content, placeholder_prefabs.py's.
 *
 * Keys (raw debug keys, read in Update after the UI had first refusal, before the other keypad keys, which they take when Control is
 * down): Control with the keypad's 1 steps the plain bus stop's layout (an enum: open, windy, closed), 2 its roof's colour, 3 moves its
 * bench by an override and back, 4 removes its bin by an override and brings it back, 5 steps the plain house's wall colour (one value
 * through nested copies to every bay), 6 reads the files again and makes every copy and path object anew: edit a prefab and press it. Each is
 * logged.
 */


// Types.
/** The test frame's prefabs. Borrowed pointers stay valid while the frame lives. */
typedef struct WorldTestPrefabsStruct
{
    World* _world;
    WorldPrefabs* _system;
    WorldPathObjects* _pathObjects;
    AssetManager* _assets;
    Logger* _logger;
    /** What the keys show now (indices into the module's lists). */
    int _layout;
    int _roof;
    int _walls;
    bool _isBenchMoved;
    bool _isBinRemoved;
} WorldTestPrefabs;


// Functions.
/** Sets up the keys over a world and its system (borrowed, as the assets and the logger are). */
void WorldTestPrefabs_Construct(WorldTestPrefabs* self, World* world, WorldPrefabs* system, WorldPathObjects* pathObjects, AssetManager* assets, Logger* logger);

/** The tick's debug keys (Control with the keypad's digits; takes the key, so the keypad's own use of it does not see it). */
void WorldTestPrefabs_HandleInput(WorldTestPrefabs* self, GameInput* input);

/** Logs one line: what the system holds (with the renderer's status, I). */
void WorldTestPrefabs_LogStatus(WorldTestPrefabs* self);
