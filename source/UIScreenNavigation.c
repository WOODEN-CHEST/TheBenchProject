#include "UIScreen.h"
#include "UIInternal.h"
#include "UIWidget.h"
#include "GameInput.h"
#include "wr/WRMemory.h"
#include "wr/WRMath.h"


// Macros.
/** Perpendicular-distance penalty when picking the nearest widget for arrow navigation. */
#define NAV_PERPENDICULAR_PENALTY 2.0f


// Types.
/* One focusable widget of the current navigation level, with its estimated box in screen coordinates. */
typedef struct NavCandidateStruct
{
    uint64_t Id;
    Rectangle Box;
} NavCandidate;

/* An arrow-navigation direction. */
typedef enum NavDirectionEnum
{
    NavDirection_Up,
    NavDirection_Down,
    NavDirection_Left,
    NavDirection_Right
} NavDirection;

/* A navigation step that repeats while its key auto-repeats: sequential (ui_next / ui_previous) or an arrow. */
typedef struct NavRepeatingStepStruct
{
    GameActionId Action;
    bool IsSequential;
    /* Sequential steps: towards the next candidate (ui_next) or the previous one (ui_previous). */
    bool IsForward;
    /* Arrow steps: the direction. */
    NavDirection Direction;
} NavRepeatingStep;


// Fields.
/* The repeating steps, in the order they are checked within a tick. */
static const NavRepeatingStep RepeatingSteps[] =
{
    { .Action = GameAction_UIPrevious, .IsSequential = true, .IsForward = false },
    { .Action = GameAction_UINext, .IsSequential = true, .IsForward = true },
    { .Action = GameAction_UIUp, .IsSequential = false, .Direction = NavDirection_Up },
    { .Action = GameAction_UIDown, .IsSequential = false, .Direction = NavDirection_Down },
    { .Action = GameAction_UILeft, .IsSequential = false, .Direction = NavDirection_Left },
    { .Action = GameAction_UIRight, .IsSequential = false, .Direction = NavDirection_Right },
};
#define NAV_REPEATING_STEP_COUNT (sizeof(RepeatingSteps) / sizeof(RepeatingSteps[0]))


// Static functions: candidates.
static bool IsCandidate(const Widget* widget)
{
    return widget->_isFocusable && widget->_isRendered && widget->_isInputEnabled && widget->_isInitialized
        && !widget->_isDying;
}

static NavCandidate* GetCandidateAt(UIScreen* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_navCandidates, index);
}

static uint64_t GetNavStackAt(const UIScreen* self, size_t index)
{
    return *(const uint64_t*)GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_navStack, index);
}

/* Adds the focusable widgets of a subtree: a focusable widget is a candidate (navigation enters it with ui_accept),
 * a non-focusable one is transparent. Recursion is bounded by WIDGET_MAX_TREE_DEPTH. */
static void CollectCandidates(UIScreen* self, Widget* widget)
{
    if (IsCandidate(widget))
    {
        NavCandidate Candidate = { .Id = Widget_GetId(widget), .Box = UIScreen_InternalGetEstimatedScreenBox(widget) };
        GenericBuffer_AddLast(&self->_navCandidates, &Candidate); // A full list just navigates fewer widgets.
        return;
    }
    for (size_t Index = 0; Index < Widget_GetSubWidgetCount(widget); Index++)
    {
        CollectCandidates(self, Widget_GetSubWidgetAt(widget, Index));
    }
}

static bool IsBeforeInReadingOrder(const NavCandidate* a, const NavCandidate* b)
{
    return (a->Box.y < b->Box.y) || ((a->Box.y == b->Box.y) && (a->Box.x < b->Box.x));
}

/* Gathers the candidates of a level (a container's subtree, or every root for the top level) in reading order: top row
 * first, then left. A stable insertion sort: levels are small and usually nearly sorted already. */
static void GatherCandidates(UIScreen* self, Widget* container)
{
    GenericBuffer_Clear(&self->_navCandidates);
    if (container != NULL)
    {
        for (size_t Index = 0; Index < Widget_GetSubWidgetCount(container); Index++)
        {
            CollectCandidates(self, Widget_GetSubWidgetAt(container, Index));
        }
    }
    else
    {
        for (size_t Index = 0; Index < UIScreen_GetRootCount(self); Index++)
        {
            CollectCandidates(self, UIScreen_GetRootAt(self, Index));
        }
    }

    for (size_t Index = 1; Index < self->_navCandidates._count; Index++)
    {
        NavCandidate Moving = *GetCandidateAt(self, Index);
        size_t Position = Index;
        while ((Position > 0U) && IsBeforeInReadingOrder(&Moving, GetCandidateAt(self, Position - 1U)))
        {
            *GetCandidateAt(self, Position) = *GetCandidateAt(self, Position - 1U);
            Position--;
        }
        *GetCandidateAt(self, Position) = Moving;
    }
}

/* The widget whose level navigation is on: the innermost entered widget, or NULL for the top level. */
static Widget* GetLevelContainer(UIScreen* self)
{
    if (self->_navStack._count == 0U)
    {
        return NULL;
    }
    return UIScreen_InternalResolve(self, GetNavStackAt(self, self->_navStack._count - 1U));
}

static size_t FindCandidate(UIScreen* self, uint64_t widgetId)
{
    for (size_t Index = 0; Index < self->_navCandidates._count; Index++)
    {
        if (GetCandidateAt(self, Index)->Id == widgetId)
        {
            return Index;
        }
    }
    return GENERIC_BUFFER_INDEX_INVALID;
}

static Vector2 GetBoxCenter(Rectangle box)
{
    return (Vector2){ .x = box.x + (box.width * 0.5f), .y = box.y + (box.height * 0.5f) };
}


// Static functions: the entered path.
static bool IsOnNavStack(const UIScreen* self, uint64_t widgetId)
{
    for (size_t Index = 0; Index < self->_navStack._count; Index++)
    {
        if (GetNavStackAt(self, Index) == widgetId)
        {
            return true;
        }
    }
    return false;
}

/* The entered path that leads to focus: the focused widget's focusable ancestors, outermost first. Returns their count
 * (0 with nothing focused): at most WIDGET_MAX_TREE_DEPTH - 1, so the array has room left for the focused widget. */
static size_t GetFocusPath(UIScreen* self, uint64_t outIds[WIDGET_MAX_TREE_DEPTH])
{
    size_t Count = 0;
    Widget* Focused = UIScreen_InternalResolve(self, self->_focusedWidgetId);
    for (Widget* Ancestor = (Focused != NULL) ? Widget_GetParent(Focused) : NULL;
        (Ancestor != NULL) && (Count < (WIDGET_MAX_TREE_DEPTH - 1U)); Ancestor = Widget_GetParent(Ancestor))
    {
        if (IsCandidate(Ancestor))
        {
            outIds[Count++] = Widget_GetId(Ancestor);
        }
    }
    // Collected innermost first.
    for (size_t Index = 0; Index < (Count / 2U); Index++)
    {
        uint64_t Swapped = outIds[Index];
        outIds[Index] = outIds[Count - 1U - Index];
        outIds[Count - 1U - Index] = Swapped;
    }
    return Count;
}

/* Makes @p ids (outermost first) the entered path: the entries no longer on it are left (untabbed, innermost first),
 * then the new ones are entered (tabbed, outermost first). Every hook may change the tree, so each widget is resolved
 * again, and an entry is only tabbed while it is still on the path, alive and attached (a hook that detaches an entry
 * also takes it off the path). */
static Error SetNavPath(UIScreen* self, const uint64_t* ids, size_t count)
{
    if (!GenericBuffer_EnsureTotalCapacity(&self->_navStack, count))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen: the navigation path could not grow.");
    }
    size_t CommonCount = 0;
    while ((CommonCount < count) && (CommonCount < self->_navStack._count) && (GetNavStackAt(self, CommonCount) == ids[CommonCount]))
    {
        CommonCount++;
    }
    uint64_t OldIds[WIDGET_MAX_TREE_DEPTH];
    size_t OldCount = 0;
    for (size_t Index = CommonCount; (Index < self->_navStack._count) && (OldCount < WIDGET_MAX_TREE_DEPTH); Index++)
    {
        OldIds[OldCount++] = GetNavStackAt(self, Index);
    }
    GenericBuffer_SetCount(&self->_navStack, CommonCount);
    for (size_t Index = CommonCount; Index < count; Index++)
    {
        uint64_t EnteredId = ids[Index];
        GenericBuffer_AddLast(&self->_navStack, &EnteredId); // Cannot fail: the capacity is reserved.
    }

    for (size_t Index = OldCount; Index-- > 0U;)
    {
        Widget* Left = UIScreen_InternalResolve(self, OldIds[Index]);
        if ((Left != NULL) && !IsOnNavStack(self, OldIds[Index]))
        {
            Error Result = Widget_InternalSetTabbed(Left, false);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
    }
    for (size_t Index = CommonCount; Index < count; Index++)
    {
        Widget* Entered = UIScreen_InternalResolve(self, ids[Index]);
        if ((Entered != NULL) && IsOnNavStack(self, ids[Index]) && UIScreen_InternalIsAttached(Entered))
        {
            Error Result = Widget_InternalSetTabbed(Entered, true);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
    }
    return Error_CreateSuccess();
}

/* While navigating, focus can move without navigation (UIScreen_SetFocusedWidget, a hook): before each step the entered
 * path is brought back in line with focus. With nothing focused the path stays, so after the focused widget is destroyed
 * navigation carries on at its level. */
static Error SyncNavPathToFocus(UIScreen* self)
{
    if (UIScreen_InternalResolve(self, self->_focusedWidgetId) == NULL)
    {
        return Error_CreateSuccess();
    }
    uint64_t PathIds[WIDGET_MAX_TREE_DEPTH];
    size_t PathCount = GetFocusPath(self, PathIds);
    return SetNavPath(self, PathIds, PathCount);
}


// Static functions: actions.
/* Starts navigation where focus is: the focused widget's focusable ancestors become the entered path, and focus moves
 * to the level's first candidate unless it is already one. Does nothing (and is not acted) when that level has no
 * candidates. */
static Error StartNavigation(UIScreen* self, bool* outIsActed)
{
    uint64_t PathIds[WIDGET_MAX_TREE_DEPTH];
    size_t PathCount = GetFocusPath(self, PathIds);
    GatherCandidates(self, (PathCount > 0U) ? UIScreen_InternalResolve(self, PathIds[PathCount - 1U]) : NULL);
    if (self->_navCandidates._count == 0U)
    {
        *outIsActed = false;
        return Error_CreateSuccess();
    }
    uint64_t TargetId = (FindCandidate(self, self->_focusedWidgetId) != GENERIC_BUFFER_INDEX_INVALID)
        ? self->_focusedWidgetId : GetCandidateAt(self, 0U)->Id;

    *outIsActed = true;
    self->_isNavigating = true;
    Error Result = SetNavPath(self, PathIds, PathCount);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIScreen_InternalSetFocus(self, TargetId);
}

/* Brings the entered path in line with focus and gathers the current level; with no candidates left there, navigation
 * ends (and the key is not used). */
static Error GatherLevelOrStop(UIScreen* self, bool* outHasCandidates)
{
    *outHasCandidates = false;
    Error Result = SyncNavPathToFocus(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GatherCandidates(self, GetLevelContainer(self));
    *outHasCandidates = self->_navCandidates._count > 0U;
    return *outHasCandidates ? Error_CreateSuccess() : UIScreen_InternalStopNavigation(self);
}

/* ui_next / ui_previous: the next or previous candidate in reading order, wrapping around. */
static Error NavigateSequential(UIScreen* self, bool isForward, bool* outIsActed)
{
    if (!self->_isNavigating)
    {
        return StartNavigation(self, outIsActed);
    }
    Error Result = GatherLevelOrStop(self, outIsActed);
    if ((Result.Code != ErrorCode_Success) || !*outIsActed)
    {
        return Result;
    }
    size_t Count = self->_navCandidates._count;
    size_t Current = FindCandidate(self, self->_focusedWidgetId);
    size_t Next = 0;
    if (Current == GENERIC_BUFFER_INDEX_INVALID)
    {
        Next = isForward ? 0U : (Count - 1U);
    }
    else
    {
        Next = isForward ? ((Current + 1U) % Count) : ((Current + Count - 1U) % Count);
    }
    return UIScreen_InternalSetFocus(self, GetCandidateAt(self, Next)->Id);
}

/* An arrow: the nearest candidate in that direction (primary distance plus a penalty for the perpendicular offset).
 * While navigating the key is used even at the edge, so arrows do not leak to the game in the middle of a menu. */
static Error NavigateArrow(UIScreen* self, NavDirection direction, bool* outIsActed)
{
    if (!self->_isNavigating)
    {
        return StartNavigation(self, outIsActed);
    }
    Error Result = GatherLevelOrStop(self, outIsActed);
    if ((Result.Code != ErrorCode_Success) || !*outIsActed)
    {
        return Result;
    }
    size_t Current = FindCandidate(self, self->_focusedWidgetId);
    if (Current == GENERIC_BUFFER_INDEX_INVALID)
    {
        return UIScreen_InternalSetFocus(self, GetCandidateAt(self, 0U)->Id);
    }

    Vector2 From = GetBoxCenter(GetCandidateAt(self, Current)->Box);
    uint64_t BestId = WIDGET_ID_INVALID;
    float BestDistance = 0.0f;
    for (size_t Index = 0; Index < self->_navCandidates._count; Index++)
    {
        if (Index == Current)
        {
            continue;
        }
        Vector2 To = GetBoxCenter(GetCandidateAt(self, Index)->Box);
        float DeltaX = To.x - From.x;
        float DeltaY = To.y - From.y;
        float Primary = 0.0f;
        float Perpendicular = 0.0f;
        switch (direction)
        {
            case NavDirection_Right: Primary = DeltaX;  Perpendicular = Math_AbsFloat(DeltaY); break;
            case NavDirection_Left:  Primary = -DeltaX; Perpendicular = Math_AbsFloat(DeltaY); break;
            case NavDirection_Down:  Primary = DeltaY;  Perpendicular = Math_AbsFloat(DeltaX); break;
            case NavDirection_Up:    Primary = -DeltaY; Perpendicular = Math_AbsFloat(DeltaX); break;
        }
        if (Primary <= 0.0f)
        {
            continue; // Not in that direction.
        }
        float Distance = Primary + (NAV_PERPENDICULAR_PENALTY * Perpendicular);
        if ((BestId == WIDGET_ID_INVALID) || (Distance < BestDistance))
        {
            BestId = GetCandidateAt(self, Index)->Id;
            BestDistance = Distance;
        }
    }
    return (BestId != WIDGET_ID_INVALID) ? UIScreen_InternalSetFocus(self, BestId) : Error_CreateSuccess();
}

/* ui_accept: enters the focused widget when it contains candidates (it becomes tabbed and comes to the top; focus moves
 * to its first candidate), starting navigation if needed, so Enter also opens a container focused by a click. Otherwise
 * it is left to the game. */
static Error NavigateAccept(UIScreen* self, bool* outIsActed)
{
    *outIsActed = false;
    Widget* Focused = UIScreen_InternalResolve(self, self->_focusedWidgetId);
    if (Focused == NULL)
    {
        return Error_CreateSuccess();
    }
    GatherCandidates(self, Focused);
    if (self->_navCandidates._count == 0U)
    {
        return Error_CreateSuccess();
    }

    *outIsActed = true;
    self->_isNavigating = true;
    uint64_t EnteredId = Widget_GetId(Focused);
    uint64_t FirstId = GetCandidateAt(self, 0U)->Id; // Captured before any hook runs.
    uint64_t PathIds[WIDGET_MAX_TREE_DEPTH];
    size_t PathCount = GetFocusPath(self, PathIds);
    PathIds[PathCount++] = EnteredId;
    Error Result = SetNavPath(self, PathIds, PathCount);
    Widget* Entered = UIScreen_InternalResolve(self, EnteredId);
    if ((Result.Code == ErrorCode_Success) && (Entered != NULL) && IsOnNavStack(self, EnteredId))
    {
        Result = UIScreen_BringToTop(self, Entered);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIScreen_InternalSetFocus(self, FirstId); // Nothing ends up focused if a hook detached it.
}

/* ui_back: leaves the innermost entered widget (it becomes focused again); at the top level it ends navigation and
 * clears focus; outside navigation it clears focus. With nothing focused outside navigation it is left to the game. */
static Error NavigateBack(UIScreen* self, bool* outIsActed)
{
    *outIsActed = true;
    Error Result = self->_isNavigating ? SyncNavPathToFocus(self) : Error_CreateSuccess();
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (self->_isNavigating && (self->_navStack._count > 0U))
    {
        uint64_t PathIds[WIDGET_MAX_TREE_DEPTH];
        size_t PathCount = 0;
        for (; (PathCount < self->_navStack._count) && (PathCount < WIDGET_MAX_TREE_DEPTH); PathCount++)
        {
            PathIds[PathCount] = GetNavStackAt(self, PathCount);
        }
        uint64_t LeftId = PathIds[PathCount - 1U];
        Result = SetNavPath(self, PathIds, PathCount - 1U);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        return UIScreen_InternalSetFocus(self, LeftId);
    }
    if (self->_isNavigating)
    {
        Result = UIScreen_InternalStopNavigation(self);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        return UIScreen_InternalSetFocus(self, WIDGET_ID_INVALID);
    }
    if (self->_focusedWidgetId != WIDGET_ID_INVALID)
    {
        return UIScreen_InternalSetFocus(self, WIDGET_ID_INVALID);
    }
    *outIsActed = false;
    return Error_CreateSuccess();
}

/* Consumes the action of a step that used it. For a repeating step, its keys that are still held are followed: the
 * consumption hides their auto-repeat from the usual queries. */
static Error ApplyStep(UIScreen* self, GameActionId action, Error result, bool isActed, bool isRepeating)
{
    if ((result.Code != ErrorCode_Success) || !isActed)
    {
        return result;
    }
    if (isRepeating)
    {
        GameKey Keys[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
        size_t KeyCount = GameInput_GetActionKeys(self->_input, action, Keys);
        for (size_t Index = 0; Index < KeyCount; Index++)
        {
            if ((GameInput_GetRawKeyState(self->_input, Keys[Index]) & GAME_INPUT_KEY_STATE_DOWN) != 0)
            {
                self->_navigationKeys[Keys[Index]] = true;
            }
        }
    }
    GameInput_ConsumeAction(self->_input, action);
    return result;
}


// Static functions: held keys.
/* Whether @p key is one of the keys an action's bindings match this tick (consumed or not). */
static bool IsActionKey(const GameInput* input, GameActionId action, GameKey key)
{
    GameKey Keys[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t KeyCount = GameInput_GetActionKeys(input, action, Keys);
    for (size_t Index = 0; Index < KeyCount; Index++)
    {
        if (Keys[Index] == key)
        {
            return true;
        }
    }
    return false;
}

/* Follows the keys navigation consumed while they are held: marks the repeating steps whose action one of them
 * auto-repeats this tick (with the modifiers held now), and forgets the keys that are up or pressed anew (a new press
 * is unconsumed, so it is routed as usual). */
static void FollowNavigationKeys(UIScreen* self, bool isRepeated[NAV_REPEATING_STEP_COUNT])
{
    for (int32_t KeyIndex = (int32_t)GameKey_Unknown + 1; KeyIndex < (int32_t)GameKey_Count; KeyIndex++)
    {
        if (!self->_navigationKeys[KeyIndex])
        {
            continue;
        }
        GameKey Key = (GameKey)KeyIndex;
        uint8_t Raw = GameInput_GetRawKeyState(self->_input, Key);
        if (((Raw & GAME_INPUT_KEY_STATE_DOWN) == 0) || ((Raw & GAME_INPUT_KEY_STATE_PRESSED) != 0))
        {
            self->_navigationKeys[KeyIndex] = false;
            continue;
        }
        if ((Raw & GAME_INPUT_KEY_STATE_REPEATED) == 0)
        {
            continue;
        }
        for (size_t StepIndex = 0; StepIndex < NAV_REPEATING_STEP_COUNT; StepIndex++)
        {
            isRepeated[StepIndex] = isRepeated[StepIndex] || IsActionKey(self->_input, RepeatingSteps[StepIndex].Action, Key);
        }
    }
}


// Internal functions (declared in UIInternal.h).
void UIScreen_InternalInitializeNavigation(UIScreen* self)
{
    GenericBuffer_AllocateVariable(&self->_navStack, 4U, sizeof(uint64_t));
    GenericBuffer_AllocateVariable(&self->_navCandidates, 8U, sizeof(NavCandidate));
}

void UIScreen_InternalReleaseNavigation(UIScreen* self)
{
    Memory_Free(self->_navStack._data);
    Memory_Free(self->_navCandidates._data);
    Memory_Zero(&self->_navStack, sizeof(self->_navStack));
    Memory_Zero(&self->_navCandidates, sizeof(self->_navCandidates));
}

Error UIScreen_InternalRouteNavigation(UIScreen* self)
{
    bool IsRepeated[NAV_REPEATING_STEP_COUNT] = { false };
    FollowNavigationKeys(self, IsRepeated);

    Error Result = Error_CreateSuccess();
    for (size_t Index = 0; (Result.Code == ErrorCode_Success) && (Index < NAV_REPEATING_STEP_COUNT); Index++)
    {
        const NavRepeatingStep* Step = &RepeatingSteps[Index];
        if (IsRepeated[Index] || GameInput_IsActionPressedOrRepeated(self->_input, Step->Action))
        {
            bool IsActed = false;
            Result = Step->IsSequential ? NavigateSequential(self, Step->IsForward, &IsActed)
                : NavigateArrow(self, Step->Direction, &IsActed);
            Result = ApplyStep(self, Step->Action, Result, IsActed, true);
        }
    }
    if ((Result.Code == ErrorCode_Success) && GameInput_IsActionPressed(self->_input, GameAction_UIAccept))
    {
        bool IsActed = false;
        Result = NavigateAccept(self, &IsActed);
        Result = ApplyStep(self, GameAction_UIAccept, Result, IsActed, false);
    }
    if ((Result.Code == ErrorCode_Success) && GameInput_IsActionPressed(self->_input, GameAction_UIBack))
    {
        bool IsActed = false;
        Result = NavigateBack(self, &IsActed);
        Result = ApplyStep(self, GameAction_UIBack, Result, IsActed, false);
    }
    return Result;
}

Error UIScreen_InternalStopNavigation(UIScreen* self)
{
    self->_isNavigating = false;
    Memory_Zero(self->_navigationKeys, sizeof(self->_navigationKeys));
    return SetNavPath(self, NULL, 0U);
}
