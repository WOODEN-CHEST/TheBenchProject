#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "GameCamera.h"
#include "GameInput.h"
#include "Logger.h"
#include "PlayerController.h"
#include "World.h"
#include "WorldCharacters.h"
#include "WorldCollision.h"

/*
 * Private header (source/) of the world test frame's characters (T13): walk mode (the player as an ordinary object with
 * a character component, driven by a PlayerController), the test NPCs (a human, a dog and a pigeon, each driven by a
 * scripted pattern through the same CharacterInput), the character events, the look target and the status lines.
 * Test content only: WorldTestFrame.c calls it from its tick.
 *
 * Keys (raw debug keys, read in Update after the UI had first refusal): F1 walk mode on and off, F2 the test NPCs on and
 * off, F3 the player's flight on and off, F4 the player's kind of character (human, dog, pigeon), F12 the head bob.
 */


// Macros.
/** How many test NPCs F2 spawns: a human, a dog and a pigeon. */
#define WORLD_TEST_NPC_COUNT 3U


// Types.
/** One test NPC: its object and its scripted brain's state. */
typedef struct WorldTestNpcStruct
{
    /** The NPC's object (0: not spawned). */
    uint64_t Id;
    /** Its body's child object (the placeholder model), for crouching it (0: none). */
    uint64_t BodyId;
    /** Seconds into the current part of its pattern, and which part. */
    float Seconds;
    uint32_t Phase;
    /** The direction it walks in this part (yaw, radians), and the centre of a circle it runs around. */
    float Heading;
    Vector3 Center;
} WorldTestNpc;

/** The test frame's characters. Borrowed pointers stay valid while the frame lives. */
typedef struct WorldTestCharactersStruct
{
    World* _world;
    WorldCharacters* _characters;
    WorldCollision* _collision;
    Logger* _logger;
    PlayerController _controller;
    bool _isWalking;
    uint64_t _playerId;
    size_t _playerKind;
    WorldTestNpc _npcs[WORLD_TEST_NPC_COUNT];
    bool _areNpcsSpawned;
} WorldTestCharacters;


// Functions.
/** Sets up the test characters over a world, its collision world and its character system (all borrowed). */
void WorldTestCharacters_Construct(WorldTestCharacters* self, World* world, WorldCollision* collision,
    WorldCharacters* characters, Logger* logger);

/** Releases what the test characters own (nothing beyond the controller; the objects belong to the world). */
void WorldTestCharacters_Deconstruct(WorldTestCharacters* self);

/** Whether walk mode is on (the camera then follows the player's eyes and the flycam does not move). */
static inline bool WorldTestCharacters_IsWalking(const WorldTestCharacters* self)
{
    return self->_isWalking;
}

/**
 * The tick's input, before the drivers: the debug keys (F1 walk mode, F2 NPCs, F3 flight, F4 kind, F12 head bob) and,
 * while walking, the player controller (which consumes the use action's press). @p camera places the player when walk
 * mode starts and the NPCs when they spawn.
 */
void WorldTestCharacters_HandleInput(WorldTestCharacters* self, GameInput* input, GameCamera* camera,
    bool isCursorCaptured);

/** The NPCs' scripted brains: each sets its character's input for the tick. Call before WorldCharacters_Update. */
void WorldTestCharacters_UpdateBrains(WorldTestCharacters* self, float deltaSeconds);

/**
 * After the collision step and the world matrices: logs and clears the character events, updates the look target (its
 * changes logged) and, while walking, puts the camera at the player's eyes.
 */
void WorldTestCharacters_AfterStep(WorldTestCharacters* self, GameCamera* camera, float deltaSeconds);

/** Logs the player's state and what it looks at (the I key's status line), or that walk mode is off. */
void WorldTestCharacters_LogStatus(WorldTestCharacters* self);
