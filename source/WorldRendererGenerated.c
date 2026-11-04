// The GPU's copy of the meshes generators made (WorldGeneratedModel.h): one raylib Model per LOD of each model entry that has
// them, uploaded when the entry's generated model is replaced. The passes draw them as they draw a world model's LODs: the
// entries' WorldRenderModel_GetLodModel hands these out. Each Model borrows its world model's materials, which the entry holds.

#include "WorldRendererInternal.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"


// Static functions.
/* Unloads one model's meshes and frees its arrays. */
static void ReleaseLod(GameModel* lod)
{
    Model* RayModel = &lod->_rayModel;
    for (int Index = 0; Index < RayModel->meshCount; Index++)
    {
        UnloadMesh(RayModel->meshes[Index]); // its CPU arrays are the component's: NULL here, so only the GPU's go
    }
    Memory_Free(RayModel->meshes);
    Memory_Free(RayModel->meshMaterial);
    RayModel->meshes = NULL;
    RayModel->meshMaterial = NULL;
    RayModel->meshCount = 0;
}

static void ReleaseRecord(WorldRendererGeneratedRecord* record)
{
    for (size_t Lod = 0U; Lod < record->_lodCount; Lod++)
    {
        ReleaseLod(&record->_lods[Lod]);
    }
    record->_lodCount = 0U;
}

/* Uploads one mesh; the arrays stay the component's. */
static Mesh UploadMeshCopy(const WorldGeneratedMesh* source)
{
    Mesh Made;
    Memory_Zero(&Made, sizeof(Made));
    Made.vertexCount = (int)source->VertexCount;
    Made.triangleCount = (int)(source->VertexCount / 3U);
    Made.vertices = source->Positions;
    Made.normals = source->Normals;
    Made.tangents = source->Tangents;
    Made.texcoords2 = source->Texcoords2;
    Made.colors = source->Colors;
    // raylib uploads texture coordinates whether it has them or not: they must not be left to chance.
    float* NoTexcoords = NULL;
    if (source->Texcoords == NULL)
    {
        size_t Bytes = (size_t)source->VertexCount * 2U * sizeof(float);
        NoTexcoords = Memory_Allocate(Bytes);
        Memory_Zero(NoTexcoords, Bytes);
    }
    Made.texcoords = (source->Texcoords != NULL) ? source->Texcoords : NoTexcoords;
    UploadMesh(&Made, false);
    Memory_Free(NoTexcoords);
    Made.vertices = NULL;
    Made.normals = NULL;
    Made.tangents = NULL;
    Made.texcoords = NULL;
    Made.texcoords2 = NULL;
    Made.colors = NULL;
    return Made;
}

/* Makes one LOD's model over its world model's materials. */
static void UploadLod(GameModel* outLod, const WorldGeneratedLod* source, Model materials, BoundingBox box)
{
    Memory_Zero(outLod, sizeof(*outLod));
    Model* RayModel = &outLod->_rayModel;
    RayModel->transform = MatrixIdentity();
    RayModel->materialCount = materials.materialCount;
    RayModel->materials = materials.materials;
    RayModel->meshCount = (int)source->MeshCount;
    if (source->MeshCount > 0U)
    {
        RayModel->meshes = Memory_Allocate((size_t)source->MeshCount * sizeof(Mesh));
        RayModel->meshMaterial = Memory_Allocate((size_t)source->MeshCount * sizeof(int));
    }
    for (uint32_t Index = 0U; Index < source->MeshCount; Index++)
    {
        const WorldGeneratedMesh* Held = &source->Meshes[Index];
        RayModel->meshes[Index] = UploadMeshCopy(Held);
        RayModel->meshMaterial[Index] = ((Held->MaterialSlot >= 0) && (Held->MaterialSlot < materials.materialCount))
            ? Held->MaterialSlot : 0;
    }
    outLod->_localBounds = box;
    outLod->_skeleton = NULL;
}

static void UploadRecord(WorldRendererGeneratedRecord* record, const WorldRenderModel* entry)
{
    const WorldGeneratedModel* Generated = entry->Generated;
    record->_serial = entry->GeneratedSerial;
    record->_revision = entry->GeneratedRevision;
    record->_asset = entry->Asset;
    record->_lodCount = Generated->LodCount;
    for (size_t Lod = 0U; Lod < Generated->LodCount; Lod++)
    {
        GameModel* Source = WorldModel_GetLodModel(entry->Asset, Lod);
        Model Materials = (Source != NULL) ? GameModel_GetRaylibModel(Source) : (Model){ 0 };
        UploadLod(&record->_lods[Lod], &Generated->Lods[Lod], Materials, Generated->Box);
        record->_lodPointers[Lod] = &record->_lods[Lod];
    }
}

/* The record of a serial: from @p cursor on (entries and records keep their order), then from the start. NULL for none. */
static WorldRendererGeneratedRecord* FindRecord(WorldRendererGeneratedState* state, uint64_t serial, size_t* inOutCursor)
{
    WorldRendererGeneratedRecord** Records = (WorldRendererGeneratedRecord**)state->_records._data;
    size_t Count = state->_records._count;
    for (size_t Step = 0U; Step < Count; Step++)
    {
        size_t Index = (*inOutCursor + Step) % Count;
        if (Records[Index]->_serial == serial)
        {
            *inOutCursor = Index + 1U;
            return Records[Index];
        }
    }
    return NULL;
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererGenerated_Construct(WorldRenderer* self)
{
    GenericBuffer_AllocateVariable(&self->_generated._records, 0U, sizeof(WorldRendererGeneratedRecord*));
}

void WorldRendererGenerated_Deconstruct(WorldRenderer* self)
{
    WorldRendererGenerated_ReleaseMeshes(self);
    Memory_Free(self->_generated._records._data);
}

void WorldRendererGenerated_ReleaseMeshes(WorldRenderer* self)
{
    WorldRendererGeneratedRecord** Records = (WorldRendererGeneratedRecord**)self->_generated._records._data;
    for (size_t Index = 0U; Index < self->_generated._records._count; Index++)
    {
        ReleaseRecord(Records[Index]);
        Memory_Free(Records[Index]);
    }
    GenericBuffer_Clear(&self->_generated._records);
}

void WorldRendererGenerated_PrepareFrame(WorldRenderer* self)
{
    WorldRendererGeneratedState* State = &self->_generated;
    WorldRendererGeneratedRecord** Records = (WorldRendererGeneratedRecord**)State->_records._data;
    for (size_t Index = 0U; Index < State->_records._count; Index++)
    {
        Records[Index]->_isUsed = false;
    }
    size_t EntryCount = 0U;
    const WorldRenderModel* Entries = WorldRenderCache_GetModels(&self->_cache, &EntryCount);
    size_t Cursor = 0U;
    for (size_t Index = 0U; Index < EntryCount; Index++)
    {
        const WorldRenderModel* Entry = &Entries[Index];
        if ((Entry->Generated == NULL) || (Entry->Asset == NULL))
        {
            continue;
        }
        WorldRendererGeneratedRecord* Record = FindRecord(State, Entry->GeneratedSerial, &Cursor);
        if (Record == NULL)
        {
            Record = Memory_Allocate(sizeof(WorldRendererGeneratedRecord));
            Memory_Zero(Record, sizeof(*Record));
            if (!GenericBuffer_AddLast(&State->_records, &Record))
            {
                Memory_Free(Record);
                continue;
            }
            UploadRecord(Record, Entry);
            Cursor = State->_records._count;
        }
        else if ((Record->_revision != Entry->GeneratedRevision) || (Record->_asset != Entry->Asset))
        {
            ReleaseRecord(Record);
            UploadRecord(Record, Entry);
        }
        Record->_isUsed = true;
        WorldRenderCache_SetGeneratedLods(&self->_cache, Index, Record->_lodPointers);
    }
    // What no entry holds any more goes: every component's generated model is its own, and a new one is a new upload.
    Records = (WorldRendererGeneratedRecord**)State->_records._data;
    for (size_t Index = State->_records._count; Index > 0U; Index--)
    {
        if (!Records[Index - 1U]->_isUsed)
        {
            ReleaseRecord(Records[Index - 1U]);
            Memory_Free(Records[Index - 1U]);
            GenericBuffer_RemoveAt(&State->_records, Index - 1U);
        }
    }
}
