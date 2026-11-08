#include "UIScreen.h"
#include "UIInternal.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "UIRenderContext.h"
#include "Renderer.h"
#include "AssetManager.h"
#include "Logger.h"
#include "UITheme.h"
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
/** A clip's edges within this many pixels of a whole one are that whole pixel: a placed box's edges are whole pixels that
 *  came back through normalized floats, and rounding them outwards would clip a pixel too wide. */
#define CLIP_SNAP_PIXELS 0.002f


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
    // A destroyed edit element is left silently, with no hook, as destroying promises.
    if (IsIdDying(self, self->_enteredWidgetId))
    {
        self->_enteredWidgetId = WIDGET_ID_INVALID;
    }
    // The hover text that shows goes with its widget at once.
    if (IsIdDying(self, UIScreen_InternalGetCurrentTooltipWidgetId(self)))
    {
        UIScreen_InternalForgetCurrentTooltip(self);
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
    // Destroying stays silent for a press too: no OnPressLost, since whoever destroys a widget knows.
    for (size_t Index = 0; Index < UI_SCREEN_MOUSE_BUTTON_COUNT; Index++)
    {
        if (self->_buttons[Index].IsPressed && IsIdDying(self, self->_buttons[Index].TargetWidgetId))
        {
            UIScreen_InternalEndPress(self, Index);
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
    UIScreen_InternalRemoveWidgetTooltip(self, Id);
    UIScreen_InternalRemoveWidgetContextMenu(self, Id);
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
/* A box of no size holds no point, also where its corner lies exactly on one. */
static bool PointInBox(Vector2 point, Vector2 position, Vector2 size)
{
    return (size.x > 0.0f) && (size.y > 0.0f) && (point.x >= position.x) && (point.x <= (position.x + size.x))
        && (point.y >= position.y) && (point.y <= (position.y + size.y));
}

/* Whether drawing and pointing go through the widget's render transform: one that changes nothing costs nothing. */
static bool HasActiveTransform(const Widget* widget)
{
    return widget->_hasRenderTransform && !UIRenderTransform_IsIdentity(&widget->_renderTransform);
}

/* A box in normalized screen coordinates, in surface pixels. */
static Rectangle BoxToPixels(Vector2 position, Vector2 size, Vector2 surfacePixels)
{
    return (Rectangle){ .x = position.x * surfacePixels.x, .y = position.y * surfacePixels.y, .width = size.x * surfacePixels.x,
        .height = size.y * surfacePixels.y };
}

/* Where in its box a widget is shown at a point of its parent's space (normalized screen coordinates, the transforms above
 * it undone), its own transform undone too. False when its transform holds no point. */
static bool TryUndoTransform(const Widget* widget, Vector2 position, Vector2 size, Vector2 surfacePixels, Vector2 point,
    Vector2* outPoint)
{
    if (!HasActiveTransform(widget))
    {
        *outPoint = point;
        return true;
    }
    Vector2 Mapped;
    Vector2 PointPixels = { .x = point.x * surfacePixels.x, .y = point.y * surfacePixels.y };
    if (!UIRenderTransform_TryInvertPoint(&widget->_renderTransform, BoxToPixels(position, size, surfacePixels),
        Widget_InternalGetParentScale(widget), PointPixels, &Mapped))
    {
        return false;
    }
    *outPoint = (Vector2){ .x = Mapped.x / surfacePixels.x, .y = Mapped.y / surfacePixels.y };
    return true;
}

/* Where a point of a widget's box (normalized screen coordinates, as layout sees it) shows in its parent's space. */
static Vector2 ApplyTransform(const Widget* widget, Vector2 position, Vector2 size, Vector2 surfacePixels, Vector2 point)
{
    if (!HasActiveTransform(widget))
    {
        return point;
    }
    Vector2 PointPixels = { .x = point.x * surfacePixels.x, .y = point.y * surfacePixels.y };
    Vector2 Shown = UIRenderTransform_ApplyToPoint(&widget->_renderTransform, BoxToPixels(position, size, surfacePixels),
        Widget_InternalGetParentScale(widget), PointPixels);
    return (Vector2){ .x = Shown.x / surfacePixels.x, .y = Shown.y / surfacePixels.y };
}

/* A point in a box, as a share of it per axis; 0 on an axis of no size. */
static Vector2 ToBoxShare(Vector2 point, Vector2 position, Vector2 size)
{
    return (Vector2)
    {
        .x = (size.x != 0.0f) ? ((point.x - position.x) / size.x) : 0.0f,
        .y = (size.y != 0.0f) ? ((point.y - position.y) / size.y) : 0.0f
    };
}

/* The top-most input-enabled widget of a subtree at a point; a widget that is not rendered is not there, with its subtree.
 * @p point is in the parent's space with the transforms above it undone; @p screenPosition is what the screen was asked about.
 * Recursion is bounded by WIDGET_MAX_TREE_DEPTH. */
static Widget* HitTestNode(Widget* widget, Vector2 screenPosition, Vector2 point, Vector2 parentPosition, Vector2 parentSize,
    Vector2 surfacePixels)
{
    if (!Widget_IsRendered(widget))
    {
        return NULL;
    }
    Vector2 Position = Widget_GetPosition(widget);
    Vector2 Size = Widget_GetSize(widget);
    Vector2 AbsolutePosition = { .x = parentPosition.x + (Position.x * parentSize.x), .y = parentPosition.y + (Position.y * parentSize.y) };
    Vector2 AbsoluteSize = { .x = Size.x * parentSize.x, .y = Size.y * parentSize.y };

    Vector2 Local = point;
    if (!TryUndoTransform(widget, AbsolutePosition, AbsoluteSize, surfacePixels, point, &Local))
    {
        return NULL; // A transform that holds no point: neither the widget nor anything it holds shows anywhere to be hit.
    }
    bool IsInside = PointInBox(Local, AbsolutePosition, AbsoluteSize);
    if (Widget_AreSubWidgetsCut(widget) && !IsInside)
    {
        return NULL; // Clipped: neither the widget nor its (clipped) children can be hit here.
    }
    for (size_t Index = widget->_drawOrder._count; Index-- > 0U;)
    {
        Widget* Hit = HitTestNode(Widget_InternalGetDrawOrderAt(&widget->_drawOrder, Index), screenPosition, Local,
            AbsolutePosition, AbsoluteSize, surfacePixels);
        if (Hit != NULL)
        {
            return Hit;
        }
    }
    if (!Widget_IsInputEnabled(widget))
    {
        return NULL;
    }
    bool IsHit = IsInside;
    if (widget->VTable->HitTest != NULL)
    {
        WidgetHitTestArgs Args = { .ScreenPosition = screenPosition, .LocalPosition = ToBoxShare(Local, AbsolutePosition, AbsoluteSize),
            .IsInsideBox = IsInside };
        IsHit = widget->VTable->HitTest(widget, &Args);
    }
    return IsHit ? widget : NULL;
}

/* A pixel edge, snapped when float noise is all that keeps it from a whole pixel. */
static float SnapClipEdge(float edge)
{
    float Nearest = Math_FloorFloat(edge + 0.5f);
    return (Math_AbsFloat(edge - Nearest) <= CLIP_SNAP_PIXELS) ? Nearest : edge;
}

static Rectangle ScreenBoxToPixels(RenderContext* renderContext, Vector2 position, Vector2 size)
{
    Vector2 PixelPosition = RenderContext_VectorRelativeToPixel(renderContext, position);
    Vector2 PixelSize = RenderContext_VectorRelativeToPixel(renderContext, size);
    float Left = SnapClipEdge(PixelPosition.x);
    float Top = SnapClipEdge(PixelPosition.y);
    float Right = SnapClipEdge(PixelPosition.x + PixelSize.x);
    float Bottom = SnapClipEdge(PixelPosition.y + PixelSize.y);
    return (Rectangle){ .x = Left, .y = Top, .width = Right - Left, .height = Bottom - Top };
}

static Error RenderNode(RenderContext* renderContext, Widget* widget, Vector2 parentPosition, Vector2 parentSize,
    RenderColor parentTint);

/* Draws a widget, then its children by draw order (clipped to its box when it cuts them). */
static Error RenderWidgetAndChildren(RenderContext* renderContext, Widget* widget, Vector2 absolutePosition, Vector2 absoluteSize,
    RenderColor tint)
{
    UIRenderContext Context;
    UIRenderContext_Create(&Context, renderContext, absolutePosition, absoluteSize, tint);
    Error RenderResult = Widget_Render(widget, &Context);
    if (RenderResult.Code != ErrorCode_Success)
    {
        return RenderResult;
    }
    if (widget->_drawOrder._count == 0U)
    {
        return Error_CreateSuccess();
    }

    bool IsClipped = Widget_AreSubWidgetsCut(widget);
    if (IsClipped)
    {
        RenderContext_PushClip(renderContext, ScreenBoxToPixels(renderContext, absolutePosition, absoluteSize));
    }
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0; (Index < widget->_drawOrder._count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = RenderNode(renderContext, Widget_InternalGetDrawOrderAt(&widget->_drawOrder, Index), absolutePosition,
            absoluteSize, tint);
    }
    if (IsClipped)
    {
        RenderContext_PopClip(renderContext);
    }
    return Result;
}

/* Draws a subtree; one that is not rendered hides its subtree, and a render transform turns all of it. Whatever it pushes it pops
 * on every path, an error included. Recursion is bounded by WIDGET_MAX_TREE_DEPTH. */
static Error RenderNode(RenderContext* renderContext, Widget* widget, Vector2 parentPosition, Vector2 parentSize,
    RenderColor parentTint)
{
    if (!Widget_IsRendered(widget))
    {
        return Error_CreateSuccess();
    }
    Vector2 Position = Widget_GetPosition(widget);
    Vector2 Size = Widget_GetSize(widget);
    Vector2 AbsolutePosition = { .x = parentPosition.x + (Position.x * parentSize.x), .y = parentPosition.y + (Position.y * parentSize.y) };
    Vector2 AbsoluteSize = { .x = Size.x * parentSize.x, .y = Size.y * parentSize.y };
    RenderColor Tint = UIRenderColor_Multiply(parentTint, Widget_GetRenderTint(widget));

    bool IsTransformed = HasActiveTransform(widget);
    if (IsTransformed)
    {
        Rectangle BoxPixels = BoxToPixels(AbsolutePosition, AbsoluteSize, renderContext->_renderBufferSizePixels);
        Matrix Local = UIRenderTransform_GetMatrix(&widget->_renderTransform, BoxPixels, Widget_InternalGetParentScale(widget));
        RenderContext_PushTransform(renderContext, &Local);
    }
    Error Result = RenderWidgetAndChildren(renderContext, widget, AbsolutePosition, AbsoluteSize, Tint);
    if (IsTransformed)
    {
        RenderContext_PopTransform(renderContext);
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

/* Draws the keyboard-navigation outlines: green around each widget navigation entered and around an entered edit element,
 * white around a focused widget that is only focused. */
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
        DrawOutline(renderContext, Focused, (self->_enteredWidgetId == self->_focusedWidgetId) ? Green : RenderColor_White());
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

/* Turns a widget's placement into its position and size, writing them only when they change: an idle screen does not
 * hit-test again every tick. Its offsets count in its parent's effective scale (a root's: the screen's). */
static Error ApplyPlacement(const UIScreen* self, Widget* widget, Vector2 parentPosition, Vector2 parentSize)
{
    Vector2 Position;
    Vector2 Size;
    UIPlacement_Resolve(&widget->_placement, parentPosition, parentSize, self->_viewport.SizePixels,
        Widget_InternalGetParentScale(widget), &Position, &Size);
    if ((Position.x != widget->_position.x) || (Position.y != widget->_position.y))
    {
        Error Result = Widget_SetPosition(widget, Position);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    if ((Size.x != widget->_size.x) || (Size.y != widget->_size.y))
    {
        return Widget_SetSize(widget, Size);
    }
    return Error_CreateSuccess();
}

/* The height in pixels of one normalized-fitted unit: the target area's height on the surface. */
static float GetFittedPixelsPerUnit(const UIViewport* viewport)
{
    float BufferAspectRatio = viewport->SizePixels.x / viewport->SizePixels.y;
    float Quotient = viewport->TargetAspectRatio / BufferAspectRatio;
    return viewport->SizePixels.y * ((Quotient >= 1.0f) ? (1.0f / Quotient) : 1.0f);
}

/* Lays out every snapshot widget that is still alive and attached, parents first (the snapshot is in pre-order): a widget's
 * placement, which its parent's Layout hook may just have written, then its own hook. */
static Error LayoutWidgets(UIScreen* self)
{
    RenderContext Measure;
    RenderContext_CreateMeasuring(&Measure, self->_viewport.SizePixels, self->_viewport.TargetAspectRatio,
        self->_viewport.TargetRelativePosition);
    UIMeasureContext Sizing = { .Measure = &Measure };
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
        if (Target->_hasPlacement)
        {
            Error PlacementResult = ApplyPlacement(self, Target, ParentPosition, ParentSize);
            if (PlacementResult.Code != ErrorCode_Success)
            {
                return PlacementResult;
            }
        }
        UILayoutContext Context =
        {
            .Measure = &Measure,
            .ParentAbsoluteSize = ParentSize,
            .ParentAbsolutePosition = ParentPosition,
            .SurfaceSizePixels = self->_viewport.SizePixels,
            .Sizing = &Sizing
        };
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
        Result = UIScreen_InternalUpdateTooltip(self, time);
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
        Result = UIScreen_InternalPlacePopups(self);
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
    uint64_t UnenteredId = WIDGET_ID_INVALID;
    Widget* EditedElement = UIScreen_InternalResolve(self, self->_enteredWidgetId);
    if ((EditedElement != NULL) && UIScreen_InternalIsInSubtree(EditedElement, widget))
    {
        UnenteredId = self->_enteredWidgetId;
        self->_enteredWidgetId = WIDGET_ID_INVALID;
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
    uint64_t LostIds[UI_SCREEN_MOUSE_BUTTON_COUNT];
    GameKey LostButtons[UI_SCREEN_MOUSE_BUTTON_COUNT];
    size_t LostCount = 0;
    for (size_t Index = 0; Index < UI_SCREEN_MOUSE_BUTTON_COUNT; Index++)
    {
        Widget* Target = UIScreen_InternalResolve(self, self->_buttons[Index].TargetWidgetId);
        if (self->_buttons[Index].IsPressed && (Target != NULL) && UIScreen_InternalIsInSubtree(Target, widget))
        {
            if (self->_buttons[Index].HasReachedWidget)
            {
                LostIds[LostCount] = self->_buttons[Index].TargetWidgetId;
                LostButtons[LostCount] = (GameKey)((int32_t)GameKey_MouseLeft + (int32_t)Index);
                LostCount++;
            }
            UIScreen_InternalEndPress(self, Index);
        }
    }

    Error FirstError = Error_CreateSuccess();
    Widget* Unhovered = UIScreen_InternalResolve(self, UnhoveredId);
    if (Unhovered != NULL)
    {
        Error Result = Widget_InternalSetHovered(Unhovered, false, NULL);
        KeepFirstError(&FirstError, &Result);
    }
    Widget* Unentered = UIScreen_InternalResolve(self, UnenteredId);
    if (Unentered != NULL)
    {
        Error Result = Widget_InternalLeave(Unentered, UILeaveReason_Keep);
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
    for (size_t Index = 0; Index < LostCount; Index++)
    {
        Widget* Lost = UIScreen_InternalResolve(self, LostIds[Index]);
        if (Lost != NULL)
        {
            Error Result = Widget_OnPressLost(Lost, LostButtons[Index]);
            KeepFirstError(&FirstError, &Result);
        }
    }
    return FirstError;
}

/* Adds a widget to the roots. Returns false, with nothing changed, when the lists could not grow. */
static bool LinkRoot(UIScreen* self, Widget* widget)
{
    if (!GenericBuffer_AddLast(&self->_roots, &widget))
    {
        return false;
    }
    Widget_InternalSetScreenRoot(widget, true, UIScreen_InternalNextSequence(self));
    if (!Widget_InternalInsertIntoDrawOrder(&self->_rootDrawOrder, widget))
    {
        GenericBuffer_RemoveLast(&self->_roots);
        Widget_InternalSetScreenRoot(widget, false, 0U);
        return false;
    }
    Widget_InternalUpdateEffectiveScale(widget, self->_scale);
    self->_isHitTestDirty = true;
    return true;
}

/* After a widget moved, or a root's layer changed: focus and presses that can no longer be go first, and then the entered path, which follows focus. */
static Error SettleAfterMove(UIScreen* self)
{
    Error FirstError = UIScreen_InternalReleaseUnreachable(self);
    Error PathResult = UIScreen_InternalOnWidgetMoved(self);
    KeepFirstError(&FirstError, &PathResult);
    return FirstError;
}

/* Whether the lists a widget is linked into can take one more. */
static bool MakeRoomForOneMore(GenericBuffer* list, GenericBuffer* drawOrder)
{
    return GenericBuffer_EnsureTotalCapacity(list, list->_count + 1U) && GenericBuffer_EnsureTotalCapacity(drawOrder, drawOrder->_count + 1U);
}

Error UIScreen_InternalMoveWidget(UIScreen* self, Widget* widget, Widget* newParent)
{
    bool HasRoom = (newParent != NULL) ? MakeRoomForOneMore(&newParent->_subWidgets, &newParent->_drawOrder)
        : MakeRoomForOneMore(&self->_roots, &self->_rootDrawOrder);
    if (!HasRoom)
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "Widget_MoveTo: the new parent's lists could not grow.");
    }
    UnlinkWidget(self, widget);
    // Neither can fail: the room is there.
    if (newParent != NULL)
    {
        Widget_InternalLinkToParent(newParent, widget);
    }
    else
    {
        LinkRoot(self, widget);
    }
    Widget_InvalidateMeasure(widget);
    return SettleAfterMove(self);
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

bool UIScreen_InternalIsInOverlay(const Widget* widget)
{
    const Widget* Root = widget;
    while (Root->_parent != NULL)
    {
        Root = Root->_parent;
    }
    return Widget_InternalGetDrawLayer(Root) == UIScreenLayer_Overlay;
}

bool UIScreen_InternalIsShown(const Widget* widget)
{
    for (const Widget* Current = widget; Current != NULL; Current = Current->_parent)
    {
        if (!Current->_isRendered)
        {
            return false;
        }
    }
    return true;
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
    self->_fittedPixelsPerUnit = GetFittedPixelsPerUnit(&self->_viewport);
    self->_scale = 1.0f;
    self->_measureGeneration = 1U;

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
    UIScreen_InternalInitializeTooltips(self);
    UIScreen_InternalInitializePopups(self);
    UIScreen_InternalInitializeContextMenus(self);

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
    if (self->_theme != NULL)
    {
        Error RetireResult = AssetManager_RetireUser(self->_themeAssets, self->_themeUser);
        KeepFirstError(&FirstError, &RetireResult);
    }

    Error AnimationResult = UIScreen_InternalReleaseAnimations(self);
    KeepFirstError(&FirstError, &AnimationResult);
    UIScreen_InternalReleaseEvents(self);
    UIScreen_InternalReleaseNavigation(self);
    UIScreen_InternalReleaseTooltips(self);
    UIScreen_InternalReleasePopups(self);
    UIScreen_InternalReleaseContextMenus(self);
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
    bool HasFittedAreaChanged = (viewport->SizePixels.x != self->_viewport.SizePixels.x)
        || (viewport->SizePixels.y != self->_viewport.SizePixels.y)
        || (viewport->TargetAspectRatio != self->_viewport.TargetAspectRatio);
    self->_viewport = *viewport;
    if (HasFittedAreaChanged)
    {
        self->_fittedPixelsPerUnit = GetFittedPixelsPerUnit(&self->_viewport);
        self->_measureGeneration++; // Text in fitted units measures differently in UI pixels now.
    }
    return Error_CreateSuccess();
}

Error UIScreen_SetScale(UIScreen* self, float scale)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_SetScale: self must not be NULL.");
    }
    if (!(scale >= UI_SCREEN_MIN_SCALE) || !(scale <= UI_SCREEN_MAX_SCALE))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIScreen_SetScale: the scale must be a finite number within "
            "[UI_SCREEN_MIN_SCALE, UI_SCREEN_MAX_SCALE].");
    }
    if (scale != self->_scale)
    {
        self->_scale = scale;
        self->_measureGeneration++;
        self->_isHitTestDirty = true; // Visual offsets count in the scale: what shows under the pointer can move with no box changing.
        for (size_t Index = 0; Index < self->_roots._count; Index++)
        {
            Widget_InternalUpdateEffectiveScale(*(Widget**)GenericBuffer_GetPointerToElement(&self->_roots, Index), scale);
        }
    }
    return Error_CreateSuccess();
}


// Public functions: theme.
/* The theme has just changed: cached sizes are old, and what was logged about the last one may hold again. */
static void OnThemeChanged(UIScreen* self, const UITheme* theme)
{
    self->_theme = theme;
    self->_themeRevision++;
    self->_measureGeneration++;
    self->_isMissingThemeReported = false;
    Memory_Zero(self->_reportedThemeParts, sizeof(self->_reportedThemeParts));
    self->_isHitTestDirty = true;
}

Error UIScreen_SetTheme(UIScreen* self, AssetManager* assets, const unsigned char* themeName)
{
    if ((self == NULL) || (assets == NULL) || (themeName == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_SetTheme: self, assets and themeName must not be NULL.");
    }

    // Under a user of its own, and the new one first: a theme that does not load leaves the screen as it was (the manager
    // logged why, once), and retiring the old user afterwards releases exactly the old theme.
    AssetUserID User = ASSET_USER_ID_INVALID;
    Error UserResult = AssetManager_GetNewUserID(assets, &User);
    if (UserResult.Code != ErrorCode_Success)
    {
        return UserResult;
    }
    UITheme* Loaded = NULL;
    Error LoadResult = AssetManager_LoadUITheme(assets, themeName, User, &Loaded);
    if ((LoadResult.Code != ErrorCode_Success) || (Loaded == self->_theme))
    {
        Error_Deconstruct(&LoadResult);
        return AssetManager_RetireUser(assets, User);
    }

    Error RetireResult = Error_CreateSuccess();
    if (self->_theme != NULL)
    {
        RetireResult = AssetManager_RetireUser(self->_themeAssets, self->_themeUser);
    }
    self->_themeAssets = assets;
    self->_themeUser = User;
    OnThemeChanged(self, Loaded);
    return RetireResult;
}

Error UIScreen_ClearTheme(UIScreen* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_ClearTheme: self must not be NULL.");
    }
    if (self->_theme == NULL)
    {
        return Error_CreateSuccess();
    }
    Error RetireResult = AssetManager_RetireUser(self->_themeAssets, self->_themeUser);
    OnThemeChanged(self, NULL);
    return RetireResult;
}

void UIScreen_ReportMissingThemePart(UIScreen* self, const unsigned char* elementKindName, size_t elementKindIndex,
    const unsigned char* partName, size_t partIndex)
{
    if (self->_theme == NULL)
    {
        if (!self->_isMissingThemeReported && (self->_logger != NULL))
        {
            Error LogResult = Logger_LogWarning(self->_logger,
                (const unsigned char*)u8"UI: a screen with elements has no theme: they draw as plain boxes.");
            Error_Deconstruct(&LogResult);
        }
        self->_isMissingThemeReported = true;
        return;
    }
    if ((elementKindIndex >= UI_SCREEN_THEME_REPORT_KINDS) || (partIndex >= 32U))
    {
        return;
    }
    uint32_t Bit = (uint32_t)1U << (uint32_t)partIndex;
    if ((self->_reportedThemeParts[elementKindIndex] & Bit) != 0U)
    {
        return;
    }
    self->_reportedThemeParts[elementKindIndex] |= Bit;
    if (self->_logger != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(self->_logger,
            (const unsigned char*)u8"UI theme \"%s\" has no \"%s\" for the element \"%s\": it draws as a plain box.",
            (const char*)UITheme_GetName(self->_theme), (const char*)partName, (const char*)elementKindName);
        Error_Deconstruct(&LogResult);
    }
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

    if (!LinkRoot(self, widget))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_AddWidget: the widget could not be stored.");
    }
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


// Public functions: layers, z ordering and hit testing.
Error UIScreen_SetRootLayer(UIScreen* self, Widget* root, UIScreenLayer layer)
{
    if ((self == NULL) || (root == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_SetRootLayer: self and root must not be NULL.");
    }
    if ((Widget_GetScreen(root) != self) || !root->_isScreenRoot || root->_isDying)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIScreen_SetRootLayer: the widget is not a root of this screen, or is being destroyed.");
    }
    if (((int32_t)layer < 0) || ((int32_t)layer >= (int32_t)UIScreenLayer_Count))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIScreen_SetRootLayer: an unknown layer.");
    }
    root->_rootLayer = layer;
    Widget_InternalResortInDrawOrder(&self->_rootDrawOrder, root);
    self->_isHitTestDirty = true;
    return SettleAfterMove(self);
}

UIScreenLayer UIScreen_GetRootLayer(const UIScreen* self, const Widget* root)
{
    UNUSED(self);
    return Widget_InternalGetDrawLayer(root);
}

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
    // The draw order is sorted, so the last one in its layer has the highest z there. Already on top: leave the z alone, so
    // repeated clicks do not make it grow.
    Widget* Topmost = NULL;
    for (size_t Index = Siblings->_count; (Index-- > 0U) && (Topmost == NULL);)
    {
        Widget* Candidate = Widget_InternalGetDrawOrderAt(Siblings, Index);
        Topmost = (Widget_InternalGetDrawLayer(Candidate) == Widget_InternalGetDrawLayer(widget)) ? Candidate : NULL;
    }
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
        Widget* Root = Widget_InternalGetDrawOrderAt(&self->_rootDrawOrder, Index);
        Widget* Hit = (Root->_rootLayer != UIScreenLayer_Overlay)
            ? HitTestNode(Root, screenPosition, screenPosition, RootPosition, RootSize, self->_viewport.SizePixels) : NULL;
        if (Hit != NULL)
        {
            *outWidget = Hit;
            return true;
        }
    }
    return false;
}


// Public functions: coordinate conversion.
/* The chain from a widget's root down to it, and each one's absolute box as layout put it. Returns how many. */
static size_t GetChainBoxes(const Widget* widget, const Widget** outChain, Vector2* outPositions, Vector2* outSizes)
{
    size_t Count = 0;
    for (const Widget* Current = widget; (Current != NULL) && (Count < WIDGET_MAX_TREE_DEPTH); Current = Current->_parent)
    {
        outChain[Count++] = Current;
    }
    // Reversed: the root first.
    for (size_t Index = 0; Index < (Count / 2U); Index++)
    {
        const Widget* Swap = outChain[Index];
        outChain[Index] = outChain[Count - 1U - Index];
        outChain[Count - 1U - Index] = Swap;
    }
    Vector2 ParentPosition = { .x = 0.0f, .y = 0.0f };
    Vector2 ParentSize = { .x = 1.0f, .y = 1.0f };
    for (size_t Index = 0; Index < Count; Index++)
    {
        const Widget* Node = outChain[Index];
        outPositions[Index] = (Vector2){ .x = ParentPosition.x + (Node->_position.x * ParentSize.x),
            .y = ParentPosition.y + (Node->_position.y * ParentSize.y) };
        outSizes[Index] = (Vector2){ .x = Node->_size.x * ParentSize.x, .y = Node->_size.y * ParentSize.y };
        ParentPosition = outPositions[Index];
        ParentSize = outSizes[Index];
    }
    return Count;
}

Vector2 UIScreen_WidgetToScreen(const UIScreen* self, const Widget* widget, Vector2 widgetLocal)
{
    const Widget* Chain[WIDGET_MAX_TREE_DEPTH];
    Vector2 Positions[WIDGET_MAX_TREE_DEPTH];
    Vector2 Sizes[WIDGET_MAX_TREE_DEPTH];
    size_t Count = GetChainBoxes(widget, Chain, Positions, Sizes);
    if (Count == 0U)
    {
        return widgetLocal;
    }
    Vector2 Point = { .x = Positions[Count - 1U].x + (widgetLocal.x * Sizes[Count - 1U].x),
        .y = Positions[Count - 1U].y + (widgetLocal.y * Sizes[Count - 1U].y) };
    // The widget's own transform first, then each ancestor's out to the root.
    for (size_t Index = Count; Index-- > 0U;)
    {
        Point = ApplyTransform(Chain[Index], Positions[Index], Sizes[Index], self->_viewport.SizePixels, Point);
    }
    return Point;
}

Vector2 UIScreen_ScreenToWidget(const UIScreen* self, const Widget* widget, Vector2 screenPosition)
{
    const Widget* Chain[WIDGET_MAX_TREE_DEPTH];
    Vector2 Positions[WIDGET_MAX_TREE_DEPTH];
    Vector2 Sizes[WIDGET_MAX_TREE_DEPTH];
    size_t Count = GetChainBoxes(widget, Chain, Positions, Sizes);
    if (Count == 0U)
    {
        return screenPosition;
    }
    // Each transform undone from the root in: a level that holds no point (a visual scale of 0) is passed as it is, so a drag
    // still has a place to go.
    Vector2 Point = screenPosition;
    for (size_t Index = 0; Index < Count; Index++)
    {
        Vector2 Undone;
        if (TryUndoTransform(Chain[Index], Positions[Index], Sizes[Index], self->_viewport.SizePixels, Point, &Undone))
        {
            Point = Undone;
        }
    }
    return ToBoxShare(Point, Positions[Count - 1U], Sizes[Count - 1U]);
}


Rectangle UIScreen_GetWidgetPixelBox(const UIScreen* self, const Widget* widget)
{
    Vector2 Position;
    Vector2 Size;
    UIScreen_InternalGetAbsoluteBox(widget, &Position, &Size);
    return BoxToPixels(Position, Size, self->_viewport.SizePixels);
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
