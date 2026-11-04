#include "WorldPathObjectComponent.h"
#include "PathObjectInternal.h"
#include "WorldInternal.h"


// Macros.
/** The format version of the table. */
#define PATH_OBJECT_COMPONENT_TABLE_VERSION 1U


// Fields.
/* The asset's name, then the settings' rows (their wire ids below 64, PathObjectInternal.h). The wire ids are the save schema:
 * never renumber or reuse one. The next free wire id of the table's own is 65. */
static const PropertyDescriptor PathObjectComponentProperties[] =
{
    {
        .Key = u8"path_object", .WireId = 64U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(WorldPathObjectComponent, _pathObject), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(PATH_OBJECT_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = PATH_OBJECT_CATEGORY_SLOTS, .DisplayName = u8"Path object",
        .Description = u8"The path object asset: its slots and how they fill the path. None generates nothing.",
        .SinceVersion = 1U
    },
    PATH_OBJECT_SETTINGS_ROWS(WorldPathObjectComponent),
};

static const PropertyTable PathObjectComponentTable =
{
    .Name = u8"path_object_component",
    .Version = PATH_OBJECT_COMPONENT_TABLE_VERSION,
    .StructSize = sizeof(WorldPathObjectComponent),
    .Descriptors = PathObjectComponentProperties,
    .DescriptorCount = sizeof(PathObjectComponentProperties) / sizeof(PathObjectComponentProperties[0]),
    .AfterSet = WorldInternal_ComponentAfterSet
};


// Static functions.
static Error ConstructPathObjectComponent(WorldComponent* component)
{
    return Properties_Construct(&PathObjectComponentTable, component);
}

static void DeconstructPathObjectComponent(WorldComponent* component)
{
    Properties_Deconstruct(&PathObjectComponentTable, component);
}

static const WorldComponentTypeInfo PathObjectComponentTypeInfo =
{
    .Type = WorldComponentType_PathObject,
    .Name = u8"path_object",
    .Size = sizeof(WorldPathObjectComponent),
    .Table = &PathObjectComponentTable,
    .IsTransformDriver = false,
    .Construct = ConstructPathObjectComponent,
    .Deconstruct = DeconstructPathObjectComponent,
    .OnAttach = NULL,
    .OnDetach = NULL,
    .OnPropertySet = NULL
};


// Functions.
const WorldComponentTypeInfo* WorldPathObjectComponent_GetTypeInfo(void)
{
    return &PathObjectComponentTypeInfo;
}

const PropertyTable* WorldPathObjectComponent_GetPropertyTable(void)
{
    return &PathObjectComponentTable;
}

Error WorldPathObjectComponent_SetPathObject(WorldPathObjectComponent* self, const unsigned char* pathObject)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldPathObjectComponent_SetPathObject: self must not be NULL.");
    }
    PropertyValue Value = PropertyValue_FromAssetName(pathObject);
    return Properties_Set(&PathObjectComponentTable, self, (const unsigned char*)u8"path_object", &Value);
}
