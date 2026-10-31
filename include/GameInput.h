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
#include "Logger.h"
#include "NamespacedKey.h"
#include "Registry.h"


/**
 * @file GameInput.h
 * @brief The game's input for one update tick, with consumption, and the action layer (registered actions bound to
 *        keys).
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
 * ACTIONS. Frames and the UI ask for actions ("tbp:move_forward", "tbp:ui_accept"), not keys, so controls can be
 * rebound. Every action is an entry of the input's action registry (GAME_INPUT_ACTION_REGISTRY_KEY): a namespaced key,
 * a display name, a description, a category of the category registry (GAME_INPUT_CATEGORY_REGISTRY_KEY, which groups
 * actions for the controls menu), default bindings, a kind and whether it always keeps a binding. A GameActionId is the
 * action's registry index: stable for the program's life, also while the action is withdrawn. The engine's own actions
 * and categories are registered at construction, in the order of the GameAction and GameActionCategory enums, so each
 * enum value is its id and C code writes GameAction_Jump with no lookup. Packs register theirs from input_actions
 * assets (InputActions.h), scripts through the same functions; they find their ids by key once and keep them.
 *
 * OVERRIDES. A source may change any registered action's display name, description or default bindings, and any
 * category's display name, description or order (GameInput_OverrideAction / GameInput_OverrideCategory): one override
 * per entry, undone when its source withdraws. The registry keeps what was registered; GameInput_TryGetActionInfo and
 * GameInput_TryGetCategoryInfo return the effective values.
 *
 * BINDINGS. Each action has up to GAME_INPUT_MAX_BINDINGS_PER_ACTION bindings. A binding is a key plus the modifiers
 * that must be held with it. Other held modifiers are ignored (W still walks while Shift sprints), unless another
 * binding on the same key asks for them: the most specific binding wins, so with Tab and Shift+Tab both bound,
 * Shift+Tab is only the Shift+Tab binding, whichever actions the player gave them. Consumption is per physical key: an
 * action bound to a key someone consumed reads as not pressed. An action follows its effective defaults until the
 * player sets its bindings; bindings the player set win over every default and override. An action that keeps a
 * binding is never left with none: emptying it restores its defaults. A key index (the bindings of the active actions
 * on each key) is rebuilt whenever bindings change, so a query only looks at the bindings on its own keys.
 *
 * THE CONTROLS FILE (references/controls_structure.md) holds what the player set: GameInput_SaveBindingsToFile writes
 * the actions whose bindings the player set, and the entries read for keys no action has now (a mod that is not
 * loaded), which apply when such an action registers.
 *
 * Construct in place with GameInput_Construct and release with GameInput_Deconstruct; never move or copy a constructed
 * input (the root registry holds pointers into it). It allocates when actions register, overrides apply and bindings
 * change, never per tick. Main thread only, like raylib's input.
 */


// Macros.
/** @brief Most text codepoints recorded per tick; more typed in one tick are dropped (raylib queues 16 per poll). */
#define GAME_INPUT_MAX_TEXT_CODEPOINTS ((size_t)32)
/** @brief Most bindings one action can have. */
#define GAME_INPUT_MAX_BINDINGS_PER_ACTION ((size_t)4)
/** @brief Longest binding text in bytes, without the terminator (e.g. "control+alt+shift+super+keypad_multiply"). */
#define GAME_INPUT_MAX_BINDING_TEXT_BYTES ((size_t)64)
/** @brief The action registry's key (in the root registry). */
#define GAME_INPUT_ACTION_REGISTRY_KEY ((const unsigned char*)u8"tbp:input_action")
/** @brief The action category registry's key (in the root registry). */
#define GAME_INPUT_CATEGORY_REGISTRY_KEY ((const unsigned char*)u8"tbp:input_category")
/** @brief The most actions the input holds, withdrawn ones included: what packs and scripts can add is bounded. */
#define GAME_INPUT_MAX_ACTIONS ((uint32_t)4096)
/** @brief The most action categories the input holds, withdrawn ones included. */
#define GAME_INPUT_MAX_CATEGORIES ((uint32_t)256)
/** @brief Longest display name of an action or a category, in bytes of UTF-8 without the terminator. */
#define GAME_INPUT_MAX_DISPLAY_NAME_BYTES ((size_t)64)
/** @brief Longest description of an action or a category, in bytes of UTF-8 without the terminator. */
#define GAME_INPUT_MAX_DESCRIPTION_BYTES ((size_t)512)
/** @brief The most entries a controls file may list. */
#define GAME_INPUT_MAX_CONTROLS_ENTRIES ((size_t)4096)

/** @brief Key state flag: the key is held. */
#define GAME_INPUT_KEY_STATE_DOWN ((uint8_t)0x01)
/** @brief Key state flag: the key went down this tick. */
#define GAME_INPUT_KEY_STATE_PRESSED ((uint8_t)0x02)
/** @brief Key state flag: the key went up this tick. */
#define GAME_INPUT_KEY_STATE_RELEASED ((uint8_t)0x04)
/** @brief Key state flag: the held key auto-repeated this tick (keyboard keys only; not set for the first press). */
#define GAME_INPUT_KEY_STATE_REPEATED ((uint8_t)0x08)


// Types.
/** @brief An action: its index in the input's action registry, stable for the program's life. */
typedef uint32_t GameActionId;

/** @brief An action category: its index in the input's category registry, stable for the program's life. */
typedef uint32_t GameActionCategoryId;

/**
 * @brief The engine's own actions: each value is the action's GameActionId (they are registered first, in this
 *        order).
 *
 * Pass a value wherever a GameActionId is taken. The keys are stored in controls files: never rename one.
 */
typedef enum GameActionEnum
{
    /** @brief UI navigation: move to the nearest widget above ("tbp:ui_up"; default Up). */
    GameAction_UIUp,
    /** @brief UI navigation: move to the nearest widget below ("tbp:ui_down"; default Down). */
    GameAction_UIDown,
    /** @brief UI navigation: move to the nearest widget to the left ("tbp:ui_left"; default Left). */
    GameAction_UILeft,
    /** @brief UI navigation: move to the nearest widget to the right ("tbp:ui_right"; default Right). */
    GameAction_UIRight,
    /** @brief UI navigation: the next widget in reading order ("tbp:ui_next"; default Tab). */
    GameAction_UINext,
    /** @brief UI navigation: the previous widget in reading order ("tbp:ui_previous"; default Shift+Tab). */
    GameAction_UIPrevious,
    /** @brief UI navigation: enter the focused container ("tbp:ui_accept"; default Enter, Keypad Enter). */
    GameAction_UIAccept,
    /** @brief UI navigation: leave the current container, or let go of the UI ("tbp:ui_back"; default Escape). */
    GameAction_UIBack,
    /** @brief Move forward ("tbp:move_forward"; default W). */
    GameAction_MoveForward,
    /** @brief Move backward ("tbp:move_back"; default S). */
    GameAction_MoveBack,
    /** @brief Move left ("tbp:move_left"; default A). */
    GameAction_MoveLeft,
    /** @brief Move right ("tbp:move_right"; default D). */
    GameAction_MoveRight,
    /** @brief Move up ("tbp:move_up"; default Space). */
    GameAction_MoveUp,
    /** @brief Move down ("tbp:move_down"; default Left Control). */
    GameAction_MoveDown,
    /** @brief Move faster while held ("tbp:sprint"; default Left Shift). */
    GameAction_Sprint,
    /** @brief Jump, when walking ("tbp:jump"; default Space, which "tbp:move_up" shares: the flycam and flight climb
     *  with it). */
    GameAction_Jump,
    /** @brief Crouch while held, when walking ("tbp:crouch"; default Left Control, which "tbp:move_down" shares). */
    GameAction_Crouch,
    /** @brief Use what the character looks at ("tbp:use"; default E). */
    GameAction_Use,
    /** @brief Pause / open the game menu ("tbp:pause"; default Escape; always keeps a binding). Read it after the UI
     *  had first refusal. */
    GameAction_Pause,

    /** @brief One past the last engine action; equals the number of engine actions. Not an action. */
    GameAction_Count
} GameAction;

/**
 * @brief The engine's own action categories: each value is the category's GameActionCategoryId (registered first, in
 *        this order).
 */
typedef enum GameActionCategoryEnum
{
    /** @brief "tbp:movement": moving and sprinting (order 100). */
    GameActionCategory_Movement,
    /** @brief "tbp:character": what a character does: jump, crouch, use (order 200). */
    GameActionCategory_Character,
    /** @brief "tbp:general": the game menu and other actions that work everywhere (order 300). */
    GameActionCategory_General,
    /** @brief "tbp:interface": moving through menus and other interfaces (order 400). */
    GameActionCategory_Interface,

    /** @brief One past the last engine category; equals their number. Not a category. */
    GameActionCategory_Count
} GameActionCategory;

/** @brief What kind of input an action is. Only buttons today: gamepad axes come with gamepads. */
typedef enum GameActionKindEnum
{
    /** @brief Pressed, held and released, from keys and buttons. */
    GameActionKind_Button,

    /** @brief One past the last kind. Not a kind. */
    GameActionKind_Count
} GameActionKind;

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
 * @brief An action's description: what GameInput_RegisterAction takes (the strings are copied) and what
 *        GameInput_TryGetActionInfo returns (with the overrides applied; the strings are then the input's).
 */
typedef struct GameActionInfoStruct
{
    /** @brief The name the controls menu shows: 1 to GAME_INPUT_MAX_DISPLAY_NAME_BYTES bytes of UTF-8 ("Move
     *  Forward"; English until string tables). */
    const unsigned char* DisplayName;
    /** @brief What the action does: at most GAME_INPUT_MAX_DESCRIPTION_BYTES bytes of UTF-8; may be empty, never
     *  NULL. */
    const unsigned char* Description;
    /** @brief The category it is listed under; active when the action registers. */
    GameActionCategoryId Category;
    /** @brief The bindings it has until the player sets its own; the first DefaultBindingCount are used. */
    GameInputBinding DefaultBindings[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    /** @brief How many default bindings: 0 to GAME_INPUT_MAX_BINDINGS_PER_ACTION. */
    size_t DefaultBindingCount;
    /** @brief What kind of input it is. */
    GameActionKind Kind;
    /** @brief Whether it always keeps a binding: the player can move it to other keys, never leave it with none (an
     *  empty list restores its defaults). Needs at least one default binding. */
    bool KeepsABinding;
} GameActionInfo;

/**
 * @brief An action category's description: what GameInput_RegisterCategory takes (the strings are copied) and what
 *        GameInput_TryGetCategoryInfo returns (with the override applied).
 */
typedef struct GameActionCategoryInfoStruct
{
    /** @brief The heading the controls menu shows: 1 to GAME_INPUT_MAX_DISPLAY_NAME_BYTES bytes of UTF-8. */
    const unsigned char* DisplayName;
    /** @brief What its actions are about: at most GAME_INPUT_MAX_DESCRIPTION_BYTES bytes of UTF-8; may be empty, never
     *  NULL. */
    const unsigned char* Description;
    /** @brief Where the controls menu lists it: by order, then by key. */
    int32_t Order;
} GameActionCategoryInfo;

/**
 * @brief What an override changes in an action (GameInput_OverrideAction): each part only when its flag is set, at
 *        least one. The strings are copied; the rules are the action's (GameActionInfo).
 */
typedef struct GameActionOverrideStruct
{
    /** @brief Whether DisplayName replaces the action's display name. */
    bool OverridesDisplayName;
    /** @brief The new display name (read only when OverridesDisplayName). */
    const unsigned char* DisplayName;
    /** @brief Whether Description replaces the action's description. */
    bool OverridesDescription;
    /** @brief The new description (read only when OverridesDescription). */
    const unsigned char* Description;
    /** @brief Whether DefaultBindings replace the action's defaults (they reach the action unless the player set its
     *  bindings). */
    bool OverridesDefaultBindings;
    /** @brief The new defaults (read only when OverridesDefaultBindings); the first DefaultBindingCount are used. */
    GameInputBinding DefaultBindings[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    /** @brief How many new defaults; at least 1 for an action that keeps a binding. */
    size_t DefaultBindingCount;
} GameActionOverride;

/**
 * @brief What an override changes in a category (GameInput_OverrideCategory): each part only when its flag is set, at
 *        least one.
 */
typedef struct GameActionCategoryOverrideStruct
{
    /** @brief Whether DisplayName replaces the category's display name. */
    bool OverridesDisplayName;
    /** @brief The new display name (read only when OverridesDisplayName). */
    const unsigned char* DisplayName;
    /** @brief Whether Description replaces the category's description. */
    bool OverridesDescription;
    /** @brief The new description (read only when OverridesDescription). */
    const unsigned char* Description;
    /** @brief Whether Order replaces the category's order. */
    bool OverridesOrder;
    /** @brief The new order (read only when OverridesOrder). */
    int32_t Order;
} GameActionCategoryOverride;

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
 * @brief The current tick's input plus consumption, the action and category registries, and the bindings.
 *
 * All fields are internal; use the GameInput_* functions. Construct in place and never move it.
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
    /** @brief The borrowed key registry every key and source is an id of. */
    KeyRegistry* _keys;
    /** @brief The borrowed root registry the two registries are added to, or NULL. */
    RegistryRoot* _root;
    /** @brief The borrowed logger (conflicts, restored bindings), or NULL to log nothing. */
    Logger* _logger;
    /** @brief The actions as registered (the input's own records with owned strings; read them through the
     *  GameInput functions, which apply the overrides). */
    Registry _actions;
    /** @brief The categories as registered (the input's own records, like the actions'). */
    Registry _categories;
    /** @brief Per action id: its bindings, whether the player set them, and its override. */
    GenericBuffer _actionStates;
    /** @brief Per category id: its override. */
    GenericBuffer _categoryStates;
    /** @brief The key index: the entries of key k are [_keyIndexStarts[k], _keyIndexStarts[k + 1]) of _keyIndex. */
    uint32_t _keyIndexStarts[GameKey_Count + 1];
    /** @brief The key index's entries (the bindings of the active actions), grouped by key. */
    GenericBuffer _keyIndex;
    /** @brief The controls file's entries for keys no action has now, in byte order of their keys. */
    GenericBuffer _keptEntries;
} GameInput;


// Lifecycle.
/**
 * @brief Initializes the input in place: no keys held, the two registries with the engine's categories and actions
 *        (each enum value is its id), every action at its defaults.
 * @param self The input; must not be NULL. Never move it afterwards.
 * @param keys The program's key registry, borrowed: every action's and category's key and every source is one of its
 *        ids. Must not be NULL and must outlive the input.
 * @param root The root registry to add the action and category registries to (they are withdrawn from it again by
 *        GameInput_Deconstruct), or NULL. Must borrow @p keys.
 * @param logger The logger for default conflicts and restored bindings, borrowed, or NULL to log nothing.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p keys is NULL. Nothing is left to release on failure.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error GameInput_Construct(GameInput* self, KeyRegistry* keys, RegistryRoot* root, Logger* logger);

/**
 * @brief Withdraws the registries from the root and releases everything the input owns (the struct is zeroed). Safe
 *        on NULL.
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


// Registration.
/**
 * @brief Registers an action category, or revives a withdrawn one under the same key (with the same id).
 * @param self The input; must not be NULL.
 * @param keyId The category's key: an id of the input's key registry.
 * @param info The category; must not be NULL. Its strings are copied.
 * @param sourceKeyId Who registers it: NAMESPACED_KEY_ID_NONE for the engine, otherwise a key id (a pack's input
 *        actions definition, a story).
 * @param outId [out] Receives the category's id; may be NULL. Unchanged on failure.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument, a NULL string, an empty or overlong display name,
 *          an overlong description or text that is not UTF-8; ErrorCode_ArgumentOutOfRange if @p keyId or
 *          @p sourceKeyId is not a key of the key registry; ErrorCode_InvalidOperation if the key is registered and
 *          active (the message names both sources) or GAME_INPUT_MAX_CATEGORIES categories exist. Nothing changes on
 *          failure.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error GameInput_RegisterCategory(GameInput* self, uint32_t keyId, const GameActionCategoryInfo* info,
    uint32_t sourceKeyId, GameActionCategoryId* outId);

/**
 * @brief Registers an action, or revives a withdrawn one under the same key (with the same id, and its bindings when
 *        the player set them).
 *
 * A new action takes the bindings of a kept controls-file entry for its key (as set by the player), else its effective
 * defaults (an override still on its slot, else @p info's). When a source other than the engine registers it, each
 * default binding that is on a key another active action's binding uses is logged once, naming both actions; the
 * binding stays.
 * @param self The input; must not be NULL.
 * @param keyId The action's key: an id of the input's key registry.
 * @param info The action; must not be NULL. Its strings are copied.
 * @param sourceKeyId Who registers it: NAMESPACED_KEY_ID_NONE for the engine, otherwise a key id.
 * @param outId [out] Receives the action's id; may be NULL. Unchanged on failure.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument, a NULL string, an empty or overlong display name,
 *          an overlong description, text that is not UTF-8, an invalid default binding, an unknown kind or
 *          KeepsABinding without a default binding; ErrorCode_ArgumentOutOfRange for more than
 *          GAME_INPUT_MAX_BINDINGS_PER_ACTION defaults, or if @p keyId or @p sourceKeyId is not a key of the key
 *          registry; ErrorCode_InvalidOperation if the category is not active, the key is registered and active (the
 *          message names both sources) or GAME_INPUT_MAX_ACTIONS actions exist. Nothing changes on failure.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error GameInput_RegisterAction(GameInput* self, uint32_t keyId, const GameActionInfo* info, uint32_t sourceKeyId,
    GameActionId* outId);

/**
 * @brief Overrides an active action's display name, description or default bindings for a source.
 *
 * One override per action: the source that holds it replaces it, another source is refused. New defaults reach the
 * action at once unless the player set its bindings, and are checked for conflicts like a registration's (logged
 * once). The override stays on the action (also while it is withdrawn) until its source withdraws.
 * @param self The input; must not be NULL.
 * @param id The action.
 * @param override What changes; must not be NULL, and must change at least one thing.
 * @param sourceKeyId Who overrides: NAMESPACED_KEY_ID_NONE for the engine, otherwise a key id.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument, an override that changes nothing, or a part that
 *          breaks the action's rules (GameInput_RegisterAction); ErrorCode_ArgumentOutOfRange if @p sourceKeyId is not
 *          a key of the key registry or there are too many defaults; ErrorCode_InvalidOperation if the action is not
 *          active, another source overrides it (the message names both), or the defaults would be empty for an action
 *          that keeps a binding. Nothing changes on failure.
 */
Error GameInput_OverrideAction(GameInput* self, GameActionId id, const GameActionOverride* override,
    uint32_t sourceKeyId);

/**
 * @brief Overrides an active category's display name, description or order for a source (one override per category,
 *        as GameInput_OverrideAction).
 * @param self The input; must not be NULL.
 * @param id The category.
 * @param override What changes; must not be NULL, and must change at least one thing.
 * @param sourceKeyId Who overrides: NAMESPACED_KEY_ID_NONE for the engine, otherwise a key id.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument, an override that changes nothing, or a string that
 *          breaks the category's rules; ErrorCode_ArgumentOutOfRange if @p sourceKeyId is not a key of the key
 *          registry; ErrorCode_InvalidOperation if the category is not active or another source overrides it. Nothing
 *          changes on failure.
 */
Error GameInput_OverrideCategory(GameInput* self, GameActionCategoryId id, const GameActionCategoryOverride* override,
    uint32_t sourceKeyId);

/**
 * @brief Withdraws everything a source registered: its overrides, then its actions, then its categories.
 *
 * Actions that followed a withdrawn override's defaults return to their registered defaults. A withdrawn action reads
 * as not pressed and keeps its slot (its bindings, whether the player set them, other sources' overrides) for when it
 * revives; its player-set bindings are still saved. Actions of other sources in a withdrawn category stay registered.
 * @param self The input; NULL does nothing.
 * @param sourceKeyId The source: NAMESPACED_KEY_ID_NONE for the engine's own entries.
 * @returns How many actions, categories and overrides were withdrawn.
 */
uint32_t GameInput_WithdrawSource(GameInput* self, uint32_t sourceKeyId);


// Actions and categories.
/**
 * @brief Finds the active action with a key.
 * @param self The input; must not be NULL.
 * @param keyId The key id.
 * @param outId [out] Receives the id when found; must not be NULL. Unchanged otherwise.
 * @returns true if an active action has the key.
 */
bool GameInput_FindAction(GameInput* self, uint32_t keyId, GameActionId* outId);

/**
 * @brief Finds the active action with a key's full text ("tbp:jump"), without interning it.
 * @param self The input; must not be NULL.
 * @param key The full key (NUL-terminated); a key as people write it, perhaps without its namespace, goes through
 *        NamespacedKey_Parse first. Must not be NULL.
 * @param outId [out] Receives the id when found; must not be NULL. Unchanged otherwise.
 * @returns true if an active action has the key.
 */
bool GameInput_FindActionText(GameInput* self, const unsigned char* key, GameActionId* outId);

/**
 * @brief Appends the ids of the active actions whose keys start with @p prefix, in byte order of their keys (for
 *        autocomplete: "mymod:" lists a mod's actions, the empty prefix every one).
 * @param self The input; must not be NULL.
 * @param prefix The prefix (NUL-terminated); must not be NULL.
 * @param outIds A buffer of GameActionId to append to (not cleared); must not be NULL.
 * @returns As Registry_FindByPrefix.
 */
Error GameInput_FindActionsByPrefix(const GameInput* self, const unsigned char* prefix, GenericBuffer* outIds);

/**
 * @brief Returns how many action ids exist, withdrawn ones included: the ids are 0 to this count - 1.
 * @param self The input; must not be NULL.
 * @returns The count.
 */
uint32_t GameInput_GetActionCount(const GameInput* self);

/**
 * @brief Reports whether an action is registered now.
 * @param self The input; must not be NULL.
 * @param id The action.
 * @returns true for an active action; false for a withdrawn one and an unknown id.
 */
bool GameInput_IsActionActive(const GameInput* self, GameActionId id);

/**
 * @brief Reads an active action's effective description (its registration with its override applied).
 * @param self The input; must not be NULL.
 * @param id The action.
 * @param outInfo [out] Receives the description; must not be NULL. Its strings are the input's, valid until the next
 *        registration, override or withdrawal. Unchanged on failure.
 * @returns true for an active action.
 */
bool GameInput_TryGetActionInfo(const GameInput* self, GameActionId id, GameActionInfo* outInfo);

/**
 * @brief Returns an action's key id, also while it is withdrawn.
 * @param self The input; must not be NULL.
 * @param id The action.
 * @returns The key id; NAMESPACED_KEY_ID_NONE for an unknown id.
 */
uint32_t GameInput_GetActionKey(const GameInput* self, GameActionId id);

/**
 * @brief Returns an action's full key text ("tbp:jump"), also while it is withdrawn.
 * @param self The input; must not be NULL.
 * @param id The action.
 * @returns The key (owned by the key registry); NULL for an unknown id.
 */
const unsigned char* GameInput_GetActionKeyText(const GameInput* self, GameActionId id);

/**
 * @brief Returns who registered an action last (also while it is withdrawn).
 * @param self The input; must not be NULL.
 * @param id The action; check it with GameInput_GetActionCount first, as the engine's source is NAMESPACED_KEY_ID_NONE.
 * @returns The source's key id: NAMESPACED_KEY_ID_NONE for the engine and for an unknown id.
 */
uint32_t GameInput_GetActionSource(const GameInput* self, GameActionId id);

/**
 * @brief Reports whether an action has an override, and whose.
 * @param self The input; must not be NULL.
 * @param id The action.
 * @param outSourceKeyId [out] Receives the override's source when there is one; must not be NULL. Unchanged
 *        otherwise.
 * @returns true if the action (active or withdrawn) has an override.
 */
bool GameInput_TryGetActionOverrideSource(const GameInput* self, GameActionId id, uint32_t* outSourceKeyId);

/**
 * @brief Finds the active category with a key.
 * @param self The input; must not be NULL.
 * @param keyId The key id.
 * @param outId [out] Receives the id when found; must not be NULL. Unchanged otherwise.
 * @returns true if an active category has the key.
 */
bool GameInput_FindCategory(GameInput* self, uint32_t keyId, GameActionCategoryId* outId);

/**
 * @brief Finds the active category with a key's full text ("tbp:movement"), without interning it.
 * @param self The input; must not be NULL.
 * @param key The full key (NUL-terminated); must not be NULL.
 * @param outId [out] Receives the id when found; must not be NULL. Unchanged otherwise.
 * @returns true if an active category has the key.
 */
bool GameInput_FindCategoryText(GameInput* self, const unsigned char* key, GameActionCategoryId* outId);

/**
 * @brief Appends the ids of the active categories whose keys start with @p prefix, in byte order of their keys.
 * @param self The input; must not be NULL.
 * @param prefix The prefix (NUL-terminated); must not be NULL.
 * @param outIds A buffer of GameActionCategoryId to append to (not cleared); must not be NULL.
 * @returns As Registry_FindByPrefix.
 */
Error GameInput_FindCategoriesByPrefix(const GameInput* self, const unsigned char* prefix, GenericBuffer* outIds);

/**
 * @brief Returns how many category ids exist, withdrawn ones included.
 * @param self The input; must not be NULL.
 * @returns The count.
 */
uint32_t GameInput_GetCategoryCount(const GameInput* self);

/**
 * @brief Reports whether a category is registered now.
 * @param self The input; must not be NULL.
 * @param id The category.
 * @returns true for an active category.
 */
bool GameInput_IsCategoryActive(const GameInput* self, GameActionCategoryId id);

/**
 * @brief Reads an active category's effective description (its registration with its override applied).
 * @param self The input; must not be NULL.
 * @param id The category.
 * @param outInfo [out] Receives the description; must not be NULL. Its strings are the input's, valid until the next
 *        registration, override or withdrawal. Unchanged on failure.
 * @returns true for an active category.
 */
bool GameInput_TryGetCategoryInfo(const GameInput* self, GameActionCategoryId id, GameActionCategoryInfo* outInfo);

/**
 * @brief Returns a category's key id, also while it is withdrawn.
 * @param self The input; must not be NULL.
 * @param id The category.
 * @returns The key id; NAMESPACED_KEY_ID_NONE for an unknown id.
 */
uint32_t GameInput_GetCategoryKey(const GameInput* self, GameActionCategoryId id);

/**
 * @brief Returns a category's full key text, also while it is withdrawn.
 * @param self The input; must not be NULL.
 * @param id The category.
 * @returns The key (owned by the key registry); NULL for an unknown id.
 */
const unsigned char* GameInput_GetCategoryKeyText(const GameInput* self, GameActionCategoryId id);

/**
 * @brief Returns who registered a category last (also while it is withdrawn).
 * @param self The input; must not be NULL.
 * @param id The category.
 * @returns The source's key id: NAMESPACED_KEY_ID_NONE for the engine and for an unknown id.
 */
uint32_t GameInput_GetCategorySource(const GameInput* self, GameActionCategoryId id);

/**
 * @brief Reports whether a category has an override, and whose.
 * @param self The input; must not be NULL.
 * @param id The category.
 * @param outSourceKeyId [out] Receives the override's source when there is one; must not be NULL. Unchanged
 *        otherwise.
 * @returns true if the category (active or withdrawn) has an override.
 */
bool GameInput_TryGetCategoryOverrideSource(const GameInput* self, GameActionCategoryId id, uint32_t* outSourceKeyId);

/**
 * @brief Returns the key registry the input borrows: every key and source it takes is an id of it.
 * @param self The input; must not be NULL.
 * @returns The key registry.
 */
KeyRegistry* GameInput_GetKeyRegistry(const GameInput* self);

/**
 * @brief Describes what is wrong with an action's or a category's display name, for a reader's messages.
 * @param text The NUL-terminated text, or NULL.
 * @returns NULL for a valid display name (1 to GAME_INPUT_MAX_DISPLAY_NAME_BYTES bytes of UTF-8 without control
 *          characters); otherwise a static description such as "is too long".
 */
const char* GameInput_DescribeDisplayNameProblem(const unsigned char* text);

/**
 * @brief Describes what is wrong with an action's or a category's description, for a reader's messages.
 * @param text The NUL-terminated text, or NULL.
 * @returns NULL for a valid description (at most GAME_INPUT_MAX_DESCRIPTION_BYTES bytes of UTF-8 without control
 *          characters; may be empty); otherwise a static description.
 */
const char* GameInput_DescribeDescriptionProblem(const unsigned char* text);

/**
 * @brief Logs every action by category, in the controls menu's order (categories by order then key, actions by key):
 *        each with its key, display name, bindings, whether the player set them, and its source and override's source
 *        when they are not the engine's; withdrawn actions and categories are left out; actions whose category is
 *        withdrawn come last. For the test frame's status key. Nothing is logged without a logger.
 * @param self The input; must not be NULL.
 */
void GameInput_LogActions(const GameInput* self);


// Action queries.
/**
 * @brief Returns whether any binding of an action is held (key down and unconsumed, matching the held modifiers).
 * @param self The input; must not be NULL.
 * @param id The action.
 * @returns true if held; false for an unknown or withdrawn action.
 */
bool GameInput_IsActionDown(const GameInput* self, GameActionId id);

/**
 * @brief Returns whether any binding of an action was pressed this tick (unconsumed, matching the held modifiers).
 * @param self The input; must not be NULL.
 * @param id The action.
 * @returns true if pressed; false for an unknown or withdrawn action.
 */
bool GameInput_IsActionPressed(const GameInput* self, GameActionId id);

/**
 * @brief Returns whether any binding of an action was released this tick (unconsumed, matching the held modifiers).
 * @param self The input; must not be NULL.
 * @param id The action.
 * @returns true if released; false for an unknown or withdrawn action.
 */
bool GameInput_IsActionReleased(const GameInput* self, GameActionId id);

/**
 * @brief Returns whether any binding of an action was pressed or auto-repeated this tick (unconsumed, matching the
 *        held modifiers).
 * @param self The input; must not be NULL.
 * @param id The action.
 * @returns true if pressed or repeated; false for an unknown or withdrawn action.
 */
bool GameInput_IsActionPressedOrRepeated(const GameInput* self, GameActionId id);

/**
 * @brief Writes the keys of an action's bindings that are involved this tick: the key down, pressed or released
 *        (consumed or not) and the binding matching the held modifiers.
 *
 * These are the keys GameInput_ConsumeAction consumes. A consumer records them to follow its own keys with the raw
 * queries afterwards: UI navigation keeps repeating while a key it consumed auto-repeats.
 * @param self The input; must not be NULL.
 * @param id The action.
 * @param outKeys [out] Receives the keys (a key bound twice may appear twice); must have room for
 *        GAME_INPUT_MAX_BINDINGS_PER_ACTION keys.
 * @returns The number of keys written; 0 for an unknown or withdrawn action.
 */
size_t GameInput_GetActionKeys(const GameInput* self, GameActionId id,
    GameKey outKeys[GAME_INPUT_MAX_BINDINGS_PER_ACTION]);

/**
 * @brief Consumes the keys of every binding of an action that is involved this tick (the keys
 *        GameInput_GetActionKeys reports). Unknown and withdrawn actions are ignored.
 * @param self The input; must not be NULL.
 * @param id The action.
 */
void GameInput_ConsumeAction(GameInput* self, GameActionId id);


// Bindings.
/**
 * @brief Returns the number of bindings an action has (also while it is withdrawn).
 * @param self The input; must not be NULL.
 * @param id The action.
 * @returns The count (0 for an unbound action and an unknown id).
 */
size_t GameInput_GetBindingCount(const GameInput* self, GameActionId id);

/**
 * @brief Reads one binding of an action (also while it is withdrawn).
 * @param self The input; must not be NULL.
 * @param id The action.
 * @param index In [0, GameInput_GetBindingCount).
 * @param outBinding [out] Receives the binding; must not be NULL.
 * @returns true if @p id and @p index are valid.
 */
bool GameInput_TryGetBinding(const GameInput* self, GameActionId id, size_t index, GameInputBinding* outBinding);

/**
 * @brief Reports whether the player set an action's bindings (so they win over its defaults and are saved).
 * @param self The input; must not be NULL.
 * @param id The action.
 * @returns true if set by the player; false while the action follows its defaults, and for an unknown id.
 */
bool GameInput_IsSetByPlayer(const GameInput* self, GameActionId id);

/**
 * @brief Replaces an active action's bindings, as set by the player.
 *
 * An empty list unbinds the action, except for one that keeps a binding: its defaults are restored instead (it
 * follows its defaults again), logged once.
 * @param self The input; must not be NULL.
 * @param id The action.
 * @param bindings The new bindings; may be NULL only when @p bindingCount is 0.
 * @param bindingCount The number of bindings; at most GAME_INPUT_MAX_BINDINGS_PER_ACTION.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL, @p bindings is NULL with a non-zero count, or a
 *          binding is invalid (see GameInputBinding_IsValid); ErrorCode_ArgumentOutOfRange if @p bindingCount is too
 *          large; ErrorCode_InvalidOperation if the action is unknown or withdrawn. Nothing changes on failure.
 */
Error GameInput_SetBindings(GameInput* self, GameActionId id, const GameInputBinding* bindings, size_t bindingCount);

/**
 * @brief Returns an active action to its effective defaults (it follows them again; the controls menu's per-action
 *        reset).
 * @param self The input; must not be NULL.
 * @param id The action.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if the action is unknown
 *          or withdrawn.
 */
Error GameInput_ResetActionBindings(GameInput* self, GameActionId id);

/**
 * @brief Returns every action to its effective defaults and forgets the kept controls-file entries (nothing is set by
 *        the player afterwards).
 * @param self The input; must not be NULL.
 */
void GameInput_ResetBindings(GameInput* self);

/**
 * @brief Returns how many bindings of active actions are on a key (whatever their modifiers): the key index's
 *        entries for it, which a query on the key looks at.
 * @param self The input; must not be NULL.
 * @param key The key.
 * @returns The count; 0 for an invalid key.
 */
size_t GameInput_GetKeyBindingCount(const GameInput* self, GameKey key);

/**
 * @brief Reads one binding on a key (for the controls menu's conflicts), in no particular order.
 * @param self The input; must not be NULL.
 * @param key The key.
 * @param index In [0, GameInput_GetKeyBindingCount).
 * @param outId [out] Receives the action; must not be NULL.
 * @param outBinding [out] Receives the binding; must not be NULL.
 * @returns true if @p key and @p index are valid.
 */
bool GameInput_TryGetKeyBinding(const GameInput* self, GameKey key, size_t index, GameActionId* outId,
    GameInputBinding* outBinding);


// The controls file.
/**
 * @brief Loads the player's bindings from a JSON controls file (references/controls_structure.md).
 *
 * Every entry the file lists sets its action's bindings as the player's (an empty list unbinds it, except for an
 * action that keeps a binding, which returns to its defaults, logged once); actions the file does not list keep
 * theirs. A key without a namespace is the default namespace's. An entry for a withdrawn action applies to it for when
 * it revives; an entry for a key no action has is kept (and replaces a kept entry for the same key) and applies when
 * such an action registers. A UTF-8 byte order mark at the start is skipped. All or nothing: on any error no binding
 * and no kept entry changes.
 * @param self The input; must not be NULL.
 * @param path The file's UTF-8 path; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument; ErrorCode_FileNotFound /
 *          ErrorCode_DirectoryNotFound if the file does not exist; ErrorCode_InvalidJSON for malformed JSON or a bad
 *          entry (an invalid key, a key listed twice, a bad binding list; the message names the key and the binding)
 *          or more than GAME_INPUT_MAX_CONTROLS_ENTRIES entries; other read errors as reported.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error GameInput_LoadBindingsFromFile(GameInput* self, const unsigned char* path);

/**
 * @brief Writes what the player set to a JSON controls file, replacing it: the bindings of every action the player
 *        set (active or withdrawn) and the kept entries, by key in full, in byte order of the keys.
 * @param self The input; must not be NULL.
 * @param path The file's UTF-8 path; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument; otherwise the JSON or file error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error GameInput_SaveBindingsToFile(const GameInput* self, const unsigned char* path);


// Bindings' text.
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
