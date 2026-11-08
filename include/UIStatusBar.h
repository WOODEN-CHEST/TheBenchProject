#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UIStatusBar.h
 * @brief The status bar element: a strip with left, centre and right sections holding any elements, and a line of text: a message that
 *        times out, else the hover text where the pointer rests (UITooltip.h) when it is asked to show it.
 *
 * The kit makes the bar with its three sections, each a row stack (UIKit_CreateStatusBar; UIStatusBar_GetSection gives their ids for
 * calling code to add to): the left and right ones as wide as what they hold, at its ends, the centre one filling what is between. The
 * text shows at the start of the centre section, cut where the right one begins. For now the bar is placed like any element; a workspace
 * reserves its strip later.
 *
 * Its look (UIKit_RegisterLook: a struct starting with UIStatusBarLookBase) draws its background and the text
 * (UIStatusBarCapability_GetShownText) and lays its sections out (UIStatusBar_ArrangeSections).
 */


// Types.
/**
 * @brief A status bar's sections.
 */
typedef enum UIStatusBarSectionEnum
{
    /** @brief At its left end, as wide as what it holds. */
    UIStatusBarSection_Left,
    /** @brief Between the two, filling what they leave; the text shows at its start. */
    UIStatusBarSection_Centre,
    /** @brief At its right end, as wide as what it holds. */
    UIStatusBarSection_Right,
    /** @brief The number of sections. Not a section. */
    UIStatusBarSection_Count
} UIStatusBarSection;

/**
 * @brief What a status bar is created with (UIKit_CreateStatusBar). A plain value; build one with UIStatusBarOptions_Create.
 */
typedef struct UIStatusBarOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief Whether it shows the hover text where the pointer rests while no message shows. */
    bool ShowsHoverText;
} UIStatusBarOptions;

/**
 * @brief What a status bar look's Construct gets (UIKit_CreateStatusBar passes it): the kit and the options.
 */
typedef struct UIStatusBarArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIStatusBarOptions* Options;
} UIStatusBarArgs;

/**
 * @brief A status bar's capability: its sections, its message and what it shows. Embedded by a look's widget type right after its Widget
 *        (UIStatusBarLookBase); its fields are read-only outside the bar's module.
 */
typedef struct UIStatusBarCapabilityStruct
{
    /** @brief Its sections' stacks by UIStatusBarSection, made with it; WIDGET_ID_INVALID until then. */
    uint64_t _sectionIds[UIStatusBarSection_Count];
    /** @brief The message, while _hasMessage. */
    UITextContent _message;
    /** @brief Whether a message shows. */
    bool _hasMessage;
    /** @brief Whether it times out. */
    bool _isMessageTimed;
    /** @brief How long it still shows, in seconds, when timed. */
    double _messageSeconds;
    /** @brief Whether the hover text shows while no message does. */
    bool _showsHoverText;
    /** @brief What shows: the message, the hover text or nothing; kept current by its update. */
    UITextContent _shown;
} UIStatusBarCapability;

/**
 * @brief How every status bar look's widget struct starts: the widget, then the capability.
 */
typedef struct UIStatusBarLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The bar's behaviour. */
    UIStatusBarCapability StatusBar;
} UIStatusBarLookBase;


// Helpers.
/**
 * @brief Default options: it shows the hover text.
 * @returns The options.
 */
static inline UIStatusBarOptions UIStatusBarOptions_Create(void)
{
    return (UIStatusBarOptions){ .Element = UIElementOptions_Create(), .ShowsHoverText = true };
}


// Calling code: by widget id.
/**
 * @brief Shows a message, in place of one it showed, for a while or until it is replaced or cleared.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param text The message as UTF-8; must not be NULL. Copied.
 * @param seconds How long it shows; finite and >= 0, 0 for until it is replaced or cleared.
 * @returns Success; ErrorCode_IllegalArgument for a NULL text; ErrorCode_InvalidOperation for a stale id or a widget that is no status bar;
 *          ErrorCode_ArgumentOutOfRange for a time that is not finite or negative; ErrorCode_InvalidTextEncoding for text that is not valid
 *          UTF-8.
 */
Error UIStatusBar_SetMessage(UIScreen* screen, uint64_t widgetId, const unsigned char* text, double seconds);

/**
 * @brief Takes the message away.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no status bar.
 */
Error UIStatusBar_ClearMessage(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Sets whether a bar shows the hover text while no message does.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param showsHoverText Whether it does.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no status bar.
 */
Error UIStatusBar_SetShowsHoverText(UIScreen* screen, uint64_t widgetId, bool showsHoverText);

/**
 * @brief Gives a section's stack, for calling code to add elements to.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param section The section.
 * @param outStackId [out] Receives its stack's id. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no status bar; ErrorCode_ArgumentOutOfRange for an
 *          unknown section.
 */
Error UIStatusBar_GetSection(UIScreen* screen, uint64_t widgetId, UIStatusBarSection section, uint64_t* outStackId);


// Helpers, for looks and the kit.
/**
 * @brief Returns what the bar shows: the message, else the hover text where the pointer rests (when it shows it), else nothing; kept
 *        current by the behaviour's update.
 * @param self The capability; must not be NULL.
 * @returns The text content.
 */
static inline const UITextContent* UIStatusBarCapability_GetShownText(const UIStatusBarCapability* self)
{
    return &self->_shown;
}

/**
 * @brief Lays a bar's sections out inside a padding with a spacing between them: the left and right ones as wide as they measure, the
 *        centre one what is left; for a look's Layout.
 * @param widget The bar's widget; must not be NULL.
 * @param context The layout's context; must not be NULL.
 * @param padding Inside the bar, in its own UI pixels.
 * @param spacing Between the sections, in its own UI pixels.
 * @returns Success, or a section's Measure or placement error.
 */
Error UIStatusBar_ArrangeSections(Widget* widget, const UILayoutContext* context, Vector2 padding, float spacing);

/**
 * @brief Where the text shows, as a share of the bar's box: from the end of the left section to the start of the right one, as last laid
 *        out; for a look's Render.
 * @param widget The bar's widget; must not be NULL.
 * @param outStart [out] Receives the start, in [0;1] of the bar's width. Must not be NULL.
 * @param outEnd [out] Receives the end. Must not be NULL.
 */
void UIStatusBar_GetTextSpan(const Widget* widget, float* outStart, float* outEnd);

/**
 * @brief Records the bar's sections, made by the kit right after the bar. Called by UIKit_CreateStatusBar.
 * @param self The capability; must not be NULL.
 * @param sectionIds The stacks by UIStatusBarSection; must not be NULL.
 */
void UIStatusBar_SetSections(UIStatusBarCapability* self, const uint64_t sectionIds[UIStatusBarSection_Count]);
