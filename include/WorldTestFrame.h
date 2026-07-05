#pragma once
#include "wr/WRError.h"


/**
 * @file WorldTestFrame.h
 * @brief A game frame that drops you into a world and lets you fly around it (a manual test scene).
 *
 * WorldTestFrame builds a small World with the test model at the centre, owns a GameCamera and a
 * WorldRenderer, and wires up a WASD + mouse-look free camera so the 3D world can be walked and the
 * renderer exercised end-to-end. It is the runnable milestone for the world system, not shipping content.
 *
 * Controls: W/A/S/D move, SPACE/LEFT_CONTROL rise/fall, LEFT_SHIFT to move faster, mouse to look. The
 * mouse cursor is captured while the frame is active.
 *
 * It is a concrete GameFrame: create it with WorldTestFrame_Create and hand the returned base frame to the
 * GameFrameManager, which then owns it.
 */


// Forward declarations.
/** @brief The shared services bundle; full type in Services.h. Borrowed by the frame. */
typedef struct ServicesStruct Services;
/** @brief The abstract game-frame base; full type in GameFrame.h. */
typedef struct GameFrameStruct GameFrame;


// Functions.
/**
 * @brief Creates the world-test frame and returns its GameFrame base (to hand to the frame manager).
 *
 * Builds the world (with the test model), the camera and the world renderer. On success @p outFrame
 * receives the embedded GameFrame*, which is what GameFrameManager_AddFrame takes; the manager owns the
 * frame from then on. On failure nothing is added and @p outFrame is NULL.
 * @param services The shared services (asset manager, logger, ...); borrowed, must outlive the frame. Must not be NULL.
 * @param outFrame [out] Receives the new frame's base on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p services or @p outFrame is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldTestFrame_Create(Services* services, GameFrame** outFrame);
