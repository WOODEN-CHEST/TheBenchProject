#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "GameInput.h"
#include "Logger.h"
#include "World.h"
#include "WorldPathObjects.h"

/*
 * Private header (source/) of the world test frame's path objects: the keys that change the walk's fence while the game runs, as
 * the editor will, through its table's rows. Test content only: WorldTestFrame.c calls it. The objects themselves (99 to 110 of
 * the
 * test world) are ordinary world-file content, placeholder_path_objects.py's.
 *
 * Keys (raw debug keys, read in Update after the UI had first refusal, before the paths' own Backspace): Control with Backspace
 * steps the fence's fit (stretch, its ends resized by scaling, its ends cut, fixed and flush at both ends); Control and Shift
 * with
 * Backspace step how its panels take a slope (shear, tilt, step). Each is logged. Shift with Backspace alone (the paths' key)
 * moves
 * one of the walk's nodes, and the fence follows.
 */


// Types.
/** The test frame's path objects. Borrowed pointers stay valid while the frame lives. */
typedef struct WorldTestPathObjectsStruct
{
    World* _world;
    WorldPathObjects* _system;
    Logger* _logger;
    /** Which fit and which slope the fence shows (indices into the module's lists). */
    int _fit;
    int _slope;
} WorldTestPathObjects;


// Functions.
/** Sets up the keys over a world and its system (both borrowed, as the logger is). */
void WorldTestPathObjects_Construct(WorldTestPathObjects* self, World* world, WorldPathObjects* system, Logger* logger);

/** The tick's debug keys (Control with Backspace; takes the key, so the paths' own Backspace does not see it). */
void WorldTestPathObjects_HandleInput(WorldTestPathObjects* self, GameInput* input);

/** Logs one line: what the system holds and what the fence shows (with the renderer's status, I). */
void WorldTestPathObjects_LogStatus(WorldTestPathObjects* self);
