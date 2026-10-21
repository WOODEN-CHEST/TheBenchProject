#include <math.h>
#include "CollisionShape.h"
#include "CollisionMath.h"

/*
 * Collision shapes (include/CollisionShape.h): the types' names, the defaults, validation, transforms and local
 * bounds. Hulls are built in source/CollisionHull.c, meshes in source/CollisionMesh.c.
 */


// Fields.
static const PropertyEnumEntry ShapeTypeEntries[] =
{
    { u8"sphere", CollisionShapeType_Sphere }, { u8"capsule", CollisionShapeType_Capsule },
    { u8"box", CollisionShapeType_Box }, { u8"cylinder", CollisionShapeType_Cylinder },
    { u8"convex_hull", CollisionShapeType_ConvexHull }, { u8"plane", CollisionShapeType_Plane },
    { u8"mesh", CollisionShapeType_Mesh }
};

static const PropertyEnumEntry PrimitiveTypeEntries[] =
{
    { u8"sphere", CollisionShapeType_Sphere }, { u8"capsule", CollisionShapeType_Capsule },
    { u8"box", CollisionShapeType_Box }, { u8"cylinder", CollisionShapeType_Cylinder },
    { u8"plane", CollisionShapeType_Plane }
};

const PropertyEnumTable COLLISION_SHAPE_TYPE_ENUM =
{
    .Name = u8"collision_shape_type", .Entries = ShapeTypeEntries,
    .EntryCount = sizeof(ShapeTypeEntries) / sizeof(ShapeTypeEntries[0])
};

const PropertyEnumTable COLLISION_PRIMITIVE_TYPE_ENUM =
{
    .Name = u8"collision_primitive_type", .Entries = PrimitiveTypeEntries,
    .EntryCount = sizeof(PrimitiveTypeEntries) / sizeof(PrimitiveTypeEntries[0])
};


// Static functions.
static bool IsPositive(float value)
{
    return isfinite(value) && (value > 0.0f);
}

static bool IsNonNegative(float value)
{
    return isfinite(value) && (value >= 0.0f);
}

static Error Refuse(const char* what)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange, u8"A collision shape's %s.", what);
}

/* The sizes the type uses. */
static Error ValidateSizes(const CollisionShape* self)
{
    switch (self->Type)
    {
        case CollisionShapeType_Sphere:
            return IsPositive(self->Radius) ? Error_CreateSuccess() : Refuse("radius must be finite and above 0");
        case CollisionShapeType_Capsule:
            if (!IsPositive(self->Radius))
            {
                return Refuse("radius must be finite and above 0");
            }
            return IsNonNegative(self->HalfHeight) ? Error_CreateSuccess()
                : Refuse("half_height must be finite and at least 0");
        case CollisionShapeType_Box:
            return (IsPositive(self->HalfExtents.x) && IsPositive(self->HalfExtents.y) && IsPositive(self->HalfExtents.z))
                ? Error_CreateSuccess() : Refuse("half_extents must each be finite and above 0");
        case CollisionShapeType_Cylinder:
            if (!IsPositive(self->Radius))
            {
                return Refuse("radius must be finite and above 0");
            }
            return IsPositive(self->HalfHeight) ? Error_CreateSuccess()
                : Refuse("half_height must be finite and above 0");
        case CollisionShapeType_ConvexHull:
            return (self->Hull != NULL) ? Error_CreateSuccess() : Refuse("hull is missing");
        case CollisionShapeType_Plane:
            if (!self->IsBounded)
            {
                return Error_CreateSuccess();
            }
            if (!IsPositive(self->PlaneHalfExtents.x) || !IsPositive(self->PlaneHalfExtents.y))
            {
                return Refuse("plane half_extents must each be finite and above 0");
            }
            return IsNonNegative(self->Depth) ? Error_CreateSuccess() : Refuse("depth must be finite and at least 0");
        case CollisionShapeType_Mesh:
            return (self->Mesh != NULL) ? Error_CreateSuccess() : Refuse("mesh is missing");
        case CollisionShapeType_Count:
            break;
    }
    return Refuse("type is not a shape type");
}


// Public functions.
CollisionShape CollisionShape_Create(CollisionShapeType type)
{
    return (CollisionShape)
    {
        .Type = type, .Center = { 0.0f, 0.0f, 0.0f }, .Rotation = { 0.0f, 0.0f, 0.0f }, .Name = NULL,
        .Radius = COLLISION_SHAPE_DEFAULT_SIZE, .HalfHeight = COLLISION_SHAPE_DEFAULT_SIZE,
        .HalfExtents = { COLLISION_SHAPE_DEFAULT_SIZE, COLLISION_SHAPE_DEFAULT_SIZE, COLLISION_SHAPE_DEFAULT_SIZE },
        .IsBounded = true, .PlaneHalfExtents = { COLLISION_SHAPE_DEFAULT_SIZE, COLLISION_SHAPE_DEFAULT_SIZE },
        .Depth = 0.0f, .Hull = NULL, .Mesh = NULL, .IsTwoSided = true
    };
}

Error CollisionShape_Validate(const CollisionShape* self)
{
    if (!CollisionMath_IsFinite(self->Center) || !CollisionMath_IsFinite(self->Rotation))
    {
        return Refuse("center and rotation must be finite");
    }
    if ((self->Name != NULL) && !CollisionShape_IsValidName(self->Name))
    {
        return Refuse("name must match [a-z][a-z0-9_.-]* and have at most 128 bytes");
    }
    return ValidateSizes(self);
}

Matrix CollisionShape_GetTransform(const CollisionShape* self)
{
    CollisionAffine Placement = CollisionAffine_FromCenterRotation(self->Center, self->Rotation);
    return CollisionAffine_ToMatrix(&Placement);
}

bool CollisionShape_TryGetLocalBounds(const CollisionShape* self, Vector3* outMin, Vector3* outMax)
{
    Vector3 Half = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    Vector3 Shift = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    *outMin = Half;
    *outMax = Half;
    switch (self->Type)
    {
        case CollisionShapeType_Sphere:
            Half = CollisionMath_Vector(self->Radius, self->Radius, self->Radius);
            break;
        case CollisionShapeType_Capsule:
            Half = CollisionMath_Vector(self->Radius, self->HalfHeight + self->Radius, self->Radius);
            break;
        case CollisionShapeType_Box:
            Half = self->HalfExtents;
            break;
        case CollisionShapeType_Cylinder:
            Half = CollisionMath_Vector(self->Radius, self->HalfHeight, self->Radius);
            break;
        case CollisionShapeType_ConvexHull:
            if (self->Hull == NULL)
            {
                return true;
            }
            *outMin = self->Hull->_boundsMin;
            *outMax = self->Hull->_boundsMax;
            return true;
        case CollisionShapeType_Plane:
            if (!self->IsBounded)
            {
                return false;
            }
            Half = CollisionMath_Vector(self->PlaneHalfExtents.x, self->Depth * 0.5f, self->PlaneHalfExtents.y);
            Shift = CollisionMath_Vector(0.0f, -self->Depth * 0.5f, 0.0f);
            break;
        case CollisionShapeType_Mesh:
            if (self->Mesh == NULL)
            {
                return true;
            }
            *outMin = self->Mesh->_boundsMin;
            *outMax = self->Mesh->_boundsMax;
            return true;
        case CollisionShapeType_Count:
            return true;
    }
    *outMin = CollisionMath_Subtract(Shift, Half);
    *outMax = CollisionMath_Add(Shift, Half);
    return true;
}

bool CollisionShape_IsValidName(const unsigned char* name)
{
    if ((name == NULL) || !((name[0] >= (unsigned char)u8'a') && (name[0] <= (unsigned char)u8'z')))
    {
        return false;
    }
    size_t Length = 0U;
    for (; name[Length] != (unsigned char)u8'\0'; Length++)
    {
        unsigned char Character = name[Length];
        bool IsAllowed = ((Character >= (unsigned char)u8'a') && (Character <= (unsigned char)u8'z'))
            || ((Character >= (unsigned char)u8'0') && (Character <= (unsigned char)u8'9'))
            || (Character == (unsigned char)u8'_') || (Character == (unsigned char)u8'.')
            || (Character == (unsigned char)u8'-');
        if (!IsAllowed || (Length >= COLLISION_NAME_MAX_LENGTH))
        {
            return false;
        }
    }
    return true;
}

const unsigned char* CollisionShapeType_GetName(CollisionShapeType type)
{
    for (size_t Index = 0U; Index < COLLISION_SHAPE_TYPE_ENUM.EntryCount; Index++)
    {
        if (ShapeTypeEntries[Index].Value == (int32_t)type)
        {
            return ShapeTypeEntries[Index].Name;
        }
    }
    return NULL;
}
