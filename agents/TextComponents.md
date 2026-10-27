# Text components

A renderer-independent, Minecraft-style rich text system. A **text component** is a node that can be rendered inside
text: a string, a translatable string, a sprite animation, or nothing. Components form a **strict tree**: each has an
ordered list of children it owns. Components are not tied to the UI; anything in the game that shows rich text uses
them (labels, papers, prompts, text in the world).

This doc covers the components themselves, their factory and styles. Showing them is the job of `TextDocument` (owns
a tree and the assets it names) and `TextLayout` (measures and draws): see [TextDocument.md](TextDocument.md).
Serialization: [TextComponentSerialization.md](TextComponentSerialization.md). Every symbol has full Doxygen in its
header; this doc is the map.

## Modules

| Module | Files | What it is |
|---|---|---|
| TextComponent | `include/TextComponent.h`, `source/TextComponent.c` | The base type, the four concrete types, the tree operations and every property setter. |
| TextComponentFactory | `include/TextComponentFactory.h`, `source/TextComponentFactory.c` | The only way to create, clone and return components; pools their slots, strings and child lists. Number, codepoint and special-character shorthands. |
| (private) | `source/TextComponentInternal.h` | Glue between the two files above (component initialization, the factory's buffer and revision services). |
| TextStyle | `include/TextStyle.h`, `source/TextStyle.c` | A presence-guarded bundle of style properties applied onto components. |

## The types

`TextComponent` is the abstract base (vtable with `Destroy`, the type, the owning factory, the parent, the child list,
the revision, the subtree height), embedded as the first member of every concrete type. Downcast with
`TextComponent_AsStringComponent` / `AsSpriteComponent` / `AsTranslateComponent` (and their `Const` variants), which
return NULL for the wrong type; upcast with `StringComponent_AsComponent` and friends.

- **StringComponent**: text, font name (NULL = the document's default font), color, size (the line height, normalized
  fitted units), shadow (active, default or custom color, default or custom offset relative to the size), underline,
  strikethrough, spacing (multiplier of the size). The shadow offset's default is the font's (`GameFont_GetShadowOffset`,
  set by the font definition's `shadow_offset`), because how far a shadow can move before it looks like a second copy
  depends on the font's stroke weight: `StringComponent_SetShadowOffsetCustom` / `SetShadowOffsetDefault`, like the
  shadow color's `SetShadowColorCustom` / `SetShadowColorDefault`.
- **TranslateComponent**: a key looked up in the document's translations, like Minecraft's `translate`. It embeds a
  `StringComponent` as its first member: that string part carries all the styling, and its text is the **fallback**,
  shown while the key has no translation (without a fallback the key itself is shown). The `StringComponent_*`
  setters work on `TranslateComponent_AsString(translate)`. There are no string tables yet, so the fallback always
  shows.
- **SpriteComponent**: a sprite-animation asset name (NULL = draws nothing), color and box size. A sprite always takes
  up its size in the layout, even when its animation is missing; it draws its document's instance of the animation.
- **EmptyComponent**: no content of its own; it lays out its children.

Defaults and valid ranges are macros in `TextComponent.h` (`STRING_COMPONENT_DEFAULT_*`, `TEXT_COMPONENT_SIZE_MAX`,
`STRING_COMPONENT_SHADOW_OFFSET_LIMIT`, `STRING_COMPONENT_SPACING_MIN/MAX`). Every setter validates
(`ErrorCode_ArgumentOutOfRange`, `ErrorCode_InvalidTextEncoding` for text that is not UTF-8), and the codec enforces the
same ranges, so whatever a setter accepts survives a round trip. Render colors need brightness and opacity in [0, 1].
Font names, animation names and translation keys follow the asset-name grammar `[a-z][a-z0-9_.-]*`.

## The strict tree

- **A parent owns its children.** Destroying a component destroys its subtree. `TextComponent_AddSubComponent` and
  `InsertSubComponent` take a **root** (no parent) from the **same factory**; anything else fails
  (`ErrorCode_InvalidOperation` for a child that has a parent or would create a cycle, `ErrorCode_IllegalArgument` for
  another factory), and on failure the child stays the caller's.
- **Taking children out:** `TextComponent_DetachSubComponentAt` (or `TextComponent_Detach(child)`) hands the child back
  to the caller as a root; `TextComponent_DestroySubComponentAt` and `TextComponent_ClearSubComponents` return the
  children to the factory.
- **Depth limit.** A tree has at most `TEXT_COMPONENT_MAX_DEPTH` (64) levels; a root alone is one level. An add that
  would exceed it fails. Each component keeps its subtree height, so the check is O(depth). Both deserializers apply
  the same limit, so every walk over a tree is bounded (AGENTS.md rule 5).
- **Roots** are owned by whoever created or detached them: add them to a parent, hand them to a `TextDocument`
  (`TextDocument_SetRoot`), or return them with `TextComponentFactory_ReturnTree`, which refuses a component that still
  has a parent.
- **Revisions.** Every change (a property, a child added, detached or destroyed) gives the component and all its
  ancestors a new revision, unique across the factory. A changed root revision therefore means "something in the tree
  changed"; `TextLayout` relies on it to know when to lay the text out again.

## Strings are owned

Components copy every string they are given (text, font and animation names, translation keys) into buffers borrowed
from their factory's buffer pool, and return them when they are destroyed. Callers never keep strings alive for
components. Getters return pointers into that storage, valid until the property changes or the component is destroyed.
`StringComponent_GetText` never returns NULL (an unset text is `""`).

## Assets by name only

Components never hold fonts or sprite animations: they name them. The `TextDocument` that owns the tree binds the names
to loaded assets when the text is laid out (AGENTS.md rule 1: whatever keeps asset data owns its own asset user). So a
component can be created before the window exists, a font can be missing (it falls back), and destroying the document
releases everything at once.

## Factory

`TextComponentFactory` owns one object pool per component type and one `WRBufferPool` for everything the components own
(their strings and child lists), so building and tearing down trees allocates little once the pools are warm.

- **Creation:** `CreateEmpty`, `CreateString(text)`, `CreateSprite(animationName)`, `CreateTranslate(key, fallback)`.
- **Shorthands** (string components with default styling): `CreateSpace`, `CreateTab`, `CreateNewline`,
  `CreateInt64` / `CreateUInt64` (+ `...Formatted` with a base and prefix), `CreateDouble` (+ `...Formatted` with
  `DecimalFormatOptions`), `CreateCodepoint`. Each formats into a factory scratch buffer and the component copies the
  text, so two numbers never share storage.
- **Clone:** `TextComponentFactory_CloneTree` deep-copies a tree (also from another factory) into a new root.
- **Return:** `TextComponentFactory_ReturnTree(root)`.
- `TextComponentFactory_GetLiveComponentCount` counts components not yet returned (a leak check; the tests use it).
- `TextComponentFactory_Deconstruct` releases all pooled storage, also that of components still alive.

## Styling

`TextStyle` is a superset of the style properties (not a union), each guarded by a presence flag, with `Set` / `Get` /
`Clear` per property. `TextStyle_ApplyToComponent` applies only the present properties that fit the component's type:
string properties to string components and to the string part of translate components, color and sprite size to
sprites, nothing to empty components. It uses the components' setters, so invalid values (including an invalid font
name) surface as errors. The style **borrows** its font name (normally a string literal); the component copies it.

## Notes

- Switches over `TextComponentType` have no `default:` (AGENTS.md rule 4), so adding a type fails the build where it is
  not handled yet: the factory pools, `TextComponent.c` destruction, the codec tables, the layout, the plain-text
  serializer and `TextStyle`.
- Not thread-safe; main thread only.
