#include "UIWidget.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMemory.h"
#include "wr/WRMath.h"
#include "wr/WRCompile.h"


// Static functions.
/* Returns true if @p value is a real, finite float (not NaN and not +/- infinity). */
static bool IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

/* Returns true if both components of @p value are finite. */
static bool IsVector2Finite(Vector2 value)
{
    return IsFloatFinite(value.x) && IsFloatFinite(value.y);
}

/* Keeps the first non-success error and releases any later one, for best-effort teardown loops. */
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

/* Returns the child widget stored at @p index in @p widget's subwidget buffer. */
static Widget* GetChildAt(const Widget* widget, size_t index)
{
    Widget** Slot = GenericBuffer_GetPointerToElement((GenericBuffer*)&widget->_subWidgets, index);
    return (Slot != NULL) ? *Slot : NULL;
}

/* Returns true if @p candidate is @p widget itself or any of its ancestors (used to reject cycles). */
static bool IsSelfOrAncestor(const Widget* candidate, const Widget* widget)
{
    const Widget* Current = widget;
    while (Current != NULL)
    {
        if (Current == candidate)
        {
            return true;
        }
        Current = Current->_parent;
    }
    return false;
}

/* Marks this widget's cached bounds and every ancestor's as stale. */
static void InvalidateBoundsUpTree(Widget* widget)
{
    Widget* Current = widget;
    while (Current != NULL)
    {
        Current->_areBoundsValid = false;
        Current = Current->_parent;
    }
}

/* Returns the axis-aligned union of two rectangles. */
static Rectangle UnionRectangle(Rectangle a, Rectangle b)
{
    float MinX = Math_MinFloat(a.x, b.x);
    float MinY = Math_MinFloat(a.y, b.y);
    float MaxX = Math_MaxFloat(a.x + a.width, b.x + b.width);
    float MaxY = Math_MaxFloat(a.y + a.height, b.y + b.height);
    return (Rectangle){ .x = MinX, .y = MinY, .width = MaxX - MinX, .height = MaxY - MinY };
}

/* Recomputes and caches this widget's estimated bounds (parent space), enclosing self and all subwidgets. */
static void ComputeBounds(Widget* self)
{
    Rectangle Bounds =
    {
        .x = self->_position.x,
        .y = self->_position.y,
        .width = self->_size.x,
        .height = self->_size.y
    };

    for (size_t Index = 0; Index < self->_subWidgets._count; Index++)
    {
        Widget* Child = GetChildAt(self, Index);
        Rectangle ChildBounds = Widget_GetEstimatedBounds(Child); // in this widget's local space
        // Map from this widget's local space into its parent space: origin + value * size.
        Rectangle Mapped =
        {
            .x = self->_position.x + (ChildBounds.x * self->_size.x),
            .y = self->_position.y + (ChildBounds.y * self->_size.y),
            .width = ChildBounds.width * self->_size.x,
            .height = ChildBounds.height * self->_size.y
        };
        Bounds = UnionRectangle(Bounds, Mapped);
    }

    self->_cachedBounds = Bounds;
    self->_areBoundsValid = true;
}

/* Recomputes the derived active state (hovered or tabbed) and fires its hooks/event on a change. */
static Error UpdateActive(Widget* self)
{
    bool NewActive = self->_isHovered || self->_isTabbed;
    if (NewActive == self->_isActive)
    {
        return Error_CreateSuccess();
    }

    self->_isActive = NewActive;
    if (NewActive)
    {
        if (self->VTable->OnActiveEnable != NULL)
        {
            Error HookResult = self->VTable->OnActiveEnable(self);
            if (HookResult.Code != ErrorCode_Success)
            {
                return HookResult;
            }
        }
    }
    else
    {
        if (self->VTable->OnActiveDisable != NULL)
        {
            Error HookResult = self->VTable->OnActiveDisable(self);
            if (HookResult.Code != ErrorCode_Success)
            {
                return HookResult;
            }
        }
    }

    return WREvent_Raise(&self->_onActiveStateChange, self);
}

/* Recursively appends a widget's descendant ids to @p out, down to @p depth levels below immediate children. */
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

        bool ShouldRecurse = (depth == WIDGET_SUBWIDGET_DEPTH_ALL) || (depth > 0);
        if (ShouldRecurse)
        {
            size_t ChildDepth = (depth == WIDGET_SUBWIDGET_DEPTH_ALL) ? WIDGET_SUBWIDGET_DEPTH_ALL : (depth - 1);
            Error RecurseResult = AppendDescendants(Child, out, ChildDepth);
            if (RecurseResult.Code != ErrorCode_Success)
            {
                return RecurseResult;
            }
        }
    }
    return Error_CreateSuccess();
}


// Public functions: lifecycle.
Error Widget_Construct(Widget* self, const WidgetVTable* vtable, UIScreen* screen, uint64_t typeId)
{
    if ((self == NULL) || (vtable == NULL) || (screen == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_Construct: self, vtable and screen must not be NULL.");
    }
    if (typeId == WIDGET_TYPE_ID_INVALID)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_Construct: typeId must not be 0.");
    }

    self->VTable = vtable;
    self->_screen = screen;
    self->_parent = NULL;
    self->_id = WIDGET_ID_INVALID;
    self->_typeId = typeId;

    self->_position = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_size = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_zLayer = 0.0f;
    self->_renderTint = RenderColor_White();

    self->_isRendered = true;
    self->_isUpdated = true;
    self->_isInputEnabled = true;
    self->_areSubWidgetsCut = false;
    self->_isScreenRoot = false;

    self->_isHovered = false;
    self->_isFocused = false;
    self->_isTabbed = false;
    self->_isActive = false;

    self->_cachedBounds = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f };
    self->_areBoundsValid = false;

    GenericBuffer_AllocateVariable(&self->_subWidgets, 4U, sizeof(Widget*));

    Error HoverEventResult = WREvent_Construct1(&self->_onHoverStateChange);
    if (HoverEventResult.Code != ErrorCode_Success)
    {
        Memory_Free(self->_subWidgets._data);
        return HoverEventResult;
    }
    Error FocusEventResult = WREvent_Construct1(&self->_onFocusStateChange);
    if (FocusEventResult.Code != ErrorCode_Success)
    {
        WREvent_Deconstruct(&self->_onHoverStateChange);
        Memory_Free(self->_subWidgets._data);
        return FocusEventResult;
    }
    Error TabEventResult = WREvent_Construct1(&self->_onTabStateChange);
    if (TabEventResult.Code != ErrorCode_Success)
    {
        WREvent_Deconstruct(&self->_onFocusStateChange);
        WREvent_Deconstruct(&self->_onHoverStateChange);
        Memory_Free(self->_subWidgets._data);
        return TabEventResult;
    }
    Error ActiveEventResult = WREvent_Construct1(&self->_onActiveStateChange);
    if (ActiveEventResult.Code != ErrorCode_Success)
    {
        WREvent_Deconstruct(&self->_onTabStateChange);
        WREvent_Deconstruct(&self->_onFocusStateChange);
        WREvent_Deconstruct(&self->_onHoverStateChange);
        Memory_Free(self->_subWidgets._data);
        return ActiveEventResult;
    }

    uint64_t Id = WIDGET_ID_INVALID;
    Error RegisterResult = UIScreen_RegisterWidget(screen, self, &Id);
    if (RegisterResult.Code != ErrorCode_Success)
    {
        WREvent_Deconstruct(&self->_onActiveStateChange);
        WREvent_Deconstruct(&self->_onTabStateChange);
        WREvent_Deconstruct(&self->_onFocusStateChange);
        WREvent_Deconstruct(&self->_onHoverStateChange);
        Memory_Free(self->_subWidgets._data);
        return RegisterResult;
    }

    self->_id = Id;
    return Error_CreateSuccess();
}

Error Widget_Deconstruct(Widget* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Error FirstError = Error_CreateSuccess();

    // Release the concrete widget's own resources while it is still valid.
    if (self->VTable->OnDeconstruct != NULL)
    {
        Error DeconstructResult = self->VTable->OnDeconstruct(self);
        KeepFirstError(&FirstError, &DeconstructResult);
    }

    // Detach from a parent (so the parent does not keep a dangling child pointer).
    if (self->_parent != NULL)
    {
        Error DetachResult = Widget_RemoveSubWidget(self->_parent, self);
        KeepFirstError(&FirstError, &DetachResult);
    }

    // Orphan any remaining children so they do not keep a dangling parent pointer.
    for (size_t Index = 0; Index < self->_subWidgets._count; Index++)
    {
        Widget* Child = GetChildAt(self, Index);
        if (Child != NULL)
        {
            Child->_parent = NULL;
        }
    }

    // Capture identity before unregistering / returning to the pool (the storage is reset afterwards).
    uint64_t TypeId = self->_typeId;
    UIScreen* Screen = self->_screen;
    uint64_t Id = self->_id;

    Error UnregisterResult = UIScreen_UnregisterWidget(Screen, Id);
    KeepFirstError(&FirstError, &UnregisterResult);

    WREvent_Deconstruct(&self->_onHoverStateChange);
    WREvent_Deconstruct(&self->_onFocusStateChange);
    WREvent_Deconstruct(&self->_onTabStateChange);
    WREvent_Deconstruct(&self->_onActiveStateChange);
    Memory_Free(self->_subWidgets._data);

    // Return the widget's storage to its type's pool (must be last: self is invalid afterwards).
    UIWidgetFactory* Factory = UIScreen_GetFactory(Screen);
    Error ReturnResult = UIWidgetFactory_ReturnWidget(Factory, TypeId, self);
    KeepFirstError(&FirstError, &ReturnResult);

    return FirstError;
}

Error Widget_InitializeWidget(Widget* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_InitializeWidget: self must not be NULL.");
    }

    if (self->VTable->Initialize != NULL)
    {
        Error InitializeResult = self->VTable->Initialize(self);
        if (InitializeResult.Code != ErrorCode_Success)
        {
            return InitializeResult;
        }
    }
    if (self->VTable->OnUpdateEnable != NULL)
    {
        Error HookResult = self->VTable->OnUpdateEnable(self);
        if (HookResult.Code != ErrorCode_Success)
        {
            return HookResult;
        }
    }
    if (self->VTable->OnRenderEnable != NULL)
    {
        Error HookResult = self->VTable->OnRenderEnable(self);
        if (HookResult.Code != ErrorCode_Success)
        {
            return HookResult;
        }
    }
    if (self->VTable->OnInputEnable != NULL)
    {
        Error HookResult = self->VTable->OnInputEnable(self);
        if (HookResult.Code != ErrorCode_Success)
        {
            return HookResult;
        }
    }
    return Error_CreateSuccess();
}

Error Widget_DeinitializeWidget(Widget* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_DeinitializeWidget: self must not be NULL.");
    }

    Error FirstError = Error_CreateSuccess();

    if (self->_isInputEnabled && (self->VTable->OnInputDisable != NULL))
    {
        Error HookResult = self->VTable->OnInputDisable(self);
        KeepFirstError(&FirstError, &HookResult);
    }
    self->_isInputEnabled = false;

    if (self->_isRendered && (self->VTable->OnRenderDisable != NULL))
    {
        Error HookResult = self->VTable->OnRenderDisable(self);
        KeepFirstError(&FirstError, &HookResult);
    }
    self->_isRendered = false;

    if (self->_isUpdated && (self->VTable->OnUpdateDisable != NULL))
    {
        Error HookResult = self->VTable->OnUpdateDisable(self);
        KeepFirstError(&FirstError, &HookResult);
    }
    self->_isUpdated = false;

    if (self->VTable->Deinitialize != NULL)
    {
        Error DeinitializeResult = self->VTable->Deinitialize(self);
        KeepFirstError(&FirstError, &DeinitializeResult);
    }

    return FirstError;
}


// Public functions: framework-driven wrappers.
Error Widget_Update(Widget* self, ProgramTime time)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_Update: self must not be NULL.");
    }
    if (self->VTable->Update == NULL)
    {
        return Error_CreateSuccess();
    }
    return self->VTable->Update(self, time);
}

Error Widget_Render(Widget* self, UIRenderContext* context)
{
    if ((self == NULL) || (context == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_Render: self and context must not be NULL.");
    }
    if (self->VTable->Render == NULL)
    {
        return Error_CreateSuccess();
    }
    return self->VTable->Render(self, context);
}

Error Widget_OnMouseInput(Widget* self, const WidgetMouseInputArgs* args)
{
    if ((self == NULL) || (args == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_OnMouseInput: self and args must not be NULL.");
    }
    if (self->VTable->OnMouseInput == NULL)
    {
        return Error_CreateSuccess();
    }
    return self->VTable->OnMouseInput(self, args);
}

Error Widget_OnKeyboardInput(Widget* self, const WidgetKeyboardInputArgs* args)
{
    if ((self == NULL) || (args == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_OnKeyboardInput: self and args must not be NULL.");
    }
    if (self->VTable->OnKeyboardInput == NULL)
    {
        return Error_CreateSuccess();
    }
    return self->VTable->OnKeyboardInput(self, args);
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

    self->_zLayer = zLayer;
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
    if (isRendered)
    {
        if (self->VTable->OnRenderEnable != NULL)
        {
            return self->VTable->OnRenderEnable(self);
        }
    }
    else if (self->VTable->OnRenderDisable != NULL)
    {
        return self->VTable->OnRenderDisable(self);
    }
    return Error_CreateSuccess();
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
    if (isUpdated)
    {
        if (self->VTable->OnUpdateEnable != NULL)
        {
            return self->VTable->OnUpdateEnable(self);
        }
    }
    else if (self->VTable->OnUpdateDisable != NULL)
    {
        return self->VTable->OnUpdateDisable(self);
    }
    return Error_CreateSuccess();
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
    if (isInputEnabled)
    {
        if (self->VTable->OnInputEnable != NULL)
        {
            return self->VTable->OnInputEnable(self);
        }
    }
    else if (self->VTable->OnInputDisable != NULL)
    {
        return self->VTable->OnInputDisable(self);
    }
    return Error_CreateSuccess();
}

Error Widget_SetAreSubWidgetsCut(Widget* self, bool areSubWidgetsCut)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetAreSubWidgetsCut: self must not be NULL.");
    }
    self->_areSubWidgetsCut = areSubWidgetsCut;
    return Error_CreateSuccess();
}


// Public functions: screen-driven state changes.
Error Widget_SetHovered(Widget* self, bool isHovered, const WidgetHoverArgs* args)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetHovered: self must not be NULL.");
    }
    if (isHovered == self->_isHovered)
    {
        return Error_CreateSuccess();
    }

    self->_isHovered = isHovered;
    if (isHovered)
    {
        if (self->VTable->OnHoverStart != NULL)
        {
            Error HookResult = self->VTable->OnHoverStart(self, args);
            if (HookResult.Code != ErrorCode_Success)
            {
                return HookResult;
            }
        }
    }
    else if (self->VTable->OnHoverEnd != NULL)
    {
        Error HookResult = self->VTable->OnHoverEnd(self);
        if (HookResult.Code != ErrorCode_Success)
        {
            return HookResult;
        }
    }

    Error RaiseResult = WREvent_Raise(&self->_onHoverStateChange, self);
    if (RaiseResult.Code != ErrorCode_Success)
    {
        return RaiseResult;
    }
    return UpdateActive(self);
}

Error Widget_SetFocused(Widget* self, bool isFocused)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetFocused: self must not be NULL.");
    }
    if (isFocused == self->_isFocused)
    {
        return Error_CreateSuccess();
    }

    self->_isFocused = isFocused;
    if (isFocused)
    {
        if (self->VTable->OnFocusEnable != NULL)
        {
            Error HookResult = self->VTable->OnFocusEnable(self);
            if (HookResult.Code != ErrorCode_Success)
            {
                return HookResult;
            }
        }
    }
    else if (self->VTable->OnFocusDisable != NULL)
    {
        Error HookResult = self->VTable->OnFocusDisable(self);
        if (HookResult.Code != ErrorCode_Success)
        {
            return HookResult;
        }
    }

    return WREvent_Raise(&self->_onFocusStateChange, self);
}

Error Widget_SetTabbed(Widget* self, bool isTabbed)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetTabbed: self must not be NULL.");
    }
    if (isTabbed == self->_isTabbed)
    {
        return Error_CreateSuccess();
    }

    self->_isTabbed = isTabbed;
    if (isTabbed)
    {
        if (self->VTable->OnTabbedEnable != NULL)
        {
            Error HookResult = self->VTable->OnTabbedEnable(self);
            if (HookResult.Code != ErrorCode_Success)
            {
                return HookResult;
            }
        }
    }
    else if (self->VTable->OnTabbedDisable != NULL)
    {
        Error HookResult = self->VTable->OnTabbedDisable(self);
        if (HookResult.Code != ErrorCode_Success)
        {
            return HookResult;
        }
    }

    Error RaiseResult = WREvent_Raise(&self->_onTabStateChange, self);
    if (RaiseResult.Code != ErrorCode_Success)
    {
        return RaiseResult;
    }
    return UpdateActive(self);
}

void Widget_SetScreenRoot(Widget* self, bool isScreenRoot)
{
    self->_isScreenRoot = isScreenRoot;
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
    if (IsSelfOrAncestor(child, self))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_AddSubWidget: cannot add the widget itself or an ancestor.");
    }
    if ((child->_parent != NULL) || child->_isScreenRoot)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "Widget_AddSubWidget: child is already attached.");
    }

    child->_parent = self;
    if (!GenericBuffer_AddLast(&self->_subWidgets, &child))
    {
        child->_parent = NULL;
        return Error_Construct2(ErrorCode_InvalidOperation, "Widget_AddSubWidget: subwidget could not be stored.");
    }

    InvalidateBoundsUpTree(self);
    return Error_CreateSuccess();
}

Error Widget_RemoveSubWidget(Widget* self, Widget* child)
{
    if ((self == NULL) || (child == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_RemoveSubWidget: self and child must not be NULL.");
    }

    for (size_t Index = 0; Index < self->_subWidgets._count; Index++)
    {
        if (GetChildAt(self, Index) == child)
        {
            if (!GenericBuffer_RemoveAt(&self->_subWidgets, Index))
            {
                return Error_Construct2(ErrorCode_InvalidOperation, "Widget_RemoveSubWidget: subwidget could not be removed.");
            }
            child->_parent = NULL;
            InvalidateBoundsUpTree(self);
            return Error_CreateSuccess();
        }
    }

    return Error_Construct2(ErrorCode_InvalidOperation, "Widget_RemoveSubWidget: child is not a subwidget of this widget.");
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
    if (index >= self->_subWidgets._count)
    {
        return NULL;
    }
    return GetChildAt(self, index);
}

/* Three-way comparison of two child widgets by ascending z layer. */
static ComparisonResult CompareChildZ(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b, const UserData* userData)
{
    UNUSED(buffer);
    UNUSED(userData);
    Widget* WidgetA = *(Widget**)a._element;
    Widget* WidgetB = *(Widget**)b._element;
    if (WidgetA->_zLayer < WidgetB->_zLayer)
    {
        return ComparisonResult_ALessThanB;
    }
    if (WidgetA->_zLayer > WidgetB->_zLayer)
    {
        return ComparisonResult_AGreaterThanB;
    }
    return ComparisonResult_AEqualsB;
}

void Widget_SortSubWidgetsByZ(Widget* self)
{
    unsigned char Scratch[2 * sizeof(Widget*)];
    GenericBuffer_SortAscending(&self->_subWidgets, CompareChildZ, NULL, Scratch);
}


// Public functions: capabilities.
bool Widget_IsCapabilitySupported(const Widget* self, uint64_t capabilityId)
{
    UIWidgetFactory* Factory = UIScreen_GetFactory(self->_screen);
    return UIWidgetFactory_IsCapabilitySupported(Factory, self->_typeId, capabilityId);
}

Error Widget_GetSupportedCapabilities(const Widget* self, GenericBuffer* outCapabilityIds)
{
    if ((self == NULL) || (outCapabilityIds == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_GetSupportedCapabilities: self and outCapabilityIds must not be NULL.");
    }
    UIWidgetFactory* Factory = UIScreen_GetFactory(self->_screen);
    return UIWidgetFactory_GetSupportedCapabilities(Factory, self->_typeId, outCapabilityIds);
}

void* Widget_GetCapability(Widget* self, uint64_t capabilityId)
{
    UIWidgetFactory* Factory = UIScreen_GetFactory(self->_screen);
    return UIWidgetFactory_ResolveCapability(Factory, self->_typeId, capabilityId, self);
}


// Public functions: generic property access.
Error Widget_GetProperty(Widget* self, int32_t propertyId, void* outValue)
{
    if ((self == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_GetProperty: self and outValue must not be NULL.");
    }

    if (propertyId >= WIDGET_BASE_PROPERTY_START)
    {
        switch (propertyId)
        {
            case WidgetBaseProperty_Position:   Memory_Copy(&self->_position, outValue, sizeof(Vector2)); return Error_CreateSuccess();
            case WidgetBaseProperty_Size:       Memory_Copy(&self->_size, outValue, sizeof(Vector2)); return Error_CreateSuccess();
            case WidgetBaseProperty_ZLayer:     Memory_Copy(&self->_zLayer, outValue, sizeof(float)); return Error_CreateSuccess();
            case WidgetBaseProperty_RenderTint: Memory_Copy(&self->_renderTint, outValue, sizeof(RenderColor)); return Error_CreateSuccess();
            default:                            return Error_Construct2(ErrorCode_InvalidOperation, "Widget_GetProperty: unknown base property id.");
        }
    }

    if (self->VTable->GetProperty != NULL)
    {
        return self->VTable->GetProperty(self, propertyId, outValue);
    }
    return Error_Construct2(ErrorCode_InvalidOperation, "Widget_GetProperty: property id is not handled by this widget.");
}

Error Widget_SetProperty(Widget* self, int32_t propertyId, const void* value)
{
    if ((self == NULL) || (value == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Widget_SetProperty: self and value must not be NULL.");
    }

    if (propertyId >= WIDGET_BASE_PROPERTY_START)
    {
        switch (propertyId)
        {
            case WidgetBaseProperty_Position:
            {
                Vector2 Position;
                Memory_Copy(value, &Position, sizeof(Vector2));
                return Widget_SetPosition(self, Position);
            }
            case WidgetBaseProperty_Size:
            {
                Vector2 Size;
                Memory_Copy(value, &Size, sizeof(Vector2));
                return Widget_SetSize(self, Size);
            }
            case WidgetBaseProperty_ZLayer:
            {
                float ZLayer;
                Memory_Copy(value, &ZLayer, sizeof(float));
                return Widget_SetZLayer(self, ZLayer);
            }
            case WidgetBaseProperty_RenderTint:
            {
                RenderColor Tint;
                Memory_Copy(value, &Tint, sizeof(RenderColor));
                return Widget_SetRenderTint(self, Tint);
            }
            default:
                return Error_Construct2(ErrorCode_InvalidOperation, "Widget_SetProperty: unknown base property id.");
        }
    }

    if (self->VTable->SetProperty != NULL)
    {
        return self->VTable->SetProperty(self, propertyId, value);
    }
    return Error_Construct2(ErrorCode_InvalidOperation, "Widget_SetProperty: property id is not handled by this widget.");
}
