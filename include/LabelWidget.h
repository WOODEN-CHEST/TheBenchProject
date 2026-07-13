#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "wr/WRError.h"
// GenericBuffer backs the label's cached line list; Vector2/Rectangle/Color and RenderColor are part of
// the label's public property data; the Widget base is embedded as the first member.
#include "wr/WRMemory.h"
#include "raylib/raylib.h"
#include "Renderer.h"
#include "UIWidget.h"


/**
 * @file LabelWidget.h
 * @brief A widget that displays a (possibly multi-line) text component.
 *
 * The label is the standard, non-special text element of the UI (for labels, buttons, textboxes, and so
 * on); heavier text (editors) uses its own systems. It renders a TextComponent tree through a shared
 * TextComponentRenderer, adding per-line horizontal alignment plus bound handling (cut / wrap / resize)
 * that the plain component renderer does not do.
 *
 * UNITS AND PLACEMENT. Text is measured and drawn in normalized-fitted units (1.0 = the target-area
 * height) scaled by the label's Size, exactly like the text component system. The text block's top-left
 * is placed at the label widget's box top-left (its Position), and the label writes its own base widget
 * Size each render so the widget box (hit-testing and the keyboard-navigation outline) hugs the rendered
 * text. Origin is the block's rotation pivot and the reference point of the reported DrawSize.
 *
 * TWO REPRESENTATIONS. The label holds the original, unmodified text component (borrowed) and a lazily
 * built cache: the text split into a list of line components (with any wrapping/resizing applied), backed
 * by an owned buffer of the substring bytes those lines reference. The cache is rebuilt only when needed
 * (on render or a draw-size query) so repeated property changes do not rebuild it each time.
 *
 * OWNERSHIP. The text component, the TextComponentFactory and the TextComponentRenderer are all borrowed
 * (passed at construction) and must outlive the label. The label owns only its cached line components
 * (returned to the factory on rebuild/deconstruct) and the substring byte buffer. Build labels through
 * the UIWidgetFactory like any widget (see LabelWidget_RegisterType / LabelWidget_Create).
 */


// Forward declarations (borrowed; full types live in their own headers).
/** @brief The program-wide widget factory; full type in UIWidgetFactory.h. */
typedef struct UIWidgetFactoryStruct UIWidgetFactory;
/** @brief The text component factory; full type in TextComponentFactory.h. Borrowed. */
typedef struct TextComponentFactoryStruct TextComponentFactory;
/** @brief The text component renderer; full type in TextComponentRenderer.h. Borrowed. */
typedef struct TextComponentRendererStruct TextComponentRenderer;
/** @brief A text component; full type in TextComponent.h. Borrowed. */
typedef struct TextComponentStruct TextComponent;


// Types.
/**
 * @brief Horizontal alignment applied per text line within the label.
 */
typedef enum LabelAlignmentEnum
{
    /** @brief Each line starts at the left edge of the text block. */
    LabelAlignment_Left,
    /** @brief Each line is centered within the text block width. */
    LabelAlignment_Center,
    /** @brief Each line ends at the right edge of the text block. */
    LabelAlignment_Right
} LabelAlignment;

/**
 * @brief How the label keeps its text within its Bounds (when bounds are present).
 */
typedef enum LabelBoundHandlingEnum
{
    /** @brief Clip the text to the bounds rectangle (GPU scissor); nothing is reflowed. */
    LabelBoundHandling_Cut,
    /** @brief Wrap lines on spaces where a line would exceed the bounds width; an unbreakable word overflows. */
    LabelBoundHandling_Wrap,
    /** @brief Shrink each over-wide line uniformly so it fits the bounds width (lines that fit are unchanged). */
    LabelBoundHandling_Resize,
    /** @brief Wrap first; any line still over-wide is then shrunk to fit. */
    LabelBoundHandling_WrapThenResize,
    /** @brief Wrap first; any line still over-wide is then clipped (scissor). */
    LabelBoundHandling_WrapThenCut
} LabelBoundHandling;

/**
 * @brief One of the nine standard origin positions, as a shorthand for setting the origin vector.
 *
 * Each maps to a combination of a horizontal (left/center/right) and a vertical (top/middle/bottom)
 * fraction; e.g. LabelOriginPosition_MiddleCenter maps to (0.5, 0.5).
 */
typedef enum LabelOriginPositionEnum
{
    /** @brief Origin (0, 0). */
    LabelOriginPosition_TopLeft,
    /** @brief Origin (0.5, 0). */
    LabelOriginPosition_TopCenter,
    /** @brief Origin (1, 0). */
    LabelOriginPosition_TopRight,
    /** @brief Origin (0, 0.5). */
    LabelOriginPosition_MiddleLeft,
    /** @brief Origin (0.5, 0.5). */
    LabelOriginPosition_MiddleCenter,
    /** @brief Origin (1, 0.5). */
    LabelOriginPosition_MiddleRight,
    /** @brief Origin (0, 1). */
    LabelOriginPosition_BottomLeft,
    /** @brief Origin (0.5, 1). */
    LabelOriginPosition_BottomCenter,
    /** @brief Origin (1, 1). */
    LabelOriginPosition_BottomRight
} LabelOriginPosition;

/**
 * @brief The label's concrete animatable property ids (for UIScreen_StartAnimation).
 *
 * Each is below WIDGET_BASE_PROPERTY_START so it does not collide with the base widget properties. The
 * comment states the UIPropertyType the animation must use.
 */
typedef enum LabelPropertyEnum
{
    /** @brief The text size multiplier (UIPropertyType_Float). */
    LabelProperty_Size,
    /** @brief The rotation in radians (UIPropertyType_Float). */
    LabelProperty_Rotation,
    /** @brief The tint (UIPropertyType_RenderColor). */
    LabelProperty_Tint,
    /** @brief The origin vector (UIPropertyType_Vector2). */
    LabelProperty_Origin
} LabelProperty;

/**
 * @brief Construction arguments for a label, passed to the widget factory.
 *
 * ComponentFactory and ComponentRenderer are required and borrowed; Text is an optional initial text
 * component (also borrowed, may be NULL for an empty label).
 */
typedef struct LabelWidgetArgsStruct
{
    /** @brief The text component factory the label builds its line cache with; borrowed, required, must outlive the label. */
    TextComponentFactory* ComponentFactory;
    /** @brief The text component renderer the label measures/draws with; borrowed, required, must outlive the label. */
    TextComponentRenderer* ComponentRenderer;
    /** @brief Optional initial text component; borrowed, may be NULL. */
    TextComponent* Text;
} LabelWidgetArgs;

/**
 * @brief A text-displaying widget. Embeds the Widget base first; underscore-prefixed fields are internal.
 *
 * Create through the widget factory (LabelWidget_Create / UIWidgetFactory_ConstructWidget) and access via
 * the LabelWidget_* getters/setters. The cached fields hold the built line representation and are managed
 * internally.
 */
typedef struct LabelWidgetStruct
{
    /** @brief Abstract widget base; must be the first member. */
    Widget Base;

    /** @brief Text component factory used to build the line cache; borrowed. */
    TextComponentFactory* _componentFactory;
    /** @brief Text component renderer used to measure and draw; borrowed. */
    TextComponentRenderer* _componentRenderer;
    /** @brief The original, unmodified text; borrowed, may be NULL (no text). */
    TextComponent* _text;

    /** @brief Per-line horizontal alignment; defaults to LabelAlignment_Left. */
    LabelAlignment _alignment;
    /** @brief Block origin (rotation pivot / DrawSize reference), normalized [0;1]; defaults to (0, 0). */
    Vector2 _origin;
    /** @brief Rotation around the origin, in radians; defaults to 0. */
    float _rotation;
    /** @brief Size multiplier applied to the text component sizes; defaults to 1. */
    float _size;
    /** @brief Tint multiplied into the text colors; defaults to opaque white. */
    RenderColor _tint;

    /** @brief Whether Bounds are present; defaults to false. */
    bool _hasBounds;
    /** @brief Maximum text extent from the top-left, in normalized-fitted units; valid only when _hasBounds. */
    Vector2 _bounds;
    /** @brief The bound handling mode; defaults to LabelBoundHandling_Cut. */
    LabelBoundHandling _boundHandling;

    /** @brief true when the cached line list reflects the current text/properties. */
    bool _isCacheValid;
    /** @brief Cached line records (private element type); rebuilt from the text as needed. */
    GenericBuffer _lines;
    /** @brief Owned byte buffer holding the NUL-terminated substrings the cached lines reference. */
    GenericBuffer _lineTextBuffer;
    /** @brief Reusable scratch of flattened atoms used while (re)building the line cache (private element type). */
    GenericBuffer _buildAtoms;
    /** @brief Cached block size in normalized-fitted units, with resize applied but before the Size multiplier. */
    Vector2 _drawSizeFitted;
} LabelWidget;


// Registration and creation.
/**
 * @brief Registers the label widget type with a widget factory, minting its type id.
 *
 * Registers the label's struct size and constructor with no capabilities. Call once per program (the
 * factory is program-wide); reuse the returned type id for every label.
 * @param factory The widget factory to register with; must not be NULL.
 * @param outTypeId [out] Receives the label type id; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p factory or @p outTypeId is NULL; otherwise a
 *          registration error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error LabelWidget_RegisterType(UIWidgetFactory* factory, uint64_t* outTypeId);

/**
 * @brief Creates a label in a screen through the widget factory.
 *
 * Convenience wrapper over UIWidgetFactory_ConstructWidget that fills a LabelWidgetArgs. The label is
 * constructed (not yet initialized or added); initialize it with Widget_InitializeWidget and add it as a
 * root or subwidget as usual.
 * @param screen The screen to build the label into; must not be NULL.
 * @param typeId The label type id from LabelWidget_RegisterType.
 * @param componentFactory The text component factory; borrowed, required, must outlive the label.
 * @param componentRenderer The text component renderer; borrowed, required, must outlive the label.
 * @param text Optional initial text component; borrowed, may be NULL.
 * @param outLabel [out] Receives the created label; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL; otherwise a construction error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error LabelWidget_Create(UIScreen* screen,
    uint64_t typeId,
    TextComponentFactory* componentFactory,
    TextComponentRenderer* componentRenderer,
    TextComponent* text,
    LabelWidget** outLabel);

/**
 * @brief Returns the label's widget base (for adding to a screen/widget and general widget operations).
 * @param self The label; must not be NULL.
 * @returns A pointer to the embedded Widget base.
 */
static inline Widget* LabelWidget_AsWidget(LabelWidget* self)
{
    return &self->Base;
}


// Properties.
/**
 * @brief Returns the label's borrowed text component (may be NULL).
 * @param self The label; must not be NULL.
 * @returns The text component, or NULL.
 */
TextComponent* LabelWidget_GetText(const LabelWidget* self);

/**
 * @brief Sets the label's text to a borrowed component (or NULL for none) and invalidates the cache.
 *
 * The component is not owned; the caller keeps ownership and must keep it (and everything it references)
 * alive while the label uses it.
 * @param self The label; must not be NULL.
 * @param text Borrowed text component, or NULL to clear.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error LabelWidget_SetText(LabelWidget* self, TextComponent* text);

/**
 * @brief Returns the per-line horizontal alignment.
 * @param self The label; must not be NULL.
 * @returns The alignment.
 */
LabelAlignment LabelWidget_GetAlignment(const LabelWidget* self);

/**
 * @brief Sets the per-line horizontal alignment and invalidates the cache.
 * @param self The label; must not be NULL.
 * @param alignment The alignment.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p alignment is out of range.
 */
Error LabelWidget_SetAlignment(LabelWidget* self, LabelAlignment alignment);

/**
 * @brief Returns the block origin (rotation pivot / DrawSize reference), normalized [0;1].
 * @param self The label; must not be NULL.
 * @returns The origin.
 */
Vector2 LabelWidget_GetOrigin(const LabelWidget* self);

/**
 * @brief Sets the block origin.
 * @param self The label; must not be NULL.
 * @param origin The origin; both components must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if a
 *          component is not finite.
 */
Error LabelWidget_SetOrigin(LabelWidget* self, Vector2 origin);

/**
 * @brief Sets the block origin from one of the nine standard positions.
 * @param self The label; must not be NULL.
 * @param position The standard position.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p position is out of range.
 */
Error LabelWidget_SetOriginPosition(LabelWidget* self, LabelOriginPosition position);

/**
 * @brief Returns the rotation around the origin, in radians.
 * @param self The label; must not be NULL.
 * @returns The rotation.
 */
float LabelWidget_GetRotation(const LabelWidget* self);

/**
 * @brief Sets the rotation around the origin, in radians.
 * @param self The label; must not be NULL.
 * @param rotationRad The rotation; must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if not finite.
 */
Error LabelWidget_SetRotation(LabelWidget* self, float rotationRad);

/**
 * @brief Returns the text size multiplier.
 * @param self The label; must not be NULL.
 * @returns The size multiplier.
 */
float LabelWidget_GetSize(const LabelWidget* self);

/**
 * @brief Sets the text size multiplier and invalidates the cache.
 * @param self The label; must not be NULL.
 * @param size The size multiplier; must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if not finite.
 */
Error LabelWidget_SetSize(LabelWidget* self, float size);

/**
 * @brief Returns the tint multiplied into the text colors.
 * @param self The label; must not be NULL.
 * @returns The tint.
 */
RenderColor LabelWidget_GetTint(const LabelWidget* self);

/**
 * @brief Sets the tint multiplied into the text colors.
 * @param self The label; must not be NULL.
 * @param tint The tint; its Brightness and Opacity must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          Brightness or Opacity is not finite.
 */
Error LabelWidget_SetTint(LabelWidget* self, RenderColor tint);

/**
 * @brief Reports whether bounds are present.
 * @param self The label; must not be NULL.
 * @returns true if bounds are set.
 */
bool LabelWidget_HasBounds(const LabelWidget* self);

/**
 * @brief Returns the bounds (valid only when HasBounds), a max extent from the top-left in fitted units.
 * @param self The label; must not be NULL.
 * @returns The bounds vector.
 */
Vector2 LabelWidget_GetBounds(const LabelWidget* self);

/**
 * @brief Sets the bounds (a max text extent from the top-left, in fitted units) and invalidates the cache.
 * @param self The label; must not be NULL.
 * @param bounds The bounds; both components must be finite and non-negative.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if a
 *          component is not finite or is negative.
 */
Error LabelWidget_SetBounds(LabelWidget* self, Vector2 bounds);

/**
 * @brief Clears the bounds (text is drawn unconstrained) and invalidates the cache.
 * @param self The label; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error LabelWidget_ClearBounds(LabelWidget* self);

/**
 * @brief Returns the bound handling mode.
 * @param self The label; must not be NULL.
 * @returns The mode.
 */
LabelBoundHandling LabelWidget_GetBoundHandling(const LabelWidget* self);

/**
 * @brief Sets the bound handling mode and invalidates the cache.
 * @param self The label; must not be NULL.
 * @param mode The mode.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p mode is out of range.
 */
Error LabelWidget_SetBoundHandling(LabelWidget* self, LabelBoundHandling mode);

/**
 * @brief Returns the text's draw size (includes the Size multiplier), in normalized-fitted units.
 *
 * Builds the line cache first if needed (which is why this is not a const call). Rotation does not affect
 * the reported size.
 * @param self The label; must not be NULL.
 * @param outSize [out] Receives the draw size; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outSize is NULL; otherwise a build error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error LabelWidget_GetDrawSize(LabelWidget* self, Vector2* outSize);
