#include "UIWidget.h"
#include "UIInternal.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include <inttypes.h>
#include "wr/WRMemory.h"
#include "wr/WRMath.h"
#include "wr/WRCompile.h"


// Fields.
/* The properties every widget has. */
static const WidgetPropertyDescriptor BaseProperties[] =
{
    { .Id = WidgetBaseProperty_Position, .Type = UIPropertyType_Vector2, .Name = u8"position" },
    { .Id = WidgetBaseProperty_Size, .Type = UIPropertyType_Vector2, .Name = u8"size" },
    { .Id = WidgetBaseProperty_ZLayer, .Type = UIPropertyType_Float, .Name = u8"z_layer" },
    { .Id = WidgetBaseProperty_RenderTint, .Type = UIPropertyType_RenderColor, .Name = u8"render_tint" },
};


// Static functions: helpers.
static bool IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

static bool IsVector2Finite(Vector2 value)
{
    return IsFloatFinite(value.x) && IsFloatFinite(value.y);
}

/* Keeps the first non-success error and releases any later one, for best-effort teardown. */
static void KeepFirstError(Error* first, Error* candidate)
{
    if ((candidate->Code != ErrorCode_Success) && (first->Code == ErrorCode_Success))
    {
        *first = *candidate;
    }
    else
    {
        Error_Deconstruct(candidate);
    }
}

static Widget* GetChildAt(const Widget* widget, size_t index)
{
    return *(Widget**)GenericBuffer_GetPointerToElement((GenericBuffer*)&widget->_subWidgets, index);
}

/* Returns true if @p candidate is @p widget itself or one of its ancestors (used to reject cycles). */
static bool IsSelfOrAncestor(const Widget* candidate, const Widget* widget)
{
    for (const Widget* Current = widget; Current != NULL; Current = Current->_parent)
    {
        if (Current == candidate)
        {
            return true;
        }
    }
    return false;
}

/* Marks this widget's cached bounds and every ancestor's as stale. */
static void InvalidateBoundsUpTree(Widget* widget)
{
    for (Widget* Current = widget; Current != NULL; Current = Current->_parent)
    {
        Current->_areBoundsValid = false;
    }
}

static Rectangle UnionRectangle(Rectangle a, Rectangle b)
{
    float MinX = Math_MinFloat(a.x, b.x);
    float MinY = Math_MinFloat(a.y, b.y);
    float MaxX = Math_MaxFloat(a.x + a.width, b.x + b.width);
    float MaxY = Math_MaxFloat(a.y + a.height, b.y + b.height);
    return (Rectangle){ .x = MinX, .y = MinY, .width = MaxX - MinX, .height = MaxY - MinY };
}

/* The widget's own box, in its parent's space. */
static Rectangle GetOwnBox(const Widget* self)
{
    return (Rectangle){ .x = self->_position.x, .y = self->_position.y, .width = self->_size.x, .height = self->_size.y };
}

/* Grows @p bounds (parent space) to enclose a child's bounds, given in this widget's local space. */
static Rectangle AddChildBounds(const Widget* self, Rectangle bounds, Rectangle childBounds)
{
    Rectangle Mapped =
    {
        .x = self->_position.x + (childBounds.x * self->_size.x),
        .y = self->_position.y + (childBounds.y * self->_size.y),
        .width = childBounds.width * self->_size.x,
        .height = childBounds.height * self->_size.y
    };
    return UnionRectangle(bounds, Mapped);
}

/* Recomputes and caches this widget's estimated bounds (parent space), enclosing it and all subwidgets. Recursion is
 * bounded by WIDGET_MAX_TREE_DEPTH. */
static void ComputeBounds(Widget* self)
{
    Rectangle Bounds = GetOwnBox(self);
    for (size_t Index = 0; Index < self->_subWidgets._count; Index++)
    {
        Bounds = AddChildBounds(self, Bounds, Widget_GetEstimatedBounds(GetChildAt(self, Index)));
    }
    self->_cachedBounds = Bounds;
    self->_areBoundsValid = true;
}

/* Queues a state change. Always called right after the field changes and before the hook runs: a hook that changes the
 * state again queues its own change after this one, so the last notification always tells the final state. */
static void PostStateEvent(Widget* self, UIEventKind kind, bool isSet)
{
    UIEvent Event =
    {
        .Kind = kind,
        .WidgetId = self->_id,
        .WidgetTypeId = self->_typeId,
        .IsSet = isSet,
        .WidgetEventId = 0,
        .HasValue = false
    };
    UIScreen_InternalPostEvent(self->_screen, &Event); // Fails only when memory runs out; the state is set either way.
}

/* Recomputes the derived active state (hovered or tabbed) and fires its hook and notification on a change. */
static Error UpdateActive(Widget* self)
{
    bool IsActiveNow = self->_isHovered || self->_isTabbed;
    if ((IsActiveNow == self->_isActive) || self->_isDying)
    {
        return Error_CreateSuccess();
    }

    self->_isActive = IsActiveNow;
    PostStateEvent(self, UIEventKind_ActiveChanged, IsActiveNow);
    Error (*Hook)(void*) = IsActiveNow ? self->VTable->OnActiveEnable : self->VTable->OnActiveDisable;
    return (Hook != NULL) ? Hook(self) : Error_CreateSuccess();
}

/* Runs a flag's enable or disable hook, if the widget is initialized (before that the flags are just set). */
static Error RunFlagHook(Widget* self, bool isEnabled, Error (*enableHook)(void*), Error (*disableHook)(void*))
{
    Error (*Hook)(void*) = isEnabled ? enableHook : disableHook;
    if (!self->_isInitialized || (Hook == NULL))
    {
        return Error_CreateSuccess();
    }
    return Hook(self);
}

/* Recursively appends a widget's descendant ids to @p out, down to @p depth levels below the immediate children. */
static Error AppendDescendants(const Widget* widget, GenericBuffer* out, size_t depth)
{
    for (size_t Index = 0; Index < widget->_subWidgets._count; Index++)
    {
        Widget* Child = GetChildAt(widget, Index);
        uint64_t Id = Child->_id;
        if (!GenericBuffer_AddLast(out, &Id))
        {
            return Error_Construct2(ErrorCode_BufferTooLarge, "Widget_GetSubWidgets: output buffer could not grow.");
        }
        if ((depth == WIDGET_SUBWIDGET_DEPTH_ALL) || (depth > 0U))
        {
            Error RecurseResult = AppendDescendants(Child, out,
                (depth == WIDGET_SUBWIDGET_DEPTH_ALL) ? WIDGET_SUBWIDGET_DEPTH_ALL : (depth - 1U));
            if (RecurseResult.Code != ErrorCode_Success)
            {
                return RecurseResult;
            }
        }
    }
    return Error_CreateSuccess();
}

/* Levels from the widget up to its top-most ancestor; 1 for a root or a detached widget. */
static size_t GetDepth(const Widget* widget)
{
    size_t Depth = 0;
    for (const Widget* Current = widget; Current != NULL; Current = Current->_parent)
    {
        Depth++;
    }
    return Depth;
}

/* Levels of the subtree under the widget, itself included. Recursion is bounded because every tree already respects
 * WIDGET_MAX_TREE_DEPTH. */
static size_t GetSubtreeHeight(const Widget* widget)
{
    size_t Highest = 0;
    for (size_t Index = 0; Index < widget->_subWidgets._count; Index++)
    {
        size_t ChildHeight = GetSubtreeHeight(GetChildAt(widget, Index));
        Highest = (ChildHeight > Highest) ? ChildHeight : Highest;
    }
    return Highest + 1U;
}

/* Whether @p a is drawn before @p b among siblings: lower z first, then earlier insertion. */
static bool IsDrawnBefore(const Widget* a, const Widget* b)
{
    if (a->_zLayer < b->_zLayer)
    {
        return true;
    }
    if (a->_zLayer > b->_zLayer)
    {
        return false;
    }
    return a->_sequence < b->_sequence;
}

static bool TryGetBasePropertyType(int32_t propertyId, UIPropertyType* outType)
{
    for (size_t Index = 0; Index < (sizeof(BaseProperties) / sizeof(BaseProperties[0])); Index++)
    {
        if (BaseProperties[Index].Id == propertyId)
        {
            *outType = BaseProperties[Index].Type;
            return true;
        }
    }
    return false;
}

/* Clears the screen's focus when this widget holds it but can no longer take it. */
static Error ReleaseFocusIfIneligible(Widget* self)
{
    bool IsFocusedWidget = (UIScreen_GetFocusedWidgetId(self->_screen) == self->_id) && (self->_id != WIDGET_ID_INVALID);
    if (!IsFocusedWidget || self->_isDying || UIScreen_InternalCanFocus(self))
    {
        return Error_CreateSuccess();
    }
    return UIScreen_InternalSetFocus(self->_screen, WIDGET_ID_INVALID);
}


// Internal functions (declared in UIInternal.h).
void Widget_InternalInitializeBase(Widget* self, const WidgetVTable* vtable, UIScreen* screen, uint64_t typeId,
    bool isFocusable)
{
    self->VTable = vtable;
    self->_screen = screen;
    self->_parent = NULL;
    self->_id = WIDGET_ID_INVALID;
    self->_typeId = typeId;
    self->_sequence = 0;

    self->_position = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_size = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_zLayer = 0.0f;
    self->_renderTint = RenderColor_White();

    self->_isRendered = true;
    self->_isUpdated = true;
    self->_isInputEnabled = true;
    self->_areSubWidgetsCut = false;
    self->_isScreenRoot = false;
    self->_isFocusable = isFocusable;

    self->_isHovered = false;
    self->_isFocused = false;
    self->_isTabbed = false;
    self->_isActive = false;

    self->_isInitialized = false;
    self->_isDying = false;

    self->_cachedBounds = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f };
    self->_areBoundsValid = false;

    // Capacity 0: most widgets never get children, and these allocate on the first one.
    GenericBuffer_AllocateVariable(&self->_subWidgets, 0U, sizeof(Widget*));
    GenericBuffer_AllocateVariable(&self->_drawOrder, 0U, sizeof(Widget*));
}

void Widget_InternalReleaseBase(Widget* self)
{
    Memory_Free(self->_subWidgets._data);
    Memory_Free(self->_drawOrder._data);
    Memory_Zero(&self->_subWidgets, sizeof(self->_subWidgets));
    Memory_Zero(&self->_drawOrder, sizeof(self->_drawOrder));
}

Error Widget_InternalRunTeardownHooks(Widget* self)
{
    Error FirstError = Error_CreateSuccess();
    if (self->_isInitialized)
    {
        // The reverse of the enable order in Widget_InitializeWidget.
        if (self->_isInputEnabled && (self->VTable->OnInputDisable != NULL))
        {
            Error HookResult = self->VTable->OnInputDisable(self);
            KeepFirstError(&FirstError, &HookResult);
        }
        if (self->_isRendered && (self->VTable->OnRenderDisable != NULL))
        {
            Error HookResult = self->VTable->OnRenderDisable(self);
            KeepFirstError(&FirstError, &HookResult);
        }
        if (self->_isUpdated && (self->VTable->OnUpdateDisable != NULL))
        {
            Error HookResult = self->VTable->OnUpdateDisable(self);
            KeepFirstError(&FirstError, &HookResult);
        }
        self->_isInputEnabled = false;
        self->_isRendered = false;
        self->_isUpdated = false;
        if (self->VTable->Deinitialize != NULL)
        {
            Error HookResult = self->VTable->Deinitialize(self);
            KeepFirstError(&FirstError, &HookResult);
        }
        self->_isInitialized = false;
    }
    if (self->VTable->OnDeconstruct != NULL)
    {
        Error HookResult = self->VTable->OnDeconstruct(self);
        KeepFirstError(&FirstError, &HookResult);
    }
    return FirstError;
}

Error Widget_InternalSetHovered(Widget* self, bool isHovered, const WidgetHoverArgs* args)
{
    if ((isHovered == self->_isHovered) || self->_isDying)
    {
        return Error_CreateSuccess();
    }

    self->_isHovered = isHovered;
    PostStateEvent(self, UIEventKind_HoverChanged, isHovered);
    Error HookResult = Error_CreateSuccess();
    if (isHovered && (self->VTable->OnHoverStart != NULL))
    {
        HookResult = self->VTable->OnHoverStart(self, args);
    }
    else if (!isHovered && (self->VTable->OnHoverEnd != NULL))
    {
        HookResult = self->VTable->OnHoverEnd(self);
    }
    if (HookResult.Code != ErrorCode_Success)
    {
        return HookResult;
    }
    return UpdateActive(self);
}

Error Widget_InternalSetFocused(Widget* self, bool isFocused)
{
    if ((isFocused == self->_isFocused) || self->_isDying)
    {
        return Error_CreateSuccess();
    }

    self->_isFocused = isFocused;
    PostStateEvent(self, UIEventKind_FocusChanged, isFocused);
    Error (*Hook)(void*) = isFocused ? self->VTable->OnFocusEnable : self->VTable->OnFocusDisable;
    return (Hook != NULL) ? Hook(self) : Error_CreateSuccess();
}

Error Widget_InternalSetTabbed(Widget* self, bool isTabbed)
{
    if ((isTabbed == self->_isTabbed) || self->_isDying)
    {
        return Error_CreateSuccess();
    }

    self->_isTabbed = isTabbed;
    PostStateEvent(self, UIEventKind_TabChanged, isTabbed);
    Error (*Hook)(void*) = isTabbed ? self->VTable->OnTabbedEnable : self->VTable->OnTabbedDisable;
    if (Hook != NULL)
    {
        Error HookResult = Hook(self);
        if (HookResult.Code != ErrorCode_Success)
        {
            return HookResult;
        }
    }
    return UpdateActive(self);
}

void Widget_InternalSetScreenRoot(Widget* self, bool isScreenRoot, uint64_t sequence)
{
    self->_isScreenRoot = isScreenRoot;
    if (isScreenRoot)
    {
        self->_sequence = sequence;
    }
}

void Widget_InternalUnlinkFromParent(Widget* child)
{
    Widget* Parent = child->_parent;
    if (Parent == NULL)
    {
        return;
    }
    // From the back: a dying subtree is torn down last child first.
    for (size_t Index = Parent->_subWidgets._count; Index-- > 0U;)
    {
        if (GetChildAt(Parent, Index) == child)
        {
            GenericBuffer_RemoveAt(&Parent->_subWidgets, Index);
            break;
        }
    }
    Widget_InternalRemoveFromDrawOrder(&Parent->_drawOrder, child);
    child->_parent = NULL;
    InvalidateBoundsUpTree(Parent);
}

bool Widget_InternalInsertIntoDrawOrder(GenericBuffer* drawOrder, Widget* widget)
{
    // Binary search for the first widget drawn after this one, so the list stays sorted without re-sorting.
    size_t Low = 0;
    size_t High = drawOrder->_count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (IsDrawnBefore(widget, Widget_InternalGetDrawOrderAt(drawOrder, Middle)))
        {
            High = Middle;
        }
        else
        {
            Low = Middle + 1U;
        }
    }
    return GenericBuffer_Insert(drawOrder, &widget, Low);
}

void Widget_InternalRemoveFromDrawOrder(GenericBuffer* drawOrder, const Widget* widget)
{
    for (size_t Index = 0; Index < drawOrder->_count; Index++)
    {
        if (Widget_InternalGetDrawOrderAt(drawOrder, Index) == widget)
        {
            GenericBuffer_RemoveAt(drawOrder, Index);
            return;
        }
    }
}

void Widget_InternalResortInDrawOrder(GenericBuffer* drawOrder, Widget* widget)
{
    Widget_InternalRemoveFromDrawOrder(drawOrder, widget);
    // Cannot fail: the removal left room for the widget.
    Widget_InternalInsertIntoDrawOrder(drawOrder, widget);
}

Widget* Widget_InternalGetDrawOrderAt(const GenericBuffer* drawOrder, size_t index)
{
    return *(Widget**)GenericBuffer_GetPointerToElement((GenericBuffer*)drawOrder, index);
}

Rectangle Widget_InternalPeekEstimatedBounds(const Widget* self)
{
    if (self->_areBoundsValid)
    {
        return self->_cachedBounds;
    }
    Rectangle Bounds = GetOwnBox(self);
    for (size_t Index = 0; Index < self->_subWidgets._count; Index++)
    {
        Bounds = AddChildBounds(self, Bounds, Widget_InternalPeekEstimatedBounds(GetChildAt(self, Index))); // Bounded by the tree depth.
    }
    return Bounds;
}


// Public functions: lifecycle.
Error Widget_InitializeWidget(Widget* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_InitializeWidget: self must not be NULL.");
    }
    if (self->_isInitialized || self->_isDying)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "Widget_InitializeWidget: the widget is already initialized or is being destroyed.");
    }

    if (self->VTable->Initialize != NULL)
    {
        Error InitializeResult = self->VTable->Initialize(self);
        if (InitializeResult.Code != ErrorCode_Success)
        {
            return InitializeResult;
        }
    }
    self->_isInitialized = true;

    if (self->_isUpdated && (self->VTable->OnUpdateEnable != NULL))
    {
        Error HookResult = self->VTable->OnUpdateEnable(self);
        if (HookResult.Code != ErrorCode_Success)
        {
            return HookResult;
        }
    }
    if (self->_isRendered && (self->VTable->OnRenderEnable != NULL))
    {
        Error HookResult = self->VTable->OnRenderEnable(self);
        if (HookResult.Code != ErrorCode_Success)
        {
            return HookResult;
        }
    }
    if (self->_isInputEnabled && (self->VTable->OnInputEnable != NULL))
    {
        Error HookResult = self->VTable->OnInputEnable(self);
        if (HookResult.Code != ErrorCode_Success)
        {
            return HookResult;
        }
    }
    return Error_CreateSuccess();
}

Error Widget_Destroy(Widget* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }
    return UIScreen_InternalDestroyWidget(self->_screen, self);
}


// Public functions: framework-driven wrappers.
Error Widget_Update(Widget* self, ProgramTime time)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_Update: self must not be NULL.");
    }
    return (self->VTable->Update != NULL) ? self->VTable->Update(self, time) : Error_CreateSuccess();
}

Error Widget_Layout(Widget* self, const UILayoutContext* context)
{
    if ((self == NULL) || (context == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_Layout: self and context must not be NULL.");
    }
    return (self->VTable->Layout != NULL) ? self->VTable->Layout(self, context) : Error_CreateSuccess();
}

Error Widget_Render(Widget* self, UIRenderContext* context)
{
    if ((self == NULL) || (context == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_Render: self and context must not be NULL.");
    }
    return (self->VTable->Render != NULL) ? self->VTable->Render(self, context) : Error_CreateSuccess();
}

Error Widget_OnMouseInput(Widget* self, const WidgetMouseInputArgs* args)
{
    if ((self == NULL) || (args == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_OnMouseInput: self and args must not be NULL.");
    }
    return (self->VTable->OnMouseInput != NULL) ? self->VTable->OnMouseInput(self, args) : Error_CreateSuccess();
}

Error Widget_OnKeyboardInput(Widget* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    if ((self == NULL) || (args == NULL) || (outIsHandled == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_OnKeyboardInput: self, args and outIsHandled must not be NULL.");
    }
    *outIsHandled = false;
    return (self->VTable->OnKeyboardInput != NULL) ? self->VTable->OnKeyboardInput(self, args, outIsHandled)
        : Error_CreateSuccess();
}


// Public functions: geometry.
Error Widget_SetPosition(Widget* self, Vector2 position)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetPosition: self must not be NULL.");
    }
    if (!IsVector2Finite(position))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "Widget_SetPosition: position components must be finite.");
    }
    self->_position = position;
    InvalidateBoundsUpTree(self);
    UIScreen_InternalInvalidateHitTest(self->_screen);
    return Error_CreateSuccess();
}

Error Widget_SetSize(Widget* self, Vector2 size)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetSize: self must not be NULL.");
    }
    if (!IsVector2Finite(size))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "Widget_SetSize: size components must be finite.");
    }
    self->_size = size;
    InvalidateBoundsUpTree(self);
    UIScreen_InternalInvalidateHitTest(self->_screen);
    return Error_CreateSuccess();
}

Error Widget_SetZLayer(Widget* self, float zLayer)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetZLayer: self must not be NULL.");
    }
    if (!IsFloatFinite(zLayer))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "Widget_SetZLayer: zLayer must be finite.");
    }
    if (zLayer == self->_zLayer)
    {
        return Error_CreateSuccess();
    }
    self->_zLayer = zLayer;
    UIScreen_InternalOnZChanged(self->_screen, self);
    return Error_CreateSuccess();
}

Rectangle Widget_GetEstimatedBounds(Widget* self)
{
    if (!self->_areBoundsValid)
    {
        ComputeBounds(self);
    }
    return self->_cachedBounds;
}


// Public functions: rendering and behavior flags.
Error Widget_SetRenderTint(Widget* self, RenderColor tint)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetRenderTint: self must not be NULL.");
    }
    if (!IsFloatFinite(tint.Brightness) || !IsFloatFinite(tint.Opacity))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "Widget_SetRenderTint: brightness and opacity must be finite.");
    }
    self->_renderTint = tint;
    return Error_CreateSuccess();
}

Error Widget_SetIsRendered(Widget* self, bool isRendered)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetIsRendered: self must not be NULL.");
    }
    if (isRendered == self->_isRendered)
    {
        return Error_CreateSuccess();
    }
    self->_isRendered = isRendered;
    return RunFlagHook(self, isRendered, self->VTable->OnRenderEnable, self->VTable->OnRenderDisable);
}

Error Widget_SetIsUpdated(Widget* self, bool isUpdated)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetIsUpdated: self must not be NULL.");
    }
    if (isUpdated == self->_isUpdated)
    {
        return Error_CreateSuccess();
    }
    self->_isUpdated = isUpdated;
    return RunFlagHook(self, isUpdated, self->VTable->OnUpdateEnable, self->VTable->OnUpdateDisable);
}

Error Widget_SetIsInputEnabled(Widget* self, bool isInputEnabled)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetIsInputEnabled: self must not be NULL.");
    }
    if (isInputEnabled == self->_isInputEnabled)
    {
        return Error_CreateSuccess();
    }
    self->_isInputEnabled = isInputEnabled;
    UIScreen_InternalInvalidateHitTest(self->_screen);
    Error HookResult = RunFlagHook(self, isInputEnabled, self->VTable->OnInputEnable, self->VTable->OnInputDisable);
    if (HookResult.Code != ErrorCode_Success)
    {
        return HookResult;
    }
    return ReleaseFocusIfIneligible(self);
}

Error Widget_SetAreSubWidgetsCut(Widget* self, bool areSubWidgetsCut)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetAreSubWidgetsCut: self must not be NULL.");
    }
    self->_areSubWidgetsCut = areSubWidgetsCut;
    UIScreen_InternalInvalidateHitTest(self->_screen);
    return Error_CreateSuccess();
}

Error Widget_SetIsFocusable(Widget* self, bool isFocusable)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetIsFocusable: self must not be NULL.");
    }
    self->_isFocusable = isFocusable;
    return ReleaseFocusIfIneligible(self);
}


// Public functions: subwidgets.
Error Widget_AddSubWidget(Widget* self, Widget* child)
{
    if ((self == NULL) || (child == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_AddSubWidget: self and child must not be NULL.");
    }
    if (child->_screen != self->_screen)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_AddSubWidget: child must belong to the same screen.");
    }
    if (self->_isDying || child->_isDying || !self->_isInitialized || !child->_isInitialized)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "Widget_AddSubWidget: both widgets must be initialized and not being destroyed.");
    }
    if ((child->_parent != NULL) || child->_isScreenRoot)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "Widget_AddSubWidget: child is already attached.");
    }
    if (IsSelfOrAncestor(child, self))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_AddSubWidget: cannot add the widget itself or an ancestor.");
    }
    if ((GetDepth(self) + GetSubtreeHeight(child)) > WIDGET_MAX_TREE_DEPTH)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "Widget_AddSubWidget: the widget tree would be deeper than %"
            PRIu64 " levels.", (uint64_t)WIDGET_MAX_TREE_DEPTH);
    }

    if (!GenericBuffer_AddLast(&self->_subWidgets, &child))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "Widget_AddSubWidget: subwidget could not be stored.");
    }
    child->_sequence = UIScreen_InternalNextSequence(self->_screen);
    if (!Widget_InternalInsertIntoDrawOrder(&self->_drawOrder, child))
    {
        GenericBuffer_RemoveLast(&self->_subWidgets);
        return Error_Construct2(ErrorCode_BufferTooLarge, "Widget_AddSubWidget: subwidget could not be stored.");
    }
    child->_parent = self;
    InvalidateBoundsUpTree(self);
    UIScreen_InternalInvalidateHitTest(self->_screen);
    return Error_CreateSuccess();
}

Error Widget_RemoveSubWidget(Widget* self, Widget* child)
{
    if ((self == NULL) || (child == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_RemoveSubWidget: self and child must not be NULL.");
    }
    if ((child->_parent != self) || child->_isDying)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "Widget_RemoveSubWidget: child is not a subwidget of this widget, or is being destroyed.");
    }
    return UIScreen_InternalDetachWidget(self->_screen, child);
}

Error Widget_GetSubWidgets(const Widget* self, GenericBuffer* outIds, size_t depth)
{
    if ((self == NULL) || (outIds == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_GetSubWidgets: self and outIds must not be NULL.");
    }
    if (outIds->_elementSize != sizeof(uint64_t))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_GetSubWidgets: outIds must be a uint64_t buffer.");
    }
    return AppendDescendants(self, outIds, depth);
}

size_t Widget_GetSubWidgetCount(const Widget* self)
{
    return self->_subWidgets._count;
}

Widget* Widget_GetSubWidgetAt(const Widget* self, size_t index)
{
    return (index < self->_subWidgets._count) ? GetChildAt(self, index) : NULL;
}


// Public functions: capabilities.
bool Widget_IsCapabilitySupported(const Widget* self, uint64_t capabilityId)
{
    return UIWidgetFactory_IsCapabilitySupported(UIScreen_GetFactory(self->_screen), self->_typeId, capabilityId);
}

Error Widget_GetSupportedCapabilities(const Widget* self, GenericBuffer* outCapabilityIds)
{
    if ((self == NULL) || (outCapabilityIds == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Widget_GetSupportedCapabilities: self and outCapabilityIds must not be NULL.");
    }
    return UIWidgetFactory_GetSupportedCapabilities(UIScreen_GetFactory(self->_screen), self->_typeId, outCapabilityIds);
}

void* Widget_GetCapability(Widget* self, uint64_t capabilityId)
{
    return UIWidgetFactory_ResolveCapability(UIScreen_GetFactory(self->_screen), self->_typeId, capabilityId, self);
}


// Public functions: typed properties.
bool Widget_TryGetPropertyType(const Widget* self, int32_t propertyId, UIPropertyType* outType)
{
    if (propertyId >= WIDGET_BASE_PROPERTY_START)
    {
        return TryGetBasePropertyType(propertyId, outType);
    }
    WidgetPropertyDescriptor Descriptor;
    if (!UIWidgetFactory_TryGetPropertyDescriptor(UIScreen_GetFactory(self->_screen), self->_typeId, propertyId, &Descriptor))
    {
        return false;
    }
    *outType = Descriptor.Type;
    return true;
}

Error Widget_GetProperty(Widget* self, int32_t propertyId, UIPropertyValue* outValue)
{
    if ((self == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_GetProperty: self and outValue must not be NULL.");
    }
    UIPropertyType Type = UIPropertyType_Float;
    if (!Widget_TryGetPropertyType(self, propertyId, &Type))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "Widget_GetProperty: the widget has no property %" PRId32 ".",
            propertyId);
    }

    outValue->Type = Type;
    switch (propertyId)
    {
        case WidgetBaseProperty_Position:   outValue->Data.Vector2Value = self->_position; return Error_CreateSuccess();
        case WidgetBaseProperty_Size:       outValue->Data.Vector2Value = self->_size; return Error_CreateSuccess();
        case WidgetBaseProperty_ZLayer:     outValue->Data.FloatValue = self->_zLayer; return Error_CreateSuccess();
        case WidgetBaseProperty_RenderTint: outValue->Data.RenderColorValue = self->_renderTint; return Error_CreateSuccess();
        default:                            break; // A property the type declares.
    }
    if (self->VTable->GetProperty == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "Widget_GetProperty: the widget's type has no GetProperty slot.");
    }
    Error Result = self->VTable->GetProperty(self, propertyId, outValue);
    outValue->Type = Type;
    return Result;
}

Error Widget_SetProperty(Widget* self, int32_t propertyId, const UIPropertyValue* value)
{
    if ((self == NULL) || (value == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetProperty: self and value must not be NULL.");
    }
    UIPropertyType Type = UIPropertyType_Float;
    if (!Widget_TryGetPropertyType(self, propertyId, &Type))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "Widget_SetProperty: the widget has no property %" PRId32 ".",
            propertyId);
    }
    if (value->Type != Type)
    {
        return Error_Construct4(ErrorCode_IllegalArgument, "Widget_SetProperty: property %" PRId32
            " has another value type.", propertyId);
    }

    switch (propertyId)
    {
        case WidgetBaseProperty_Position:   return Widget_SetPosition(self, value->Data.Vector2Value);
        case WidgetBaseProperty_Size:       return Widget_SetSize(self, value->Data.Vector2Value);
        case WidgetBaseProperty_ZLayer:     return Widget_SetZLayer(self, value->Data.FloatValue);
        case WidgetBaseProperty_RenderTint: return Widget_SetRenderTint(self, value->Data.RenderColorValue);
        default:                            break; // A property the type declares.
    }
    if (self->VTable->SetProperty == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "Widget_SetProperty: the widget's type has no SetProperty slot.");
    }
    return self->VTable->SetProperty(self, propertyId, value);
}
