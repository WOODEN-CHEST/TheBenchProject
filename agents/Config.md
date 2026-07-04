# Config module

Files: `include/Config.h`, `source/Config.c`.

## Purpose

Loads the game's human-editable settings from a JSON file (by default `config.json` in the working
directory) into a plain `GameConfig` struct. The rest of the game reads settings from this struct and never
touches JSON: the JSON document and its object pool are created, read from, and fully deconstructed inside
the loader, so nothing JSON-related outlives the call.

## Type

`GameConfig` — a plain value type owning no heap memory (copy it freely). Current fields:

- `IsFPSUnlocked` (`bool`) — let the frame rate run unbounded when true.
- `IsFullscreen` (`bool`) — create the window fullscreen when true.
- `ResolutionWidth` / `ResolutionHeight` (`int32_t`) — window size in pixels; always strictly positive and
  within a sane range after loading.

## Usage

```c
GameConfig Config;
Error Result = GameConfig_LoadFromFile((const unsigned char*)u8"config.json", &Config);
if (Result.Code != ErrorCode_Success)
{
    // Config is still left at valid defaults; log and continue if desired.
    Error_Deconstruct(&Result);
}
// ... use Config ...
GameConfig_Deconstruct(&Config);
```

- `GameConfig_SetDefaults` fills the struct with compiled-in defaults without touching the filesystem.
- `GameConfig_LoadFromFile` initializes to defaults, then overlays the file's settings. It returns an error
  for unreadable files or a non-object root, but **always leaves the config in a valid, usable state** (bad
  or missing individual values keep their default; out-of-range resolutions are replaced). Unknown keys are
  ignored.
- `GameConfig_Deconstruct` is currently a no-op (no owned resources) but must still be called for symmetry.

## Expanding

Adding a setting is intentionally cheap:

1. Add a field to `GameConfig` in `Config.h`.
2. Give it a default in `GameConfig_SetDefaults` (and a `CONFIG_DEFAULT_*` macro in `Config.c`).
3. Read it in `ReadConfigValues` in `Config.c`, reusing `ReadBool` / `ReadArrayInt32` or adding a small
   reader that keeps the default on absence/type mismatch.

Old and new config files stay mutually compatible because missing and unknown keys are both tolerated.

## Validation

Values are guarded during loading: the resolution is clamped/replaced against
`CONFIG_MIN_*` / `CONFIG_MAX_*` bounds so consumers (e.g. window creation in `main.c`) can trust the loaded
values without re-validating.
