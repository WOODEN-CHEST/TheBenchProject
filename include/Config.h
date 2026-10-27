#pragma once
#include "wr/WRError.h"
#include <stdint.h>
#include <stdbool.h>


/**
 * The game configuration module.
 *
 * Holds the small set of human-editable settings loaded from the game's JSON config file (see
 * GameConfig_LoadFromFile; the game passes the path of config.json next to the executable). The settings are copied into the plain GameConfig struct so the
 * rest of the game never has to touch JSON: the JSON document and its object pool are discarded once
 * reading is done.
 *
 * The config is designed to be expandable: to add a new setting, add a field to GameConfig, give it a
 * default in GameConfig_SetDefaults, and read it in the loader. Unknown keys in the file are ignored, and
 * a missing or malformed individual setting simply keeps its default, so old and new config files stay
 * compatible.
 *
 * After loading, the values in a GameConfig are always valid (out-of-range values are replaced with
 * defaults), so consumers such as window initialization can use them directly without re-validating.
 */


// Types.
/**
 * @brief The set of loaded game configuration values.
 *
 * A plain value type owning no heap memory; copy it freely. Populate it either with GameConfig_SetDefaults
 * or GameConfig_LoadFromFile, and release it with GameConfig_Deconstruct (currently a no-op, present so the
 * teardown hook exists if the struct later gains owned resources). All fields are guaranteed to be within a
 * usable range after a successful or failed GameConfig_LoadFromFile.
 */
typedef struct GameConfigStruct
{
    /** @brief true to let the frame rate run unbounded (render every loop iteration); false to cap rendering
     *  at TargetFPS. The update loop runs at its own fixed rate regardless of this. */
    bool IsFPSUnlocked;
    /** @brief Target render frames per second when IsFPSUnlocked is false; always within a sane range after
     *  loading. Ignored when IsFPSUnlocked is true. */
    int32_t TargetFPS;
    /** @brief true to synchronize buffer swaps with the display refresh (no tearing). The swap then blocks
     *  until the next refresh, and the fixed-step updates owed for that wait run right after it, so input
     *  is still polled every update tick but in a burst. Usually paired with IsFPSUnlocked so the display,
     *  not TargetFPS, paces rendering. Default false. */
    bool IsVSyncEnabled;
    /** @brief true to create the window in fullscreen mode; false for a regular windowed mode. */
    bool IsFullscreen;
    /** @brief Window width in pixels; always strictly positive and within a sane range after loading. */
    int32_t ResolutionWidth;
    /** @brief Window height in pixels; always strictly positive and within a sane range after loading. */
    int32_t ResolutionHeight;

    // ---- Post-effect config-side multipliers ----
    // These are the game-wide multipliers the renderer combines (by multiplication) with each world's own
    // per-effect strength. A value of 0 disables the effect regardless of the world's setting; 1 leaves the
    // world's setting untouched. All are finite and >= 0 after loading (negatives/NaN fall back to default).
    /** @brief Config-side shadow strength multiplier (times the world's ShadowStrength). Default 1. */
    float ShadowStrength;
    /** @brief Config-side bloom strength multiplier (times the world's BloomStrength). Default 1. */
    float BloomStrength;
    /** @brief Config-side sunshaft strength multiplier (times the world's SunshaftStrength). Default 1. */
    float SunshaftStrength;
    /** @brief Config-side ambient-occlusion strength multiplier (times the world's AmbientOcclusionStrength).
     *  Default 1. Scales how strongly screen-space AO darkens creased/contact areas. */
    float AmbientOcclusionStrength;
    /** @brief Config-side haze strength multiplier (times the world's FogStrength). Default 1. Multiplies every haze
     *  distance and the ground haze's depth (the aerial perspective): 1 the real air, 0 no haze on anything (an
     *  accessibility setting: far things stay crisp). */
    float FogStrength;
    /** @brief Scales every draw distance that is not fixed (each world model's and world sprite's own, or a placement's;
     *  a sprite's kind's when its world sprite gives none: 150 m, decals 60 m): beyond its draw distance an object is not
     *  drawn, and over the last tenth of it (at least 3 m) it dithers out. Default 1; [0, 16] like the multipliers above
     *  (0 draws nothing but fixed ones). Lower it for slower machines or to see the fade sooner. */
    float DrawDistanceScale;
    /** @brief Scales every LOD switch distance (each world model's, or a placement's own, times its multiplier): above 1
     *  keeps the finer LODs farther (more detail), below 1 switches to coarser ones sooner. A fixed draw distance does
     *  not exempt the LODs. Default 1; [0, 16] like the multipliers above (0 always draws the coarsest LOD). */
    float LodDistanceScale;
} GameConfig;


// Functions.
/**
 * @brief Resets a config to its built-in default values.
 *
 * Overwrites every field of @p self with the compiled-in defaults, producing a valid, ready-to-use config
 * without touching the filesystem.
 * @param self The config to populate. May be NULL, in which case the call does nothing.
 */
void GameConfig_SetDefaults(GameConfig* self);

/**
 * @brief Loads the game configuration from a JSON file into a GameConfig.
 *
 * Initializes @p outConfig to the defaults, then reads @p path, parses it as JSON and overlays any settings
 * it contains. Missing keys, wrong-typed values and unknown keys are tolerated (the corresponding default is
 * kept), and any value that is out of range is clamped/replaced so @p outConfig is always left in a valid,
 * usable state — even when this function returns a failure. The JSON document and the object pool used to
 * parse it are fully released before returning; nothing JSON-related outlives the call.
 * @param path Null-terminated UTF-8 path of the config file to read. Must not be NULL.
 * @param outConfig [out] Receives the loaded configuration. Must not be NULL. Left at valid defaults if the
 *        file cannot be read or is not a JSON object.
 * @returns A success Error when the file was read and parsed as a JSON object. Raises
 *          ErrorCode_IllegalArgument if @p path or @p outConfig is NULL; ErrorCode_Deserialize if the root
 *          value is not a JSON object; propagates filesystem errors (e.g. ErrorCode_FileNotFound,
 *          ErrorCode_IO) and JSON parse errors (e.g. ErrorCode_InvalidJSON) from the underlying calls.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameConfig_LoadFromFile(const unsigned char* path, GameConfig* outConfig);

/**
 * @brief Releases any resources owned by a config.
 *
 * Present for API symmetry and future-proofing; a GameConfig currently owns nothing, so this is a no-op.
 * Safe to call on any config, including one never loaded.
 * @param self The config to release. May be NULL, in which case the call does nothing.
 */
void GameConfig_Deconstruct(GameConfig* self);
