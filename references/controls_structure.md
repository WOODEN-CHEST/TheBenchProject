# Controls file structure

The player's controls: which keys drive which action, for the actions whose bindings the player set. The file is
optional (`controls.json` next to `config.json`; without it every action has its default bindings) and is read by
`GameInput_LoadBindingsFromFile` after the packs' actions registered (see `agents/GameInput.md`).

```json
{
    "bindings": {
        "tbp:move_forward": ["w", "up"],
        "tbp:ui_previous": ["shift+tab"],
        "tbp:sprint": ["left_shift", "mouse_side"],
        "tbp:jump": [],
        "mymod:open_journal": ["j"]
    }
}
```

- `bindings` (object, optional): one entry per action, keyed by the action's key in full (`tbp:move_forward`; the
  engine's actions are listed in `agents/GameInput.md`, a pack's in its `input_actions` files,
  `references/asset_structure.md`). A key without a namespace is `tbp:`'s (`"move_forward"` is `tbp:move_forward`), so
  files written before keys had namespaces load unchanged; the game writes keys in full. A file without `bindings`
  changes nothing.
- Each entry is a list of up to 4 binding strings. The list becomes the action's bindings, as set by the player: they
  win over the action's defaults, also when a pack or a story changes those defaults. An empty list unbinds the action,
  except an action that **always keeps a binding** (`tbp:pause`, or a pack's action marked `keeps_a_binding`): it keeps
  its default bindings instead, and the game logs that once. Actions the file does not list keep their bindings (their
  defaults, unless something rebound them).
- **Entries for actions that do not exist now are kept** (a mod that is not loaded): their keys and bindings are
  written back when the file is saved, and they apply as soon as an action registers under that key. An entry for an
  action that is registered but withdrawn applies to it for when it comes back.
- A binding string is zero or more modifiers, then a key id, joined by `+`: `"w"`, `"shift+tab"`,
  `"control+alt+delete"`. The modifiers are `shift`, `control`, `alt` and `super`, each at most once. Other held
  modifiers do not matter, unless another binding on the same key asks for them: the most specific binding wins (with
  `"tab"` and `"shift+tab"` both bound, Shift+Tab is only the `"shift+tab"` binding). Key ids are lowercase (`a`-`z`,
  `0`-`9`, `f1`-`f12`, `space`, `enter`, `escape`, `tab`, `left_shift`, `keypad_add`, `mouse_left`, ...; the full list is
  the table in `source/GameKey.c`). At most 64 bytes.
- The game writes only what the player set: the actions whose bindings the player set and the kept entries, in byte
  order of the keys. An action the player never rebound follows its defaults, so new defaults from a story reach it.
- All or nothing: a malformed entry (a key that is not a namespaced key, the same key twice such as `"sprint"` and
  `"tbp:sprint"`, not a list, too many bindings, a non-string, an empty string or an unknown key or modifier) or more
  than 4096 entries fails the whole load with `ErrorCode_InvalidJSON`, naming the key and the binding, and nothing
  changes.
- The file is UTF-8 JSON; a byte order mark at the start (some editors write one) is skipped.
