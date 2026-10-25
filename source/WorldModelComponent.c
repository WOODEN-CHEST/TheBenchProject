#include "WorldModelComponent.h"
#include "WorldInternal.h"
#include "WorldModelComponentInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** The format version of the model component's property table. */
#define MODEL_TABLE_VERSION 1U


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
    ModelProperty_OverridesShadowTier,
    ModelProperty_ShadowTier,
    ModelProperty_OverridesCastsShadows,
    ModelProperty_CastsShadows,
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


// Fields.
/* Every property, in inspector order: each override flag sits right before the value it switches on. The rows' keys,
 * types, defaults, rules and texts are shared with the model instances component (source/WorldModelComponentInternal.h,
 * B03). The wire ids are the save schema: never renumber or reuse one; a removed property's id goes into the table's
 * retired list. (T10 renamed wire 1 from "model", a model asset, to "world_model", and made rows 2 to 5 the placement's
 * own values behind their override flags; T11 added the LOD rows, 17 to 21.) The next free wire id is 22. */
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
    [ModelProperty_OverridesShadowTier] =
    {
        WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_SHADOW_TIER, .WireId = 10U,
        PROPERTY_MEMBER(WorldModelComponent, _overridesShadowTier), .SinceVersion = 1U
    },
    [ModelProperty_ShadowTier] =
    {
        WORLD_MODEL_PLACEMENT_ROW_SHADOW_TIER, .WireId = 4U, PROPERTY_MEMBER(WorldModelComponent, _shadowTier),
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

static const PropertyTable ModelTable =
{
    .Name = u8"model_component",
    .Version = MODEL_TABLE_VERSION,
    .StructSize = sizeof(WorldModelComponent),
    .Descriptors = ModelProperties,
    .DescriptorCount = ModelProperty_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = NULL
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
        case WorldModelSetting_ShadowTier:
            *outFlag = &self->_overridesShadowTier;
            *outProperty = ModelProperty_OverridesShadowTier;
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
        case WorldModelSetting_HasOutline:          return self->_overridesHasOutline;
        case WorldModelSetting_OmitPixelation:      return self->_overridesOmitPixelation;
        case WorldModelSetting_ShadowTier:          return self->_overridesShadowTier;
        case WorldModelSetting_CastsShadows:        return self->_overridesCastsShadows;
        case WorldModelSetting_DrawDistance:        return self->_overridesDrawDistance;
        case WorldModelSetting_IsDrawDistanceFixed: return self->_overridesIsDrawDistanceFixed;
        case WorldModelSetting_LodDistances:        return self->_overridesLodDistances;
        case WorldModelSetting_Count:               break;
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

Error WorldModelComponent_SetShadowTier(WorldModelComponent* self, WorldShadowTier shadowTier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldModelComponent_SetShadowTier");
    }
    Error CheckResult = CheckProperty(ModelProperty_ShadowTier, PropertyValue_FromEnum((int32_t)shadowTier));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    self->_shadowTier = shadowTier;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_ShadowTier]);
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
    if (self->_overridesShadowTier)
    {
        Settings.ShadowTier = self->_shadowTier;
    }
    if (self->_overridesCastsShadows)
    {
        Settings.CastsShadows = self->_castsShadows;
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
