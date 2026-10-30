#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "GameInput.h"
#include "Logger.h"
#include "World.h"
#include "WorldRenderer.h"

/*
 * Private header (source/) of the world test frame's shadow content: the far field and the shadow levels' debug view.
 * Test content only: WorldTestFrame.c calls it from its tick.
 *
 * The far field is a plain of WORLD_TEST_FAR_FIELD_SIZE metres a side just under the slab (the test cube, flattened;
 * it casts no shadow) and towers of the test cube at WORLD_TEST_FAR_TOWER_RING_COUNT distances in
 * WORLD_TEST_FAR_TOWER_DIRECTION_COUNT directions around the slab (300 m, 1 km and 3 km out, taller the farther they
 * stand), seen to 20 km, so the shadow levels can be watched far out: every tower casts as far as it is drawn. It is a
 * runtime object (never saved) and its children, made when it is first shown, around the terrain's object.
 *
 * Keys (raw debug keys, read in Update after the UI had first refusal): keypad + shows and hides the far field; keypad *
 * switches the shadow levels' debug view (WorldRenderer_SetShadowLevelViewEnabled: every lit surface tinted by the level
 * its shadow comes from). Each is logged.
 */


// Macros.
/** The far field's plain, metres a side. */
#define WORLD_TEST_FAR_FIELD_SIZE 12000.0
/** The towers: how many rings (distances) and how many directions. */
#define WORLD_TEST_FAR_TOWER_RING_COUNT 3U
#define WORLD_TEST_FAR_TOWER_DIRECTION_COUNT 8U


// Types.
/** The test frame's shadow content. Borrowed pointers stay valid while the frame lives. */
typedef struct WorldTestShadowsStruct
{
    World* _world;
    WorldRenderer* _renderer;
    Logger* _logger;
    /** The far field's root object (0 until it is first shown), and whether it is shown. */
    uint64_t _fieldId;
    bool _isFieldShown;
} WorldTestShadows;


// Functions.
/** Sets up the shadow content over a world and its renderer (both borrowed, as the logger is). Nothing is made until
 *  the far field is first shown. */
void WorldTestShadows_Construct(WorldTestShadows* self, World* world, WorldRenderer* renderer, Logger* logger);

/** The tick's debug keys (keypad + the far field, keypad * the levels' debug view). */
void WorldTestShadows_HandleInput(WorldTestShadows* self, const GameInput* input);

/** Logs one line: the far field and the levels' debug view (with the renderer's status, I). */
void WorldTestShadows_LogStatus(const WorldTestShadows* self);
