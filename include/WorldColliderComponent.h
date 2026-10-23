#pragma once
#include <stdbool.h>
#include <stdint.h>
// Vector2 and Vector3 are raylib types held by value (the own shape's sizes, the local transform); this include is
// unavoidable here. The local transform's matrix is a double one (WRVectorMath), like the objects' world matrices.
#include "raylib/raylib.h"
#include "CollisionLayer.h"
#include "CollisionShape.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"
#include "WorldModel.h"
#include "wr/WRError.h"
#include "wr/WRVectorMath.h"


/**
 * @file WorldColliderComponent.h
 * @brief The collider component: how an object takes part in collision (T12).
 *
 * WHICH SHAPES. By default the collider uses its object's world model (the model component's world model) and that
 * model's default collision set; with overrides_collision_set on, the set named by collision_set (switching sets is how
 * a door swaps its closed and open hitboxes). With uses_own_shape on it uses one primitive of its own instead (the
 * shape_* rows: a sphere, capsule, box, cylinder or plane), for invisible walls, trigger volumes and objects without a
 * model. An object without a model component (or whose world model has no collision) and without an own shape has no
 * shapes.
 *
 * HOW IT COLLIDES. A layer (CollisionLayer.h), the mask of layers it collides with, and whether it is a trigger (it is
 * then reported by overlaps and trigger events and never blocks). Each defaults to its world model's collision section
 * (a layer, an optional mask, a trigger flag; without one: prop, the layer's default mask, no trigger), and each can be
 * overridden by the placement with an explicit flag next to its value (the owner's rule, G17): overrides_layer with
 * layer, overrides_collides_with with collides_with, overrides_is_trigger with is_trigger. While a flag is off its value
 * is kept and saved. WorldColliderComponent_ResolveSettings is the pure function that gives the settings in effect.
 *
 * WHERE. The shapes sit in the object's space, through a local transform of the collider (offset, rotation, scale: the
 * "distorting" of hitboxes), and follow the object's world matrix by the rotation mode: full (they turn with the
 * object), yaw_only (they stay upright: only the turn about the world's up axis) or none (they stay axis-aligned). An
 * uneven scale applies exactly to every shape (an unevenly scaled sphere is an ellipsoid).
 *
 * HOW IT MOVES (T27). The collision step (WorldCollision_Step) sweeps every moving collider and pushes apart what
 * overlaps. A movable object's collider with is_pushable on is PUSHED: characters, NPCs, crates (later physics objects);
 * pushables share a push by mass (overrides_mass with mass over its world model's mass, 1 kg without one; equal
 * masses give way equally). A movable object's collider that is not pushable is KINEMATIC: doors, elevators, anything a
 * script or an animation moves; it pushes and is never pushed, and when_blocked says what it does when it would squeeze
 * a pushable against something that does not give way (stop: it stays where it was this tick; push_through: it moves on).
 * A static object's collider is never pushed and never moves in play. is_continuous off skips the sweep of this
 * collider's own motion, its pushes included (cheap movers that never go fast: its contacts still hold it, and what
 * moves against it is still swept, against where it ends up). reports_persisting_contacts asks for a contact event
 * every tick while it touches something, not only when a touch begins and ends.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Collider, &component); reach its fields with
 * WorldColliderComponent_FromComponent. Its properties are the rows of its table ("collider_component"), in this order:
 * is_enabled, overrides_collision_set, collision_set, uses_own_shape, shape_type, shape_center, shape_rotation,
 * shape_radius, shape_half_height, shape_half_extents, shape_is_bounded, shape_plane_half_extents, shape_depth,
 * overrides_layer, layer, overrides_collides_with, collides_with, overrides_is_trigger, is_trigger, offset, rotation,
 * scale, rotation_mode, is_pushable, overrides_mass, mass, when_blocked, is_continuous, reports_persisting_contacts.
 * Every change advances the component's revision; the collision world (WorldCollision.h) watches it.
 */


// Types.
/**
 * @brief How a collider's shapes follow their object's rotation. Saved by value: never renumber.
 */
typedef enum WorldColliderRotationModeEnum
{
    /** @brief The shapes turn with the object (the default). */
    WorldColliderRotationMode_Full = 0,
    /** @brief The shapes keep only the object's turn about the world's up axis: they stay upright (characters, posts). */
    WorldColliderRotationMode_YawOnly = 1,
    /** @brief The shapes do not turn: they stay axis-aligned (cheap boxes for crates). */
    WorldColliderRotationMode_None = 2
} WorldColliderRotationMode;

/**
 * @brief What a kinematic mover does when its push cannot be satisfied: it would squeeze a pushable against something
 *        that does not give way (T27). Saved by value: never renumber.
 */
typedef enum WorldColliderBlockedModeEnum
{
    /** @brief The mover stays where it was at the start of the tick (the default): a door stops on a pinned box. */
    WorldColliderBlockedMode_Stop = 0,
    /** @brief The mover moves on, and the pushable is left overlapping it as little as it can. */
    WorldColliderBlockedMode_PushThrough = 1
} WorldColliderBlockedMode;

/**
 * @brief The settings a placement can override, each with its own flag.
 */
typedef enum WorldColliderSettingEnum
{
    /** @brief The collision set (collision_set, flag overrides_collision_set). */
    WorldColliderSetting_CollisionSet,
    /** @brief The layer (layer, flag overrides_layer). */
    WorldColliderSetting_Layer,
    /** @brief The mask (collides_with, flag overrides_collides_with). */
    WorldColliderSetting_CollidesWith,
    /** @brief The trigger flag (is_trigger, flag overrides_is_trigger). */
    WorldColliderSetting_IsTrigger,
    /** @brief The mass (mass, flag overrides_mass; T27). */
    WorldColliderSetting_Mass,
    /** @brief The number of settings; not a setting. */
    WorldColliderSetting_Count
} WorldColliderSetting;

/**
 * @brief The settings in effect for one collider (WorldColliderComponent_ResolveSettings). A plain value type.
 */
typedef struct WorldColliderSettingsStruct
{
    /** @brief Whether the collider uses its own primitive (WorldColliderComponent_GetOwnShape). */
    bool UsesOwnShape;
    /** @brief Whether a set of the world model is used (SetIndex): false with an own shape, without a world model's
     *  collision, or when the named set does not exist. */
    bool HasSet;
    /** @brief The set's index in the world model's collision, while HasSet. */
    uint32_t SetIndex;
    /** @brief The collider names a set its world model does not have (it then has no shapes; worth a warning). */
    bool IsSetMissing;
    /** @brief The layer. */
    CollisionLayer Layer;
    /** @brief The layers it collides with (COLLISION_LAYER_BIT per layer). */
    uint32_t CollidesWith;
    /** @brief Whether it is a trigger. */
    bool IsTrigger;
    /** @brief Whether the collision step may push it (the collider's is_pushable; it applies to movable objects only, and
     *  a trigger is never pushed). */
    bool IsPushable;
    /** @brief The mass, in kilograms (the placement's, else its world model's, else COLLISION_DEFAULT_MASS). */
    float Mass;
    /** @brief What it does as a kinematic mover whose push cannot be satisfied. */
    WorldColliderBlockedMode WhenBlocked;
    /** @brief Whether its own motion is swept (continuous) or only pushed out at its new pose. */
    bool IsContinuous;
    /** @brief Whether its contacts report every tick while they last (persist events). */
    bool ReportsPersistingContacts;
} WorldColliderSettings;

/**
 * @brief The collider component. Underscore-prefixed fields are read-only: use the accessors and setters (or the table).
 */
typedef struct WorldColliderComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Whether the collider is in the collision world (default true). */
    bool _isEnabled;
    /** @brief Whether _collisionSet replaces the world model's default set (default false). */
    bool _overridesCollisionSet;
    /** @brief Owned name of the set used while _overridesCollisionSet is on (at most COLLISION_NAME_MAX_LENGTH bytes;
     *  default NULL); kept while it is off. */
    unsigned char* _collisionSet;
    /** @brief Whether the collider uses its own primitive (the _shape* fields) instead of the world model's sets
     *  (default false). */
    bool _usesOwnShape;
    /** @brief The own shape's type: a primitive (sphere, capsule, box, cylinder or plane; default box). */
    CollisionShapeType _shapeType;
    /** @brief The own shape's centre and rotation (Euler XYZ, radians) in the object's space (defaults: none). */
    Vector3 _shapeCenter;
    Vector3 _shapeRotation;
    /** @brief Sphere, capsule, cylinder: the radius (above 0; default COLLISION_SHAPE_DEFAULT_SIZE). */
    float _shapeRadius;
    /** @brief Capsule, cylinder: the half height (at least 0; default COLLISION_SHAPE_DEFAULT_SIZE; a cylinder needs
     *  more than 0). */
    float _shapeHalfHeight;
    /** @brief Box: the half extents (each above 0; default COLLISION_SHAPE_DEFAULT_SIZE). */
    Vector3 _shapeHalfExtents;
    /** @brief Plane: a rectangle (true, the default) or an infinite half-space. */
    bool _shapeIsBounded;
    /** @brief Plane: the rectangle's half extents along local X and Z (each above 0; default
     *  COLLISION_SHAPE_DEFAULT_SIZE). */
    Vector2 _shapePlaneHalfExtents;
    /** @brief Plane: how far behind the rectangle it is solid (at least 0; default 0, a flat rectangle). */
    float _shapeDepth;
    /** @brief Whether _layer replaces the default layer (default false). */
    bool _overridesLayer;
    /** @brief The layer used while _overridesLayer is on (default CollisionLayer_Prop). */
    CollisionLayer _layer;
    /** @brief Whether _collidesWith replaces the default mask (default false). */
    bool _overridesCollidesWith;
    /** @brief The mask used while _overridesCollidesWith is on (default: prop's default mask). */
    uint32_t _collidesWith;
    /** @brief Whether _isTrigger replaces the default trigger flag (default false). */
    bool _overridesIsTrigger;
    /** @brief The trigger flag used while _overridesIsTrigger is on (default false). */
    bool _isTrigger;
    /** @brief The collider's local transform on top of the object's: offset, rotation (Euler XYZ, radians) and scale
     *  (each above 0). Defaults: none, none, (1, 1, 1). */
    Vector3 _offset;
    Vector3 _rotation;
    Vector3 _scale;
    /** @brief How the shapes follow the object's rotation (default WorldColliderRotationMode_Full). */
    WorldColliderRotationMode _rotationMode;
    /** @brief Whether the collision step may push the object (default false: a movable object's collider is kinematic). */
    bool _isPushable;
    /** @brief Whether _mass replaces the world model's mass (default false). */
    bool _overridesMass;
    /** @brief The mass used while _overridesMass is on, in kilograms (above 0, at most COLLISION_MAX_MASS; default
     *  COLLISION_DEFAULT_MASS). */
    float _mass;
    /** @brief What a kinematic mover does when its push cannot be satisfied (default WorldColliderBlockedMode_Stop). */
    WorldColliderBlockedMode _whenBlocked;
    /** @brief Whether the collider's own motion is swept (default true). */
    bool _isContinuous;
    /** @brief Whether its contacts report every tick while they last (default false). */
    bool _reportsPersistingContacts;
} WorldColliderComponent;


// Fields.
/** @brief The rotation modes' names ("full", "yaw_only", "none"). */
extern const PropertyEnumTable WORLD_COLLIDER_ROTATION_MODE_ENUM;
/** @brief The blocked modes' names ("stop", "push_through"). */
extern const PropertyEnumTable WORLD_COLLIDER_BLOCKED_MODE_ENUM;


// Functions: the type.
/**
 * @brief Returns the collider component's registry entry (WorldComponentType_Collider, "collider").
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldColliderComponent_GetTypeInfo(void);

/**
 * @brief Returns the collider component's property table ("collider_component").
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldColliderComponent_GetPropertyTable(void);

/**
 * @brief Downcasts a component to a collider component.
 * @param component The component, or NULL.
 * @returns The collider component, or NULL when @p component is NULL or of another type.
 */
static inline WorldColliderComponent* WorldColliderComponent_FromComponent(WorldComponent* component)
{
    return ((component != NULL) && (component->_type == WorldComponentType_Collider))
        ? (WorldColliderComponent*)component : NULL;
}

/**
 * @brief Upcasts a collider component to its base.
 * @param self The collider component; must not be NULL.
 * @returns The embedded base.
 */
static inline WorldComponent* WorldColliderComponent_AsComponent(WorldColliderComponent* self)
{
    return &self->Base;
}


// Functions: reading.
/** @brief Whether the collider is in the collision world. @param self The component; must not be NULL. */
static inline bool WorldColliderComponent_IsEnabled(const WorldColliderComponent* self)
{
    return self->_isEnabled;
}

/** @brief Whether the collider uses its own primitive. @param self The component; must not be NULL. */
static inline bool WorldColliderComponent_UsesOwnShape(const WorldColliderComponent* self)
{
    return self->_usesOwnShape;
}

/**
 * @brief The set name the placement keeps (used while its override flag is on).
 * @param self The component; must not be NULL.
 * @returns The name (borrowed: valid until it changes), or NULL.
 */
static inline const unsigned char* WorldColliderComponent_GetCollisionSet(const WorldColliderComponent* self)
{
    return self->_collisionSet;
}

/** @brief The placement's own layer (used while its override flag is on). @param self The component; must not be
 *  NULL. */
static inline CollisionLayer WorldColliderComponent_GetLayer(const WorldColliderComponent* self)
{
    return self->_layer;
}

/** @brief The placement's own mask (used while its override flag is on). @param self The component; must not be
 *  NULL. */
static inline uint32_t WorldColliderComponent_GetCollidesWith(const WorldColliderComponent* self)
{
    return self->_collidesWith;
}

/** @brief The placement's own trigger flag (used while its override flag is on). @param self The component; must not
 *  be NULL. */
static inline bool WorldColliderComponent_IsTrigger(const WorldColliderComponent* self)
{
    return self->_isTrigger;
}

/** @brief The collider's local offset. @param self The component; must not be NULL. */
static inline Vector3 WorldColliderComponent_GetOffset(const WorldColliderComponent* self)
{
    return self->_offset;
}

/** @brief The collider's local rotation (Euler XYZ, radians). @param self The component; must not be NULL. */
static inline Vector3 WorldColliderComponent_GetRotation(const WorldColliderComponent* self)
{
    return self->_rotation;
}

/** @brief The collider's local scale. @param self The component; must not be NULL. */
static inline Vector3 WorldColliderComponent_GetScale(const WorldColliderComponent* self)
{
    return self->_scale;
}

/** @brief How the shapes follow the object's rotation. @param self The component; must not be NULL. */
static inline WorldColliderRotationMode WorldColliderComponent_GetRotationMode(const WorldColliderComponent* self)
{
    return self->_rotationMode;
}

/** @brief Whether the collision step may push the object (on a movable object). @param self The component; must not
 *  be NULL. */
static inline bool WorldColliderComponent_IsPushable(const WorldColliderComponent* self)
{
    return self->_isPushable;
}

/** @brief The placement's own mass (used while its override flag is on), in kilograms. @param self The component; must
 *  not be NULL. */
static inline float WorldColliderComponent_GetMass(const WorldColliderComponent* self)
{
    return self->_mass;
}

/** @brief What a kinematic mover does when its push cannot be satisfied. @param self The component; must not be
 *  NULL. */
static inline WorldColliderBlockedMode WorldColliderComponent_GetWhenBlocked(const WorldColliderComponent* self)
{
    return self->_whenBlocked;
}

/** @brief Whether the collider's own motion is swept. @param self The component; must not be NULL. */
static inline bool WorldColliderComponent_IsContinuous(const WorldColliderComponent* self)
{
    return self->_isContinuous;
}

/** @brief Whether its contacts report every tick while they last. @param self The component; must not be NULL. */
static inline bool WorldColliderComponent_ReportsPersistingContacts(const WorldColliderComponent* self)
{
    return self->_reportsPersistingContacts;
}

/**
 * @brief Whether a setting's override flag is on.
 * @param self The component; must not be NULL.
 * @param setting The setting.
 * @returns The flag (false for a value that is not a setting).
 */
bool WorldColliderComponent_IsOverridden(const WorldColliderComponent* self, WorldColliderSetting setting);

/**
 * @brief The collider's own primitive, as a shape (centre, rotation, sizes; unnamed).
 * @param self The component; must not be NULL.
 * @returns The shape (its type one of the primitives).
 */
CollisionShape WorldColliderComponent_GetOwnShape(const WorldColliderComponent* self);

/**
 * @brief The collider's local transform: its scale, then its rotation, then its offset (the order of an object's own).
 * @param self The component; must not be NULL.
 * @returns The matrix, in double like the objects' world matrices it is multiplied with (WRVectorMath's conventions,
 *          raylib's order: Matrix4Double_Multiply(local, objectWorld) applies it first).
 */
Matrix4Double WorldColliderComponent_GetLocalTransform(const WorldColliderComponent* self);

/**
 * @brief The settings in effect: which shapes (an own shape, or which of the world model's sets), the layer, the mask
 *        and the trigger flag, from the placement's overrides over the world model's defaults.
 *
 * A pure function. The defaults without a world model's collision are prop, the layer's default mask, no trigger and
 * COLLISION_DEFAULT_MASS.
 * @param self The component; must not be NULL.
 * @param collision The object's world model's collision, or NULL when it has none (or no world model).
 * @param outSettings [out] Receives the settings. Must not be NULL.
 */
void WorldColliderComponent_ResolveSettings(const WorldColliderComponent* self, const WorldModelCollision* collision,
    WorldColliderSettings* outSettings);


// Functions: writing (each validates through the table and advances the revision).
/**
 * @brief Puts the collider in the collision world or takes it out.
 * @param self The component; must not be NULL.
 * @param isEnabled The flag.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self.
 */
Error WorldColliderComponent_SetEnabled(WorldColliderComponent* self, bool isEnabled);

/**
 * @brief Switches a setting's override flag.
 * @param self The component; must not be NULL.
 * @param setting The setting.
 * @param isOverridden Whether the placement's own value applies.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a value that is not
 *          a setting.
 */
Error WorldColliderComponent_SetOverridden(WorldColliderComponent* self, WorldColliderSetting setting,
    bool isOverridden);

/**
 * @brief Sets the placement's own set name (it applies while its override flag is on; the flag is not changed).
 * @param self The component; must not be NULL.
 * @param name The name (copied), or NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a name longer than
 *          COLLISION_NAME_MAX_LENGTH bytes or not valid UTF-8.
 */
Error WorldColliderComponent_SetCollisionSet(WorldColliderComponent* self, const unsigned char* name);

/**
 * @brief Switches between the collider's own primitive and its world model's sets.
 * @param self The component; must not be NULL.
 * @param usesOwnShape Whether the own shape is used.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self.
 */
Error WorldColliderComponent_SetUsesOwnShape(WorldColliderComponent* self, bool usesOwnShape);

/**
 * @brief Sets the collider's own primitive from a shape: its type, centre, rotation and sizes (the name is ignored).
 *
 * All or nothing: every value is checked before anything changes.
 * @param self The component; must not be NULL.
 * @param shape The shape; must not be NULL. Its type must be a primitive (not a hull or a mesh).
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument; ErrorCode_ArgumentOutOfRange for a hull or mesh type
 *          or a value the table refuses.
 */
Error WorldColliderComponent_SetOwnShape(WorldColliderComponent* self, const CollisionShape* shape);

/**
 * @brief Sets the placement's own layer (it applies while its override flag is on).
 * @param self The component; must not be NULL.
 * @param layer The layer.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a value that is not
 *          a layer.
 */
Error WorldColliderComponent_SetLayer(WorldColliderComponent* self, CollisionLayer layer);

/**
 * @brief Sets the placement's own mask (it applies while its override flag is on).
 * @param self The component; must not be NULL.
 * @param mask The layers (COLLISION_LAYER_BIT per layer).
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a bit that is not a
 *          layer.
 */
Error WorldColliderComponent_SetCollidesWith(WorldColliderComponent* self, uint32_t mask);

/**
 * @brief Sets the placement's own trigger flag (it applies while its override flag is on).
 * @param self The component; must not be NULL.
 * @param isTrigger The flag.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self.
 */
Error WorldColliderComponent_SetIsTrigger(WorldColliderComponent* self, bool isTrigger);

/**
 * @brief Sets the collider's local transform.
 * @param self The component; must not be NULL.
 * @param offset The offset.
 * @param rotation The rotation, Euler XYZ in radians.
 * @param scale The scale, each above 0.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a value that is not
 *          finite or a scale not above 0 (nothing is changed then).
 */
Error WorldColliderComponent_SetLocalTransform(WorldColliderComponent* self, Vector3 offset, Vector3 rotation,
    Vector3 scale);

/**
 * @brief Sets how the shapes follow the object's rotation.
 * @param self The component; must not be NULL.
 * @param mode The mode.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a value that is not
 *          a mode.
 */
Error WorldColliderComponent_SetRotationMode(WorldColliderComponent* self, WorldColliderRotationMode mode);

/**
 * @brief Lets the collision step push the object, or not (a movable object's collider that is not pushable is
 *        kinematic: it pushes and is never pushed).
 * @param self The component; must not be NULL.
 * @param isPushable The flag.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self.
 */
Error WorldColliderComponent_SetPushable(WorldColliderComponent* self, bool isPushable);

/**
 * @brief Sets the placement's own mass (it applies while its override flag is on; the flag is not changed).
 * @param self The component; must not be NULL.
 * @param mass The mass in kilograms: above 0, at most COLLISION_MAX_MASS.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a mass outside the
 *          range or not finite.
 */
Error WorldColliderComponent_SetMass(WorldColliderComponent* self, float mass);

/**
 * @brief Sets what a kinematic mover does when its push cannot be satisfied.
 * @param self The component; must not be NULL.
 * @param mode The mode.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a value that is not
 *          a mode.
 */
Error WorldColliderComponent_SetWhenBlocked(WorldColliderComponent* self, WorldColliderBlockedMode mode);

/**
 * @brief Sets whether the collider's own motion is swept (off: it is only pushed out where it ends up).
 * @param self The component; must not be NULL.
 * @param isContinuous The flag.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self.
 */
Error WorldColliderComponent_SetContinuous(WorldColliderComponent* self, bool isContinuous);

/**
 * @brief Sets whether its contacts report every tick while they last.
 * @param self The component; must not be NULL.
 * @param reportsPersistingContacts The flag.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self.
 */
Error WorldColliderComponent_SetReportsPersistingContacts(WorldColliderComponent* self, bool reportsPersistingContacts);
