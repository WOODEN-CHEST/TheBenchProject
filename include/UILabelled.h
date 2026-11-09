#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UILabelled.h
 * @brief Two compound elements for rows of settings: the labelled element (kind labelled), a label and any element beside it or above it;
 *        and the form (kind form), a column whose labelled rows share their widest label's width, so the rows line up.
 *
 * LABELLED: UIKit_CreateLabelled makes it with its label (a label element of the kit, styled by the theme's label kind); calling code then
 * adds the element it labels to it as to any parent (a dropdown, a toggle, a text box, a property box). One labelled element serves every
 * kind: the element keeps its own id and answers its own functions (UIDropdown_*, UIValue_*), and the labelled one only places the two:
 * across (the label at the left, vertically centred, the element filling the rest of the row's height and width) or stacked (the label on
 * top, the element below it, as wide as the row). Hover text set on it shows over its label and over an element with none of its own.
 *
 * FORM: a column of rows (any elements; the labelled ones across line up): each row as tall as it measures, as wide as the form, the theme's
 * spacing between them and padding around. The labels' column is as wide as the widest label of its labelled rows that have no width of
 * their own.
 *
 * Their looks (UIKit_RegisterLook: structs starting with UILabelledLookBase and UIFormLookBase) draw what they like and tell the behaviour
 * the theme's spacing and padding (UILabelledCapability_SetSpacing, UIFormCapability_SetMetrics) from their Update; the standard looks draw
 * nothing of their own.
 */


// Types.
/**
 * @brief Where a labelled element's label sits.
 */
typedef enum UILabelledLayoutEnum
{
    /** @brief At the left of the element, vertically centred. */
    UILabelledLayout_Across,
    /** @brief Above the element. */
    UILabelledLayout_Stacked
} UILabelledLayout;

/**
 * @brief What a labelled element is created with (UIKit_CreateLabelled). A plain value; build one with UILabelledOptions_Create.
 */
typedef struct UILabelledOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief The label's text, UTF-8, or NULL for none. Copied. */
    const unsigned char* Text;
    /** @brief Where the label sits. */
    UILabelledLayout Layout;
    /** @brief Whether the label's column is LabelWidth wide, rather than as wide as the label or a form's shared width. */
    bool HasLabelWidth;
    /** @brief The label column's width, in UI pixels, when HasLabelWidth; finite and >= 0. */
    float LabelWidth;
} UILabelledOptions;

/**
 * @brief What a labelled look's Construct gets: the kit and the options.
 */
typedef struct UILabelledArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UILabelledOptions* Options;
} UILabelledArgs;

/**
 * @brief A labelled element's capability: its label's id, where the label sits and how wide its column is. Read-only outside its module.
 */
typedef struct UILabelledCapabilityStruct
{
    /** @brief The label made with it; WIDGET_ID_INVALID until the kit made it. */
    uint64_t _labelId;
    /** @brief Where the label sits. */
    UILabelledLayout _layout;
    /** @brief Whether the label's column has a width of its own, and the width. */
    bool _hasLabelWidth;
    float _labelWidth;
    /** @brief Whether a form gave it the shared width of its labels, and the width (given at each of the form's layouts). */
    bool _hasSharedLabelWidth;
    float _sharedLabelWidth;
    /** @brief Between the label and the element, in its own UI pixels, as its look last said. */
    float _spacing;
} UILabelledCapability;

/**
 * @brief How every labelled look's widget struct starts: the widget, then the capability.
 */
typedef struct UILabelledLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The labelled element's behaviour. */
    UILabelledCapability Labelled;
} UILabelledLookBase;

/**
 * @brief What a form is created with (UIKit_CreateForm). A plain value; build one with UIFormOptions_Create.
 */
typedef struct UIFormOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
} UIFormOptions;

/**
 * @brief What a form look's Construct gets: the kit and the options.
 */
typedef struct UIFormArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIFormOptions* Options;
} UIFormArgs;

/**
 * @brief A form's capability: its metrics as its look last said, and the label width it shared. Read-only outside its module.
 */
typedef struct UIFormCapabilityStruct
{
    /** @brief Around its rows, in its own UI pixels. */
    Vector2 _padding;
    /** @brief Between its rows, in its own UI pixels. */
    float _spacing;
    /** @brief The width its labelled rows shared at its last layout, in UI pixels. */
    float _labelWidth;
} UIFormCapability;

/**
 * @brief How every form look's widget struct starts: the widget, then the capability.
 */
typedef struct UIFormLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The form's behaviour. */
    UIFormCapability Form;
} UIFormLookBase;


// Helpers.
/**
 * @brief Default options: no text, the label across, its column as wide as it (or a form's).
 * @param text The label's text, or NULL; read when the element is made.
 * @returns The options.
 */
static inline UILabelledOptions UILabelledOptions_Create(const unsigned char* text)
{
    return (UILabelledOptions){ .Element = UIElementOptions_Create(), .Text = text, .Layout = UILabelledLayout_Across, .HasLabelWidth = false,
        .LabelWidth = 0.0f };
}

/**
 * @brief Default options for a form.
 * @returns The options.
 */
static inline UIFormOptions UIFormOptions_Create(void)
{
    return (UIFormOptions){ .Element = UIElementOptions_Create() };
}


// Calling code: by widget id.
/*
 * Each takes the element's screen (not NULL) and its widget id, and returns ErrorCode_InvalidOperation for a stale id or a widget of
 * another kind.
 */
/** @brief Replaces a labelled element's label text (UTF-8; NULL for none); ErrorCode_InvalidTextEncoding for text that is not valid UTF-8. */
Error UILabelled_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text);
/** @brief Reads the id of a labelled element's label into @p outLabelId (not NULL). */
Error UILabelled_GetLabelId(UIScreen* screen, uint64_t widgetId, uint64_t* outLabelId);
/** @brief Reads the id of the element a labelled element labels (its first child other than its label) into @p outElementId (not NULL);
 *  WIDGET_ID_INVALID while it labels nothing. */
Error UILabelled_GetElementId(UIScreen* screen, uint64_t widgetId, uint64_t* outElementId);
/** @brief Sets where a labelled element's label sits; ErrorCode_ArgumentOutOfRange for a value that is no layout. */
Error UILabelled_SetLayout(UIScreen* screen, uint64_t widgetId, UILabelledLayout layout);
/** @brief Gives a labelled element's label column a width of its own (UI pixels, finite and >= 0), or takes it away; ErrorCode_ArgumentOutOfRange
 *  for a width that is not one. */
Error UILabelled_SetLabelWidth(UIScreen* screen, uint64_t widgetId, bool hasLabelWidth, float width);


// Behaviour helpers, for looks and the kit.
/**
 * @brief Tells a labelled element the theme's spacing; call it from the look's Update.
 * @param self The capability; must not be NULL.
 * @param widget Its widget (re-measured when the spacing changes); must not be NULL.
 * @param spacing Between the label and the element, in its own UI pixels; one that is not a finite number >= 0 counts as 0.
 */
void UILabelledCapability_SetSpacing(UILabelledCapability* self, Widget* widget, float spacing);

/**
 * @brief Lays a labelled element's label and element out in its box; for a look's Layout and the kind's own.
 * @param widget The labelled element's widget; must not be NULL.
 * @param context The layout's context; must not be NULL.
 * @returns Success, or a child's Measure or placement error.
 */
Error UILabelled_Arrange(Widget* widget, const UILayoutContext* context);

/**
 * @brief Measures a labelled element as UILabelled_Arrange lays it out; for a look's Measure.
 * @param widget The labelled element's widget; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param availableWidth The width it may take, in its own UI pixels (UI_MEASURE_UNBOUNDED_WIDTH for any).
 * @param outSize [out] Receives the size in its own UI pixels; must not be NULL.
 * @returns Success, or a child's Measure error.
 */
Error UILabelled_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize);

/**
 * @brief Records a labelled element's label, made by the kit right after it. Called by UIKit_CreateLabelled.
 * @param self The capability; must not be NULL.
 * @param labelId The label.
 */
void UILabelled_SetLabel(UILabelledCapability* self, uint64_t labelId);

/**
 * @brief Tells a form the theme's padding and spacing; call it from the look's Update.
 * @param self The capability; must not be NULL.
 * @param widget Its widget (re-measured when they change); must not be NULL.
 * @param padding Around its rows, in its own UI pixels; components that are not finite numbers >= 0 count as 0.
 * @param spacing Between its rows, in its own UI pixels; one that is not a finite number >= 0 counts as 0.
 */
void UIFormCapability_SetMetrics(UIFormCapability* self, Widget* widget, Vector2 padding, float spacing);

/**
 * @brief Lays a form's rows out, giving its labelled rows across their shared label width; for a look's Layout and the kind's own.
 * @param widget The form's widget; must not be NULL.
 * @param context The layout's context; must not be NULL.
 * @returns Success, or a row's Measure or placement error.
 */
Error UIForm_Arrange(Widget* widget, const UILayoutContext* context);

/**
 * @brief Measures a form as UIForm_Arrange lays it out; for a look's Measure.
 * @param widget The form's widget; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param availableWidth The width it may take, in its own UI pixels (UI_MEASURE_UNBOUNDED_WIDTH for any).
 * @param outSize [out] Receives the size in its own UI pixels; must not be NULL.
 * @returns Success, or a row's Measure error.
 */
Error UIForm_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize);
