#include <math.h>
#include "WorldColliderComponent.h"
#include "WorldInternal.h"
#include "WorldTransform.h"
#include "wr/WRMemory.h"

/*
 * The collider component (include/WorldColliderComponent.h): its property table, setters and the pure function that
 * resolves its settings over its world model's collision defaults. It holds no asset: the collision world
 * (source/WorldCollision.c) resolves the world model by name and builds the shapes.
 */


// Macros.
/** The format version of the collider component's property table. */
#define COLLIDER_TABLE_VERSION 1U
/** The inspector's categories. */
#define CATEGORY_SHAPES ((const unsigned char*)u8"Shapes")
#define CATEGORY_OWN_SHAPE ((const unsigned char*)u8"Own shape")
#define CATEGORY_COLLISION ((const unsigned char*)u8"Collision")
#define CATEGORY_PLACEMENT ((const unsigned char*)u8"Placement")
#define CATEGORY_MOTION ((const unsigned char*)u8"Motion")
/** A mass's rules: above 0 (an inverse mass is taken), at most COLLISION_MAX_MASS. */
#define MASS_RULES { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true, \
    .Maximum = (double)COLLISION_MAX_MASS }


// Types.
/* The rows of the collider component's table, so the setters reach their descriptors without a search. */
typedef enum ColliderPropertyEnum
{
    ColliderProperty_IsEnabled,
    ColliderProperty_OverridesCollisionSet,
    ColliderProperty_CollisionSet,
    ColliderProperty_UsesOwnShape,
    ColliderProperty_ShapeType,
    ColliderProperty_ShapeCenter,
    ColliderProperty_ShapeRotation,
    ColliderProperty_ShapeRadius,
    ColliderProperty_ShapeHalfHeight,
    ColliderProperty_ShapeHalfExtents,
    ColliderProperty_ShapeIsBounded,
    ColliderProperty_ShapePlaneHalfExtents,
    ColliderProperty_ShapeDepth,
    ColliderProperty_OverridesLayer,
    ColliderProperty_Layer,
    ColliderProperty_OverridesCollidesWith,
    ColliderProperty_CollidesWith,
    ColliderProperty_OverridesIsTrigger,
    ColliderProperty_IsTrigger,
    ColliderProperty_Offset,
    ColliderProperty_Rotation,
    ColliderProperty_Scale,
    ColliderProperty_RotationMode,
    ColliderProperty_IsPushable,
    ColliderProperty_OverridesMass,
    ColliderProperty_Mass,
    ColliderProperty_WhenBlocked,
    ColliderProperty_IsContinuous,
    ColliderProperty_ReportsPersistingContacts,
    ColliderProperty_Count
} ColliderProperty;


// Fields.
static const PropertyEnumEntry RotationModeEntries[] =
{
    { .Name = u8"full", .Value = WorldColliderRotationMode_Full },
    { .Name = u8"yaw_only", .Value = WorldColliderRotationMode_YawOnly },
    { .Name = u8"none", .Value = WorldColliderRotationMode_None },
};

const PropertyEnumTable WORLD_COLLIDER_ROTATION_MODE_ENUM =
{
    .Name = u8"rotation_mode", .Entries = RotationModeEntries,
    .EntryCount = sizeof(RotationModeEntries) / sizeof(RotationModeEntries[0])
};

static const PropertyEnumEntry BlockedModeEntries[] =
{
    { .Name = u8"stop", .Value = WorldColliderBlockedMode_Stop },
    { .Name = u8"push_through", .Value = WorldColliderBlockedMode_PushThrough },
};

const PropertyEnumTable WORLD_COLLIDER_BLOCKED_MODE_ENUM =
{
    .Name = u8"when_blocked", .Entries = BlockedModeEntries,
    .EntryCount = sizeof(BlockedModeEntries) / sizeof(BlockedModeEntries[0])
};

/* Every property, in inspector order. The wire ids are the save schema: never renumber or reuse one; a removed
 * property's id goes into the table's retired list. */
static const PropertyDescriptor ColliderProperties[ColliderProperty_Count] =
{
    [ColliderProperty_IsEnabled] =
    {
        .Key = u8"is_enabled", .WireId = 1U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _isEnabled), .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SHAPES, .DisplayName = u8"Enabled",
        .Description = u8"Off takes the collider out of the collision world.", .SinceVersion = 1U
    },
    [ColliderProperty_OverridesCollisionSet] =
    {
        .Key = u8"overrides_collision_set", .WireId = 2U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _overridesCollisionSet), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SHAPES, .DisplayName = u8"Own collision set",
        .Description = u8"Uses the set named below instead of the world model's default set.", .SinceVersion = 1U
    },
    [ColliderProperty_CollisionSet] =
    {
        .Key = u8"collision_set", .WireId = 3U, .Type = PropertyType_String,
        PROPERTY_MEMBER(WorldColliderComponent, _collisionSet), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(COLLISION_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SHAPES, .DisplayName = u8"Collision set",
        .Description = u8"Which of the world model's collision sets to use while the override is on (a door's "
            u8"\"open\" or \"closed\").", .SinceVersion = 1U
    },
    [ColliderProperty_UsesOwnShape] =
    {
        .Key = u8"uses_own_shape", .WireId = 4U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _usesOwnShape), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OWN_SHAPE, .DisplayName = u8"Own shape",
        .Description = u8"Uses one primitive of the collider's own instead of the world model's sets: invisible walls, "
            u8"trigger volumes, objects without a model.", .SinceVersion = 1U
    },
    [ColliderProperty_ShapeType] =
    {
        .Key = u8"shape_type", .WireId = 5U, .Type = PropertyType_Enum,
        PROPERTY_MEMBER(WorldColliderComponent, _shapeType), .Default.EnumValue = CollisionShapeType_Box,
        .EnumTable = &COLLISION_PRIMITIVE_TYPE_ENUM, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OWN_SHAPE,
        .DisplayName = u8"Shape type", .Description = u8"The own shape: a sphere, capsule, box, cylinder or plane.",
        .SinceVersion = 1U
    },
    [ColliderProperty_ShapeCenter] =
    {
        .Key = u8"shape_center", .WireId = 6U, .Type = PropertyType_Vector3,
        PROPERTY_MEMBER(WorldColliderComponent, _shapeCenter), .Default.Vector3Value = { 0.0f, 0.0f, 0.0f },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OWN_SHAPE, .DisplayName = u8"Shape centre",
        .Description = u8"Where the own shape sits in the object's space.", .SinceVersion = 1U
    },
    [ColliderProperty_ShapeRotation] =
    {
        .Key = u8"shape_rotation", .WireId = 7U, .Type = PropertyType_AngleVector3,
        PROPERTY_MEMBER(WorldColliderComponent, _shapeRotation), .Default.Vector3Value = { 0.0f, 0.0f, 0.0f },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OWN_SHAPE, .DisplayName = u8"Shape rotation",
        .Description = u8"The own shape's rotation in the object's space (Euler XYZ, degrees in files).",
        .SinceVersion = 1U
    },
    [ColliderProperty_ShapeRadius] =
    {
        .Key = u8"shape_radius", .WireId = 8U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldColliderComponent, _shapeRadius), .Default.FloatValue = COLLISION_SHAPE_DEFAULT_SIZE,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OWN_SHAPE,
        .DisplayName = u8"Radius", .Description = u8"A sphere's, capsule's or cylinder's radius, in metres.",
        .SinceVersion = 1U
    },
    [ColliderProperty_ShapeHalfHeight] =
    {
        .Key = u8"shape_half_height", .WireId = 9U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldColliderComponent, _shapeHalfHeight), .Default.FloatValue = COLLISION_SHAPE_DEFAULT_SIZE,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OWN_SHAPE,
        .DisplayName = u8"Half height",
        .Description = u8"A capsule's or cylinder's half height along its local Y, in metres (a capsule's straight "
            u8"part; a cylinder needs more than 0).", .SinceVersion = 1U
    },
    [ColliderProperty_ShapeHalfExtents] =
    {
        .Key = u8"shape_half_extents", .WireId = 10U, .Type = PropertyType_Vector3,
        PROPERTY_MEMBER(WorldColliderComponent, _shapeHalfExtents),
        .Default.Vector3Value = { COLLISION_SHAPE_DEFAULT_SIZE, COLLISION_SHAPE_DEFAULT_SIZE,
            COLLISION_SHAPE_DEFAULT_SIZE },
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OWN_SHAPE,
        .DisplayName = u8"Half extents", .Description = u8"A box's half size along each local axis, in metres.",
        .SinceVersion = 1U
    },
    [ColliderProperty_ShapeIsBounded] =
    {
        .Key = u8"shape_is_bounded", .WireId = 11U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _shapeIsBounded), .Default.BoolValue = true,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OWN_SHAPE, .DisplayName = u8"Bounded plane",
        .Description = u8"A plane is a rectangle (on) or an infinite half-space, solid behind it (off).",
        .SinceVersion = 1U
    },
    [ColliderProperty_ShapePlaneHalfExtents] =
    {
        .Key = u8"shape_plane_half_extents", .WireId = 12U, .Type = PropertyType_Vector2,
        PROPERTY_MEMBER(WorldColliderComponent, _shapePlaneHalfExtents),
        .Default.Vector2Value = { COLLISION_SHAPE_DEFAULT_SIZE, COLLISION_SHAPE_DEFAULT_SIZE },
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OWN_SHAPE,
        .DisplayName = u8"Plane half extents",
        .Description = u8"A bounded plane's half size along its local X and Z, in metres; its front is local +Y.",
        .SinceVersion = 1U
    },
    [ColliderProperty_ShapeDepth] =
    {
        .Key = u8"shape_depth", .WireId = 13U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldColliderComponent, _shapeDepth), .Default.FloatValue = 0.0f,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_OWN_SHAPE,
        .DisplayName = u8"Plane depth",
        .Description = u8"How far behind a bounded plane it is solid, in metres (0: a flat rectangle).",
        .SinceVersion = 1U
    },
    [ColliderProperty_OverridesLayer] =
    {
        .Key = u8"overrides_layer", .WireId = 14U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _overridesLayer), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_COLLISION, .DisplayName = u8"Own layer",
        .Description = u8"Uses the layer below instead of the world model's.", .SinceVersion = 1U
    },
    [ColliderProperty_Layer] =
    {
        .Key = u8"layer", .WireId = 15U, .Type = PropertyType_Enum, PROPERTY_MEMBER(WorldColliderComponent, _layer),
        .Default.EnumValue = CollisionLayer_Prop, .EnumTable = &COLLISION_LAYER_ENUM, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_COLLISION, .DisplayName = u8"Layer",
        .Description = u8"The collision layer: world, prop, character, vehicle, dynamic, trigger or interaction.",
        .SinceVersion = 1U
    },
    [ColliderProperty_OverridesCollidesWith] =
    {
        .Key = u8"overrides_collides_with", .WireId = 16U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _overridesCollidesWith), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_COLLISION, .DisplayName = u8"Own mask",
        .Description = u8"Uses the layers below instead of the world model's mask (or the layer's default).",
        .SinceVersion = 1U
    },
    [ColliderProperty_CollidesWith] =
    {
        .Key = u8"collides_with", .WireId = 17U, .Type = PropertyType_EnumSet,
        PROPERTY_MEMBER(WorldColliderComponent, _collidesWith),
        .Default.EnumSetValue = COLLISION_LAYER_BIT(CollisionLayer_World) | COLLISION_LAYER_BIT(CollisionLayer_Prop)
            | COLLISION_LAYER_BIT(CollisionLayer_Character) | COLLISION_LAYER_BIT(CollisionLayer_Vehicle)
            | COLLISION_LAYER_BIT(CollisionLayer_Dynamic) | COLLISION_LAYER_BIT(CollisionLayer_Trigger),
        .EnumTable = &COLLISION_LAYER_ENUM, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_COLLISION,
        .DisplayName = u8"Collides with",
        .Description = u8"The layers this collider blocks and is blocked by (both colliders must list each other's).",
        .SinceVersion = 1U
    },
    [ColliderProperty_OverridesIsTrigger] =
    {
        .Key = u8"overrides_is_trigger", .WireId = 18U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _overridesIsTrigger), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_COLLISION, .DisplayName = u8"Own trigger flag",
        .Description = u8"Uses the trigger flag below instead of the world model's.", .SinceVersion = 1U
    },
    [ColliderProperty_IsTrigger] =
    {
        .Key = u8"is_trigger", .WireId = 19U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _isTrigger), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_COLLISION, .DisplayName = u8"Trigger",
        .Description = u8"A trigger is reported by overlaps and trigger events and never blocks anything.",
        .SinceVersion = 1U
    },
    [ColliderProperty_Offset] =
    {
        .Key = u8"offset", .WireId = 20U, .Type = PropertyType_Vector3, PROPERTY_MEMBER(WorldColliderComponent, _offset),
        .Default.Vector3Value = { 0.0f, 0.0f, 0.0f }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_PLACEMENT,
        .DisplayName = u8"Offset", .Description = u8"Moves every shape in the object's space.", .SinceVersion = 1U
    },
    [ColliderProperty_Rotation] =
    {
        .Key = u8"rotation", .WireId = 21U, .Type = PropertyType_AngleVector3,
        PROPERTY_MEMBER(WorldColliderComponent, _rotation), .Default.Vector3Value = { 0.0f, 0.0f, 0.0f },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_PLACEMENT, .DisplayName = u8"Rotation",
        .Description = u8"Turns every shape in the object's space (Euler XYZ, degrees in files).", .SinceVersion = 1U
    },
    [ColliderProperty_Scale] =
    {
        .Key = u8"scale", .WireId = 22U, .Type = PropertyType_Vector3, PROPERTY_MEMBER(WorldColliderComponent, _scale),
        .Default.Vector3Value = { 1.0f, 1.0f, 1.0f }, .Rules = PROPERTY_RULES_ABOVE(0.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_PLACEMENT, .DisplayName = u8"Scale",
        .Description = u8"Scales every shape on top of the object's own scale (an uneven scale distorts them "
            u8"exactly).", .SinceVersion = 1U
    },
    [ColliderProperty_RotationMode] =
    {
        .Key = u8"rotation_mode", .WireId = 23U, .Type = PropertyType_Enum,
        PROPERTY_MEMBER(WorldColliderComponent, _rotationMode), .Default.EnumValue = WorldColliderRotationMode_Full,
        .EnumTable = &WORLD_COLLIDER_ROTATION_MODE_ENUM, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_PLACEMENT,
        .DisplayName = u8"Rotation mode",
        .Description = u8"full turns the shapes with the object, yaw_only keeps them upright, none keeps them "
            u8"axis-aligned.", .SinceVersion = 1U
    },
    [ColliderProperty_IsPushable] =
    {
        .Key = u8"is_pushable", .WireId = 24U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _isPushable), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOTION, .DisplayName = u8"Pushable",
        .Description = u8"On a movable object: the collision step may push it out of the way (characters, crates). Off, a "
            u8"movable object's collider is kinematic: it pushes and is never pushed (doors, elevators).",
        .SinceVersion = 1U
    },
    [ColliderProperty_OverridesMass] =
    {
        .Key = u8"overrides_mass", .WireId = 25U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _overridesMass), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOTION, .DisplayName = u8"Own mass",
        .Description = u8"Uses the mass below instead of the world model's.", .SinceVersion = 1U
    },
    [ColliderProperty_Mass] =
    {
        .Key = u8"mass", .WireId = 26U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldColliderComponent, _mass),
        .Default.FloatValue = COLLISION_DEFAULT_MASS, .Rules = MASS_RULES, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MOTION, .DisplayName = u8"Mass",
        .Description = u8"Kilograms: how pushables share a push (equal masses give way equally, a heavy one barely "
            u8"moves).", .SinceVersion = 1U
    },
    [ColliderProperty_WhenBlocked] =
    {
        .Key = u8"when_blocked", .WireId = 27U, .Type = PropertyType_Enum,
        PROPERTY_MEMBER(WorldColliderComponent, _whenBlocked), .Default.EnumValue = WorldColliderBlockedMode_Stop,
        .EnumTable = &WORLD_COLLIDER_BLOCKED_MODE_ENUM, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOTION,
        .DisplayName = u8"When blocked",
        .Description = u8"A kinematic mover that would squeeze a pushable against something that does not give way: stop "
            u8"stays where it was this tick, push_through moves on.", .SinceVersion = 1U
    },
    [ColliderProperty_IsContinuous] =
    {
        .Key = u8"is_continuous", .WireId = 28U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _isContinuous), .Default.BoolValue = true,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOTION, .DisplayName = u8"Continuous",
        .Description = u8"Sweeps the collider's motion so it never passes through anything; off only pushes it out where "
            u8"it ends up (cheap movers that never go fast).", .SinceVersion = 1U
    },
    [ColliderProperty_ReportsPersistingContacts] =
    {
        .Key = u8"reports_persisting_contacts", .WireId = 29U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldColliderComponent, _reportsPersistingContacts), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOTION, .DisplayName = u8"Report persisting contacts",
        .Description = u8"A contact event every tick while it touches something, not only when a touch begins and ends.",
        .SinceVersion = 1U
    },
};

static const PropertyTable ColliderTable =
{
    .Name = u8"collider_component",
    .Version = COLLIDER_TABLE_VERSION,
    .StructSize = sizeof(WorldColliderComponent),
    .Descriptors = ColliderProperties,
    .DescriptorCount = ColliderProperty_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = NULL
};


// Static functions.
static Error ConstructCollider(WorldComponent* component)
{
    return Properties_Construct(&ColliderTable, component);
}

static void DeconstructCollider(WorldComponent* component)
{
    Properties_Deconstruct(&ColliderTable, component);
}

static Error CheckProperty(ColliderProperty property, PropertyValue value)
{
    return Properties_CheckValue(&ColliderTable, &ColliderProperties[property], &value);
}

static Error CreateNullSelfError(const char* function)
{
    return Error_Construct4(ErrorCode_IllegalArgument, "%s: self must not be NULL.", function);
}

static void SetBool(WorldColliderComponent* self, bool* field, bool value, ColliderProperty property)
{
    *field = value;
    WorldInternal_ComponentAfterSet(self, &ColliderProperties[property]);
}

static bool TryGetOverrideFlag(WorldColliderComponent* self, WorldColliderSetting setting, bool** outFlag,
    ColliderProperty* outProperty)
{
    switch (setting)
    {
        case WorldColliderSetting_CollisionSet:
            *outFlag = &self->_overridesCollisionSet;
            *outProperty = ColliderProperty_OverridesCollisionSet;
            return true;
        case WorldColliderSetting_Layer:
            *outFlag = &self->_overridesLayer;
            *outProperty = ColliderProperty_OverridesLayer;
            return true;
        case WorldColliderSetting_CollidesWith:
            *outFlag = &self->_overridesCollidesWith;
            *outProperty = ColliderProperty_OverridesCollidesWith;
            return true;
        case WorldColliderSetting_IsTrigger:
            *outFlag = &self->_overridesIsTrigger;
            *outProperty = ColliderProperty_OverridesIsTrigger;
            return true;
        case WorldColliderSetting_Mass:
            *outFlag = &self->_overridesMass;
            *outProperty = ColliderProperty_OverridesMass;
            return true;
        case WorldColliderSetting_Count:
            break;
    }
    return false;
}

static bool IsPrimitive(CollisionShapeType type)
{
    switch (type)
    {
        case CollisionShapeType_Sphere:
        case CollisionShapeType_Capsule:
        case CollisionShapeType_Box:
        case CollisionShapeType_Cylinder:
        case CollisionShapeType_Plane:
            return true;
        case CollisionShapeType_ConvexHull:
        case CollisionShapeType_Mesh:
        case CollisionShapeType_Count:
            break;
    }
    return false;
}


// Fields: the registry entry (it refers to the functions above).
static const WorldComponentTypeInfo ColliderTypeInfo =
{
    .Type = WorldComponentType_Collider,
    .Name = u8"collider",
    .Size = sizeof(WorldColliderComponent),
    .Table = &ColliderTable,
    .IsTransformDriver = false,
    .Construct = ConstructCollider,
    .Deconstruct = DeconstructCollider,
    .OnAttach = NULL,
    .OnDetach = NULL,
    .OnPropertySet = NULL
};


// Public functions: the type.
const WorldComponentTypeInfo* WorldColliderComponent_GetTypeInfo(void)
{
    return &ColliderTypeInfo;
}

const PropertyTable* WorldColliderComponent_GetPropertyTable(void)
{
    return &ColliderTable;
}


// Public functions: reading.
bool WorldColliderComponent_IsOverridden(const WorldColliderComponent* self, WorldColliderSetting setting)
{
    switch (setting)
    {
        case WorldColliderSetting_CollisionSet: return self->_overridesCollisionSet;
        case WorldColliderSetting_Layer:        return self->_overridesLayer;
        case WorldColliderSetting_CollidesWith: return self->_overridesCollidesWith;
        case WorldColliderSetting_IsTrigger:    return self->_overridesIsTrigger;
        case WorldColliderSetting_Mass:         return self->_overridesMass;
        case WorldColliderSetting_Count:        break;
    }
    // Only reachable with a value outside the enum: nothing is overridden.
    return false;
}

CollisionShape WorldColliderComponent_GetOwnShape(const WorldColliderComponent* self)
{
    CollisionShapeType Type = IsPrimitive(self->_shapeType) ? self->_shapeType : CollisionShapeType_Box;
    CollisionShape Shape = CollisionShape_Create(Type);
    Shape.Center = self->_shapeCenter;
    Shape.Rotation = self->_shapeRotation;
    Shape.Radius = self->_shapeRadius;
    Shape.HalfHeight = self->_shapeHalfHeight;
    Shape.HalfExtents = self->_shapeHalfExtents;
    Shape.IsBounded = self->_shapeIsBounded;
    Shape.PlaneHalfExtents = self->_shapePlaneHalfExtents;
    Shape.Depth = self->_shapeDepth;
    return Shape;
}

Matrix WorldColliderComponent_GetLocalTransform(const WorldColliderComponent* self)
{
    return WorldTransform_Compose(self->_offset, self->_rotation, self->_scale);
}

void WorldColliderComponent_ResolveSettings(const WorldColliderComponent* self, const WorldModelCollision* collision,
    WorldColliderSettings* outSettings)
{
    Memory_Zero(outSettings, sizeof(*outSettings));
    CollisionLayer DefaultLayer = (collision != NULL) ? collision->_layer : CollisionLayer_Prop;
    outSettings->Layer = self->_overridesLayer ? self->_layer : DefaultLayer;
    bool DefaultTrigger = (collision != NULL) && collision->_isTrigger;
    outSettings->IsTrigger = self->_overridesIsTrigger ? self->_isTrigger : DefaultTrigger;
    if (self->_overridesCollidesWith)
    {
        outSettings->CollidesWith = self->_collidesWith;
    }
    else
    {
        outSettings->CollidesWith = ((collision != NULL) && collision->_hasCollidesWith) ? collision->_collidesWith
            : CollisionLayer_GetDefaultMask(outSettings->Layer);
    }
    float DefaultMass = (collision != NULL) ? collision->_mass : COLLISION_DEFAULT_MASS;
    outSettings->Mass = self->_overridesMass ? self->_mass : DefaultMass;
    outSettings->IsPushable = self->_isPushable;
    outSettings->WhenBlocked = self->_whenBlocked;
    outSettings->IsContinuous = self->_isContinuous;
    outSettings->ReportsPersistingContacts = self->_reportsPersistingContacts;
    outSettings->UsesOwnShape = self->_usesOwnShape;
    if (self->_usesOwnShape || (collision == NULL))
    {
        return;
    }
    if (!self->_overridesCollisionSet)
    {
        outSettings->HasSet = true;
        outSettings->SetIndex = collision->_defaultSet;
        return;
    }
    outSettings->HasSet = WorldModelCollision_TryFindSet(collision, self->_collisionSet, &outSettings->SetIndex);
    outSettings->IsSetMissing = !outSettings->HasSet;
}


// Public functions: writing.
Error WorldColliderComponent_SetEnabled(WorldColliderComponent* self, bool isEnabled)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetEnabled");
    }
    SetBool(self, &self->_isEnabled, isEnabled, ColliderProperty_IsEnabled);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetOverridden(WorldColliderComponent* self, WorldColliderSetting setting,
    bool isOverridden)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetOverridden");
    }
    bool* Flag = NULL;
    ColliderProperty Property = ColliderProperty_Count;
    if (!TryGetOverrideFlag(self, setting, &Flag, &Property))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "WorldColliderComponent_SetOverridden: %d is not a WorldColliderSetting.", (int)setting);
    }
    SetBool(self, Flag, isOverridden, Property);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetCollisionSet(WorldColliderComponent* self, const unsigned char* name)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetCollisionSet");
    }
    PropertyValue Value = PropertyValue_FromString(name);
    return Properties_Set(&ColliderTable, self, ColliderProperties[ColliderProperty_CollisionSet].Key, &Value);
}

Error WorldColliderComponent_SetUsesOwnShape(WorldColliderComponent* self, bool usesOwnShape)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetUsesOwnShape");
    }
    SetBool(self, &self->_usesOwnShape, usesOwnShape, ColliderProperty_UsesOwnShape);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetOwnShape(WorldColliderComponent* self, const CollisionShape* shape)
{
    if ((self == NULL) || (shape == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldColliderComponent_SetOwnShape: self and shape must not be NULL.");
    }
    if (!IsPrimitive(shape->Type))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldColliderComponent_SetOwnShape: an own shape is a sphere, capsule, box, cylinder or plane.");
    }
    struct { ColliderProperty Property; PropertyValue Value; } Checks[] =
    {
        { ColliderProperty_ShapeType, PropertyValue_FromEnum((int32_t)shape->Type) },
        { ColliderProperty_ShapeCenter, PropertyValue_FromVector3(shape->Center) },
        { ColliderProperty_ShapeRotation, PropertyValue_FromAngleVector3(shape->Rotation) },
        { ColliderProperty_ShapeRadius, PropertyValue_FromFloat(shape->Radius) },
        { ColliderProperty_ShapeHalfHeight, PropertyValue_FromFloat(shape->HalfHeight) },
        { ColliderProperty_ShapeHalfExtents, PropertyValue_FromVector3(shape->HalfExtents) },
        { ColliderProperty_ShapePlaneHalfExtents, PropertyValue_FromVector2(shape->PlaneHalfExtents) },
        { ColliderProperty_ShapeDepth, PropertyValue_FromFloat(shape->Depth) },
    };
    for (size_t Index = 0U; Index < (sizeof(Checks) / sizeof(Checks[0])); Index++)
    {
        Error Result = CheckProperty(Checks[Index].Property, Checks[Index].Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    self->_shapeType = shape->Type;
    self->_shapeCenter = shape->Center;
    self->_shapeRotation = shape->Rotation;
    self->_shapeRadius = shape->Radius;
    self->_shapeHalfHeight = shape->HalfHeight;
    self->_shapeHalfExtents = shape->HalfExtents;
    self->_shapeIsBounded = shape->IsBounded;
    self->_shapePlaneHalfExtents = shape->PlaneHalfExtents;
    self->_shapeDepth = shape->Depth;
    WorldInternal_ComponentAfterSet(self, &ColliderProperties[ColliderProperty_ShapeType]);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetLayer(WorldColliderComponent* self, CollisionLayer layer)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetLayer");
    }
    Error Result = CheckProperty(ColliderProperty_Layer, PropertyValue_FromEnum((int32_t)layer));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_layer = layer;
    WorldInternal_ComponentAfterSet(self, &ColliderProperties[ColliderProperty_Layer]);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetCollidesWith(WorldColliderComponent* self, uint32_t mask)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetCollidesWith");
    }
    Error Result = CheckProperty(ColliderProperty_CollidesWith, PropertyValue_FromEnumSet(mask));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_collidesWith = mask;
    WorldInternal_ComponentAfterSet(self, &ColliderProperties[ColliderProperty_CollidesWith]);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetIsTrigger(WorldColliderComponent* self, bool isTrigger)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetIsTrigger");
    }
    SetBool(self, &self->_isTrigger, isTrigger, ColliderProperty_IsTrigger);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetLocalTransform(WorldColliderComponent* self, Vector3 offset, Vector3 rotation,
    Vector3 scale)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetLocalTransform");
    }
    Error Result = CheckProperty(ColliderProperty_Offset, PropertyValue_FromVector3(offset));
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckProperty(ColliderProperty_Rotation, PropertyValue_FromAngleVector3(rotation));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckProperty(ColliderProperty_Scale, PropertyValue_FromVector3(scale));
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_offset = offset;
    self->_rotation = rotation;
    self->_scale = scale;
    WorldInternal_ComponentAfterSet(self, &ColliderProperties[ColliderProperty_Offset]);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetRotationMode(WorldColliderComponent* self, WorldColliderRotationMode mode)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetRotationMode");
    }
    Error Result = CheckProperty(ColliderProperty_RotationMode, PropertyValue_FromEnum((int32_t)mode));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_rotationMode = mode;
    WorldInternal_ComponentAfterSet(self, &ColliderProperties[ColliderProperty_RotationMode]);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetPushable(WorldColliderComponent* self, bool isPushable)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetPushable");
    }
    SetBool(self, &self->_isPushable, isPushable, ColliderProperty_IsPushable);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetMass(WorldColliderComponent* self, float mass)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetMass");
    }
    Error Result = CheckProperty(ColliderProperty_Mass, PropertyValue_FromFloat(mass));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_mass = mass;
    WorldInternal_ComponentAfterSet(self, &ColliderProperties[ColliderProperty_Mass]);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetWhenBlocked(WorldColliderComponent* self, WorldColliderBlockedMode mode)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetWhenBlocked");
    }
    Error Result = CheckProperty(ColliderProperty_WhenBlocked, PropertyValue_FromEnum((int32_t)mode));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_whenBlocked = mode;
    WorldInternal_ComponentAfterSet(self, &ColliderProperties[ColliderProperty_WhenBlocked]);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetContinuous(WorldColliderComponent* self, bool isContinuous)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetContinuous");
    }
    SetBool(self, &self->_isContinuous, isContinuous, ColliderProperty_IsContinuous);
    return Error_CreateSuccess();
}

Error WorldColliderComponent_SetReportsPersistingContacts(WorldColliderComponent* self, bool reportsPersistingContacts)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldColliderComponent_SetReportsPersistingContacts");
    }
    SetBool(self, &self->_reportsPersistingContacts, reportsPersistingContacts,
        ColliderProperty_ReportsPersistingContacts);
    return Error_CreateSuccess();
}
