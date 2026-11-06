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
 * A plain value type: construct it in place, one per hosted screen; it owns nothing.
 */


// Types.
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
 * @brief Deconstructs a host (it owns nothing).
 * @param self The host, or NULL.
 */
void UIWindowHost_Deconstruct(UIWindowHost* self);

/**
 * @brief One tick of the screen's UI: the viewport and the scale, then UIScreen_Update with the window's pointer and the keyboard.
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
