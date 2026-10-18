#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "wr/WRError.h"
// GenericBuffer backs the subwidget lists and the id output of GetSubWidgets; Vector2/Rectangle (geometry),
// RenderColor (tint), ProgramTime (update time), the UI input/animation/render types and RenderContext (layout
// measuring) are all part of this public API, so these includes are unavoidable.
#include "wr/WRMemory.h"
#include "raylib/raylib.h"
#include "Renderer.h"
#include "ProgramTime.h"
#include "UIInput.h"
#include "UIAnimation.h"
#include "UIRenderContext.h"


/**
 * @file UIWidget.h
 * @brief The abstract base every UI widget derives from: identity, geometry, flags, state, subwidgets, capabilities,
 *        typed properties, and the lifecycle the screen drives.
 *
 * A widget is one thing in the UI. Concrete widgets embed a Widget as their FIRST member and register a widget type
 * (a WidgetTypeDescriptor with a WidgetVTable of behavior) with the program-wide UIWidgetFactory. A concrete widget is
 * never constructed or freed directly: UIWidgetFactory_CreateWidget builds it (the base first, then the type's
 * constructor, then it is registered with its screen) and Widget_Destroy tears it down.
 *
 * Geometry is normalized [0;1] and RELATIVE TO THE PARENT (or the screen for a root): a child at position p with a
 * parent at position P and size S sits at absolute P + p*S. Estimated bounds (a cached box that also encloses all
 * subwidgets) drive keyboard-navigation distances and outlines.
 *
 * LIFECYCLE. create -> (configure) -> Widget_InitializeWidget -> add to the screen or a parent -> ... ->
 * Widget_Destroy. A widget must be initialized before it is added. Widget_Destroy is always deferred and takes the
 * whole subtree: the subtree is unlinked and its ids stop resolving at once, but its hooks (disable, Deinitialize,
 * OnDeconstruct) run and its memory is freed only at the end of the screen's next UIScreen_Update (or in
 * UIScreen_Deconstruct), children before their parents. So a widget may destroy itself or anything else from any hook
 * or event handler: no memory is freed while a callback is running.
 *
 * IDS, NOT POINTERS. Widget ids encode a registry slot and a generation: a stale id never resolves, not even to a newer
 * widget in the same slot. Never hold a Widget* across anything that can run hooks or event handlers; keep the id and
 * re-resolve it (UIScreen_TryGetWidgetById / UIScreen_IsWidgetAlive).
 *
 * Underscore-prefixed struct fields are read-only to code outside the UI framework; use the functions below.
 */


// Forward declarations.
/** @brief The UI screen/context a widget belongs to; full type in UIScreen.h. Borrowed. */
typedef struct UIScreenStruct UIScreen;
/** @brief The widget base itself; forward-declared so the struct can hold parent/subwidget pointers. */
typedef struct WidgetStruct Widget;


// Macros.
/** @brief Invalid widget id (a real id is never 0). */
#define WIDGET_ID_INVALID ((uint64_t)0)
/** @brief Passed as the depth to Widget_GetSubWidgets to collect all descendants regardless of nesting. */
#define WIDGET_SUBWIDGET_DEPTH_ALL (~((size_t)0))
/** @brief First property id reserved for base properties; concrete property ids must be below this (and >= 0). */
#define WIDGET_BASE_PROPERTY_START ((int32_t)0x40000000)
/** @brief The most levels a widget tree may have (a root alone is one level); Widget_AddSubWidget enforces it, so
 *  every walk of a tree is bounded. */
#define WIDGET_MAX_TREE_DEPTH ((size_t)64)


// Types.
/**
 * @brief Base widget properties addressable through the typed property get/set (e.g. for animation).
 *
 * These ids live in a reserved high range (see WIDGET_BASE_PROPERTY_START) so they never collide with a concrete
 * widget's own property ids. Every widget has them.
 */
typedef enum WidgetBasePropertyEnum
{
    /** @brief The widget's parent-relative position; UIPropertyType_Vector2. */
    WidgetBaseProperty_Position = WIDGET_BASE_PROPERTY_START,
    /** @brief The widget's parent-relative size; UIPropertyType_Vector2. */
    WidgetBaseProperty_Size,
    /** @brief The widget's z layer; UIPropertyType_Float. */
    WidgetBaseProperty_ZLayer,
    /** @brief The widget's render tint; UIPropertyType_RenderColor. */
    WidgetBaseProperty_RenderTint
} WidgetBaseProperty;

/**
 * @brief What a widget's Layout hook gets: a way to convert units for the screen's viewport, and where its parent is.
 *
 * Built by the screen on the stack for each call; owns nothing.
 */
typedef struct UILayoutContextStruct
{
    /** @brief A measuring-only render context for the screen's viewport (RenderContext_CreateMeasuring), for converting
     *  between pixel, relative and fitted units. Borrowed for the call; never draw through it. */
    RenderContext* Measure;
    /** @brief The parent's box size in screen-normalized units ({1, 1} for a root). */
    Vector2 ParentAbsoluteSize;
} UILayoutContext;

/**
 * @brief Virtual table of behavior for a concrete widget type.
 *
 * A concrete type supplies one static instance through its WidgetTypeDescriptor. Every slot receives the widget's own
 * object as @c self (a @c void* recovered without a cast). Every slot is OPTIONAL: a NULL slot means "no behavior".
 * A slot returning a non-success Error is treated as a broken invariant: the screen stops and propagates it (a frame
 * then shuts the game down), so recoverable problems must be handled inside the widget.
 *
 * Hooks may destroy widgets (including their own) and change focus: the screen re-resolves every widget by id after
 * each call. Render may not change any state.
 */
typedef struct WidgetVTableStruct
{
    /** @brief Initializes the widget after construction (Widget_InitializeWidget); create child widgets here, not in
     *  the constructor. Optional. */
    Error (*Initialize)(void* self);
    /** @brief Deinitializes the widget during its destruction, before OnDeconstruct (only if it was initialized). Its
     *  children are already gone. Optional. */
    Error (*Deinitialize)(void* self);
    /** @brief Releases the concrete widget's own resources during its destruction. Must NOT free the widget or touch
     *  the base (the framework does that). Optional. */
    Error (*OnDeconstruct)(void* self);

    /** @brief Advances the widget's logic by one update tick. Optional. */
    Error (*Update)(void* self, ProgramTime time);
    /** @brief Fits the widget's geometry after the tick's updates (e.g. a label sizing its box to its text). Called
     *  once per tick, parents before children, for every attached widget that was in the tree when the tick began (a
     *  widget attached during the tick is laid out from the next tick on). May change the widget's own geometry.
     *  Optional. */
    Error (*Layout)(void* self, const UILayoutContext* context);
    /** @brief Draws the widget through its widget-local render context; reads state, never writes it. Optional. */
    Error (*Render)(void* self, UIRenderContext* context);

    /** @brief Called when the widget's update flag becomes enabled. Optional. */
    Error (*OnUpdateEnable)(void* self);
    /** @brief Called when the widget's update flag becomes disabled. Optional. */
    Error (*OnUpdateDisable)(void* self);
    /** @brief Called when the widget's render flag becomes enabled. Optional. */
    Error (*OnRenderEnable)(void* self);
    /** @brief Called when the widget's render flag becomes disabled. Optional. */
    Error (*OnRenderDisable)(void* self);
    /** @brief Called when the widget's input flag becomes enabled. Optional. */
    Error (*OnInputEnable)(void* self);
    /** @brief Called when the widget's input flag becomes disabled. Optional. */
    Error (*OnInputDisable)(void* self);

    /** @brief Called when the widget begins to be hovered; @p args gives the hover position. Optional. */
    Error (*OnHoverStart)(void* self, const WidgetHoverArgs* args);
    /** @brief Called when the widget stops being hovered. Optional. */
    Error (*OnHoverEnd)(void* self);
    /** @brief Called for a mouse press on the widget, and the release and drags of that press, and a scroll over it.
     *  A press on an input-enabled widget is always consumed (the game does not see it). Optional. */
    Error (*OnMouseInput)(void* self, const WidgetMouseInputArgs* args);
    /** @brief Called with a key event or typed text while the widget is focused or contains the focused widget (the
     *  focused widget first, then its input-enabled ancestors; input-disabled ones are skipped). Set @p outIsHandled to
     *  true to consume it and stop it there; an unhandled key goes on to the next ancestor, then to keyboard
     *  navigation, then to the game. A widget that handles a key's press owns the key until it is released: the key's
     *  repeats and release then come to it alone (even after focus moved, while it stays attached and input-enabled),
     *  and its answer to them changes nothing. @p outIsHandled starts false. Optional (a NULL slot handles nothing). */
    Error (*OnKeyboardInput)(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled);
    /** @brief Called when the widget becomes focused in its screen. Optional. */
    Error (*OnFocusEnable)(void* self);
    /** @brief Called when the widget stops being focused. Optional. */
    Error (*OnFocusDisable)(void* self);
    /** @brief Called when keyboard navigation enters the widget (it becomes tabbed). Optional. */
    Error (*OnTabbedEnable)(void* self);
    /** @brief Called when keyboard navigation leaves the widget (it stops being tabbed). Optional. */
    Error (*OnTabbedDisable)(void* self);
    /** @brief Called when the widget becomes active (hovered or tabbed). Optional. */
    Error (*OnActiveEnable)(void* self);
    /** @brief Called when the widget stops being active. Optional. */
    Error (*OnActiveDisable)(void* self);

    /** @brief Reads a concrete property declared by the type (the base has checked the id; @p outValue->Type is already
     *  set to the declared type). Optional. */
    Error (*GetProperty)(void* self, int32_t propertyId, UIPropertyValue* outValue);
    /** @brief Writes a concrete property declared by the type (the base has checked the id and that @p value has the
     *  declared type). Optional. */
    Error (*SetProperty)(void* self, int32_t propertyId, const UIPropertyValue* value);
} WidgetVTable;

/**
 * @brief Abstract base of every widget: its vtable plus identity, geometry, flags, state and subwidgets.
 *
 * Embed as the FIRST member of a concrete widget. All fields are managed through the Widget_* functions;
 * underscore-prefixed fields are read-only to code outside the UI framework.
 */
typedef struct WidgetStruct
{
    /** @brief Behavior table of the widget's type; set at creation. Never NULL. */
    const WidgetVTable* VTable;

    /** @brief The screen this widget belongs to; borrowed, set at creation. */
    UIScreen* _screen;
    /** @brief The parent widget, or NULL if this is a root or detached. */
    Widget* _parent;
    /** @brief The widget's id (registry slot + generation); minted at creation. */
    uint64_t _id;
    /** @brief The widget's registered factory type id. */
    uint64_t _typeId;
    /** @brief When the widget was last added to its parent or the roots (screen-wide counter): orders siblings of equal
     *  z by insertion. */
    uint64_t _sequence;

    /** @brief Parent-relative position, normalized [0;1]. */
    Vector2 _position;
    /** @brief Parent-relative size, normalized [0;1]. */
    Vector2 _size;
    /** @brief Z layer relative to the siblings; higher draws on top. */
    float _zLayer;
    /** @brief Render tint multiplied into this widget's (and its subtree's) drawing; defaults to white. */
    RenderColor _renderTint;

    /** @brief When true (default) the widget is rendered. */
    bool _isRendered;
    /** @brief When true (default) the widget receives update ticks. */
    bool _isUpdated;
    /** @brief When true (default) the widget receives input and is hit by the pointer. */
    bool _isInputEnabled;
    /** @brief When true, subwidget rendering and hit testing are clipped to this widget's box. */
    bool _areSubWidgetsCut;
    /** @brief True while this widget is a root of its screen. */
    bool _isScreenRoot;
    /** @brief When true the widget can take keyboard focus (clicks and keyboard navigation); the type sets the default. */
    bool _isFocusable;

    /** @brief True while the pointer hovers the widget. */
    bool _isHovered;
    /** @brief True while the widget is its screen's focused widget. */
    bool _isFocused;
    /** @brief True while keyboard navigation has entered the widget. */
    bool _isTabbed;
    /** @brief True while the widget is active (hovered or tabbed). */
    bool _isActive;

    /** @brief True once Widget_InitializeWidget succeeded. */
    bool _isInitialized;
    /** @brief True once Widget_Destroy took the widget (or an ancestor): its id no longer resolves and its memory is
     *  freed at the screen's next flush. */
    bool _isDying;

    /** @brief Cached estimated bounds (parent space), valid only when _areBoundsValid is true. */
    Rectangle _cachedBounds;
    /** @brief True when _cachedBounds is up to date. */
    bool _areBoundsValid;

    /** @brief Owned buffer of the children (Widget*, borrowed) in insertion order. */
    GenericBuffer _subWidgets;
    /** @brief Owned buffer of the same children (Widget*) in drawing order: ascending z, then insertion. */
    GenericBuffer _drawOrder;
} Widget;


// Lifecycle.
/**
 * @brief Runs the widget's Initialize hook, then the update, render and input enable hooks (the flags start enabled).
 *
 * Call once after creation and configuration, before the widget is first added to a screen or parent.
 * @param self The widget; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if the widget is already
 *          initialized or is being destroyed; otherwise the first error from a hook.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_InitializeWidget(Widget* self);

/**
 * @brief Destroys the widget and its whole subtree, deferred.
 *
 * At once: the widget is unlinked from its parent (or the roots), every widget of the subtree stops resolving by id,
 * and the screen forgets them (hover, focus, keyboard navigation, mouse presses, animations); no hook runs. At the end
 * of the screen's next UIScreen_Update (or in UIScreen_Deconstruct), the subtree is torn down children first: each
 * widget's disable hooks and Deinitialize (if it was initialized), then OnDeconstruct, then its storage returns to the
 * factory. Safe to call from any hook or event handler, on the widget being called back included. Destroying a widget
 * that is already being destroyed does nothing.
 * @param self The widget, or NULL (nothing happens).
 * @returns Success (also for NULL and a widget already being destroyed); ErrorCode_InvalidOperation if the widget is
 *          not alive in its screen (still being constructed); ErrorCode_BufferTooLarge if the screen could not record it
 *          (nothing changed then).
 */
Error Widget_Destroy(Widget* self);


// Framework-driven wrappers (the screen calls these; they call the vtable slot when present).
/**
 * @brief Updates the widget (calls the Update slot if present).
 * @param self The widget; must not be NULL.
 * @param time The update-tick time.
 * @returns The slot's result, or success if there is no Update slot.
 */
Error Widget_Update(Widget* self, ProgramTime time);

/**
 * @brief Lays the widget out (calls the Layout slot if present).
 * @param self The widget; must not be NULL.
 * @param context The layout context; must not be NULL.
 * @returns The slot's result, or success if there is no Layout slot.
 */
Error Widget_Layout(Widget* self, const UILayoutContext* context);

/**
 * @brief Renders the widget (calls the Render slot if present).
 * @param self The widget; must not be NULL.
 * @param context The widget-local render context; must not be NULL.
 * @returns The slot's result, or success if there is no Render slot.
 */
Error Widget_Render(Widget* self, UIRenderContext* context);

/**
 * @brief Delivers mouse input to the widget (calls the OnMouseInput slot if present).
 * @param self The widget; must not be NULL.
 * @param args The mouse input arguments; must not be NULL.
 * @returns The slot's result, or success if there is no OnMouseInput slot.
 */
Error Widget_OnMouseInput(Widget* self, const WidgetMouseInputArgs* args);

/**
 * @brief Delivers keyboard input to the widget (calls the OnKeyboardInput slot if present).
 * @param self The widget; must not be NULL.
 * @param args The keyboard input arguments; must not be NULL.
 * @param outIsHandled [out] Receives whether the widget handled the input; false without a slot. Must not be NULL.
 * @returns The slot's result, or success if there is no OnKeyboardInput slot.
 */
Error Widget_OnKeyboardInput(Widget* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled);


// Identity and references (never fail).
/**
 * @brief Returns the widget's id.
 * @param self The widget; must not be NULL.
 * @returns The id.
 */
static inline uint64_t Widget_GetId(const Widget* self)
{
    return self->_id;
}

/**
 * @brief Returns the widget's factory type id.
 * @param self The widget; must not be NULL.
 * @returns The type id.
 */
static inline uint64_t Widget_GetTypeId(const Widget* self)
{
    return self->_typeId;
}

/**
 * @brief Returns the screen the widget belongs to.
 * @param self The widget; must not be NULL.
 * @returns The borrowed screen.
 */
static inline UIScreen* Widget_GetScreen(const Widget* self)
{
    return self->_screen;
}

/**
 * @brief Returns the widget's parent, or NULL if it is a root or detached.
 * @param self The widget; must not be NULL.
 * @returns The borrowed parent, or NULL.
 */
static inline Widget* Widget_GetParent(const Widget* self)
{
    return self->_parent;
}

/**
 * @brief Returns whether the widget is currently a root of its screen.
 * @param self The widget; must not be NULL.
 * @returns true if the widget is a screen root.
 */
static inline bool Widget_IsScreenRoot(const Widget* self)
{
    return self->_isScreenRoot;
}

/**
 * @brief Returns whether Widget_InitializeWidget has succeeded on the widget.
 * @param self The widget; must not be NULL.
 * @returns true once initialized.
 */
static inline bool Widget_IsInitialized(const Widget* self)
{
    return self->_isInitialized;
}


// Geometry.
/**
 * @brief Returns the widget's parent-relative position.
 * @param self The widget; must not be NULL.
 * @returns The position.
 */
static inline Vector2 Widget_GetPosition(const Widget* self)
{
    return self->_position;
}

/**
 * @brief Sets the widget's parent-relative position (invalidates cached bounds up the tree).
 * @param self The widget; must not be NULL.
 * @param position The new position; both components must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if a component is NaN
 *          or infinite.
 */
Error Widget_SetPosition(Widget* self, Vector2 position);

/**
 * @brief Returns the widget's parent-relative size.
 * @param self The widget; must not be NULL.
 * @returns The size.
 */
static inline Vector2 Widget_GetSize(const Widget* self)
{
    return self->_size;
}

/**
 * @brief Sets the widget's parent-relative size (invalidates cached bounds up the tree).
 * @param self The widget; must not be NULL.
 * @param size The new size; both components must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if a component is NaN
 *          or infinite.
 */
Error Widget_SetSize(Widget* self, Vector2 size);

/**
 * @brief Returns the widget's z layer.
 * @param self The widget; must not be NULL.
 * @returns The z layer.
 */
static inline float Widget_GetZLayer(const Widget* self)
{
    return self->_zLayer;
}

/**
 * @brief Sets the widget's z layer (higher draws on top of its siblings; siblings of equal z keep insertion order).
 * @param self The widget; must not be NULL.
 * @param zLayer The new z layer; must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p zLayer is NaN or
 *          infinite.
 */
Error Widget_SetZLayer(Widget* self, float zLayer);

/**
 * @brief Returns the widget's estimated bounds (parent space), enclosing the widget and all subwidgets.
 *
 * The bounds are cached and lazily recomputed when invalidated (by a geometry or subwidget change on this widget or a
 * descendant), which is why @p self is non-const.
 * @param self The widget; must not be NULL.
 * @returns The estimated bounds as a Rectangle in the widget's parent coordinate space.
 */
Rectangle Widget_GetEstimatedBounds(Widget* self);


// Rendering and behavior flags.
/**
 * @brief Returns the widget's render tint.
 * @param self The widget; must not be NULL.
 * @returns The tint.
 */
static inline RenderColor Widget_GetRenderTint(const Widget* self)
{
    return self->_renderTint;
}

/**
 * @brief Sets the widget's render tint (multiplied into the widget and its subtree while drawing).
 * @param self The widget; must not be NULL.
 * @param tint The new tint; its Brightness and Opacity must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if Brightness or
 *          Opacity is NaN or infinite.
 */
Error Widget_SetRenderTint(Widget* self, RenderColor tint);

/**
 * @brief Returns whether the widget is rendered.
 * @param self The widget; must not be NULL.
 * @returns true if rendered.
 */
static inline bool Widget_IsRendered(const Widget* self)
{
    return self->_isRendered;
}

/**
 * @brief Enables or disables rendering, firing OnRenderEnable / OnRenderDisable on a change.
 * @param self The widget; must not be NULL.
 * @param isRendered The new render flag.
 * @returns Success, or a non-success Error propagated from the fired hook.
 */
Error Widget_SetIsRendered(Widget* self, bool isRendered);

/**
 * @brief Returns whether the widget receives update ticks.
 * @param self The widget; must not be NULL.
 * @returns true if updated.
 */
static inline bool Widget_IsUpdated(const Widget* self)
{
    return self->_isUpdated;
}

/**
 * @brief Enables or disables updates, firing OnUpdateEnable / OnUpdateDisable on a change.
 * @param self The widget; must not be NULL.
 * @param isUpdated The new update flag.
 * @returns Success, or a non-success Error propagated from the fired hook.
 */
Error Widget_SetIsUpdated(Widget* self, bool isUpdated);

/**
 * @brief Returns whether the widget receives input and is hit by the pointer.
 * @param self The widget; must not be NULL.
 * @returns true if input-enabled.
 */
static inline bool Widget_IsInputEnabled(const Widget* self)
{
    return self->_isInputEnabled;
}

/**
 * @brief Enables or disables input, firing OnInputEnable / OnInputDisable on a change.
 *
 * A disabled widget receives no input, is transparent to the pointer (whatever is behind it is hit) and cannot keep
 * focus: disabling the focused widget clears the screen's focus.
 * @param self The widget; must not be NULL.
 * @param isInputEnabled The new input flag.
 * @returns Success, or a non-success Error propagated from a fired hook.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_SetIsInputEnabled(Widget* self, bool isInputEnabled);

/**
 * @brief Returns whether subwidgets are clipped to this widget's box (for rendering and hit testing).
 * @param self The widget; must not be NULL.
 * @returns true if subwidgets are cut.
 */
static inline bool Widget_AreSubWidgetsCut(const Widget* self)
{
    return self->_areSubWidgetsCut;
}

/**
 * @brief Sets whether subwidgets are clipped to this widget's box (affects rendering and hit testing).
 * @param self The widget; must not be NULL.
 * @param areSubWidgetsCut The new clip flag.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error Widget_SetAreSubWidgetsCut(Widget* self, bool areSubWidgetsCut);

/**
 * @brief Returns whether the widget can take keyboard focus.
 * @param self The widget; must not be NULL.
 * @returns true if focusable.
 */
static inline bool Widget_IsFocusable(const Widget* self)
{
    return self->_isFocusable;
}

/**
 * @brief Sets whether the widget can take keyboard focus (a click on it, or keyboard navigation). Making the focused
 *        widget unfocusable clears the screen's focus. Non-focusable widgets are transparent to navigation: their
 *        focusable descendants are navigated as if they were siblings.
 * @param self The widget; must not be NULL.
 * @param isFocusable The new flag.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; otherwise an error from the focus hooks.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_SetIsFocusable(Widget* self, bool isFocusable);


// Interaction state (driven by the screen).
/**
 * @brief Returns whether the pointer hovers the widget.
 * @param self The widget; must not be NULL.
 * @returns true if hovered.
 */
static inline bool Widget_IsHovered(const Widget* self)
{
    return self->_isHovered;
}

/**
 * @brief Returns whether the widget is its screen's focused widget.
 * @param self The widget; must not be NULL.
 * @returns true if focused.
 */
static inline bool Widget_IsFocused(const Widget* self)
{
    return self->_isFocused;
}

/**
 * @brief Returns whether keyboard navigation has entered the widget.
 * @param self The widget; must not be NULL.
 * @returns true if tabbed.
 */
static inline bool Widget_IsTabbed(const Widget* self)
{
    return self->_isTabbed;
}

/**
 * @brief Returns whether the widget is active (hovered or tabbed).
 * @param self The widget; must not be NULL.
 * @returns true if active.
 */
static inline bool Widget_IsActive(const Widget* self)
{
    return self->_isActive;
}


// Subwidgets.
/**
 * @brief Adds a subwidget (child) to this widget, on top of its siblings of equal z.
 *
 * Both widgets must belong to the same screen, be initialized and not being destroyed; the child must be detached (no
 * parent, not a root) and must not be this widget or one of its ancestors; and the tree may not get deeper than
 * WIDGET_MAX_TREE_DEPTH levels. The child is borrowed, not owned. Invalidates cached bounds up the tree.
 * @param self The widget; must not be NULL.
 * @param child The subwidget to add; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a widget is NULL, they belong to different screens, or @p child is
 *          this widget or an ancestor; ErrorCode_InvalidOperation if either is not initialized or is being destroyed,
 *          or @p child is attached; ErrorCode_ArgumentOutOfRange if the tree would get too deep;
 *          ErrorCode_BufferTooLarge if the child could not be stored.
 */
Error Widget_AddSubWidget(Widget* self, Widget* child);

/**
 * @brief Removes a subwidget (child) from this widget without destroying it (it can be added again).
 *
 * The screen forgets what pointed into the removed subtree: a hovered widget gets OnHoverEnd, the focused widget loses
 * focus, keyboard navigation leaves it and a mouse press on it ends (its release goes nowhere). Its animations keep
 * running.
 * @param self The widget; must not be NULL.
 * @param child The subwidget to remove; must not be NULL and must be a current child.
 * @returns Success; ErrorCode_IllegalArgument if a widget is NULL; ErrorCode_InvalidOperation if @p child is not a
 *          current subwidget or is being destroyed; otherwise the first error from a fired hook.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_RemoveSubWidget(Widget* self, Widget* child);

/**
 * @brief Appends the ids of this widget's descendants to a buffer, down to a given depth.
 *
 * Depth 0 appends only the immediate children; depth N appends descendants up to N levels below the immediate
 * children; WIDGET_SUBWIDGET_DEPTH_ALL appends all descendants. Children appear in insertion order. Does not clear the
 * buffer first (append semantics).
 * @param self The widget; must not be NULL.
 * @param outIds [out] A uint64_t buffer to append ids to; must not be NULL with element size sizeof(uint64_t).
 * @param depth The nesting depth (0 = immediate children, WIDGET_SUBWIDGET_DEPTH_ALL = all descendants).
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outIds is NULL or the element size is wrong;
 *          ErrorCode_BufferTooLarge if the buffer could not grow.
 */
Error Widget_GetSubWidgets(const Widget* self, GenericBuffer* outIds, size_t depth);

/**
 * @brief Returns the number of immediate subwidgets.
 * @param self The widget; must not be NULL.
 * @returns The immediate subwidget count.
 */
size_t Widget_GetSubWidgetCount(const Widget* self);

/**
 * @brief Returns the immediate subwidget at a given index, in insertion order (z never reorders this list).
 * @param self The widget; must not be NULL.
 * @param index Index in [0, Widget_GetSubWidgetCount).
 * @returns The borrowed subwidget, or NULL if @p index is out of range.
 */
Widget* Widget_GetSubWidgetAt(const Widget* self, size_t index);


// Capabilities.
/**
 * @brief Returns whether this widget's type supports a capability.
 * @param self The widget; must not be NULL.
 * @param capabilityId The capability id to test.
 * @returns true if supported.
 */
bool Widget_IsCapabilitySupported(const Widget* self, uint64_t capabilityId);

/**
 * @brief Appends the capability ids this widget's type supports to a buffer (append semantics).
 * @param self The widget; must not be NULL.
 * @param outCapabilityIds [out] A uint64_t buffer; must not be NULL with element size sizeof(uint64_t).
 * @returns Success; propagates factory errors (see UIWidgetFactory_GetSupportedCapabilities).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_GetSupportedCapabilities(const Widget* self, GenericBuffer* outCapabilityIds);

/**
 * @brief Returns a pointer to this widget's instance of a capability struct, or NULL if unsupported.
 *
 * The pointer refers to storage inside the live widget; it is only as stable as the widget.
 * @param self The widget; must not be NULL.
 * @param capabilityId The capability id to resolve.
 * @returns A pointer to the capability struct, or NULL if the type does not support it.
 */
void* Widget_GetCapability(Widget* self, uint64_t capabilityId);


// Typed properties (used by animation and generic tooling).
/**
 * @brief Looks up the type of one of the widget's properties: a WidgetBaseProperty, or a property its type declares.
 * @param self The widget; must not be NULL.
 * @param propertyId The property id.
 * @param outType [out] Receives the type when found; must not be NULL.
 * @returns true if the widget has the property.
 */
bool Widget_TryGetPropertyType(const Widget* self, int32_t propertyId, UIPropertyType* outType);

/**
 * @brief Reads a property value.
 * @param self The widget; must not be NULL.
 * @param propertyId The property id (a WidgetBaseProperty or one the type declares).
 * @param outValue [out] Receives the typed value; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outValue is NULL; ErrorCode_InvalidOperation if the
 *          widget has no such property or its type declares it without a GetProperty slot.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_GetProperty(Widget* self, int32_t propertyId, UIPropertyValue* outValue);

/**
 * @brief Writes a property value through the widget's validating setter.
 * @param self The widget; must not be NULL.
 * @param propertyId The property id (a WidgetBaseProperty or one the type declares).
 * @param value The typed value; must not be NULL and must have the property's type.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p value is NULL or @p value's type is not the property's;
 *          ErrorCode_InvalidOperation if the widget has no such property or its type declares it without a SetProperty
 *          slot; otherwise the setter's validation error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_SetProperty(Widget* self, int32_t propertyId, const UIPropertyValue* value);
