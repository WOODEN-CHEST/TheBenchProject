#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UIButton.h
 * @brief The button element: a click by its click mode, Enter or Space; an enabled flag; an optional toggled look.
 *
 * Calling code keeps a widget id and uses the UIButton_* functions, which resolve the id and the capability: a stale id is
 * refused, never a crash, and any widget type embedding the capability answers. It posts UIElementEvent_Activated when its click
 * mode says so, or at a key's press, and the events every element shares (UIElement_PostMouseEvents). Made with TakesWheel it also
 * takes the wheel and posts UIElementEvent_Scrolled; otherwise the wheel goes on up the tree.
 *
 * Disabled is the capability's state, not the input flag: an input-disabled widget is transparent to the pointer, and a
 * click on a disabled button must not reach what lies behind it. A disabled button swallows everything and reports nothing.
 *
 * The capability is behaviour alone: its text and icon are plain content, and its look (UIKit_RegisterLook: a struct starting
 * with UIButtonLookBase) draws them however it likes, reading the state to show with UIButtonCapability_GetState. Calling code
 * does not change for any look.
 */


// Types.
/**
 * @brief What a button is created with (UIKit_CreateButton). A plain value; build one with UIButtonOptions_Create.
 */
typedef struct UIButtonOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief Its text as UTF-8, or NULL for none. Copied. */
    const unsigned char* Text;
    /** @brief Whether it takes clicks. */
    bool IsEnabled;
    /** @brief Whether it shows its selected look. */
    bool IsToggled;
    /** @brief When it activates. */
    UIClickMode ClickMode;
    /** @brief The buttons that activate it (UI_BUTTON_BIT values ORed together). */
    uint8_t ActivationButtons;
    /** @brief Whether it takes the wheel, posting UIElementEvent_Scrolled, in place of letting it go on up the tree. */
    bool TakesWheel;
    /** @brief Its icon, beside the text or in place of it as its look decides; none by default. Copied. */
    UIElementIcon Icon;
} UIButtonOptions;

/**
 * @brief What a button look's Construct gets (UIKit_CreateButton passes it): the kit and the options.
 */
typedef struct UIButtonArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIButtonOptions* Options;
} UIButtonArgs;

/**
 * @brief A button's capability: its logical state and its contract, no look. Embedded by a look's widget type right after its
 *        Widget (UIButtonLookBase); its fields are read-only outside the button's module.
 */
typedef struct UIButtonCapabilityStruct
{
    /** @brief The text, as plain content. */
    UITextContent _text;
    /** @brief The icon. */
    UIElementIcon _icon;
    /** @brief The press behaviour. */
    UIPressable _press;
    /** @brief Whether it takes clicks. */
    bool _isEnabled;
    /** @brief Whether it shows its selected look. */
    bool _isToggled;
    /** @brief Whether it takes the wheel. */
    bool _takesWheel;
} UIButtonCapability;

/**
 * @brief How every button look's widget struct starts: the widget, then the capability. A look's struct embeds this first (or
 *        declares the same two members first), then what it draws with.
 */
typedef struct UIButtonLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The button's behaviour. */
    UIButtonCapability Button;
} UIButtonLookBase;


// Helpers.
/**
 * @brief Default options: enabled, not toggled, activating by the left button on a press and a release on it, not taking the wheel.
 * @param text The text as UTF-8, or NULL.
 * @returns The options.
 */
static inline UIButtonOptions UIButtonOptions_Create(const unsigned char* text)
{
    return (UIButtonOptions){ .Element = UIElementOptions_Create(), .Text = text, .IsEnabled = true, .IsToggled = false,
        .ClickMode = UIClickMode_OnPressAndRelease, .ActivationButtons = UI_BUTTONS_LEFT, .TakesWheel = false,
        .Icon = UIElementIcon_None() };
}


// Calling code: by widget id.
/**
 * @brief Changes a button's text.
 * @param screen The button's screen; must not be NULL.
 * @param widgetId The button.
 * @param text The text as UTF-8; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no button; otherwise the text's error.
 */
Error UIButton_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text);

/**
 * @brief Reads a button's text.
 * @param screen The button's screen; must not be NULL.
 * @param widgetId The button.
 * @param outText [out] Receives the UTF-8 text, valid until it changes. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no button.
 */
Error UIButton_GetText(UIScreen* screen, uint64_t widgetId, const unsigned char** outText);

/**
 * @brief Enables or disables a button. Disabling it ends a press in progress and takes its focus away.
 * @param screen The button's screen; must not be NULL.
 * @param widgetId The button.
 * @param isEnabled Whether it takes clicks.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no button; otherwise a focus hook's error.
 */
Error UIButton_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled);

/**
 * @brief Reads whether a button is enabled.
 * @param screen The button's screen; must not be NULL.
 * @param widgetId The button.
 * @param outIsEnabled [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no button.
 */
Error UIButton_IsEnabled(UIScreen* screen, uint64_t widgetId, bool* outIsEnabled);

/**
 * @brief Sets whether a button shows its selected look.
 * @param screen The button's screen; must not be NULL.
 * @param widgetId The button.
 * @param isToggled Whether it does.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no button.
 */
Error UIButton_SetIsToggled(UIScreen* screen, uint64_t widgetId, bool isToggled);

/**
 * @brief Reads whether a button shows its selected look.
 * @param screen The button's screen; must not be NULL.
 * @param widgetId The button.
 * @param outIsToggled [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no button.
 */
Error UIButton_IsToggled(UIScreen* screen, uint64_t widgetId, bool* outIsToggled);

/**
 * @brief Sets when a button activates.
 * @param screen The button's screen; must not be NULL.
 * @param widgetId The button.
 * @param clickMode The click mode.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no button; ErrorCode_ArgumentOutOfRange for an
 *          unknown mode.
 */
Error UIButton_SetClickMode(UIScreen* screen, uint64_t widgetId, UIClickMode clickMode);

/**
 * @brief Sets the mouse buttons that activate a button.
 * @param screen The button's screen; must not be NULL.
 * @param widgetId The button.
 * @param activationButtons UI_BUTTON_BIT values ORed together; none leaves the keyboard alone to activate it.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no button; ErrorCode_ArgumentOutOfRange for a bit
 *          that is no mouse button.
 */
Error UIButton_SetActivationButtons(UIScreen* screen, uint64_t widgetId, uint8_t activationButtons);

/**
 * @brief Sets a button's icon.
 * @param screen The button's screen; must not be NULL.
 * @param widgetId The button.
 * @param icon The icon; must not be NULL. Copied.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no button; ErrorCode_ArgumentOutOfRange for an
 *          icon that is not valid (UIElementIcon_IsValid).
 */
Error UIButton_SetIcon(UIScreen* screen, uint64_t widgetId, const UIElementIcon* icon);

/**
 * @brief Sets whether a button takes the wheel, posting UIElementEvent_Scrolled, in place of letting it go on up the tree.
 * @param screen The button's screen; must not be NULL.
 * @param widgetId The button.
 * @param takesWheel Whether it does.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no button.
 */
Error UIButton_SetTakesWheel(UIScreen* screen, uint64_t widgetId, bool takesWheel);


// Behaviour helpers, for looks and tests.
/**
 * @brief Constructs the capability from the kind's arguments.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The arguments; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an unknown click mode, a bit that is no mouse button or an icon that is not
 *          valid; otherwise the options' or the text's error, with nothing left to release.
 */
Error UIButtonCapability_Construct(UIButtonCapability* self, Widget* widget, const UIButtonArgs* args);

/**
 * @brief Releases the capability.
 * @param self The capability; must not be NULL.
 */
void UIButtonCapability_Deconstruct(UIButtonCapability* self);

/**
 * @brief The behaviour's part of a tick: lets go of a key or a button whose release cannot reach it any more.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 */
void UIButtonCapability_Update(UIButtonCapability* self, Widget* widget);

/**
 * @brief Returns the state a look shows for the button: disabled, pressed, entered, toggled (selected), hovered, focused or
 *        normal (UIElementState_Choose).
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @returns The state.
 */
UIElementState UIButtonCapability_GetState(const UIButtonCapability* self, const Widget* widget);

/**
 * @brief Interprets mouse input.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIButtonCapability_OnMouseInput(UIButtonCapability* self, Widget* widget, const WidgetMouseInputArgs* args);

/**
 * @brief Lets go of a press the screen says is lost.
 * @param self The capability; must not be NULL.
 * @param button The press's mouse button.
 */
void UIButtonCapability_OnPressLost(UIButtonCapability* self, GameKey button);

/**
 * @brief Interprets the wheel.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param outIsHandled [out] Set when it takes the wheel. Must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIButtonCapability_OnScrollInput(UIButtonCapability* self, Widget* widget, const WidgetScrollInputArgs* args, bool* outIsHandled);

/**
 * @brief Interprets keyboard input.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param outIsHandled [out] Set when the input was used. Must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIButtonCapability_OnKeyboardInput(UIButtonCapability* self, Widget* widget, const WidgetKeyboardInputArgs* args,
    bool* outIsHandled);
