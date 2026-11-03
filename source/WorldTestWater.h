#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "GameInput.h"
#include "Logger.h"
#include "World.h"

/*
 * Private header (source/) of the world test frame's water content: the sea. Test content only: WorldTestFrame.c calls
 * it from its tick.
 *
 * The sea is the water plane (the world model tbp:test_sea: a 1 km grid with the sea's water material, seen at any
 * distance) scaled to WORLD_TEST_SEA_SIZE metres a side around the terrain's object, its surface WORLD_TEST_SEA_LEVEL
 * metres under it: the terrain's outer slope (falling to the level of the terrain's object at its edge) becomes a shore,
 * and so the test world an island. A plane knows no shore of its own: every low place of the terrain under its level
 * shows water too (the valley, the hollows beside the slab). It is a runtime object (never saved), made when it is
 * first shown; there is no global sea (every water is placed, as this one).
 *
 * Keys (raw debug keys, read in Update after the UI had first refusal): Shift with keypad + shows and hides the sea
 * (keypad + alone is the far field's). Logged.
 */


// Macros.
/** The sea's side, metres: its edge 100 km out, past the horizon from up to about 780 m. */
#define WORLD_TEST_SEA_SIZE 200000.0
/** Its surface, metres under the terrain's object (the terrain's outer edge lies at its object's height). */
#define WORLD_TEST_SEA_LEVEL (-0.3)


// Types.
/** The test frame's water content. Borrowed pointers stay valid while the frame lives. */
typedef struct WorldTestWaterStruct
{
    World* _world;
    Logger* _logger;
    /** The sea's object (0 until it is first shown), and whether it is shown. */
    uint64_t _seaId;
    bool _isSeaShown;
} WorldTestWater;


// Functions.
/** Sets up the water content over a world (borrowed, as the logger is). Nothing is made until the sea is first shown. */
void WorldTestWater_Construct(WorldTestWater* self, World* world, Logger* logger);

/** The tick's debug keys (Shift with keypad +: the sea). */
void WorldTestWater_HandleInput(WorldTestWater* self, const GameInput* input);

/** Logs one line: the sea (with the renderer's status, I). */
void WorldTestWater_LogStatus(const WorldTestWater* self);
