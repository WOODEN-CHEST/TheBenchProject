#include "GameModel.h"


// Public functions.
void GameMaterialFactors_Apply(const GameMaterialFactors* self, bool hasEmissiveMap, MaterialMap* maps)
{
    if (self->HasTint)
    {
        maps[MATERIAL_MAP_ALBEDO].color = self->Tint;
    }
    if (self->HasMetallic)
    {
        maps[MATERIAL_MAP_METALNESS].value = self->Metallic;
    }
    if (self->HasRoughness)
    {
        maps[MATERIAL_MAP_ROUGHNESS].value = self->Roughness;
    }

    // An emissive MAP with no explicit colour or intensity implies white x 1, so the map is actually visible (otherwise
    // the default intensity 0 would suppress it).
    if (self->HasEmissiveColor)
    {
        maps[MATERIAL_MAP_EMISSION].color = self->EmissiveColor;
    }
    else if (hasEmissiveMap)
    {
        maps[MATERIAL_MAP_EMISSION].color = WHITE;
    }
    if (self->HasEmissiveIntensity)
    {
        maps[MATERIAL_MAP_EMISSION].value = self->EmissiveIntensity;
    }
    else if (hasEmissiveMap)
    {
        maps[MATERIAL_MAP_EMISSION].value = 1.0f;
    }
}
