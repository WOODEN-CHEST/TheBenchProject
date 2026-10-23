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
| Textures (standalone textures, sprite sheet images, texture animation frames, model material maps) | `png` `bmp` `gif` `qoi` `dds` |
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
* Textures (standalone textures, sprite sheet images, texture animation frames, model material maps) have no `format`
  key, so their locations must be file paths; a reference there is rejected when the definition is read.
* Shader stages are read as text and need no format, so references work there as they are. (A shader's `#include`
  lines always name files.)


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
    "filtering": "point" or "bilinear" or "trilinear", // [OPTIONAL] Defaults to bilinear. Trilinear needs "mipmaps": true.
    "wrap": "repeat" or "clamp" or "mirror_repeat" or "mirror_clamp", // [OPTIONAL] Defaults to repeat.
    "mipmaps": boolean // [OPTIONAL] Defaults to false. Generates the mip levels at load (a dds file's own are kept).
}
```

Every key is optional, and leaving one out keeps the behaviour textures had before the key existed.
* `filtering`: `point` keeps hard pixels, `bilinear` smooths between texels, `trilinear` also blends between mip levels
  (the smoothest choice for textures seen at many distances, such as a sky panorama).
* `wrap`: what texture coordinates outside 0..1 read: `repeat` tiles, `clamp` stretches the edge texels,
  `mirror_repeat` tiles mirrored, `mirror_clamp` mirrors once and then clamps (it needs a GPU with the
  GL_EXT_texture_mirror_clamp extension; without it the texture keeps repeating and raylib logs a warning).
* `mipmaps`: smaller copies of the texture (half size, quarter size, ...) that the GPU uses for distant or small
  surfaces, so a texture seen from afar does not shimmer at the game's 640x360. A mip chain costs one third more
  memory. A compressed dds file cannot get generated mip levels: it must contain its own, or the load fails.

Not every asset applies every key. Standalone textures and model material maps apply all three. Sprite sheets,
sprite animation frames and fonts apply only `filtering`, and they reject `trilinear` (they never have mipmaps);
`wrap` and `mipmaps` are still checked there but have no effect. A bad value of any key rejects the definition.


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
    "shadow_offset": vector              // [OPTIONAL] where text in this font draws its shadow when the text sets no
                                         //            offset of its own: a decimal vector relative to the text size,
                                         //            each component in [-10;10]. Default: [-0.1, 0.1]
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

`shadow_offset` belongs to the font because a shadow only reads as a shadow while it overlaps the strokes: moved further
than about one stroke width, it looks like a second copy of the text. A regular-weight font wants about
`[-0.05, 0.05]` (Noto Sans Regular's strokes are 6.5% of the text size); the default `[-0.1, 0.1]` suits bold fonts and
pixel fonts with one-pixel strokes. raylib's built-in font uses the default.


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

### Shared shader code: `#include`.
A stage source may pull in shared GLSL with a line of its own:
```
#include "common/atmosphere"
```
* The argument is a file location like every other one: relative to the `shaders/` directory, without extension,
  found through the search roots (so a resource pack can override a shared file). `"common/atmosphere"` finds
  `shaders/common/atmosphere.glsl`. Shared files live in `shaders/common/` and have no JSON definition: they are not
  shader assets themselves.
* Spaces or tabs may come before the `#`, between `#` and `include`, and before the quote; a `//` comment may follow.
  Anything else on the line is an error.
* Includes nest at most 8 levels deep.
* A file included a second time in the same stage is skipped, as if every file had include guards. So a shared file
  can include what it needs (`common/atmosphere` includes `common/math`) without clashing with the main file's own
  includes.
* An included file must not contain `#version`, and no `#include` may come before the main file's `#version` line,
  which stays the first line.
* Includes are expanded before the GLSL preprocessor runs and line by line: an `#include` inside a block comment or
  an `#if` block that is switched off is still expanded. Keep includes out of conditional blocks.
* Errors stay traceable. Each included file becomes a numbered GLSL source string (the main file is 0, the others
  follow in the order they are first included), so a compiler error such as `1(27)` (NVIDIA) or `1:27(26)` (Mesa)
  means line 27 of file 1, and the load error the game logs lists which number is which file. A missing include fails
  the load with an error naming the include, the including file and the line.

Shared files today: `common/math` (`PI`), `common/atmosphere` (the Rayleigh + Mie sky that the sky pass draws and
the PBR shader's distance fog fades toward), `common/night_sky` (the air mass and extinction near the horizon, and
the profile of light pollution's skyglow), `common/night_vision`, `common/lighting`, `common/distance_packing`,
`common/dither` and `common/skinning` (T29: the bone attributes, the bone palette and a vertex's blended skinning rows,
included by every skinned vertex stage; `agents/Animation.md`).

**Skinned twins (T29).** Every world geometry shader that draws models has a twin for skinned meshes, named with
`_skinned`: `world_pbr_skinned`, `world_normal_skinned`, `world_depth_skinned`, `world_cube_depth_skinned` (each its
directory's `skinned.vert` with the rigid shader's own fragment stage) and `world_unlit_skinned` (`unlit/`, for
models drawn without the PBR shader). A resource pack that replaces a rigid shader's fragment stage changes its twin
with it; one that replaces a vertex stage should replace the twin's too.


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
            "slot": 0,                   // material index within the model (REQUIRED, 0 to 2147483647). A slot
                                         // past the model's materials is skipped. Addressing a material by
                                         // name is not implemented yet; use the index.

            // --- PBR texture maps (all [OPTIONAL]; each a texture FILE location, resolved against the models dir) ---
            "albedo": asset location,    // base colour (sRGB)
            "normal": asset location,    // tangent-space normal map (the shader derives a TBN per-fragment, so
                                         // the mesh needs no tangents)
            "mra": asset location,       // packed ORM map: R=occlusion, G=roughness, B=metallic. MULTIPLIES the
                                         // metallic/roughness/occlusion factors below (glTF metal-rough style).
            "emissive": asset location,  // sRGB emissive colour map; MULTIPLIES emissive_color*emissive_intensity

            // --- PBR scalar factors (all [OPTIONAL]; world model variants read them with the same parser) ---
            "tint": color,               // multiplies the albedo (combined with the object's own tint)
            "metallic": number,          // in [0, 1]: base metallic factor (default: importer value, else 0)
            "roughness": number,         // in [0, 1]: base roughness factor (default: importer value, else 0.5)
            "emissive_color": color,     // emissive colour (default WHITE when an emissive map is present)
            "emissive_intensity": number,// >= 0: emissive multiplier (default 0; defaults to 1 when an emissive
                                         // map is present, so the map is visible without extra keys)

            "texture_properties": texture properties  // [OPTIONAL] filtering, wrap and mipmaps, applied to all
                                         // this slot's maps (mipmaps keep distant textured models calm)
        }
    ],

    "format": "glb",                     // [OPTIONAL] see "Format": file extension WITHOUT the dot, one of
                                         // obj / gltf / glb / iqm / vox / m3d. For a file location it
                                         // narrows the file choice to that extension. REQUIRED when
                                         // "location" is a reference (in-memory) model, since the format
                                         // can't be inferred from bytes. Reference models must be
                                         // self-contained single files (.glb recommended).

    "rig": "none"                        // [OPTIONAL, GLTF only] not read: a file with a skin is skinned (below)
}
```

**Skinned models (T29).** A glTF (`gltf`, `glb`) file with a skin loads its skeleton and each vertex's bone
indices and weights (at most four bones a vertex) with it: such a model bends with the pose of the object it is placed
on, playing an animation library's clips (`agents/Animation.md`). The skeleton is checked when the model loads (at most
256 bones, every parent a bone listed in the file and no cycle); a mesh with an index past the bones or a weight that is
not a number is drawn as it is (rigid). Bones are found by **name**, so the skeletons of every model that plays one
library must name their bones alike (a tall and a short human with the same bone names play the same walk).


A value outside its range (a metallic of 1.5, a negative intensity) rejects the definition with a message naming the key.
Every number must be finite and fit a float.


## Textures.
A standalone texture: one image file uploaded to the GPU as it is, for things such as sky panoramas, the moon, noise,
normal maps and masks. (Sprites still come from sprite sheets and sprite animations, model maps from model
definitions.) Definitions and images live under `textures/`.

```
{
    "name": "sky_milky_way",
    "location": asset location,              // the image file (png / bmp / gif / qoi / dds); must be a file path
    "texture_properties": texture properties, // [OPTIONAL] filtering, wrap and mipmaps; see "Texture properties"
    "color_space": "srgb" or "linear"        // [OPTIONAL] Defaults to srgb.
}
```

* `color_space` says what the texels are: `srgb` for colour painted on screen (albedo, sprites, the moon, the Milky
  Way), `linear` for data (normal maps, masks, noise). The game tells the shader which one it gets; shaders linearize
  sRGB textures themselves, so the file is uploaded the same way in both cases.
* Size: at most 8192 pixels per side. A larger image fails to load with an error naming its size.
* A gif loads its first frame only.

Example (the Milky Way placeholder, `textures/sky/milky_way.json`):
```
{
    "name": "sky_milky_way",
    "location": "sky/placeholder_milky_way",
    "texture_properties": { "filtering": "trilinear", "wrap": "repeat", "mipmaps": true },
    "color_space": "srgb"
}
```

The moon's face (`textures/sky/moon.json`) is pixel art seen at about its own size, so it keeps hard pixels and no
mip levels:
```
{
    "name": "sky_moon",
    "location": "sky/placeholder_moon",
    "texture_properties": { "filtering": "point", "wrap": "clamp", "mipmaps": false },
    "color_space": "srgb"
}
```
These two are the defaults of a world's `milky_way_texture` and `moon_texture` settings; a world can name other
textures.


## World models.
A world model is the reusable definition of a placeable thing in the 3D world (a desk, a street lamp, a car): which
model to draw at which distance, how far it is seen, the default render settings of the objects placed with it, and its
material variants. It is built on other assets, as a sprite animation is built on sprite sheets: its LOD models are
`model` assets and the maps its variants swap in are `texture` assets, all named by their asset names and loaded with it
as its dependencies (holding the world model holds them; they unload with it). A model stays a plain asset that the UI
can use too; what only matters in the world lives here. Definitions live under `world_models/` and have no resource
files of their own.

A placed object names its world model in its model component (`references/world_structure.md`) and can override each
default setting of this file for that one placement: an explicit override flag next to its own value.

```
{
    "name": "office_desk",
    "lods": [                                      // 1 to 8 LODs, the finest first
        { "model": "office_desk_lod0", "max_distance": 15 },
        { "model": "office_desk_lod1", "max_distance": 40 },
        { "model": "office_desk_lod2", "max_distance": 90 }
    ],
    "draw_distance": 120,                          // [OPTIONAL] metres; default: the last LOD's max_distance
    "is_draw_distance_fixed": false,               // [OPTIONAL] default false
    "lod_fade_seconds": 0.3,                       // [OPTIONAL] default 0.3
    "lod_hysteresis": 0.05,                        // [OPTIONAL] default 0.05
    "render": {                                    // [OPTIONAL] each key [OPTIONAL]
        "has_outline": true, "omit_pixelation": false, "shadow_tier": "both", "casts_shadows": true
    },
    "variants": [                                  // [OPTIONAL] at most 64
        {
            "name": "dark_wood",
            "material_overrides": [ { "slot": 0, "albedo": "office_desk_dark_albedo", "tint": "#d0c0b0" } ]
        }
    ],
    "bounds": { "center": [0, 0.4, 0], "radius": 1.1 }, // [OPTIONAL] default: LOD 0's (a posed object's: its pose's)
    "animation_libraries": [ "human_motions" ]     // [OPTIONAL] at most 16 (T29)
}
```

| Key | Value | Meaning |
|---|---|---|
| `lods` | 1 to 8 `{ "model", "max_distance" }` | The models to draw, the finest first. `model` is a model asset's name; `max_distance` (metres, above 0, rising strictly from one LOD to the next) is how far that LOD is used, at the default 60-degree field of view. Past the last one the last LOD keeps drawing until the draw distance. A placement may give its own distances, scale them or force a LOD (`references/world_structure.md`, the model block); the player's `lod_distance_scale` (`config.json`) multiplies them, and a narrower field of view (zooming in) picks finer LODs. Every LOD should share the same origin, axes, scale and material slots. |
| `draw_distance` | metres | How far objects of this world model are seen: at least the last LOD's `max_distance`; left out, it is that distance. The player's `draw_distance_scale` (`config.json`) multiplies it unless it is fixed, and a narrower field of view (zooming in) pushes it out. Over its last tenth (at least 3 m) the object dithers away. |
| `is_draw_distance_fixed` | boolean | true: the player's `draw_distance_scale` leaves the draw distance as it is (landmarks, gameplay-critical props). |
| `lod_fade_seconds` | seconds, >= 0 | How long a LOD cross-fade takes: both LODs are drawn with complementary pixel dithers meanwhile; 0 swaps at once. |
| `lod_hysteresis` | in [0, 1) | A LOD switches to a coarser one at its switch distance x (1 + h) and back at x (1 - h), so an object standing at a boundary does not flicker. |
| `render` | object | The default render settings of the placements: `has_outline` (default true: a 1-pixel outline), `omit_pixelation` (default false: true draws it at full resolution over the pixelated image, for readable screens), `shadow_tier` (`both` (default), `near`: only the sharp near shadow, for small props, `far`: only the coarse far one, for big objects) and `casts_shadows` (default true). |
| `variants` | at most 64 `{ "name", "material_overrides" }` | Other looks of the same meshes (dark wood, a red car). `name`: `[a-z][a-z0-9_.-]*`, at most 128 bytes, unique in the world model. `material_overrides` (optional; at most 32, one per slot): what the variant changes in each material slot, below. A placed object picks one with `uses_variant` and `variant`. |
| `bounds` | `{ "center": [x, y, z], "radius": metres >= 0 }` | The bounding sphere culling and draw distances use, in the object's own space (after each LOD's import transform). Left out, it is the sphere around LOD 0's mesh bounds, or for an object an animator poses (T29) the sphere around its pose. Give it to keep one fixed sphere whatever the pose. |
| `animation_libraries` | at most 16 asset names, each once | The animation libraries (below, "Animation libraries") whose clips the placements play (T29), loaded with the world model as its dependencies. Two of them may not hold a clip of the same name. The skinned LODs must share one skeleton: every LOD with bones must have the first one's bones and bind poses, or the world model is rejected (a coarse LOD may be rigid). A placement may play its own libraries instead (`references/world_structure.md`, the animator block). |

**A variant's material override** takes the same keys as a model's `material_overrides` and reads them with the same
parser, with one difference: the maps name **texture assets** (`textures/`, see "Textures"), so a texture several
variants or world models share loads once, and its filtering, wrap and mipmaps come from its own definition (there is no
`texture_properties` here).
* `slot` (required, 0 to 2147483647): the material's index in every LOD's model. A slot that no LOD model has changes
  nothing (logged once, when the world model loads).
* `albedo`, `normal`, `mra` (the packed occlusion/roughness/metallic map), `emissive`: texture asset names; a map that is
  left out keeps the model's.
* `tint`, `metallic` and `roughness` (in [0, 1]), `emissive_color`, `emissive_intensity` (>= 0): as for models; an
  emissive map without a colour or intensity of its own shows at white x 1.
A variant is applied when the object is drawn, to the renderer's copy of each material, never to the model asset, so
the same model can show every variant at once.

**Collision** (`collision`, optional, T12): the hitboxes of the objects placed with this world model, as named
**collision sets**. Each set is a compound of shapes placed in the world model's own space (after the LOD models' import
transforms, like the meshes they stand for). A placed object needs a collider component to collide; it uses the default
set, or the one it names (a door swaps between `closed` and `open`), and may override the layer, the mask, the
trigger flag and the mass (`references/world_structure.md`, the collider block).
```
"collision": {
    "default_set": "closed",                       // [OPTIONAL] default: the first set
    "layer": "world",                              // [OPTIONAL] the placements' layer; default "prop"
    "collides_with": ["character", "vehicle"],     // [OPTIONAL] default: the layer's default mask
    "is_trigger": false,                           // [OPTIONAL] default false
    "mass": 1.0,                                   // [OPTIONAL] kilograms, above 0, at most 1000000; default 1 (T27)
    "sets": [                                      // 1 to 32 sets, at most 1024 shapes together
        { "name": "closed", "shapes": [            // 1 to 128 shapes
            { "type": "box", "name": "panel", "center": [0.5, 1, 0], "half_extents": [0.5, 1, 0.05] },
            { "type": "sphere", "name": "handle", "center": [0.9, 1, 0.09], "radius": 0.04 } ] },
        { "name": "open", "shapes": [
            { "type": "box", "center": [0, 1, 0.5], "rotation": [0, 90, 0], "half_extents": [0.5, 1, 0.05] } ] },
        { "name": "walls", "shapes": [ { "type": "mesh", "model": "shop_collision", "is_two_sided": false } ] }
    ]
}
```
`"collision": "bounds"` is the shortcut: one set, `bounds`, holding a box around LOD 0's mesh (exact under its import
transform). Without a `collision` key the world model has no collision.

Every shape has a `type`, an optional `center` ([x, y, z], default the origin), `rotation` (Euler XYZ degrees, default
none) and `name` (the part a hit reports, `[a-z][a-z0-9_.-]*`, at most 128 bytes), and the keys of its type only (a key
of another type is refused, so a typo is caught):

| `type` | Keys | Shape |
|---|---|---|
| `sphere` | `radius` (> 0) | Around the centre. |
| `capsule` | `radius` (> 0), `half_height` (>= 0) | A segment of 2 x `half_height` along local Y swept by the radius; 0 is a sphere. |
| `box` | `half_extents` ([x, y, z], each > 0) | Turned by the rotation. |
| `cylinder` | `radius` (> 0), `half_height` (> 0) | Along local Y. |
| `convex_hull` | `points` ([[x, y, z], ...], 4 to 65536, spanning a volume) or `model` (a model asset: its vertices after its import transform); optional `max_vertices` (4 to 256, default 64) | The convex hull of the points. A finer hull is reduced to `max_vertices`, keeping its extremes. |
| `plane` | optional `is_bounded` (default true), `half_extents` ([x, z], each > 0, required while bounded), `depth` (>= 0, default 0) | The front is local +Y. Bounded: a rectangle in local XZ, solid `depth` metres behind it within the rectangle (depth 0: a flat rectangle, solid from the front only). Unbounded: an infinite half-space, solid without limit behind it. Contacts and casts push to the front; rays hit the front only. |
| `mesh` | `model` (a model asset: its triangles after its import transform); optional `is_two_sided` (default true) | For architecture and terrain: never pushed (T27). Vertices at the same place are welded, degenerate triangles skipped; seams between flat or concave neighbours let shapes slide without catching. One-sided meshes are hit from their front (counter-clockwise) side only. |

`layer` and the names in `collides_with` are collision layers: `world`, `prop`, `character`, `vehicle`, `dynamic`,
`trigger`, `interaction`. Two colliders block each other when each one's mask holds the other's layer; left out, a mask
is its layer's default (the solid layers collide with each other, `trigger` sees `prop`, `character`, `vehicle` and
`dynamic`, `interaction` collides with nothing). A trigger reports what touches it and blocks nothing. The `mass` is
how pushable placements share a push (T27): equal masses give way equally, a heavy one barely moves. A `model` of a
hull or mesh loads with the world model as one more dependency; a missing one fails the world model. Loading a model
needs the GPU, so tools without it can use only `points` hulls.

**Rules.** A definition that breaks one is rejected when the definitions are read, with a message naming the key and
where it is (`World model "desk": lods[1].max_distance (12) must be above lods[0].max_distance (15)...`). Unknown keys
are rejected in the root, each LOD, `render`, each variant, each material override, `bounds`, `collision`, each set and
each shape, with the keys allowed there: the `surfaces` (T19) section comes with its task. (Vectors and colours accept their
usual forms, see "Common data structures".) The LOD models and variant textures must exist when the world model loads; a missing
one fails the load (logged once, like every load failure) and the objects using it draw nothing.

The test world's world models are in `world_models/test_world/`: `test`, `pbr_demo` and `pbr_metal`, one LOD each, the
model of the same name, seen 600 m (the draw distance every model had before world models), `painted_cube`, the test
cube with two variants, `red` and `blue` (tints only), and `lod_test`, three LODs of a sphere switching at 13 and 16 m and
seen 60 m: the placeholder models `lod_test_lod0` to `lod_test_lod2` (`models/placeholder_lod_test/`, UV spheres of
radius 0.5 m at 32x16, 16x8 and 8x4 segments). Since T12 they collide: `test`, `pbr_metal` and `painted_cube` with
`"bounds"`, `lod_test` with a hull of `lod_test_lod0`, `pbr_demo` with a mesh of its own model; and `test_door` (the
model `test_door`: the test cube made a 1 x 2 m panel by its import transform, hinged at its origin) has the sets
`closed` and `open`.


## World sprites.
A world sprite is the reusable definition of a flat image placed in the 3D world (a tree, a sign, a tire mark, a
glow): which sprite animation to play, how its quad stands, how it is lit and how far it is seen. It is built on its
sprite animation as a world model is built on models: the animation is named by its asset name and loaded with the
world sprite as its dependency (holding the world sprite holds it; it unloads with it). A sprite animation stays a
plain asset that text uses too; what only matters in the world lives here. Definitions live under `world_sprites/` and
have no resource files of their own.

A placed object names its world sprite in its sprite component (`references/world_structure.md`) and can override
every setting of this file for that one placement: an explicit override flag next to its own value.

```
{
    "name": "street_tree",
    "sprite_animation": "street_tree",       // the sprite animation to play
    "orientation": "axis_billboard",         // [OPTIONAL] default "billboard"
    "pivot": [0.5, 0.0],                     // [OPTIONAL] default [0.5, 0.5]
    "pixels_per_unit": 32,                   // [OPTIONAL] default 32
    "flip_x": false,                         // [OPTIONAL] default false
    "flip_y": false,                         // [OPTIONAL] default false
    "alpha_mode": "cutout",                  // [OPTIONAL] default "cutout"
    "alpha_cutoff": 0.5,                     // [OPTIONAL] default 0.5
    "is_lit": true,                          // [OPTIONAL] default true
    "emission": 1.0,                         // [OPTIONAL] default 1
    "light_wrap": 1.0,                       // [OPTIONAL] default 1
    "is_decal": false,                       // [OPTIONAL] default false
    "decal_offset": 0.01,                    // [OPTIONAL] default 0.01
    "render": { "has_outline": true, "omit_pixelation": false, "casts_shadows": true },  // [OPTIONAL] each key too
    "has_draw_distance": true,               // [OPTIONAL] default: whether "draw_distance" is given
    "draw_distance": 150,                    // [OPTIONAL] used while has_draw_distance is on
    "is_draw_distance_fixed": false          // [OPTIONAL] default false
}
```

| Key | Value | Meaning |
|---|---|---|
| `sprite_animation` | asset name, required | The sprite animation to play (`sprite_animations/`). Its playback (fps, running, looped) comes from the animation; each placement plays its own copy. |
| `orientation` | `billboard`, `axis_billboard`, `fixed` | `billboard` faces the camera; `axis_billboard` turns only around the object's own up axis to face it (trees, lamp posts); `fixed` keeps the object's rotation, the image in its local XY plane facing +Z (signs, posters, decals). |
| `pivot` | `[x, y]`, finite | The point of the image at the object's position: `[0, 0]` its bottom left, `[1, 1]` its top right; `[0.5, 0]` for things standing on the ground. It may lie outside the image. |
| `pixels_per_unit` | number above 0 | Image pixels per metre: the sprite's size is its frame's size divided by this, times the object's X and Y scale. |
| `flip_x`, `flip_y` | boolean | Mirror the image left to right, or top to bottom, about the pivot. |
| `alpha_mode` | `cutout`, `blend`, `additive` | `cutout`: hard edges, opaque where drawn (pixel art; casts shadows, outlined); `blend`: soft edges (smoke, stains), sorted, no shadow; `additive`: adds its light (glows). |
| `alpha_cutoff` | in [0, 1] | With `cutout`, texels less opaque than this are not drawn. |
| `is_lit` | boolean | Lit by the sun, the moon and lamps like the models around it; `false`: its colours glow as they are, by day and night (neon, fire), still fogged. |
| `emission` | >= 0 | How bright an unlit sprite glows; 1 shows its colours as they are. |
| `light_wrap` | in [0, 1] | Billboards only: how far light wraps around the sprite. 0 lights it as a flat card facing you, 1 as a round thing, half lit from the side. |
| `is_decal` | boolean | With `fixed`: lies on a surface (dirt, tire marks), drawn over it without shadow or outline. |
| `decal_offset` | metres, >= 0 | How far a decal is lifted off its surface so it does not flicker against it. |
| `render` | object | `has_outline` (a 1-pixel outline), `omit_pixelation` (drawn at full resolution over the pixelated image) and `casts_shadows`: all three for cutout sprites that are not decals only. The same block as a world model's, without `shadow_tier` (sprites cast into both shadow cascades). |
| `has_draw_distance` | boolean | Whether `draw_distance` applies. Left out, it is on exactly when `draw_distance` is given, so a hand-written file just gives the distance. Off, the sprite is seen as far as its kind: 150 m, or 60 m for a decal, decided for each placement (a placement may turn a sprite into a decal), and a `draw_distance` next to it is kept unused. A tool that writes definitions and leaves out default values must still write this key whenever it writes `draw_distance`. |
| `draw_distance` | metres, above 0 | How far the sprite is seen while `has_draw_distance` is on. The player's `draw_distance_scale` (`config.json`) multiplies it unless it is fixed, and a narrower field of view (zooming in) pushes it out. Over its last tenth (at least 3 m) the sprite dithers away (blended and additive ones fade their alpha). |
| `is_draw_distance_fixed` | boolean | true: the player's `draw_distance_scale` leaves the draw distance as it is (landmarks, gameplay-critical signs). |

**The same keys, the same rules.** These are the sprite component's settings, with its names, defaults and rules: the
world sprite's tables and the component's table are built from the same rows, so a placement's own value is checked
exactly as the asset's. There are no rules between keys: a combination that does not apply (a "decal" billboard, crisp
smoke) is kept as written and simply has no effect.

**Rules.** A definition that breaks one is rejected when the definitions are read, with a message naming the key and
the rule (`World sprite "tree": world_sprite.pixels_per_unit (PixelsPerUnit) must be finite and > 0.`). Unknown keys are
rejected in the root and in `render`, with the keys allowed there (the render flags belong in `render`). The sprite
animation must exist when the world sprite loads; a missing one fails the load (logged once, like every load failure)
and the objects using it draw nothing.

The test world's world sprites are in `world_sprites/test_world/`, each named as the animation it plays: `test_sprite`
(the test image at 1280 pixels per metre), `test_tire_mark`, `test_tree`, `test_sign`, `test_smoke`, `test_glow` and
`test_neon` (T06's placeholders, with the look the test world gave them).

## Characters.
A character is the reusable definition of a kind of character (a human, a dog, a pigeon): its body and how it moves. The
player and NPCs are world objects with a character component that names one (`references/world_structure.md`), and a
placement may override every key of this file for that one character, with an explicit override flag next to its own
value. Gravity itself is the world's (the environment's `gravity`); a character multiplies it. Definitions live under
`characters/` and have no resource files and no dependencies. How a character's model looks and animates comes later
(T29, T30).

```
{
    "name": "human",
    "radius": 0.3,                      // [OPTIONAL] default 0.3: the upright capsule's radius, metres
    "height": 1.75,                     // [OPTIONAL] default 1.75: its whole height, caps included (>= 2 x radius)
    "eye_height": 1.62,                 // [OPTIONAL] default 1.62: the eyes above the feet
    "mass": 75.0,                       // [OPTIONAL] default 75: kilograms
    "is_pushable": true,                // [OPTIONAL] default true
    "collides_with": ["world", "prop", "character", "vehicle", "dynamic", "trigger"],  // [OPTIONAL] these by default
    "walk_speed": 1.4,                  // [OPTIONAL] default 1.4 m/s
    "can_run": true,                    // [OPTIONAL] default true
    "run_speed": 3.5,                   // [OPTIONAL] default 3.5 m/s
    "acceleration": 8.0,                // [OPTIONAL] default 8 m/s²
    "ground_friction": 10.0,            // [OPTIONAL] default 10 m/s²
    "ground_control": "steer",          // [OPTIONAL] default "steer"; or "push"
    "step_height": 0.35,                // [OPTIONAL] default 0.35 m
    "slope_limit": 45,                  // [OPTIONAL] default 45 degrees
    "ground_snap_distance": 0.0,        // [OPTIONAL] default 0 m: it falls off every step
    "turn_speed": 360,                  // [OPTIONAL] default 360 degrees per second
    "is_turn_instant": false,           // [OPTIONAL] default false
    "can_crouch": true,                 // [OPTIONAL] default true
    "crouch_height": 1.15,              // [OPTIONAL] default 1.15 m (>= 2 x radius, <= height)
    "crouch_eye_height": 1.0,           // [OPTIONAL] default 1.0 m
    "crouch_speed": 1.0,                // [OPTIONAL] default 1.0 m/s
    "crouch_transition_seconds": 0.2,   // [OPTIONAL] default 0.2
    "can_jump": false,                  // [OPTIONAL] default false
    "jump_speed": 3.2,                  // [OPTIONAL] default 3.2 m/s
    "can_jump_while_crouched": false,   // [OPTIONAL] default false
    "coyote_seconds": 0.1,              // [OPTIONAL] default 0.1
    "jump_buffer_seconds": 0.1,         // [OPTIONAL] default 0.1
    "walk_jump_boost": 0.0,             // [OPTIONAL] default 0 m/s
    "run_jump_boost": 0.0,              // [OPTIONAL] default 0 m/s
    "can_auto_jump": true,              // [OPTIONAL] default true
    "auto_jump_delay_seconds": 0.25,    // [OPTIONAL] default 0.25
    "gravity_multiplier": 1.0,          // [OPTIONAL] default 1
    "air_acceleration": 0.0,            // [OPTIONAL] default 0 m/s²: no air control
    "air_drag": 0.004,                  // [OPTIONAL] default 0.004 per metre
    "max_fall_speed": 55.0,             // [OPTIONAL] default 55 m/s
    "can_fly": false,                   // [OPTIONAL] default false
    "fly_speed": 5.0,                   // [OPTIONAL] default 5 m/s
    "fly_fast_speed": 10.0,             // [OPTIONAL] default 10 m/s
    "fly_acceleration": 6.0,            // [OPTIONAL] default 6 m/s²
    "fly_deceleration": 6.0,            // [OPTIONAL] default 6 m/s²
    "interaction_reach": 2.5,           // [OPTIONAL] default 2.5 m
    "interaction_layers": ["world", "prop", "character", "vehicle", "dynamic", "interaction"]  // [OPTIONAL] these by default
}
```

| Key | Value | Meaning |
|---|---|---|
| `radius` | metres, above 0, at most 100 | The radius of the character's upright capsule: how close it comes to walls and how narrow a gap it fits through. Every character is an upright capsule (the owner's decision G22); a dog's nose and tail may poke past it. |
| `height` | metres, above 0, at most 100 | The standing capsule's whole height, from the feet (the object's position) to the top of the head; at least twice the radius. |
| `eye_height` | metres, 0 to 100 | The eyes above the feet while standing: where the player's camera sits and where the character looks from. |
| `mass` | kilograms, above 0, at most 1000000 | How pushes are shared: characters and crates push each other by mass (equal masses give way equally). |
| `is_pushable` | boolean | Whether others (characters, doors, platforms) push it. `false`: it is never pushed; it pushes and stops when it cannot. |
| `collides_with` | list of layer names | The collision layers it collides with (`world`, `prop`, `character`, `vehicle`, `dynamic`, `trigger`, `interaction`). |
| `walk_speed`, `run_speed` | metres per second, 0 to 1000 | Walking and running speeds. |
| `can_run` | boolean | Whether it may run (asking to run walks otherwise). |
| `acceleration` | metres per second squared, above 0, at most 10000 | How hard its movement keys change its motion on the ground, toward the wished speed in their direction: speeding up, a turn, a turn back. |
| `ground_friction` | metres per second squared, 0 to 10000 | How fast the ground slows it, a constant deceleration: it stops when the keys let go, and loses the speed they do not drive (above the walking or running speed after a sprint-jump). 0 slides like ice. |
| `ground_control` | `"steer"` or `"push"` | How the keys act on the ground. `steer` turns the whole motion toward them (turns as quick as a turn back; a wall pressed into at a slant is slid along at the walk's share along it). `push` adds motion along their direction and leaves the rest to friction (Quake's way: a turn sideways slides; against a wall at a slant it slides at full speed, or sticks pushing nearly straight in): momentum, for ice or a heavy animal. Either way the keys never raise the speed past the walking or running speed. |
| `step_height` | metres, 0 to 100 | The highest step it walks up; anything higher is a wall. |
| `slope_limit` | degrees, 0 to below 90 | The steepest ground it walks on; steeper ground is a wall it slides down. |
| `ground_snap_distance` | metres, 0 to 100 | How far down it steps without falling, at once (a stair's riser). 0: it falls off every step, stairs too, like Minecraft. Walkable slopes are followed whatever this is. |
| `turn_speed` | degrees per second, above 0 | How fast its body turns (toward where it moves or looks, as its input asks). |
| `is_turn_instant` | boolean | Turns its body at once instead. |
| `can_crouch` | boolean | Whether it may crouch. |
| `crouch_height` | metres, above 0 | The crouched capsule's whole height: at least twice the radius, at most `height`. What it fits under. |
| `crouch_eye_height` | metres, 0 to 100 | The eyes above the feet while crouched. |
| `crouch_speed` | metres per second, 0 to 1000 | Its speed while crouched (running does not change it). |
| `crouch_transition_seconds` | seconds, 0 to 60 | How long the eyes take to go down or up; the capsule changes at once (standing up waits for room). |
| `can_jump` | boolean | Whether it may jump. |
| `jump_speed` | metres per second, 0 to 1000 | The upward speed a jump starts with: 3.2 jumps about half a metre on Earth. |
| `can_jump_while_crouched` | boolean | Whether a crouched character may jump. |
| `coyote_seconds` | seconds, 0 to 60 | How long after walking off an edge a jump still starts. |
| `jump_buffer_seconds` | seconds, 0 to 60 | How long a jump asked for just before landing waits for the ground. |
| `walk_jump_boost`, `run_jump_boost` | metres per second, 0 to 1000 | The speed a jump adds along the movement keys while walking, and while running (sprint-jumping): never past the walking or running speed plus the boost, so jumps in a row do not add up. |
| `can_auto_jump` | boolean | Whether a jump held down jumps again whenever it is on the ground. |
| `auto_jump_delay_seconds` | seconds, 0 to 60 | The least time from one jump to the next that a held jump starts: with jumps longer than it, a held jump hops again as it lands. |
| `gravity_multiplier` | 0 to 100 | Times the world's gravity while walking and falling (flying has none). |
| `air_acceleration` | metres per second squared, 0 to 10000 | How hard its movement keys push it in the air, along their direction up to the walking or running speed: they steer and brake, never speed it up past that (or past what it has). 0: no air control, only gravity and the air move it. |
| `air_drag` | per metre, 0 to 100 | How much the air slows it: a deceleration of this times its speed squared. 0.004 gives a person's 50 m/s terminal speed on Earth and is hardly felt at running speed. |
| `max_fall_speed` | metres per second, above 0 | The fastest it falls. |
| `can_fly` | boolean | Whether it may fly (birds). |
| `fly_speed`, `fly_fast_speed` | metres per second, 0 to 1000 | Flying speed, and while running. |
| `fly_acceleration`, `fly_deceleration` | metres per second squared, above 0 | How fast it speeds up and slows down while flying. |
| `interaction_reach` | metres, above 0, at most 100 | How far from its eyes it can look at and use things. |
| `interaction_layers` | list of layer names | What its look and use ray sees; `trigger` is never seen. |

**The same keys, the same rules.** These are the character component's settings, with its names, defaults and rules:
the character's table and the component's table are built from the same rows, so a placement's own value is checked
exactly as the asset's.

**Rules.** A definition that breaks one is rejected when the definitions are read, with a message naming the key and
the rule (`Character "dog": character.radius (Radius) must be finite and in ...`). Two rules span keys: `height` and
`crouch_height` include the capsule's caps, so each must be at least twice `radius`, and `crouch_height` must not pass
`height` (`Character "dog": character: height (0.5 m) must be at least twice the radius (0.3 m): ...`). A placement whose
overrides break them gets the nearest heights that fit. Unknown keys are rejected, with the keys allowed.

The test characters are in `characters/test_world/`: `test_human` (the owner's tuning: 1.8 m tall, walks at 2 and runs at
5 m/s, jumps at 5 m/s with a 1 m/s running boost, acceleration 64, friction 16, air control 4), `test_dog` (0.28 m by
0.7 m, 25 kg, runs at 6 m/s, lies down as its crouch) and `test_pigeon` (0.1 m by 0.3 m, 0.35 kg, flies at 6 m/s and
cannot crouch).


## Animation libraries.
An animation library (T29) is a file of animation clips, kept apart from the models that play it, so one library plays
on every model whose skeleton names its bones alike: the humans of every height and build share one walk. Definitions
live under `animation_libraries/`, next to their `.glb` or `.gltf` file (a `.gltf`'s buffer files beside it). How clips
play on a model, retargeted to its bone lengths, is in `agents/Animation.md`.

```
{
    "name": "human_motions",
    "location": "humans/motions",                  // the glTF file (gltf / glb)
    "format": "glb",                               // [OPTIONAL] see "Format"
    "clips": [                                     // [OPTIONAL] at most 256, each name and each source once
        { "name": "walk", "is_looped": true },
        { "name": "sprint", "source": "Run.001", "is_looped": true, "speed": 1.25 },
        { "name": "wave" }
    ]
}
```

| Key | Value | Meaning |
|---|---|---|
| `location` | asset location | The file. A model file with its mesh and skin works, and so does a file of the clips alone (Blender's export of the actions: its armature object becomes a bone no skeleton has, which costs nothing). |
| `clips` | array | What the definition sets for the file's clips. A clip it does not list keeps its name in the file (when that is a valid clip name; otherwise it is skipped with a warning), does not loop and plays at speed 1. |
| `clips[].name` | `[a-z][a-z0-9_.-]*`, at most 64 bytes | The name the clip plays under (an animator's `clip`, the game's code). Unique in the library; a world model's libraries may not share one. |
| `clips[].source` | text, at most 256 bytes; default: `name` | The clip's name in the file (any text, as Blender names actions). It must be a clip of the file. |
| `clips[].is_looped` | boolean; default false | Whether the clip loops; a clip that does not stops at its end and holds its last pose. A placement may override it. |
| `clips[].speed` | above 0, at most 100; default 1 | How fast the clip plays (an animator's speed multiplies it). |

**The file is checked as hostile** when the library loads (not when the definition is read): it must parse and validate
as glTF 2.0; its buffers are inside it (`.glb`, data URIs) or files beside it (no absolute paths, `..`, backslashes,
drive letters or NUL, percent escapes decoded first), declare 1 byte or more, at most 256 MiB together, and a buffer
file holds at least its declared length; buffer views fit in their buffers with glTF's strides (none, or 4 to 252 bytes
in fours; meshopt compression is not read); accessors fit in their views, aligned to their components (a sparse one's
replacements too, 1 to its count of them); a `.glb`'s chunks are padded to four bytes; a clip's channels must sample
floats of the right shape (rotations may be normalized integers); key times must be finite, at least 0, rising, and at
most 3600 s; values finite, rotations of a length above 0; at most 256 bones (animated nodes and their ancestors) and
4 194 304 floats of keys. Morph target weights are ignored (with a warning per clip). Interpolation is glTF's: `STEP`,
`LINEAR` (rotations along the shorter arc) and `CUBICSPLINE`. Unknown keys are rejected.

The test library is `animation_libraries/placeholder_humanoid/motions.json`: `test_humanoid_motions`, the placeholder
figure's nine clips (idle, walk, run, crouch_idle, crouch_walk and fall looped; jump, land and wave once).
