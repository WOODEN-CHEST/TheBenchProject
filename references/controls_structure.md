# Controls file structure

The player's controls: which keys drive which action. The file is optional (`controls.json` next to `config.json`;
without it the default controls apply) and is read by `GameInput_LoadBindingsFromFile` (see `agents/GameInput.md`).

```json
{
    "bindings": {
        "move_forward": ["w", "up"],
        "ui_previous": ["shift+tab"],
        "sprint": ["left_shift", "mouse_side"],
        "pause": []
    }
}
```

- `bindings` (object, optional): one entry per action, keyed by the action's id (`move_forward`, `ui_accept`, ...; the
  list is `GameAction` in `include/GameInput.h`). A file without `bindings` changes nothing.
- Each entry is a list of up to 4 binding strings. The list replaces the action's bindings; an empty list unbinds the
  action. Actions the file does not list keep their bindings (the defaults, unless something rebound them).
- A binding string is zero or more modifiers, then a key id, joined by `+`: `"w"`, `"shift+tab"`,
  `"control+alt+delete"`. The modifiers are `shift`, `control`, `alt` and `super`, each at most once. Other held
  modifiers do not matter, unless another binding on the same key asks for them: the most specific binding wins (with
  `"tab"` and `"shift+tab"` both bound, Shift+Tab is only the `"shift+tab"` binding). Key ids are lowercase (`a`-`z`, `0`-`9`, `f1`-`f12`, `space`, `enter`, `escape`, `tab`,
  `left_shift`, `keypad_add`, `mouse_left`, ...; the full list is the table in `source/GameKey.c`). At most 64 bytes.
- Names that are not actions are ignored, so a file from an older or newer version keeps loading.
- All or nothing: a malformed entry (not a list, too many bindings, a non-string, an empty string or an unknown key or
  modifier) fails the whole load with `ErrorCode_InvalidJSON`, naming the action and the binding, and no binding changes.
- The file is UTF-8 JSON; a byte order mark at the start (some editors write one) is skipped.
