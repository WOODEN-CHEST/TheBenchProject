# Text component serialization

Turns text component trees (see [TextComponents.md](TextComponents.md)) into the game's formats and back. JSON is the
human-facing form (content files, console commands; the shape is in `references/text_component_structure.md`), GHDF
is the compact binary storage form, and plain text is a lossy strings-only extraction. Every symbol has full Doxygen in
its header; this doc is the map.

## Modules

| Module | Files | What it is |
|---|---|---|
| TextComponentJSON | `include/TextComponentJSON.h`, `source/TextComponentJSON.c` | Component tree ⇄ WRJSON value tree (the JSON adapter of the codec). |
| TextComponentGHDF | `include/TextComponentGHDF.h`, `source/TextComponentGHDF.c` | Component tree ⇄ GHDF compound tree (the GHDF adapter of the codec). |
| TextComponentCodec (private) | `source/TextComponentCodec.h`, `source/TextComponentCodec.c` | The one codec both formats use: every type's fields, validation, error messages, the tree walks. |
| TextComponentText | `include/TextComponentText.h`, `source/TextComponentText.c` | Component tree → plain UTF-8 (no deserializer). |

The structured serializers convert to and from the format's value tree, not bytes: pair them with `JSON_Serialize` /
`JSON_Deserialize` or `GHDF_Write` / `GHDF_Read`.

## One codec for both formats

`TextComponentCodec.c` lists every component type's fields **once**: the JSON key (and the group it sits in, such as
`shadow`), the GHDF entry id, the value kind, whether it is required, and the valid range. The codec walks the tree,
validates every value against its field, applies it through the components' setters and builds the error messages.
A format adapter only moves values in and out of its document tree through two small interfaces
(`ITextCodecReader` / `ITextCodecWriter`), so JSON and GHDF cannot drift apart: the same fields, defaults, ranges and
rules apply to both. Adding a field means one table row (plus its two cases in the codec's get/set switches).

The table could later be generated from the property descriptors of architecture decision A1; until then it is
hand-written (the owner's decision of 2026-10-18).

## Writing

- Only fields that differ from their defaults are written; required fields (a string's `text`, a translate
  component's `key`) always are.
- JSON uses the shorthands where they fit: a default-styled string component without children becomes a bare string,
  and an empty component becomes an array of its children. An opaque full-brightness color is written as `"#rrggbb"`,
  anything else as `{ "tint": [r, g, b], "brightness", "opacity" }`.
- GHDF writes every component as a compound: entry 1 is the type as an explicit wire id (1 empty, 2 string, 3 sprite,
  4 translate; independent of the C enum's values), entry 2 the children (an array of compounds), and the field ids
  listed in `TextComponentCodec.c`. A custom shadow color is stored as entry 16; its absence means the default.
- Serializers borrow everything from the caller's `JSONObjectPool` / `GHDFObjectPool`; on failure everything built so
  far goes back to the pool.

## Reading

- The components are created by the caller's factory and own copies of every string, so the value tree can be
  released right after the call. The result is a root owned by the caller (normally handed to a `TextDocument`).
- Unknown keys (and unknown GHDF ids) are ignored, for forward compatibility.
- Every bad value fails the whole read: `ErrorCode_InvalidJSON` for JSON, `ErrorCode_Deserialize` for GHDF. That covers
  wrong shapes, out-of-range numbers, invalid names, text that is not UTF-8 or contains NUL, unknown types, missing
  required fields, and nesting deeper than `TEXT_COMPONENT_MAX_DEPTH` (64) levels, which is rejected before anything
  deeper is read.
- The message names the field and the component's path as a JSON pointer, e.g.
  `Text component at "/children/1/children/0": "size": must be a decimal within [0, 100].` (`/<i>` for an entry of an
  array shorthand, "at the root" for the root).
- On failure nothing is left allocated.
- WRFramework's own readers stop at 512 levels (`JSON_MAX_NESTING_DEPTH` / `GHDF_MAX_NESTING_DEPTH`), so a hostile file
  cannot overflow the stack before the codec's 64-level check sees the tree.

## Plain text

`TextComponentText_Serialize` appends the text of every string component, depth first (own text, then children). A
translate component contributes its fallback, or its key when it has none (what a document without translations
shows); sprites and empty components contribute nothing. It does not clear or NUL-terminate the destination.

## Asset references

Components refer to fonts and sprite animations by name (`font`, `animation` keys). Reading only stores the names;
binding them to assets is the job of the `TextDocument` the tree is given to ([TextDocument.md](TextDocument.md)).
