#include "GameKey.h"
#include "GameKeyInternal.h"
#include "wr/WRString.h"
#include "raylib/raylib.h"


// Types.
/* Everything known about one key; GameKeyInfos is indexed by GameKey. */
typedef struct GameKeyInfoStruct
{
    /* KeyboardKey or MouseButton value (see IsMouseButton). */
    int32_t RaylibCode;
    bool IsMouseButton;
    uint8_t ModifierFlag;
    const unsigned char* IdString;
    const unsigned char* DisplayName;
} GameKeyInfo;


// Macros.
/* Table entry builders, so each key is one short line. */
#define KEYBOARD_KEY(raylibCode, idString, displayName) \
    { .RaylibCode = (raylibCode), .IsMouseButton = false, .ModifierFlag = GAME_KEY_MODIFIER_NONE, \
      .IdString = (idString), .DisplayName = (displayName) }
#define MODIFIER_KEY(raylibCode, modifierFlag, idString, displayName) \
    { .RaylibCode = (raylibCode), .IsMouseButton = false, .ModifierFlag = (modifierFlag), \
      .IdString = (idString), .DisplayName = (displayName) }
#define MOUSE_BUTTON(raylibCode, idString, displayName) \
    { .RaylibCode = (raylibCode), .IsMouseButton = true, .ModifierFlag = GAME_KEY_MODIFIER_NONE, \
      .IdString = (idString), .DisplayName = (displayName) }


// Fields.
/* Indexed by GameKey with designated initializers, so the order of the enum cannot drift from the table. The id
 * strings are stored in controls files: never rename one. */
// TODO: an icon sprite name per key (for the controls menu and "Press [icon]" prompts) comes with the controls menu.
static const GameKeyInfo GameKeyInfos[GameKey_Count] =
{
    [GameKey_Unknown] = { .RaylibCode = -1, .IsMouseButton = false, .ModifierFlag = GAME_KEY_MODIFIER_NONE,
        .IdString = NULL, .DisplayName = NULL },

    [GameKey_A] = KEYBOARD_KEY(KEY_A, u8"a", u8"A"),
    [GameKey_B] = KEYBOARD_KEY(KEY_B, u8"b", u8"B"),
    [GameKey_C] = KEYBOARD_KEY(KEY_C, u8"c", u8"C"),
    [GameKey_D] = KEYBOARD_KEY(KEY_D, u8"d", u8"D"),
    [GameKey_E] = KEYBOARD_KEY(KEY_E, u8"e", u8"E"),
    [GameKey_F] = KEYBOARD_KEY(KEY_F, u8"f", u8"F"),
    [GameKey_G] = KEYBOARD_KEY(KEY_G, u8"g", u8"G"),
    [GameKey_H] = KEYBOARD_KEY(KEY_H, u8"h", u8"H"),
    [GameKey_I] = KEYBOARD_KEY(KEY_I, u8"i", u8"I"),
    [GameKey_J] = KEYBOARD_KEY(KEY_J, u8"j", u8"J"),
    [GameKey_K] = KEYBOARD_KEY(KEY_K, u8"k", u8"K"),
    [GameKey_L] = KEYBOARD_KEY(KEY_L, u8"l", u8"L"),
    [GameKey_M] = KEYBOARD_KEY(KEY_M, u8"m", u8"M"),
    [GameKey_N] = KEYBOARD_KEY(KEY_N, u8"n", u8"N"),
    [GameKey_O] = KEYBOARD_KEY(KEY_O, u8"o", u8"O"),
    [GameKey_P] = KEYBOARD_KEY(KEY_P, u8"p", u8"P"),
    [GameKey_Q] = KEYBOARD_KEY(KEY_Q, u8"q", u8"Q"),
    [GameKey_R] = KEYBOARD_KEY(KEY_R, u8"r", u8"R"),
    [GameKey_S] = KEYBOARD_KEY(KEY_S, u8"s", u8"S"),
    [GameKey_T] = KEYBOARD_KEY(KEY_T, u8"t", u8"T"),
    [GameKey_U] = KEYBOARD_KEY(KEY_U, u8"u", u8"U"),
    [GameKey_V] = KEYBOARD_KEY(KEY_V, u8"v", u8"V"),
    [GameKey_W] = KEYBOARD_KEY(KEY_W, u8"w", u8"W"),
    [GameKey_X] = KEYBOARD_KEY(KEY_X, u8"x", u8"X"),
    [GameKey_Y] = KEYBOARD_KEY(KEY_Y, u8"y", u8"Y"),
    [GameKey_Z] = KEYBOARD_KEY(KEY_Z, u8"z", u8"Z"),

    [GameKey_0] = KEYBOARD_KEY(KEY_ZERO, u8"0", u8"0"),
    [GameKey_1] = KEYBOARD_KEY(KEY_ONE, u8"1", u8"1"),
    [GameKey_2] = KEYBOARD_KEY(KEY_TWO, u8"2", u8"2"),
    [GameKey_3] = KEYBOARD_KEY(KEY_THREE, u8"3", u8"3"),
    [GameKey_4] = KEYBOARD_KEY(KEY_FOUR, u8"4", u8"4"),
    [GameKey_5] = KEYBOARD_KEY(KEY_FIVE, u8"5", u8"5"),
    [GameKey_6] = KEYBOARD_KEY(KEY_SIX, u8"6", u8"6"),
    [GameKey_7] = KEYBOARD_KEY(KEY_SEVEN, u8"7", u8"7"),
    [GameKey_8] = KEYBOARD_KEY(KEY_EIGHT, u8"8", u8"8"),
    [GameKey_9] = KEYBOARD_KEY(KEY_NINE, u8"9", u8"9"),

    [GameKey_F1] = KEYBOARD_KEY(KEY_F1, u8"f1", u8"F1"),
    [GameKey_F2] = KEYBOARD_KEY(KEY_F2, u8"f2", u8"F2"),
    [GameKey_F3] = KEYBOARD_KEY(KEY_F3, u8"f3", u8"F3"),
    [GameKey_F4] = KEYBOARD_KEY(KEY_F4, u8"f4", u8"F4"),
    [GameKey_F5] = KEYBOARD_KEY(KEY_F5, u8"f5", u8"F5"),
    [GameKey_F6] = KEYBOARD_KEY(KEY_F6, u8"f6", u8"F6"),
    [GameKey_F7] = KEYBOARD_KEY(KEY_F7, u8"f7", u8"F7"),
    [GameKey_F8] = KEYBOARD_KEY(KEY_F8, u8"f8", u8"F8"),
    [GameKey_F9] = KEYBOARD_KEY(KEY_F9, u8"f9", u8"F9"),
    [GameKey_F10] = KEYBOARD_KEY(KEY_F10, u8"f10", u8"F10"),
    [GameKey_F11] = KEYBOARD_KEY(KEY_F11, u8"f11", u8"F11"),
    [GameKey_F12] = KEYBOARD_KEY(KEY_F12, u8"f12", u8"F12"),

    [GameKey_Space] = KEYBOARD_KEY(KEY_SPACE, u8"space", u8"Space"),
    [GameKey_Enter] = KEYBOARD_KEY(KEY_ENTER, u8"enter", u8"Enter"),
    [GameKey_Escape] = KEYBOARD_KEY(KEY_ESCAPE, u8"escape", u8"Escape"),
    [GameKey_Tab] = KEYBOARD_KEY(KEY_TAB, u8"tab", u8"Tab"),
    [GameKey_Backspace] = KEYBOARD_KEY(KEY_BACKSPACE, u8"backspace", u8"Backspace"),
    [GameKey_Delete] = KEYBOARD_KEY(KEY_DELETE, u8"delete", u8"Delete"),
    [GameKey_Insert] = KEYBOARD_KEY(KEY_INSERT, u8"insert", u8"Insert"),
    [GameKey_Home] = KEYBOARD_KEY(KEY_HOME, u8"home", u8"Home"),
    [GameKey_End] = KEYBOARD_KEY(KEY_END, u8"end", u8"End"),
    [GameKey_PageUp] = KEYBOARD_KEY(KEY_PAGE_UP, u8"page_up", u8"Page Up"),
    [GameKey_PageDown] = KEYBOARD_KEY(KEY_PAGE_DOWN, u8"page_down", u8"Page Down"),

    [GameKey_Up] = KEYBOARD_KEY(KEY_UP, u8"up", u8"Up"),
    [GameKey_Down] = KEYBOARD_KEY(KEY_DOWN, u8"down", u8"Down"),
    [GameKey_Left] = KEYBOARD_KEY(KEY_LEFT, u8"left", u8"Left"),
    [GameKey_Right] = KEYBOARD_KEY(KEY_RIGHT, u8"right", u8"Right"),

    [GameKey_LeftShift] = MODIFIER_KEY(KEY_LEFT_SHIFT, GAME_KEY_MODIFIER_SHIFT, u8"left_shift", u8"Left Shift"),
    [GameKey_RightShift] = MODIFIER_KEY(KEY_RIGHT_SHIFT, GAME_KEY_MODIFIER_SHIFT, u8"right_shift", u8"Right Shift"),
    [GameKey_LeftControl] = MODIFIER_KEY(KEY_LEFT_CONTROL, GAME_KEY_MODIFIER_CONTROL, u8"left_control", u8"Left Control"),
    [GameKey_RightControl] = MODIFIER_KEY(KEY_RIGHT_CONTROL, GAME_KEY_MODIFIER_CONTROL, u8"right_control", u8"Right Control"),
    [GameKey_LeftAlt] = MODIFIER_KEY(KEY_LEFT_ALT, GAME_KEY_MODIFIER_ALT, u8"left_alt", u8"Left Alt"),
    [GameKey_RightAlt] = MODIFIER_KEY(KEY_RIGHT_ALT, GAME_KEY_MODIFIER_ALT, u8"right_alt", u8"Right Alt"),
    [GameKey_LeftSuper] = MODIFIER_KEY(KEY_LEFT_SUPER, GAME_KEY_MODIFIER_SUPER, u8"left_super", u8"Left Super"),
    [GameKey_RightSuper] = MODIFIER_KEY(KEY_RIGHT_SUPER, GAME_KEY_MODIFIER_SUPER, u8"right_super", u8"Right Super"),

    [GameKey_Apostrophe] = KEYBOARD_KEY(KEY_APOSTROPHE, u8"apostrophe", u8"'"),
    [GameKey_Comma] = KEYBOARD_KEY(KEY_COMMA, u8"comma", u8","),
    [GameKey_Minus] = KEYBOARD_KEY(KEY_MINUS, u8"minus", u8"-"),
    [GameKey_Period] = KEYBOARD_KEY(KEY_PERIOD, u8"period", u8"."),
    [GameKey_Slash] = KEYBOARD_KEY(KEY_SLASH, u8"slash", u8"/"),
    [GameKey_Semicolon] = KEYBOARD_KEY(KEY_SEMICOLON, u8"semicolon", u8";"),
    [GameKey_Equal] = KEYBOARD_KEY(KEY_EQUAL, u8"equal", u8"="),
    [GameKey_LeftBracket] = KEYBOARD_KEY(KEY_LEFT_BRACKET, u8"left_bracket", u8"["),
    [GameKey_Backslash] = KEYBOARD_KEY(KEY_BACKSLASH, u8"backslash", u8"\\"),
    [GameKey_RightBracket] = KEYBOARD_KEY(KEY_RIGHT_BRACKET, u8"right_bracket", u8"]"),
    [GameKey_Grave] = KEYBOARD_KEY(KEY_GRAVE, u8"grave", u8"`"),

    [GameKey_CapsLock] = KEYBOARD_KEY(KEY_CAPS_LOCK, u8"caps_lock", u8"Caps Lock"),
    [GameKey_ScrollLock] = KEYBOARD_KEY(KEY_SCROLL_LOCK, u8"scroll_lock", u8"Scroll Lock"),
    [GameKey_NumLock] = KEYBOARD_KEY(KEY_NUM_LOCK, u8"num_lock", u8"Num Lock"),
    [GameKey_PrintScreen] = KEYBOARD_KEY(KEY_PRINT_SCREEN, u8"print_screen", u8"Print Screen"),
    [GameKey_Pause] = KEYBOARD_KEY(KEY_PAUSE, u8"pause", u8"Pause"),
    [GameKey_Menu] = KEYBOARD_KEY(KEY_KB_MENU, u8"menu", u8"Menu"),

    [GameKey_Keypad0] = KEYBOARD_KEY(KEY_KP_0, u8"keypad_0", u8"Keypad 0"),
    [GameKey_Keypad1] = KEYBOARD_KEY(KEY_KP_1, u8"keypad_1", u8"Keypad 1"),
    [GameKey_Keypad2] = KEYBOARD_KEY(KEY_KP_2, u8"keypad_2", u8"Keypad 2"),
    [GameKey_Keypad3] = KEYBOARD_KEY(KEY_KP_3, u8"keypad_3", u8"Keypad 3"),
    [GameKey_Keypad4] = KEYBOARD_KEY(KEY_KP_4, u8"keypad_4", u8"Keypad 4"),
    [GameKey_Keypad5] = KEYBOARD_KEY(KEY_KP_5, u8"keypad_5", u8"Keypad 5"),
    [GameKey_Keypad6] = KEYBOARD_KEY(KEY_KP_6, u8"keypad_6", u8"Keypad 6"),
    [GameKey_Keypad7] = KEYBOARD_KEY(KEY_KP_7, u8"keypad_7", u8"Keypad 7"),
    [GameKey_Keypad8] = KEYBOARD_KEY(KEY_KP_8, u8"keypad_8", u8"Keypad 8"),
    [GameKey_Keypad9] = KEYBOARD_KEY(KEY_KP_9, u8"keypad_9", u8"Keypad 9"),
    [GameKey_KeypadDecimal] = KEYBOARD_KEY(KEY_KP_DECIMAL, u8"keypad_decimal", u8"Keypad ."),
    [GameKey_KeypadDivide] = KEYBOARD_KEY(KEY_KP_DIVIDE, u8"keypad_divide", u8"Keypad /"),
    [GameKey_KeypadMultiply] = KEYBOARD_KEY(KEY_KP_MULTIPLY, u8"keypad_multiply", u8"Keypad *"),
    [GameKey_KeypadSubtract] = KEYBOARD_KEY(KEY_KP_SUBTRACT, u8"keypad_subtract", u8"Keypad -"),
    [GameKey_KeypadAdd] = KEYBOARD_KEY(KEY_KP_ADD, u8"keypad_add", u8"Keypad +"),
    [GameKey_KeypadEnter] = KEYBOARD_KEY(KEY_KP_ENTER, u8"keypad_enter", u8"Keypad Enter"),
    [GameKey_KeypadEqual] = KEYBOARD_KEY(KEY_KP_EQUAL, u8"keypad_equal", u8"Keypad ="),

    [GameKey_MouseLeft] = MOUSE_BUTTON(MOUSE_BUTTON_LEFT, u8"mouse_left", u8"Left Mouse"),
    [GameKey_MouseRight] = MOUSE_BUTTON(MOUSE_BUTTON_RIGHT, u8"mouse_right", u8"Right Mouse"),
    [GameKey_MouseMiddle] = MOUSE_BUTTON(MOUSE_BUTTON_MIDDLE, u8"mouse_middle", u8"Middle Mouse"),
    [GameKey_MouseSide] = MOUSE_BUTTON(MOUSE_BUTTON_SIDE, u8"mouse_side", u8"Mouse Side"),
    [GameKey_MouseExtra] = MOUSE_BUTTON(MOUSE_BUTTON_EXTRA, u8"mouse_extra", u8"Mouse Extra"),
    [GameKey_MouseForward] = MOUSE_BUTTON(MOUSE_BUTTON_FORWARD, u8"mouse_forward", u8"Mouse Forward"),
    [GameKey_MouseBack] = MOUSE_BUTTON(MOUSE_BUTTON_BACK, u8"mouse_back", u8"Mouse Back"),
};


// Static functions.
/* Exact byte equality of two NUL-terminated strings (neither NULL). */
static bool AreStringsEqual(const unsigned char* a, const unsigned char* b)
{
    bool AreEqual = false;
    Error Result = StringUTF8_EqualsExact(a, b, &AreEqual);
    Error_Deconstruct(&Result);
    return AreEqual;
}


// Public functions.
bool GameKey_IsValid(GameKey key)
{
    return (key > GameKey_Unknown) && (key < GameKey_Count);
}

bool GameKey_IsMouseButton(GameKey key)
{
    return GameKey_IsValid(key) && GameKeyInfos[key].IsMouseButton;
}

bool GameKey_IsKeyboardKey(GameKey key)
{
    return GameKey_IsValid(key) && !GameKeyInfos[key].IsMouseButton;
}

uint8_t GameKey_GetModifierFlag(GameKey key)
{
    return GameKey_IsValid(key) ? GameKeyInfos[key].ModifierFlag : GAME_KEY_MODIFIER_NONE;
}

const unsigned char* GameKey_GetIdString(GameKey key)
{
    return GameKey_IsValid(key) ? GameKeyInfos[key].IdString : NULL;
}

const unsigned char* GameKey_GetDisplayName(GameKey key)
{
    return GameKey_IsValid(key) ? GameKeyInfos[key].DisplayName : NULL;
}

bool GameKey_TryParseIdString(const unsigned char* idString, GameKey* outKey)
{
    if (idString == NULL)
    {
        return false;
    }
    for (int32_t Index = (int32_t)GameKey_Unknown + 1; Index < (int32_t)GameKey_Count; Index++)
    {
        if (AreStringsEqual(GameKeyInfos[Index].IdString, idString))
        {
            *outKey = (GameKey)Index;
            return true;
        }
    }
    return false;
}

int32_t GameKey_GetRaylibCode(GameKey key)
{
    return GameKey_IsValid(key) ? GameKeyInfos[key].RaylibCode : -1;
}
