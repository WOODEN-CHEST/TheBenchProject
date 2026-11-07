#pragma once
#include <stdint.h>
#include "wr/WRError.h"


/**
 * @file UIStandardLooks.h
 * @brief The kit's standard look (UI_KIT_STANDARD_LOOK, tbp:test_box) for the panel, the button, the toggle and the slider: boxes
 *        drawn from the screen's theme, flat or nine-slice, with the theme's metrics, colours per state and fades.
 *
 * Each is a look as any other (UIKit_RegisterLook): a struct starting with its kind's look base, a UIElementLook (so variants work)
 * and what it draws its text with, and hooks for Construct, Update, Render and Measure (the panel's Layout too, for its padding).
 * The kit registers them when it is constructed; they are the example to follow for a look of one's own.
 */


// Forward declarations.
/** @brief The kit; full type in UIKit.h. */
typedef struct UIKitStruct UIKit;
/** @brief The widget factory; full type in UIWidgetFactory.h. */
typedef struct UIWidgetFactoryStruct UIWidgetFactory;


// Functions.
/**
 * @brief Registers the standard look for the panel, the button, the toggle and the slider with a kit (UIKit_Construct does).
 * @param kit The kit; must not be NULL.
 * @returns Success, or the first registration's error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIStandardLooks_Register(UIKit* kit);

/**
 * @brief Registers the widget type that shows hover text (UITooltip.h): a box at the theme's offset from the pointer (either way)
 *        with the text in it, wrapped at the theme's width, from the theme's tooltip kind, kept on the surface: past a side edge it
 *        slides in, past the top or the bottom it goes to the other side of the pointer. A plain widget type made with no arguments;
 *        the kit names it to the factory (UIWidgetFactory_SetTooltipType).
 * @param kit The kit, whose capabilities exist already; must not be NULL.
 * @param factory The kit's factory; must not be NULL.
 * @param outTypeId [out] Receives the type's id. Must not be NULL.
 * @returns Success, or the factory's error.
 */
Error UIStandardLooks_RegisterTooltip(UIKit* kit, UIWidgetFactory* factory, uint64_t* outTypeId);
