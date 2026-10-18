# GameKey and GameInput modules

Files: `include/GameKey.h`, `source/GameKey.c`, `include/GameInput.h`, `source/GameInput.c`, and the private
`source/GameKeyInternal.h` (the raylib code behind each key; only the input layer includes it).

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
controls menu; it is not in Phase 5. Everything lives in one table in `GameKey.c` indexed by `GameKey` (designated
initializers, so the enum order cannot drift from the table).

## GameInput: the tick's input

```c
GameInput Input;
GameInput_Construct(&Input);                 // nothing held, default bindings
GameInput_LoadBindingsFromFile(&Input, path); // optional: the player's controls over the defaults
// once per update tick, right after PollInputEvents (GameFrameManager_Update does this):
GameInput_Poll(&Input);                       // = GameInput_SampleDevices + GameInput_BeginTick
```

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

`GameAction` is the closed list of the game's actions, each with a stable id string, a display name and default
bindings:

| Action | Id | Default |
|---|---|---|
| UI navigation | `ui_up`, `ui_down`, `ui_left`, `ui_right` | arrow keys |
| | `ui_next`, `ui_previous` | Tab, Shift+Tab |
| | `ui_accept` | Enter, Keypad Enter |
| | `ui_back` | Escape |
| Movement | `move_forward`, `move_back`, `move_left`, `move_right`, `move_up`, `move_down` | W, S, A, D, Space, Left Control |
| | `sprint` | Left Shift |
| Pause / menu | `pause` | Escape |

Queries: `GameInput_IsActionDown` / `IsActionPressed` / `IsActionReleased` / `IsActionPressedOrRepeated`. An action
has up to `GAME_INPUT_MAX_BINDINGS_PER_ACTION` (4) bindings; a binding is a key plus the modifiers that must be held
with it (`GameInputBinding`). Other held modifiers are ignored (W still walks while Shift sprints), unless another
binding on the same key asks for them: **the most specific binding wins**. With Tab and Shift+Tab both bound, holding
Shift makes Tab only the Shift+Tab binding, whichever actions the player gave them, so no caller has to check chords
in a special order. Consumption is per physical key, so an action bound to a key someone consumed reads as not pressed.
`GameInput_GetActionKeys` lists the keys an action matches this tick (consumed or not), which are exactly the keys
`GameInput_ConsumeAction` consumes; UI navigation records them to keep repeating while a key it consumed is held. Rebinding: `GameInput_SetBindings` (validates; all or nothing), `GameInput_ResetBindings`,
`GameInput_GetBindingCount` / `GameInput_TryGetBinding`. Ui_back and pause share Escape on purpose: the UI uses Escape
while it has something to close, and only an unconsumed Escape reaches the game as `pause`.

Adding an action: add the enum value and its row in `GameActionInfos` (`GameInput.c`, id string, display name,
defaults). Never rename an id string (controls files store them).

### The controls file

`GameInput_LoadBindingsFromFile` reads a JSON controls file (format: `references/controls_structure.md`): every action
it lists replaces its bindings, actions it does not list keep theirs, names that are not actions are ignored (older or
newer files keep loading). It is all or nothing: any bad entry fails with `ErrorCode_InvalidJSON` (the message names the
action and the binding) and changes nothing. `GameInput_SaveBindingsToFile` writes every action. A binding's text form is
the modifiers in the order `control`, `alt`, `shift`, `super`, then the key id, joined by `+` (`"control+shift+s"`;
`GameInputBinding_AppendText` / `GameInputBinding_TryParse`).

`main.c` loads `<base directory>/controls.json` after constructing the input; a missing file means the default
controls, a bad one is warned about and ignored. Nothing writes the file yet: that comes with the controls menu.

## Pitfalls

- Main thread only; `GameInput_SampleDevices` / `GameInput_Poll` need the window and must run once per update tick after
  `PollInputEvents` (only `GameFrameManager_Update` does it).
- Never read input in `Render` (see the input notes in AGENTS.md and BUILDING.md); edge flags are per update tick.
- Read actions, not keys, for anything a player could want to rebind. Raw keys are fine for debug toggles and for
  text editing.
- A frame that does not want input it did not consume should still check consumption (`GameInput_IsKeyPressed` already
  does), never `GameInput_GetRawKeyState`, unless it consumed that press itself.
- `GameInput` owns no heap memory and never allocates per tick; `GameInput_Deconstruct` only zeroes it.
