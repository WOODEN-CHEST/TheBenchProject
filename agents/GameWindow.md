# Game window

Files: `include/GameWindow.h`, `source/GameWindow.c`; `include/GameWindowState.h`, `source/GameWindowState.c` (pure: no window). Tests:
`tests/GameWindowTests.c`, `tests/GameWindowStateTests.c`.

The game's one window, and the one module that calls raylib's window functions after start. It runs as a resizable window, borderless
or fullscreen, switches between them while the game runs, and remembers what the player had.

## Modes

| Mode | What it is |
|---|---|
| `GameWindowMode_Windowed` | A normal window with a title bar, resizable, at least 640 x 360 (`GAME_WINDOW_MIN_WIDTH`, `GAME_WINDOW_MIN_HEIGHT`). |
| `GameWindowMode_Borderless` | The whole monitor at the desktop's video mode. |
| `GameWindowMode_Fullscreen` | The whole monitor at the highest refresh rate it has at that size. |

Both whole-screen modes are raylib's (`ToggleBorderlessWindowed`, `ToggleFullscreen`), and both are GLFW's fullscreen underneath. Neither changes the monitor's size. Borderless asks for
the refresh rate the monitor already has, so it changes nothing; fullscreen asks for none, so GLFW takes the highest at that size, a video mode change on a monitor running below its best.
On Windows both are topmost windows that do not minimize when they lose focus (raylib turns GLFW's auto-iconify off), so another window may open behind them. High DPI stays off (`FLAG_WINDOW_HIGHDPI` is never set): GLFW is DPI-aware on Windows without it, and one window pixel stays
one physical pixel, which the UI's scale policy answers by the monitor's height ([UIFramework.md](UIFramework.md), "Scale policies").

## Using it

```c
GameWindow Window;
GameWindow_Construct(&Window, &Config, BaseDirectory, &Log);   // the config's mode and size, or the state file's
GameWindow_Open(&Window, Title);                               // the real window and its GL context
if (!GameWindow_IsOpen(&Window)) { /* raylib has said why */ }
GameFrameManager_SetWindow(&Manager, &Window);                 // each tick, after the poll and before the frames
while (!GameWindow_ShouldClose(&Window) && ...) { ... }
// ... everything holding GL resources goes first ...
GameWindow_Close(&Window);                                     // saves the state, closes the window
GameWindow_Deconstruct(&Window);
```

**Closing.** raylib's `WindowShouldClose` answers only until its next poll, and the game polls once per update tick, several times between two
looks from the game loop: read there, a press of the close button would be lost most of the time. `GameWindow_Update` runs right after each poll
and keeps the answer, so read `GameWindow_ShouldClose` and never raylib's.

**Minimized.** raylib's poll waits for events while the window is minimized, so the game stands still until it is back, and the game loop's clamp on a frame's
time keeps the simulation from catching up afterwards.

## The fullscreen toggle

The engine action `tbp:toggle_fullscreen`, Alt+Enter and F11 by default, rebindable like any action. `GameWindow_Update` handles it before any frame
updates, and consumes it, so nothing sees its keys. The most specific binding wins, so `tbp:ui_accept` on plain Enter does not fire with Alt held. From a
window it goes to the config's `fullscreen_toggle_mode` (`"borderless"` by default); from either whole-screen mode it goes back to a window.
`GameWindow_SetMode` switches from code.

**A held key switches once.** A switch can take the focus away for a moment. GLFW reports every held key up when focus goes, and the key's next auto-repeat then arrives as a new
press, with a modifier lost until it is pressed again: an F11 held for three seconds would switch several times, and an Alt+Enter would reach the UI as a plain Enter. So the keys
that fired the toggle are held back: consumed, and doing nothing, until they have stayed up for `GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS`, which outlasts the longest usual delay before a
key repeats. A second press within that second does nothing either. An Alt held down across the switch stays lost: Enter pressed after the second, with it still held, is a plain Enter.

## Remembering

`<base>/window_state.json`: the mode, the windowed size and position, and whether the window was maximized. It is the player's, so `config.json`
stays hand-edited: the config's `window_mode` and `resolution` count on a first run, and after that the state file does. Delete the file to go back to them.

```json
{
    "mode": "windowed",
    "width": 1280,
    "height": 720,
    "has_position": true,
    "x": 320,
    "y": 180,
    "is_maximized": false
}
```

- **When:** when the mode changes and when the game ends, never per tick. Atomically: a temporary file next to it, then a replace.
- **What it holds in every mode:** the windowed size and position, also while maximized or in a whole-screen mode: what the window goes back to.
- **Read as hostile.** A property table reads it (`GameWindowState_GetPropertyTable`), and it is refused whole, with one log line, if it is longer than
  `GAME_WINDOW_STATE_MAX_BYTES`, is no JSON object or holds a value of the wrong type; the config then stands. A key left out takes its default, and an unknown key
  is ignored. A size past the limits is clamped. At start, once the monitors are known, `GameWindowState_FitToMonitors` keeps the size within the box around all the monitors (a window may span
  them), then drops a position with no monitor holding `GAME_WINDOW_MIN_VISIBLE_WIDTH` of the window's width and its top edge: at least `GAME_WINDOW_MIN_VISIBLE_HEIGHT` above the bottom of the monitor and
  `GAME_WINDOW_MIN_TITLE_BAR_ROOM` below its top, so a title bar can be reached. A window with no position opens centred on the first monitor, and is kept within that one.

## What the window does when

- **Opening** goes through a hidden window: created at the minimum size (what a file asks for could be more than a GPU can allocate), sized and placed once the monitors are known (only then are they), maximized or switched to its mode, then shown and focused. So it never flashes elsewhere, and a whole-screen mode opens on the monitor it was left on.
- **Following it.** While windowed, `GameWindowWatch` takes in a new size and position once they have held for `GAME_WINDOW_SETTLE_SECONDS`. A window manager reports a maximize as a new size and a new state, in either order and some ticks apart, and the maximized size must never be remembered as the size to come back to. What a minimized window reports means nothing (Windows puts it at -32000).
- **Switching** takes in what is pending at once, restores a maximized window first, and on the way back to a window sets the size and the place, then maximizes: raylib's own record holds the maximized size, and the order is what sets a window manager's memory of where to go back to. For a moment after a switch nothing the window reports is believed (`GameWindowWatch_Hold`): it is still arriving, and window managers answer late.

## Testing

A window that was never opened keeps its state and saves it with no raylib call, so headless tests drive everything but the real window: the toggle's keys and consumption, saving, the file's hostile cases, the fit to monitors and the watch, with their timing as plain numbers.

The real window needs a display: a private X server with a real window manager, driven from outside, reaches it (resizes, the minimum, the toggles, a maximize and back, restarts, hostile files, closing) with nothing showing on the desktop.

## Pitfalls

- Main thread only. `GameWindow_Open` first of anything that needs GL; `GameWindow_Close` after everything that holds GL resources.
- Never move a `GameWindow`: the frame manager keeps a pointer to it.
- The window's size arrives late after a switch: `GetScreenWidth` gives the new size at once, the framebuffer follows when the window manager has answered. Read sizes through `GameInput_GetWindowSize` each tick and rebuild what depends on them when they change.
- `FLAG_WINDOW_RESIZABLE` is set once, at creation. A window with a monitor (either whole-screen mode) ignores it.
- On Windows the event poll blocks while an edge is being dragged (the system's own move and resize loop, GLFW's documented behaviour), so the game stands still until the drag ends. Nothing here works around it yet.
