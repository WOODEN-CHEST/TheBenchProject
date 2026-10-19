#include "WorldModelComponent.h"
#include "WorldInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** The format version of the model component's property table. */
#define MODEL_TABLE_VERSION 1U
/** The inspector's category. */
#define CATEGORY_MODEL ((const unsigned char*)u8"Model")


// Types.
/* The rows of the model component's table, so the setters reach their descriptors without a search. */
typedef enum ModelPropertyEnum
{
    ModelProperty_Model,
    ModelProperty_HasOutline,
    ModelProperty_OmitPixelation,
    ModelProperty_ShadowTier,
    ModelProperty_CastsShadows,
    ModelProperty_Count
} ModelProperty;


// Fields.
static const PropertyEnumEntry ShadowTierEntries[] =
{
    { .Name = u8"both", .Value = WorldShadowTier_Both },
    { .Name = u8"near", .Value = WorldShadowTier_Near },
    { .Name = u8"far", .Value = WorldShadowTier_Far },
};

static const PropertyEnumTable ShadowTierEnum =
{
    .Name = u8"shadow_tier", .Entries = ShadowTierEntries,
    .EntryCount = sizeof(ShadowTierEntries) / sizeof(ShadowTierEntries[0])
};

/* Every property, in inspector order. The wire ids are the save schema: never renumber or reuse one; a removed
 * property's id goes into the table's retired list. */
static const PropertyDescriptor ModelProperties[ModelProperty_Count] =
{
    [ModelProperty_Model] =
    {
        .Key = u8"model", .WireId = 1U, .Type = PropertyType_AssetName, PROPERTY_MEMBER(WorldModelComponent, _model),
        .Default.StringValue = NULL, .Rules = PROPERTY_RULES_STRING(WORLD_MODEL_COMPONENT_NAME_MAX_LENGTH, true),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MODEL, .DisplayName = u8"Model",
        .Description = u8"The model asset to draw; none draws nothing.", .SinceVersion = 1U
    },
    [ModelProperty_HasOutline] =
    {
        .Key = u8"has_outline", .WireId = 2U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _hasOutline), .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MODEL, .DisplayName = u8"Outline",
        .Description = u8"Draws a 1-pixel outline around the model.", .SinceVersion = 1U
    },
    [ModelProperty_OmitPixelation] =
    {
        .Key = u8"omit_pixelation", .WireId = 3U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _omitPixelation), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MODEL, .DisplayName = u8"Crisp",
        .Description = u8"Draws the model at full resolution over the pixelated image, for readable screens.",
        .SinceVersion = 1U
    },
    [ModelProperty_ShadowTier] =
    {
        .Key = u8"shadow_tier", .WireId = 4U, .Type = PropertyType_Enum,
        PROPERTY_MEMBER(WorldModelComponent, _shadowTier), .Default.EnumValue = WorldShadowTier_Both,
        .EnumTable = &ShadowTierEnum, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MODEL,
        .DisplayName = u8"Shadow range",
        .Description = u8"Which shadows the model casts: \"near\" only up close (small props), \"far\" only the coarse "
            u8"long-range one (big objects), \"both\" everywhere.",
        .SinceVersion = 1U
    },
    [ModelProperty_CastsShadows] =
    {
        .Key = u8"casts_shadows", .WireId = 5U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldModelComponent, _castsShadows), .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MODEL, .DisplayName = u8"Casts shadows",
        .Description = u8"When off, the model casts no shadow (it still receives them).", .SinceVersion = 1U
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

Error WorldModelComponent_SetModel(WorldModelComponent* self, const unsigned char* model)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldModelComponent_SetModel: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(ModelProperty_Model, PropertyValue_FromAssetName(model));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    unsigned char* Clone = WorldInternal_CloneString(model);
    Memory_Free(self->_model);
    self->_model = Clone;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_Model]);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetHasOutline(WorldModelComponent* self, bool hasOutline)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldModelComponent_SetHasOutline: self must not be NULL.");
    }

    self->_hasOutline = hasOutline;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_HasOutline]);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetOmitsPixelation(WorldModelComponent* self, bool omitPixelation)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldModelComponent_SetOmitsPixelation: self must not be NULL.");
    }

    self->_omitPixelation = omitPixelation;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_OmitPixelation]);
    return Error_CreateSuccess();
}

Error WorldModelComponent_SetShadowTier(WorldModelComponent* self, WorldShadowTier shadowTier)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldModelComponent_SetShadowTier: self must not be NULL.");
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
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldModelComponent_SetCastsShadows: self must not be NULL.");
    }

    self->_castsShadows = castsShadows;
    WorldInternal_ComponentAfterSet(self, &ModelProperties[ModelProperty_CastsShadows]);
    return Error_CreateSuccess();
}
