# Text component serialization

Serializes and deserializes text component trees (see `agents/TextComponents.md`) to and from the game's
three formats, plus a helper that binds a parsed tree's asset references to live handles. Every symbol has
full Doxygen in its header; this doc is the map.

## Modules

| Module | Files | What it is |
|---|---|---|
| TextComponentText | `include/TextComponentText.h`, `source/TextComponentText.c` | Component → plain UTF-8 string (no deserializer). |
| TextComponentJSON | `include/TextComponentJSON.h`, `source/TextComponentJSON.c` | Component ⇄ WRJSON value tree. |
| TextComponentGHDF | `include/TextComponentGHDF.h`, `source/TextComponentGHDF.c` | Component ⇄ GHDF compound tree. |
| TextComponentResolver | `include/TextComponentResolver.h`, `source/TextComponentResolver.c` | Binds font/animation names to live GameFont/SpriteAnimationInstance handles. |

JSON is the human-facing form (modding files, console commands); its structure is documented in
`references/text_component_structure.md`. GHDF is the compact binary-storage form (internal integer field
IDs; not user-facing). Plain text is a lossy "just the strings" extraction.

Both structured serializers convert to/from the format's **struct tree** (JSONCompound/GHDFCompound), NOT
bytes — pair them with `JSON_Serialize`/`JSON_Deserialize` or `GHDF_WriteDocument`/`GHDF_ReadDocument` to
reach bytes.

## Caller-provided memory

Per the project's reuse policy, the serializers take all their working memory from the caller:

- **Serialize** borrows JSON/GHDF structures from a caller `JSONObjectPool` / `GHDFObjectPool`; the returned
  root is owned by that pool (return it with the pool's `Return*`, or deconstruct the pool).
- **Deserialize** builds components with a caller `TextComponentFactory`, and copies every borrowed string
  (text, font name, animation name) into byte buffers borrowed from a caller `WRBufferPool` — one buffer
  per string, giving stable pointers the components reference. The caller keeps that pool alive while the
  components are used and frees all the strings by deconstructing it. On failure the partial component tree
  is returned to the factory.

## Asset references and the resolver

Components reference fonts and sprite animations by **name** (`StringComponent._fontName`,
`SpriteComponent._animationName`) — this is what serializes, matching how `WorldSpriteObject` references
animations. Deserialization only fills these names; the live `GameFont` / `SpriteAnimationInstance` are left
unbound.

`TextComponentResolver` does the binding, after parsing, wherever the tree needs to become render-ready:

- `TextComponentResolver_ConstructInstancePool` sets up an `ObjectPool` for the sprite instances it creates
  (configured so deconstructing the pool tears down every created instance).
- `TextComponentResolver_ResolveTree(root, assetManager, user, instancePool)` walks the tree: string
  components get their font loaded and set; sprite components (with a name and no instance yet) get their
  animation loaded and a playing instance created from the pool and set. Loads are attributed to `user`
  (release with `AssetManager_ReleaseAllAssetsForUser`). Best-effort — the first error is returned, the rest
  still bind. Keeps serialization decoupled from the asset/runtime layer.

## Shared parsing

The JSON deserializer parses numbers/colors/vectors through the shared `GameJSON` module (see
`agents/GameJSON.md`), the same code the asset-definition parsers use — no duplication. Value-shape errors
surface as `ErrorCode_InvalidJSON`.
