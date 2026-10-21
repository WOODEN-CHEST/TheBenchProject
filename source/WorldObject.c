#include "WorldObject.h"
#include "WorldInternal.h"
#include "WorldTransform.h"
#include "wr/WRCompile.h"
#include "wr/WRMemory.h"


// Macros.
/** The format version of the object's property table. */
#define OBJECT_TABLE_VERSION 1U
/** The inspector's categories. */
#define CATEGORY_OBJECT ((const unsigned char*)u8"Object")
#define CATEGORY_TRANSFORM ((const unsigned char*)u8"Transform")
#define CATEGORY_APPEARANCE ((const unsigned char*)u8"Appearance")
#define CATEGORY_DATA ((const unsigned char*)u8"Data")


// Types.
/* The rows of the object's table, so the setters reach their descriptors without a search. */
typedef enum ObjectPropertyEnum
{
    ObjectProperty_Name,
    ObjectProperty_Parent,
    ObjectProperty_Position,
    ObjectProperty_Rotation,
    ObjectProperty_Scale,
    ObjectProperty_Tint,
    ObjectProperty_IsEnabled,
    ObjectProperty_IsVisible,
    ObjectProperty_Mobility,
    ObjectProperty_Data,
    ObjectProperty_Count
} ObjectProperty;


// Static functions: the table's hook (the table below refers to it).
static void ObjectAfterSet(void* instance, const PropertyDescriptor* descriptor);


// Fields.
static const PropertyEnumEntry MobilityEntries[] =
{
    { .Name = u8"static", .Value = WorldMobility_Static },
    { .Name = u8"movable", .Value = WorldMobility_Movable },
};

static const PropertyEnumTable MobilityEnum =
{
    .Name = u8"mobility", .Entries = MobilityEntries, .EntryCount = sizeof(MobilityEntries) / sizeof(MobilityEntries[0])
};

/* Every property, in inspector order. The wire ids are the save schema: never renumber or reuse one; a removed
 * property's id goes into the table's retired list. 1 and 2 are left for the world file's own per-object entries. */
static const PropertyDescriptor ObjectProperties[ObjectProperty_Count] =
{
    [ObjectProperty_Name] =
    {
        .Key = u8"name", .WireId = 3U, .Type = PropertyType_String, PROPERTY_MEMBER(WorldObject, _name),
        .Default.StringValue = NULL, .Rules = PROPERTY_RULES_STRING(WORLD_OBJECT_NAME_MAX_LENGTH, true),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OBJECT, .DisplayName = u8"Name",
        .Description = u8"A name for people and scripts; need not be unique.", .SinceVersion = 1U
    },
    [ObjectProperty_Parent] =
    {
        // Read-only for Properties_Set: World_SetParent changes it (it refuses cycles and trees too deep). A read or a
        // copy that brings another parent has the world attach the object to it, or refuse it (ObjectAfterSet).
        .Key = u8"parent", .WireId = 12U, .Type = PropertyType_ObjectId, PROPERTY_MEMBER(WorldObject, _parentId),
        .Default.ObjectIdValue = 0U,
        .Flags = PropertyFlags_Persistent | PropertyFlags_Editable | PropertyFlags_ReadOnly | PropertyFlags_Script,
        .Category = CATEGORY_TRANSFORM, .DisplayName = u8"Parent",
        .Description = u8"The object this one is placed on (0 for none): its position, rotation and scale are relative "
            u8"to the parent's, and it moves, hides and is disabled with it.",
        .SinceVersion = 1U
    },
    [ObjectProperty_Position] =
    {
        .Key = u8"position", .WireId = 4U, .Type = PropertyType_Vector3, PROPERTY_MEMBER(WorldObject, _position),
        .Default.Vector3Value = { 0.0f, 0.0f, 0.0f }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_TRANSFORM,
        .DisplayName = u8"Position",
        .Description = u8"Where the object is, in world units (+Y up), relative to its parent.", .SinceVersion = 1U
    },
    [ObjectProperty_Rotation] =
    {
        .Key = u8"rotation", .WireId = 5U, .Type = PropertyType_AngleVector3, PROPERTY_MEMBER(WorldObject, _rotation),
        .Default.Vector3Value = { 0.0f, 0.0f, 0.0f }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_TRANSFORM,
        .DisplayName = u8"Rotation",
        .Description = u8"Turns around the X, Y and Z axes in degrees, applied in that order, relative to its parent.",
        .SinceVersion = 1U
    },
    [ObjectProperty_Scale] =
    {
        .Key = u8"scale", .WireId = 6U, .Type = PropertyType_Vector3, PROPERTY_MEMBER(WorldObject, _scale),
        .Default.Vector3Value = { 1.0f, 1.0f, 1.0f }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_TRANSFORM,
        .DisplayName = u8"Scale",
        .Description = u8"Size along each axis; 1 is the model's own size. Multiplies the parent's.",
        .SinceVersion = 1U
    },
    [ObjectProperty_Tint] =
    {
        .Key = u8"tint", .WireId = 7U, .Type = PropertyType_RenderColor, PROPERTY_MEMBER(WorldObject, _tint),
        .Default.RenderColorValue = { { 255, 255, 255, 255 }, 1.0f, 1.0f }, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_APPEARANCE, .DisplayName = u8"Tint",
        .Description = u8"A colour multiplied into the object's own, with a brightness and an opacity.",
        .SinceVersion = 1U
    },
    [ObjectProperty_IsEnabled] =
    {
        .Key = u8"is_enabled", .WireId = 8U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WorldObject, _isEnabled),
        .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OBJECT,
        .DisplayName = u8"Enabled",
        .Description = u8"When off, every system skips the object: it is not drawn, does not collide and scripts' "
            u8"searches do not find it.",
        .SinceVersion = 1U
    },
    [ObjectProperty_IsVisible] =
    {
        .Key = u8"is_visible", .WireId = 9U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WorldObject, _isVisible),
        .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OBJECT,
        .DisplayName = u8"Visible",
        .Description = u8"When off, the object is not drawn (nor its light), but it still collides and can be used: "
            u8"invisible walls, hidden triggers.",
        .SinceVersion = 1U
    },
    [ObjectProperty_Mobility] =
    {
        .Key = u8"mobility", .WireId = 10U, .Type = PropertyType_Enum, PROPERTY_MEMBER(WorldObject, _mobility),
        .Default.EnumValue = WorldMobility_Static, .EnumTable = &MobilityEnum, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_OBJECT, .DisplayName = u8"Mobility",
        .Description = u8"\"static\" for scenery that does not move in play, \"movable\" for anything that does "
            u8"(doors, people, cars). A hint for speed: moving a static object still works, it just costs more.",
        .SinceVersion = 1U
    },
    [ObjectProperty_Data] =
    {
        // Read-only for Properties_Set: changes go through the world (World_SetData ...), which keeps the tag index,
        // the world's limit and the data revision. World files and copies still fill it.
        .Key = u8"data", .TagsKey = u8"tags", .WireId = 11U, .Type = PropertyType_DataMap,
        PROPERTY_MEMBER(WorldObject, _data), .Default.DataMapValue = NULL,
        .Rules = PROPERTY_RULES_DATA_MAP(WORLD_OBJECT_DATA_MAX_ENTRIES, WORLD_OBJECT_DATA_MAX_BYTES),
        .Flags = PropertyFlags_Persistent | PropertyFlags_Editable | PropertyFlags_ReadOnly | PropertyFlags_Script,
        .Category = CATEGORY_DATA, .DisplayName = u8"Tags and data",
        .Description = u8"Namespaced keys (\"town:visits\") with typed values, set in the editor or by scripts; a tag is "
            u8"a key with no value. Selectors search them.",
        .SinceVersion = 1U
    },
};

static const PropertyTable ObjectTable =
{
    .Name = u8"world_object",
    .Version = OBJECT_TABLE_VERSION,
    .StructSize = sizeof(WorldObject),
    .Descriptors = ObjectProperties,
    .DescriptorCount = ObjectProperty_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = ObjectAfterSet,
    .Migrate = NULL
};


// Static functions.
static bool IsTransformRow(const PropertyDescriptor* descriptor)
{
    return (descriptor == &ObjectProperties[ObjectProperty_Position])
        || (descriptor == &ObjectProperties[ObjectProperty_Rotation])
        || (descriptor == &ObjectProperties[ObjectProperty_Scale]);
}

/* A change of the tags and data advances the data revision (and tells the world: its tag index and byte count); any
 * other change, the revision. A change of the transform marks the object's subtree dirty. A read or a copy (no row) may
 * have changed everything, the parent included: the world attaches the object to the parent it brought, or refuses it
 * (the object keeps its parent). */
static void ObjectAfterSet(void* instance, const PropertyDescriptor* descriptor)
{
    WorldObject* Object = instance;
    const PropertyDescriptor* DataRow = &ObjectProperties[ObjectProperty_Data];
    if (descriptor != DataRow)
    {
        Object->_revision++;
    }
    if ((descriptor == NULL) || (descriptor == DataRow))
    {
        Object->_dataRevision++;
        if (Object->_world != NULL)
        {
            WorldInternal_SyncObjectData(Object->_world, Object);
        }
    }
    if (Object->_world == NULL)
    {
        return;
    }
    if (descriptor == NULL)
    {
        WorldInternal_SyncObjectParent(Object->_world, Object);
    }
    if ((descriptor == NULL) || IsTransformRow(descriptor))
    {
        WorldInternal_MarkTransformChanged(Object->_world, Object);
    }
}

/* Checks @p value against one row's rules (so the setters' rules live in the table only). */
static Error CheckProperty(ObjectProperty property, PropertyValue value)
{
    return Properties_CheckValue(&ObjectTable, &ObjectProperties[property], &value);
}


// Internal functions (declared in WorldInternal.h).
Error WorldObject_Construct(WorldObject* self, World* world, uint64_t id, const unsigned char* name)
{
    Error NameResult = CheckProperty(ObjectProperty_Name, PropertyValue_FromString(name));
    if (NameResult.Code != ErrorCode_Success)
    {
        return NameResult;
    }

    // Zeroes the struct (every component slot NULL, revision 0), then writes the defaults.
    Error ConstructResult = Properties_Construct(&ObjectTable, self);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        return ConstructResult;
    }
    self->_world = world;
    self->_id = id;
    self->_name = WorldInternal_CloneString(name);
    // A new object is a root at its default transform: its world matrix is its local one, up to date.
    self->_worldMatrix = WorldTransform_Compose(self->_position, self->_rotation, self->_scale);
    self->_isWorldMatrixDirty = false;
    self->_isPersistent = true;
    return Error_CreateSuccess();
}

void WorldObject_Deconstruct(WorldObject* self)
{
    if (self == NULL)
    {
        return;
    }
    Properties_Deconstruct(&ObjectTable, self);
}

Error WorldObject_CheckTransform(Vector3 position, Vector3 rotation, Vector3 scale)
{
    Error Result = CheckProperty(ObjectProperty_Position, PropertyValue_FromVector3(position));
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckProperty(ObjectProperty_Rotation, PropertyValue_FromAngleVector3(rotation));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckProperty(ObjectProperty_Scale, PropertyValue_FromVector3(scale));
    }
    return Result;
}


// Public functions.
const PropertyTable* WorldObject_GetPropertyTable(void)
{
    return &ObjectTable;
}

Error WorldObject_SetName(WorldObject* self, const unsigned char* name)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetName: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(ObjectProperty_Name, PropertyValue_FromString(name));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    unsigned char* Clone = WorldInternal_CloneString(name);
    Memory_Free(self->_name);
    self->_name = Clone;
    ObjectAfterSet(self, &ObjectProperties[ObjectProperty_Name]);
    return Error_CreateSuccess();
}

Error WorldObject_SetPosition(WorldObject* self, Vector3 position)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetPosition: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(ObjectProperty_Position, PropertyValue_FromVector3(position));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_position = position;
    ObjectAfterSet(self, &ObjectProperties[ObjectProperty_Position]);
    return Error_CreateSuccess();
}

Error WorldObject_SetRotation(WorldObject* self, Vector3 rotation)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetRotation: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(ObjectProperty_Rotation, PropertyValue_FromAngleVector3(rotation));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_rotation = rotation;
    ObjectAfterSet(self, &ObjectProperties[ObjectProperty_Rotation]);
    return Error_CreateSuccess();
}

Error WorldObject_SetScale(WorldObject* self, Vector3 scale)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetScale: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(ObjectProperty_Scale, PropertyValue_FromVector3(scale));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_scale = scale;
    ObjectAfterSet(self, &ObjectProperties[ObjectProperty_Scale]);
    return Error_CreateSuccess();
}

Error WorldObject_SetTint(WorldObject* self, RenderColor tint)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetTint: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(ObjectProperty_Tint, PropertyValue_FromRenderColor(tint));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_tint = tint;
    ObjectAfterSet(self, &ObjectProperties[ObjectProperty_Tint]);
    return Error_CreateSuccess();
}

Error WorldObject_SetEnabled(WorldObject* self, bool isEnabled)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetEnabled: self must not be NULL.");
    }

    self->_isEnabled = isEnabled;
    ObjectAfterSet(self, &ObjectProperties[ObjectProperty_IsEnabled]);
    return Error_CreateSuccess();
}

Error WorldObject_SetVisible(WorldObject* self, bool isVisible)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetVisible: self must not be NULL.");
    }

    self->_isVisible = isVisible;
    ObjectAfterSet(self, &ObjectProperties[ObjectProperty_IsVisible]);
    return Error_CreateSuccess();
}

Error WorldObject_SetMobility(WorldObject* self, WorldMobility mobility)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetMobility: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(ObjectProperty_Mobility, PropertyValue_FromEnum((int32_t)mobility));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_mobility = mobility;
    ObjectAfterSet(self, &ObjectProperties[ObjectProperty_Mobility]);
    return Error_CreateSuccess();
}
