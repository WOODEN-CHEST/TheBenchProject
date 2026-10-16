# SpriteSheet module

Files: `include/SpriteSheet.h`, `source/SpriteSheet.c`. The asset loader that packs sheets is
`source/SpriteSheetDefinition.c` (JSON format: `references/asset_structure.md`, "Spritesheets").

## Purpose

A texture atlas with named regions. The `SpriteSheet` struct is only a view: it borrows one texture and a buffer of
named rectangles. Sheets are normally assets (`AssetManager_LoadSpriteSheet`), built by packing separate image files
into one texture. Sprite animations can use sheet images as frames ([SpriteAnimation.md](SpriteAnimation.md)).

## Types

- `SPRITE_SHEET_ENTRY_MAX_NAME_LENGTH` (128): the size of an entry's name buffer including the NUL, so a name is at
  most 127 bytes of UTF-8.
- `SpriteSheetEntry`: `_name` (stored inline, NUL-terminated UTF-8) and `_textureArea` (the image's rectangle in
  texture **pixels**, padding excluded).
- `SpriteSheet`: `_texture` (the atlas `Texture2D`, borrowed) and `_entries` (a borrowed `GenericBuffer` of
  `SpriteSheetEntry`).

## API

- `SpriteSheet_Construct1(self, texture, entries)`: keeps the texture handle and the entry-buffer pointer and owns
  neither. `ErrorCode_IllegalArgument` for a NULL `self` or `entries`.
- `SpriteSheet_Deconstruct(self)`: zeroes the struct. Frees neither the texture nor the entries.
- `SpriteSheet_GetTextureArea(self, entryName, &Area)`: exact-match linear search; the **first** entry with the name
  wins. `Area` is zeroed first and stays zeroed when no entry matches (`ErrorCode_InvalidOperation`).
- There is no texture getter: read `Sheet->_texture` (read-only), as `SpriteAnimationDefinition.c` does.

## Loaded sheets (SpriteSheetDefinition.c)

Definition keys:
- `images` (required, at least one): each `{ "name": string, "location": file location }`. The location must be a
  file path (a reference is rejected when the definition is read) and resolves with the texture extensions
  (`png` `bmp` `gif` `qoi` `dds`, see [AssetManager.md](AssetManager.md)). Image names are free UTF-8, not asset names.
- `padding_pixels` (integer vector, default (0, 0)): a border added to both sides of each image on each axis. Negative
  values count as 0; more than `SPRITE_SHEET_MAX_PADDING_PIXELS` (64) on either axis rejects the definition.
- `padding_color`: `nearest_pixel` (default; replicates the edge and corner pixels into the border), `transparent`, or
  `nearest_pixel_transparent`, which is **currently identical to `nearest_pixel`** (zero-alpha edges are a TODO in the
  loader).
- `texture_properties.filtering`: `point` or `bilinear` (default), applied to the atlas.

Loading:
1. Each image is loaded with `LoadImage` and copied into a padded canvas. A padded image larger than
   `SPRITE_SHEET_MAX_ATLAS_DIMENSION` (8192) on a side fails the load (`ErrorCode_InvalidAssetData`).
2. A shelf packer places the padded images in definition order, left to right, and starts a new row when the next
   image would push a non-empty row past `SPRITE_SHEET_MAX_ATLAS_WIDTH` (2048 px). An atlas taller than 8192 fails the
   load ("split it into several sheets"). The atlas is as wide as its widest row, so a single image wider than 2048
   widens it. All sizes are checked in 64-bit before raylib's `GenImageColor`, which sizes its buffer in `int`.
3. The images are drawn into one atlas image, which is uploaded with `LoadTextureFromImage` and given the filter. No
   mipmaps are generated.
4. Entries: an image's area is its packed position plus the padding, at its original size. A name of 128 bytes or more
   fails the load (checked at load time, not when the definition is read). Duplicate image names are not detected; a
   lookup returns the first.

The loaded asset owns the atlas texture (`UnloadTexture`) and the entry buffer (borrowed from the manager's pool and
returned on unload). Every failure path unloads the images loaded so far.

## Rules

- A loaded sheet is a shared, read-only asset. Don't keep the `SpriteSheet*` or a copy of `_texture` past your asset
  user (lifetime rules in [AssetManager.md](AssetManager.md)). An animation with sheet frames holds its sheets through
  its dependency user, so holding the animation is enough to keep its frames valid.
- The padding stops neighbouring images from bleeding in under bilinear filtering; entry areas never include it.
- A sheet built in code needs a texture and an entry buffer that outlive it; whoever created them frees them.
