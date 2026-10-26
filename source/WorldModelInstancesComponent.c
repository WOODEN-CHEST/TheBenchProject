#include "WorldModelInstancesComponent.h"
#include "WorldInternal.h"
#include "WorldModelComponentInternal.h"
#include "WorldObject.h"
#include "wr/WRMemory.h"


// Macros.
/** The format version of the model instances component's property table. */
#define INSTANCES_TABLE_VERSION 1U
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
    InstancesProperty_OverridesShadowTier,
    InstancesProperty_ShadowTier,
    InstancesProperty_OverridesCastsShadows,
    InstancesProperty_CastsShadows,
    InstancesProperty_OverridesHaze,
    InstancesProperty_Haze,
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
 * ids are the save schema: never renumber or reuse one. (T35 added the haze's pair, 23 and 24, as in the model
 * component.) The next free wire id is 25. */
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
    [InstancesProperty_OverridesShadowTier] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_SHADOW_TIER, .WireId = 10U,
        PROPERTY_MEMBER(WorldModelInstancesComponent, _overridesShadowTier), .SinceVersion = 1U
    },
    [InstancesProperty_ShadowTier] =
    {
        WORLD_MODEL_PLACEMENT_ROW_SHADOW_TIER, .WireId = 4U, PROPERTY_MEMBER(WorldModelInstancesComponent, _shadowTier),
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

static const PropertyTable InstancesTable =
{
    .Name = u8"model_instances_component",
    .Version = INSTANCES_TABLE_VERSION,
    .StructSize = sizeof(WorldModelInstancesComponent),
    .Descriptors = InstancesProperties,
    .DescriptorCount = InstancesProperty_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = NULL
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

/* Replaces an owned string field with a clone of @p value (checked by the caller), then reports the change. */
static void SetString(WorldModelInstancesComponent* self, unsigned char** field, const unsigned char* value,
    InstancesProperty property)
{
    unsigned char* Clone = WorldInternal_CloneString(value);
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
        case WorldModelSetting_ShadowTier:
            *outFlag = &self->_overridesShadowTier;
            *outProperty = InstancesProperty_OverridesShadowTier;
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
        case WorldModelSetting_HasOutline:          return self->_overridesHasOutline;
        case WorldModelSetting_ShadowTier:          return self->_overridesShadowTier;
        case WorldModelSetting_CastsShadows:        return self->_overridesCastsShadows;
        case WorldModelSetting_DrawDistance:        return self->_overridesDrawDistance;
        case WorldModelSetting_IsDrawDistanceFixed: return self->_overridesIsDrawDistanceFixed;
        case WorldModelSetting_LodDistances:        return self->_overridesLodDistances;
        case WorldModelSetting_Haze:                return self->_overridesHaze;
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

Error WorldModelInstancesComponent_SetShadowTier(WorldModelInstancesComponent* self, WorldShadowTier shadowTier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelInstancesComponent_SetShadowTier");
    }
    Error CheckResult = CheckProperty(InstancesProperty_ShadowTier, PropertyValue_FromEnum((int32_t)shadowTier));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    self->_shadowTier = shadowTier;
    WorldInternal_ComponentAfterSet(self, &InstancesProperties[InstancesProperty_ShadowTier]);
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
    if (self->_overridesShadowTier)
    {
        Settings.ShadowTier = self->_shadowTier;
    }
    if (self->_overridesCastsShadows)
    {
        Settings.CastsShadows = self->_castsShadows;
    }
    if (self->_overridesHaze)
    {
        Settings.Haze = self->_haze;
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
