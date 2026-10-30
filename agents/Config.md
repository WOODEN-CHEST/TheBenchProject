# Config module

Files: `include/Config.h`, `source/Config.c`.

## Purpose

Loads the game's human-editable settings from a JSON file (the game uses `config.json` next to the
executable; `main.c` builds the path from `Services.BaseDirectory`) into a plain `GameConfig` struct. The rest of the game reads settings from this struct and never
touches JSON: the JSON document and its object pool are created, read from, and fully deconstructed inside
the loader, so nothing JSON-related outlives the call.

## Type

`GameConfig` — a plain value type owning no heap memory (copy it freely). Current fields:

| Field | JSON key | Default | Meaning |
|---|---|---|---|
| `IsFPSUnlocked` (`bool`) | `is_fps_unlocked` | false | Render every loop iteration instead of capping at `TargetFPS`. |
| `TargetFPS` (`int32_t`) | `target_fps` | 60 | Render cap when not unlocked; [10, 1000], otherwise the default. |
| `IsVSyncEnabled` (`bool`) | `is_vsync_enabled` | false | Sync buffer swaps to the display refresh. The swap then blocks, and the update ticks owed for the wait run right after it. Usually paired with `is_fps_unlocked: true`. |
| `IsFullscreen` (`bool`) | `is_fullscreen` | false | Create the window fullscreen. |
| `ResolutionWidth` / `ResolutionHeight` (`int32_t`) | `resolution` (`[w, h]`) | 1280×720 | Window size in pixels; [320, 16384] × [240, 16384], otherwise the default. |
| `ShadowStrength`, `BloomStrength`, `SunshaftStrength`, `AmbientOcclusionStrength`, `FogStrength` (`float`) | `*_strength` | 1 | Config-side multipliers on each world's effect strength; [0, 16], otherwise the default. `FogStrength` multiplies the haze (every haze distance and the ground haze's depth; 1 the real air, 0 no haze, far things crisp). |
| `DrawDistanceScale` (`float`) | `draw_distance_scale` | 1 | Scales every draw distance that is not fixed (each world model's and world sprite's own or a placement's; a sprite's kind's, 150 m or 60 m for a decal, when its world sprite gives none): beyond it an object is not drawn, over its last tenth (at least 3 m) it dithers out (WorldRuntime.md, "Visibility"). [0, 16] like the multipliers (0 draws nothing but the fixed ones), otherwise the default. Lower it for slower machines, or to see the fade sooner. |
| `LodDistanceScale` (`float`) | `lod_distance_scale` | 1 | Scales every LOD switch distance (each world model's, or a placement's own, times its multiplier): above 1 keeps the finer LODs farther (more detail), below 1 switches to coarser LODs sooner (WorldModels.md, "LODs"). A fixed draw distance does not exempt the LODs. [0, 16] like the multipliers (0 always draws the coarsest LOD), otherwise the default. |
| `ShadowDistanceScale` (`float`) | `shadow_distance_scale` | 1 | Scales every shadow distance that is not fixed (how far from the camera a thing still casts: its own, its asset's, or its draw distance when neither gives one); a thing never casts beyond its draw distance (WorldRuntime.md, "Sun and moon shadows"). [0, 16] like the multipliers (0: only fixed shadow distances cast), otherwise the default. |
| `ShadowLevelCount` (`int32_t`) | `shadow_level_count` | 5 | The most shadow levels: the nested maps around the camera, each `shadow_level_factor` times as wide as the one before (32, 128, 512, 2048 and 8192 m by default); fewer end the sun's and the moon's shadows nearer than the world's `shadow_reach`. [1, 8] (`CONFIG_MAX_SHADOW_LEVELS`), otherwise the default. |
| `ShadowLevelFactor` (`float`) | `shadow_level_factor` | 4 | Each shadow level's width over the one before it: smaller keeps the shadows sharper farther out but needs more levels for the same reach. [1.5, 8], otherwise the default. |
| `ShadowMapSize` (`int32_t`) | `shadow_map_size` | 2048 | Every shadow level's map's texels along each side (16 MB a level at 2048; 1.6 cm texels in the first level): a power of two in [256, 8192], otherwise the default. |
| `ShadowUpdatePeriods` (`int32_t[8]`) | `shadow_update_periods` (an array) | 1, 1, 2, 4, 8, 16, 32, 64 | Each shadow level renders every this many frames (staggered, so no two far levels render in one frame); a light that turned or a camera that moved far renders a level at once. Longer periods cost less; a moving thing's shadow there lags by up to a period. Each element given replaces its level's (a shorter array leaves the others as they are); each in [1, 256], otherwise its level's default. |

Integer settings follow GameJSON's integer rules: a JSON integer, a whole real number (`60.0`) or a numeric
string. A fractional, out-of-range or non-finite value (`59.9`, `1e19`) is treated as malformed and keeps the
default.

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
3. Read it in `ReadConfigValues` in `Config.c`, reusing `ReadBool` / `ReadInt32` / `ReadFloat` /
   `ReadArrayInt32` or adding a small reader that keeps the default on absence/type mismatch.

Old and new config files stay mutually compatible because missing and unknown keys are both tolerated.

## Validation

Values are guarded during loading: the resolution is clamped/replaced against
`CONFIG_MIN_*` / `CONFIG_MAX_*` bounds so consumers (e.g. window creation in `main.c`) can trust the loaded
values without re-validating.
