// The path objects system: see WorldPathObjects.h. It follows the components and runs a rebuild when something a component
// depends on changed: the layout (PathObjectLayout.h), then the pieces (WorldPathObjectsRigid.c, WorldPathObjectsBent.c), then
// the children (WorldPathObjectsChildren.c).

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldPathObjectsInternal.h"
#include "GameModel.h"
#include "WorldInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** How much past a run's points the area watched for ground changes reaches, in metres. */
#define GROUND_AREA_MARGIN 1.0


// Static functions: a rebuild.
/* A world model's measure: LOD 0's vertices under its import transform, or its box where it has no vertices at hand. */
static PathObjectMeasure MeasureWorldModel(WorldPathObjects* self, const WorldModel* asset)
{
    const PathObjectMeasuredModel* Measures = (const PathObjectMeasuredModel*)self->_measures._data;
    for (size_t Index = 0U; Index < self->_measures._count; Index++)
    {
        if (Measures[Index].Asset == asset)
        {
            return Measures[Index].Measure;
        }
    }
    GameModel* Lod = WorldModel_GetLodModel(asset, 0U);
    Model Source = GameModel_GetRaylibModel(Lod);
    PathObjectMeasure Measure = PathObjectMeasure_CreateEmpty();
    bool HasVertices = false;
    for (int MeshIndex = 0; MeshIndex < Source.meshCount; MeshIndex++)
    {
        if ((Source.meshes[MeshIndex].vertices != NULL) && (Source.meshes[MeshIndex].vertexCount > 0))
        {
            PathObjectMeasure_AddVertices(&Measure, Source.meshes[MeshIndex].vertices,
                (size_t)Source.meshes[MeshIndex].vertexCount, Source.transform);
            HasVertices = true;
        }
    }
    if (!HasVertices)
    {
        PathObjectMeasure_AddBox(&Measure, GameModel_GetLocalBounds(Lod), Source.transform);
    }
    PathObjectMeasuredModel Held = { .Asset = asset, .Measure = Measure };
    GenericBuffer_AddLast(&self->_measures, &Held);
    return Measure;
}

/* A slot's settings and models in effect: each model's world model loaded, a failure warned about once and the model left out. */
static void BuildSlot(PathObjectRebuild* rebuild, PathObjectSlot slot, const PathObjectSettings* assetSettings)
{
    WorldPathObjects* System = rebuild->System;
    PathObjectSlotBuild* Build = &rebuild->Slots[slot];
    const PathObjectSettings* Placement = WorldPathObjectComponent_GetSettings(rebuild->Component);
    Build->Settings = PathObject_ResolveSlot(slot, assetSettings, Placement);
    Build->ModelCount = 0U;
    Build->IsUsed = PathObjectSettings_IsSlotUsed(&rebuild->Settings, slot);
    if (!Build->IsUsed)
    {
        return;
    }
    const PathObjectModelRecord* Records[WORLD_PATH_OBJECTS_MAX_SLOT_MODELS];
    size_t Count = PathObject_GetSlotModels(slot, assetSettings, Placement, Records, WORLD_PATH_OBJECTS_MAX_SLOT_MODELS);
    if ((Count > WORLD_PATH_OBJECTS_MAX_SLOT_MODELS) && !rebuild->State->HasWarnedModel)
    {
        rebuild->State->HasWarnedModel = true;
        WorldPathObjectsInternal_Warn(System, rebuild->Owner, "names more than %u models for a slot: the rest are left out.",
            WORLD_PATH_OBJECTS_MAX_SLOT_MODELS);
    }
    Count = (Count > WORLD_PATH_OBJECTS_MAX_SLOT_MODELS) ? WORLD_PATH_OBJECTS_MAX_SLOT_MODELS : Count;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        WorldModel* Asset = NULL;
        Error Result = (Records[Index]->WorldModel != NULL)
            ? AssetManager_LoadWorldModel(System->_assets, Records[Index]->WorldModel, System->_assetUser, &Asset)
            : Error_Construct2(ErrorCode_InvalidOperation, "a model of its slot names no world model.");
        if (Result.Code != ErrorCode_Success)
        {
            if (!rebuild->State->HasWarnedModel)
            {
                rebuild->State->HasWarnedModel = true;
                WorldPathObjectsInternal_Warn(System, rebuild->Owner, "leaves out a model: %s",
                    (Result.Message != NULL) ? (const char*)Result.Message : "");
            }
            Error_Deconstruct(&Result);
            continue;
        }
        PathObjectSlotModel* Held = &Build->Models[Build->ModelCount];
        Held->Asset = Asset;
        Held->Record = Records[Index];
        Held->HasParts = false;
        Held->LastPart = 0U;
        Held->Measure = MeasureWorldModel(System, Asset);
        Held->Length = PathObjectMeasure_GetLength(&Held->Measure, &Build->Settings);
        Build->LayoutModels[Build->ModelCount] = (PathObjectLayoutModel){ .Length = Held->Length,
            .Weight = Records[Index]->Weight };
        Build->ModelCount++;
    }
}

static void ClearCopyLists(WorldPathObjects* self)
{
    size_t Count = (size_t)PathObjectSlot_Count * WORLD_PATH_OBJECTS_MAX_SLOT_MODELS;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        GenericBuffer_Clear(&self->_copyLists[Index].Copies);
        GenericBuffer_Clear(&self->_copyLists[Index].Shapes);
        GenericBuffer_Clear(&self->_copyLists[Index].Placements);
    }
}

/* Resolves the asset a component names, when the name changed. */
static void ResolveAsset(WorldPathObjects* self, PathObjectState* state, const WorldPathObjectComponent* component,
    const WorldObject* owner)
{
    const unsigned char* Name = WorldPathObjectComponent_GetPathObject(component);
    if (WorldInternal_AreStringsEqual(Name,
        state->AssetName) && ((state->Asset != NULL) || state->HasWarnedAsset || (Name == NULL)))
    {
        return;
    }
    Memory_Free(state->AssetName);
    state->AssetName = WorldInternal_CloneString(Name);
    state->Asset = NULL;
    state->HasWarnedAsset = false;
    if (Name == NULL)
    {
        return;
    }
    PathObject* Asset = NULL;
    Error Result = AssetManager_LoadPathObject(self->_assets, Name, self->_assetUser, &Asset);
    if (Result.Code != ErrorCode_Success)
    {
        state->HasWarnedAsset = true;
        WorldPathObjectsInternal_Warn(self, owner, "generates nothing: its path object does not load: %s",
            (Result.Message != NULL) ? (const char*)Result.Message : "");
        Error_Deconstruct(&Result);
        return;
    }
    state->Asset = Asset;
}

/* Takes what a state holds out of the stats, as it starts over or goes. */
static void ForgetCounts(WorldPathObjects* self, PathObjectState* state)
{
    self->_stats.PieceCount -= state->PieceCount;
    self->_totalVertices -= state->VertexCount;
    self->_stats.CopyCount -= state->CopyCount;
    self->_stats.PlacedShapeCount -= state->ShapeCount;
    self->_stats.GeneratedModelCount -= state->GeneratedModelCount;
    self->_stats.GeneratedTriangleCount -= state->TriangleCount;
    state->PieceCount = 0U;
    state->CopyCount = 0U;
    state->ShapeCount = 0U;
    state->GeneratedModelCount = 0U;
    state->TriangleCount = 0U;
    state->VertexCount = 0U;
}

/* A rebuild's data over a component. */
static void StartRebuild(WorldPathObjects* self, PathObjectState* state, WorldPathObjectComponent* component,
    const WorldPathComponent* path, size_t ownShapes, PathObjectRebuild* outBuild)
{
    WorldObject* Owner = WorldComponent_GetOwner(&component->Base);
    Memory_Zero(outBuild, sizeof(*outBuild));
    outBuild->System = self;
    outBuild->State = state;
    outBuild->Component = component;
    outBuild->Owner = Owner;
    outBuild->Curve = WorldPathComponent_GetCurve(path);
    outBuild->Settings = PathObject_ResolveSettings(PathObject_GetSettings(state->Asset),
        WorldPathObjectComponent_GetSettings(component));
    outBuild->Slots = self->_slotBuilds;
    outBuild->ToWorld = WorldObject_GetWorldMatrix(Owner);
    Matrix4Double Axes = outBuild->ToWorld;
    Matrix4Double_SetTranslation(&Axes, Vector3Double_Zero());
    outBuild->HasInverse = Matrix4Double_TryInvertAffine(&Axes, &outBuild->ToLocalAxes);
    outBuild->Ground = PathObjectSettings_GetGround(&outBuild->Settings);
    outBuild->Ground.IgnoredObjectId = WorldObject_GetId(Owner);
    size_t VerticesLeft = (self->_totalVertices < self->_limits.MaxVertices)
        ? (self->_limits.MaxVertices - self->_totalVertices) : 0U;
    outBuild->VertexBudget = (VerticesLeft < WORLD_PATH_OBJECTS_MAX_BUILD_VERTICES)
        ? VerticesLeft : WORLD_PATH_OBJECTS_MAX_BUILD_VERTICES;
    outBuild->ShapeBudget = (self->_stats.PlacedShapeCount < self->_limits.MaxPlacedShapes)
        ? (self->_limits.MaxPlacedShapes - self->_stats.PlacedShapeCount) : 0U;
    outBuild->BentWorkLeft = WORLD_PATH_OBJECTS_MAX_BENT_WORK;
    outBuild->GroundWorkLeft = WORLD_PATH_OBJECTS_MAX_GROUND_WORK;
    outBuild->GroundCost = 1U + (uint64_t)ownShapes;
    for (size_t Slot = 0U; Slot < (size_t)PathObjectSlot_Count; Slot++)
    {
        self->_sampleCaches[Slot].Section = UINT32_MAX;
    }
    self->_cornersSpan = NULL;
    state->IsTerrainOnly = outBuild->Settings.GroundTarget == PathGroundTarget_Terrain;
}

/* Lays out the pieces of a rebuild's path, within what the system may still hold. False when nothing comes of it. */
static bool LayOutPieces(PathObjectRebuild* build, const WorldPathComponent* path)
{
    WorldPathObjects* Self = build->System;
    PathObjectState* State = build->State;
    PathObjectLayoutInput Input;
    Memory_Zero(&Input, sizeof(Input));
    Input.Curve = build->Curve;
    size_t NodeCount = 0U;
    Input.Nodes = WorldPathComponent_GetNodes(path, &NodeCount);
    Input.NodeCount = NodeCount;
    Input.Settings = &build->Settings;
    size_t Budget = (Self->_stats.PieceCount < Self->_limits.MaxPieces)
        ? (Self->_limits.MaxPieces - Self->_stats.PieceCount) : 0U;
    Input.MaxPieces = (Budget < WORLD_PATH_OBJECTS_MAX_PIECES) ? Budget : WORLD_PATH_OBJECTS_MAX_PIECES;
    if (Input.MaxPieces == 0U)
    {
        if (!State->HasWarnedLimit)
        {
            State->HasWarnedLimit = true;
            WorldPathObjectsInternal_Warn(Self, build->Owner,
                "generates nothing: the world's path objects hold as many pieces as they may.");
        }
        return false;
    }
    const PathObjectSettings* AssetSettings = PathObject_GetSettings(State->Asset);
    for (uint32_t Slot = 0U; Slot < (uint32_t)PathObjectSlot_Count; Slot++)
    {
        BuildSlot(build, (PathObjectSlot)Slot, AssetSettings);
        PathObjectSlotBuild* Held = &Self->_slotBuilds[Slot];
        Input.Slots[Slot] = (PathObjectLayoutSlot){ .Models = Held->LayoutModels, .ModelCount = Held->ModelCount,
            .IsUsed = Held->IsUsed, .Pick = Held->Settings.Pick, .Stretches = Held->Settings.Stretches,
            .IsRigid = Held->Settings.Curve == PathObjectCurve_Rigid };
    }
    Error Result = PathObjectLayout_Build(&Self->_layout, &Input);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return false;
    }
    if (PathObjectLayout_WasLimited(&Self->_layout) && !State->HasWarnedLimit)
    {
        State->HasWarnedLimit = true;
        WorldPathObjectsInternal_Warn(Self, build->Owner,
            "lays out more pieces than a path object may hold (%" PRIu64 "): the rest are left out.",
            (uint64_t)Input.MaxPieces);
    }
    if (PathObjectLayout_HasZeroLengthMiddle(&Self->_layout) && !State->HasWarnedZeroMiddle)
    {
        State->HasWarnedZeroMiddle = true;
        WorldPathObjectsInternal_Warn(Self, build->Owner, "has a middle of no length: nothing can fill a path with it.");
    }
    return true;
}

/* Makes what the laid out pieces make: into the copy lists and the parts. */
static void MakePieces(PathObjectRebuild* build)
{
    WorldPathObjects* Self = build->System;
    ClearCopyLists(Self);
    WorldPathObjectsBent_FreeParts(Self);
    size_t PieceCount = 0U;
    const PathObjectPiece* Pieces = PathObjectLayout_GetPieces(&Self->_layout, &PieceCount);
    size_t SpanCount = 0U;
    const PathObjectSpan* Spans = PathObjectLayout_GetSpans(&Self->_layout, &SpanCount);
    for (size_t Index = 0U; Index < PieceCount; Index++)
    {
        const PathObjectPiece* Piece = &Pieces[Index];
        const PathObjectSlotBuild* Slot = &Self->_slotBuilds[Piece->Slot];
        const PathObjectSlotModel* ModelBuild = &Slot->Models[Piece->Model];
        const PathObjectSpan* Span = &Spans[Piece->Span];
        float Signs[WORLD_PATH_OBJECTS_MAX_PIECE_SIDES];
        uint32_t Sides = WorldPathObjectsRigid_GetPieceSides(Piece, &Slot->Settings, Signs);
        bool IsCut = (Piece->CutStart > 0.0f) || (Piece->CutEnd > 0.0f);
        float Nominal = PathObjectMeasure_GetLength(&ModelBuild->Measure, &Slot->Settings);
        bool IsBent = (((Slot->Settings.Curve == PathObjectCurve_Bent) && (Nominal > 0.0f)) || IsCut)
            && (Piece->End > Piece->Start);
        for (uint32_t Side = 0U; Side < Sides; Side++)
        {
            if (IsBent)
            {
                WorldPathObjectsBent_AddCopy(build, Piece, Span, Slot, Side, Signs[Side]);
            }
            else
            {
                WorldPathObjectsRigid_AddCopy(build, Piece, Span, Slot, Side, Signs[Side]);
            }
        }
    }
    build->State->PieceCount = PieceCount;
    Self->_stats.PieceCount += PieceCount;
}

/* Writes what came out on the children. */
static void WriteChildren(PathObjectRebuild* build)
{
    WorldPathObjects* Self = build->System;
    PathObjectState* State = build->State;
    for (uint32_t Slot = 0U; Slot < (uint32_t)PathObjectSlot_Count; Slot++)
    {
        for (uint32_t ModelIndex = 0U; ModelIndex < Self->_slotBuilds[Slot].ModelCount; ModelIndex++)
        {
            WorldPathObjectsChildren_WriteCopies(build, (PathObjectSlot)Slot, ModelIndex);
        }
    }
    const PathObjectBentPart* Parts = (const PathObjectBentPart*)Self->_parts._data;
    for (size_t Index = 0U; Index < Self->_parts._count; Index++)
    {
        if (!WorldPathObjectsBent_IsPartEmpty(&Parts[Index]))
        {
            WorldPathObjectsChildren_WritePart(build, &Parts[Index]);
        }
    }
    WorldPathObjectsBent_FreeParts(Self);
    Self->_stats.CopyCount += State->CopyCount;
    Self->_stats.PlacedShapeCount += State->ShapeCount;
    Self->_stats.GeneratedModelCount += State->GeneratedModelCount;
    Self->_stats.GeneratedTriangleCount += State->TriangleCount;
    Self->_totalVertices += State->VertexCount;
    if (!build->HasAnyChange)
    {
        Self->_stats.UnchangedRebuildCount++;
    }
}

/* Lays out the pieces of one component and makes what they make; what no longer comes out goes. */
static void RunRebuild(WorldPathObjects* self, PathObjectState* state, WorldPathObjectComponent* component,
    const WorldPathComponent* path)
{
    self->_stats.RebuildCount++;
    size_t OwnShapes = state->ShapeCount;
    ForgetCounts(self, state);
    state->HasSnapped = false;
    state->GroundArea = (BoundingBoxDouble){ .Min = Vector3Double_Create(1.0e300, 1.0e300, 1.0e300),
        .Max = Vector3Double_Create(-1.0e300, -1.0e300, -1.0e300) };
    WorldPathObjectsChildren_MarkUnused(state);
    ResolveAsset(self, state, component, WorldComponent_GetOwner(&component->Base));
    if ((state->Asset != NULL) && (path != NULL))
    {
        PathObjectRebuild Build;
        StartRebuild(self, state, component, path, OwnShapes, &Build);
        if (LayOutPieces(&Build, path))
        {
            MakePieces(&Build);
            WriteChildren(&Build);
        }
    }
    WorldPathObjectsChildren_RemoveUnused(self, state);
}

/* The state of a component's serial, or NULL. A list in the states' order costs one comparison each: the search goes on from the
 * last one found. */
static PathObjectState* FindState(WorldPathObjects* self, uint64_t serial)
{
    PathObjectState* States = (PathObjectState*)self->_states._data;
    size_t Count = self->_states._count;
    for (size_t Step = 0U; Step < Count; Step++)
    {
        size_t Index = self->_stateCursor + Step;
        Index = (Index < Count) ? Index : (Index - Count);
        if (States[Index].Serial == serial)
        {
            self->_stateCursor = ((Index + 1U) < Count) ? (Index + 1U) : 0U;
            return &States[Index];
        }
    }
    return NULL;
}

/* The children a state holds go, its counts leave the stats and its own memory is freed. */
static void DropState(WorldPathObjects* self, PathObjectState* state)
{
    WorldPathObjectsChildren_MarkUnused(state);
    WorldPathObjectsChildren_RemoveUnused(self, state);
    ForgetCounts(self, state);
    Memory_Free(state->AssetName);
    Memory_Free(state->Children._data);
}

/* Matches the states to the components: a state for each component, in their order, and none for a component that went (its
 * children go, unless its object took them along). */
static void RematchStates(WorldPathObjects* self)
{
    size_t Count = 0U;
    WorldComponent* const* Components = World_GetComponents(self->_world, WorldComponentType_PathObject, &Count);
    PathObjectState* States = (PathObjectState*)self->_states._data;
    for (size_t Index = 0U; Index < self->_states._count; Index++)
    {
        States[Index].IsListed = false;
    }
    self->_stateCursor = 0U;
    for (size_t Component = 0U; Component < Count; Component++)
    {
        uint64_t Serial = WorldComponent_GetSerial(Components[Component]);
        PathObjectState* Found = FindState(self, Serial);
        if (Found != NULL)
        {
            Found->IsListed = true;
            continue;
        }
        PathObjectState Fresh;
        Memory_Zero(&Fresh, sizeof(Fresh));
        Fresh.Serial = Serial;
        Fresh.ObjectId = WorldObject_GetId(WorldComponent_GetOwner(Components[Component]));
        Fresh.Watch = WorldPathWatch_Create();
        Fresh.IsListed = true;
        GenericBuffer_AllocateVariable(&Fresh.Children, 0U, sizeof(PathObjectChildRecord));
        GenericBuffer_AddLast(&self->_states, &Fresh);
    }
    States = (PathObjectState*)self->_states._data;
    for (size_t Index = self->_states._count; Index > 0U; Index--)
    {
        if (!States[Index - 1U].IsListed)
        {
            DropState(self, &States[Index - 1U]);
            GenericBuffer_RemoveAt(&self->_states, Index - 1U);
        }
    }
    self->_stateCursor = 0U;
    self->_stats.ComponentCount = Count;
}

/* One component: a rebuild when something it depends on changed. */
static void RefreshComponent(WorldPathObjects* self, PathObjectState* state, WorldPathObjectComponent* component)
{
    WorldObject* Owner = WorldComponent_GetOwner(&component->Base);
    if (!WorldObject_IsEffectivelyEnabled(Owner))
    {
        return; // children follow their parent's flags; a disabled one waits
    }
    WorldPathComponent* Path = WorldPathComponent_FindForObject(Owner);
    WorldPathChange Change = WorldPathWatch_Update(&state->Watch, Path);
    uint32_t Revision = WorldComponent_GetRevision(&component->Base);
    uint32_t TransformRevision = WorldObject_GetTransformRevision(Owner);
    RenderColor Tint = WorldObject_GetTint(Owner);
    bool NeedsRebuild = !state->HasBuilt || (Revision != state->SeenRevision) || Change.HasPathChanged
        || !WorldPathObjectsInternal_AreTintsEqual(Tint, state->SeenTint);
    if (!NeedsRebuild && state->HasSnapped)
    {
        // the path's object lies at or above it: its move counts too
        NeedsRebuild = TransformRevision != state->SeenTransformRevision;
        if (!NeedsRebuild && (self->_collision != NULL))
        {
            BoundingBoxDouble Area = BoundingBoxDouble_Expand(state->GroundArea, GROUND_AREA_MARGIN);
            NeedsRebuild = WorldCollision_HasGroundChanged(self->_collision, state->SeenGroundRevision, &Area,
                state->IsTerrainOnly);
        }
    }
    uint64_t GroundRevision = (self->_collision != NULL) ? WorldCollision_GetGroundRevision(self->_collision) : 0U;
    if (NeedsRebuild)
    {
        RunRebuild(self, state, component, Path);
    }
    state->HasBuilt = true;
    state->SeenRevision = Revision;
    state->SeenTransformRevision = TransformRevision;
    state->SeenGroundRevision = GroundRevision;
    state->SeenTint = Tint;
}


// Functions.
void WorldPathObjectsInternal_Warn(const WorldPathObjects* self, const WorldObject* object, const char* format, ...)
{
    if (self->_logger == NULL)
    {
        return;
    }
    char Text[512];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        return;
    }
    const unsigned char* Name = WorldObject_GetName(object);
    Error Result = Logger_LogFormatted(self->_logger, LogLevel_Warning, (const unsigned char*)u8"WorldPathObjects: object %"
        PRIu64 " (\"%s\") %s", WorldObject_GetId(object), (Name != NULL) ? (const char*)Name : "", Text);
    Error_Deconstruct(&Result);
}

Error WorldPathObjects_Create(World* world, WorldCollision* collision, AssetManager* assets, Logger* logger,
    WorldPathObjects** outSystem)
{
    if ((world == NULL) || (assets == NULL) || (outSystem == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldPathObjects_Create: the world, the asset manager and the out parameter are required.");
    }
    WorldPathObjects* Self = Memory_Allocate(sizeof(WorldPathObjects));
    Memory_Zero(Self, sizeof(*Self));
    Self->_world = world;
    Self->_collision = collision;
    Self->_assets = assets;
    Self->_logger = logger;
    Self->_limits = WorldPathObjectsLimits_CreateDefault();
    Error Result = AssetManager_GetNewUserID(assets, &Self->_assetUser);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Self);
        return Result;
    }
    GenericBuffer_AllocateVariable(&Self->_states, 0U, sizeof(PathObjectState));
    GenericBuffer_AllocateVariable(&Self->_measures, 0U, sizeof(PathObjectMeasuredModel));
    for (size_t Slot = 0U; Slot < (size_t)PathObjectSlot_Count; Slot++)
    {
        GenericBuffer_AllocateVariable(&Self->_sampleCaches[Slot].Samples, 0U, sizeof(double));
    }
    GenericBuffer_AllocateVariable(&Self->_corners, 0U, sizeof(PathObjectSpanCorner));
    GenericBuffer_AllocateVariable(&Self->_distances, 0U, sizeof(double));
    GenericBuffer_AllocateVariable(&Self->_ends, 0U, sizeof(PathObjectBentEnd));
    GenericBuffer_AllocateVariable(&Self->_places, 0U, sizeof(PathObjectBentPlace));
    GenericBuffer_AllocateVariable(&Self->_parts, 0U, sizeof(PathObjectBentPart));
    PathObjectLayout_Construct(&Self->_layout);
    Self->_slotBuilds = Memory_Allocate((size_t)PathObjectSlot_Count * sizeof(PathObjectSlotBuild));
    size_t ListCount = (size_t)PathObjectSlot_Count * WORLD_PATH_OBJECTS_MAX_SLOT_MODELS;
    Self->_copyLists = Memory_Allocate(ListCount * sizeof(PathObjectCopyList));
    for (size_t Index = 0U; Index < ListCount; Index++)
    {
        GenericBuffer_AllocateVariable(&Self->_copyLists[Index].Copies, 0U, sizeof(PropertyInstanceEntry));
        GenericBuffer_AllocateVariable(&Self->_copyLists[Index].Shapes, 0U, sizeof(CollisionShape));
        GenericBuffer_AllocateVariable(&Self->_copyLists[Index].Placements, 0U, sizeof(Matrix4Double));
    }
    *outSystem = Self;
    return Error_CreateSuccess();
}

void WorldPathObjects_Destroy(WorldPathObjects* self)
{
    if (self == NULL)
    {
        return;
    }
    PathObjectState* States = (PathObjectState*)self->_states._data;
    for (size_t Index = 0U; Index < self->_states._count; Index++)
    {
        DropState(self, &States[Index]);
    }
    Memory_Free(self->_states._data);
    Memory_Free(self->_measures._data);
    WorldPathObjectsBent_FreeParts(self);
    for (size_t Slot = 0U; Slot < (size_t)PathObjectSlot_Count; Slot++)
    {
        Memory_Free(self->_sampleCaches[Slot].Samples._data);
    }
    Memory_Free(self->_corners._data);
    Memory_Free(self->_distances._data);
    Memory_Free(self->_ends._data);
    Memory_Free(self->_places._data);
    Memory_Free(self->_parts._data);
    PathObjectLayout_Deconstruct(&self->_layout);
    Memory_Free(self->_slotBuilds);
    size_t ListCount = (size_t)PathObjectSlot_Count * WORLD_PATH_OBJECTS_MAX_SLOT_MODELS;
    for (size_t Index = 0U; Index < ListCount; Index++)
    {
        Memory_Free(self->_copyLists[Index].Copies._data);
        Memory_Free(self->_copyLists[Index].Shapes._data);
        Memory_Free(self->_copyLists[Index].Placements._data);
    }
    Memory_Free(self->_copyLists);
    // The collision world lets go of the placed shapes it holds now, for they borrow from the world models that go next.
    if (self->_collision != NULL)
    {
        WorldCollision_Sync(self->_collision);
    }
    Error Result = AssetManager_RetireUser(self->_assets, self->_assetUser);
    Error_Deconstruct(&Result);
    Memory_Free(self);
}

void WorldPathObjects_Update(WorldPathObjects* self)
{
    uint64_t Version = World_GetComponentSetVersion(self->_world);
    if (!self->_hasComponentSetVersion || (Version != self->_componentSetVersion))
    {
        RematchStates(self);
    }
    // By the states, not the world's list: a rebuild can remove a path object that lies under another's runtime child.
    bool IsAnyGone = false;
    PathObjectState* States = (PathObjectState*)self->_states._data;
    for (size_t Index = 0U; Index < self->_states._count; Index++)
    {
        WorldComponent* Component = NULL;
        if (World_TryGetComponent(self->_world, States[Index].ObjectId, WorldComponentType_PathObject, &Component))
        {
            RefreshComponent(self, &States[Index], WorldPathObjectComponent_FromComponent(Component));
        }
        else
        {
            IsAnyGone = true;
        }
    }
    self->_componentSetVersion = World_GetComponentSetVersion(self->_world);
    self->_hasComponentSetVersion = !IsAnyGone;
}

void WorldPathObjects_SetLimits(WorldPathObjects* self, const WorldPathObjectsLimits* limits)
{
    self->_limits = *limits;
}

WorldPathObjectsStats WorldPathObjects_GetStats(const WorldPathObjects* self)
{
    return self->_stats;
}
