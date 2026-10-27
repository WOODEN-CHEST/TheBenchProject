# UI Framework

The in-game UI. Widgets never touch raylib input or drawing directly: a **screen** (`UIScreen`) holds a tree of
**widgets**, routes the tick's input to them, drives keyboard navigation, runs property animations, delivers
notifications, lays the widgets out and draws them. A screen works on any **surface**: the window, or a texture shown in
the world (a paper on a desk, the office computer).

All positions and sizes are normalized `[0;1]` of the screen's surface: `(0,0)` is top-left, `(1,1)` bottom-right. A
widget's position and size are relative to its parent (or the screen, for a root), so a child at `p` inside a parent at
`P` with size `S` sits at absolute `P + p*S`.

## Modules

- **UIInput** (`UIInput.h`, header-only): the argument structs handed to a widget's input hooks
  (`WidgetHoverArgs`, `WidgetMouseInputArgs`, `WidgetKeyboardInputArgs`). Keys and mouse buttons are `GameKey`s
  ([GameInput.md](GameInput.md)).
- **UIAnimation** (`UIAnimation.h/.c`): typed property values (`UIPropertyType`, `UIPropertyData`, `UIPropertyValue`
  and its `UIPropertyValue_From*` helpers), easing (`UIInterpolation`), keyframes, `UIAnimation_ValidateKeyframes` and
  `UIAnimation_EvaluateKeyframes`. Pure data and math.
- **UIRenderContext** (`UIRenderContext.h/.c`): the widget-local drawing device handed to `Render`: the renderer's
  `RenderContext` plus the widget's absolute box and accumulated tint.
- **UIWidgetFactory** (`UIWidgetFactory.h/.c`): one per program. Registers capabilities and widget types (a
  `WidgetTypeDescriptor`: struct size, vtable, constructor, capabilities, typed properties, default focusability), owns
  a pool per type, creates widgets (`UIWidgetFactory_CreateWidget`) and counts each type's live widgets.
- **UIWidget** (`UIWidget.h/.c`): the abstract widget base (vtable, identity, geometry, flags, state, subwidgets,
  capabilities, typed properties, `Widget_Destroy`). Concrete widgets embed it first.
- **UIScreen** (`UIScreen.h`, split by job into `source/UIScreen.c` (registry, roots, draw order, destruction,
  update/render, layout), `UIScreenInput.c` (pointer, buttons, wheel, keys, text, focus), `UIScreenNavigation.c`,
  `UIScreenAnimation.c` and `UIScreenEvents.c` (notifications), sharing the private `source/UIInternal.h`).
- **LabelWidget** (`LabelWidget.h/.c`): the first concrete widget (below).
- **UITestFrame** (`UITestFrame.h/.c`): a debug game frame running a screen over the world test (see
  [WorldRuntime.md](WorldRuntime.md)).

## Setting up a screen

```c
UIWidgetFactory Factory;                         // one per program
UIWidgetFactory_Construct(&Factory);
uint64_t LabelType;
LabelWidget_RegisterType(&Factory, &LabelType);  // every widget type, once

UIScreen Screen;
UIScreen_Construct(&Screen, &Factory, logger);   // logger: may be NULL (tests)

// each update tick (in the hosting frame's Update, before its game logic):
UIViewport Viewport = UIViewport_Create(GameInput_GetWindowSize(input), aspect, placement); // when it changes
UIScreen_SetViewport(&Screen, &Viewport);
UIScreenInput In = UIScreenInput_CreateFromWindow(input);   // full-window UI
UIScreen_Update(&Screen, time, &In);

// each render (into the surface's render context):
UIScreen_Render(&Screen, &renderContext);

// teardown: screens first (destroys their widgets), then the factory
UIScreen_Deconstruct(&Screen);
UIWidgetFactory_Deconstruct(&Factory);
```

**Surfaces.** The screen never reads the window. Each tick its host passes a `UIScreenInput`: the tick's `GameInput`
(may be NULL), whether the pointer is over this screen and where in the screen's own `[0;1]` space, and whether the
screen has the keyboard. `UIScreenInput_CreateFromWindow` makes the full-window case (mouse position divided by the
window size; no pointer while the cursor is captured, outside the window, or already claimed by UI above). For a UI on
a world surface, the host raycasts the pointer onto the surface, passes the hit's UV (or no pointer) and gives the
keyboard to the surface in use. The host also keeps the `UIViewport` (the surface's size in pixels and the fitted-area
settings) current; the Layout pass converts units with it.

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
- **Destroy** (`Widget_Destroy`) is always **deferred** and takes the whole subtree. At once: the subtree is unlinked,
  its ids stop resolving, and the screen forgets it (hover, focus, navigation, mouse presses, animations); no hook runs.
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

1. **Pointer.** When the pointer moved (or the tree or geometry changed), the top-most input-enabled widget under it is
   looked up again and becomes hovered (`OnHoverStart` / `OnHoverEnd`). While the pointer is over a widget the screen
   claims it (`GameInput_ConsumePointer`). Pointer movement ends keyboard navigation (focus stays).
2. **Mouse buttons.** A press over a widget is **consumed**, focuses the nearest focusable widget under the pointer (or
   clears focus), raises that widget's root, and reaches the widget; its release and drags go to that widget even
   outside it. If a focus hook destroys, detaches or disables the widget, the press stays consumed but reaches nobody.
   A press that hits no widget is a **click outside**: it ends navigation and clears focus, and the game gets it. That
   includes any press while the screen has no pointer (the cursor captured for mouse look, or the pointer over UI of a
   higher frame), so clicking into the world blurs the UI.
3. **Wheel.** Wheel movement over a widget is consumed and reaches it.
4. **Keys and text** (when the screen has the keyboard). The **focused** widget gets every key first (Tab, arrows, Enter
   and Escape included), then its input-enabled ancestors, until one sets `*outIsHandled` in `OnKeyboardInput`. A
   handled key is consumed and **owned** by the widget that handled it until it is released: the key's repeats and
   release go to that widget alone, even after focus moved (and are dropped once it is destroyed, detached or
   input-disabled). Typed text follows the same path (if any codepoint is handled, the tick's text is consumed).
5. **Keyboard navigation** acts on the navigation actions nobody handled (`GameAction_UI*`, rebindable), and consumes
   only what it uses:
   - `ui_next` / `ui_previous` (Tab / Shift+Tab) move focus between the current level's focusable widgets in reading
     order (top row first, then left); the arrows (`ui_up` ...) move to the nearest one in that direction. The first
     such key only starts navigation (outlines appear) where focus is. Holding one keeps moving with the key's
     auto-repeat: navigation consumed the key, so it follows that key's repeats itself (with the modifiers held at each
     repeat) until the key is released or navigation ends.
   - `ui_accept` (Enter) enters the focused widget when it contains focusable widgets: it becomes **tabbed** (green
     outline) and focus moves to its first one. This also starts navigation, so Enter opens a container focused by a
     click. Otherwise Enter is left to the game.
   - `ui_back` (Escape) leaves the entered widget (it is focused again); at the top level it ends navigation and clears
     focus; outside navigation it clears focus; with nothing to do it is left to the game (where it is `pause`).
   - Non-focusable widgets are transparent to navigation: their focusable descendants belong to the enclosing level.
   - While navigating the white outline marks the focused widget; navigation re-enters where focus is.
   - The entered (tabbed) widgets always lead to focus. When focus moves without navigation (`UIScreen_SetFocusedWidget`,
     a hook), the next navigation step leaves and enters widgets to match; with nothing focused (the focused widget was
     destroyed) navigation carries on at its level. A widget a hook detaches is never left tabbed or focused.

Whatever is not consumed reaches the game: the frame's game logic (and frames below) read it through the same
`GameInput`. Widgets that need more read the input of the running update with `UIScreen_GetInput`.

**States.** Hovered (pointer), focused (keyboard target; at most one per screen), tabbed (entered by navigation), active
(hovered or tabbed). Widgets get `IsFocusable` from their type (`Widget_SetIsFocusable` changes it); an input-disabled
widget is transparent to the pointer and cannot keep focus.

## Notifications

State changes (`UIEventKind_HoverChanged`, `FocusChanged`, `TabChanged`, `ActiveChanged`) and a widget type's own events
(`UIScreen_PostWidgetEvent`, `UIEventKind_Widget` with a type-defined id and an optional `UIPropertyValue`) are
**queued** and delivered at one point, at the end of `UIScreen_Update` (after the widget updates and layout), never in
the middle of input routing. Subscribe with `UIScreen_Subscribe(screen, widgetId or 0 for every widget, kind mask,
handler, userData, &id)`; a subscription to one widget ends when it is torn down (at the end of the update it was
destroyed in). A `UIEvent` names its widget by id. A state change is queued right after the state changes and before
the widget's hook runs, so when a hook changes the state again (a focus hook moving focus on), the last notification
about a widget always tells its final state. A state change is dropped when its widget is destroyed before delivery; a
widget type's own event is delivered even then (it carries everything by value), so a button that closes its dialog
still reports the click. Handlers may destroy widgets, move focus, start animations, subscribe or unsubscribe, and post
more notifications (delivered in the same pass, at most `UI_SCREEN_MAX_EVENTS_PER_DISPATCH` per update: the rest are
dropped, and logged once per streak of such updates, so handlers that keep posting cannot hang the game or flood the
log). A widget's own vtable hooks still run immediately. The queue is shaped to later post into a game-wide event
queue.

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
`Widget_GetSubWidgetAt` / `UIScreen_GetRootAt` (insertion order) never change meaning. The screen draws roots in draw
order, each widget before its children. `AreSubWidgetsCut` clips a subtree (rendering and hit testing) to the widget's
box through the render context's **clip stack** (`RenderContext_PushClip`), so nested clips intersect and a label's own
cut nests inside its parent's. While navigating, outlines are drawn on top.

## Properties and animation

Properties are typed. Every widget has the base properties (`WidgetBaseProperty_Position` / `Size` (Vector2), `ZLayer`
(float), `RenderTint` (RenderColor)); a type declares its own in its descriptor (`WidgetPropertyDescriptor`: id below
`WIDGET_BASE_PROPERTY_START`, type, name). `Widget_GetProperty` / `Widget_SetProperty` take `UIPropertyValue`s and
reject a value of the wrong type; `Widget_TryGetPropertyType` looks a property up.

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
  the label on success); give it text with `LabelWidget_SetText` or edit the tree in place.

Writing a new widget type: embed `Widget` first; give the type a static `WidgetVTable` and a descriptor (declare its
typed properties there); construct only the concrete part in the constructor; create child widgets in `Initialize`;
report keys you handle through `outIsHandled` (handle a key's press to get its repeats and release); post your own
events with `UIScreen_PostWidgetEvent` (they reach subscribers even if the widget is destroyed in the same update);
never write state in `Render`.

## Memory ownership

The program-wide factory owns every widget's storage (per-type pools). A screen borrows the factory and owns its
registry, queues and widgets' lifetimes. Teardown order at program end: screens (they destroy their widgets) → factory.
Nothing allocates per tick in steady state: the snapshot, navigation candidates, event queue and keyframe storage are
reused; childless widgets allocate no child lists.
