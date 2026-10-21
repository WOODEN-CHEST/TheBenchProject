// The world model asset's data functions: the shadow tier's names, the default settings, the variants (lookup by
// name, and applying one to a per-draw copy of a material's maps) and the collision sets (lookup by name). The
// definition and the loader are in WorldModelDefinition.c, the collision section's in WorldModelCollisionDefinition.c.

#include "WorldModel.h"
#include "GameTexture.h"
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
        .IsDrawDistanceFixed = false
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
