#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UIToggle.h
 * @brief The toggle element: a checkbox that is on, off or mixed (for a multiple selection whose values differ).
 *
 * A click by its click mode, Enter or Space. Off and on swap; a click from mixed turns it on. It posts UIElementEvent_Changed and
 * then UIElementEvent_Committed, each with the bool it is now, and the events every element shares (UIElement_PostMouseEvents). Calling
 * code uses the UIToggle_* functions by widget id; widget types of their own embed the capability and call the UIToggleCapability_*
 * helpers.
 */


// Types.
/**
 * @brief A toggle's state.
 */
typedef enum UIToggleStateEnum
{
    /** @brief Off. */
    UIToggleState_Off,
    /** @brief On. */
    UIToggleState_On,
    /** @brief Neither: shown, and only code sets it. */
    UIToggleState_Mixed
} UIToggleState;

/**
 * @brief What a toggle is created with (UIKit_CreateToggle). A plain value; build one with UIToggleOptions_Create.
 */
typedef struct UIToggleOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief Its text as UTF-8, or NULL for none. Copied. */
    const unsigned char* Text;
    /** @brief Its state. */
    UIToggleState State;
    /** @brief Whether it takes clicks. */
    bool IsEnabled;
    /** @brief When it activates. */
    UIClickMode ClickMode;
    /** @brief The buttons that activate it (UI_BUTTON_BIT values ORed together). */
    uint8_t ActivationButtons;
} UIToggleOptions;

/**
 * @brief What a toggle type's constructor gets (UIKit_CreateToggle passes it): the kit and the options.
 */
typedef struct UIToggleArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIToggleOptions* Options;
} UIToggleArgs;

/**
 * @brief A toggle's capability. Embedded by a widget type; its fields are read-only outside the toggle's module.
 */
typedef struct UIToggleCapabilityStruct
{
    /** @brief The look. */
    UIElementLook _look;
    /** @brief The text. */
    UIElementText _text;
    /** @brief The press behaviour. */
    UIPressable _press;
    /** @brief Its state. */
    UIToggleState _state;
    /** @brief Whether it takes clicks. */
    bool _isEnabled;
} UIToggleCapability;


// Helpers.
/**
 * @brief Default options: off, enabled, activating by the left button on a press and a release on it.
 * @param text The text as UTF-8, or NULL.
 * @returns The options.
 */
static inline UIToggleOptions UIToggleOptions_Create(const unsigned char* text)
{
    return (UIToggleOptions){ .Element = UIElementOptions_Create(), .Text = text, .State = UIToggleState_Off, .IsEnabled = true,
        .ClickMode = UIClickMode_OnPressAndRelease, .ActivationButtons = UI_BUTTONS_LEFT };
}


// Calling code: by widget id.
/**
 * @brief Sets a toggle's state. Posts no event: only the player's change does.
 * @param screen The toggle's screen; must not be NULL.
 * @param widgetId The toggle.
 * @param state The state.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no toggle;
 *          ErrorCode_ArgumentOutOfRange for an unknown state.
 */
Error UIToggle_SetState(UIScreen* screen, uint64_t widgetId, UIToggleState state);

/**
 * @brief Reads a toggle's state.
 * @param screen The toggle's screen; must not be NULL.
 * @param widgetId The toggle.
 * @param outState [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no toggle.
 */
Error UIToggle_GetState(UIScreen* screen, uint64_t widgetId, UIToggleState* outState);

/**
 * @brief Changes a toggle's text.
 * @param screen The toggle's screen; must not be NULL.
 * @param widgetId The toggle.
 * @param text The text as UTF-8; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no toggle; otherwise the text's error.
 */
Error UIToggle_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text);

/**
 * @brief Enables or disables a toggle. Disabling it ends a press in progress and takes its focus away; a click on a
 *        disabled toggle is swallowed and reports nothing.
 * @param screen The toggle's screen; must not be NULL.
 * @param widgetId The toggle.
 * @param isEnabled Whether it takes clicks.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no toggle; otherwise a focus hook's error.
 */
Error UIToggle_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled);

/**
 * @brief Sets when a toggle activates.
 * @param screen The toggle's screen; must not be NULL.
 * @param widgetId The toggle.
 * @param clickMode The click mode.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no toggle; ErrorCode_ArgumentOutOfRange for an
 *          unknown mode.
 */
Error UIToggle_SetClickMode(UIScreen* screen, uint64_t widgetId, UIClickMode clickMode);

/**
 * @brief Sets the mouse buttons that activate a toggle.
 * @param screen The toggle's screen; must not be NULL.
 * @param widgetId The toggle.
 * @param activationButtons UI_BUTTON_BIT values ORed together; none leaves the keyboard alone to activate it.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no toggle; ErrorCode_ArgumentOutOfRange for a bit
 *          that is no mouse button.
 */
Error UIToggle_SetActivationButtons(UIScreen* screen, uint64_t widgetId, uint8_t activationButtons);


// Behaviour helpers, for widget types embedding the capability.
/**
 * @brief Constructs the capability from a widget type's constructor.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The constructor's arguments; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an unknown state or click mode, or a bit that is no mouse button; otherwise the
 *          look's or the text's error, with nothing left to release.
 */
Error UIToggleCapability_Construct(UIToggleCapability* self, Widget* widget, const UIToggleArgs* args);

/**
 * @brief Releases the capability; call it from OnDeconstruct.
 * @param self The capability; must not be NULL.
 * @returns Success, or the first error.
 */
Error UIToggleCapability_Deconstruct(UIToggleCapability* self);

/**
 * @brief Advances the look and refreshes the text; call it from Update.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param time The tick's time.
 * @returns Success, or the text's error.
 */
Error UIToggleCapability_Update(UIToggleCapability* self, Widget* widget, ProgramTime time);

/**
 * @brief Interprets mouse input; call it from OnMouseInput.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIToggleCapability_OnMouseInput(UIToggleCapability* self, Widget* widget, const WidgetMouseInputArgs* args);

/**
 * @brief Lets go of a press the screen says is lost; call it from OnPressLost.
 * @param self The capability; must not be NULL.
 * @param button The press's mouse button.
 */
void UIToggleCapability_OnPressLost(UIToggleCapability* self, GameKey button);

/**
 * @brief Interprets keyboard input; call it from OnKeyboardInput.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param outIsHandled [out] Set when the input was used. Must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIToggleCapability_OnKeyboardInput(UIToggleCapability* self, Widget* widget, const WidgetKeyboardInputArgs* args,
    bool* outIsHandled);

/**
 * @brief Gives its preferred size: the box, the spacing and the text, at least the theme's minimum; call it from Measure.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param outSize [out] Receives the size in UI pixels. Must not be NULL.
 * @returns Success, or the text's error.
 */
Error UIToggleCapability_Measure(UIToggleCapability* self, Widget* widget, Vector2* outSize);

/**
 * @brief Registers the toggle's standard widget type. Once per factory (the kit does).
 * @param factory The factory; must not be NULL.
 * @param lookCapabilityId The look's capability id.
 * @param toggleCapabilityId The toggle's capability id.
 * @param outTypeId [out] Receives the type's id. Must not be NULL.
 * @returns Success, or the factory's error.
 */
Error UIToggle_RegisterType(UIWidgetFactory* factory, uint64_t lookCapabilityId, uint64_t toggleCapabilityId, uint64_t* outTypeId);
