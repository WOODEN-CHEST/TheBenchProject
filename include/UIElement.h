#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
// The element's parts are embedded by value: a look's state and its text (a document and a layout), and the press behaviour.
#include "raylib/raylib.h"
#include "GameInput.h"
#include "GameKey.h"
#include "NamespacedKey.h"
#include "ProgramTime.h"
#include "TextDocument.h"
#include "TextLayout.h"
#include "UIBoxStyle.h"
#include "UIPlacement.h"
#include "UIScreen.h"
#include "UITheme.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"


/**
 * @file UIElement.h
 * @brief What every element kind of the kit shares: its options, its events, plain text content, the press behaviour, and the
 *        helpers looks draw with (a look's state and fades, its text, icons).
 *
 * An ELEMENT is two things kept apart. Its BEHAVIOUR is a capability struct (UIButtonCapability) holding logical state (pressed,
 * enabled, the value, its text as plain content) with helpers that interpret input and post events; calling code talks only to
 * it, by widget id. Its LOOK is a widget type that embeds the capability and draws: fades, theme lookups, laid-out text and
 * animation state live there (UIKit.h, UIKit_RegisterLook). A look picks up theme values, nine-slice drawing and state fades
 * through the helpers here (UIElementLook_*, UIElementText_*, UIBoxStyle_*), and is free to ignore them.
 *
 * A variant of a kind costs, from cheapest: theme data (colours, sizes, nine-slice art), a named variant in the theme, or a look
 * of its own (Render and Measure, maybe Update). Calling code does not change for any of them.
 *
 * Fields of the structs here are read-only outside the UI's element modules.
 */


// Macros.
/** @brief The bit of a mouse button in a mask of buttons. */
#define UI_BUTTON_BIT(button) ((uint8_t)(1U << (uint32_t)((int32_t)(button) - (int32_t)GameKey_MouseLeft)))
/** @brief The mask of buttons an element activates by unless it says otherwise: the left one. */
#define UI_BUTTONS_LEFT UI_BUTTON_BIT(GameKey_MouseLeft)
/** @brief Every mouse button. */
#define UI_BUTTONS_ALL ((uint8_t)((1U << (uint32_t)((int32_t)GameKey_MouseBack - (int32_t)GameKey_MouseLeft + 1)) - 1U))


// Forward declarations.
/** @brief The kit; full type in UIKit.h. */
typedef struct UIKitStruct UIKit;
/** @brief The factory of text components; full type in TextComponentFactory.h. Borrowed. */
typedef struct TextComponentFactoryStruct TextComponentFactory;


// Types.
/**
 * @brief The events elements post, the same ids for every kind: a UIEvent's WidgetEventId. They are the one mechanism for what an
 *        input does.
 *
 * Value elements distinguish three: Changed during a drag or while typing, for a live preview; Committed at the release or
 * the key's release, which is what an undo history records; Cancelled when Escape put the first value back. A disabled element
 * posts none of them.
 */
typedef enum UIElementEventEnum
{
    /** @brief The element did what it is for: its click mode says so, or the keyboard does. The event's mouse data tells the
     *  button or the key, and the modifiers. A button posts it; a toggle's click is Changed and Committed. */
    UIElementEvent_Activated,
    /** @brief The value changed and the change may go on. The value is in the event: a bool or a double. */
    UIElementEvent_Changed,
    /** @brief The change is done. The value is in the event. Not posted when the value ends where it started. */
    UIElementEvent_Committed,
    /** @brief The change was cancelled and the value at its start is back. That value is in the event. */
    UIElementEvent_Cancelled,
    /** @brief Any mouse button went down on it. The mouse data tells the button, the modifiers and the position. */
    UIElementEvent_Pressed,
    /** @brief A button of a press on it came up, or one held elsewhere came up over it. The mouse data tells the button, the modifiers,
     *  the position and whether the pointer is over it. */
    UIElementEvent_Released,
    /** @brief A second press of one button came soon after the first, in the same place. The mouse data tells the button and the
     *  modifiers. A third in the row does not post it again. */
    UIElementEvent_DoubleClicked,
    /** @brief The wheel moved over an element that takes it. The mouse data tells the delta and the modifiers. */
    UIElementEvent_Scrolled,
    /** @brief A right press and release on it, or the context menu's key while it is focused. The mouse data tells the position. */
    UIElementEvent_ContextRequested
} UIElementEvent;

/**
 * @brief When a pressable element activates.
 */
typedef enum UIClickModeEnum
{
    /** @brief When a button goes down on it: for what should happen in a hurry. */
    UIClickMode_OnPress,
    /** @brief When a button comes up on it, wherever the press began: for press-drag-release menus. */
    UIClickMode_OnRelease,
    /** @brief When a press on it comes up on it: dragging off takes an accidental click back. The default. */
    UIClickMode_OnPressAndRelease
} UIClickMode;

/**
 * @brief What every element is created with. A plain value; build one with UIElementOptions_Create.
 */
typedef struct UIElementOptionsStruct
{
    /** @brief Whether Placement says where it sits (a child of a stack or a panel is placed by them instead). */
    bool HasPlacement;
    /** @brief Where it sits in its parent, when HasPlacement. */
    UIPlacement Placement;
    /** @brief How it wants to sit in the stack or the panel that holds it. */
    UIStackHint Hint;
    /** @brief The name of the theme's variant it uses, or NULL for the kind's own values. Copied. */
    const unsigned char* Variant;
    /** @brief The name of the look it is made with (a namespaced key such as tbp:test_box; written without a namespace it is
     *  tbp's), or NULL for the look the screen's theme names for its kind, or else the kit's standard one. A name no look answers
     *  is logged once and gets the standard look. Fixed for the element's life. Read for the call. */
    const unsigned char* Look;
    /** @brief Its own scale (Widget_SetScale): everything drawn inside it that much larger. Within [WIDGET_MIN_SCALE,
     *  WIDGET_MAX_SCALE]. */
    float Scale;
} UIElementOptions;

/**
 * @brief An element's text as plain content: an owned copy of the UTF-8 and a revision that moves with every change. Its
 *        capability holds it; its look lays it out and draws it (UIElementText_Refresh reads the revision).
 */
typedef struct UITextContentStruct
{
    /** @brief The text, NUL-terminated UTF-8; owned, never NULL once constructed ("" for none). */
    unsigned char* _text;
    /** @brief Moves on with every change. */
    uint64_t _revision;
} UITextContent;

/**
 * @brief An element's look: its kind and variant, and the state it shows, eased from state to state over the theme's
 *        transition time. Render only reads it (UIElementLook_Update advances it, on the update side).
 */
typedef struct UIElementLookStruct
{
    /** @brief The kind whose theme block it reads. */
    UIElementKind _kind;
    /** @brief The variant's name; empty for the kind's own look. */
    unsigned char _variant[UI_THEME_VARIANT_NAME_CAPACITY];
    /** @brief How much of each state shows: they sum to 1, and only _state rises. */
    float _weights[UIElementState_Count];
    /** @brief The state it is going to. */
    UIElementState _state;
    /** @brief Whether the look has a state yet: the first one shows at once. */
    bool _hasState;
    /** @brief The screen's theme revision whose parts were last checked for what is missing. */
    uint64_t _checkedThemeRevision;
    /** @brief Whether _checkedThemeRevision means anything yet. */
    bool _hasCheckedTheme;
} UIElementLook;

/**
 * @brief What an icon is.
 */
typedef enum UIElementIconKindEnum
{
    /** @brief No icon. */
    UIElementIconKind_None,
    /** @brief A placeholder letter, until art exists. */
    UIElementIconKind_Letter,
    /** @brief A sprite animation asset by name: the engine's own type for frames, so a still icon is an animation of one frame. */
    UIElementIconKind_SpriteAnimation
} UIElementIconKind;

/**
 * @brief An element's icon as plain content: a placeholder letter or a sprite animation by name, with an explicit kind. A plain
 *        value; build one with UIElementIcon_None, UIElementIcon_FromLetter or UIElementIcon_FromSpriteAnimation. How it shows is
 *        the look's (UIElementIconView).
 */
typedef struct UIElementIconStruct
{
    /** @brief What it is. */
    UIElementIconKind Kind;
    /** @brief The letter's codepoint, when Kind is UIElementIconKind_Letter. */
    uint32_t Letter;
    /** @brief The sprite animation's full asset name (NUL-terminated), when Kind is UIElementIconKind_SpriteAnimation. */
    unsigned char Name[NAMESPACED_KEY_BUFFER_SIZE];
} UIElementIcon;

/**
 * @brief A look's view of an icon: a document and a layout showing the letter or playing the animation, one line of the theme's
 *        text tall times its widget's effective scale. A look holds it and keeps it in step with a capability's UIElementIcon.
 */
typedef struct UIElementIconViewStruct
{
    /** @brief Owns what it shows, the font and the animation's playback. */
    TextDocument _document;
    /** @brief The laid-out icon. */
    TextLayout _layout;
    /** @brief The text components' factory its documents' components come from; borrowed. */
    TextComponentFactory* _factory;
    /** @brief The icon the document shows. */
    UIElementIcon _shown;
    /** @brief The screen's theme revision whose font the document has. */
    uint64_t _appliedThemeRevision;
    /** @brief Whether _appliedThemeRevision means anything yet. */
    bool _hasAppliedTheme;
} UIElementIconView;

/**
 * @brief A look's laid-out text: a document and a layout of one plain text, in the screen's theme's font and size times its
 *        widget's effective scale, wrapped at a width when it has one. A look holds it; its content comes from a capability's
 *        UITextContent.
 */
typedef struct UIElementTextStruct
{
    /** @brief Owns the text and the font it uses. */
    TextDocument _document;
    /** @brief The laid-out text. */
    TextLayout _layout;
    /** @brief The screen's theme revision whose font the document has. */
    uint64_t _appliedThemeRevision;
    /** @brief Whether _appliedThemeRevision means anything yet. */
    bool _hasAppliedTheme;
    /** @brief The content's revision the document has. */
    uint64_t _appliedContentRevision;
    /** @brief Whether _appliedContentRevision means anything yet. */
    bool _hasAppliedContent;
    /** @brief Whether it wraps at _wrapWidth. */
    bool _isWrapped;
    /** @brief Where its lines wrap, in its widget's own UI pixels, when _isWrapped. */
    float _wrapWidth;
} UIElementText;

/**
 * @brief The press behaviour a button and a toggle share: per button, a click mode, and Enter or Space while focused.
 */
typedef struct UIPressableStruct
{
    /** @brief When it activates. */
    UIClickMode _clickMode;
    /** @brief The buttons that activate it (UI_BUTTON_BIT). */
    uint8_t _activationButtons;
    /** @brief The buttons among those whose press on it is held. */
    uint8_t _heldButtons;
    /** @brief The pointer is over it during such a press. */
    bool _isPointerInside;
    /** @brief The key whose press activated it and is still down, or GameKey_Unknown. */
    GameKey _heldKey;
} UIPressable;


// Options.
/**
 * @brief Default options: no placement, sized to its content and stretched across in a stack, the kind's own look.
 * @returns The options.
 */
static inline UIElementOptions UIElementOptions_Create(void)
{
    return (UIElementOptions)
    {
        .HasPlacement = false,
        .Placement = UIPlacement_Stretch(UIInsets_All(0.0f)),
        .Hint = UIStackHint_Content(),
        .Variant = NULL,
        .Look = NULL,
        .Scale = 1.0f
    };
}


// State.
/**
 * @brief The one state an element shows, from everything that holds for it.
 *
 * Disabled first, then pressed, invalid, entered, selected, hovered and focused: a focused button under the pointer shows as
 * hovered (keyboard navigation has its own outline), a selected one stays selected under the pointer, and an entered edit
 * element shows so under the pointer too, so the player can tell its keys are its own.
 * @param isEnabled Whether it takes input.
 * @param isPressed Whether it is being pressed.
 * @param isInvalid Whether its value is refused.
 * @param isEntered Whether it is an edit element that was entered (Widget_IsEntered).
 * @param isSelected Whether it is the chosen one.
 * @param isHovered Whether the pointer is over it.
 * @param isFocused Whether it has the keyboard's focus.
 * @returns The state.
 */
UIElementState UIElementState_Choose(bool isEnabled, bool isPressed, bool isInvalid, bool isEntered, bool isSelected, bool isHovered,
    bool isFocused);


// The look.
/**
 * @brief Starts a look.
 * @param self The look; must not be NULL.
 * @param kind The element's kind.
 * @param variant The variant's name, or NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange if the variant's name is too long or not a valid local name.
 */
Error UIElementLook_Construct(UIElementLook* self, UIElementKind kind, const unsigned char* variant);

/**
 * @brief Changes the look's variant.
 * @param self The look; must not be NULL.
 * @param variant The variant's name, or NULL for the kind's own look.
 * @returns Success; ErrorCode_ArgumentOutOfRange if the name is too long or not a valid local name.
 */
Error UIElementLook_SetVariant(UIElementLook* self, const unsigned char* variant);

/**
 * @brief Advances the look by a tick: moves toward @p state over the theme's transition time, and logs once, through the
 *        screen, each part the theme leaves out. Call it from the element's Update.
 * @param self The look; must not be NULL.
 * @param screen The element's screen; must not be NULL.
 * @param state The state it should show.
 * @param passedSeconds The tick's seconds.
 */
void UIElementLook_Update(UIElementLook* self, UIScreen* screen, UIElementState state, double passedSeconds);

/**
 * @brief Finds the theme's values for this look. Valid until the screen's theme changes: read it where it is used.
 * @param self The look; must not be NULL.
 * @param screen The element's screen; must not be NULL.
 * @returns The style, or NULL when there is no theme or it leaves the kind out (every lookup takes a NULL style).
 */
const UIThemeStyle* UIElementLook_GetStyle(const UIElementLook* self, const UIScreen* screen);

/**
 * @brief Returns a colour set's colour at the moment: the states showing, blended by how much each shows.
 * @param self The look; must not be NULL.
 * @param colors The colours, or NULL.
 * @param fallback The colour where there is none.
 * @returns The colour.
 */
Color UIElementLook_BlendColors(const UIElementLook* self, const UIStateColors* colors, Color fallback);

/**
 * @brief Draws one of the look's box parts in @p box: a part the theme leaves out draws a plain box.
 * @param self The look; must not be NULL.
 * @param style The look's style (UIElementLook_GetStyle), or NULL.
 * @param part The part's index.
 * @param context The widget's render context; must not be NULL.
 * @param box The box in surface pixels.
 * @param scale Surface pixels per UI pixel: the widget's effective scale (Widget_GetEffectiveScale).
 * @returns Success, or the drawing's error.
 */
Error UIElementLook_DrawBox(const UIElementLook* self, const UIThemeStyle* style, int32_t part, UIRenderContext* context,
    Rectangle box, float scale);


// The text content.
/**
 * @brief Starts a text content with a copy of @p text.
 * @param self The content; must not be NULL.
 * @param text The text as UTF-8, or NULL for none. Copied.
 * @returns Success; ErrorCode_InvalidTextEncoding if the text is not valid UTF-8, with nothing left to release.
 */
Error UITextContent_Construct(UITextContent* self, const unsigned char* text);

/**
 * @brief Releases a text content.
 * @param self The content; must not be NULL.
 */
void UITextContent_Deconstruct(UITextContent* self);

/**
 * @brief Changes the text: a copy of @p text, and the revision moves on, unless it is the same text.
 * @param self The content; must not be NULL.
 * @param text The text as UTF-8; must not be NULL. Copied.
 * @param outHasChanged [out] Set to whether the text changed; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p text is NULL; ErrorCode_InvalidTextEncoding if it is not valid UTF-8, with the
 *          old text kept.
 */
Error UITextContent_Set(UITextContent* self, const unsigned char* text, bool* outHasChanged);

/**
 * @brief Returns the text.
 * @param self The content; must not be NULL.
 * @returns The NUL-terminated UTF-8 text; valid until it changes.
 */
static inline const unsigned char* UITextContent_Get(const UITextContent* self)
{
    return self->_text;
}

/**
 * @brief Returns the content's revision, which moves on with every change.
 * @param self The content; must not be NULL.
 * @returns The revision.
 */
static inline uint64_t UITextContent_GetRevision(const UITextContent* self)
{
    return self->_revision;
}


// The text a look draws.
/**
 * @brief Starts a look's text.
 * @param self The text; must not be moved afterwards. Must not be NULL.
 * @param factory The text components' factory; borrowed, must outlive it. Must not be NULL.
 * @param assets Loads the font; borrowed, may be NULL (the built-in font).
 * @param logger Reports a font that does not load; borrowed, may be NULL.
 * @returns Success; otherwise the document's or the component's error, with nothing left to release.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIElementText_Construct(UIElementText* self, TextComponentFactory* factory, AssetManager* assets, Logger* logger);

/**
 * @brief Releases a look's text. Keeps the first error.
 * @param self The text; must not be NULL.
 * @returns Success, or the first error.
 */
Error UIElementText_Deconstruct(UIElementText* self);

/**
 * @brief Makes the text wrap at a width, or not.
 * @param self The text; must not be NULL.
 * @param isWrapped Whether it wraps.
 * @param wrapWidth Where its lines wrap, in its widget's own UI pixels, when @p isWrapped; a value that is not a positive finite
 *        number does not wrap.
 */
void UIElementText_SetWrapWidth(UIElementText* self, bool isWrapped, float wrapWidth);

/**
 * @brief Lays the text out if anything it depends on changed: the content (by its revision), the theme's font and size, its
 *        widget's effective scale, the viewport, the wrap width. Call it from Update and before measuring. Cheap when nothing did.
 * @param self The text; must not be NULL.
 * @param widget The look's widget; must not be NULL.
 * @param content What it shows, or NULL to keep what it has.
 * @returns Success; otherwise the content's or the layout's error.
 */
Error UIElementText_Refresh(UIElementText* self, const Widget* widget, const UITextContent* content);

/**
 * @brief Returns the laid-out text's size in surface pixels.
 * @param self The text; must not be NULL.
 * @param screen The element's screen; must not be NULL.
 * @returns The size.
 */
Vector2 UIElementText_GetPixelSize(const UIElementText* self, const UIScreen* screen);

/**
 * @brief Draws the text with its top-left corner at a whole surface pixel.
 * @param self The text; must not be NULL.
 * @param context The widget's render context; must not be NULL.
 * @param x The left edge in surface pixels.
 * @param y The top edge in surface pixels.
 * @param color The text's colour; the context's tint is multiplied in.
 * @returns Success, or the drawing's error.
 */
Error UIElementText_Draw(const UIElementText* self, UIRenderContext* context, float x, float y, Color color);


// The icon.
/**
 * @brief Returns no icon.
 * @returns The icon.
 */
static inline UIElementIcon UIElementIcon_None(void)
{
    UIElementIcon Icon = { .Kind = UIElementIconKind_None, .Letter = 0U };
    Icon.Name[0] = 0U;
    return Icon;
}

/**
 * @brief Returns a placeholder letter as an icon.
 * @param codepoint The letter's codepoint; a valid one other than 0 (UIElementIcon_IsValid).
 * @returns The icon.
 */
static inline UIElementIcon UIElementIcon_FromLetter(uint32_t codepoint)
{
    UIElementIcon Icon = { .Kind = UIElementIconKind_Letter, .Letter = codepoint };
    Icon.Name[0] = 0U;
    return Icon;
}

/**
 * @brief Makes an icon of a sprite animation asset.
 * @param animationName The animation's asset name (written without a namespace it is tbp's); must not be NULL.
 * @param outIcon [out] Receives the icon; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange if the name is not a valid asset name.
 */
Error UIElementIcon_FromSpriteAnimation(const unsigned char* animationName, UIElementIcon* outIcon);

/**
 * @brief Returns whether an icon's values make sense: a known kind, a letter that is a valid codepoint other than 0, a name that is
 *        a valid full asset name.
 * @param icon The icon; must not be NULL.
 * @returns true if valid.
 */
bool UIElementIcon_IsValid(const UIElementIcon* icon);

/**
 * @brief Returns whether two icons show the same.
 * @param a The first; must not be NULL.
 * @param b The second; must not be NULL.
 * @returns true if they do.
 */
bool UIElementIcon_Equals(const UIElementIcon* a, const UIElementIcon* b);

/**
 * @brief Starts a look's icon view, showing nothing.
 * @param self The view; must not be moved afterwards. Must not be NULL.
 * @param factory The text components' factory; borrowed, must outlive it. Must not be NULL.
 * @param assets Loads the font and the animation; borrowed, may be NULL (the built-in font, no animation).
 * @param logger Reports a font or an animation that does not load, once; borrowed, may be NULL.
 * @returns Success; otherwise the document's error, with nothing left to release.
 */
Error UIElementIconView_Construct(UIElementIconView* self, TextComponentFactory* factory, AssetManager* assets, Logger* logger);

/**
 * @brief Releases a look's icon view. Keeps the first error.
 * @param self The view; must not be NULL.
 * @returns Success, or the first error.
 */
Error UIElementIconView_Deconstruct(UIElementIconView* self);

/**
 * @brief Brings the view in line with an icon and lays it out if anything changed: the icon, the theme's font and text size, its
 *        widget's effective scale, the viewport. Call it from Update and before measuring.
 * @param self The view; must not be NULL.
 * @param widget The look's widget; must not be NULL.
 * @param icon What it shows; must not be NULL and valid.
 * @returns Success; otherwise the components' or the layout's error, with the old icon kept.
 */
Error UIElementIconView_Refresh(UIElementIconView* self, const Widget* widget, const UIElementIcon* icon);

/**
 * @brief Advances an animation's playback by a tick. Call it from the look's Update.
 * @param self The view; must not be NULL.
 * @param time The tick's time.
 * @returns Success, or the document's error.
 */
Error UIElementIconView_Update(UIElementIconView* self, ProgramTime time);

/**
 * @brief Returns whether the view shows anything.
 * @param self The view; must not be NULL.
 * @returns true for an icon of a kind other than none.
 */
static inline bool UIElementIconView_IsShowing(const UIElementIconView* self)
{
    return self->_shown.Kind != UIElementIconKind_None;
}

/**
 * @brief Returns the laid-out icon's size in surface pixels; zero with nothing to show.
 * @param self The view; must not be NULL.
 * @param screen The element's screen; must not be NULL.
 * @returns The size.
 */
Vector2 UIElementIconView_GetPixelSize(const UIElementIconView* self, const UIScreen* screen);

/**
 * @brief Draws the icon with its top-left corner at a surface pixel; a letter in @p color, an animation's frame tinted by it.
 * @param self The view; must not be NULL.
 * @param context The widget's render context; must not be NULL.
 * @param x The left edge in surface pixels.
 * @param y The top edge in surface pixels.
 * @param color The colour; the context's tint is multiplied in.
 * @returns Success, or the drawing's error.
 */
Error UIElementIconView_Draw(const UIElementIconView* self, UIRenderContext* context, float x, float y, Color color);


// The press behaviour.
/**
 * @brief Returns the behaviour's starting state.
 * @returns The state: nothing held, activating by the left button, on a press and a release on it.
 */
static inline UIPressable UIPressable_Create(void)
{
    return (UIPressable){ ._clickMode = UIClickMode_OnPressAndRelease, ._activationButtons = UI_BUTTONS_LEFT, ._heldButtons = 0U,
        ._isPointerInside = false, ._heldKey = GameKey_Unknown };
}

/**
 * @brief Returns whether a click mode and a mask of buttons make sense.
 * @param clickMode The click mode.
 * @param activationButtons The mask; UI_BUTTON_BIT values ORed together. No bit at all leaves the keyboard alone to activate it.
 * @returns true if they do.
 */
static inline bool UIPressable_AreSettingsValid(UIClickMode clickMode, uint8_t activationButtons)
{
    return ((int32_t)clickMode >= (int32_t)UIClickMode_OnPress) && ((int32_t)clickMode <= (int32_t)UIClickMode_OnPressAndRelease)
        && ((activationButtons & (uint8_t)~UI_BUTTONS_ALL) == 0U);
}

/**
 * @brief Returns whether it should look pressed: a press of a button that activates it held with the pointer over it, or its key
 *        held.
 * @param self The behaviour; must not be NULL.
 * @returns true if pressed.
 */
static inline bool UIPressable_IsPressed(const UIPressable* self)
{
    return ((self->_heldButtons != 0U) && self->_isPointerInside) || (self->_heldKey != GameKey_Unknown);
}

/**
 * @brief Interprets mouse input from the widget's OnMouseInput hook.
 *
 * *outIsActivated is set by what the click mode says, for a button that activates it: OnPress at the press; OnRelease at a release
 * over it, wherever the press began (UIMouseInputType_ReleaseOver); OnPressAndRelease at the release of a press on it, over it
 * (nothing covering it there). Other buttons do nothing here. A disabled element swallows it all: the screen already consumed the
 * press, so it does not reach what lies behind.
 * @param self The behaviour; must not be NULL.
 * @param widget The element's widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param isEnabled Whether the element is enabled.
 * @param outIsActivated [out] Set when it was activated; must not be NULL.
 */
void UIPressable_OnMouseInput(UIPressable* self, Widget* widget, const WidgetMouseInputArgs* args, bool isEnabled,
    bool* outIsActivated);

/**
 * @brief Interprets keyboard input from the widget's OnKeyboardInput hook.
 *
 * Enter (the accept action's keys) or Space while focused activates it at the press, in every click mode. Handling the press owns
 * the key, so its repeats do not activate it again; the space that key press types is swallowed too.
 * @param self The behaviour; must not be NULL.
 * @param widget The element's widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param isEnabled Whether the element is enabled.
 * @param outIsHandled [out] Set when the input was used; must not be NULL.
 * @param outIsActivated [out] Set when it was activated; must not be NULL.
 */
void UIPressable_OnKeyboardInput(UIPressable* self, Widget* widget, const WidgetKeyboardInputArgs* args, bool isEnabled,
    bool* outIsHandled, bool* outIsActivated);

/**
 * @brief Forgets a press the screen says is lost; call it from the element's OnPressLost hook.
 * @param self The behaviour; must not be NULL.
 * @param button The press's mouse button.
 */
void UIPressable_OnPressLost(UIPressable* self, GameKey button);

/**
 * @brief Lets go of a key whose release cannot reach the element any more (it was detached, or disabled for input while held).
 *        Call it from the element's Update.
 * @param self The behaviour; must not be NULL.
 * @param screen The element's screen; must not be NULL.
 */
void UIPressable_Update(UIPressable* self, const UIScreen* screen);

/**
 * @brief Forgets every press in progress: the release that follows does nothing. The click mode and the buttons stay.
 * @param self The behaviour; must not be NULL.
 */
void UIPressable_Cancel(UIPressable* self);


// Shared helpers.
/**
 * @brief Whether a box in pixels has no room: an element placed or stacked and not laid out yet, or left no room by its stack.
 *        A Render hook draws nothing then, also the parts whose sizes the theme gives.
 * @param box The box.
 * @returns true if it is empty.
 */
static inline bool UIElement_IsBoxEmpty(Rectangle box)
{
    return !(box.width > 0.0f) || !(box.height > 0.0f);
}

/**
 * @brief Returns whether a key is one of an action's bindings (its modifiers aside): how an element answers a rebound key.
 * @param screen The element's screen; must not be NULL. Its running update's input is read.
 * @param action The action.
 * @param key The key.
 * @returns true if bound; false with no input.
 */
bool UIElement_IsActionKey(const UIScreen* screen, GameActionId action, GameKey key);

/**
 * @brief Returns whether a key that is down this tick is one of an action's, its modifiers counted: Backspace for reset, the C of
 *        Ctrl+C for copy, and not a plain C. How an element answers a rebindable shortcut, asked of a key's press.
 * @param screen The element's screen; must not be NULL. Its running update's input is read.
 * @param action The action.
 * @param key The key.
 * @returns true if it is; false with no input.
 */
bool UIElement_IsActionPress(const UIScreen* screen, GameActionId action, GameKey key);

/**
 * @brief Posts the events every element with input shares for a mouse input: Pressed and, on the second press in a row, DoubleClicked
 *        at a press; Released at a release, its own or one over it; ContextRequested at a right press and release on it. Call it from
 *        OnMouseInput while enabled, before what the element itself does: a disabled element reports nothing.
 * @param widget The element's widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIElement_PostMouseEvents(Widget* widget, const WidgetMouseInputArgs* args);

/**
 * @brief Posts a sound cue for an element (UIScreen_PostCue): nothing is queued while nobody listens.
 * @param widget The element's widget; must not be NULL.
 * @param cue The cue.
 * @returns Success, or the event queue's error.
 */
Error UIElement_PostCue(Widget* widget, UIElementCue cue);

/**
 * @brief Posts the cues of a mouse input: Press at a press on an enabled element, Refused at one on a disabled element, Release at
 *        a press's release. Call it from OnMouseInput.
 * @param widget The element's widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param isEnabled Whether the element is enabled.
 * @returns Success, or the event queue's error.
 */
Error UIElement_PostMouseCues(Widget* widget, const WidgetMouseInputArgs* args, bool isEnabled);

/**
 * @brief Posts UIElementEvent_Activated for a mouse input.
 * @param widget The element's widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIElement_PostActivatedByMouse(Widget* widget, const WidgetMouseInputArgs* args);

/**
 * @brief Posts UIElementEvent_Activated for a key, its position the middle of the widget.
 * @param widget The element's widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIElement_PostActivatedByKey(Widget* widget, const WidgetKeyboardInputArgs* args);

/**
 * @brief Answers the context menu's key: its press posts UIElementEvent_ContextRequested and is handled. Call it from OnKeyboardInput
 *        while enabled.
 * @param widget The element's widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param outIsHandled [out] Set when it was that key; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIElement_OnContextKey(Widget* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled);

/**
 * @brief Applies an element's creation options to its widget: its placement, its hint and its scale. Call it from the
 *        constructor.
 * @param widget The element's widget; must not be NULL.
 * @param options The options; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for a placement, a hint or a scale that is not valid.
 */
Error UIElement_ApplyOptions(Widget* widget, const UIElementOptions* options);

/**
 * @brief Changes the theme's variant an element uses.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param variant The variant's name, or NULL for the kind's own values.
 * @returns Success; ErrorCode_InvalidOperation for an id that is stale, or an element whose look does not read the theme (it does
 *          not declare the look capability); ErrorCode_ArgumentOutOfRange if the name is too long or not a valid local name.
 */
Error UIElement_SetVariant(UIScreen* screen, uint64_t widgetId, const unsigned char* variant);

/**
 * @brief Changes an element's own scale (Widget_SetScale): everything drawn inside it that much larger.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element (any widget of the screen).
 * @param scale The scale; within [WIDGET_MIN_SCALE, WIDGET_MAX_SCALE].
 * @returns Success; ErrorCode_InvalidOperation for a stale id; ErrorCode_ArgumentOutOfRange for a scale out of bounds.
 */
Error UIElement_SetScale(UIScreen* screen, uint64_t widgetId, float scale);
