#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "StackWidget.h"
#include "UIButton.h"
#include "UIElement.h"
#include "UILabel.h"
#include "UIPanel.h"
#include "UISlider.h"
#include "UITheme.h"
#include "UIToggle.h"


/**
 * @file UIKit.h
 * @brief The one way calling code makes elements, over one widget factory.
 *
 * The kit registers each element's capability and standard widget type once and keeps the table from element kind to widget
 * type, which a new look can replace (UIKit_SetElementType). Calling code creates an element with UIKit_Create* and keeps
 * its widget id, then uses the kind's functions by id (UIButton_SetText(screen, id, text)): it never names a widget type or
 * a look, so looks can be swapped under it.
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
    /** @brief The number of capabilities. */
    UIElementCapability_Count
} UIElementCapability;

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
    /** @brief The widget type of each element kind. */
    uint64_t _types[UIElementKind_Count];
    /** @brief The stack's widget type. */
    uint64_t _stackType;
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
 * @brief Leaves the factory without a kit. Its widget types stay registered until the factory goes. Safe on NULL.
 * @param self The kit, or NULL.
 * @returns Success.
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
 * @brief Returns the widget type an element kind is made with.
 * @param self The kit; must not be NULL.
 * @param kind The kind.
 * @returns The type id; WIDGET_TYPE_ID_INVALID for a value out of range.
 */
uint64_t UIKit_GetElementType(const UIKit* self, UIElementKind kind);

/**
 * @brief Replaces the widget type an element kind is made with: from now on UIKit_Create* makes it. Its constructor gets the
 *        same arguments as the standard type's (UIButtonArgs, ...), and it must embed the same capability.
 * @param self The kit; must not be NULL.
 * @param kind The kind.
 * @param typeId The registered type.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for an unknown kind;
 *          ErrorCode_InvalidOperation if the type is not registered or lacks the kind's capability.
 */
Error UIKit_SetElementType(UIKit* self, UIElementKind kind, uint64_t typeId);

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
 * Every UIKit_Create* makes the element, initializes it and adds it to its parent, or as a root of the screen when the parent
 * is WIDGET_ID_INVALID, and gives back its id. On a failure nothing stays behind and the id is WIDGET_ID_INVALID. Each takes:
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
