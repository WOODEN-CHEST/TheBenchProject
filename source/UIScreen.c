#include "UIScreen.h"
#include "UIInternal.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "UIRenderContext.h"
#include "Renderer.h"
#include "wr/WRMemory.h"
#include "wr/WRMath.h"
#include "wr/WRCompile.h"


// Macros.
/** Navigation outline thickness, as a fraction of the render buffer height (a constant relative size). */
#define OUTLINE_THICKNESS_RELATIVE 0.0035f
/** How far above the highest sibling z a widget is placed when brought to the top. */
#define BRING_TO_TOP_Z_STEP 1.0f
/** The viewport a screen starts with, until its host sets the real one. */
#define DEFAULT_VIEWPORT_WIDTH 1280.0f
#define DEFAULT_VIEWPORT_HEIGHT 720.0f
#define DEFAULT_VIEWPORT_ASPECT_RATIO (16.0f / 9.0f)
/** Bits of an id holding the registry slot index + 1; the generation is above them. */
#define ID_SLOT_BITS 32U
#define ID_SLOT_MASK ((uint64_t)0xFFFFFFFFU)
/** Most registry slots (the slot index + 1 must fit in ID_SLOT_BITS). */
#define MAX_SLOT_COUNT ((size_t)UINT32_MAX - 1U)


// Types.
/* The state of one registry slot. */
typedef enum SlotStateEnum
{
    SlotState_Free,
    /* Created but its constructor has not finished: the id does not resolve yet. */
    SlotState_Reserved,
    SlotState_Alive,
    /* Destroyed, waiting for the flush: the id no longer resolves but the memory is still valid. */
    SlotState_Dying
} SlotState;

/* One registry slot. An id is (Generation << 32) | (slot index + 1); freeing a slot bumps its generation, so old ids
 * never resolve to a newer widget. */
typedef struct RegistrySlotStruct
{
    Widget* Widget;
    uint32_t Generation;
    SlotState State;
    /* Index + 1 of the next free slot (0: none), while this slot is on the free list. */
    uint32_t NextFree;
} RegistrySlot;


// Static functions: helpers.
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

static bool IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

static uint64_t MakeId(size_t slotIndex, uint32_t generation)
{
    return ((uint64_t)generation << ID_SLOT_BITS) | (uint64_t)(slotIndex + 1U);
}

/* The slot an id names when its generation still matches, NULL otherwise. */
static RegistrySlot* FindSlot(const UIScreen* self, uint64_t widgetId, size_t* outIndex)
{
    uint64_t SlotNumber = widgetId & ID_SLOT_MASK;
    if ((SlotNumber == 0U) || (SlotNumber > self->_slots._count))
    {
        return NULL;
    }
    size_t Index = (size_t)(SlotNumber - 1U);
    RegistrySlot* Slot = GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_slots, Index);
    if (Slot->Generation != (uint32_t)(widgetId >> ID_SLOT_BITS))
    {
        return NULL;
    }
    if (outIndex != NULL)
    {
        *outIndex = Index;
    }
    return Slot;
}

/* Frees a slot; its generation moves on so its old ids stop resolving. A slot whose generations are used up is
 * retired (never reused). */
static void FreeSlot(UIScreen* self, size_t index)
{
    RegistrySlot* Slot = GenericBuffer_GetPointerToElement(&self->_slots, index);
    Slot->Widget = NULL;
    Slot->State = SlotState_Free;
    if (Slot->Generation == UINT32_MAX)
    {
        return;
    }
    Slot->Generation++;
    Slot->NextFree = self->_freeSlotHead;
    self->_freeSlotHead = (uint32_t)(index + 1U);
}

static bool IsIdDying(const UIScreen* self, uint64_t widgetId)
{
    const RegistrySlot* Slot = FindSlot(self, widgetId, NULL);
    return (Slot != NULL) && (Slot->State == SlotState_Dying);
}

/* Maps bounds given in a widget's parent space to absolute screen coordinates. */
static Rectangle ParentSpaceToScreen(const Widget* widget, Rectangle bounds)
{
    Vector2 ParentPosition = { .x = 0.0f, .y = 0.0f };
    Vector2 ParentSize = { .x = 1.0f, .y = 1.0f };
    if (widget->_parent != NULL)
    {
        UIScreen_InternalGetAbsoluteBox(widget->_parent, &ParentPosition, &ParentSize);
    }
    return (Rectangle)
    {
        .x = ParentPosition.x + (bounds.x * ParentSize.x),
        .y = ParentPosition.y + (bounds.y * ParentSize.y),
        .width = bounds.width * ParentSize.x,
        .height = bounds.height * ParentSize.y
    };
}

static void RemoveRoot(UIScreen* self, Widget* widget)
{
    for (size_t Index = 0; Index < self->_roots._count; Index++)
    {
        if (*(Widget**)GenericBuffer_GetPointerToElement(&self->_roots, Index) == widget)
        {
            GenericBuffer_RemoveAt(&self->_roots, Index);
            break;
        }
    }
    Widget_InternalRemoveFromDrawOrder(&self->_rootDrawOrder, widget);
    Widget_InternalSetScreenRoot(widget, false, 0U);
}

/* Unlinks a widget from its parent or from the roots. */
static void UnlinkWidget(UIScreen* self, Widget* widget)
{
    if (widget->_isScreenRoot)
    {
        RemoveRoot(self, widget);
    }
    else
    {
        Widget_InternalUnlinkFromParent(widget);
    }
    self->_isHitTestDirty = true;
}


// Static functions: destruction.
/* Marks a subtree dying: its ids stop resolving. Recursion is bounded by WIDGET_MAX_TREE_DEPTH. */
static void MarkSubtreeDying(UIScreen* self, Widget* widget)
{
    size_t Index = 0;
    RegistrySlot* Slot = FindSlot(self, widget->_id, &Index);
    if ((Slot != NULL) && (Slot->State == SlotState_Alive))
    {
        Slot->State = SlotState_Dying;
        self->_aliveWidgetCount--;
    }
    widget->_isDying = true;
    for (size_t ChildIndex = 0; ChildIndex < widget->_subWidgets._count; ChildIndex++)
    {
        MarkSubtreeDying(self, Widget_GetSubWidgetAt(widget, ChildIndex));
    }
}

/* Forgets every reference into dying subtrees, without running hooks (the widgets are being destroyed). */
static void ForgetDyingReferences(UIScreen* self)
{
    if (IsIdDying(self, self->_hoveredWidgetId))
    {
        self->_hoveredWidgetId = WIDGET_ID_INVALID;
    }
    if (IsIdDying(self, self->_focusedWidgetId))
    {
        self->_focusedWidgetId = WIDGET_ID_INVALID;
    }
    // The stack is a path into the tree: everything after the first dying entry lies inside it.
    for (size_t Index = 0; Index < self->_navStack._count; Index++)
    {
        if (IsIdDying(self, *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_navStack, Index)))
        {
            GenericBuffer_SetCount(&self->_navStack, Index);
            break;
        }
    }
    for (size_t Index = 0; Index < UI_SCREEN_MOUSE_BUTTON_COUNT; Index++)
    {
        if (self->_buttons[Index].IsPressed && IsIdDying(self, self->_buttons[Index].TargetWidgetId))
        {
            self->_buttons[Index].IsPressed = false;
            self->_buttons[Index].TargetWidgetId = WIDGET_ID_INVALID;
        }
    }
    UIScreen_InternalStopDyingAnimations(self);
}

/* Tears down a dying subtree, children first: hooks, subscriptions, registry slot, storage. Recursion is bounded by
 * WIDGET_MAX_TREE_DEPTH. */
static Error TeardownSubtree(UIScreen* self, Widget* widget)
{
    Error FirstError = Error_CreateSuccess();
    // The widget is never drawn again: emptying its draw order spares each child's unlink a search in it.
    GenericBuffer_Clear(&widget->_drawOrder);
    // Each child's teardown unlinks it, and nothing can be added to a dying widget, so this ends.
    while (widget->_subWidgets._count > 0U)
    {
        Error ChildResult = TeardownSubtree(self, Widget_GetSubWidgetAt(widget, widget->_subWidgets._count - 1U));
        KeepFirstError(&FirstError, &ChildResult);
    }

    Error HookResult = Widget_InternalRunTeardownHooks(widget);
    KeepFirstError(&FirstError, &HookResult);
    Widget_InternalUnlinkFromParent(widget);

    uint64_t Id = widget->_id;
    UIScreen_InternalRemoveWidgetSubscriptions(self, Id);
    size_t SlotIndex = 0;
    if (FindSlot(self, Id, &SlotIndex) != NULL)
    {
        FreeSlot(self, SlotIndex);
    }
    Widget_InternalReleaseBase(widget);
    Error ReturnResult = UIWidgetFactory_InternalReturnWidget(self->_factory, widget);
    KeepFirstError(&FirstError, &ReturnResult);
    return FirstError;
}

/* Tears down every destroyed subtree, in the order they were destroyed. Subtrees that teardown hooks destroy are
 * appended and torn down in the same pass. */
static Error FlushDestroyedWidgets(UIScreen* self)
{
    Error FirstError = Error_CreateSuccess();
    for (size_t Index = 0; Index < self->_pendingDestroy._count; Index++)
    {
        Widget* Root = *(Widget**)GenericBuffer_GetPointerToElement(&self->_pendingDestroy, Index);
        Error TeardownResult = TeardownSubtree(self, Root);
        KeepFirstError(&FirstError, &TeardownResult);
    }
    GenericBuffer_Clear(&self->_pendingDestroy);
    return FirstError;
}


// Static functions: hit testing and rendering.
static bool PointInBox(Vector2 point, Vector2 position, Vector2 size)
{
    return (point.x >= position.x) && (point.x <= (position.x + size.x))
        && (point.y >= position.y) && (point.y <= (position.y + size.y));
}

/* The top-most input-enabled widget of a subtree at a point; recursion is bounded by WIDGET_MAX_TREE_DEPTH. */
static Widget* HitTestNode(Widget* widget, Vector2 screenPosition, Vector2 parentPosition, Vector2 parentSize)
{
    Vector2 Position = Widget_GetPosition(widget);
    Vector2 Size = Widget_GetSize(widget);
    Vector2 AbsolutePosition = { .x = parentPosition.x + (Position.x * parentSize.x), .y = parentPosition.y + (Position.y * parentSize.y) };
    Vector2 AbsoluteSize = { .x = Size.x * parentSize.x, .y = Size.y * parentSize.y };

    bool IsInside = PointInBox(screenPosition, AbsolutePosition, AbsoluteSize);
    if (Widget_AreSubWidgetsCut(widget) && !IsInside)
    {
        return NULL; // Clipped: neither the widget nor its (clipped) children can be hit here.
    }
    for (size_t Index = widget->_drawOrder._count; Index-- > 0U;)
    {
        Widget* Hit = HitTestNode(Widget_InternalGetDrawOrderAt(&widget->_drawOrder, Index), screenPosition,
            AbsolutePosition, AbsoluteSize);
        if (Hit != NULL)
        {
            return Hit;
        }
    }
    return (Widget_IsInputEnabled(widget) && IsInside) ? widget : NULL;
}

static Rectangle ScreenBoxToPixels(RenderContext* renderContext, Vector2 position, Vector2 size)
{
    Vector2 PixelPosition = RenderContext_VectorRelativeToPixel(renderContext, position);
    Vector2 PixelSize = RenderContext_VectorRelativeToPixel(renderContext, size);
    return (Rectangle){ .x = PixelPosition.x, .y = PixelPosition.y, .width = PixelSize.x, .height = PixelSize.y };
}

/* Draws a subtree: the widget, then its children by draw order; recursion is bounded by WIDGET_MAX_TREE_DEPTH. */
static Error RenderNode(RenderContext* renderContext, Widget* widget, Vector2 parentPosition, Vector2 parentSize,
    RenderColor parentTint)
{
    Vector2 Position = Widget_GetPosition(widget);
    Vector2 Size = Widget_GetSize(widget);
    Vector2 AbsolutePosition = { .x = parentPosition.x + (Position.x * parentSize.x), .y = parentPosition.y + (Position.y * parentSize.y) };
    Vector2 AbsoluteSize = { .x = Size.x * parentSize.x, .y = Size.y * parentSize.y };
    RenderColor Tint = UIRenderColor_Multiply(parentTint, Widget_GetRenderTint(widget));

    if (Widget_IsRendered(widget))
    {
        UIRenderContext Context;
        UIRenderContext_Create(&Context, renderContext, AbsolutePosition, AbsoluteSize, Tint);
        Error RenderResult = Widget_Render(widget, &Context);
        if (RenderResult.Code != ErrorCode_Success)
        {
            return RenderResult;
        }
    }
    if (widget->_drawOrder._count == 0U)
    {
        return Error_CreateSuccess();
    }

    bool IsClipped = Widget_AreSubWidgetsCut(widget);
    if (IsClipped)
    {
        RenderContext_PushClip(renderContext, ScreenBoxToPixels(renderContext, AbsolutePosition, AbsoluteSize));
    }
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0; (Index < widget->_drawOrder._count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = RenderNode(renderContext, Widget_InternalGetDrawOrderAt(&widget->_drawOrder, Index), AbsolutePosition,
            AbsoluteSize, Tint);
    }
    if (IsClipped)
    {
        RenderContext_PopClip(renderContext);
    }
    return Result;
}

static void DrawOutline(RenderContext* renderContext, const Widget* widget, RenderColor color)
{
    Rectangle Box = UIScreen_InternalPeekEstimatedScreenBox(widget);
    RectangleOutlineRenderArguments Arguments =
    {
        .Position = RenderVector2D_Relative((Vector2){ .x = Box.x, .y = Box.y }),
        .Size = RenderVector2D_Relative((Vector2){ .x = Box.width, .y = Box.height }),
        .Thickness = RenderFloat_Relative(OUTLINE_THICKNESS_RELATIVE),
        .TargetColor = color
    };
    RenderContext_RenderRectangleOutline(renderContext, &Arguments);
}

/* Draws the keyboard-navigation outlines: green around each entered widget, white around the focused one. */
static void DrawNavigationOutlines(UIScreen* self, RenderContext* renderContext)
{
    if (!self->_isNavigating)
    {
        return;
    }
    RenderColor Green = { .Tint = GREEN, .Brightness = 1.0f, .Opacity = 1.0f };
    for (size_t Index = 0; Index < self->_navStack._count; Index++)
    {
        Widget* Entered = UIScreen_InternalResolve(self, *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_navStack, Index));
        if (Entered != NULL)
        {
            DrawOutline(renderContext, Entered, Green);
        }
    }
    Widget* Focused = UIScreen_InternalResolve(self, self->_focusedWidgetId);
    if (Focused != NULL)
    {
        DrawOutline(renderContext, Focused, RenderColor_White());
    }
}


// Static functions: update.
/* Appends the ids of a subtree in pre-order (parents before children); recursion is bounded by WIDGET_MAX_TREE_DEPTH. */
static Error SnapshotSubtree(Widget* widget, GenericBuffer* out)
{
    uint64_t Id = Widget_GetId(widget);
    if (!GenericBuffer_AddLast(out, &Id))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_Update: the update snapshot could not grow.");
    }
    for (size_t Index = 0; Index < Widget_GetSubWidgetCount(widget); Index++)
    {
        Error ChildResult = SnapshotSubtree(Widget_GetSubWidgetAt(widget, Index), out);
        if (ChildResult.Code != ErrorCode_Success)
        {
            return ChildResult;
        }
    }
    return Error_CreateSuccess();
}

static Error TakeSnapshot(UIScreen* self)
{
    GenericBuffer_Clear(&self->_updateSnapshot);
    for (size_t Index = 0; Index < self->_roots._count; Index++)
    {
        Error Result = SnapshotSubtree(*(Widget**)GenericBuffer_GetPointerToElement(&self->_roots, Index), &self->_updateSnapshot);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* Updates every snapshot widget that is still alive, attached and updated (widgets added this tick start next tick). */
static Error UpdateWidgets(UIScreen* self, ProgramTime time)
{
    for (size_t Index = 0; Index < self->_updateSnapshot._count; Index++)
    {
        Widget* Target = UIScreen_InternalResolve(self, *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_updateSnapshot, Index));
        if ((Target == NULL) || !Widget_IsUpdated(Target) || !UIScreen_InternalIsAttached(Target))
        {
            continue;
        }
        Error Result = Widget_Update(Target, time);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* Lays out every snapshot widget that is still alive and attached, parents first (the snapshot is in pre-order). */
static Error LayoutWidgets(UIScreen* self)
{
    RenderContext Measure;
    RenderContext_CreateMeasuring(&Measure, self->_viewport.SizePixels, self->_viewport.TargetAspectRatio,
        self->_viewport.TargetRelativePosition);
    for (size_t Index = 0; Index < self->_updateSnapshot._count; Index++)
    {
        Widget* Target = UIScreen_InternalResolve(self, *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_updateSnapshot, Index));
        if ((Target == NULL) || !UIScreen_InternalIsAttached(Target))
        {
            continue;
        }
        Vector2 ParentPosition = { .x = 0.0f, .y = 0.0f };
        Vector2 ParentSize = { .x = 1.0f, .y = 1.0f };
        if (Widget_GetParent(Target) != NULL)
        {
            UIScreen_InternalGetAbsoluteBox(Widget_GetParent(Target), &ParentPosition, &ParentSize);
        }
        UILayoutContext Context = { .Measure = &Measure, .ParentAbsoluteSize = ParentSize };
        Error Result = Widget_Layout(Target, &Context);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

static Error RunUpdate(UIScreen* self, ProgramTime time, const UIScreenInput* input)
{
    Error Result = TakeSnapshot(self);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_InternalRouteInput(self, time, input);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_InternalAdvanceAnimations(self, time);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UpdateWidgets(self, time);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = LayoutWidgets(self);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_InternalDispatchEvents(self);
    }
    return Result;
}


// Internal functions (declared in UIInternal.h).
Error UIScreen_InternalReserveWidget(UIScreen* self, Widget* widget)
{
    if (self->_isTearingDown)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen: no widget can be created while the screen is torn down.");
    }

    size_t Index = 0;
    if (self->_freeSlotHead != 0U)
    {
        Index = (size_t)(self->_freeSlotHead - 1U);
        RegistrySlot* FreeSlotEntry = GenericBuffer_GetPointerToElement(&self->_slots, Index);
        self->_freeSlotHead = FreeSlotEntry->NextFree;
    }
    else
    {
        RegistrySlot NewSlot = { .Widget = NULL, .Generation = 1U, .State = SlotState_Free, .NextFree = 0U };
        if ((self->_slots._count >= MAX_SLOT_COUNT) || !GenericBuffer_AddLast(&self->_slots, &NewSlot))
        {
            return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen: the widget registry could not grow.");
        }
        Index = self->_slots._count - 1U;
    }

    RegistrySlot* Slot = GenericBuffer_GetPointerToElement(&self->_slots, Index);
    Slot->Widget = widget;
    Slot->State = SlotState_Reserved;
    Slot->NextFree = 0U;
    widget->_id = MakeId(Index, Slot->Generation);
    return Error_CreateSuccess();
}

void UIScreen_InternalCancelWidget(UIScreen* self, uint64_t widgetId)
{
    size_t Index = 0;
    RegistrySlot* Slot = FindSlot(self, widgetId, &Index);
    if ((Slot != NULL) && (Slot->State == SlotState_Reserved))
    {
        FreeSlot(self, Index);
    }
}

void UIScreen_InternalActivateWidget(UIScreen* self, uint64_t widgetId)
{
    RegistrySlot* Slot = FindSlot(self, widgetId, NULL);
    if ((Slot != NULL) && (Slot->State == SlotState_Reserved))
    {
        Slot->State = SlotState_Alive;
        self->_aliveWidgetCount++;
    }
}

Widget* UIScreen_InternalResolve(const UIScreen* self, uint64_t widgetId)
{
    const RegistrySlot* Slot = FindSlot(self, widgetId, NULL);
    return ((Slot != NULL) && (Slot->State == SlotState_Alive)) ? Slot->Widget : NULL;
}

uint64_t UIScreen_InternalNextSequence(UIScreen* self)
{
    return self->_nextSequence++;
}

void UIScreen_InternalInvalidateHitTest(UIScreen* self)
{
    self->_isHitTestDirty = true;
}

void UIScreen_InternalOnZChanged(UIScreen* self, Widget* widget)
{
    if (widget->_isScreenRoot)
    {
        Widget_InternalResortInDrawOrder(&self->_rootDrawOrder, widget);
    }
    else if (widget->_parent != NULL)
    {
        Widget_InternalResortInDrawOrder(&widget->_parent->_drawOrder, widget);
    }
    self->_isHitTestDirty = true;
}

Error UIScreen_InternalDetachWidget(UIScreen* self, Widget* widget)
{
    UnlinkWidget(self, widget);

    // Decide everything first (the widget is detached, so its subtree is what still has it as an ancestor), then run
    // the hooks last, re-resolving each widget by id: a hook may destroy or move anything.
    uint64_t UnhoveredId = WIDGET_ID_INVALID;
    Widget* Hovered = UIScreen_InternalResolve(self, self->_hoveredWidgetId);
    if ((Hovered != NULL) && UIScreen_InternalIsInSubtree(Hovered, widget))
    {
        UnhoveredId = self->_hoveredWidgetId;
        self->_hoveredWidgetId = WIDGET_ID_INVALID;
    }
    uint64_t UnfocusedId = WIDGET_ID_INVALID;
    Widget* Focused = UIScreen_InternalResolve(self, self->_focusedWidgetId);
    if ((Focused != NULL) && UIScreen_InternalIsInSubtree(Focused, widget))
    {
        UnfocusedId = self->_focusedWidgetId;
        self->_focusedWidgetId = WIDGET_ID_INVALID;
    }
    uint64_t LeftIds[WIDGET_MAX_TREE_DEPTH];
    size_t LeftCount = 0;
    for (size_t Index = 0; Index < self->_navStack._count; Index++)
    {
        uint64_t EnteredId = *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_navStack, Index);
        Widget* Entered = UIScreen_InternalResolve(self, EnteredId);
        if ((Entered != NULL) && UIScreen_InternalIsInSubtree(Entered, widget))
        {
            // The stack is a path: this entry and every one after it are inside the detached subtree.
            for (size_t Inner = Index; (Inner < self->_navStack._count) && (LeftCount < WIDGET_MAX_TREE_DEPTH); Inner++)
            {
                LeftIds[LeftCount++] = *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_navStack, Inner);
            }
            GenericBuffer_SetCount(&self->_navStack, Index);
            break;
        }
    }
    for (size_t Index = 0; Index < UI_SCREEN_MOUSE_BUTTON_COUNT; Index++)
    {
        Widget* Target = UIScreen_InternalResolve(self, self->_buttons[Index].TargetWidgetId);
        if (self->_buttons[Index].IsPressed && (Target != NULL) && UIScreen_InternalIsInSubtree(Target, widget))
        {
            self->_buttons[Index].IsPressed = false;
            self->_buttons[Index].TargetWidgetId = WIDGET_ID_INVALID;
        }
    }

    Error FirstError = Error_CreateSuccess();
    Widget* Unhovered = UIScreen_InternalResolve(self, UnhoveredId);
    if (Unhovered != NULL)
    {
        Error Result = Widget_InternalSetHovered(Unhovered, false, NULL);
        KeepFirstError(&FirstError, &Result);
    }
    Widget* Unfocused = UIScreen_InternalResolve(self, UnfocusedId);
    if (Unfocused != NULL)
    {
        Error Result = Widget_InternalSetFocused(Unfocused, false);
        KeepFirstError(&FirstError, &Result);
    }
    for (size_t Index = LeftCount; Index-- > 0U;)
    {
        Widget* Left = UIScreen_InternalResolve(self, LeftIds[Index]);
        if (Left != NULL)
        {
            Error Result = Widget_InternalSetTabbed(Left, false);
            KeepFirstError(&FirstError, &Result);
        }
    }
    return FirstError;
}

Error UIScreen_InternalDestroyWidget(UIScreen* self, Widget* widget)
{
    if (widget->_isDying)
    {
        return Error_CreateSuccess();
    }
    RegistrySlot* Slot = FindSlot(self, widget->_id, NULL);
    if ((Slot == NULL) || (Slot->State != SlotState_Alive))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "Widget_Destroy: the widget is not alive in its screen.");
    }
    // Queued first, so a failure changes nothing.
    if (!GenericBuffer_AddLast(&self->_pendingDestroy, &widget))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "Widget_Destroy: the screen could not record the widget.");
    }
    UnlinkWidget(self, widget);
    MarkSubtreeDying(self, widget);
    ForgetDyingReferences(self);
    return Error_CreateSuccess();
}

bool UIScreen_InternalIsInSubtree(const Widget* node, const Widget* ancestor)
{
    for (const Widget* Current = node; Current != NULL; Current = Current->_parent)
    {
        if (Current == ancestor)
        {
            return true;
        }
    }
    return false;
}

bool UIScreen_InternalIsAttached(const Widget* widget)
{
    for (const Widget* Current = widget; Current != NULL; Current = Current->_parent)
    {
        if (Current->_isScreenRoot)
        {
            return true;
        }
    }
    return false;
}

void UIScreen_InternalGetAbsoluteBox(const Widget* widget, Vector2* outPosition, Vector2* outSize)
{
    Vector2 ParentPosition = { .x = 0.0f, .y = 0.0f };
    Vector2 ParentSize = { .x = 1.0f, .y = 1.0f };
    if (widget->_parent != NULL)
    {
        UIScreen_InternalGetAbsoluteBox(widget->_parent, &ParentPosition, &ParentSize); // Bounded by the tree depth.
    }
    outPosition->x = ParentPosition.x + (widget->_position.x * ParentSize.x);
    outPosition->y = ParentPosition.y + (widget->_position.y * ParentSize.y);
    outSize->x = widget->_size.x * ParentSize.x;
    outSize->y = widget->_size.y * ParentSize.y;
}

Rectangle UIScreen_InternalGetEstimatedScreenBox(Widget* widget)
{
    return ParentSpaceToScreen(widget, Widget_GetEstimatedBounds(widget));
}

Rectangle UIScreen_InternalPeekEstimatedScreenBox(const Widget* widget)
{
    return ParentSpaceToScreen(widget, Widget_InternalPeekEstimatedBounds(widget));
}


// Public functions: lifecycle.
Error UIScreen_Construct(UIScreen* self, UIWidgetFactory* factory, Logger* logger)
{
    if ((self == NULL) || (factory == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_Construct: self and factory must not be NULL.");
    }

    Memory_Zero(self, sizeof(*self));
    self->_factory = factory;
    self->_logger = logger;
    self->_viewport = UIViewport_Create((Vector2){ .x = DEFAULT_VIEWPORT_WIDTH, .y = DEFAULT_VIEWPORT_HEIGHT },
        DEFAULT_VIEWPORT_ASPECT_RATIO, RenderTargetPosition_Centered());

    Error AnimationResult = UIScreen_InternalInitializeAnimations(self);
    if (AnimationResult.Code != ErrorCode_Success)
    {
        return AnimationResult;
    }
    GenericBuffer_AllocateVariable(&self->_slots, 16U, sizeof(RegistrySlot));
    GenericBuffer_AllocateVariable(&self->_roots, 4U, sizeof(Widget*));
    GenericBuffer_AllocateVariable(&self->_rootDrawOrder, 4U, sizeof(Widget*));
    GenericBuffer_AllocateVariable(&self->_pendingDestroy, 4U, sizeof(Widget*));
    GenericBuffer_AllocateVariable(&self->_updateSnapshot, 16U, sizeof(uint64_t));
    UIScreen_InternalInitializeNavigation(self);
    UIScreen_InternalInitializeEvents(self);

    self->_nextSequence = 1U;
    return Error_CreateSuccess();
}

Error UIScreen_Deconstruct(UIScreen* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }
    if (self->_isUpdating || self->_isTearingDown)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIScreen_Deconstruct: called from inside UIScreen_Update or UIScreen_Deconstruct.");
    }

    // Destroy every top-most alive widget (roots and detached ones); that covers every alive widget.
    self->_isTearingDown = true;
    Error FirstError = Error_CreateSuccess();
    for (size_t Index = 0; Index < self->_slots._count; Index++)
    {
        RegistrySlot* Slot = GenericBuffer_GetPointerToElement(&self->_slots, Index);
        if ((Slot->State == SlotState_Alive) && (Slot->Widget->_parent == NULL))
        {
            Error DestroyResult = UIScreen_InternalDestroyWidget(self, Slot->Widget);
            KeepFirstError(&FirstError, &DestroyResult);
        }
    }
    Error FlushResult = FlushDestroyedWidgets(self);
    KeepFirstError(&FirstError, &FlushResult);

    Error AnimationResult = UIScreen_InternalReleaseAnimations(self);
    KeepFirstError(&FirstError, &AnimationResult);
    UIScreen_InternalReleaseEvents(self);
    UIScreen_InternalReleaseNavigation(self);
    Memory_Free(self->_slots._data);
    Memory_Free(self->_roots._data);
    Memory_Free(self->_rootDrawOrder._data);
    Memory_Free(self->_pendingDestroy._data);
    Memory_Free(self->_updateSnapshot._data);

    // The factory is borrowed (program-wide); the screen does not deconstruct it.
    Memory_Zero(self, sizeof(*self));
    return FirstError;
}


// Public functions: viewport.
Error UIScreen_SetViewport(UIScreen* self, const UIViewport* viewport)
{
    if ((self == NULL) || (viewport == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_SetViewport: self and viewport must not be NULL.");
    }
    bool IsSizeValid = IsFloatFinite(viewport->SizePixels.x) && IsFloatFinite(viewport->SizePixels.y)
        && (viewport->SizePixels.x > 0.0f) && (viewport->SizePixels.y > 0.0f);
    bool IsAspectValid = IsFloatFinite(viewport->TargetAspectRatio) && (viewport->TargetAspectRatio > 0.0f);
    bool IsPlacementValid = IsFloatFinite(viewport->TargetRelativePosition.x) && IsFloatFinite(viewport->TargetRelativePosition.y);
    if (!IsSizeValid || !IsAspectValid || !IsPlacementValid)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIScreen_SetViewport: the size and aspect ratio must be finite and positive, the placement finite.");
    }
    self->_viewport = *viewport;
    return Error_CreateSuccess();
}


// Public functions: widgets by id.
bool UIScreen_TryGetWidgetById(const UIScreen* self, uint64_t widgetId, Widget** outWidget)
{
    if ((self == NULL) || (outWidget == NULL))
    {
        return false;
    }
    *outWidget = UIScreen_InternalResolve(self, widgetId);
    return *outWidget != NULL;
}

bool UIScreen_IsWidgetAlive(const UIScreen* self, uint64_t widgetId)
{
    return (self != NULL) && (UIScreen_InternalResolve(self, widgetId) != NULL);
}


// Public functions: root widgets.
Error UIScreen_AddWidget(UIScreen* self, Widget* widget)
{
    if ((self == NULL) || (widget == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_AddWidget: self and widget must not be NULL.");
    }
    if (Widget_GetScreen(widget) != self)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_AddWidget: the widget belongs to another screen.");
    }
    if (widget->_isDying || !widget->_isInitialized)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_AddWidget: the widget must be initialized and not being destroyed.");
    }
    if ((widget->_parent != NULL) || widget->_isScreenRoot)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_AddWidget: the widget is already attached.");
    }

    if (!GenericBuffer_AddLast(&self->_roots, &widget))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_AddWidget: the widget could not be stored.");
    }
    Widget_InternalSetScreenRoot(widget, true, UIScreen_InternalNextSequence(self));
    if (!Widget_InternalInsertIntoDrawOrder(&self->_rootDrawOrder, widget))
    {
        GenericBuffer_RemoveLast(&self->_roots);
        Widget_InternalSetScreenRoot(widget, false, 0U);
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_AddWidget: the widget could not be stored.");
    }
    self->_isHitTestDirty = true;
    return Error_CreateSuccess();
}

Error UIScreen_RemoveWidget(UIScreen* self, Widget* widget)
{
    if ((self == NULL) || (widget == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_RemoveWidget: self and widget must not be NULL.");
    }
    if ((Widget_GetScreen(widget) != self) || !widget->_isScreenRoot || widget->_isDying)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIScreen_RemoveWidget: the widget is not a root of this screen, or is being destroyed.");
    }
    return UIScreen_InternalDetachWidget(self, widget);
}

size_t UIScreen_GetRootCount(const UIScreen* self)
{
    return self->_roots._count;
}

Widget* UIScreen_GetRootAt(const UIScreen* self, size_t index)
{
    if (index >= self->_roots._count)
    {
        return NULL;
    }
    return *(Widget**)GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_roots, index);
}


// Public functions: z ordering and hit testing.
Error UIScreen_BringToTop(UIScreen* self, Widget* widget)
{
    if ((self == NULL) || (widget == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_BringToTop: self and widget must not be NULL.");
    }

    const GenericBuffer* Siblings = (widget->_parent != NULL) ? &widget->_parent->_drawOrder : &self->_rootDrawOrder;
    if (!widget->_isScreenRoot && (widget->_parent == NULL))
    {
        return Error_CreateSuccess(); // Detached: no siblings.
    }
    // The draw order is sorted, so the last other widget has the highest z. Already on top: leave the z alone, so
    // repeated clicks do not make it grow.
    Widget* Topmost = (Siblings->_count > 0U) ? Widget_InternalGetDrawOrderAt(Siblings, Siblings->_count - 1U) : NULL;
    if ((Topmost == NULL) || (Topmost == widget))
    {
        return Error_CreateSuccess();
    }
    return Widget_SetZLayer(widget, Widget_GetZLayer(Topmost) + BRING_TO_TOP_Z_STEP);
}

bool UIScreen_TryGetTopmostAt(UIScreen* self, Vector2 screenPosition, Widget** outWidget)
{
    if ((self == NULL) || (outWidget == NULL))
    {
        return false;
    }
    *outWidget = NULL;
    Vector2 RootPosition = { .x = 0.0f, .y = 0.0f };
    Vector2 RootSize = { .x = 1.0f, .y = 1.0f };
    for (size_t Index = self->_rootDrawOrder._count; Index-- > 0U;)
    {
        Widget* Hit = HitTestNode(Widget_InternalGetDrawOrderAt(&self->_rootDrawOrder, Index), screenPosition, RootPosition, RootSize);
        if (Hit != NULL)
        {
            *outWidget = Hit;
            return true;
        }
    }
    return false;
}


// Public functions: coordinate conversion.
Vector2 UIScreen_WidgetToScreen(const UIScreen* self, const Widget* widget, Vector2 widgetLocal)
{
    UNUSED(self);
    Vector2 AbsolutePosition;
    Vector2 AbsoluteSize;
    UIScreen_InternalGetAbsoluteBox(widget, &AbsolutePosition, &AbsoluteSize);
    return (Vector2)
    {
        .x = AbsolutePosition.x + (widgetLocal.x * AbsoluteSize.x),
        .y = AbsolutePosition.y + (widgetLocal.y * AbsoluteSize.y)
    };
}

Vector2 UIScreen_ScreenToWidget(const UIScreen* self, const Widget* widget, Vector2 screenPosition)
{
    UNUSED(self);
    Vector2 AbsolutePosition;
    Vector2 AbsoluteSize;
    UIScreen_InternalGetAbsoluteBox(widget, &AbsolutePosition, &AbsoluteSize);
    return (Vector2)
    {
        .x = (AbsoluteSize.x != 0.0f) ? ((screenPosition.x - AbsolutePosition.x) / AbsoluteSize.x) : 0.0f,
        .y = (AbsoluteSize.y != 0.0f) ? ((screenPosition.y - AbsolutePosition.y) / AbsoluteSize.y) : 0.0f
    };
}


// Public functions: update and render.
Error UIScreen_Update(UIScreen* self, ProgramTime time, const UIScreenInput* input)
{
    if ((self == NULL) || (input == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_Update: self and input must not be NULL.");
    }
    if (self->_isUpdating || self->_isTearingDown)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_Update: the screen is already updating or is torn down.");
    }

    self->_isUpdating = true;
    Error Result = RunUpdate(self, time, input);
    // Destroyed widgets are freed even after a failure, while no callback is running.
    Error FlushResult = FlushDestroyedWidgets(self);
    self->_input = NULL;
    self->_isUpdating = false;
    KeepFirstError(&Result, &FlushResult);
    return Result;
}

Error UIScreen_Render(UIScreen* self, RenderContext* renderContext)
{
    if ((self == NULL) || (renderContext == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_Render: self and renderContext must not be NULL.");
    }

    Vector2 RootPosition = { .x = 0.0f, .y = 0.0f };
    Vector2 RootSize = { .x = 1.0f, .y = 1.0f };
    for (size_t Index = 0; Index < self->_rootDrawOrder._count; Index++)
    {
        Error Result = RenderNode(renderContext, Widget_InternalGetDrawOrderAt(&self->_rootDrawOrder, Index), RootPosition,
            RootSize, RenderColor_White());
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    DrawNavigationOutlines(self, renderContext);
    return Error_CreateSuccess();
}
