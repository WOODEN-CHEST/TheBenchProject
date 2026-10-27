#include "WorldAmbientZoneComponent.h"
#include "WorldInternal.h"


// Macros.
/** The format version of the ambient zone component's property table. */
#define AMBIENT_ZONE_TABLE_VERSION 1U
/** The inspector's categories. */
#define CATEGORY_SHAPE ((const unsigned char*)u8"Shape")
#define CATEGORY_LIGHT ((const unsigned char*)u8"Light")
#define CATEGORY_EYE ((const unsigned char*)u8"Eye and air")


// Types.
/* The rows of the ambient zone component's table, so the setters reach their descriptors without a search. */
typedef enum AmbientZonePropertyEnum
{
    AmbientZoneProperty_Shape,
    AmbientZoneProperty_HalfExtents,
    AmbientZoneProperty_Radius,
    AmbientZoneProperty_BlendDistance,
    AmbientZoneProperty_AmbientColor,
    AmbientZoneProperty_AmbientIntensity,
    AmbientZoneProperty_SkyAmbientScale,
    AmbientZoneProperty_ExposureBias,
    AmbientZoneProperty_Priority,
    AmbientZoneProperty_Haze,
    AmbientZoneProperty_Count
} AmbientZoneProperty;


// Fields.
static const PropertyEnumEntry ShapeEntries[] =
{
    { .Name = u8"box", .Value = WorldAmbientZoneShape_Box },
    { .Name = u8"sphere", .Value = WorldAmbientZoneShape_Sphere },
};

const PropertyEnumTable WORLD_AMBIENT_ZONE_SHAPE_ENUM =
{
    .Name = u8"ambient_zone_shape", .Entries = ShapeEntries, .EntryCount = sizeof(ShapeEntries) / sizeof(ShapeEntries[0])
};

/* Every property, in inspector order. The wire ids are the save schema: never renumber or reuse one; a removed
 * property's id goes into the table's retired list. The next free wire id is 11. */
static const PropertyDescriptor AmbientZoneProperties[AmbientZoneProperty_Count] =
{
    [AmbientZoneProperty_Shape] =
    {
        .Key = u8"shape", .WireId = 1U, .Type = PropertyType_Enum, PROPERTY_MEMBER(WorldAmbientZoneComponent, _shape),
        .Default.EnumValue = WorldAmbientZoneShape_Box, .EnumTable = &WORLD_AMBIENT_ZONE_SHAPE_ENUM,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SHAPE, .DisplayName = u8"Shape",
        .Description = u8"A box (its half extents) or a sphere (its radius), around the object's position, turned and "
            u8"scaled with the object.", .SinceVersion = 1U
    },
    [AmbientZoneProperty_HalfExtents] =
    {
        .Key = u8"half_extents", .WireId = 2U, .Type = PropertyType_Vector3,
        PROPERTY_MEMBER(WorldAmbientZoneComponent, _halfExtents), .Default.Vector3Value = { 2.0f, 1.5f, 2.0f },
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SHAPE,
        .DisplayName = u8"Half extents",
        .Description = u8"A box's half size along each of the object's axes, in the object's own units (a 4 x 3 x 4 "
            u8"room by default).", .SinceVersion = 1U
    },
    [AmbientZoneProperty_Radius] =
    {
        .Key = u8"radius", .WireId = 3U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldAmbientZoneComponent, _radius),
        .Default.FloatValue = 2.0f, .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SHAPE, .DisplayName = u8"Radius",
        .Description = u8"A sphere's radius, in the object's own units.", .SinceVersion = 1U
    },
    [AmbientZoneProperty_BlendDistance] =
    {
        .Key = u8"blend_distance", .WireId = 4U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldAmbientZoneComponent, _blendDistance), .Default.FloatValue = 0.1f,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SHAPE,
        .DisplayName = u8"Blend distance",
        .Description = u8"How far inward from its surface the zone fades in, in metres: its light never reaches past its "
            u8"surface (0: a hard edge). Make a room's zone reach this far into its walls, floor and ceiling, and "
            u8"everything in the room takes its whole light.", .SinceVersion = 1U
    },
    [AmbientZoneProperty_AmbientColor] =
    {
        .Key = u8"ambient_color", .WireId = 5U, .Type = PropertyType_Color,
        PROPERTY_MEMBER(WorldAmbientZoneComponent, _ambientColor), .Default.ColorValue = { 255, 200, 150, 255 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHT, .DisplayName = u8"Ambient colour",
        .Description = u8"The colour of the light the room gives everything in it (warm white by default).",
        .SinceVersion = 1U
    },
    [AmbientZoneProperty_AmbientIntensity] =
    {
        .Key = u8"ambient_intensity", .WireId = 6U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldAmbientZoneComponent, _ambientIntensity),
        .Default.FloatValue = WORLD_AMBIENT_ZONE_DEFAULT_INTENSITY, .Rules = PROPERTY_RULES_AT_LEAST(0.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHT, .DisplayName = u8"Ambient intensity",
        .Description = u8"How bright the room's own light is, as a white surface shows it in the game's light units "
            u8"(a clear noon's sky gives a surface facing up about 0.07, a moonless night 0.005; 0.02, the default, is a "
            u8"lit office, 0.004 a dim living room).",
        .SinceVersion = 1U
    },
    [AmbientZoneProperty_SkyAmbientScale] =
    {
        .Key = u8"sky_ambient_scale", .WireId = 7U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldAmbientZoneComponent, _skyAmbientScale), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHT,
        .DisplayName = u8"Sky light scale",
        .Description = u8"Multiplies the sky's light inside, after what the roof already keeps out (0: no sky light at "
            u8"all, a cellar).", .SinceVersion = 1U
    },
    [AmbientZoneProperty_ExposureBias] =
    {
        .Key = u8"exposure_bias", .WireId = 8U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldAmbientZoneComponent, _exposureBias), .Default.FloatValue = 0.0f,
        .Rules = PROPERTY_RULES_BETWEEN(-WORLD_AMBIENT_ZONE_MAX_EXPOSURE_BIAS, WORLD_AMBIENT_ZONE_MAX_EXPOSURE_BIAS),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_EYE, .DisplayName = u8"Exposure bias",
        .Description = u8"Stops added to the eye's exposure while the camera is inside (1: twice as bright, -1: half).",
        .SinceVersion = 1U
    },
    [AmbientZoneProperty_Priority] =
    {
        .Key = u8"priority", .WireId = 9U, .Type = PropertyType_Int32,
        PROPERTY_MEMBER(WorldAmbientZoneComponent, _priority), .Default.Int32Value = 0, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_LIGHT, .DisplayName = u8"Priority",
        .Description = u8"Where zones overlap the higher priority wins; equal ones blend.", .SinceVersion = 1U
    },
    [AmbientZoneProperty_Haze] =
    {
        .Key = u8"haze", .WireId = 10U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldAmbientZoneComponent, _haze),
        .Default.FloatValue = 1.0f, .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_AMBIENT_ZONE_MAX_HAZE),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_EYE, .DisplayName = u8"Haze",
        .Description = u8"How hazy the zone's air is: it multiplies the haze of the part of a view inside it (1 the "
            u8"outside air's, 0 a room's still air).", .SinceVersion = 1U
    },
};

static const PropertyTable AmbientZoneTable =
{
    .Name = u8"ambient_zone_component",
    .Version = AMBIENT_ZONE_TABLE_VERSION,
    .StructSize = sizeof(WorldAmbientZoneComponent),
    .Descriptors = AmbientZoneProperties,
    .DescriptorCount = AmbientZoneProperty_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = NULL
};


// Static functions.
static Error ConstructAmbientZone(WorldComponent* component)
{
    return Properties_Construct(&AmbientZoneTable, component);
}

static void DeconstructAmbientZone(WorldComponent* component)
{
    Properties_Deconstruct(&AmbientZoneTable, component);
}

/* Checks @p value against one row's rules (so the setters' rules live in the table only). */
static Error CheckProperty(AmbientZoneProperty property, PropertyValue value)
{
    return Properties_CheckValue(&AmbientZoneTable, &AmbientZoneProperties[property], &value);
}

static Error CreateNullSelfError(const char* function)
{
    return Error_Construct4(ErrorCode_IllegalArgument, "%s: self must not be NULL.", function);
}

/* Checks a float against its row, then stores it and advances the revision. */
static Error SetFloat(WorldAmbientZoneComponent* self, float* field, float value, AmbientZoneProperty property)
{
    Error CheckResult = CheckProperty(property, PropertyValue_FromFloat(value));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    *field = value;
    WorldInternal_ComponentAfterSet(self, &AmbientZoneProperties[property]);
    return Error_CreateSuccess();
}


// Fields: the registry entry (it refers to the functions above).
static const WorldComponentTypeInfo AmbientZoneTypeInfo =
{
    .Type = WorldComponentType_AmbientZone,
    .Name = u8"ambient_zone",
    .Size = sizeof(WorldAmbientZoneComponent),
    .Table = &AmbientZoneTable,
    .IsTransformDriver = false,
    .Construct = ConstructAmbientZone,
    .Deconstruct = DeconstructAmbientZone,
    .OnAttach = NULL,
    .OnDetach = NULL,
    .OnPropertySet = NULL
};


// Public functions.
const WorldComponentTypeInfo* WorldAmbientZoneComponent_GetTypeInfo(void)
{
    return &AmbientZoneTypeInfo;
}

const PropertyTable* WorldAmbientZoneComponent_GetPropertyTable(void)
{
    return &AmbientZoneTable;
}

Error WorldAmbientZoneComponent_SetShape(WorldAmbientZoneComponent* self, WorldAmbientZoneShape shape)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldAmbientZoneComponent_SetShape");
    }
    Error CheckResult = CheckProperty(AmbientZoneProperty_Shape, PropertyValue_FromEnum((int32_t)shape));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    self->_shape = shape;
    WorldInternal_ComponentAfterSet(self, &AmbientZoneProperties[AmbientZoneProperty_Shape]);
    return Error_CreateSuccess();
}

Error WorldAmbientZoneComponent_SetHalfExtents(WorldAmbientZoneComponent* self, Vector3 halfExtents)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldAmbientZoneComponent_SetHalfExtents");
    }
    Error CheckResult = CheckProperty(AmbientZoneProperty_HalfExtents, PropertyValue_FromVector3(halfExtents));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    self->_halfExtents = halfExtents;
    WorldInternal_ComponentAfterSet(self, &AmbientZoneProperties[AmbientZoneProperty_HalfExtents]);
    return Error_CreateSuccess();
}

Error WorldAmbientZoneComponent_SetRadius(WorldAmbientZoneComponent* self, float radius)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldAmbientZoneComponent_SetRadius");
    }
    return SetFloat(self, &self->_radius, radius, AmbientZoneProperty_Radius);
}

Error WorldAmbientZoneComponent_SetBlendDistance(WorldAmbientZoneComponent* self, float blendDistance)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldAmbientZoneComponent_SetBlendDistance");
    }
    return SetFloat(self, &self->_blendDistance, blendDistance, AmbientZoneProperty_BlendDistance);
}

Error WorldAmbientZoneComponent_SetAmbientColor(WorldAmbientZoneComponent* self, Color color)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldAmbientZoneComponent_SetAmbientColor");
    }
    self->_ambientColor = color;
    WorldInternal_ComponentAfterSet(self, &AmbientZoneProperties[AmbientZoneProperty_AmbientColor]);
    return Error_CreateSuccess();
}

Error WorldAmbientZoneComponent_SetAmbientIntensity(WorldAmbientZoneComponent* self, float intensity)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldAmbientZoneComponent_SetAmbientIntensity");
    }
    return SetFloat(self, &self->_ambientIntensity, intensity, AmbientZoneProperty_AmbientIntensity);
}

Error WorldAmbientZoneComponent_SetSkyAmbientScale(WorldAmbientZoneComponent* self, float scale)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldAmbientZoneComponent_SetSkyAmbientScale");
    }
    return SetFloat(self, &self->_skyAmbientScale, scale, AmbientZoneProperty_SkyAmbientScale);
}

Error WorldAmbientZoneComponent_SetExposureBias(WorldAmbientZoneComponent* self, float stops)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldAmbientZoneComponent_SetExposureBias");
    }
    return SetFloat(self, &self->_exposureBias, stops, AmbientZoneProperty_ExposureBias);
}

Error WorldAmbientZoneComponent_SetPriority(WorldAmbientZoneComponent* self, int32_t priority)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldAmbientZoneComponent_SetPriority");
    }
    self->_priority = priority;
    WorldInternal_ComponentAfterSet(self, &AmbientZoneProperties[AmbientZoneProperty_Priority]);
    return Error_CreateSuccess();
}

Error WorldAmbientZoneComponent_SetHaze(WorldAmbientZoneComponent* self, float haze)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldAmbientZoneComponent_SetHaze");
    }
    return SetFloat(self, &self->_haze, haze, AmbientZoneProperty_Haze);
}
