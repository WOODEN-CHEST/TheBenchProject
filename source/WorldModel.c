// The world model asset's data functions: the shadow tier's names, the default settings, the variants (lookup by
// name, and applying one to a per-draw copy of a material's maps), the collision sets (lookup by name) and the sockets
// (lookup by name, a socket's frame in a pose; T30). The definition and the loader are in WorldModelDefinition.c, the
// collision section's in WorldModelCollisionDefinition.c.

#include "WorldModel.h"
#include "GameSkeleton.h"
#include "GameTexture.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Fields.
static const PropertyEnumEntry ShadowTierEntries[] =
{
    { .Name = u8"both", .Value = WorldShadowTier_Both },
    { .Name = u8"near", .Value = WorldShadowTier_Near },
    { .Name = u8"far", .Value = WorldShadowTier_Far },
};

const PropertyEnumTable WORLD_SHADOW_TIER_ENUM =
{
    .Name = u8"shadow_tier", .Entries = ShadowTierEntries,
    .EntryCount = sizeof(ShadowTierEntries) / sizeof(ShadowTierEntries[0])
};


// Static functions.
/* A transform's turn and place without its scale: its X axis and its Y axis made unit length and square to it, Z their
 * cross product (a mirrored transform's mirror is dropped too), the translation kept. Axes too short to have a direction
 * leave the turn out. */
static Matrix MakeRigid(Matrix transform)
{
    Vector3 X = { transform.m0, transform.m1, transform.m2 };
    Vector3 Y = { transform.m4, transform.m5, transform.m6 };
    Matrix Rigid = MatrixTranslate(transform.m12, transform.m13, transform.m14);
    float LengthX = Vector3Length(X);
    if (!(LengthX > 1e-12f))
    {
        return Rigid;
    }
    X = Vector3Scale(X, 1.0f / LengthX);
    Y = Vector3Subtract(Y, Vector3Scale(X, Vector3DotProduct(Y, X)));
    float LengthY = Vector3Length(Y);
    if (!(LengthY > 1e-12f))
    {
        return Rigid;
    }
    Y = Vector3Scale(Y, 1.0f / LengthY);
    Vector3 Z = Vector3CrossProduct(X, Y);
    Rigid.m0 = X.x;
    Rigid.m1 = X.y;
    Rigid.m2 = X.z;
    Rigid.m4 = Y.x;
    Rigid.m5 = Y.y;
    Rigid.m6 = Y.z;
    Rigid.m8 = Z.x;
    Rigid.m9 = Z.y;
    Rigid.m10 = Z.z;
    return Rigid;
}

/* Replaces a map's texture with a texture asset's, when the override names one. */
static void ApplyTexture(const GameTexture* texture, MaterialMap* map)
{
    if (texture != NULL)
    {
        map->texture = GameTexture_GetRaylibTexture(texture);
    }
}


// Public functions.
WorldModelSettings WorldModelSettings_CreateDefault(void)
{
    return (WorldModelSettings)
    {
        .HasOutline = true,
        .OmitsPixelation = false,
        .ShadowTier = WorldShadowTier_Both,
        .CastsShadows = true,
        .DrawDistance = WORLD_MODEL_DEFAULT_DRAW_DISTANCE,
        .IsDrawDistanceFixed = false,
        .Haze = 1.0f
    };
}

bool WorldModel_TryFindVariant(const WorldModel* self, const unsigned char* name, size_t* outIndex)
{
    if (name == NULL)
    {
        return false;
    }
    for (size_t Index = 0U; Index < self->_variantCount; Index++)
    {
        bool IsEqual = false;
        Error Result = StringUTF8_EqualsExact(self->_variants[Index]._name, name, &IsEqual);
        Error_Deconstruct(&Result);
        if (IsEqual)
        {
            *outIndex = Index;
            return true;
        }
    }
    return false;
}

bool WorldModelCollision_TryFindSet(const WorldModelCollision* self, const unsigned char* name, uint32_t* outIndex)
{
    if (name == NULL)
    {
        return false;
    }
    for (uint32_t Index = 0U; Index < self->_setCount; Index++)
    {
        bool IsEqual = false;
        Error Result = StringUTF8_EqualsExact(self->_sets[Index]._name, name, &IsEqual);
        Error_Deconstruct(&Result);
        if (IsEqual)
        {
            *outIndex = Index;
            return true;
        }
    }
    return false;
}

void WorldModel_ApplyVariant(const WorldModel* self, size_t variantIndex, int materialIndex, MaterialMap* maps)
{
    if (variantIndex >= self->_variantCount)
    {
        return;
    }
    const WorldModelVariant* Variant = &self->_variants[variantIndex];
    for (size_t Index = 0U; Index < Variant->_overrideCount; Index++)
    {
        const WorldModelMaterialOverride* Override = &Variant->_overrides[Index];
        if (Override->_slot != materialIndex)
        {
            continue;
        }
        ApplyTexture(Override->_albedo, &maps[MATERIAL_MAP_ALBEDO]);
        ApplyTexture(Override->_normal, &maps[MATERIAL_MAP_NORMAL]);
        // The metalness slot carries the packed occlusion/roughness/metallic map, as in the model type.
        ApplyTexture(Override->_mra, &maps[MATERIAL_MAP_METALNESS]);
        ApplyTexture(Override->_emissive, &maps[MATERIAL_MAP_EMISSION]);
        GameMaterialFactors_Apply(&Override->_factors, Override->_emissive != NULL, maps);
        return; // one override per slot (the definition refuses a slot twice)
    }
}

bool WorldModel_TryFindSocket(const WorldModel* self, const unsigned char* name, uint32_t* outIndex)
{
    if (name == NULL)
    {
        return false;
    }
    size_t Length = StringUTF8_GetByteLength(name);
    for (uint32_t Index = 0U; Index < self->_socketCount; Index++)
    {
        const unsigned char* Candidate = self->_sockets[Index]._name;
        if ((StringUTF8_GetByteLength(Candidate) == Length) && Memory_IsEqual(Candidate, name, Length))
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

bool WorldModel_GetFirstPerson(const WorldModel* self, uint32_t* outEyesSocket, const uint32_t** outHiddenBones,
    uint32_t* outHiddenBoneCount)
{
    if (outEyesSocket != NULL)
    {
        *outEyesSocket = self->_eyesSocket;
    }
    if (outHiddenBones != NULL)
    {
        *outHiddenBones = self->_hiddenBones;
    }
    if (outHiddenBoneCount != NULL)
    {
        *outHiddenBoneCount = self->_hiddenBoneCount;
    }
    return self->_hasFirstPerson;
}

Matrix WorldModel_GetSocketMatrix(const WorldModel* self, uint32_t socket, const Matrix* boneMatrices)
{
    const WorldModelSocket* Socket = &self->_sockets[socket];
    if (!Socket->_hasBone || (self->_skeleton == NULL))
    {
        return Socket->_offset;
    }
    // The bone's frame in the object's own space (its space to mesh space: the pose's matrix, or the bind matrix; then
    // the skeleton LOD's import transform, mesh space to the object's), made rigid: a bone or an import may scale (a
    // model authored in centimetres), and the offset is in metres, what hangs on the socket keeps its own size.
    const GameSkeletonBone* Bone = GameSkeleton_GetBone(self->_skeleton, Socket->_bone);
    Matrix BoneMatrix = (boneMatrices != NULL) ? boneMatrices[Socket->_bone] : Bone->_bind;
    Matrix Import = GameModel_GetRaylibModel(self->_lods[self->_skeletonLod]._model).transform;
    return MatrixMultiply(Socket->_offset, MakeRigid(MatrixMultiply(BoneMatrix, Import)));
}
