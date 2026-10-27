#include "WorldTerrainComponent.h"
#include "WorldInternal.h"
#include "wr/WRMemory.h"

/*
 * The terrain component (include/WorldTerrainComponent.h): its property table, setters and the pure function that
 * resolves its settings over its terrain's defaults. It holds no asset: the renderer and the collision world resolve the
 * terrain by name and build their chunks.
 */


// Macros.
/** The format version of the terrain component's property table. */
#define TERRAIN_TABLE_VERSION 1U
/** The inspector's categories. */
#define CATEGORY_TERRAIN ((const unsigned char*)u8"Terrain")
#define CATEGORY_RENDERING ((const unsigned char*)u8"Rendering")
#define CATEGORY_DRAW_DISTANCE ((const unsigned char*)u8"Draw distance")
#define CATEGORY_COLLISION ((const unsigned char*)u8"Collision")
/** The world layer's default mask (CollisionLayer_GetDefaultMask(CollisionLayer_World)), as a constant for the table. */
#define WORLD_LAYER_DEFAULT_MASK (COLLISION_LAYER_BIT(CollisionLayer_World) | COLLISION_LAYER_BIT(CollisionLayer_Prop) \
    | COLLISION_LAYER_BIT(CollisionLayer_Character) | COLLISION_LAYER_BIT(CollisionLayer_Vehicle) \
    | COLLISION_LAYER_BIT(CollisionLayer_Dynamic))


// Types.
/* The rows of the terrain component's table, so the setters reach their descriptors without a search. */
typedef enum TerrainPropertyEnum
{
    TerrainProperty_Terrain,
    TerrainProperty_OverridesHasOutline,
    TerrainProperty_HasOutline,
    TerrainProperty_OverridesShadowTier,
    TerrainProperty_ShadowTier,
    TerrainProperty_OverridesCastsShadows,
    TerrainProperty_CastsShadows,
    TerrainProperty_OverridesHaze,
    TerrainProperty_Haze,
    TerrainProperty_OverridesDrawDistance,
    TerrainProperty_DrawDistance,
    TerrainProperty_DrawDistanceMultiplier,
    TerrainProperty_OverridesIsDrawDistanceFixed,
    TerrainProperty_IsDrawDistanceFixed,
    TerrainProperty_OverridesIsCollisionEnabled,
    TerrainProperty_IsCollisionEnabled,
    TerrainProperty_OverridesCollisionLayer,
    TerrainProperty_CollisionLayer,
    TerrainProperty_OverridesCollidesWith,
    TerrainProperty_CollidesWith,
    TerrainProperty_Count
} TerrainProperty;


// Fields.
/* Every property, in inspector order: each override flag sits right before the value it switches on. The wire ids are
 * the save schema: never renumber or reuse one; a removed property's id goes into the table's retired list. The haze's
 * pair is 19 and 20, out of the rows' order. The next free wire id is 21. */
static const PropertyDescriptor TerrainProperties[TerrainProperty_Count] =
{
    [TerrainProperty_Terrain] =
    {
        .Key = u8"terrain", .WireId = 1U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(WorldTerrainComponent, _terrain), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(WORLD_TERRAIN_COMPONENT_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_TERRAIN, .DisplayName = u8"Terrain",
        .Description = u8"The terrain to place (its mesh, palette and the defaults below); none places nothing.",
        .SinceVersion = 1U
    },
    [TerrainProperty_OverridesHasOutline] =
    {
        .Key = u8"overrides_has_outline", .WireId = 2U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _overridesHasOutline), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING, .DisplayName = u8"Override outline",
        .Description = u8"Uses this object's outline setting instead of its terrain's.", .SinceVersion = 1U
    },
    [TerrainProperty_HasOutline] =
    {
        .Key = u8"has_outline", .WireId = 3U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _hasOutline), .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_RENDERING, .DisplayName = u8"Outline",
        .Description = u8"Draws a 1-pixel outline around the terrain's silhouettes (while \"Override outline\" is on).",
        .SinceVersion = 1U
    },
    [TerrainProperty_OverridesShadowTier] =
    {
        .Key = u8"overrides_shadow_tier", .WireId = 4U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _overridesShadowTier), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING, .DisplayName = u8"Override shadow range",
        .Description = u8"Uses this object's shadow range instead of its terrain's.", .SinceVersion = 1U
    },
    [TerrainProperty_ShadowTier] =
    {
        .Key = u8"shadow_tier", .WireId = 5U, .Type = PropertyType_Enum,
        PROPERTY_MEMBER(WorldTerrainComponent, _shadowTier), .Default.EnumValue = WorldShadowTier_Both,
        .EnumTable = &WORLD_SHADOW_TIER_ENUM, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING,
        .DisplayName = u8"Shadow range",
        .Description = u8"Which shadows the terrain casts: \"near\" only up close, \"far\" only the coarse "
            u8"long-range one, \"both\" everywhere (while \"Override shadow range\" is on).",
        .SinceVersion = 1U
    },
    [TerrainProperty_OverridesCastsShadows] =
    {
        .Key = u8"overrides_casts_shadows", .WireId = 6U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _overridesCastsShadows), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING, .DisplayName = u8"Override casts shadows",
        .Description = u8"Uses this object's \"Casts shadows\" instead of its terrain's.", .SinceVersion = 1U
    },
    [TerrainProperty_CastsShadows] =
    {
        .Key = u8"casts_shadows", .WireId = 7U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _castsShadows), .Default.BoolValue = true,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING, .DisplayName = u8"Casts shadows",
        .Description = u8"When off, the terrain casts no shadow; it still receives them (while \"Override casts "
            u8"shadows\" is on).",
        .SinceVersion = 1U
    },
    [TerrainProperty_OverridesHaze] =
    {
        .Key = u8"overrides_haze", .WireId = 19U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _overridesHaze), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING, .DisplayName = u8"Override haze",
        .Description = u8"Uses this object's haze instead of its terrain's.", .SinceVersion = 1U
    },
    [TerrainProperty_Haze] =
    {
        .Key = u8"haze", .WireId = 20U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldTerrainComponent, _haze),
        .Default.FloatValue = 1.0f, .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_MODEL_MAX_HAZE),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING, .DisplayName = u8"Haze",
        .Description = u8"How hazy the terrain looks at a distance: it multiplies the haze in front of it, 1 the real "
            u8"air's, 0 as if the air were clear (while \"Override haze\" is on).",
        .SinceVersion = 1U
    },
    [TerrainProperty_OverridesDrawDistance] =
    {
        .Key = u8"overrides_draw_distance", .WireId = 8U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _overridesDrawDistance), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DRAW_DISTANCE, .DisplayName = u8"Override draw distance",
        .Description = u8"Uses this object's draw distance instead of its terrain's.", .SinceVersion = 1U
    },
    [TerrainProperty_DrawDistance] =
    {
        .Key = u8"draw_distance", .WireId = 9U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldTerrainComponent, _drawDistance), .Default.FloatValue = TERRAIN_DEFAULT_DRAW_DISTANCE,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DRAW_DISTANCE,
        .DisplayName = u8"Draw distance",
        .Description = u8"How far each chunk is seen, in metres, before the quality setting and the zoom (while "
            u8"\"Override draw distance\" is on); beyond it the chunk fades out.",
        .SinceVersion = 1U
    },
    [TerrainProperty_DrawDistanceMultiplier] =
    {
        .Key = u8"draw_distance_multiplier", .WireId = 10U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldTerrainComponent, _drawDistanceMultiplier), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DRAW_DISTANCE,
        .DisplayName = u8"Draw distance multiplier",
        .Description = u8"Multiplies the draw distance in effect, the terrain's or this object's: 2 is seen twice "
            u8"as far.",
        .SinceVersion = 1U
    },
    [TerrainProperty_OverridesIsDrawDistanceFixed] =
    {
        .Key = u8"overrides_is_draw_distance_fixed", .WireId = 11U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _overridesIsDrawDistanceFixed), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DRAW_DISTANCE,
        .DisplayName = u8"Override fixed draw distance",
        .Description = u8"Uses this object's \"Fixed draw distance\" instead of its terrain's.", .SinceVersion = 1U
    },
    [TerrainProperty_IsDrawDistanceFixed] =
    {
        .Key = u8"is_draw_distance_fixed", .WireId = 12U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _isDrawDistanceFixed), .Default.BoolValue = true,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DRAW_DISTANCE, .DisplayName = u8"Fixed draw distance",
        .Description = u8"Keeps the draw distance as it is when the player lowers or raises the draw distance quality "
            u8"setting, so hills never vanish (while \"Override fixed draw distance\" is on).",
        .SinceVersion = 1U
    },
    [TerrainProperty_OverridesIsCollisionEnabled] =
    {
        .Key = u8"overrides_is_collision_enabled", .WireId = 13U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _overridesIsCollisionEnabled), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_COLLISION, .DisplayName = u8"Override collides",
        .Description = u8"Uses this object's \"Collides\" instead of its terrain's.", .SinceVersion = 1U
    },
    [TerrainProperty_IsCollisionEnabled] =
    {
        .Key = u8"is_collision_enabled", .WireId = 14U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _isCollisionEnabled), .Default.BoolValue = true,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_COLLISION, .DisplayName = u8"Collides",
        .Description = u8"When off, nothing stands on or bumps into the terrain (while \"Override collides\" is on).",
        .SinceVersion = 1U
    },
    [TerrainProperty_OverridesCollisionLayer] =
    {
        .Key = u8"overrides_collision_layer", .WireId = 15U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _overridesCollisionLayer), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_COLLISION, .DisplayName = u8"Override layer",
        .Description = u8"Uses this object's collision layer instead of its terrain's.", .SinceVersion = 1U
    },
    [TerrainProperty_CollisionLayer] =
    {
        .Key = u8"collision_layer", .WireId = 16U, .Type = PropertyType_Enum,
        PROPERTY_MEMBER(WorldTerrainComponent, _collisionLayer), .Default.EnumValue = CollisionLayer_World,
        .EnumTable = &COLLISION_LAYER_ENUM, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_COLLISION,
        .DisplayName = u8"Layer",
        .Description = u8"The collision layer of the terrain's chunks (while \"Override layer\" is on).",
        .SinceVersion = 1U
    },
    [TerrainProperty_OverridesCollidesWith] =
    {
        .Key = u8"overrides_collides_with", .WireId = 17U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldTerrainComponent, _overridesCollidesWith), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_COLLISION, .DisplayName = u8"Own mask",
        .Description = u8"Uses the layers below instead of the terrain's mask (or the layer's default).",
        .SinceVersion = 1U
    },
    [TerrainProperty_CollidesWith] =
    {
        .Key = u8"collides_with", .WireId = 18U, .Type = PropertyType_EnumSet,
        PROPERTY_MEMBER(WorldTerrainComponent, _collidesWith), .Default.EnumSetValue = WORLD_LAYER_DEFAULT_MASK,
        .EnumTable = &COLLISION_LAYER_ENUM, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_COLLISION,
        .DisplayName = u8"Collides with",
        .Description = u8"The layers the terrain blocks and is blocked by (both sides must list each other's).",
        .SinceVersion = 1U
    },
};

static const PropertyTable TerrainTable =
{
    .Name = u8"terrain_component",
    .Version = TERRAIN_TABLE_VERSION,
    .StructSize = sizeof(WorldTerrainComponent),
    .Descriptors = TerrainProperties,
    .DescriptorCount = TerrainProperty_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = NULL
};


// Static functions.
static Error ConstructTerrain(WorldComponent* component)
{
    return Properties_Construct(&TerrainTable, component);
}

static void DeconstructTerrain(WorldComponent* component)
{
    Properties_Deconstruct(&TerrainTable, component);
}

/* Checks @p value against one row's rules (so the setters' rules live in the table only). */
static Error CheckProperty(TerrainProperty property, PropertyValue value)
{
    return Properties_CheckValue(&TerrainTable, &TerrainProperties[property], &value);
}

static Error CreateNullSelfError(const char* function)
{
    return Error_Construct4(ErrorCode_IllegalArgument, "%s: self must not be NULL.", function);
}

static void SetBool(WorldTerrainComponent* self, bool* field, bool value, TerrainProperty property)
{
    *field = value;
    WorldInternal_ComponentAfterSet(self, &TerrainProperties[property]);
}

/* A setting's override flag and its row; false for a value outside the enum. */
static bool TryGetOverrideFlag(WorldTerrainComponent* self, WorldTerrainSetting setting, bool** outFlag,
    TerrainProperty* outProperty)
{
    switch (setting)
    {
        case WorldTerrainSetting_HasOutline:
            *outFlag = &self->_overridesHasOutline;
            *outProperty = TerrainProperty_OverridesHasOutline;
            return true;
        case WorldTerrainSetting_ShadowTier:
            *outFlag = &self->_overridesShadowTier;
            *outProperty = TerrainProperty_OverridesShadowTier;
            return true;
        case WorldTerrainSetting_CastsShadows:
            *outFlag = &self->_overridesCastsShadows;
            *outProperty = TerrainProperty_OverridesCastsShadows;
            return true;
        case WorldTerrainSetting_DrawDistance:
            *outFlag = &self->_overridesDrawDistance;
            *outProperty = TerrainProperty_OverridesDrawDistance;
            return true;
        case WorldTerrainSetting_IsDrawDistanceFixed:
            *outFlag = &self->_overridesIsDrawDistanceFixed;
            *outProperty = TerrainProperty_OverridesIsDrawDistanceFixed;
            return true;
        case WorldTerrainSetting_IsCollisionEnabled:
            *outFlag = &self->_overridesIsCollisionEnabled;
            *outProperty = TerrainProperty_OverridesIsCollisionEnabled;
            return true;
        case WorldTerrainSetting_CollisionLayer:
            *outFlag = &self->_overridesCollisionLayer;
            *outProperty = TerrainProperty_OverridesCollisionLayer;
            return true;
        case WorldTerrainSetting_CollidesWith:
            *outFlag = &self->_overridesCollidesWith;
            *outProperty = TerrainProperty_OverridesCollidesWith;
            return true;
        case WorldTerrainSetting_Haze:
            *outFlag = &self->_overridesHaze;
            *outProperty = TerrainProperty_OverridesHaze;
            return true;
        case WorldTerrainSetting_Count:
            break;
    }
    return false;
}


// Fields: the registry entry (it refers to the functions above).
static const WorldComponentTypeInfo TerrainTypeInfo =
{
    .Type = WorldComponentType_Terrain,
    .Name = u8"terrain",
    .Size = sizeof(WorldTerrainComponent),
    .Table = &TerrainTable,
    .IsTransformDriver = false,
    .Construct = ConstructTerrain,
    .Deconstruct = DeconstructTerrain,
    .OnAttach = NULL,
    .OnDetach = NULL,
    .OnPropertySet = NULL
};


// Public functions.
const WorldComponentTypeInfo* WorldTerrainComponent_GetTypeInfo(void)
{
    return &TerrainTypeInfo;
}

const PropertyTable* WorldTerrainComponent_GetPropertyTable(void)
{
    return &TerrainTable;
}

Error WorldTerrainComponent_SetTerrain(WorldTerrainComponent* self, const unsigned char* terrain)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetTerrain");
    }
    Error Result = CheckProperty(TerrainProperty_Terrain, PropertyValue_FromAssetName(terrain));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    unsigned char* Clone = WorldInternal_CloneString(terrain);
    Memory_Free(self->_terrain);
    self->_terrain = Clone;
    WorldInternal_ComponentAfterSet(self, &TerrainProperties[TerrainProperty_Terrain]);
    return Error_CreateSuccess();
}

bool WorldTerrainComponent_IsOverridden(const WorldTerrainComponent* self, WorldTerrainSetting setting)
{
    switch (setting)
    {
        case WorldTerrainSetting_HasOutline:          return self->_overridesHasOutline;
        case WorldTerrainSetting_ShadowTier:          return self->_overridesShadowTier;
        case WorldTerrainSetting_CastsShadows:        return self->_overridesCastsShadows;
        case WorldTerrainSetting_DrawDistance:        return self->_overridesDrawDistance;
        case WorldTerrainSetting_IsDrawDistanceFixed: return self->_overridesIsDrawDistanceFixed;
        case WorldTerrainSetting_IsCollisionEnabled:  return self->_overridesIsCollisionEnabled;
        case WorldTerrainSetting_CollisionLayer:      return self->_overridesCollisionLayer;
        case WorldTerrainSetting_CollidesWith:        return self->_overridesCollidesWith;
        case WorldTerrainSetting_Haze:                return self->_overridesHaze;
        case WorldTerrainSetting_Count:               break;
    }
    // Only reachable with a value outside the enum: nothing is overridden.
    return false;
}

Error WorldTerrainComponent_SetOverridden(WorldTerrainComponent* self, WorldTerrainSetting setting, bool isOverridden)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetOverridden");
    }
    bool* Flag = NULL;
    TerrainProperty Property = TerrainProperty_Count;
    if (!TryGetOverrideFlag(self, setting, &Flag, &Property))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "WorldTerrainComponent_SetOverridden: %d is not a WorldTerrainSetting.", (int)setting);
    }
    SetBool(self, Flag, isOverridden, Property);
    return Error_CreateSuccess();
}

Error WorldTerrainComponent_SetHasOutline(WorldTerrainComponent* self, bool hasOutline)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetHasOutline");
    }
    SetBool(self, &self->_hasOutline, hasOutline, TerrainProperty_HasOutline);
    return Error_CreateSuccess();
}

Error WorldTerrainComponent_SetShadowTier(WorldTerrainComponent* self, WorldShadowTier shadowTier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetShadowTier");
    }
    Error Result = CheckProperty(TerrainProperty_ShadowTier, PropertyValue_FromEnum((int32_t)shadowTier));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_shadowTier = shadowTier;
    WorldInternal_ComponentAfterSet(self, &TerrainProperties[TerrainProperty_ShadowTier]);
    return Error_CreateSuccess();
}

Error WorldTerrainComponent_SetCastsShadows(WorldTerrainComponent* self, bool castsShadows)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetCastsShadows");
    }
    SetBool(self, &self->_castsShadows, castsShadows, TerrainProperty_CastsShadows);
    return Error_CreateSuccess();
}

Error WorldTerrainComponent_SetHaze(WorldTerrainComponent* self, float haze)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetHaze");
    }
    Error Result = CheckProperty(TerrainProperty_Haze, PropertyValue_FromFloat(haze));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_haze = haze;
    WorldInternal_ComponentAfterSet(self, &TerrainProperties[TerrainProperty_Haze]);
    return Error_CreateSuccess();
}

Error WorldTerrainComponent_SetDrawDistance(WorldTerrainComponent* self, float drawDistance)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetDrawDistance");
    }
    Error Result = CheckProperty(TerrainProperty_DrawDistance, PropertyValue_FromFloat(drawDistance));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_drawDistance = drawDistance;
    WorldInternal_ComponentAfterSet(self, &TerrainProperties[TerrainProperty_DrawDistance]);
    return Error_CreateSuccess();
}

Error WorldTerrainComponent_SetDrawDistanceMultiplier(WorldTerrainComponent* self, float multiplier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetDrawDistanceMultiplier");
    }
    Error Result = CheckProperty(TerrainProperty_DrawDistanceMultiplier, PropertyValue_FromFloat(multiplier));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_drawDistanceMultiplier = multiplier;
    WorldInternal_ComponentAfterSet(self, &TerrainProperties[TerrainProperty_DrawDistanceMultiplier]);
    return Error_CreateSuccess();
}

Error WorldTerrainComponent_SetIsDrawDistanceFixed(WorldTerrainComponent* self, bool isFixed)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetIsDrawDistanceFixed");
    }
    SetBool(self, &self->_isDrawDistanceFixed, isFixed, TerrainProperty_IsDrawDistanceFixed);
    return Error_CreateSuccess();
}

Error WorldTerrainComponent_SetIsCollisionEnabled(WorldTerrainComponent* self, bool isEnabled)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetIsCollisionEnabled");
    }
    SetBool(self, &self->_isCollisionEnabled, isEnabled, TerrainProperty_IsCollisionEnabled);
    return Error_CreateSuccess();
}

Error WorldTerrainComponent_SetCollisionLayer(WorldTerrainComponent* self, CollisionLayer layer)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetCollisionLayer");
    }
    Error Result = CheckProperty(TerrainProperty_CollisionLayer, PropertyValue_FromEnum((int32_t)layer));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_collisionLayer = layer;
    WorldInternal_ComponentAfterSet(self, &TerrainProperties[TerrainProperty_CollisionLayer]);
    return Error_CreateSuccess();
}

Error WorldTerrainComponent_SetCollidesWith(WorldTerrainComponent* self, uint32_t mask)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldTerrainComponent_SetCollidesWith");
    }
    Error Result = CheckProperty(TerrainProperty_CollidesWith, PropertyValue_FromEnumSet(mask));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_collidesWith = mask;
    WorldInternal_ComponentAfterSet(self, &TerrainProperties[TerrainProperty_CollidesWith]);
    return Error_CreateSuccess();
}

TerrainSettings WorldTerrainComponent_ResolveSettings(const WorldTerrainComponent* self,
    const TerrainSettings* assetSettings)
{
    TerrainSettings Settings = *assetSettings;
    if (self->_overridesHasOutline)
    {
        Settings.Render.HasOutline = self->_hasOutline;
    }
    if (self->_overridesShadowTier)
    {
        Settings.Render.ShadowTier = self->_shadowTier;
    }
    if (self->_overridesCastsShadows)
    {
        Settings.Render.CastsShadows = self->_castsShadows;
    }
    if (self->_overridesHaze)
    {
        Settings.Render.Haze = self->_haze;
    }
    if (self->_overridesDrawDistance)
    {
        Settings.Render.DrawDistance = self->_drawDistance;
    }
    Settings.Render.DrawDistance *= self->_drawDistanceMultiplier;
    if (self->_overridesIsDrawDistanceFixed)
    {
        Settings.Render.IsDrawDistanceFixed = self->_isDrawDistanceFixed;
    }
    if (self->_overridesIsCollisionEnabled)
    {
        Settings.Collision.IsEnabled = self->_isCollisionEnabled;
    }
    if (self->_overridesCollisionLayer)
    {
        Settings.Collision.Layer = self->_collisionLayer;
    }
    if (self->_overridesCollidesWith)
    {
        Settings.Collision.CollidesWith = self->_collidesWith;
    }
    else if (!assetSettings->Collision.HasCollidesWith)
    {
        // As a world model's collision: a mask the asset leaves out is the default mask of the layer in effect.
        Settings.Collision.CollidesWith = CollisionLayer_GetDefaultMask(Settings.Collision.Layer);
    }
    Settings.Collision.HasCollidesWith = true;
    return Settings;
}
