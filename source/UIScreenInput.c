#include "UIScreen.h"
#include "UIInternal.h"
#include "UIWidget.h"
#include "GameInput.h"
#include "GameKey.h"
#include "wr/WRMemory.h"


// Static functions: helpers.
static GameKey GetMouseButtonAt(size_t index)
{
    return (GameKey)((int32_t)GameKey_MouseLeft + (int32_t)index);
}

static bool IsPointInUnitSquare(Vector2 point)
{
    return (point.x >= 0.0f) && (point.x <= 1.0f) && (point.y >= 0.0f) && (point.y <= 1.0f);
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

static WidgetMouseInputArgs MakeMouseArgs(const UIScreen* self, UIMouseInputType type, GameKey button, const Widget* widget,
    Vector2 startScreen, double duration, Vector2 scrollDelta)
{
    return (WidgetMouseInputArgs)
    {
        .Type = type,
        .Button = button,
        .ScreenPosition = self->_pointerPosition,
        .WidgetPosition = UIScreen_ScreenToWidget(self, widget, self->_pointerPosition),
        .ClickStartScreenPosition = startScreen,
        .ClickStartWidgetPosition = UIScreen_ScreenToWidget(self, widget, startScreen),
        .DurationSeconds = duration,
        .ScrollDelta = scrollDelta
    };
}

/* Hands keyboard input to the focused widget, then up through its ancestors (input-enabled ones only) until one
 * handles it; @p outHandlerId receives that widget's id (WIDGET_ID_INVALID if none did). Each step resolves the widget by
 * id (captured before the previous hook ran). */
static Error DeliverKeyboardInput(UIScreen* self, const WidgetKeyboardInputArgs* args, uint64_t* outHandlerId)
{
    *outHandlerId = WIDGET_ID_INVALID;
    uint64_t CurrentId = self->_focusedWidgetId;
    for (size_t Step = 0; (Step < WIDGET_MAX_TREE_DEPTH) && (CurrentId != WIDGET_ID_INVALID); Step++)
    {
        Widget* Current = UIScreen_InternalResolve(self, CurrentId);
        if (Current == NULL)
        {
            return Error_CreateSuccess();
        }
        uint64_t NextId = (Widget_GetParent(Current) != NULL) ? Widget_GetId(Widget_GetParent(Current)) : WIDGET_ID_INVALID;
        if (Widget_IsInputEnabled(Current))
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

static Error DeliverKey(UIScreen* self, UIKeyboardInputType type, GameKey key, uint8_t modifiers, uint64_t* outHandlerId)
{
    WidgetKeyboardInputArgs Arguments = { .Type = type, .Key = key, .Modifiers = modifiers, .Codepoint = 0U };
    return DeliverKeyboardInput(self, &Arguments, outHandlerId);
}

/* Hands a repeat or the release of a key to the widget that handled its press, and to no other, while that widget still
 * takes input (alive, attached, input-enabled); otherwise it is dropped. Either way the key stays consumed for everyone
 * else until it is released. */
static Error DeliverKeyToOwner(UIScreen* self, uint64_t ownerId, UIKeyboardInputType type, GameKey key, uint8_t modifiers)
{
    Widget* Owner = UIScreen_InternalResolve(self, ownerId);
    if ((Owner == NULL) || !Widget_IsInputEnabled(Owner) || !UIScreen_InternalIsAttached(Owner))
    {
        return Error_CreateSuccess();
    }
    WidgetKeyboardInputArgs Arguments = { .Type = type, .Key = key, .Modifiers = modifiers, .Codepoint = 0U };
    bool IsHandled = false; // The owner has the key already: its answer changes nothing.
    return Widget_OnKeyboardInput(Owner, &Arguments, &IsHandled);
}


// Static functions: routing.
/* Looks the hovered widget up again when the pointer moved or the tree changed, and claims the pointer while it is over
 * a widget. */
static Error RouteHover(UIScreen* self, bool hasPointerMoved)
{
    if (hasPointerMoved || self->_isHitTestDirty)
    {
        self->_isHitTestDirty = false;
        Widget* Top = GetWidgetUnderPointer(self);
        uint64_t NewId = (Top != NULL) ? Widget_GetId(Top) : WIDGET_ID_INVALID;
        if (NewId != self->_hoveredWidgetId)
        {
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
            if ((New != NULL) && (self->_hoveredWidgetId == NewId))
            {
                WidgetHoverArgs HoverArgs =
                {
                    .ScreenPosition = self->_pointerPosition,
                    .WidgetPosition = UIScreen_ScreenToWidget(self, New, self->_pointerPosition)
                };
                Error Result = Widget_InternalSetHovered(New, true, &HoverArgs);
                if (Result.Code != ErrorCode_Success)
                {
                    return Result;
                }
            }
        }
    }
    if ((self->_input != NULL) && self->_hasPointer && (self->_hoveredWidgetId != WIDGET_ID_INVALID))
    {
        GameInput_ConsumePointer(self->_input);
    }
    return Error_CreateSuccess();
}

/* A fresh (unconsumed) press: over a widget it is consumed, focuses the nearest focusable widget under the pointer,
 * raises the widget's root and reaches the widget; over empty space it only clears focus (the game gets the click). */
static Error PressButton(UIScreen* self, ProgramTime time, GameKey button, size_t index)
{
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
        return UIScreen_InternalSetFocus(self, WIDGET_ID_INVALID);
    }
    uint64_t TargetId = Widget_GetId(Target);
    GameInput_ConsumeKey(self->_input, button);
    self->_buttons[index] = (UIScreenButtonTracking)
    {
        .IsPressed = true,
        .TargetWidgetId = TargetId,
        .StartScreenPosition = self->_pointerPosition,
        .StartTime = time.TotalTime
    };

    Widget* FocusTarget = Target;
    while ((FocusTarget != NULL) && !UIScreen_InternalCanFocus(FocusTarget))
    {
        FocusTarget = Widget_GetParent(FocusTarget);
    }
    Error Result = UIScreen_InternalSetFocus(self, (FocusTarget != NULL) ? Widget_GetId(FocusTarget) : WIDGET_ID_INVALID);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // The focus hooks may have destroyed, detached or disabled the widget: the press only reaches it while it still
    // takes input (it stays consumed either way).
    Target = UIScreen_InternalResolve(self, TargetId);
    if ((Target == NULL) || !Widget_IsInputEnabled(Target) || !UIScreen_InternalIsAttached(Target))
    {
        self->_buttons[index].IsPressed = false;
        self->_buttons[index].TargetWidgetId = WIDGET_ID_INVALID;
        return Error_CreateSuccess();
    }
    Widget* Root = Target;
    while (Widget_GetParent(Root) != NULL)
    {
        Root = Widget_GetParent(Root);
    }
    Result = UIScreen_BringToTop(self, Root); // Runs no hooks.
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    WidgetMouseInputArgs Arguments = MakeMouseArgs(self, UIMouseInputType_ButtonPress, button, Target,
        self->_pointerPosition, 0.0, (Vector2){ .x = 0.0f, .y = 0.0f });
    return Widget_OnMouseInput(Target, &Arguments);
}

/* A press this screen took: its release and drags go to the widget it started on (followed with the raw state, since
 * the screen itself consumed the press). */
static Error FollowPressedButton(UIScreen* self, ProgramTime time, GameKey button, size_t index, bool hasPointerMoved)
{
    UIScreenButtonTracking Tracking = self->_buttons[index];
    uint8_t Raw = GameInput_GetRawKeyState(self->_input, button);
    bool IsReleased = ((Raw & GAME_INPUT_KEY_STATE_RELEASED) != 0) || ((Raw & GAME_INPUT_KEY_STATE_DOWN) == 0);
    if (IsReleased)
    {
        self->_buttons[index].IsPressed = false;
        self->_buttons[index].TargetWidgetId = WIDGET_ID_INVALID;
    }
    else if (!hasPointerMoved || !self->_hasPointer)
    {
        return Error_CreateSuccess();
    }

    Widget* Target = UIScreen_InternalResolve(self, Tracking.TargetWidgetId);
    if (Target == NULL)
    {
        return Error_CreateSuccess();
    }
    WidgetMouseInputArgs Arguments = MakeMouseArgs(self, IsReleased ? UIMouseInputType_ButtonRelease : UIMouseInputType_Move,
        button, Target, Tracking.StartScreenPosition, time.TotalTime - Tracking.StartTime, (Vector2){ .x = 0.0f, .y = 0.0f });
    return Widget_OnMouseInput(Target, &Arguments);
}

static Error RouteButtons(UIScreen* self, ProgramTime time, bool hasPointerMoved)
{
    for (size_t Index = 0; Index < UI_SCREEN_MOUSE_BUTTON_COUNT; Index++)
    {
        GameKey Button = GetMouseButtonAt(Index);
        Error Result = Error_CreateSuccess();
        if (self->_buttons[Index].IsPressed)
        {
            Result = FollowPressedButton(self, time, Button, Index, hasPointerMoved);
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

/* Wheel movement over a widget is consumed and reaches that widget. */
static Error RouteScroll(UIScreen* self)
{
    Vector2 Wheel = GameInput_GetWheelDelta(self->_input);
    if ((Wheel.x == 0.0f) && (Wheel.y == 0.0f))
    {
        return Error_CreateSuccess();
    }
    Widget* Target = GetWidgetUnderPointer(self);
    if (Target == NULL)
    {
        return Error_CreateSuccess();
    }
    GameInput_ConsumeWheel(self->_input);
    WidgetMouseInputArgs Arguments = MakeMouseArgs(self, UIMouseInputType_Scroll, GameKey_Unknown, Target,
        self->_pointerPosition, 0.0, Wheel);
    return Widget_OnMouseInput(Target, &Arguments);
}

/* Routes one keyboard key. Unconsumed key input goes to the focused widget and its ancestors; the widget that handles
 * a held key consumes it and owns it until its release: the repeats and the release go to that widget alone, even after
 * focus moved (followed with the raw state, since the key reads as consumed for everyone else). */
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

    uint64_t HandlerId = WIDGET_ID_INVALID;
    Error Result = DeliverKey(self, Type, key, modifiers, &HandlerId);
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

/* Typed text goes to the focused widget and its ancestors; if any codepoint is handled, the tick's text is consumed. */
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
            .Codepoint = Codepoints[Index]
        };
        uint64_t HandlerId = WIDGET_ID_INVALID;
        Error Result = DeliverKeyboardInput(self, &Arguments, &HandlerId);
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
    bool HadPointer = self->_hasPointer;
    Vector2 OldPosition = self->_pointerPosition;
    self->_hasPointer = input->IsPointerPresent;
    if (self->_hasPointer)
    {
        self->_pointerPosition = input->PointerPosition;
    }
    self->_hasKeyboardFocus = input->HasKeyboardFocus && (input->Input != NULL);
    bool HasPointerMoved = (self->_hasPointer != HadPointer) || (self->_hasPointer
        && ((OldPosition.x != self->_pointerPosition.x) || (OldPosition.y != self->_pointerPosition.y)));

    // Pointer movement ends keyboard navigation (focus stays).
    Error Result = Error_CreateSuccess();
    if (HasPointerMoved && self->_hasPointer && self->_isNavigating)
    {
        Result = UIScreen_InternalStopNavigation(self);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = RouteHover(self, HasPointerMoved);
    }
    if ((Result.Code == ErrorCode_Success) && (self->_input != NULL))
    {
        Result = RouteButtons(self, time, HasPointerMoved);
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
        && widget->_isInputEnabled && UIScreen_InternalIsAttached(widget);
}

Error UIScreen_InternalSetFocus(UIScreen* self, uint64_t widgetId)
{
    if (widgetId == self->_focusedWidgetId)
    {
        return Error_CreateSuccess();
    }
    uint64_t OldId = self->_focusedWidgetId;
    self->_focusedWidgetId = widgetId;

    Widget* Old = UIScreen_InternalResolve(self, OldId);
    if (Old != NULL)
    {
        Error Result = Widget_InternalSetFocused(Old, false);
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

    // No pointer while the cursor drives mouse look, or when UI above (a higher frame) has claimed it.
    Vector2 WindowSize = GameInput_GetWindowSize(input);
    if (GameInput_IsCursorCaptured(input) || GameInput_IsPointerConsumed(input)
        || !(WindowSize.x > 0.0f) || !(WindowSize.y > 0.0f))
    {
        return Result;
    }
    Vector2 Mouse = GameInput_GetMousePosition(input);
    Vector2 Position = { .x = Mouse.x / WindowSize.x, .y = Mouse.y / WindowSize.y };
    if (IsPointInUnitSquare(Position))
    {
        Result.IsPointerPresent = true;
        Result.PointerPosition = Position;
    }
    return Result;
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
