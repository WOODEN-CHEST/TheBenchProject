#pragma once


/**
 * @file UIScalePolicy.h
 * @brief How a screen's scale (UIScreen_SetScale) follows the window and the monitor, as pure maths.
 *
 * A screen has one of three policies, and its host asks for the scale each tick:
 * - Fixed: the given value, whatever the window does: a screen in the world.
 * - ConstantPixels: controls keep their size when the window changes, and a larger window gives more room. The monitor
 *   sets the scale, so a control takes the same share of any monitor (1080p is 1, 1440p is 1.25, 4K is 2, in quarters and
 *   never below 1), with the player's scale on top: the editor.
 * - FollowHeight: everything grows with the window: the window's height against 1080, times the player's scale: menus and
 *   game screens.
 *
 * Every answer lies within [CONFIG_MIN_UI_SCALE, UI_SCALE_POLICY_MAX_SCALE], so it is always a scale UIScreen_SetScale takes.
 */


// Macros.
/** @brief The height in pixels at which a window or a monitor gives a scale of 1. */
#define UI_SCALE_POLICY_REFERENCE_HEIGHT 1080.0f
/** @brief How many steps per unit the monitor's scale rounds to: in quarters (1440 / 1080 is 1.25, not 1.33), so a UI pixel
 *  stays a simple number of surface pixels. */
#define UI_SCALE_POLICY_MONITOR_STEPS 4.0f
/** @brief The largest scale a policy answers. */
#define UI_SCALE_POLICY_MAX_SCALE 8.0f


// Types.
/** @brief The kinds of scale policy. */
typedef enum UIScalePolicyKindEnum
{
    /** @brief The given value: a screen in the world. */
    UIScalePolicyKind_Fixed,
    /** @brief From the monitor's height, times the player's scale: resizing the window gives more room. */
    UIScalePolicyKind_ConstantPixels,
    /** @brief The window's height against 1080, times the player's scale: resizing the window scales the UI. */
    UIScalePolicyKind_FollowHeight,
} UIScalePolicyKind;

/** @brief A screen's scale policy: a plain value. Build with UIScalePolicy_Fixed, _ConstantPixels or _FollowHeight. */
typedef struct UIScalePolicyStruct
{
    /** @brief Which policy. */
    UIScalePolicyKind Kind;
    /** @brief UIScalePolicyKind_Fixed's value; unused otherwise. */
    float FixedScale;
} UIScalePolicy;


// Functions.
/**
 * @brief Makes the policy of a screen whose scale stays @p scale.
 * @param scale The scale.
 * @returns The policy.
 */
static inline UIScalePolicy UIScalePolicy_Fixed(float scale)
{
    return (UIScalePolicy){ .Kind = UIScalePolicyKind_Fixed, .FixedScale = scale };
}

/** @brief Makes the policy of a screen that keeps its controls' size when the window changes. @returns The policy. */
static inline UIScalePolicy UIScalePolicy_ConstantPixels(void)
{
    return (UIScalePolicy){ .Kind = UIScalePolicyKind_ConstantPixels, .FixedScale = 1.0f };
}

/** @brief Makes the policy of a screen that scales with the window's height. @returns The policy. */
static inline UIScalePolicy UIScalePolicy_FollowHeight(void)
{
    return (UIScalePolicy){ .Kind = UIScalePolicyKind_FollowHeight, .FixedScale = 1.0f };
}

/**
 * @brief The scale a policy answers. Pure.
 *
 * A height that is not a finite number above 0 (a NaN, zero, a negative, an unknown monitor) gives the player's scale
 * alone, and so does a Fixed value that is not one; a player's scale that is not one counts as 1.
 * @param policy The policy; NULL gives the player's scale alone.
 * @param playerScale The player's scale (GameConfig.UIScale): on top of ConstantPixels and FollowHeight.
 * @param windowHeight The window's height in pixels.
 * @param monitorHeight The height in pixels of the monitor the window is on.
 * @returns The scale, in [CONFIG_MIN_UI_SCALE, UI_SCALE_POLICY_MAX_SCALE].
 */
float UIScalePolicy_GetScale(const UIScalePolicy* policy, float playerScale, float windowHeight, float monitorHeight);
