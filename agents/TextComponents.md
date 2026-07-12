# Text components

A renderer-independent, Minecraft-style rich text system. A **text component** is an abstract, mutable
node that can be rendered inside text — it may carry a string, a sprite animation, or nothing, and it
holds an ordered list of child components, so components chain into a tree. Components are **not** tied to
the UI; they are used across the game. This layer covers creation/pooling, styling, and rendering.

Every symbol has full Doxygen in its header; this doc is the map.

## Modules

| Module | Files | What it is |
|---|---|---|
| TextComponent | `include/TextComponent.h`, `source/TextComponent.c` | Abstract base (`TextComponent`) + the three concrete types (`StringComponent`, `SpriteComponent`, `EmptyComponent`) + subcomponent management. |
| TextComponentFactory | `include/TextComponentFactory.h`, `source/TextComponentFactory.c` | The only way to create/clone/return components; pools the structs and subcomponent buffers; number/codepoint/special-char helpers. |
| TextStyle | `include/TextStyle.h`, `source/TextStyle.c` | Presence-guarded bundle of style properties applied onto components. |
| TextComponentRenderer | `include/TextComponentRenderer.h`, `source/TextComponentRenderer.c` | Measures and draws component trees through a `RenderContext`. |

## Object model

`TextComponent` is an abstract vtable class (`Destroy` slot) embedded as the FIRST member of each concrete
type, so any concrete pointer is a valid `TextComponent*`. A `TextComponentType` discriminator drives
downcasting, rendering and (future) serialization. Shared state: the vtable, the type, and the borrowed
ordered subcomponent list (`_subComponents`, element `TextComponent*`, owned by the factory).

- **StringComponent**: borrowed UTF-8 `_text` (encoding-validated on set, never owned), `_font`
  (default raylib font), `_color` (white), `_size` (0.1), `IsShadowActive` (true), a `TextShadowColorType`
  shadow-color mode (Default = derive from color at render time / Custom = explicit `_shadowColor`),
  `_shadowOffset` (-0.1, 0.1, relative to the size), `IsUnderlined`/`IsStrikethrough` (false), `_spacing` (0).
- **SpriteComponent**: borrowed `_animationInstance` (may be NULL = renders nothing), `_color`, `_size` (a
  Vector2).
- **EmptyComponent**: no own content (but may still hold children).

Properties are **per-component** and are NOT inherited by children. Validated numeric/color/text
properties use validating setters + inline getters; the plain bool flags (`IsShadowActive`,
`IsUnderlined`, `IsStrikethrough`) are public fields.

**Subcomponents** (`TextComponent_Add/Insert/RemoveAt/Remove/Clear/GetCount/GetAt`): children are
referenced, not owned. Add/insert reject cycles — a component may not be added to itself or to any
component in its own subtree (`ErrorCode_InvalidOperation`); `TextComponent_IsInSubtree` exposes the
reachability test. Remove/clear only drop references; they do not return children to the factory.

## Factory, pooling and ownership

Components are created, cloned and released **only** through the `TextComponentFactory`. It owns an
`ObjectPool` per component type (structs) and a `WRBufferPool` for the subcomponent list buffers, so
building/tearing down is allocation-light. The concrete constructors in `TextComponent.h` are the
factory's building blocks, not end-user API.

- **Creation**: `CreateEmpty`, `CreateString`/`CreateStringStyled`, `CreateSprite`/`CreateSpriteSized`.
- **Special characters**: `CreateSpace`/`CreateTab`/`CreateNewline` (backed by static string literals).
- **Numbers/codepoints**: `CreateInt64`/`CreateUInt64`/`CreateDouble` (+ `...Styled` variants with base/
  format options and font/color/size), and `CreateCodepoint`/`CreateCodepointStyled`. These format their
  text into a **caller-owned** byte buffer (element size 1) that the component borrows — the factory never
  owns strings. Use one buffer per generated string and keep it alive, unreallocated, while the component
  references it.
- **Clone**: `CloneComponent` deep-clones the component and its whole subtree; borrowed strings/sprite
  instances are shared (not copied).
- **Return**: `ReturnComponent` recycles one component's struct + subcomponent buffer (NOT its children);
  `ReturnComponentTree` returns a component and its whole subtree (children first) — only safe when the
  caller exclusively owns that subtree.

The factory owns only component structs and subcomponent buffers. Strings, textures and sprite instances
stay owned by the caller and must outlive the components that reference them. A component may be referenced
from many places; the caller decides when each is returned.

## Styling

`TextStyle` is a superset (not a union) of style-related properties, each guarded by a presence flag, with
`Set`/`Get`/`Clear` per property. `TextStyle_ApplyToComponent` applies only the present properties that are
relevant to the component's concrete type (e.g. string size is ignored by sprites; color applies to both),
using the components' validating setters. The shadow-color directive is a set-Default / set-Custom / clear
tri-state.

## Rendering

`TextComponentRenderer` is a small reusable object (owns scratch buffers; construct once, reuse across
frames). It flattens the component + subtree into inline runs and lays them out: own content first then
children, left to right, breaking on `\n`; within a line, shorter runs are BOTTOM-aligned.

- **Units**: all component sizes and the measured size are in **normalized-fitted** units where 1.0 = the
  full target-area height, for BOTH axes (string size = line height; sprite size = box, both as fractions
  of target-area height), so text and sprites compose on one scale. Measurement
  (`TextComponentRenderer_MeasureComponent`) is render-context-free; drawing converts to pixels.
- **Draw args** (`ComponentRenderArguments`, a struct so it can grow): position, additional size
  multiplier (multiplies component sizes), tint (multiplied into component colors), rotation and relative
  origin (both applied to the whole composed block), and an optional cached render size.
- **Shadows**: same text drawn behind at an offset relative to the component size; excluded from
  measurement and origin; default color = component color with brightness × 0.25.
- Underline/strikethrough draw as line primitives. The renderer never touches the render target or
  shaders — it is an advanced draw-text call.

## Notes / conventions

- Cycle violations, unknown types and "not a child" use `ErrorCode_InvalidOperation` /
  `ErrorCode_IllegalArgument`; text encoding uses `ErrorCode_InvalidTextEncoding`.
- More component types are expected later; each embeds the base and registers a pool/size in the factory.
- Serialization/deserialization is planned as a follow-up (the `TextComponentType` discriminator and the
  plain-data property layout are there to support it).
