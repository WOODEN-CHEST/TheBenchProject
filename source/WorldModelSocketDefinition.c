#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldModelSocketInternal.h"
#include "AssetDefinitionCommon.h"
#include "AssetManager.h"
#include "GameJSON.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * A world model definition's "sockets" and "first_person" sections (T30; source/WorldModelSocketInternal.h):
 *   "sockets": { "hand_r": { "bone": "hand_r", "position": [x, y, z], "rotation": [x, y, z] }, "seat": { ... } }
 *   "first_person": { "eyes": "<socket>", "hidden_bones": ["head", ...] }
 * A socket with a bone follows it (its position and rotation in the bone's space); one without is fixed in the object's
 * own space. Rotations are degrees about X, Y and Z, applied in that order, as an object's.
 */


// Macros.
/** Room for one message's text, a "where" for unknown keys and a name from the file. */
#define MESSAGE_CAPACITY ((size_t)384)
#define WHERE_CAPACITY ((size_t)256)
#define NAME_PRINT_CAPACITY ((size_t)129)
/** The number of keys in a list of known keys. */
#define KEY_COUNT(keys) (sizeof(keys) / sizeof((keys)[0]))
/** The most bytes of a bone's name a skeleton keeps. */
#define BONE_NAME_MAX_LENGTH (GAME_SKELETON_BONE_NAME_CAPACITY - 1U)


// Types.
typedef struct SocketEntryStruct
{
    unsigned char* Name; // owned
    unsigned char* Bone; // owned, NULL for a fixed socket
    Vector3 Position;
    Vector3 Rotation;    // radians
} SocketEntry;

struct WorldModelSocketDefinitionStruct
{
    SocketEntry Sockets[WORLD_MODEL_MAX_SOCKETS];
    size_t SocketCount;
    bool HasFirstPerson;
    uint32_t EyesSocket;
    unsigned char* HiddenBones[WORLD_MODEL_MAX_HIDDEN_BONES]; // owned
    size_t HiddenBoneCount;
};

/* The definition being read and the world model's name in printable form, for messages. */
typedef struct SocketParseStruct
{
    WorldModelSocketDefinition* Definition;
    const char* Name;
} SocketParse;


// Fields.
static const unsigned char* const SocketKeys[] = { u8"bone", u8"position", u8"rotation" };
static const unsigned char* const FirstPersonKeys[] = { u8"eyes", u8"hidden_bones" };


// Static functions.
/* snprintf for paths in messages: a path cut short only shortens a message. */
static void FormatPath(char* buffer, size_t capacity, const char* format, ...)
{
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(buffer, capacity, format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        buffer[0] = '\0';
    }
}

static Error Fail(const char* name, ErrorCode code, const char* format, ...)
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
    return Error_Construct4(code, "World model \"%s\": %s", name, Text);
}

static Error WrapInner(const char* name, const char* where, Error* inner)
{
    Error Wrapped = Error_Construct4(inner->Code, "World model \"%s\": %s: %s", name, where,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}

static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == StringUTF8_GetByteLength(b)) && Memory_IsEqual(a, b, Length);
}

/* A bone's name from a value: UTF-8 of 1 to BONE_NAME_MAX_LENGTH bytes (an owned copy). */
static Error ReadBoneName(const char* name, const JSONObjectValue* value, const char* where, unsigned char** outBone)
{
    *outBone = NULL;
    Error Result = (value->Type == JSONValueType_String) ? GameJSON_ValueToOwnedString(value, outBone)
        : Error_Construct2(ErrorCode_InvalidAssetDefinition, "not a string");
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s must be a bone's name (a string).", where);
    }
    size_t Length = StringUTF8_GetByteLength(*outBone);
    if ((Length == 0U) || (Length > BONE_NAME_MAX_LENGTH) || !StringUTF8_IsEncodingValid(*outBone))
    {
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s must be a bone's name: UTF-8 of 1 to %" PRIu64 " bytes "
            "(skeletons keep that many).", where, (uint64_t)BONE_NAME_MAX_LENGTH);
    }
    return Error_CreateSuccess();
}

/* An optional vector key ([x, y, z] or { "x", "y", "z" }). */
static Error ReadVector(const char* name, JSONCompound* compound, const char* key, const char* where, Vector3* outVector)
{
    JSONObjectValue Value;
    bool Found = false;
    *outVector = (Vector3){ 0.0f, 0.0f, 0.0f };
    Error Result = JSONCompound_GetOptional(compound, (const unsigned char*)key, &Value, &Found);
    if ((Result.Code == ErrorCode_Success) && Found)
    {
        Result = GameJSON_ParseVector3(&Value, outVector);
    }
    if (Result.Code != ErrorCode_Success)
    {
        char Path[WHERE_CAPACITY];
        FormatPath(Path, sizeof(Path), "%s.%s", where, key);
        return WrapInner(name, Path, &Result);
    }
    return Error_CreateSuccess();
}

/* One socket: "name": { "bone", "position", "rotation" }. */
static Error ParseSocket(SocketParse* parse, const unsigned char* key, const JSONObjectValue* value)
{
    char Where[WHERE_CAPACITY];
    char Shown[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(key, Shown, sizeof(Shown));
    FormatPath(Where, sizeof(Where), "sockets.%s", Shown);
    WorldModelSocketDefinition* Definition = parse->Definition;
    if (!AssetManager_IsValidAssetName(key) || (StringUTF8_GetByteLength(key) > WORLD_MODEL_SOCKET_NAME_MAX_LENGTH))
    {
        return Fail(parse->Name, ErrorCode_InvalidAssetDefinition, "sockets: \"%s\" is not a valid socket name "
            "([a-z][a-z0-9_.-]*, at most %" PRIu64 " bytes).", Shown, (uint64_t)WORLD_MODEL_SOCKET_NAME_MAX_LENGTH);
    }
    if (Definition->SocketCount >= WORLD_MODEL_MAX_SOCKETS)
    {
        return Fail(parse->Name, ErrorCode_InvalidAssetDefinition, "\"sockets\" holds more than %" PRIu64 " sockets.",
            (uint64_t)WORLD_MODEL_MAX_SOCKETS);
    }
    if (value->Type != JSONValueType_Compound)
    {
        return Fail(parse->Name, ErrorCode_InvalidAssetDefinition, "%s must be a JSON object.", Where);
    }
    JSONCompound* Compound = value->Value.Compound;
    char KeysWhere[WHERE_CAPACITY];
    FormatPath(KeysWhere, sizeof(KeysWhere), "world model \"%s\", %s", parse->Name, Where);
    Error Result = AssetJSON_RejectUnknownKeys(Compound, SocketKeys, KEY_COUNT(SocketKeys), KeysWhere);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // Counted before it is read, so the definition's teardown frees what a failed read kept.
    SocketEntry* Entry = &Definition->Sockets[Definition->SocketCount];
    Definition->SocketCount++;
    size_t NameBytes = StringUTF8_GetByteLength(key) + 1U;
    Entry->Name = Memory_Allocate(NameBytes);
    Memory_Copy(key, Entry->Name, NameBytes);
    JSONObjectValue Bone;
    bool HasBone = false;
    Result = JSONCompound_GetOptional(Compound, (const unsigned char*)u8"bone", &Bone, &HasBone);
    if ((Result.Code == ErrorCode_Success) && HasBone)
    {
        char BoneWhere[WHERE_CAPACITY];
        FormatPath(BoneWhere, sizeof(BoneWhere), "%s.bone", Where);
        Result = ReadBoneName(parse->Name, &Bone, BoneWhere, &Entry->Bone);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadVector(parse->Name, Compound, "position", Where, &Entry->Position);
    }
    Vector3 Degrees = { 0.0f, 0.0f, 0.0f };
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadVector(parse->Name, Compound, "rotation", Where, &Degrees);
    }
    Entry->Rotation = Vector3Scale(Degrees, DEG2RAD);
    return Result;
}

static Error ParseSockets(SocketParse* parse, JSONCompound* root)
{
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptionalVerified(root, (const unsigned char*)u8"sockets", JSONValueType_Compound, &Value,
        &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(parse->Name, ErrorCode_InvalidAssetDefinition, "\"sockets\" must be a JSON object of sockets.");
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }
    ICollection* Entries = JSONCompound_GetEntryCollection(Value.Value.Compound);
    CollectionEnumerator* Enumerator = (Entries != NULL) ? ICollection_CreateEnumerator(Entries) : NULL;
    if (Enumerator == NULL)
    {
        return Fail(parse->Name, ErrorCode_BufferTooSmall, "could not enumerate \"sockets\".");
    }
    bool HasNext = false;
    Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        JSONCompoundEntry Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ParseSocket(parse, Entry.Key, &Entry.Value);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        }
    }
    CollectionEnumerator_Destroy(Enumerator);
    return Result;
}

/* "first_person": { "eyes": a socket of this world model, "hidden_bones": [bone names] }. */
static Error ParseFirstPerson(SocketParse* parse, JSONCompound* root)
{
    JSONObjectValue Value;
    bool Found = false;
    WorldModelSocketDefinition* Definition = parse->Definition;
    Error Result = JSONCompound_GetOptionalVerified(root, (const unsigned char*)u8"first_person", JSONValueType_Compound,
        &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(parse->Name, ErrorCode_InvalidAssetDefinition, "\"first_person\" must be a JSON object.");
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }
    JSONCompound* Compound = Value.Value.Compound;
    char KeysWhere[WHERE_CAPACITY];
    FormatPath(KeysWhere, sizeof(KeysWhere), "world model \"%s\", first_person", parse->Name);
    Result = AssetJSON_RejectUnknownKeys(Compound, FirstPersonKeys, KEY_COUNT(FirstPersonKeys), KeysWhere);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    unsigned char* Eyes = NULL;
    bool HasEyes = false;
    Result = AssetJSON_ReadOptionalOwnedString(Compound, (const unsigned char*)u8"eyes", &Eyes, &HasEyes);
    if ((Result.Code != ErrorCode_Success) || !HasEyes)
    {
        Error_Deconstruct(&Result);
        Memory_Free(Eyes);
        return Fail(parse->Name, ErrorCode_InvalidAssetDefinition, "first_person needs \"eyes\": the socket a "
            "first-person camera rides.");
    }
    bool IsFound = false;
    for (size_t Index = 0U; (Index < Definition->SocketCount) && !IsFound; Index++)
    {
        if (AreNamesEqual(Definition->Sockets[Index].Name, Eyes))
        {
            Definition->EyesSocket = (uint32_t)Index;
            IsFound = true;
        }
    }
    char Shown[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Eyes, Shown, sizeof(Shown));
    Memory_Free(Eyes);
    if (!IsFound)
    {
        return Fail(parse->Name, ErrorCode_InvalidAssetDefinition, "first_person.eyes names \"%s\", which is not one of "
            "its sockets.", Shown);
    }
    JSONObjectValue Hidden;
    bool HasHidden = false;
    Result = JSONCompound_GetOptionalVerified(Compound, (const unsigned char*)u8"hidden_bones", JSONValueType_Array,
        &Hidden, &HasHidden);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(parse->Name, ErrorCode_InvalidAssetDefinition, "first_person.hidden_bones must be an array of bone "
            "names.");
    }
    size_t Count = HasHidden ? JSONArray_GetElementCount(Hidden.Value.Array) : 0U;
    if (Count > WORLD_MODEL_MAX_HIDDEN_BONES)
    {
        return Fail(parse->Name, ErrorCode_InvalidAssetDefinition, "first_person.hidden_bones names %" PRIu64 " bones; at "
            "most %" PRIu64 ".", (uint64_t)Count, (uint64_t)WORLD_MODEL_MAX_HIDDEN_BONES);
    }
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        char Where[WHERE_CAPACITY];
        FormatPath(Where, sizeof(Where), "first_person.hidden_bones[%" PRIu64 "]", (uint64_t)Index);
        JSONObjectValue Element;
        Result = JSONArray_Get(Hidden.Value.Array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return WrapInner(parse->Name, Where, &Result);
        }
        Result = ReadBoneName(parse->Name, &Element, Where, &Definition->HiddenBones[Index]);
        Definition->HiddenBoneCount = Index + 1U;
    }
    Definition->HasFirstPerson = Result.Code == ErrorCode_Success;
    return Result;
}


// Functions (declared in WorldModelSocketInternal.h).
Error WorldModelSocketDefinition_Parse(const char* printableName, JSONCompound* root,
    WorldModelSocketDefinition** outDefinition)
{
    *outDefinition = NULL;
    JSONObjectValue Value;
    bool HasSockets = false;
    bool HasFirstPerson = false;
    Error Result = JSONCompound_GetOptional(root, (const unsigned char*)u8"sockets", &Value, &HasSockets);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(root, (const unsigned char*)u8"first_person", &Value, &HasFirstPerson);
    }
    if ((Result.Code != ErrorCode_Success) || (!HasSockets && !HasFirstPerson))
    {
        return Result;
    }
    WorldModelSocketDefinition* Definition = Memory_Allocate(sizeof(WorldModelSocketDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    SocketParse Parse = { .Definition = Definition, .Name = printableName };
    Result = ParseSockets(&Parse, root);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseFirstPerson(&Parse, root);
    }
    if (Result.Code != ErrorCode_Success)
    {
        WorldModelSocketDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = Definition;
    return Error_CreateSuccess();
}

void WorldModelSocketDefinition_Destroy(WorldModelSocketDefinition* self)
{
    if (self == NULL)
    {
        return;
    }
    for (size_t Index = 0U; Index < self->SocketCount; Index++)
    {
        Memory_Free(self->Sockets[Index].Name);
        Memory_Free(self->Sockets[Index].Bone);
    }
    for (size_t Index = 0U; Index < self->HiddenBoneCount; Index++)
    {
        Memory_Free(self->HiddenBones[Index]);
    }
    Memory_Free(self);
}

Error WorldModelSocketDefinition_Load(const WorldModelSocketDefinition* self, const unsigned char* worldModelName,
    WorldModel* model, void** outBlock)
{
    *outBlock = NULL;
    model->_sockets = NULL;
    model->_socketCount = 0U;
    model->_hasFirstPerson = false;
    model->_eyesSocket = 0U;
    model->_hiddenBones = NULL;
    model->_hiddenBoneCount = 0U;
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }
    const char* Name = (const char*)worldModelName;
    const GameSkeleton* Skeleton = model->_skeleton;
    // The sockets, then the hidden bones, then the names; at most 32 sockets of 64-byte names: no overflow.
    size_t SocketBytes = self->SocketCount * sizeof(WorldModelSocket);
    size_t HiddenBytes = self->HiddenBoneCount * sizeof(uint32_t);
    size_t NameBytes = 0U;
    for (size_t Index = 0U; Index < self->SocketCount; Index++)
    {
        NameBytes += StringUTF8_GetByteLength(self->Sockets[Index].Name) + 1U;
    }
    unsigned char* Block = Memory_Allocate(SocketBytes + HiddenBytes + NameBytes + 1U);
    WorldModelSocket* Sockets = (WorldModelSocket*)(void*)Block;
    uint32_t* Hidden = (uint32_t*)(void*)(Block + SocketBytes);
    unsigned char* Names = Block + SocketBytes + HiddenBytes;
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < self->SocketCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        const SocketEntry* Entry = &self->Sockets[Index];
        WorldModelSocket* Socket = &Sockets[Index];
        size_t Length = StringUTF8_GetByteLength(Entry->Name) + 1U;
        Memory_Copy(Entry->Name, Names, Length);
        Socket->_name = Names;
        Names += Length;
        Socket->_offset = MatrixMultiply(MatrixRotateXYZ(Entry->Rotation),
            MatrixTranslate(Entry->Position.x, Entry->Position.y, Entry->Position.z));
        Socket->_hasBone = Entry->Bone != NULL;
        Socket->_bone = 0U;
        if (!Socket->_hasBone)
        {
            continue;
        }
        char Bone[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(Entry->Bone, Bone, sizeof(Bone));
        if (Skeleton == NULL)
        {
            Result = Fail(Name, ErrorCode_InvalidAssetData, "socket \"%s\" sits on bone \"%s\", but none of its LOD models "
                "has a skeleton.", (const char*)Entry->Name, Bone);
        }
        else if (!GameSkeleton_TryFindBone(Skeleton, Entry->Bone, &Socket->_bone))
        {
            Result = Fail(Name, ErrorCode_InvalidAssetData, "socket \"%s\" sits on bone \"%s\", which its skeleton "
                "lacks.", (const char*)Entry->Name, Bone);
        }
    }
    if ((Result.Code == ErrorCode_Success) && self->HasFirstPerson && (Skeleton == NULL))
    {
        Result = Fail(Name, ErrorCode_InvalidAssetData, "first_person hides bones, but none of its LOD models has a "
            "skeleton.");
    }
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < self->HiddenBoneCount); Index++)
    {
        if (!GameSkeleton_TryFindBone(Skeleton, self->HiddenBones[Index], &Hidden[Index]))
        {
            char Bone[NAME_PRINT_CAPACITY];
            AssetJSON_CopyPrintable(self->HiddenBones[Index], Bone, sizeof(Bone));
            Result = Fail(Name, ErrorCode_InvalidAssetData, "first_person.hidden_bones names \"%s\", which its skeleton "
                "lacks.", Bone);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Block);
        return Result;
    }
    model->_sockets = Sockets;
    model->_socketCount = (uint32_t)self->SocketCount;
    model->_hasFirstPerson = self->HasFirstPerson;
    model->_eyesSocket = self->EyesSocket;
    model->_hiddenBones = (self->HiddenBoneCount > 0U) ? Hidden : NULL;
    model->_hiddenBoneCount = (uint32_t)self->HiddenBoneCount;
    *outBlock = Block;
    return Error_CreateSuccess();
}
