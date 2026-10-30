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
            .HasOutline = true, .CastsShadows = true, .HasShadowDistance = false,
            .ShadowDistance = TERRAIN_DEFAULT_SHADOW_DISTANCE, .IsShadowDistanceFixed = false,
            .DrawDistance = TERRAIN_DEFAULT_DRAW_DISTANCE, .IsDrawDistanceFixed = true, .Haze = 1.0f
        },
        .Collision =
        {
            .IsEnabled = true, .Layer = CollisionLayer_World, .HasCollidesWith = false,
            .CollidesWith = CollisionLayer_GetDefaultMask(CollisionLayer_World)
        }
    };
}
