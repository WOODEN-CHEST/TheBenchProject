# This file describes the JSON structure for asset definitions in this game.

All asset definitions have a root compound { } which has the property "name", its value is a string which
describes the asset's name to access it by.
The name must match `[a-z][a-z0-9_.-]*`: at least 1 character, starting with a lowercase letter a-z, followed only
by a-z, 0-9, '_', '.' and '-'. Uppercase letters, spaces, '/', and any non-ASCII character make the name invalid.
The asset manager checks this for every definition (whatever its type, and also for definitions built in code);
a definition with an invalid name is rejected.

No two asset definitions OF THE SAME TYPE may have the same name. Each one must be unique. Names can
be shared across different asset types, however. How a repeated name is handled:
* Two definitions of the same type and name inside the SAME search root are an error. The definition read first
  stays registered, the other one is skipped, and the error names both files. (Which file is read first depends
  on directory order, so fix the duplicate rather than relying on it.)
* The same type and name in DIFFERENT search roots is the override mechanism: the higher-priority root (lower
  index) wins and the other definition is ignored without an error. Resource packs and mods use this.

For example.
```
{
    "name": "main_background"
    ... // Other stuff afterwards.
}
```

### Bad definition files are skipped, not fatal.
Definitions are read per file. A file that cannot be read, is not valid JSON, fails its type's checks (a bad key,
an unsupported `format`, a padding over the limit...), has an invalid name, or duplicates a name in the same root
is skipped: the game logs an error naming the file and the reason (`Skipped font definition "<path>": <reason>`),
keeps reading every other file and type, and logs a warning with the number of skipped files at the end. One broken
file therefore only costs the asset it defines.

When an asset fails to load later (for example its texture file is missing), the failure is logged once and
remembered: further loads of that asset fail immediately, without touching the disk, until definitions, search
roots or reference resources change (the game can also clear the remembered failures explicitly to retry).


### There are 2 types of asset locations (places they're pulled from).**

**The first location type is the path of a file which contains the asset.**
It is a string path relative to the asset type's root directory. All directory separators are forward slashes '/'.
Relative directory parts like ".." or "." are not allowed, absolute and empty paths aren't either. (The ".."/"."
rule is not enforced by the loader yet, so breaking it is not reported: just don't.)
The path does not contain the file extension, that is resolved automatically (see "How a file location picks a
file" below).
Paths may start with a directory separator, though it is unnecessary.
For example: `dir_a/dir_b/file_name`
The full path this would resolve to would be `{asset_root_dir}/{asset_type_dir_name}/dir_a/dir_b/file_name{found_extension}`

**How a file location picks a file.**
The search roots are tried in priority order. In each root, the candidates are the files in the location's directory
* whose name without its LAST extension equals the location's last part (`file_name` above),
* that are not definition files (`.json` for the standard types), and
* whose extension the loader can read (table below; compared case-insensitively). When the definition has a
  `format`, only that extension is a candidate.

The first root with at least one candidate decides:
* exactly 1 candidate: that file is used;
* 2 or more candidates: loading fails with an error listing them ("location is ambiguous"). Keep only one, or
  pick one with `format` where the asset type has that key;
* no candidate: the next root is tried. If no root has one, loading fails: "no file found", or, when files with
  the right name exist but none has a supported extension, an error listing those files and the supported
  extensions (so a `.jpg` texture gets a clear "unsupported format" message).

Examples (model directory): `ships/ship` picks `ships/ship.obj` even though Blender also wrote `ships/ship.mtl`,
because `mtl` is not a model extension. `a/b.c` only matches files named `b.c.<ext>` (such as `a/b.c.png`), never
`a/b.png`. A leftover `fragment.frag.bak` never collides with `fragment.frag`, since its name without the last
extension is `fragment.frag`.

| Loader | Accepted extensions |
|---|---|
| Textures (sprite sheet images, texture animation frames, model material maps) | `png` `bmp` `gif` `qoi` `dds` |
| Models | `obj` `gltf` `glb` `iqm` `vox` `m3d` |
| Fonts | `ttf` `otf` |
| Sounds | `wav` `ogg` `mp3` `qoa` |
| Shader stages | `vert` `frag` `vs` `fs` `glsl` |

These are exactly the formats the game's raylib build decodes with the function each loader uses (jpg, tga, psd and
hdr readers are not compiled in; BMFont `.fnt` and the `xm`/`mod` tracker formats are only read by raylib functions
the font and sound loaders do not use).


**The second location type is a name which references an asset.**
Sometimes the asset in question is not on the host's file system, for example, a dynamic asset
generated or retrieved at runtime, or an asset which is just a data structure which needs to be constructed.
In these cases, the asset can be referenced by a name (any valid UTF-8 Unicode string). The game, from that
referenced name, resolves what needs to be loaded or created.
This form of location specification is mostly used by the game itself when loading dynamic assets rather than
the asset JSON definitions.
A reference has no file extension, so loaders that need a real file must be told the format:
* Models, fonts and sounds: a reference location REQUIRES the definition's `format` key; without it the definition
  is rejected when it is read.
* Textures (sprite sheet images, texture animation frames, model material maps) have no `format` key, so their
  locations must be file paths; a reference there is rejected when the definition is read.
* Shader stages are read as text and need no format, so references work there as they are.


**Specifying locations.**
When a location needs to be specified, simply using a string value `"..."` defaults to being the path of a file.
Using a compound value `{ ... }` allows to specify both file paths and name references.
Inside the compound there are 2 properties.
* `type`: The type of the location. Must be either `file` for a file path, or `reference` for a reference name.
anything else is invalid.
* `value`: The actual value of the location. Depends on the location type.


## Common data structures.
Below are documented common data structures and objects which may be used in asset definitions and
how they're define.

When parsing strings as numbers, base specifier letter 'x' may be either lowercase or uppercase, and letters
in the number may be in any case too.


### Decimal numbers.
All decimal numbers can be specified in 3 ways:
* A decimal number like `3.5671`
* An integer like `3`
* A whitespace trimmed string like `"9.35"`


### Integer numbers.
All integer numbers can be specified in 2 ways:
* An integer like `3`
* A whitespace trimmed string like `"9"`. May start with base specifiers 0b and 0x to specify the base 2 or 16.


### Booleans
Booleans are either the value `true` or `false`, either as a JSON boolean or written inside of a whitespace trimmed string.


### Colors.
Colors are specified either as a string, compound or array.
The string contains the hex representation of the color.
The channels are specified (in the string) in the order RGBA.

The string must be whitespace trimmed.
* `"#ffffffff"` With a hashtag at the start, for easier pasting from
* `"ffffffff"` Without a hashtag, just a number. May optionally have the base specified 0x present at the start.

As a compound:
```
{
    "r": integer number,
    "g": integer number,
    "b": integer number,
    "a": integer number, // [OPTIONAL]
}
```

As an array
```
[
    r, // integer number,
    g, // integer number,
    b, // integer number,
    a // [OPTIONAL], integer number,
]
```

The alpha channel in all forms may optionally not be present, if it isn't then the color is fully opaque (255).

Every channel is a whole number from 0 to 255. A channel outside that range is an error (it is not clamped), and so is
a hex string with other than 6 or 8 hex digits (`0-9`, `a-f`, `A-F`; no sign, spaces or other characters).


### Vectors.
Vectors have 2 forms, one a compound and one an array.
```
{
    "x": number,
    "y: number
}
```

```
[
    number, // x value
    number // y value
]
```

Both forms are acceptable for both integer and decimal vectors. Integer vectors, however, only accept integer numbers.

### Rectangles.
```
{
    "x": decimal number, // Top left x
    "y": decimal number, // Top left y
    "width": decimal number,
    "height": decimal number,
}
```


### Texture properties.
```
{
    "filtering": "point" or "bilinear" // [OPTIONAL] Defaults to bilinear.
}
```


### Format.
Models, fonts and sounds accept an optional `"format"` string: a file extension WITHOUT the dot (e.g. `"glb"`),
compared case-insensitively. It must be one of the extensions that asset type's loader accepts (see the table
under "How a file location picks a file"); anything else rejects the definition when it is read, with a message
listing the supported values.
* For a file location it narrows the choice to that extension, e.g. `"format": "glb"` picks `ship.glb` when
  `ship.obj` sits next to it.
* For a reference location it is required and names the file type of the referenced data.



## Asset definition structure.

## Spritesheets.

A spritesheet is a collection of images stitched together into 1 texture.
Sprite sheet textures are each sourced from a separate file.
There is no requirement that each image in a sheet has to be of the same dimensions or aspect ratio;
images can vary in size, the spritesheet loading class takes care of packing everything tightly.

Each image in a sheet has a name it can be referenced by, so that it is possible to pick an image
from the sheet individually.

Each image in the sheet can have padding added (on x an y axis, both sides) to account for fucking mipmaps.
The padding is per-image. So if an image has 2x2 padding, then it has 2 pixels of width / height added on all 4 sides.
The color of the pixels depends on the padding color property.
It can be
* Nearest pixel color: The padding pixel color will be the one of the nearest pixel on the image, basically like extending
the edges of the image.
* Transparent: The padding will be transparent.
* Nearest transparent: Same as nearest pixel color, but with an alpha value of 0.

The texture filtering of the spritesheet texture is specifiable.

```
{
    "images": [ // The individual images this sheet is made from.
        {
            "name": "abc123", // Any valid utf-8 unicode string, must be <= 127 bytes in length.
            "location": asset location
        }
    ],
    "padding_pixels": vector, // [OPTIONAL] Integer vector. Negative values count as 0; more than 64 on either axis is an error. If not present, set to (0;0).
    "padding_color": "nearest_pixel" or "transparent" or "nearest_pixel_transparent" // [OPTIONAL]. If not present, defaults to nearest pixel. NOTE: "nearest_pixel_transparent" currently behaves exactly like "nearest_pixel" (the alpha-0 variant is a TODO in SpriteSheetDefinition.c).
    "texture_properties": texture properties // [OPTIONAL]
}
```

Image locations must be file paths (see "Specifying locations"; textures cannot be references).

Size limits: `padding_pixels` is at most 64 per axis (a larger value rejects the definition when it is read).
Every image plus its padding on both sides, and the packed atlas as a whole, must fit in 8192 x 8192 pixels; a
sheet that does not fit fails to load with an error naming the image or the sheet (split it into several sheets).


## Sprite animations.

A sprite animation is a 2D texture animation, the sprites can be sourced
from either a spritesheet, be standalone textures, or mixed (different frames use different sources).

The source of a frame can be specified to either be a texture, in which case this animation owns the
texture asset, or it can be a spritesheet image, then this asset depends on that spritesheet asset.


Sprite animation:
```
{
    "frames": [ // The frames of the animation.
        {
            "source": "spritesheet" or "texture",

            "location": asset location, // The location of the texture OR spritesheet. A texture location must be a file path.

            "name": spritesheet image name // The name of the spritehseet image (as discussed earlier) IF the source is a spritehseet, otherwise not present.

            "texture_properties": texture properties // [OPTIONAL] Present if source is a texture
        }
    ],

    "fps": 15, // [OPTIONAL] Integer, defaults to 0
    "frame_step": 1, // [OPTIONAL] Integer, defaults to 0,
    "is_running": true, // [OPTIONAL] Boolean, defaults to false
    "is_looped": false // [OPTIONAL] Boolean, defaults to false
}
```


## Sounds.
A sound is any audio played in the game. This includes music.

```
{
    "location": asset location, // a wav, ogg, mp3 or qoa file
    "format": "ogg"             // [OPTIONAL] see "Format"; REQUIRED when "location" is a reference
}
```


## Fonts.

```
{
    "location": asset location,          // a ttf or otf file
    "format": "ttf",                     // [OPTIONAL] see "Format"; REQUIRED when "location" is a reference
    "size": integer number,              // [OPTIONAL] pixel height the glyphs are rasterized at, in [8;256]. Text drawn
                                         //            larger than this is upscaled (softer). Default: 32
    "codepoints": [ entry, ... ],        // [OPTIONAL] which characters get glyphs; see below.
                                         //            Default: ["basic_latin", "latin_1_supplement", "latin_extended_a"]
    "texture_properties": texture properties // [OPTIONAL] filtering of the glyph atlas. Default: bilinear
}
```

A font only has glyphs for the codepoints its definition lists; any other character draws as `?`. Each `codepoints`
entry is one of:
* a **block name**: `"basic_latin"` (U+0020-U+007E), `"latin_1_supplement"` (U+00A0-U+00FF), `"latin_extended_a"`
  (U+0100-U+017F, includes the Latvian and Lithuanian letters), `"latin_extended_b"` (U+0180-U+024F) or `"cyrillic"`
  (U+0400-U+04FF). Only blocks in which every codepoint is assigned are named; give other ranges as numbers;
* **one codepoint**, as a number (`8364` for the euro sign);
* a **range** `[first, last]` (inclusive), whose bounds are numbers or numeric strings such as `"0x0370"`.

Entries may overlap; they are merged. `?` (U+003F) and the space (U+0020) are always added: `?` is drawn for every
missing character, and the space's width spaces words. Every codepoint must be within U+0020-U+10FFFF and no range may
include the surrogates U+D800-U+DFFF. `codepoints` may have at most 1024 entries (use ranges for more), a font may have
at most 8192 codepoints, and their count times `size` squared must fit one 8192 x 8192 atlas (for example about 4000
codepoints at 128 px); a definition that breaks these rules is rejected when it is read. When the font loads, the game logs one
WARNING if the font file lacks some of the requested codepoints, listing the first few; they draw as `?`.

Memory: roughly `size`² x 2 bytes per glyph, so the default set at 32 px is about 1 MB, and four blocks at 48 px about
4-6 MB.


## Shaders.

```
{
    "name": "world_pbr",
    "vertex_location": asset location,   // [OPTIONAL] the vertex-stage source; omit to use Raylib's default
    "fragment_location": asset location  // [OPTIONAL] the fragment-stage source; omit to use Raylib's default
}
```

At least one of `vertex_location` / `fragment_location` must be present. A bare `"location"` is accepted as a
back-compat alias for `fragment_location` (the original single-stage behavior). Every shader-stage extension
(`vert`, `frag`, `vs`, `fs`, `glsl`) is accepted for either stage, so two stage files with the same name but
different extensions would make the location ambiguous: give the two stage files DISTINCT names — e.g.
`shaders/pbr/vertex.vert` and `shaders/pbr/fragment.frag`, referenced as `"pbr/vertex"` and `"pbr/fragment"`.


## Models.

```
{
    "name": "player_character",
    "location": asset location,          // the model file (obj / gltf / glb / iqm / vox / m3d)

    "transform": {                       // [OPTIONAL] baked import transform applied at load
        "scale": number or vector3,      // uniform if a single number
        "rotation_euler": vector3,       // degrees
        "translation": vector3
    },

    "material_overrides": [              // [OPTIONAL] rebind material slots to game-managed assets
        {
            "slot": 0,                   // material index within the model (REQUIRED, >= 0). Addressing a
                                         // material by name is not implemented yet; use the index.

            // --- PBR texture maps (all [OPTIONAL]; each a texture FILE location, resolved against the models dir) ---
            "albedo": asset location,    // base colour (sRGB)
            "normal": asset location,    // tangent-space normal map (the shader derives a TBN per-fragment, so
                                         // the mesh needs no tangents)
            "mra": asset location,       // packed ORM map: R=occlusion, G=roughness, B=metallic. MULTIPLIES the
                                         // metallic/roughness/occlusion factors below (glTF metal-rough style).
            "emissive": asset location,  // sRGB emissive colour map; MULTIPLIES emissive_color*emissive_intensity

            // --- PBR scalar factors (all [OPTIONAL]) ---
            "tint": color,               // multiplies the albedo (combined with the object's own tint)
            "metallic": number,          // 0..1 base metallic factor (default: importer value, else 0)
            "roughness": number,         // 0..1 base roughness factor (default: importer value, else 0.5)
            "emissive_color": color,     // emissive colour (default WHITE when an emissive map is present)
            "emissive_intensity": number,// emissive multiplier (default 0; defaults to 1 when an emissive map
                                         // is present, so the map is visible without extra keys)

            "texture_properties": texture properties  // [OPTIONAL] filtering, applied to all this slot's maps
        }
    ],

    "format": "glb",                     // [OPTIONAL] see "Format": file extension WITHOUT the dot, one of
                                         // obj / gltf / glb / iqm / vox / m3d. For a file location it
                                         // narrows the file choice to that extension. REQUIRED when
                                         // "location" is a reference (in-memory) model, since the format
                                         // can't be inferred from bytes. Reference models must be
                                         // self-contained single files (.glb recommended).

    "rig": "none"                        // [OPTIONAL, GLTF only] "none" (default) | "skinned"
                                         // present only if/when skeletal support lands; OBJ ignores it
}
```