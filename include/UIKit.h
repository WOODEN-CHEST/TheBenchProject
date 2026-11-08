#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "NamespacedKey.h"
#include "StackWidget.h"
#include "UIButton.h"
#include "UIChoice.h"
#include "UIDropdown.h"
#include "UIElement.h"
#include "UILabel.h"
#include "UIMenu.h"
#include "UIMenuBar.h"
#include "UIPanel.h"
#include "UIScrollArea.h"
#include "UIScrollbar.h"
#include "UIScrollable.h"
#include "UISlider.h"
#include "UIStatusBar.h"
#include "UITheme.h"
#include "UIToggle.h"


/**
 * @file UIKit.h
 * @brief The one way calling code makes elements, over one widget factory, and where looks are registered.
 *
 * Calling code creates an element with UIKit_Create* and keeps its widget id, then uses the kind's functions by id
 * (UIButton_SetText(screen, id, text)): it never names a widget type, so looks can be swapped under it.
 *
 * LOOKS. An element is behaviour (its kind's capability) and a look (a widget type that draws). A look costs a struct that starts
 * with its kind's look base (UIButtonLookBase: the Widget, then the capability) and a UILookVTable with Render and Measure, maybe
 * Update; UIKit_RegisterLook composes the widget type from it and the kind's behaviour: the kind answers the mouse, the keys, the
 * wheel, a lost press, entering and leaving, and the look draws. Looks are registered by kind and name (a namespaced key); the kit
 * registers its standard look for every kind under UI_KIT_STANDARD_LOOK (theme-driven boxes, flat or nine-slice). An element is made
 * with the look its options name (UIElementOptions.Look), else the one the screen's theme names for its kind, else the standard one;
 * a name no look answers is logged once and gets the standard look. Looks are fixed at creation: swapping the theme changes the values
 * a look reads, never an element's widget type.
 *
 * ONE KIT PER FACTORY. A second kit over a factory is refused. Teardown order: screens first (they destroy the elements),
 * then the kit, then the factory.
 */


// Forward declarations.
/** @brief The factory of text components; full type in TextComponentFactory.h. Borrowed. */
typedef struct TextComponentFactoryStruct TextComponentFactory;
/** @brief The asset manager; full type in AssetManager.h. Borrowed. */
typedef struct AssetManagerStruct AssetManager;
/** @brief The logger; full type in Logger.h. Borrowed. */
typedef struct LoggerStruct Logger;


// Macros.
/** @brief The name the kit's standard look is registered under for every kind: theme-driven boxes, flat or nine-slice. */
#define UI_KIT_STANDARD_LOOK ((const unsigned char*)u8"tbp:test_box")
/** @brief How many look names that answer no look the kit remembers having logged; past that, no more are logged. */
#define UI_KIT_MAX_REPORTED_LOOKS ((size_t)16)


// Types.
/**
 * @brief The capabilities the kit registers.
 */
typedef enum UIElementCapabilityEnum
{
    /** @brief UIElementLook: every element with a look. */
    UIElementCapability_Look,
    /** @brief UILabelCapability (LabelWidget.h). */
    UIElementCapability_Label,
    /** @brief UIButtonCapability. */
    UIElementCapability_Button,
    /** @brief UIToggleCapability. */
    UIElementCapability_Toggle,
    /** @brief UISliderCapability. */
    UIElementCapability_Slider,
    /** @brief UIScrollableCapability (UIScrollable.h): what scrolls; a scroll area's, a dropdown list's. */
    UIElementCapability_Scrollable,
    /** @brief UIScrollbarCapability (UIScrollbar.h). */
    UIElementCapability_Scrollbar,
    /** @brief UIChoiceCapability (UIChoice.h): a dropdown's options and the selected one. */
    UIElementCapability_Choice,
    /** @brief UIDropdownCapability (UIDropdown.h). */
    UIElementCapability_Dropdown,
    /** @brief UIMenuBarCapability (UIMenuBar.h). */
    UIElementCapability_MenuBar,
    /** @brief UIStatusBarCapability (UIStatusBar.h). */
    UIElementCapability_StatusBar,
    /** @brief The number of capabilities. */
    UIElementCapability_Count
} UIElementCapability;

/**
 * @brief What a look is made of: the hooks it writes. The kit calls them on the look's widget, a struct that starts with its kind's
 *        look base (UIButtonLookBase, ...). Render and Measure are required; the rest are optional.
 *
 * Every hook gets the widget as @c self. A hook returning an error is a broken invariant, as any widget hook's (UIWidget.h).
 */
typedef struct UILookVTableStruct
{
    /** @brief Builds the look's own state (a UIElementLook, a UIElementText): after the kind built its capability, with the same
     *  arguments (UIButtonArgs, ...). On a failure it must release what it acquired. Optional. */
    Error (*Construct)(void* self, void* args);
    /** @brief Releases the look's own state, before the kind releases its capability. Optional. */
    Error (*Deconstruct)(void* self);
    /** @brief The look's part of a tick (state fades, laid-out text, animation), after the behaviour's. Optional. */
    Error (*Update)(void* self, ProgramTime time);
    /** @brief Lays the children out, for a kind that holds them (the panel), in place of the kind's own arrangement. Optional. */
    Error (*Layout)(void* self, const UILayoutContext* context);
    /** @brief Draws; reads state, never writes it (WidgetVTable's Render). Required. */
    Error (*Render)(void* self, UIRenderContext* context);
    /** @brief Gives the preferred size in the widget's own UI pixels (WidgetVTable's Measure). Required. */
    Error (*Measure)(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize);
    /** @brief Answers whether a point holds the widget, in place of its box (WidgetVTable's HitTest): a round button, a world
     *  object standing for a button. Optional. */
    bool (*HitTest)(void* self, const WidgetHitTestArgs* args);
    /** @brief Returns the look's UIElementLook: a look that reads the theme declares the look capability through it, so
     *  UIElement_SetVariant works on it; a look that ignores the theme leaves it out. Optional. */
    UIElementLook* (*GetThemeLook)(void* self);
} UILookVTable;

/**
 * @brief The kit. Construct with UIKit_Construct; its fields are read-only.
 */
typedef struct UIKitStruct
{
    /** @brief The factory it stands over; borrowed. */
    UIWidgetFactory* _factory;
    /** @brief The text components' factory; borrowed. */
    TextComponentFactory* _textFactory;
    /** @brief Loads the elements' fonts; borrowed, may be NULL. */
    AssetManager* _assets;
    /** @brief Reports recoverable problems; borrowed, may be NULL. */
    Logger* _logger;
    /** @brief The capability ids, by UIElementCapability. */
    uint64_t _capabilities[UIElementCapability_Count];
    /** @brief The stack's widget type. */
    uint64_t _stackType;
    /** @brief The kit's own widget types that no look stands for: a dropdown's open list and its rows, a box that cuts its children,
     *  a menu's list and its items. */
    uint64_t _dropdownListType;
    uint64_t _dropdownRowType;
    uint64_t _clipType;
    uint64_t _menuListType;
    uint64_t _menuItemType;
    uint64_t _menuBarEntryType;
    /** @brief The registered looks (a private element type): kind, name, widget type, hooks. Owned. */
    GenericBuffer _looks;
    /** @brief Per factory type id less one, the index + 1 of its look in _looks (uint32_t), 0 for a type that is no look. Owned. */
    GenericBuffer _lookByType;
    /** @brief Look names already logged as answering no look. */
    unsigned char _reportedLooks[UI_KIT_MAX_REPORTED_LOOKS][NAMESPACED_KEY_BUFFER_SIZE];
    /** @brief How many of _reportedLooks are used. */
    size_t _reportedLookCount;
} UIKit;


// Lifecycle.
/**
 * @brief Registers the capabilities and the standard widget types over a factory.
 * @param self The kit; must not be moved afterwards. Must not be NULL.
 * @param factory The factory; borrowed, must outlive the kit. Must not be NULL.
 * @param textFactory The text components' factory; borrowed, must outlive the elements. Must not be NULL.
 * @param assets Loads the elements' fonts; borrowed, may be NULL (raylib's built-in font).
 * @param logger Reports recoverable problems; borrowed, may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL; ErrorCode_InvalidOperation if the factory has a
 *          kit already; otherwise the factory's error.
 */
Error UIKit_Construct(UIKit* self, UIWidgetFactory* factory, TextComponentFactory* textFactory, AssetManager* assets, Logger* logger);

/**
 * @brief Leaves the factory without a kit and releases the kit's tables. Its widget types stay registered until the factory goes,
 *        but no widget of a look's type can be made past this. Refused while a widget of a look's type is alive, since its hooks
 *        are found through the kit's tables: deconstruct the screens first. Safe on NULL.
 * @param self The kit, or NULL.
 * @returns Success; ErrorCode_InvalidOperation while widgets of its looks are alive (the kit is left as it was).
 */
Error UIKit_Deconstruct(UIKit* self);

/**
 * @brief Returns the kit over a screen's factory, which is how the element functions find it.
 * @param screen The screen; must not be NULL.
 * @returns The kit, or NULL for none.
 */
UIKit* UIKit_FromScreen(const UIScreen* screen);

/**
 * @brief Returns a capability's id, for a widget type of its own to declare.
 * @param self The kit; must not be NULL.
 * @param capability The capability.
 * @returns The id; WIDGET_CAPABILITY_ID_INVALID for a value out of range.
 */
uint64_t UIKit_GetCapabilityId(const UIKit* self, UIElementCapability capability);

/**
 * @brief Returns the widget type of a kind's standard look (UI_KIT_STANDARD_LOOK).
 * @param self The kit; must not be NULL.
 * @param kind The kind.
 * @returns The type id; WIDGET_TYPE_ID_INVALID for a value out of range.
 */
uint64_t UIKit_GetElementType(const UIKit* self, UIElementKind kind);

/**
 * @brief Registers a look for a kind: composes a widget type from the kind's behaviour and the look's hooks.
 *
 * The look's struct starts with the kind's look base (UIPanelLookBase, UIButtonLookBase, UIToggleLookBase, UISliderLookBase): the
 * Widget, then the capability, which the kind builds and answers input with. The widget type declares the kind's capability, and the
 * look capability when the look has GetThemeLook. The label's and the tooltip's looks are not replaceable yet.
 * @param self The kit; must not be NULL.
 * @param kind The kind: the panel, the button, the toggle or the slider.
 * @param lookName The look's name, a namespaced key (written without a namespace it is tbp's); must not be NULL. Copied.
 * @param structSize The size of the look's struct; at least its kind's look base.
 * @param look The hooks; must not be NULL, with Render and Measure; static storage (the kit keeps the pointer).
 * @param outTypeId [out] Receives the widget type's id; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL or Render or Measure is missing; ErrorCode_ArgumentOutOfRange for
 *          a kind whose look is not replaceable, a name that is no valid key, or a struct smaller than the kind's look base;
 *          ErrorCode_InvalidOperation if the kind has a look by that name already; otherwise the factory's error.
 */
Error UIKit_RegisterLook(UIKit* self, UIElementKind kind, const unsigned char* lookName, size_t structSize, const UILookVTable* look,
    uint64_t* outTypeId);

/**
 * @brief Finds the widget type of a kind's look by name.
 * @param self The kit; must not be NULL.
 * @param kind The kind.
 * @param lookName The look's name (written without a namespace it is tbp's); must not be NULL.
 * @param outTypeId [out] Receives the type id when found; must not be NULL.
 * @returns true if the kind has a look by that name.
 */
bool UIKit_TryGetLookType(const UIKit* self, UIElementKind kind, const unsigned char* lookName, uint64_t* outTypeId);

/**
 * @brief Returns the text components' factory the elements use.
 * @param self The kit; must not be NULL.
 * @returns The borrowed factory.
 */
static inline TextComponentFactory* UIKit_GetTextFactory(const UIKit* self)
{
    return self->_textFactory;
}

/**
 * @brief Returns the asset manager the elements' fonts come from.
 * @param self The kit; must not be NULL.
 * @returns The borrowed manager, or NULL.
 */
static inline AssetManager* UIKit_GetAssets(const UIKit* self)
{
    return self->_assets;
}

/**
 * @brief Returns the logger the elements report through.
 * @param self The kit; must not be NULL.
 * @returns The borrowed logger, or NULL.
 */
static inline Logger* UIKit_GetLogger(const UIKit* self)
{
    return self->_logger;
}


// Creating elements.
/*
 * Every UIKit_Create* makes the element with its look (UIElementOptions.Look, else the screen's theme's for the kind, else the
 * standard one), initializes it and adds it to its parent, or as a root of the screen when the parent is WIDGET_ID_INVALID, and
 * gives back its id. On a failure nothing stays behind and the id is WIDGET_ID_INVALID. Each takes:
 *   self: the kit, not NULL; screen: of the kit's factory, not NULL; parentId: an alive widget of the screen, or
 *   WIDGET_ID_INVALID; options: not NULL, read for the call; outId: not NULL.
 * Each returns ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_InvalidOperation for a parent that is gone; otherwise
 * the first error of its options, its constructor or attaching it.
 */
/** @brief Makes a stack (StackWidget.h). See the comment above for the parameters and the result. */
Error UIKit_CreateStack(UIKit* self, UIScreen* screen, uint64_t parentId, const UIStackOptions* options, uint64_t* outId);
/** @brief Makes a panel (UIPanel.h). See the comment above for the parameters and the result. */
Error UIKit_CreatePanel(UIKit* self, UIScreen* screen, uint64_t parentId, const UIPanelOptions* options, uint64_t* outId);
/** @brief Makes a label (UILabel.h). See the comment above for the parameters and the result. */
Error UIKit_CreateLabel(UIKit* self, UIScreen* screen, uint64_t parentId, const UILabelOptions* options, uint64_t* outId);
/** @brief Makes a button (UIButton.h). See the comment above for the parameters and the result. */
Error UIKit_CreateButton(UIKit* self, UIScreen* screen, uint64_t parentId, const UIButtonOptions* options, uint64_t* outId);
/** @brief Makes a toggle (UIToggle.h). See the comment above for the parameters and the result. */
Error UIKit_CreateToggle(UIKit* self, UIScreen* screen, uint64_t parentId, const UIToggleOptions* options, uint64_t* outId);
/** @brief Makes a slider (UISlider.h). See the comment above for the parameters and the result. */
Error UIKit_CreateSlider(UIKit* self, UIScreen* screen, uint64_t parentId, const UISliderOptions* options, uint64_t* outId);
/** @brief Makes a scroll bar (UIScrollbar.h). See the comment above for the parameters and the result. */
Error UIKit_CreateScrollbar(UIKit* self, UIScreen* screen, uint64_t parentId, const UIScrollbarOptions* options, uint64_t* outId);
/** @brief Makes a scroll area (UIScrollArea.h) with its two bars, each of the look the screen's theme names for the scroll bar (else the
 *  standard one), on top of whatever calling code adds to it. See the comment above for the parameters and the result. */
Error UIKit_CreateScrollArea(UIKit* self, UIScreen* screen, uint64_t parentId, const UIScrollAreaOptions* options, uint64_t* outId);
/** @brief Makes a dropdown (UIDropdown.h). See the comment above for the parameters and the result. */
Error UIKit_CreateDropdown(UIKit* self, UIScreen* screen, uint64_t parentId, const UIDropdownOptions* options, uint64_t* outId);
/** @brief Makes a menu bar (UIMenuBar.h), with no entries yet. See the comment above for the parameters and the result. */
Error UIKit_CreateMenuBar(UIKit* self, UIScreen* screen, uint64_t parentId, const UIMenuBarOptions* options, uint64_t* outId);
/** @brief Makes a status bar (UIStatusBar.h) with its three sections, each a row stack. See the comment above for the parameters and the
 *  result. */
Error UIKit_CreateStatusBar(UIKit* self, UIScreen* screen, uint64_t parentId, const UIStatusBarOptions* options, uint64_t* outId);
