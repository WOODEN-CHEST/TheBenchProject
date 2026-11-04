#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "GameInput.h"
#include "Logger.h"
#include "NamespacedKey.h"
#include "World.h"
#include "WorldCollision.h"
#include "WorldRenderer.h"
#include "wr/WRMemory.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/) of the world test frame's paths: their debug view, a node that moves while the game runs, and
 * which region the camera is in. Test content only: WorldTestFrame.c calls it. The paths themselves (objects 95 to 98 of the
 * test world) are ordinary world-file content: a walk mixing every node mode and section setting, a closed park, and two
 * helices, one upright and one rotation minimizing.
 *
 * Keys (raw debug keys, read in Update after the UI had first refusal): Backspace steps the view (off, the paths, the paths
 * with their samples, frames and the ground under them); Shift with Backspace starts and stops one of the walk's nodes
 * moving, so its curve follows live. Each is logged, and so is the camera entering or leaving a region (a query by the
 * region's tag: tbp:test_park).
 */


// Types.
/** The test frame's paths. Borrowed pointers stay valid while the frame lives. */
typedef struct WorldTestPathsStruct
{
    World* _world;
    WorldCollision* _collision;
    WorldRenderer* _renderer;
    Logger* _logger;
    /** The list the renderer draws (WorldDebugLine; owned), rebuilt once per shown frame while the view is on. */
    GenericBuffer _lines;
    /** The view: 0 off, 1 the paths, 2 with the detail. */
    int _view;
    /** Whether the walk's node is moving, and for how long it has. */
    bool _isWaving;
    double _waveSeconds;
    /** The key of the park's tag; 0 when the registry holds no such key. */
    uint32_t _parkTag;
    /** Whether the camera was inside a region at the last look. */
    bool _wasCameraInside;
} WorldTestPaths;


// Functions.
/** Sets up the paths' test content over a world, its collision world and its renderer (all borrowed, as the logger is). */
void WorldTestPaths_Construct(WorldTestPaths* self, World* world, WorldCollision* collision, WorldRenderer* renderer,
    Logger* logger);

/** Takes the list back from the renderer and releases it. */
void WorldTestPaths_Deconstruct(WorldTestPaths* self);

/** The tick's debug keys (Backspace the view, Shift with it the moving node). */
void WorldTestPaths_HandleInput(WorldTestPaths* self, const GameInput* input);

/** Once per shown frame: moves the node by @p seconds, rebuilds the view's list and logs the camera crossing a region. */
void WorldTestPaths_PrepareRender(WorldTestPaths* self, double seconds, Vector3Double cameraPosition);

/** Logs one line: the paths, the view and what lies in the park (with the renderer's status, I). */
void WorldTestPaths_LogStatus(WorldTestPaths* self);
