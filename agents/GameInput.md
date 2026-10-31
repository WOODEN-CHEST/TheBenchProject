# GameKey and GameInput modules

Files: `include/GameKey.h`, `source/GameKey.c`, `include/GameInput.h`, `source/GameInput.c`, the private
`source/GameKeyInternal.h` (the raylib code behind each key; only the input layer includes it) and
`source/GameInputInternal.h` (a tests-only counter); the `input_actions` asset type in `include/InputActions.h`,
`source/InputActions.c` and `source/InputActionsDefinition.c`.

## Purpose

All game input goes through one per-tick snapshot with **consumption**, so the UI and the game can share input without
both reacting to the same key or click, and through **actions**, so controls can be rebound. Nothing above the input
layer calls raylib input functions (`IsKeyPressed`, `GetMouseDelta`, `GetCharPressed`, ...) or uses raylib key codes.

## GameKey: the identity of every key

`GameKey` is the game-wide key enum: keyboard keys, then the mouse buttons (`GameKey_MouseLeft` ...
`GameKey_MouseBack`); gamepad buttons will be appended after them (`// TODO:` in the enum), so never assume the mouse
buttons are last (use `GameKey_IsMouseButton` / `GameKey_IsKeyboardKey`). `GameKey_Unknown` and `GameKey_Count` are
not keys (`GameKey_IsValid`).

Each key has:
- a **stable id string** (`GameKey_GetIdString`: `"w"`, `"left_shift"`, `"keypad_add"`, `"mouse_left"`), which controls
  files store: never rename one. `GameKey_TryParseIdString` finds a key by it (a linear search: load time only);
- a **display name** (`GameKey_GetDisplayName`: `"W"`, `"Left Shift"`, `"Left Mouse"`), English until string tables
  exist;
- a modifier flag for the modifier keys (`GameKey_GetModifierFlag`: `GAME_KEY_MODIFIER_SHIFT` for either Shift, and
  `CONTROL`, `ALT`, `SUPER`).

An **icon** per key (a sprite referenced by name, so text components can inline "Press [icon] to jump") comes with the
controls menu, which is not built yet. Everything lives in one table in `GameKey.c` indexed by `GameKey` (designated
initializers, so the enum order cannot drift from the table).

## GameInput: the tick's input

```c
GameInput Input;                                           // constructed in place; never moved afterwards
GameInput_Construct(&Input, &Keys, &Registries, &Log);     // the engine's actions at their defaults
InputActions_RegisterDefinitions(Assets, &Input, &Log);    // the packs' actions (input_actions assets)
GameInput_LoadBindingsFromFile(&Input, path);              // optional: the player's controls over the defaults
// once per update tick, right after PollInputEvents (GameFrameManager_Update does this):
GameInput_Poll(&Input);                                    // = GameInput_SampleDevices + GameInput_BeginTick
```

The input borrows the program's key registry (every action's and category's key and every source is one of its ids)
and adds its two registries to the root registry (`Services.Registries`); tests pass a key registry of their own and
NULL for the root and the logger.

`GameInput_SampleDevices` reads every key's state (down / pressed / released / auto-repeated), the mouse position and
movement, the wheel, the window size, whether the cursor is hidden (`IsCursorHidden`: captured for mouse look, or hidden
with `HideCursor`; either way there is no pointer for the UI), and drains raylib's typed-text queue once (so no
reader can starve another). Tests build a `GameInputSample` themselves (`GameInputSample_Clear`, then set fields) and
call `GameInput_BeginTick` directly: everything but the sampling runs headless.

Reading keys: `GameInput_IsKeyDown` / `IsKeyPressed` / `IsKeyReleased` / `IsKeyPressedOrRepeated`, or the raw flags
(`GameInput_GetKeyState`, `GAME_INPUT_KEY_STATE_*`). Held modifiers: `GameInput_GetHeldModifiers` (ignores
consumption). Mouse: `GameInput_GetMousePosition` (window pixels), `GetMouseDelta`, `GetWindowSize`,
`IsCursorCaptured`. Wheel: `GameInput_GetWheelDelta`. Text: `GameInput_GetTextCodepointCount` /
`GameInput_GetTextCodepoint`.

### Consumption

Whoever handles input consumes it, and every later reader in the tick sees it as not pressed:

| What | Consume with | How long |
|---|---|---|
| A key or mouse button | `GameInput_ConsumeKey` | while held, and on the tick of its release; a new press starts unconsumed |
| An action's keys | `GameInput_ConsumeAction` | as keys (every binding of the action active this tick) |
| The wheel | `GameInput_ConsumeWheel` | this tick |
| Typed text | `GameInput_ConsumeText` | this tick (all of it) |
| The pointer ("it is over UI") | `GameInput_ConsumePointer` | this tick |

Key consumption is sticky so a key typed into a text box never also walks the player forward on the following ticks.
The raw queries (`GameInput_GetRawKeyState`) ignore consumption: the consumer of a press uses them to follow its own
release (the UI screen does this for mouse buttons and for keys a widget handled).

Who reads first is decided by update order: the frame manager updates frames **top-down** (highest z first), and a
frame that hosts UI updates its `UIScreen` before its game logic. So a menu frame over the game frame, and the UI inside
a frame, always get first refusal.

### Actions

Every action is an entry of the input's action registry (`tbp:input_action`, [Registry.md](Registry.md)): a namespaced
key, a display name (at most 64 bytes of UTF-8, English until string tables), a description (at most 512 bytes), a
category of the category registry (`tbp:input_category`: a display name, a description and an order, which group
actions for the controls menu), up to `GAME_INPUT_MAX_BINDINGS_PER_ACTION` (4) default bindings, a kind (only
`GameActionKind_Button` until gamepads) and whether it **always keeps a binding**. A `GameActionId` is the action's
registry index: stable for the program's life, also while the action is withdrawn, and the same action again when it
revives under its key. Names and descriptions may hold no control characters.

**The engine's actions** are the `GameAction` enum, registered at construction in enum order, so each value is its id
and C code passes `GameAction_Jump` wherever a `GameActionId` is taken (no lookup, no misspelling). Their categories are
the `GameActionCategory` enum, the same way:

| Category (order) | Action | Key | Default |
|---|---|---|---|
| Movement (100) | Move Forward / Back / Left / Right / Up / Down | `tbp:move_forward`, `tbp:move_back`, `tbp:move_left`, `tbp:move_right`, `tbp:move_up`, `tbp:move_down` | W, S, A, D, Space, Left Control |
| | Sprint | `tbp:sprint` | Left Shift |
| Character (200) | Jump, Crouch | `tbp:jump`, `tbp:crouch` | Space, Left Control (shared with `move_up` and `move_down`: walking reads these, the flycam and flight those) |
| | Use | `tbp:use` | E (use what the character looks at) |
| General (300) | Pause | `tbp:pause` | Escape; always keeps a binding |
| Interface (400) | Menu Up / Down / Left / Right | `tbp:ui_up`, `tbp:ui_down`, `tbp:ui_left`, `tbp:ui_right` | arrow keys |
| | Next, Previous | `tbp:ui_next`, `tbp:ui_previous` | Tab, Shift+Tab |
| | Accept | `tbp:ui_accept` | Enter, Keypad Enter |
| | Back | `tbp:ui_back` | Escape |

Adding an engine action: an enum value and its row in `BuiltInActions` (`GameInput.c`: key, display name, description,
category, defaults, the keeps-a-binding flag); a new engine category likewise in `GameActionCategory` and
`BuiltInCategories`. Never rename a key (controls files store them).

**Other actions** register at runtime: `GameInput_RegisterCategory` and `GameInput_RegisterAction` take the key id, the
description (strings copied) and the **source** (`NAMESPACED_KEY_ID_NONE` for the engine, otherwise a key: a pack's
definition, a story). A key that is active refuses, naming both sources (no silent replacement); every field is
checked. Packs declare theirs in `input_actions` assets (below); scripts will use the same functions. A reader finds an
action's id once (`GameInput_FindAction` / `FindActionText`) and keeps it. The other lookups: `GameInput_IsActionActive`,
`TryGetActionInfo` (the effective values), `GetActionKey` / `GetActionKeyText`, `GetActionSource`,
`TryGetActionOverrideSource`, `GetActionCount` (ids, withdrawn ones included), `FindActionsByPrefix` (autocomplete, byte
order), and the same for categories. `GameInput_LogActions` logs every action by category in menu order (the test
frame's I key).

**Overrides.** `GameInput_OverrideAction` changes an active action's display name, description or default bindings for
a source, `GameInput_OverrideCategory` a category's display name, description or order; each part only when its
`Overrides*` flag is set, at least one. One override per entry: another source's is refused (naming both), the source
that holds it replaces its own. Overrides live beside the registry, so the registry keeps what was registered and
`TryGetActionInfo` returns the values with the override applied. New defaults reach an action at once unless the
player set its bindings. Nobody may empty the defaults of an action that keeps a binding, and only the action's own
registration sets that flag.

**Withdrawing.** `GameInput_WithdrawSource` withdraws a source's overrides, then its actions, then its categories (the
packs round calls it when a pack unloads). Actions that followed a withdrawn override return to their registered
defaults. A withdrawn action reads as not pressed, leaves the key index and keeps its slot: its bindings, whether the
player set them, other sources' overrides; its player-set bindings are still saved, and when it revives it keeps them
(an action the player never set takes its new registration's defaults). An action whose category is withdrawn stays
registered (the listing shows it under "Without a category").

**Queries:** `GameInput_IsActionDown` / `IsActionPressed` / `IsActionReleased` / `IsActionPressedOrRepeated`; an unknown
or withdrawn id reads as not pressed. A binding is a key plus the modifiers that must be held with it
(`GameInputBinding`). Other held modifiers are ignored (W still walks while Shift sprints), unless another binding on
the same key asks for them: **the most specific binding wins**. With Tab and Shift+Tab both bound, holding Shift makes
Tab only the Shift+Tab binding, whichever actions (and sources) the player gave them, so no caller has to check chords
in a special order. Consumption is per physical key, so an action bound to a key someone consumed reads as not pressed.
`GameInput_GetActionKeys` lists the keys an action matches this tick (consumed or not), which are exactly the keys
`GameInput_ConsumeAction` consumes; UI navigation records them to keep repeating while a key it consumed is held.
Ui_back and pause share Escape on purpose: the UI uses Escape while it has something to close, and only an unconsumed
Escape reaches the game as `pause`.

**The key index.** For each key, the bindings of the active actions on it, rebuilt whenever bindings change (a set, a
reset, a file, a registration, an override, a withdrawal). The most-specific check looks only at the bindings on its
own key, so a query costs the same with 19 actions as with 4096 (`GAME_INPUT_MAX_ACTIONS`; categories at most 256).
`GameInput_GetKeyBindingCount` / `TryGetKeyBinding` read it (the controls menu's conflicts). A default on a key another
action uses (same key, same modifiers) is allowed and logged once, naming both, when an action of a source other than
the engine registers or an override changes defaults; the engine's own shared defaults are not logged.

**Bindings.** An action follows its effective defaults until the player sets its bindings: `GameInput_SetBindings`
(validates; all or nothing; the action must be active) sets them as the player's (`GameInput_IsSetByPlayer`);
`GameInput_ResetActionBindings` returns one action to its defaults, `GameInput_ResetBindings` every action (and forgets
the kept controls-file entries). An empty list unbinds an action, except one that **keeps a binding** (`tbp:pause`, or a
pack's action with `keeps_a_binding`): its defaults are restored instead, logged once. `GameInput_GetBindingCount` /
`TryGetBinding` read an action's bindings (also while it is withdrawn).

### Pack actions: the input_actions asset type

The sixteenth standard asset type (`input_actions/` in a resource pack; format: `references/asset_structure.md`, "Input
actions"; `include/InputActions.h`). One definition is one category with its actions, plus overrides of actions and
categories that others registered: its name is the category's key, and each action's key is the category's namespace,
a colon and the action's name. Every rule is checked when the definitions are read (a broken file is skipped at
startup, the message naming the path to the bad value).

`InputActions_RegisterDefinitions(assets, input, logger)` runs once at startup, after the engine's actions and before
the controls file: it lists the definitions in byte order of their names, loads each under an asset user of its own,
then (1) registers each one's category and actions **all or nothing** (a key already active, or a failure, registers
nothing of it, logged once), and (2) applies each registered one's overrides all or nothing (an unknown or withdrawn
target, or one another source overrides, refuses that definition's overrides, logged once; its actions stay). The two
passes make an override independent of the order of the names. Each definition registers under its own key as the
source, so withdrawing one definition is one call. The input keeps copies, and the definitions are released. One
summary line is logged (`Input actions: 1 of 1 definitions registered, with 1 actions.`).

### The controls file

`GameInput_LoadBindingsFromFile` reads a JSON controls file (format: `references/controls_structure.md`). Keys are
written in full (`tbp:move_forward`); a key without a namespace is `tbp:`'s, so files written before keys had
namespaces load unchanged. Every entry sets its action's bindings as the player's; actions the file does not list keep
theirs. An entry for a withdrawn action applies to it for when it revives; an entry for a key no action has is **kept**
(a mod that is not loaded) and applies, as the player's, when an action registers under that key. It is all or nothing:
a bad entry (an invalid key, a key listed twice, a bad binding list) or more than 4096 entries fails with
`ErrorCode_InvalidJSON` (the message names the key and the binding) and changes nothing.

`GameInput_SaveBindingsToFile` writes **only what the player set**: the actions whose bindings the player set (active
or withdrawn) and the kept entries, in byte order of the keys. So new defaults from a story or a pack reach every player
who never rebound that action. A binding's text form is the modifiers in the order `control`, `alt`, `shift`, `super`,
then the key id, joined by `+` (`"control+shift+s"`; `GameInputBinding_AppendText` / `GameInputBinding_TryParse`).

`main.c` loads `<base directory>/controls.json` after the packs' actions; a missing file means the default controls, a
bad one is warned about and ignored. Nothing writes the file yet: that comes with the controls menu.

## Pitfalls

- Main thread only; `GameInput_SampleDevices` / `GameInput_Poll` need the window and must run once per update tick after
  `PollInputEvents` (only `GameFrameManager_Update` does it).
- Never read input in `Render` (see the input notes in AGENTS.md and BUILDING.md); edge flags are per update tick.
- Read actions, not keys, for anything a player could want to rebind. Raw keys are fine for debug toggles and for
  text editing. A pack's or a script's action is found by key once and its id kept; never look it up per tick.
- A frame that does not want input it did not consume should still check consumption (`GameInput_IsKeyPressed` already
  does), never `GameInput_GetRawKeyState`, unless it consumed that press itself.
- `GameInput` allocates when actions register, overrides apply and bindings change, never per tick; construct it in
  place and never move or copy it (the root registry holds pointers into it). `GameInput_Deconstruct` withdraws its
  registries from the root.
- A query's cost follows the bindings on its key, not the number of actions; a test counts the key-index entries a
  query looks at (`GameInputInternal_GetMatchVisitCount`).
