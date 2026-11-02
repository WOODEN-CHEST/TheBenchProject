#include "WorldScreenComponent.h"
#include "WorldInternal.h"
#include "WorldModel.h"


// Macros.
/** The format version of the screen component's property table. */
#define SCREEN_TABLE_VERSION 1U
/** The inspector's category. */
#define CATEGORY_SCREEN ((const unsigned char*)u8"Screen")


// Types.
/* The rows of the screen component's table, so the setters reach their descriptors without a search. */
typedef enum ScreenPropertyEnum
{
    ScreenProperty_Surface,
    ScreenProperty_IsPowered,
    ScreenProperty_Brightness,
    ScreenProperty_Count
} ScreenProperty;


// Fields.
/* Every property, in inspector order. The wire ids are the save schema: never renumber or reuse one; a removed
 * property's id goes into the table's retired list. The next free wire id is 4. */
static const PropertyDescriptor ScreenProperties[ScreenProperty_Count] =
{
    [ScreenProperty_Surface] =
    {
        .Key = u8"surface", .WireId = 1U, .Type = PropertyType_LocalName,
        PROPERTY_MEMBER(WorldScreenComponent, _surface), .Default.StringValue = u8"screen",
        .Rules = PROPERTY_RULES_STRING(WORLD_MODEL_SURFACE_NAME_MAX_LENGTH, false), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SCREEN, .DisplayName = u8"Surface",
        .Description = u8"Which surface of the object's world model shows the picture (its \"surfaces\").",
        .SinceVersion = 1U
    },
    [ScreenProperty_IsPowered] =
    {
        .Key = u8"is_powered", .WireId = 2U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldScreenComponent, _isPowered), .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SCREEN, .DisplayName = u8"Powered",
        .Description = u8"Whether the screen is on; off, it shows no picture.", .SinceVersion = 1U
    },
    [ScreenProperty_Brightness] =
    {
        .Key = u8"brightness", .WireId = 3U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldScreenComponent, _brightness), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_SCREEN_COMPONENT_MAX_BRIGHTNESS), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SCREEN, .DisplayName = u8"Brightness",
        .Description = u8"Times the surface's emission: how brightly a screen's picture glows (a paper takes none).",
        .SinceVersion = 1U
    },
};

static const PropertyTable ScreenTable =
{
    .Name = u8"screen_component",
    .Version = SCREEN_TABLE_VERSION,
    .StructSize = sizeof(WorldScreenComponent),
    .Descriptors = ScreenProperties,
    .DescriptorCount = ScreenProperty_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = NULL
};


// Static functions.
static Error ConstructScreen(WorldComponent* component)
{
    return Properties_Construct(&ScreenTable, component);
}

static void DeconstructScreen(WorldComponent* component)
{
    Properties_Deconstruct(&ScreenTable, component);
}


// Fields: the registry entry (it refers to the functions above).
static const WorldComponentTypeInfo ScreenTypeInfo =
{
    .Type = WorldComponentType_Screen,
    .Name = u8"screen",
    .Size = sizeof(WorldScreenComponent),
    .Table = &ScreenTable,
    .IsTransformDriver = false,
    .Construct = ConstructScreen,
    .Deconstruct = DeconstructScreen,
    .OnAttach = NULL,
    .OnDetach = NULL,
    .OnPropertySet = NULL
};


// Public functions.
const WorldComponentTypeInfo* WorldScreenComponent_GetTypeInfo(void)
{
    return &ScreenTypeInfo;
}

const PropertyTable* WorldScreenComponent_GetPropertyTable(void)
{
    return &ScreenTable;
}

Error WorldScreenComponent_SetSurface(WorldScreenComponent* self, const unsigned char* surface)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldScreenComponent_SetSurface: self must not be NULL.");
    }
    PropertyValue Value = PropertyValue_FromLocalName(surface);
    return Properties_Set(&ScreenTable, self, ScreenProperties[ScreenProperty_Surface].Key, &Value);
}

Error WorldScreenComponent_SetIsPowered(WorldScreenComponent* self, bool isPowered)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldScreenComponent_SetIsPowered: self must not be NULL.");
    }
    self->_isPowered = isPowered;
    WorldInternal_ComponentAfterSet(self, &ScreenProperties[ScreenProperty_IsPowered]);
    return Error_CreateSuccess();
}

Error WorldScreenComponent_SetBrightness(WorldScreenComponent* self, float brightness)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldScreenComponent_SetBrightness: self must not be NULL.");
    }
    PropertyValue Value = PropertyValue_FromFloat(brightness);
    Error CheckResult = Properties_CheckValue(&ScreenTable, &ScreenProperties[ScreenProperty_Brightness], &Value);
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    self->_brightness = brightness;
    WorldInternal_ComponentAfterSet(self, &ScreenProperties[ScreenProperty_Brightness]);
    return Error_CreateSuccess();
}
