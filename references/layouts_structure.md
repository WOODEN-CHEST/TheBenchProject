# Layouts file structure

A workspace's layouts: named arrangements of its windows (where each floats, which are docked as tabs and splits, which is
maximized) and the one shown. The file belongs to the player, at a path its host gives (the UI gallery's is
`ui_gallery_layouts.json` next to the game). It is optional: without it the host shows its default layout, which it then
keeps as a layout named `Default`. It is read by `UILayouts_Load` and written by `UILayouts_Save` (see
`agents/UIElements.md`, "Docking and layouts").

```json
{
    "version": 1,
    "active": "Default",
    "layouts": [
        {
            "name": "Default",
            "windows": [
                { "type": "tbp:gallery_dock_outline", "box": [530.0, 200.0, 220.0, 320.0] },
                { "type": "tbp:gallery_dock_view", "box": [400.0, 200.0, 480.0, 320.0] },
                { "type": "tbp:gallery_dock_log", "box": [430.0, 270.0, 420.0, 180.0] },
                { "type": "tbp:gallery_dock_notes", "box": [480.0, 260.0, 320.0, 200.0] },
                { "type": "tbp:gallery_dock_notes", "box": [80.0, 90.0, 320.0, 200.0], "minimized": true }
            ],
            "dock": {
                "split": "across",
                "ratio": 0.2,
                "first": { "tabs": [0], "selected": 0 },
                "second": {
                    "split": "down",
                    "ratio": 0.7,
                    "first": { "tabs": [1], "selected": 1 },
                    "second": { "tabs": [2, 3], "selected": 2 }
                }
            },
            "places": [
                { "type": "tbp:gallery_dock_inspector", "box": [490.0, 240.0, 300.0, 240.0] }
            ]
        }
    ]
}
```

- `version` (integer, required): 1.
- `active` (string, optional): the name of the layout shown; the first one when it is missing or names none.
- `layouts` (list, required): up to 16 layouts, each name used once.
- A layout:
  - `name` (string, required): 1 to 64 bytes of UTF-8 without control characters.
  - `windows` (list, optional): up to 128 windows, each with
    - `type` (string, required): the window type's key (`agents/UIElements.md`, "Window types"), in full.
    - `box` (list of four numbers, required): its floating box, `[x, y, width, height]` in the screen's UI pixels, the
      place it goes back to. The width and the height are above 0; no number is beyond 1e9 either way. The workspace keeps
      the window reachable however small the surface is, without changing this box.
    - `minimized`, `maximized` (booleans, optional, false when missing): at most one window of a layout shows maximized
      (maximized and not minimized); a minimized one may keep that it was maximized, and restores to it; a docked one is never
      minimized. A docked window that is maximized is maximized out of the dock and goes back to its tab when restored.
  - `dock` (object, optional; none for an empty dock): its docking areas as a tree, at most 16 splits deep and 32 areas,
    128 tabs in all. A node is either
    - a split: `split` (`"across"`: `first` at the left, `second` at the right; `"down"`: `first` above, `second` below),
      `ratio` (a number within [0, 1]: `first`'s share of the room), `first` and `second` (nodes, required); or
    - an area: `tabs` (a list of 1 to 128 window indices into `windows`, in the order the tabs stand; each window at most
      once in the whole dock) and `selected` (the index of one of its tabs, optional: the first when missing).
    A window in the dock is docked; the others float.
  - `places` (list, optional): up to 128 entries, each `type` and `box` as above, one per window type: where the type's
    last window was when it closed, so the next one opens there.
- **Hostile by design.** A file that is not what the game writes (not JSON, a wrong type, a number out of range, a dock
  deeper or larger than allowed, a tab naming a missing window or one already in the dock, two layouts of one name,
  another version, more than 1 MB) is put aside as `<file>.bad` (replacing an older one) with one line in the log, and
  the host shows its default, which is saved next as a fresh file. Nothing of a bad file is used. A file that is there but cannot
  be read (no permission, a lock, an I/O error) is not bad: it is left as it is with one line in the log, the default shows, and
  nothing is saved over it while the game runs.
- Keys the game does not know are passed over, with one line in the log for the whole file. Windows and places of window
  types that are not registered now (a mod that is not loaded) are dropped, with one line in the log, their tabs taken out
  of the dock (an area left with none goes, its split giving its room to its neighbour). A second window of a type that
  opens once is dropped too.
- The game writes the whole file at once, through a temporary file next to it that then replaces it, so a crash never
  leaves half a file. It writes when the arrangement's changes have settled (a second without another change) and when
  its host goes, never every tick. Ratios are written as fractions and boxes in UI pixels, so a layout fits any window
  size.
- The file is UTF-8 JSON; a byte order mark at the start (some editors write one) is skipped.
