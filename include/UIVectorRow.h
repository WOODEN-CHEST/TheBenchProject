#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIElement.h"
#include "UIValue.h"
#include "UIWidget.h"


/**
 * @file UIVectorRow.h
 * @brief The row of fields (kind vector_row): two to four property boxes in a row, each after an axis tag (X, Y, Z, W), for a vector's
 *        components.
 *
 * Each field is a property box of the kit, made with the row by UIKit_CreateVectorRow and reachable by its own id (UIVectorRow_GetFieldId)
 * for what is its own: UIValue_SetRange, UIValue_SetMixed per axis and the rest. The fields' value events are posted under the row's id,
 * one per field, each carrying its axis as the event's index (UIEvent.HasIndex, UIEvent.Index) and the field's value, so a handler knows
 * which component changed and writes only that one (what several objects with different components need). Tab moves from field to field,
 * typing in each at once (the fields are edit elements side by side). Dragging down across several fields to edit them together is not
 * built.
 *
 * Its look (UIKit_RegisterLook: a struct starting with UIVectorRowLookBase) draws the tags (UIVectorRow_GetTagBox gives where) and tells the
 * behaviour the theme's spacing and tag width (UIVectorRowCapability_SetMetrics).
 */


// Macros.
/** @brief The fewest fields a row has. */
#define UI_VECTOR_ROW_MIN_COUNT ((size_t)2)
/** @brief The most fields a row has. */
#define UI_VECTOR_ROW_MAX_COUNT ((size_t)4)


// Types.
/**
 * @brief What a row of fields is created with (UIKit_CreateVectorRow). A plain value; build one with UIVectorRowOptions_Create.
 */
typedef struct UIVectorRowOptionsStruct
{
    /** @brief What every element is created with (the row's own). */
    UIElementOptions Element;
    /** @brief How many fields, within [UI_VECTOR_ROW_MIN_COUNT, UI_VECTOR_ROW_MAX_COUNT]. */
    size_t Count;
    /** @brief Every field's value settings and states; each field starts at its own of Values. */
    UIValueOptions Value;
    /** @brief Each field's value, by axis; finite. */
    double Values[UI_VECTOR_ROW_MAX_COUNT];
    /** @brief The fields' digits after the point. */
    int32_t Precision;
    /** @brief The fields' unit, UTF-8, or NULL for none. Copied. */
    const unsigned char* Unit;
} UIVectorRowOptions;

/**
 * @brief What a row look's Construct gets: the kit and the options.
 */
typedef struct UIVectorRowArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIVectorRowOptions* Options;
} UIVectorRowArgs;

/**
 * @brief A row of fields' capability: its fields' ids and its metrics as its look last said. Read-only outside its module.
 */
typedef struct UIVectorRowCapabilityStruct
{
    /** @brief How many fields. */
    size_t _count;
    /** @brief The fields by axis, made by the kit; WIDGET_ID_INVALID until then. */
    uint64_t _fieldIds[UI_VECTOR_ROW_MAX_COUNT];
    /** @brief Between one field and the next tag, and an axis tag's width, in its own UI pixels. */
    float _spacing;
    float _tagWidth;
} UIVectorRowCapability;

/**
 * @brief How every row look's widget struct starts: the widget, then the capability.
 */
typedef struct UIVectorRowLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The row's behaviour. */
    UIVectorRowCapability VectorRow;
} UIVectorRowLookBase;


// Helpers.
/**
 * @brief Default options: three fields with no limits at 0, the property box's default precision, no unit.
 * @returns The options.
 */
UIVectorRowOptions UIVectorRowOptions_Create(void);


// Calling code: by widget id.
/*
 * Each takes the row's screen (not NULL), its widget id and an axis (0 for X up to the count less one), and returns
 * ErrorCode_InvalidOperation for a stale id or a widget that is no row, ErrorCode_ArgumentOutOfRange for an axis past its fields.
 */
/** @brief Reads the id of an axis's field into @p outFieldId (not NULL). */
Error UIVectorRow_GetFieldId(UIScreen* screen, uint64_t widgetId, size_t axis, uint64_t* outFieldId);
/** @brief Sets an axis's value by code, as UIValue_Set on its field. */
Error UIVectorRow_SetValue(UIScreen* screen, uint64_t widgetId, size_t axis, double value);
/** @brief Reads an axis's value into @p outValue (not NULL). */
Error UIVectorRow_GetValue(UIScreen* screen, uint64_t widgetId, size_t axis, double* outValue);
/** @brief Sets whether an axis shows as mixed, as UIValue_SetMixed on its field. */
Error UIVectorRow_SetMixed(UIScreen* screen, uint64_t widgetId, size_t axis, bool isMixed);


// Behaviour helpers, for looks and the kit.
/**
 * @brief Tells the row the theme's spacing and tag width; call it from the look's Update.
 * @param self The capability; must not be NULL.
 * @param widget Its widget (re-measured when they change); must not be NULL.
 * @param spacing Between a field and the next tag, in its own UI pixels; one that is not a finite number >= 0 counts as 0.
 * @param tagWidth An axis tag's width, in its own UI pixels; the same.
 */
void UIVectorRowCapability_SetMetrics(UIVectorRowCapability* self, Widget* widget, float spacing, float tagWidth);

/**
 * @brief Lays the fields out, each after the room of its tag, sharing the row's width evenly; for a look's Layout and the kind's own.
 * @param widget The row's widget; must not be NULL.
 * @param context The layout's context; must not be NULL.
 * @returns Success, or a field's placement error.
 */
Error UIVectorRow_Arrange(Widget* widget, const UILayoutContext* context);

/**
 * @brief Measures the row as UIVectorRow_Arrange lays it out; for a look's Measure.
 * @param widget The row's widget; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param availableWidth The width it may take, in its own UI pixels.
 * @param outSize [out] Receives the size in its own UI pixels; must not be NULL.
 * @returns Success, or a field's Measure error.
 */
Error UIVectorRow_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize);

/**
 * @brief Where an axis's tag is, as last laid out: the room before its field, as tall as the field, in the row's own UI pixels.
 * @param widget The row's widget; must not be NULL.
 * @param axis The axis.
 * @param outBox [out] Receives the box, from the row's top-left corner; must not be NULL.
 * @returns false for an axis past the fields or a field that is gone or hidden (the box left alone).
 */
bool UIVectorRow_GetTagBox(const Widget* widget, size_t axis, Rectangle* outBox);

/**
 * @brief Records the fields, made by the kit right after the row, and binds their value events to it with their axes. Called by
 *        UIKit_CreateVectorRow.
 * @param screen The screen; must not be NULL.
 * @param widgetId The row.
 * @param fieldIds The fields by axis, as many as the row has; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a field that is gone or holds no value.
 */
Error UIVectorRow_BindFields(UIScreen* screen, uint64_t widgetId, const uint64_t* fieldIds);
