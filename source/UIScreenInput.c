#include "UIScreen.h"
#include "UIInternal.h"
#include "UIWidget.h"
#include "GameInput.h"
#include "GameKey.h"
#include "wr/WRMemory.h"


// Types.
/* What the pointer did this tick. */
typedef struct PointerMotionStruct
{
    /* The pointer over the screen moved, came or went: hover is looked up again. */
    bool HasHoverMoved;
    /* A held press hears of it. */
    bool IsDrag;
    /* A hidden-cursor drag: the position stands still, and held presses hear the raw motion. */
    bool IsRelative;
    Vector2 RelativeDeltaPixels;
} PointerMotion;


// Static functions: helpers.
static GameKey GetMouseButtonAt(size_t index)
{
    return (GameKey)((int32_t)GameKey_MouseLeft + (int32_t)index);
}

static bool IsPointInUnitSquare(Vector2 point)
{
    return (point.x >= 0.0f) && (point.x <= 1.0f) && (point.y >= 0.0f) && (point.y <= 1.0f);
}

/* Keeps the first non-success error and releases any later one. */
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

/* The top-most input-enabled widget under the pointer, or NULL (also when there is no pointer). */
static Widget* GetWidgetUnderPointer(UIScreen* self)
{
    Widget* Top = NULL;
    if (self->_hasPointer)
    {
        UIScreen_TryGetTopmostAt(self, self->_pointerPosition, &Top);
    }
    return Top;
}

static bool IsAnyPressHeld(const UIScreen* self)
{
    for (size_t Index = 0; Index < UI_SCREEN_MOUSE_BUTTON_COUNT; Index++)
    {
        if (self->_buttons[Index].IsPressed)
        {
            return true;
        }
    }
    return false;
}

/* Whether a widget holds a press of any button. */
static bool IsHoldingPress(const UIScreen* self, uint64_t widgetId)
{
    for (size_t Index = 0; Index < UI_SCREEN_MOUSE_BUTTON_COUNT; Index++)
    {
        if (self->_buttons[Index].IsPressed && (self->_buttons[Index].TargetWidgetId == widgetId))
        {
            return true;
        }
    }
    return false;
}

static WidgetMouseInputArgs MakeMouseArgs(const UIScreen* self, UIMouseInputType type, GameKey button, const Widget* widget,
    const UIScreenButtonTracking* press, ProgramTime time, Vector2 deltaPixels)
{
    return (WidgetMouseInputArgs)
    {
        .Type = type,
        .Button = button,
        .Modifiers = GameInput_GetHeldModifiers(self->_input),
        .ScreenPosition = self->_pointerPosition,
        .WidgetPosition = UIScreen_ScreenToWidget(self, widget, self->_pointerPosition),
        .ClickStartScreenPosition = press->StartScreenPosition,
        .ClickStartWidgetPosition = UIScreen_ScreenToWidget(self, widget, press->StartScreenPosition),
        .DurationSeconds = time.TotalTime - press->StartTime,
        .DeltaPixels = deltaPixels,
        .ClickCount = press->ClickCount
    };
}

/* Hands keyboard input to a widget, then up through its ancestors (those that take input) until one handles it;
 * @p outHandlerId receives that widget's id (WIDGET_ID_INVALID if none did). With @p isStartPassedOver the first widget does
 * not hear it, and its ancestors do. Each step resolves the widget by id (captured before the previous hook ran). */
static Error DeliverKeyboardInput(UIScreen* self, uint64_t startId, bool isStartPassedOver, const WidgetKeyboardInputArgs* args,
    uint64_t* outHandlerId)
{
    *outHandlerId = WIDGET_ID_INVALID;
    uint64_t CurrentId = startId;
    for (size_t Step = 0; (Step < WIDGET_MAX_TREE_DEPTH) && (CurrentId != WIDGET_ID_INVALID); Step++)
    {
        Widget* Current = UIScreen_InternalResolve(self, CurrentId);
        if (Current == NULL)
        {
            return Error_CreateSuccess();
        }
        uint64_t NextId = (Widget_GetParent(Current) != NULL) ? Widget_GetId(Widget_GetParent(Current)) : WIDGET_ID_INVALID;
        bool IsPassedOver = (Step == 0U) && isStartPassedOver;
        if (!IsPassedOver && UIScreen_InternalTakesPressedInput(Current)) // A hook may have taken it away, and hover stands still during a hidden-cursor drag.
        {
            bool IsHandled = false;
            Error Result = Widget_OnKeyboardInput(Current, args, &IsHandled);
            if (IsHandled)
            {
                *outHandlerId = CurrentId;
            }
            if ((Result.Code != ErrorCode_Success) || IsHandled)
            {
                return Result;
            }
        }
        CurrentId = NextId;
    }
    return Error_CreateSuccess();
}

/* Whether a pressed key is one of the short list a hovered widget hears when nobody with the focus used it. */
static bool IsHoverKey(const GameInput* input, GameKey key)
{
    return UIScreen_InternalIsActionKey(input, GameAction_UIReset, key) || UIScreen_InternalIsActionKey(input, GameAction_UICopy, key)
        || UIScreen_InternalIsActionKey(input, GameAction_UIPaste, key);
}

/* Whether a pressed key is one of the short list a focused edit element that is not entered hears, as a hovered widget does:
 * the hover keys and the context menu. */
static bool IsShortListKey(const GameInput* input, GameKey key)
{
    return IsHoverKey(input, key) || UIScreen_InternalIsActionKey(input, GameAction_UIContextMenu, key);
}

/* The focused widget when it is an edit element that is not entered, or NULL. */
static Widget* GetFocusedEditElementOutside(const UIScreen* self)
{
    Widget* Focused = UIScreen_InternalResolve(self, self->_focusedWidgetId);
    if ((Focused == NULL) || !Focused->_needsEntering || (self->_enteredWidgetId == self->_focusedWidgetId))
    {
        return NULL;
    }
    return Focused;
}

/* Whether keyboard input passes the focused widget by: an edit element that is not entered hears only the presses of the short
 * list, and its ancestors hear the rest. */
static bool IsFocusedPassedOver(const UIScreen* self, const WidgetKeyboardInputArgs* args)
{
    if (GetFocusedEditElementOutside(self) == NULL)
    {
        return false;
    }
    return (args->Type != UIKeyboardInputType_KeyPress) || !IsShortListKey(self->_input, args->Key);
}

/* Hands a repeat or the release of a key to the widget that handled its press, and to no other, while that widget still
 * takes input (alive, attached, input-enabled, shown); otherwise it is dropped. Either way the key stays consumed for
 * everyone else until it is released. */
static Error DeliverKeyToOwner(UIScreen* self, uint64_t ownerId, UIKeyboardInputType type, GameKey key, uint8_t modifiers)
{
    Widget* Owner = UIScreen_InternalResolve(self, ownerId);
    if ((Owner == NULL) || !UIScreen_InternalTakesPressedInput(Owner))
    {
        return Error_CreateSuccess();
    }
    WidgetKeyboardInputArgs Arguments = { .Type = type, .Key = key, .Modifiers = modifiers, .Codepoint = 0U, .IsHoverDelivery = false };
    bool IsHandled = false; // The owner has the key already: its answer changes nothing.
    return Widget_OnKeyboardInput(Owner, &Arguments, &IsHandled);
}


// Static functions: routing.
/* Takes in where the pointer is. During a hidden-cursor drag the screen's pointer stands still, and what the host passes as
 * its position or presence means nothing: only the raw motion counts. */
static PointerMotion TakePointer(UIScreen* self, const UIScreenInput* input)
{
    PointerMotion Motion = { .HasHoverMoved = false, .IsDrag = false, .IsRelative = false, .RelativeDeltaPixels = { .x = 0.0f, .y = 0.0f } };
    if (input->IsPointerRelative && (self->_pointerCaptureWidgetId != WIDGET_ID_INVALID))
    {
        Motion.IsRelative = true;
        Motion.RelativeDeltaPixels = input->PointerDeltaPixels;
        Motion.IsDrag = (input->PointerDeltaPixels.x != 0.0f) || (input->PointerDeltaPixels.y != 0.0f);
        return Motion;
    }
    bool HadPointer = self->_hasPointer;
    Vector2 OldPosition = self->_pointerPosition;
    self->_hasPointer = input->IsPointerPresent;
    self->_isPointerKnown = input->IsPointerPresent || input->IsPointerTracked;
    if (self->_isPointerKnown)
    {
        self->_pointerPosition = input->PointerPosition;
    }
    bool HasPositionMoved = (OldPosition.x != self->_pointerPosition.x) || (OldPosition.y != self->_pointerPosition.y);
    bool HasPresenceChanged = self->_hasPointer != HadPointer;
    Motion.HasHoverMoved = HasPresenceChanged || (self->_hasPointer && HasPositionMoved);
    Motion.IsDrag = self->_isPointerKnown && (HasPresenceChanged || HasPositionMoved);
    return Motion;
}

/* Looks the hovered widget up again when the pointer moved or the tree changed. */
static Error RouteHover(UIScreen* self, bool hasPointerMoved)
{
    if (!hasPointerMoved && !self->_isHitTestDirty)
    {
        return Error_CreateSuccess();
    }
    self->_isHitTestDirty = false;
    Widget* Top = GetWidgetUnderPointer(self);
    uint64_t NewId = (Top != NULL) ? Widget_GetId(Top) : WIDGET_ID_INVALID;
    if (NewId == self->_hoveredWidgetId)
    {
        return Error_CreateSuccess();
    }
    uint64_t OldId = self->_hoveredWidgetId;
    self->_hoveredWidgetId = NewId;
    Widget* Old = UIScreen_InternalResolve(self, OldId);
    if (Old != NULL)
    {
        Error Result = Widget_InternalSetHovered(Old, false, NULL);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    // A hook may have moved the hover meanwhile; that change wins.
    Widget* New = UIScreen_InternalResolve(self, NewId);
    if ((New == NULL) || (self->_hoveredWidgetId != NewId))
    {
        return Error_CreateSuccess();
    }
    WidgetHoverArgs HoverArgs =
    {
        .ScreenPosition = self->_pointerPosition,
        .WidgetPosition = UIScreen_ScreenToWidget(self, New, self->_pointerPosition)
    };
    return Widget_InternalSetHovered(New, true, &HoverArgs);
}

/* The pointer is the UI's while it is over a widget, and while a press holds it, wherever it is. */
static void ClaimPointer(UIScreen* self)
{
    bool IsOverWidget = self->_hoveredWidgetId != WIDGET_ID_INVALID;
    if ((self->_input != NULL) && (IsOverWidget || IsAnyPressHeld(self)))
    {
        GameInput_ConsumePointer(self->_input);
    }
}

/* Counts a press in the row of presses it continues, and keeps it for the next. */
static uint32_t CountClick(UIScreen* self, ProgramTime time, GameKey button, uint64_t widgetId)
{
    const UIScreenClickRecord* Last = &self->_lastClick;
    float MovedX = ((self->_pointerPosition.x - Last->Position.x) * self->_viewport.SizePixels.x) / self->_scale;
    float MovedY = ((self->_pointerPosition.y - Last->Position.y) * self->_viewport.SizePixels.y) / self->_scale;
    bool IsNear = ((MovedX * MovedX) + (MovedY * MovedY)) <= (UI_SCREEN_MULTI_CLICK_DISTANCE * UI_SCREEN_MULTI_CLICK_DISTANCE);
    bool IsSoon = (time.TotalTime - Last->Time) <= UI_SCREEN_MULTI_CLICK_SECONDS;
    bool IsSameTarget = (Last->Button == button) && (Last->WidgetId == widgetId);
    uint32_t Count = (IsSameTarget && IsSoon && IsNear) ? (Last->Count + 1U) : 1U;
    self->_lastClick = (UIScreenClickRecord){ .Count = Count, .Button = button, .WidgetId = widgetId, .Position = self->_pointerPosition,
        .Time = time.TotalTime };
    return Count;
}

/* A press raises its root only within the layers made for that. */
static Error RaiseRootOf(UIScreen* self, Widget* widget)
{
    Widget* Root = widget;
    while (Widget_GetParent(Root) != NULL)
    {
        Root = Widget_GetParent(Root);
    }
    return UIScreen_InternalIsRaisedByInput(Root) ? UIScreen_BringToTop(self, Root) : Error_CreateSuccess(); // Runs no hooks.
}

/* A fresh (unconsumed) press: over a widget it is consumed, focuses the nearest focusable widget under the pointer (entering it
 * if it is an edit element and the screen has the keyboard), raises the widget's root and reaches the widget; over empty space, with
 * no other press held, it only clears focus (the game gets the click). */
static Error PressButton(UIScreen* self, ProgramTime time, GameKey button, size_t index)
{
    // A held press owns the pointer: another button's press over nothing is nobody's, neither a click outside nor the game's.
    if (IsAnyPressHeld(self) && (GetWidgetUnderPointer(self) == NULL))
    {
        GameInput_ConsumeKey(self->_input, button);
        return Error_CreateSuccess();
    }
    if (self->_isNavigating)
    {
        Error StopResult = UIScreen_InternalStopNavigation(self);
        if (StopResult.Code != ErrorCode_Success)
        {
            return StopResult;
        }
    }

    Widget* Target = GetWidgetUnderPointer(self);
    if (Target == NULL)
    {
        self->_lastClick.Count = 0U;
        return UIScreen_InternalSetFocus(self, WIDGET_ID_INVALID);
    }
    GameInput_ConsumeKey(self->_input, button);
    self->_buttons[index] = (UIScreenButtonTracking)
    {
        .IsPressed = true,
        .HasReachedWidget = false,
        .TargetWidgetId = Widget_GetId(Target),
        .StartScreenPosition = self->_pointerPosition,
        .StartTime = time.TotalTime,
        .LastPosition = self->_pointerPosition,
        .ClickCount = CountClick(self, time, button, Widget_GetId(Target))
    };

    Widget* FocusTarget = Target;
    while ((FocusTarget != NULL) && !UIScreen_InternalCanFocus(FocusTarget))
    {
        FocusTarget = Widget_GetParent(FocusTarget);
    }
    uint64_t FocusTargetId = (FocusTarget != NULL) ? Widget_GetId(FocusTarget) : WIDGET_ID_INVALID;
    Error Result = UIScreen_InternalSetFocus(self, FocusTargetId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // An edit element the press focused is entered before the press arrives, unless it enters itself, and only on a screen that has
    // the keyboard (its keys are what entering gives it: a world screen not in use only focuses it); a hook that moved the focus on
    // wins (only the focused widget can be entered).
    Widget* FocusedTarget = UIScreen_InternalResolve(self, FocusTargetId);
    if ((FocusedTarget != NULL) && !FocusedTarget->_entersItself && self->_hasKeyboardFocus)
    {
        Result = UIScreen_InternalEnter(self, FocusTargetId, UIEnterCause_Press);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    // The focus hooks may have destroyed, detached or disabled the widget, which ended the press (it stays consumed), or handed it on.
    Target = UIScreen_InternalResolve(self, self->_buttons[index].TargetWidgetId);
    if (Target == NULL)
    {
        return Error_CreateSuccess();
    }
    Result = RaiseRootOf(self, Target);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_buttons[index].HasReachedWidget = true;
    WidgetMouseInputArgs Arguments = MakeMouseArgs(self, UIMouseInputType_ButtonPress, button, Target, &self->_buttons[index], time,
        (Vector2){ .x = 0.0f, .y = 0.0f });
    return Widget_OnMouseInput(Target, &Arguments);
}

/* Tells the widget holding a press where the pointer went, wherever that is. */
static Error DeliverMove(UIScreen* self, ProgramTime time, GameKey button, size_t index, const PointerMotion* motion)
{
    UIScreenButtonTracking Press = self->_buttons[index];
    Widget* Target = UIScreen_InternalResolve(self, Press.TargetWidgetId);
    if (Target == NULL)
    {
        return Error_CreateSuccess();
    }
    Vector2 Delta = motion->RelativeDeltaPixels;
    if (!motion->IsRelative)
    {
        Delta.x = (self->_pointerPosition.x - Press.LastPosition.x) * self->_viewport.SizePixels.x;
        Delta.y = (self->_pointerPosition.y - Press.LastPosition.y) * self->_viewport.SizePixels.y;
    }
    self->_buttons[index].LastPosition = self->_pointerPosition;
    WidgetMouseInputArgs Move = MakeMouseArgs(self, UIMouseInputType_Move, button, Target, &Press, time, Delta);
    return Widget_OnMouseInput(Target, &Move);
}

/* The press is over: its widget hears of the release, then the top widget under the pointer, if another one. */
static Error DeliverRelease(UIScreen* self, ProgramTime time, GameKey button, size_t index)
{
    UIScreenButtonTracking Press = self->_buttons[index];
    Vector2 NoMotion = { .x = 0.0f, .y = 0.0f };
    UIScreen_InternalEndPress(self, index);
    Widget* Target = UIScreen_InternalResolve(self, Press.TargetWidgetId);
    if (Target != NULL)
    {
        WidgetMouseInputArgs Release = MakeMouseArgs(self, UIMouseInputType_ButtonRelease, button, Target, &Press, time, NoMotion);
        Error Result = Widget_OnMouseInput(Target, &Release);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Widget* Over = GetWidgetUnderPointer(self); // After the hook: what it did to the tree counts.
    if ((Over == NULL) || (Widget_GetId(Over) == Press.TargetWidgetId))
    {
        return Error_CreateSuccess();
    }
    WidgetMouseInputArgs ReleaseOver = MakeMouseArgs(self, UIMouseInputType_ReleaseOver, button, Over, &Press, time, NoMotion);
    return Widget_OnMouseInput(Over, &ReleaseOver);
}

/* A press this screen took: its drags and its release go to the widget that holds it (followed with the raw state, since the
 * screen itself consumed the press). A pointer that moved in the release's tick arrives as a move first: the end of a drag is
 * where the pointer ended. */
static Error FollowPressedButton(UIScreen* self, ProgramTime time, GameKey button, size_t index, const PointerMotion* motion)
{
    uint8_t Raw = GameInput_GetRawKeyState(self->_input, button);
    bool IsReleased = ((Raw & GAME_INPUT_KEY_STATE_RELEASED) != 0) || ((Raw & GAME_INPUT_KEY_STATE_DOWN) == 0);
    if (motion->IsDrag)
    {
        Error Result = DeliverMove(self, time, button, index, motion);
        if ((Result.Code != ErrorCode_Success) || !self->_buttons[index].IsPressed)
        {
            return Result; // The hook may have taken the press away.
        }
    }
    return IsReleased ? DeliverRelease(self, time, button, index) : Error_CreateSuccess();
}

static Error RouteButtons(UIScreen* self, ProgramTime time, const PointerMotion* motion)
{
    for (size_t Index = 0; Index < UI_SCREEN_MOUSE_BUTTON_COUNT; Index++)
    {
        GameKey Button = GetMouseButtonAt(Index);
        Error Result = Error_CreateSuccess();
        if (self->_buttons[Index].IsPressed)
        {
            Result = FollowPressedButton(self, time, Button, Index, motion);
        }
        else if (GameInput_IsKeyPressed(self->_input, Button))
        {
            Result = PressButton(self, time, Button, Index);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* Wheel movement over a widget is consumed from the game, and goes to that widget, then up through its ancestors that take input,
 * until one handles it. */
static Error RouteScroll(UIScreen* self)
{
    Vector2 Wheel = GameInput_GetWheelDelta(self->_input);
    if ((Wheel.x == 0.0f) && (Wheel.y == 0.0f))
    {
        return Error_CreateSuccess();
    }
    Widget* Top = GetWidgetUnderPointer(self);
    if (Top == NULL)
    {
        return Error_CreateSuccess();
    }
    GameInput_ConsumeWheel(self->_input);
    uint8_t Modifiers = GameInput_GetHeldModifiers(self->_input);
    uint64_t CurrentId = Widget_GetId(Top);
    for (size_t Step = 0; (Step < WIDGET_MAX_TREE_DEPTH) && (CurrentId != WIDGET_ID_INVALID); Step++)
    {
        Widget* Current = UIScreen_InternalResolve(self, CurrentId);
        if (Current == NULL)
        {
            return Error_CreateSuccess();
        }
        uint64_t NextId = (Widget_GetParent(Current) != NULL) ? Widget_GetId(Widget_GetParent(Current)) : WIDGET_ID_INVALID;
        if (UIScreen_InternalTakesPressedInput(Current)) // A hook may have taken it away.
        {
            WidgetScrollInputArgs Arguments =
            {
                .ScrollDelta = Wheel,
                .Modifiers = Modifiers,
                .ScreenPosition = self->_pointerPosition,
                .WidgetPosition = UIScreen_ScreenToWidget(self, Current, self->_pointerPosition)
            };
            bool IsHandled = false;
            Error Result = Widget_OnScrollInput(Current, &Arguments, &IsHandled);
            if ((Result.Code != ErrorCode_Success) || IsHandled)
            {
                return Result;
            }
        }
        CurrentId = NextId;
    }
    return Error_CreateSuccess();
}

/* Routes one keyboard key. Unconsumed key input goes to the focused widget and its ancestors, and a press of the reset, copy
 * or paste action they leave goes on to the hovered widget and its ancestors. The widget that handles a held key consumes it
 * and owns it until its release: the repeats and the release go to that widget alone, even after focus moved (followed with the
 * raw state, since the key reads as consumed for everyone else). */
static Error RouteKey(UIScreen* self, GameKey key, uint8_t modifiers)
{
    uint8_t Raw = GameInput_GetRawKeyState(self->_input, key);
    bool IsNewPress = (Raw & GAME_INPUT_KEY_STATE_PRESSED) != 0;
    uint64_t OwnerId = self->_keyOwnerIds[key];
    if (OwnerId != WIDGET_ID_INVALID)
    {
        // A new press (released and pressed again within one tick) ends the ownership like a release.
        bool IsOwnershipOver = IsNewPress || ((Raw & GAME_INPUT_KEY_STATE_DOWN) == 0);
        Error Result = Error_CreateSuccess();
        if (IsOwnershipOver)
        {
            self->_keyOwnerIds[key] = WIDGET_ID_INVALID;
            if ((Raw & GAME_INPUT_KEY_STATE_RELEASED) != 0)
            {
                Result = DeliverKeyToOwner(self, OwnerId, UIKeyboardInputType_KeyRelease, key, modifiers);
            }
        }
        else if ((Raw & GAME_INPUT_KEY_STATE_REPEATED) != 0)
        {
            Result = DeliverKeyToOwner(self, OwnerId, UIKeyboardInputType_KeyRepeat, key, modifiers);
        }
        if ((Result.Code != ErrorCode_Success) || !IsNewPress)
        {
            return Result;
        }
    }

    uint8_t State = GameInput_GetKeyState(self->_input, key);
    UIKeyboardInputType Type = UIKeyboardInputType_KeyPress;
    if ((State & GAME_INPUT_KEY_STATE_PRESSED) != 0)
    {
        Type = UIKeyboardInputType_KeyPress;
    }
    else if ((State & GAME_INPUT_KEY_STATE_REPEATED) != 0)
    {
        Type = UIKeyboardInputType_KeyRepeat;
    }
    else if ((State & GAME_INPUT_KEY_STATE_RELEASED) != 0)
    {
        Type = UIKeyboardInputType_KeyRelease;
    }
    else
    {
        return Error_CreateSuccess();
    }

    // The accept action's press enters a focused edit element that is not entered, at its place in the chain: before its
    // ancestors, so a dialog around it cannot take that press. Consumed, it reaches nobody while it is held.
    Widget* Outside = GetFocusedEditElementOutside(self);
    if ((Outside != NULL) && (Type == UIKeyboardInputType_KeyPress) && UIScreen_InternalTakesPressedInput(Outside)
        && UIScreen_InternalIsActionKey(self->_input, GameAction_UIAccept, key))
    {
        GameInput_ConsumeKey(self->_input, key);
        return UIScreen_InternalEnter(self, Widget_GetId(Outside), UIEnterCause_Key);
    }

    WidgetKeyboardInputArgs Arguments = { .Type = Type, .Key = key, .Modifiers = modifiers, .Codepoint = 0U, .IsHoverDelivery = false };
    uint64_t HandlerId = WIDGET_ID_INVALID;
    Error Result = DeliverKeyboardInput(self, self->_focusedWidgetId, IsFocusedPassedOver(self, &Arguments), &Arguments, &HandlerId);
    if ((Result.Code == ErrorCode_Success) && (HandlerId == WIDGET_ID_INVALID) && (Type == UIKeyboardInputType_KeyPress) && IsHoverKey(self->_input, key))
    {
        Arguments.IsHoverDelivery = true;
        Result = DeliverKeyboardInput(self, self->_hoveredWidgetId, false, &Arguments, &HandlerId);
    }
    if (HandlerId != WIDGET_ID_INVALID)
    {
        GameInput_ConsumeKey(self->_input, key);
        if ((Raw & GAME_INPUT_KEY_STATE_DOWN) != 0)
        {
            self->_keyOwnerIds[key] = HandlerId;
        }
    }
    return Result;
}

static Error RouteKeys(UIScreen* self)
{
    uint8_t Modifiers = GameInput_GetHeldModifiers(self->_input);
    for (int32_t KeyIndex = (int32_t)GameKey_Unknown + 1; KeyIndex < (int32_t)GameKey_Count; KeyIndex++)
    {
        if (!GameKey_IsKeyboardKey((GameKey)KeyIndex))
        {
            continue;
        }
        Error Result = RouteKey(self, (GameKey)KeyIndex, Modifiers);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* Typed text goes to the focused widget and its ancestors (past an edit element that is not entered); if any codepoint is
 * handled, the tick's text is consumed. */
static Error RouteText(UIScreen* self)
{
    size_t Count = GameInput_GetTextCodepointCount(self->_input);
    if ((Count == 0U) || (UIScreen_InternalResolve(self, self->_focusedWidgetId) == NULL))
    {
        return Error_CreateSuccess();
    }
    // Copied first: a handler could consume the text through the input while it is being delivered.
    uint32_t Codepoints[GAME_INPUT_MAX_TEXT_CODEPOINTS];
    for (size_t Index = 0; Index < Count; Index++)
    {
        Codepoints[Index] = GameInput_GetTextCodepoint(self->_input, Index);
    }

    bool IsAnyHandled = false;
    uint8_t Modifiers = GameInput_GetHeldModifiers(self->_input);
    for (size_t Index = 0; Index < Count; Index++)
    {
        WidgetKeyboardInputArgs Arguments =
        {
            .Type = UIKeyboardInputType_Text,
            .Key = GameKey_Unknown,
            .Modifiers = Modifiers,
            .Codepoint = Codepoints[Index],
            .IsHoverDelivery = false
        };
        uint64_t HandlerId = WIDGET_ID_INVALID;
        Error Result = DeliverKeyboardInput(self, self->_focusedWidgetId, IsFocusedPassedOver(self, &Arguments), &Arguments, &HandlerId);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        IsAnyHandled = IsAnyHandled || (HandlerId != WIDGET_ID_INVALID);
    }
    if (IsAnyHandled)
    {
        GameInput_ConsumeText(self->_input);
    }
    return Error_CreateSuccess();
}


// Internal functions (declared in UIInternal.h).
Error UIScreen_InternalRouteInput(UIScreen* self, ProgramTime time, const UIScreenInput* input)
{
    self->_input = input->Input;
    self->_hasKeyboardFocus = input->HasKeyboardFocus && (input->Input != NULL);
    PointerMotion Motion = TakePointer(self, input);

    // Pointer movement ends keyboard navigation (focus stays).
    Error Result = Error_CreateSuccess();
    if (Motion.HasHoverMoved && self->_hasPointer && self->_isNavigating)
    {
        Result = UIScreen_InternalStopNavigation(self);
    }
    if ((Result.Code == ErrorCode_Success) && !Motion.IsRelative)
    {
        Result = RouteHover(self, Motion.HasHoverMoved);
    }
    if (Result.Code == ErrorCode_Success)
    {
        ClaimPointer(self);
    }
    if ((Result.Code == ErrorCode_Success) && (self->_input != NULL))
    {
        Result = RouteButtons(self, time, &Motion);
    }
    if ((Result.Code == ErrorCode_Success) && (self->_input != NULL))
    {
        Result = RouteScroll(self);
    }
    if ((Result.Code == ErrorCode_Success) && self->_hasKeyboardFocus)
    {
        Result = RouteKeys(self);
    }
    if ((Result.Code == ErrorCode_Success) && self->_hasKeyboardFocus)
    {
        Result = RouteText(self);
    }
    if ((Result.Code == ErrorCode_Success) && self->_hasKeyboardFocus)
    {
        Result = UIScreen_InternalRouteNavigation(self);
    }
    return Result;
}

bool UIScreen_InternalCanFocus(const Widget* widget)
{
    return (widget != NULL) && !widget->_isDying && widget->_isInitialized && widget->_isFocusable
        && widget->_isInputEnabled && UIScreen_InternalIsAttached(widget) && UIScreen_InternalIsShown(widget)
        && !UIScreen_InternalIsInOverlay(widget);
}

bool UIScreen_InternalCanEnter(const Widget* widget)
{
    return UIScreen_InternalCanFocus(widget) && widget->_needsEntering;
}

Error UIScreen_InternalEnter(UIScreen* self, uint64_t widgetId, UIEnterCause cause)
{
    Widget* Target = UIScreen_InternalResolve(self, widgetId);
    if ((widgetId != self->_focusedWidgetId) || !UIScreen_InternalCanEnter(Target))
    {
        return Error_CreateSuccess();
    }
    self->_enteredWidgetId = widgetId;
    return Widget_InternalEnter(Target, cause); // Nothing for the widget that is entered already.
}

Error UIScreen_InternalLeave(UIScreen* self, UILeaveReason reason)
{
    uint64_t LeftId = self->_enteredWidgetId;
    if (LeftId == WIDGET_ID_INVALID)
    {
        return Error_CreateSuccess();
    }
    self->_enteredWidgetId = WIDGET_ID_INVALID;
    Widget* Left = UIScreen_InternalResolve(self, LeftId);
    return (Left != NULL) ? Widget_InternalLeave(Left, reason) : Error_CreateSuccess();
}

bool UIScreen_InternalIsRaisedByInput(const Widget* widget)
{
    UIScreenLayer Layer = Widget_InternalGetDrawLayer(widget);
    return (Layer == UIScreenLayer_Normal) || (Layer == UIScreenLayer_Floating);
}

bool UIScreen_InternalTakesPressedInput(const Widget* widget)
{
    return Widget_IsInputEnabled(widget) && UIScreen_InternalIsAttached(widget) && UIScreen_InternalIsShown(widget)
        && !UIScreen_InternalIsInOverlay(widget);
}

void UIScreen_InternalEndPress(UIScreen* self, size_t buttonIndex)
{
    uint64_t HolderId = self->_buttons[buttonIndex].TargetWidgetId;
    self->_buttons[buttonIndex].IsPressed = false;
    self->_buttons[buttonIndex].HasReachedWidget = false;
    self->_buttons[buttonIndex].TargetWidgetId = WIDGET_ID_INVALID;
    if ((HolderId == self->_pointerCaptureWidgetId) && !IsHoldingPress(self, HolderId))
    {
        self->_pointerCaptureWidgetId = WIDGET_ID_INVALID;
    }
}

Error UIScreen_InternalReleaseUnreachable(UIScreen* self)
{
    Error FirstError = Error_CreateSuccess();
    Widget* Focused = UIScreen_InternalResolve(self, self->_focusedWidgetId);
    if ((Focused != NULL) && !UIScreen_InternalCanFocus(Focused))
    {
        Error Result = UIScreen_InternalSetFocus(self, WIDGET_ID_INVALID);
        KeepFirstError(&FirstError, &Result);
    }
    for (size_t Index = 0; Index < UI_SCREEN_MOUSE_BUTTON_COUNT; Index++)
    {
        Widget* Target = UIScreen_InternalResolve(self, self->_buttons[Index].TargetWidgetId);
        if ((Target == NULL) || UIScreen_InternalTakesPressedInput(Target))
        {
            continue;
        }
        bool HadReachedIt = self->_buttons[Index].HasReachedWidget;
        UIScreen_InternalEndPress(self, Index);
        if (HadReachedIt)
        {
            Error Result = Widget_OnPressLost(Target, GetMouseButtonAt(Index));
            KeepFirstError(&FirstError, &Result);
        }
    }
    return FirstError;
}

bool UIScreen_InternalIsActionKey(const GameInput* input, GameActionId action, GameKey key)
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

Error UIScreen_InternalSetFocus(UIScreen* self, uint64_t widgetId)
{
    if (widgetId == self->_focusedWidgetId)
    {
        return Error_CreateSuccess();
    }
    uint64_t OldId = self->_focusedWidgetId;
    self->_focusedWidgetId = widgetId;

    // The entered element (the old focus) is left first, keeping what was done; a leave hook that focused the old widget again
    // keeps it focused.
    Error Result = UIScreen_InternalLeave(self, UILeaveReason_Keep);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Widget* Old = UIScreen_InternalResolve(self, OldId);
    if ((Old != NULL) && (self->_focusedWidgetId != OldId))
    {
        Result = Widget_InternalSetFocused(Old, false);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    // A hook moved focus again: that later change already ran and wins, so only one widget ends up focused.
    if (self->_focusedWidgetId != widgetId)
    {
        return Error_CreateSuccess();
    }
    Widget* New = UIScreen_InternalResolve(self, widgetId);
    if (!UIScreen_InternalCanFocus(New))
    {
        self->_focusedWidgetId = WIDGET_ID_INVALID; // Cleared on purpose, or destroyed, detached or disabled by a hook.
        return Error_CreateSuccess();
    }
    return Widget_InternalSetFocused(New, true);
}


// Public functions.
UIScreenInput UIScreenInput_CreateFromWindow(GameInput* input)
{
    UIScreenInput Result = UIScreenInput_CreateEmpty();
    if (input == NULL)
    {
        return Result;
    }
    Result.Input = input;
    Result.HasKeyboardFocus = true;

    // Nothing is known of the pointer while the cursor drives mouse look. Elsewhere its position is what the device says, past the
    // window's edges too; it is over this screen only inside the window, with the cursor on it, and when no UI above claimed it.
    Vector2 WindowSize = GameInput_GetWindowSize(input);
    if (GameInput_IsCursorCaptured(input) || !(WindowSize.x > 0.0f) || !(WindowSize.y > 0.0f))
    {
        return Result;
    }
    Vector2 Mouse = GameInput_GetMousePosition(input);
    Result.IsPointerTracked = true;
    Result.PointerPosition = (Vector2){ .x = Mouse.x / WindowSize.x, .y = Mouse.y / WindowSize.y };
    Result.IsPointerPresent = GameInput_IsCursorOnWindow(input) && !GameInput_IsPointerConsumed(input) && IsPointInUnitSquare(Result.PointerPosition);
    return Result;
}

bool UIScreen_HasHeldPress(const UIScreen* self)
{
    return IsAnyPressHeld(self);
}

Error UIScreen_TransferPress(UIScreen* self, GameKey button, uint64_t newWidgetId)
{
    if ((self == NULL) || !GameKey_IsMouseButton(button))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_TransferPress: self must not be NULL and the button must be a mouse button.");
    }
    size_t Index = (size_t)((int32_t)button - (int32_t)GameKey_MouseLeft);
    Widget* New = UIScreen_InternalResolve(self, newWidgetId);
    if (!self->_buttons[Index].IsPressed || (New == NULL) || !UIScreen_InternalTakesPressedInput(New))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIScreen_TransferPress: the button must hold a press, and the widget must be alive, attached, taking input and shown.");
    }
    uint64_t OldId = self->_buttons[Index].TargetWidgetId;
    if (OldId == newWidgetId)
    {
        return Error_CreateSuccess();
    }
    bool HadReachedIt = self->_buttons[Index].HasReachedWidget;
    self->_buttons[Index].TargetWidgetId = newWidgetId;
    if ((OldId == self->_pointerCaptureWidgetId) && !IsHoldingPress(self, OldId))
    {
        self->_pointerCaptureWidgetId = WIDGET_ID_INVALID;
    }
    Widget* Old = UIScreen_InternalResolve(self, OldId);
    return ((Old != NULL) && HadReachedIt) ? Widget_OnPressLost(Old, button) : Error_CreateSuccess();
}

Error UIScreen_CapturePointer(UIScreen* self, uint64_t widgetId)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_CapturePointer: self must not be NULL.");
    }
    bool IsHeldByAnother = (self->_pointerCaptureWidgetId != WIDGET_ID_INVALID) && (self->_pointerCaptureWidgetId != widgetId);
    if ((UIScreen_InternalResolve(self, widgetId) == NULL) || !IsHoldingPress(self, widgetId) || IsHeldByAnother)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIScreen_CapturePointer: the widget must be alive and hold a press, and no other widget may have the capture.");
    }
    self->_pointerCaptureWidgetId = widgetId;
    return Error_CreateSuccess();
}

void UIScreen_ReleasePointerCapture(UIScreen* self)
{
    self->_pointerCaptureWidgetId = WIDGET_ID_INVALID;
}

UICursorShape UIScreen_GetCursorShape(const UIScreen* self)
{
    for (size_t Index = 0; Index < UI_SCREEN_MOUSE_BUTTON_COUNT; Index++)
    {
        const Widget* Holder = UIScreen_InternalResolve(self, self->_buttons[Index].TargetWidgetId);
        if (self->_buttons[Index].IsPressed && (Holder != NULL))
        {
            return Widget_GetCursorShape(Holder);
        }
    }
    const Widget* Hovered = UIScreen_InternalResolve(self, self->_hoveredWidgetId);
    return (Hovered != NULL) ? Widget_GetCursorShape(Hovered) : UICursorShape_Arrow;
}

Error UIScreen_SetFocusedWidget(UIScreen* self, Widget* widget)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_SetFocusedWidget: self must not be NULL.");
    }
    if (widget == NULL)
    {
        return UIScreen_InternalSetFocus(self, WIDGET_ID_INVALID);
    }
    if (Widget_GetScreen(widget) != self)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_SetFocusedWidget: the widget belongs to another screen.");
    }
    if (!UIScreen_InternalCanFocus(widget))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIScreen_SetFocusedWidget: the widget must be alive, attached, focusable and input-enabled.");
    }
    return UIScreen_InternalSetFocus(self, Widget_GetId(widget));
}

Error UIScreen_EnterWidget(UIScreen* self, uint64_t widgetId)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_EnterWidget: self must not be NULL.");
    }
    if (!UIScreen_InternalCanEnter(UIScreen_InternalResolve(self, widgetId)))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIScreen_EnterWidget: the widget must be an edit element that is alive, attached, focusable, input-enabled and shown.");
    }
    Error Result = UIScreen_InternalSetFocus(self, widgetId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIScreen_InternalEnter(self, widgetId, UIEnterCause_Code); // Nothing if it is entered already, or a hook moved the focus on.
}

Error UIScreen_LeaveWidget(UIScreen* self, UILeaveReason reason)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_LeaveWidget: self must not be NULL.");
    }
    if ((reason != UILeaveReason_Keep) && (reason != UILeaveReason_Cancel))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIScreen_LeaveWidget: an unknown reason.");
    }
    return UIScreen_InternalLeave(self, reason);
}
