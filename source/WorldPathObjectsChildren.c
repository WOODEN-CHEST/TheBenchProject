// The path objects system's runtime children (WorldPathObjectsInternal.h): what a rebuild came to, written on them only where it
// differs.

#include <stdio.h>
#include "WorldPathObjectsInternal.h"
#include "CollisionShape.h"
#include "DataValue.h"
#include "GameModel.h"
#include "WorldInternal.h"
#include "WorldModelComponent.h"
#include "WorldModelInstancesComponent.h"
#include "WorldPrefabComponent.h"
#include "WorldPrefabs.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** Room for a prefab piece's name. */
#define PREFAB_PIECE_NAME_CAPACITY ((size_t)64)
/** How many salts a prefab piece tries for its id. */
#define PREFAB_PIECE_MAX_SALTS 64U
/** A multiplier that spreads a copy's pick over its enum properties (the golden ratio's, 32 bits). */
#define PREFAB_PICK_SPREAD 2654435761U
/** MurmurHash3's 32-bit finalizer. */
#define PREFAB_PICK_MIX_FIRST 0x85EBCA6BU
#define PREFAB_PICK_MIX_SECOND 0xC2B2AE35U
#define PREFAB_PICK_MIX_SHIFT_FIRST 16U
#define PREFAB_PICK_MIX_SHIFT_SECOND 13U


// Static functions: finding and making.
/* A prefab's piece: a derived id from the path object's own, its slot, its model and its index, so what a script holds of a lamp in a
 * row survives a load, as long as the row keeps its pieces. */
static Error CreatePrefabPiece(World* world, uint64_t ownerId, PathObjectSlot slot, uint32_t model, uint32_t index, WorldObject** outChild)
{
    char Name[PREFAB_PIECE_NAME_CAPACITY];
    snprintf(Name, sizeof(Name), "path_object_piece_%u_%u_%u", (unsigned int)slot, (unsigned int)model, (unsigned int)index);
    Error Result = Error_Construct2(ErrorCode_InvalidOperation, "no id is free.");
    for (uint32_t Salt = 0U; (Salt < PREFAB_PIECE_MAX_SALTS) && (Result.Code != ErrorCode_Success); Salt++)
    {
        Error_Deconstruct(&Result);
        Result = World_CreateDerivedObject(world, (const unsigned char*)Name,
            WorldPrefabs_DeriveObjectId(ownerId, (const unsigned char*)Name, Salt), outChild);
    }
    return Result;
}

/* The child for a slot, a model and a part, made when it does not exist. NULL only when the world refuses it. */
static WorldObject* FindOrMakeChild(PathObjectRebuild* rebuild, PathObjectChildKind kind, PathObjectSlot slot, uint32_t model,
    uint32_t part)
{
    World* TheWorld = rebuild->System->_world;
    PathObjectState* State = rebuild->State;
    PathObjectChildRecord* Records = (PathObjectChildRecord*)State->Children._data;
    size_t Count = State->Children._count;
    // From the last one found: a rebuild writes them in the order they were made, so a run of a thousand lamps costs a thousand looks.
    for (size_t Step = 0U; Step < Count; Step++)
    {
        size_t Index = rebuild->ChildCursor + Step;
        Index = (Index < Count) ? Index : (Index - Count);
        PathObjectChildRecord* Record = &Records[Index];
        if ((Record->Kind == kind) && (Record->Slot == slot) && (Record->Model == model) && (Record->Part == part))
        {
            WorldObject* Found = NULL;
            if (World_TryGetObject(TheWorld, Record->ObjectId, &Found))
            {
                Record->IsUsed = true;
                rebuild->ChildCursor = ((Index + 1U) < Count) ? (Index + 1U) : 0U;
                return Found;
            }
            GenericBuffer_RemoveAt(&State->Children, Index);
            rebuild->System->_stats.ChildCount--;
            rebuild->ChildCursor = 0U;
            break; // removed from outside: made again below
        }
    }
    if (rebuild->System->_stats.ChildCount >= rebuild->System->_limits.MaxChildren)
    {
        if (!State->HasWarnedChildren)
        {
            State->HasWarnedChildren = true;
            WorldPathObjectsInternal_Warn(rebuild->System, rebuild->Owner,
                "makes more children than the world's path objects may hold: the rest is left out.");
        }
        return NULL;
    }
    WorldObject* Child = NULL;
    Error Result = (kind == PathObjectChildKind_PrefabCopy) ? CreatePrefabPiece(TheWorld, State->ObjectId, slot, model, part, &Child)
        : World_CreateObject(TheWorld, (kind == PathObjectChildKind_Part) ? (const unsigned char*)u8"path_object_part"
            : (const unsigned char*)u8"path_object_copies", &Child);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return NULL;
    }
    uint64_t Id = WorldObject_GetId(Child);
    WorldObject_SetPersistent(Child, false);
    Result = World_SetParent(TheWorld, Id, State->ObjectId, false);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        Result = World_RemoveObject(TheWorld, Id);
        Error_Deconstruct(&Result);
        return NULL;
    }
    PathObjectChildRecord Fresh = { .ObjectId = Id, .Kind = kind, .Slot = slot, .Model = model, .Part = part, .IsUsed = true };
    GenericBuffer_AddLast(&State->Children, &Fresh);
    rebuild->System->_stats.ChildCount++;
    rebuild->HasAnyChange = true;
    return Child;
}

/* A component of a type on a child, added when it has none. */
static WorldComponent* FindOrAddComponent(World* world, WorldObject* child, WorldComponentType type)
{
    WorldComponent* Component = WorldObject_GetComponent(child, type);
    if (Component != NULL)
    {
        return Component;
    }
    Error Result = World_AddComponent(world, WorldObject_GetId(child), type, &Component);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return NULL;
    }
    return Component;
}

/* Removes a component of a type from a child, if it has one. */
static void RemoveComponent(World* world, WorldObject* child, WorldComponentType type)
{
    if (WorldObject_GetComponent(child, type) != NULL)
    {
        Error Result = World_RemoveComponent(world, WorldObject_GetId(child), type);
        Error_Deconstruct(&Result);
    }
}


// Static functions: rows.
/* Whether two values of a row a slot shares with a component are equal: only these types are shared; any other counts as
 * different, which only costs a write. */
static bool AreValuesEqual(const PropertyValue* a, const PropertyValue* b)
{
    if (a->Type != b->Type)
    {
        return false;
    }
    if (a->Type == PropertyType_Bool)
    {
        return a->Data.BoolValue == b->Data.BoolValue;
    }
    if (a->Type == PropertyType_Float)
    {
        return a->Data.FloatValue == b->Data.FloatValue;
    }
    if (a->Type == PropertyType_UInt32)
    {
        return a->Data.UInt32Value == b->Data.UInt32Value;
    }
    return (a->Type == PropertyType_FloatList) && PropertyFloatList_Equals(a->Data.FloatListValue, b->Data.FloatListValue);
}

/* Copies a row of a slot's settings onto a component's table, when a table holds it and it differs. */
static void CopyRow(const PropertyTable* table, void* component, const PropertyDescriptor* row, const PathObjectSlotRecord* slot)
{
    PropertyValue Value;
    Error Result = Properties_Get(PathObject_GetSlotPropertyTable(), slot, row->Key, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return;
    }
    PropertyValue Current;
    Result = Properties_Get(table, component, row->Key, &Current);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return;
    }
    if (AreValuesEqual(&Value, &Current))
    {
        return;
    }
    Result = Properties_Set(table, component, row->Key, &Value);
    Error_Deconstruct(&Result); // a slot's rows follow the same rules as the component's
}

/* The render and LOD rows a slot shares with a component's table, those the component has and the piece's own do not name. */
static void CopyRenderRows(const PropertyTable* table, void* component, const PathObjectSlotRecord* slot)
{
    const PropertyTable* SlotTable = PathObject_GetSlotPropertyTable();
    for (size_t Row = 0U; Row < table->DescriptorCount; Row++)
    {
        const PropertyDescriptor* Own = &table->Descriptors[Row];
        const PropertyDescriptor* Shared = PropertyTable_FindByKey(SlotTable, Own->Key);
        if ((Shared != NULL) && (Shared->Type == Own->Type))
        {
            CopyRow(table, component, Shared, slot);
        }
    }
}

/* Writes a string row when it differs. */
static void SetStringRow(const PropertyTable* table, void* component, const unsigned char* key, PropertyType type,
    const unsigned char* value)
{
    PropertyValue Current;
    Error Result = Properties_Get(table, component, key, &Current);
    if ((Result.Code == ErrorCode_Success) && WorldInternal_AreStringsEqual(Current.Data.StringValue, value))
    {
        return;
    }
    Error_Deconstruct(&Result);
    PropertyValue Value = (type == PropertyType_AssetName) ? PropertyValue_FromAssetName(value) : PropertyValue_FromString(value);
    Result = Properties_Set(table, component, key, &Value);
    Error_Deconstruct(&Result);
}

/* Writes a bool row when it differs. */
static void SetBoolRow(const PropertyTable* table, void* component, const unsigned char* key, bool value)
{
    PropertyValue Current;
    Error Result = Properties_Get(table, component, key, &Current);
    if ((Result.Code == ErrorCode_Success) && (Current.Data.BoolValue == value))
    {
        return;
    }
    Error_Deconstruct(&Result);
    PropertyValue Value = PropertyValue_FromBool(value);
    Result = Properties_Set(table, component, key, &Value);
    Error_Deconstruct(&Result);
}

/* What a child's model rows hold: the model's name and variant, then the slot's render rows. */
static void ApplyModelRows(const PropertyTable* table, void* component, const PathObjectSlotModel* model,
    const PathObjectSlotRecord* slot)
{
    SetStringRow(table, component, (const unsigned char*)u8"world_model", PropertyType_AssetName, model->Record->WorldModel);
    SetBoolRow(table, component, (const unsigned char*)u8"uses_variant", model->Record->UsesVariant);
    SetStringRow(table, component, (const unsigned char*)u8"variant", PropertyType_String, model->Record->Variant);
    CopyRenderRows(table, component, slot);
}

/* The object's tint on a child: so a tinted fence is tinted through its children. */
static void ApplyTint(WorldObject* child, RenderColor tint)
{
    if (!WorldPathObjectsInternal_AreTintsEqual(WorldObject_GetTint(child), tint))
    {
        Error Result = WorldObject_SetTint(child, tint);
        Error_Deconstruct(&Result);
    }
}

/* The layer, the mask and the trigger flag of a slot's colliders: its own, else the world model's, else the layer's. A collider
 * on a runtime child has no model component to find them through. */
static void ApplyColliderRows(WorldColliderComponent* collider, const PathObjectSlotModel* model,
    const PathObjectSlotRecord* slot)
{
    const WorldModelCollision* Collision = WorldModel_GetCollision(model->Asset);
    CollisionLayer Layer = slot->OverridesCollisionLayerValue ? slot->CollisionLayerValue
        : ((Collision != NULL) ? Collision->_layer : CollisionLayer_Prop);
    uint32_t Mask = slot->OverridesCollidesWith ? slot->CollidesWith
        : (((Collision != NULL) && Collision->_hasCollidesWith) ? Collision->_collidesWith
            : CollisionLayer_GetDefaultMask(Layer));
    if (!WorldColliderComponent_IsOverridden(collider, WorldColliderSetting_Layer)
        || (WorldColliderComponent_GetLayer(collider) != Layer))
    {
        Error Result = WorldColliderComponent_SetOverridden(collider, WorldColliderSetting_Layer, true);
        Error_Deconstruct(&Result);
        Result = WorldColliderComponent_SetLayer(collider, Layer);
        Error_Deconstruct(&Result);
    }
    if (!WorldColliderComponent_IsOverridden(collider, WorldColliderSetting_CollidesWith)
        || (WorldColliderComponent_GetCollidesWith(collider) != Mask))
    {
        Error Result = WorldColliderComponent_SetOverridden(collider, WorldColliderSetting_CollidesWith, true);
        Error_Deconstruct(&Result);
        Result = WorldColliderComponent_SetCollidesWith(collider, Mask);
        Error_Deconstruct(&Result);
    }
    bool IsTrigger = (Collision != NULL) && Collision->_isTrigger;
    if (!WorldColliderComponent_IsOverridden(collider, WorldColliderSetting_IsTrigger)
        || (WorldColliderComponent_IsTrigger(collider) != IsTrigger))
    {
        Error Result = WorldColliderComponent_SetOverridden(collider, WorldColliderSetting_IsTrigger, true);
        Error_Deconstruct(&Result);
        Result = WorldColliderComponent_SetIsTrigger(collider, IsTrigger);
        Error_Deconstruct(&Result);
    }
}


// Static functions: what a part collides as, and its tint.
/* A part's tint: the object's, and the model's multiplied in as a copy's is (its alpha an opacity). */
static RenderColor GetPartTint(RenderColor object, Color model)
{
    RenderColor Tint = object;
    Tint.Tint.r = (unsigned char)(((unsigned int)object.Tint.r * model.r) / 255U);
    Tint.Tint.g = (unsigned char)(((unsigned int)object.Tint.g * model.g) / 255U);
    Tint.Tint.b = (unsigned char)(((unsigned int)object.Tint.b * model.b) / 255U);
    Tint.Opacity = object.Opacity * ((float)model.a / 255.0f);
    return Tint;
}

/* The collision block of a part's own mesh: the generated model's LOD 0 as one two-sided mesh shape; NULL when it has nothing. */
static WorldColliderPlacedShapes* MakeMeshCollision(const WorldGeneratedModel* generated)
{
    const WorldGeneratedLod* Lod = &generated->Lods[0];
    if (Lod->MeshCount == 0U)
    {
        return NULL;
    }
    CollisionMeshPart* Parts = Memory_Allocate((size_t)Lod->MeshCount * sizeof(CollisionMeshPart));
    for (uint32_t MeshIndex = 0U; MeshIndex < Lod->MeshCount; MeshIndex++)
    {
        Parts[MeshIndex] = (CollisionMeshPart){ .Vertices = Lod->Meshes[MeshIndex].Positions,
            .VertexCount = Lod->Meshes[MeshIndex].VertexCount, .Indices = NULL, .TriangleCount = 0U,
            .MaterialIndex = Lod->Meshes[MeshIndex].MaterialSlot, .ActiveEdges = NULL, .SourceIndices = NULL };
    }
    CollisionMesh* Built = NULL;
    Error Result = CollisionMesh_Create(Parts, Lod->MeshCount, MatrixIdentity(), &Built);
    Memory_Free(Parts);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return NULL;
    }
    WorldColliderPlacedShapes* Block = WorldColliderPlacedShapes_Create(1U, 1U);
    Block->Meshes[0] = Built;
    Block->Shapes[0] = CollisionShape_Create(CollisionShapeType_Mesh);
    Block->Shapes[0].Mesh = Built;
    Block->Shapes[0].IsTwoSided = true;
    return Block;
}


// Functions.
/* Whether a collider's placed shapes are the ones it holds. By field, never by bytes: a shape's padding is whatever it was, and a
 * false "differ" here would rebuild the collider, which changes the ground, which runs another rebuild: for ever. */
static bool ArePlacedShapesEqual(const WorldColliderPlacedShapes* held, const CollisionShape* shapes,
    const Matrix4Double* placements, size_t count)
{
    if ((held == NULL) || (held->Count != count) || !Memory_IsEqual(held->Placements, placements, count * sizeof(Matrix4Double)))
    {
        return false;
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (!CollisionShape_Equals(&held->Shapes[Index], &shapes[Index]))
        {
            return false;
        }
    }
    return true;
}

/* The child of a slot's and model's copies: its copies and placed shapes, written only when they differ. */
void WorldPathObjectsChildren_WriteCopies(PathObjectRebuild* rebuild, PathObjectSlot slot, uint32_t modelIndex)
{
    WorldPathObjects* System = rebuild->System;
    World* TheWorld = System->_world;
    PathObjectCopyList* List = WorldPathObjectsInternal_GetCopyList(System, slot, modelIndex);
    size_t CopyCount = List->Copies._count;
    size_t ShapeCount = List->Shapes._count;
    if ((CopyCount == 0U) && (ShapeCount == 0U))
    {
        return;
    }
    rebuild->State->CopyCount += CopyCount;
    rebuild->State->ShapeCount += ShapeCount;
    const PathObjectSlotModel* ModelBuild = &rebuild->Slots[slot].Models[modelIndex];
    const PathObjectSlotRecord* Settings = &rebuild->Slots[slot].Settings;
    WorldObject* Child = FindOrMakeChild(rebuild, PathObjectChildKind_Copies, slot, modelIndex, 0U);
    if (Child == NULL)
    {
        return;
    }
    ApplyTint(Child, WorldObject_GetTint(rebuild->Owner));
    if (CopyCount > 0U)
    {
        WorldModelInstancesComponent* Instances = WorldModelInstancesComponent_FromComponent(FindOrAddComponent(TheWorld, Child,
            WorldComponentType_ModelInstances));
        if (Instances != NULL)
        {
            ApplyModelRows(WorldModelInstancesComponent_GetPropertyTable(), Instances, ModelBuild, Settings);
            size_t HeldCount = 0U;
            const PropertyInstanceEntry* Held = WorldModelInstancesComponent_GetInstances(Instances, &HeldCount);
            const PropertyInstanceEntry* Copies = (const PropertyInstanceEntry*)List->Copies._data;
            if ((HeldCount != CopyCount) || !Memory_IsEqual(Held, Copies, CopyCount * sizeof(PropertyInstanceEntry)))
            {
                Error Result = WorldModelInstancesComponent_SetInstances(Instances, Copies, CopyCount);
                if (Result.Code != ErrorCode_Success)
                {
                    WorldPathObjectsInternal_Warn(System, rebuild->Owner, "made copies its list refused: %s",
                        (Result.Message != NULL) ? (const char*)Result.Message : "");
                }
                Error_Deconstruct(&Result);
                rebuild->HasAnyChange = true;
                System->_stats.WriteCount++;
            }
        }
    }
    else
    {
        RemoveComponent(TheWorld, Child, WorldComponentType_ModelInstances);
    }
    if (ShapeCount > 0U)
    {
        WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(FindOrAddComponent(TheWorld, Child,
            WorldComponentType_Collider));
        if (Collider != NULL)
        {
            ApplyColliderRows(Collider, ModelBuild, Settings);
            const CollisionShape* Shapes = (const CollisionShape*)List->Shapes._data;
            const Matrix4Double* Placements = (const Matrix4Double*)List->Placements._data;
            if (!ArePlacedShapesEqual(WorldColliderComponent_GetPlacedShapes(Collider), Shapes, Placements, ShapeCount))
            {
                WorldColliderPlacedShapes* Block = WorldColliderPlacedShapes_Create(ShapeCount, 0U);
                Memory_Copy(Shapes, Block->Shapes, ShapeCount * sizeof(CollisionShape));
                Memory_Copy(Placements, Block->Placements, ShapeCount * sizeof(Matrix4Double));
                WorldColliderComponent_SetPlacedShapes(Collider, Block);
                WorldColliderPlacedShapes_Release(Block);
                rebuild->HasAnyChange = true;
                System->_stats.WriteCount++;
            }
        }
    }
    else
    {
        RemoveComponent(TheWorld, Child, WorldComponentType_Collider);
    }
}

/* A copy's pick for one enum: every bit of both mixed, so two enums of one copy choose apart. */
static uint32_t GetEnumPick(uint32_t pick, uint32_t property)
{
    uint32_t Mixed = pick ^ ((property + 1U) * PREFAB_PICK_SPREAD);
    Mixed ^= Mixed >> PREFAB_PICK_MIX_SHIFT_FIRST;
    Mixed *= PREFAB_PICK_MIX_FIRST;
    Mixed ^= Mixed >> PREFAB_PICK_MIX_SHIFT_SECOND;
    Mixed *= PREFAB_PICK_MIX_SECOND;
    Mixed ^= Mixed >> PREFAB_PICK_MIX_SHIFT_FIRST;
    return Mixed;
}

/* A copy's values: the model's, then a pick for every enum its values leave out, while it asks for that. */
static DataValue MakePieceValues(const PathObjectSlotModel* model, uint32_t pick)
{
    DataValue Values = DataValue_CreateTag();
    if (model->Record->Values != NULL)
    {
        Values = DataValue_Clone(model->Record->Values);
    }
    else
    {
        Error Result = DataValue_CreateCompound(&Values);
        Error_Deconstruct(&Result);
    }
    size_t Count = model->Record->RandomizesEnums ? Prefab_GetPropertyCount(model->Prefab) : 0U;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const PrefabProperty* Property = Prefab_GetProperty(model->Prefab, Index);
        if ((Property->Type != PrefabPropertyType_Enum) || (DataValue_FindCompoundEntry(&Values, Property->Name) != NULL))
        {
            continue;
        }
        const PrefabOption* Option = &Property->Options[GetEnumPick(pick, (uint32_t)Index) % Property->OptionCount];
        DataValue Name = DataValue_CreateTag();
        Error Result = DataValue_CreateString(Option->Name, &Name);
        if (Result.Code == ErrorCode_Success)
        {
            Result = DataValue_CompoundSet(&Values, Property->Name, &Name);
        }
        Error_Deconstruct(&Result);
        DataValue_Deconstruct(&Name);
    }
    return Values;
}

/* Writes a row of a child's prefab component when it differs. */
static bool SetPieceRow(WorldPrefabComponent* component, const unsigned char* key, const PropertyValue* value)
{
    const PropertyTable* Table = WorldPrefabComponent_GetPropertyTable();
    PropertyValue Current;
    Error Result = Properties_Get(Table, component, key, &Current);
    if ((Result.Code == ErrorCode_Success) && PropertyValue_Equals(&Current, value))
    {
        return false;
    }
    Error_Deconstruct(&Result);
    Result = Properties_Set(Table, component, key, value);
    Error_Deconstruct(&Result);
    return true;
}

/* Places a child, writing only what differs. */
static bool PlaceChild(WorldObject* child, const PropertyInstanceEntry* copy)
{
    bool HasChanged = false;
    if (!Vector3Double_EqualsClose(WorldObject_GetPosition(child), copy->Position, 0.0))
    {
        Error Result = WorldObject_SetPosition(child, copy->Position);
        Error_Deconstruct(&Result);
        HasChanged = true;
    }
    Vector3 Rotation = WorldObject_GetRotation(child);
    if ((Rotation.x != copy->Rotation.x) || (Rotation.y != copy->Rotation.y) || (Rotation.z != copy->Rotation.z))
    {
        Error Result = WorldObject_SetRotation(child, copy->Rotation);
        Error_Deconstruct(&Result);
        HasChanged = true;
    }
    Vector3 Scale = WorldObject_GetScale(child);
    if ((Scale.x != copy->Scale.x) || (Scale.y != copy->Scale.y) || (Scale.z != copy->Scale.z))
    {
        Error Result = WorldObject_SetScale(child, copy->Scale);
        Error_Deconstruct(&Result);
        HasChanged = true;
    }
    return HasChanged;
}

void WorldPathObjectsChildren_WritePrefabCopies(PathObjectRebuild* rebuild, PathObjectSlot slot, uint32_t modelIndex)
{
    WorldPathObjects* System = rebuild->System;
    PathObjectCopyList* List = WorldPathObjectsInternal_GetCopyList(System, slot, modelIndex);
    const PathObjectSlotModel* ModelBuild = &rebuild->Slots[slot].Models[modelIndex];
    const PropertyInstanceEntry* Copies = (const PropertyInstanceEntry*)List->Copies._data;
    const uint32_t* Picks = (const uint32_t*)List->Picks._data;
    rebuild->State->CopyCount += List->Copies._count;
    for (size_t Index = 0U; Index < List->Copies._count; Index++)
    {
        WorldObject* Child = FindOrMakeChild(rebuild, PathObjectChildKind_PrefabCopy, slot, modelIndex, (uint32_t)Index);
        if (Child == NULL)
        {
            return;
        }
        WorldPrefabComponent* Component = WorldPrefabComponent_FromComponent(FindOrAddComponent(System->_world, Child,
            WorldComponentType_Prefab));
        if (Component == NULL)
        {
            continue;
        }
        bool HasChanged = PlaceChild(Child, &Copies[Index]);
        PropertyValue Name = PropertyValue_FromAssetName(ModelBuild->Record->Prefab);
        HasChanged = SetPieceRow(Component, (const unsigned char*)u8"prefab", &Name) || HasChanged;
        DataValue Values = MakePieceValues(ModelBuild, Picks[Index]);
        PropertyValue Row = PropertyValue_FromDataValue(&Values);
        HasChanged = SetPieceRow(Component, (const unsigned char*)u8"values", &Row) || HasChanged;
        DataValue_Deconstruct(&Values);
        if (HasChanged)
        {
            rebuild->HasAnyChange = true;
            System->_stats.WriteCount++;
        }
    }
}

/* The child of a part: its place, its rows and its generated model, written only when they differ. */
void WorldPathObjectsChildren_WritePart(PathObjectRebuild* rebuild, const PathObjectBentPart* part)
{
    WorldPathObjects* System = rebuild->System;
    World* TheWorld = System->_world;
    const PathObjectSlotBuild* Slot = &rebuild->Slots[part->Slot];
    const PathObjectSlotModel* ModelBuild = &Slot->Models[part->Model];
    WorldObject* Child = FindOrMakeChild(rebuild, PathObjectChildKind_Part, part->Slot, part->Model, part->Part);
    if (Child == NULL)
    {
        return;
    }
    ApplyTint(Child, GetPartTint(WorldObject_GetTint(rebuild->Owner), ModelBuild->Record->Tint));
    WorldModelComponent* Component = WorldModelComponent_FromComponent(FindOrAddComponent(TheWorld, Child,
        WorldComponentType_Model));
    if (Component == NULL)
    {
        return;
    }
    ApplyModelRows(WorldModelComponent_GetPropertyTable(), Component, ModelBuild, &Slot->Settings);
    WorldGeneratedModel* Generated = WorldPathObjectsBent_MakeGeneratedModel(rebuild, part);
    bool IsSame = WorldGeneratedModel_Equals(WorldModelComponent_GetGeneratedModel(Component), Generated)
        && Vector3Double_EqualsClose(WorldObject_GetPosition(Child), part->Center, 0.0);
    rebuild->State->GeneratedModelCount++;
    for (uint32_t MeshIndex = 0U; MeshIndex < Generated->Lods[0].MeshCount; MeshIndex++)
    {
        rebuild->State->TriangleCount += Generated->Lods[0].Meshes[MeshIndex].VertexCount / 3U;
    }
    for (size_t Lod = 0U; Lod < Generated->LodCount; Lod++)
    {
        for (uint32_t MeshIndex = 0U; MeshIndex < Generated->Lods[Lod].MeshCount; MeshIndex++)
        {
            rebuild->State->VertexCount += Generated->Lods[Lod].Meshes[MeshIndex].VertexCount;
        }
    }
    if (IsSame)
    {
        WorldGeneratedModel_Destroy(Generated);
    }
    else
    {
        Error Result = WorldObject_SetPosition(Child, part->Center);
        if (Result.Code != ErrorCode_Success)
        {
            // Past where an object may lie: it stays out, and is not tried as a change on every rebuild.
            Error_Deconstruct(&Result);
            WorldGeneratedModel_Destroy(Generated);
            if (!rebuild->State->HasWarnedPlace)
            {
                rebuild->State->HasWarnedPlace = true;
                WorldPathObjectsInternal_Warn(System, rebuild->Owner,
                    "bends a stretch too far out for an object to lie: it is left out.");
            }
            return;
        }
        Error_Deconstruct(&Result);
        WorldModelComponent_SetGeneratedModel(Component, Generated);
        rebuild->HasAnyChange = true;
        System->_stats.WriteCount++;
    }
    bool WantsMesh = Slot->Settings.Collision == PathObjectCollision_Mesh;
    if (!WantsMesh)
    {
        RemoveComponent(TheWorld, Child, WorldComponentType_Collider);
        return;
    }
    WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(FindOrAddComponent(TheWorld, Child,
        WorldComponentType_Collider));
    if (Collider == NULL)
    {
        return;
    }
    ApplyColliderRows(Collider, ModelBuild, &Slot->Settings);
    if (!IsSame || (WorldColliderComponent_GetPlacedShapes(Collider) == NULL))
    {
        WorldColliderPlacedShapes* Block = MakeMeshCollision(WorldModelComponent_GetGeneratedModel(Component));
        if (Block == NULL)
        {
            // Nothing at its finest LOD: no collider at all, for one without placed shapes would fall back on its world model's
            // set.
            RemoveComponent(TheWorld, Child, WorldComponentType_Collider);
            return;
        }
        WorldColliderComponent_SetPlacedShapes(Collider, Block);
        WorldColliderPlacedShapes_Release(Block);
    }
}

/* Marks every child unused; those a rebuild does not use go at its end. */
void WorldPathObjectsChildren_MarkUnused(PathObjectState* state)
{
    PathObjectChildRecord* Records = (PathObjectChildRecord*)state->Children._data;
    for (size_t Index = 0U; Index < state->Children._count; Index++)
    {
        Records[Index].IsUsed = false;
    }
}

static bool IsChildUsed(GenericBuffer* buffer, GenericBufferElementData element, const UserData* userData)
{
    (void)buffer;
    (void)userData;
    return ((const PathObjectChildRecord*)element._element)->IsUsed;
}

/* Removes the children no rebuild uses, all in one go: one by one they would walk the world's lists once each. */
void WorldPathObjectsChildren_RemoveUnused(WorldPathObjects* self, PathObjectState* state)
{
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 0U, sizeof(uint64_t));
    PathObjectChildRecord* Records = (PathObjectChildRecord*)state->Children._data;
    for (size_t Index = 0U; Index < state->Children._count; Index++)
    {
        if (!Records[Index].IsUsed)
        {
            GenericBuffer_AddLast(&Ids, &Records[Index].ObjectId);
        }
    }
    Error Result = World_RemoveObjects(self->_world, (const uint64_t*)Ids._data, Ids._count);
    Error_Deconstruct(&Result);
    self->_stats.ChildCount -= Ids._count;
    Memory_Free(Ids._data);
    GenericBuffer_Filter(&state->Children, IsChildUsed, NULL);
}
