# This file describes the JSON structure for asset definitions in this game.

All asset definitions have a root compound { } which has the property "name", its value is a string which
describes the asset's name to access it by.
The name's value can be any UTF-8 string whose characters consist of a-z, 0-9, '_', '.' and '-', it must start with a letter and have a
length of >= 1.
No two asset definitions OF THE SAME TYPE may have the same name. Each one must be unique. Names can
be shared across different asset types, however.

For example.
```
{
    "name": "main_background"
    ... // Other stuff afterwards.
}
```


### There are 2 types of asset locations (places they're pulled from).**

**The first location type is the path of a file which contains the asset.**
It is a string path relative to the asset type's root directory. All directory separators are forward slashes '/'.
Relative directory parts like ".." or "." are not allowed, absolute and empty paths aren't either.
The path does not contain the file extension, that is resolved automatically. This means there may only
be 1 file in that directory with a given name, no matter the extension.
Paths may start with a directory separator, though it is unnecessary.
For example: `dir_a/dir_b/file_name`
The full path this would resolve to would be `{asset_root_dir}/{asset_type_dir_name}/dir_a/dir_b/file_name{found_extension}`


**The second location type is a name which references an asset.**
Sometimes the asset in question is not on the host's file system, for example, a dynamic asset
generated or retrieved at runtime, or an asset which is just a data structure which needs to be constructed.
In these cases, the asset can be referenced by a name (any valid UTF-8 Unicode string). The game, from that
referenced name, resolves what needs to be loaded or created.
This form of location specification is mostly used by the game itself when loading dynamic assets rather than
the asset JSON definitions.


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
    "padding_pixels": vector, // [OPTIONAL] (values are clamped to >= 0, floored to integer if not one already). If not present, set to (0;0).
    "padding_color": "nearest_pixel" or "transparent" or "nearest_pixel_transparent" // [OPTIONAL]. If not present, defaults to nearest pixel.
    "texture_properties": texture properties // [OPTIONAL]
}
```


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

            "location": asset location, // The location of the texture OR spritesheet.

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
    "location": asset location
}
```


## Fonts.

```
{
    "location": asset location
}
```


## Shaders.

```
{
    "location": asset location
}
```