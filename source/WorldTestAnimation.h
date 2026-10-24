#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "AssetManager.h"
#include "GameCamera.h"
#include "GameInput.h"
#include "Logger.h"
#include "World.h"
#include "WorldAnimation.h"
#include "wr/WRError.h"

/*
 * Private header (source/) of the world test frame's animation (T29): the animation system the frame ticks and poses,
 * and its debug keys. Test content only: WorldTestFrame.c calls it from its tick and its PrepareRender.
 *
 * Keys (raw debug keys, read in Update after the UI had first refusal): 5 steps the showcase figure (the test world's
 * "humanoid_showcase") through every clip of the placeholder library, cross-fading over its animator's fade time; 6
 * adds a crowd of WORLD_TEST_CROWD_COUNT placeholder figures in front of the camera, each walking at its own phase (half
 * of them the short figure), and removes it; 7 slows every animation to WORLD_TEST_SLOW_MOTION_SCALE of its speed and
 * back. Each is logged. The crowd's objects are runtime objects, never saved.
 */


// Macros.
/** How many figures the crowd has (a square of WORLD_TEST_CROWD_SIDE by WORLD_TEST_CROWD_SIDE). */
#define WORLD_TEST_CROWD_SIDE 10U
#define WORLD_TEST_CROWD_COUNT (WORLD_TEST_CROWD_SIDE * WORLD_TEST_CROWD_SIDE)
/** The share of its speed every animation plays at in slow motion. */
#define WORLD_TEST_SLOW_MOTION_SCALE 0.25f


// Types.
/** The test frame's animation. Borrowed pointers stay valid while the frame lives. */
typedef struct WorldTestAnimationStruct
{
    World* _world;
    Logger* _logger;
    /** The animation system (owned). */
    WorldAnimation* _animation;
    /** The showcase figure (0 if the test world lacks it) and the clip it plays (an index into the placeholder
     *  library's clip names). */
    uint64_t _showcaseId;
    size_t _showcaseClip;
    /** The crowd's objects, and whether it is out. */
    uint64_t _crowdIds[WORLD_TEST_CROWD_COUNT];
    bool _isCrowdShown;
    /** Whether slow motion is on. */
    bool _isSlowMotion;
} WorldTestAnimation;


// Functions.
/** Creates the animation system over a world (borrowed, as the assets and the logger are) and finds the showcase. */
Error WorldTestAnimation_Construct(WorldTestAnimation* self, World* world, AssetManager* assets, Logger* logger);

/** Destroys the animation system (before the world goes: WorldAnimation.h). The crowd's objects belong to the world. */
void WorldTestAnimation_Deconstruct(WorldTestAnimation* self);

/** The tick's debug keys (5 the showcase's clip, 6 the crowd, 7 slow motion); @p camera places the crowd. */
void WorldTestAnimation_HandleInput(WorldTestAnimation* self, const GameInput* input, const GameCamera* camera);

/** The tick's animation step (after the collision step and its events, before World_UpdateTransforms). */
void WorldTestAnimation_Update(WorldTestAnimation* self, float deltaSeconds);

/** The pose step, once per shown frame in the frame's PrepareRender (before World_UpdateTransforms and the render). */
void WorldTestAnimation_Pose(WorldTestAnimation* self);

/** Logs the animation system's last update and the debug state (the I key's status line). */
void WorldTestAnimation_LogStatus(const WorldTestAnimation* self);
