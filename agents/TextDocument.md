# TextDocument and TextLayout

How a text component tree ([TextComponents.md](TextComponents.md)) becomes something on screen. `TextDocument` owns one
rich text and everything it needs; `TextLayout` measures and arranges a document once and draws the result as often
as needed. Every symbol has full Doxygen in its header; this doc is the map.

| Module | Files | What it is |
|---|---|---|
| TextDocument | `include/TextDocument.h`, `source/TextDocument.c` | Owns a tree, an asset user, the name→asset bindings and one playback instance per sprite-animation name. |
| TextLayout | `include/TextLayout.h`, `source/TextLayout.c` | Lays a document out (lines, wrapping, shrinking, alignment) into cached glyph and sprite runs, and draws them. |

## TextDocument: one owner per text

A document owns:
- **the tree**: `TextDocument_SetRoot(document, root)` takes a root (returning the previous tree to the factory),
  `TextDocument_TakeRoot` gives it back, `TextDocument_GetRoot` exposes it for editing in place;
- **its own `AssetUserID`** (AssetManager.md rule): the fonts and sprite animations the tree names are loaded under it
  the first time they are resolved, and stay loaded until the document is destroyed;
- **the bindings**: one per resolved name. A name that fails to load is logged once (WARNING) and remembered, so it is
  never retried;
- **one playback instance per animation name**: every sprite component naming `coin_spin` shows the same frame.
  `TextDocument_Update(document, time)` advances them once per tick (it raises their end/loop events);
  `TextDocument_ResolveAnimation` returns an instance, e.g. to subscribe to its events or change its playback.

`TextDocument_Deconstruct` returns the tree, tears down the instances and retires the asset user, so nothing a layout
or a component pointed at survives. Never move or copy a constructed document (layouts point into it).

**Fonts.** `TextDocument_ResolveFont(name)` falls back in this order: the named font, the document's default font name
(`TextDocument_SetDefaultFontName`), raylib's built-in font. A component without a font name starts at the default
font name. raylib's built-in font is not an asset (raylib owns it until `CloseWindow`) and has no glyphs before
`InitWindow`; the document refreshes its copy until it does.

**Translations.** `TextDocument_ResolveTranslation(key)` returns NULL for now (no string tables yet), so translate
components show their fallback, else their key.

**Without an asset manager** (`assets` NULL, e.g. in tests) nothing is ever loaded: text uses the built-in font and
sprites draw nothing, but they still take up their size.

## TextLayout: measure once, draw many times

`TextLayout_Build(layout, document, options)` walks the tree once and produces runs (styled glyph spans and sprite
boxes) with final positions; `TextLayout_Draw(layout, context, args)` only draws them. `TextLayout_Refresh` rebuilds
only when the layout is out of date: another document, the document's revision changed (new root, default font), the
tree's root revision changed (any edit anywhere in the tree), other options (the maximum width, wrapping and shrinking
only count with `HasMaxWidth`), or the last build used a font without glyphs (raylib's built-in font before
`InitWindow`, so text laid out that early is redone once the window exists). Call `Refresh` every update tick; it is
a few comparisons when nothing changed.

**Build in update, draw in render.** Building resolves names through the document, which may load fonts and
animations, so it runs in update code (and needs the window for real fonts). Drawing reads sprite frames but changes
nothing (AGENTS.md rule 3). A layout must not be drawn after its document is destroyed.

### Layout rules
- Flow: a component's own content first, then its children in order. `\n` ends a line; a final `\n` does not add an
  empty line (unless the build tracks offsets: then the caret after it has a line to stand on). A tab is a gap of `TEXT_LAYOUT_TAB_SPACE_COUNT` (4) spaces; other control characters (C0, DEL and C1,
  U+0080-U+009F) are skipped.
- Each glyph is resolved once, through the font's sorted glyph table (O(log n)); a codepoint the font lacks draws as
  `?`. A glyph advances by its font advance plus the component's spacing, never by less than nothing (a negative
  spacing at most stacks glyphs).
- A shadow is drawn at the component's shadow offset, or at its font's (`GameFont_GetShadowOffset`, from the font
  definition's `shadow_offset`) when it sets none, both times its size.
- A line ends at its last glyph or sprite: trailing spaces and the spacing after the last glyph count neither for
  wrapping, shrinking or alignment, nor for underline and strikethrough. Spaces at the start of a line are kept.
- Within a line, shorter runs are **bottom-aligned**. A line is as tall as its tallest run.
- **Options** (`TextLayoutOptions`, start from `TextLayoutOptions_CreateDefault`):
  - `SizeMultiplier` scales every component size (a label's Size);
  - `HasMaxWidth` / `MaxWidth` in normalized-fitted units, compared after the multiplier;
  - `IsWrapEnabled`: break at the last space or tab that follows content on the line, dropping the spaces at the
    break; words made of several components stay together; a word that does not fit on its own line overflows;
  - `IsShrinkEnabled`: scale every line still wider than `MaxWidth` down to fit (applied after wrapping);
  - `Alignment`: left, center or right within the block's width (the widest line);
  - `TracksOffsets`: keep what carets need (below). Off by default, so labels pay nothing for it.
- Units: normalized-fitted, where 1.0 is the height of the render target area on both axes. A string's size is its
  line height; a sprite's size is its box.
- Queries: `TextLayout_GetSize`, `GetLineCount`, `GetLineBounds(i)`, `GetLineAtY(y)` (the line at a height, by a
  binary search: what a view onto part of a long text reads to touch only the lines it shows), `GetRunCount`,
  `GetRunBounds(i)` (block-local boxes), `GetRunShadowOffset(i)` (how far a run's shadow is drawn from it, so how far
  drawn text reaches past its bounds).

### Carets
A build with `TracksOffsets` also keeps the **flow's text** (`TextLayout_GetFlowText`: the string and translate
components' text in flow order, a sprite as one U+FFFC OBJECT REPLACEMENT CHARACTER) and, for every character the
layout shows, where its bytes start, its line and its place. Offsets are bytes of the flow's text.
- `TextLayout_GetCaretBox(offset)`: where a caret before that byte stands, as wide as the character it stands before
  (an overwrite caret's block; none at a line's end). An offset inside a codepoint stands before the next one.
- `TextLayout_HitTest(point)`: the caret place nearest a point: the line whose height holds it, then the nearest place
  between characters (a character's left half before it, its right half after it), never among the spaces a wrap
  dropped. A tab and a sprite are one character each.
- `GetLineAtOffset`, `GetLineOffsets` (a line's start and end as shown: a wrapped line ends before the spaces the wrap
  dropped, so a line's end is never the next one's start), `GetOffsetOnLine(line, x)` (Up and Down with a remembered
  column), `GetRangeBoxOnLine` (a selection's box on one line), `GetCaretExtent` (how far right a caret can stand: the
  widest line with its trailing spaces, which the block's size leaves out; what a view keeping the caret in sight scrolls to).
Characters run left to right: bidirectional text and shaping are not handled.

### Drawing
`TextLayoutDrawArguments` (from `TextLayoutDrawArguments_Create(position)`): the position of the block's origin, the
origin within the block (normalized; also the rotation pivot), a rotation of the whole block, a tint multiplied into
every color, an optional scissor (screen-relative; pushed on the render context's clip stack, so it nests inside
any outer clip, e.g. a parent widget's) and an optional cull box (`HasCullBox`, `CullBox`: the part of the block that
can show, in its own units before the rotation). All shadows are drawn first, then text, underlines and strikethroughs, and sprites. Glyphs are drawn one quad
each through `RenderContext_RenderGlyph` (Renderer.md) with the indices cached at build time, so drawing does no
codepoint lookups. With a cull box, runs wholly outside it and the glyphs of a run outside it are skipped (with a
margin of two font sizes for wide glyphs, bearings and underlines): a view onto part of a long text (a text box)
costs what shows, not the text's length. It does not clip: pair it with a scissor or a clip.

## Example

```c
TextDocument Document;
TextLayout Layout;
Error Result = TextDocument_Construct(&Document, &Factory, Services->Assets, Services->Logger);
// ... check, then:
TextDocument_SetDefaultFontName(&Document, (const unsigned char*)u8"noto_sans");
TextDocument_SetRoot(&Document, Root);          // Root from TextComponentJSON_Deserialize, for example
TextLayout_Construct(&Layout);

// Update, every tick:
TextDocument_Update(&Document, Time);
TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
TextLayout_Refresh(&Layout, &Document, &Options);

// Render:
TextLayoutDrawArguments Arguments = TextLayoutDrawArguments_Create(RenderVector2D_Fitted((Vector2){ 0.02f, 0.02f }));
TextLayout_Draw(&Layout, &Context, &Arguments);

// Teardown: the layout first (it points into the document), then the document.
TextLayout_Deconstruct(&Layout);
TextDocument_Deconstruct(&Document);
```

`WorldTestFrame` does exactly this for its text sample (T toggles it), and `LabelWidget` embeds a document and a
layout (UIFramework.md).
