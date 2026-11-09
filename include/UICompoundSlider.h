#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIElement.h"
#include "UISlider.h"
#include "UIValue.h"
#include "UIWidget.h"


/**
 * @file UICompoundSlider.h
 * @brief The compound slider (kind compound_slider): a name, a slider and a property box showing and taking the same value, placed by the
 *        slider's orientation: across, in a row (the name, the slider filling, the box); upright, in a column (the name, the slider
 *        filling, the box below). Options choose which parts show.
 *
 * It answers the value piece (UIValue.h) as its parts do: UIValue_Set on it moves both parts, and setting either part (by its id, or by the
 * pointer and the keys) moves the other and the compound's own. Its parts' value events are posted under the compound's id, Changed,
 * Committed and Cancelled, one of each per change, and none under the parts' own ids. It scales as one (its parts inside its scale), and its
 * parts' hover text falls back to its own. Its parts are a label, a slider and a property box of the kit, made with it by
 * UIKit_CreateCompoundSlider, each of the look the theme names for its kind, and reachable by id (UICompoundSlider_GetPartIds) for what
 * only they have (UISlider_SetOrientation is the compound's own, UICompoundSlider_SetOrientation).
 *
 * Its look (UIKit_RegisterLook: a struct starting with UICompoundSliderLookBase) draws what it likes behind its parts and tells the
 * behaviour the theme's spacing and box width (UICompoundSliderCapability_SetMetrics); the standard look draws nothing of its own.
 */


// Types.
/**
 * @brief What a compound slider is created with (UIKit_CreateCompoundSlider). A plain value; build one with UICompoundSliderOptions_Create.
 */
typedef struct UICompoundSliderOptionsStruct
{
    /** @brief What every element is created with (the compound's own). */
    UIElementOptions Element;
    /** @brief The value, its settings (with both ends of a drag range) and its states, the parts' too. */
    UIValueOptions Value;
    /** @brief Which way the slider runs, and so where the parts sit. */
    UISliderOrientation Orientation;
    /** @brief The name, UTF-8, or NULL for none. Copied. */
    const unsigned char* Name;
    /** @brief Whether the name shows. */
    bool ShowsName;
    /** @brief Whether the slider shows. */
    bool ShowsSlider;
    /** @brief Whether the property box shows. */
    bool ShowsBox;
    /** @brief The box's digits after the point (UIPropertyBox.h). */
    int32_t Precision;
    /** @brief The box's unit, UTF-8, or NULL for none. Copied. */
    const unsigned char* Unit;
} UICompoundSliderOptions;

/**
 * @brief What a compound slider look's Construct gets: the kit and the options.
 */
typedef struct UICompoundSliderArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UICompoundSliderOptions* Options;
} UICompoundSliderArgs;

/**
 * @brief A compound slider's capability: the value its parts share, its parts' ids and what shows. Read-only outside its module (the value
 *        piece's outside the value's: calling code reaches it by UIValue_*).
 */
typedef struct UICompoundSliderCapabilityStruct
{
    /** @brief The value its parts share: what UIValue_* reach on it. */
    UIValueCapability Value;
    /** @brief Which way the slider runs. */
    UISliderOrientation _orientation;
    /** @brief Its parts, made by the kit; WIDGET_ID_INVALID until then. */
    uint64_t _nameId;
    uint64_t _sliderId;
    uint64_t _boxId;
    /** @brief Which parts show. */
    bool _showsName;
    bool _showsSlider;
    bool _showsBox;
    /** @brief The revision of its value's settings its parts last took. */
    uint64_t _pushedSettingsRevision;
    /** @brief Between its parts, and how wide the box is beside a slider across, in its own UI pixels, as its look last said. */
    float _spacing;
    float _boxWidth;
} UICompoundSliderCapability;

/**
 * @brief How every compound slider look's widget struct starts: the widget, then the capability.
 */
typedef struct UICompoundSliderLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The compound's behaviour. */
    UICompoundSliderCapability CompoundSlider;
} UICompoundSliderLookBase;


// Helpers.
/**
 * @brief Default options: across, the range [0, 1] at 0, every part showing, no name, the box's default precision, no unit.
 * @returns The options.
 */
UICompoundSliderOptions UICompoundSliderOptions_Create(void);


// Calling code: by widget id (the value through UIValue_*).
/*
 * Each takes the compound's screen (not NULL) and its widget id, and returns ErrorCode_InvalidOperation for a stale id or a widget that is no
 * compound slider.
 */
/** @brief Reads the ids of its parts (each pointer may be NULL to skip it). */
Error UICompoundSlider_GetPartIds(UIScreen* screen, uint64_t widgetId, uint64_t* outNameId, uint64_t* outSliderId, uint64_t* outBoxId);
/** @brief Chooses which parts show. */
Error UICompoundSlider_SetShows(UIScreen* screen, uint64_t widgetId, bool showsName, bool showsSlider, bool showsBox);
/** @brief Turns the slider across or upright, and the parts with it; ErrorCode_ArgumentOutOfRange for a value that is no orientation. */
Error UICompoundSlider_SetOrientation(UIScreen* screen, uint64_t widgetId, UISliderOrientation orientation);
/** @brief Replaces the name (UTF-8; NULL for none); ErrorCode_InvalidTextEncoding for text that is not valid UTF-8. */
Error UICompoundSlider_SetName(UIScreen* screen, uint64_t widgetId, const unsigned char* name);


// Behaviour helpers, for looks and the kit.
/**
 * @brief Tells the compound the theme's spacing and box width; call it from the look's Update.
 * @param self The capability; must not be NULL.
 * @param widget Its widget (re-measured when they change); must not be NULL.
 * @param spacing Between its parts, in its own UI pixels; one that is not a finite number >= 0 counts as 0.
 * @param boxWidth The box's width beside a slider across, in its own UI pixels; the same.
 */
void UICompoundSliderCapability_SetMetrics(UICompoundSliderCapability* self, Widget* widget, float spacing, float boxWidth);

/**
 * @brief Lays the parts out by the orientation; for a look's Layout and the kind's own.
 * @param widget The compound's widget; must not be NULL.
 * @param context The layout's context; must not be NULL.
 * @returns Success, or a part's Measure or placement error.
 */
Error UICompoundSlider_Arrange(Widget* widget, const UILayoutContext* context);

/**
 * @brief Measures the compound as UICompoundSlider_Arrange lays it out; for a look's Measure.
 * @param widget The compound's widget; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param availableWidth The width it may take, in its own UI pixels.
 * @param outSize [out] Receives the size in its own UI pixels; must not be NULL.
 * @returns Success, or a part's Measure error.
 */
Error UICompoundSlider_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize);

/**
 * @brief Records the parts, made by the kit right after the compound, and binds their value events and settings to it. Called by
 *        UIKit_CreateCompoundSlider.
 * @param screen The screen; must not be NULL.
 * @param widgetId The compound.
 * @param nameId The name's label.
 * @param sliderId The slider.
 * @param boxId The property box.
 * @returns Success; ErrorCode_InvalidOperation for a part that is gone or of another kind; otherwise a part's error.
 */
Error UICompoundSlider_BindParts(UIScreen* screen, uint64_t widgetId, uint64_t nameId, uint64_t sliderId, uint64_t boxId);
