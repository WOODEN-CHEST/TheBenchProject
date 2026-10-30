#include "WorldModelComponent.h"
#include "WorldInternal.h"
#include "WorldModelComponentInternal.h"
#include "Logger.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** The key names of the shadow tier's pair in version 1 of the placement tables, and of the shadow distance's pair. */
#define LEGACY_SHADOW_TIER_KEY ((const unsigned char*)u8"shadow_tier")
#define LEGACY_OVERRIDES_SHADOW_TIER_KEY ((const unsigned char*)u8"overrides_shadow_tier")
#define SHADOW_DISTANCE_KEY ((const unsigned char*)u8"shadow_distance")
#define OVERRIDES_SHADOW_DISTANCE_KEY ((const unsigned char*)u8"overrides_shadow_distance")
/** The table version older files hold the shadow tier in. */
#define LEGACY_SHADOW_TIER_VERSION 1U


// Types.
/* The rows of the model component's table, so the setters reach their descriptors without a search. */
typedef enum ModelPropertyEnum
{
    ModelProperty_WorldModel,
    ModelProperty_UsesVariant,
    ModelProperty_Variant,
    ModelProperty_OverridesHasOutline,
    ModelProperty_HasOutline,
    ModelProperty_OverridesOmitPixelation,
    ModelProperty_OmitPixelation,
    ModelProperty_OverridesCastsShadows,
    ModelProperty_CastsShadows,
    ModelProperty_OverridesShadowDistance,
    ModelProperty_ShadowDistance,
    ModelProperty_ShadowDistanceMultiplier,
    ModelProperty_OverridesIsShadowDistanceFixed,
    ModelProperty_IsShadowDistanceFixed,
    ModelProperty_OverridesIsShadowOnly,
    ModelProperty_IsShadowOnly,
    ModelProperty_OverridesHasTwoSidedShadows,
    ModelProperty_HasTwoSidedShadows,
    ModelProperty_OverridesShadowLod,
    ModelProperty_ShadowLod,
    ModelProperty_OverridesHaze,
    ModelProperty_Haze,
    ModelProperty_OverridesOccludesSky,
    ModelProperty_OccludesSky,
    ModelProperty_OverridesDrawDistance,
    ModelProperty_DrawDistance,
    ModelProperty_DrawDistanceMultiplier,
    ModelProperty_OverridesIsDrawDistanceFixed,
    ModelProperty_IsDrawDistanceFixed,
    ModelProperty_OverridesLodDistances,
    ModelProperty_LodDistances,
    ModelProperty_LodDistanceMultiplier,
    ModelProperty_IsLodForced,
    ModelProperty_ForcedLod,
    ModelProperty_Count
} ModelProperty;

/* The shadow tier version 1 of the placement tables held, by its saved values (JSON wrote their names). */
typedef enum LegacyShadowTierEnum
{
    LegacyShadowTier_Both = 0,
    LegacyShadowTier_Near = 1,
    LegacyShadowTier_Far = 2,
    LegacyShadowTier_Count = 3
} LegacyShadowTier;


// Fields.
/* The shadow tier's names, by its values. */
static const char* const LegacyShadowTierNames[LegacyShadowTier_Count] = { "both", "near", "far" };

/* Every property, in inspector order: each override flag sits right before the value it switches on. The rows' keys,
 * types, defaults, rules and texts are shared with the model instances component
 * (source/WorldModelComponentInternal.h). The wire ids are the save schema: never renumber or reuse one; a removed
 * property's id goes into the table's retired list. They do not follow the rows' order: 1 is world_model, 2, 3 and 5
 * the placement's own values behind the override flags 8, 9 and 11, 17 to 21 the LOD rows, 23 and 24 the haze's pair,
 * 25 and 26 the sky's and 27 to 37 the shadow rows, as in the model instances component, whose list is 22. 4 and 10,
 * the shadow tier's pair, are retired. The next free wire id is 38. */
static const PropertyDescriptor ModelProperties[ModelProperty_Count] =
{
    [ModelProperty_WorldModel] =
    {
        WORLD_MODEL_PLACEMENT_ROW_WORLD_MODEL, .WireId = 1U, PROPERTY_MEMBER(WorldModelComponent, _worldModel),
        .SinceVersion = 1U
    },
    [ModelProperty_UsesVariant] =
    {
        WORLD_MODEL_PLACEMENT_ROW_USES_VARIANT, .WireId = 6U, PROPERTY_MEMBER(WorldModelComponent, _usesVariant),
        .SinceVersion = 1U
    },
    [ModelProperty_Variant] =
    {
        WORLD_MODEL_PLACEMENT_ROW_VARIANT, .WireId = 7U, PROPERTY_MEMBER(WorldModelComponent, _variant),
        .SinceVersion = 1U
    },
    [ModelProperty_OverridesHasOutline] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAS_OUTLINE, .WireId = 8U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesHasOutline), .SinceVersion = 1U
    },
    [ModelProperty_HasOutline] =
    {
        WORLD_MODEL_PLACEMENT_ROW_HAS_OUTLINE, .WireId = 2U, PROPERTY_MEMBER(WorldModelComponent, _hasOutline),
        .SinceVersion = 1U
    },
    [ModelProperty_OverridesOmitPixelation] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_OMIT_PIXELATION, .WireId = 9U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesOmitPixelation), .SinceVersion = 1U
    },
    [ModelProperty_OmitPixelation] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OMIT_PIXELATION, .WireId = 3U, PROPERTY_MEMBER(WorldModelComponent, _omitPixelation),
        .SinceVersion = 1U
    },
    [ModelProperty_OverridesCastsShadows] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_CASTS_SHADOWS, .WireId = 11U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesCastsShadows), .SinceVersion = 1U
    },
    [ModelProperty_CastsShadows] =
    {
        WORLD_MODEL_PLACEMENT_ROW_CASTS_SHADOWS, .WireId = 5U, PROPERTY_MEMBER(WorldModelComponent, _castsShadows),
        .SinceVersion = 1U
    },
    [ModelProperty_OverridesShadowDistance] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_SHADOW_DISTANCE, .WireId = WORLD_MODEL_PLACEMENT_WIRE_OVERRIDES_SHADOW_DISTANCE,
        PROPERTY_MEMBER(WorldModelComponent, _overridesShadowDistance), .SinceVersion = 2U
    },
    [ModelProperty_ShadowDistance] =
    {
        WORLD_MODEL_PLACEMENT_ROW_SHADOW_DISTANCE, .WireId = WORLD_MODEL_PLACEMENT_WIRE_SHADOW_DISTANCE,
        PROPERTY_MEMBER(WorldModelComponent, _shadowDistance), .SinceVersion = 2U
    },
    [ModelProperty_ShadowDistanceMultiplier] =
    {
        WORLD_MODEL_PLACEMENT_ROW_SHADOW_DISTANCE_MULTIPLIER, .WireId = 29U,
        PROPERTY_MEMBER(WorldModelComponent, _shadowDistanceMultiplier), .SinceVersion = 2U
    },
    [ModelProperty_OverridesIsShadowDistanceFixed] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_IS_SHADOW_DISTANCE_FIXED, .WireId = 30U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesIsShadowDistanceFixed), .SinceVersion = 2U
    },
    [ModelProperty_IsShadowDistanceFixed] =
    {
        WORLD_MODEL_PLACEMENT_ROW_IS_SHADOW_DISTANCE_FIXED, .WireId = 31U,
        PROPERTY_MEMBER(WorldModelComponent, _isShadowDistanceFixed), .SinceVersion = 2U
    },
    [ModelProperty_OverridesIsShadowOnly] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_IS_SHADOW_ONLY, .WireId = 32U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesIsShadowOnly), .SinceVersion = 2U
    },
    [ModelProperty_IsShadowOnly] =
    {
        WORLD_MODEL_PLACEMENT_ROW_IS_SHADOW_ONLY, .WireId = 33U, PROPERTY_MEMBER(WorldModelComponent, _isShadowOnly),
        .SinceVersion = 2U
    },
    [ModelProperty_OverridesHasTwoSidedShadows] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAS_TWO_SIDED_SHADOWS, .WireId = 34U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesHasTwoSidedShadows), .SinceVersion = 2U
    },
    [ModelProperty_HasTwoSidedShadows] =
    {
        WORLD_MODEL_PLACEMENT_ROW_HAS_TWO_SIDED_SHADOWS, .WireId = 35U,
        PROPERTY_MEMBER(WorldModelComponent, _hasTwoSidedShadows), .SinceVersion = 2U
    },
    [ModelProperty_OverridesShadowLod] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_SHADOW_LOD, .WireId = 36U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesShadowLod), .SinceVersion = 2U
    },
    [ModelProperty_ShadowLod] =
    {
        WORLD_MODEL_PLACEMENT_ROW_SHADOW_LOD, .WireId = 37U, PROPERTY_MEMBER(WorldModelComponent, _shadowLod),
        .SinceVersion = 2U
    },
    [ModelProperty_OverridesHaze] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAZE, .WireId = 23U, PROPERTY_MEMBER(WorldModelComponent, _overridesHaze),
        .SinceVersion = 1U
    },
    [ModelProperty_Haze] =
    {
        WORLD_MODEL_PLACEMENT_ROW_HAZE, .WireId = 24U, PROPERTY_MEMBER(WorldModelComponent, _haze), .SinceVersion = 1U
    },
    [ModelProperty_OverridesOccludesSky] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_OCCLUDES_SKY, .WireId = 25U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesOccludesSky), .SinceVersion = 1U
    },
    [ModelProperty_OccludesSky] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OCCLUDES_SKY, .WireId = 26U, PROPERTY_MEMBER(WorldModelComponent, _occludesSky),
        .SinceVersion = 1U
    },
    [ModelProperty_OverridesDrawDistance] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_DRAW_DISTANCE, .WireId = 12U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesDrawDistance), .SinceVersion = 1U
    },
    [ModelProperty_DrawDistance] =
    {
        WORLD_MODEL_PLACEMENT_ROW_DRAW_DISTANCE, .WireId = 13U, PROPERTY_MEMBER(WorldModelComponent, _drawDistance),
        .SinceVersion = 1U
    },
    [ModelProperty_DrawDistanceMultiplier] =
    {
        WORLD_MODEL_PLACEMENT_ROW_DRAW_DISTANCE_MULTIPLIER, .WireId = 14U,
        PROPERTY_MEMBER(WorldModelComponent, _drawDistanceMultiplier), .SinceVersion = 1U
    },
    [ModelProperty_OverridesIsDrawDistanceFixed] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_IS_DRAW_DISTANCE_FIXED, .WireId = 15U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesIsDrawDistanceFixed), .SinceVersion = 1U
    },
    [ModelProperty_IsDrawDistanceFixed] =
    {
        WORLD_MODEL_PLACEMENT_ROW_IS_DRAW_DISTANCE_FIXED, .WireId = 16U,
        PROPERTY_MEMBER(WorldModelComponent, _isDrawDistanceFixed), .SinceVersion = 1U
    },
    [ModelProperty_OverridesLodDistances] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_LOD_DISTANCES, .WireId = 17U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesLodDistances), .SinceVersion = 1U
    },
    [ModelProperty_LodDistances] =
    {
        WORLD_MODEL_PLACEMENT_ROW_LOD_DISTANCES, .WireId = 18U, PROPERTY_MEMBER(WorldModelComponent, _lodDistances),
        .SinceVersion = 1U
    },
    [ModelProperty_LodDistanceMultiplier] =
    {
        WORLD_MODEL_PLACEMENT_ROW_LOD_DISTANCE_MULTIPLIER, .WireId = 19U,
        PROPERTY_MEMBER(WorldModelComponent, _lodDistanceMultiplier), .SinceVersion = 1U
    },
    [ModelProperty_IsLodForced] =
    {
        WORLD_MODEL_PLACEMENT_ROW_IS_LOD_FORCED, .WireId = 20U, PROPERTY_MEMBER(WorldModelComponent, _isLodForced),
        .SinceVersion = 1U
    },
    [ModelProperty_ForcedLod] =
    {
        WORLD_MODEL_PLACEMENT_ROW_FORCED_LOD, .WireId = 21U, PROPERTY_MEMBER(WorldModelComponent, _forcedLod),
        .SinceVersion = 1U
    },
};

/* The shadow tier's pair, which version 1 held. */
static const uint16_t ModelRetiredWireIds[] =
{
    WORLD_MODEL_PLACEMENT_WIRE_SHADOW_TIER, WORLD_MODEL_PLACEMENT_WIRE_OVERRIDES_SHADOW_TIER
};

/* The wire ids the GHDF migration of the shadow tier reads and writes (the model instances component's too). */
static const WorldModelPlacementTierIds PlacementTierIds =
{
    .OverridesShadowTier = WORLD_MODEL_PLACEMENT_WIRE_OVERRIDES_SHADOW_TIER,
    .ShadowTier = WORLD_MODEL_PLACEMENT_WIRE_SHADOW_TIER,
    .OverridesShadowDistance = WORLD_MODEL_PLACEMENT_WIRE_OVERRIDES_SHADOW_DISTANCE,
    .ShadowDistance = WORLD_MODEL_PLACEMENT_WIRE_SHADOW_DISTANCE
};


// Static functions: the migrations of older data.
/* Version 1's JSON: the shadow tier read into the shadow distance. */
static Error MigrateModelJSON(uint32_t fromVersion, JSONCompound* compound, JSONObjectPool* pool, Logger* logger)
{
    (void)pool;
    return (fromVersion <= LEGACY_SHADOW_TIER_VERSION)
        ? WorldModelPlacement_MigrateShadowTierJSON(compound, logger, "model_component") : Error_CreateSuccess();
}

/* Version 1's GHDF: the same, by wire ids. */
static Error MigrateModelGHDF(uint32_t fromVersion, GHDFCompound* compound, GHDFObjectPool* pool, Logger* logger)
{
    (void)pool;
    return (fromVersion <= LEGACY_SHADOW_TIER_VERSION)
        ? WorldModelPlacement_MigrateShadowTierGHDF(compound, &PlacementTierIds, logger, "model_component")
        : Error_CreateSuccess();
}


// Fields: the table (it refers to the migrations above).
static const PropertyTable ModelTable =
{
    .Name = u8"model_component",
    .Version = WORLD_MODEL_PLACEMENT_TABLE_VERSION,
    .StructSize = sizeof(WorldModelComponent),
    .Descriptors = ModelProperties,
    .DescriptorCount = ModelProperty_Count,
    .RetiredWireIds = ModelRetiredWireIds,
    .RetiredWireIdCount = sizeof(ModelRetiredWireIds) / sizeof(ModelRetiredWireIds[0]),
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = MigrateModelJSON,
    .MigrateGHDF = MigrateModelGHDF
};


// Static functions.
static Error ConstructModel(WorldComponent* component)
{
    return Properties_Construct(&ModelTable, component);
}

static void DeconstructModel(WorldComponent* component)
{
    Properties_Deconstruct(&ModelTable, component);
}

/* Checks @p value against one row's rules (so the setters' rules live in the table only). */
static Error CheckProperty(ModelProperty property, PropertyValue value)
{
    return Properties_CheckValue(&ModelTable, &ModelProperties[property], &value);
}

/* The error of a setter called without a component. */
static Error CreateNullSelfError(const char* function)
{
    return Error_Construct4(ErrorCode_IllegalArgument, "%s: self must not be NULL.", function);
}

/* Replaces an owned string field with a clone of @p value (checked by the caller), then reports the change. */
static void SetString(WorldModelComponent* self, unsigned char** field, const unsigned char* value,
    ModelProperty property)
{
    unsigned char* Clone = WorldInternal_CloneString(value);
    Memory_Free(*field);
    *field = Clone;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[property]);
}

/* Sets a bool field and reports the change. */
static void SetBool(WorldModelComponent* self, bool* field, bool value, ModelProperty property)
{
    *field = value;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[property]);
}

/* Sets a float field after checking it against its row, and reports the change. */
static Error SetFloat(WorldModelComponent* self, float* field, float value, ModelProperty property)
{
    Error CheckResult = CheckProperty(property, PropertyValue_FromFloat(value));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    *field = value;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[property]);
    return Error_CreateSuccess();
}

/* A setting's override flag and its row; false for a value outside the enum. */
static bool TryGetOverrideFlag(WorldModelComponent* self, WorldModelSetting setting, bool** outFlag,
    ModelProperty* outProperty)
{
    switch (setting)
    {
        case WorldModelSetting_HasOutline:
            *outFlag = &self->_overridesHasOutline;
            *outProperty = ModelProperty_OverridesHasOutline;
            return true;
        case WorldModelSetting_OmitPixelation:
            *outFlag = &self->_overridesOmitPixelation;
            *outProperty = ModelProperty_OverridesOmitPixelation;
            return true;
        case WorldModelSetting_CastsShadows:
            *outFlag = &self->_overridesCastsShadows;
            *outProperty = ModelProperty_OverridesCastsShadows;
            return true;
        case WorldModelSetting_DrawDistance:
            *outFlag = &self->_overridesDrawDistance;
            *outProperty = ModelProperty_OverridesDrawDistance;
            return true;
        case WorldModelSetting_IsDrawDistanceFixed:
            *outFlag = &self->_overridesIsDrawDistanceFixed;
            *outProperty = ModelProperty_OverridesIsDrawDistanceFixed;
            return true;
        case WorldModelSetting_LodDistances:
            *outFlag = &self->_overridesLodDistances;
            *outProperty = ModelProperty_OverridesLodDistances;
            return true;
        case WorldModelSetting_Haze:
            *outFlag = &self->_overridesHaze;
            *outProperty = ModelProperty_OverridesHaze;
            return true;
        case WorldModelSetting_OccludesSky:
            *outFlag = &self->_overridesOccludesSky;
            *outProperty = ModelProperty_OverridesOccludesSky;
            return true;
        case WorldModelSetting_ShadowDistance:
            *outFlag = &self->_overridesShadowDistance;
            *outProperty = ModelProperty_OverridesShadowDistance;
            return true;
        case WorldModelSetting_IsShadowDistanceFixed:
            *outFlag = &self->_overridesIsShadowDistanceFixed;
            *outProperty = ModelProperty_OverridesIsShadowDistanceFixed;
            return true;
        case WorldModelSetting_IsShadowOnly:
            *outFlag = &self->_overridesIsShadowOnly;
            *outProperty = ModelProperty_OverridesIsShadowOnly;
            return true;
        case WorldModelSetting_HasTwoSidedShadows:
            *outFlag = &self->_overridesHasTwoSidedShadows;
            *outProperty = ModelProperty_OverridesHasTwoSidedShadows;
            return true;
        case WorldModelSetting_ShadowLod:
            *outFlag = &self->_overridesShadowLod;
            *outProperty = ModelProperty_OverridesShadowLod;
            return true;
        case WorldModelSetting_Count:
            break;
    }
    return false;
}


// Fields: the registry entry (it refers to the functions above).
static const WorldComponentTypeInfo ModelTypeInfo =
{
    .Type = WorldComponentType_Model,
    .Name = u8"model",
    .Size = sizeof(WorldModelComponent),
    .Table = &ModelTable,
    .IsTransformDriver = false,
    .Construct = ConstructModel,
    .Deconstruct = DeconstructModel,
    .OnAttach = NULL,
    .OnDetach = NULL,
    .OnPropertySet = NULL
};


// Public functions.
const WorldComponentTypeInfo* WorldModelComponent_GetTypeInfo(void)
{
    return &ModelTypeInfo;
}

const PropertyTable* WorldModelComponent_GetPropertyTable(void)
{
    return &ModelTable;
}

Error WorldModelComponent_SetWorldModel(WorldModelComponent* self, const unsigned char* worldModel)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetWorldModel");
    }
    Error CheckResult = CheckProperty(ModelProperty_WorldModel, PropertyValue_FromAssetName(worldModel));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    SetString(self, &self->_worldModel, worldModel, ModelProperty_WorldModel);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetUsesVariant(WorldModelComponent* self, bool usesVariant)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetUsesVariant");
    }
    SetBool(self, &self->_usesVariant, usesVariant, ModelProperty_UsesVariant);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetVariant(WorldModelComponent* self, const unsigned char* variant)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetVariant");
    }
    Error CheckResult = CheckProperty(ModelProperty_Variant, PropertyValue_FromString(variant));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    SetString(self, &self->_variant, variant, ModelProperty_Variant);
    return Error_CreateSuccess();
}

bool WorldModelComponent_IsOverridden(const WorldModelComponent* self, WorldModelSetting setting)
{
    switch (setting)
    {
        case WorldModelSetting_HasOutline:            return self->_overridesHasOutline;
        case WorldModelSetting_OmitPixelation:        return self->_overridesOmitPixelation;
        case WorldModelSetting_CastsShadows:          return self->_overridesCastsShadows;
        case WorldModelSetting_DrawDistance:          return self->_overridesDrawDistance;
        case WorldModelSetting_IsDrawDistanceFixed:   return self->_overridesIsDrawDistanceFixed;
        case WorldModelSetting_LodDistances:          return self->_overridesLodDistances;
        case WorldModelSetting_Haze:                  return self->_overridesHaze;
        case WorldModelSetting_OccludesSky:           return self->_overridesOccludesSky;
        case WorldModelSetting_ShadowDistance:        return self->_overridesShadowDistance;
        case WorldModelSetting_IsShadowDistanceFixed: return self->_overridesIsShadowDistanceFixed;
        case WorldModelSetting_IsShadowOnly:          return self->_overridesIsShadowOnly;
        case WorldModelSetting_HasTwoSidedShadows:    return self->_overridesHasTwoSidedShadows;
        case WorldModelSetting_ShadowLod:             return self->_overridesShadowLod;
        case WorldModelSetting_Count:                 break;
    }
    // Only reachable with a value outside the enum: nothing is overridden.
    return false;
}

Error WorldModelComponent_SetOverridden(WorldModelComponent* self, WorldModelSetting setting, bool isOverridden)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetOverridden");
    }
    bool* Flag = NULL;
    ModelProperty Property = ModelProperty_Count;
    if (!TryGetOverrideFlag(self, setting, &Flag, &Property))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "WorldModelComponent_SetOverridden: %d is not a WorldModelSetting.", (int)setting);
    }
    SetBool(self, Flag, isOverridden, Property);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetHasOutline(WorldModelComponent* self, bool hasOutline)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetHasOutline");
    }
    SetBool(self, &self->_hasOutline, hasOutline, ModelProperty_HasOutline);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetOmitsPixelation(WorldModelComponent* self, bool omitPixelation)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetOmitsPixelation");
    }
    SetBool(self, &self->_omitPixelation, omitPixelation, ModelProperty_OmitPixelation);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetCastsShadows(WorldModelComponent* self, bool castsShadows)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetCastsShadows");
    }
    SetBool(self, &self->_castsShadows, castsShadows, ModelProperty_CastsShadows);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetShadowDistance(WorldModelComponent* self, float shadowDistance)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetShadowDistance");
    }
    return SetFloat(self, &self->_shadowDistance, shadowDistance, ModelProperty_ShadowDistance);
}

Error WorldModelComponent_SetShadowDistanceMultiplier(WorldModelComponent* self, float multiplier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetShadowDistanceMultiplier");
    }
    return SetFloat(self, &self->_shadowDistanceMultiplier, multiplier, ModelProperty_ShadowDistanceMultiplier);
}

Error WorldModelComponent_SetIsShadowDistanceFixed(WorldModelComponent* self, bool isFixed)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetIsShadowDistanceFixed");
    }
    SetBool(self, &self->_isShadowDistanceFixed, isFixed, ModelProperty_IsShadowDistanceFixed);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetIsShadowOnly(WorldModelComponent* self, bool isShadowOnly)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetIsShadowOnly");
    }
    SetBool(self, &self->_isShadowOnly, isShadowOnly, ModelProperty_IsShadowOnly);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetHasTwoSidedShadows(WorldModelComponent* self, bool hasTwoSidedShadows)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetHasTwoSidedShadows");
    }
    SetBool(self, &self->_hasTwoSidedShadows, hasTwoSidedShadows, ModelProperty_HasTwoSidedShadows);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetShadowLod(WorldModelComponent* self, uint32_t lod)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetShadowLod");
    }
    self->_shadowLod = lod;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_ShadowLod]);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetOccludesSky(WorldModelComponent* self, bool occludesSky)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetOccludesSky");
    }
    SetBool(self, &self->_occludesSky, occludesSky, ModelProperty_OccludesSky);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetHaze(WorldModelComponent* self, float haze)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetHaze");
    }
    Error CheckResult = CheckProperty(ModelProperty_Haze, PropertyValue_FromFloat(haze));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    self->_haze = haze;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_Haze]);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetDrawDistance(WorldModelComponent* self, float drawDistance)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetDrawDistance");
    }
    Error CheckResult = CheckProperty(ModelProperty_DrawDistance, PropertyValue_FromFloat(drawDistance));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    self->_drawDistance = drawDistance;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_DrawDistance]);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetDrawDistanceMultiplier(WorldModelComponent* self, float multiplier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetDrawDistanceMultiplier");
    }
    Error CheckResult = CheckProperty(ModelProperty_DrawDistanceMultiplier, PropertyValue_FromFloat(multiplier));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    self->_drawDistanceMultiplier = multiplier;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_DrawDistanceMultiplier]);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetIsDrawDistanceFixed(WorldModelComponent* self, bool isFixed)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetIsDrawDistanceFixed");
    }
    SetBool(self, &self->_isDrawDistanceFixed, isFixed, ModelProperty_IsDrawDistanceFixed);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetLodDistances(WorldModelComponent* self, const float* distances, size_t count)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetLodDistances");
    }
    PropertyFloatList* List = NULL;
    Error Result = PropertyFloatList_Create(distances, count, &List);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = CheckProperty(ModelProperty_LodDistances, PropertyValue_FromFloatList(List));
    if (Result.Code != ErrorCode_Success)
    {
        PropertyFloatList_Destroy(List);
        return Result;
    }
    PropertyFloatList_Destroy(self->_lodDistances);
    self->_lodDistances = List;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_LodDistances]);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetLodDistanceMultiplier(WorldModelComponent* self, float multiplier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetLodDistanceMultiplier");
    }
    Error CheckResult = CheckProperty(ModelProperty_LodDistanceMultiplier, PropertyValue_FromFloat(multiplier));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    self->_lodDistanceMultiplier = multiplier;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_LodDistanceMultiplier]);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetIsLodForced(WorldModelComponent* self, bool isForced)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetIsLodForced");
    }
    SetBool(self, &self->_isLodForced, isForced, ModelProperty_IsLodForced);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetForcedLod(WorldModelComponent* self, uint32_t lod)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetForcedLod");
    }
    self->_forcedLod = lod;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_ForcedLod]);
    return Error_CreateSuccess();
}

bool WorldModelComponent_ResolveLodSettings(const WorldModelComponent* self, const WorldModel* worldModel,
    WorldModelLodSettings* outSettings)
{
    size_t LodCount = WorldModel_GetLodCount(worldModel);
    size_t LastLod = (LodCount > 0U) ? (LodCount - 1U) : 0U;
    size_t OwnCount = 0U;
    const float* Own = WorldModelComponent_GetLodDistances(self, &OwnCount);
    bool IsOwnListUsed = self->_overridesLodDistances && (OwnCount == LodCount);

    WorldModelLodSettings Settings =
    {
        .LodCount = LodCount,
        .Hysteresis = WorldModel_GetLodHysteresis(worldModel),
        .FadeSeconds = WorldModel_GetLodFadeSeconds(worldModel),
        .IsLodForced = self->_isLodForced,
        .ForcedLod = ((size_t)self->_forcedLod < LastLod) ? (size_t)self->_forcedLod : LastLod
    };
    for (size_t Index = 0U; (Index < LodCount) && (Index < WORLD_MODEL_MAX_LODS); Index++)
    {
        float Distance = IsOwnListUsed ? Own[Index] : WorldModel_GetLodMaxDistance(worldModel, Index);
        Settings.SwitchDistances[Index] = Distance * self->_lodDistanceMultiplier;
    }
    *outSettings = Settings;
    return self->_isLodForced || !self->_overridesLodDistances || IsOwnListUsed;
}

WorldModelSettings WorldModelComponent_ResolveSettings(const WorldModelComponent* self,
    const WorldModelSettings* modelSettings)
{
    WorldModelSettings Settings = *modelSettings;
    if (self->_overridesHasOutline)
    {
        Settings.HasOutline = self->_hasOutline;
    }
    if (self->_overridesOmitPixelation)
    {
        Settings.OmitsPixelation = self->_omitPixelation;
    }
    if (self->_overridesCastsShadows)
    {
        Settings.CastsShadows = self->_castsShadows;
    }
    if (self->_overridesIsShadowDistanceFixed)
    {
        Settings.IsShadowDistanceFixed = self->_isShadowDistanceFixed;
    }
    if (self->_overridesIsShadowOnly)
    {
        Settings.IsShadowOnly = self->_isShadowOnly;
    }
    if (self->_overridesHasTwoSidedShadows)
    {
        Settings.HasTwoSidedShadows = self->_hasTwoSidedShadows;
    }
    if (self->_overridesShadowLod)
    {
        Settings.ShadowLod = self->_shadowLod;
        Settings.HasShadowLod = true;
    }
    if (self->_overridesHaze)
    {
        Settings.Haze = self->_haze;
    }
    if (self->_overridesOccludesSky)
    {
        Settings.OccludesSky = self->_occludesSky;
    }
    if (self->_overridesDrawDistance)
    {
        Settings.DrawDistance = self->_drawDistance;
    }
    Settings.DrawDistance *= self->_drawDistanceMultiplier;
    if (self->_overridesIsDrawDistanceFixed)
    {
        Settings.IsDrawDistanceFixed = self->_isDrawDistanceFixed;
    }
    WorldModelPlacement_ResolveShadowDistance(&Settings, self->_overridesShadowDistance, self->_shadowDistance,
        self->_shadowDistanceMultiplier);
    return Settings;
}


// Internal functions (declared in WorldModelComponentInternal.h): the migrations of the shadow tier.
/* Whether a JSON value is the string @p text. */
static bool IsJSONText(const JSONObjectValue* value, const char* text)
{
    if (value->Type != JSONValueType_String)
    {
        return false;
    }
    size_t Length = StringUTF8_GetByteLength((const unsigned char*)text);
    const GenericBuffer* Buffer = value->Value.String;
    size_t Count = (Buffer != NULL) ? Buffer->_count : 0U;
    return (Count == Length) && ((Length == 0U) || Memory_IsEqual(Buffer->_data, text, Length));
}

/* The warning for a dropped "far" tier. */
static void WarnFarTierDropped(Logger* logger, const char* tableName)
{
    if (logger == NULL)
    {
        return;
    }
    Error LogResult = Logger_LogWarningFormatted(logger, (const unsigned char*)u8"%s: an older file's shadow_tier \"far\" "
        u8"is dropped: every caster casts from the camera out to its shadow distance.", tableName);
    Error_Deconstruct(&LogResult);
}

/* Removes a JSON key that is there (absent keys are fine). */
static Error RemoveJSONKey(JSONCompound* compound, const unsigned char* key, bool isPresent)
{
    return isPresent ? JSONCompound_Remove(compound, key) : Error_CreateSuccess();
}

/* Sets a JSON key the data does not give yet. */
static Error SetJSONIfAbsent(JSONCompound* compound, const unsigned char* key, JSONObjectValue value)
{
    JSONObjectValue Existing;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(compound, key, &Existing, &IsFound);
    if ((Result.Code != ErrorCode_Success) || IsFound)
    {
        return Result;
    }
    return JSONCompound_Set(compound, key, &value);
}

Error WorldModelPlacement_MigrateShadowTierJSON(JSONCompound* compound, Logger* logger, const char* tableName)
{
    JSONObjectValue Value;
    bool HasFlag = false;
    Error Result = JSONCompound_GetOptional(compound, LEGACY_OVERRIDES_SHADOW_TIER_KEY, &Value, &HasFlag);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (HasFlag && (Value.Type != JSONValueType_Boolean))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "overrides_shadow_tier: Expected true or false.");
    }
    bool OverridesTier = HasFlag && Value.Value.Boolean;

    bool HasTier = false;
    Result = JSONCompound_GetOptional(compound, LEGACY_SHADOW_TIER_KEY, &Value, &HasTier);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    LegacyShadowTier Tier = LegacyShadowTier_Count;
    for (int Index = 0; HasTier && (Index < (int)LegacyShadowTier_Count); Index++)
    {
        if (IsJSONText(&Value, LegacyShadowTierNames[Index]))
        {
            Tier = (LegacyShadowTier)Index;
        }
    }
    if (HasTier && (Tier == LegacyShadowTier_Count))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "shadow_tier must be \"both\", \"near\" or \"far\".");
    }

    Result = RemoveJSONKey(compound, LEGACY_OVERRIDES_SHADOW_TIER_KEY, HasFlag);
    if (Result.Code == ErrorCode_Success)
    {
        Result = RemoveJSONKey(compound, LEGACY_SHADOW_TIER_KEY, HasTier);
    }
    if ((Result.Code != ErrorCode_Success) || !HasTier)
    {
        return Result;
    }
    if (Tier == LegacyShadowTier_Far)
    {
        WarnFarTierDropped(logger, tableName);
        return Result;
    }
    if (Tier != LegacyShadowTier_Near)
    {
        return Result;
    }
    // "near" cast within the near shadow map's reach: that distance, overridden where the tier was.
    Result = SetJSONIfAbsent(compound, SHADOW_DISTANCE_KEY, (JSONObjectValue){ .Type = JSONValueType_RealNumber,
        .Value.RealNumber = (double)WORLD_MODEL_NEAR_TIER_SHADOW_DISTANCE });
    if ((Result.Code == ErrorCode_Success) && OverridesTier)
    {
        Result = SetJSONIfAbsent(compound, OVERRIDES_SHADOW_DISTANCE_KEY,
            (JSONObjectValue){ .Type = JSONValueType_Boolean, .Value.Boolean = true });
    }
    return Result;
}

/* Sets a GHDF entry the data does not hold yet. */
static Error SetGHDFIfAbsent(GHDFCompound* compound, uint16_t id, GHDFObjectValue value)
{
    GHDFObjectValue Existing;
    bool IsFound = false;
    Error Result = GHDFCompound_GetOptional(compound, (GHDFEntryID)id, &Existing, &IsFound);
    if ((Result.Code != ErrorCode_Success) || IsFound)
    {
        return Result;
    }
    return GHDFCompound_SetValue(compound, (GHDFEntryID)id, GHDF_CreateRegularType(value.Type), &value);
}

/* Reads an optional scalar entry of @p type; a present entry of another type is refused with @p message. */
static Error GetGHDFScalar(GHDFCompound* compound, uint16_t id, GHDFValueType type, const char* message,
    GHDFObjectValue* outEntry, bool* outFound)
{
    Error Result = GHDFCompound_GetOptionalVerified(compound, (GHDFEntryID)id, GHDF_CreateRegularType(type), outEntry,
        outFound);
    if (Result.Code == ErrorCode_Success)
    {
        return Result;
    }
    Error_Deconstruct(&Result);
    *outFound = false;
    return Error_Construct2(ErrorCode_Deserialize, message);
}

Error WorldModelPlacement_MigrateShadowTierGHDF(GHDFCompound* compound, const WorldModelPlacementTierIds* ids,
    Logger* logger, const char* tableName)
{
    GHDFObjectValue Entry;
    bool HasFlag = false;
    Error Result = GetGHDFScalar(compound, ids->OverridesShadowTier, GHDFValueType_Boolean,
        "overrides_shadow_tier: Expected a boolean.", &Entry, &HasFlag);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool OverridesTier = HasFlag && Entry.Value.Boolean;
    bool HasTier = false;
    Result = GetGHDFScalar(compound, ids->ShadowTier, GHDFValueType_EncodedInteger,
        "shadow_tier: Expected an enum value.", &Entry, &HasTier);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    int64_t TierValue = HasTier ? Entry.Value.EncodedInteger : (int64_t)LegacyShadowTier_Both;
    if ((TierValue < (int64_t)LegacyShadowTier_Both) || (TierValue > (int64_t)LegacyShadowTier_Far))
    {
        return Error_Construct2(ErrorCode_Deserialize, "shadow_tier: The enum value is out of range.");
    }

    if (HasFlag)
    {
        Result = GHDFCompound_Remove(compound, (GHDFEntryID)ids->OverridesShadowTier);
    }
    if ((Result.Code == ErrorCode_Success) && HasTier)
    {
        Result = GHDFCompound_Remove(compound, (GHDFEntryID)ids->ShadowTier);
    }
    if ((Result.Code != ErrorCode_Success) || !HasTier)
    {
        return Result;
    }
    LegacyShadowTier Tier = (LegacyShadowTier)TierValue;
    if (Tier == LegacyShadowTier_Far)
    {
        WarnFarTierDropped(logger, tableName);
        return Result;
    }
    if (Tier != LegacyShadowTier_Near)
    {
        return Result;
    }
    Result = SetGHDFIfAbsent(compound, ids->ShadowDistance, GHDFObjectValue_CreateFloat(WORLD_MODEL_NEAR_TIER_SHADOW_DISTANCE));
    if ((Result.Code == ErrorCode_Success) && OverridesTier)
    {
        Result = SetGHDFIfAbsent(compound, ids->OverridesShadowDistance, GHDFObjectValue_CreateBoolean(true));
    }
    return Result;
}
