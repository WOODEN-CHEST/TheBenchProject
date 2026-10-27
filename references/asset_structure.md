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
| Terrains: `source: model` / `source: data` | `glb` `gltf` / `ghdf` |

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

Every key is optional, and leaving one out gives its default above.
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
    "fragment_location": asset location, // [OPTIONAL] the fragment-stage source; omit to use Raylib's default
    "defines": [ string, ... ]           // [OPTIONAL] names #defined in both stages; none by default
}
```

At least one of `vertex_location` / `fragment_location` must be present. A bare `"location"` is accepted as an
alias for `fragment_location`. Every shader-stage extension
(`vert`, `frag`, `vs`, `fs`, `glsl`) is accepted for either stage, so two stage files with the same name but
different extensions would make the location ambiguous: give the two stage files DISTINCT names — e.g.
`shaders/pbr/vertex.vert` and `shaders/pbr/fragment.frag`, referenced as `"pbr/vertex"` and `"pbr/fragment"`.

`defines` names preprocessor symbols for both stages, so one source serves a shader and its variants: each name
becomes a line `#define <name>` right after the main file's `#version` line (first, when it has none), followed by a
`#line` that keeps the file's line numbers, and shared files test it with `#ifdef`. At most 16 names, each at most once;
a name is a GLSL identifier (a letter or `_`, then letters, digits or `_`) that neither starts with `GL_` nor holds `__`
(GLSL reserves both). Anything else fails the definition. The instanced twins use it:
```
{
    "name": "world_depth_instanced",
    "vertex_location": "depth/instanced",
    "fragment_location": "depth/fragment",
    "defines": ["WORLD_INSTANCED"]
}
```

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

The shared files: `common/math` (`PI`), `common/atmosphere` (the air, the atmosphere's tables and their march: the sky
the sky pass draws, and the haze's volume), `common/haze` (the haze in front of what the camera sees, the volume's
readers and the ground haze), `common/night_sky` (the air mass and extinction near the horizon, and the profile of
light pollution's skyglow), `common/night_vision`, `common/lighting`, `common/distance_packing`,
`common/dither`, `common/depth`, `common/instancing`, `common/value_noise` and `common/skinning` (the bone attributes,
the bone palette and a vertex's blended skinning rows, included by every skinned vertex stage; `agents/Animation.md`).

**Skinned twins.** Every world geometry shader that draws models has a twin for skinned meshes, named with
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

**Skinned models.** A glTF (`gltf`, `glb`) file with a skin loads its skeleton and each vertex's bone
indices and weights (at most four bones a vertex) with it: such a model bends with the pose of the object it is placed
on, playing an animation library's clips (`agents/Animation.md`). The skeleton is checked when the model loads (at most
256 bones, every parent a bone listed in the file and no cycle); a mesh with an index past the bones or a weight that is
not a number is drawn as it is (rigid). Bones are found by **name**, so the skeletons of every model that plays one
library must name their bones alike (a tall and a short human with the same bone names play the same walk).


A value outside its range (a metallic of 1.5, a negative intensity) rejects the definition with a message naming the key.
Every number must be finite and fit a float.


## Textures.
A standalone texture: one image file uploaded to the GPU as it is, for things such as sky panoramas, the moon, noise,
normal maps and masks. (Sprites come from sprite sheets and sprite animations, model maps from model
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
        "has_outline": true, "omit_pixelation": false, "shadow_tier": "both", "casts_shadows": true, "haze": 1.0
    },
    "variants": [                                  // [OPTIONAL] at most 64
        {
            "name": "dark_wood",
            "material_overrides": [ { "slot": 0, "albedo": "office_desk_dark_albedo", "tint": "#d0c0b0" } ]
        }
    ],
    "bounds": { "center": [0, 0.4, 0], "radius": 1.1 }, // [OPTIONAL] default: LOD 0's (a posed object's: its pose's)
    "animation_libraries": [ "human_motions" ],    // [OPTIONAL] at most 16
    "animation_graph": "human",                    // [OPTIONAL]
    "sockets": {                                   // [OPTIONAL] at most 32
        "hand_r": { "bone": "hand_r", "position": [0.03, -0.07, 0], "rotation": [100, 0, 0] },
        "eyes": { "bone": "head", "position": [0, 0.1, 0.1] },
        "seat": { "position": [0, 0, 0.55] }      // no bone: fixed in the object's own space
    },
    "first_person": { "eyes": "eyes", "hidden_bones": [ "neck" ] } // [OPTIONAL]
}
```

| Key | Value | Meaning |
|---|---|---|
| `lods` | 1 to 8 `{ "model", "max_distance" }` | The models to draw, the finest first. `model` is a model asset's name; `max_distance` (metres, above 0, rising strictly from one LOD to the next) is how far that LOD is used, at the default 60-degree field of view. Past the last one the last LOD keeps drawing until the draw distance. A placement may give its own distances, scale them or force a LOD (`references/world_structure.md`, the model block); the player's `lod_distance_scale` (`config.json`) multiplies them, and a narrower field of view (zooming in) picks finer LODs. Every LOD should share the same origin, axes, scale and material slots. |
| `draw_distance` | metres | How far objects of this world model are seen: at least the last LOD's `max_distance`; left out, it is that distance. The player's `draw_distance_scale` (`config.json`) multiplies it unless it is fixed, and a narrower field of view (zooming in) pushes it out. Over its last tenth (at least 3 m) the object dithers away. |
| `is_draw_distance_fixed` | boolean | true: the player's `draw_distance_scale` leaves the draw distance as it is (landmarks, gameplay-critical props). |
| `lod_fade_seconds` | seconds, >= 0 | How long a LOD cross-fade takes: both LODs are drawn with complementary pixel dithers meanwhile; 0 swaps at once. |
| `lod_hysteresis` | in [0, 1) | A LOD switches to a coarser one at its switch distance x (1 + h) and back at x (1 - h), so an object standing at a boundary does not flicker. |
| `render` | object | The default render settings of the placements: `has_outline` (default true: a 1-pixel outline), `omit_pixelation` (default false: true draws it at full resolution over the pixelated image, for readable screens), `shadow_tier` (`both` (default), `near`: only the sharp near shadow, for small props, `far`: only the coarse far one, for big objects), `casts_shadows` (default true) and `haze` (in [0, 1000], default 1: times the air's haze in front of it; 0 as if the air were clear, for a beacon or a sign that must read from far away). |
| `variants` | at most 64 `{ "name", "material_overrides" }` | Other looks of the same meshes (dark wood, a red car). `name`: `[a-z][a-z0-9_.-]*`, at most 128 bytes, unique in the world model. `material_overrides` (optional; at most 32, one per slot): what the variant changes in each material slot, below. A placed object picks one with `uses_variant` and `variant`. |
| `bounds` | `{ "center": [x, y, z], "radius": metres >= 0 }` | The bounding sphere culling and draw distances use, in the object's own space (after each LOD's import transform). Left out, it is the sphere around LOD 0's mesh bounds, or, for an object an animator poses, the sphere around its pose. Give it to keep one fixed sphere whatever the pose. |
| `animation_libraries` | at most 16 asset names, each once | The animation libraries (below, "Animation libraries") whose clips the placements play, loaded with the world model as its dependencies. Two of them may not hold a clip of the same name. The skinned LODs must share one skeleton: every LOD with bones must have the first one's bones and bind poses, or the world model is rejected (a coarse LOD may be rigid). A placement may play its own libraries instead (`references/world_structure.md`, the animator block). |
| `animation_graph` | an asset name | The animation graph (below, "Animation graphs") the placements' animators play with those clips, loaded with the world model as a dependency. A placement may play its own instead (the animator block). |
| `sockets` | at most 32 `"name": { "bone", "position", "rotation" }` | Named places on the model: where a held item hangs (a hand), where an action's anchor puts a character (a bench's seat, in front of a cup), where a first-person camera sits. The name: `[a-z][a-z0-9_.-]*`, at most 64 bytes. `bone` (optional, at most 31 bytes): a bone of the skeleton the socket follows, its `position` ([x, y, z] metres, default the origin) and `rotation` (degrees about X, Y and Z in that order, default none) along the bone's axes (its frame in the object's space, without any scale a bone or the import transform carries); without a bone the socket is fixed in the object's own space. Its +Z is the way it faces (an anchor turns a character that way). A bone socket on a world model without a skeleton, or on a bone its skeleton lacks, fails the load. |
| `first_person` | `{ "eyes", "hidden_bones" }` | What a first-person camera does with this body: `eyes` (required) names one of its sockets, the camera's place; `hidden_bones` (at most 16 bone names) the bones the camera's passes leave out, each with its subtree (the head, from inside). Hidden bones need a skeleton that has them. |

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

**Collision** (`collision`, optional): the hitboxes of the objects placed with this world model, as named
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
    "mass": 1.0,                                   // [OPTIONAL] kilograms, above 0, at most 1000000; default 1
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
| `mesh` | `model` (a model asset: its triangles after its import transform); optional `is_two_sided` (default true) | For architecture and terrain: never pushed. Vertices at the same place are welded, degenerate triangles skipped; seams between flat or concave neighbours let shapes slide without catching. One-sided meshes are hit from their front (counter-clockwise) side only. |

`layer` and the names in `collides_with` are collision layers: `world`, `prop`, `character`, `vehicle`, `dynamic`,
`trigger`, `interaction`. Two colliders block each other when each one's mask holds the other's layer; left out, a mask
is its layer's default (the solid layers collide with each other, `trigger` sees `prop`, `character`, `vehicle` and
`dynamic`, `interaction` collides with nothing). A trigger reports what touches it and blocks nothing. The `mass` is
how pushable placements share a push: equal masses give way equally, a heavy one barely moves. A `model` of a
hull or mesh loads with the world model as one more dependency; a missing one fails the world model. Loading a model
needs the GPU, so tools without it can use only `points` hulls.

**Rules.** A definition that breaks one is rejected when the definitions are read, with a message naming the key and
where it is (`World model "desk": lods[1].max_distance (12) must be above lods[0].max_distance (15)...`). Unknown keys
are rejected in the root, each LOD, `render`, each variant, each material override, `bounds`, `collision`, each set and
each shape, with the keys allowed there: a `surfaces` section is not part of the format yet and is refused as unknown.
(Vectors and colours accept their
usual forms, see "Common data structures".) The LOD models and variant textures must exist when the world model loads; a missing
one fails the load (logged once, like every load failure) and the objects using it draw nothing.

The test world's world models are in `world_models/test_world/`: `test`, `pbr_demo` and `pbr_metal`, one LOD each, the
model of the same name, seen 600 m, `painted_cube`, the test
cube with two variants, `red` and `blue` (tints only), and `lod_test`, three LODs of a sphere switching at 13 and 16 m and
seen 60 m: the placeholder models `lod_test_lod0` to `lod_test_lod2` (`models/placeholder_lod_test/`, UV spheres of
radius 0.5 m at 32x16, 16x8 and 8x4 segments). They collide: `test`, `pbr_metal` and `painted_cube` with
`"bounds"`, `lod_test` with a hull of `lod_test_lod0`, `pbr_demo` with a mesh of its own model; and `test_door` (the
model `test_door`: the test cube made a 1 x 2 m panel by its import transform, hinged at its origin) has the sets
`closed` and `open`, and an `opener` socket (where a character stands to push it). The others are `test_humanoid`
and `test_humanoid_short` (the placeholder humanoid; they play the `test_humanoid` graph and have the sockets `hand_r`,
`hand_l` and `eyes` and a first_person block), `test_bench` (the model `test_bench`, collision `bounds`,
its `seat` socket on the floor 0.55 m in front), `test_cup` (collision `bounds`, its `grip` on its side and its
`reach` 0.52 m in front of it, facing it), and `test_tree` and `test_pebble`, the placeholders for instanced drawing
(`asset_source/scripts/placeholder_instances.py`): a conifer of three LODs seen 250 m, which the test world's grove
draws, and a pebble of two seen 45 m, which the test frame's pebble field draws.


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
    "render": { "has_outline": true, "omit_pixelation": false, "casts_shadows": true, "haze": 1.0 },  // [OPTIONAL] each key too
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
| `is_lit` | boolean | Lit by the sun, the moon and lamps like the models around it; `false`: its colours glow as they are, by day and night (neon, fire), still hazed. |
| `emission` | >= 0 | How bright an unlit sprite glows; 1 shows its colours as they are. |
| `light_wrap` | in [0, 1] | Billboards only: how far light wraps around the sprite. 0 lights it as a flat card facing you, 1 as a round thing, half lit from the side. |
| `is_decal` | boolean | With `fixed`: lies on a surface (dirt, tire marks), drawn over it without shadow or outline. |
| `decal_offset` | metres, >= 0 | How far a decal is lifted off its surface so it does not flicker against it. |
| `render` | object | `has_outline` (a 1-pixel outline), `omit_pixelation` (drawn at full resolution over the pixelated image) and `casts_shadows`: all three for cutout sprites that are not decals only; and `haze` (in [0, 1000], default 1: times the air's haze in front of every kind of sprite; 0 as if the air were clear). The same block as a world model's, without `shadow_tier` (sprites cast into both shadow cascades). |
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
`test_neon` (placeholders).

## Characters.
A character is the reusable definition of a kind of character (a human, a dog, a pigeon): its body and how it moves. The
player and NPCs are world objects with a character component that names one (`references/world_structure.md`), and a
placement may override every key of this file for that one character, with an explicit override flag next to its own
value. Gravity itself is the world's (the environment's `gravity`); a character multiplies it. Definitions live under
`characters/` and have no resource files and no dependencies. A character has no model of its own: another component on
its object (a world model, posed by its animator: `agents/Animation.md`) or child objects show it.

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
| `radius` | metres, above 0, at most 100 | The radius of the character's upright capsule: how close it comes to walls and how narrow a gap it fits through. Every character is an upright capsule; a dog's nose and tail may poke past it. |
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

The test characters are in `characters/test_world/`: `test_human` (tuned by hand: 1.8 m tall, walks at 2 and runs at
5 m/s, jumps at 5 m/s with a 1 m/s running boost, acceleration 48, friction 16, air control 4), `test_dog` (0.28 m by
0.7 m, 25 kg, runs at 6 m/s, lies down as its crouch) and `test_pigeon` (0.1 m by 0.3 m, 0.35 kg, flies at 6 m/s and
cannot crouch).


## Animation libraries.
An animation library is a file of animation clips, kept apart from the models that play it, so one library plays
on every model whose skeleton names its bones alike: the humans of every height and build share one walk. Definitions
live under `animation_libraries/`, next to their `.glb` or `.gltf` file (a `.gltf`'s buffer files beside it). How clips
play on a model, retargeted to its bone lengths, is in `agents/Animation.md`.

```
{
    "name": "human_motions",
    "location": "humans/motions",                  // the glTF file (gltf / glb)
    "format": "glb",                               // [OPTIONAL] see "Format"
    "clips": [                                     // [OPTIONAL] at most 256, each name and each source once
        { "name": "walk", "is_looped": true, "distance": 1.1 },
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
| `clips[].distance` | metres, above 0, at most 1000; default none | How far the clip carries the body per cycle at speed 1 (a gait's stride length times its steps per cycle): a graph state's `match_speed` plays it at the character's real speed from it, scaled by the skeleton's height ratio. |

**The file is checked as hostile** when the library loads (not when the definition is read): it must parse and validate
as glTF 2.0; its buffers are inside it (`.glb`, data URIs) or files beside it (no absolute paths, `..`, backslashes,
drive letters or NUL, percent escapes decoded first), declare 1 byte or more, at most 256 MiB together, and a buffer
file holds at least its declared length; buffer views fit in their buffers with glTF's strides (none, or 4 to 252 bytes
in fours; meshopt compression is not read); accessors fit in their views, aligned to their components (a sparse one's
replacements too, 1 to its count of them); a `.glb`'s chunks are padded to four bytes; a clip's channels must sample
floats of the right shape (rotations may be normalized integers); key times must be finite, at least 0, rising, and at
most 3600 s; values finite, rotations of a length above 0; at most 256 bones (animated nodes and their ancestors) and
4 194 304 floats of keys; and reading its glTF structure may take at most 64 MiB beyond the file's own size (a small
file listing millions of empty objects is refused). Morph target weights are ignored (with a warning per clip).
Interpolation is glTF's: `STEP`, `LINEAR` (rotations along the shorter arc) and `CUBICSPLINE`. Unknown keys are
rejected.

The test library is `animation_libraries/placeholder_humanoid/motions.json`: `test_humanoid_motions`, the placeholder
figure's sixteen clips (idle, walk, run, crouch_idle, crouch_walk, fall, sit_idle and hold_r looped; jump, land, wave,
sit_down, stand_up, pick_up, put_down and open_door once; walk, run and crouch_walk with their distances, 1.1, 1.5789
and 0.7333 m per cycle).


## Animation graphs.
An animation graph says what an animated object plays when: layers of states and the transitions between them,
the parameters and events they read, the special actions, and look-at. Definitions live under `animation_graphs/`
(JSON only, no file of their own). A world model names its graph (`animation_graph`); an animator may name another. The
graph names clips, not libraries: they come from the animator's libraries. How a graph plays is in
`agents/Animation.md` ("Animation graphs", "Actions").

```
{
    "name": "human",
    "fade_seconds": 0.2,                           // [OPTIONAL] the default cross-fade, 0 to 60 s; default 0.2
    "parameters": {                                // [OPTIONAL] at most 64 custom parameters, 32 of them triggers
        "mood": { "type": "float", "default": 0, "min": -1, "max": 1 },
        "is_placing": { "type": "bool", "default": false },
        "cheer": { "type": "trigger" }
    },
    "look_at": {                                   // [OPTIONAL]
        "bones": [ { "bone": "neck", "share": 0.35 }, { "bone": "head", "share": 0.65 } ],
        "max_yaw": 70, "max_pitch_up": 40, "max_pitch_down": 55, "turn_speed": 400
    },
    "layers": [                                    // 1 to 8; the first is the body
        {
            "name": "body",
            "entry": "idle",                       // the state it starts in
            "states": {                            // 1 to 64
                "idle": { "clip": "idle", "look_at": 1 },
                "move": { "blend": { "parameter": "speed", "points": [ { "clip": "walk", "at": 2 },
                    { "clip": "run", "at": 5 } ] }, "match_speed": "speed", "min_rate": 0.3, "max_rate": 3 },
                "pick_up": { "clip": "pick_up", "events": [
                    { "name": "grab", "time": 0.65, "attach": "hand_r", "grip": "grip" } ] },
                "sitting": { "clip": "sit_idle" }
            },
            "transitions": [                       // at most 256 (a list of states counts once per state)
                { "from": "any", "to": "jump", "when": [ { "event": "jumped" } ], "fade_seconds": 0.08 },
                { "from": "idle", "to": "move", "when": [ { "parameter": "speed", "above": 0.15 } ] },
                { "from": [ "pick_up", "put_down" ], "to": "idle", "exit_time": 1 }
            ]
        },
        { "name": "right_arm", "mask": [ "shoulder_r" ], "weight": 1, "entry": "free",
          "states": { "free": {}, "holding": { "clip": "hold_r" } },
          "transitions": [ { "from": "free", "to": "holding", "when": [ { "socket": "hand_r", "is_holding": true } ] } ] }
    ],
    "actions": {                                   // [OPTIONAL] at most 64
        "sit": { "layer": "body", "states": [ "sit_down", "sitting", "stand_up" ], "movement": "locked",
                 "target": "required", "anchor": { "socket": "seat", "max_distance": 2.5, "approach_seconds": 0.6 } },
        "wave": { "layer": "right_arm", "states": [ "waving" ], "target": "none" }
    }
}
```

**Names** (layers, states, parameters, events, actions): `[a-z][a-z0-9_.-]*`, at most 64 bytes, each unique in its
kind (states within their layer); `any` is no state's name. Custom parameters must not take a built-in's name.

| Key | Value | Meaning |
|---|---|---|
| `parameters.*.type` | `float`, `bool`, `trigger` | A float has a `default` within its `min` and `max` (default 0, -1e9, 1e9; each within ±1e9), a bool a `default` (false); a trigger is an event code fires (`WorldAnimation_FireTrigger`). The built-in parameters and events are listed in `agents/Animation.md`. |
| `look_at.bones` | 1 to 8 `{ "bone", "share" }` | The bones look-at turns, each by its share of the turn (above 0; together at most 1). |
| `look_at.max_yaw`, `max_pitch_up`, `max_pitch_down` | degrees, 0 to 180 (yaw) or 90 (pitch); default 70, 40, 50 | How far the head turns from the body. |
| `look_at.turn_speed` | degrees a second, above 0, at most 100000; default 360 | How fast it follows the look. |
| `layers[].mask` | at most 32 bone names | The bones (with their subtrees) the layer plays on; none: every bone. |
| `layers[].weight` | 0 to 1; default 1 | How strongly the layer covers the ones before it. |
| `states.*.clip` | a clip's name | One clip. Without `clip` or `blend` the state is empty: it plays nothing (a layer at rest). |
| `states.*.blend` | `{ "parameter", "points" }` | 2 to 8 clips placed `at` rising values of a parameter; the two around the parameter's value share it, kept in step by phase. |
| `states.*.speed` | 0 to 100; default 1 | Times the state's rate. |
| `states.*.speed_parameter` | a parameter | Its value multiplies the rate too. |
| `states.*.match_speed` | a parameter | The rate becomes the parameter over the clips' natural speed (their libraries' `distance`), within `min_rate` and `max_rate` (default 0.25 and 4; at most 100). |
| `states.*.is_looped` | boolean | Overrides the clip's loop (a blend loops unless set). |
| `states.*.look_at` | 0 to 1; default 1 | How much look-at turns the head in this state. |
| `states.*.events` | at most 16 | Timed events: a `name`, and `time` (seconds into a one-clip state's clip) or `phase` (a share of the cycle, below 1). Effects: `attach` (a socket of this object: the action's target hangs on it; `grip`: the target's socket that lands on it), `detach` (a socket: what hangs on it goes back to the world where it is), `set` (at most 4 custom parameters and values). |
| `transitions[].from` | a state, a list of states, or `any` | Where it leaves from. |
| `transitions[].to` | a state | Where it goes. |
| `transitions[].when` | at most 8 conditions, all must hold | `{ "parameter", "above" / "below" / "at_least" / "at_most" / "is": value }` (`is` compares booleans), `{ "event" }` (fired this tick), `{ "socket", "is_holding" }`. |
| `transitions[].exit_time` | cycles of the current state, 0 to 1000 | It waits until the state has played this much (1: a one-shot clip's end). |
| `transitions[].fade_seconds` | 0 to 60 | The cross-fade; default the graph's. |
| `transitions[].restarts` | boolean; default false | Whether it may enter the state the layer is in, from the start. |
| `actions.*.layer`, `states` | a layer; 1 to 16 of its states | The action plays these states, entering the first; it ends when the layer leaves them. |
| `actions.*.fade_seconds` | 0 to 60 | The cross-fade into the first state; default the graph's. |
| `actions.*.movement` | `free`, `no_moving`, `no_turning`, `locked`; default `free` | How the character's movement is locked while the action plays. |
| `actions.*.target` | `none`, `optional`, `required`; default `optional` | Whether it takes a target object (what an anchor's socket and an attach refer to). |
| `actions.*.anchor` | `{ "socket", "max_distance", "approach_seconds" }` | A socket of the target the character slides onto first (within `max_distance` metres, 0 to 100, default 1.5, of its feet; over `approach_seconds`, 0 to 60, default 0.5), facing the socket's +Z. Needs a target. |

**Rules.** The reader treats a definition as hostile (downloaded content): every count has the limit above, every
reference must resolve (a transition's states, a condition's parameter or event, an event's parameters to set, an
action's layer and states), and unknown keys are rejected everywhere, with the keys allowed there. A definition that
breaks a rule is rejected when the definitions are read, with a message naming where (`Animation graph "human":
layers[0].transitions[2].to names "jumpp", which is not a state of layer "body".`). Clips and bones are matched when an
animator plays the graph: a clip none of its libraries has plays nothing (for a second, so exit times pass), a mask or
look-at bone its skeleton lacks is left out, each warned once.

The test graph is `animation_graphs/test_world/test_humanoid.json` (`test_humanoid`): the placeholder humanoid's body
layer (idle, a walk-run blend matched to the speed, the crouches, jump, fall, land, flight, and the actions' states),
a right-arm layer (holding what the hand holds, waving), look-at on the neck and head, and the actions `sit` (anchored
on a `seat`), `pick_up` (anchored on a `reach`, the grab attaching to `hand_r` by the target's `grip`), `put_down` (the
release detaching `hand_r`), `open_door` (anchored on an `opener`, its `push` event for the game) and `wave` (the
right arm, over whatever the body does).


## Terrains.
A terrain is the ground of a world: one free-form triangle mesh (vertices anywhere, overhangs and caves allowed)
with painted material weights and vegetation densities per vertex, and holes and hard per-face materials per triangle.
It is cut into chunks (32 m by default) that draw in every render pass and collide as triangle meshes. Definitions live
under `terrains/`, next to their `.glb` or their binary file; a world places one with a `terrain` component
(`references/world_structure.md`, "The terrain block"). How it works inside is in `agents/Terrain.md`.

```
{
    "name": "placeholder_terrain",
    "source": "model",                                   // "model" (a .glb imported when it loads) or "data"
    "model": "test_world/placeholder_terrain",           // source model: the glTF file (glb / gltf)
    "materials": [ "grass", "dirt", "rock" ],            // the palette, 1 to 16 terrain material names
    "density_channels": [ "trees" ],                     // [OPTIONAL] at most 8
    "weights": [ { "attribute": "COLOR_0", "r": "grass", "g": "dirt", "b": "rock" } ],
    "densities": [ { "attribute": "_DENSITY_0", "r": "trees" } ],
    "normals": "smooth",                                 // [OPTIONAL] "smooth" or "imported"
    "blend_slot": "blend",                               // [OPTIONAL] the slot of the painted faces
    "chunk_size": 32.0,                                  // [OPTIONAL]
    "render": { "has_outline": true, "shadow_tier": "both", "casts_shadows": true, "draw_distance": 4000.0,
        "is_draw_distance_fixed": true },               // [OPTIONAL] each key optional
    "collision": { "is_enabled": true, "layer": "world" }, // [OPTIONAL] each key optional
    "rules": [                                           // [OPTIONAL] at most 8, applied in order
        { "material": "rock", "slope": { "min": 35.0, "blend": 8.0 }, "only_on": [ "grass", "dirt" ] },
        { "material": "dirt", "height": { "max": -4.5, "blend": 2.5 },
            "noise": { "scale": 2.5, "strength": 0.6, "seed": 11 }, "only_on": [ "grass" ] }
    ]
}
```

| Key | Value | Meaning |
|---|---|---|
| `source` | `model` or `data`, required | Where the mesh comes from: a glTF file imported when the terrain loads (`model`), or the terrain's binary form (`data`), which the in-game editor will write. |
| `model` | asset location; `source: model` only | The `.glb` (or `.gltf` with its buffers beside it). `format` (`glb`, `gltf`) narrows it as for models. |
| `data` | asset location; `source: data` only | The binary form, a `.ghdf` file (`TerrainMesh_SaveGHDF`). |
| `materials` | 1 to 16 asset names, each once; with `source: model` none named `hole` or as the blend slot (`blend` unless `blend_slot` says) | The palette: `terrain_material` assets (see "Terrain materials"), loaded with the terrain as its dependencies. Each vertex weighs them, and a face slot may name one. Their maps of one kind must share one size (a texture array holds one): a mismatch fails the terrain's load, naming both materials and sizes. |
| `density_channels` | at most 8 names (`[a-z][a-z0-9_]*`), each once | Vegetation and other densities per vertex, for the scattering (not built yet). |
| `weights` | at most one mapping per palette material | Which attribute of the file feeds which palette material: `{ "attribute": "<name in the file>", "r": "<material>", "g": ..., "b": ..., "a": ... }`, any channels left out unused. Each material is fed by one channel at most. Without mappings every weight is 0, and the terrain shows its first material. |
| `densities` | at most one mapping per density channel | The same for the density channels. |
| `normals` | `smooth` (default) or `imported`; `source: model` only | `smooth`: computed by the game over the whole welded mesh (no seams between chunks). `imported`: the file's own normals, so hard edges modelled in Blender stay hard (vertices at one position with different normals stay apart). |
| `blend_slot` | a material slot's name (default `blend`); `source: model` only | The slot whose faces use the painted weights. It may not be `hole` or a palette material's name. |
| `chunk_size` | metres, 8 to 256 (default 32) | The side of the chunks' square cells in the terrain's X and Z. |
| `render.has_outline` | boolean (true) | Whether the renderer outlines it. |
| `render.shadow_tier` | `near`, `far`, `both` (`both`) | Which shadow cascades it casts into. |
| `render.casts_shadows` | boolean (true) | Off: it casts no shadow; it still receives them. |
| `render.haze` | in [0, 1000] (1) | Times the air's haze in front of it; 0 as if the air were clear. |
| `render.draw_distance` | metres, above 0 (4000) | How far each chunk is seen before it fades out. |
| `render.is_draw_distance_fixed` | boolean (true) | On: the player's draw distance setting does not change it, so hills never vanish on low settings. |
| `collision.is_enabled` | boolean (true) | Off: nothing stands on it. |
| `collision.layer` | a layer (`world`) | Its chunks' collision layer. |
| `collision.collides_with` | an array of layers, each once (left out: the layer's default mask) | The layers it blocks and is blocked by. |
| `rules` | at most 8 rules | Put materials down by the ground's slope, its height and noise, per pixel, in order (below). |

A placed terrain may override every `render` and `collision` value (and multiply the draw distance) with the same keys
in its component, each behind its own `overrides_` flag.

**Rules** put materials down where the ground's shape says, per pixel, whatever the vertex spacing, and follow the
shape when it changes. Each rule:

| Key | Value | Meaning |
|---|---|---|
| `material` | a palette name, required | The material it puts down. |
| `opacity` | 0 to 1 (1) | How strongly it applies where its conditions hold fully. |
| `only_on` | palette names, at least one, each once (left out: every material) | The materials it may cover: "rock on steep slopes, but not over painted gravel". |
| `slope` | `{ "min", "max", "blend" }` in degrees: bounds 0 to 180, a blend 0 to 90; at least one bound, `min` not above `max` | Where the ground is that steep: 0 flat, 90 a wall, 180 an overhang's underside (the slope of the ground's normal in the world). |
| `height` | `{ "min", "max", "blend" }` in metres: bounds within 10 000 km, a blend 0 to 10 000; at least one bound, `min` not above `max` | Where the ground is that high: the world's Y (absolute: a snow line stays where it is when the terrain moves). |
| `noise` | `{ "scale", "strength", "seed" }`: a cell size in metres above 0 up to 10 000 (4), a strength 0 to 1 (1), a whole seed 0 to 4 294 967 295 (0); each key optional | Value noise in the terrain's own space, cells of `scale` metres, scaling the rule's strength by `1 - strength + strength x noise`: patches instead of a smooth band. |

A range is 1 between its bounds and falls to 0 across `blend`, centred on each bound (`min: 35, blend: 8` is 0 below 31
degrees and 1 above 39); without a blend the step is hard (the bound counts as inside); a bound left out is open. A
rule's strength is `opacity` times its slope's, height's and noise's factors; every covered material gives up that share
of its weight to the rule's material, so the weights keep adding up to one. Faces with a face material are left alone
(they take their material outright). Unknown keys are refused in a rule and in each of its blocks.

**Material slots** decide each face (`source: model`): a primitive's material is its slot, and its name says what its
faces are. `blend` (or the `blend_slot`; also a primitive without a material) uses the painted weights; `hole` makes
holes (not drawn, no collision: cave mouths, basements); a palette material's name gives its faces that material
outright, with a hard edge along their border (a path, a ploughed field). Any other slot name is refused with the name in
the message.

**Weights and densities** are read from colour or custom attributes: a scalar, RGB or RGBA of floats (clamped to 0..1)
or of normalized unsigned bytes or shorts. Weights need not add up to 1: the material mix divides by their sum. Every
mapped attribute must be on every primitive the file has; a channel mapped past the attribute's own is refused.

**The file is checked as hostile** when the terrain loads, with the same structure checks the animation libraries run
(see "Animation libraries": buffers, views, accessors, alignment, buffer files beside the file, the structure's memory
budget), plus: at most 256 MiB; the default scene's node tree at most 256 levels deep; every primitive triangles (glTF
mode 4; points, lines and strips are refused with the mesh's name) and not Draco-compressed, with float positions and,
for `imported` normals, float normals; every accessor the terrain reads (positions, indices, normals, mapped
attributes) stored in the file (an accessor without a buffer view, all zeros of any count, is refused); indices inside
their vertices; node transforms finite and not flattening (a mirrored node keeps its faces' front side); every position
inside 8192 m of the origin on each axis, no edge longer than that; at most 8 388 608 vertices in the primitives,
2 097 152 once welded, and 4 194 304 triangles, filling at most 16 384 chunks (128 x 128 cells: 4 km square at 32 m;
larger chunks cover more). Each position is taken through its node's
transform in double and rounded once onto a 1/1024 m grid; vertices at exactly one position (and, with `imported`
normals, one normal) are welded into one, their weights and densities averaged where the file gave them different
values (a seam), and triangles left with two corners at one position are dropped (both logged once with counts). A mapping naming an attribute a primitive lacks
fails the load, naming the mesh and the attribute. The binary form is read with the mesh's limits (the file at most
192 MiB, its parsed tree at most 256 MiB) and must hold as many materials and density channels as the definition
names. Unknown keys are
rejected everywhere in the definition, and a definition that breaks a rule is rejected when it is read, with a message
naming the rule (`Terrain "hills": weights[0].g is "dirtt", which is not one of its "materials".`).

**From Blender 5.** Model the terrain as mesh objects in Blender's usual space (+Z up); the exporter turns
it into glTF's +Y up, which is the game's.
1. **Materials:** give the mesh the material slots it needs, named as above: `blend` for the painted ground, `hole`
   for cave mouths, and a palette material's name (`dirt`) for faces that take one material outright. Assign the faces.
2. **Weights:** add a colour attribute (Object Data properties, Color Attributes; domain Vertex, type Color) and paint
   it in Vertex Paint mode, one material per channel: the red, green, blue and alpha channels map to materials in the
   definition's `weights`. Make it the active colour attribute: it exports as `COLOR_0`.
3. **More channels** (more than four materials, densities): add more attributes named with a leading underscore in
   capitals (`_WEIGHTS_1`, `_DENSITY_0`; colour or float attributes, domain Vertex) and map them by that name.
4. **Export** (File, Export, glTF 2.0): format glTF Binary (`.glb`); Include: Selected Objects if the scene holds more;
   Transform: +Y Up (on); Data, Mesh: Apply Modifiers (on), Normals (on for `normals: imported`), Attributes (on: it
   exports the attributes whose names start with `_`); Data, Mesh, Vertex Color: Use Vertex Color **Active** (the
   default, Material, exports a colour attribute only when a material's node tree uses it); Data, Material: Materials
   **Export** or Placeholder (No Export merges the primitives and loses the slots).
5. Put the `.glb` next to the definition and load the world: a refusal names what to fix (a slot, an attribute, a
   limit). If the game cannot find an attribute, check its name in the file: exporters have changed the case of custom
   attribute names over the versions, which is why they are written in capitals here.

The test terrain is `terrains/test_world/placeholder_terrain.json` (`placeholder_terrain`), written with its `.glb` by
`asset_source/scripts/placeholder_terrain.py`: hills around the test world's slab out to about 250 m (1 m triangles near
the slab, 8 m far out), a valley, a cliff steeper than 35 degrees with an overhang and a cave behind a mouth of `hole`
faces, a footpath, a round gravel patch and a `dirt` patch; `COLOR_0` paints grass, dirt and gravel (its red, green and
alpha; its blue maps rock, which only the rules put down: on the cliff above 35 degrees, and dirt on the valley floor
below 4.5 m under the slab, broken up by noise), `_DENSITY_0` the trees. Its four materials are the placeholders under
"Terrain materials".


## Terrain materials.
A terrain material is one material of a terrain's palette: grass, dirt, rock, gravel, asphalt. Its maps are
texture assets (see "Textures") whose image files are read on the CPU; the game builds one texture array per map kind of
each terrain, one layer per material. Definitions live under `terrain_materials/`; a terrain's `materials`
name them. How the blend works is in `agents/Terrain.md`.

```
{
    "name": "grass",
    "albedo": "placeholder_terrain_grass_albedo",              // a texture asset: the colour (sRGB)
    "height": "placeholder_terrain_grass_height",              // [OPTIONAL] a texture asset: the surface's height
    "normal": "placeholder_terrain_grass_normal",              // [OPTIONAL] a texture asset: a tangent-space normal map
    "roughness_ao": "placeholder_terrain_grass_roughness_ao",  // [OPTIONAL] a texture asset: roughness (red), AO (green)
    "tile_size": 2.0,              // [OPTIONAL]
    "albedo_tint": "#ffffff",      // [OPTIONAL]
    "roughness": 0.9,              // [OPTIONAL]
    "normal_strength": 1.0,        // [OPTIONAL]
    "height_blend_strength": 1.0,  // [OPTIONAL]
    "triplanar": "steep",          // [OPTIONAL] "off", "steep" or "always"
    "triplanar_slope": 40.0,       // [OPTIONAL] degrees
    "surface": "grass",            // [OPTIONAL] a namespaced key
    "puddles": 0.0,                // [OPTIONAL]
    "porosity": 0.6                // [OPTIONAL]
}
```

| Key | Value (default) | Meaning |
|---|---|---|
| `albedo` | a texture asset, required | The colour, read as sRGB. |
| `height` | a texture asset (none: flat at mid-height) | The surface's height (its red channel), for height blending: where materials meet, the one standing higher wins, so stones poke through grass. The albedo's size. |
| `normal` | a texture asset (none: flat) | A tangent-space normal map in OpenGL's convention: red toward the image's right, green toward its top (what Blender bakes and glTF uses); blue is rebuilt. |
| `roughness_ao` | a texture asset (none: roughness 1, no occlusion) | Red the roughness, green the ambient occlusion. |
| `tile_size` | metres, above 0 up to 1000 (2) | Metres per repeat of the maps, laid along the terrain's own axes (they move with the terrain object). |
| `albedo_tint` | a colour (white) | Multiplies the colour. |
| `roughness` | 0 to 1 (0.9) | Multiplies the map's roughness. |
| `normal_strength` | 0 to 8 (1) | How strongly the normal map tilts the surface; 0 is flat. |
| `height_blend_strength` | 0 to 10 (1) | How strongly its height wins where it meets another material: its share of a pixel is decided by its weight plus its height times this (0 blends by the weights alone). |
| `triplanar` | `off`, `steep`, `always` (`steep`) | When the maps are laid from three sides instead of from above, so cliffs are not stretched: never, where the ground is steeper than `triplanar_slope` (blended in over 10 degrees), or everywhere. |
| `triplanar_slope` | degrees, 0 to 90 (40) | With `steep`: where the three sides take over. |
| `surface` | a namespaced key (`tbp:ground`; `grass` reads as `tbp:grass`) | What the ground is: footsteps, where scattering may place things, later systems (none built yet). |
| `puddles` | 0 to 1 (0) | How much rain's puddles may form on it (asphalt 1, grass 0). Read by the weather's wetness (not built yet). |
| `porosity` | 0 to 1 (0.5) | How much wetness darkens it. Read by the weather's wetness (not built yet). |

* **Maps.** Each map's slot decides how it is read, whatever its texture's `color_space` says, and the texture's
  `texture_properties` do not apply: the arrays are mipmapped, filtered trilinearly (and anisotropically where the
  graphics driver can) and repeat. Each map is at most 1024 pixels per side; the height must be the albedo's size (it
  is stored in the albedo's alpha). Images are read as 8-bit RGBA (a greyscale image's grey in red, green and blue)
  from png, bmp, gif or qoi files: a dds file is refused (the image reader trusts the sizes a dds header claims, which a
  hostile file could use to read past its end).
* **One size per kind and terrain.** The materials of one terrain's palette must have albedo maps of one size, normal
  maps of one size and roughness maps of one size (a material without a normal or roughness map is fine: it gets a flat
  or plain layer). The terrain refuses a mismatch, naming both materials and their sizes. Sizes need not be powers of
  two; 256 or 512 pixels suit 640 x 360.
* **Tileable images.** The maps repeat every `tile_size` metres: paint them so their edges meet.
* Unknown keys are refused, and every rule is checked when the definition is read (`Terrain material "grass":
  terrain_material.roughness ...`); a texture that is not defined, or not read from an image file, fails the material's
  load, naming the slot and the texture.

The placeholders, written by `asset_source/scripts/placeholder_terrain_materials.py` (the Python standard library
only), are `terrain_materials/placeholder_terrain/` (`grass`, `dirt`, `rock`, `gravel`) over 256 x 256 tileable images in
`textures/placeholder_terrain/` (`<material>_albedo`, `_height`, `_normal`, `_roughness_ao`, each with its texture
definition). To use your own: give each map a texture definition (as those), point the material at it and keep the sizes
equal across the terrain's materials.
