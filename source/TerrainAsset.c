#include "TerrainAsset.h"

/*
 * The terrain asset's data functions (include/TerrainAsset.h). The asset type itself (the definition's parser and the
 * loader) is source/TerrainDefinition.c.
 */


// Public functions.
TerrainSettings TerrainSettings_CreateDefault(void)
{
    return (TerrainSettings)
    {
        .Render =
        {
            .HasOutline = true, .ShadowTier = WorldShadowTier_Both, .CastsShadows = true,
            .DrawDistance = TERRAIN_DEFAULT_DRAW_DISTANCE, .IsDrawDistanceFixed = true
        },
        .Collision =
        {
            .IsEnabled = true, .Layer = CollisionLayer_World, .HasCollidesWith = false,
            .CollidesWith = CollisionLayer_GetDefaultMask(CollisionLayer_World)
        }
    };
}
