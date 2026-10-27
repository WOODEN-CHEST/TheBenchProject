#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "AnimationLibrary.h"
#include "AssetManager.h"
#include "GltfChecks.h"
#include "SkeletalAnimation.h"
#include "raylib/cgltf.h"
#include "raylib/raymath.h"
#include "wr/WRFileSystem.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * Reads an animation library from glTF (AnimationLibrary.h) with cgltf, the glTF parser raylib bundles (its
 * implementation is compiled into libraylib.a; include/raylib/cgltf.h is the same version's header). Every file is
 * hostile: source/GltfChecks.h checks the file's structure before cgltf loads its buffers and validates it (the
 * terrain's import shares those checks), and this reader then checks the rest (accessor shapes, key times and values,
 * limits) before it trusts a number.
 *
 * Two passes: the plan (which nodes are bones, which clips are kept, how many keys and floats, where each shared key
 * time accessor goes) and then the fill, into one block sized by the plan: the library, its clips, their tracks, the
 * bones, every float (key times first, each time accessor once, then the values) and the clip names. cgltf's memory
 * goes through WRMemory, so a leak shows in the tests' allocation counts.
 */


// Macros.
/** Room for one message's text and for a name from the file in printable form. */
#define MESSAGE_CAPACITY ((size_t)512)
#define NAME_PRINT_CAPACITY ((size_t)129)
/** A node that is not a bone. */
#define NOT_A_BONE UINT32_MAX
/** An accessor whose key times are not (yet) placed. */
#define NO_TIMES SIZE_MAX
/** The parts of a bone a channel moves, and how many there are. */
#define PART_TRANSLATION 0U
#define PART_ROTATION 1U
#define PART_SCALE 2U
#define PART_COUNT 3U
/** Floats per translation or scale value, and per rotation. */
#define VECTOR3_FLOATS 3U
#define ROTATION_FLOATS 4U
/** A cubic spline key holds three values: in-tangent, value, out-tangent. */
#define CUBIC_VALUES_PER_KEY 3U
/** Rotations shorter than this have no direction. */
#define MIN_ROTATION_LENGTH 1e-12f


// Types.
/* One clip the plan keeps: its animation in the file and its settings. */
typedef struct ClipPlanStruct
{
    const cgltf_animation* Animation;
    const unsigned char* Name; // borrowed: the settings' name or the file's
    bool IsLooped;
    float Speed;
    bool HasDistance;
    float Distance;
} ClipPlan;

/* Everything one read holds. The arrays are WRMemory allocations freed by DestroyReader. */
typedef struct ReaderStruct
{
    const AnimationLibraryReadOptions* Options;
    char Description[NAME_PRINT_CAPACITY + 32U];
    cgltf_data* Data;
    uint32_t* BoneOfNode;        // per node: its bone's index, or NOT_A_BONE
    uint32_t BoneCount;
    ClipPlan* Clips;             // one per kept clip (room for every animation)
    uint32_t ClipCount;
    bool* IsSettingUsed;         // per clip setting: whether a clip of the file took it
    size_t* TimesOfAccessor;     // per accessor: where its key times go among the floats, or NO_TIMES
    bool* AreTimesFilled;        // per accessor: whether its key times are in the block (and checked)
    size_t TimeFloatCount;
    size_t ValueFloatCount;
    size_t KeyCount;
    size_t NameByteCount;
    uint8_t* SeenParts;          // per bone and part: whether the animation being read moves it (cleared per clip)
} Reader;


// Static functions: messages.
/* An error with "<description>: " before the formatted text. */
static Error Fail(const Reader* reader, ErrorCode code, const char* format, ...)
{
    char Text[MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        Text[0] = '\0';
    }
    return Error_Construct4(code, "%s: %s", reader->Description, Text);
}

static void Warn(const Reader* reader, const char* format, ...)
{
    if (reader->Options->Logger == NULL)
    {
        return;
    }
    char Text[MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        Text[0] = '\0';
    }
    Error LogResult = Logger_LogWarningFormatted(reader->Options->Logger, (const unsigned char*)u8"%s: %s",
        reader->Description, Text);
    Error_Deconstruct(&LogResult);
}


// Static functions: reading.
/* Normalizes a rotation's four finite floats in place, its length reckoned in double: a float's square may overflow
 * (a length of 3e19 squares past FLT_MAX, and dividing by the infinite length would leave no rotation at all), its
 * double cannot. Returns false for a rotation without a direction. */
static bool NormalizeRotation(float* rotation)
{
    double LengthSquared = 0.0;
    for (size_t Component = 0U; Component < ROTATION_FLOATS; Component++)
    {
        LengthSquared += (double)rotation[Component] * (double)rotation[Component];
    }
    double Length = sqrt(LengthSquared);
    if (!isfinite(Length) || !(Length > (double)MIN_ROTATION_LENGTH))
    {
        return false;
    }
    for (size_t Component = 0U; Component < ROTATION_FLOATS; Component++)
    {
        rotation[Component] = (float)((double)rotation[Component] / Length);
    }
    return true;
}

/* Parses, loads the buffers and validates (source/GltfChecks.h: the file's structure is checked before cgltf trusts a
 * number). reader->Data receives the parsed data whenever the parse succeeded; DestroyReader frees it. */
static Error ParseFile(Reader* reader, const void* data, size_t byteCount)
{
    GltfReadContext Context = { .Description = reader->Description, .MaxBytes = ANIMATION_LIBRARY_MAX_FILE_BYTES,
        .FilePath = reader->Options->FilePath };
    return GltfChecks_Parse(&Context, data, byteCount, &reader->Data);
}

// Static functions: the plan.
static size_t GetNodeIndex(const Reader* reader, const cgltf_node* node)
{
    return (size_t)(node - reader->Data->nodes);
}

static size_t GetAccessorIndex(const Reader* reader, const cgltf_accessor* accessor)
{
    return (size_t)(accessor - reader->Data->accessors);
}

/* Which part of a bone a channel moves, or PART_COUNT for none this reader reads (morph target weights). */
static uint32_t GetPart(cgltf_animation_path_type path)
{
    switch (path)
    {
        case cgltf_animation_path_type_translation: return PART_TRANSLATION;
        case cgltf_animation_path_type_rotation: return PART_ROTATION;
        case cgltf_animation_path_type_scale: return PART_SCALE;
        case cgltf_animation_path_type_invalid:
        case cgltf_animation_path_type_weights:
        case cgltf_animation_path_type_max_enum:
            return PART_COUNT;
    }
    return PART_COUNT;
}

/* The bones: every node a translation, rotation or scale channel animates, and their ancestors, in node order. */
static Error MarkBones(Reader* reader)
{
    cgltf_data* Data = reader->Data;
    size_t NodeCount = Data->nodes_count;
    reader->BoneOfNode = Memory_Allocate(((NodeCount > 0U) ? NodeCount : 1U) * sizeof(uint32_t));
    for (size_t Index = 0U; Index < NodeCount; Index++)
    {
        reader->BoneOfNode[Index] = NOT_A_BONE;
    }
    for (cgltf_size Animation = 0U; Animation < Data->animations_count; Animation++)
    {
        const cgltf_animation* Clip = &Data->animations[Animation];
        for (cgltf_size Channel = 0U; Channel < Clip->channels_count; Channel++)
        {
            const cgltf_animation_channel* Source = &Clip->channels[Channel];
            if ((Source->target_node == NULL) || (GetPart(Source->target_path) == PART_COUNT))
            {
                continue;
            }
            // Marked as 0 for now; cgltf_validate refused cycles, so the walk up ends.
            for (const cgltf_node* Node = Source->target_node; Node != NULL; Node = Node->parent)
            {
                size_t Index = GetNodeIndex(reader, Node);
                if (reader->BoneOfNode[Index] != NOT_A_BONE)
                {
                    break;
                }
                reader->BoneOfNode[Index] = 0U;
            }
        }
    }
    uint32_t Count = 0U;
    for (size_t Index = 0U; Index < NodeCount; Index++)
    {
        if (reader->BoneOfNode[Index] == NOT_A_BONE)
        {
            continue;
        }
        if ((size_t)Count >= ANIMATION_LIBRARY_MAX_BONES)
        {
            return Fail(reader, ErrorCode_BufferTooLarge, "its clips animate more than %" PRIu64 " nodes.",
                (uint64_t)ANIMATION_LIBRARY_MAX_BONES);
        }
        reader->BoneOfNode[Index] = Count;
        Count++;
    }
    reader->BoneCount = Count;
    return Error_CreateSuccess();
}

/* Whether a sampler's accessors have the shapes a channel of @p part needs (float key times; float values, or
 * normalized integers for a rotation). */
static Error CheckSampler(const Reader* reader, const cgltf_animation_sampler* sampler, uint32_t part,
    const char* clipName)
{
    const cgltf_accessor* Input = sampler->input;
    const cgltf_accessor* Output = sampler->output;
    if ((Input == NULL) || (Output == NULL))
    {
        return Fail(reader, ErrorCode_InvalidAssetData, "clip \"%s\" has a sampler without key times or values.",
            clipName);
    }
    if ((Input->type != cgltf_type_scalar) || (Input->component_type != cgltf_component_type_r_32f)
        || (Input->count == 0U))
    {
        return Fail(reader, ErrorCode_InvalidAssetData, "clip \"%s\": key times must be at least one float each.",
            clipName);
    }
    cgltf_type Expected = (part == PART_ROTATION) ? cgltf_type_vec4 : cgltf_type_vec3;
    bool IsFloat = Output->component_type == cgltf_component_type_r_32f;
    bool IsNormalizedInteger = Output->normalized && ((Output->component_type == cgltf_component_type_r_8)
        || (Output->component_type == cgltf_component_type_r_8u) || (Output->component_type == cgltf_component_type_r_16)
        || (Output->component_type == cgltf_component_type_r_16u));
    if ((Output->type != Expected) || !(IsFloat || ((part == PART_ROTATION) && IsNormalizedInteger)))
    {
        return Fail(reader, ErrorCode_InvalidAssetData, "clip \"%s\": a %s channel's values must be %s.", clipName,
            (part == PART_ROTATION) ? "rotation" : ((part == PART_SCALE) ? "scale" : "translation"),
            (part == PART_ROTATION) ? "four floats (or normalized integers) each" : "three floats each");
    }
    size_t PerKey = (sampler->interpolation == cgltf_interpolation_type_cubic_spline) ? CUBIC_VALUES_PER_KEY : 1U;
    if ((sampler->interpolation == cgltf_interpolation_type_max_enum) || (Output->count != (Input->count * PerKey)))
    {
        return Fail(reader, ErrorCode_InvalidAssetData, "clip \"%s\" has a sampler whose values do not fit its key "
            "times.", clipName);
    }
    return Error_CreateSuccess();
}

/* The settings a clip takes (by its file name), or NULL. */
static const AnimationClipSettings* FindSettings(Reader* reader, const char* fileName, size_t* outIndex)
{
    if (fileName == NULL)
    {
        return NULL;
    }
    size_t Length = strlen(fileName);
    for (size_t Index = 0U; Index < reader->Options->ClipCount; Index++)
    {
        const AnimationClipSettings* Settings = &reader->Options->Clips[Index];
        if ((StringUTF8_GetByteLength(Settings->Source) == Length) && Memory_IsEqual(Settings->Source, fileName, Length))
        {
            *outIndex = Index;
            return Settings;
        }
    }
    return NULL;
}

static bool IsValidClipName(const unsigned char* name)
{
    return AssetManager_IsValidAssetName(name) && (StringUTF8_GetByteLength(name) <= ANIMATION_CLIP_NAME_MAX_LENGTH);
}

/* Counts one channel's keys and floats into the plan (its key times once per accessor). */
static Error PlanChannel(Reader* reader, const cgltf_animation_channel* channel, uint32_t part, const char* clipName)
{
    const cgltf_animation_sampler* Sampler = channel->sampler;
    Error Result = CheckSampler(reader, Sampler, part, clipName);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint32_t Bone = reader->BoneOfNode[GetNodeIndex(reader, channel->target_node)];
    uint8_t* Seen = &reader->SeenParts[((size_t)Bone * PART_COUNT) + part];
    if (*Seen != 0U)
    {
        return Fail(reader, ErrorCode_InvalidAssetData, "clip \"%s\" moves one part of a node twice.", clipName);
    }
    *Seen = 1U;

    size_t KeyCount = Sampler->input->count;
    if ((KeyCount > ANIMATION_LIBRARY_MAX_KEYS) || ((reader->KeyCount + KeyCount) > ANIMATION_LIBRARY_MAX_KEYS))
    {
        return Fail(reader, ErrorCode_BufferTooLarge, "its clips hold more than %" PRIu64 " keys.",
            (uint64_t)ANIMATION_LIBRARY_MAX_KEYS);
    }
    reader->KeyCount += KeyCount;
    size_t Accessor = GetAccessorIndex(reader, Sampler->input);
    if (reader->TimesOfAccessor[Accessor] == NO_TIMES)
    {
        reader->TimesOfAccessor[Accessor] = reader->TimeFloatCount;
        reader->TimeFloatCount += KeyCount;
    }
    size_t Floats = (part == PART_ROTATION) ? ROTATION_FLOATS : VECTOR3_FLOATS;
    reader->ValueFloatCount += Sampler->output->count * Floats;
    return Error_CreateSuccess();
}

/* Plans one animation of the file: its name and settings, and its channels' keys; skips it (with a warning) when it has
 * no usable name or no channel this reader reads. */
static Error PlanClip(Reader* reader, const cgltf_animation* animation)
{
    char Name[NAME_PRINT_CAPACITY];
    GltfChecks_Printable(animation->name, Name, sizeof(Name));
    size_t SettingIndex = 0U;
    const AnimationClipSettings* Settings = FindSettings(reader, animation->name, &SettingIndex);
    ClipPlan Plan = { .Animation = animation, .Name = NULL, .IsLooped = false, .Speed = ANIMATION_CLIP_DEFAULT_SPEED,
        .HasDistance = false, .Distance = 0.0f };
    if (Settings != NULL)
    {
        reader->IsSettingUsed[SettingIndex] = true;
        Plan.Name = Settings->Name;
        Plan.IsLooped = Settings->IsLooped;
        Plan.Speed = Settings->Speed;
        Plan.HasDistance = Settings->HasDistance;
        Plan.Distance = Settings->HasDistance ? Settings->Distance : 0.0f;
    }
    else if ((animation->name != NULL) && IsValidClipName((const unsigned char*)animation->name))
    {
        Plan.Name = (const unsigned char*)animation->name;
    }
    else
    {
        Warn(reader, "clip \"%s\" is skipped: its name is not a valid clip name ([a-z][a-z0-9_.-]*, at most %" PRIu64
            " bytes) and the definition gives it none (\"clips\": [{ \"source\": ..., \"name\": ... }]).", Name,
            (uint64_t)ANIMATION_CLIP_NAME_MAX_LENGTH);
        return Error_CreateSuccess();
    }

    Memory_Zero(reader->SeenParts, ((size_t)reader->BoneCount * PART_COUNT) + 1U);
    bool HasChannel = false;
    bool HasIgnoredChannel = false;
    for (cgltf_size Channel = 0U; Channel < animation->channels_count; Channel++)
    {
        const cgltf_animation_channel* Source = &animation->channels[Channel];
        uint32_t Part = GetPart(Source->target_path);
        if ((Source->target_node == NULL) || (Part == PART_COUNT))
        {
            HasIgnoredChannel = HasIgnoredChannel || (Source->target_path == cgltf_animation_path_type_weights);
            continue;
        }
        Error Result = PlanChannel(reader, Source, Part, Name);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        HasChannel = true;
    }
    if (HasIgnoredChannel)
    {
        Warn(reader, "clip \"%s\" animates morph target weights, which are not played.", Name);
    }
    if (!HasChannel)
    {
        Warn(reader, "clip \"%s\" is skipped: it moves no node.", Name);
        return Error_CreateSuccess();
    }
    if ((size_t)reader->ClipCount >= ANIMATION_LIBRARY_MAX_CLIPS)
    {
        return Fail(reader, ErrorCode_BufferTooLarge, "it has more than %" PRIu64 " clips.",
            (uint64_t)ANIMATION_LIBRARY_MAX_CLIPS);
    }
    reader->Clips[reader->ClipCount] = Plan;
    reader->ClipCount++;
    reader->NameByteCount += StringUTF8_GetByteLength(Plan.Name) + 1U;
    return Error_CreateSuccess();
}

/* Plans every clip, then checks that every setting found its clip and that no two clips share a name. */
static Error PlanClips(Reader* reader)
{
    cgltf_data* Data = reader->Data;
    size_t AccessorCount = (Data->accessors_count > 0U) ? Data->accessors_count : 1U;
    reader->TimesOfAccessor = Memory_Allocate(AccessorCount * sizeof(size_t));
    reader->AreTimesFilled = Memory_Allocate(AccessorCount * sizeof(bool));
    for (size_t Index = 0U; Index < AccessorCount; Index++)
    {
        reader->TimesOfAccessor[Index] = NO_TIMES;
        reader->AreTimesFilled[Index] = false;
    }
    reader->Clips = Memory_Allocate(((Data->animations_count > 0U) ? Data->animations_count : 1U) * sizeof(ClipPlan));
    size_t SettingCount = reader->Options->ClipCount;
    reader->IsSettingUsed = Memory_Allocate((SettingCount > 0U) ? SettingCount : 1U);
    Memory_Zero(reader->IsSettingUsed, (SettingCount > 0U) ? SettingCount : 1U);
    reader->SeenParts = Memory_Allocate(((size_t)reader->BoneCount * PART_COUNT) + 1U);

    for (cgltf_size Animation = 0U; Animation < Data->animations_count; Animation++)
    {
        Error Result = PlanClip(reader, &Data->animations[Animation]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    for (size_t Index = 0U; Index < SettingCount; Index++)
    {
        if (!reader->IsSettingUsed[Index])
        {
            char Name[NAME_PRINT_CAPACITY];
            return Fail(reader, ErrorCode_InvalidAssetDefinition, "the definition names clip \"%s\", which the file "
                "does not have.", GltfChecks_Printable((const char*)reader->Options->Clips[Index].Source, Name,
                sizeof(Name)));
        }
    }
    for (uint32_t First = 0U; First < reader->ClipCount; First++)
    {
        for (uint32_t Second = First + 1U; Second < reader->ClipCount; Second++)
        {
            const unsigned char* A = reader->Clips[First].Name;
            const unsigned char* B = reader->Clips[Second].Name;
            size_t Length = StringUTF8_GetByteLength(A);
            if ((Length == StringUTF8_GetByteLength(B)) && Memory_IsEqual(A, B, Length))
            {
                char Name[NAME_PRINT_CAPACITY];
                return Fail(reader, ErrorCode_InvalidAssetData, "two clips are called \"%s\"; give one another name "
                    "in the definition.", GltfChecks_Printable((const char*)A, Name, sizeof(Name)));
            }
        }
    }
    return Error_CreateSuccess();
}


// Static functions: the fill.
/* A node's rest pose: its translation, rotation and scale, or its matrix taken apart. */
static Error ReadRest(const Reader* reader, const cgltf_node* node, Transform* outRest)
{
    Transform Rest = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };
    if (node->has_matrix)
    {
        const cgltf_float* M = node->matrix;
        Matrix Local = MatrixIdentity();
        Local.m0 = M[0];
        Local.m1 = M[1];
        Local.m2 = M[2];
        Local.m4 = M[4];
        Local.m5 = M[5];
        Local.m6 = M[6];
        Local.m8 = M[8];
        Local.m9 = M[9];
        Local.m10 = M[10];
        Local.m12 = M[12];
        Local.m13 = M[13];
        Local.m14 = M[14];
        Rest = SkeletalAnimation_DecomposeMatrix(Local);
    }
    else
    {
        if (node->has_translation)
        {
            Rest.translation = (Vector3){ node->translation[0], node->translation[1], node->translation[2] };
        }
        if (node->has_rotation)
        {
            Rest.rotation = (Quaternion){ node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3] };
        }
        if (node->has_scale)
        {
            Rest.scale = (Vector3){ node->scale[0], node->scale[1], node->scale[2] };
        }
    }
    float Rotation[ROTATION_FLOATS] = { Rest.rotation.x, Rest.rotation.y, Rest.rotation.z, Rest.rotation.w };
    bool IsFinite = isfinite(Rest.translation.x) && isfinite(Rest.translation.y) && isfinite(Rest.translation.z)
        && isfinite(Rest.scale.x) && isfinite(Rest.scale.y) && isfinite(Rest.scale.z) && isfinite(Rotation[0])
        && isfinite(Rotation[1]) && isfinite(Rotation[2]) && isfinite(Rotation[3]);
    if (!IsFinite || !NormalizeRotation(Rotation))
    {
        char Name[NAME_PRINT_CAPACITY];
        return Fail(reader, ErrorCode_InvalidAssetData, "node \"%s\" has a transform that is not finite or a rotation "
            "of length 0.", GltfChecks_Printable(node->name, Name, sizeof(Name)));
    }
    Rest.rotation = (Quaternion){ Rotation[0], Rotation[1], Rotation[2], Rotation[3] };
    *outRest = Rest;
    return Error_CreateSuccess();
}

/* The bones' names, parents, rest poses and rest positions (each bone's rest pose composed with its ancestors'). */
static Error FillBones(const Reader* reader, AnimationLibraryBone* bones)
{
    cgltf_data* Data = reader->Data;
    for (cgltf_size Index = 0U; Index < Data->nodes_count; Index++)
    {
        uint32_t BoneIndex = reader->BoneOfNode[Index];
        if (BoneIndex == NOT_A_BONE)
        {
            continue;
        }
        const cgltf_node* Node = &Data->nodes[Index];
        AnimationLibraryBone* Bone = &bones[BoneIndex];
        size_t Length = 0U;
        while ((Node->name != NULL) && (Length < (ANIMATION_BONE_NAME_CAPACITY - 1U)) && (Node->name[Length] != '\0'))
        {
            Bone->_name[Length] = (unsigned char)Node->name[Length];
            Length++;
        }
        Bone->_name[Length] = 0U;
        // Every ancestor of a bone is a bone (MarkBones), so the parent node maps to one.
        Bone->_parent = (Node->parent != NULL) ? (int32_t)reader->BoneOfNode[GetNodeIndex(reader, Node->parent)] : -1;
        Error Result = ReadRest(reader, Node, &Bone->_rest);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    // Rest positions, parents first: each pass places the bones whose parent is placed (the tree is at most BoneCount
    // levels deep, so BoneCount passes place them all).
    Matrix Placed[ANIMATION_LIBRARY_MAX_BONES];
    bool IsPlaced[ANIMATION_LIBRARY_MAX_BONES] = { false };
    uint32_t PlacedCount = 0U;
    for (uint32_t Pass = 0U; (Pass < reader->BoneCount) && (PlacedCount < reader->BoneCount); Pass++)
    {
        for (uint32_t Index = 0U; Index < reader->BoneCount; Index++)
        {
            int32_t Parent = bones[Index]._parent;
            if (IsPlaced[Index] || ((Parent >= 0) && !IsPlaced[Parent]))
            {
                continue;
            }
            Matrix Local = SkeletalAnimation_ComposeTransform(bones[Index]._rest);
            Placed[Index] = (Parent < 0) ? Local : MatrixMultiply(Local, Placed[Parent]);
            bones[Index]._restPosition = (Vector3){ Placed[Index].m12, Placed[Index].m13, Placed[Index].m14 };
            IsPlaced[Index] = true;
            PlacedCount++;
        }
    }
    return Error_CreateSuccess();
}

/* Unpacks one accessor's floats into @p out (exactly @p floatCount of them) and checks they are finite. */
static bool UnpackFloats(const cgltf_accessor* accessor, float* out, size_t floatCount)
{
    if (cgltf_accessor_unpack_floats(accessor, out, floatCount) != floatCount)
    {
        return false;
    }
    for (size_t Index = 0U; Index < floatCount; Index++)
    {
        if (!isfinite(out[Index]))
        {
            return false;
        }
    }
    return true;
}

/* Puts an accessor's key times into the block once, checked: at least 0, strictly increasing, within the longest clip. */
static Error FillTimes(Reader* reader, const cgltf_accessor* input, float* floats, const char* clipName,
    const float** outTimes)
{
    size_t Accessor = GetAccessorIndex(reader, input);
    float* Times = floats + reader->TimesOfAccessor[Accessor];
    *outTimes = Times;
    if (reader->AreTimesFilled[Accessor])
    {
        return Error_CreateSuccess();
    }
    if (!UnpackFloats(input, Times, input->count))
    {
        return Fail(reader, ErrorCode_InvalidAssetData, "clip \"%s\" has key times that cannot be read or are not "
            "finite.", clipName);
    }
    for (size_t Key = 0U; Key < input->count; Key++)
    {
        bool IsInOrder = (Key == 0U) ? (Times[0] >= 0.0f) : (Times[Key] > Times[Key - 1U]);
        if (!IsInOrder || (Times[Key] > ANIMATION_CLIP_MAX_DURATION_SECONDS))
        {
            return Fail(reader, ErrorCode_InvalidAssetData, "clip \"%s\": key times must start at 0 or later, rise "
                "from key to key and stay within %g seconds.", clipName, (double)ANIMATION_CLIP_MAX_DURATION_SECONDS);
        }
    }
    reader->AreTimesFilled[Accessor] = true;
    return Error_CreateSuccess();
}

/* Puts one channel's values into the block (rotations normalized; a cubic spline's tangents kept as they are). */
static Error FillValues(const Reader* reader, const cgltf_animation_sampler* sampler, uint32_t part, float* values,
    const char* clipName)
{
    size_t PerValue = (part == PART_ROTATION) ? ROTATION_FLOATS : VECTOR3_FLOATS;
    size_t ValueCount = sampler->output->count;
    if (!UnpackFloats(sampler->output, values, ValueCount * PerValue))
    {
        return Fail(reader, ErrorCode_InvalidAssetData, "clip \"%s\" has values that cannot be read or are not finite.",
            clipName);
    }
    if (part != PART_ROTATION)
    {
        return Error_CreateSuccess();
    }
    bool IsCubic = sampler->interpolation == cgltf_interpolation_type_cubic_spline;
    for (size_t Value = 0U; Value < ValueCount; Value++)
    {
        if (IsCubic && ((Value % CUBIC_VALUES_PER_KEY) != 1U))
        {
            continue;
        }
        if (!NormalizeRotation(values + (Value * ROTATION_FLOATS)))
        {
            return Fail(reader, ErrorCode_InvalidAssetData, "clip \"%s\" has a rotation of length 0.", clipName);
        }
    }
    return Error_CreateSuccess();
}

static AnimationInterpolation ToInterpolation(cgltf_interpolation_type interpolation)
{
    switch (interpolation)
    {
        case cgltf_interpolation_type_step: return AnimationInterpolation_Step;
        case cgltf_interpolation_type_cubic_spline: return AnimationInterpolation_CubicSpline;
        case cgltf_interpolation_type_linear:
        case cgltf_interpolation_type_max_enum:
            return AnimationInterpolation_Linear;
    }
    return AnimationInterpolation_Linear;
}

/* Fills one clip: its name, its settings and its tracks; *valueCursor moves past its values. */
static Error FillClip(Reader* reader, const ClipPlan* plan, AnimationClip* clip, AnimationBoneTracks* tracks,
    float* floats, size_t* valueCursor, unsigned char* name)
{
    size_t NameBytes = StringUTF8_GetByteLength(plan->Name) + 1U;
    Memory_Copy(plan->Name, name, NameBytes);
    clip->_name = name;
    clip->_isLooped = plan->IsLooped;
    clip->_speed = plan->Speed;
    clip->_hasDistance = plan->HasDistance;
    clip->_distance = plan->Distance;
    clip->_bones = tracks;
    char ClipName[NAME_PRINT_CAPACITY];
    GltfChecks_Printable((const char*)plan->Name, ClipName, sizeof(ClipName));

    float Duration = 0.0f;
    const cgltf_animation* Animation = plan->Animation;
    for (cgltf_size Channel = 0U; Channel < Animation->channels_count; Channel++)
    {
        const cgltf_animation_channel* Source = &Animation->channels[Channel];
        uint32_t Part = GetPart(Source->target_path);
        if ((Source->target_node == NULL) || (Part == PART_COUNT))
        {
            continue;
        }
        uint32_t Bone = reader->BoneOfNode[GetNodeIndex(reader, Source->target_node)];
        AnimationTrack* Track = (Part == PART_TRANSLATION) ? &tracks[Bone]._translation
            : ((Part == PART_ROTATION) ? &tracks[Bone]._rotation : &tracks[Bone]._scale);
        const cgltf_animation_sampler* Sampler = Source->sampler;
        Error Result = FillTimes(reader, Sampler->input, floats, ClipName, &Track->_times);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        float* Values = floats + reader->TimeFloatCount + *valueCursor;
        Result = FillValues(reader, Sampler, Part, Values, ClipName);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Track->_keyCount = (uint32_t)Sampler->input->count;
        Track->_interpolation = ToInterpolation(Sampler->interpolation);
        Track->_values = Values;
        *valueCursor += Sampler->output->count * ((Part == PART_ROTATION) ? ROTATION_FLOATS : VECTOR3_FLOATS);
        Duration = fmaxf(Duration, Track->_times[Track->_keyCount - 1U]);
    }
    clip->_duration = Duration;
    return Error_CreateSuccess();
}

/* Adds @p count elements of @p size bytes to *ioOffset, false on overflow. */
static bool TryAddArray(size_t* ioOffset, size_t count, size_t size)
{
    size_t Bytes = 0U;
    return Memory_TryMultiplySize(count, size, &Bytes) && Memory_TryAddSize(*ioOffset, Bytes, ioOffset);
}

/* Builds the library's block from the plan. */
static Error Fill(Reader* reader, AnimationLibrary** outLibrary)
{
    // The block's parts, each aligned for what follows (the struct sizes are multiples of their alignment, the larger
    // alignments first).
    size_t TrackCount = 0U;
    size_t FloatCount = 0U;
    size_t ClipsOffset = sizeof(AnimationLibrary);
    size_t TracksOffset = ClipsOffset;
    size_t BonesOffset = 0U;
    size_t FloatsOffset = 0U;
    size_t NamesOffset = 0U;
    size_t ByteCount = 0U;
    bool IsSized = Memory_TryMultiplySize(reader->ClipCount, reader->BoneCount, &TrackCount)
        && Memory_TryAddSize(reader->TimeFloatCount, reader->ValueFloatCount, &FloatCount)
        && TryAddArray(&TracksOffset, reader->ClipCount, sizeof(AnimationClip));
    BonesOffset = TracksOffset;
    IsSized = IsSized && TryAddArray(&BonesOffset, TrackCount, sizeof(AnimationBoneTracks));
    FloatsOffset = BonesOffset;
    IsSized = IsSized && TryAddArray(&FloatsOffset, reader->BoneCount, sizeof(AnimationLibraryBone));
    NamesOffset = FloatsOffset;
    IsSized = IsSized && TryAddArray(&NamesOffset, FloatCount, sizeof(float));
    ByteCount = NamesOffset;
    IsSized = IsSized && Memory_TryAddSize(ByteCount, reader->NameByteCount, &ByteCount);
    if (!IsSized)
    {
        return Fail(reader, ErrorCode_BufferTooLarge, "its clips are too large.");
    }

    unsigned char* Block = Memory_Allocate(ByteCount);
    Memory_Zero(Block, ByteCount);
    AnimationLibrary* Library = (AnimationLibrary*)(void*)Block;
    AnimationClip* Clips = (AnimationClip*)(void*)(Block + ClipsOffset);
    AnimationBoneTracks* Tracks = (AnimationBoneTracks*)(void*)(Block + TracksOffset);
    AnimationLibraryBone* Bones = (AnimationLibraryBone*)(void*)(Block + BonesOffset);
    float* Floats = (float*)(void*)(Block + FloatsOffset);
    unsigned char* Names = Block + NamesOffset;

    Error Result = FillBones(reader, Bones);
    size_t ValueCursor = 0U;
    for (uint32_t Clip = 0U; (Clip < reader->ClipCount) && (Result.Code == ErrorCode_Success); Clip++)
    {
        Result = FillClip(reader, &reader->Clips[Clip], &Clips[Clip], Tracks + ((size_t)Clip * reader->BoneCount),
            Floats, &ValueCursor, Names);
        Names += StringUTF8_GetByteLength(reader->Clips[Clip].Name) + 1U;
    }
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Block);
        return Result;
    }
    Library->_bones = Bones;
    Library->_boneCount = reader->BoneCount;
    Library->_clips = Clips;
    Library->_clipCount = reader->ClipCount;
    *outLibrary = Library;
    return Error_CreateSuccess();
}

static void DestroyReader(Reader* reader)
{
    if (reader->Data != NULL)
    {
        cgltf_free(reader->Data);
    }
    Memory_Free(reader->BoneOfNode);
    Memory_Free(reader->Clips);
    Memory_Free(reader->IsSettingUsed);
    Memory_Free(reader->TimesOfAccessor);
    Memory_Free(reader->AreTimesFilled);
    Memory_Free(reader->SeenParts);
}


// Public functions.
Error AnimationLibrary_CreateFromGltf(const void* data, size_t byteCount, const AnimationLibraryReadOptions* options,
    AnimationLibrary** outLibrary)
{
    if ((outLibrary == NULL) || (options == NULL) || ((data == NULL) && (byteCount > 0U))
        || ((options->Clips == NULL) && (options->ClipCount > 0U)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "AnimationLibrary_CreateFromGltf: outLibrary, options and the data must not be NULL.");
    }
    *outLibrary = NULL;
    Reader Self;
    Memory_Zero(&Self, sizeof(Self));
    Self.Options = options;
    if (options->Description != NULL)
    {
        char Name[NAME_PRINT_CAPACITY];
        snprintf(Self.Description, sizeof(Self.Description), "%s",
            GltfChecks_Printable((const char*)options->Description, Name, sizeof(Name)));
    }
    else
    {
        snprintf(Self.Description, sizeof(Self.Description), "The animation library");
    }
    if (byteCount == 0U)
    {
        return Fail(&Self, ErrorCode_InvalidAssetData, "the file is empty.");
    }
    if (byteCount > ANIMATION_LIBRARY_MAX_FILE_BYTES)
    {
        return Fail(&Self, ErrorCode_BufferTooLarge, "the file has %" PRIu64 " bytes; at most %" PRIu64 " are read.",
            (uint64_t)byteCount, (uint64_t)ANIMATION_LIBRARY_MAX_FILE_BYTES);
    }

    Error Result = ParseFile(&Self, data, byteCount);
    if (Result.Code == ErrorCode_Success)
    {
        Result = MarkBones(&Self);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = PlanClips(&Self);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Fill(&Self, outLibrary);
    }
    DestroyReader(&Self);
    return Result;
}

Error AnimationLibrary_CreateFromFile(const unsigned char* path, const AnimationLibraryReadOptions* options,
    AnimationLibrary** outLibrary)
{
    if ((path == NULL) || (options == NULL) || (outLibrary == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "AnimationLibrary_CreateFromFile: path, options and outLibrary must not be NULL.");
    }
    *outLibrary = NULL;
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    Error Result = GltfChecks_ReadLimitedFile(path, ANIMATION_LIBRARY_MAX_FILE_BYTES, &Bytes);
    if (Result.Code == ErrorCode_Success)
    {
        AnimationLibraryReadOptions Options = *options;
        if (Options.FilePath == NULL)
        {
            Options.FilePath = path;
        }
        Result = AnimationLibrary_CreateFromGltf(Bytes._data, Bytes._count, &Options, outLibrary);
    }
    Memory_Free(Bytes._data);
    return Result;
}
