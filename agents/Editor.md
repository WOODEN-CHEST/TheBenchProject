# Editor

The world editor: a world shown in a window of a workspace, a fly camera, and the shell the editor's tools plug into. It runs as a game frame of
its own, alone: `sh compile/run_linux_x11.sh --editor [world name]`.

| Module | Files | What it is |
|---|---|---|
| EditorFrame | `include/EditorFrame.h`, `source/EditorFrame.c`, `source/EditorShell.c`, private `source/EditorInternal.h` | The frame: the world and its systems, the camera, the UI shell, the world's picture. |
| EditorCamera | `include/EditorCamera.h`, `source/EditorCamera.c` | The fly camera as pure maths: what one tick of controls does to a `GameCamera`. |

The world's window shows a render view ([UIElements.md](UIElements.md), "Render view"): a general element that shows a picture its host draws
and hands the host the pointer, the buttons and the wheel in its box. The editor frame is that host.

## Starting it

`--editor <name>` opens `worlds/<name>.world.json` from the game's folder (`Services.BaseDirectory`), `--editor` alone an empty world. A name
that is no plain file name (`../x`, `a/b`, an empty one: `Path_IsEntryNameValid`), a missing file or a broken one is logged and leaves an empty
world, with the debug grid on so the empty world can be seen. The world test frame does not run
beside it. `EditorFrame_Create(services, worldName, &frame)` needs the services' asset manager, key registry and window types; it builds
everything at once (a failure tears down whatever was built and returns the error).

## What it owns

- **The world** and what keeps it as it is, nothing that simulates it: a `WorldCollision` that places rather than moves
  (`AreMotionsSwept` and `WarnsWhenStaticObjectsMove` off), the generators that build what prefabs and path objects make (`WorldPrefabs`,
  `WorldPathObjects`, settled at load the way the test frame settles them, then updated each tick: they rebuild only what changed), and a
  `WorldRenderer`. No character, no physics step, no animation system, no game clock, no weather system: the sky shows the time of day and the
  weather the world's environment holds, and skinned figures stand in their bind pose.
- **The camera** (`GameCamera`, starting where the world test's starts: (0, 2.5, -7), pitch -0.2, so their pictures can be compared) and its fly
  controls (`EditorCamera`).
- **The UI**: its own widget and text factories, kit and screen (`UIScalePolicy_ConstantPixels`, the theme `tbp:test_flat`; without the theme
  the UI still works, in plain boxes), its `UIWindowHost`, and a workspace with a menu bar and a status bar.
- **Its window types**, registered under the source `tbp:editor` while it runs and withdrawn when it goes (after its windows):
  - `tbp:editor_world` ("World"): the world's render view filling the window, edge to edge (the window theme's `view` variant). Only one opens;
    it can be closed and comes back from the Window menu where it was.
  - `tbp:editor_controls` ("Controls"): what the mouse and the keys do. `tbp:editor_notes` ("Notes"): a text box, any number of them. Both are
    plain windows that show docking until the editor's real windows (the outliner, the inspector, the asset browser) take their place.
- **Its layouts**, in `editor_layouts.json` in the game's folder ([UIElements.md](UIElements.md), "Layouts"): the default docks the world in the
  middle, alone in its area, the controls at its left and notes under it. The arrangement is kept as it changes and when the frame goes.

**The menus.** File: Quit. Window: one item per window type (a single one comes back to the front). Layout: New layout, Delete layout, Reset to
the default (all three off when the layouts cannot be kept: the game has no folder). The status bar: where the camera is and how fast it flies at its left (set at most ten times a second, and only when it changed), a
message, and the layout switcher at its right.

**What later tools reach:** `EditorFrame_GetWorld`, `_GetCollision` (picking), `_GetCamera`, `_GetFlySpeed`, `_GetScreen`, `_GetWorkspaceId`
(their windows open through it), `_GetViewId` (the world's render view: `WIDGET_ID_INVALID` while its window is closed), `_IsWorldLoaded`,
`_GetWorldRenderCount`, and `EditorFrame_SetHostCursor` for tests and harnesses (the UI host's cursor, with no window). Tests, which have no GL,
make and unload the view's picture with stand-ins (`EditorFrame_InternalSetTargetFunctions` in the private `source/EditorInternal.h`).

## Each tick

1. **The UI**: `UIWindowHost_Update` (the viewport, the scale, the pointer and the keys from `GameInput`, then `UIScreen_Update`).
2. **The view's record**: `UIRenderView_TakeInput`, every tick, whether the view shows or not, so nothing old waits in it. A view whose window
   closed is forgotten.
3. **The camera**, from the record and the input actions (below).
4. **The world**: `WorldPrefabs_Update`, `WorldPathObjects_Update`, `World_UpdateTransforms`, `WorldCollision_Sync`: what moved this tick is
   where it went for whatever reads the world or the collision world next.
5. The status bar's place text, when the camera moved or its speed changed.

**The camera's controls** (`input_actions/editor.json`, the category `tbp:editor`, rebindable as any pack's actions):

| Action | Default | Does |
|---|---|---|
| `tbp:editor_fly_forward`, `_back`, `_left`, `_right` | W, S, A, D | fly along where the camera looks, and across |
| `tbp:editor_fly_up`, `_down` | E, Q | rise and sink along the world's +Y |
| `tbp:editor_fly_fast` | Left Shift | three times as fast |

- **Flying** is holding the right mouse button on the view: its press enters the view (the first press on a view that has not the focus acts at
  once) and hides the cursor (`UIRenderView_CaptureOnPress`), whose raw motion turns the camera; the cursor comes back where it was when the
  button comes up. While it is held, and only while the view is shown and entered, the fly actions move the camera. They are not consumed: a
  consumed key reads as up for as long as it is held, which would stop the flight a tick later.
- **The wheel over the view** steps the speed, focused or not (`EditorCamera`: a doubling every three notches, from 0.375 m/s to about 6 km/s).
- **The keys reach the world only while the view is entered** (the render view keeps Tab, the arrows, Enter and Escape unconsumed for its host
  then). A click in the view, or on the world window's tab or title bar, gives it the keys (the title bar and the tab act on nothing inside); a
  click anywhere else takes them away. Escape does not quit the editor: File, Quit does, or closing the game's window.

## The world's picture

The frame is the view's host, as [UIElements.md](UIElements.md)'s "Render view" says a host is:
- **In PrepareRender** (once per shown frame, on the update side): while the view shows, a render target the view's size in surface pixels,
  made again at every new size (a window edge or a splitter dragged gives a new size each frame, and the picture follows it: no stretching of an
  old picture), handed to the view when it is new to it, its rows bottom-up. A view that does not show (a tab not selected, a minimized window, a
  workspace hidden) keeps its target for when it shows again and draws nothing; a closed view's target is unloaded. Every target the view was
  handed is taken back from it before it is unloaded. A target GL cannot make, or not complete, is logged once (until one is made), nothing is
  drawn and the view shows its background; that size is not tried again until the view's size changes or the view opens again. Then how open the camera's surroundings are to the sky (`WorldCollision_GetSkyOpenness`), which the eye
  adapts indoors by, as the test frame does.
- **In Render**: the world into that target (`WorldRenderer_RenderToTarget`, outside any pass), then the UI, which draws the target's texture in
  the view. The camera's aspect is the target's.

**What it costs.** The view's picture is exactly the renderer's picture of the world at the view's size: drawn into the screen 1:1, the view's
region equals `WorldRenderer_RenderToTarget` of the same world, camera and size pixel for pixel. A steady frame costs what the world test
frame's does at the same window size. A frame in which the view's size changes costs about 5 ms more (the renderer's targets made again at the
new size): a drag of an edge or a splitter runs at about 15 ms a frame on a GTX 980 Ti. The eye's adaptation and the clouds' history carry on
through a drag without a flash.

**The pixelated scene** keeps its long side at 640 pixels whatever the view's size. A view wider than that shows world pixels of uneven widths
at ratios that are not whole (1 and 2 screen pixels side by side at 1.55); a view narrower than 640 samples the scene down with point filtering,
so thin things (lamp posts, fences) break up and shimmer while the camera moves.

## Not built

- The document, saving, undo, selection, picking, the overlay and the tools: the active tool will read the same record each tick, after the
  camera (a `// TODO:` in `EditorFrame_Update` marks the place).
- More than one world window.
- A view covered whole by a window maximized over it still draws its world.
