#include <math.h>
#include "AnimationLibrary.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * Sampling and lookups over a loaded animation library (the reader is source/AnimationLibraryGltf.c, the asset type
 * source/AnimationLibraryDefinition.c). Pure functions over immutable data, so any number of animators sample one
 * library at once.
 */


// Macros.
/** Floats per translation or scale value, and per rotation. */
#define VECTOR3_FLOATS 3U
#define ROTATION_FLOATS 4U
/** A cubic spline key holds three values: its in-tangent, its value, its out-tangent. */
#define CUBIC_VALUES_PER_KEY 3U


// Static functions.
/* The key whose span holds @p time: times[result] <= time < times[result + 1]. Needs at least two keys and
 * times[0] <= time < times[last]. */
static uint32_t FindSpan(const AnimationTrack* track, float time)
{
    uint32_t Low = 0U;
    uint32_t High = track->_keyCount - 1U;
    while ((High - Low) > 1U)
    {
        uint32_t Middle = Low + ((High - Low) / 2U);
        if (track->_times[Middle] <= time)
        {
            Low = Middle;
        }
        else
        {
            High = Middle;
        }
    }
    return Low;
}

/* Where a key's value starts in the track's floats: a cubic spline key's value sits between its two tangents. */
static const float* GetValue(const AnimationTrack* track, uint32_t key, uint32_t floatsPerValue)
{
    if (track->_interpolation == AnimationInterpolation_CubicSpline)
    {
        return track->_values + (((size_t)key * CUBIC_VALUES_PER_KEY) + 1U) * floatsPerValue;
    }
    return track->_values + ((size_t)key * floatsPerValue);
}

/* A key's in-tangent (cubic spline keys only). */
static const float* GetInTangent(const AnimationTrack* track, uint32_t key, uint32_t floatsPerValue)
{
    return track->_values + ((size_t)key * CUBIC_VALUES_PER_KEY * floatsPerValue);
}

/* A key's out-tangent (cubic spline keys only). */
static const float* GetOutTangent(const AnimationTrack* track, uint32_t key, uint32_t floatsPerValue)
{
    return track->_values + ((((size_t)key * CUBIC_VALUES_PER_KEY) + 2U) * floatsPerValue);
}

/* Which key's value a time outside the track's span reads (the first before it, the last from its end on), or false
 * when the time falls between two keys (then *outSpan holds the first of them and *outAmount how far along it is). */
static bool TryGetHeldKey(const AnimationTrack* track, float time, uint32_t* outKey, uint32_t* outSpan,
    float* outAmount)
{
    uint32_t Last = track->_keyCount - 1U;
    // A NaN time reads the first key: !(time > first) is true for it.
    if ((track->_keyCount == 1U) || !(time > track->_times[0]))
    {
        *outKey = 0U;
        return true;
    }
    if (time >= track->_times[Last])
    {
        *outKey = Last;
        return true;
    }
    uint32_t Span = FindSpan(track, time);
    float Start = track->_times[Span];
    float Length = track->_times[Span + 1U] - Start;
    *outSpan = Span;
    *outAmount = Clamp((time - Start) / Length, 0.0f, 1.0f);
    return false;
}

/* glTF's cubic Hermite spline for one component: the values and tangents of two keys @p length seconds apart. */
static float Hermite(float start, float outTangent, float end, float inTangent, float length, float amount)
{
    float Square = amount * amount;
    float Cube = Square * amount;
    return (((2.0f * Cube) - (3.0f * Square) + 1.0f) * start)
        + ((Cube - (2.0f * Square) + amount) * length * outTangent)
        + ((-2.0f * Cube + (3.0f * Square)) * end)
        + ((Cube - Square) * length * inTangent);
}

/* Interpolates @p count components between key @p span and the next. */
static void Interpolate(const AnimationTrack* track, uint32_t span, float amount, uint32_t count, float* outValue)
{
    const float* Start = GetValue(track, span, count);
    const float* End = GetValue(track, span + 1U, count);
    switch (track->_interpolation)
    {
        case AnimationInterpolation_Step:
            Memory_Copy(Start, outValue, count * sizeof(float));
            return;
        case AnimationInterpolation_Linear:
            for (uint32_t Component = 0U; Component < count; Component++)
            {
                outValue[Component] = Start[Component] + ((End[Component] - Start[Component]) * amount);
            }
            return;
        case AnimationInterpolation_CubicSpline:
        {
            float Length = track->_times[span + 1U] - track->_times[span];
            const float* OutTangent = GetOutTangent(track, span, count);
            const float* InTangent = GetInTangent(track, span + 1U, count);
            for (uint32_t Component = 0U; Component < count; Component++)
            {
                outValue[Component] = Hermite(Start[Component], OutTangent[Component], End[Component],
                    InTangent[Component], Length, amount);
            }
            return;
        }
    }
    // Only reachable with a corrupted interpolation value: hold the earlier key.
    Memory_Copy(Start, outValue, count * sizeof(float));
}

/* Whether a stored bone name equals @p name as the library keeps names (its first ANIMATION_BONE_NAME_CAPACITY - 1
 * bytes). */
static bool IsBoneNamed(const AnimationLibraryBone* bone, const unsigned char* name)
{
    size_t Length = StringUTF8_GetByteLength(name);
    if (Length > (ANIMATION_BONE_NAME_CAPACITY - 1U))
    {
        Length = ANIMATION_BONE_NAME_CAPACITY - 1U;
    }
    return (StringUTF8_GetByteLength(bone->_name) == Length) && Memory_IsEqual(bone->_name, name, Length);
}

static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == StringUTF8_GetByteLength(b)) && Memory_IsEqual(a, b, Length);
}


// Public functions.
void AnimationLibrary_Destroy(AnimationLibrary* self)
{
    // The library, its bones, clips, tracks, names and floats are one block.
    Memory_Free(self);
}

bool AnimationLibrary_TryFindClip(const AnimationLibrary* self, const unsigned char* name, uint32_t* outIndex)
{
    if (name == NULL)
    {
        return false;
    }
    for (uint32_t Index = 0U; Index < self->_clipCount; Index++)
    {
        if (AreNamesEqual(self->_clips[Index]._name, name))
        {
            if (outIndex != NULL)
            {
                *outIndex = Index;
            }
            return true;
        }
    }
    return false;
}

bool AnimationLibrary_TryFindBone(const AnimationLibrary* self, const unsigned char* name, uint32_t* outIndex)
{
    if (StringUTF8_IsNullOrEmpty(name))
    {
        return false;
    }
    for (uint32_t Index = 0U; Index < self->_boneCount; Index++)
    {
        if (IsBoneNamed(&self->_bones[Index], name))
        {
            if (outIndex != NULL)
            {
                *outIndex = Index;
            }
            return true;
        }
    }
    return false;
}

Vector3 AnimationTrack_SampleVector3(const AnimationTrack* self, float time)
{
    uint32_t Key = 0U;
    uint32_t Span = 0U;
    float Amount = 0.0f;
    float Value[VECTOR3_FLOATS];
    if (TryGetHeldKey(self, time, &Key, &Span, &Amount))
    {
        Memory_Copy(GetValue(self, Key, VECTOR3_FLOATS), Value, sizeof(Value));
    }
    else
    {
        Interpolate(self, Span, Amount, VECTOR3_FLOATS, Value);
    }
    return (Vector3){ Value[0], Value[1], Value[2] };
}

Quaternion AnimationTrack_SampleRotation(const AnimationTrack* self, float time)
{
    uint32_t Key = 0U;
    uint32_t Span = 0U;
    float Amount = 0.0f;
    if (TryGetHeldKey(self, time, &Key, &Span, &Amount))
    {
        const float* Held = GetValue(self, Key, ROTATION_FLOATS);
        return (Quaternion){ Held[0], Held[1], Held[2], Held[3] };
    }
    if (self->_interpolation == AnimationInterpolation_Linear)
    {
        // Spherical along the shorter arc (glTF's linear rotation; raymath falls back to a normalized blend for keys
        // close together, where the two agree).
        const float* Start = GetValue(self, Span, ROTATION_FLOATS);
        const float* End = GetValue(self, Span + 1U, ROTATION_FLOATS);
        return QuaternionNormalize(QuaternionSlerp((Quaternion){ Start[0], Start[1], Start[2], Start[3] },
            (Quaternion){ End[0], End[1], End[2], End[3] }, Amount));
    }
    float Value[ROTATION_FLOATS];
    Interpolate(self, Span, Amount, ROTATION_FLOATS, Value);
    return QuaternionNormalize((Quaternion){ Value[0], Value[1], Value[2], Value[3] });
}

Transform AnimationClip_SampleBone(const AnimationClip* self, uint32_t boneIndex, Transform rest, float time)
{
    const AnimationBoneTracks* Tracks = &self->_bones[boneIndex];
    Transform Result = rest;
    if (AnimationTrack_IsAnimated(&Tracks->_translation))
    {
        Result.translation = AnimationTrack_SampleVector3(&Tracks->_translation, time);
    }
    if (AnimationTrack_IsAnimated(&Tracks->_rotation))
    {
        Result.rotation = AnimationTrack_SampleRotation(&Tracks->_rotation, time);
    }
    if (AnimationTrack_IsAnimated(&Tracks->_scale))
    {
        Result.scale = AnimationTrack_SampleVector3(&Tracks->_scale, time);
    }
    return Result;
}
