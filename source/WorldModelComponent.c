#include "WorldModelComponent.h"
#include "WorldInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** The format version of the model component's property table. */
#define MODEL_TABLE_VERSION 1U
/** The inspector's categories. */
#define CATEGORY_MODEL ((const unsigned char*)u8"Model")
#define CATEGORY_RENDERING ((const unsigned char*)u8"Rendering")
#define CATEGORY_DRAW_DISTANCE ((const unsigned char*)u8"Draw distance")


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
    ModelProperty_Count
} ModelProperty;


// Fields.
/* Every property, in inspector order: each override flag sits right before the value it switches on. The wire ids are
 * the save schema: never renumber or reuse one; a removed property's id goes into the table's retired list. (T10
 * renamed wire 1 from "model", a model asset, to "world_model", and made rows 2 to 5 the placement's own values behind
 * their override flags.) The next free wire id is 17. */
static const PropertyDescriptor ModelProperties[ModelProperty_Count] =
{
    [ModelProperty_WorldModel] =
    {
        .Key = u8"world_model", .WireId = 1U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(WorldModelComponent, _worldModel), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(WORLD_MODEL_COMPONENT_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MODEL, .DisplayName = u8"World model",
        .Description = u8"The world model to draw (its LOD models, draw distance, render defaults and variants); none "
            u8"draws nothing.",
        .SinceVersion = 1U
    },
    [ModelProperty_UsesVariant] =
    {
        .Key = u8"uses_variant", .WireId = 6U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _usesVariant), .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MODEL, .DisplayName = u8"Use a variant",
        .Description = u8"Draws the object with one of its world model's variants instead of the model's own "
            u8"materials.",
        .SinceVersion = 1U
    },
    [ModelProperty_Variant] =
    {
        .Key = u8"variant", .WireId = 7U, .Type = PropertyType_String, PROPERTY_MEMBER(WorldModelComponent, _variant),
        .Default.StringValue = NULL, .Rules = PROPERTY_RULES_STRING(WORLD_MODEL_VARIANT_NAME_MAX_LENGTH, true),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MODEL, .DisplayName = u8"Variant",
        .Description = u8"The variant drawn while \"Use a variant\" is on; an unknown name draws the model's own "
            u8"materials.",
        .SinceVersion = 1U
    },
    [ModelProperty_OverridesHasOutline] =
    {
        .Key = u8"overrides_has_outline", .WireId = 8U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _overridesHasOutline), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING, .DisplayName = u8"Override outline",
        .Description = u8"Uses this object's outline setting instead of its world model's.", .SinceVersion = 1U
    },
    [ModelProperty_HasOutline] =
    {
        .Key = u8"has_outline", .WireId = 2U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _hasOutline), .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_RENDERING, .DisplayName = u8"Outline",
        .Description = u8"Draws a 1-pixel outline around the model (while \"Override outline\" is on).",
        .SinceVersion = 1U
    },
    [ModelProperty_OverridesOmitPixelation] =
    {
        .Key = u8"overrides_omit_pixelation", .WireId = 9U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _overridesOmitPixelation), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING, .DisplayName = u8"Override crisp",
        .Description = u8"Uses this object's crisp setting instead of its world model's.", .SinceVersion = 1U
    },
    [ModelProperty_OmitPixelation] =
    {
        .Key = u8"omit_pixelation", .WireId = 3U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _omitPixelation), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING, .DisplayName = u8"Crisp",
        .Description = u8"Draws the model at full resolution over the pixelated image, for readable screens (while "
            u8"\"Override crisp\" is on).",
        .SinceVersion = 1U
    },
    [ModelProperty_OverridesShadowTier] =
    {
        .Key = u8"overrides_shadow_tier", .WireId = 10U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _overridesShadowTier), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING, .DisplayName = u8"Override shadow range",
        .Description = u8"Uses this object's shadow range instead of its world model's.", .SinceVersion = 1U
    },
    [ModelProperty_ShadowTier] =
    {
        .Key = u8"shadow_tier", .WireId = 4U, .Type = PropertyType_Enum,
        PROPERTY_MEMBER(WorldModelComponent, _shadowTier), .Default.EnumValue = WorldShadowTier_Both,
        .EnumTable = &WORLD_SHADOW_TIER_ENUM, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING,
        .DisplayName = u8"Shadow range",
        .Description = u8"Which shadows the model casts: \"near\" only up close (small props), \"far\" only the coarse "
            u8"long-range one (big objects), \"both\" everywhere (while \"Override shadow range\" is on).",
        .SinceVersion = 1U
    },
    [ModelProperty_OverridesCastsShadows] =
    {
        .Key = u8"overrides_casts_shadows", .WireId = 11U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _overridesCastsShadows), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_RENDERING, .DisplayName = u8"Override casts shadows",
        .Description = u8"Uses this object's \"Casts shadows\" instead of its world model's.", .SinceVersion = 1U
    },
    [ModelProperty_CastsShadows] =
    {
        .Key = u8"casts_shadows", .WireId = 5U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _castsShadows), .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_RENDERING, .DisplayName = u8"Casts shadows",
        .Description = u8"When off, the model casts no shadow; it still receives them (while \"Override casts "
            u8"shadows\" is on).",
        .SinceVersion = 1U
    },
    [ModelProperty_OverridesDrawDistance] =
    {
        .Key = u8"overrides_draw_distance", .WireId = 12U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _overridesDrawDistance), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DRAW_DISTANCE, .DisplayName = u8"Override draw distance",
        .Description = u8"Uses this object's draw distance instead of its world model's.", .SinceVersion = 1U
    },
    [ModelProperty_DrawDistance] =
    {
        .Key = u8"draw_distance", .WireId = 13U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldModelComponent, _drawDistance), .Default.FloatValue = WORLD_MODEL_DEFAULT_DRAW_DISTANCE,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DRAW_DISTANCE,
        .DisplayName = u8"Draw distance",
        .Description = u8"How far the object is seen, in metres, before the quality setting and the zoom (while "
            u8"\"Override draw distance\" is on); beyond it the object fades out.",
        .SinceVersion = 1U
    },
    [ModelProperty_DrawDistanceMultiplier] =
    {
        .Key = u8"draw_distance_multiplier", .WireId = 14U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldModelComponent, _drawDistanceMultiplier), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DRAW_DISTANCE,
        .DisplayName = u8"Draw distance multiplier",
        .Description = u8"Multiplies the draw distance in effect, the world model's or this object's: 2 is seen twice "
            u8"as far.",
        .SinceVersion = 1U
    },
    [ModelProperty_OverridesIsDrawDistanceFixed] =
    {
        .Key = u8"overrides_is_draw_distance_fixed", .WireId = 15U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _overridesIsDrawDistanceFixed), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DRAW_DISTANCE,
        .DisplayName = u8"Override fixed draw distance",
        .Description = u8"Uses this object's \"Fixed draw distance\" instead of its world model's.", .SinceVersion = 1U
    },
    [ModelProperty_IsDrawDistanceFixed] =
    {
        .Key = u8"is_draw_distance_fixed", .WireId = 16U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _isDrawDistanceFixed), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DRAW_DISTANCE, .DisplayName = u8"Fixed draw distance",
        .Description = u8"Keeps the draw distance as it is when the player lowers or raises the draw distance quality "
            u8"setting: for landmarks and gameplay-critical things (while \"Override fixed draw distance\" is on).",
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
