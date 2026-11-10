#include <math.h>
#include "UIRenderView.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIScreen.h"
#include "wr/WRMemory.h"


// Static functions: helpers.
static UIRenderViewCapability* GetCapability(void* widget)
{
    return &((UIRenderViewLookBase*)widget)->View;
}

static Error ResolveView(const UIScreen* screen, uint64_t viewId, Widget** outWidget, UIRenderViewCapability** outView)
{
    UIKit* Kit = UIKit_FromScreen(screen);
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, viewId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIRenderView: the widget is gone, or its screen has no kit.");
    }
    UIRenderViewCapability* View = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_RenderView));
    if (View == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIRenderView: the widget is not a render view.");
    }
    *outWidget = Target;
    *outView = View;
    return Error_CreateSuccess();
}

/* The record's slot of a mouse button; NULL for a key that is no mouse button. */
static UIRenderViewButton* GetButton(UIRenderViewCapability* self, GameKey button)
{
    if (!GameKey_IsMouseButton(button))
    {
        return NULL;
    }
    size_t Index = (size_t)((int32_t)button - (int32_t)GameKey_MouseLeft);
    return (Index < UI_RENDER_VIEW_BUTTON_COUNT) ? &self->_buttons[Index] : NULL;
}

/* A box's length in whole surface pixels: its far edge's pixel less its near edge's, never below 0. */
static int32_t GetWholeLength(float start, float length)
{
    if (!isfinite(start) || !isfinite(length) || !(length > 0.0f))
    {
        return 0;
    }
    long Length = lroundf(start + length) - lroundf(start);
    return (Length > 0L) ? (int32_t)Length : 0;
}

static Int32Vector GetWholeSize(const UIScreen* screen, const Widget* widget)
{
    Rectangle Box = UIScreen_GetWidgetPixelBox(screen, widget);
    return (Int32Vector){ .X = GetWholeLength(Box.x, Box.width), .Y = GetWholeLength(Box.y, Box.height) };
}

/* Shown: it and every ancestor shown, its top a root of the screen, and room to draw in. The tree is at most WIDGET_MAX_TREE_DEPTH deep. */
static bool IsWidgetShown(const UIScreen* screen, const Widget* widget)
{
    const Widget* Top = widget;
    for (const Widget* Current = widget; Current != NULL; Current = Widget_GetParent(Current))
    {
        if (!Widget_IsRendered(Current))
        {
            return false;
        }
        Top = Current;
    }
    if (!Widget_IsScreenRoot(Top))
    {
        return false;
    }
    Int32Vector Size = GetWholeSize(screen, widget);
    return (Size.X > 0) && (Size.Y > 0);
}

static bool IsAnyButtonDown(const UIRenderViewCapability* self)
{
    for (size_t Index = 0U; Index < UI_RENDER_VIEW_BUTTON_COUNT; Index++)
    {
        if (self->_buttons[Index].IsDown)
        {
            return true;
        }
    }
    return false;
}

/* A press handed to the view (UIScreen_TransferPress) never pressed on it: it is held from when the view first hears of it, began where it
 * began, and tells no modifiers (they were held over another widget). */
static void AdoptPress(UIRenderViewButton* button, uint32_t clickCount, Vector2 pressPosition)
{
    button->IsDown = true;
    button->Modifiers = GAME_KEY_MODIFIER_NONE;
    button->ClickCount = clickCount;
    button->PressPosition = pressPosition;
}

/* The press of a button that asked for the hidden-cursor drag is over: the drag ends with the last of them, though a press of another button
 * goes on (the pointer stands still over the view during the drag, so a second button pressed then lands on it). */
static void EndCaptureOf(void* widget, UIRenderViewCapability* self, GameKey button)
{
    uint8_t Bit = UI_BUTTON_BIT(button);
    if ((self->_capturingButtons & Bit) == 0U)
    {
        return;
    }
    self->_capturingButtons = (uint8_t)(self->_capturingButtons & (uint8_t)~Bit);
    UIScreen* Screen = Widget_GetScreen(widget);
    if ((self->_capturingButtons == 0U) && (Screen != NULL) && (Screen->_pointerCaptureWidgetId == Widget_GetId(widget)))
    {
        UIScreen_ReleasePointerCapture(Screen);
    }
}


// Static functions: the kind's behaviour, for the kit's composed widget types.
static void* ResolveCapability(void* widget)
{
    return GetCapability(widget);
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderView: the arguments are required.");
    }
    const UIRenderViewOptions* Options = ((const UIRenderViewArgs*)args)->Options;
    if ((Options->CapturedButtons & (uint8_t)~UI_BUTTONS_ALL) != 0U)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIRenderView: the captured buttons must be mouse buttons (UI_BUTTON_BIT).");
    }
    UIRenderViewCapability* Self = GetCapability(widget);
    Memory_Zero(Self, sizeof(*Self));
    Self->_capturedButtons = Options->CapturedButtons;
    // Its host's tools read the keys while it is entered: navigation and leaving by a key leave them alone.
    Error Result = Widget_SetKeepsKeysWhenEntered(widget, true);
    return (Result.Code == ErrorCode_Success) ? UIElement_ApplyOptions(widget, &Options->Element) : Result;
}

static Error Behaviour_OnMouseInput(void* widget, const WidgetMouseInputArgs* args)
{
    UIRenderViewCapability* Self = GetCapability(widget);
    UIRenderViewButton* Button = GetButton(Self, args->Button);
    Error Result = UIElement_PostMouseEvents(widget, args);
    if ((Result.Code != ErrorCode_Success) || (Button == NULL))
    {
        return Result;
    }
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
        {
            Button->IsDown = true;
            Button->WasPressed = true;
            Button->Modifiers = args->Modifiers;
            Button->ClickCount = args->ClickCount;
            Button->PressPosition = args->WidgetPosition;
            if ((Self->_capturedButtons & UI_BUTTON_BIT(args->Button)) != 0U)
            {
                // Another widget's drag holding the capture (a second press held elsewhere) leaves this one absolute: DeltaPixels either way.
                Error Captured = UIScreen_CapturePointer(Widget_GetScreen(widget), Widget_GetId(widget));
                if (Captured.Code == ErrorCode_Success)
                {
                    Self->_capturingButtons = (uint8_t)(Self->_capturingButtons | UI_BUTTON_BIT(args->Button));
                }
                Error_Deconstruct(&Captured);
            }
            return Error_CreateSuccess();
        }
        case UIMouseInputType_Move:
            Button->DeltaPixels.x += args->DeltaPixels.x;
            Button->DeltaPixels.y += args->DeltaPixels.y;
            return Error_CreateSuccess();
        case UIMouseInputType_ButtonRelease:
            if (!Button->IsDown)
            {
                AdoptPress(Button, args->ClickCount, args->ClickStartWidgetPosition);
            }
            Button->IsDown = false;
            Button->WasReleased = true;
            EndCaptureOf(widget, Self, args->Button);
            return Error_CreateSuccess();
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess(); // Another widget's press: not the view's.
    }
    return Error_CreateSuccess();
}

static Error Behaviour_OnPressLost(void* widget, GameKey button)
{
    UIRenderViewCapability* Self = GetCapability(widget);
    UIRenderViewButton* Button = GetButton(Self, button);
    if (Button != NULL)
    {
        Button->IsDown = false;
        Button->WasLost = true;
        EndCaptureOf(widget, Self, button);
    }
    return Error_CreateSuccess();
}

/* The wheel over the view is its host's, focused or not: it goes no further up. */
static Error Behaviour_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    UIRenderViewCapability* Self = GetCapability(widget);
    *outIsHandled = true;
    Self->_wheel.x += args->ScrollDelta.x;
    Self->_wheel.y += args->ScrollDelta.y;
    Self->_wheelModifiers = args->Modifiers;
    UIEventMouse Mouse = { .Button = GameKey_Unknown, .Modifiers = args->Modifiers, .ScreenPosition = args->ScreenPosition,
        .ScrollDelta = args->ScrollDelta, .IsOverWidget = true };
    return UIScreen_PostWidgetMouseEvent(Widget_GetScreen(widget), Widget_GetId(widget), UIElementEvent_Scrolled, &Mouse);
}


// Fields.
static const UIKindCapability RenderViewCapabilities[] = { { .Capability = UIElementCapability_RenderView, .Resolve = ResolveCapability } };

static const UIKindBehaviour RenderViewBehaviour =
{
    .Kind = UIElementKind_RenderView,
    .Capabilities = RenderViewCapabilities,
    .CapabilityCount = sizeof(RenderViewCapabilities) / sizeof(RenderViewCapabilities[0]),
    .BaseSize = sizeof(UIRenderViewLookBase),
    .IsFocusable = true,
    .NeedsEntering = true,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .OnMouseInput = Behaviour_OnMouseInput,
    .OnPressLost = Behaviour_OnPressLost,
    .OnScrollInput = Behaviour_OnScrollInput
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIRenderView_GetKindBehaviour(void)
{
    return &RenderViewBehaviour;
}


// Public functions.
bool UIRenderViewCapability_GetTexture(const UIRenderViewCapability* self, Texture2D* outTexture, bool* outIsBottomUp)
{
    if (!self->_hasTexture)
    {
        return false;
    }
    *outTexture = self->_texture;
    *outIsBottomUp = self->_isBottomUp;
    return true;
}

Error UIRenderView_SetTexture(UIScreen* screen, uint64_t viewId, Texture2D texture, bool isBottomUp)
{
    if (screen == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderView_SetTexture: screen must not be NULL.");
    }
    Widget* Target = NULL;
    UIRenderViewCapability* Self = NULL;
    Error Result = ResolveView(screen, viewId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_texture = texture;
    Self->_hasTexture = true;
    Self->_isBottomUp = isBottomUp;
    return Error_CreateSuccess();
}

Error UIRenderView_ClearTexture(UIScreen* screen, uint64_t viewId)
{
    if (screen == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderView_ClearTexture: screen must not be NULL.");
    }
    Widget* Target = NULL;
    UIRenderViewCapability* Self = NULL;
    Error Result = ResolveView(screen, viewId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Memory_Zero(&Self->_texture, sizeof(Self->_texture));
    Self->_hasTexture = false;
    Self->_isBottomUp = false;
    return Error_CreateSuccess();
}

Error UIRenderView_CaptureOnPress(UIScreen* screen, uint64_t viewId, uint8_t buttonMask)
{
    if (screen == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderView_CaptureOnPress: screen must not be NULL.");
    }
    if ((buttonMask & (uint8_t)~UI_BUTTONS_ALL) != 0U)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIRenderView_CaptureOnPress: the mask must hold mouse buttons (UI_BUTTON_BIT).");
    }
    Widget* Target = NULL;
    UIRenderViewCapability* Self = NULL;
    Error Result = ResolveView(screen, viewId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_capturedButtons = buttonMask;
    return Error_CreateSuccess();
}

Error UIRenderView_TakeInput(UIScreen* screen, uint64_t viewId, UIRenderViewInput* outInput)
{
    if ((screen == NULL) || (outInput == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderView_TakeInput: screen and outInput must not be NULL.");
    }
    Memory_Zero(outInput, sizeof(*outInput));
    Widget* Target = NULL;
    UIRenderViewCapability* Self = NULL;
    Error Result = ResolveView(screen, viewId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // A press handed to the view that has not come up yet is held from now on (it may not have moved, so the view may not have heard of it).
    for (size_t Index = 0U; Index < UI_RENDER_VIEW_BUTTON_COUNT; Index++)
    {
        const UIScreenButtonTracking* Press = &screen->_buttons[Index];
        if (!Self->_buttons[Index].IsDown && Press->IsPressed && Press->HasReachedWidget && (Press->TargetWidgetId == viewId))
        {
            AdoptPress(&Self->_buttons[Index], Press->ClickCount, UIScreen_ScreenToWidget(screen, Target, Press->StartScreenPosition));
        }
    }
    outInput->IsShown = IsWidgetShown(screen, Target);
    outInput->IsHovered = Widget_IsHovered(Target);
    outInput->IsFocused = Widget_IsFocused(Target);
    outInput->IsEntered = Widget_IsEntered(Target);
    outInput->PixelSize = GetWholeSize(screen, Target);
    // Over the view the pointer is present; a press the view holds tracks it past the box (where it last was, while a hidden-cursor drag
    // holds it still).
    outInput->HasPointer = (outInput->IsHovered && UIScreen_IsPointerPresent(screen)) || IsAnyButtonDown(Self);
    if (outInput->HasPointer)
    {
        Rectangle Box = UIScreen_GetWidgetPixelBox(screen, Target);
        outInput->Pointer = UIScreen_ScreenToWidget(screen, Target, UIScreen_GetPointerPosition(screen));
        outInput->PointerPixels = (Vector2){ .x = outInput->Pointer.x * Box.width, .y = outInput->Pointer.y * Box.height };
    }
    for (size_t Index = 0U; Index < UI_RENDER_VIEW_BUTTON_COUNT; Index++)
    {
        UIRenderViewButton* Button = &Self->_buttons[Index];
        outInput->Buttons[Index] = *Button;
        Button->WasPressed = false;
        Button->WasReleased = false;
        Button->WasLost = false;
        Button->DeltaPixels = (Vector2){ .x = 0.0f, .y = 0.0f };
    }
    outInput->Wheel = Self->_wheel;
    outInput->WheelModifiers = Self->_wheelModifiers;
    Self->_wheel = (Vector2){ .x = 0.0f, .y = 0.0f };
    outInput->IsPointerCaptured = screen->_pointerCaptureWidgetId == viewId;
    return Error_CreateSuccess();
}

Error UIRenderView_GetPixelSize(const UIScreen* screen, uint64_t viewId, Int32Vector* outSize)
{
    if ((screen == NULL) || (outSize == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderView_GetPixelSize: screen and outSize must not be NULL.");
    }
    *outSize = (Int32Vector){ .X = 0, .Y = 0 };
    Widget* Target = NULL;
    UIRenderViewCapability* Self = NULL;
    Error Result = ResolveView(screen, viewId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outSize = GetWholeSize(screen, Target);
    return Error_CreateSuccess();
}

Error UIRenderView_IsShown(const UIScreen* screen, uint64_t viewId, bool* outIsShown)
{
    if ((screen == NULL) || (outIsShown == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderView_IsShown: screen and outIsShown must not be NULL.");
    }
    *outIsShown = false;
    Widget* Target = NULL;
    UIRenderViewCapability* Self = NULL;
    Error Result = ResolveView(screen, viewId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outIsShown = IsWidgetShown(screen, Target);
    return Error_CreateSuccess();
}
