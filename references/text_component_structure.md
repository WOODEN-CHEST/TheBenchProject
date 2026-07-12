# This file describes the JSON structure for text components in this game.

Text components (see `agents/TextComponents.md`) are Minecraft-style rich-text nodes: a component carries a
string, a sprite animation, or nothing, and holds an ordered list of child components ("children"), so
components form a tree. This document defines how a component tree is written as JSON, used by modding files
and by console commands. The corresponding GHDF (binary) form is an internal storage format and is not
documented here.

This format reuses the common value shapes defined in `references/asset_structure.md` — **decimal numbers**,
**integer numbers**, **booleans**, **colors**, and **vectors** are parsed exactly as documented there. Read
that file's "Common data structures" section first; this document only adds the **render color** shape and
the component shapes.


## A component value

A "component" is one of three JSON value shapes:

1. **An object** `{ "type": ..., ... }` — one component, described below.
2. **An array** `[ component, component, ... ]` — shorthand for an **empty** component whose `children` are
   the array's entries (in order). This is the idiomatic way to write "a sequence of components".
3. **A string** `"some text"` — shorthand for a **string** component with that text and default styling.

Shorthands may appear anywhere a component is expected, including inside a `children` array.


## The render color shape

Component colors are **render colors** (a tint color plus separate brightness and opacity), not plain
colors. A render color is written as either:

* **A plain color** (any color form from `asset_structure.md`: hex string, `[r,g,b,(a)]` array, or
  `{r,g,b,(a)}` compound). The color's RGB becomes the tint, its alpha becomes the opacity
  (`alpha/255`), and brightness is `1.0`.

* **A compound** giving full control:
  ```
  {
      "tint": color,          // the base color (any color form). Its alpha seeds "opacity" if that is absent.
      "brightness": number,   // [OPTIONAL] decimal in [0;1], default 1.0
      "opacity": number       // [OPTIONAL] decimal in [0;1], default = tint alpha / 255 (else 1.0)
  }
  ```

A compound is treated as the second (render-color) form when it contains a `tint`, `brightness`, or
`opacity` key; otherwise a compound is treated as a plain `{r,g,b,(a)}` color.


## Common component object fields

Every component **object** shares these fields:

* `type`: string — REQUIRED in object form. One of `"string"`, `"sprite"`, `"empty"`.
* `children`: array of components — [OPTIONAL]. The ordered child components. Each entry is itself a
  component value (object or either shorthand). Defaults to none.


## String component

```
{
    "type": "string",

    "text": "Hello, world",     // REQUIRED. A UTF-8 string (only the encoding is validated).

    "font": "asset_name",       // [OPTIONAL] the font asset's reference name. Default: the game default font.

    "color": render color,      // [OPTIONAL] Default: white, brightness 1, opacity 1.

    "size": decimal number,     // [OPTIONAL] Default: 0.1

    "shadow": {                 // [OPTIONAL] shadow settings; any missing sub-field keeps its default.
        "active": boolean,          // [OPTIONAL] Default: true
        "color": "default"          // [OPTIONAL] "default" (or absent) => derived from the text color;
                 | render color,    //            a render color => an explicit custom shadow color.
        "offset": vector            // [OPTIONAL] decimal vector, relative to the size. Default: [-0.1, 0.1]
    },

    "underline": boolean,       // [OPTIONAL] Default: false
    "strikethrough": boolean,   // [OPTIONAL] Default: false
    "spacing": decimal number,  // [OPTIONAL] Default: 0

    "children": [ component, ... ]  // [OPTIONAL]
}
```


## Sprite component

A sprite component renders a sprite animation, referenced by the **name** of its sprite-animation asset
(the same name the asset is registered under; see `asset_structure.md`). The live animation is bound
separately at runtime — the JSON only records which animation to use.

```
{
    "type": "sprite",

    "animation": "asset_name",  // [OPTIONAL] the sprite-animation asset's reference name. If absent, the
                                //            component renders nothing (an "empty" sprite).

    "color": render color,      // [OPTIONAL] Default: white, brightness 1, opacity 1.

    "size": vector,             // [OPTIONAL] decimal vector, the render size. Default: [0, 0]

    "children": [ component, ... ]  // [OPTIONAL]
}
```


## Empty component

```
{
    "type": "empty",

    "children": [ component, ... ]  // [OPTIONAL]
}
```

An empty component renders nothing itself but still lays out its children. The array shorthand
`[ ... ]` produces exactly this.


## Examples

A styled greeting with a red, bold-looking exclamation appended as a child:
```
{
    "type": "string",
    "text": "Hello ",
    "size": 0.12,
    "children": [
        { "type": "string", "text": "world", "color": "#55ff55" },
        "!"
    ]
}
```

A line mixing text and an inline sprite, written with the array shorthand:
```
[
    "You found ",
    { "type": "sprite", "animation": "ui/coin_spin", "size": [0.1, 0.1] },
    { "type": "string", "text": " x3", "color": { "tint": "#ffd700", "brightness": 0.9 } }
]
```
