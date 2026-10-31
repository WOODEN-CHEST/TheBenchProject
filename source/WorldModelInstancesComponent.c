#include "WorldModelInstancesComponent.h"
#include "WorldInternal.h"
#include "WorldModelComponentInternal.h"
#include "WorldObject.h"
#include "wr/WRMemory.h"


// Macros.
/** The table version older files hold the shadow tier in. */
#define LEGACY_SHADOW_TIER_VERSION 1U
/** The inspector's category of the copies. */
#define CATEGORY_INSTANCES ((const unsigned char*)u8"Instances")


// Types.
/* The rows of the table, so the setters reach their descriptors without a search. */
typedef enum InstancesPropertyEnum
{
    InstancesProperty_WorldModel,
    InstancesProperty_UsesVariant,
    InstancesProperty_Variant,
    InstancesProperty_OverridesHasOutline,
    InstancesProperty_HasOutline,
    InstancesProperty_OverridesCastsShadows,
    InstancesProperty_CastsShadows,
    InstancesProperty_OverridesShadowDistance,
    InstancesProperty_ShadowDistance,
    InstancesProperty_ShadowDistanceMultiplier,
    InstancesProperty_OverridesIsShadowDistanceFixed,
    InstancesProperty_IsShadowDistanceFixed,
    InstancesProperty_OverridesIsShadowOnly,
    InstancesProperty_IsShadowOnly,
    InstancesProperty_OverridesHasTwoSidedShadows,
    InstancesProperty_HasTwoSidedShadows,
    InstancesProperty_OverridesShadowLod,
    InstancesProperty_ShadowLod,
    InstancesProperty_OverridesHaze,
    InstancesProperty_Haze,
    InstancesProperty_OverridesOccludesSky,
    InstancesProperty_OccludesSky,
    InstancesProperty_OverridesDrawDistance,
    InstancesProperty_DrawDistance,
    InstancesProperty_DrawDistanceMultiplier,
    InstancesProperty_OverridesIsDrawDistanceFixed,
    InstancesProperty_IsDrawDistanceFixed,
    InstancesProperty_OverridesLodDistances,
    InstancesProperty_LodDistances,
    InstancesProperty_LodDistanceMultiplier,
    InstancesProperty_IsLodForced,
    InstancesProperty_ForcedLod,
    InstancesProperty_Instances,
    InstancesProperty_Count
} InstancesProperty;


// Fields.
/* Every property, in inspector order: the model component's rows with the model component's wire ids (shared through
 * source/WorldModelComponentInternal.h; no omit_pixelation, 3 and 9: copies are never crisp), then the copies. The wire
 * ids are the save schema: never renumber or reuse one; a removed property's id goes into the table's retired list. The
 * haze's pair is 23 and 24, the sky's 25 and 26 and the shadow rows 27 to 37, as in the model component, and the copies
 * 22; 4 and 10, the shadow tier's pair, are retired. The next free wire id is 38. */
static const PropertyDescriptor InstancesProperties[InstancesProperty_Count] =
{
    [InstancesProperty_WorldModel] =
    {
        WORLD_MODEL_PLACEMENT_ROW_WORLD_MODEL, .WireId = 1U, PROPERTY_MEMBER(WorldModelInstancesComponent, _worldModel),
        .SinceVersion = 1U
    },
    [InstancesProperty_UsesVariant] =
    {
        WORLD_MODEL_PLACEMENT_ROW_USES_VARIANT, .WireId = 6U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _usesVariant), .SinceVersion = 1U
    },
    [InstancesProperty_Variant] =
    {
        WORLD_MODEL_PLACEMENT_ROW_VARIANT, .WireId = 7U, PROPERTY_MEMBER(WorldModelInstancesComponent, _variant),
        .SinceVersion = 1U
    },
    [InstancesProperty_OverridesHasOutline] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAS_OUTLINE, .WireId = 8U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesHasOutline), .SinceVersion = 1U
    },
    [InstancesProperty_HasOutline] =
    {
        WORLD_MODEL_PLACEMENT_ROW_HAS_OUTLINE, .WireId = 2U, PROPERTY_MEMBER(WorldModelInstancesComponent, _hasOutline),
        .SinceVersion = 1U
    },
    [InstancesProperty_OverridesCastsShadows] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_CASTS_SHADOWS, .WireId = 11U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesCastsShadows), .SinceVersion = 1U
    },
    [InstancesProperty_CastsShadows] =
    {
        WORLD_MODEL_PLACEMENT_ROW_CASTS_SHADOWS, .WireId = 5U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _castsShadows), .SinceVersion = 1U
    },
    [InstancesProperty_OverridesShadowDistance] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_SHADOW_DISTANCE, .WireId = WORLD_MODEL_PLACEMENT_WIRE_OVERRIDES_SHADOW_DISTANCE,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesShadowDistance), .SinceVersion = 2U
    },
    [InstancesProperty_ShadowDistance] =
    {
        WORLD_MODEL_PLACEMENT_ROW_SHADOW_DISTANCE, .WireId = WORLD_MODEL_PLACEMENT_WIRE_SHADOW_DISTANCE,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _shadowDistance), .SinceVersion = 2U
    },
    [InstancesProperty_ShadowDistanceMultiplier] =
    {
        WORLD_MODEL_PLACEMENT_ROW_SHADOW_DISTANCE_MULTIPLIER, .WireId = 29U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _shadowDistanceMultiplier), .SinceVersion = 2U
    },
    [InstancesProperty_OverridesIsShadowDistanceFixed] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_IS_SHADOW_DISTANCE_FIXED, .WireId = 30U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesIsShadowDistanceFixed), .SinceVersion = 2U
    },
    [InstancesProperty_IsShadowDistanceFixed] =
    {
        WORLD_MODEL_PLACEMENT_ROW_IS_SHADOW_DISTANCE_FIXED, .WireId = 31U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _isShadowDistanceFixed), .SinceVersion = 2U
    },
    [InstancesProperty_OverridesIsShadowOnly] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_IS_SHADOW_ONLY, .WireId = 32U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesIsShadowOnly), .SinceVersion = 2U
    },
    [InstancesProperty_IsShadowOnly] =
    {
        WORLD_MODEL_PLACEMENT_ROW_IS_SHADOW_ONLY, .WireId = 33U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _isShadowOnly), .SinceVersion = 2U
    },
    [InstancesProperty_OverridesHasTwoSidedShadows] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAS_TWO_SIDED_SHADOWS, .WireId = 34U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesHasTwoSidedShadows), .SinceVersion = 2U
    },
    [InstancesProperty_HasTwoSidedShadows] =
    {
        WORLD_MODEL_PLACEMENT_ROW_HAS_TWO_SIDED_SHADOWS, .WireId = 35U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _hasTwoSidedShadows), .SinceVersion = 2U
    },
    [InstancesProperty_OverridesShadowLod] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_SHADOW_LOD, .WireId = 36U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesShadowLod), .SinceVersion = 2U
    },
    [InstancesProperty_ShadowLod] =
    {
        WORLD_MODEL_PLACEMENT_ROW_SHADOW_LOD, .WireId = 37U, PROPERTY_MEMBER(WorldModelInstancesComponent, _shadowLod),
        .SinceVersion = 2U
    },
    [InstancesProperty_OverridesHaze] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAZE, .WireId = 23U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesHaze), .SinceVersion = 1U
    },
    [InstancesProperty_Haze] =
    {
        WORLD_MODEL_PLACEMENT_ROW_HAZE, .WireId = 24U, PROPERTY_MEMBER(WorldModelInstancesComponent, _haze),
        .SinceVersion = 1U
    },
    [InstancesProperty_OverridesOccludesSky] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_OCCLUDES_SKY, .WireId = 25U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesOccludesSky), .SinceVersion = 1U
    },
    [InstancesProperty_OccludesSky] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OCCLUDES_SKY, .WireId = 26U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _occludesSky), .SinceVersion = 1U
    },
    [InstancesProperty_OverridesDrawDistance] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_DRAW_DISTANCE, .WireId = 12U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesDrawDistance), .SinceVersion = 1U
    },
    [InstancesProperty_DrawDistance] =
    {
        WORLD_MODEL_PLACEMENT_ROW_DRAW_DISTANCE, .WireId = 13U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _drawDistance), .SinceVersion = 1U
    },
    [InstancesProperty_DrawDistanceMultiplier] =
    {
        WORLD_MODEL_PLACEMENT_ROW_DRAW_DISTANCE_MULTIPLIER, .WireId = 14U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _drawDistanceMultiplier), .SinceVersion = 1U
    },
    [InstancesProperty_OverridesIsDrawDistanceFixed] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_IS_DRAW_DISTANCE_FIXED, .WireId = 15U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesIsDrawDistanceFixed), .SinceVersion = 1U
    },
    [InstancesProperty_IsDrawDistanceFixed] =
    {
        WORLD_MODEL_PLACEMENT_ROW_IS_DRAW_DISTANCE_FIXED, .WireId = 16U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _isDrawDistanceFixed), .SinceVersion = 1U
    },
    [InstancesProperty_OverridesLodDistances] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_LOD_DISTANCES, .WireId = 17U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesLodDistances), .SinceVersion = 1U
    },
    [InstancesProperty_LodDistances] =
    {
        WORLD_MODEL_PLACEMENT_ROW_LOD_DISTANCES, .WireId = 18U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _lodDistances), .SinceVersion = 1U
    },
    [InstancesProperty_LodDistanceMultiplier] =
    {
        WORLD_MODEL_PLACEMENT_ROW_LOD_DISTANCE_MULTIPLIER, .WireId = 19U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _lodDistanceMultiplier), .SinceVersion = 1U
    },
    [InstancesProperty_IsLodForced] =
    {
        WORLD_MODEL_PLACEMENT_ROW_IS_LOD_FORCED, .WireId = 20U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _isLodForced), .SinceVersion = 1U
    },
    [InstancesProperty_ForcedLod] =
    {
        WORLD_MODEL_PLACEMENT_ROW_FORCED_LOD, .WireId = 21U, PROPERTY_MEMBER(WorldModelInstancesComponent, _forcedLod),
        .SinceVersion = 1U
    },
    [InstancesProperty_Instances] =
    {
        .Key = u8"instances", .WireId = 22U, .Type = PropertyType_InstanceList,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _instances), .Default.InstanceListValue = NULL,
        .Rules = PROPERTY_RULES_INSTANCE_LIST(WORLD_MODEL_INSTANCES_MAX_COUNT, WORLD_POSITION_LIMIT),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_INSTANCES, .DisplayName = u8"Instances",
        .Description = u8"The copies of the world model: each one's position relative to the object (in metres), its "
            u8"rotation (degrees), its scale and its tint (its alpha an opacity).",
        .SinceVersion = 1U
    },
};

/* The shadow tier's pair, which version 1 held. */
static const uint16_t InstancesRetiredWireIds[] =
{
    WORLD_MODEL_PLACEMENT_WIRE_SHADOW_TIER, WORLD_MODEL_PLACEMENT_WIRE_OVERRIDES_SHADOW_TIER
};

/* The wire ids the GHDF migration of the shadow tier reads and writes: the model component's. */
static const WorldModelPlacementTierIds InstancesTierIds =
{
    .OverridesShadowTier = WORLD_MODEL_PLACEMENT_WIRE_OVERRIDES_SHADOW_TIER,
    .ShadowTier = WORLD_MODEL_PLACEMENT_WIRE_SHADOW_TIER,
    .OverridesShadowDistance = WORLD_MODEL_PLACEMENT_WIRE_OVERRIDES_SHADOW_DISTANCE,
    .ShadowDistance = WORLD_MODEL_PLACEMENT_WIRE_SHADOW_DISTANCE
};


// Static functions: the migrations of older data.
/* Version 1's JSON: the shadow tier read into the shadow distance. */
static Error MigrateInstancesJSON(uint32_t fromVersion, JSONCompound* compound, JSONObjectPool* pool, Logger* logger)
{
    (void)pool;
    return (fromVersion <= LEGACY_SHADOW_TIER_VERSION)
        ? WorldModelPlacement_MigrateShadowTierJSON(compound, logger, "model_instances_component")
        : Error_CreateSuccess();
}

/* Version 1's GHDF: the same, by wire ids. */
static Error MigrateInstancesGHDF(uint32_t fromVersion, GHDFCompound* compound, GHDFObjectPool* pool, Logger* logger)
{
    (void)pool;
    return (fromVersion <= LEGACY_SHADOW_TIER_VERSION)
        ? WorldModelPlacement_MigrateShadowTierGHDF(compound, &InstancesTierIds, logger, "model_instances_component")
        : Error_CreateSuccess();
}


// Fields: the table (it refers to the migrations above).
static const PropertyTable InstancesTable =
{
    .Name = u8"model_instances_component",
    .Version = WORLD_MODEL_PLACEMENT_TABLE_VERSION,
    .StructSize = sizeof(WorldModelInstancesComponent),
    .Descriptors = InstancesProperties,
    .DescriptorCount = InstancesProperty_Count,
    .RetiredWireIds = InstancesRetiredWireIds,
    .RetiredWireIdCount = sizeof(InstancesRetiredWireIds) / sizeof(InstancesRetiredWireIds[0]),
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = MigrateInstancesJSON,
    .MigrateGHDF = MigrateInstancesGHDF
};


// Static functions.
static Error ConstructInstances(WorldComponent* component)
{
    return Properties_Construct(&InstancesTable, component);
}

static void DeconstructInstances(WorldComponent* component)
{
    Properties_Deconstruct(&InstancesTable, component);
}

/* The copies may have changed: after their setter or a table write of their row, and after a read or a copy (no row). */
static void OnInstancesPropertySet(World* world, WorldObject* object, WorldComponent* component,
    const PropertyDescriptor* descriptor)
{
    (void)world;
    (void)object;
    if ((descriptor == NULL) || (descriptor == &InstancesProperties[InstancesProperty_Instances]))
    {
        WorldModelInstancesComponent* Self = WorldModelInstancesComponent_FromComponent(component);
        Self->_instancesRevision++;
    }
}

/* Checks @p value against one row's rules (so the setters' rules live in the table only). */
static Error CheckProperty(InstancesProperty property, PropertyValue value)
{
    return Properties_CheckValue(&InstancesTable, &InstancesProperties[property], &value);
}

/* The error of a setter called without a component. */
static Error CreateNullSelfError(const char* function)
{
    return Error_Construct4(ErrorCode_IllegalArgument, "%s: self must not be NULL.", function);
}

/* Replaces an owned string field with a clone of @p value (checked by the caller; an asset name in full), then reports
 * the change. */
static void SetString(WorldModelInstancesComponent* self, unsigned char** field, const unsigned char* value,
    InstancesProperty property)
{
    unsigned char* Clone = (InstancesProperties[property].Type == PropertyType_AssetName)
        ? WorldInternal_CloneAssetName(value) : WorldInternal_CloneString(value);
    Memory_Free(*field);
    *field = Clone;
    WorldInternal_ComponentAfterSet(self, &InstancesProperties[property]);
}

/* Sets a bool field and reports the change. */
static void SetBool(WorldModelInstancesComponent* self, bool* field, bool value, InstancesProperty property)
{
    *field = value;
    WorldInternal_ComponentAfterSet(self, &InstancesProperties[property]);
}

/* Sets a float field after checking it against its row, and reports the change. */
static Error SetFloat(WorldModelInstancesComponent* self, float* field, float value, InstancesProperty property)
{
    Error CheckResult = CheckProperty(property, PropertyValue_FromFloat(value));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    *field = value;
    WorldInternal_ComponentAfterSet(self, &InstancesProperties[property]);
    return Error_CreateSuccess();
}

/* A setting's override flag and its row; false for WorldModelSetting_OmitPixelation (copies are never crisp) and for a
 * value outside the enum. */
static bool TryGetOverrideFlag(WorldModelInstancesComponent* self, WorldModelSetting setting, bool** outFlag,
    InstancesProperty* outProperty)
{
    switch (setting)
    {
        case WorldModelSetting_HasOutline:
            *outFlag = &self->_overridesHasOutline;
            *outProperty = InstancesProperty_OverridesHasOutline;
            return true;
        case WorldModelSetting_CastsShadows:
            *outFlag = &self->_overridesCastsShadows;
            *outProperty = InstancesProperty_OverridesCastsShadows;
            return true;
        case WorldModelSetting_DrawDistance:
            *outFlag = &self->_overridesDrawDistance;
            *outProperty = InstancesProperty_OverridesDrawDistance;
            return true;
        case WorldModelSetting_IsDrawDistanceFixed:
            *outFlag = &self->_overridesIsDrawDistanceFixed;
            *outProperty = InstancesProperty_OverridesIsDrawDistanceFixed;
            return true;
        case WorldModelSetting_LodDistances:
            *outFlag = &self->_overridesLodDistances;
            *outProperty = InstancesProperty_OverridesLodDistances;
            return true;
        case WorldModelSetting_Haze:
            *outFlag = &self->_overridesHaze;
            *outProperty = InstancesProperty_OverridesHaze;
            return true;
        case WorldModelSetting_OccludesSky:
            *outFlag = &self->_overridesOccludesSky;
            *outProperty = InstancesProperty_OverridesOccludesSky;
            return true;
        case WorldModelSetting_ShadowDistance:
            *outFlag = &self->_overridesShadowDistance;
            *outProperty = InstancesProperty_OverridesShadowDistance;
            return true;
        case WorldModelSetting_IsShadowDistanceFixed:
            *outFlag = &self->_overridesIsShadowDistanceFixed;
            *outProperty = InstancesProperty_OverridesIsShadowDistanceFixed;
            return true;
        case WorldModelSetting_IsShadowOnly:
            *outFlag = &self->_overridesIsShadowOnly;
            *outProperty = InstancesProperty_OverridesIsShadowOnly;
            return true;
        case WorldModelSetting_HasTwoSidedShadows:
            *outFlag = &self->_overridesHasTwoSidedShadows;
            *outProperty = InstancesProperty_OverridesHasTwoSidedShadows;
            return true;
        case WorldModelSetting_ShadowLod:
            *outFlag = &self->_overridesShadowLod;
            *outProperty = InstancesProperty_OverridesShadowLod;
            return true;
        case WorldModelSetting_OmitPixelation:
        case WorldModelSetting_Count:
            break;
    }
    return false;
}


// Fields: the registry entry (it refers to the functions above).
static const WorldComponentTypeInfo InstancesTypeInfo =
{
    .Type = WorldComponentType_ModelInstances,
    .Name = u8"model_instances",
    .Size = sizeof(WorldModelInstancesComponent),
    .Table = &InstancesTable,
    .IsTransformDriver = false,
    .Construct = ConstructInstances,
    .Deconstruct = DeconstructInstances,
    .OnAttach = NULL,
    .OnDetach = NULL,
    .OnPropertySet = OnInstancesPropertySet
};


// Public functions.
const WorldComponentTypeInfo* WorldModelInstancesComponent_GetTypeInfo(void)
{
    return &InstancesTypeInfo;
}

const PropertyTable* WorldModelInstancesComponent_GetPropertyTable(void)
{
    return &InstancesTable;
}

Error WorldModelInstancesComponent_SetWorldModel(WorldModelInstancesComponent* self, const unsigned char* worldModel)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetWorldModel");
    }
    Error CheckResult = CheckProperty(InstancesProperty_WorldModel, PropertyValue_FromAssetName(worldModel));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    SetString(self, &self->_worldModel, worldModel, InstancesProperty_WorldModel);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetUsesVariant(WorldModelInstancesComponent* self, bool usesVariant)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetUsesVariant");
    }
    SetBool(self, &self->_usesVariant, usesVariant, InstancesProperty_UsesVariant);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetVariant(WorldModelInstancesComponent* self, const unsigned char* variant)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetVariant");
    }
    Error CheckResult = CheckProperty(InstancesProperty_Variant, PropertyValue_FromString(variant));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    SetString(self, &self->_variant, variant, InstancesProperty_Variant);
    return Error_CreateSuccess();
}

bool WorldModelInstancesComponent_IsOverridden(const WorldModelInstancesComponent* self, WorldModelSetting setting)
{
    switch (setting)
    {
        case WorldModelSetting_HasOutline:            return self->_overridesHasOutline;
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
        case WorldModelSetting_OmitPixelation:
        case WorldModelSetting_Count:
            break;
    }
    // Copies are never crisp, and a value outside the enum overrides nothing.
    return false;
}

Error WorldModelInstancesComponent_SetOverridden(WorldModelInstancesComponent* self, WorldModelSetting setting,
    bool isOverridden)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetOverridden");
    }
    bool* Flag = NULL;
    InstancesProperty Property = InstancesProperty_Count;
    if (!TryGetOverrideFlag(self, setting, &Flag, &Property))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "WorldModelInstancesComponent_SetOverridden: %d is not a "
            "setting of model instances (they are never crisp).", (int)setting);
    }
    SetBool(self, Flag, isOverridden, Property);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetHasOutline(WorldModelInstancesComponent* self, bool hasOutline)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetHasOutline");
    }
    SetBool(self, &self->_hasOutline, hasOutline, InstancesProperty_HasOutline);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetCastsShadows(WorldModelInstancesComponent* self, bool castsShadows)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetCastsShadows");
    }
    SetBool(self, &self->_castsShadows, castsShadows, InstancesProperty_CastsShadows);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetShadowDistance(WorldModelInstancesComponent* self, float shadowDistance)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetShadowDistance");
    }
    return SetFloat(self, &self->_shadowDistance, shadowDistance, InstancesProperty_ShadowDistance);
}

Error WorldModelInstancesComponent_SetShadowDistanceMultiplier(WorldModelInstancesComponent* self, float multiplier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetShadowDistanceMultiplier");
    }
    return SetFloat(self, &self->_shadowDistanceMultiplier, multiplier, InstancesProperty_ShadowDistanceMultiplier);
}

Error WorldModelInstancesComponent_SetIsShadowDistanceFixed(WorldModelInstancesComponent* self, bool isFixed)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetIsShadowDistanceFixed");
    }
    SetBool(self, &self->_isShadowDistanceFixed, isFixed, InstancesProperty_IsShadowDistanceFixed);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetIsShadowOnly(WorldModelInstancesComponent* self, bool isShadowOnly)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetIsShadowOnly");
    }
    SetBool(self, &self->_isShadowOnly, isShadowOnly, InstancesProperty_IsShadowOnly);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetHasTwoSidedShadows(WorldModelInstancesComponent* self, bool hasTwoSidedShadows)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetHasTwoSidedShadows");
    }
    SetBool(self, &self->_hasTwoSidedShadows, hasTwoSidedShadows, InstancesProperty_HasTwoSidedShadows);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetShadowLod(WorldModelInstancesComponent* self, uint32_t lod)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetShadowLod");
    }
    self->_shadowLod = lod;
    WorldInternal_ComponentAfterSet(self, &InstancesProperties[InstancesProperty_ShadowLod]);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetOccludesSky(WorldModelInstancesComponent* self, bool occludesSky)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetOccludesSky");
    }
    SetBool(self, &self->_occludesSky, occludesSky, InstancesProperty_OccludesSky);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetHaze(WorldModelInstancesComponent* self, float haze)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetHaze");
    }
    return SetFloat(self, &self->_haze, haze, InstancesProperty_Haze);
}

Error WorldModelInstancesComponent_SetDrawDistance(WorldModelInstancesComponent* self, float drawDistance)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetDrawDistance");
    }
    return SetFloat(self, &self->_drawDistance, drawDistance, InstancesProperty_DrawDistance);
}

Error WorldModelInstancesComponent_SetDrawDistanceMultiplier(WorldModelInstancesComponent* self, float multiplier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetDrawDistanceMultiplier");
    }
    return SetFloat(self, &self->_drawDistanceMultiplier, multiplier, InstancesProperty_DrawDistanceMultiplier);
}

Error WorldModelInstancesComponent_SetIsDrawDistanceFixed(WorldModelInstancesComponent* self, bool isFixed)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetIsDrawDistanceFixed");
    }
    SetBool(self, &self->_isDrawDistanceFixed, isFixed, InstancesProperty_IsDrawDistanceFixed);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetLodDistances(WorldModelInstancesComponent* self, const float* distances,
    size_t count)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetLodDistances");
    }
    PropertyFloatList* List = NULL;
    Error Result = PropertyFloatList_Create(distances, count, &List);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = CheckProperty(InstancesProperty_LodDistances, PropertyValue_FromFloatList(List));
    if (Result.Code != ErrorCode_Success)
    {
        PropertyFloatList_Destroy(List);
        return Result;
    }
    PropertyFloatList_Destroy(self->_lodDistances);
    self->_lodDistances = List;
    WorldInternal_ComponentAfterSet(self, &InstancesProperties[InstancesProperty_LodDistances]);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetLodDistanceMultiplier(WorldModelInstancesComponent* self, float multiplier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetLodDistanceMultiplier");
    }
    return SetFloat(self, &self->_lodDistanceMultiplier, multiplier, InstancesProperty_LodDistanceMultiplier);
}

Error WorldModelInstancesComponent_SetIsLodForced(WorldModelInstancesComponent* self, bool isForced)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetIsLodForced");
    }
    SetBool(self, &self->_isLodForced, isForced, InstancesProperty_IsLodForced);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetForcedLod(WorldModelInstancesComponent* self, uint32_t lod)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetForcedLod");
    }
    self->_forcedLod = lod;
    WorldInternal_ComponentAfterSet(self, &InstancesProperties[InstancesProperty_ForcedLod]);
    return Error_CreateSuccess();
}

Error WorldModelInstancesComponent_SetInstances(WorldModelInstancesComponent* self,
    const PropertyInstanceEntry* instances, size_t count)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetInstances");
    }
    PropertyInstanceList* List = NULL;
    Error Result = PropertyInstanceList_Create(instances, count, &List);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = CheckProperty(InstancesProperty_Instances, PropertyValue_FromInstanceList(List));
    if (Result.Code != ErrorCode_Success)
    {
        PropertyInstanceList_Destroy(List);
        return Result;
    }
    PropertyInstanceList_Destroy(self->_instances);
    self->_instances = List;
    WorldInternal_ComponentAfterSet(self, &InstancesProperties[InstancesProperty_Instances]);
    return Error_CreateSuccess();
}

WorldModelSettings WorldModelInstancesComponent_ResolveSettings(const WorldModelInstancesComponent* self,
    const WorldModelSettings* modelSettings)
{
    WorldModelSettings Settings = *modelSettings;
    if (self->_overridesHasOutline)
    {
        Settings.HasOutline = self->_hasOutline;
    }
    Settings.OmitsPixelation = false;
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

bool WorldModelInstancesComponent_ResolveLodSettings(const WorldModelInstancesComponent* self,
    const WorldModel* worldModel, WorldModelLodSettings* outSettings)
{
    size_t LodCount = WorldModel_GetLodCount(worldModel);
    size_t LastLod = (LodCount > 0U) ? (LodCount - 1U) : 0U;
    size_t OwnCount = 0U;
    const float* Own = WorldModelInstancesComponent_GetLodDistances(self, &OwnCount);
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
