#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "TextDocument.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UILabel.h
 * @brief The label element: LabelWidget in the kit, its text size in UI pixels and its look from the theme.
 *
 * A kit label takes its font, its text size and its colours from the screen's theme, or its text size from its options. It
 * takes no input, so the pointer goes through it to the panel or the button behind. Calling code uses the UILabel_*
 * functions by widget id; the whole of LabelWidget.h stays there for rich text.
 */


// Types.
/**
 * @brief What a label is created with (UIKit_CreateLabel). A plain value; build one with UILabelOptions_Create.
 */
typedef struct UILabelOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief Its text as UTF-8, or NULL for none. Copied. */
    const unsigned char* Text;
    /** @brief Whether TextSize replaces the theme's. */
    bool HasTextSize;
    /** @brief The line height in UI pixels, when HasTextSize; finite and > 0. */
    float TextSize;
    /** @brief Whether long lines wrap at the width it is given. */
    bool IsWrapped;
} UILabelOptions;

/**
 * @brief A label's capability: its look, and where its text lives. Embedded by a widget type; its fields are read-only
 *        outside the label's module.
 */
typedef struct UILabelCapabilityStruct
{
    /** @brief The document that owns its text; borrowed, inside the widget. */
    TextDocument* _document;
    /** @brief Whether _textSize is its line height. */
    bool _hasTextSize;
    /** @brief The line height in UI pixels. */
    float _textSize;
    /** @brief Whether long lines wrap at the width it is given. */
    bool _isWrapped;
} UILabelCapability;


// Helpers.
/**
 * @brief Default options: the theme's text size, no wrapping, centred across in a stack.
 * @param text The text as UTF-8, or NULL.
 * @returns The options.
 */
static inline UILabelOptions UILabelOptions_Create(const unsigned char* text)
{
    UILabelOptions Options =
    {
        .Element = UIElementOptions_Create(),
        .Text = text,
        .HasTextSize = false,
        .TextSize = 0.0f,
        .IsWrapped = false
    };
    Options.Element.Hint.Align = UIStackAlign_Center;
    return Options;
}


// Calling code: by widget id.
/**
 * @brief Replaces a label's text with one plain string.
 * @param screen The label's screen; must not be NULL.
 * @param widgetId The label.
 * @param text The text as UTF-8; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no label; otherwise the text's error, with
 *          the old text kept.
 */
Error UILabel_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text);

/**
 * @brief Sets a label's line height, or gives it back to the theme.
 * @param screen The label's screen; must not be NULL.
 * @param widgetId The label.
 * @param hasTextSize Whether @p textSize replaces the theme's.
 * @param textSize The line height in UI pixels, when @p hasTextSize; finite and > 0.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no label; ErrorCode_ArgumentOutOfRange for a
 *          size that is not valid.
 */
Error UILabel_SetTextSize(UIScreen* screen, uint64_t widgetId, bool hasTextSize, float textSize);
