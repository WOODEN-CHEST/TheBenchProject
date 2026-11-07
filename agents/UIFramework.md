# UI Framework

The in-game UI. Widgets never touch raylib input or drawing directly: a **screen** (`UIScreen`) holds a tree of
**widgets**, routes the tick's input to them, drives keyboard navigation, runs property animations, delivers
notifications, lays the widgets out and draws them. A screen works on any **surface**: the window, or a texture shown in
the world (a paper on a desk, the office computer).

All positions and sizes are normalized `[0;1]` of the screen's surface: `(0,0)` is top-left, `(1,1)` bottom-right. A
widget's position and size are relative to its parent (or the screen, for a root), so a child at `p` inside a parent at
`P` with size `S` sits at absolute `P + p*S`. Widgets sized in pixels get there through placements (below, "UI pixels").

The elements calling code builds a UI from (the kit, panels, buttons, stacks, themes) are in [UIElements.md](UIElements.md).

## Modules

- **UIInput** (`UIInput.h`, header-only): the argument structs handed to a widget's input hooks
  (`WidgetHoverArgs`, `WidgetMouseInputArgs`, `WidgetScrollInputArgs`, `WidgetKeyboardInputArgs`), what entered an edit element and why it was
  left (`UIEnterCause`, `UILeaveReason`), the layers of a screen's roots (`UIScreenLayer`) and the pointer's shapes (`UICursorShape`). Keys and
  mouse buttons are `GameKey`s ([GameInput.md](GameInput.md)).
- **UIAnimation** (`UIAnimation.h/.c`): typed property values (`UIPropertyType`, `UIPropertyData`, `UIPropertyValue`
  and its `UIPropertyValue_From*` helpers), easing (`UIInterpolation`), keyframes, `UIAnimation_ValidateKeyframes` and
  `UIAnimation_EvaluateKeyframes`. Pure data and math.
- **UIRenderContext** (`UIRenderContext.h/.c`): the widget-local drawing device handed to `Render`: the renderer's
  `RenderContext` plus the widget's absolute box and accumulated tint.
- **UIWidgetFactory** (`UIWidgetFactory.h/.c`): one per program. Registers capabilities and widget types (a
  `WidgetTypeDescriptor`: struct size, vtable, constructor, capabilities, typed properties, default focusability, whether its widgets are edit
  elements and enter themselves), owns a pool per type, creates widgets (`UIWidgetFactory_CreateWidget`) and counts each type's live widgets.
- **UIWidget** (`UIWidget.h/.c`): the abstract widget base (vtable, identity, geometry, flags, state, subwidgets,
  capabilities, typed properties, `Widget_Destroy`). Concrete widgets embed it first.
- **UIScreen** (`UIScreen.h`, split by job into `source/UIScreen.c` (registry, roots, draw order, destruction,
  update/render, layout, hit testing and coordinates through render transforms), `UIScreenInput.c` (pointer, buttons, wheel, keys,
  text, focus), `UIScreenNavigation.c`, `UIScreenAnimation.c`, `UIScreenEvents.c` (notifications and cues) and `UIScreenTooltips.c`
  (hover text), sharing the private `source/UIInternal.h`).
- **UITooltip** (`UITooltip.h`, implemented in `source/UIScreenTooltips.c`): hover text, a service of the screen (below, "Hover text").
- **LabelWidget** (`LabelWidget.h/.c`): the first concrete widget (below).
- **UITestFrame** (`UITestFrame.h/.c`): a debug game frame running a screen over the world test (see
  [WorldRuntime.md](WorldRuntime.md)).
- **WorldScreenHost** (`WorldScreenHost.h/.c`): the host of a screen shown on a screen in the world (below, "World
  screens").
- **UIWindowHost** (`UIWindowHost.h/.c`): the host of a screen that fills the game window (below, "Hosts").
- **UIScalePolicy** (`UIScalePolicy.h/.c`): how a screen's scale follows the window and the monitor, pure (below, "Scale policies").
- **UIPlacement** (`UIPlacement.h/.c`): placements, insets and a stack's hints, and the render transform (`UIRenderTransform`), with their
  pure arithmetic (below, "UI pixels" and "Rendering, z order, clipping").

## Setting up a screen

```c
UIWidgetFactory Factory;                         // one per program
UIWidgetFactory_Construct(&Factory);
uint64_t LabelType;
LabelWidget_RegisterType(&Factory, &LabelType);  // every widget type, once

UIScreen Screen;
UIScreen_Construct(&Screen, &Factory, logger);   // logger: may be NULL (tests)

// a UI that fills the window has a host, which sets the viewport and the scale, and runs the update:
UIWindowHost Host;
UIWindowHost_Construct(&Host, UIScalePolicy_ConstantPixels(), playerScale, aspect, placement);

// each update tick (in the hosting frame's Update, before its game logic):
UIWindowHost_Update(&Host, &Screen, time, input);

// each render (into the surface's render context):
UIScreen_Render(&Screen, &renderContext);

// teardown: screens first (destroys their widgets), then the factory
UIScreen_Deconstruct(&Screen);
UIWidgetFactory_Deconstruct(&Factory);
```

**Surfaces.** The screen never reads the window. Each tick its host passes a `UIScreenInput`: the tick's `GameInput`
(may be NULL), where the pointer is in the screen's own space and whether the screen has the keyboard. Two flags say what the
position means. `IsPointerPresent`: it is over this screen, so it hovers and can start a press. `IsPointerTracked`: it is known
even off the surface, past `[0;1]` and unclamped, so a held press follows it. With neither, the position is ignored.
`UIScreenInput_CreateEmpty` is neither, and a hand-built input that sets only `IsPointerPresent` gets no drag off the surface.
`UIScreenInput_CreateFromWindow` makes the full-window case: the mouse position divided by the window size; tracked unless the
cursor is captured for mouse look or the window has no size; present only inside the window, with the cursor on it, and not claimed
by UI above (a drag held below that UI goes on, since it stays tracked). For a UI on a world surface, the host raycasts the pointer
onto the surface, passes the hit's UV and gives the keyboard to the surface in use. `IsPointerRelative` and `PointerDeltaPixels` carry
a hidden-cursor drag (below, "Input"). The host also keeps the `UIViewport` (the surface's size in pixels and the fitted-area
settings) current; the Layout pass converts units with it. A screen in the world has a host of its own:
`WorldScreenHost` (below, "World screens").

## Hosts

A screen never reads the window, so something outside it passes it the pointer, the keyboard, its size and its scale each tick. Two hosts do:
- **`UIWindowHost`**, for a UI that fills the game window: the viewport is the window's, the scale comes from its policy with the window's height and the monitor's, the
  pointer and the keyboard from `UIScreenInput_CreateFromWindow`, then `UIScreen_Update`. It reads everything from `GameInput`, so a test drives it with a sample.
  `UIWindowHost_SetPolicy` and `UIWindowHost_SetPlayerScale` change what it does from the next update on. After the update it applies what the screen asks of the real
  cursor, each only when it changes: a hidden-cursor drag (below, "Input") and the pointer's shape. It does so through a `UIWindowHostCursor`: `UIWindowHostCursor_CreateStandard` is
  the real cursor, and `UIWindowHost_SetCursor` puts another in its place, which is how tests and harnesses watch it. `UIWindowHost_Deconstruct` gives back a cursor it still holds
  and restores the arrow. Each host keeps to its own screen's shape, so of two hosts over each other the last to change the shape wins, whichever the pointer is over: a stack of them
  needs one place that decides.
- **`WorldScreenHost`**, for a UI on a screen in the world (below, "World screens"). Its screen keeps a fixed scale of its own, set once with `UIScreen_SetScale`.

## Scale policies

Each screen in the window has a policy (`UIScalePolicy.h`, pure), so a screen can keep its controls' size or grow with the window:

| Policy | Scale | For |
|---|---|---|
| `UIScalePolicy_Fixed(value)` | the given value | what must not follow the window |
| `UIScalePolicy_ConstantPixels()` | `max(1, round(monitorHeight / 1080 * 4) / 4)`, times the player's `ui_scale` | the editor: 1080p is 1, 1440p is 1.25, 4K is 2. Resizing gives more room, not bigger controls |
| `UIScalePolicy_FollowHeight()` | `windowHeight / 1080`, times the player's `ui_scale` | menus and game screens. Resizing scales the UI |

Every answer lies within `CONFIG_MIN_UI_SCALE` and `UI_SCALE_POLICY_MAX_SCALE`. A height that is not a finite number above 0 (an unknown monitor) gives the player's scale
alone. The monitor's height is what counts, so a portrait monitor counts as its tall side.

## UI pixels, placements and preferred sizes

A **UI pixel** is a surface pixel times the screen's scale (`UIScreen_SetScale`, which a host sets from its policy; a
screen in the world has its own, and its pixels are its render target's). Results round to whole surface pixels, so edges stay crisp.

A widget may have a **placement** (`Widget_SetPlacement`, `UIPlacement.h`): two anchors in `[0;1]` of its parent's box and two offsets from
them in UI pixels, which covers a fixed size at any corner (`UIPlacement_Fixed`, `UIPlacement_FromTopLeft`) and a stretch (`UIPlacement_Stretch`).
The layout pass turns it into the normalized `_position` and `_size`, parents first, before the widget's `Layout` hook, each edge
rounded to a whole surface pixel and written only when it changes, so an idle screen does not hit-test again. Two boxes sharing an edge in UI
pixels share it on the surface at any scale, and a box a whole number of pixels long keeps that length wherever it lies. A parent of zero size gives zero, never a
NaN. Offsets past `UI_PLACEMENT_MAX_OFFSET` are refused, and a stack's padding, spacing and hints past `UI_LAYOUT_MAX_LENGTH`: what a stack adds up always stays a number. A widget without a placement behaves as its
position and size say, in normalized units. **A placement wins** over the position and the size set any other way: an animation of a placed
widget's `Position` or `Size` changes nothing, so animate its tint or its placement. `Widget_ClearPlacement` leaves the box as it is.

A widget's **preferred size** is an optional vtable slot, `Measure`: its size in its own UI pixels for an available width (`UI_MEASURE_UNBOUNDED_WIDTH` when
nothing limits it). Containers ask through `Widget_Measure`, which caches the answer until `Widget_InvalidateMeasure` reaches the widget (from itself or
anything in its subtree: adding or removing a child, showing or hiding it and changing its stack hint do so themselves), the width changes (compared at a
sixteenth of a UI pixel, so float noise does not count), an effective scale on its way changes, or the screen's scale, viewport or theme changes. Nothing
allocates. A size that is not a finite number `>= 0` counts as 0, and one past `UI_LAYOUT_MAX_LENGTH` is kept to it. The hook runs in the layout pass: it may
load assets and refresh its own text, and must not destroy widgets or move focus.

**Scale.** Every widget has a scale of its own (`Widget_SetScale`, default 1, within `WIDGET_MIN_SCALE` and `WIDGET_MAX_SCALE`; the typed property
`WidgetBaseProperty_Scale`, so it can be animated), separate from its size: size is the area it takes (its placement or its stack hint), scale multiplies
what is drawn inside it (text, nine-slice corners, padding, a thumb, minimum sizes). Its **effective scale** (`Widget_GetEffectiveScale`) is the screen's
times its own and every ancestor's, kept within `WIDGET_MIN_EFFECTIVE_SCALE` and `WIDGET_MAX_EFFECTIVE_SCALE`: surface pixels per one of its own UI pixels,
what its hooks turn UI pixels into surface pixels with. It is kept current where it can change (a widget's scale, the screen's, a widget attached or moved:
`Widget_InternalUpdateEffectiveScale` over the subtree), so reading it costs nothing, in Render too, and a parent's Layout measuring a child attached this
tick already sees the child's. The rules:
- A `Measure` hook answers in the widget's own UI pixels; `Widget_Measure` divides the width it is asked about by the scale the widget adds first and
  multiplies the answer by it, so a container hears back in its own units. That scale is its own, or less where the effective scale is kept to its bounds:
  the box always fits what is drawn.
- A placement's offsets count in the parent's effective scale (a root's: the screen's), and so do a stack's padding, spacing and its children's hints
  (`UIStackLayout_Arrange` counts in the stack's own). So a fixed or stretched box never changes with the widget's own scale, a content-sized one grows
  with what it measures, and a child's fixed size counts in its parent's scale.
- Scale multiplies down the tree: a compound scales as one, and a window can zoom what it holds.

The `Layout` hook's `UILayoutContext` also has the parent's absolute position, the surface's size in pixels and what to hand to `Widget_Measure`.
`UIRenderContext_GetPixelBox` gives a widget its box in whole surface pixels at render, and the `UIRenderContext_RenderPixel*` calls draw in them.

## World screens

A UI can be the picture of a screen in the world (the office computer): a world model's surface
([WorldModels.md](WorldModels.md), "Surfaces") that an object's screen component shows ([WorldData.md](WorldData.md),
"Screen component"). The code that owns the screen's content keeps a `UIScreen`, a render target at the surface's
resolution (`WorldScreens_TryGetScreen`) and a `WorldScreenHost` (`include/WorldScreenHost.h`), and each frame:
1. **In Update**, before the game's own controls (the screen reads first, as a UI above the game does): finds where the
   player points (`WorldScreens_Raycast`: the aim from the eyes or the camera, out to the character's reach, the
   player's own body ignored; or the free cursor's ray, `GetScreenToWorldRayEx` through the camera) and calls
   `WorldScreenHost_Update(host, screen, time, input, &hit or NULL)`. While the screen holds a press (`UIScreen_HasHeldPress`) it also asks
   `WorldScreens_ContinueRay(screens, objectId, origin, direction, &continued)` and calls `WorldScreenHost_UpdateContinued(host, screen, time, input, &hit or NULL,
   &continued or NULL)`, so the press can follow the ray off the picture.
2. **In Render, before the world renders**: draws the screen into its render target (`RenderContext_Create` on the
   target with the surface's aspect, `UIScreen_Render`; mipmaps after it keep a small or slanted screen calm), then hands
   it over with `WorldRenderer_SetSurfaceTexture(renderer, objectId, surfaceName, target.texture, true)` (a render
   target's rows run bottom-up: [WorldRuntime.md](WorldRuntime.md), "Surfaces' pictures"). It holds for that render
   only.

What the host does each tick:
- **The pointer** is where the player points on the hosted screen's picture: the hit's place on the picture
  (`WorldScreenHit.Picture`: its texture coordinates as they are, a CRT's through its curvature, the point of its bulging
  picture the glass shows there) is the UI's own [0;1] position, (0, 0) the picture's top-left. Off the picture, at another screen, nowhere, or while
  UI above claimed the pointer (`GameInput_IsPointerConsumed`), there is none. Buttons go to the UI under the pointer; a
  press on the picture that no widget takes is consumed anyway: the game below never sees a click on the screen.
- **A held press owns the pointer.** While the screen's UI holds one, the screen stays pointed at and focused wherever the ray goes. The position goes on along the
  continued ray, past [0;1] (a CRT's goes on from where its glass's edge shows), nothing hovers off the picture, and the press hears its drags and its release there. The
  ray's place is what counts while it still hits this screen's glass; another screen in front of it changes nothing. Where the ray says nothing (no plane, or straight away from it) the
  last place holds. A drag on a world screen is always absolute: a hidden-cursor capture does nothing here. When the press ends off the picture the screen is let go as usual.
- **Using it.** The use action (`tbp:use`, E) or a left click on the picture **focuses** the screen: it gets the
  keyboard. The press of use that focuses it is consumed with the tick's typed text, and while its key stays held each
  tick's text is dropped too (the key's repeats), so it neither acts in the game nor types its letter; the click also
  goes to the UI.
- **The keyboard while focused**: the screen gets every key and the typed text first within its frame
  (`HasKeyboardFocus`; a frame above reads before it, as frames update top-down), then the host consumes every keyboard
  key down or released and the text: nothing below acts on a key while the player types,
  the movement actions included. The mouse is not the keyboard's, so the view still turns and the pointer follows the
  aim.
- **Letting go**: an Escape (`ui_back` or `pause`) the UI leaves unused releases the screen and is consumed (it does not
  pause the game); one the UI uses (leaving an entered element, clearing its focus, ending navigation) does not, and neither does a press in a
  tick whose typed text the UI took (a `pause` bound to a letter types it into a field; an Escape in that same tick needs
  pressing again). A `ui_back` bound to a letter passes an edit element that is not entered (it hears only the short list), so it clears the
  focus; once the element is entered it hears that letter first, and a text box claims the presses that type text, so the letter types.
  Pointing away from the picture with no press held releases it too: a screen the player no longer points at gets no input.
  `WorldScreenHost_Release(host, screen)` lets go at once (the object went away). However the screen is let go, its entered edit element is left,
  keeping what was done (`UILeaveReason_Keep`); the game window losing the system's focus does not leave it.

**Each Escape goes one level out**: it leaves an entered edit element (a cancel), then clears the UI's focus (or leaves the group navigation
entered), then lets go of the screen. Content that wants one Escape less keeps a widget focused while the screen is in use and releases the
host when the UI lets go of it (the test frame's monitor does: its full-screen desk widget, so clearing the focus there lets go). A UI frame
above the world that always has the keyboard (the UI test frame) still reads Tab and the arrows first. Tests:
`tests/WorldScreenTests.c` (a probe screen: the pointer, focus by use or a click, the keyboard taken after the UI read
it, the use press neither typing nor reaching the UI, keys and clicks left to the game while the screen is not in use,
letting go by Escape, pause or pointing away, a press held off the picture, the Escape chain out of an edit element and a letter bound to
`ui_back`).

## Widget lifecycle

`create → (configure) → Widget_InitializeWidget → add → … → Widget_Destroy`.

- **Create** only through the factory: `UIWidgetFactory_CreateWidget(factory, screen, typeId, args, &widget)` (or a
  type's helper, e.g. `LabelWidget_Create`). The factory borrows a pool slot, initializes the base (position and size 0,
  z 0, white tint, rendered / updated / input-enabled, the type's focusability), **reserves** the id, runs the type's
  constructor for the concrete part, and only then makes the widget alive. A failing constructor leaves nothing
  registered (it must release what it acquired itself). The constructor must not create widgets, use its id or
  subscribe: do that in `Initialize`.
- **Initialize** once (`Widget_InitializeWidget`): the `Initialize` hook, then the update / render / input enable hooks.
  A widget must be initialized before it is added. Before initialization the flag setters only set the flags.
- **Add / remove**: `UIScreen_AddWidget` / `UIScreen_RemoveWidget` for roots, `Widget_AddSubWidget` /
  `Widget_RemoveSubWidget` for children. Removing does not destroy; the widget can be added again. A tree is at most
  `WIDGET_MAX_TREE_DEPTH` (64) levels deep, so every walk is bounded.
- **Move** (`Widget_MoveTo(widget, newParentOrNull)`): to another parent, or to the roots with NULL, at once and whole: it keeps hover, focus, the tabbed state, presses, held
  keys and animations, unless what the widget lands under takes them away (hidden, say, as hiding does). Both ends must be attached. Cycles and too deep a tree are refused,
  and room in the new lists is made first, so a failed move changes nothing. It lands on top of its new siblings, and the screen looks up the hover again.
- **Destroy** (`Widget_Destroy`) is always **deferred** and takes the whole subtree. At once: the subtree is unlinked,
  its ids stop resolving, and the screen forgets it (hover, focus, navigation, mouse presses, animations); no hook runs, `OnPressLost` neither, since whoever destroys a
  widget knows.
  At the end of the screen's next `UIScreen_Update` (or in `UIScreen_Deconstruct`), the subtree is torn down children
  first: each widget's disable hooks and `Deinitialize` (if it was initialized), then `OnDeconstruct`, then its storage
  returns to the pool. A widget may therefore destroy itself or anything else from any hook or notification handler.
  `UIScreen_Deconstruct` destroys every widget still in the screen. The factory reports widgets still alive at its own
  deconstruction as an error.

## Ids, not pointers

A widget id is `(generation << 32) | (registry slot + 1)`. Freeing a slot bumps its generation, so a stale id never
resolves, not even to a newer widget in the same slot. Lookups are O(1): `UIScreen_TryGetWidgetById`,
`UIScreen_IsWidgetAlive`. Never hold a `Widget*` across anything that can run hooks or notification handlers; keep the id
and re-resolve it (the screen itself does this after every hook).

## Input

The screen routes, in this order, from its `UIScreenInput`:

1. **Pointer.** When the pointer moved over the screen (or the tree or geometry changed), the top-most input-enabled widget under it is
   looked up again and becomes hovered (`OnHoverStart` / `OnHoverEnd`). While the pointer is over a widget, and while a press is held wherever the pointer is, the screen
   claims it (`GameInput_ConsumePointer`). Pointer movement over the screen ends keyboard navigation (focus stays).
2. **Mouse buttons.** A press over a widget is **consumed**, focuses the nearest focusable widget under the pointer (or
   clears focus), raises that widget's root if its layer raises (below, "Rendering"), and reaches the widget as a `ButtonPress`.
   A press that hits no widget is a **click outside**: it ends navigation and clears focus, and the game gets it. That
   includes any press while the screen has no pointer (the cursor captured for mouse look, or the pointer over UI of a
   higher frame), so clicking into the world blurs the UI. With another press held it is neither: it is consumed and does nothing. The rest is under "A held press" below.
3. **Wheel.** Wheel movement over a widget is consumed from the game, whether anything uses it or not. It goes to that widget's `OnScrollInput`, then up through its
   ancestors that still take input (a hook may have taken one away), until one sets `*outIsHandled`.
4. **Keys and text** (when the screen has the keyboard). The **focused** widget gets every key first (Tab, arrows, Enter
   and Escape included), then its ancestors that still take input, until one sets `*outIsHandled` in `OnKeyboardInput`. A focused **edit
   element that is not entered** is the exception (below, "Entering elements"): it hears only the presses of the short list, and every other
   key and the typed text pass it by to its ancestors. A key **press** of the reset, copy or paste action
   (`tbp:ui_reset`, `tbp:ui_copy`, `tbp:ui_paste`) they leave goes on to the **hovered** widget and up its ancestors, with `IsHoverDelivery` set, so a focused widget can
   hear it twice for two reasons. Nothing else goes this way, no repeat and no typed text. A
   handled key is consumed and **owned** by the widget that handled it until it is released: the key's repeats and
   release go to that widget alone, even after focus moved (and are dropped once it is destroyed, detached or
   input-disabled). Typed text follows the focused path (if any codepoint is handled, the tick's text is consumed).
5. **Keyboard navigation** acts on the navigation actions nobody handled (`GameAction_UI*`, rebindable), and consumes
   only what it uses. While an edit element is entered, the screen's own rules for it take navigation's place (below):
   - `ui_next` / `ui_previous` (Tab / Shift+Tab) move focus between the current level's focusable widgets in reading
     order (top row first, then left); the arrows (`ui_up` ...) move to the nearest one in that direction. The first
     such key only starts navigation (outlines appear) where focus is. Holding one keeps moving with the key's
     auto-repeat: navigation consumed the key, so it follows that key's repeats itself (with the modifiers held at each
     repeat) until the key is released or navigation ends.
   - `ui_accept` (Enter) enters the focused widget when it contains focusable widgets (a **group**): it becomes **tabbed** (green
     outline) and focus moves to its first one. This also starts navigation, so Enter opens a container focused by a
     click. On an edit element Enter enters it instead (below). Otherwise Enter is left to the game.
   - `ui_back` (Escape) leaves the entered widget (it is focused again); at the top level it ends navigation and clears
     focus; outside navigation it clears focus; with nothing to do it is left to the game (where it is `pause`).
   - Non-focusable widgets are transparent to navigation: their focusable descendants belong to the enclosing level.
   - While navigating the white outline marks the focused widget, and a green one an entered edit element (as it marks the groups navigation
     entered); navigation re-enters where focus is.
   - The entered (tabbed) widgets always lead to focus. When focus moves without navigation (`UIScreen_SetFocusedWidget`,
     a hook), the next navigation step leaves and enters widgets to match; with nothing focused (the focused widget was
     destroyed) navigation carries on at its level. A widget a hook detaches is never left tabbed or focused.

Whatever is not consumed reaches the game: the frame's game logic (and frames below) read it through the same
`GameInput`. Widgets that need more read the input of the running update with `UIScreen_GetInput`.

### Entering elements

One keyboard rule for every element: **moving the focus never changes anything**, and nothing focus passes over takes Tab or the arrows. An
element is one of three kinds, by what Enter does:

| Kind | What it is | Enter, or a press | Focused but not entered, it hears |
|---|---|---|---|
| **Press** | a button, a toggle, a menu item | activates it | every key first |
| **Group** | a focusable widget holding focusable widgets | goes in (navigation, above) | every key first |
| **Edit** | a type whose descriptor says `NeedsEntering` (`Widget_SetNeedsEntering` per widget): a slider, a text box, a list, a world view | enters it | the presses of the short list only |

An edit element that is **not entered** hears only the presses of the short list, the reset, copy, paste and context menu actions
(`tbp:ui_reset`, `tbp:ui_copy`, `tbp:ui_paste`, `tbp:ui_context_menu`): every other key and the typed text pass it by as if it had not handled
them, on to its ancestors, then navigation, then the game. So the arrows and Tab move past it and typing on it does nothing.

**Entering.** At most one widget of a screen is entered (`UIScreen_GetEnteredWidgetId`, `Widget_IsEntered`), and it is always the focused one,
so focus moving anywhere leaves it first. It is kept apart from navigation's tabbed path: pointer movement ends navigation, never an edit.
- **Enter** (`ui_accept`) on a focused edit element that is not entered: the screen takes that press at the element's place in the chain,
  before its ancestors, so a dialog around a field cannot swallow it. It is consumed until released, so its repeats do nothing; an Enter
  held from before the focus came enters nothing.
- **A press of any mouse button** on it: the press focuses it as usual and enters it before the press arrives, so the element sees itself
  entered in its `ButtonPress`. Only on a screen that has the keyboard: entering gives the element its keys, so on a world screen not in use
  (a right or middle click: only a left click or the use action takes the keyboard) the press only focuses it. A type that enters itself (`EntersItself` in the descriptor, `Widget_SetEntersItself`) is only focused: it
  calls `UIScreen_EnterWidget` when it chooses, as a field whose drag scrubs does on a click that did not move.
- **Code:** `UIScreen_EnterWidget(screen, id)` focuses and enters an edit element (a popup's list, a dialog's first field, the console's
  line); it is refused for a widget that is no edit element or cannot take focus. Only the focused widget is entered, so a focus hook that
  moves the focus elsewhere meanwhile wins.
- **Tab** out of another entered element (below).

The element hears `OnEnter(self, cause)`: `UIEnterCause_Key`, `_Press` or `_Code` (a text box selects everything on a key and places the
caret on a press).

**Inside**, the element hears every key first, then its ancestors, as a focused widget does; a console's history or a dialog's default
button can still take what the element leaves. What both leave is the screen's, in place of navigation's:
- **Escape** (`ui_back`): leaves with `UILeaveReason_Cancel`.
- **Enter** (`ui_accept`): leaves with `UILeaveReason_Keep`.
- **Tab and Shift+Tab** (`ui_next`, `ui_previous`): leave with `Keep` and move to the next or previous candidate at once, starting navigation
  if it was not running (no first press that only shows the outlines); the element they land on is entered if it is an edit element
  (`UIEnterCause_Key`), and a button or a group is only focused. While the key is held, navigation follows its repeats and each step enters
  the edit element it lands on, past buttons too; no element hears those repeats.
- **The arrows** (`ui_up` and the rest): consumed, and nothing happens. They never throw you out of an element, nor reach the game while you
  edit.

Every one of those actions pressed while an element is entered is consumed, also when another acts first in the same tick (Escape before
Enter before Tab), so none reaches the game; the keys that leave stay consumed until released. **Focus moving** leaves with `Keep`: a press elsewhere or outside, `UIScreen_SetFocusedWidget`,
hiding, disabling, making it unfocusable, an overlay, detaching it, `Widget_SetNeedsEntering(false)`. **A destroyed element is left
silently**: no hook runs, as destroying promises. `UIScreen_LeaveWidget(screen, reason)` leaves by code. The element hears
`OnLeave(self, reason)`; what `Cancel` means is the element's (a slider whose presses each committed has nothing to put back; a text box goes
back to its text at entering).

**Kept keys.** `Widget_SetKeepsKeysWhenEntered(widget, true)` (a world view's tools read the keys): while it is entered the screen neither
navigates nor leaves it by a key, and Tab, the arrows, Enter and Escape stay unconsumed in `GameInput` for their reader. Only focus moving,
code or a world screen let go leave it.

**A held press.** It owns the pointer. Its `Move`s and its `ButtonRelease` go to the widget that took it, wherever the pointer is, past the surface too (positions past
`[0;1]`, unclamped), for as long as the widget stays attached, takes input and shows. One `Move` comes per tick the pointer moved or came or went, for every button held; a pointer
that moved in the release's tick arrives as a `Move` first, so a drag ends where the pointer ended. After the `ButtonRelease`, the top widget under the pointer, if another one, gets a
`ReleaseOver`: `OnRelease` clicks and press-drag-release menus stand on it. A press that hit no widget has none. `UIScreen_HasHeldPress` tells whether any is held.

**What a mouse input says** (`WidgetMouseInputArgs`): the button, the held `Modifiers`, the pointer's place on the screen and in the widget (through the render
transforms of the widget and its ancestors, undone: a turned slider is dragged along its own track), the place the press began, how long ago,
`DeltaPixels` (surface pixels since this press's last `Move`) and `ClickCount`: how many presses of this button on this widget in a row, each within
`UI_SCREEN_MULTI_CLICK_SECONDS` of the last and `UI_SCREEN_MULTI_CLICK_DISTANCE` UI pixels of its place.

**A press that stops reaching its widget.** The optional `OnPressLost(self, button)` runs at once when that happens: the widget detached, hidden, input-disabled or put in an
overlay, directly or through an ancestor, or its press handed on. Nothing more of that press reaches it, and the button's release does nothing. A press a focus hook took away
before it arrived was never the widget's: it stays consumed, reaches nobody and is not told lost. `Widget_Destroy` tells nothing.

**Handing a press on.** `UIScreen_TransferPress(screen, button, newWidgetId)`: its drags and its release now go to another widget, with its start, its time and its count kept; the old
one gets `OnPressLost`. With `Widget_MoveTo` this is how a drag can tear a tab off into a window of its own, in one tick. From a focus hook, before the press arrives, the new widget
gets the `ButtonPress` itself.

**A hidden-cursor drag.** `UIScreen_CapturePointer(screen, widgetId)`, legal only while that widget holds a press and no other has the capture, asks for it; it ends with the widget's
last press, on a transfer, or with `UIScreen_ReleasePointerCapture`. The screen only records it (`UIScreen_IsPointerCaptured`). `UIWindowHost` applies it after the update: the cursor
hides and its movement is free of the window's edges (`GameInput_CaptureCursor`), and the next input is relative: the pointer's position and hover stand still, and `Move`s carry the raw
motion in `DeltaPixels`. When it ends the cursor comes back to where it was when the host took it (`GameInput_ReleaseCursor`). A world screen's host does not apply it, so there the
capture changes nothing and the drag stays absolute: read `DeltaPixels` either way. Hover stands still during it, but what has been hidden meanwhile hears no hover key.

**The pointer's shape.** `Widget_SetCursorShape(widget, shape)`; `UIScreen_GetCursorShape` answers the shape of the widget holding the first held press, or else of the hovered one, or
else the arrow, and `UIWindowHost` applies it when it changes. `NotAllowed` and the two diagonal resizes need a cursor theme on X11, and without one show the arrow.

**What is under a point.** The top-most input-enabled widget whose box holds the point, with each render transform on the way undone (below,
"Rendering, z order, clipping"); a cut subtree is cut to its box, and a transform with a visual scale of 0 holds no point, with its subtree. A widget type
can stand in for its box with the optional `HitTest` slot (`WidgetHitTestArgs`: the point on the screen as asked, the point in its box with the transforms
undone, and whether it lies in the box): a round button answers inside its circle, a widget standing for a world object answers through its own ray. It runs
on the update side whenever the screen looks up what is under a point, and must not change state.

**States.** Hovered (pointer), focused (keyboard target; at most one per screen), tabbed (a group navigation entered), entered (the
edit element being edited: at most one, always the focused one), active (hovered or tabbed). Widgets get `IsFocusable`, `NeedsEntering`
and `EntersItself` from their type (`Widget_SetIsFocusable`, `Widget_SetNeedsEntering`, `Widget_SetEntersItself` change them); an
input-disabled widget is transparent to the pointer and cannot keep focus. A hidden widget (`Widget_SetIsRendered`) is not there for the pointer or the keyboard, with its
subtree: nothing in it can have the focus (hiding takes it away), navigation passes it, navigation inside it ends, and a press or a held key that began on it is over
for it. A box of no size holds no point.

## Notifications

State changes (`UIEventKind_HoverChanged`, `FocusChanged`, `TabChanged`, `ActiveChanged`, `EnteredChanged`), sound cues (`UIEventKind_Cue`, below) and a widget type's own events
(`UIScreen_PostWidgetEvent`, `UIEventKind_Widget` with a type-defined id and an optional `UIPropertyValue`; `UIScreen_PostWidgetMouseEvent` adds the mouse's data by value,
`UIEvent.HasMouse` and `UIEvent.Mouse`: button, modifiers, screen position, scroll delta, whether over the widget) are
**queued** and delivered at one point, at the end of `UIScreen_Update` (after the widget updates and layout), never in
the middle of input routing. Subscribe with `UIScreen_Subscribe(screen, widgetId or 0 for every widget, kind mask,
handler, userData, &id)`; a subscription to one widget ends when it is torn down (at the end of the update it was
destroyed in). A `UIEvent` names its widget by id. A state change is queued right after the state changes and before
the widget's hook runs, so when a hook changes the state again (a focus hook moving focus on), the last notification
about a widget always tells its final state. When focus moves off an entered element, its `EnteredChanged` comes before its `FocusChanged`. A state change is dropped when its widget is destroyed before delivery; a
widget type's own event is delivered even then (it carries everything by value), so a button that closes its dialog
still reports the click. Handlers may destroy widgets, move focus, start animations, subscribe or unsubscribe, and post
more notifications (delivered in the same pass, at most `UI_SCREEN_MAX_EVENTS_PER_DISPATCH` per update: the rest are
dropped, and logged once per streak of such updates, so handlers that keep posting cannot hang the game or flood the
log). A widget's own vtable hooks still run immediately. The queue is shaped to later post into a game-wide event
queue.

**Cues.** `UIScreen_PostCue(screen, id, cue)` posts a named sound cue (`UIElementCue`: hover, press, release, activate, a value's step, open, close,
refused, enter, leave), a `UIEventKind_Cue` notification whose `WidgetEventId` is the cue. Nothing plays them yet. While no subscription asks for the kind,
for that widget or for every widget, nothing is queued, so a cue costs one look at the subscriptions. The screen posts `Enter` and `Leave` itself when an edit
element is entered and left (best effort, as the state notifications are); elements post the rest. A cue is delivered even if its widget was destroyed after
posting it.

## Hover text

A widget can have a text shown beside the pointer after it rests on it (`UITooltip.h`), a service of the screen rather than anything a widget type embeds:
`UITooltip_Set(screen, id, text)` by value, `UITooltip_SetProvider(screen, id, provider, userData)` for text that changes (asked each update while the
pointer rests there, on the update side, writing into a buffer the screen owns), `UITooltip_SetDelay` for a delay of its own (else the theme's
`tooltip_delay`), `UITooltip_Clear`. The screen keeps a side table, emptied when a widget is torn down, so a widget without one costs nothing.

Each update, right after the input, the screen works out where the pointer rests: the hovered widget, or the nearest ancestor with a hover text (a
compound's parts show the compound's). The rest time grows while it stays there; once it passes the delay the text shows. Resting on something with
another hover text, or the pointer leaving the screen, hides it and starts again; a mouse press hides it until the pointer rests on something else. A
widget destroyed, or its text cleared, takes what shows with it at once.
`UITooltip_GetCurrent` tells where it rests, the text and whether it shows, before the delay too, for a status bar.

It shows as a widget of the type the factory names (`UIWidgetFactory_SetTooltipType`; the kit registers the theme-driven one, [UIElements.md](UIElements.md)),
made with no arguments the first time a text shows, as a root of the overlay layer: it takes no input, so the widget under it keeps the hover. That widget
reads `UITooltip_GetCurrent` in its Update and places itself in its Layout. Without a type nothing shows and the rest still works.

## Update order

`UIScreen_Update`: snapshot the attached widgets (pre-order) → route input → advance animations → update the snapshot
widgets that are still alive, attached and updated → **Layout** the snapshot widgets that are still alive and attached,
parents first (`Layout` hook, with a `UILayoutContext`: a measuring render context for the viewport and the parent's
absolute size) → deliver notifications → tear down destroyed widgets. Widgets created or attached during the tick (by a
hook, a handler or input routing) get their first Update and Layout next tick, like frames added during a manager
update. Render reads state only (the navigation outlines read the bounds without filling the bounds cache).

## Rendering, z order, clipping

Each widget has a float `ZLayer` relative to its siblings. Siblings are kept in a **draw order** sorted by (z, insertion)
and updated only when a z value or the child set changes, so siblings of equal z keep their insertion order and
`Widget_GetSubWidgetAt` / `UIScreen_GetRootAt` (insertion order) never change meaning. A root also has a **layer** (`UIScreen_SetRootLayer`, `UIScreenLayer`), which comes before its z,
for drawing and for hit testing alike:

| Layer | A press raises it | For |
|---|---|---|
| `Background` | no | what stays behind everything |
| `Normal` (the default) | yes, within its layer | the ordinary root |
| `Floating` | yes, within its layer | movable windows |
| `Maximized` | no | a window that fills the screen |
| `Popup` | no | menus and the like |
| `Overlay` | no, and it takes no input | what only shows: the pointer goes through it, nothing in it can have focus or a press, and navigation passes it and leaves it at once if it becomes one |

A layer belongs to a root: a root moved under a parent stops having one, and gets back its own if it becomes a root again. `UIScreen_BringToTop` and a root's `Widget_SetZLayer` work within the
layer, and entering a root with `ui_accept` raises it as a press does. The screen draws roots in draw
order, each widget before its children, and a hidden widget hides its subtree. `AreSubWidgetsCut` clips a subtree (rendering and hit testing) to the widget's
box through the render context's **clip stack** (`RenderContext_PushClip`), so nested clips intersect and a label's own
cut nests inside its parent's. While navigating, outlines are drawn on top, untransformed.

**The render transform.** Any widget can be turned, moved and scaled with its subtree for drawing and pointing alone (`Widget_SetRenderTransform`, a
`UIRenderTransform`: `Rotation` in radians, clockwise on the surface; `Pivot` in [0;1] of its box, its centre by default; `Offset` in its parent's UI pixels;
`VisualScale` per axis), with an explicit flag (`Widget_HasRenderTransform`, `Widget_ClearRenderTransform`), and as typed properties for animation
(`WidgetBaseProperty_Rotation`, `_Pivot`, `_VisualOffset`, `_VisualScale`; setting one gives the widget the transform). It is worked out in surface pixels,
where the axes share a unit. Drawing pushes its matrix around the widget's whole subtree (`RenderContext_PushTransform`, [Renderer.md](Renderer.md)),
composed with the transforms above it, and pops it on every path, an error included; one that changes nothing pushes nothing. The hit test and
`UIScreen_ScreenToWidget` go through its inverse (`UIScreen_WidgetToScreen` through the transform itself), so a turned button is clicked where it shows and a
turned slider is dragged along its track. Layout, measuring, `Widget_GetEstimatedBounds`, keyboard navigation and its outlines keep the box layout gave it;
`UIScreen_GetWidgetPixelBox` gives that box in surface pixels. Clips stay axis-aligned: a cut subtree inside a turned widget is drawn clipped to the bounding
box of its turned clip, and hit only inside the turned box itself. This is a known limit. The visual offset and scale do what a placement cannot: a placed
widget's position cannot be animated (the placement wins), but its offset can, so a window can slide in.

## Properties and animation

Properties are typed. Every widget has the base properties (`WidgetBaseProperty_Position` / `Size` (Vector2), `ZLayer`
(float), `RenderTint` (RenderColor), `Scale` (float), the render transform's `Rotation` (float), `Pivot`, `VisualOffset` and `VisualScale` (Vector2), each
validated by its setter); a type declares its own in its descriptor (`WidgetPropertyDescriptor`: id below
`WIDGET_BASE_PROPERTY_START`, type, name). `Widget_GetProperty` / `Widget_SetProperty` take `UIPropertyValue`s and
reject a value of the wrong type; `Widget_TryGetPropertyType` looks a property up. A value is a float, a vector, a colour, a render colour, a bool or a
double; nothing blends the last two, so keyframes refuse them (they travel in events and plain properties).

`UIScreen_StartAnimation(screen, widgetId, propertyId, type, keyframes, count, options, &id)` validates the keyframes
(times finite, >= 0 and non-decreasing; values finite; a custom curve present when used) and the property's type, copies
the keyframes into pooled storage and drives the property each tick; the animation started last wins. An animation whose
setter fails **stops and is logged once**; it never fails the update. Destroying a widget stops its animations;
detaching it does not. `UIScreen_StopAnimation`, `UIScreen_StopWidgetAnimations`, `UIScreen_IsAnimationRunning`.

## Widgets

- **LabelWidget** (`LabelWidget.h/.c`): displays a (possibly multi-line) rich text, for labels, buttons, text boxes.
  It embeds a `TextDocument` (owns the text tree and the fonts and sprite animations it names) and a `TextLayout`
  ([TextDocument.md](TextDocument.md)); its properties are layout options: per-line alignment and bound handling (Bounds
  plus **Cut** / **Wrap** / **Resize** / **WrapThenResize** / **WrapThenCut**; cutting goes through the clip stack). Text
  is measured and drawn in normalized-fitted units times the label's `Size`; the block's top-left sits at the widget box
  top-left. Each tick its Update advances the document's sprites and lays the text out again when it changed, and its
  **Layout** step sizes the widget box to the text (only writing it when it changes), so hit testing and outlines hug
  the text; Render only draws. Typed properties (`LabelProperty_*`): Size, Rotation (float), Tint (RenderColor), Origin
  (Vector2). Not focusable by default. Build with `LabelWidget_RegisterType` (once) and `LabelWidget_Create`
  (borrowed `TextComponentFactory`, optional `AssetManager` and `Logger`, optional initial text whose ownership passes to
  the label on success); give it text with `LabelWidget_SetText` or edit the tree in place. `LabelWidget_SetUIPixelSize` gives it a line height in UI
  pixels in place of its Size (followed through the scale and the viewport at each layout). It answers `Measure` with its text block's size, and with a
  placement it keeps the box it is given. As an element of the kit it also takes its font and colours from the theme ([UIElements.md](UIElements.md)).

Writing a new widget type (an element of the kit: [UIElements.md](UIElements.md)): embed `Widget` first; give the type a static `WidgetVTable` and a descriptor (declare its
typed properties there; a type that uses the arrows or typing sets `NeedsEntering`, and answers `OnEnter` and `OnLeave` where entering and a
cancel mean something to it); construct only the concrete part in the constructor; create child widgets in `Initialize`;
report keys you handle through `outIsHandled` (handle a key's press to get its repeats and release), and the wheel the same way in `OnScrollInput`; a press that starts something must
answer `OnPressLost` as well as the release (Destroy tells nothing: the widget is gone); answer `Measure` in your own UI pixels and turn them into surface pixels
with `Widget_GetEffectiveScale`; give a `HitTest` hook when your shape is not your box; post your own
events with `UIScreen_PostWidgetEvent` or `UIScreen_PostWidgetMouseEvent` (they reach subscribers even if the widget is destroyed in the same update);
never write state in `Render`.

## Memory ownership

The program-wide factory owns every widget's storage (per-type pools). A screen borrows the factory and owns its
registry, queues and widgets' lifetimes. Teardown order at program end: screens (they destroy their widgets) → factory.
Nothing allocates per tick in steady state: the snapshot, navigation candidates, event queue, keyframe storage and hover text's
buffer (which a provider writes into) are reused; childless widgets allocate no child lists.
