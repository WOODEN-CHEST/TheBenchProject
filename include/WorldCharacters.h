#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector3 is a raylib type held by value (positions); this include is unavoidable here.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRVectorMath.h"
#include "WorldCollision.h"

// Forward declarations (World.h, Logger.h).
typedef struct WorldStruct World;
typedef struct LoggerStruct Logger;


/**
 * @file WorldCharacters.h
 * @brief The character system: moves every character of a world once a tick.
 *
 * A character is an object with a character component (WorldCharacterComponent.h): an upright capsule whose feet are at
 * its object's position, set up by a character asset and its placement's overrides, driven by a CharacterInput that the
 * player controller or an NPC brain fills the same way. WorldCharacters_Update runs in the tick's DRIVERS slot, after
 * the other drivers (platforms, doors, scripts: what they moved this tick carries the characters exactly) and before
 * WorldCollision_Step, which then pushes characters and pushables apart by mass. For each enabled character it:
 * 1. switches between walking and flying, and between standing and crouching (standing up only where there is room);
 * 2. carries a grounded character with the object it stands on (its move and its turn since the last update);
 * 3. turns the input into a velocity: the keys steering (or pushing) it on the ground and the ground's friction, jumps
 *    (buffered, with coyote time, auto-jump and a jump boost), gravity, the air's drag and air control, or flight;
 * 4. moves and slides the capsule against static and kinematic colliders (pushables are left to the collision step),
 *    keeping the skin to everything it passes; while walking, steep ground and edges higher than its step height
 *    count as walls, and steps up to its step height are climbed;
 * 5. keeps a walking character on walkable slopes (and within its step-down distance), or lets it fall off (every step,
 *    by default) and land;
 * 6. turns its body, eases its eyes between the standing and the crouched height, and writes its object's transform;
 * 7. answers a use press with a "used" event naming what it looks at.
 * An ACTION may constrain it (WorldCharacterComponent_SetActionControl, set by the animation system): a movement
 * lock (no moving: no move, jump, crouch or flight changes; no turning: the body keeps its facing; locked: both, the
 * look still free), and an ANCHOR: the character slides there instead of walking, swept against walls like any move,
 * turning to the anchor's facing, over the approach time; within CHARACTER_ANCHOR_TOLERANCE and
 * CHARACTER_ANCHOR_YAW_TOLERANCE it has arrived, and stays there held (its collider not pushable) while its movement
 * is locked; past the approach time and CHARACTER_APPROACH_GRACE_SECONDS more it has failed.
 * It also gives every character a capsule collider (added when its object has none) and keeps that collider's shape,
 * layer, mask, mass and motion rows in step with the character.
 *
 * Events (jumped, landed, left the ground, crouched, stood up, took off, stopped flying, used) queue until
 * WorldCharacters_ClearEvents, like the collision world's.
 *
 * LIFETIME. Create it after the world and the collision world, which it borrows; destroy it before them. It holds no
 * pointers to components between updates (it finds them through the world each time).
 */


// Macros.
/** @brief The gap a character's capsule keeps to everything it moves past, metres: the collision step's skin
 *  (WORLD_COLLISION_SKIN), so the step never has to push a character out of what it walks by. It rests a little
 *  farther (the private CHARACTER_REST_GAP more) from what it stands on and slides along. */
#define CHARACTER_SKIN WORLD_COLLISION_SKIN
/** @brief The most planes one move slides along before it stops. */
#define CHARACTER_MAX_SLIDE_ITERATIONS 4U
/** @brief How near its anchor a character's feet must come to have arrived, in metres. */
#define CHARACTER_ANCHOR_TOLERANCE 0.01f
/** @brief How near its anchor's facing a character must turn to have arrived, in radians (1 degree). */
#define CHARACTER_ANCHOR_YAW_TOLERANCE 0.017453292f
/** @brief How long past its approach time a character may still arrive before the approach fails, in seconds. */
#define CHARACTER_APPROACH_GRACE_SECONDS 0.5f


// Types.
/**
 * @brief What happened to a character. Saved by value in event logs and scripts later: never renumber.
 */
typedef enum WorldCharacterEventKindEnum
{
    /** @brief It jumped (from the ground, or within its coyote time). */
    WorldCharacterEventKind_Jumped = 0,
    /** @brief It landed on walkable ground: OtherId is what it landed on, Value the impact speed (metres per second,
     *  downward). */
    WorldCharacterEventKind_Landed = 1,
    /** @brief It left the ground without jumping: walked off an edge, was pushed off, lost its ground. */
    WorldCharacterEventKind_LeftGround = 2,
    /** @brief It crouched. */
    WorldCharacterEventKind_Crouched = 3,
    /** @brief It stood up. */
    WorldCharacterEventKind_StoodUp = 4,
    /** @brief It started flying. */
    WorldCharacterEventKind_TookOff = 5,
    /** @brief It stopped flying (it walks, and falls if it is in the air). */
    WorldCharacterEventKind_StoppedFlying = 6,
    /** @brief It used something: OtherId is what it looked at (0: nothing within reach), Value its distance. */
    WorldCharacterEventKind_Used = 7
} WorldCharacterEventKind;

/**
 * @brief One queued character event.
 */
typedef struct WorldCharacterEventStruct
{
    /** @brief What happened. */
    WorldCharacterEventKind Kind;
    /** @brief The character's object. */
    uint64_t CharacterId;
    /** @brief The other object involved (what it landed on, what it used), 0 for none. */
    uint64_t OtherId;
    /** @brief The kind's number (the landing's impact speed, the used object's distance), 0 otherwise. */
    float Value;
    /** @brief The update it happened in (WorldCharacters_GetTick). */
    uint64_t Tick;
} WorldCharacterEvent;

/**
 * @brief The character system of one world. Opaque; create with WorldCharacters_Create.
 */
typedef struct WorldCharactersStruct WorldCharacters;


// Functions.
/**
 * @brief Creates the character system of a world.
 * @param world The world (borrowed: it must outlive the system); must not be NULL.
 * @param collision The world's collision world (borrowed: it must outlive the system); must not be NULL.
 * @param logger Logs warnings once each (a character whose collider could not be added, an object scaled); may be
 *        NULL (borrowed).
 * @param outCharacters [out] Receives the system. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument.
 */
Error WorldCharacters_Create(World* world, WorldCollision* collision, Logger* logger, WorldCharacters** outCharacters);

/**
 * @brief Destroys a character system. The characters keep their state in their components.
 * @param self The system, or NULL.
 */
void WorldCharacters_Destroy(WorldCharacters* self);

/**
 * @brief Moves every enabled character by one tick (see the file comment). Call once a tick in the drivers slot, after
 *        the other drivers and before WorldCollision_Step.
 * @param self The system; must not be NULL.
 * @param deltaSeconds The tick's length, in seconds (at most 0.1 is used; nothing happens at 0 or less).
 */
void WorldCharacters_Update(WorldCharacters* self, float deltaSeconds);

/**
 * @brief Places a character: feet at @p feetPosition, facing and looking along @p yaw, at rest; without a sweep (a
 *        spawn, a script's teleport). It is settled on walkable ground within its step height below, or falls.
 * @param self The system; must not be NULL.
 * @param objectId The character's object.
 * @param feetPosition Where its feet go, in world space (double, like every world position).
 * @param yaw Its facing, radians.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self or an object without a character component;
 *          ErrorCode_ArgumentOutOfRange for a yaw or a position that is not finite, or a position where the object's
 *          own (its local one, under a parent) would pass WORLD_POSITION_LIMIT on an axis; ErrorCode_InvalidOperation
 *          under a parent whose world matrix cannot be inverted (a zero scale). Refused, nothing changes.
 */
Error WorldCharacters_Teleport(WorldCharacters* self, uint64_t objectId, Vector3Double feetPosition, float yaw);

/**
 * @brief What a character looks at: a ray from its eyes along its input's look direction, out to its reach, on its
 *        interaction layers, ignoring triggers, its own object and what it carries. Any character can ask: the player
 *        each tick, an NPC when its brain wants to.
 * @param self The system; must not be NULL.
 * @param objectId The character's object.
 * @param outHit [out] Receives the hit. Must not be NULL.
 * @returns true when the ray hit something; false otherwise (also for an object without a character).
 */
bool WorldCharacters_FindLookTarget(WorldCharacters* self, uint64_t objectId, WorldRayHit* outHit);

/**
 * @brief Returns the queued events, oldest first.
 * @param self The system; must not be NULL.
 * @param outCount [out] Receives how many. Must not be NULL.
 * @returns The events (borrowed: valid until the next update or clear), or NULL with a count of 0.
 */
const WorldCharacterEvent* WorldCharacters_GetEvents(const WorldCharacters* self, size_t* outCount);

/**
 * @brief Drops the queued events (after handling them).
 * @param self The system; must not be NULL.
 */
void WorldCharacters_ClearEvents(WorldCharacters* self);

/**
 * @brief Returns how many updates have run.
 * @param self The system; must not be NULL.
 * @returns The count.
 */
uint64_t WorldCharacters_GetTick(const WorldCharacters* self);
