#include "WorldLightComponent.h"
#include <math.h>
#include "WorldInternal.h"
#include "WorldObject.h"


// Macros.
/** The format version of the light component's property table. */
#define LIGHT_TABLE_VERSION 1U
/** The inspector's categories. */
#define CATEGORY_LIGHT ((const unsigned char*)u8"Light")
#define CATEGORY_CONE ((const unsigned char*)u8"Cone")
#define CATEGORY_FLICKER ((const unsigned char*)u8"Flicker")
/** The cone angles' rule: strictly between 0 and WORLD_LIGHT_COMPONENT_MAX_CONE_ANGLE. */
#define CONE_ANGLE_RULES \
    { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true, .IsMaximumExclusive = true, \
      .Maximum = WORLD_LIGHT_COMPONENT_MAX_CONE_ANGLE }


// Types.
/* The rows of the light component's table, so the setters reach their descriptors without a search. */
typedef enum LightPropertyEnum
{
    LightProperty_IsOn,
    LightProperty_Type,
    LightProperty_Color,
    LightProperty_Intensity,
    LightProperty_Range,
    LightProperty_InnerAngle,
    LightProperty_OuterAngle,
    LightProperty_CastsShadows,
    LightProperty_Flicker,
    LightProperty_FlickerAmount,
    LightProperty_FlickerSpeed,
    LightProperty_Count
} LightProperty;


// Fields.
static const PropertyEnumEntry TypeEntries[] =
{
    { .Name = u8"point", .Value = WorldLightType_Point },
    { .Name = u8"spot", .Value = WorldLightType_Spot },
};

const PropertyEnumTable WORLD_LIGHT_TYPE_ENUM =
{
    .Name = u8"light_type", .Entries = TypeEntries, .EntryCount = sizeof(TypeEntries) / sizeof(TypeEntries[0])
};

static const PropertyEnumEntry FlickerEntries[] =
{
    { .Name = u8"none", .Value = WorldLightFlicker_None },
    { .Name = u8"candle", .Value = WorldLightFlicker_Candle },
    { .Name = u8"fluorescent", .Value = WorldLightFlicker_Fluorescent },
    { .Name = u8"broken", .Value = WorldLightFlicker_Broken },
};

const PropertyEnumTable WORLD_LIGHT_FLICKER_ENUM =
{
    .Name = u8"light_flicker", .Entries = FlickerEntries,
    .EntryCount = sizeof(FlickerEntries) / sizeof(FlickerEntries[0])
};

/* Every property, in inspector order. The wire ids are the save schema: never renumber or reuse one; a removed
 * property's id goes into the table's retired list. The next free wire id is 12. */
static const PropertyDescriptor LightProperties[LightProperty_Count] =
{
    [LightProperty_IsOn] =
    {
        .Key = u8"is_on", .WireId = 8U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WorldLightComponent, _isOn),
        .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHT, .DisplayName = u8"On",
        .Description = u8"Whether the light is on: off it lights nothing and casts no shadow, and keeps its other "
            u8"settings (a lamp's switch).", .SinceVersion = 1U
    },
    [LightProperty_Type] =
    {
        .Key = u8"type", .WireId = 5U, .Type = PropertyType_Enum, PROPERTY_MEMBER(WorldLightComponent, _type),
        .Default.EnumValue = WorldLightType_Point, .EnumTable = &WORLD_LIGHT_TYPE_ENUM, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_LIGHT, .DisplayName = u8"Type",
        .Description = u8"\"point\" shines every way; \"spot\" shines in a cone along the object's forward (+Z after its "
            u8"rotation).", .SinceVersion = 1U
    },
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
    [LightProperty_InnerAngle] =
    {
        .Key = u8"inner_angle", .WireId = 6U, .Type = PropertyType_Angle,
        PROPERTY_MEMBER(WorldLightComponent, _innerAngle), .Default.FloatValue = WORLD_LIGHT_COMPONENT_DEFAULT_INNER_ANGLE,
        .Rules = CONE_ANGLE_RULES, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_CONE,
        .DisplayName = u8"Inner angle",
        .Description = u8"A spot's full light reaches this far from its axis, in degrees (above 0, below 90; at most the "
            u8"outer angle: past it, it counts as the outer one, a hard edge).", .SinceVersion = 1U
    },
    [LightProperty_OuterAngle] =
    {
        .Key = u8"outer_angle", .WireId = 7U, .Type = PropertyType_Angle,
        PROPERTY_MEMBER(WorldLightComponent, _outerAngle), .Default.FloatValue = WORLD_LIGHT_COMPONENT_DEFAULT_OUTER_ANGLE,
        .Rules = CONE_ANGLE_RULES, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_CONE,
        .DisplayName = u8"Outer angle",
        .Description = u8"A spot lights nothing past this angle from its axis, in degrees (above 0, below 90); its light "
            u8"fades smoothly from the inner angle to it.", .SinceVersion = 1U
    },
    [LightProperty_CastsShadows] =
    {
        .Key = u8"casts_shadows", .WireId = 4U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldLightComponent, _castsShadows), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHT, .DisplayName = u8"Casts shadows",
        .Description = u8"Lets the light cast shadows: the shadow-casting point light nearest the camera gets them, and "
            u8"the strongest few shadow-casting spots around it.", .SinceVersion = 1U
    },
    [LightProperty_Flicker] =
    {
        .Key = u8"flicker", .WireId = 9U, .Type = PropertyType_Enum, PROPERTY_MEMBER(WorldLightComponent, _flicker),
        .Default.EnumValue = WorldLightFlicker_None, .EnumTable = &WORLD_LIGHT_FLICKER_ENUM,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_FLICKER, .DisplayName = u8"Flicker",
        .Description = u8"\"none\" (steady), \"candle\" (a soft, random wavering), \"fluorescent\" (a fast shimmer with "
            u8"rare dips) or \"broken\" (long dark gaps, stuttering).", .SinceVersion = 1U
    },
    [LightProperty_FlickerAmount] =
    {
        .Key = u8"flicker_amount", .WireId = 10U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldLightComponent, _flickerAmount),
        .Default.FloatValue = WORLD_LIGHT_COMPONENT_DEFAULT_FLICKER_AMOUNT, .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_FLICKER, .DisplayName = u8"Flicker amount",
        .Description = u8"How deep the flicker goes: the intensity times a value between 1 minus this and 1 (1 reaches "
            u8"darkness).", .SinceVersion = 1U
    },
    [LightProperty_FlickerSpeed] =
    {
        .Key = u8"flicker_speed", .WireId = 11U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldLightComponent, _flickerSpeed),
        .Default.FloatValue = WORLD_LIGHT_COMPONENT_DEFAULT_FLICKER_SPEED,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, (double)WORLD_LIGHT_FLICKER_MAX_SPEED), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_FLICKER, .DisplayName = u8"Flicker speed",
        .Description = u8"The flicker's tempo: 1 its kind's own, 2 twice as fast, 0 held still.", .SinceVersion = 1U
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

/* The error a setter returns when @p self is NULL, naming the setter. */
static Error NullSelfError(const char* setter)
{
    return Error_Construct2(ErrorCode_IllegalArgument, setter);
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

Error WorldLightComponent_SetIsOn(WorldLightComponent* self, bool isOn)
{
    if (self == NULL)
    {
        return NullSelfError("WorldLightComponent_SetIsOn: self must not be NULL.");
    }

    self->_isOn = isOn;
    WorldInternal_ComponentAfterSet(self, &LightProperties[LightProperty_IsOn]);
    return Error_CreateSuccess();
}

Error WorldLightComponent_SetType(WorldLightComponent* self, WorldLightType type)
{
    if (self == NULL)
    {
        return NullSelfError("WorldLightComponent_SetType: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(LightProperty_Type, PropertyValue_FromEnum((int32_t)type));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_type = type;
    WorldInternal_ComponentAfterSet(self, &LightProperties[LightProperty_Type]);
    return Error_CreateSuccess();
}

Error WorldLightComponent_SetColor(WorldLightComponent* self, Color color)
{
    if (self == NULL)
    {
        return NullSelfError("WorldLightComponent_SetColor: self must not be NULL.");
    }

    self->_color = color;
    WorldInternal_ComponentAfterSet(self, &LightProperties[LightProperty_Color]);
    return Error_CreateSuccess();
}

Error WorldLightComponent_SetIntensity(WorldLightComponent* self, float intensity)
{
    if (self == NULL)
    {
        return NullSelfError("WorldLightComponent_SetIntensity: self must not be NULL.");
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
        return NullSelfError("WorldLightComponent_SetRange: self must not be NULL.");
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

WorldLightCone WorldLightComponent_GetCone(const WorldLightComponent* self)
{
    return (WorldLightCone){ .InnerAngle = fminf(self->_innerAngle, self->_outerAngle), .OuterAngle = self->_outerAngle };
}

Error WorldLightComponent_SetCone(WorldLightComponent* self, float innerAngle, float outerAngle)
{
    if (self == NULL)
    {
        return NullSelfError("WorldLightComponent_SetCone: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(LightProperty_InnerAngle, PropertyValue_FromAngle(innerAngle));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    CheckResult = CheckProperty(LightProperty_OuterAngle, PropertyValue_FromAngle(outerAngle));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    if (innerAngle > outerAngle)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldLightComponent_SetCone: the inner angle must not exceed the outer angle.");
    }

    self->_innerAngle = innerAngle;
    self->_outerAngle = outerAngle;
    // Both rows changed: the hook's "anything may have changed" form.
    WorldInternal_ComponentAfterSet(self, NULL);
    return Error_CreateSuccess();
}

Error WorldLightComponent_SetCastsShadows(WorldLightComponent* self, bool castsShadows)
{
    if (self == NULL)
    {
        return NullSelfError("WorldLightComponent_SetCastsShadows: self must not be NULL.");
    }

    self->_castsShadows = castsShadows;
    WorldInternal_ComponentAfterSet(self, &LightProperties[LightProperty_CastsShadows]);
    return Error_CreateSuccess();
}

Error WorldLightComponent_SetFlicker(WorldLightComponent* self, WorldLightFlicker flicker)
{
    if (self == NULL)
    {
        return NullSelfError("WorldLightComponent_SetFlicker: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(LightProperty_Flicker, PropertyValue_FromEnum((int32_t)flicker));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_flicker = flicker;
    WorldInternal_ComponentAfterSet(self, &LightProperties[LightProperty_Flicker]);
    return Error_CreateSuccess();
}

Error WorldLightComponent_SetFlickerAmount(WorldLightComponent* self, float amount)
{
    if (self == NULL)
    {
        return NullSelfError("WorldLightComponent_SetFlickerAmount: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(LightProperty_FlickerAmount, PropertyValue_FromFloat(amount));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_flickerAmount = amount;
    WorldInternal_ComponentAfterSet(self, &LightProperties[LightProperty_FlickerAmount]);
    return Error_CreateSuccess();
}

Error WorldLightComponent_SetFlickerSpeed(WorldLightComponent* self, float speed)
{
    if (self == NULL)
    {
        return NullSelfError("WorldLightComponent_SetFlickerSpeed: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(LightProperty_FlickerSpeed, PropertyValue_FromFloat(speed));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_flickerSpeed = speed;
    WorldInternal_ComponentAfterSet(self, &LightProperties[LightProperty_FlickerSpeed]);
    return Error_CreateSuccess();
}

float WorldLightComponent_GetFlickerFactor(const WorldLightComponent* self, double seconds)
{
    const WorldObject* Owner = WorldComponent_GetOwner(&self->Base);
    uint64_t Seed = (Owner != NULL) ? WorldObject_GetId(Owner) : 0U;
    return WorldLightFlicker_GetFactor(self->_flicker, self->_flickerAmount, self->_flickerSpeed, seconds, Seed);
}
