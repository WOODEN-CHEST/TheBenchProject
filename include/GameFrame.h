#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WREvent.h"
#include "ProgramTime.h"
// Renderer.h pulls in Raylib; GameFrame needs RenderColor (composite tint), RenderTexture2D (render
// target) and Vector2 (target-area placement), all of which are unavoidable in this public API.
#include "Renderer.h"


/**
 * Game frame module.
 *
 * A "game frame" is one self-contained section of the game: an in-game level, the editor, the main
 * menu, a loading screen, and so on. GameFrame is the ABSTRACT base every such section derives from; a
 * concrete frame embeds a GameFrame as its FIRST member and supplies a vtable of behavior. The
 * GameFrameManager owns the frames and drives them through their lifecycle.
 *
 * A frame has two parallel lifecycles that the manager runs:
 *   - Resource lifecycle (stepped / cooperative): BeginLoad -> LoadStep* -> IsLoaded, and symmetrically
 *     BeginUnload -> UnloadStep* -> IsUnloaded. Each step does a bounded chunk of work and returns, so
 *     the loop can keep rendering a loading screen between steps (this mirrors the AssetManager's
 *     stepped bulk-load model).
 *   - Logic lifecycle: Start (once, after loading) -> Update* (while active) -> End (once, before
 *     unloading).
 * The overall order the manager applies is:
 *     BeginLoad -> [LoadStep ... until IsLoaded] -> (OnLoad) -> Start -> [Update / Render ...]
 *         -> End -> BeginUnload -> [UnloadStep ... until IsUnloaded] -> (OnUnload) -> Destroy.
 *
 * Rendering: each rendered frame is handed its OWN render target and draws into it; the manager then
 * composites every frame's target onto the screen using the frame's CompositeColor. That is what lets
 * several frames be on screen at once (e.g. a menu over a frozen game) and enables whole-scene
 * cross-fades by animating CompositeColor.
 *
 * Every method that can fail returns an Error so failures propagate like exceptions; the manager turns
 * any such error into a logged, graceful shutdown.
 */


// Types.
/**
 * @brief The per-render payload handed to a frame's Render method (alongside its target).
 *
 * Carries the render-tick time plus the aspect-ratio fitting configuration the frame should draw with,
 * so every frame fits its content to the same target area consistently. A plain value type owning
 * nothing. (Distinct from the renderer's RenderContext, which is the drawing device a frame builds from
 * the target it is given.)
 */
typedef struct FrameRenderContextStruct
{
    /** @brief The render tick's time (real wall time and real frame delta, in seconds). */
    ProgramTime Time;
    /** @brief Aspect ratio (width / height) the game is designed for; defines the target render area. */
    float TargetAspectRatio;
    /** @brief Normalized [0;1] placement of the target area within the buffer; (0.5, 0.5) centers it. */
    Vector2 TargetAreaPosition;
} FrameRenderContext;


/**
 * @brief Virtual table of behavior for a concrete game frame.
 *
 * A concrete frame provides one static instance of this table. Every function pointer receives the
 * frame's own object as @c self (a @c void* recovered without a cast, per the project's OOP
 * convention). The core slots must be non-NULL; the slots explicitly documented as optional may be
 * NULL, in which case the manager substitutes a sensible default (see the GameFrame_* wrappers).
 *
 * Any slot returning an Error aborts the manager's current operation: the manager logs the failure as
 * CRITICAL and shuts the program down gracefully, so treat a returned error as fatal.
 */
typedef struct GameFrameVTableStruct
{
    /**
     * @brief Begins the frame's logic; called once after it has finished loading, before the first Update.
     * @param self The concrete frame.
     * @param time The update-tick time at which the frame is started.
     * @returns Success, or a non-success Error to abort (fatal).
     */
    Error (*Start)(void* self, ProgramTime time);

    /**
     * @brief Ends the frame's logic; called once when the frame is being removed, before unloading.
     *
     * Only called if the frame was actually started (reached the active state). Should stop timers,
     * persist state, etc. — but NOT release loaded resources (that is the unload phase's job).
     * @param self The concrete frame.
     * @param time The update-tick time at which the frame is ended.
     * @returns Success, or a non-success Error to abort (fatal).
     */
    Error (*End)(void* self, ProgramTime time);

    /**
     * @brief Advances the frame's logic by one fixed update step.
     *
     * Called every update tick while the frame is active and its IsUpdated flag is set. @c time.PassedTime
     * is the fixed update delta (constant), not a variable frame time.
     * @param self The concrete frame.
     * @param time The update-tick time (fixed delta).
     * @returns Success, or a non-success Error to abort (fatal).
     */
    Error (*Update)(void* self, ProgramTime time);

    /**
     * @brief Starts the frame's loading process (does not perform the loading itself).
     *
     * Sets up whatever the LoadStep calls will drive (e.g. an AssetManager bulk operation). Called once
     * before the first LoadStep.
     * @param self The concrete frame.
     * @returns Success, or a non-success Error to abort (fatal).
     */
    Error (*BeginLoad)(void* self);

    /**
     * @brief Performs one bounded chunk of loading work and returns control to the caller.
     *
     * Called repeatedly (typically once per update tick) until IsLoaded reports completion. Each call
     * should do a small, bounded amount of work so the loop stays responsive.
     * @param self The concrete frame.
     * @returns Success, or a non-success Error to abort (fatal).
     */
    Error (*LoadStep)(void* self);

    /**
     * @brief Reports whether the frame has finished loading.
     * @param self The concrete frame.
     * @returns true once loading is complete (no more LoadStep calls are needed), false otherwise.
     */
    bool (*IsLoaded)(void* self);

    /**
     * @brief Starts the frame's unloading process (does not perform the unloading itself).
     * @param self The concrete frame.
     * @returns Success, or a non-success Error to abort (fatal).
     */
    Error (*BeginUnload)(void* self);

    /**
     * @brief Performs one bounded chunk of unloading work and returns control to the caller.
     *
     * Called repeatedly until IsUnloaded reports completion.
     * @param self The concrete frame.
     * @returns Success, or a non-success Error to abort (fatal).
     */
    Error (*UnloadStep)(void* self);

    /**
     * @brief Reports whether the frame has finished unloading.
     * @param self The concrete frame.
     * @returns true once unloading is complete, false otherwise.
     */
    bool (*IsUnloaded)(void* self);

    /**
     * @brief Renders the frame into the given target.
     *
     * Called each render pass while the frame is active and its IsRendered flag is set. The frame builds
     * a renderer RenderContext from @p target (using the fitting configuration in @p context), begins a
     * pass, clears the target (typically to transparent) and draws, then ends the pass. The manager
     * composites the target onto the screen afterwards using the frame's CompositeColor.
     * @param self The concrete frame.
     * @param context The render payload (time + fitting configuration); borrowed, valid for the call.
     * @param target The render target to draw into; owned by the manager, borrowed for the call.
     * @returns Success, or a non-success Error to abort (fatal).
     */
    Error (*Render)(void* self, const FrameRenderContext* context, RenderTexture2D target);

    /**
     * @brief OPTIONAL: reports loading progress as a fraction in [0;1] for a loading bar. May be NULL.
     *
     * When NULL, the manager's GameFrame_GetLoadProgress wrapper returns a negative value meaning
     * "unknown / indeterminate".
     * @param self The concrete frame.
     * @returns A progress fraction in [0;1].
     */
    float (*GetLoadProgress)(void* self);

    /**
     * @brief OPTIONAL: notifies the frame that the render surface size changed. May be NULL.
     *
     * Called by the manager when the window/render resolution changes so the frame can rebuild any
     * size-dependent state. Frames drawing purely in normalized coordinates usually need nothing here.
     * @param self The concrete frame.
     * @param width New render width in pixels.
     * @param height New render height in pixels.
     * @returns Success, or a non-success Error to abort (fatal).
     */
    Error (*OnResize)(void* self, int32_t width, int32_t height);

    /**
     * @brief Frees the concrete frame and everything it owns.
     *
     * Must release the frame's own resources (loaded assets, GPU objects, allocations), call
     * GameFrame_Deconstruct on its embedded base, and free the frame object itself. Called by the
     * manager once the frame is fully unloaded, and also during a forced teardown, so it must fully
     * release resources even if the stepped unload did not run.
     * @param self The concrete frame.
     */
    void (*Destroy)(void* self);
} GameFrameVTable;


/**
 * @brief Abstract base of every game frame: its vtable plus manager-visible shared state.
 *
 * A concrete frame embeds this as its FIRST member, so a pointer to the concrete frame is also a valid
 * GameFrame*. Initialize the base with GameFrame_Construct inside the concrete constructor, and release
 * it with GameFrame_Deconstruct inside the concrete Destroy. The manager tracks each frame's lifecycle
 * state separately (see GameFrameManager); it is not stored here.
 */
typedef struct GameFrameStruct
{
    /** @brief Behavior table for this frame; set by GameFrame_Construct. Never NULL after construction. */
    const GameFrameVTable* VTable;

    /** @brief When true (default), the manager calls Update on this frame each tick while it is active. */
    bool IsUpdated;
    /** @brief When true (default), the manager renders and composites this frame while it is active. */
    bool IsRendered;
    /** @brief Tint/opacity the manager composites this frame's target with; defaults to opaque white.
     *         Animate its Opacity to fade the whole frame in or out for transitions. */
    RenderColor CompositeColor;

    /** @brief Raised (by the manager) once loading finishes; the event payload is this GameFrame*. */
    WREvent OnLoad;
    /** @brief Raised (by the manager) once unloading finishes; the event payload is this GameFrame*. */
    WREvent OnUnload;

    /** @brief Borrowed, NUL-terminated UTF-8 name used in log/crash messages; may be NULL. Must outlive the frame. */
    const unsigned char* DebugName;
} GameFrame;


// Base functions.
/**
 * @brief Initializes the abstract base of a game frame.
 *
 * Sets the vtable and DebugName, defaults IsUpdated and IsRendered to true and CompositeColor to opaque
 * white, and constructs the OnLoad and OnUnload events. Call this first inside a concrete frame's
 * constructor, then fill in the concrete fields. Release with GameFrame_Deconstruct.
 * @param self The base to initialize (usually &concrete->Base). Must not be NULL.
 * @param vtable The concrete frame's behavior table. Must not be NULL; its core slots must be non-NULL.
 * @param debugName Borrowed NUL-terminated UTF-8 name for diagnostics, or NULL. Must outlive the frame.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p vtable is NULL; propagates event
 *          construction errors.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameFrame_Construct(GameFrame* self, const GameFrameVTable* vtable, const unsigned char* debugName);

/**
 * @brief Releases the abstract base of a game frame (its events).
 *
 * Deconstructs the OnLoad and OnUnload events. Does NOT free @p self or any concrete resources — the
 * concrete Destroy is responsible for those and calls this as part of its teardown. Safe on NULL.
 * @param self The base to release, or NULL.
 */
void GameFrame_Deconstruct(GameFrame* self);


// Vtable wrappers (call sites go through these, never through the vtable directly).
/**
 * @brief Starts the frame's logic. See GameFrameVTable::Start.
 * @param self The frame; must not be NULL.
 * @param time The update-tick time.
 * @returns The frame's Start result.
 */
static inline Error GameFrame_Start(GameFrame* self, ProgramTime time)
{
    return self->VTable->Start(self, time);
}

/**
 * @brief Ends the frame's logic. See GameFrameVTable::End.
 * @param self The frame; must not be NULL.
 * @param time The update-tick time.
 * @returns The frame's End result.
 */
static inline Error GameFrame_End(GameFrame* self, ProgramTime time)
{
    return self->VTable->End(self, time);
}

/**
 * @brief Advances the frame's logic by one update step. See GameFrameVTable::Update.
 * @param self The frame; must not be NULL.
 * @param time The update-tick time (fixed delta).
 * @returns The frame's Update result.
 */
static inline Error GameFrame_Update(GameFrame* self, ProgramTime time)
{
    return self->VTable->Update(self, time);
}

/**
 * @brief Begins loading the frame. See GameFrameVTable::BeginLoad.
 * @param self The frame; must not be NULL.
 * @returns The frame's BeginLoad result.
 */
static inline Error GameFrame_BeginLoad(GameFrame* self)
{
    return self->VTable->BeginLoad(self);
}

/**
 * @brief Performs one loading step. See GameFrameVTable::LoadStep.
 * @param self The frame; must not be NULL.
 * @returns The frame's LoadStep result.
 */
static inline Error GameFrame_LoadStep(GameFrame* self)
{
    return self->VTable->LoadStep(self);
}

/**
 * @brief Reports whether the frame finished loading. See GameFrameVTable::IsLoaded.
 * @param self The frame; must not be NULL.
 * @returns true if loading is complete.
 */
static inline bool GameFrame_IsLoaded(GameFrame* self)
{
    return self->VTable->IsLoaded(self);
}

/**
 * @brief Begins unloading the frame. See GameFrameVTable::BeginUnload.
 * @param self The frame; must not be NULL.
 * @returns The frame's BeginUnload result.
 */
static inline Error GameFrame_BeginUnload(GameFrame* self)
{
    return self->VTable->BeginUnload(self);
}

/**
 * @brief Performs one unloading step. See GameFrameVTable::UnloadStep.
 * @param self The frame; must not be NULL.
 * @returns The frame's UnloadStep result.
 */
static inline Error GameFrame_UnloadStep(GameFrame* self)
{
    return self->VTable->UnloadStep(self);
}

/**
 * @brief Reports whether the frame finished unloading. See GameFrameVTable::IsUnloaded.
 * @param self The frame; must not be NULL.
 * @returns true if unloading is complete.
 */
static inline bool GameFrame_IsUnloaded(GameFrame* self)
{
    return self->VTable->IsUnloaded(self);
}

/**
 * @brief Renders the frame into a target. See GameFrameVTable::Render.
 * @param self The frame; must not be NULL.
 * @param context The render payload; must not be NULL.
 * @param target The render target to draw into.
 * @returns The frame's Render result.
 */
static inline Error GameFrame_Render(GameFrame* self, const FrameRenderContext* context, RenderTexture2D target)
{
    return self->VTable->Render(self, context, target);
}

/**
 * @brief Returns the frame's load progress, or a negative value if the frame does not report progress.
 * @param self The frame; must not be NULL.
 * @returns A fraction in [0;1] from the frame, or -1.0f when GetLoadProgress is not implemented.
 */
static inline float GameFrame_GetLoadProgress(GameFrame* self)
{
    if (self->VTable->GetLoadProgress == NULL)
    {
        return -1.0f;
    }
    return self->VTable->GetLoadProgress(self);
}

/**
 * @brief Notifies the frame of a render-surface size change, or does nothing if unimplemented.
 * @param self The frame; must not be NULL.
 * @param width New render width in pixels.
 * @param height New render height in pixels.
 * @returns The frame's OnResize result, or success when OnResize is not implemented.
 */
static inline Error GameFrame_OnResize(GameFrame* self, int32_t width, int32_t height)
{
    if (self->VTable->OnResize == NULL)
    {
        return Error_CreateSuccess();
    }
    return self->VTable->OnResize(self, width, height);
}

/**
 * @brief Destroys the frame, freeing it and everything it owns. See GameFrameVTable::Destroy.
 * @param self The frame; must not be NULL. Must not be used afterwards.
 */
static inline void GameFrame_Destroy(GameFrame* self)
{
    self->VTable->Destroy(self);
}
