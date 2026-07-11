# UI Framework

The in-game UI framework. It is renderer-independent in its update/logic path (widgets never touch
raylib input or drawing directly) and revolves around a **screen** context object that owns a factory,
routes input, drives keyboard navigation, hosts property animations, and renders a tree of **widgets**.

This document covers the whole framework, which spans several modules (mirroring how `WorldData.md`
covers the world data layer). Only the framework and the abstract widget base exist so far — concrete
widget types are to follow.

All positions and sizes are **window-relative normalized `[0;1]`**: `(0,0)` is top-left, `(1,1)` is
bottom-right. A widget's position/size are relative to its parent (or the screen, for a root), so a child
at `p` inside a parent at `P` with size `S` sits at absolute `P + p*S`.

Everything that can fail returns an `Error` (exception-style), and the screen propagates the first error
out of `UIScreen_Update` / `UIScreen_Render`, exactly like `GameFrameManager` treats a frame error as
fatal. Pure value getters (ids, flags, cached bounds) do not return `Error`.

## Modules

- **UIInput** (`UIInput.h`, header-only) — the raylib-free input value types shared by widgets and the
  screen: the `UIKey` and `UIMouseButton` enums, and the argument structs delivered to widget callbacks
  (`WidgetHoverArgs`, `WidgetMouseInputArgs`, `WidgetKeyboardInputArgs`). The screen translates raylib's
  keys/buttons into these; widgets never see raylib input constants.
- **UIAnimation** (`UIAnimation.h/.c`) — keyframed interpolation of a typed property. Provides the easing
  methods (`UIInterpolation`: constant/step, linear, exponential in/out pow 2–5, sine in-out, and a custom
  function pointer), a `UIKeyframe` (time + value + the easing of the segment starting at it), the value
  kind (`UIPropertyType`: float / Vector2 / Color / RenderColor), and `UIAnimation_EvaluateKeyframes`. Pure
  math + data; it holds no running state.
- **UIRenderContext** (`UIRenderContext.h/.c`) — the widget-local drawing device handed to a widget's
  `Render`. It wraps the renderer's `RenderContext`, carries the widget's absolute screen box and the tint
  accumulated from its ancestors, maps widget-local `[0;1]` coordinates onto the screen, and multiplies
  every draw's color by that tint (a per-draw color still multiplies on top). Draw methods: rectangle,
  rectangle outline, line, texture, text, and a world-space model passthrough.
- **UIWidgetFactory** (`UIWidgetFactory.h/.c`) — the registry and producer of widgets. Registers
  capabilities (each minting a capability id) and widget types (each supplying a struct size, an in-place
  constructor, and the `(capabilityId, resolver)` pairs it implements, and minting a type id). It owns one
  object pool per type: `UIWidgetFactory_ConstructWidget` borrows a slot and runs the constructor;
  `UIWidgetFactory_ReturnWidget` (driven by `Widget_Deconstruct`) returns it for reuse. Capability
  resolution is keyed by `(type, capability)`. Lives embedded in a screen (borrows it, doesn't own it).
- **UIWidget** (`UIWidget.h/.c`) — the abstract widget base. A concrete widget embeds a `Widget` as its
  FIRST member and supplies a `WidgetVTable` (all slots optional; a NULL slot is a no-op success). Holds
  identity (unique id + type id + screen + parent), geometry (`Position`, `Size`, `ZLayer`, cached
  estimated bounds that enclose subwidgets), the `RenderTint`, the `IsRendered` / `IsUpdated` /
  `IsInputEnabled` / `AreSubWidgetsCut` flags, the interaction state (`IsHovered` / `IsFocused` / `IsTabbed`
  / `IsActive`), the subwidget list, the four state-change `WREvent`s, and the generic property get/set.
- **UIScreen** (`UIScreen.h/.c`) — the UI context. Owns the factory and the registry of every widget it
  minted, the ordered roots, the focused root, the keyboard-navigation state, and the running animations.
  `UIScreen_Update` snapshots the live widgets, routes input, advances animations, and updates widgets;
  `UIScreen_Render` draws the tree and the navigation outlines. Also exposes focus/z-order control,
  top-most hit testing, coordinate conversion, and the input-state query functions widgets call.

The **Renderer** module gained three 2D primitives used by the framework:
`RenderContext_RenderRectangle`, `RenderContext_RenderRectangleOutline`, and `RenderContext_RenderLine`
(with a matching `_primitiveDrawCount`). Line/outline thickness is a tagged `RenderFloat`, so a relative
thickness stays a constant on-screen size across resolutions.

## Widget lifecycle

`construct → initialize → (update/render/input enable) → … → (disable) → deinitialize → deconstruct`.

- **Construct**: only through the factory. `UIWidgetFactory_ConstructWidget(typeId, args, &widget)` borrows
  pooled storage and runs the type's constructor, which calls `Widget_Construct(&self->Base, &vtable,
  screen, typeId)` (mints the id, registers with the screen, sets defaults) and then fills concrete fields.
- **Initialize**: `Widget_InitializeWidget(widget)` runs the `Initialize` hook, then fires
  `OnUpdateEnable`/`OnRenderEnable`/`OnInputEnable` (the flags default to enabled). Call once before the
  widget is first added.
- **Add / remove**: `UIScreen_AddWidget` / `UIScreen_RemoveWidget` for roots, `Widget_AddSubWidget` /
  `Widget_RemoveSubWidget` for children. A widget may be added and removed any number of times between
  initialize and deinitialize. Removal does not deconstruct.
- **Deinitialize**: `Widget_DeinitializeWidget(widget)` fires the disable hooks (for still-enabled flags)
  in reverse, then `Deinitialize`. Call once when use has ended. Lifecycles do not restart.
- **Deconstruct**: `Widget_Deconstruct(widget)` runs the concrete `OnDeconstruct` hook, detaches from its
  parent, unregisters from the screen (clearing focus/nav references and purging its animations),
  deconstructs the state events and subwidget buffer, and **returns the storage to the factory pool** for
  reuse. Use `Widget_InitializeWidget` / `Widget_DeinitializeWidget` so the ordering is not copy-pasted.

## Factory & capabilities

Register capabilities first (they mint ids used by types), then types:

```c
uint64_t capId; UIWidgetFactory_RegisterCapability(factory, &capId);
WidgetCapabilityEntry caps[] = { { capId, MyWidget_ResolveCap } };  // resolver: void* widget -> cap struct*
uint64_t typeId; UIWidgetFactory_RegisterType(factory, sizeof(MyWidget), MyWidget_Construct, caps, 1, &typeId);
```

A capability is an interface the widget implements; `Widget_GetCapability(widget, capId)` returns a pointer
to the capability struct embedded in that concrete widget (no stability guarantee — it points inside the
live widget). Resolution goes through the widget's own type on the screen's factory.

## Input

The screen (never the widget) polls raylib and routes:

- Hover, clicks and scroll go to the **top-most input-enabled widget** at the pointer; keyboard goes to the
  **focused** widget. A button **release** is delivered to the widget the press **started** on (drag A→B ⇒
  A gets the release), with `DurationSeconds` and the click-start position filled in.
- Clicking a widget focuses its **root** and brings that root to the top; clicking empty space clears focus
  to none.
- Widgets query input state through `UIScreen_IsKeyDown` / `IsKeyPressed` / `IsKeyReleased`,
  `UIScreen_IsMouseButton*`, `UIScreen_GetMousePosition`, `UIScreen_GetScrollDelta`.
- `IsInputEnabled == false` makes a widget transparent to input (skipped in hit testing).

**Keyboard navigation** (screen-owned): **Tab** starts navigation at the top-left-most widget, then cycles
to the next; **arrows** move to the nearest widget in that direction (by estimated-bounds distance);
**Enter** tabs into the target (green outline) and descends into its subwidgets; **Escape** tabs out one
level, and at the top level stops navigation; **any mouse movement** stops navigation. The target has a
white outline, tabbed-in widgets have green outlines, drawn by the screen on top of everything.

## Rendering & z-order

Each widget gets a float `ZLayer` relative to its parent (or the screen for roots). The screen renders
roots in ascending z, and within each subtree draws the widget first, then its children in ascending z —
so for `A(z0){A.A(z0),A.B(z0),A.C(z5)}, B(z1){B.A(z0),B.B(z1)}` the order is `A A.A A.B A.C B B.A B.B`.
`AreSubWidgetsCut` clips a subtree to the widget's box (GPU scissor, intersected for nesting). Each widget
is handed a `UIRenderContext` that applies its accumulated transform and multiplied tint.

## Animation

`UIScreen_StartAnimation(widgetId, propertyId, type, keyframes, count, options, &animId)` drives a widget
property each tick, writing the evaluated value through `Widget_SetProperty`. Base properties (position,
size, z, tint) are addressable via the reserved `WidgetBaseProperty` ids and work with no concrete-widget
code; concrete property ids (below `WIDGET_BASE_PROPERTY_START`) are forwarded to the widget's vtable.
Multiple animations may target the same widget+property; the one added last is applied last and wins.
Removing/deconstructing a widget purges its animations.

## Behavior notes & simplifications (current basics)

- The `IsRendered` / `IsUpdated` / `IsInputEnabled` flags are **per-widget and do not cascade** to
  subwidgets — each widget participates independently. Hit testing treats a widget as present regardless of
  `IsRendered` (obstruction is by z-order).
- Navigation keys (Tab, arrows, Enter, Escape) are consumed by navigation and are **not** forwarded to the
  focused widget; all other keys and text are.
- Pointer positions are normalized against the **window** size, so the screen is assumed to fill the
  window (aspect-ratio fitting for a sub-area is not yet handled).
- The green outline is drawn on every tabbed widget in the path plus a white outline on the current target.
- The screen is a standalone object the caller drives (`Update` / `Render`); it is not yet wired into a
  `GameFrame` host (that arrives with concrete widgets).
- No allocations happen per tick/frame: the update snapshot, z-sort scratch, navigation candidate buffer,
  and animation keyframe storage are all preallocated and reused.

## Memory ownership

- The factory owns each type's pool (and thus every widget's storage); it borrows the screen. The screen
  owns the factory and its registries but **borrows** the widgets (roots and subwidgets are borrowed
  pointers). Deconstruct widgets that own external resources **before** `UIScreen_Deconstruct`, since that
  only releases the pools, not each widget's own events/subwidget buffer.
