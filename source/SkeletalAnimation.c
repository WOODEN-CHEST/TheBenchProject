#include <float.h>
#include <math.h>
#include "SkeletalAnimation.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"

/*
 * Skeletal animation's runtime maths: bindings (libraries on skeletons, retargeted), blends, the mixer and the pose's
 * matrices, palette and bounds. See SkeletalAnimation.h for the rules; everything here is pure and allocates nothing.
 */

_Static_assert(ANIMATION_BONE_NAME_CAPACITY == GAME_SKELETON_BONE_NAME_CAPACITY,
    "library and skeleton bone names must be kept to the same length to compare alike");
_Static_assert(ANIMATION_LIBRARY_MAX_BONES <= 256U, "a library's bone map is a 256-bit set");


// Macros.
/** Columns shorter than this have no direction (a zero scale on that axis). */
#define DECOMPOSE_MIN_COLUMN_LENGTH 1e-12f
/** Rotation sums shorter than this have no direction (opposite rotations of equal weight). */
#define BLEND_MIN_ROTATION_LENGTH 1e-12f
/** Library rest scales nearer zero than this cannot be divided out (the clip's scale is taken as it is). */
#define RETARGET_MIN_SCALE 1e-6f
/** How many 64-bit words a set of library bones takes. */
#define LIBRARY_BONE_SET_WORDS ((ANIMATION_LIBRARY_MAX_BONES + 63U) / 64U)


// Static functions: transforms.
static Vector3 GetColumn(const Matrix* matrix, int column)
{
    switch (column)
    {
        case 0: return (Vector3){ matrix->m0, matrix->m1, matrix->m2 };
        case 1: return (Vector3){ matrix->m4, matrix->m5, matrix->m6 };
        default: return (Vector3){ matrix->m8, matrix->m9, matrix->m10 };
    }
}

/* A unit vector at right angles to @p axis (for completing a frame whose columns degenerated). */
static Vector3 GetPerpendicular(Vector3 axis)
{
    Vector3 Other = (fabsf(axis.x) < 0.9f) ? (Vector3){ 1.0f, 0.0f, 0.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };
    return Vector3Normalize(Vector3CrossProduct(axis, Other));
}

/* The rotation of three orthonormal columns (a right-handed frame). */
static Quaternion RotationFromColumns(Vector3 x, Vector3 y, Vector3 z)
{
    Matrix Rotation = MatrixIdentity();
    Rotation.m0 = x.x;
    Rotation.m1 = x.y;
    Rotation.m2 = x.z;
    Rotation.m4 = y.x;
    Rotation.m5 = y.y;
    Rotation.m6 = y.z;
    Rotation.m8 = z.x;
    Rotation.m9 = z.y;
    Rotation.m10 = z.z;
    return QuaternionNormalize(QuaternionFromMatrix(Rotation));
}


/* The part of @p column at right angles to the unit vector @p axis, normalized; false when nothing is left. */
static bool TryOrthogonalize(Vector3 column, Vector3 axis, Vector3* outUnit)
{
    Vector3 Rest = Vector3Subtract(column, Vector3Scale(axis, Vector3DotProduct(axis, column)));
    float Length = Vector3Length(Rest);
    if (!(Length > DECOMPOSE_MIN_COLUMN_LENGTH))
    {
        return false;
    }
    *outUnit = Vector3Scale(Rest, 1.0f / Length);
    return true;
}

/* A right-handed orthonormal frame from the matrix's columns: Gram-Schmidt in column order over the columns that have a
 * direction, the missing axes completed from the others (cross products), so a zero scale on one axis keeps the turn the
 * other two give. */
static void BuildFrame(const Vector3 columns[3], const float lengths[3], Vector3* outX, Vector3* outY, Vector3* outZ)
{
    bool HasX = lengths[0] > DECOMPOSE_MIN_COLUMN_LENGTH;
    Vector3 X = HasX ? Vector3Scale(columns[0], 1.0f / lengths[0]) : Vector3Zero();
    Vector3 Y = Vector3Zero();
    Vector3 Z = Vector3Zero();
    if (HasX)
    {
        if (TryOrthogonalize(columns[1], X, &Y))
        {
            Z = Vector3CrossProduct(X, Y);
        }
        else if (TryOrthogonalize(columns[2], X, &Z))
        {
            Y = Vector3CrossProduct(Z, X);
        }
        else
        {
            Y = GetPerpendicular(X);
            Z = Vector3CrossProduct(X, Y);
        }
    }
    else if (lengths[1] > DECOMPOSE_MIN_COLUMN_LENGTH)
    {
        Y = Vector3Scale(columns[1], 1.0f / lengths[1]);
        if (!TryOrthogonalize(columns[2], Y, &Z))
        {
            Z = GetPerpendicular(Y);
        }
        X = Vector3CrossProduct(Y, Z);
    }
    else if (lengths[2] > DECOMPOSE_MIN_COLUMN_LENGTH)
    {
        Z = Vector3Scale(columns[2], 1.0f / lengths[2]);
        X = GetPerpendicular(Z);
        Y = Vector3CrossProduct(Z, X);
    }
    else
    {
        X = (Vector3){ 1.0f, 0.0f, 0.0f };
        Y = (Vector3){ 0.0f, 1.0f, 0.0f };
        Z = (Vector3){ 0.0f, 0.0f, 1.0f };
    }
    *outX = X;
    *outY = Y;
    *outZ = Z;
}

// Static functions: bindings.
static bool IsInSet(const uint64_t* set, int32_t index)
{
    return (index >= 0) && ((set[(uint32_t)index / 64U] >> ((uint32_t)index % 64U)) & 1U) != 0U;
}

static void AddToSet(uint64_t* set, int32_t index)
{
    set[(uint32_t)index / 64U] |= (uint64_t)1U << ((uint32_t)index % 64U);
}

/* Folds the rest poses of the unmatched library ancestors above @p libraryBone into its binding entry, and returns its
 * nearest matched library ancestor (or -1). */
static int32_t FoldUnmatchedAncestors(const AnimationLibrary* library, const uint64_t* matched, int32_t libraryBone,
    SkeletalBindingBone* entry)
{
    Matrix Offset = MatrixIdentity();
    bool HasOffset = false;
    int32_t Ancestor = AnimationLibrary_GetBone(library, (uint32_t)libraryBone)->_parent;
    // The library's hierarchy is a tree (its reader checks), so the walk ends within its bone count.
    for (uint32_t Step = 0U; (Ancestor >= 0) && !IsInSet(matched, Ancestor) && (Step < library->_boneCount); Step++)
    {
        const AnimationLibraryBone* Bone = AnimationLibrary_GetBone(library, (uint32_t)Ancestor);
        Offset = MatrixMultiply(Offset, SkeletalAnimation_ComposeTransform(Bone->_rest));
        HasOffset = true;
        Ancestor = Bone->_parent;
    }
    Transform Rest = AnimationLibrary_GetBone(library, (uint32_t)libraryBone)->_rest;
    entry->HasOffset = HasOffset;
    entry->Offset = HasOffset ? SkeletalAnimation_DecomposeMatrix(Offset)
        : (Transform){ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };
    entry->LibraryRest = HasOffset
        ? SkeletalAnimation_DecomposeMatrix(MatrixMultiply(SkeletalAnimation_ComposeTransform(Rest), Offset)) : Rest;
    return Ancestor;
}

/* The skeleton's height over the library's: the highest rest position among the bones both share, on either side. */
static float MeasureHeightRatio(const GameSkeleton* skeleton, const AnimationLibrary* library,
    const SkeletalBindingBone* bones)
{
    float SkeletonHeight = -FLT_MAX;
    float LibraryHeight = -FLT_MAX;
    for (uint32_t Index = 0U; Index < skeleton->_boneCount; Index++)
    {
        if (bones[Index].LibraryBone < 0)
        {
            continue;
        }
        SkeletonHeight = fmaxf(SkeletonHeight, skeleton->_bones[Index]._restPosition.y);
        LibraryHeight = fmaxf(LibraryHeight,
            AnimationLibrary_GetBone(library, (uint32_t)bones[Index].LibraryBone)->_restPosition.y);
    }
    if (!(SkeletonHeight >= SKELETAL_BINDING_MIN_HEIGHT) || !(LibraryHeight >= SKELETAL_BINDING_MIN_HEIGHT))
    {
        return 1.0f;
    }
    float Ratio = SkeletonHeight / LibraryHeight;
    return isfinite(Ratio) ? Ratio : 1.0f;
}

/* A clip's sample of one bone, retargeted onto a skeleton bone (see SkeletalAnimation.h, "Retargeting"). */
static Transform Retarget(const SkeletalBinding* binding, const GameSkeletonBone* bone,
    const SkeletalBindingBone* entry, const AnimationClip* clip, float time)
{
    uint32_t LibraryBone = (uint32_t)entry->LibraryBone;
    const AnimationBoneTracks* Tracks = AnimationClip_GetBoneTracks(clip, LibraryBone);
    Transform Sample = AnimationClip_SampleBone(clip, LibraryBone,
        AnimationLibrary_GetBone(binding->Library, LibraryBone)->_rest, time);
    if (entry->HasOffset)
    {
        Sample = SkeletalAnimation_DecomposeMatrix(MatrixMultiply(SkeletalAnimation_ComposeTransform(Sample),
            SkeletalAnimation_ComposeTransform(entry->Offset)));
    }

    Transform Result = bone->_rest;
    if (AnimationTrack_IsAnimated(&Tracks->_rotation))
    {
        Result.rotation = Sample.rotation;
    }
    if (AnimationTrack_IsAnimated(&Tracks->_translation))
    {
        Vector3 Change = Vector3Subtract(Sample.translation, entry->LibraryRest.translation);
        Result.translation = Vector3Add(bone->_rest.translation, Vector3Scale(Change, binding->HeightRatio));
    }
    if (AnimationTrack_IsAnimated(&Tracks->_scale))
    {
        const Vector3 LibraryScale = entry->LibraryRest.scale;
        Result.scale.x = (fabsf(LibraryScale.x) > RETARGET_MIN_SCALE)
            ? bone->_rest.scale.x * (Sample.scale.x / LibraryScale.x) : Sample.scale.x;
        Result.scale.y = (fabsf(LibraryScale.y) > RETARGET_MIN_SCALE)
            ? bone->_rest.scale.y * (Sample.scale.y / LibraryScale.y) : Sample.scale.y;
        Result.scale.z = (fabsf(LibraryScale.z) > RETARGET_MIN_SCALE)
            ? bone->_rest.scale.z * (Sample.scale.z / LibraryScale.z) : Sample.scale.z;
    }
    return Result;
}


// Static functions: blends.
static void AddRotation(Quaternion* sum, Quaternion rotation, float weight)
{
    float Alignment = (sum->x * rotation.x) + (sum->y * rotation.y) + (sum->z * rotation.z) + (sum->w * rotation.w);
    float Sign = (Alignment < 0.0f) ? -1.0f : 1.0f;
    float Scale = weight * Sign;
    sum->x += rotation.x * Scale;
    sum->y += rotation.y * Scale;
    sum->z += rotation.z * Scale;
    sum->w += rotation.w * Scale;
}


// Static functions: bounds.
/* The smaller and the larger of two finite values. The box maths compares instead of calling fminf and fmaxf: those
 * order NaNs, which a pose never holds, and are library calls the compiler does not inline without fast maths (a tenth
 * of a tick's animation for a crowd went to them). */
static float Lower(float a, float b)
{
    return (a < b) ? a : b;
}

static float Higher(float a, float b)
{
    return (a > b) ? a : b;
}

static Vector3 LowerCorner(Vector3 a, Vector3 b)
{
    return (Vector3){ Lower(a.x, b.x), Lower(a.y, b.y), Lower(a.z, b.z) };
}

static Vector3 HigherCorner(Vector3 a, Vector3 b)
{
    return (Vector3){ Higher(a.x, b.x), Higher(a.y, b.y), Higher(a.z, b.z) };
}


// Static functions: the mixer.
static float SanitizeSeconds(float seconds)
{
    return (isfinite(seconds) && (seconds > 0.0f)) ? seconds : 0.0f;
}

/* A start time inside the clip: wrapped for a looping clip, clamped for any other. */
static double PlaceTime(const SkeletalClipRef* clip, double time)
{
    if (!isfinite(time) || !(clip->Duration > 0.0f))
    {
        return 0.0;
    }
    double Duration = (double)clip->Duration;
    if (clip->IsLooped)
    {
        double Wrapped = fmod(time, Duration);
        return (Wrapped < 0.0) ? (Wrapped + Duration) : Wrapped;
    }
    return (time < 0.0) ? 0.0 : ((time > Duration) ? Duration : time);
}

static SkeletalClipState* FindState(SkeletalMixer* self, uint32_t library, uint32_t clip)
{
    for (uint32_t Index = 0U; Index < self->StateCount; Index++)
    {
        if ((self->States[Index].Clip.Library == library) && (self->States[Index].Clip.Clip == clip))
        {
            return &self->States[Index];
        }
    }
    return NULL;
}

static void RemoveState(SkeletalMixer* self, uint32_t index)
{
    for (uint32_t Index = index + 1U; Index < self->StateCount; Index++)
    {
        self->States[Index - 1U] = self->States[Index];
    }
    self->StateCount--;
}

/* Removes every clip that has faded out (its weight and its target 0). */
static void RemoveFadedOut(SkeletalMixer* self)
{
    for (uint32_t Index = self->StateCount; Index > 0U; Index--)
    {
        const SkeletalClipState* State = &self->States[Index - 1U];
        if ((State->Weight <= 0.0f) && (State->TargetWeight <= 0.0f))
        {
            RemoveState(self, Index - 1U);
        }
    }
}

/* The state of @p clip, added (weight 0, at @p startTime) when it does not play; a full mixer first drops its lightest
 * clip. */
static SkeletalClipState* FindOrAddState(SkeletalMixer* self, const SkeletalClipRef* clip, double startTime)
{
    SkeletalClipState* State = FindState(self, clip->Library, clip->Clip);
    if (State != NULL)
    {
        return State;
    }
    if (self->StateCount == SKELETAL_MIXER_MAX_CLIPS)
    {
        uint32_t Lightest = 0U;
        for (uint32_t Index = 1U; Index < self->StateCount; Index++)
        {
            if (self->States[Index].Weight < self->States[Lightest].Weight)
            {
                Lightest = Index;
            }
        }
        RemoveState(self, Lightest);
    }
    State = &self->States[self->StateCount];
    self->StateCount++;
    Memory_Zero(State, sizeof(*State));
    State->Clip = *clip;
    State->Time = PlaceTime(clip, startTime);
    return State;
}

/* Starts a fade of one state toward @p target over @p seconds (0: at once). */
static void StartFade(SkeletalClipState* state, float target, float seconds)
{
    state->StartWeight = state->Weight;
    state->TargetWeight = target;
    state->FadeSeconds = seconds;
    state->FadeElapsed = 0.0f;
    if (seconds <= 0.0f)
    {
        state->Weight = target;
    }
}

static void AdvanceTime(SkeletalClipState* state, double seconds)
{
    if (state->HasEnded || !(state->Clip.Speed > 0.0f))
    {
        return;
    }
    double Duration = (double)state->Clip.Duration;
    if (!(Duration > 0.0))
    {
        state->Time = 0.0;
        state->HasEnded = !state->Clip.IsLooped;
        return;
    }
    double Time = state->Time + (seconds * (double)state->Clip.Speed);
    if (state->Clip.IsLooped)
    {
        state->Time = fmod(Time, Duration);
        return;
    }
    if (Time >= Duration)
    {
        state->Time = Duration;
        state->HasEnded = true;
        return;
    }
    state->Time = Time;
}

static void AdvanceFade(SkeletalClipState* state, float seconds)
{
    if (state->FadeElapsed >= state->FadeSeconds)
    {
        state->Weight = state->TargetWeight;
        return;
    }
    state->FadeElapsed += seconds;
    float Progress = (state->FadeElapsed >= state->FadeSeconds) ? 1.0f : (state->FadeElapsed / state->FadeSeconds);
    state->Weight = state->StartWeight + ((state->TargetWeight - state->StartWeight) * Progress);
}


// Public functions: transforms.
Matrix SkeletalAnimation_ComposeTransform(Transform transform)
{
    Matrix Rotation = QuaternionToMatrix(transform.rotation);
    Matrix Result = Rotation;
    Result.m0 = Rotation.m0 * transform.scale.x;
    Result.m1 = Rotation.m1 * transform.scale.x;
    Result.m2 = Rotation.m2 * transform.scale.x;
    Result.m4 = Rotation.m4 * transform.scale.y;
    Result.m5 = Rotation.m5 * transform.scale.y;
    Result.m6 = Rotation.m6 * transform.scale.y;
    Result.m8 = Rotation.m8 * transform.scale.z;
    Result.m9 = Rotation.m9 * transform.scale.z;
    Result.m10 = Rotation.m10 * transform.scale.z;
    Result.m3 = 0.0f;
    Result.m7 = 0.0f;
    Result.m11 = 0.0f;
    Result.m12 = transform.translation.x;
    Result.m13 = transform.translation.y;
    Result.m14 = transform.translation.z;
    Result.m15 = 1.0f;
    return Result;
}

Transform SkeletalAnimation_DecomposeMatrix(Matrix matrix)
{
    Transform Result;
    Result.translation = (Vector3){ matrix.m12, matrix.m13, matrix.m14 };
    Vector3 Columns[3] = { GetColumn(&matrix, 0), GetColumn(&matrix, 1), GetColumn(&matrix, 2) };
    float Lengths[3] = { Vector3Length(Columns[0]), Vector3Length(Columns[1]), Vector3Length(Columns[2]) };
    Vector3 X;
    Vector3 Y;
    Vector3 Z;
    BuildFrame(Columns, Lengths, &X, &Y, &Z);

    // A mirroring matrix (its columns left-handed): the frame stays right-handed, so the x scale takes the sign.
    float Determinant = Vector3DotProduct(Columns[0], Vector3CrossProduct(Columns[1], Columns[2]));
    float SignX = 1.0f;
    if (Determinant < 0.0f)
    {
        SignX = -1.0f;
        X = Vector3Negate(X);
        Z = Vector3CrossProduct(X, Y);
    }
    Result.rotation = RotationFromColumns(X, Y, Z);
    Result.scale = (Vector3){ Lengths[0] * SignX, Lengths[1], Lengths[2] };
    return Result;
}


// Public functions: bindings.
void SkeletalBinding_Build(const GameSkeleton* skeleton, const AnimationLibrary* library,
    SkeletalBindingBone* storage, SkeletalBinding* outBinding)
{
    outBinding->Library = library;
    outBinding->Skeleton = skeleton;
    outBinding->Bones = storage;
    outBinding->MappedCount = 0U;
    outBinding->MismatchedParentCount = 0U;
    outBinding->HeightRatio = 1.0f;

    uint64_t Matched[LIBRARY_BONE_SET_WORDS];
    Memory_Zero(Matched, sizeof(Matched));
    for (uint32_t Index = 0U; Index < skeleton->_boneCount; Index++)
    {
        uint32_t LibraryBone = 0U;
        bool IsFound = AnimationLibrary_TryFindBone(library, skeleton->_bones[Index]._name, &LibraryBone);
        storage[Index].LibraryBone = IsFound ? (int32_t)LibraryBone : SKELETAL_BINDING_UNMAPPED;
        storage[Index].HasOffset = false;
        storage[Index].Offset = (Transform){ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };
        if (IsFound)
        {
            storage[Index].LibraryRest = AnimationLibrary_GetBone(library, LibraryBone)->_rest;
            AddToSet(Matched, (int32_t)LibraryBone);
            outBinding->MappedCount++;
        }
    }

    for (uint32_t Index = 0U; Index < skeleton->_boneCount; Index++)
    {
        SkeletalBindingBone* Entry = &storage[Index];
        if (Entry->LibraryBone < 0)
        {
            continue;
        }
        int32_t MatchedAncestor = FoldUnmatchedAncestors(library, Matched, Entry->LibraryBone, Entry);
        int32_t SkeletonParent = skeleton->_bones[Index]._parent;
        int32_t Expected = (SkeletonParent >= 0) ? storage[SkeletonParent].LibraryBone : SKELETAL_BINDING_UNMAPPED;
        if (MatchedAncestor != Expected)
        {
            outBinding->MismatchedParentCount++;
        }
    }
    outBinding->HeightRatio = MeasureHeightRatio(skeleton, library, storage);
}

void SkeletalBinding_SampleClip(const SkeletalBinding* self, uint32_t clipIndex, float time, Transform* outPose)
{
    const AnimationClip* Clip = AnimationLibrary_GetClip(self->Library, clipIndex);
    const GameSkeleton* Skeleton = self->Skeleton;
    for (uint32_t Index = 0U; Index < Skeleton->_boneCount; Index++)
    {
        const GameSkeletonBone* Bone = &Skeleton->_bones[Index];
        const SkeletalBindingBone* Entry = &self->Bones[Index];
        outPose[Index] = (Entry->LibraryBone < 0) ? Bone->_rest : Retarget(self, Bone, Entry, Clip, time);
    }
}


// Public functions: poses.
void SkeletalPose_SetRest(const GameSkeleton* skeleton, Transform* outPose)
{
    for (uint32_t Index = 0U; Index < skeleton->_boneCount; Index++)
    {
        outPose[Index] = skeleton->_bones[Index]._rest;
    }
}

void SkeletalPose_ClearBlend(SkeletalBlendBone* blend, uint32_t boneCount)
{
    Memory_Zero(blend, (size_t)boneCount * sizeof(SkeletalBlendBone));
}

void SkeletalPose_AddToBlend(SkeletalBlendBone* blend, const Transform* pose, uint32_t boneCount, float weight)
{
    for (uint32_t Index = 0U; Index < boneCount; Index++)
    {
        SkeletalBlendBone* Bone = &blend[Index];
        Bone->Translation = Vector3Add(Bone->Translation, Vector3Scale(pose[Index].translation, weight));
        Bone->Scale = Vector3Add(Bone->Scale, Vector3Scale(pose[Index].scale, weight));
        AddRotation(&Bone->Rotation, pose[Index].rotation, weight);
    }
}

void SkeletalPose_FinishBlend(const SkeletalBlendBone* blend, const GameSkeleton* skeleton, float totalWeight,
    Transform* outPose)
{
    float Weight = (isfinite(totalWeight) && (totalWeight > 0.0f)) ? totalWeight : 0.0f;
    float RestWeight = (Weight < 1.0f) ? (1.0f - Weight) : 0.0f;
    float Divisor = (Weight < 1.0f) ? 1.0f : Weight;
    for (uint32_t Index = 0U; Index < skeleton->_boneCount; Index++)
    {
        const Transform* Rest = &skeleton->_bones[Index]._rest;
        SkeletalBlendBone Bone = blend[Index];
        if (RestWeight > 0.0f)
        {
            Bone.Translation = Vector3Add(Bone.Translation, Vector3Scale(Rest->translation, RestWeight));
            Bone.Scale = Vector3Add(Bone.Scale, Vector3Scale(Rest->scale, RestWeight));
            AddRotation(&Bone.Rotation, Rest->rotation, RestWeight);
        }
        outPose[Index].translation = Vector3Scale(Bone.Translation, 1.0f / Divisor);
        outPose[Index].scale = Vector3Scale(Bone.Scale, 1.0f / Divisor);
        float Length = sqrtf((Bone.Rotation.x * Bone.Rotation.x) + (Bone.Rotation.y * Bone.Rotation.y)
            + (Bone.Rotation.z * Bone.Rotation.z) + (Bone.Rotation.w * Bone.Rotation.w));
        outPose[Index].rotation = (Length > BLEND_MIN_ROTATION_LENGTH)
            ? QuaternionScale(Bone.Rotation, 1.0f / Length) : Rest->rotation;
    }
}

void SkeletalPose_ComputeModelMatrices(const GameSkeleton* skeleton, const Transform* pose, Matrix* outMatrices)
{
    for (uint32_t Step = 0U; Step < skeleton->_boneCount; Step++)
    {
        uint32_t Index = skeleton->_order[Step];
        Matrix Local = SkeletalAnimation_ComposeTransform(pose[Index]);
        int32_t Parent = skeleton->_bones[Index]._parent;
        // The order puts every parent first, so its matrix is ready.
        outMatrices[Index] = (Parent < 0) ? Local : MatrixMultiply(Local, outMatrices[Parent]);
    }
}

void SkeletalPose_ComputePalette(const GameSkeleton* skeleton, const Matrix* modelMatrices, float* outPalette)
{
    for (uint32_t Index = 0U; Index < skeleton->_boneCount; Index++)
    {
        Matrix Skinning = MatrixMultiply(skeleton->_bones[Index]._inverseBind, modelMatrices[Index]);
        float* Rows = outPalette + ((size_t)Index * SKELETAL_PALETTE_FLOATS_PER_BONE);
        Rows[0] = Skinning.m0;
        Rows[1] = Skinning.m4;
        Rows[2] = Skinning.m8;
        Rows[3] = Skinning.m12;
        Rows[4] = Skinning.m1;
        Rows[5] = Skinning.m5;
        Rows[6] = Skinning.m9;
        Rows[7] = Skinning.m13;
        Rows[8] = Skinning.m2;
        Rows[9] = Skinning.m6;
        Rows[10] = Skinning.m10;
        Rows[11] = Skinning.m14;
    }
}

bool SkeletalPose_ComputeBounds(const GameSkeleton* skeleton, const float* palette, BoundingBox* outBounds)
{
    bool HasBounds = false;
    BoundingBox Result = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    for (uint32_t Index = 0U; Index < skeleton->_boneCount; Index++)
    {
        const GameSkeletonBone* Bone = &skeleton->_bones[Index];
        if (!Bone->_hasVertices)
        {
            continue;
        }
        // Arvo's box transform: per row of the skinning matrix (the palette's rows), the translation plus each entry
        // times whichever end of the box makes it smallest (and largest).
        const float* Rows = palette + ((size_t)Index * SKELETAL_PALETTE_FLOATS_PER_BONE);
        float Low[3] = { Bone->_vertexBounds.min.x, Bone->_vertexBounds.min.y, Bone->_vertexBounds.min.z };
        float High[3] = { Bone->_vertexBounds.max.x, Bone->_vertexBounds.max.y, Bone->_vertexBounds.max.z };
        float Minimum[3];
        float Maximum[3];
        for (int Row = 0; Row < 3; Row++)
        {
            Minimum[Row] = Rows[(Row * 4) + 3];
            Maximum[Row] = Rows[(Row * 4) + 3];
            for (int Column = 0; Column < 3; Column++)
            {
                float A = Rows[(Row * 4) + Column] * Low[Column];
                float B = Rows[(Row * 4) + Column] * High[Column];
                Minimum[Row] += Lower(A, B);
                Maximum[Row] += Higher(A, B);
            }
        }
        BoundingBox Moved = { { Minimum[0], Minimum[1], Minimum[2] }, { Maximum[0], Maximum[1], Maximum[2] } };
        if (!HasBounds)
        {
            Result = Moved;
            HasBounds = true;
            continue;
        }
        Result.min = LowerCorner(Result.min, Moved.min);
        Result.max = HigherCorner(Result.max, Moved.max);
    }
    if (skeleton->_hasStaticBounds)
    {
        Result.min = HasBounds ? LowerCorner(Result.min, skeleton->_staticBounds.min) : skeleton->_staticBounds.min;
        Result.max = HasBounds ? HigherCorner(Result.max, skeleton->_staticBounds.max) : skeleton->_staticBounds.max;
        HasBounds = true;
    }
    *outBounds = Result;
    return HasBounds;
}


// Public functions: the mixer.
void SkeletalMixer_Construct(SkeletalMixer* self)
{
    Memory_Zero(self, sizeof(*self));
}

void SkeletalMixer_Play(SkeletalMixer* self, const SkeletalClipRef* clip, float fadeSeconds, bool isRestarted,
    double startTime)
{
    float Fade = SanitizeSeconds(fadeSeconds);
    SkeletalClipState* Played = FindOrAddState(self, clip, startTime);
    if (isRestarted || Played->HasEnded)
    {
        Played->Time = PlaceTime(clip, startTime);
        Played->HasEnded = false;
    }
    Played->Clip = *clip;
    for (uint32_t Index = 0U; Index < self->StateCount; Index++)
    {
        SkeletalClipState* State = &self->States[Index];
        StartFade(State, (State == Played) ? 1.0f : 0.0f, Fade);
    }
    RemoveFadedOut(self);
}

void SkeletalMixer_SetWeight(SkeletalMixer* self, const SkeletalClipRef* clip, float weight, float fadeSeconds,
    double startTime)
{
    float Target = isfinite(weight) ? Clamp(weight, 0.0f, 1.0f) : 0.0f;
    SkeletalClipState* State = FindState(self, clip->Library, clip->Clip);
    if ((State == NULL) && (Target <= 0.0f))
    {
        return;
    }
    if (State == NULL)
    {
        State = FindOrAddState(self, clip, startTime);
    }
    State->Clip = *clip;
    StartFade(State, Target, SanitizeSeconds(fadeSeconds));
    RemoveFadedOut(self);
}

void SkeletalMixer_StopAll(SkeletalMixer* self, float fadeSeconds)
{
    float Fade = SanitizeSeconds(fadeSeconds);
    for (uint32_t Index = 0U; Index < self->StateCount; Index++)
    {
        StartFade(&self->States[Index], 0.0f, Fade);
    }
    RemoveFadedOut(self);
}

void SkeletalMixer_Advance(SkeletalMixer* self, double seconds)
{
    if (!isfinite(seconds) || !(seconds > 0.0))
    {
        return;
    }
    for (uint32_t Index = 0U; Index < self->StateCount; Index++)
    {
        AdvanceTime(&self->States[Index], seconds);
        AdvanceFade(&self->States[Index], (float)seconds);
    }
    RemoveFadedOut(self);
}

const SkeletalClipState* SkeletalMixer_FindClip(const SkeletalMixer* self, uint32_t library, uint32_t clip)
{
    for (uint32_t Index = 0U; Index < self->StateCount; Index++)
    {
        if ((self->States[Index].Clip.Library == library) && (self->States[Index].Clip.Clip == clip))
        {
            return &self->States[Index];
        }
    }
    return NULL;
}

void SkeletalMixer_SamplePose(const SkeletalMixer* self, const SkeletalBinding* bindings, uint32_t bindingCount,
    const GameSkeleton* skeleton, Transform* scratch, SkeletalBlendBone* blend, Transform* outPose)
{
    // One clip at full weight (a clip playing alone, the common case) is its own pose: no blend.
    const SkeletalClipState* Only = NULL;
    uint32_t Contributing = 0U;
    for (uint32_t Index = 0U; Index < self->StateCount; Index++)
    {
        const SkeletalClipState* State = &self->States[Index];
        if ((State->Weight > 0.0f) && (State->Clip.Library < bindingCount))
        {
            Only = State;
            Contributing++;
        }
    }
    if ((Contributing == 1U) && (Only->Weight >= 1.0f))
    {
        SkeletalBinding_SampleClip(&bindings[Only->Clip.Library], Only->Clip.Clip, (float)Only->Time, outPose);
        return;
    }

    uint32_t BoneCount = skeleton->_boneCount;
    SkeletalPose_ClearBlend(blend, BoneCount);
    float TotalWeight = 0.0f;
    for (uint32_t Index = 0U; Index < self->StateCount; Index++)
    {
        const SkeletalClipState* State = &self->States[Index];
        if (!(State->Weight > 0.0f) || (State->Clip.Library >= bindingCount))
        {
            continue;
        }
        SkeletalBinding_SampleClip(&bindings[State->Clip.Library], State->Clip.Clip, (float)State->Time, scratch);
        SkeletalPose_AddToBlend(blend, scratch, BoneCount, State->Weight);
        TotalWeight += State->Weight;
    }
    SkeletalPose_FinishBlend(blend, skeleton, TotalWeight, outPose);
}
