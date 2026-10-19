#include "WorldLightComponent.h"
#include "WorldInternal.h"


// Macros.
/** The format version of the light component's property table. */
#define LIGHT_TABLE_VERSION 1U
/** The inspector's category. */
#define CATEGORY_LIGHT ((const unsigned char*)u8"Light")


// Types.
/* The rows of the light component's table, so the setters reach their descriptors without a search. */
typedef enum LightPropertyEnum
{
    LightProperty_Color,
    LightProperty_Intensity,
    LightProperty_Range,
    LightProperty_CastsShadows,
    LightProperty_Count
} LightProperty;


// Fields.
/* Every property, in inspector order. The wire ids are the save schema: never renumber or reuse one; a removed
 * property's id goes into the table's retired list. */
static const PropertyDescriptor LightProperties[LightProperty_Count] =
{
    [LightProperty_Color] =
    {
        .Key = u8"color", .WireId = 1U, .Type = PropertyType_Color, PROPERTY_MEMBER(WorldLightComponent, _color),
        .Default.ColorValue = { 255, 255, 255, 255 }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHT,
        .DisplayName = u8"Colour", .Description = u8"The light's colour.", .SinceVersion = 1U
    },
    [LightProperty_Intensity] =
    {
        .Key = u8"intensity", .WireId = 2U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldLightComponent, _intensity), .Default.FloatValue = WORLD_LIGHT_COMPONENT_DEFAULT_INTENSITY,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHT,
        .DisplayName = u8"Intensity", .Description = u8"How bright the light is.", .SinceVersion = 1U
    },
    [LightProperty_Range] =
    {
        .Key = u8"range", .WireId = 3U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldLightComponent, _range),
        .Default.FloatValue = WORLD_LIGHT_COMPONENT_DEFAULT_RANGE, .Rules = PROPERTY_RULES_AT_LEAST(0.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHT, .DisplayName = u8"Range",
        .Description = u8"How far the light reaches, in world units: it fades to nothing there.", .SinceVersion = 1U
    },
    [LightProperty_CastsShadows] =
    {
        .Key = u8"casts_shadows", .WireId = 4U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldLightComponent, _castsShadows), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHT, .DisplayName = u8"Casts shadows",
        .Description = u8"Lets the light cast shadows; the shadow-casting light nearest the camera gets them.",
        .SinceVersion = 1U
    },
};

static const PropertyTable LightTable =
{
    .Name = u8"light_component",
    .Version = LIGHT_TABLE_VERSION,
    .StructSize = sizeof(WorldLightComponent),
    .Descriptors = LightProperties,
    .DescriptorCount = LightProperty_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = NULL
};


// Static functions.
static Error ConstructLight(WorldComponent* component)
{
    return Properties_Construct(&LightTable, component);
}

static void DeconstructLight(WorldComponent* component)
{
    Properties_Deconstruct(&LightTable, component);
}

/* Checks @p value against one row's rules (so the setters' rules live in the table only). */
static Error CheckProperty(LightProperty property, PropertyValue value)
{
    return Properties_CheckValue(&LightTable, &LightProperties[property], &value);
}


// Fields: the registry entry (it refers to the functions above).
static const WorldComponentTypeInfo LightTypeInfo =
{
    .Type = WorldComponentType_Light,
    .Name = u8"light",
    .Size = sizeof(WorldLightComponent),
    .Table = &LightTable,
    .IsTransformDriver = false,
    .Construct = ConstructLight,
    .Deconstruct = DeconstructLight,
    .OnAttach = NULL,
    .OnDetach = NULL,
    .OnPropertySet = NULL
};


// Public functions.
const WorldComponentTypeInfo* WorldLightComponent_GetTypeInfo(void)
{
    return &LightTypeInfo;
}

const PropertyTable* WorldLightComponent_GetPropertyTable(void)
{
    return &LightTable;
}

Error WorldLightComponent_SetColor(WorldLightComponent* self, Color color)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldLightComponent_SetColor: self must not be NULL.");
    }

    self->_color = color;
    WorldInternal_ComponentAfterSet(self, &LightProperties[LightProperty_Color]);
    return Error_CreateSuccess();
}

Error WorldLightComponent_SetIntensity(WorldLightComponent* self, float intensity)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldLightComponent_SetIntensity: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(LightProperty_Intensity, PropertyValue_FromFloat(intensity));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_intensity = intensity;
    WorldInternal_ComponentAfterSet(self, &LightProperties[LightProperty_Intensity]);
    return Error_CreateSuccess();
}

Error WorldLightComponent_SetRange(WorldLightComponent* self, float range)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldLightComponent_SetRange: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(LightProperty_Range, PropertyValue_FromFloat(range));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_range = range;
    WorldInternal_ComponentAfterSet(self, &LightProperties[LightProperty_Range]);
    return Error_CreateSuccess();
}

Error WorldLightComponent_SetCastsShadows(WorldLightComponent* self, bool castsShadows)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldLightComponent_SetCastsShadows: self must not be NULL.");
    }

    self->_castsShadows = castsShadows;
    WorldInternal_ComponentAfterSet(self, &LightProperties[LightProperty_CastsShadows]);
    return Error_CreateSuccess();
}
