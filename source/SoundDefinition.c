#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "SoundEngine.h"
#include "wr/WRMemory.h"
#include "wr/WRJSON.h"

/*
 * The sound loader decodes an audio file into normalized interleaved 32-bit float samples and wraps them in
 * an audio-engine GameSound (the source sound the SoundEngine plays), so loaded sounds can be fed straight
 * to AudioTrack_CreateSoundInstance. The loaded asset owns the sample buffer and the GameSound borrows it.
 * Decoding is done with Raylib's wave loader and does not touch the audio device.
 */


// Types.
typedef struct SoundDefinitionStruct
{
    AssetDefinition Base;
    OwnedAssetLocation Location;
    unsigned char* Format; // owned; extension hint for reference locations, else NULL
} SoundDefinition;

typedef struct SoundLoadedStruct
{
    float* Samples;  // Owned decoded sample buffer (from LoadWaveSamples); borrowed by Sound.
    GameSound Sound; // Asset points here; borrows Samples.
} SoundLoaded;


// Static functions.
static void SoundLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    SoundLoaded* Loaded = self->DestroyContext;
    Error DeconstructResult = GameSound_Deconstruct(&Loaded->Sound);
    Error_Deconstruct(&DeconstructResult);
    UnloadWaveSamples(Loaded->Samples); // Raylib-allocated; must be freed with its matching unloader.
    Memory_Free(Loaded);
}

static const LoadedAssetVTable SoundLoadedVTable = { SoundLoaded_Destroy };

static Error SoundDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    SoundDefinition* Definition = self;

    AssetLocation Location = OwnedAssetLocation_View(&Definition->Location);
    AssetResourcePath* PathHandle = NULL;
    Error PathResult = AssetManager_AcquireResourcePath(manager, Definition->Base.Type, &Location, Definition->Format, &PathHandle);
    if (PathResult.Code != ErrorCode_Success) { return PathResult; }

    Wave LoadedWave = LoadWave((const char*)AssetResourcePath_Get(PathHandle));
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, PathHandle);
    Error_Deconstruct(&ReleaseResult);

    if (LoadedWave.frameCount == 0)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to load sound \"%s\".", Definition->Base.Name);
    }
    if ((LoadedWave.channels < 1U) || (LoadedWave.channels > (unsigned int)MAX_AUDIO_CHANNELS))
    {
        Error Result = Error_Construct3(ErrorCode_InvalidAssetData,
            u8"Sound \"%s\" has %d channels; only 1 to %d are supported.",
            Definition->Base.Name, (int32_t)LoadedWave.channels, MAX_AUDIO_CHANNELS);
        UnloadWave(LoadedWave);
        return Result;
    }

    // Decode into normalized interleaved float samples; the raw wave is no longer needed afterwards.
    float* Samples = LoadWaveSamples(LoadedWave);
    AudioFormat Format = { .SampleRate = (float)LoadedWave.sampleRate, .ChannelCount = (int32_t)LoadedWave.channels };
    size_t SampleCount = (size_t)LoadedWave.frameCount * (size_t)LoadedWave.channels;
    UnloadWave(LoadedWave);

    if (Samples == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to decode samples for sound \"%s\".", Definition->Base.Name);
    }

    SoundLoaded* Loaded = Memory_Allocate(sizeof(SoundLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Samples = Samples;

    Error ConstructResult = GameSound_Construct1(&Loaded->Sound, Samples, SampleCount, Format);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        UnloadWaveSamples(Samples);
        Memory_Free(Loaded);
        return ConstructResult;
    }

    outLoaded->Asset = &Loaded->Sound;
    outLoaded->VTable = &SoundLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void SoundDefinition_Destroy(void* self)
{
    SoundDefinition* Definition = self;
    OwnedAssetLocation_Deconstruct(&Definition->Location);
    Memory_Free(Definition->Format);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable SoundDefinitionVTable = { SoundDefinition_LoadAsset, SoundDefinition_Destroy };


// Public functions.
Error SoundDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
    const unsigned char* sourceDescription, AssetDefinition** outDefinition)
{
    (void)manager;
    *outDefinition = NULL;

    JSONObjectPool* Pool = UserData_GetPointer(userData);
    JSONObjectValue Root;
    JSONCompound* RootCompound = NULL;
    Error Result = AssetJSON_DeserializeRoot(Pool, rawData, sourceDescription, &Root, &RootCompound);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    SoundDefinition* Definition = Memory_Allocate(sizeof(SoundDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &SoundDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadLocation(RootCompound, (const unsigned char*)u8"location", sourceDescription, &Definition->Location);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalOwnedString(RootCompound, (const unsigned char*)u8"format", &Definition->Format, NULL);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        SoundDefinition_Destroy(Definition);
        return Result;
    }

    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
