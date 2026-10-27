#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldModelCollisionInternal.h"
#include "AssetDefinitionCommon.h"
#include "AssetTypesStandard.h"
#include "CollisionMath.h"
#include "GameJSON.h"
#include "GameMath.h"
#include "WorldTransform.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * A world model definition's "collision" section (source/WorldModelCollisionInternal.h).
 *
 *   "collision": "bounds"            one set, "bounds", holding a box around LOD 0 (exact under its import transform)
 *   "collision": {
 *     "default_set": "closed",       the set placements use (default: the first)
 *     "layer": "prop",               the placements' default layer, mask ("collides_with": layer names; left out: the
 *     "collides_with": [...],        layer's default mask), trigger flag and mass (kilograms, above 0: how pushables
 *     "is_trigger": false,           share a push)
 *     "mass": 1,
 *     "sets": [ { "name": "closed", "shapes": [ { "type": "box", ... }, ... ] }, ... ]
 *   }
 *
 * Every shape has "type", optional "name", "center" and "rotation" (degrees), and the keys of its type only:
 * sphere radius; capsule radius, half_height; box half_extents; cylinder radius, half_height; convex_hull points or
 * model, max_vertices; plane is_bounded, half_extents ([x, z]), depth; mesh model, is_two_sided. Anything else is
 * refused, so a key meant for another type is caught.
 */


// Macros.
/** Room for a place in the section ("collision.sets[31].shapes[127]") and for one message. */
#define PATH_CAPACITY ((size_t)96)
#define SHAPE_PATH_CAPACITY ((size_t)128)
#define WHERE_CAPACITY ((size_t)320)
#define MESSAGE_CAPACITY ((size_t)512)
/** How many bytes of a name from the file a message shows. */
#define NAME_PRINT_CAPACITY ((size_t)129)
/** The name of the "bounds" fallback's one set. */
#define BOUNDS_SET_NAME ((const unsigned char*)u8"bounds")
/** The number of keys in a list of known keys. */
#define KEY_COUNT(keys) (sizeof(keys) / sizeof((keys)[0]))
/** An import transform recomposed from its decomposition must agree within this to give an exact bounds box. */
#define BOUNDS_DECOMPOSITION_TOLERANCE 1e-4f


// Types.
typedef struct ShapeDefinitionStruct
{
    CollisionShape Shape;  // the values (Name, Hull and Mesh are set at load)
    unsigned char* Name;   // owned part name, or NULL
    unsigned char* Model;  // owned model asset name (a hull from a model, a mesh), or NULL
    Vector3* Points;       // owned hull points, or NULL
    size_t PointCount;
    uint32_t MaxVertices;
} ShapeDefinition;

typedef struct SetDefinitionStruct
{
    unsigned char* Name;       // owned
    ShapeDefinition* Shapes;   // owned array
    size_t ShapeCount;
} SetDefinition;

struct WorldModelCollisionDefinitionStruct
{
    bool IsBoundsFallback;
    SetDefinition* Sets;       // owned array
    size_t SetCount;
    size_t DefaultSet;
    CollisionLayer Layer;
    bool HasCollidesWith;
    uint32_t CollidesWith;
    bool IsTrigger;
    float Mass;
};

typedef struct ParseContextStruct
{
    const char* Name;          // the world model's name, printable
    WorldModelCollisionDefinition* Definition;
    size_t ShapeTotal;
} ParseContext;


// Fields.
static const unsigned char* const SectionKeys[] =
{
    u8"default_set", u8"layer", u8"collides_with", u8"is_trigger", u8"mass", u8"sets"
};
static const unsigned char* const SetKeys[] = { u8"name", u8"shapes" };
static const unsigned char* const SphereKeys[] = { u8"type", u8"name", u8"center", u8"rotation", u8"radius" };
static const unsigned char* const CapsuleKeys[] =
{
    u8"type", u8"name", u8"center", u8"rotation", u8"radius", u8"half_height"
};
static const unsigned char* const BoxKeys[] = { u8"type", u8"name", u8"center", u8"rotation", u8"half_extents" };
static const unsigned char* const HullKeys[] =
{
    u8"type", u8"name", u8"center", u8"rotation", u8"points", u8"model", u8"max_vertices"
};
static const unsigned char* const PlaneKeys[] =
{
    u8"type", u8"name", u8"center", u8"rotation", u8"is_bounded", u8"half_extents", u8"depth"
};
static const unsigned char* const MeshKeys[] = { u8"type", u8"name", u8"center", u8"rotation", u8"model", u8"is_two_sided" };


// Static functions: messages.
/* An InvalidAssetDefinition error: "World model \"<name>\": " and the formatted text. */
static Error Fail(const ParseContext* context, const char* format, ...)
{
    char Text[MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        Text[0] = '\0';
    }
    return Error_Construct4(ErrorCode_InvalidAssetDefinition, "World model \"%s\": %s", context->Name, Text);
}

/* The inner error with the world model and the place before its message (its code kept); releases it. */
static Error WrapError(const ParseContext* context, const char* where, Error* inner)
{
    Error Wrapped = Error_Construct4(inner->Code, "World model \"%s\": %s: %s", context->Name, where,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}

static Error RejectUnknownKeys(const ParseContext* context, JSONCompound* compound, const unsigned char* const* keys,
    size_t keyCount, const char* path)
{
    char Where[WHERE_CAPACITY];
    snprintf(Where, sizeof(Where), "world model \"%s\", %s", context->Name, path);
    return AssetJSON_RejectUnknownKeys(compound, keys, keyCount, Where);
}

static const char* Printable(const unsigned char* text, char* buffer, size_t capacity)
{
    AssetJSON_CopyPrintable(text, buffer, capacity);
    return buffer;
}


// Static functions: values.
static Error ReadFloat(const ParseContext* context, JSONCompound* compound, const unsigned char* key, const char* path,
    float defaultValue, float* outValue, bool* outFound)
{
    Error Result = AssetJSON_ReadOptionalFloat(compound, key, defaultValue, outValue, outFound);
    return (Result.Code == ErrorCode_Success) ? Result : WrapError(context, path, &Result);
}

static Error ReadBoolean(const ParseContext* context, JSONCompound* compound, const unsigned char* key,
    const char* path, bool defaultValue, bool* outValue)
{
    Error Result = AssetJSON_ReadOptionalBoolean(compound, key, defaultValue, outValue);
    if (Result.Code == ErrorCode_Success)
    {
        return Result;
    }
    Error_Deconstruct(&Result);
    return Fail(context, "%s.%s must be true or false.", path, (const char*)key);
}

/* An optional vector: a Vector3 ([x, y, z]) or, with @p isPlanar, a Vector2 ([x, z]) into (out.x, out.z). */
static Error ReadVector(const ParseContext* context, JSONCompound* compound, const unsigned char* key,
    const char* path, bool isPlanar, Vector3* outValue, bool* outFound)
{
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (outFound != NULL)
    {
        *outFound = Found;
    }
    if ((Result.Code != ErrorCode_Success) || !Found)
    {
        return Result;
    }
    char Where[SHAPE_PATH_CAPACITY + 32U];
    snprintf(Where, sizeof(Where), "%s.%s", path, (const char*)key);
    if (isPlanar)
    {
        Vector2 Planar;
        Result = GameJSON_ParseVector2(&Value, &Planar);
        *outValue = CollisionMath_Vector(Planar.x, 0.0f, Planar.y);
    }
    else
    {
        Result = GameJSON_ParseVector3(&Value, outValue);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(context, Where, &Result);
    }
    return CollisionMath_IsFinite(*outValue) ? Error_CreateSuccess() : Fail(context, "%s must be finite.", Where);
}

/* An optional string that must be one of an enum table's names; *outValue its value. */
static Error ReadEnumName(const ParseContext* context, JSONCompound* compound, const unsigned char* key,
    const char* path, const PropertyEnumTable* table, int32_t* outValue, bool* outFound)
{
    unsigned char* Text = NULL;
    bool Found = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(compound, key, &Text, &Found);
    *outFound = Found;
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(context, "%s.%s must be a string.", path, (const char*)key);
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }
    for (size_t Index = 0U; Index < table->EntryCount; Index++)
    {
        bool IsEqual = false;
        Error CompareResult = StringUTF8_EqualsExact(Text, table->Entries[Index].Name, &IsEqual);
        Error_Deconstruct(&CompareResult);
        if (IsEqual)
        {
            *outValue = table->Entries[Index].Value;
            Memory_Free(Text);
            return Error_CreateSuccess();
        }
    }
    char Name[NAME_PRINT_CAPACITY];
    Result = Fail(context, "%s.%s is \"%s\", which is not a %s.", path, (const char*)key,
        Printable(Text, Name, sizeof(Name)), (const char*)table->Name);
    Memory_Free(Text);
    return Result;
}

/* An optional name ([a-z][a-z0-9_.-]*, at most COLLISION_NAME_MAX_LENGTH bytes) or asset name, owned. */
static Error ReadName(const ParseContext* context, JSONCompound* compound, const unsigned char* key, const char* path,
    bool isAssetName, bool isRequired, unsigned char** outName)
{
    bool Found = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(compound, key, outName, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(context, "%s.%s must be a string.", path, (const char*)key);
    }
    if (!Found)
    {
        return isRequired ? Fail(context, "%s needs a \"%s\".", path, (const char*)key) : Error_CreateSuccess();
    }
    bool IsValid = isAssetName ? AssetManager_IsValidAssetName(*outName) : CollisionShape_IsValidName(*outName);
    if (!IsValid)
    {
        char Name[NAME_PRINT_CAPACITY];
        return Fail(context, "%s.%s \"%s\" is not a valid name ([a-z][a-z0-9_.-]*, at most %u bytes).", path,
            (const char*)key, Printable(*outName, Name, sizeof(Name)), (unsigned)COLLISION_NAME_MAX_LENGTH);
    }
    return Error_CreateSuccess();
}


// Static functions: shapes.
/* A required size above 0 (or at least 0 when @p allowsZero). */
static Error ReadSize(const ParseContext* context, JSONCompound* compound, const unsigned char* key, const char* path,
    bool allowsZero, float* outValue)
{
    bool Found = false;
    Error Result = ReadFloat(context, compound, key, path, 0.0f, outValue, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Found)
    {
        return Fail(context, "%s needs a \"%s\" in metres.", path, (const char*)key);
    }
    bool IsValid = allowsZero ? (*outValue >= 0.0f) : (*outValue > 0.0f);
    return IsValid ? Error_CreateSuccess() : Fail(context, "%s.%s is %g; it must be %s 0.", path, (const char*)key,
        (double)*outValue, allowsZero ? "at least" : "above");
}

static Error ReadHalfExtents(const ParseContext* context, JSONCompound* compound, const char* path, bool isPlanar,
    Vector3* outValue)
{
    bool Found = false;
    Error Result = ReadVector(context, compound, (const unsigned char*)u8"half_extents", path, isPlanar, outValue,
        &Found);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Found)
    {
        return Fail(context, "%s needs \"half_extents\": %s in metres.", path, isPlanar ? "[x, z]" : "[x, y, z]");
    }
    bool IsValid = (outValue->x > 0.0f) && (outValue->z > 0.0f) && (isPlanar || (outValue->y > 0.0f));
    return IsValid ? Error_CreateSuccess() : Fail(context, "%s.half_extents must each be above 0.", path);
}

/* A hull's source: "points" (4 or more, spanning a volume: built once here to check) or "model"; and max_vertices. */
static Error ParseHull(const ParseContext* context, JSONCompound* compound, const char* path, ShapeDefinition* shape)
{
    int64_t MaxVertices = COLLISION_HULL_DEFAULT_MAX_VERTICES;
    Error Result = AssetJSON_ReadOptionalInteger(compound, (const unsigned char*)u8"max_vertices",
        COLLISION_HULL_DEFAULT_MAX_VERTICES, &MaxVertices);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(context, path, &Result);
    }
    if ((MaxVertices < (int64_t)COLLISION_HULL_MIN_VERTICES) || (MaxVertices > (int64_t)COLLISION_HULL_MAX_VERTICES))
    {
        return Fail(context, "%s.max_vertices is %" PRId64 "; it must be %u to %u.", path, MaxVertices,
            COLLISION_HULL_MIN_VERTICES, COLLISION_HULL_MAX_VERTICES);
    }
    shape->MaxVertices = (uint32_t)MaxVertices;
    Result = ReadName(context, compound, (const unsigned char*)u8"model", path, true, false, &shape->Model);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    JSONObjectValue Points;
    bool HasPoints = false;
    Result = JSONCompound_GetOptionalVerified(compound, (const unsigned char*)u8"points", JSONValueType_Array, &Points,
        &HasPoints);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(context, "%s.points must be an array of [x, y, z].", path);
    }
    if (HasPoints == (shape->Model != NULL))
    {
        return Fail(context, "%s needs either \"points\" or \"model\" (one of them).", path);
    }
    if (!HasPoints)
    {
        return Error_CreateSuccess();
    }
    size_t Count = JSONArray_GetElementCount(Points.Value.Array);
    if ((Count < 4U) || (Count > COLLISION_HULL_MAX_INPUT_POINTS))
    {
        return Fail(context, "%s.points holds %" PRIu64 " points; a hull needs 4 to %u.", path, (uint64_t)Count,
            (unsigned)COLLISION_HULL_MAX_INPUT_POINTS);
    }
    shape->Points = Memory_Allocate(Count * sizeof(Vector3));
    shape->PointCount = Count;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        JSONObjectValue Point;
        Result = JSONArray_Get(Points.Value.Array, Index, &Point);
        if (Result.Code == ErrorCode_Success)
        {
            Result = GameJSON_ParseVector3(&Point, &shape->Points[Index]);
        }
        if ((Result.Code != ErrorCode_Success) || !CollisionMath_IsFinite(shape->Points[Index]))
        {
            Error_Deconstruct(&Result);
            return Fail(context, "%s.points[%" PRIu64 "] must be a finite [x, y, z].", path, (uint64_t)Index);
        }
    }
    CollisionHull* Trial = NULL;
    Result = CollisionHull_Create(shape->Points, Count, shape->MaxVertices, &Trial);
    CollisionHull_Destroy(Trial);
    return (Result.Code == ErrorCode_Success) ? Result : WrapError(context, path, &Result);
}

/* The keys and values of one type. */
static Error ParseTypeValues(const ParseContext* context, JSONCompound* compound, const char* path,
    ShapeDefinition* shape)
{
    CollisionShape* Shape = &shape->Shape;
    Error Result = Error_CreateSuccess();
    switch (Shape->Type)
    {
        case CollisionShapeType_Sphere:
            Result = RejectUnknownKeys(context, compound, SphereKeys, KEY_COUNT(SphereKeys), path);
            return (Result.Code == ErrorCode_Success)
                ? ReadSize(context, compound, (const unsigned char*)u8"radius", path, false, &Shape->Radius) : Result;
        case CollisionShapeType_Capsule:
        case CollisionShapeType_Cylinder:
        {
            // Capsules and cylinders take the same keys; a capsule may have no straight part (a sphere).
            bool IsCapsule = Shape->Type == CollisionShapeType_Capsule;
            Result = RejectUnknownKeys(context, compound, CapsuleKeys, KEY_COUNT(CapsuleKeys), path);
            if (Result.Code == ErrorCode_Success)
            {
                Result = ReadSize(context, compound, (const unsigned char*)u8"radius", path, false, &Shape->Radius);
            }
            return (Result.Code == ErrorCode_Success) ? ReadSize(context, compound,
                (const unsigned char*)u8"half_height", path, IsCapsule, &Shape->HalfHeight) : Result;
        }
        case CollisionShapeType_Box:
            Result = RejectUnknownKeys(context, compound, BoxKeys, KEY_COUNT(BoxKeys), path);
            return (Result.Code == ErrorCode_Success) ? ReadHalfExtents(context, compound, path, false,
                &Shape->HalfExtents) : Result;
        case CollisionShapeType_ConvexHull:
            Result = RejectUnknownKeys(context, compound, HullKeys, KEY_COUNT(HullKeys), path);
            return (Result.Code == ErrorCode_Success) ? ParseHull(context, compound, path, shape) : Result;
        case CollisionShapeType_Plane:
        {
            Result = RejectUnknownKeys(context, compound, PlaneKeys, KEY_COUNT(PlaneKeys), path);
            if (Result.Code == ErrorCode_Success)
            {
                Result = ReadBoolean(context, compound, (const unsigned char*)u8"is_bounded", path, true,
                    &Shape->IsBounded);
            }
            bool HasDepth = false;
            if (Result.Code == ErrorCode_Success)
            {
                Result = ReadFloat(context, compound, (const unsigned char*)u8"depth", path, 0.0f, &Shape->Depth,
                    &HasDepth);
            }
            if ((Result.Code == ErrorCode_Success) && !(Shape->Depth >= 0.0f))
            {
                return Fail(context, "%s.depth is %g; it must be at least 0.", path, (double)Shape->Depth);
            }
            if ((Result.Code != ErrorCode_Success) || !Shape->IsBounded)
            {
                // An infinite plane keeps its rectangle's values, if given, unused.
                Vector3 Unused;
                bool HasExtents = false;
                return (Result.Code == ErrorCode_Success) ? ReadVector(context, compound,
                    (const unsigned char*)u8"half_extents", path, true, &Unused, &HasExtents) : Result;
            }
            Vector3 Half;
            Result = ReadHalfExtents(context, compound, path, true, &Half);
            Shape->PlaneHalfExtents = (Vector2){ .x = Half.x, .y = Half.z };
            return Result;
        }
        case CollisionShapeType_Mesh:
            Result = RejectUnknownKeys(context, compound, MeshKeys, KEY_COUNT(MeshKeys), path);
            if (Result.Code == ErrorCode_Success)
            {
                Result = ReadName(context, compound, (const unsigned char*)u8"model", path, true, true, &shape->Model);
            }
            return (Result.Code == ErrorCode_Success) ? ReadBoolean(context, compound,
                (const unsigned char*)u8"is_two_sided", path, true, &Shape->IsTwoSided) : Result;
        case CollisionShapeType_Count:
            break;
    }
    return Fail(context, "%s has an unknown type.", path);
}

static Error ParseShape(const ParseContext* context, JSONCompound* compound, const char* path, ShapeDefinition* shape)
{
    int32_t Type = 0;
    bool HasType = false;
    Error Result = ReadEnumName(context, compound, (const unsigned char*)u8"type", path, &COLLISION_SHAPE_TYPE_ENUM, &Type,
        &HasType);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HasType)
    {
        return Fail(context, "%s needs a \"type\": sphere, capsule, box, cylinder, convex_hull, plane or mesh.", path);
    }
    shape->Shape = CollisionShape_Create((CollisionShapeType)Type);
    shape->MaxVertices = COLLISION_HULL_DEFAULT_MAX_VERTICES;
    Result = ParseTypeValues(context, compound, path, shape);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadName(context, compound, (const unsigned char*)u8"name", path, false, false, &shape->Name);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadVector(context, compound, (const unsigned char*)u8"center", path, false, &shape->Shape.Center, NULL);
    }
    Vector3 Degrees = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadVector(context, compound, (const unsigned char*)u8"rotation", path, false, &Degrees, NULL);
    }
    shape->Shape.Rotation = CollisionMath_Scale(Degrees, DEG2RAD);
    return Result;
}

static Error ParseSet(ParseContext* context, JSONCompound* compound, size_t index)
{
    char Path[PATH_CAPACITY];
    snprintf(Path, sizeof(Path), "collision.sets[%" PRIu64 "]", (uint64_t)index);
    Error Result = RejectUnknownKeys(context, compound, SetKeys, KEY_COUNT(SetKeys), Path);
    SetDefinition* Set = &context->Definition->Sets[index];
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadName(context, compound, (const unsigned char*)u8"name", Path, false, true, &Set->Name);
    }
    for (size_t Earlier = 0U; (Earlier < index) && (Result.Code == ErrorCode_Success); Earlier++)
    {
        bool IsEqual = false;
        Error CompareResult = StringUTF8_EqualsExact(context->Definition->Sets[Earlier].Name, Set->Name, &IsEqual);
        Error_Deconstruct(&CompareResult);
        if (IsEqual)
        {
            Result = Fail(context, "%s.name \"%s\" is used by collision.sets[%" PRIu64 "] too; set names must be "
                "unique.", Path, (const char*)Set->Name, (uint64_t)Earlier);
        }
    }
    JSONObjectValue Shapes;
    bool HasShapes = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptionalVerified(compound, (const unsigned char*)u8"shapes", JSONValueType_Array, &Shapes,
            &HasShapes);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            Result = Fail(context, "%s.shapes must be an array.", Path);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Count = HasShapes ? JSONArray_GetElementCount(Shapes.Value.Array) : 0U;
    if ((Count == 0U) || (Count > COLLISION_MAX_SET_SHAPES))
    {
        return Fail(context, "%s.shapes must hold 1 to %" PRIu64 " shapes; it holds %" PRIu64 ".", Path,
            (uint64_t)COLLISION_MAX_SET_SHAPES, (uint64_t)Count);
    }
    context->ShapeTotal += Count;
    if (context->ShapeTotal > COLLISION_MAX_MODEL_SHAPES)
    {
        return Fail(context, "the collision sets hold more than %" PRIu64 " shapes together.",
            (uint64_t)COLLISION_MAX_MODEL_SHAPES);
    }
    Set->Shapes = Memory_Allocate(Count * sizeof(ShapeDefinition));
    Memory_Zero(Set->Shapes, Count * sizeof(ShapeDefinition));
    Set->ShapeCount = Count;
    for (size_t Shape = 0U; (Shape < Count) && (Result.Code == ErrorCode_Success); Shape++)
    {
        char ShapePath[SHAPE_PATH_CAPACITY];
        snprintf(ShapePath, sizeof(ShapePath), "%s.shapes[%" PRIu64 "]", Path, (uint64_t)Shape);
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Shapes.Value.Array, Shape, JSONValueType_Compound, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(context, "%s must be a JSON object.", ShapePath);
        }
        Result = ParseShape(context, Element.Value.Compound, ShapePath, &Set->Shapes[Shape]);
    }
    return Result;
}

/* The placements' defaults: default_set, layer, collides_with, is_trigger, mass. */
static Error ParseDefaults(ParseContext* context, JSONCompound* section)
{
    WorldModelCollisionDefinition* Definition = context->Definition;
    unsigned char* DefaultSet = NULL;
    Error Result = ReadName(context, section, (const unsigned char*)u8"default_set", "collision", false, false,
        &DefaultSet);
    if ((Result.Code == ErrorCode_Success) && (DefaultSet != NULL))
    {
        bool IsFound = false;
        for (size_t Index = 0U; (Index < Definition->SetCount) && !IsFound; Index++)
        {
            Error CompareResult = StringUTF8_EqualsExact(Definition->Sets[Index].Name, DefaultSet, &IsFound);
            Error_Deconstruct(&CompareResult);
            Definition->DefaultSet = IsFound ? Index : Definition->DefaultSet;
        }
        if (!IsFound)
        {
            Result = Fail(context, "collision.default_set \"%s\" names no set.", (const char*)DefaultSet);
        }
    }
    Memory_Free(DefaultSet);
    int32_t Layer = CollisionLayer_Prop;
    bool HasLayer = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadEnumName(context, section, (const unsigned char*)u8"layer", "collision", &COLLISION_LAYER_ENUM,
            &Layer, &HasLayer);
        Definition->Layer = (CollisionLayer)Layer;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBoolean(context, section, (const unsigned char*)u8"is_trigger", "collision", false,
            &Definition->IsTrigger);
    }
    if (Result.Code == ErrorCode_Success)
    {
        bool HasMass = false;
        Result = ReadFloat(context, section, (const unsigned char*)u8"mass", "collision", COLLISION_DEFAULT_MASS,
            &Definition->Mass, &HasMass);
        bool IsMassValid = (Definition->Mass > 0.0f) && (Definition->Mass <= COLLISION_MAX_MASS);
        if ((Result.Code == ErrorCode_Success) && !IsMassValid)
        {
            return Fail(context, "collision.mass is %g; it must be above 0 and at most %g (kilograms).",
                (double)Definition->Mass, (double)COLLISION_MAX_MASS);
        }
    }
    JSONObjectValue Mask;
    bool HasMask = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptionalVerified(section, (const unsigned char*)u8"collides_with", JSONValueType_Array,
            &Mask, &HasMask);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(context, "collision.collides_with must be an array of layer names.");
        }
    }
    if ((Result.Code != ErrorCode_Success) || !HasMask)
    {
        return Result;
    }
    Definition->HasCollidesWith = true;
    size_t Count = JSONArray_GetElementCount(Mask.Value.Array);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        JSONObjectValue Element;
        unsigned char* Name = NULL;
        Result = JSONArray_Get(Mask.Value.Array, Index, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            Result = AssetJSON_ValueToOwnedString(&Element, &Name);
        }
        CollisionLayer Named = CollisionLayer_Prop;
        bool IsLayer = (Result.Code == ErrorCode_Success) && CollisionLayer_TryParse(Name, &Named);
        if (!IsLayer)
        {
            char Printed[NAME_PRINT_CAPACITY];
            Error_Deconstruct(&Result);
            Result = Fail(context, "collision.collides_with[%" PRIu64 "] \"%s\" is not a layer.", (uint64_t)Index,
                Printable(Name, Printed, sizeof(Printed)));
            Memory_Free(Name);
            return Result;
        }
        Definition->CollidesWith |= COLLISION_LAYER_BIT(Named);
        Memory_Free(Name);
    }
    return Result;
}

static Error ParseSection(ParseContext* context, JSONCompound* section)
{
    Error Result = RejectUnknownKeys(context, section, SectionKeys, KEY_COUNT(SectionKeys), "collision");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    JSONObjectValue Sets;
    bool HasSets = false;
    Result = JSONCompound_GetOptionalVerified(section, (const unsigned char*)u8"sets", JSONValueType_Array, &Sets,
        &HasSets);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(context, "collision.sets must be an array.");
    }
    size_t Count = HasSets ? JSONArray_GetElementCount(Sets.Value.Array) : 0U;
    if ((Count == 0U) || (Count > COLLISION_MAX_SETS))
    {
        return Fail(context, "collision.sets must hold 1 to %" PRIu64 " sets (each { \"name\": ..., \"shapes\": [...] "
            "}); it holds %" PRIu64 ".", (uint64_t)COLLISION_MAX_SETS, (uint64_t)Count);
    }
    WorldModelCollisionDefinition* Definition = context->Definition;
    Definition->Sets = Memory_Allocate(Count * sizeof(SetDefinition));
    Memory_Zero(Definition->Sets, Count * sizeof(SetDefinition));
    Definition->SetCount = Count;
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Sets.Value.Array, Index, JSONValueType_Compound, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(context, "collision.sets[%" PRIu64 "] must be a JSON object.", (uint64_t)Index);
        }
        Result = ParseSet(context, Element.Value.Compound, Index);
    }
    return (Result.Code == ErrorCode_Success) ? ParseDefaults(context, section) : Result;
}


// Static functions: loading.
/* The model a hull or a mesh is built from, loaded as a dependency. */
static Error LoadSourceModel(AssetManager* manager, AssetUserID dependencyUser, const unsigned char* name,
    GameModel** outModel)
{
    AssetTypeID ModelType = ASSET_TYPE_ID_INVALID;
    Error Result = AssetManager_GetAssetTypeByName(manager, ASSET_TYPE_NAME_MODEL, &ModelType);
    if ((Result.Code == ErrorCode_Success) && (ModelType == ASSET_TYPE_ID_INVALID))
    {
        return Error_Construct3(ErrorCode_InvalidState, u8"The model asset type is not registered.");
    }
    void* Asset = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetManager_LoadAssetSingle(manager, ModelType, name, dependencyUser, &Asset);
    }
    *outModel = Asset;
    return Result;
}

/* Builds one shape's hull or mesh (other types need nothing); the block keeps them in @p outHull / @p outMesh. */
static Error BuildShape(const ShapeDefinition* source, AssetManager* manager, AssetUserID dependencyUser,
    CollisionShape* target, CollisionHull** outHull, CollisionMesh** outMesh)
{
    if ((target->Type != CollisionShapeType_ConvexHull) && (target->Type != CollisionShapeType_Mesh))
    {
        return Error_CreateSuccess();
    }
    if (source->Points != NULL)
    {
        Error Result = CollisionHull_Create(source->Points, source->PointCount, source->MaxVertices, outHull);
        target->Hull = *outHull;
        return Result;
    }
    GameModel* Source = NULL;
    Error Result = LoadSourceModel(manager, dependencyUser, source->Model, &Source);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (target->Type == CollisionShapeType_ConvexHull)
    {
        Result = CollisionHull_CreateFromModel(Source, source->MaxVertices, outHull);
        target->Hull = *outHull;
        return Result;
    }
    Result = CollisionMesh_CreateFromModel(Source, outMesh);
    target->Mesh = *outMesh;
    return Result;
}

/* The "bounds" fallback's box: LOD 0's mesh-space box under its import transform; a hull of its corners when the
 * transform shears it (a box cannot hold that). */
static Error BuildBoundsShape(GameModel* lod0, CollisionShape* outShape, CollisionHull** outHull)
{
    BoundingBox Bounds = GameModel_GetLocalBounds(lod0);
    Matrix Import = GameModel_GetRaylibModel(lod0).transform;
    Vector3 Center = CollisionMath_Scale(CollisionMath_Add(Bounds.min, Bounds.max), 0.5f);
    Vector3 Half = CollisionMath_Scale(CollisionMath_Subtract(Bounds.max, Bounds.min), 0.5f);
    Matrix4Double ImportDouble = GameMath_FromMatrix(Import);
    Vector3Double Position;
    Vector3 Rotation;
    Vector3 Scale;
    WorldTransform_Decompose(&ImportDouble, CollisionMath_Vector(1.0f, 1.0f, 1.0f), &Position, &Rotation, &Scale);
    Matrix4Double Recomposed = WorldTransform_Compose(Position, Rotation, Scale);
    bool IsExact = true;
    for (size_t Index = 0U; Index < MATRIX4_DOUBLE_ENTRY_COUNT; Index++)
    {
        IsExact = IsExact && (fabs(ImportDouble.M[Index] - Recomposed.M[Index])
            <= ((double)BOUNDS_DECOMPOSITION_TOLERANCE * (1.0 + fabs(ImportDouble.M[Index]))));
    }
    float Smallest = 1e-6f;
    if (IsExact)
    {
        *outShape = CollisionShape_Create(CollisionShapeType_Box);
        outShape->Center = Vector3Transform(Center, Import);
        outShape->Rotation = Rotation;
        outShape->HalfExtents = CollisionMath_Vector(fmaxf(Smallest, Half.x * fabsf(Scale.x)),
            fmaxf(Smallest, Half.y * fabsf(Scale.y)), fmaxf(Smallest, Half.z * fabsf(Scale.z)));
        return Error_CreateSuccess();
    }
    Vector3 Corners[8];
    for (uint32_t Corner = 0U; Corner < 8U; Corner++)
    {
        Vector3 Local = CollisionMath_Vector(((Corner & 1U) != 0U) ? Bounds.max.x : Bounds.min.x,
            ((Corner & 2U) != 0U) ? Bounds.max.y : Bounds.min.y, ((Corner & 4U) != 0U) ? Bounds.max.z : Bounds.min.z);
        Corners[Corner] = Vector3Transform(Local, Import);
    }
    *outShape = CollisionShape_Create(CollisionShapeType_ConvexHull);
    Error Result = CollisionHull_Create(Corners, 8U, COLLISION_HULL_DEFAULT_MAX_VERTICES, outHull);
    outShape->Hull = *outHull;
    return Result;
}

/* The loaded block: the struct, then the sets, the shapes, the owned hulls and meshes (one slot per shape) and the
 * names. */
typedef struct CollisionBlockStruct
{
    WorldModelCollision Collision;
    uint32_t ShapeCount;
    CollisionHull** Hulls;
    CollisionMesh** Meshes;
} CollisionBlock;


// Public functions.
Error WorldModelCollisionDefinition_Parse(const char* printableName, JSONCompound* root,
    WorldModelCollisionDefinition** outDefinition)
{
    *outDefinition = NULL;
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptional(root, (const unsigned char*)u8"collision", &Value, &Found);
    if ((Result.Code != ErrorCode_Success) || !Found)
    {
        return Result;
    }
    WorldModelCollisionDefinition* Definition = Memory_Allocate(sizeof(WorldModelCollisionDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Layer = CollisionLayer_Prop;
    Definition->Mass = COLLISION_DEFAULT_MASS;
    ParseContext Context = { .Name = printableName, .Definition = Definition, .ShapeTotal = 0U };
    if (Value.Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Result = AssetJSON_ValueToOwnedString(&Value, &Text);
        bool IsBounds = false;
        if (Result.Code == ErrorCode_Success)
        {
            Error CompareResult = StringUTF8_EqualsExact(Text, BOUNDS_SET_NAME, &IsBounds);
            Error_Deconstruct(&CompareResult);
        }
        Error_Deconstruct(&Result);
        Memory_Free(Text);
        Definition->IsBoundsFallback = IsBounds;
        Result = IsBounds ? Error_CreateSuccess()
            : Fail(&Context, "collision must be \"bounds\" (a box around LOD 0) or a JSON object with \"sets\".");
    }
    else if (Value.Type == JSONValueType_Compound)
    {
        Result = ParseSection(&Context, Value.Value.Compound);
    }
    else
    {
        Result = Fail(&Context, "collision must be \"bounds\" (a box around LOD 0) or a JSON object with \"sets\".");
    }
    if (Result.Code != ErrorCode_Success)
    {
        WorldModelCollisionDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = Definition;
    return Result;
}

void WorldModelCollisionDefinition_Destroy(WorldModelCollisionDefinition* self)
{
    if (self == NULL)
    {
        return;
    }
    for (size_t Set = 0U; Set < self->SetCount; Set++)
    {
        SetDefinition* Definition = &self->Sets[Set];
        for (size_t Shape = 0U; Shape < Definition->ShapeCount; Shape++)
        {
            Memory_Free(Definition->Shapes[Shape].Name);
            Memory_Free(Definition->Shapes[Shape].Model);
            Memory_Free(Definition->Shapes[Shape].Points);
        }
        Memory_Free(Definition->Shapes);
        Memory_Free(Definition->Name);
    }
    Memory_Free(self->Sets);
    Memory_Free(self);
}

Error WorldModelCollisionDefinition_Load(const WorldModelCollisionDefinition* self, const unsigned char* worldModelName,
    AssetManager* manager, AssetUserID dependencyUser, GameModel* lod0, void** outBlock,
    const WorldModelCollision** outCollision)
{
    *outBlock = NULL;
    *outCollision = NULL;
    size_t SetCount = self->IsBoundsFallback ? 1U : self->SetCount;
    size_t ShapeCount = self->IsBoundsFallback ? 1U : 0U;
    size_t NameBytes = self->IsBoundsFallback ? (StringUTF8_GetByteLength(BOUNDS_SET_NAME) + 1U) : 0U;
    for (size_t Set = 0U; Set < self->SetCount; Set++)
    {
        ShapeCount += self->Sets[Set].ShapeCount;
        NameBytes += StringUTF8_GetByteLength(self->Sets[Set].Name) + 1U;
        for (size_t Shape = 0U; Shape < self->Sets[Set].ShapeCount; Shape++)
        {
            const unsigned char* Name = self->Sets[Set].Shapes[Shape].Name;
            NameBytes += (Name != NULL) ? (StringUTF8_GetByteLength(Name) + 1U) : 0U;
        }
    }
    size_t SetsOffset = sizeof(CollisionBlock);
    size_t ShapesOffset = SetsOffset + (SetCount * sizeof(CollisionSet));
    size_t HullsOffset = ShapesOffset + (ShapeCount * sizeof(CollisionShape));
    size_t MeshesOffset = HullsOffset + (ShapeCount * sizeof(CollisionHull*));
    size_t NamesOffset = MeshesOffset + (ShapeCount * sizeof(CollisionMesh*));
    unsigned char* Bytes = Memory_Allocate(NamesOffset + NameBytes);
    Memory_Zero(Bytes, NamesOffset + NameBytes);
    CollisionBlock* Block = (CollisionBlock*)(void*)Bytes;
    CollisionSet* Sets = (CollisionSet*)(void*)(Bytes + SetsOffset);
    CollisionShape* Shapes = (CollisionShape*)(void*)(Bytes + ShapesOffset);
    Block->Hulls = (CollisionHull**)(void*)(Bytes + HullsOffset);
    Block->Meshes = (CollisionMesh**)(void*)(Bytes + MeshesOffset);
    unsigned char* Names = Bytes + NamesOffset;
    Block->ShapeCount = (uint32_t)ShapeCount;

    Error Result = Error_CreateSuccess();
    if (self->IsBoundsFallback)
    {
        size_t Length = StringUTF8_GetByteLength(BOUNDS_SET_NAME) + 1U;
        Memory_Copy(BOUNDS_SET_NAME, Names, Length);
        Sets[0] = (CollisionSet){ ._name = Names, ._shapes = Shapes, ._shapeCount = 1U };
        Result = BuildBoundsShape(lod0, &Shapes[0], &Block->Hulls[0]);
    }
    size_t Next = 0U;
    for (size_t Set = 0U; (Set < self->SetCount) && (Result.Code == ErrorCode_Success); Set++)
    {
        const SetDefinition* Definition = &self->Sets[Set];
        size_t Length = StringUTF8_GetByteLength(Definition->Name) + 1U;
        Memory_Copy(Definition->Name, Names, Length);
        Sets[Set] = (CollisionSet){ ._name = Names, ._shapes = &Shapes[Next],
            ._shapeCount = (uint32_t)Definition->ShapeCount };
        Names += Length;
        for (size_t Shape = 0U; (Shape < Definition->ShapeCount) && (Result.Code == ErrorCode_Success); Shape++)
        {
            const ShapeDefinition* Source = &Definition->Shapes[Shape];
            size_t Slot = Next++;
            CollisionShape* Target = &Shapes[Slot];
            *Target = Source->Shape;
            if (Source->Name != NULL)
            {
                size_t NameLength = StringUTF8_GetByteLength(Source->Name) + 1U;
                Memory_Copy(Source->Name, Names, NameLength);
                Target->Name = Names;
                Names += NameLength;
            }
            Result = BuildShape(Source, manager, dependencyUser, Target, &Block->Hulls[Slot], &Block->Meshes[Slot]);
            if (Result.Code != ErrorCode_Success)
            {
                Error Wrapped = Error_Construct3(Result.Code, u8"World model \"%s\": collision set \"%s\", shape %u: %s",
                    worldModelName, Definition->Name, (unsigned)Shape,
                    (Result.Message != NULL) ? Result.Message : (const unsigned char*)u8"it could not be built.");
                Error_Deconstruct(&Result);
                Result = Wrapped;
            }
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        WorldModelCollision_DestroyBlock(Block);
        return Result;
    }
    Block->Collision = (WorldModelCollision)
    {
        ._sets = Sets, ._setCount = (uint32_t)SetCount, ._defaultSet = (uint32_t)self->DefaultSet,
        ._layer = self->Layer, ._hasCollidesWith = self->HasCollidesWith, ._collidesWith = self->CollidesWith,
        ._isTrigger = self->IsTrigger, ._mass = self->Mass, ._isBoundsFallback = self->IsBoundsFallback
    };
    *outBlock = Block;
    *outCollision = &Block->Collision;
    return Result;
}

void WorldModelCollision_DestroyBlock(void* block)
{
    if (block == NULL)
    {
        return;
    }
    CollisionBlock* Block = block;
    for (uint32_t Shape = 0U; Shape < Block->ShapeCount; Shape++)
    {
        CollisionHull_Destroy(Block->Hulls[Shape]);
        CollisionMesh_Destroy(Block->Meshes[Shape]);
    }
    Memory_Free(Block);
}
