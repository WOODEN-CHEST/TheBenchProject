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
#include "WorldAnimation.h"
#include "WorldCharacters.h"
#include "WorldCollision.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/) of the world test frame's characters: walk mode (the player as an ordinary object with
 * a character component, driven by a PlayerController), the test NPCs (a human, a dog and a pigeon, each driven by a
 * scripted pattern through the same CharacterInput), the character events, the look target and the status lines.
 * Test content only: WorldTestFrame.c calls it from its tick.
 *
 * Bodies and actions: a human (the player as one, and the NPC human) wears the placeholder humanoid, whose
 * animator plays the world model's graph; the dog and the pigeon keep their boxes. The player's use (E) starts the
 * action the object looked at offers (text data under tbp:action on it or an ancestor: the bench's "sit", the coffee
 * cup's "pick_up", the door's "open_door"; a stand-in for a later interactable component), or ends the action the
 * player is in (standing up). Every action goes through the animation system's API, the NPC human's too.
 *
 * Keys (raw debug keys, read in Update after the UI had first refusal): F1 walk mode on and off, F2 the test NPCs on and
 * off, F3 the player's flight on and off, F4 the player's kind of character (human, dog, pigeon); 8 sends the NPC human to
 * sit on the bench and gets it up again, 9 has the player put down what it holds, 0 has the player and the NPC human
 * wave (the right arm's layer, over whatever they do).
 */


// Macros.
/** How many test NPCs F2 spawns: a human, a dog and a pigeon. */
#define WORLD_TEST_NPC_COUNT 3U
/** How many holders the test characters remember what they hold of (the player and the NPC human, with room). */
#define WORLD_TEST_MAX_HOLDERS 4U
/** The test content's action that makes everyone wave (input_actions/test_frame.json; default 0). */
#define WORLD_TEST_WAVE_ACTION ((const unsigned char*)u8"tbp:test_wave")


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
    /** The direction it walks in this part (yaw, radians), and the centre of a circle it runs around (in the world,
     *  double). */
    float Heading;
    Vector3Double Center;
    /** Whether it heads for the bench to sit (the 8 key; the human only). */
    bool WantsToSit;
} WorldTestNpc;

/** What one holder took (its action's "grab"), until a "release" lets it go: the test frame then settles it on what is
 *  under it (there is no physics yet). */
typedef struct WorldTestHeldStruct
{
    uint64_t HolderId;
    uint64_t HeldId;
} WorldTestHeld;

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
    /** The animation system (borrowed; NULL until WorldTestCharacters_SetAnimation): the bodies' actions. */
    WorldAnimation* _animation;
    /** What the holders took, by their grabs (HolderId 0: a free slot). */
    WorldTestHeld _held[WORLD_TEST_MAX_HOLDERS];
    /** Whether the test content's wave action (WORLD_TEST_WAVE_ACTION) is registered, and its id. */
    bool _hasWaveAction;
    GameActionId _waveAction;
} WorldTestCharacters;


// Functions.
/** Sets up the test characters over a world, its collision world and its character system (all borrowed), and finds
 *  the wave action in @p input once (without it, logged, nobody waves). */
void WorldTestCharacters_Construct(WorldTestCharacters* self, World* world, WorldCollision* collision,
    WorldCharacters* characters, GameInput* input, Logger* logger);

/** Releases what the test characters own (nothing beyond the controller; the objects belong to the world). */
void WorldTestCharacters_Deconstruct(WorldTestCharacters* self);

/** Gives the test characters the animation system the frame ticks (borrowed), for the bodies' actions. */
void WorldTestCharacters_SetAnimation(WorldTestCharacters* self, WorldAnimation* animation);

/** The player's object while walking, else 0: the object the renderer's camera rides (WorldRenderer_SetFirstPersonObject). */
static inline uint64_t WorldTestCharacters_GetPlayer(const WorldTestCharacters* self)
{
    return self->_isWalking ? self->_playerId : 0U;
}

/** Whether walk mode is on (the camera then follows the player's eyes and the flycam does not move). */
static inline bool WorldTestCharacters_IsWalking(const WorldTestCharacters* self)
{
    return self->_isWalking;
}

/**
 * The tick's input, before the drivers: the debug keys (F1 walk mode, F2 NPCs, F3 flight, F4 kind, 8 the NPC human
 * sits or stands, 9 the player puts down), the wave action (WORLD_TEST_WAVE_ACTION, default 0) and, while walking, the
 * player controller (which consumes the use action's press). @p camera places the player when walk mode starts and the
 * NPCs when they spawn.
 */
void WorldTestCharacters_HandleInput(WorldTestCharacters* self, GameInput* input, GameCamera* camera,
    bool isCursorCaptured);

/** The NPCs' scripted brains: each sets its character's input for the tick. Call before WorldCharacters_Update. */
void WorldTestCharacters_UpdateBrains(WorldTestCharacters* self, float deltaSeconds);

/**
 * After the collision step and the world matrices: logs and clears the character events (the player's use starts or
 * ends an action) and updates the look target (its changes logged).
 */
void WorldTestCharacters_AfterStep(WorldTestCharacters* self);

/**
 * After the animation step: logs the actions' starts, ends and failures and the graphs' events, settles what a
 * "release" let go of onto what is under it, and clears the animation system's queue. Returns the object a "push" event
 * pushed this tick (the door the frame swings), or 0.
 */
uint64_t WorldTestCharacters_HandleAnimationEvents(WorldTestCharacters* self);

/** While walking, puts the camera at the player's eyes (PlayerController_ApplyCamera): once per shown frame, in the
 *  frame's PrepareRender after the pose step and the world matrices, so it rides the posed body. */
void WorldTestCharacters_PlaceCamera(const WorldTestCharacters* self, GameCamera* camera);

/** Logs the player's state and what it looks at (the I key's status line), or that walk mode is off. */
void WorldTestCharacters_LogStatus(WorldTestCharacters* self);

/**
 * The F11 key's far shift, after the world moved by @p offset (every root object, the characters among them) and
 * the collision world was synced: places every character of the world again where it now stands, through the character
 * system's teleport, so its motion starts afresh on its moved ground (the player keeps where it looks, the others where
 * they face), and moves the test NPCs' patterns by @p offset. Returns how many characters it placed.
 */
size_t WorldTestCharacters_Shift(WorldTestCharacters* self, Vector3Double offset);
