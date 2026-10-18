# GameJSON

`include/GameJSON.h`, `source/GameJSON.c`

Shared parsers for the game's common JSON value shapes, so every consumer parses numbers, booleans,
strings, colors, render colors and vectors the exact same way instead of duplicating the logic. It operates
on a WRJSON value tree (from a shared `JSONObjectPool`); it never touches the pool or the text parser.

Implements the value conventions in `references/asset_structure.md` (and the render-color shape in
`references/text_component_structure.md`). **Every bad value raises `ErrorCode_InvalidJSON`**, whatever the
underlying parser reported: a wrong shape, an unparsable numeric string (the WRNumber error's message is kept), an
infinite or NaN decimal, a render color with brightness or opacity outside [0, 1], or a vector component that does not
fit a float. Messages describe the value, not the key: callers that know the key add it (the text codec reports
`"<key>": <message>` with the component's path).

## What it provides

- **Value converters** (from a `JSONObjectValue`): `GameJSON_ValueToInt64` (integer / real / numeric string
  with 0x/0b bases), `GameJSON_ValueToDouble`, `GameJSON_ValueToBoolean` (bool or "true"/"false"),
  `GameJSON_ValueToOwnedString` (heap copy, free with `Memory_Free`).
- **Structured parsers**: `GameJSON_ParseColor` (hex string / `[r,g,b,(a)]` / `{r,g,b,(a)}`),
  `GameJSON_ParseRenderColor` (a plain color, or `{tint, brightness, opacity}`), `GameJSON_ParseVector2`
  (decimal), `GameJSON_ParseVector2Int`.
- **Optional compound readers** (key + default): `GameJSON_ReadOptionalInteger` / `...Double` /
  `...Boolean` / `...Vector2Int`.

## Usage / relationships

- `source/AssetDefinitionCommon.c` (the asset-definition JSON helpers) delegates its generic value parsing
  to this module — the `AssetJSON_*` value/color/vector helpers are thin wrappers over `GameJSON_*`. Asset
  structural parsing (locations, `name`, texture properties) stays in the asset module.
- The text component JSON adapter (`TextComponentJSON.c`) uses the value/color/vector parsers when deserializing
  components, and the font loader's codepoint parser (`FontCodepoints.c`) uses `GameJSON_ValueToInt64`.

Owns nothing; no lifecycle. Include and call.
