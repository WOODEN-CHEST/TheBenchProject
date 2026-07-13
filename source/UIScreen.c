#include "UIScreen.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "UIRenderContext.h"
#include "UIAnimation.h"
#include "UIInput.h"
#include "Renderer.h"
#include "raylib/raylib.h"
#include "wr/WRMemory.h"
#include "wr/WRMath.h"
#include "wr/WRCompile.h"


// Macros.
/** Navigation outline thickness, as a fraction of the render buffer height (constant relative size). */
#define OUTLINE_THICKNESS_RELATIVE 0.0035f
/** How far above the highest sibling z a widget is placed when brought to the top. */
#define BRING_TO_TOP_Z_STEP 1.0f
/** Perpendicular-distance penalty when picking the nearest widget for arrow navigation. */
#define NAV_PERPENDICULAR_PENALTY 2.0f


// Types.
/* One widget-registry slot: an id and the widget it maps to. */
typedef struct RegistryEntryStruct
{
    uint64_t Id;
    Widget* Widget;
} RegistryEntry;

/* Storage large enough for any animatable property value. */
typedef union AnimationValueUnion
{
    float FloatValue;
    Vector2 Vector2Value;
    Color ColorValue;
    RenderColor RenderColorValue;
} AnimationValue;

/* One running animation on a widget property. */
typedef struct AnimationRecordStruct
{
    uint64_t Id;
    uint64_t WidgetId;
    int32_t PropertyId;
    UIPropertyType Type;
    UIAnimationOptions Options;
    double Elapsed;
    bool IsPendingRemoval;
    GenericBuffer Keyframes; // of UIKeyframe
} AnimationRecord;

/* An arrow-navigation direction. */
typedef enum NavDirectionEnum
{
    NavDirection_Up,
    NavDirection_Down,
    NavDirection_Left,
    NavDirection_Right
} NavDirection;


// Static function forward declarations (recursive or used before definition).
static void GetWidgetAbsoluteBox(const Widget* widget, Vector2* outPos, Vector2* outSize);
static Widget* HitTestNode(Widget* widget, Vector2 screenPos, Vector2 parentPos, Vector2 parentSize);
static Error RenderNode(UIScreen* self, RenderContext* renderContext, Widget* widget,
    Vector2 parentPos, Vector2 parentSize, RenderColor parentTint, Rectangle clip, bool hasClip);
static Error SnapshotWidget(Widget* widget, GenericBuffer* out);
static void GatherNavCandidates(UIScreen* self);
static Error StartNavigation(UIScreen* self);
static Error StopNavigation(UIScreen* self);


// Static functions: small helpers.
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

/* Returns the index of a widget in the registry, or GENERIC_BUFFER_INDEX_INVALID if absent. */
static size_t FindRegistryIndex(UIScreen* self, uint64_t id)
{
    for (size_t Index = 0; Index < self->_widgets._count; Index++)
    {
        RegistryEntry* Entry = GenericBuffer_GetPointerToElement(&self->_widgets, Index);
        if (Entry->Id == id)
        {
            return Index;
        }
    }
    return GENERIC_BUFFER_INDEX_INVALID;
}

/* Looks up a widget by id, or NULL if not found (id 0 is never found). */
static Widget* LookupWidget(UIScreen* self, uint64_t id)
{
    if (id == 0)
    {
        return NULL;
    }
    size_t Index = FindRegistryIndex(self, id);
    if (Index == GENERIC_BUFFER_INDEX_INVALID)
    {
        return NULL;
    }
    RegistryEntry* Entry = GenericBuffer_GetPointerToElement(&self->_widgets, Index);
    return Entry->Widget;
}

/* Returns the top-most ancestor (root) of a widget. */
static Widget* GetRoot(Widget* widget)
{
    Widget* Current = widget;
    while (Widget_GetParent(Current) != NULL)
    {
        Current = Widget_GetParent(Current);
    }
    return Current;
}

/* Returns true if @p node is @p ancestor or lies within its subtree. */
static bool IsInSubtree(const Widget* node, const Widget* ancestor)
{
    const Widget* Current = node;
    while (Current != NULL)
    {
        if (Current == ancestor)
        {
            return true;
        }
        Current = Widget_GetParent(Current);
    }
    return false;
}

/* Returns true if the point lies within the axis-aligned box [pos, pos+size]. */
static bool PointInBox(Vector2 point, Vector2 pos, Vector2 size)
{
    return (point.x >= pos.x) && (point.x <= (pos.x + size.x))
        && (point.y >= pos.y) && (point.y <= (pos.y + size.y));
}

/* Returns true if a widget is reachable from a screen root (and so is updated/rendered). */
static bool IsWidgetLive(const Widget* widget)
{
    const Widget* Current = widget;
    while (Current != NULL)
    {
        if (Widget_IsScreenRoot(Current))
        {
            return true;
        }
        Current = Widget_GetParent(Current);
    }
    return false;
}

/* Recursively composes a widget's absolute screen box from its parent chain. */
static void GetWidgetAbsoluteBox(const Widget* widget, Vector2* outPos, Vector2* outSize)
{
    Widget* Parent = Widget_GetParent(widget);
    Vector2 Position = Widget_GetPosition(widget);
    Vector2 Size = Widget_GetSize(widget);

    if (Parent == NULL)
    {
        *outPos = Position;
        *outSize = Size;
        return;
    }

    Vector2 ParentPos;
    Vector2 ParentSize;
    GetWidgetAbsoluteBox(Parent, &ParentPos, &ParentSize);
    outPos->x = ParentPos.x + (Position.x * ParentSize.x);
    outPos->y = ParentPos.y + (Position.y * ParentSize.y);
    outSize->x = Size.x * ParentSize.x;
    outSize->y = Size.y * ParentSize.y;
}

/* Computes a widget's estimated bounds (including subwidgets) mapped into absolute screen coordinates. */
static void GetWidgetEstimatedScreenBox(Widget* widget, Vector2* outPos, Vector2* outSize)
{
    Rectangle Bounds = Widget_GetEstimatedBounds(widget); // in the widget's parent space
    Vector2 ParentPos = { .x = 0.0f, .y = 0.0f };
    Vector2 ParentSize = { .x = 1.0f, .y = 1.0f };
    Widget* Parent = Widget_GetParent(widget);
    if (Parent != NULL)
    {
        GetWidgetAbsoluteBox(Parent, &ParentPos, &ParentSize);
    }
    outPos->x = ParentPos.x + (Bounds.x * ParentSize.x);
    outPos->y = ParentPos.y + (Bounds.y * ParentSize.y);
    outSize->x = Bounds.width * ParentSize.x;
    outSize->y = Bounds.height * ParentSize.y;
}

/* Returns the geometric center of a rectangle. */
static Vector2 RectangleCenter(Rectangle rectangle)
{
    return (Vector2)
    {
        .x = rectangle.x + (rectangle.width * 0.5f),
        .y = rectangle.y + (rectangle.height * 0.5f)
    };
}


// Static functions: z ordering.
/* Three-way comparison of two widgets by ascending z layer. */
static ComparisonResult CompareWidgetZ(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b, const UserData* userData)
{
    UNUSED(buffer);
    UNUSED(userData);
    Widget* WidgetA = *(Widget**)a._element;
    Widget* WidgetB = *(Widget**)b._element;
    float ZA = Widget_GetZLayer(WidgetA);
    float ZB = Widget_GetZLayer(WidgetB);
    if (ZA < ZB)
    {
        return ComparisonResult_ALessThanB;
    }
    if (ZA > ZB)
    {
        return ComparisonResult_AGreaterThanB;
    }
    return ComparisonResult_AEqualsB;
}

/* Three-way comparison of two widgets by reading order (top row first, then left) of their bounds. */
static ComparisonResult CompareReadingOrder(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b, const UserData* userData)
{
    UNUSED(buffer);
    UNUSED(userData);
    Widget* WidgetA = *(Widget**)a._element;
    Widget* WidgetB = *(Widget**)b._element;
    Rectangle BoundsA = Widget_GetEstimatedBounds(WidgetA);
    Rectangle BoundsB = Widget_GetEstimatedBounds(WidgetB);
    if (BoundsA.y < BoundsB.y)
    {
        return ComparisonResult_ALessThanB;
    }
    if (BoundsA.y > BoundsB.y)
    {
        return ComparisonResult_AGreaterThanB;
    }
    if (BoundsA.x < BoundsB.x)
    {
        return ComparisonResult_ALessThanB;
    }
    if (BoundsA.x > BoundsB.x)
    {
        return ComparisonResult_AGreaterThanB;
    }
    return ComparisonResult_AEqualsB;
}

/* Sorts the screen's root widgets into ascending z order (in place). */
static void SortRootsByZ(UIScreen* self)
{
    GenericBuffer_SortAscending(&self->_roots, CompareWidgetZ, NULL, self->_sortScratch);
}


// Static functions: hit testing.
static Widget* HitTestNode(Widget* widget, Vector2 screenPos, Vector2 parentPos, Vector2 parentSize)
{
    Vector2 Position = Widget_GetPosition(widget);
    Vector2 Size = Widget_GetSize(widget);
    Vector2 AbsolutePos = { .x = parentPos.x + (Position.x * parentSize.x), .y = parentPos.y + (Position.y * parentSize.y) };
    Vector2 AbsoluteSize = { .x = Size.x * parentSize.x, .y = Size.y * parentSize.y };

    bool IsInside = PointInBox(screenPos, AbsolutePos, AbsoluteSize);
    if (Widget_AreSubWidgetsCut(widget) && !IsInside)
    {
        return NULL; // clipped: neither this widget nor its (clipped) children can be hit here
    }

    // Test children front-first (descending z).
    Widget_SortSubWidgetsByZ(widget);
    size_t Count = Widget_GetSubWidgetCount(widget);
    for (size_t Index = Count; Index-- > 0;)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        Widget* Hit = HitTestNode(Child, screenPos, AbsolutePos, AbsoluteSize);
        if (Hit != NULL)
        {
            return Hit;
        }
    }

    if (Widget_IsInputEnabled(widget) && IsInside)
    {
        return widget;
    }
    return NULL;
}


// Static functions: rendering.
/* Converts a normalized screen box to a pixel-space rectangle in the render buffer. */
static Rectangle ScreenBoxToPixels(RenderContext* renderContext, Vector2 pos, Vector2 size)
{
    Vector2 PixelPos = RenderContext_VectorRelativeToPixel(renderContext, pos);
    Vector2 PixelSize = RenderContext_VectorRelativeToPixel(renderContext, size);
    return (Rectangle){ .x = PixelPos.x, .y = PixelPos.y, .width = PixelSize.x, .height = PixelSize.y };
}

/* Returns the axis-aligned intersection of two rectangles (clamped to non-negative size). */
static Rectangle IntersectRectangle(Rectangle a, Rectangle b)
{
    float X1 = Math_MaxFloat(a.x, b.x);
    float Y1 = Math_MaxFloat(a.y, b.y);
    float X2 = Math_MinFloat(a.x + a.width, b.x + b.width);
    float Y2 = Math_MinFloat(a.y + a.height, b.y + b.height);
    return (Rectangle)
    {
        .x = X1,
        .y = Y1,
        .width = Math_MaxFloat(0.0f, X2 - X1),
        .height = Math_MaxFloat(0.0f, Y2 - Y1)
    };
}

/* Enables the GPU scissor to a pixel rectangle. */
static void BeginScissorRectangle(Rectangle rectangle)
{
    BeginScissorMode((int)rectangle.x, (int)rectangle.y, (int)rectangle.width, (int)rectangle.height);
}

static Error RenderNode(UIScreen* self, RenderContext* renderContext, Widget* widget,
    Vector2 parentPos, Vector2 parentSize, RenderColor parentTint, Rectangle clip, bool hasClip)
{
    Vector2 Position = Widget_GetPosition(widget);
    Vector2 Size = Widget_GetSize(widget);
    Vector2 AbsolutePos = { .x = parentPos.x + (Position.x * parentSize.x), .y = parentPos.y + (Position.y * parentSize.y) };
    Vector2 AbsoluteSize = { .x = Size.x * parentSize.x, .y = Size.y * parentSize.y };
    RenderColor Tint = UIRenderColor_Multiply(parentTint, Widget_GetRenderTint(widget));

    if (Widget_IsRendered(widget))
    {
        UIRenderContext Context;
        UIRenderContext_Create(&Context, renderContext, AbsolutePos, AbsoluteSize, Tint);
        Error RenderResult = Widget_Render(widget, &Context);
        if (RenderResult.Code != ErrorCode_Success)
        {
            return RenderResult;
        }
    }

    Rectangle ChildClip = clip;
    bool ChildHasClip = hasClip;
    bool StartedScissor = false;
    if (Widget_AreSubWidgetsCut(widget))
    {
        Rectangle Box = ScreenBoxToPixels(renderContext, AbsolutePos, AbsoluteSize);
        ChildClip = hasClip ? IntersectRectangle(clip, Box) : Box;
        ChildHasClip = true;
        BeginScissorRectangle(ChildClip);
        StartedScissor = true;
    }

    Widget_SortSubWidgetsByZ(widget);
    Error Result = Error_CreateSuccess();
    size_t Count = Widget_GetSubWidgetCount(widget);
    for (size_t Index = 0; Index < Count; Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        Result = RenderNode(self, renderContext, Child, AbsolutePos, AbsoluteSize, Tint, ChildClip, ChildHasClip);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
    }

    if (StartedScissor)
    {
        if (hasClip)
        {
            BeginScissorRectangle(clip);
        }
        else
        {
            EndScissorMode();
        }
    }
    return Result;
}

/* Draws a navigation outline (estimated bounds, on top) around a widget in the given color. */
static void DrawOutlineForWidget(RenderContext* renderContext, Widget* widget, RenderColor color)
{
    Vector2 BoxPos;
    Vector2 BoxSize;
    GetWidgetEstimatedScreenBox(widget, &BoxPos, &BoxSize);
    RectangleOutlineRenderArguments Arguments =
    {
        .Position = RenderVector2D_Relative(BoxPos),
        .Size = RenderVector2D_Relative(BoxSize),
        .Thickness = RenderFloat_Relative(OUTLINE_THICKNESS_RELATIVE),
        .TargetColor = color
    };
    RenderContext_RenderRectangleOutline(renderContext, &Arguments);
}

/* Draws the keyboard-navigation outlines: green around each tabbed widget, white around the target. */
static void DrawNavigationOutlines(UIScreen* self, RenderContext* renderContext)
{
    if (!self->_isNavigating)
    {
        return;
    }

    RenderColor Green = { .Tint = GREEN, .Brightness = 1.0f, .Opacity = 1.0f };
    RenderColor White = RenderColor_White();

    for (size_t Index = 0; Index < self->_navStack._count; Index++)
    {
        uint64_t Id = *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_navStack, Index);
        Widget* TabbedWidget = LookupWidget(self, Id);
        if (TabbedWidget != NULL)
        {
            DrawOutlineForWidget(renderContext, TabbedWidget, Green);
        }
    }

    if (self->_navTargetId != 0)
    {
        Widget* Target = LookupWidget(self, self->_navTargetId);
        if (Target != NULL)
        {
            DrawOutlineForWidget(renderContext, Target, White);
        }
    }
}


// Static functions: update snapshot.
static Error SnapshotWidget(Widget* widget, GenericBuffer* out)
{
    uint64_t Id = Widget_GetId(widget);
    if (!GenericBuffer_AddLast(out, &Id))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_Update: update snapshot could not grow.");
    }

    size_t Count = Widget_GetSubWidgetCount(widget);
    for (size_t Index = 0; Index < Count; Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        Error ChildResult = SnapshotWidget(Child, out);
        if (ChildResult.Code != ErrorCode_Success)
        {
            return ChildResult;
        }
    }
    return Error_CreateSuccess();
}


// Static functions: input mapping.
/* Maps a UIMouseButton to its raylib button code. */
static int MapButtonToRaylib(UIMouseButton button)
{
    switch (button)
    {
        case UIMouseButton_Left:    return MOUSE_BUTTON_LEFT;
        case UIMouseButton_Right:   return MOUSE_BUTTON_RIGHT;
        case UIMouseButton_Middle:  return MOUSE_BUTTON_MIDDLE;
        case UIMouseButton_Side:    return MOUSE_BUTTON_SIDE;
        case UIMouseButton_Extra:   return MOUSE_BUTTON_EXTRA;
        case UIMouseButton_Forward: return MOUSE_BUTTON_FORWARD;
        case UIMouseButton_Back:    return MOUSE_BUTTON_BACK;
        default:                    return MOUSE_BUTTON_LEFT;
    }
}

/* Maps a UIKey to its raylib key code, or KEY_NULL if unmapped. */
static int MapUIKeyToRaylib(UIKey key)
{
    if ((key >= UIKey_A) && (key <= UIKey_Z))
    {
        return KEY_A + (int)(key - UIKey_A);
    }
    if ((key >= UIKey_0) && (key <= UIKey_9))
    {
        return KEY_ZERO + (int)(key - UIKey_0);
    }
    if ((key >= UIKey_F1) && (key <= UIKey_F12))
    {
        return KEY_F1 + (int)(key - UIKey_F1);
    }

    switch (key)
    {
        case UIKey_Tab:          return KEY_TAB;
        case UIKey_Enter:        return KEY_ENTER;
        case UIKey_Escape:       return KEY_ESCAPE;
        case UIKey_Space:        return KEY_SPACE;
        case UIKey_Backspace:    return KEY_BACKSPACE;
        case UIKey_Delete:       return KEY_DELETE;
        case UIKey_Up:           return KEY_UP;
        case UIKey_Down:         return KEY_DOWN;
        case UIKey_Left:         return KEY_LEFT;
        case UIKey_Right:        return KEY_RIGHT;
        case UIKey_Home:         return KEY_HOME;
        case UIKey_End:          return KEY_END;
        case UIKey_PageUp:       return KEY_PAGE_UP;
        case UIKey_PageDown:     return KEY_PAGE_DOWN;
        case UIKey_Insert:       return KEY_INSERT;
        case UIKey_LeftShift:    return KEY_LEFT_SHIFT;
        case UIKey_RightShift:   return KEY_RIGHT_SHIFT;
        case UIKey_LeftControl:  return KEY_LEFT_CONTROL;
        case UIKey_RightControl: return KEY_RIGHT_CONTROL;
        case UIKey_LeftAlt:      return KEY_LEFT_ALT;
        case UIKey_RightAlt:     return KEY_RIGHT_ALT;
        default:                 return KEY_NULL;
    }
}

/* Returns true if a key drives keyboard navigation (and so is not forwarded to widgets). */
static bool IsNavigationKey(UIKey key)
{
    switch (key)
    {
        case UIKey_Tab:
        case UIKey_Enter:
        case UIKey_Escape:
        case UIKey_Up:
        case UIKey_Down:
        case UIKey_Left:
        case UIKey_Right:
            return true;
        default:
            return false;
    }
}


// Static functions: input argument builders.
static WidgetMouseInputArgs MakeMouseArgs(UIScreen* self, UIMouseInputType type, UIMouseButton button,
    const Widget* widget, Vector2 screenPos, Vector2 startScreen, double duration, Vector2 scrollDelta)
{
    WidgetMouseInputArgs Arguments;
    Arguments.Type = type;
    Arguments.Button = button;
    Arguments.ScreenPosition = screenPos;
    Arguments.WidgetPosition = UIScreen_ScreenToWidget(self, widget, screenPos);
    Arguments.ClickStartScreenPosition = startScreen;
    Arguments.ClickStartWidgetPosition = UIScreen_ScreenToWidget(self, widget, startScreen);
    Arguments.DurationSeconds = duration;
    Arguments.ScrollDelta = scrollDelta;
    return Arguments;
}

static WidgetKeyboardInputArgs MakeKeyboardArgs(UIKeyboardInputType type, UIKey key, uint32_t codepoint)
{
    return (WidgetKeyboardInputArgs)
    {
        .Type = type,
        .Key = key,
        .Codepoint = codepoint
    };
}


// Static functions: keyboard navigation.
/* Gathers the current navigation level's candidate widgets into _navCandidates (reading order). */
static void GatherNavCandidates(UIScreen* self)
{
    GenericBuffer_Clear(&self->_navCandidates);

    if (self->_navStack._count == 0)
    {
        for (size_t Index = 0; Index < self->_roots._count; Index++)
        {
            Widget* Root = *(Widget**)GenericBuffer_GetPointerToElement(&self->_roots, Index);
            GenericBuffer_AddLast(&self->_navCandidates, &Root);
        }
    }
    else
    {
        uint64_t ParentId = *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_navStack, self->_navStack._count - 1);
        Widget* Parent = LookupWidget(self, ParentId);
        if (Parent != NULL)
        {
            size_t Count = Widget_GetSubWidgetCount(Parent);
            for (size_t Index = 0; Index < Count; Index++)
            {
                Widget* Child = Widget_GetSubWidgetAt(Parent, Index);
                GenericBuffer_AddLast(&self->_navCandidates, &Child);
            }
        }
    }

    GenericBuffer_SortAscending(&self->_navCandidates, CompareReadingOrder, NULL, self->_sortScratch);
}

/* Returns the id of the first (top-left-most) navigation candidate, or 0 if there are none. */
static uint64_t FirstNavCandidateId(UIScreen* self)
{
    if (self->_navCandidates._count == 0)
    {
        return 0;
    }
    Widget* First = *(Widget**)GenericBuffer_GetPointerToElement(&self->_navCandidates, 0);
    return Widget_GetId(First);
}

static Error StartNavigation(UIScreen* self)
{
    self->_isNavigating = true;
    GenericBuffer_Clear(&self->_navStack);
    GatherNavCandidates(self);
    self->_navTargetId = FirstNavCandidateId(self);
    return Error_CreateSuccess();
}

static Error StopNavigation(UIScreen* self)
{
    Error FirstError = Error_CreateSuccess();
    for (size_t Index = 0; Index < self->_navStack._count; Index++)
    {
        uint64_t Id = *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_navStack, Index);
        Widget* TabbedWidget = LookupWidget(self, Id);
        if (TabbedWidget != NULL)
        {
            Error TabResult = Widget_SetTabbed(TabbedWidget, false);
            KeepFirstError(&FirstError, &TabResult);
        }
    }
    GenericBuffer_Clear(&self->_navStack);
    self->_isNavigating = false;
    self->_navTargetId = 0;
    return FirstError;
}

static Error NavigateNext(UIScreen* self)
{
    if (!self->_isNavigating)
    {
        return StartNavigation(self);
    }

    GatherNavCandidates(self);
    size_t Count = self->_navCandidates._count;
    if (Count == 0)
    {
        return Error_CreateSuccess();
    }

    size_t CurrentIndex = GENERIC_BUFFER_INDEX_INVALID;
    for (size_t Index = 0; Index < Count; Index++)
    {
        Widget* Candidate = *(Widget**)GenericBuffer_GetPointerToElement(&self->_navCandidates, Index);
        if (Widget_GetId(Candidate) == self->_navTargetId)
        {
            CurrentIndex = Index;
            break;
        }
    }

    size_t NextIndex = (CurrentIndex == GENERIC_BUFFER_INDEX_INVALID) ? 0 : ((CurrentIndex + 1) % Count);
    Widget* Next = *(Widget**)GenericBuffer_GetPointerToElement(&self->_navCandidates, NextIndex);
    self->_navTargetId = Widget_GetId(Next);
    return Error_CreateSuccess();
}

static Error NavigateArrow(UIScreen* self, NavDirection direction)
{
    if (!self->_isNavigating)
    {
        return StartNavigation(self);
    }

    GatherNavCandidates(self);
    Widget* From = LookupWidget(self, self->_navTargetId);
    if (From == NULL)
    {
        self->_navTargetId = FirstNavCandidateId(self);
        return Error_CreateSuccess();
    }

    Vector2 FromCenter = RectangleCenter(Widget_GetEstimatedBounds(From));
    Widget* Best = NULL;
    float BestDistance = 0.0f;

    for (size_t Index = 0; Index < self->_navCandidates._count; Index++)
    {
        Widget* Candidate = *(Widget**)GenericBuffer_GetPointerToElement(&self->_navCandidates, Index);
        if (Candidate == From)
        {
            continue;
        }

        Vector2 CandidateCenter = RectangleCenter(Widget_GetEstimatedBounds(Candidate));
        float DeltaX = CandidateCenter.x - FromCenter.x;
        float DeltaY = CandidateCenter.y - FromCenter.y;

        float Primary;
        float Perpendicular;
        switch (direction)
        {
            case NavDirection_Right: if (DeltaX <= 0.0f) { continue; } Primary = DeltaX;  Perpendicular = Math_AbsFloat(DeltaY); break;
            case NavDirection_Left:  if (DeltaX >= 0.0f) { continue; } Primary = -DeltaX; Perpendicular = Math_AbsFloat(DeltaY); break;
            case NavDirection_Down:  if (DeltaY <= 0.0f) { continue; } Primary = DeltaY;  Perpendicular = Math_AbsFloat(DeltaX); break;
            case NavDirection_Up:    if (DeltaY >= 0.0f) { continue; } Primary = -DeltaY; Perpendicular = Math_AbsFloat(DeltaX); break;
            default:                 continue;
        }

        float Distance = Primary + (NAV_PERPENDICULAR_PENALTY * Perpendicular);
        if ((Best == NULL) || (Distance < BestDistance))
        {
            Best = Candidate;
            BestDistance = Distance;
        }
    }

    if (Best != NULL)
    {
        self->_navTargetId = Widget_GetId(Best);
    }
    return Error_CreateSuccess();
}

static Error NavigateEnter(UIScreen* self)
{
    if (self->_navTargetId == 0)
    {
        return Error_CreateSuccess();
    }
    Widget* Target = LookupWidget(self, self->_navTargetId);
    if (Target == NULL)
    {
        return Error_CreateSuccess();
    }

    if (!GenericBuffer_AddLast(&self->_navStack, &self->_navTargetId))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen: navigation stack could not grow.");
    }

    Error TabResult = Widget_SetTabbed(Target, true);
    if (TabResult.Code != ErrorCode_Success)
    {
        return TabResult;
    }
    Error TopResult = UIScreen_BringToTop(self, Target);
    if (TopResult.Code != ErrorCode_Success)
    {
        return TopResult;
    }

    GatherNavCandidates(self); // now the tabbed widget's children
    self->_navTargetId = FirstNavCandidateId(self);
    return Error_CreateSuccess();
}

static Error NavigateEscape(UIScreen* self)
{
    if (self->_navStack._count == 0)
    {
        return StopNavigation(self);
    }

    size_t LastIndex = self->_navStack._count - 1;
    uint64_t PoppedId = *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_navStack, LastIndex);
    GenericBuffer_RemoveAt(&self->_navStack, LastIndex);

    Widget* Popped = LookupWidget(self, PoppedId);
    if (Popped != NULL)
    {
        Error TabResult = Widget_SetTabbed(Popped, false);
        if (TabResult.Code != ErrorCode_Success)
        {
            return TabResult;
        }
    }
    self->_navTargetId = PoppedId;
    return Error_CreateSuccess();
}


// Static functions: input processing.
static Error ProcessHover(UIScreen* self)
{
    Widget* Top = NULL;
    UIScreen_TryGetTopmostAt(self, self->_mousePosition, &Top);
    uint64_t NewHoverId = (Top != NULL) ? Widget_GetId(Top) : 0;

    if (NewHoverId == self->_hoveredWidgetId)
    {
        return Error_CreateSuccess();
    }

    Widget* Old = LookupWidget(self, self->_hoveredWidgetId);
    if (Old != NULL)
    {
        Error EndResult = Widget_SetHovered(Old, false, NULL);
        if (EndResult.Code != ErrorCode_Success)
        {
            return EndResult;
        }
    }

    self->_hoveredWidgetId = NewHoverId;

    if (Top != NULL)
    {
        WidgetHoverArgs HoverArgs =
        {
            .ScreenPosition = self->_mousePosition,
            .WidgetPosition = UIScreen_ScreenToWidget(self, Top, self->_mousePosition)
        };
        Error StartResult = Widget_SetHovered(Top, true, &HoverArgs);
        if (StartResult.Code != ErrorCode_Success)
        {
            return StartResult;
        }
    }
    return Error_CreateSuccess();
}

static Error ProcessButtons(UIScreen* self, ProgramTime time, bool mouseMoved)
{
    Vector2 NoScroll = { .x = 0.0f, .y = 0.0f };

    for (int ButtonIndex = 0; ButtonIndex < (int)UIMouseButton_Count; ButtonIndex++)
    {
        UIMouseButton Button = (UIMouseButton)ButtonIndex;
        int RaylibButton = MapButtonToRaylib(Button);
        UIScreenButtonTracking* Tracking = &self->_buttons[ButtonIndex];

        if (IsMouseButtonPressed(RaylibButton))
        {
            Widget* Top = NULL;
            UIScreen_TryGetTopmostAt(self, self->_mousePosition, &Top);

            Tracking->IsPressed = true;
            Tracking->TargetWidgetId = (Top != NULL) ? Widget_GetId(Top) : 0;
            Tracking->StartScreenPosition = self->_mousePosition;
            Tracking->StartTime = time.TotalTime;

            if (self->_isNavigating)
            {
                Error StopResult = StopNavigation(self);
                if (StopResult.Code != ErrorCode_Success)
                {
                    return StopResult;
                }
            }

            Error FocusResult = UIScreen_SetFocusedWidget(self, Top); // NULL clears focus (empty click)
            if (FocusResult.Code != ErrorCode_Success)
            {
                return FocusResult;
            }

            if (Top != NULL)
            {
                WidgetMouseInputArgs Arguments = MakeMouseArgs(self, UIMouseInputType_ButtonPress, Button, Top,
                    self->_mousePosition, self->_mousePosition, 0.0, NoScroll);
                Error InputResult = Widget_OnMouseInput(Top, &Arguments);
                if (InputResult.Code != ErrorCode_Success)
                {
                    return InputResult;
                }
            }
        }
        else if (IsMouseButtonReleased(RaylibButton))
        {
            if (Tracking->IsPressed)
            {
                Widget* Target = LookupWidget(self, Tracking->TargetWidgetId);
                if (Target != NULL)
                {
                    double Duration = time.TotalTime - Tracking->StartTime;
                    WidgetMouseInputArgs Arguments = MakeMouseArgs(self, UIMouseInputType_ButtonRelease, Button, Target,
                        self->_mousePosition, Tracking->StartScreenPosition, Duration, NoScroll);
                    Error InputResult = Widget_OnMouseInput(Target, &Arguments);
                    if (InputResult.Code != ErrorCode_Success)
                    {
                        return InputResult;
                    }
                }
                Tracking->IsPressed = false;
                Tracking->TargetWidgetId = 0;
            }
        }
        else if (mouseMoved && Tracking->IsPressed && IsMouseButtonDown(RaylibButton))
        {
            Widget* Target = LookupWidget(self, Tracking->TargetWidgetId);
            if (Target != NULL)
            {
                double Duration = time.TotalTime - Tracking->StartTime;
                WidgetMouseInputArgs Arguments = MakeMouseArgs(self, UIMouseInputType_Move, Button, Target,
                    self->_mousePosition, Tracking->StartScreenPosition, Duration, NoScroll);
                Error InputResult = Widget_OnMouseInput(Target, &Arguments);
                if (InputResult.Code != ErrorCode_Success)
                {
                    return InputResult;
                }
            }
        }
    }
    return Error_CreateSuccess();
}

static Error ProcessScroll(UIScreen* self)
{
    if ((self->_scrollDelta.x == 0.0f) && (self->_scrollDelta.y == 0.0f))
    {
        return Error_CreateSuccess();
    }

    Widget* Top = NULL;
    if (!UIScreen_TryGetTopmostAt(self, self->_mousePosition, &Top))
    {
        return Error_CreateSuccess();
    }

    WidgetMouseInputArgs Arguments = MakeMouseArgs(self, UIMouseInputType_Scroll, UIMouseButton_Left, Top,
        self->_mousePosition, self->_mousePosition, 0.0, self->_scrollDelta);
    return Widget_OnMouseInput(Top, &Arguments);
}

static Error ProcessKeyboardNavigation(UIScreen* self)
{
    if (IsKeyPressed(KEY_TAB))
    {
        Error Result = NavigateNext(self);
        if (Result.Code != ErrorCode_Success) { return Result; }
    }
    if (IsKeyPressed(KEY_UP))
    {
        Error Result = NavigateArrow(self, NavDirection_Up);
        if (Result.Code != ErrorCode_Success) { return Result; }
    }
    if (IsKeyPressed(KEY_DOWN))
    {
        Error Result = NavigateArrow(self, NavDirection_Down);
        if (Result.Code != ErrorCode_Success) { return Result; }
    }
    if (IsKeyPressed(KEY_LEFT))
    {
        Error Result = NavigateArrow(self, NavDirection_Left);
        if (Result.Code != ErrorCode_Success) { return Result; }
    }
    if (IsKeyPressed(KEY_RIGHT))
    {
        Error Result = NavigateArrow(self, NavDirection_Right);
        if (Result.Code != ErrorCode_Success) { return Result; }
    }
    if (self->_isNavigating && IsKeyPressed(KEY_ENTER))
    {
        Error Result = NavigateEnter(self);
        if (Result.Code != ErrorCode_Success) { return Result; }
    }
    if (self->_isNavigating && IsKeyPressed(KEY_ESCAPE))
    {
        Error Result = NavigateEscape(self);
        if (Result.Code != ErrorCode_Success) { return Result; }
    }
    return Error_CreateSuccess();
}

static Error ProcessKeyboardToFocused(UIScreen* self)
{
    Widget* Focused = self->_focusedWidget;

    for (int KeyIndex = 1; KeyIndex < (int)UIKey_Count; KeyIndex++)
    {
        UIKey Key = (UIKey)KeyIndex;
        if (IsNavigationKey(Key))
        {
            continue;
        }
        int RaylibKey = MapUIKeyToRaylib(Key);
        if ((RaylibKey == KEY_NULL) || (Focused == NULL))
        {
            continue;
        }

        if (IsKeyPressed(RaylibKey))
        {
            WidgetKeyboardInputArgs Arguments = MakeKeyboardArgs(UIKeyboardInputType_KeyPress, Key, 0);
            Error Result = Widget_OnKeyboardInput(Focused, &Arguments);
            if (Result.Code != ErrorCode_Success) { return Result; }
        }
        if (IsKeyReleased(RaylibKey))
        {
            WidgetKeyboardInputArgs Arguments = MakeKeyboardArgs(UIKeyboardInputType_KeyRelease, Key, 0);
            Error Result = Widget_OnKeyboardInput(Focused, &Arguments);
            if (Result.Code != ErrorCode_Success) { return Result; }
        }
        if (IsKeyPressedRepeat(RaylibKey))
        {
            WidgetKeyboardInputArgs Arguments = MakeKeyboardArgs(UIKeyboardInputType_KeyRepeat, Key, 0);
            Error Result = Widget_OnKeyboardInput(Focused, &Arguments);
            if (Result.Code != ErrorCode_Success) { return Result; }
        }
    }

    // Text codepoints (drain the queue regardless of focus so it does not build up).
    int Codepoint = GetCharPressed();
    while (Codepoint != 0)
    {
        if (Focused != NULL)
        {
            WidgetKeyboardInputArgs Arguments = MakeKeyboardArgs(UIKeyboardInputType_Text, UIKey_Unknown, (uint32_t)Codepoint);
            Error Result = Widget_OnKeyboardInput(Focused, &Arguments);
            if (Result.Code != ErrorCode_Success) { return Result; }
        }
        Codepoint = GetCharPressed();
    }
    return Error_CreateSuccess();
}

static Error ProcessInput(UIScreen* self, ProgramTime time)
{
    Vector2 CurrentPixel = GetMousePosition();
    bool MouseMoved = (CurrentPixel.x != self->_previousMousePixel.x) || (CurrentPixel.y != self->_previousMousePixel.y);
    self->_previousMousePixel = CurrentPixel;

    float ScreenWidth = (float)GetScreenWidth();
    float ScreenHeight = (float)GetScreenHeight();
    self->_mousePosition.x = (ScreenWidth > 0.0f) ? (CurrentPixel.x / ScreenWidth) : 0.0f;
    self->_mousePosition.y = (ScreenHeight > 0.0f) ? (CurrentPixel.y / ScreenHeight) : 0.0f;
    self->_scrollDelta = GetMouseWheelMoveV();

    if (MouseMoved && self->_isNavigating)
    {
        Error StopResult = StopNavigation(self);
        if (StopResult.Code != ErrorCode_Success) { return StopResult; }
    }

    Error Result = ProcessHover(self);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ProcessButtons(self, time, MouseMoved);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ProcessScroll(self);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ProcessKeyboardNavigation(self);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ProcessKeyboardToFocused(self);
    if (Result.Code != ErrorCode_Success) { return Result; }
    return Error_CreateSuccess();
}


// Static functions: animation advancing.
/* Wraps a looping animation's elapsed time into [0, cycleLength). */
static double WrapLoopTime(double elapsed, double cycleLength)
{
    if (cycleLength <= 0.0)
    {
        return 0.0;
    }
    double Cycles = Math_FloorDouble(elapsed / cycleLength);
    return elapsed - (Cycles * cycleLength);
}

static Error AdvanceAnimations(UIScreen* self, ProgramTime time)
{
    for (size_t Index = 0; Index < self->_animations._count; Index++)
    {
        AnimationRecord* Record = GenericBuffer_GetPointerToElement(&self->_animations, Index);
        Record->Elapsed += time.PassedTime;

        size_t KeyframeCount = Record->Keyframes._count;
        if (KeyframeCount == 0)
        {
            continue;
        }

        UIKeyframe* Keyframes = (UIKeyframe*)Record->Keyframes._data;
        double LastTime = Keyframes[KeyframeCount - 1].Time;
        double EvalTime = Record->Elapsed;
        if (Record->Options.IsLooping)
        {
            EvalTime = WrapLoopTime(Record->Elapsed, LastTime);
        }

        AnimationValue Value;
        Memory_Zero(&Value, sizeof(Value));
        Error EvalResult = UIAnimation_EvaluateKeyframes(Keyframes, KeyframeCount, Record->Type, EvalTime, &Value);
        if (EvalResult.Code != ErrorCode_Success)
        {
            return EvalResult;
        }

        Widget* Target = LookupWidget(self, Record->WidgetId);
        if (Target != NULL)
        {
            Error SetResult = Widget_SetProperty(Target, Record->PropertyId, &Value);
            if (SetResult.Code != ErrorCode_Success)
            {
                return SetResult;
            }
        }

        if (!Record->Options.IsLooping && (Record->Elapsed >= LastTime) && Record->Options.IsRemovedOnFinish)
        {
            Record->IsPendingRemoval = true;
        }
    }

    // Remove finished animations (backwards so indices stay valid).
    size_t Cursor = self->_animations._count;
    while (Cursor > 0)
    {
        Cursor--;
        AnimationRecord* Record = GenericBuffer_GetPointerToElement(&self->_animations, Cursor);
        if (Record->IsPendingRemoval)
        {
            Memory_Free(Record->Keyframes._data);
            GenericBuffer_RemoveAt(&self->_animations, Cursor);
        }
    }
    return Error_CreateSuccess();
}


// Public functions: lifecycle.
Error UIScreen_Construct(UIScreen* self, UIWidgetFactory* factory)
{
    if ((self == NULL) || (factory == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_Construct: self and factory must not be NULL.");
    }

    Memory_Zero(self, sizeof(*self));
    self->_factory = factory;

    GenericBuffer_AllocateVariable(&self->_widgets, 8U, sizeof(RegistryEntry));
    GenericBuffer_AllocateVariable(&self->_roots, 4U, sizeof(Widget*));
    GenericBuffer_AllocateVariable(&self->_navStack, 4U, sizeof(uint64_t));
    GenericBuffer_AllocateVariable(&self->_animations, 4U, sizeof(AnimationRecord));
    GenericBuffer_AllocateVariable(&self->_updateSnapshot, 16U, sizeof(uint64_t));
    GenericBuffer_AllocateVariable(&self->_navCandidates, 8U, sizeof(Widget*));

    self->_nextWidgetId = 1;
    self->_nextAnimationId = 1;
    return Error_CreateSuccess();
}

Error UIScreen_Deconstruct(UIScreen* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    for (size_t Index = 0; Index < self->_animations._count; Index++)
    {
        AnimationRecord* Record = GenericBuffer_GetPointerToElement(&self->_animations, Index);
        Memory_Free(Record->Keyframes._data);
    }

    Memory_Free(self->_animations._data);
    Memory_Free(self->_updateSnapshot._data);
    Memory_Free(self->_navCandidates._data);
    Memory_Free(self->_navStack._data);
    Memory_Free(self->_roots._data);
    Memory_Free(self->_widgets._data);

    // The factory is borrowed (program-wide); the screen does not deconstruct it.
    Memory_Zero(self, sizeof(*self));
    return Error_CreateSuccess();
}


// Public functions: widget registration.
Error UIScreen_RegisterWidget(UIScreen* self, Widget* widget, uint64_t* outId)
{
    if ((self == NULL) || (widget == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_RegisterWidget: self, widget and outId must not be NULL.");
    }

    RegistryEntry Entry = { .Id = self->_nextWidgetId, .Widget = widget };
    if (!GenericBuffer_AddLast(&self->_widgets, &Entry))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_RegisterWidget: widget registry could not grow.");
    }

    *outId = Entry.Id;
    self->_nextWidgetId++;
    return Error_CreateSuccess();
}

Error UIScreen_UnregisterWidget(UIScreen* self, uint64_t widgetId)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_UnregisterWidget: self must not be NULL.");
    }

    // Purge the widget's animations.
    Error AnimationResult = UIScreen_StopWidgetAnimations(self, widgetId);
    Error_Deconstruct(&AnimationResult);

    // Clear screen references to the widget without firing its hooks (it is being torn down).
    if ((self->_focusedWidget != NULL) && (Widget_GetId(self->_focusedWidget) == widgetId))
    {
        self->_focusedWidget = NULL;
    }
    if (self->_hoveredWidgetId == widgetId)
    {
        self->_hoveredWidgetId = 0;
    }
    if (self->_navTargetId == widgetId)
    {
        self->_navTargetId = 0;
    }
    for (size_t Index = 0; Index < self->_navStack._count; Index++)
    {
        uint64_t Id = *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_navStack, Index);
        if (Id == widgetId)
        {
            GenericBuffer_RemoveAt(&self->_navStack, Index);
            break;
        }
    }
    for (int ButtonIndex = 0; ButtonIndex < (int)UIMouseButton_Count; ButtonIndex++)
    {
        if (self->_buttons[ButtonIndex].TargetWidgetId == widgetId)
        {
            self->_buttons[ButtonIndex].IsPressed = false;
            self->_buttons[ButtonIndex].TargetWidgetId = 0;
        }
    }

    // Remove from roots if present.
    for (size_t Index = 0; Index < self->_roots._count; Index++)
    {
        Widget* Root = *(Widget**)GenericBuffer_GetPointerToElement(&self->_roots, Index);
        if (Widget_GetId(Root) == widgetId)
        {
            GenericBuffer_RemoveAt(&self->_roots, Index);
            break;
        }
    }

    // Remove from the registry.
    size_t RegistryIndex = FindRegistryIndex(self, widgetId);
    if (RegistryIndex != GENERIC_BUFFER_INDEX_INVALID)
    {
        GenericBuffer_RemoveAt(&self->_widgets, RegistryIndex);
    }
    return Error_CreateSuccess();
}

bool UIScreen_TryGetWidgetById(UIScreen* self, uint64_t widgetId, Widget** outWidget)
{
    if ((self == NULL) || (outWidget == NULL))
    {
        return false;
    }
    Widget* Found = LookupWidget(self, widgetId);
    *outWidget = Found;
    return Found != NULL;
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
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_AddWidget: widget belongs to a different screen.");
    }
    if ((Widget_GetParent(widget) != NULL) || Widget_IsScreenRoot(widget))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_AddWidget: widget is already attached.");
    }

    if (!GenericBuffer_AddLast(&self->_roots, &widget))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_AddWidget: widget could not be stored.");
    }
    Widget_SetScreenRoot(widget, true);
    return Error_CreateSuccess();
}

Error UIScreen_RemoveWidget(UIScreen* self, Widget* widget)
{
    if ((self == NULL) || (widget == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_RemoveWidget: self and widget must not be NULL.");
    }

    size_t RootIndex = GENERIC_BUFFER_INDEX_INVALID;
    for (size_t Index = 0; Index < self->_roots._count; Index++)
    {
        if (*(Widget**)GenericBuffer_GetPointerToElement(&self->_roots, Index) == widget)
        {
            RootIndex = Index;
            break;
        }
    }
    if (RootIndex == GENERIC_BUFFER_INDEX_INVALID)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_RemoveWidget: widget is not a root.");
    }

    Error FirstError = Error_CreateSuccess();

    if (self->_focusedWidget == widget)
    {
        Error FocusResult = Widget_SetFocused(widget, false);
        KeepFirstError(&FirstError, &FocusResult);
        self->_focusedWidget = NULL;
    }
    if (self->_isNavigating)
    {
        Error NavResult = StopNavigation(self);
        KeepFirstError(&FirstError, &NavResult);
    }

    // Purge animations and input references for the removed subtree.
    size_t AnimationCursor = 0;
    while (AnimationCursor < self->_animations._count)
    {
        AnimationRecord* Record = GenericBuffer_GetPointerToElement(&self->_animations, AnimationCursor);
        Widget* Target = LookupWidget(self, Record->WidgetId);
        if ((Target != NULL) && IsInSubtree(Target, widget))
        {
            Memory_Free(Record->Keyframes._data);
            GenericBuffer_RemoveAt(&self->_animations, AnimationCursor);
        }
        else
        {
            AnimationCursor++;
        }
    }
    Widget* Hovered = LookupWidget(self, self->_hoveredWidgetId);
    if ((Hovered != NULL) && IsInSubtree(Hovered, widget))
    {
        self->_hoveredWidgetId = 0;
    }
    for (int ButtonIndex = 0; ButtonIndex < (int)UIMouseButton_Count; ButtonIndex++)
    {
        Widget* ButtonTarget = LookupWidget(self, self->_buttons[ButtonIndex].TargetWidgetId);
        if ((ButtonTarget != NULL) && IsInSubtree(ButtonTarget, widget))
        {
            self->_buttons[ButtonIndex].IsPressed = false;
            self->_buttons[ButtonIndex].TargetWidgetId = 0;
        }
    }

    GenericBuffer_RemoveAt(&self->_roots, RootIndex);
    Widget_SetScreenRoot(widget, false);
    return FirstError;
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


// Public functions: focus and z ordering.
Error UIScreen_SetFocusedWidget(UIScreen* self, Widget* widget)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_SetFocusedWidget: self must not be NULL.");
    }

    Widget* NewFocus = (widget != NULL) ? GetRoot(widget) : NULL;
    if (NewFocus == self->_focusedWidget)
    {
        return Error_CreateSuccess();
    }

    if (self->_focusedWidget != NULL)
    {
        Error DisableResult = Widget_SetFocused(self->_focusedWidget, false);
        if (DisableResult.Code != ErrorCode_Success)
        {
            return DisableResult;
        }
    }

    self->_focusedWidget = NewFocus;

    if (NewFocus != NULL)
    {
        Error EnableResult = Widget_SetFocused(NewFocus, true);
        if (EnableResult.Code != ErrorCode_Success)
        {
            return EnableResult;
        }
        Error TopResult = UIScreen_BringToTop(self, NewFocus);
        if (TopResult.Code != ErrorCode_Success)
        {
            return TopResult;
        }
    }
    return Error_CreateSuccess();
}

Error UIScreen_BringToTop(UIScreen* self, Widget* widget)
{
    if ((self == NULL) || (widget == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_BringToTop: self and widget must not be NULL.");
    }

    float MaxZ = 0.0f;
    bool HasSibling = false;
    Widget* Parent = Widget_GetParent(widget);

    if (Parent != NULL)
    {
        size_t Count = Widget_GetSubWidgetCount(Parent);
        for (size_t Index = 0; Index < Count; Index++)
        {
            Widget* Sibling = Widget_GetSubWidgetAt(Parent, Index);
            if (Sibling != widget)
            {
                float Z = Widget_GetZLayer(Sibling);
                if (!HasSibling || (Z > MaxZ)) { MaxZ = Z; HasSibling = true; }
            }
        }
    }
    else
    {
        for (size_t Index = 0; Index < self->_roots._count; Index++)
        {
            Widget* Sibling = *(Widget**)GenericBuffer_GetPointerToElement(&self->_roots, Index);
            if (Sibling != widget)
            {
                float Z = Widget_GetZLayer(Sibling);
                if (!HasSibling || (Z > MaxZ)) { MaxZ = Z; HasSibling = true; }
            }
        }
    }

    if (HasSibling)
    {
        return Widget_SetZLayer(widget, MaxZ + BRING_TO_TOP_Z_STEP);
    }
    return Error_CreateSuccess();
}

bool UIScreen_TryGetTopmostAt(UIScreen* self, Vector2 screenPosition, Widget** outWidget)
{
    if ((self == NULL) || (outWidget == NULL))
    {
        return false;
    }
    *outWidget = NULL;

    Vector2 RootPos = { .x = 0.0f, .y = 0.0f };
    Vector2 RootSize = { .x = 1.0f, .y = 1.0f };

    SortRootsByZ(self);
    for (size_t Index = self->_roots._count; Index-- > 0;)
    {
        Widget* Root = *(Widget**)GenericBuffer_GetPointerToElement(&self->_roots, Index);
        Widget* Hit = HitTestNode(Root, screenPosition, RootPos, RootSize);
        if (Hit != NULL)
        {
            *outWidget = Hit;
            return true;
        }
    }
    return false;
}


// Public functions: coordinate conversion.
Vector2 UIScreen_WidgetToScreen(UIScreen* self, const Widget* widget, Vector2 widgetLocal)
{
    UNUSED(self);
    Vector2 AbsolutePos;
    Vector2 AbsoluteSize;
    GetWidgetAbsoluteBox(widget, &AbsolutePos, &AbsoluteSize);
    return (Vector2)
    {
        .x = AbsolutePos.x + (widgetLocal.x * AbsoluteSize.x),
        .y = AbsolutePos.y + (widgetLocal.y * AbsoluteSize.y)
    };
}

Vector2 UIScreen_ScreenToWidget(UIScreen* self, const Widget* widget, Vector2 screenPosition)
{
    UNUSED(self);
    Vector2 AbsolutePos;
    Vector2 AbsoluteSize;
    GetWidgetAbsoluteBox(widget, &AbsolutePos, &AbsoluteSize);
    return (Vector2)
    {
        .x = (AbsoluteSize.x != 0.0f) ? ((screenPosition.x - AbsolutePos.x) / AbsoluteSize.x) : 0.0f,
        .y = (AbsoluteSize.y != 0.0f) ? ((screenPosition.y - AbsolutePos.y) / AbsoluteSize.y) : 0.0f
    };
}


// Public functions: update and render.
Error UIScreen_Update(UIScreen* self, ProgramTime time)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_Update: self must not be NULL.");
    }

    // Snapshot the live widgets first, so widgets added this tick update next tick and removed ones are skipped.
    GenericBuffer_Clear(&self->_updateSnapshot);
    for (size_t Index = 0; Index < self->_roots._count; Index++)
    {
        Widget* Root = *(Widget**)GenericBuffer_GetPointerToElement(&self->_roots, Index);
        Error SnapshotResult = SnapshotWidget(Root, &self->_updateSnapshot);
        if (SnapshotResult.Code != ErrorCode_Success)
        {
            return SnapshotResult;
        }
    }

    Error InputResult = ProcessInput(self, time);
    if (InputResult.Code != ErrorCode_Success)
    {
        return InputResult;
    }

    Error AnimationResult = AdvanceAnimations(self, time);
    if (AnimationResult.Code != ErrorCode_Success)
    {
        return AnimationResult;
    }

    for (size_t Index = 0; Index < self->_updateSnapshot._count; Index++)
    {
        uint64_t Id = *(uint64_t*)GenericBuffer_GetPointerToElement(&self->_updateSnapshot, Index);
        Widget* Target = LookupWidget(self, Id);
        if ((Target == NULL) || !IsWidgetLive(Target) || !Widget_IsUpdated(Target))
        {
            continue;
        }
        Error UpdateResult = Widget_Update(Target, time);
        if (UpdateResult.Code != ErrorCode_Success)
        {
            return UpdateResult;
        }
    }
    return Error_CreateSuccess();
}

Error UIScreen_Render(UIScreen* self, RenderContext* renderContext)
{
    if ((self == NULL) || (renderContext == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_Render: self and renderContext must not be NULL.");
    }

    Vector2 RootPos = { .x = 0.0f, .y = 0.0f };
    Vector2 RootSize = { .x = 1.0f, .y = 1.0f };
    RenderColor RootTint = RenderColor_White();

    SortRootsByZ(self);
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0; Index < self->_roots._count; Index++)
    {
        Widget* Root = *(Widget**)GenericBuffer_GetPointerToElement(&self->_roots, Index);
        Result = RenderNode(self, renderContext, Root, RootPos, RootSize, RootTint,
            (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f }, false);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    DrawNavigationOutlines(self, renderContext);
    return Error_CreateSuccess();
}


// Public functions: input state queries.
bool UIScreen_IsMouseButtonDown(const UIScreen* self, UIMouseButton button)
{
    UNUSED(self);
    return IsMouseButtonDown(MapButtonToRaylib(button));
}

bool UIScreen_IsMouseButtonPressed(const UIScreen* self, UIMouseButton button)
{
    UNUSED(self);
    return IsMouseButtonPressed(MapButtonToRaylib(button));
}

bool UIScreen_IsMouseButtonReleased(const UIScreen* self, UIMouseButton button)
{
    UNUSED(self);
    return IsMouseButtonReleased(MapButtonToRaylib(button));
}

bool UIScreen_IsKeyDown(const UIScreen* self, UIKey key)
{
    UNUSED(self);
    int RaylibKey = MapUIKeyToRaylib(key);
    return (RaylibKey != KEY_NULL) && IsKeyDown(RaylibKey);
}

bool UIScreen_IsKeyPressed(const UIScreen* self, UIKey key)
{
    UNUSED(self);
    int RaylibKey = MapUIKeyToRaylib(key);
    return (RaylibKey != KEY_NULL) && IsKeyPressed(RaylibKey);
}

bool UIScreen_IsKeyReleased(const UIScreen* self, UIKey key)
{
    UNUSED(self);
    int RaylibKey = MapUIKeyToRaylib(key);
    return (RaylibKey != KEY_NULL) && IsKeyReleased(RaylibKey);
}


// Public functions: animation.
Error UIScreen_StartAnimation(UIScreen* self,
    uint64_t widgetId,
    int32_t propertyId,
    UIPropertyType type,
    const UIKeyframe* keyframes,
    size_t keyframeCount,
    UIAnimationOptions options,
    uint64_t* outAnimationId)
{
    if ((self == NULL) || (keyframes == NULL) || (outAnimationId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_StartAnimation: self, keyframes and outAnimationId must not be NULL.");
    }
    if (keyframeCount == 0)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_StartAnimation: keyframeCount must be at least 1.");
    }
    if (UIPropertyType_GetValueSize(type) == 0)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIScreen_StartAnimation: unrecognized property type.");
    }
    if (LookupWidget(self, widgetId) == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_StartAnimation: target widget does not exist.");
    }

    AnimationRecord Record;
    Record.Id = self->_nextAnimationId;
    Record.WidgetId = widgetId;
    Record.PropertyId = propertyId;
    Record.Type = type;
    Record.Options = options;
    Record.Elapsed = 0.0;
    Record.IsPendingRemoval = false;
    GenericBuffer_AllocateVariable(&Record.Keyframes, keyframeCount, sizeof(UIKeyframe));

    for (size_t Index = 0; Index < keyframeCount; Index++)
    {
        UIKeyframe Keyframe = keyframes[Index];
        if (!GenericBuffer_AddLast(&Record.Keyframes, &Keyframe))
        {
            Memory_Free(Record.Keyframes._data);
            return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_StartAnimation: keyframe storage could not grow.");
        }
    }

    if (!GenericBuffer_AddLast(&self->_animations, &Record))
    {
        Memory_Free(Record.Keyframes._data);
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_StartAnimation: animation storage could not grow.");
    }

    *outAnimationId = Record.Id;
    self->_nextAnimationId++;
    return Error_CreateSuccess();
}

Error UIScreen_StopAnimation(UIScreen* self, uint64_t animationId)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_StopAnimation: self must not be NULL.");
    }

    for (size_t Index = 0; Index < self->_animations._count; Index++)
    {
        AnimationRecord* Record = GenericBuffer_GetPointerToElement(&self->_animations, Index);
        if (Record->Id == animationId)
        {
            Memory_Free(Record->Keyframes._data);
            GenericBuffer_RemoveAt(&self->_animations, Index);
            return Error_CreateSuccess();
        }
    }
    return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_StopAnimation: no animation with that id.");
}

Error UIScreen_StopWidgetAnimations(UIScreen* self, uint64_t widgetId)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_StopWidgetAnimations: self must not be NULL.");
    }

    size_t Index = 0;
    while (Index < self->_animations._count)
    {
        AnimationRecord* Record = GenericBuffer_GetPointerToElement(&self->_animations, Index);
        if (Record->WidgetId == widgetId)
        {
            Memory_Free(Record->Keyframes._data);
            GenericBuffer_RemoveAt(&self->_animations, Index);
        }
        else
        {
            Index++;
        }
    }
    return Error_CreateSuccess();
}
