#include <math.h>
#include "WorldPathComponent.h"
#include "WorldInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** The format versions of the two tables. */
#define PATH_TABLE_VERSION 1U
#define PATH_NODE_TABLE_VERSION 1U
/** The inspector's categories. */
#define CATEGORY_PATH ((const unsigned char*)u8"Path")
#define CATEGORY_NODE ((const unsigned char*)u8"Node")
#define CATEGORY_SECTION ((const unsigned char*)u8"Section")
/** What is left of a vector along an axis counts as nothing below this share of it (squared): it is rounding, with no
 *  direction. */
#define SQUARE_REST_SQUARED 1e-12


// Types.
/* The rows of the path component's table, so the setters reach their descriptors without a search. */
typedef enum PathPropertyEnum
{
    PathProperty_Nodes,
    PathProperty_IsClosed,
    PathProperty_UpMode,
    PathProperty_SampleSpacing,
    PathProperty_Count
} PathProperty;


// Fields.
static const PropertyEnumEntry NodeModeEntries[] =
{
    { .Name = u8"smooth", .Value = PathNodeMode_Smooth },
    { .Name = u8"corner", .Value = PathNodeMode_Corner },
    { .Name = u8"bezier", .Value = PathNodeMode_Bezier },
};

static const PropertyEnumTable NodeModeTable =
{
    .Name = u8"path_node_mode",
    .Entries = NodeModeEntries,
    .EntryCount = sizeof(NodeModeEntries) / sizeof(NodeModeEntries[0])
};

static const PropertyEnumEntry UpModeEntries[] =
{
    { .Name = u8"upright", .Value = PathUpMode_Upright },
    { .Name = u8"rotation_minimizing", .Value = PathUpMode_RotationMinimizing },
};

static const PropertyEnumTable UpModeTable =
{
    .Name = u8"path_up_mode",
    .Entries = UpModeEntries,
    .EntryCount = sizeof(UpModeEntries) / sizeof(UpModeEntries[0])
};

/* One node, in inspector order. The wire ids are the save schema: never renumber or reuse one; a removed row's id goes
 * into the table's retired list. The next free wire id is 12. */
static const PropertyDescriptor NodeProperties[] =
{
    {
        .Key = u8"position", .WireId = 1U, .Type = PropertyType_Vector3Double, PROPERTY_MEMBER(PathNode, Position),
        .Default.Vector3DoubleValue = { 0.0, 0.0, 0.0 },
        .Rules = PROPERTY_RULES_BETWEEN(-WORLD_POSITION_LIMIT, WORLD_POSITION_LIMIT), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_NODE, .DisplayName = u8"Position",
        .Description = u8"Where the node lies, in metres from the path's object, in its space.", .SinceVersion = 1U
    },
    {
        .Key = u8"mode", .WireId = 2U, .Type = PropertyType_Enum, PROPERTY_MEMBER(PathNode, Mode),
        .Default.EnumValue = PathNodeMode_Smooth, .EnumTable = &NodeModeTable, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_NODE, .DisplayName = u8"Mode",
        .Description = u8"Smooth: the curve passes through with no kink. Corner: sharp, the segments touching it straight, "
            u8"and it starts a section. Bezier: the curve follows the node's two handles.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"in_handle", .WireId = 3U, .Type = PropertyType_Vector3, PROPERTY_MEMBER(PathNode, InHandle),
        .Default.Vector3Value = { 0.0f, 0.0f, 0.0f },
        .Rules = PROPERTY_RULES_BETWEEN(-WORLD_POSITION_LIMIT, WORLD_POSITION_LIMIT), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_NODE, .DisplayName = u8"In handle",
        .Description = u8"A bezier node's handle toward the node before it, as an offset in metres; kept but unused in the "
            u8"other modes.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"out_handle", .WireId = 4U, .Type = PropertyType_Vector3, PROPERTY_MEMBER(PathNode, OutHandle),
        .Default.Vector3Value = { 0.0f, 0.0f, 0.0f },
        .Rules = PROPERTY_RULES_BETWEEN(-WORLD_POSITION_LIMIT, WORLD_POSITION_LIMIT), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_NODE, .DisplayName = u8"Out handle",
        .Description = u8"A bezier node's handle toward the node after it, as an offset in metres; kept but unused in the "
            u8"other modes.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"roll", .WireId = 5U, .Type = PropertyType_Angle, PROPERTY_MEMBER(PathNode, Roll),
        .Default.FloatValue = 0.0f, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_NODE, .DisplayName = u8"Roll",
        .Description = u8"How far what follows the path leans about it, in degrees; positive leans its top to the right, "
            u8"as a road banks into a right-hand bend.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"width", .WireId = 6U, .Type = PropertyType_Float, PROPERTY_MEMBER(PathNode, Width),
        .Default.FloatValue = 1.0f, .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_PATH_NODE_MAX_FACTOR),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_NODE, .DisplayName = u8"Width",
        .Description = u8"How wide what follows the path is across it here, as a factor: 1 is as its own settings say.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"scale", .WireId = 7U, .Type = PropertyType_Float, PROPERTY_MEMBER(PathNode, Scale),
        .Default.FloatValue = 1.0f, .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_PATH_NODE_MAX_FACTOR),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_NODE, .DisplayName = u8"Scale",
        .Description = u8"How large what follows the path is here in every direction, as a factor: 1 is as its own "
            u8"settings say.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"starts_section", .WireId = 8U, .Type = PropertyType_Bool, PROPERTY_MEMBER(PathNode, StartsSection),
        .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SECTION,
        .DisplayName = u8"Starts a section",
        .Description = u8"Whether a section starts here (a corner node always starts one). Each section fits what follows "
            u8"the path on its own, and takes the settings below from the node that starts it.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"skips_section", .WireId = 9U, .Type = PropertyType_Bool, PROPERTY_MEMBER(PathNode, SkipsSection),
        .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SECTION,
        .DisplayName = u8"Skips the section",
        .Description = u8"Whether nothing follows the path along the section this node starts: a gate's opening, a gap in "
            u8"a wall.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"overrides_snaps_to_terrain", .WireId = 10U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(PathNode, OverridesSnapsToTerrain), .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SECTION, .DisplayName = u8"Overrides snapping",
        .Description = u8"Whether the section this node starts sets its own terrain snapping, over what follows the path.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"snaps_to_terrain", .WireId = 11U, .Type = PropertyType_Bool, PROPERTY_MEMBER(PathNode, SnapsToTerrain),
        .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SECTION,
        .DisplayName = u8"Snaps to the ground",
        .Description = u8"The section's own terrain snapping, while it overrides: off for a bridge.", .SinceVersion = 1U
    },
};

static const PropertyTable NodeTable =
{
    .Name = u8"path_node",
    .Version = PATH_NODE_TABLE_VERSION,
    .StructSize = sizeof(PathNode),
    .Descriptors = NodeProperties,
    .DescriptorCount = sizeof(NodeProperties) / sizeof(NodeProperties[0])
};

/* Every property, in inspector order. The wire ids are the save schema: never renumber or reuse one; a removed property's
 * id goes into the table's retired list. The next free wire id is 5. */
static const PropertyDescriptor PathProperties[PathProperty_Count] =
{
    [PathProperty_Nodes] =
    {
        .Key = u8"nodes", .WireId = 1U, .Type = PropertyType_RecordList, PROPERTY_MEMBER(WorldPathComponent, _nodes),
        .Default.RecordListValue = NULL, .RecordTable = &NodeTable,
        .Rules = PROPERTY_RULES_RECORD_LIST(WORLD_PATH_MAX_NODES),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_PATH, .DisplayName = u8"Nodes",
        .Description = u8"The points the path runs through, relative to its object.", .SinceVersion = 1U
    },
    [PathProperty_IsClosed] =
    {
        .Key = u8"is_closed", .WireId = 2U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WorldPathComponent, _isClosed),
        .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_PATH, .DisplayName = u8"Closed",
        .Description = u8"Whether the path runs from its last node back to its first. A closed path is also a region: the "
            u8"area it encloses, for scattering, painting and scripts.",
        .SinceVersion = 1U
    },
    [PathProperty_UpMode] =
    {
        .Key = u8"up_mode", .WireId = 3U, .Type = PropertyType_Enum, PROPERTY_MEMBER(WorldPathComponent, _upMode),
        .Default.EnumValue = PathUpMode_Upright, .EnumTable = &UpModeTable, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_PATH, .DisplayName = u8"Up",
        .Description = u8"Upright: what follows the path stands up, for anything on the ground. Rotation minimizing: it "
            u8"turns as little as it can, for paths that climb steeply or loop.",
        .SinceVersion = 1U
    },
    [PathProperty_SampleSpacing] =
    {
        .Key = u8"sample_spacing", .WireId = 4U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldPathComponent, _sampleSpacing), .Default.FloatValue = WORLD_PATH_DEFAULT_SAMPLE_SPACING,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_PATH,
        .DisplayName = u8"Sample spacing",
        .Description = u8"The most metres between two points where a reader samples this path, closer where it bends.",
        .SinceVersion = 1U
    },
};

static const PropertyTable PathTable =
{
    .Name = u8"path_component",
    .Version = PATH_TABLE_VERSION,
    .StructSize = sizeof(WorldPathComponent),
    .Descriptors = PathProperties,
    .DescriptorCount = PathProperty_Count,
    .AfterSet = WorldInternal_ComponentAfterSet
};


// Static functions.
static Error ConstructPath(WorldComponent* component)
{
    WorldPathComponent* Path = (WorldPathComponent*)component;
    Error Result = Properties_Construct(&PathTable, Path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    PathCurve_Construct(&Path->_curve);
    return Error_CreateSuccess();
}

static void DeconstructPath(WorldComponent* component)
{
    WorldPathComponent* Path = (WorldPathComponent*)component;
    PathCurve_Deconstruct(&Path->_curve);
    Properties_Deconstruct(&PathTable, Path);
}

/* The curve from the rows, whichever way they changed (a setter or the table; a read or a copy names no row). The sample
 * spacing does not shape it. The rows' rules are stricter than the build's, so a failure cannot happen: if it ever did the
 * curve would be left empty. */
static void OnPathPropertySet(World* world, WorldObject* object, WorldComponent* component,
    const PropertyDescriptor* descriptor)
{
    (void)world;
    (void)object;
    if (descriptor == &PathProperties[PathProperty_SampleSpacing])
    {
        return;
    }
    WorldPathComponent* Path = (WorldPathComponent*)component;
    size_t Count = 0U;
    const PathNode* Nodes = WorldPathComponent_GetNodes(Path, &Count);
    Error Result = PathCurve_Build(&Path->_curve, Nodes, Count, Path->_isClosed, Path->_upMode);
    Error_Deconstruct(&Result);
}

static Error CreateNullSelfError(const char* function)
{
    return Error_Construct4(ErrorCode_IllegalArgument, "%s: self must not be NULL.", function);
}

/* Hands a list to the nodes row after checking it by the row's rules; the list is consumed either way. */
static Error CommitNodes(WorldPathComponent* self, PropertyRecordList* list)
{
    const PropertyDescriptor* Row = &PathProperties[PathProperty_Nodes];
    PropertyValue Value = PropertyValue_FromRecordList(list);
    Error Result = Properties_CheckValue(&PathTable, Row, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        PropertyRecordList_Destroy(list);
        return Result;
    }
    PropertyRecordList_Destroy(self->_nodes);
    self->_nodes = list;
    WorldInternal_ComponentAfterSet(self, Row);
    return Error_CreateSuccess();
}

/* The nodes in a heap block of @p extra more than it has, for an insertion or a removal; the caller frees it. */
static PathNode* CopyNodes(const WorldPathComponent* self, size_t extra)
{
    size_t Count = 0U;
    const PathNode* Nodes = WorldPathComponent_GetNodes(self, &Count);
    size_t ByteCount = (Count + extra) * sizeof(PathNode);
    PathNode* Copy = Memory_Allocate((ByteCount > 0U) ? ByteCount : sizeof(PathNode));
    if (Count > 0U)
    {
        Memory_Copy(Nodes, Copy, Count * sizeof(PathNode));
    }
    return Copy;
}

static Vector3 ToFloats(Vector3Double value)
{
    return (Vector3){ (float)value.X, (float)value.Y, (float)value.Z };
}

static Vector3Double FromFloats(Vector3 value)
{
    return Vector3Double_Create((double)value.x, (double)value.y, (double)value.z);
}

/* @p vector with its part along the unit @p axis taken out, made unit; zero when nothing but rounding is left of it. */
static Vector3Double GetSquareTo(Vector3Double vector, Vector3Double axis)
{
    Vector3Double Rest = Vector3Double_AddScaled(vector, axis, -Vector3Double_Dot(vector, axis));
    if (Vector3Double_LengthSquared(Rest) <= (SQUARE_REST_SQUARED * Vector3Double_LengthSquared(vector)))
    {
        return Vector3Double_Zero();
    }
    return Vector3Double_NormalizeOr(Rest, Vector3Double_Zero());
}


// Fields (after the functions they name).
static const WorldComponentTypeInfo PathTypeInfo =
{
    .Type = WorldComponentType_Path,
    .Name = u8"path",
    .Size = sizeof(WorldPathComponent),
    .Table = &PathTable,
    .IsTransformDriver = false,
    .Construct = ConstructPath,
    .Deconstruct = DeconstructPath,
    .OnAttach = NULL,
    .OnDetach = NULL,
    .OnPropertySet = OnPathPropertySet
};


// Public functions: the registry.
const WorldComponentTypeInfo* WorldPathComponent_GetTypeInfo(void)
{
    return &PathTypeInfo;
}

const PropertyTable* WorldPathComponent_GetPropertyTable(void)
{
    return &PathTable;
}

const PropertyTable* WorldPathComponent_GetNodePropertyTable(void)
{
    return &NodeTable;
}


// Public functions: the nodes.
Error WorldPathComponent_SetNodes(WorldPathComponent* self, const PathNode* nodes, size_t count)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldPathComponent_SetNodes");
    }
    PropertyRecordList* List = NULL;
    Error Result = PropertyRecordList_Create(&NodeTable, nodes, count, &List);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return CommitNodes(self, List);
}

Error WorldPathComponent_SetNode(WorldPathComponent* self, size_t index, const PathNode* node)
{
    if ((self == NULL) || (node == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldPathComponent_SetNode: self and node must not be NULL.");
    }
    size_t Count = PropertyRecordList_GetCount(self->_nodes);
    if (index >= Count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "WorldPathComponent_SetNode: no node at that index.");
    }
    PathNode* Copy = CopyNodes(self, 0U);
    Copy[index] = *node;
    Error Result = WorldPathComponent_SetNodes(self, Copy, Count);
    Memory_Free(Copy);
    return Result;
}

Error WorldPathComponent_InsertNode(WorldPathComponent* self, size_t index, const PathNode* node)
{
    if ((self == NULL) || (node == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldPathComponent_InsertNode: self and node must not be NULL.");
    }
    size_t Count = PropertyRecordList_GetCount(self->_nodes);
    if (index > Count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "WorldPathComponent_InsertNode: the index is past the end.");
    }
    PathNode* Copy = CopyNodes(self, 1U);
    Memory_Move(&Copy[index], &Copy[index + 1U], (Count - index) * sizeof(PathNode));
    Copy[index] = *node;
    Error Result = WorldPathComponent_SetNodes(self, Copy, Count + 1U);
    Memory_Free(Copy);
    return Result;
}

Error WorldPathComponent_RemoveNode(WorldPathComponent* self, size_t index)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldPathComponent_RemoveNode");
    }
    size_t Count = PropertyRecordList_GetCount(self->_nodes);
    if (index >= Count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "WorldPathComponent_RemoveNode: no node at that index.");
    }
    PathNode* Copy = CopyNodes(self, 0U);
    Memory_Move(&Copy[index + 1U], &Copy[index], (Count - index - 1U) * sizeof(PathNode));
    Error Result = WorldPathComponent_SetNodes(self, Copy, Count - 1U);
    Memory_Free(Copy);
    return Result;
}


// Public functions: the settings.
Error WorldPathComponent_SetClosed(WorldPathComponent* self, bool isClosed)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldPathComponent_SetClosed");
    }
    self->_isClosed = isClosed;
    WorldInternal_ComponentAfterSet(self, &PathProperties[PathProperty_IsClosed]);
    return Error_CreateSuccess();
}

Error WorldPathComponent_SetUpMode(WorldPathComponent* self, PathUpMode upMode)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldPathComponent_SetUpMode");
    }
    const PropertyDescriptor* Row = &PathProperties[PathProperty_UpMode];
    PropertyValue Value = PropertyValue_FromEnum((int32_t)upMode);
    Error Result = Properties_CheckValue(&PathTable, Row, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_upMode = upMode;
    WorldInternal_ComponentAfterSet(self, Row);
    return Error_CreateSuccess();
}

Error WorldPathComponent_SetSampleSpacing(WorldPathComponent* self, float spacing)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldPathComponent_SetSampleSpacing");
    }
    const PropertyDescriptor* Row = &PathProperties[PathProperty_SampleSpacing];
    PropertyValue Value = PropertyValue_FromFloat(spacing);
    Error Result = Properties_CheckValue(&PathTable, Row, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_sampleSpacing = spacing;
    WorldInternal_ComponentAfterSet(self, Row);
    return Error_CreateSuccess();
}


// Public functions: finding a path, and following it.
WorldPathComponent* WorldPathComponent_FindForObject(const WorldObject* object)
{
    // The hierarchy has no cycles and at most WORLD_HIERARCHY_MAX_DEPTH levels, whatever a file says.
    for (const WorldObject* Current = object; Current != NULL; Current = WorldObject_GetParent(Current))
    {
        WorldPathComponent* Path = WorldPathComponent_FromComponent(WorldObject_GetComponent(Current,
            WorldComponentType_Path));
        if (Path != NULL)
        {
            return Path;
        }
    }
    return NULL;
}

WorldPathChange WorldPathWatch_Update(WorldPathWatch* self, const WorldPathComponent* path)
{
    uint64_t Serial = (path != NULL) ? WorldComponent_GetSerial(&path->Base) : 0U;
    if (Serial != self->_serial)
    {
        self->_serial = Serial;
        self->_revision = (path != NULL) ? WorldComponent_GetRevision(&path->Base) : 0U;
        self->_transformRevision = (path != NULL) ? WorldObject_GetTransformRevision(path->Base._owner) : 0U;
        return (WorldPathChange){ .HasPathChanged = true, .HasPlaceChanged = true };
    }
    if (path == NULL)
    {
        return (WorldPathChange){ .HasPathChanged = false, .HasPlaceChanged = false };
    }
    uint32_t Revision = WorldComponent_GetRevision(&path->Base);
    uint32_t TransformRevision = WorldObject_GetTransformRevision(path->Base._owner);
    WorldPathChange Change = { .HasPathChanged = Revision != self->_revision,
        .HasPlaceChanged = TransformRevision != self->_transformRevision };
    self->_revision = Revision;
    self->_transformRevision = TransformRevision;
    return Change;
}


// Public functions: the world.
Vector3Double WorldPathComponent_LocalToWorld(const WorldPathComponent* self, Vector3Double local)
{
    Matrix4Double WorldMatrix = WorldObject_GetWorldMatrix(self->Base._owner);
    return Matrix4Double_TransformPoint(&WorldMatrix, local);
}

bool WorldPathComponent_WorldToLocal(const WorldPathComponent* self, Vector3Double world, Vector3Double* outLocal)
{
    Matrix4Double WorldMatrix = WorldObject_GetWorldMatrix(self->Base._owner);
    Vector3Double Offset = Vector3Double_Subtract(world, Matrix4Double_GetTranslation(&WorldMatrix));
    Matrix4Double_SetTranslation(&WorldMatrix, Vector3Double_Zero());
    Matrix4Double Inverse;
    if (!Matrix4Double_TryInvertAffine(&WorldMatrix, &Inverse))
    {
        return false;
    }
    *outLocal = Matrix4Double_TransformDirection(&Inverse, Offset);
    return true;
}

void WorldPathComponent_PointToWorld(const WorldPathComponent* self, const PathCurvePoint* local, PathCurvePoint* outWorld)
{
    Matrix4Double WorldMatrix = WorldObject_GetWorldMatrix(self->Base._owner);
    Vector3Double LocalTangent = FromFloats(local->Tangent);
    Vector3Double LocalUp = FromFloats(local->Up);
    Vector3Double Tangent = Vector3Double_NormalizeOr(Matrix4Double_TransformDirection(&WorldMatrix, LocalTangent),
        LocalTangent);
    // Under a scale of 0 the up may collapse onto the tangent or to nothing: it takes what it can from the local up, then
    // any perpendicular, so the frame stays unit and square.
    Vector3Double Up = GetSquareTo(Matrix4Double_TransformDirection(&WorldMatrix, LocalUp), Tangent);
    if (Vector3Double_LengthSquared(Up) < 0.5)
    {
        Up = GetSquareTo(LocalUp, Tangent);
    }
    if (Vector3Double_LengthSquared(Up) < 0.5)
    {
        Up = Vector3Double_NormalizeOr(Vector3Double_Perpendicular(Tangent), LocalUp);
    }
    PathCurvePoint Result = *local;
    Result.Position = Matrix4Double_TransformPoint(&WorldMatrix, local->Position);
    Result.Tangent = ToFloats(Tangent);
    Result.Up = ToFloats(Up);
    Result.Right = ToFloats(Vector3Double_Cross(Tangent, Up));
    *outWorld = Result;
}

bool WorldPathComponent_IsWorldPointInside(const WorldPathComponent* self, Vector3Double world)
{
    Vector3Double Local;
    return WorldPathComponent_WorldToLocal(self, world, &Local) && PathCurve_IsPointInside(&self->_curve, Local.X, Local.Z);
}
