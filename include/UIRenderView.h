#pragma once
#include <stdbool.h>
#include <stdint.h>
// Texture2D (the picture a view shows) and Vector2 (places) are raylib types the view's API is made of, so the raylib header is
// unavoidable here.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRInt32Vector.h"
#include "UIElement.h"
#include "UIScreen.h"
#include "UIWidget.h"


/**
 * @file UIRenderView.h
 * @brief The render view element (kind render_view): a picture its host draws, shown in its box, and the pointer, buttons and wheel in
 *        that box handed back to the host as a record.
 *
 * The view knows nothing of what it shows: a world, a model's preview, a test pattern. Its host keeps a render target the size of the
 * view (UIRenderView_GetPixelSize) and hands its texture over (UIRenderView_SetTexture) on the update side: its frame's PrepareRender,
 * once per shown frame, is the place. In Render it draws into the target before the screen renders, and draws nothing while the view
 * does not show (UIRenderView_IsShown: hidden, minimized, in a tab that is not selected, or with no room). The view's look only draws
 * that texture, stretched to its box (a render target's rows run bottom-up: say so when handing it over).
 *
 * INPUT. A press on the view goes to it like any widget's, consumed for the game, and so does the wheel over it, focused or not. Its
 * host reads what happened through UIRenderView_TakeInput once per tick, right after UIScreen_Update: each button's presses and
 * releases with their modifiers and click counts, its motion, a press lost, the wheel, and where the pointer is. A press of a button
 * named with UIRenderView_CaptureOnPress asks for a hidden-cursor drag (UIScreen_CapturePointer): the cursor hides, its motion is free
 * of the window's edges, and it comes back where it was when the last press that asked for it ends (a press of another button, which
 * lands on the view while the pointer stands still, does not keep it hidden).
 *
 * KEYS. The view is an edit element that keeps its keys (Widget_SetKeepsKeysWhenEntered): any press on it enters it, Enter enters it
 * once the keys focused it, and Tab onto it only focuses it. While it is entered, navigation and leaving by a key do nothing, so Tab,
 * the arrows, Enter and Escape stay unconsumed in GameInput for its host's tools to read; a press elsewhere, or code, leaves it. The view
 * handles no key itself: its host reads the keys through GameInput, and only while the record says it is entered.
 *
 * It works on any screen, and is a drop target like any widget (UIDragDrop_SetTarget).
 */


// Macros.
/** @brief How many mouse buttons a view's record tells about: GameKey_MouseLeft through GameKey_MouseBack, by key less
 *  GameKey_MouseLeft. */
#define UI_RENDER_VIEW_BUTTON_COUNT UI_SCREEN_MOUSE_BUTTON_COUNT


// Forward declarations.
/** @brief The kit; full type in UIKit.h. Borrowed. */
typedef struct UIKitStruct UIKit;


// Types.
/**
 * @brief What a render view is created with (UIKit_CreateRenderView). A plain value; build one with UIRenderViewOptions_Create.
 */
typedef struct UIRenderViewOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief The buttons whose press on the view asks for a hidden-cursor drag (UI_BUTTON_BIT of each); none by default. */
    uint8_t CapturedButtons;
} UIRenderViewOptions;

/**
 * @brief What one mouse button did on a view since its host last took the record, and whether it is held now. A plain value.
 */
typedef struct UIRenderViewButtonStruct
{
    /** @brief Held by a press that began on the view, or was handed to it (UIScreen_TransferPress: no WasPressed then), and still reaches
     *  it. */
    bool IsDown;
    /** @brief A press began on the view since the last take. */
    bool WasPressed;
    /** @brief A press of it on the view came up since the last take. */
    bool WasReleased;
    /** @brief A press of it stopped reaching the view since the last take (the view hidden, detached or input-disabled, the press
     *  handed on): no release follows it. */
    bool WasLost;
    /** @brief The GAME_KEY_MODIFIER_* flags held at its last press; none for a press handed to it (they were held over another widget). */
    uint8_t Modifiers;
    /** @brief Its last press's click count: 1 for a single press, 2 for the second of a double click. 0 before any press. */
    uint32_t ClickCount;
    /** @brief Where its last press began, in [0;1] of the view's box (one handed to it may have begun past the box). */
    Vector2 PressPosition;
    /** @brief How far its press moved the pointer since the last take, in surface pixels (summed over the ticks); during a hidden-cursor
     *  drag the raw motion, the pointer itself standing still. */
    Vector2 DeltaPixels;
} UIRenderViewButton;

/**
 * @brief A view's record (UIRenderView_TakeInput): what happened since the last take, told once, and what is now. A plain value.
 */
typedef struct UIRenderViewInputStruct
{
    /** @brief The view shows: it and everything above it are shown, it is attached and its box is not empty. */
    bool IsShown;
    /** @brief The pointer is over the view and nothing above it. */
    bool IsHovered;
    /** @brief The view has the focus. */
    bool IsFocused;
    /** @brief The view is entered: the keys are its host's (a focused view that is not entered leaves them to navigation). */
    bool IsEntered;
    /** @brief The pointer's place is known to the view: over it, or past it while a press of its holds the pointer. */
    bool HasPointer;
    /** @brief The pointer's place in [0;1] of the view's box (past it while a press holds the pointer), when HasPointer. */
    Vector2 Pointer;
    /** @brief The same place in surface pixels from the box's top-left, when HasPointer. */
    Vector2 PointerPixels;
    /** @brief The view's box in whole surface pixels: what its host's picture is sized to. */
    Int32Vector PixelSize;
    /** @brief Each mouse button, by key less GameKey_MouseLeft. */
    UIRenderViewButton Buttons[UI_RENDER_VIEW_BUTTON_COUNT];
    /** @brief The wheel over the view since the last take, in notches (x across, y up and down), focused or not. */
    Vector2 Wheel;
    /** @brief The GAME_KEY_MODIFIER_* flags held at the last wheel movement. */
    uint8_t WheelModifiers;
    /** @brief A hidden-cursor drag of the view's runs. */
    bool IsPointerCaptured;
} UIRenderViewInput;

/**
 * @brief A view's state: the texture it shows and the record its hooks fill. No look; embedded by a look's widget type right after its
 *        Widget (UIRenderViewLookBase). Read-only outside the view's module.
 */
typedef struct UIRenderViewCapabilityStruct
{
    /** @brief The texture shown, borrowed from the host; meaningful while _hasTexture. */
    Texture2D _texture;
    /** @brief Whether a texture is set. */
    bool _hasTexture;
    /** @brief Whether the texture's rows run bottom-up (a render target's). */
    bool _isBottomUp;
    /** @brief The buttons whose press asks for a hidden-cursor drag. */
    uint8_t _capturedButtons;
    /** @brief The buttons whose press, held now, began the hidden-cursor drag the view has: it ends with the last of them. */
    uint8_t _capturingButtons;
    /** @brief Each button's record since the last take, and whether it is held. */
    UIRenderViewButton _buttons[UI_RENDER_VIEW_BUTTON_COUNT];
    /** @brief The wheel since the last take, in notches. */
    Vector2 _wheel;
    /** @brief The GAME_KEY_MODIFIER_* flags held at the last wheel movement. */
    uint8_t _wheelModifiers;
} UIRenderViewCapability;

/**
 * @brief How every render view look's widget struct starts: the widget, then the view's state.
 */
typedef struct UIRenderViewLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The view's state. */
    UIRenderViewCapability View;
} UIRenderViewLookBase;

/**
 * @brief What a render view look's Construct gets (UIKit_CreateRenderView passes it): the kit and the options.
 */
typedef struct UIRenderViewArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIRenderViewOptions* Options;
} UIRenderViewArgs;


// Helpers.
/**
 * @brief Default options: no hidden-cursor drag.
 * @returns The options.
 */
static inline UIRenderViewOptions UIRenderViewOptions_Create(void)
{
    return (UIRenderViewOptions){ .Element = UIElementOptions_Create(), .CapturedButtons = 0U };
}

/**
 * @brief Returns the texture a view shows, for its look.
 * @param self The view's state; must not be NULL.
 * @param outTexture [out] Receives the texture when there is one; must not be NULL.
 * @param outIsBottomUp [out] Receives whether its rows run bottom-up; must not be NULL.
 * @returns true if a texture is set.
 */
bool UIRenderViewCapability_GetTexture(const UIRenderViewCapability* self, Texture2D* outTexture, bool* outIsBottomUp);


// Functions, by id. Each returns ErrorCode_InvalidOperation for a widget that is gone or no render view, or a screen with no kit.
/**
 * @brief Hands a view the texture it shows, drawn stretched to its box from the next render on. The texture is borrowed: its host keeps
 *        it alive while the view shows it, and clears it or hands over another before unloading it. Call it on the update side (a
 *        frame's PrepareRender), never from Render.
 * @param screen The view's screen; must not be NULL.
 * @param viewId The view.
 * @param texture The texture; a texture of no size shows nothing.
 * @param isBottomUp Whether its rows run bottom-up, as a render target's do: the view draws it upright either way.
 * @returns Success; ErrorCode_IllegalArgument for a NULL screen.
 */
Error UIRenderView_SetTexture(UIScreen* screen, uint64_t viewId, Texture2D texture, bool isBottomUp);

/**
 * @brief Takes the texture away: the view shows its background.
 * @param screen The view's screen; must not be NULL.
 * @param viewId The view.
 * @returns Success; ErrorCode_IllegalArgument for a NULL screen.
 */
Error UIRenderView_ClearTexture(UIScreen* screen, uint64_t viewId);

/**
 * @brief Names the buttons whose press on the view asks for a hidden-cursor drag (a button whose drag looks around), from the next
 *        press on. A drag in progress keeps going as it began.
 * @param screen The view's screen; must not be NULL.
 * @param viewId The view.
 * @param buttonMask UI_BUTTON_BIT of each button; 0 for none.
 * @returns Success; ErrorCode_IllegalArgument for a NULL screen; ErrorCode_ArgumentOutOfRange for bits past UI_BUTTONS_ALL.
 */
Error UIRenderView_CaptureOnPress(UIScreen* screen, uint64_t viewId, uint8_t buttonMask);

/**
 * @brief Gives a view's record and starts the next: what happened since the last take (each button's presses, releases, losses, its
 *        motion; the wheel) is told once, and what is (shown, hovered, focused, entered, the pointer's place, the buttons held, the box's
 *        size) is as it is now. Its host takes it once per tick, right after UIScreen_Update, whether the view shows or not, so nothing
 *        old waits in it.
 * @param screen The view's screen; must not be NULL.
 * @param viewId The view.
 * @param outInput [out] Receives the record; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL pointer.
 */
Error UIRenderView_TakeInput(UIScreen* screen, uint64_t viewId, UIRenderViewInput* outInput);

/**
 * @brief Returns a view's box in whole surface pixels, as layout put it: the size its host's picture is drawn at.
 * @param screen The view's screen; must not be NULL.
 * @param viewId The view.
 * @param outSize [out] Receives the width and the height, each >= 0; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL pointer.
 */
Error UIRenderView_GetPixelSize(const UIScreen* screen, uint64_t viewId, Int32Vector* outSize);

/**
 * @brief Returns whether a view shows: it and everything above it are shown (its window is not hidden, minimized or in a tab that is not
 *        selected), it is attached to the screen and its box is not empty. Its host draws nothing for a view that does not show.
 * @param screen The view's screen; must not be NULL.
 * @param viewId The view.
 * @param outIsShown [out] Receives the answer; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL pointer.
 */
Error UIRenderView_IsShown(const UIScreen* screen, uint64_t viewId, bool* outIsShown);
