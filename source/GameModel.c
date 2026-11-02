#include "GameModel.h"


// Public functions.
void GameMaterialFactors_Apply(const GameMaterialFactors* self, bool hasEmissiveMap, Material* material)
{
    MaterialMap* Maps = material->maps;
    if (self->HasTint)
    {
        Maps[MATERIAL_MAP_ALBEDO].color = self->Tint;
    }
    if (self->HasMetallic)
    {
        Maps[MATERIAL_MAP_METALNESS].value = self->Metallic;
    }
    if (self->HasRoughness)
    {
        Maps[MATERIAL_MAP_ROUGHNESS].value = self->Roughness;
    }

    // An emissive MAP with no explicit colour or intensity implies white x 1, so the map is actually visible (otherwise
    // the default intensity 0 would suppress it).
    if (self->HasEmissiveColor)
    {
        Maps[MATERIAL_MAP_EMISSION].color = self->EmissiveColor;
    }
    else if (hasEmissiveMap)
    {
        Maps[MATERIAL_MAP_EMISSION].color = WHITE;
    }
    if (self->HasEmissiveIntensity)
    {
        Maps[MATERIAL_MAP_EMISSION].value = self->EmissiveIntensity;
    }
    else if (hasEmissiveMap)
    {
        Maps[MATERIAL_MAP_EMISSION].value = 1.0f;
    }
    if (self->HasPorosity)
    {
        material->params[GAME_MATERIAL_PARAM_POROSITY] = self->Porosity;
    }
    if (self->HasPuddles)
    {
        material->params[GAME_MATERIAL_PARAM_PUDDLES] = self->Puddles;
    }
}

void GameMaterialParams_SetDefaults(Material* material)
{
    material->params[GAME_MATERIAL_PARAM_POROSITY] = GAME_MATERIAL_DEFAULT_POROSITY;
    material->params[GAME_MATERIAL_PARAM_PUDDLES] = GAME_MATERIAL_DEFAULT_PUDDLES;
}
