#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UIButton.h
 * @brief The button element: a click, Enter or Space; an enabled flag; an optional toggled look.
 *
 * Calling code keeps a widget id and uses the UIButton_* functions, which resolve the id and the capability: a stale id is
 * refused, never a crash, and any widget type embedding the capability answers. A click posts UIElementEvent_Clicked.
 *
 * Disabled is the capability's state, not the input flag: an input-disabled widget is transparent to the pointer, and a
 * click on a disabled button must not reach what lies behind it. A disabled button swallows the click and reports nothing.
 *
 * The UIButtonCapability_* helpers are for widget types of their own: embed the capability, call them from the hooks and
 * draw differently. Calling code does not change.
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
} UIButtonOptions;

/**
 * @brief What a button type's constructor gets (UIKit_CreateButton passes it): the kit and the options. Hand it to
 *        UIButtonCapability_Construct.
 */
typedef struct UIButtonArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIButtonOptions* Options;
} UIButtonArgs;

/**
 * @brief A button's capability: its state and its contract. Embedded by a widget type; its fields are read-only outside the
 *        button's module.
 */
typedef struct UIButtonCapabilityStruct
{
    /** @brief The look. */
    UIElementLook _look;
    /** @brief The text. */
    UIElementText _text;
    /** @brief The press behaviour. */
    UIPressable _press;
    /** @brief Whether it takes clicks. */
    bool _isEnabled;
    /** @brief Whether it shows its selected look. */
    bool _isToggled;
} UIButtonCapability;


// Helpers.
/**
 * @brief Default options: enabled, not toggled.
 * @param text The text as UTF-8, or NULL.
 * @returns The options.
 */
static inline UIButtonOptions UIButtonOptions_Create(const unsigned char* text)
{
    return (UIButtonOptions){ .Element = UIElementOptions_Create(), .Text = text, .IsEnabled = true, .IsToggled = false };
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


// Behaviour helpers, for widget types embedding the capability.
/**
 * @brief Constructs the capability from a widget type's constructor.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The constructor's arguments; must not be NULL.
 * @returns Success; otherwise the look's or the text's error, with nothing left to release.
 */
Error UIButtonCapability_Construct(UIButtonCapability* self, Widget* widget, const UIButtonArgs* args);

/**
 * @brief Releases the capability; call it from OnDeconstruct.
 * @param self The capability; must not be NULL.
 * @returns Success, or the first error.
 */
Error UIButtonCapability_Deconstruct(UIButtonCapability* self);

/**
 * @brief Advances the look and refreshes the text; call it from Update.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param time The tick's time.
 * @returns Success, or the text's error.
 */
Error UIButtonCapability_Update(UIButtonCapability* self, Widget* widget, ProgramTime time);

/**
 * @brief Interprets mouse input; call it from OnMouseInput.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIButtonCapability_OnMouseInput(UIButtonCapability* self, Widget* widget, const WidgetMouseInputArgs* args);

/**
 * @brief Interprets keyboard input; call it from OnKeyboardInput.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param outIsHandled [out] Set when the input was used. Must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIButtonCapability_OnKeyboardInput(UIButtonCapability* self, Widget* widget, const WidgetKeyboardInputArgs* args,
    bool* outIsHandled);

/**
 * @brief Gives its preferred size: its text with the theme's padding, at least the theme's minimum; call it from Measure.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param outSize [out] Receives the size in UI pixels. Must not be NULL.
 * @returns Success, or the text's error.
 */
Error UIButtonCapability_Measure(UIButtonCapability* self, Widget* widget, Vector2* outSize);

/**
 * @brief Draws its text, centred in a box (clipped to it when too long); a type of its own may draw it elsewhere.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param context The render context; must not be NULL.
 * @param box The box in surface pixels.
 * @returns Success, or the drawing's error.
 */
Error UIButtonCapability_RenderText(const UIButtonCapability* self, Widget* widget, UIRenderContext* context, Rectangle box);

/**
 * @brief Registers the button's standard widget type. Once per factory (the kit does).
 * @param factory The factory; must not be NULL.
 * @param lookCapabilityId The look's capability id.
 * @param buttonCapabilityId The button's capability id.
 * @param outTypeId [out] Receives the type's id. Must not be NULL.
 * @returns Success, or the factory's error.
 */
Error UIButton_RegisterType(UIWidgetFactory* factory, uint64_t lookCapabilityId, uint64_t buttonCapabilityId, uint64_t* outTypeId);
