#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Transform, Vector3 and Quaternion are raylib types: a bone's rest pose and every sample are raylib transforms, so this
// include is unavoidable here.
#include "raylib/raylib.h"
#include "Logger.h"
#include "wr/WRError.h"


/**
 * @file AnimationLibrary.h
 * @brief Animation libraries: named skeletal animation clips read from a glTF file (T29), and how to sample them.
 *
 * WHAT. An animation library is the clips of one glTF file (.glb or .gltf): Blender's actions, each a set of tracks
 * that move bones over time. It is a plain asset type of its own ("animation_library", JSON definitions in
 * animation_libraries/; AssetManager_LoadAnimationLibrary), apart from the models that play it, so ONE library of
 * motions can play on many models: every model whose skeleton has bones of the same names. A world model names the
 * libraries its placements play (WorldModel.h), and the animator component plays their clips (WorldAnimatorComponent.h).
 *
 * THE LIBRARY'S SKELETON. The file's bones are the nodes its clips animate and their ancestors (an armature exported
 * without its mesh has no skin, so the skin is not needed), each with its name (the first
 * ANIMATION_BONE_NAME_CAPACITY - 1 bytes, as raylib keeps a model's bone names), its parent and its rest pose (the
 * node's own translation, rotation and scale). Other nodes (meshes, cameras, unrelated empties) are ignored. Matching a
 * library to a model by bone name, and playing it on a model of other proportions, is SkeletalAnimation.h's work.
 *
 * CLIPS. Each clip keeps its tracks exactly as the file has them: per bone an optional translation, rotation and scale
 * track, each with its own key times and interpolation (step, linear, or glTF's cubic spline), so a clip plays at any
 * time without being resampled. A clip's duration is its latest key. Sampling holds the first key before it and the
 * last key from the last key's time on (raylib's own loader resamples at 60 frames a second and reads the last frame
 * wrong). The library's definition sets each clip's game name, whether it loops and its speed.
 *
 * MEMORY. A library is one block: create it with AnimationLibrary_CreateFromGltf and free it with
 * AnimationLibrary_Destroy (the asset manager does both for the asset type). It is immutable once created, so any
 * number of animators read it at once.
 */


// Macros.
/** @brief Room for a bone's name with its terminator: raylib's model bone names (BoneInfo.name) hold 31 bytes, so a
 *  library keeps the same bytes and names compare alike on both sides. */
#define ANIMATION_BONE_NAME_CAPACITY ((size_t)32)
/** @brief The most bones a library may have: the nodes its clips animate and their ancestors. */
#define ANIMATION_LIBRARY_MAX_BONES ((size_t)256)
/** @brief The most clips a library may have. */
#define ANIMATION_LIBRARY_MAX_CLIPS ((size_t)256)
/** @brief The most bytes a clip's name may have (without the terminator). */
#define ANIMATION_CLIP_NAME_MAX_LENGTH ((size_t)64)
/** @brief The most bytes a clip's name in its file may have, for a definition's "source" (without the terminator). */
#define ANIMATION_CLIP_SOURCE_MAX_LENGTH ((size_t)256)
/** @brief The longest clip, in seconds: its latest key may not lie later. */
#define ANIMATION_CLIP_MAX_DURATION_SECONDS 3600.0f
/** @brief The most keys of all the tracks of all the clips of one library together (a cubic spline key counts once). */
#define ANIMATION_LIBRARY_MAX_KEYS ((size_t)4194304)
/** @brief The fastest a clip may be set to play (its speed multiplies time). */
#define ANIMATION_CLIP_MAX_SPEED 100.0f
/** @brief The speed of a clip whose definition sets none. */
#define ANIMATION_CLIP_DEFAULT_SPEED 1.0f
/** @brief The longest distance a clip may cover per cycle, in metres (its "distance" setting). */
#define ANIMATION_CLIP_MAX_DISTANCE 1000.0f
/** @brief The largest library file, and the largest buffer file next to a .gltf, the loader reads (bytes). */
#define ANIMATION_LIBRARY_MAX_FILE_BYTES ((size_t)256 * 1024 * 1024)


// Types.
/**
 * @brief How a track moves between two keys (glTF's sampler interpolation). The values are stable.
 */
typedef enum AnimationInterpolationEnum
{
    /** @brief Holds each key's value until the next key. */
    AnimationInterpolation_Step = 0,
    /** @brief Straight from key to key: a linear blend for translations and scales, the shorter arc for rotations. */
    AnimationInterpolation_Linear = 1,
    /** @brief glTF's cubic Hermite spline through the keys, with each key's in- and out-tangent. */
    AnimationInterpolation_CubicSpline = 2
} AnimationInterpolation;

/**
 * @brief One part of one bone's motion in a clip: its keys over time. Read-only; part of a library's block.
 */
typedef struct AnimationTrackStruct
{
    /** @brief How many keys; 0 when the clip does not move this part of the bone (it keeps its rest value). */
    uint32_t _keyCount;
    /** @brief How the track moves between keys. */
    AnimationInterpolation _interpolation;
    /** @brief The keys' times in seconds, _keyCount of them, finite, at least 0 and strictly increasing. */
    const float* _times;
    /** @brief The keys' values: 3 floats per value for a translation or a scale, 4 (x, y, z, w, unit length) for a
     *  rotation; a cubic spline key holds three values (its in-tangent, its value, its out-tangent). */
    const float* _values;
} AnimationTrack;

/**
 * @brief The tracks of one bone in one clip. Read-only; part of a library's block.
 */
typedef struct AnimationBoneTracksStruct
{
    /** @brief The bone's local translation over time. */
    AnimationTrack _translation;
    /** @brief The bone's local rotation over time. */
    AnimationTrack _rotation;
    /** @brief The bone's local scale over time. */
    AnimationTrack _scale;
} AnimationBoneTracks;

/**
 * @brief One clip of a library: its name, its settings and its tracks. Read-only; part of a library's block.
 */
typedef struct AnimationClipStruct
{
    /** @brief The clip's game name (the definition's, or the file's), NUL-terminated: an asset-style name
     *  ([a-z][a-z0-9_.-]*) of at most ANIMATION_CLIP_NAME_MAX_LENGTH bytes, unique in its library. */
    const unsigned char* _name;
    /** @brief Its length in seconds: its latest key's time (0 for a clip of one pose). */
    float _duration;
    /** @brief Whether it plays on from its start when it reaches its end (the definition says; off by default). */
    bool _isLooped;
    /** @brief How fast it plays (time multiplied), in (0, ANIMATION_CLIP_MAX_SPEED]; 1 by default. */
    float _speed;
    /** @brief Whether the definition says how far the clip covers per cycle (a walk or a run in place), and that
     *  distance in metres at the library's own size, in (0, ANIMATION_CLIP_MAX_DISTANCE]: an animation graph plays it
     *  at the speed a character really moves (T30). */
    bool _hasDistance;
    float _distance;
    /** @brief One entry per library bone, in the library's bone order. */
    const AnimationBoneTracks* _bones;
} AnimationClip;

/**
 * @brief One bone of a library's skeleton. Read-only; part of a library's block.
 */
typedef struct AnimationLibraryBoneStruct
{
    /** @brief The node's name: its first ANIMATION_BONE_NAME_CAPACITY - 1 bytes, NUL-terminated (empty for a node without
     *  a name, which matches no model bone). */
    unsigned char _name[ANIMATION_BONE_NAME_CAPACITY];
    /** @brief The parent bone's index, or -1 for a root (a node at the top of the file's hierarchy). */
    int32_t _parent;
    /** @brief The rest pose: the node's own translation, rotation (unit length) and scale, relative to its parent. */
    Transform _rest;
    /** @brief The bone's rest position in the file's model space (its rest pose composed with its ancestors'). */
    Vector3 _restPosition;
} AnimationLibraryBone;

/**
 * @brief An animation library: its skeleton and its clips, in one block. Read-only; see the file comment.
 */
typedef struct AnimationLibraryStruct
{
    /** @brief The bones, in the file's node order (a parent may come after its child). */
    const AnimationLibraryBone* _bones;
    /** @brief How many bones (at most ANIMATION_LIBRARY_MAX_BONES). */
    uint32_t _boneCount;
    /** @brief The clips, in the file's order (without the ones it skipped). */
    const AnimationClip* _clips;
    /** @brief How many clips (at most ANIMATION_LIBRARY_MAX_CLIPS). */
    uint32_t _clipCount;
} AnimationLibrary;

/**
 * @brief What a library's definition sets for one clip of its file. A plain value type; its strings are borrowed.
 */
typedef struct AnimationClipSettingsStruct
{
    /** @brief The clip's name in the file (any UTF-8, at most ANIMATION_CLIP_SOURCE_MAX_LENGTH bytes); never NULL. */
    const unsigned char* Source;
    /** @brief The name it plays under (a valid clip name: [a-z][a-z0-9_.-]*, at most ANIMATION_CLIP_NAME_MAX_LENGTH
     *  bytes); never NULL. */
    const unsigned char* Name;
    /** @brief Whether it loops. */
    bool IsLooped;
    /** @brief Its speed, in (0, ANIMATION_CLIP_MAX_SPEED]. */
    float Speed;
    /** @brief Whether Distance is set: the metres the clip covers per cycle at the library's size, in
     *  (0, ANIMATION_CLIP_MAX_DISTANCE]. */
    bool HasDistance;
    float Distance;
} AnimationClipSettings;

/**
 * @brief How AnimationLibrary_CreateFromGltf reads a file. A plain value type; everything in it is borrowed for the call.
 */
typedef struct AnimationLibraryReadOptionsStruct
{
    /** @brief The settings of the clips the definition names, matched to the file's clips by Source; every Source must be
     *  a clip of the file. A clip they do not name keeps its file name (when that is a valid clip name; otherwise it is
     *  skipped with a warning), does not loop and plays at speed 1. May be NULL when ClipCount is 0. */
    const AnimationClipSettings* Clips;
    /** @brief How many entries Clips holds. */
    size_t ClipCount;
    /** @brief The file's path, so that buffers in files of their own (a .gltf with its .bin) are read relative to it;
     *  NULL reads only buffers inside the data (a .glb's binary chunk, data URIs). */
    const unsigned char* FilePath;
    /** @brief Where warnings go (a skipped clip, ignored morph target tracks); may be NULL. */
    Logger* Logger;
    /** @brief What messages call the library (e.g. u8"animation library \"human_motions\""); NULL: "the animation
     *  library". */
    const unsigned char* Description;
} AnimationLibraryReadOptions;


// Functions.
/**
 * @brief Reads an animation library from a glTF file's bytes (a .glb, or a .gltf's JSON).
 *
 * The file is treated as hostile. Before cgltf loads or validates anything, its structure is checked in overflow-safe
 * sums: every buffer declares 1 byte or more, all of them together at most ANIMATION_LIBRARY_MAX_FILE_BYTES, a data URI
 * no more than its base64 holds; every buffer view fits in its buffer, with glTF's strides (none, or 4 to 252 bytes in
 * fours) and no meshopt compression; every accessor has a type and an element, fits in its view and is aligned to its
 * components, and a sparse one's replacements (1 to its count) fit in theirs; a buffer file's URI, with its percent
 * escapes decoded, stays beside the file (no absolute
 * path, "..", backslash, drive or NUL). A buffer file must hold its declared length, and loaded buffers must be aligned
 * (a .glb's chunks padded to four). Then it must validate as glTF 2.0 (cgltf: an acyclic node tree, sampler counts),
 * its clips' samplers must read floats of the right shapes (rotations may be normalized integers), every key time must
 * be finite, at least 0 and above the one before it, every value finite and every rotation of a length above 0 (it is
 * normalized), and the limits in this header hold (bones, clips, keys, duration, file sizes). Channels that animate
 * morph target weights are ignored with one warning per clip; a clip with no channel left is skipped with a warning.
 * @param data The file's bytes; must not be NULL when @p byteCount is above 0.
 * @param byteCount How many bytes (at most ANIMATION_LIBRARY_MAX_FILE_BYTES).
 * @param options How to read it; must not be NULL.
 * @param outLibrary [out] Receives the library (one block, owned by the caller: AnimationLibrary_Destroy); NULL on failure.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_InvalidAssetData when the file is not valid
 *          glTF, breaks a rule above or names its clips so that two would share a name; ErrorCode_InvalidAssetDefinition
 *          when a clip setting names a source the file does not have; ErrorCode_BufferTooLarge past a limit;
 *          ErrorCode_FileNotFound when a buffer file is missing, ErrorCode_IO when one cannot be read.
 */
Error AnimationLibrary_CreateFromGltf(const void* data, size_t byteCount, const AnimationLibraryReadOptions* options,
    AnimationLibrary** outLibrary);

/**
 * @brief Reads an animation library from a glTF file on disk: its bytes (at most ANIMATION_LIBRARY_MAX_FILE_BYTES, as
 *        every buffer file next to it), then AnimationLibrary_CreateFromGltf.
 * @param path The file's path (UTF-8); must not be NULL. Buffer files are read relative to it unless
 *        options->FilePath names another path.
 * @param options How to read it; must not be NULL.
 * @param outLibrary [out] Receives the library (owned by the caller: AnimationLibrary_Destroy); NULL on failure.
 * @returns As AnimationLibrary_CreateFromGltf; ErrorCode_BufferTooLarge for a file past the limit; ErrorCode_IO or
 *          ErrorCode_FileNotFound when it cannot be read.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error AnimationLibrary_CreateFromFile(const unsigned char* path, const AnimationLibraryReadOptions* options,
    AnimationLibrary** outLibrary);

/**
 * @brief Frees a library made by AnimationLibrary_CreateFromGltf or AnimationLibrary_CreateFromFile.
 * @param self The library, or NULL (nothing happens).
 */
void AnimationLibrary_Destroy(AnimationLibrary* self);

/** @brief Returns how many bones the library's skeleton has. @param self The library; must not be NULL. */
static inline uint32_t AnimationLibrary_GetBoneCount(const AnimationLibrary* self)
{
    return self->_boneCount;
}

/**
 * @brief Returns one bone of the library's skeleton.
 * @param self The library; must not be NULL.
 * @param index The bone's index, below AnimationLibrary_GetBoneCount.
 * @returns The bone (part of the library).
 */
static inline const AnimationLibraryBone* AnimationLibrary_GetBone(const AnimationLibrary* self, uint32_t index)
{
    return &self->_bones[index];
}

/** @brief Returns how many clips the library has. @param self The library; must not be NULL. */
static inline uint32_t AnimationLibrary_GetClipCount(const AnimationLibrary* self)
{
    return self->_clipCount;
}

/**
 * @brief Returns one clip of the library.
 * @param self The library; must not be NULL.
 * @param index The clip's index, below AnimationLibrary_GetClipCount.
 * @returns The clip (part of the library).
 */
static inline const AnimationClip* AnimationLibrary_GetClip(const AnimationLibrary* self, uint32_t index)
{
    return &self->_clips[index];
}

/**
 * @brief Finds a clip by its game name.
 * @param self The library; must not be NULL.
 * @param name The name; NULL finds nothing.
 * @param outIndex [out] Receives the clip's index when found; may be NULL.
 * @returns Whether the library has a clip of that name.
 */
bool AnimationLibrary_TryFindClip(const AnimationLibrary* self, const unsigned char* name, uint32_t* outIndex);

/**
 * @brief Finds a bone by its name (compared as the library keeps it: its first ANIMATION_BONE_NAME_CAPACITY - 1 bytes).
 * @param self The library; must not be NULL.
 * @param name The name; NULL or empty finds nothing.
 * @param outIndex [out] Receives the first bone of that name when found; may be NULL.
 * @returns Whether a bone has that name.
 */
bool AnimationLibrary_TryFindBone(const AnimationLibrary* self, const unsigned char* name, uint32_t* outIndex);

/** @brief Returns the clip's game name (NUL-terminated, part of the library). @param self The clip; must not be NULL. */
static inline const unsigned char* AnimationClip_GetName(const AnimationClip* self)
{
    return self->_name;
}

/** @brief Returns the clip's length in seconds (its latest key). @param self The clip; must not be NULL. */
static inline float AnimationClip_GetDuration(const AnimationClip* self)
{
    return self->_duration;
}

/** @brief Returns whether the clip loops. @param self The clip; must not be NULL. */
static inline bool AnimationClip_IsLooped(const AnimationClip* self)
{
    return self->_isLooped;
}

/** @brief Returns the clip's speed (time multiplied). @param self The clip; must not be NULL. */
static inline float AnimationClip_GetSpeed(const AnimationClip* self)
{
    return self->_speed;
}

/**
 * @brief Returns how far the clip covers per cycle, at the library's size, when its definition says.
 * @param self The clip; must not be NULL.
 * @param outDistance [out] Receives the metres per cycle when set; may be NULL.
 * @returns Whether the definition sets it.
 */
static inline bool AnimationClip_TryGetDistance(const AnimationClip* self, float* outDistance)
{
    if (self->_hasDistance && (outDistance != NULL))
    {
        *outDistance = self->_distance;
    }
    return self->_hasDistance;
}

/**
 * @brief Returns one bone's tracks in the clip.
 * @param self The clip; must not be NULL.
 * @param boneIndex A bone of the clip's library, below AnimationLibrary_GetBoneCount.
 * @returns The tracks (part of the library).
 */
static inline const AnimationBoneTracks* AnimationClip_GetBoneTracks(const AnimationClip* self, uint32_t boneIndex)
{
    return &self->_bones[boneIndex];
}

/** @brief Returns whether the track moves its part of the bone (it has keys). @param self The track; must not be NULL. */
static inline bool AnimationTrack_IsAnimated(const AnimationTrack* self)
{
    return self->_keyCount > 0U;
}

/**
 * @brief Samples a translation or scale track at a time: the first key's value before it, the last key's from its time
 *        on, and between two keys as the track's interpolation says.
 * @param self The track; must not be NULL and must have keys.
 * @param time The time in seconds (any finite value).
 * @returns The value.
 */
Vector3 AnimationTrack_SampleVector3(const AnimationTrack* self, float time);

/**
 * @brief Samples a rotation track at a time, as AnimationTrack_SampleVector3 does: linear keys turn along the shorter
 *        arc between two keys (spherical), cubic spline values are normalized.
 * @param self The track; must not be NULL and must have keys.
 * @param time The time in seconds (any finite value).
 * @returns The rotation (unit length).
 */
Quaternion AnimationTrack_SampleRotation(const AnimationTrack* self, float time);

/**
 * @brief Samples one bone of a clip at a time: each part the clip animates from its track, every other part from
 *        @p rest.
 * @param self The clip; must not be NULL.
 * @param boneIndex A bone of the clip's library.
 * @param rest The values of the parts the clip leaves alone (usually the bone's rest pose).
 * @param time The time in seconds into the clip (the caller wraps a looping clip's time).
 * @returns The bone's local transform.
 */
Transform AnimationClip_SampleBone(const AnimationClip* self, uint32_t boneIndex, Transform rest, float time);
