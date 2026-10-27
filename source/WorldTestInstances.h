#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "GameInput.h"
#include "Logger.h"
#include "World.h"
#include "WorldCollision.h"
#include "WorldRenderer.h"

/*
 * Private header (source/) of the world test frame's instanced copies: the pebble field and the instancing A/B.
 * Test content only: WorldTestFrame.c calls it from its tick. The grove (the test world's object 52, a model_instances
 * component of test_tree) is ordinary world-file content.
 *
 * The pebble field is WORLD_TEST_PEBBLE_COUNT copies of test_pebble scattered in front of the slab, made when it is
 * first shown: a runtime object (never saved) at the terrain's object, every pebble at the ground's height under it (a
 * raycast), turned, scaled (a quarter of them unevenly) and tinted, seeded so it is the same every time.
 *
 * Keys (raw debug keys, read in Update after the UI had first refusal): the apostrophe shows and hides the pebble field;
 * the semicolon switches the renderer's instanced drawing (WorldRenderer_SetInstancingEnabled): off, every copy is drawn
 * on its own through the ordinary shaders, and the picture must not change. Each is logged.
 */


// Macros.
/** How many pebbles the field has. */
#define WORLD_TEST_PEBBLE_COUNT 20000U


// Types.
/** The test frame's instanced copies. Borrowed pointers stay valid while the frame lives. */
typedef struct WorldTestInstancesStruct
{
    World* _world;
    WorldCollision* _collision;
    WorldRenderer* _renderer;
    Logger* _logger;
    /** The pebble field's object (0 until it is first shown), and whether it is shown. */
    uint64_t _fieldId;
    bool _isFieldShown;
} WorldTestInstances;


// Functions.
/** Sets up the copies' test content over a world, its collision world and its renderer (all borrowed, as the logger is).
 *  Nothing is made until the field is first shown. */
void WorldTestInstances_Construct(WorldTestInstances* self, World* world, WorldCollision* collision,
    WorldRenderer* renderer, Logger* logger);

/** The tick's debug keys (the apostrophe the pebble field, the semicolon the instancing A/B). */
void WorldTestInstances_HandleInput(WorldTestInstances* self, const GameInput* input);

/** Logs one line: the pebble field and whether the copies draw instanced (with the renderer's status, I). */
void WorldTestInstances_LogStatus(const WorldTestInstances* self);
