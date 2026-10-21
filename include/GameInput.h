#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
// Vector2 (mouse position, mouse delta, wheel delta, window size) is part of the input snapshot, so raylib's math
// types are unavoidable in this public header. No raylib input function or key code is exposed: keys are GameKeys.
#include "raylib/raylib.h"
#include "GameKey.h"


/**
 * @file GameInput.h
 * @brief The game's input for one update tick, with consumption, and the action layer (named actions bound to keys).
 *
 * THE SNAPSHOT. Once per update tick, right after raylib's PollInputEvents, the frame manager calls GameInput_Poll:
 * it records the state of every GameKey (down / pressed / released / auto-repeated), the mouse position and
 * movement, the wheel, and the text typed this tick (drained from raylib once, so no reader can starve another).
 * Everything the game reads about input in an update comes from this snapshot; nothing above the input layer
 * calls raylib input functions. Tests feed snapshots directly with GameInput_BeginTick.
 *
 * CONSUMPTION. Whoever handles a piece of input consumes it, and consumed input reads as not pressed for every
 * later reader in the tick. Frames are updated top-down, so a menu frame over the game frame (and the UI inside a
 * frame, before its game logic) gets first refusal. A consumed key stays consumed while it is held (until the tick
 * after its release), so a key typed into a text box never also walks the player forward; a new press starts
 * unconsumed. The wheel, the typed text and the pointer ("the pointer is over UI") are consumed for the current
 * tick only. The raw queries ignore consumption: the consumer of a press uses them to follow its own release.
 *
 * ACTIONS. Frames and the UI ask for actions ("move_forward", "ui_accept"), not keys, so controls can be rebound.
 * GameAction is the closed list of the game's actions; each has up to GAME_INPUT_MAX_BINDINGS_PER_ACTION bindings.
 * A binding is a key plus the modifiers that must be held with it. Other held modifiers are ignored (W still walks
 * while Shift sprints), unless another binding on the same key asks for them: the most specific binding wins, so with
 * Tab and Shift+Tab both bound, Shift+Tab is only the Shift+Tab binding, whichever actions the player gave them.
 * Consumption is per physical key: an action bound to a key someone consumed reads as not pressed. Bindings start at
 * the defaults and can be loaded from and saved to a JSON controls file (references/controls_structure.md).
 *
 * Construct with GameInput_Construct and release with GameInput_Deconstruct; the struct owns no heap memory and
 * never allocates per tick. Main thread only, like raylib's input.
 */


// Macros.
/** @brief Most text codepoints recorded per tick; more typed in one tick are dropped (raylib queues 16 per poll). */
#define GAME_INPUT_MAX_TEXT_CODEPOINTS ((size_t)32)
/** @brief Most bindings one action can have. */
#define GAME_INPUT_MAX_BINDINGS_PER_ACTION ((size_t)4)
/** @brief Longest binding text in bytes, without the terminator (e.g. "control+alt+shift+super+keypad_multiply"). */
#define GAME_INPUT_MAX_BINDING_TEXT_BYTES ((size_t)64)

/** @brief Key state flag: the key is held. */
#define GAME_INPUT_KEY_STATE_DOWN ((uint8_t)0x01)
/** @brief Key state flag: the key went down this tick. */
#define GAME_INPUT_KEY_STATE_PRESSED ((uint8_t)0x02)
/** @brief Key state flag: the key went up this tick. */
#define GAME_INPUT_KEY_STATE_RELEASED ((uint8_t)0x04)
/** @brief Key state flag: the held key auto-repeated this tick (keyboard keys only; not set for the first press). */
#define GAME_INPUT_KEY_STATE_REPEATED ((uint8_t)0x08)


// Types.
/**
 * @brief The game's actions: everything a player does that controls can be bound to.
 *
 * A closed list, like GameKey: each action has a stable id string (stored in controls files), a display name and
 * default bindings. The ui_* actions drive the UI's keyboard navigation.
 */
typedef enum GameActionEnum
{
    /** @brief UI navigation: move to the nearest widget above ("ui_up"; default Up). */
    GameAction_UIUp,
    /** @brief UI navigation: move to the nearest widget below ("ui_down"; default Down). */
    GameAction_UIDown,
    /** @brief UI navigation: move to the nearest widget to the left ("ui_left"; default Left). */
    GameAction_UILeft,
    /** @brief UI navigation: move to the nearest widget to the right ("ui_right"; default Right). */
    GameAction_UIRight,
    /** @brief UI navigation: the next widget in reading order ("ui_next"; default Tab). */
    GameAction_UINext,
    /** @brief UI navigation: the previous widget in reading order ("ui_previous"; default Shift+Tab). */
    GameAction_UIPrevious,
    /** @brief UI navigation: enter the focused container ("ui_accept"; default Enter, Keypad Enter). */
    GameAction_UIAccept,
    /** @brief UI navigation: leave the current container, or let go of the UI ("ui_back"; default Escape). */
    GameAction_UIBack,
    /** @brief Move forward ("move_forward"; default W). */
    GameAction_MoveForward,
    /** @brief Move backward ("move_back"; default S). */
    GameAction_MoveBack,
    /** @brief Move left ("move_left"; default A). */
    GameAction_MoveLeft,
    /** @brief Move right ("move_right"; default D). */
    GameAction_MoveRight,
    /** @brief Move up ("move_up"; default Space). */
    GameAction_MoveUp,
    /** @brief Move down ("move_down"; default Left Control). */
    GameAction_MoveDown,
    /** @brief Move faster while held ("sprint"; default Left Shift). */
    GameAction_Sprint,
    /** @brief Jump, when walking ("jump"; default Space, which "move_up" shares: the flycam and flight climb with it). */
    GameAction_Jump,
    /** @brief Crouch while held, when walking ("crouch"; default Left Control, which "move_down" shares). */
    GameAction_Crouch,
    /** @brief Use what the character looks at ("use"; default E). */
    GameAction_Use,
    /** @brief Pause / open the game menu ("pause"; default Escape). Read it after the UI had first refusal. */
    GameAction_Pause,

    /** @brief One past the last action; equals the number of actions. Not an action. */
    GameAction_Count
} GameAction;

/**
 * @brief One binding of an action: a key and the modifiers that must be held with it.
 *
 * The binding is active while @c Key is in the requested state (unconsumed) and it matches the held modifiers: every
 * modifier in @c Modifiers is held, and no binding (of any action) on the same key asks for more of the held modifiers
 * (the most specific binding wins). Other held modifiers do not matter. Its text form is the modifiers then the key id,
 * joined by '+' ("shift+tab", "w"); see GameInputBinding_AppendText.
 */
typedef struct GameInputBindingStruct
{
    /** @brief The key; must be a real key (GameKey_IsValid). */
    GameKey Key;
    /** @brief GAME_KEY_MODIFIER_* flags that must be held; GAME_KEY_MODIFIER_NONE for a plain key. */
    uint8_t Modifiers;
} GameInputBinding;

/**
 * @brief The raw state of every input device for one tick, as read from raylib (or built by a test).
 *
 * Fill with GameInput_SampleDevices, or start from GameInputSample_Clear and set fields, then hand it to
 * GameInput_BeginTick. A plain value type.
 */
typedef struct GameInputSampleStruct
{
    /** @brief GAME_INPUT_KEY_STATE_* flags per GameKey (index GameKey_Unknown is unused). */
    uint8_t KeyStates[GameKey_Count];
    /** @brief Mouse position in window pixels. */
    Vector2 MousePosition;
    /** @brief Mouse movement in pixels since the previous tick (also while the cursor is captured). */
    Vector2 MouseDelta;
    /** @brief Wheel movement this tick (x: horizontal, y: vertical). */
    Vector2 WheelDelta;
    /** @brief The window's size in pixels; the mouse position is relative to it. */
    Vector2 WindowSize;
    /** @brief Whether the cursor is hidden (raylib's IsCursorHidden: captured for mouse look, or hidden with HideCursor):
     *  there is no pointer for the UI then. */
    bool IsCursorCaptured;
    /** @brief The Unicode codepoints typed this tick, in order; valid entries are [0, TextCodepointCount). */
    uint32_t TextCodepoints[GAME_INPUT_MAX_TEXT_CODEPOINTS];
    /** @brief The number of valid entries in @c TextCodepoints (at most GAME_INPUT_MAX_TEXT_CODEPOINTS). */
    size_t TextCodepointCount;
} GameInputSample;

/**
 * @brief The current tick's input plus consumption, and the action bindings.
 *
 * All fields are internal; use the GameInput_* functions. Owns no heap memory.
 */
typedef struct GameInputStruct
{
    /** @brief This tick's device state. */
    GameInputSample _sample;
    /** @brief Per GameKey: consumed (sticky while the key is held). */
    bool _isKeyConsumed[GameKey_Count];
    /** @brief Whether this tick's wheel movement was consumed. */
    bool _isWheelConsumed;
    /** @brief Whether this tick's typed text was consumed. */
    bool _isTextConsumed;
    /** @brief Whether the pointer was claimed this tick (it is over some UI). */
    bool _isPointerConsumed;
    /** @brief The bindings of every action; the first _bindingCounts[action] entries are valid. */
    GameInputBinding _bindings[GameAction_Count][GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    /** @brief The number of bindings of each action. */
    size_t _bindingCounts[GameAction_Count];
} GameInput;


// Lifecycle.
/**
 * @brief Initializes the input with no keys held and the default bindings.
 * @param self The input; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error GameInput_Construct(GameInput* self);

/**
 * @brief Releases the input (it owns nothing; the struct is zeroed). Safe on NULL.
 * @param self The input, or NULL.
 * @returns Success.
 */
Error GameInput_Deconstruct(GameInput* self);


// Ticks.
/**
 * @brief Resets a sample to "nothing held, nothing moved, nothing typed".
 * @param self The sample; must not be NULL.
 */
void GameInputSample_Clear(GameInputSample* self);

/**
 * @brief Reads every input device from raylib into a sample, draining the typed-text queue.
 *
 * Call once per update tick, after PollInputEvents and only from the main thread (GameInput_Poll does both steps).
 * Needs an initialized window.
 * @param outSample [out] The sample to fill; must not be NULL.
 */
void GameInput_SampleDevices(GameInputSample* outSample);

/**
 * @brief Starts a tick from a sample: stores it, ends the consumption of keys that are no longer held, and clears
 *        the wheel, text and pointer consumption.
 * @param self The input; must not be NULL.
 * @param sample The tick's device state; must not be NULL. A TextCodepointCount above the maximum is clamped.
 */
void GameInput_BeginTick(GameInput* self, const GameInputSample* sample);

/**
 * @brief Samples the devices (GameInput_SampleDevices) and begins the tick with that sample.
 *
 * The frame manager calls this once per update tick right after PollInputEvents.
 * @param self The input; must not be NULL.
 */
void GameInput_Poll(GameInput* self);


// Keys.
/**
 * @brief Returns a key's state flags this tick, ignoring consumption.
 * @param self The input; must not be NULL.
 * @param key The key.
 * @returns GAME_INPUT_KEY_STATE_* flags; 0 for an invalid key.
 */
uint8_t GameInput_GetRawKeyState(const GameInput* self, GameKey key);

/**
 * @brief Returns a key's state flags this tick, or 0 once the key is consumed.
 * @param self The input; must not be NULL.
 * @param key The key.
 * @returns GAME_INPUT_KEY_STATE_* flags; 0 for a consumed or invalid key.
 */
uint8_t GameInput_GetKeyState(const GameInput* self, GameKey key);

/**
 * @brief Returns whether a key is held and not consumed.
 * @param self The input; must not be NULL.
 * @param key The key.
 * @returns true if the key is down and unconsumed.
 */
bool GameInput_IsKeyDown(const GameInput* self, GameKey key);

/**
 * @brief Returns whether a key went down this tick and is not consumed.
 * @param self The input; must not be NULL.
 * @param key The key.
 * @returns true for an unconsumed press.
 */
bool GameInput_IsKeyPressed(const GameInput* self, GameKey key);

/**
 * @brief Returns whether a key went up this tick and is not consumed (a release of a consumed press stays consumed).
 * @param self The input; must not be NULL.
 * @param key The key.
 * @returns true for an unconsumed release.
 */
bool GameInput_IsKeyReleased(const GameInput* self, GameKey key);

/**
 * @brief Returns whether a key went down or auto-repeated this tick and is not consumed (for actions that repeat
 *        while held, like moving a caret).
 * @param self The input; must not be NULL.
 * @param key The key.
 * @returns true for an unconsumed press or repeat.
 */
bool GameInput_IsKeyPressedOrRepeated(const GameInput* self, GameKey key);

/**
 * @brief Returns whether a key is consumed.
 * @param self The input; must not be NULL.
 * @param key The key.
 * @returns true if consumed; false for an invalid key.
 */
bool GameInput_IsKeyConsumed(const GameInput* self, GameKey key);

/**
 * @brief Consumes a key: it reads as not pressed until the tick after it is released. Invalid keys are ignored.
 * @param self The input; must not be NULL.
 * @param key The key.
 */
void GameInput_ConsumeKey(GameInput* self, GameKey key);

/**
 * @brief Returns the modifier keys held this tick, ignoring consumption (for chords and shortcuts).
 * @param self The input; must not be NULL.
 * @returns GAME_KEY_MODIFIER_* flags.
 */
uint8_t GameInput_GetHeldModifiers(const GameInput* self);


// Mouse, wheel and pointer.
/**
 * @brief Returns the mouse position in window pixels.
 * @param self The input; must not be NULL.
 * @returns The position.
 */
Vector2 GameInput_GetMousePosition(const GameInput* self);

/**
 * @brief Returns the mouse movement in pixels since the previous tick (also while the cursor is captured).
 * @param self The input; must not be NULL.
 * @returns The movement.
 */
Vector2 GameInput_GetMouseDelta(const GameInput* self);

/**
 * @brief Returns the window's size in pixels.
 * @param self The input; must not be NULL.
 * @returns The size; (0, 0) before the first sample.
 */
Vector2 GameInput_GetWindowSize(const GameInput* self);

/**
 * @brief Returns whether the cursor is hidden (captured for mouse look, or hidden with raylib's HideCursor), in which
 *        case there is no pointer for UI.
 * @param self The input; must not be NULL.
 * @returns true while hidden.
 */
bool GameInput_IsCursorCaptured(const GameInput* self);

/**
 * @brief Returns this tick's wheel movement, or (0, 0) once it is consumed.
 * @param self The input; must not be NULL.
 * @returns The wheel movement (x: horizontal, y: vertical).
 */
Vector2 GameInput_GetWheelDelta(const GameInput* self);

/**
 * @brief Consumes this tick's wheel movement.
 * @param self The input; must not be NULL.
 */
void GameInput_ConsumeWheel(GameInput* self);

/**
 * @brief Returns whether the pointer was claimed this tick (by UI it is over).
 * @param self The input; must not be NULL.
 * @returns true if claimed.
 */
bool GameInput_IsPointerConsumed(const GameInput* self);

/**
 * @brief Claims the pointer for this tick: it is over some UI, so the game should not react to where it points
 *        (hover effects, world picking).
 * @param self The input; must not be NULL.
 */
void GameInput_ConsumePointer(GameInput* self);


// Text.
/**
 * @brief Returns the number of codepoints typed this tick, or 0 once the text is consumed.
 * @param self The input; must not be NULL.
 * @returns The count.
 */
size_t GameInput_GetTextCodepointCount(const GameInput* self);

/**
 * @brief Returns one codepoint typed this tick.
 * @param self The input; must not be NULL.
 * @param index In [0, GameInput_GetTextCodepointCount).
 * @returns The codepoint, or 0 when @p index is out of range or the text is consumed.
 */
uint32_t GameInput_GetTextCodepoint(const GameInput* self, size_t index);

/**
 * @brief Consumes all text typed this tick.
 * @param self The input; must not be NULL.
 */
void GameInput_ConsumeText(GameInput* self);


// Actions.
/**
 * @brief Returns whether any binding of an action is held (key down and unconsumed, matching the held modifiers).
 * @param self The input; must not be NULL.
 * @param action The action.
 * @returns true if held; false for an invalid action.
 */
bool GameInput_IsActionDown(const GameInput* self, GameAction action);

/**
 * @brief Returns whether any binding of an action was pressed this tick (unconsumed, matching the held modifiers).
 * @param self The input; must not be NULL.
 * @param action The action.
 * @returns true if pressed; false for an invalid action.
 */
bool GameInput_IsActionPressed(const GameInput* self, GameAction action);

/**
 * @brief Returns whether any binding of an action was released this tick (unconsumed, matching the held modifiers).
 * @param self The input; must not be NULL.
 * @param action The action.
 * @returns true if released; false for an invalid action.
 */
bool GameInput_IsActionReleased(const GameInput* self, GameAction action);

/**
 * @brief Returns whether any binding of an action was pressed or auto-repeated this tick (unconsumed, matching the
 *        held modifiers).
 * @param self The input; must not be NULL.
 * @param action The action.
 * @returns true if pressed or repeated; false for an invalid action.
 */
bool GameInput_IsActionPressedOrRepeated(const GameInput* self, GameAction action);

/**
 * @brief Writes the keys of an action's bindings that are involved this tick: the key down, pressed or released
 *        (consumed or not) and the binding matching the held modifiers.
 *
 * These are the keys GameInput_ConsumeAction consumes. A consumer records them to follow its own keys with the raw
 * queries afterwards: UI navigation keeps repeating while a key it consumed auto-repeats.
 * @param self The input; must not be NULL.
 * @param action The action.
 * @param outKeys [out] Receives the keys (a key bound twice may appear twice); must have room for
 *        GAME_INPUT_MAX_BINDINGS_PER_ACTION keys.
 * @returns The number of keys written; 0 for an invalid action.
 */
size_t GameInput_GetActionKeys(const GameInput* self, GameAction action, GameKey outKeys[GAME_INPUT_MAX_BINDINGS_PER_ACTION]);

/**
 * @brief Consumes the keys of every binding of an action that is involved this tick (the keys
 *        GameInput_GetActionKeys reports). Invalid actions are ignored.
 * @param self The input; must not be NULL.
 * @param action The action.
 */
void GameInput_ConsumeAction(GameInput* self, GameAction action);

/**
 * @brief Returns the number of bindings an action has.
 * @param self The input; must not be NULL.
 * @param action The action.
 * @returns The count (0 for an unbound or invalid action).
 */
size_t GameInput_GetBindingCount(const GameInput* self, GameAction action);

/**
 * @brief Reads one binding of an action.
 * @param self The input; must not be NULL.
 * @param action The action.
 * @param index In [0, GameInput_GetBindingCount).
 * @param outBinding [out] Receives the binding; must not be NULL.
 * @returns true if @p action and @p index are valid.
 */
bool GameInput_TryGetBinding(const GameInput* self, GameAction action, size_t index, GameInputBinding* outBinding);

/**
 * @brief Replaces an action's bindings.
 * @param self The input; must not be NULL.
 * @param action The action.
 * @param bindings The new bindings; may be NULL only when @p bindingCount is 0 (which unbinds the action).
 * @param bindingCount The number of bindings; at most GAME_INPUT_MAX_BINDINGS_PER_ACTION.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL, @p action is invalid, @p bindings is NULL with a
 *          non-zero count, or a binding is invalid (see GameInputBinding_IsValid); ErrorCode_ArgumentOutOfRange if
 *          @p bindingCount is too large. Nothing changes on failure.
 */
Error GameInput_SetBindings(GameInput* self, GameAction action, const GameInputBinding* bindings, size_t bindingCount);

/**
 * @brief Restores every action's default bindings.
 * @param self The input; must not be NULL.
 */
void GameInput_ResetBindings(GameInput* self);

/**
 * @brief Loads bindings from a JSON controls file (references/controls_structure.md).
 *
 * Every action listed in the file replaces its bindings (an empty list unbinds it); actions the file does not list
 * keep their current bindings; names that are not actions are ignored. A UTF-8 byte order mark at the start is
 * skipped. All or nothing: on any error no binding changes.
 * @param self The input; must not be NULL.
 * @param path The file's UTF-8 path; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument; ErrorCode_FileNotFound /
 *          ErrorCode_DirectoryNotFound if the file does not exist; ErrorCode_InvalidJSON for malformed JSON or a bad
 *          entry (the message names the action and the binding); other read errors as reported.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error GameInput_LoadBindingsFromFile(GameInput* self, const unsigned char* path);

/**
 * @brief Writes every action's bindings to a JSON controls file, replacing it.
 * @param self The input; must not be NULL.
 * @param path The file's UTF-8 path; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument; otherwise the JSON or file error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error GameInput_SaveBindingsToFile(const GameInput* self, const unsigned char* path);


// Names.
/**
 * @brief Returns an action's stable id string, as stored in controls files ("move_forward").
 * @param action The action.
 * @returns A static NUL-terminated UTF-8 string, or NULL for an invalid action.
 */
const unsigned char* GameAction_GetIdString(GameAction action);

/**
 * @brief Returns an action's display name for a controls menu ("Move Forward"; English for now).
 * @param action The action.
 * @returns A static NUL-terminated UTF-8 string, or NULL for an invalid action.
 */
const unsigned char* GameAction_GetDisplayName(GameAction action);

/**
 * @brief Finds the action with the given id string (exact match).
 * @param idString The id string; NULL never matches.
 * @param outAction [out] Receives the action on success; left unchanged otherwise. Must not be NULL.
 * @returns true if an action has that id string.
 */
bool GameAction_TryParseIdString(const unsigned char* idString, GameAction* outAction);

/**
 * @brief Returns whether a binding has a real key and only known modifier flags.
 * @param binding The binding; must not be NULL.
 * @returns true if valid.
 */
bool GameInputBinding_IsValid(const GameInputBinding* binding);

/**
 * @brief Appends a binding's text form ("control+shift+s": modifiers in the order control, alt, shift, super, then
 *        the key id) to a byte buffer and NUL-terminates it (a trailing terminator already there is dropped first).
 * @param binding The binding; must not be NULL.
 * @param outText [out] A byte buffer (element size 1); must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument, a buffer that is not a byte buffer or an invalid
 *          binding; ErrorCode_BufferTooLarge if the buffer could not grow.
 */
Error GameInputBinding_AppendText(const GameInputBinding* binding, GenericBuffer* outText);

/**
 * @brief Parses a binding's text form: zero or more of "shift", "control", "alt", "super" (each at most once),
 *        then a key id, joined by '+'.
 * @param text The NUL-terminated text; NULL never parses.
 * @param outBinding [out] Receives the binding on success; left unchanged otherwise. Must not be NULL.
 * @returns true if @p text is a valid binding.
 */
bool GameInputBinding_TryParse(const unsigned char* text, GameInputBinding* outBinding);
