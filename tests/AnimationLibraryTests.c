#include "TestSuites.h"
#include "TestAnimation.h"
#include "AnimationLibrary.h"
#include "AssetManager.h"
#include "SkeletalAnimation.h"
#include "raylib/raymath.h"
#include "wr/WRFileSystem.h"
#include "wr/WRJSON.h"
#include "wr/WRPath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Tolerance for sampled values. */
#define TEST_EPSILON 1e-5f
/** Room for paths and texts in buffers. */
#define TEST_BUFFER_CAPACITY 512U
/** The shipped placeholder files (asset_source/scripts/placeholder_humanoid.py writes them). */
#define MOTIONS_PATH "compile/out/asset/animation_libraries/placeholder_humanoid/motions.glb"
/** The placeholder library's clips: nine (the movements and a wave), and seven for the actions. */
#define PLACEHOLDER_CLIP_COUNT 16U
#define MODEL_WITH_CLIPS_PATH "tests/data/placeholder_humanoid_with_clips.glb"


// Fields: a small test file (a root and one bone, each moved by one clip).
static const float TimesA[] = { 0.0f, 0.5f, 1.0f };
static const float TranslationsA[] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f };


// Static functions: helpers.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_EPSILON;
}

static bool IsNearVector(Vector3 a, Vector3 b)
{
    return IsNear(a.x, b.x) && IsNear(a.y, b.y) && IsNear(a.z, b.z);
}

/* Whether two rotations are the same turn (q and -q are). */
static bool IsSameRotation(Quaternion a, Quaternion b, float tolerance)
{
    float Dot = (a.x * b.x) + (a.y * b.y) + (a.z * b.z) + (a.w * b.w);
    return fabsf(fabsf(Dot) - 1.0f) < tolerance;
}

static AnimationLibraryReadOptions MakeOptions(const AnimationClipSettings* clips, size_t clipCount)
{
    return (AnimationLibraryReadOptions){ .Clips = clips, .ClipCount = clipCount, .FilePath = NULL, .Logger = NULL,
        .Description = (const unsigned char*)u8"test library" };
}

/* Two nodes: "root" at the origin and "bone" 1 m up, with one linear translation clip "slide" on the bone. */
static void WriteSlideFile(const char* interpolation, const float* times, uint32_t keyCount, const float* values,
    GenericBuffer* out)
{
    TestGltfNode Nodes[2] =
    {
        { .Name = "root", .Parent = -1, .Rest = TestAnimation_Identity() },
        { .Name = "bone", .Parent = 0, .Rest = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
    };
    TestGltfChannel Channel = { .Node = 1, .Path = "translation", .Interpolation = interpolation, .Times = times,
        .KeyCount = keyCount, .Values = values, .ValueComponents = 3U };
    TestGltfClip Clip = { .Name = "slide", .Channels = &Channel, .ChannelCount = 1U };
    TestGltf_Write(Nodes, 2U, &Clip, 1U, out);
}

static Error ReadText(const GenericBuffer* text, const AnimationLibraryReadOptions* options,
    AnimationLibrary** outLibrary)
{
    // The text's counted terminator is not part of the file.
    return AnimationLibrary_CreateFromGltf(text->_data, text->_count - 1U, options, outLibrary);
}

/* Reads the slide file and samples the bone's translation track at @p time. */
static bool SampleSlide(TestRun* run, const AnimationLibrary* library, float time, Vector3* outValue)
{
    uint32_t Clip = 0U;
    uint32_t Bone = 0U;
    if (!TEST_CHECK(run, AnimationLibrary_TryFindClip(library, (const unsigned char*)u8"slide", &Clip))
        || !TEST_CHECK(run, AnimationLibrary_TryFindBone(library, (const unsigned char*)u8"bone", &Bone)))
    {
        return false;
    }
    const AnimationBoneTracks* Tracks = AnimationClip_GetBoneTracks(AnimationLibrary_GetClip(library, Clip), Bone);
    *outValue = AnimationTrack_SampleVector3(&Tracks->_translation, time);
    return true;
}

static bool ReadRepositoryLibrary(TestRun* run, const char* relativePath, const AnimationLibraryReadOptions* options,
    AnimationLibrary** outLibrary)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    bool IsRead = TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, relativePath, &Path))
        && TEST_CHECK_SUCCESS(run, AnimationLibrary_CreateFromFile(Path._data, options, outLibrary));
    Memory_Free(Path._data);
    return IsRead;
}


// Static functions: the shipped files.
static void TheMotionsLibraryReadsThePlaceholderClips(TestRun* run)
{
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    if (!ReadRepositoryLibrary(run, MOTIONS_PATH, &Options, &Library))
    {
        return;
    }
    // The armature object above the bones (no skin: what Blender exports for actions alone) and its 19 bones.
    TEST_CHECK(run, AnimationLibrary_GetBoneCount(Library) == 20U);
    TEST_CHECK(run, AnimationLibrary_GetClipCount(Library) == PLACEHOLDER_CLIP_COUNT);
    uint32_t Armature = 0U;
    uint32_t Hips = 0U;
    uint32_t Head = 0U;
    TEST_CHECK(run, AnimationLibrary_TryFindBone(Library, (const unsigned char*)u8"armature", &Armature));
    TEST_CHECK(run, AnimationLibrary_TryFindBone(Library, (const unsigned char*)u8"hips", &Hips));
    TEST_CHECK(run, AnimationLibrary_TryFindBone(Library, (const unsigned char*)u8"head", &Head));
    TEST_CHECK(run, AnimationLibrary_GetBone(Library, Armature)->_parent == -1);
    TEST_CHECK(run, AnimationLibrary_GetBone(Library, Hips)->_parent == (int32_t)Armature);
    TEST_CHECK(run, IsNear(AnimationLibrary_GetBone(Library, Hips)->_restPosition.y, 0.95f));
    TEST_CHECK(run, IsNear(AnimationLibrary_GetBone(Library, Head)->_restPosition.y, 1.55f));

    static const char* const Names[] =
    {
        "idle", "walk", "run", "crouch_idle", "crouch_walk", "jump", "fall", "land", "wave", "sit_down", "sit_idle",
        "stand_up", "pick_up", "hold_r", "put_down", "open_door"
    };
    static const float Durations[] =
    {
        3.0f, 1.0f, 0.68f, 2.0f, 1.4f, 0.6f, 1.0f, 0.5f, 2.0f, 1.3f, 4.0f, 1.3f, 1.5f, 2.0f, 1.6f, 1.4f
    };
    for (size_t Index = 0U; Index < sizeof(Names) / sizeof(Names[0]); Index++)
    {
        uint32_t Clip = 0U;
        bool IsFound = AnimationLibrary_TryFindClip(Library, (const unsigned char*)Names[Index], &Clip);
        TEST_CHECK(run, IsFound);
        if (IsFound)
        {
            const AnimationClip* Found = AnimationLibrary_GetClip(Library, Clip);
            TEST_CHECK(run, IsNear(AnimationClip_GetDuration(Found), Durations[Index]));
            // No definition settings here: the file's names, no looping, speed 1.
            TEST_CHECK(run, !AnimationClip_IsLooped(Found) && (AnimationClip_GetSpeed(Found) == 1.0f));
            TEST_CHECK(run, AnimationTrack_IsAnimated(&AnimationClip_GetBoneTracks(Found, Hips)->_translation));
            TEST_CHECK(run, !AnimationTrack_IsAnimated(&AnimationClip_GetBoneTracks(Found, Armature)->_rotation));
        }
    }
    AnimationLibrary_Destroy(Library);
}

static void AModelFileWithItsSkinReadsAsALibrary(TestRun* run)
{
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    if (!ReadRepositoryLibrary(run, MODEL_WITH_CLIPS_PATH, &Options, &Library))
    {
        return;
    }
    // The mesh's node is no bone: only the 19 joints, the hips a root.
    TEST_CHECK(run, AnimationLibrary_GetBoneCount(Library) == 19U);
    TEST_CHECK(run, AnimationLibrary_GetClipCount(Library) == PLACEHOLDER_CLIP_COUNT);
    uint32_t Hips = 0U;
    TEST_CHECK(run, AnimationLibrary_TryFindBone(Library, (const unsigned char*)u8"hips", &Hips)
        && (AnimationLibrary_GetBone(Library, Hips)->_parent == -1));
    TEST_CHECK(run, !AnimationLibrary_TryFindBone(Library, (const unsigned char*)u8"body", NULL));
    AnimationLibrary_Destroy(Library);
}

static void ClipSettingsRenameLoopAndSpeedClips(TestRun* run)
{
    AnimationClipSettings Settings[2] =
    {
        { .Source = (const unsigned char*)u8"walk", .Name = (const unsigned char*)u8"stroll", .IsLooped = true,
            .Speed = 1.5f },
        { .Source = (const unsigned char*)u8"run", .Name = (const unsigned char*)u8"run", .IsLooped = true,
            .Speed = 1.0f },
    };
    AnimationLibraryReadOptions Options = MakeOptions(Settings, 2U);
    AnimationLibrary* Library = NULL;
    if (!ReadRepositoryLibrary(run, MOTIONS_PATH, &Options, &Library))
    {
        return;
    }
    uint32_t Stroll = 0U;
    uint32_t Run = 0U;
    uint32_t WaveClip = 0U;
    TEST_CHECK(run, !AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"walk", NULL));
    TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"stroll", &Stroll)
        && AnimationClip_IsLooped(AnimationLibrary_GetClip(Library, Stroll))
        && (AnimationClip_GetSpeed(AnimationLibrary_GetClip(Library, Stroll)) == 1.5f));
    TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"run", &Run)
        && AnimationClip_IsLooped(AnimationLibrary_GetClip(Library, Run)));
    TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"wave", &WaveClip)
        && !AnimationClip_IsLooped(AnimationLibrary_GetClip(Library, WaveClip)));
    TEST_CHECK(run, AnimationLibrary_GetClipCount(Library) == PLACEHOLDER_CLIP_COUNT);
    AnimationLibrary_Destroy(Library);
}

static void SettingsThatDoNotFitTheFileAreRefused(TestRun* run)
{
    AnimationClipSettings Missing = { .Source = (const unsigned char*)u8"dance",
        .Name = (const unsigned char*)u8"dance", .IsLooped = false, .Speed = 1.0f };
    AnimationLibraryReadOptions Options = MakeOptions(&Missing, 1U);
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    AnimationLibrary* Library = NULL;
    if (TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, MOTIONS_PATH, &Path)))
    {
        TEST_CHECK_ERROR(run, AnimationLibrary_CreateFromFile(Path._data, &Options, &Library),
            ErrorCode_InvalidAssetDefinition);
        TEST_CHECK(run, Library == NULL);
        // Renamed onto another clip's name: two clips of one name.
        AnimationClipSettings Colliding = { .Source = (const unsigned char*)u8"walk",
            .Name = (const unsigned char*)u8"run", .IsLooped = false, .Speed = 1.0f };
        Options = MakeOptions(&Colliding, 1U);
        TEST_CHECK_ERROR(run, AnimationLibrary_CreateFromFile(Path._data, &Options, &Library),
            ErrorCode_InvalidAssetData);
        TEST_CHECK(run, Library == NULL);
    }
    Memory_Free(Path._data);
}


// Static functions: sampling.
static void SamplingHoldsTheEndsAndReadsTheLastKeyExactly(TestRun* run)
{
    GenericBuffer Text;
    WriteSlideFile("LINEAR", TimesA, 3U, TranslationsA, &Text);
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    Vector3 Value = { 0.0f, 0.0f, 0.0f };
    if (TEST_CHECK_SUCCESS(run, ReadText(&Text, &Options, &Library)))
    {
        TEST_CHECK(run, IsNear(AnimationClip_GetDuration(AnimationLibrary_GetClip(Library, 0U)), 1.0f));
        TEST_CHECK(run, SampleSlide(run, Library, -1.0f, &Value) && (Value.x == 0.0f));
        TEST_CHECK(run, SampleSlide(run, Library, 0.25f, &Value) && IsNear(Value.x, 0.5f));
        TEST_CHECK(run, SampleSlide(run, Library, 0.75f, &Value) && IsNear(Value.x, 2.5f));
        // At the last key's own time the last key, exactly (raylib's loader read the second key there).
        TEST_CHECK(run, SampleSlide(run, Library, 1.0f, &Value) && (Value.x == 4.0f));
        TEST_CHECK(run, SampleSlide(run, Library, 5.0f, &Value) && (Value.x == 4.0f));
        TEST_CHECK(run, SampleSlide(run, Library, NAN, &Value) && (Value.x == 0.0f));
        // The bone a clip does not move keeps its rest pose in AnimationClip_SampleBone.
        uint32_t Root = 0U;
        TEST_CHECK(run, AnimationLibrary_TryFindBone(Library, (const unsigned char*)u8"root", &Root));
        Transform Rest = TestAnimation_Translation(7.0f, 0.0f, 0.0f);
        Transform Sampled = AnimationClip_SampleBone(AnimationLibrary_GetClip(Library, 0U), Root, Rest, 0.5f);
        TEST_CHECK(run, Sampled.translation.x == 7.0f);
        AnimationLibrary_Destroy(Library);
    }
    Memory_Free(Text._data);
}

static void StepAndCubicSplineTracksSampleAsGltfSays(TestRun* run)
{
    GenericBuffer Text;
    WriteSlideFile("STEP", TimesA, 3U, TranslationsA, &Text);
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    Vector3 Value = { 0.0f, 0.0f, 0.0f };
    if (TEST_CHECK_SUCCESS(run, ReadText(&Text, &Options, &Library)))
    {
        TEST_CHECK(run, SampleSlide(run, Library, 0.49f, &Value) && (Value.x == 0.0f));
        TEST_CHECK(run, SampleSlide(run, Library, 0.5f, &Value) && (Value.x == 1.0f));
        TEST_CHECK(run, SampleSlide(run, Library, 0.99f, &Value) && (Value.x == 1.0f));
        TEST_CHECK(run, SampleSlide(run, Library, 1.0f, &Value) && (Value.x == 4.0f));
        AnimationLibrary_Destroy(Library);
    }
    Memory_Free(Text._data);

    // Two cubic keys with flat tangents: the curve 3u^2 - 2u^3 from 0 to 1 (each key: in-tangent, value, out-tangent).
    static const float CubicTimes[] = { 0.0f, 2.0f };
    static const float CubicValues[] =
    {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    };
    WriteSlideFile("CUBICSPLINE", CubicTimes, 2U, CubicValues, &Text);
    if (TEST_CHECK_SUCCESS(run, ReadText(&Text, &Options, &Library)))
    {
        TEST_CHECK(run, SampleSlide(run, Library, 1.0f, &Value) && IsNear(Value.x, 0.5f));
        TEST_CHECK(run, SampleSlide(run, Library, 0.5f, &Value) && IsNear(Value.x, 0.15625f));
        TEST_CHECK(run, SampleSlide(run, Library, 2.0f, &Value) && (Value.x == 1.0f));
        AnimationLibrary_Destroy(Library);
    }
    Memory_Free(Text._data);

    // Tangents: a slope of 1 m/s at both keys over 2 s from 0 to 2 is the straight line.
    static const float SlopeValues[] =
    {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
    };
    WriteSlideFile("CUBICSPLINE", CubicTimes, 2U, SlopeValues, &Text);
    if (TEST_CHECK_SUCCESS(run, ReadText(&Text, &Options, &Library)))
    {
        TEST_CHECK(run, SampleSlide(run, Library, 0.5f, &Value) && IsNear(Value.x, 0.5f));
        TEST_CHECK(run, SampleSlide(run, Library, 1.5f, &Value) && IsNear(Value.x, 1.5f));
        AnimationLibrary_Destroy(Library);
    }
    Memory_Free(Text._data);
}

static void RotationsTakeTheShorterArcAndComeNormalized(TestRun* run)
{
    float Sine = sinf(PI / 4.0f);
    float Cosine = cosf(PI / 4.0f);
    // A quarter turn about Y, written in the far hemisphere (-q is the same turn).
    const float Rotations[] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -Sine, 0.0f, -Cosine };
    static const float Times[] = { 0.0f, 1.0f };
    TestGltfNode Nodes[1] = { { .Name = "bone", .Parent = -1, .Rest = TestAnimation_Identity() } };
    for (int Shorts = 0; Shorts < 2; Shorts++)
    {
        TestGltfChannel Channel = { .Node = 0, .Path = "rotation", .Interpolation = "LINEAR", .Times = Times,
            .KeyCount = 2U, .Values = Rotations, .ValueComponents = 4U, .AreValuesShorts = (Shorts == 1) };
        TestGltfClip Clip = { .Name = "turn", .Channels = &Channel, .ChannelCount = 1U };
        GenericBuffer Text;
        TestGltf_Write(Nodes, 1U, &Clip, 1U, &Text);
        AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
        AnimationLibrary* Library = NULL;
        if (TEST_CHECK_SUCCESS(run, ReadText(&Text, &Options, &Library)))
        {
            const AnimationTrack* Track = &AnimationClip_GetBoneTracks(AnimationLibrary_GetClip(Library, 0U), 0U)->_rotation;
            Quaternion Half = AnimationTrack_SampleRotation(Track, 0.5f);
            float Tolerance = (Shorts == 1) ? 1e-4f : 1e-6f;
            // Halfway along the shorter arc: an eighth of a turn about Y, not three eighths the other way.
            TEST_CHECK(run, IsSameRotation(Half, QuaternionFromAxisAngle((Vector3){ 0.0f, 1.0f, 0.0f }, PI / 4.0f),
                Tolerance));
            TEST_CHECK(run, fabsf(QuaternionLength(AnimationTrack_SampleRotation(Track, 1.0f)) - 1.0f) < 1e-6f);
            AnimationLibrary_Destroy(Library);
        }
        Memory_Free(Text._data);
    }

    // A rotation so long that its float square overflows: normalized all the same, a half turn about X, as a key and
    // as a node's rest pose, never a rotation of length 0.
    const float Long[] = { 3e19f, 0.0f, 0.0f, 0.0f, 3e19f, 0.0f, 0.0f, 0.0f };
    TestGltfChannel LongChannel = { .Node = 0, .Path = "rotation", .Interpolation = "LINEAR", .Times = Times,
        .KeyCount = 2U, .Values = Long, .ValueComponents = 4U };
    TestGltfClip LongClip = { .Name = "turn", .Channels = &LongChannel, .ChannelCount = 1U };
    TestGltfNode LongNodes[1] = { { .Name = "bone", .Parent = -1, .Rest = TestAnimation_Identity() } };
    LongNodes[0].Rest.rotation = (Quaternion){ 3e19f, 0.0f, 0.0f, 0.0f };
    GenericBuffer LongText;
    TestGltf_Write(LongNodes, 1U, &LongClip, 1U, &LongText);
    AnimationLibraryReadOptions LongOptions = MakeOptions(NULL, 0U);
    AnimationLibrary* LongLibrary = NULL;
    if (TEST_CHECK_SUCCESS(run, ReadText(&LongText, &LongOptions, &LongLibrary)))
    {
        const AnimationTrack* Track = &AnimationClip_GetBoneTracks(AnimationLibrary_GetClip(LongLibrary, 0U), 0U)->_rotation;
        Quaternion HalfTurn = { 1.0f, 0.0f, 0.0f, 0.0f };
        TEST_CHECK(run, IsSameRotation(AnimationTrack_SampleRotation(Track, 0.0f), HalfTurn, 1e-6f));
        TEST_CHECK(run, IsSameRotation(AnimationLibrary_GetBone(LongLibrary, 0U)->_rest.rotation, HalfTurn, 1e-6f));
        AnimationLibrary_Destroy(LongLibrary);
    }
    Memory_Free(LongText._data);
}

static void ANodeMatrixReadsAsItsTransform(TestRun* run)
{
    // A non-uniform scale under a turn: what raymath's own decomposition reads wrong.
    Transform Expected = { { 1.0f, 2.0f, 3.0f }, QuaternionFromAxisAngle((Vector3){ 0.0f, 0.0f, 1.0f }, PI / 2.0f),
        { 2.0f, 1.0f, 0.5f } };
    TestGltfNode Nodes[2] =
    {
        { .Name = "root", .Parent = -1, .HasMatrix = true, .Matrix = SkeletalAnimation_ComposeTransform(Expected) },
        { .Name = "bone", .Parent = 0, .Rest = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
    };
    TestGltfChannel Channel = { .Node = 1, .Path = "translation", .Interpolation = "LINEAR", .Times = TimesA,
        .KeyCount = 3U, .Values = TranslationsA, .ValueComponents = 3U };
    TestGltfClip Clip = { .Name = "slide", .Channels = &Channel, .ChannelCount = 1U };
    GenericBuffer Text;
    TestGltf_Write(Nodes, 2U, &Clip, 1U, &Text);
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    if (TEST_CHECK_SUCCESS(run, ReadText(&Text, &Options, &Library)))
    {
        uint32_t Root = 0U;
        uint32_t Bone = 0U;
        TEST_CHECK(run, AnimationLibrary_TryFindBone(Library, (const unsigned char*)u8"root", &Root));
        TEST_CHECK(run, AnimationLibrary_TryFindBone(Library, (const unsigned char*)u8"bone", &Bone));
        Transform Rest = AnimationLibrary_GetBone(Library, Root)->_rest;
        TEST_CHECK(run, IsNearVector(Rest.translation, Expected.translation));
        TEST_CHECK(run, IsNearVector(Rest.scale, Expected.scale));
        TEST_CHECK(run, IsSameRotation(Rest.rotation, Expected.rotation, 1e-6f));
        // The bone 1 m up its parent's y axis, which the turn points along -x and the scale leaves 1 long.
        TEST_CHECK(run, IsNearVector(AnimationLibrary_GetBone(Library, Bone)->_restPosition,
            (Vector3){ 0.0f, 2.0f, 3.0f }));
        AnimationLibrary_Destroy(Library);
    }
    Memory_Free(Text._data);
}

static void AParentListedAfterItsChildIsPlacedFirst(TestRun* run)
{
    TestGltfNode Nodes[2] =
    {
        { .Name = "child", .Parent = 1, .Rest = TestAnimation_Translation(0.0f, 0.5f, 0.0f) },
        { .Name = "parent", .Parent = -1, .Rest = TestAnimation_Translation(1.0f, 0.0f, 0.0f) },
    };
    TestGltfChannel Channel = { .Node = 0, .Path = "translation", .Interpolation = "LINEAR", .Times = TimesA,
        .KeyCount = 3U, .Values = TranslationsA, .ValueComponents = 3U };
    TestGltfClip Clip = { .Name = "slide", .Channels = &Channel, .ChannelCount = 1U };
    GenericBuffer Text;
    TestGltf_Write(Nodes, 2U, &Clip, 1U, &Text);
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    if (TEST_CHECK_SUCCESS(run, ReadText(&Text, &Options, &Library)))
    {
        uint32_t Child = 0U;
        uint32_t Parent = 0U;
        TEST_CHECK(run, AnimationLibrary_TryFindBone(Library, (const unsigned char*)u8"child", &Child));
        TEST_CHECK(run, AnimationLibrary_TryFindBone(Library, (const unsigned char*)u8"parent", &Parent));
        TEST_CHECK(run, (Child == 0U) && (AnimationLibrary_GetBone(Library, Child)->_parent == (int32_t)Parent));
        TEST_CHECK(run, IsNearVector(AnimationLibrary_GetBone(Library, Child)->_restPosition,
            (Vector3){ 1.0f, 0.5f, 0.0f }));
        AnimationLibrary_Destroy(Library);
    }
    Memory_Free(Text._data);
}

static void ClipsWithoutAUsableNameOrChannelAreSkipped(TestRun* run)
{
    TestGltfNode Nodes[2] =
    {
        { .Name = "root", .Parent = -1, .Rest = TestAnimation_Identity() },
        { .Name = "bone", .Parent = 0, .Rest = TestAnimation_Identity() },
    };
    TestGltfChannel Channel = { .Node = 1, .Path = "translation", .Interpolation = "LINEAR", .Times = TimesA,
        .KeyCount = 3U, .Values = TranslationsA, .ValueComponents = 3U };
    TestGltfChannel NoTarget = Channel;
    NoTarget.Node = -1;
    TestGltfClip Clips[4] =
    {
        { .Name = "Walk Cycle", .Channels = &Channel, .ChannelCount = 1U },
        { .Name = NULL, .Channels = &Channel, .ChannelCount = 1U },
        { .Name = "nothing", .Channels = &NoTarget, .ChannelCount = 1U },
        { .Name = "walk", .Channels = &Channel, .ChannelCount = 1U },
    };
    GenericBuffer Text;
    TestGltf_Write(Nodes, 2U, Clips, 4U, &Text);
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    if (TEST_CHECK_SUCCESS(run, ReadText(&Text, &Options, &Library)))
    {
        TEST_CHECK(run, AnimationLibrary_GetClipCount(Library) == 1U);
        TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"walk", NULL));
        AnimationLibrary_Destroy(Library);
    }
    // A definition setting gives the invalid name a usable one.
    AnimationClipSettings Rename = { .Source = (const unsigned char*)u8"Walk Cycle",
        .Name = (const unsigned char*)u8"walk_cycle", .IsLooped = true, .Speed = 1.0f };
    Options = MakeOptions(&Rename, 1U);
    if (TEST_CHECK_SUCCESS(run, ReadText(&Text, &Options, &Library)))
    {
        TEST_CHECK(run, AnimationLibrary_GetClipCount(Library) == 2U);
        TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"walk_cycle", NULL));
        AnimationLibrary_Destroy(Library);
    }
    Memory_Free(Text._data);
}


// Static functions: hostile files.
/* Reads the slide file changed by one replacement and expects @p expected. */
static void CheckRefusedVariant(TestRun* run, const char* from, const char* to, ErrorCode expected)
{
    GenericBuffer Text;
    WriteSlideFile("LINEAR", TimesA, 3U, TranslationsA, &Text);
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    bool IsReplaced = TestGltf_Replace(&Text, from, to);
    TEST_CHECK(run, IsReplaced);
    if (IsReplaced && !TEST_CHECK_ERROR(run, ReadText(&Text, &Options, &Library), expected))
    {
        printf("    (the variant replaced \"%s\" with \"%s\")\n", from, to);
    }
    TEST_CHECK(run, Library == NULL);
    AnimationLibrary_Destroy(Library);
    Memory_Free(Text._data);
}

/* As CheckRefusedVariant, and the refusal's message holds @p fragment: which check refused it, when several would. */
static void CheckRefusedVariantSaying(TestRun* run, const char* from, const char* to, const char* fragment)
{
    GenericBuffer Text;
    WriteSlideFile("LINEAR", TimesA, 3U, TranslationsA, &Text);
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    bool IsReplaced = TestGltf_Replace(&Text, from, to);
    TEST_CHECK(run, IsReplaced);
    if (IsReplaced)
    {
        Error Result = ReadText(&Text, &Options, &Library);
        bool IsSaid = (Result.Code == ErrorCode_InvalidAssetData) && (Result.Message != NULL)
            && (strstr((const char*)Result.Message, fragment) != NULL);
        if (!TEST_CHECK(run, IsSaid))
        {
            printf("    (the variant replaced \"%s\" with \"%s\": %s)\n", from, to,
                (Result.Message != NULL) ? (const char*)Result.Message : "no message");
        }
        Error_Deconstruct(&Result);
    }
    TEST_CHECK(run, Library == NULL);
    AnimationLibrary_Destroy(Library);
    Memory_Free(Text._data);
}

/* Reads a slide file with the given keys and expects @p expected. */
static void CheckRefusedKeys(TestRun* run, const char* path, const float* times, uint32_t keyCount, const float* values,
    uint32_t components, ErrorCode expected)
{
    TestGltfNode Nodes[2] =
    {
        { .Name = "root", .Parent = -1, .Rest = TestAnimation_Identity() },
        { .Name = "bone", .Parent = 0, .Rest = TestAnimation_Identity() },
    };
    TestGltfChannel Channel = { .Node = 1, .Path = path, .Interpolation = "LINEAR", .Times = times,
        .KeyCount = keyCount, .Values = values, .ValueComponents = components };
    TestGltfClip Clip = { .Name = "bad", .Channels = &Channel, .ChannelCount = 1U };
    GenericBuffer Text;
    TestGltf_Write(Nodes, 2U, &Clip, 1U, &Text);
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    TEST_CHECK_ERROR(run, ReadText(&Text, &Options, &Library), expected);
    TEST_CHECK(run, Library == NULL);
    Memory_Free(Text._data);
}

static void HostileFilesAreRefused(TestRun* run)
{
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    static const char NotGltf[] = "{ this is not glTF";
    TEST_CHECK_ERROR(run, AnimationLibrary_CreateFromGltf(NotGltf, sizeof(NotGltf) - 1U, &Options, &Library),
        ErrorCode_InvalidAssetData);
    TEST_CHECK_ERROR(run, AnimationLibrary_CreateFromGltf(NULL, 0U, &Options, &Library), ErrorCode_InvalidAssetData);

    static const float Falling[] = { 0.0f, 1.0f, 0.5f };
    static const float Negative[] = { -0.5f, 1.0f, 2.0f };
    static const float TooLong[] = { 0.0f, 1.0f, 4000.0f };
    const float WithNaN[] = { 0.0f, 0.0f, 0.0f, NAN, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f };
    const float WithInfinity[] = { 0.0f, 1.0f, INFINITY };
    static const float ZeroRotation[] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    CheckRefusedKeys(run, "translation", Falling, 3U, TranslationsA, 3U, ErrorCode_InvalidAssetData);
    CheckRefusedKeys(run, "translation", Negative, 3U, TranslationsA, 3U, ErrorCode_InvalidAssetData);
    CheckRefusedKeys(run, "translation", TooLong, 3U, TranslationsA, 3U, ErrorCode_InvalidAssetData);
    CheckRefusedKeys(run, "translation", WithInfinity, 3U, TranslationsA, 3U, ErrorCode_InvalidAssetData);
    CheckRefusedKeys(run, "translation", TimesA, 3U, WithNaN, 3U, ErrorCode_InvalidAssetData);
    CheckRefusedKeys(run, "rotation", TimesA, 3U, ZeroRotation, 4U, ErrorCode_InvalidAssetData);
    // A translation's values with four components each.
    CheckRefusedKeys(run, "translation", TimesA, 3U, ZeroRotation, 4U, ErrorCode_InvalidAssetData);

    // Key times that are not floats; values of the wrong shape; values that do not fit the times (cgltf's check).
    CheckRefusedVariant(run, "\"componentType\":5126,\"count\":3,\"type\":\"SCALAR\"",
        "\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"", ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"count\":3,\"type\":\"VEC3\"", "\"count\":3,\"type\":\"VEC2\"",
        ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"count\":3,\"type\":\"VEC3\"", "\"count\":2,\"type\":\"VEC3\"",
        ErrorCode_InvalidAssetData);
    // A buffer file outside the library's directory, and one there is no path to find.
    CheckRefusedVariant(run, "\"uri\":\"data:application/octet-stream;base64,", "\"uri\":\"../secret.bin\",\"x\":\"",
        ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"uri\":\"data:application/octet-stream;base64,", "\"uri\":\"/etc/secret.bin\",\"x\":\"",
        ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"uri\":\"data:application/octet-stream;base64,", "\"uri\":\"motions.bin\",\"x\":\"",
        ErrorCode_InvalidAssetData);
    // A node whose rest pose is not finite.
    CheckRefusedVariant(run, "\"scale\":[1,1,1]", "\"scale\":[1,1e39,1]", ErrorCode_InvalidAssetData);

    // Numbers cgltf's own sums would overflow or trust: checked before cgltf loads or validates.
    // A stride that wraps the view's reach and strides glTF does not allow (refused by the view's check: the accessor's
    // would refuse them too), a stride below an element, a meshopt-compressed view.
    CheckRefusedVariantSaying(run, "\"byteOffset\":12,\"byteLength\":36}",
        "\"byteOffset\":12,\"byteLength\":36,\"byteStride\":2305843009213693952}", "has a stride of");
    CheckRefusedVariantSaying(run, "\"byteOffset\":12,\"byteLength\":36}",
        "\"byteOffset\":12,\"byteLength\":36,\"byteStride\":2}", "has a stride of");
    CheckRefusedVariantSaying(run, "\"byteOffset\":12,\"byteLength\":36}",
        "\"byteOffset\":12,\"byteLength\":36,\"byteStride\":14}", "has a stride of");
    CheckRefusedVariantSaying(run, "\"byteOffset\":12,\"byteLength\":36}",
        "\"byteOffset\":12,\"byteLength\":36,\"byteStride\":4}", "does not fit in its buffer view");
    CheckRefusedVariantSaying(run, "\"byteOffset\":12,\"byteLength\":36}", "\"byteOffset\":12,\"byteLength\":36,"
        "\"extensions\":{\"EXT_meshopt_compression\":{\"buffer\":0,\"byteLength\":36,\"byteStride\":12,\"count\":3,"
        "\"mode\":\"ATTRIBUTES\"}}}", "meshopt-compressed");
    // A view whose offset reaches past its buffer (cgltf caps the text at 2^63 - 1).
    CheckRefusedVariantSaying(run, "\"byteOffset\":12,\"byteLength\":36}",
        "\"byteOffset\":18446744073709551600,\"byteLength\":36}", "does not fit in its buffer");
    // Sparse replacements: a count that would overflow cgltf's sums, more than the accessor's elements, indices of a
    // float type, values past their view, misaligned indices. An accessor without elements, one misaligned.
    CheckRefusedVariant(run, "\"count\":3,\"type\":\"VEC3\"}", "\"count\":3,\"type\":\"VEC3\",\"sparse\":{\"count\":"
        "4611686018427387904,\"indices\":{\"bufferView\":0,\"componentType\":5125},\"values\":{\"bufferView\":1}}}",
        ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"count\":3,\"type\":\"VEC3\"}", "\"count\":3,\"type\":\"VEC3\",\"sparse\":{\"count\":"
        "4,\"indices\":{\"bufferView\":1,\"componentType\":5125},\"values\":{\"bufferView\":1}}}",
        ErrorCode_InvalidAssetData);
    // Four replacements for the three key times, with room for them in both views: only the count rule refuses it.
    // (Their indices, the bytes of the first time, 0.0, all name the first key, and the last of their values, 0.0,
    // leaves the times rising, so nothing else would.)
    CheckRefusedVariant(run, "\"count\":3,\"type\":\"SCALAR\",\"min\":[0],\"max\":[1]}", "\"count\":3,\"type\":"
        "\"SCALAR\",\"min\":[0],\"max\":[1],\"sparse\":{\"count\":4,\"indices\":{\"bufferView\":0,\"componentType\":"
        "5121},\"values\":{\"bufferView\":1,\"byteOffset\":4}}}", ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"count\":3,\"type\":\"VEC3\"}", "\"count\":3,\"type\":\"VEC3\",\"sparse\":{\"count\":"
        "1,\"indices\":{\"bufferView\":0,\"componentType\":5126},\"values\":{\"bufferView\":1}}}",
        ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"count\":3,\"type\":\"VEC3\"}", "\"count\":3,\"type\":\"VEC3\",\"sparse\":{\"count\":"
        "1,\"indices\":{\"bufferView\":0,\"componentType\":5125},\"values\":{\"bufferView\":1,\"byteOffset\":32}}}",
        ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"count\":3,\"type\":\"VEC3\"}", "\"count\":3,\"type\":\"VEC3\",\"sparse\":{\"count\":"
        "1,\"indices\":{\"bufferView\":0,\"byteOffset\":2,\"componentType\":5125},\"values\":{\"bufferView\":1}}}",
        ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"count\":3,\"type\":\"VEC3\"", "\"count\":0,\"type\":\"VEC3\"",
        ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"count\":3,\"type\":\"VEC3\"", "\"byteOffset\":2,\"count\":2,\"type\":\"VEC3\"",
        ErrorCode_InvalidAssetData);
    // Buffers declaring more than the file limit (cgltf would allocate it first), or more than their data holds.
    CheckRefusedVariant(run, "\"byteLength\":48,\"uri\":\"data:", "\"byteLength\":4611686018427387904,\"uri\":\"data:",
        ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"byteLength\":48,\"uri\":\"data:", "\"byteLength\":100000,\"uri\":\"data:",
        ErrorCode_InvalidAssetData);
    CheckRefusedVariant(run, "\"byteLength\":48,\"uri\":\"data:application/octet-stream;base64,",
        "\"byteLength\":314572800,\"uri\":\"big.bin\",\"x\":\"", ErrorCode_InvalidAssetData);

    // One part of one node moved twice by a clip.
    TestGltfNode Nodes[2] =
    {
        { .Name = "root", .Parent = -1, .Rest = TestAnimation_Identity() },
        { .Name = "bone", .Parent = 0, .Rest = TestAnimation_Identity() },
    };
    TestGltfChannel Twice[2] =
    {
        { .Node = 1, .Path = "translation", .Interpolation = "LINEAR", .Times = TimesA, .KeyCount = 3U,
            .Values = TranslationsA, .ValueComponents = 3U },
        { .Node = 1, .Path = "translation", .Interpolation = "STEP", .Times = TimesA, .KeyCount = 3U,
            .Values = TranslationsA, .ValueComponents = 3U },
    };
    TestGltfClip Clip = { .Name = "twice", .Channels = Twice, .ChannelCount = 2U };
    GenericBuffer Text;
    TestGltf_Write(Nodes, 2U, &Clip, 1U, &Text);
    TEST_CHECK_ERROR(run, ReadText(&Text, &Options, &Library), ErrorCode_InvalidAssetData);
    Memory_Free(Text._data);
    TEST_CHECK(run, Library == NULL);
}

/* Writes @p bytes to "<directory>/<name>". */
static bool WriteBytes(TestRun* run, const unsigned char* directory, const char* name, const void* bytes, size_t count)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    bool IsWritten = TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)name, &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_WriteAllBytes(Path._data, bytes, count));
    Memory_Free(Path._data);
    return IsWritten;
}

/* Writes the slide file into "<directory>/<name>" naming its buffer @p uri, and the buffer's bytes, cut to @p byteCount
 * (or with zeros added past them), into "<directory>/<binary>"; then reads the file and expects @p expected. */
static void CheckBufferFile(TestRun* run, const unsigned char* directory, const char* name, const char* uri,
    const char* binary, size_t byteCount, ErrorCode expected)
{
    TestGltfNode Nodes[2] =
    {
        { .Name = "root", .Parent = -1, .Rest = TestAnimation_Identity() },
        { .Name = "bone", .Parent = 0, .Rest = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
    };
    TestGltfChannel Channel = { .Node = 1, .Path = "translation", .Interpolation = "LINEAR", .Times = TimesA,
        .KeyCount = 3U, .Values = TranslationsA, .ValueComponents = 3U };
    TestGltfClip Clip = { .Name = "slide", .Channels = &Channel, .ChannelCount = 1U };
    GenericBuffer Text;
    GenericBuffer Bytes;
    TestGltf_WriteWithBufferFile(Nodes, 2U, &Clip, 1U, uri, &Text, &Bytes);
    while (Bytes._count < byteCount)
    {
        GenericBuffer_AppendByte(&Bytes, 0U);
    }
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    if (WriteBytes(run, directory, name, Text._data, Text._count - 1U)
        && ((binary == NULL) || WriteBytes(run, directory, binary, Bytes._data, byteCount))
        && TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)name, &Path)))
    {
        Error Result = AnimationLibrary_CreateFromFile(Path._data, &Options, &Library);
        if (!TEST_CHECK(run, Result.Code == expected))
        {
            printf("    (\"%s\" naming \"%s\": code %d, %s)\n", name, uri, (int)Result.Code,
                (Result.Message != NULL) ? (const char*)Result.Message : "no message");
        }
        Error_Deconstruct(&Result);
    }
    if ((expected == ErrorCode_Success) && TEST_CHECK(run, Library != NULL))
    {
        Vector3 Value;
        TEST_CHECK(run, SampleSlide(run, Library, 0.5f, &Value) && IsNearVector(Value, (Vector3){ 1.0f, 0.0f, 0.0f }));
    }
    AnimationLibrary_Destroy(Library);
    Memory_Free(Path._data);
    Memory_Free(Text._data);
    Memory_Free(Bytes._data);
}

/* A .gltf's buffer in a file of its own, beside it: read when it holds the declared length (more bytes are left out),
 * refused when shorter; its name checked after its percent escapes are decoded, as cgltf opens it. */
static void BufferFilesBesideTheLibraryAreReadAndChecked(TestRun* run)
{
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Sub;
    GenericBuffer_AllocateVariable(&Sub, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "animation_buffer_files", &Root))
        && TEST_CHECK_SUCCESS(run, Path_Append(Root._data, (const unsigned char*)u8"sub", &Sub))
        && TEST_CHECK_SUCCESS(run, FileSystem_CreateAllDirectories(Sub._data)))
    {
        // The slide's buffer is 48 bytes: its key times and values.
        CheckBufferFile(run, Root._data, "slide.gltf", "slide.bin", "slide.bin", 48U, ErrorCode_Success);
        CheckBufferFile(run, Root._data, "longer.gltf", "longer.bin", "longer.bin", 4096U, ErrorCode_Success);
        CheckBufferFile(run, Root._data, "short.gltf", "short.bin", "short.bin", 8U, ErrorCode_InvalidAssetData);
        // Refused as its buffers load (its data is too short), before anything reads past the 8 bytes.
        GenericBuffer ShortPath;
        GenericBuffer_AllocateVariable(&ShortPath, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
        AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
        AnimationLibrary* Library = NULL;
        if (TEST_CHECK_SUCCESS(run, Path_Append(Root._data, (const unsigned char*)u8"short.gltf", &ShortPath)))
        {
            Error Result = AnimationLibrary_CreateFromFile(ShortPath._data, &Options, &Library);
            TEST_CHECK(run, (Result.Code == ErrorCode_InvalidAssetData) && (Result.Message != NULL)
                && (strstr((const char*)Result.Message, "buffers cannot be loaded") != NULL));
            Error_Deconstruct(&Result);
        }
        AnimationLibrary_Destroy(Library);
        Memory_Free(ShortPath._data);
        CheckBufferFile(run, Root._data, "missing.gltf", "nowhere.bin", NULL, 0U, ErrorCode_FileNotFound);
        CheckBufferFile(run, Root._data, "spaced.gltf", "my%20slide.bin", "my slide.bin", 48U, ErrorCode_Success);
        // "%2e%2e" is "..", "%2f" a slash, "%00" a NUL that would cut the name short. First the file the escaped names
        // below would reach from sub/, a good buffer in itself.
        CheckBufferFile(run, Root._data, "secret.bin.gltf", "secret.bin", "secret.bin", 48U, ErrorCode_Success);
        CheckBufferFile(run, Sub._data, "escaped.gltf", "%2e%2e/secret.bin", NULL, 0U, ErrorCode_InvalidAssetData);
        CheckBufferFile(run, Sub._data, "escaped_slash.gltf", "..%2fsecret.bin", NULL, 0U, ErrorCode_InvalidAssetData);
        CheckBufferFile(run, Root._data, "nul.gltf", "slide.bin%00.txt", NULL, 0U, ErrorCode_InvalidAssetData);
    }
    Memory_Free(Root._data);
    Memory_Free(Sub._data);
}

/* A .glb's binary chunk starts where its JSON chunk's length puts it: a length that is not padded to four would make
 * every float read misaligned, so the reader refuses it; the padded file reads. */
static void AGlbsChunksMustBePadded(TestRun* run)
{
    TestGltfNode Nodes[2] =
    {
        { .Name = "root", .Parent = -1, .Rest = TestAnimation_Identity() },
        { .Name = "bone", .Parent = 0, .Rest = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
    };
    TestGltfChannel Channel = { .Node = 1, .Path = "translation", .Interpolation = "LINEAR", .Times = TimesA,
        .KeyCount = 3U, .Values = TranslationsA, .ValueComponents = 3U };
    TestGltfClip Clip = { .Name = "slide", .Channels = &Channel, .ChannelCount = 1U };
    for (int Padding = 0; Padding < 2; Padding++)
    {
        GenericBuffer Text;
        GenericBuffer Bytes;
        TestGltf_WriteWithBufferFile(Nodes, 2U, &Clip, 1U, "none", &Text, &Bytes);
        TEST_CHECK(run, TestGltf_Replace(&Text, ",\"uri\":\"none\"", ""));
        size_t JsonLength = Text._count - 1U;
        size_t PaddedLength = (Padding == 0) ? ((JsonLength + 3U) & ~(size_t)3U) : (((JsonLength + 3U) & ~(size_t)3U) + 1U);
        uint32_t Header[5] =
        {
            0x46546C67U, 2U, (uint32_t)(12U + 8U + PaddedLength + 8U + Bytes._count), (uint32_t)PaddedLength, 0x4E4F534AU
        };
        GenericBuffer Glb;
        GenericBuffer_AllocateVariable(&Glb, 256U, sizeof(unsigned char));
        GenericBuffer_AddLastRange(&Glb, Header, sizeof(Header));
        GenericBuffer_AddLastRange(&Glb, Text._data, JsonLength);
        while (Glb._count < (20U + PaddedLength))
        {
            GenericBuffer_AppendByte(&Glb, (unsigned char)' ');
        }
        uint32_t BinHeader[2] = { (uint32_t)Bytes._count, 0x004E4942U };
        GenericBuffer_AddLastRange(&Glb, BinHeader, sizeof(BinHeader));
        GenericBuffer_AddLastRange(&Glb, Bytes._data, Bytes._count);
        AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
        AnimationLibrary* Library = NULL;
        Error Result = AnimationLibrary_CreateFromGltf(Glb._data, Glb._count, &Options, &Library);
        TEST_CHECK(run, Result.Code == ((Padding == 0) ? ErrorCode_Success : ErrorCode_InvalidAssetData));
        Error_Deconstruct(&Result);
        AnimationLibrary_Destroy(Library);
        Memory_Free(Glb._data);
        Memory_Free(Text._data);
        Memory_Free(Bytes._data);
    }
}

/* A sparse accessor within the rules reads with its replacements (Blender writes shape keys this way): the slide's
 * first translation key replaced by its third, (4, 0, 0). Its index comes from the key times' first float, 0.0, read
 * as an unsigned 32-bit integer: 0. */
static void ASparseAccessorReadsWithItsReplacements(TestRun* run)
{
    GenericBuffer Text;
    WriteSlideFile("LINEAR", TimesA, 3U, TranslationsA, &Text);
    bool IsReplaced = TestGltf_Replace(&Text, "\"count\":3,\"type\":\"VEC3\"}", "\"count\":3,\"type\":\"VEC3\","
        "\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":0,\"componentType\":5125},\"values\":{\"bufferView\":1,"
        "\"byteOffset\":24}}}");
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    if (TEST_CHECK(run, IsReplaced) && TEST_CHECK_SUCCESS(run, ReadText(&Text, &Options, &Library)))
    {
        Vector3 First;
        Vector3 Second;
        TEST_CHECK(run, SampleSlide(run, Library, 0.0f, &First) && IsNearVector(First, (Vector3){ 4.0f, 0.0f, 0.0f }));
        TEST_CHECK(run, SampleSlide(run, Library, 0.5f, &Second) && IsNearVector(Second, (Vector3){ 1.0f, 0.0f, 0.0f }));
    }
    AnimationLibrary_Destroy(Library);
    Memory_Free(Text._data);
}

static void TooManyAnimatedNodesAreRefused(TestRun* run)
{
    // One more node than a library may have, each moved by a one-key clip channel.
    enum { NODE_COUNT = 257 };
    static TestGltfNode Nodes[NODE_COUNT];
    static TestGltfChannel Channels[NODE_COUNT];
    static char Names[NODE_COUNT][16];
    static const float OneTime[] = { 0.0f };
    static const float OneValue[] = { 0.0f, 0.0f, 0.0f };
    for (int Index = 0; Index < NODE_COUNT; Index++)
    {
        snprintf(Names[Index], sizeof(Names[Index]), "node_%d", Index);
        Nodes[Index] = (TestGltfNode){ .Name = Names[Index], .Parent = -1, .Rest = TestAnimation_Identity() };
        Channels[Index] = (TestGltfChannel){ .Node = Index, .Path = "translation", .Interpolation = "LINEAR",
            .Times = OneTime, .KeyCount = 1U, .Values = OneValue, .ValueComponents = 3U };
    }
    AnimationLibraryReadOptions Options = MakeOptions(NULL, 0U);
    AnimationLibrary* Library = NULL;
    GenericBuffer Text;
    TestGltfClip Clip = { .Name = "many", .Channels = Channels, .ChannelCount = NODE_COUNT };
    TestGltf_Write(Nodes, NODE_COUNT, &Clip, 1U, &Text);
    TEST_CHECK_ERROR(run, ReadText(&Text, &Options, &Library), ErrorCode_BufferTooLarge);
    Memory_Free(Text._data);
    // The limit itself reads.
    Clip.ChannelCount = NODE_COUNT - 1;
    TestGltf_Write(Nodes, NODE_COUNT - 1, &Clip, 1U, &Text);
    if (TEST_CHECK_SUCCESS(run, ReadText(&Text, &Options, &Library)))
    {
        TEST_CHECK(run, AnimationLibrary_GetBoneCount(Library) == (uint32_t)(NODE_COUNT - 1));
        AnimationLibrary_Destroy(Library);
    }
    Memory_Free(Text._data);
}


// Static functions: the asset type.
static bool BuildDefinition(AssetManager* manager, AssetTypeID type, const char* json, AssetDefinition** outDefinition)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    Error Result = AssetManager_BuildDefinition(manager, type, &Bytes, (const unsigned char*)u8"test", outDefinition);
    Memory_Free(Bytes._data);
    bool IsBuilt = Result.Code == ErrorCode_Success;
    Error_Deconstruct(&Result);
    return IsBuilt;
}

static void TheDefinitionChecksItsClipSettings(TestRun* run)
{
    AssetManager* Manager = NULL;
    StandardAssetTypes Types;
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &Manager))
        || !TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(Manager, &Types, &Pool)))
    {
        return;
    }
    AssetDefinition* Definition = NULL;
    TEST_CHECK(run, BuildDefinition(Manager, Types.AnimationLibrary,
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"name\": \"stroll\", \"source\": \"Walk Cycle\", "
        "\"is_looped\": true, \"speed\": 2, \"distance\": 0.9}, {\"name\": \"run\"}]}", &Definition));
    if (Definition != NULL)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(Manager, Definition));
    }
    static const char* const Refused[] =
    {
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clip\": []}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": {}}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"name\": \"walk\", \"loop\": true}]}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"source\": \"walk\"}]}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"name\": \"Walk\"}]}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"name\": \"walk\", \"speed\": 0}]}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"name\": \"walk\", \"speed\": 101}]}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"name\": \"walk\", \"distance\": 0}]}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"name\": \"walk\", \"distance\": 1001}]}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"name\": \"walk\", \"distance\": \"far\"}]}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"name\": \"walk\", \"source\": \"\"}]}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"name\": \"walk\"}, {\"name\": \"walk\", "
            "\"source\": \"other\"}]}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"clips\": [{\"name\": \"walk\"}, {\"name\": \"stroll\", "
            "\"source\": \"walk\"}]}",
        "{\"name\": \"lib\", \"location\": \"a/b\", \"format\": \"fbx\"}",
        "{\"name\": \"lib\", \"location\": {\"type\": \"reference\", \"value\": \"x\"}}",
    };
    for (size_t Index = 0U; Index < sizeof(Refused) / sizeof(Refused[0]); Index++)
    {
        AssetDefinition* Bad = NULL;
        bool IsBuilt = BuildDefinition(Manager, Types.AnimationLibrary, Refused[Index], &Bad);
        if (!TEST_CHECK(run, !IsBuilt))
        {
            printf("    (accepted: %s)\n", Refused[Index]);
        }
    }
    TEST_CHECK_SUCCESS(run, AssetManager_Deconstruct(Manager));
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}

static void TheLibraryAssetLoadsItsFile(TestRun* run)
{
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Source;
    GenericBuffer_AllocateVariable(&Source, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer Target;
    GenericBuffer_AllocateVariable(&Target, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    AssetManager* Manager = NULL;
    StandardAssetTypes Types;
    JSONObjectPool* Pool = NULL;
    bool IsReady = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "animation_library_asset", &Root))
        && TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Root._data, "animation_libraries/humans/motions.json",
            "{\"name\": \"human_motions\", \"location\": \"humans/motions\", \"clips\": [{\"name\": \"walk\", "
            "\"is_looped\": true, \"distance\": 1.1}, {\"name\": \"sprint\", \"source\": \"run\", \"is_looped\": true, "
            "\"speed\": 1.25}]}"))
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, MOTIONS_PATH, &Source))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Source._data, &Bytes))
        && TEST_CHECK(run, GenericBuffer_AppendString(&Target, Root._data))
        && TEST_CHECK(run, GenericBuffer_AppendString(&Target, (const unsigned char*)u8"/animation_libraries/humans/motions.glb"))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&Target))
        && TEST_CHECK_SUCCESS(run, FileSystem_WriteAllBytes(Target._data, Bytes._data, Bytes._count))
        && TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(Manager, &Types, &Pool))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Manager, Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Manager, NULL));
    AssetUserID User = ASSET_USER_ID_INVALID;
    AnimationLibrary* Library = NULL;
    if (IsReady && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Manager, &User))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadAnimationLibrary(Manager, (const unsigned char*)u8"human_motions",
            User, &Library)))
    {
        uint32_t Walk = 0U;
        uint32_t Sprint = 0U;
        TEST_CHECK(run, AnimationLibrary_GetClipCount(Library) == PLACEHOLDER_CLIP_COUNT);
        TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"walk", &Walk)
            && AnimationClip_IsLooped(AnimationLibrary_GetClip(Library, Walk)));
        TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"sprint", &Sprint)
            && (AnimationClip_GetSpeed(AnimationLibrary_GetClip(Library, Sprint)) == 1.25f));
        // The distance a clip covers per cycle, when the definition says (a graph's match_speed plays a gait by it).
        float Distance = 0.0f;
        TEST_CHECK(run, AnimationClip_TryGetDistance(AnimationLibrary_GetClip(Library, Walk), &Distance)
            && (Distance == 1.1f) && !AnimationClip_TryGetDistance(AnimationLibrary_GetClip(Library, Sprint), NULL));
        TEST_CHECK(run, !AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"run", NULL));
    }
    if (Manager != NULL)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_Deconstruct(Manager));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Root._data);
    Memory_Free(Source._data);
    Memory_Free(Bytes._data);
    Memory_Free(Target._data);
}


/* The game's own library definition (compile/out/asset/animation_libraries): it loads its placeholder file through the
 * asset manager with the definition's loops, as the test world's figures play it, and the metres the gaits cover per
 * cycle: the placeholder generator's strides (twice the half stride over the stance's share of the cycle). */
static void TheShippedLibraryDefinitionLoadsItsClips(TestRun* run)
{
    static const char* const Looped[] = { "idle", "walk", "run", "crouch_idle", "crouch_walk", "fall", "sit_idle", "hold_r" };
    static const char* const Once[] = { "jump", "land", "wave", "sit_down", "stand_up", "pick_up", "put_down", "open_door" };
    static const char* const Gaits[] = { "walk", "run", "crouch_walk" };
    static const float Distances[] = { 2.0f * 0.33f / 0.6f, 2.0f * 0.30f / 0.38f, 2.0f * 0.22f / 0.6f };
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    AssetManager* Manager = NULL;
    StandardAssetTypes Types;
    JSONObjectPool* Pool = NULL;
    size_t FailedCount = 1U;
    AssetUserID User = ASSET_USER_ID_INVALID;
    AnimationLibrary* Library = NULL;
    if (TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/asset", &Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(Manager, &Types, &Pool))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Manager, Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Manager, &FailedCount))
        && TEST_CHECK(run, FailedCount == 0U)
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Manager, &User))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadAnimationLibrary(Manager,
            (const unsigned char*)u8"test_humanoid_motions", User, &Library)))
    {
        TEST_CHECK(run, AnimationLibrary_GetClipCount(Library) == PLACEHOLDER_CLIP_COUNT);
        for (size_t Index = 0U; Index < (sizeof(Looped) / sizeof(Looped[0])); Index++)
        {
            uint32_t Clip = 0U;
            TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)Looped[Index], &Clip)
                && AnimationClip_IsLooped(AnimationLibrary_GetClip(Library, Clip)));
        }
        for (size_t Index = 0U; Index < (sizeof(Once) / sizeof(Once[0])); Index++)
        {
            uint32_t Clip = 0U;
            TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)Once[Index], &Clip)
                && !AnimationClip_IsLooped(AnimationLibrary_GetClip(Library, Clip))
                && !AnimationClip_TryGetDistance(AnimationLibrary_GetClip(Library, Clip), NULL));
        }
        for (size_t Index = 0U; Index < (sizeof(Gaits) / sizeof(Gaits[0])); Index++)
        {
            uint32_t Clip = 0U;
            float Distance = 0.0f;
            TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)Gaits[Index], &Clip)
                && AnimationClip_TryGetDistance(AnimationLibrary_GetClip(Library, Clip), &Distance)
                && (fabsf(Distance - Distances[Index]) < 1e-3f));
        }
    }
    if (Manager != NULL)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_Deconstruct(Manager));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Root._data);
}


// Public functions.
void AnimationLibraryTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_motions_library_reads_the_placeholder_clips", TheMotionsLibraryReadsThePlaceholderClips },
        { "a_model_file_with_its_skin_reads_as_a_library", AModelFileWithItsSkinReadsAsALibrary },
        { "clip_settings_rename_loop_and_speed_clips", ClipSettingsRenameLoopAndSpeedClips },
        { "settings_that_do_not_fit_the_file_are_refused", SettingsThatDoNotFitTheFileAreRefused },
        { "sampling_holds_the_ends_and_reads_the_last_key_exactly", SamplingHoldsTheEndsAndReadsTheLastKeyExactly },
        { "step_and_cubic_spline_tracks_sample_as_gltf_says", StepAndCubicSplineTracksSampleAsGltfSays },
        { "rotations_take_the_shorter_arc_and_come_normalized", RotationsTakeTheShorterArcAndComeNormalized },
        { "a_node_matrix_reads_as_its_transform", ANodeMatrixReadsAsItsTransform },
        { "a_parent_listed_after_its_child_is_placed_first", AParentListedAfterItsChildIsPlacedFirst },
        { "clips_without_a_usable_name_or_channel_are_skipped", ClipsWithoutAUsableNameOrChannelAreSkipped },
        { "hostile_files_are_refused", HostileFilesAreRefused },
        { "buffer_files_beside_the_library_are_read_and_checked", BufferFilesBesideTheLibraryAreReadAndChecked },
        { "a_glbs_chunks_must_be_padded", AGlbsChunksMustBePadded },
        { "a_sparse_accessor_reads_with_its_replacements", ASparseAccessorReadsWithItsReplacements },
        { "too_many_animated_nodes_are_refused", TooManyAnimatedNodesAreRefused },
        { "the_definition_checks_its_clip_settings", TheDefinitionChecksItsClipSettings },
        { "the_library_asset_loads_its_file", TheLibraryAssetLoadsItsFile },
        { "the_shipped_library_definition_loads_its_clips", TheShippedLibraryDefinitionLoadsItsClips },
    };
    TestRun_RunSuite(run, "AnimationLibrary", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
