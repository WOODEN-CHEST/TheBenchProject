# This file describes the JSON structure for text components in this game.

Text components (see `agents/TextComponents.md`) are Minecraft-style rich-text nodes: a component carries a string, a
translatable string, a sprite animation, or nothing, and holds an ordered list of child components ("children"), so
components form a tree. This document defines how a component tree is written as JSON, used by content files and by
console commands. The corresponding GHDF (binary) form is an internal storage format and is not documented here; both
come from the same codec, so they accept exactly the same fields, defaults and ranges.

This format reuses the common value shapes defined in `references/asset_structure.md`: **decimal numbers**,
**booleans**, **colors** and **vectors** are parsed exactly as documented there. Read that file's "Common data
structures" section first; this document only adds the **render color** shape and the component shapes.


## Rules for every component tree

* **Depth:** a tree has at most 64 levels (a root alone is one level; every array shorthand and every `children` entry
  adds one). A deeper tree is rejected.
* **Unknown keys are ignored**, so files written for a newer game version still load.
* **Bad values are errors, not defaults.** A wrong shape, an out-of-range number, an invalid name, text that is not
  UTF-8 or contains a NUL character (`\u0000`), an unknown `type` or a missing required key rejects the whole tree. The
  error message names the key and the component's place as a JSON pointer, for example
  `Text component at "/children/1/children/0": "size": must be a decimal within [0, 100].`
* **Names.** `font` and `animation` are asset names, the names the assets are registered under (see
  `asset_structure.md`, "Asset names are namespaced keys"): `namespace:path`, lowercase; one written without its
  namespace (`noto_sans`) is `tbp`'s, and the game writes them in full (`tbp:noto_sans`). Fonts and animations are bound
  when the text is shown, and a name that cannot be loaded is logged once and falls back (a font to the default font,
  an animation to drawing nothing). A translate component's `key` follows `[a-z][a-z0-9_.-]*`: lowercase ASCII
  letters, digits, `_`, `.` and `-`, starting with a letter, and no namespace.


## A component value

A "component" is one of three JSON value shapes:

1. **An object** `{ "type": ..., ... }`: one component, described below.
2. **An array** `[ component, component, ... ]`: shorthand for an **empty** component whose `children` are the array's
   entries (in order). This is the idiomatic way to write "a sequence of components".
3. **A string** `"some text"`: shorthand for a **string** component with that text and default styling.

Shorthands may appear anywhere a component is expected, including inside a `children` array. When the game writes
JSON it uses the shorthands where they fit and leaves out every key that has its default value.


## The render color shape

Component colors are **render colors** (a tint color plus separate brightness and opacity), not plain colors. A render
color is written as either:

* **A plain color** (any color form from `asset_structure.md`: hex string, `[r,g,b,(a)]` array, or `{r,g,b,(a)}`
  compound). The color's RGB becomes the tint, its alpha becomes the opacity (`alpha/255`), and brightness is `1.0`.

* **A compound** giving full control:
  ```
  {
      "tint": color,          // the base color (any color form). Its alpha seeds "opacity" if that is absent.
      "brightness": number,   // [OPTIONAL] decimal in [0;1], default 1.0
      "opacity": number       // [OPTIONAL] decimal in [0;1], default = tint alpha / 255 (else 1.0)
  }
  ```

A compound is treated as the second (render-color) form when it contains a `tint`, `brightness`, or `opacity` key;
otherwise a compound is treated as a plain `{r,g,b,(a)}` color. Brightness or opacity outside [0;1] is an error.


## Common component object fields

Every component **object** shares these fields:

* `type`: string, REQUIRED in object form. One of `"string"`, `"translate"`, `"sprite"`, `"empty"`.
* `children`: array of components, [OPTIONAL]. The ordered child components. Each entry is itself a component value
  (object or either shorthand). Defaults to none.


## String component

```
{
    "type": "string",

    "text": "Hello, world",     // REQUIRED. UTF-8 text without NUL characters. "\n" starts a new line; a tab is a gap of
                                //           four spaces; other control characters are not drawn.

    "font": "asset_name",       // [OPTIONAL] the font asset's name. Default: the text's default font (each text
                                //            document sets one; without one, raylib's built-in font, which
                                //            covers U+0020-U+00FF: Basic Latin and Latin-1 Supplement).

    "color": render color,      // [OPTIONAL] Default: white, brightness 1, opacity 1.

    "size": decimal number,     // [OPTIONAL] the line height, as a fraction of the screen area's height, in [0;100].
                                //            Default: 0.1

    "shadow": {                 // [OPTIONAL] shadow settings; any missing sub-field keeps its default.
        "active": boolean,          // [OPTIONAL] Default: true
        "color": "default"          // [OPTIONAL] "default" (or absent) => the text color with its brightness reduced;
                 | render color,    //            a render color => an explicit custom shadow color.
        "offset": "default"         // [OPTIONAL] "default" (or absent) => the shadow offset of the text's font (its
                 | vector           //            definition's "shadow_offset", see asset_structure.md, "Fonts");
                                    //            a decimal vector, relative to the size, each component in [-10;10]
                                    //            => this offset, whatever the font.
    },

    "underline": boolean,       // [OPTIONAL] Default: false
    "strikethrough": boolean,   // [OPTIONAL] Default: false
    "spacing": decimal number,  // [OPTIONAL] extra space after each character, relative to the size, in [-1;10].
                                //            Default: 0

    "children": [ component, ... ]  // [OPTIONAL]
}
```


## Translate component

A string shown in the player's language: the text is looked up by `key` in the game's translations, like Minecraft's
`translate` component. While a key has no translation (there are no translation tables yet), the `fallback` is shown,
or the key itself when there is no fallback. It takes every styling key of the string component.

```
{
    "type": "translate",

    "key": "paper.form_17.title",   // REQUIRED. The translation key (the name grammar above, so keys can be namespaced).
    "fallback": "Form 17",          // [OPTIONAL] UTF-8 text shown while the key has no translation. Default: none.

    "font", "color", "size", "shadow", "underline", "strikethrough", "spacing": as in the string component.

    "children": [ component, ... ]  // [OPTIONAL]
}
```


## Sprite component

A sprite component renders a sprite animation inline, referenced by the **name** of its sprite-animation asset. It
always takes up its `size` in the line, even when the animation is absent or cannot be loaded (it then draws nothing).
Every sprite naming the same animation in one text shows the same frame.

```
{
    "type": "sprite",

    "animation": "asset_name",  // [OPTIONAL] the sprite-animation asset's name. If absent, the sprite draws nothing.

    "color": render color,      // [OPTIONAL] Default: white, brightness 1, opacity 1.

    "size": vector,             // [OPTIONAL] the box, as fractions of the screen area's height, each component in
                                //            [0;100]. Default: [0, 0]

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

An empty component renders nothing itself but still lays out its children. The array shorthand `[ ... ]` produces
exactly this.


## Examples

A styled greeting with a green word and an exclamation appended as children:
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
    { "type": "sprite", "animation": "tbp:coin_spin", "size": [0.1, 0.1] },
    { "type": "string", "text": " x3", "color": { "tint": "#ffd700", "brightness": 0.9 } }
]
```

A translatable title with a fallback:
```
{ "type": "translate", "key": "menu.title", "fallback": "The Bench Project", "size": 0.08 }
```
