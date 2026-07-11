#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "wr/WRError.h"
#include "wr/WREvent.h"
// GenericBuffer backs the subwidget list and the id output of GetSubWidgets; Vector2/Rectangle (geometry),
// RenderColor (tint), ProgramTime (update time) and the UI input/render types are all part of this public
// API, so these includes are unavoidable.
#include "wr/WRMemory.h"
#include "raylib/raylib.h"
#include "Renderer.h"
#include "ProgramTime.h"
#include "UIInput.h"
#include "UIRenderContext.h"


/**
 * @file UIWidget.h
 * @brief The abstract base every UI widget derives from: identity, geometry, state, subwidgets,
 *        capabilities, events, and the lifecycle the screen drives.
 *
 * A widget is one thing in the UI. Concrete widgets embed a Widget as their FIRST member and supply a
 * WidgetVTable of behavior; because the base is first, a pointer to any concrete widget is also a valid
 * Widget*. Widgets are produced by a screen's factory (which pools their storage), so a concrete widget
 * is never constructed or freed directly — UIWidgetFactory_ConstructWidget builds it and Widget_Deconstruct
 * tears it down and returns its storage to the pool.
 *
 * Geometry is normalized [0;1] and RELATIVE TO THE PARENT (or the screen for a root): a widget has a
 * Position and a Size, and a child at position p with a parent at position P and size S sits at absolute
 * P + p*S. Estimated bounds (a cached box that also encloses all subwidgets) drive keyboard-navigation
 * distances and hit testing.
 *
 * A widget exposes no public fields — everything is reached through getters/setters, and setters that can
 * fail (or that fire behavior) return an Error, propagated by the screen. Underscore-prefixed struct
 * fields are read-only to code outside this module.
 *
 * LIFECYCLE. construct -> initialize -> (update/render enable) -> ... -> (disable) -> deinitialize ->
 * deconstruct. Use Widget_InitializeWidget after construction and Widget_DeinitializeWidget before
 * Widget_Deconstruct so the ordering is applied in one place rather than copied around. A widget's
 * lifecycle is not restartable: once deinitialized it is done for good (though it may be added to and
 * removed from screens/parents any number of times between initialize and deinitialize).
 */


// Forward declarations.
/** @brief The UI screen/context a widget belongs to; full type in UIScreen.h. Borrowed. */
typedef struct UIScreenStruct UIScreen;
/** @brief The widget base itself; forward-declared so the struct can hold parent/subwidget pointers. */
typedef struct WidgetStruct Widget;


// Macros.
/** @brief Invalid widget id (a real id is >= 1). */
#define WIDGET_ID_INVALID ((uint64_t)0)
/** @brief Passed as the depth to Widget_GetSubWidgets to collect all descendants regardless of nesting. */
#define WIDGET_SUBWIDGET_DEPTH_ALL (~((size_t)0))
/** @brief First property id reserved for base properties; concrete property ids must be below this. */
#define WIDGET_BASE_PROPERTY_START ((int32_t)0x40000000)


// Types.
/**
 * @brief Base widget properties addressable through the generic property get/set (e.g. for animation).
 *
 * These ids live in a reserved high range (see WIDGET_BASE_PROPERTY_START) so they never collide with a
 * concrete widget's own property ids (which start at 0). Widget_GetProperty / Widget_SetProperty handle
 * them directly; unknown lower ids are forwarded to the concrete vtable.
 */
typedef enum WidgetBasePropertyEnum
{
    /** @brief The widget's parent-relative position; value type Vector2. */
    WidgetBaseProperty_Position = WIDGET_BASE_PROPERTY_START,
    /** @brief The widget's parent-relative size; value type Vector2. */
    WidgetBaseProperty_Size,
    /** @brief The widget's z layer; value type float. */
    WidgetBaseProperty_ZLayer,
    /** @brief The widget's render tint; value type RenderColor. */
    WidgetBaseProperty_RenderTint
} WidgetBaseProperty;

/**
 * @brief Virtual table of behavior for a concrete widget.
 *
 * A concrete type supplies one static instance. Every slot receives the widget's own object as @c self
 * (a @c void* recovered without a cast). Every slot is OPTIONAL: a NULL slot means "no behavior" — the
 * corresponding Widget_* wrapper does nothing and succeeds (for property slots, the property is treated
 * as unhandled). A slot returning a non-success Error aborts the operation, and the screen propagates it.
 */
typedef struct WidgetVTableStruct
{
    /** @brief Initializes the concrete widget after construction (called by Widget_InitializeWidget). Optional. */
    Error (*Initialize)(void* self);
    /** @brief Deinitializes the concrete widget before deconstruction (called by Widget_DeinitializeWidget). Optional. */
    Error (*Deinitialize)(void* self);
    /** @brief Releases the concrete widget's own resources during Widget_Deconstruct. Must NOT free the widget
     *         or touch the base (the framework does that). Optional. */
    Error (*OnDeconstruct)(void* self);

    /** @brief Advances the widget's logic by one update tick. Optional. */
    Error (*Update)(void* self, ProgramTime time);
    /** @brief Draws the widget through its widget-local render context. Optional. */
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
    /** @brief Called when any mouse input is directed at the widget (click, drag, scroll). Optional. */
    Error (*OnMouseInput)(void* self, const WidgetMouseInputArgs* args);
    /** @brief Called when any keyboard input is directed at the widget. Optional. */
    Error (*OnKeyboardInput)(void* self, const WidgetKeyboardInputArgs* args);
    /** @brief Called when the widget becomes focused in its screen. Optional. */
    Error (*OnFocusEnable)(void* self);
    /** @brief Called when the widget stops being focused. Optional. */
    Error (*OnFocusDisable)(void* self);
    /** @brief Called when the widget becomes tabbed (keyboard-selected). Optional. */
    Error (*OnTabbedEnable)(void* self);
    /** @brief Called when the widget stops being tabbed. Optional. */
    Error (*OnTabbedDisable)(void* self);
    /** @brief Called when the widget becomes active (hovered or tabbed). Optional. */
    Error (*OnActiveEnable)(void* self);
    /** @brief Called when the widget stops being active. Optional. */
    Error (*OnActiveDisable)(void* self);

    /** @brief Reads a concrete property (id < WIDGET_BASE_PROPERTY_START) into @p outValue. Optional. */
    Error (*GetProperty)(void* self, int32_t propertyId, void* outValue);
    /** @brief Writes a concrete property (id < WIDGET_BASE_PROPERTY_START) from @p value. Optional. */
    Error (*SetProperty)(void* self, int32_t propertyId, const void* value);
} WidgetVTable;

/**
 * @brief Abstract base of every widget: its vtable plus identity, geometry, flags, state and subwidgets.
 *
 * Embed as the FIRST member of a concrete widget. All fields are managed through the Widget_* functions;
 * underscore-prefixed fields are read-only to code outside this module. Initialize with Widget_Construct
 * (from the type's constructor) and release with Widget_Deconstruct.
 */
typedef struct WidgetStruct
{
    /** @brief Behavior table for this widget; set by Widget_Construct. Never NULL after construction. */
    const WidgetVTable* VTable;

    /** @brief The screen this widget belongs to; borrowed, set at construction, read-only thereafter. */
    UIScreen* _screen;
    /** @brief The parent widget, or NULL if this is a root / detached. */
    Widget* _parent;
    /** @brief Unique widget id within the screen; minted at construction, read-only. 0 is invalid. */
    uint64_t _id;
    /** @brief The widget's registered factory type id; set at construction, read-only. */
    uint64_t _typeId;

    /** @brief Parent-relative position, normalized [0;1]. */
    Vector2 _position;
    /** @brief Parent-relative size, normalized [0;1]. */
    Vector2 _size;
    /** @brief Z layer relative to the parent/screen; higher renders on top. */
    float _zLayer;
    /** @brief Render tint multiplied into this widget's (and its subtree's) drawing; defaults to white. */
    RenderColor _renderTint;

    /** @brief When true (default) the widget is rendered. */
    bool _isRendered;
    /** @brief When true (default) the widget receives update ticks. */
    bool _isUpdated;
    /** @brief When true (default) the widget receives input and participates in hit testing. */
    bool _isInputEnabled;
    /** @brief When true, subwidget rendering and input are clipped to this widget's box. */
    bool _areSubWidgetsCut;
    /** @brief True while this widget is a top-level widget of its screen (set by the screen). */
    bool _isScreenRoot;

    /** @brief True while the mouse hovers the widget. */
    bool _isHovered;
    /** @brief True while the widget is the focused (front-most) widget of its screen. */
    bool _isFocused;
    /** @brief True while the widget is tabbed (keyboard-selected). */
    bool _isTabbed;
    /** @brief True while the widget is active (hovered or tabbed); derived from the two. */
    bool _isActive;

    /** @brief Cached estimated bounds (parent space), valid only when _areBoundsValid is true. */
    Rectangle _cachedBounds;
    /** @brief True when _cachedBounds is up to date. */
    bool _areBoundsValid;

    /** @brief Owned buffer of Widget* subwidgets (child pointers are borrowed, not owned). */
    GenericBuffer _subWidgets;

    /** @brief Raised when the hover state changes; payload is this Widget*. */
    WREvent _onHoverStateChange;
    /** @brief Raised when the focus state changes; payload is this Widget*. */
    WREvent _onFocusStateChange;
    /** @brief Raised when the tab state changes; payload is this Widget*. */
    WREvent _onTabStateChange;
    /** @brief Raised when the active state changes; payload is this Widget*. */
    WREvent _onActiveStateChange;
} Widget;


// Lifecycle (called by concrete types and the framework, not end users directly).
/**
 * @brief Initializes the abstract base of a widget, minting its id and registering it with the screen.
 *
 * Sets the vtable, screen and type id; defaults geometry (position 0, size 0), z 0, tint white, the
 * render/update/input flags to true (without firing the enable hooks — that happens in
 * Widget_InitializeWidget) and all state flags to false; constructs the four state-change events; and
 * asks the screen for the next unique id, registering the widget. Call first inside a type's constructor,
 * then fill concrete fields.
 * @param self The base to initialize (usually &concrete->Base); must not be NULL.
 * @param vtable The concrete type's behavior table; must not be NULL.
 * @param screen The owning screen; must not be NULL.
 * @param typeId The widget's registered type id; must not be 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self, @p vtable or @p screen is NULL or @p typeId is 0;
 *          propagates event-construction and screen-registration errors.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_Construct(Widget* self, const WidgetVTable* vtable, UIScreen* screen, uint64_t typeId);

/**
 * @brief Deconstructs a widget: releases concrete + base resources and returns its storage to the pool.
 *
 * Runs the concrete OnDeconstruct hook, detaches the widget from its parent (if any), unregisters it from
 * the screen (clearing focus/tab/animations that referenced it), deconstructs the state-change events and
 * the subwidget buffer, then returns the widget's storage to its type's pool via the factory. After this
 * the widget must not be used. Deinitialize the widget (Widget_DeinitializeWidget) before calling this.
 * Best-effort: the first error is returned and later ones are released. Safe on NULL.
 * @param self The widget to deconstruct, or NULL.
 * @returns Success (including the NULL case), or the first non-success Error encountered.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_Deconstruct(Widget* self);

/**
 * @brief Runs the widget's initialize step and enables update, render and input (firing their enable hooks).
 *
 * The non-instance half of the lifecycle: calls Initialize, then the OnUpdateEnable, OnRenderEnable and
 * OnInputEnable hooks (the flags are enabled by default). Call once after construction, before the widget
 * is first added to a screen or parent.
 * @param self The widget; must not be NULL.
 * @returns Success, or the first non-success Error from a hook.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_InitializeWidget(Widget* self);

/**
 * @brief The inverse of Widget_InitializeWidget: disables input/render/update then runs deinitialize.
 *
 * Fires the disable hooks for whichever of input/render/update are still enabled (in reverse of the enable
 * order), clearing those flags, then calls Deinitialize. Call once when the widget's use has ended, before
 * Widget_Deconstruct. The lifecycle does not restart afterwards.
 * @param self The widget; must not be NULL.
 * @returns Success, or the first non-success Error from a hook.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_DeinitializeWidget(Widget* self);


// Framework-driven wrappers (the screen calls these; call sites go through them, not the vtable directly).
/**
 * @brief Updates the widget (calls the Update vtable slot if present).
 * @param self The widget; must not be NULL.
 * @param time The update-tick time.
 * @returns The slot's result, or success if there is no Update slot.
 */
Error Widget_Update(Widget* self, ProgramTime time);

/**
 * @brief Renders the widget (calls the Render vtable slot if present).
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
 * @returns The slot's result, or success if there is no OnKeyboardInput slot.
 */
Error Widget_OnKeyboardInput(Widget* self, const WidgetKeyboardInputArgs* args);


// Identity and references (never fail).
/**
 * @brief Returns the widget's unique id (0 if unconstructed).
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
 * @brief Returns whether the widget is currently a top-level widget of its screen.
 * @param self The widget; must not be NULL.
 * @returns true if the widget is a screen root.
 */
static inline bool Widget_IsScreenRoot(const Widget* self)
{
    return self->_isScreenRoot;
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
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if a
 *          component is NaN or infinite.
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
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if a
 *          component is NaN or infinite.
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
 * @brief Sets the widget's z layer (higher renders on top; relative to the parent/screen).
 * @param self The widget; must not be NULL.
 * @param zLayer The new z layer; must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p zLayer is NaN or infinite.
 */
Error Widget_SetZLayer(Widget* self, float zLayer);

/**
 * @brief Returns the widget's estimated bounds (parent space), enclosing the widget and all subwidgets.
 *
 * The bounds are cached and lazily recomputed when invalidated (by a geometry or subwidget change on this
 * widget or a descendant), which is why @p self is non-const.
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
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          Brightness or Opacity is NaN or infinite.
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
 * @brief Returns whether the widget receives input and participates in hit testing.
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
 * A disabled widget receives no hover/click/keyboard input and is skipped when the screen searches for the
 * top-most widget at a position (input passes through it).
 * @param self The widget; must not be NULL.
 * @param isInputEnabled The new input flag.
 * @returns Success, or a non-success Error propagated from the fired hook.
 */
Error Widget_SetIsInputEnabled(Widget* self, bool isInputEnabled);

/**
 * @brief Returns whether subwidgets are clipped to this widget's box (for rendering and input).
 * @param self The widget; must not be NULL.
 * @returns true if subwidgets are cut.
 */
static inline bool Widget_AreSubWidgetsCut(const Widget* self)
{
    return self->_areSubWidgetsCut;
}

/**
 * @brief Sets whether subwidgets are clipped to this widget's box (affects rendering and input).
 * @param self The widget; must not be NULL.
 * @param areSubWidgetsCut The new clip flag.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error Widget_SetAreSubWidgetsCut(Widget* self, bool areSubWidgetsCut);


// Interaction state (getters; the setters below are driven by the screen).
/**
 * @brief Returns whether the widget is hovered by the mouse.
 * @param self The widget; must not be NULL.
 * @returns true if hovered.
 */
static inline bool Widget_IsHovered(const Widget* self)
{
    return self->_isHovered;
}

/**
 * @brief Returns whether the widget is focused (the front-most widget of its screen).
 * @param self The widget; must not be NULL.
 * @returns true if focused.
 */
static inline bool Widget_IsFocused(const Widget* self)
{
    return self->_isFocused;
}

/**
 * @brief Returns whether the widget is tabbed (keyboard-selected).
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


// Screen-driven state changes (intended for the owning screen; not general-purpose setters).
/**
 * @brief Sets the widget's hover state, firing OnHoverStart/OnHoverEnd, raising OnHoverStateChange, and
 *        recomputing the active state. Intended for the screen.
 * @param self The widget; must not be NULL.
 * @param isHovered The new hover state.
 * @param args The hover arguments (position) used when @p isHovered is true; may be NULL when false.
 * @returns Success, or the first non-success Error from a hook/event.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_SetHovered(Widget* self, bool isHovered, const WidgetHoverArgs* args);

/**
 * @brief Sets the widget's focus state, firing OnFocusEnable/OnFocusDisable and raising OnFocusStateChange.
 *        Intended for the screen.
 * @param self The widget; must not be NULL.
 * @param isFocused The new focus state.
 * @returns Success, or the first non-success Error from a hook/event.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_SetFocused(Widget* self, bool isFocused);

/**
 * @brief Sets the widget's tab state, firing OnTabbedEnable/OnTabbedDisable, raising OnTabStateChange, and
 *        recomputing the active state. Intended for the screen.
 * @param self The widget; must not be NULL.
 * @param isTabbed The new tab state.
 * @returns Success, or the first non-success Error from a hook/event.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_SetTabbed(Widget* self, bool isTabbed);

/**
 * @brief Marks the widget as a screen root or not. Intended for the screen's add/remove functions.
 * @param self The widget; must not be NULL.
 * @param isScreenRoot The new root flag.
 */
void Widget_SetScreenRoot(Widget* self, bool isScreenRoot);


// Subwidgets.
/**
 * @brief Adds a subwidget (child) to this widget.
 *
 * The child must belong to the same screen, must not be this widget or any of its ancestors (no cycles),
 * and must not already be attached (no parent and not a screen root). The child is borrowed, not owned.
 * Invalidates cached bounds up the tree.
 * @param self The widget; must not be NULL.
 * @param child The subwidget to add; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p child is NULL, they differ in screen, or
 *          @p child is this widget or an ancestor; ErrorCode_InvalidOperation if @p child is already
 *          attached or could not be stored.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_AddSubWidget(Widget* self, Widget* child);

/**
 * @brief Removes a subwidget (child) from this widget. Does not deconstruct the child.
 * @param self The widget; must not be NULL.
 * @param child The subwidget to remove; must not be NULL and must be a current child.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p child is NULL; ErrorCode_InvalidOperation if
 *          @p child is not a current subwidget.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_RemoveSubWidget(Widget* self, Widget* child);

/**
 * @brief Appends the ids of this widget's descendants to a buffer, down to a given depth.
 *
 * Depth 0 appends only the immediate children; depth N appends descendants up to N levels below the
 * immediate children; WIDGET_SUBWIDGET_DEPTH_ALL appends all descendants. Does not clear the buffer first
 * (append semantics).
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
 * @brief Returns the immediate subwidget at a given index (for traversal).
 * @param self The widget; must not be NULL.
 * @param index Index in [0, Widget_GetSubWidgetCount).
 * @returns The borrowed subwidget, or NULL if @p index is out of range.
 */
Widget* Widget_GetSubWidgetAt(const Widget* self, size_t index);

/**
 * @brief Sorts this widget's immediate subwidgets into ascending z order, in place.
 *
 * Used by the screen for z-ordered rendering and hit testing. The relative order of subwidgets is only
 * meaningful by z (ties are unspecified), so this reorder is semantically neutral and cannot fail.
 * @param self The widget; must not be NULL.
 */
void Widget_SortSubWidgetsByZ(Widget* self);


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


// Generic property access (used by animation and generic tooling).
/**
 * @brief Reads a property value into @p outValue.
 *
 * Ids at or above WIDGET_BASE_PROPERTY_START are handled by the base (see WidgetBaseProperty); lower ids
 * are forwarded to the concrete GetProperty slot. @p outValue must be large enough for the property's type.
 * @param self The widget; must not be NULL.
 * @param propertyId The property id.
 * @param outValue [out] Destination for the value; must not be NULL and large enough for the property type.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outValue is NULL; ErrorCode_InvalidOperation
 *          if the id is unknown / unhandled.
 */
Error Widget_GetProperty(Widget* self, int32_t propertyId, void* outValue);

/**
 * @brief Writes a property value from @p value.
 *
 * Ids at or above WIDGET_BASE_PROPERTY_START are handled by the base (see WidgetBaseProperty) through the
 * validating base setters; lower ids are forwarded to the concrete SetProperty slot.
 * @param self The widget; must not be NULL.
 * @param propertyId The property id.
 * @param value The value to write; must not be NULL and must point to the property's type.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p value is NULL; ErrorCode_InvalidOperation if
 *          the id is unknown / unhandled; propagates validation errors from the base setters.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Widget_SetProperty(Widget* self, int32_t propertyId, const void* value);


// Events.
/**
 * @brief Returns the widget's hover-state-change event (subscribe to observe hover transitions).
 * @param self The widget; must not be NULL.
 * @returns A pointer to the event; payload on raise is this Widget*.
 */
static inline WREvent* Widget_GetOnHoverStateChange(Widget* self)
{
    return &self->_onHoverStateChange;
}

/**
 * @brief Returns the widget's focus-state-change event.
 * @param self The widget; must not be NULL.
 * @returns A pointer to the event; payload on raise is this Widget*.
 */
static inline WREvent* Widget_GetOnFocusStateChange(Widget* self)
{
    return &self->_onFocusStateChange;
}

/**
 * @brief Returns the widget's tab-state-change event.
 * @param self The widget; must not be NULL.
 * @returns A pointer to the event; payload on raise is this Widget*.
 */
static inline WREvent* Widget_GetOnTabStateChange(Widget* self)
{
    return &self->_onTabStateChange;
}

/**
 * @brief Returns the widget's active-state-change event.
 * @param self The widget; must not be NULL.
 * @returns A pointer to the event; payload on raise is this Widget*.
 */
static inline WREvent* Widget_GetOnActiveStateChange(Widget* self)
{
    return &self->_onActiveStateChange;
}
