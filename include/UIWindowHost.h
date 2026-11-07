#pragma once
#include "raylib/raylib.h" // Vector2: the fitted area's placement, which UIViewport holds.
#include "wr/WRError.h"
#include "GameInput.h"
#include "ProgramTime.h"
#include "UIScalePolicy.h"
#include "UIScreen.h"


/**
 * @file UIWindowHost.h
 * @brief Hosts a UI (UIScreen.h) that fills the game window: the counterpart of WorldScreenHost for a screen in the world.
 *
 * The frame that owns the UI drives it through a host, once a tick in its Update, before its own game logic (the UI reads first):
 * the host sets the screen's viewport to the window's size and its scale from the policy (UIScalePolicy.h), the window's height and
 * the monitor's, builds the pointer and the keyboard from the tick's GameInput, and runs UIScreen_Update. It reads everything from
 * GameInput, so a test drives it with a sample.
 *
 * It also applies what the screen asks of the cursor, after the update: a hidden-cursor drag (UIScreen_CapturePointer), giving the
 * screen the raw motion and putting the cursor back where it began when the drag ends, and the pointer's shape
 * (UIScreen_GetCursorShape) when it changes. These calls go through a UIWindowHostCursor, which a test or a windowless harness can
 * replace: the standard one calls raylib, and does nothing with no window.
 *
 * A plain value type: construct it in place, one per hosted screen; it owns nothing.
 */


// Types.
/**
 * @brief What a host does to the real cursor, as plain function pointers and a borrowed context handed back to them.
 */
typedef struct UIWindowHostCursorStruct
{
    /** @brief Hides the cursor and gives raw motion (GameInput_CaptureCursor). */
    void (*Capture)(void* context, GameInput* input);
    /** @brief Shows the cursor again at @p position, in window pixels (GameInput_ReleaseCursor). */
    void (*Release)(void* context, GameInput* input, Vector2 position);
    /** @brief Shows the pointer as @p shape. */
    void (*SetShape)(void* context, UICursorShape shape);
    /** @brief Handed back to the functions; borrowed, may be NULL. */
    void* Context;
} UIWindowHostCursor;

/** @brief The host of one full-window screen. Fields are read-only (underscore-prefixed): use the accessors. */
typedef struct UIWindowHostStruct
{
    /** @brief How the screen's scale follows the window. */
    UIScalePolicy _policy;
    /** @brief The player's scale: on top of the policies that have one. */
    float _playerScale;
    /** @brief The fitted area's settings the screen's viewport carries. */
    float _targetAspectRatio;
    Vector2 _targetAreaPosition;
    /** @brief What it does to the real cursor. */
    UIWindowHostCursor _cursor;
    /** @brief The input it captured the cursor through, while it has it; borrowed. */
    GameInput* _capturedInput;
    /** @brief Where the cursor was when it captured it, in window pixels. */
    Vector2 _captureStart;
    /** @brief The shape it last applied. */
    UICursorShape _shownShape;
} UIWindowHost;


// Functions.
/**
 * @brief Constructs a host.
 * @param self The host; must not be NULL.
 * @param policy How the screen's scale follows the window.
 * @param playerScale The player's scale (GameConfig.UIScale).
 * @param targetAspectRatio What fitted units mean on this screen (GameFrameManager_GetTargetAspectRatio).
 * @param targetAreaPosition Where the fitted area lies (GameFrameManager_GetTargetAreaPosition).
 */
void UIWindowHost_Construct(UIWindowHost* self, UIScalePolicy policy, float playerScale, float targetAspectRatio,
    Vector2 targetAreaPosition);

/**
 * @brief Deconstructs a host: it owns nothing, and it gives back the cursor if a drag still has it, and the normal shape.
 * @param self The host, or NULL.
 */
void UIWindowHost_Deconstruct(UIWindowHost* self);

/**
 * @brief Returns what the standard host does to the cursor: raylib's calls, none with no window.
 * @returns The functions.
 */
UIWindowHostCursor UIWindowHostCursor_CreateStandard(void);

/**
 * @brief Replaces what the host does to the cursor (a test's or a harness's).
 * @param self The host; must not be NULL.
 * @param cursor The functions; none may be NULL. Copied.
 */
void UIWindowHost_SetCursor(UIWindowHost* self, const UIWindowHostCursor* cursor);

/**
 * @brief One tick of the screen's UI: the viewport and the scale, then UIScreen_Update with the window's pointer and the keyboard,
 *        then what the screen asks of the cursor.
 *
 * A window of no size leaves the viewport as it was.
 * @param self The host; must not be NULL.
 * @param screen The UI; must not be NULL.
 * @param time The tick's time, for UIScreen_Update.
 * @param input The tick's input (consumption is recorded in it), or NULL: the screen gets no input this tick.
 * @returns Success, or the error of UIScreen_Update (a broken invariant: see AGENTS.md rule 7).
 */
Error UIWindowHost_Update(UIWindowHost* self, UIScreen* screen, ProgramTime time, GameInput* input);

/**
 * @brief Changes the scale policy, from the next update on.
 * @param self The host; must not be NULL.
 * @param policy The policy.
 */
static inline void UIWindowHost_SetPolicy(UIWindowHost* self, UIScalePolicy policy)
{
    self->_policy = policy;
}

/** @brief Returns the scale policy. @param self The host; must not be NULL. @returns The policy. */
static inline UIScalePolicy UIWindowHost_GetPolicy(const UIWindowHost* self)
{
    return self->_policy;
}

/**
 * @brief Changes the player's scale, from the next update on.
 * @param self The host; must not be NULL.
 * @param playerScale The player's scale.
 */
static inline void UIWindowHost_SetPlayerScale(UIWindowHost* self, float playerScale)
{
    self->_playerScale = playerScale;
}

/** @brief Returns the player's scale. @param self The host; must not be NULL. @returns The player's scale. */
static inline float UIWindowHost_GetPlayerScale(const UIWindowHost* self)
{
    return self->_playerScale;
}
