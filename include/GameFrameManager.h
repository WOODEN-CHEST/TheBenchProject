#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "ProgramTime.h"
#include "GameFrame.h"


/**
 * Game frame manager module.
 *
 * The GameFrameManager is the core of the game-frame system: it owns a set of game frames, drives each
 * through its full lifecycle (load -> start -> update -> render -> end -> unload -> destroy), and
 * composites all rendered frames onto the screen.
 *
 * OWNERSHIP. A frame handed to GameFrameManager_AddFrame is OWNED by the manager from then on: once the
 * frame finishes unloading (or when the manager is deconstructed) the manager calls the frame's Destroy.
 * Do not free an added frame yourself.
 *
 * MULTIPLE ACTIVE FRAMES. Frames are held in an ordered z-stack (index 0 at the bottom). Several can be
 * active at once — a menu on top of a frozen game, a loading screen while the next scene loads, or two
 * scenes mid-cross-fade. Toggle a frame's GameFrame.IsUpdated / GameFrame.IsRendered to freeze or hide
 * it without removing it.
 *
 * RENDERING. Each active, rendered frame draws into its own render target; the manager then composites
 * every target onto the screen bottom-to-top, tinted by each frame's GameFrame.CompositeColor. Animate a
 * frame's CompositeColor opacity to fade the whole frame in or out.
 *
 * LOOP INTEGRATION. The caller owns the main loop and its timing. It calls GameFrameManager_Update once
 * per fixed update tick (with the fixed delta) and GameFrameManager_Render once per render frame (with
 * the real frame delta). If any frame method fails, the manager logs it as CRITICAL and asks the loop to
 * stop (GameFrameManager_ShouldStop), turning an error into a graceful shutdown.
 *
 * All manager calls must be made from the main thread (they drive Raylib rendering and, through frames,
 * the AssetManager, both of which are main-thread-only).
 */


// Forward declarations.
/** @brief The logger the manager writes crash diagnostics to; full type in Logger.h. Borrowed. */
typedef struct LoggerStruct Logger;


// Types.
/**
 * @brief Lifecycle state of a frame as tracked by the manager.
 *
 * Progresses Loading -> Loaded -> Active -> Unloading -> Unloaded. A frame in Loaded has finished
 * loading but not yet started (useful for preloading the next scene and activating it later).
 */
typedef enum GameFrameStateEnum
{
    /** @brief Loading: BeginLoad has run and LoadStep is being called until IsLoaded. */
    GameFrameState_Loading,
    /** @brief Loaded: loading is complete; awaiting start (immediately if started automatically). */
    GameFrameState_Loaded,
    /** @brief Active: Start has run; the frame is updated and rendered. */
    GameFrameState_Active,
    /** @brief Unloading: End/BeginUnload have run and UnloadStep is being called until IsUnloaded. */
    GameFrameState_Unloading,
    /** @brief Unloaded: unloading is complete; the manager will destroy and drop the frame. */
    GameFrameState_Unloaded
} GameFrameState;

/**
 * @brief Options controlling how a frame is added to the manager.
 */
typedef struct GameFrameAddOptionsStruct
{
    /** @brief true to Start the frame automatically once it finishes loading; false to wait for
     *         GameFrameManager_ActivateFrame (e.g. to preload a scene and activate it later). */
    bool StartAutomatically;
    /** @brief true to place the frame at the top of the z-stack (drawn last, over others); false to
     *         place it at the bottom (drawn first, behind others). */
    bool AddToTop;
} GameFrameAddOptions;

/**
 * @brief The game frame manager: owns and drives a z-stack of game frames.
 *
 * A value type the caller holds (typically in main). Construct with GameFrameManager_Construct and
 * release with GameFrameManager_Deconstruct. Underscore-prefixed fields are internal and read-only to
 * code outside this module.
 */
typedef struct GameFrameManagerStruct
{
    /** @brief Borrowed logger used for CRITICAL crash diagnostics; not owned. */
    Logger* _logger;
    /** @brief Owned buffer of internal frame records (one heap record pointer per resident frame). */
    GenericBuffer _records;
    /** @brief Aspect ratio (width / height) every frame renders its content fitted to. */
    float _targetAspectRatio;
    /** @brief Normalized [0;1] placement of the target area within each render target. */
    Vector2 _targetAreaPosition;
    /** @brief Current render-target width in pixels (tracks the window; 0 until the first render). */
    int32_t _targetWidth;
    /** @brief Current render-target height in pixels (tracks the window; 0 until the first render). */
    int32_t _targetHeight;
    /** @brief Set true when a frame method failed fatally; the loop should stop and shut down. */
    bool _shouldStop;
} GameFrameManager;


// Functions.
/**
 * @brief Returns the default add options: start automatically, add to the top of the z-stack.
 * @returns A GameFrameAddOptions with StartAutomatically = true and AddToTop = true.
 */
GameFrameAddOptions GameFrameAddOptions_Default(void);

/**
 * @brief Initializes an empty game frame manager.
 *
 * The manager starts with no frames. @p logger is borrowed and must outlive the manager. The render
 * target size is discovered from the window on the first render, so the window need not exist yet.
 * @param self The manager to initialize. Must not be NULL.
 * @param logger Borrowed logger for crash diagnostics. Must not be NULL and must outlive the manager.
 * @param targetAspectRatio Aspect ratio (width / height) frames render fitted to; must be > 0.
 * @param targetAreaPosition Normalized [0;1] placement of the target area within each render target
 *        (e.g. (0.5, 0.5) to center it).
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p logger is NULL, or @p targetAspectRatio
 *          is not positive.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameFrameManager_Construct(GameFrameManager* self, Logger* logger, float targetAspectRatio,
    Vector2 targetAreaPosition);

/**
 * @brief Tears down the manager and every frame it still owns.
 *
 * For each resident frame, ends it (if it was active) and destroys it, releasing all render targets, then
 * frees the manager's own storage. Teardown is best-effort: the first error is returned and all later
 * errors are released so none leak, and every frame is still destroyed. Must be called while the window
 * (GL context) still exists, since it frees GPU render targets. Safe on a NULL manager.
 * @param self The manager to release, or NULL.
 * @returns Success, or the first non-success Error encountered while tearing down.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameFrameManager_Deconstruct(GameFrameManager* self);

/**
 * @brief Adds a frame to the manager and begins loading it; the manager takes ownership.
 *
 * Places the frame in the z-stack per @p options, then calls the frame's BeginLoad. From here the manager
 * drives it: LoadStep each update until loaded (raising OnLoad), then Start (immediately if
 * @p options.StartAutomatically, otherwise on GameFrameManager_ActivateFrame). On failure the frame is
 * NOT added and the caller retains ownership.
 * @param self The manager. Must not be NULL.
 * @param frame The frame to add; ownership transfers to the manager on success. Must not be NULL and must
 *        not already be in the manager.
 * @param options How to place and start the frame (see GameFrameAddOptions_Default).
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p frame is NULL; ErrorCode_InvalidOperation
 *          if the frame is already managed; propagates the frame's BeginLoad error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameFrameManager_AddFrame(GameFrameManager* self, GameFrame* frame, GameFrameAddOptions options);

/**
 * @brief Begins removing a frame: ends it (if active) and starts its unload; the manager destroys it later.
 *
 * Safe to call at any time, including from within a frame's Update. The frame is not destroyed
 * immediately; the manager runs its stepped unload and then destroys it. Removing an already-unloading
 * frame is a no-op success.
 * @param self The manager. Must not be NULL.
 * @param frame The frame to remove; must currently be managed by @p self. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p frame is NULL; ErrorCode_InvalidOperation
 *          if the frame is not managed by @p self; propagates the frame's End/BeginUnload error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameFrameManager_RemoveFrame(GameFrameManager* self, GameFrame* frame);

/**
 * @brief Requests that a loaded-but-not-yet-started frame be started.
 *
 * For a frame added with StartAutomatically = false: once it has finished loading, this makes the manager
 * Start it on the next update. Calling it before loading finishes is remembered and applied when loading
 * completes. Harmless (success no-op) if the frame is already active.
 * @param self The manager. Must not be NULL.
 * @param frame The frame to activate; must currently be managed by @p self. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p frame is NULL; ErrorCode_InvalidOperation
 *          if the frame is not managed by @p self.
 */
Error GameFrameManager_ActivateFrame(GameFrameManager* self, GameFrame* frame);

/**
 * @brief Advances every frame by one update tick, driving lifecycle transitions and updates.
 *
 * For each frame the manager performs one lifecycle action this tick: a load step, a start, an update
 * (active frames whose IsUpdated is set), an unload step, or destruction once unloaded. Call once per
 * fixed update tick with @p time.PassedTime equal to the fixed update delta.
 * @param self The manager. Must not be NULL.
 * @param time The update-tick time (fixed delta).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL. If a frame method fails, the manager
 *          has already logged it as CRITICAL and set the stop flag, and that Error is returned.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameFrameManager_Update(GameFrameManager* self, ProgramTime time);

/**
 * @brief Renders all active frames and composites them onto the screen.
 *
 * Recreates render targets if the window size changed (notifying frames via OnResize), renders each
 * active, IsRendered frame into its own target, then composites the targets onto the backbuffer
 * bottom-to-top using each frame's CompositeColor. Call once per render frame with @p time.PassedTime
 * equal to the real frame delta.
 * @param self The manager. Must not be NULL.
 * @param time The render-frame time (real delta).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL. If a frame's Render (or OnResize) fails,
 *          the manager has already logged it as CRITICAL and set the stop flag, and that Error is returned.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameFrameManager_Render(GameFrameManager* self, ProgramTime time);

/**
 * @brief Reports whether a fatal frame error has asked the loop to stop.
 * @param self The manager; may be NULL (returns true, i.e. "stop").
 * @returns true if the manager wants the loop to stop and shut down, false to keep running.
 */
bool GameFrameManager_ShouldStop(GameFrameManager* self);

/**
 * @brief Returns the number of frames currently resident in the manager.
 * @param self The manager; may be NULL (returns 0).
 * @returns The resident frame count.
 */
size_t GameFrameManager_GetFrameCount(GameFrameManager* self);

/**
 * @brief Reads a managed frame's current lifecycle state.
 * @param self The manager. Must not be NULL.
 * @param frame The frame to query; must currently be managed by @p self. Must not be NULL.
 * @param outState [out] Receives the frame's state on success. Must not be NULL.
 * @returns true if the frame is managed and its state was written; false otherwise (including NULL args).
 */
bool GameFrameManager_TryGetFrameState(GameFrameManager* self, GameFrame* frame, GameFrameState* outState);
