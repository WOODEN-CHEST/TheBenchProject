#include "Config.h"
#include "wr/WRMemory.h"
#include "wr/WRFileSystem.h"
#include "wr/WRJSON.h"
#include "wr/WRCompile.h"
#include <stdint.h>


// Macros.
/** JSON key for the unlocked-frame-rate flag. */
#define CONFIG_KEY_IS_FPS_UNLOCKED ((const unsigned char*)u8"is_fps_unlocked")
/** JSON key for the fullscreen flag. */
#define CONFIG_KEY_IS_FULLSCREEN ((const unsigned char*)u8"is_fullscreen")
/** JSON key for the [width, height] resolution array. */
#define CONFIG_KEY_RESOLUTION ((const unsigned char*)u8"resolution")
/** JSON key for the target render frame rate. */
#define CONFIG_KEY_TARGET_FPS ((const unsigned char*)u8"target_fps")

/** Index of the width element within the resolution array. */
#define CONFIG_RESOLUTION_WIDTH_INDEX ((size_t)0)
/** Index of the height element within the resolution array. */
#define CONFIG_RESOLUTION_HEIGHT_INDEX ((size_t)1)

/** Initial capacity (bytes) reserved for the raw config file contents. */
#define CONFIG_FILE_INITIAL_CAPACITY ((size_t)256)

/** Default value for the unlocked-frame-rate flag. */
#define CONFIG_DEFAULT_IS_FPS_UNLOCKED false
/** Default value for the fullscreen flag. */
#define CONFIG_DEFAULT_IS_FULLSCREEN false
/** Default window width in pixels. */
#define CONFIG_DEFAULT_RESOLUTION_WIDTH ((int32_t)1280)
/** Default window height in pixels. */
#define CONFIG_DEFAULT_RESOLUTION_HEIGHT ((int32_t)720)
/** Default target render frame rate (used when the frame rate is not unlocked). */
#define CONFIG_DEFAULT_TARGET_FPS ((int32_t)60)
/** Smallest accepted target FPS; smaller values fall back to the default. */
#define CONFIG_MIN_TARGET_FPS ((int32_t)10)
/** Largest accepted target FPS; larger values fall back to the default. */
#define CONFIG_MAX_TARGET_FPS ((int32_t)1000)

/** Smallest window width accepted from the config; smaller values fall back to the default. */
#define CONFIG_MIN_RESOLUTION_WIDTH ((int32_t)320)
/** Smallest window height accepted from the config; smaller values fall back to the default. */
#define CONFIG_MIN_RESOLUTION_HEIGHT ((int32_t)240)
/** Largest window width accepted from the config; larger values fall back to the default. */
#define CONFIG_MAX_RESOLUTION_WIDTH ((int32_t)16384)
/** Largest window height accepted from the config; larger values fall back to the default. */
#define CONFIG_MAX_RESOLUTION_HEIGHT ((int32_t)16384)


// Static functions.
static int32_t ClampToInt32(int64_t value)
{
    if (value < INT32_MIN) { return INT32_MIN; }
    if (value > INT32_MAX) { return INT32_MAX; }
    return (int32_t)value;
}

/* Replaces out-of-range resolution values with the defaults so the config is always usable. */
static void ClampResolution(GameConfig* config)
{
    if ((config->ResolutionWidth < CONFIG_MIN_RESOLUTION_WIDTH)
        || (config->ResolutionWidth > CONFIG_MAX_RESOLUTION_WIDTH))
    {
        config->ResolutionWidth = CONFIG_DEFAULT_RESOLUTION_WIDTH;
    }
    if ((config->ResolutionHeight < CONFIG_MIN_RESOLUTION_HEIGHT)
        || (config->ResolutionHeight > CONFIG_MAX_RESOLUTION_HEIGHT))
    {
        config->ResolutionHeight = CONFIG_DEFAULT_RESOLUTION_HEIGHT;
    }
}

/* Reads a boolean setting; a missing or wrong-typed value leaves the default untouched. */
static void ReadBool(JSONCompound* root, const unsigned char* key, bool* outValue)
{
    JSONObjectValue Value;
    bool WasFound = false;
    Error Result = JSONCompound_GetOptionalVerified(root, key, JSONValueType_Boolean, &Value, &WasFound);
    if ((Result.Code == ErrorCode_Success) && WasFound)
    {
        *outValue = Value.Value.Boolean;
        return;
    }
    Error_Deconstruct(&Result);
}

/* Reads an integer setting (JSON integer or truncated real); a missing/wrong-typed value keeps the default. */
static void ReadInt32(JSONCompound* root, const unsigned char* key, int32_t* outValue)
{
    JSONObjectValue Value;
    bool WasFound = false;
    Error Result = JSONCompound_GetOptional(root, key, &Value, &WasFound);
    if ((Result.Code == ErrorCode_Success) && WasFound)
    {
        if (Value.Type == JSONValueType_Integer) { *outValue = ClampToInt32(Value.Value.Integer); }
        else if (Value.Type == JSONValueType_RealNumber) { *outValue = ClampToInt32((int64_t)Value.Value.RealNumber); }
    }
    Error_Deconstruct(&Result);
}

/* Reads a single integer element from an array. Returns false (leaving outValue untouched) when the element
   is absent or not numeric; JSON reals are truncated toward zero. */
static bool ReadArrayInt32(JSONArray* array, size_t index, int32_t* outValue)
{
    JSONObjectValue Value;
    bool WasFound = false;
    Error Result = JSONArray_GetOptional(array, index, &Value, &WasFound);
    if ((Result.Code != ErrorCode_Success) || !WasFound)
    {
        Error_Deconstruct(&Result);
        return false;
    }

    if (Value.Type == JSONValueType_Integer)
    {
        *outValue = ClampToInt32(Value.Value.Integer);
        return true;
    }
    if (Value.Type == JSONValueType_RealNumber)
    {
        *outValue = ClampToInt32((int64_t)Value.Value.RealNumber);
        return true;
    }
    return false;
}

/* Reads the resolution array; only overwrites the defaults when both components are present and numeric. */
static void ReadResolution(JSONCompound* root, GameConfig* config)
{
    JSONObjectValue Value;
    bool WasFound = false;
    Error Result = JSONCompound_GetOptionalVerified(root, CONFIG_KEY_RESOLUTION, JSONValueType_Array, &Value, &WasFound);
    if ((Result.Code != ErrorCode_Success) || !WasFound)
    {
        Error_Deconstruct(&Result);
        return;
    }

    int32_t Width = 0;
    int32_t Height = 0;
    if (ReadArrayInt32(Value.Value.Array, CONFIG_RESOLUTION_WIDTH_INDEX, &Width)
        && ReadArrayInt32(Value.Value.Array, CONFIG_RESOLUTION_HEIGHT_INDEX, &Height))
    {
        config->ResolutionWidth = Width;
        config->ResolutionHeight = Height;
    }
}

/* Copies every recognized setting from the parsed root object into the config. Each reader keeps the
   existing default when its key is absent or malformed. */
static void ReadConfigValues(JSONCompound* root, GameConfig* config)
{
    ReadBool(root, CONFIG_KEY_IS_FPS_UNLOCKED, &config->IsFPSUnlocked);
    ReadBool(root, CONFIG_KEY_IS_FULLSCREEN, &config->IsFullscreen);
    ReadInt32(root, CONFIG_KEY_TARGET_FPS, &config->TargetFPS);
    ReadResolution(root, config);
}

/* Parses the raw file bytes as a JSON object and overlays its settings onto the config, then discards the
   JSON object pool (and everything it owns) before returning. */
static Error ParseConfigBuffer(GenericBuffer* source, GameConfig* config)
{
    JSONObjectPool* Pool = NULL;
    Error PoolResult = JSONObjectPool_Create(&Pool);
    if (PoolResult.Code != ErrorCode_Success)
    {
        return PoolResult;
    }

    JSONObjectValue Root;
    Error ParseResult = JSON_Deserialize(Pool, source, &Root);
    if (ParseResult.Code != ErrorCode_Success)
    {
        Error DeconstructResult = JSONObjectPool_Deconstruct(Pool);
        Error_Deconstruct(&DeconstructResult);
        return ParseResult;
    }

    if (Root.Type == JSONValueType_Compound)
    {
        ReadConfigValues(Root.Value.Compound, config);
    }
    else
    {
        ParseResult = Error_Construct3(ErrorCode_Deserialize, u8"Config root must be a JSON object.");
    }

    // The values have been copied into plain fields, so the whole JSON document can be thrown away here.
    Error DeconstructResult = JSONObjectPool_Deconstruct(Pool);
    if ((ParseResult.Code == ErrorCode_Success) && (DeconstructResult.Code != ErrorCode_Success))
    {
        return DeconstructResult;
    }
    Error_Deconstruct(&DeconstructResult);
    return ParseResult;
}


// Public functions.
void GameConfig_SetDefaults(GameConfig* self)
{
    if (self == NULL) { return; }

    self->IsFPSUnlocked = CONFIG_DEFAULT_IS_FPS_UNLOCKED;
    self->IsFullscreen = CONFIG_DEFAULT_IS_FULLSCREEN;
    self->TargetFPS = CONFIG_DEFAULT_TARGET_FPS;
    self->ResolutionWidth = CONFIG_DEFAULT_RESOLUTION_WIDTH;
    self->ResolutionHeight = CONFIG_DEFAULT_RESOLUTION_HEIGHT;
}

Error GameConfig_LoadFromFile(const unsigned char* path, GameConfig* outConfig)
{
    if ((path == NULL) || (outConfig == NULL))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"GameConfig_LoadFromFile: path and outConfig must not be NULL.");
    }

    GameConfig_SetDefaults(outConfig);

    GenericBuffer FileBuffer;
    GenericBuffer_AllocateVariable(&FileBuffer, CONFIG_FILE_INITIAL_CAPACITY, sizeof(unsigned char));

    // Read raw bytes, NOT ReadAllText: the JSON parser wants the exact JSON bytes with no trailing null
    // terminator (a terminator would be seen as unexpected trailing content and fail the parse).
    Error ReadResult = FileSystem_ReadAllBytes(path, &FileBuffer);
    if (ReadResult.Code != ErrorCode_Success)
    {
        Memory_Free(FileBuffer._data);
        return ReadResult;
    }

    Error ParseResult = ParseConfigBuffer(&FileBuffer, outConfig);
    Memory_Free(FileBuffer._data);

    // Guard against invalid values regardless of whether parsing fully succeeded.
    ClampResolution(outConfig);
    if ((outConfig->TargetFPS < CONFIG_MIN_TARGET_FPS) || (outConfig->TargetFPS > CONFIG_MAX_TARGET_FPS))
    {
        outConfig->TargetFPS = CONFIG_DEFAULT_TARGET_FPS;
    }
    return ParseResult;
}

void GameConfig_Deconstruct(GameConfig* self)
{
    UNUSED(self);
}
