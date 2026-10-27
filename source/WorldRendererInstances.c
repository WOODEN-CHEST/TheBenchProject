// The model instances components' copies: each cache entry's records on the GPU (one vertex buffer per build of
// its cells, uploaded once), and their draws in every geometry pass. A cell is drawn one LOD at a time, only the LODs
// its copies can show (WorldRenderInstanceCell.LodMask): every mesh of the LOD once for all the cell's copies (an
// instanced draw), with the cell's render-space matrix as the model matrix and the records as instanced attributes
// (shaders/common/instancing.glsl), whose vertex stage works out each copy's fade and LOD band and drops the copies
// that draw does not show. Without a pass's instanced twin or a buffer, and in every pass while instancing is off (the
// A/B), each copy is drawn on its own through the pass's ordinary shader, with the copy's values from the C mirror
// (WorldInstancing.c); the unlit fallback (no PBR lighting) draws them so too, with the materials' own shaders.

#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"


// Macros.
/** Asset names of the G-buffer's and the shadows' instanced twins (the lit one, world_pbr_instanced, is the lighting
 *  pass's): each its rigid shader's fragment stage over a vertex stage that places every copy of a cell. */
#define NORMAL_INSTANCED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_normal_instanced")
#define DEPTH_INSTANCED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_depth_instanced")
#define CUBE_DEPTH_INSTANCED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_cube_depth_instanced")
/** The instance attributes' locations (shaders/common/instancing.glsl; agents/WorldRuntime.md, "Vertex attributes"): a
 *  record's three matrix rows, its tint and its random value. */
#define INSTANCE_ROW_LOCATION 9U
#define INSTANCE_ROW_COUNT 3U
#define INSTANCE_TINT_LOCATION 12U
#define INSTANCE_RANDOM_LOCATION 13U
/** Bytes of one matrix row in a record. */
#define INSTANCE_ROW_BYTES (4U * sizeof(float))
/** Initial capacity of the buffer lists. */
#define INITIAL_BUFFER_CAPACITY 8U

_Static_assert(sizeof(WorldInstanceRecord) == 56U, "the instance attributes read 56-byte records");
_Static_assert(offsetof(WorldInstanceRecord, Tint) == (INSTANCE_ROW_COUNT * INSTANCE_ROW_BYTES),
    "the tint follows the three rows");
_Static_assert(offsetof(WorldInstanceRecord, Random) == (offsetof(WorldInstanceRecord, Tint) + 4U),
    "the random value follows the tint");


// Types.
/* How one pass draws the copies of a cell: the pass the C mirror evaluates for; the instanced twin, its uniforms and
 * (in the lit pass) its PBR program, or NULL to draw one at a time; and for the one-at-a-time draws the ordinary
 * shaders, their dither (NULL: solid) and whether the pass is lit. A plain value type; everything in it is borrowed for
 * the pass. */
typedef struct CopyPassStruct
{
    WorldInstancePass Pass;
    const Shader* InstancedShader;
    const WorldRendererInstancingLocations* Instancing;
    const WorldRendererPbrProgram* InstancedLitProgram;
    WorldRendererMeshShaders OrdinaryShaders;
    const WorldRendererDitherLocations* OrdinaryDither;
    bool IsLit;
} CopyPass;


// Static functions.
/* The camera's zoom factor as the visibility takes it: 1 for anything not finite or not above 0. */
static float SanitizeZoomFactor(float zoomFactor)
{
    return (isfinite(zoomFactor) && (zoomFactor > 0.0f)) ? zoomFactor : 1.0f;
}

/* What every copy of an entry needs this frame (the instanced vertex stage's uniforms, the C mirror's view). */
static WorldInstanceView GetView(const WorldRenderInstances* entry, float zoomFactor, float drawDistanceScale)
{
    WorldInstanceView View;
    Memory_Zero(&View, sizeof(View));
    View.ZoomFactor = SanitizeZoomFactor(zoomFactor);
    WorldInstancing_SetDrawDistance(&View, WorldVisibility_ScaleDrawDistance(entry->DrawDistance,
        entry->IsDrawDistanceFixed, drawDistanceScale));
    View.ObjectOpacity = (float)entry->Tint.a / 255.0f;
    View.BoundsCenter = entry->Bounds.Center;
    View.BoundsRadius = entry->Bounds.Radius * entry->WorldStretch;
    View.Bands = entry->Bands;
    return View;
}

/* The model entry the material and the one-at-a-time draws take an instance entry's world model as: its asset, its
 * variant and its tint; no pose. */
static WorldRenderModel MakeCopyModel(const WorldRenderInstances* entry)
{
    WorldRenderModel Copy;
    Memory_Zero(&Copy, sizeof(Copy));
    Copy.ObjectId = entry->ObjectId;
    Copy.Asset = entry->Asset;
    Copy.UsesVariant = entry->UsesVariant;
    Copy.HasVariant = entry->HasVariant;
    Copy.VariantIndex = entry->VariantIndex;
    Copy.IsDrawn = true;
    Copy.Tint = entry->Tint;
    Copy.RenderMatrix = MatrixIdentity();
    return Copy;
}

/* Finds the instanced uniforms of a program and uploads the pass it draws (once: each program draws one pass). */
static void PrepareInstancing(Shader shader, WorldInstancePass pass, WorldRendererInstancingLocations* outLocations)
{
    outLocations->_locLod = GetShaderLocation(shader, "instanceLod");
    outLocations->_locLodCount = GetShaderLocation(shader, "instanceLodCount");
    outLocations->_locForcedLod = GetShaderLocation(shader, "instanceForcedLod");
    outLocations->_locBandStart = GetShaderLocation(shader, "instanceBandStart");
    outLocations->_locBandEnd = GetShaderLocation(shader, "instanceBandEnd");
    outLocations->_locZoomFactor = GetShaderLocation(shader, "instanceZoomFactor");
    outLocations->_locDrawDistance = GetShaderLocation(shader, "instanceDrawDistance");
    outLocations->_locFadeStart = GetShaderLocation(shader, "instanceFadeStart");
    outLocations->_locFadeBand = GetShaderLocation(shader, "instanceFadeBand");
    outLocations->_locObjectOpacity = GetShaderLocation(shader, "instanceObjectOpacity");
    outLocations->_locBoundsCenter = GetShaderLocation(shader, "instanceBoundsCenter");
    outLocations->_locBoundsRadius = GetShaderLocation(shader, "instanceBoundsRadius");
    outLocations->_locLodTransform = GetShaderLocation(shader, "instanceLodTransform");
    int Pass = (int)pass;
    SetShaderValue(shader, GetShaderLocation(shader, "instancePass"), &Pass, SHADER_UNIFORM_INT);
}

/* Loads an instanced twin and prepares its dither and instancing uniforms; NULL when it is unavailable (logged once by
 * the loader). */
static GameShader* LoadTwin(WorldRenderer* self, const unsigned char* assetName, WorldInstancePass pass,
    WorldRendererDitherLocations* outDither, WorldRendererInstancingLocations* outInstancing)
{
    GameShader* Twin = WorldRendererCommon_LoadShader(self, assetName);
    if (Twin != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(Twin);
        WorldRendererCommon_PrepareDither(RayShader, outDither);
        PrepareInstancing(RayShader, pass, outInstancing);
    }
    return Twin;
}

/* Uploads an entry's view to an instanced program, for its cells drawn next. */
static void UploadView(Shader shader, const WorldRendererInstancingLocations* locations, const WorldInstanceView* view)
{
    int LodCount = (int)view->Bands.LodCount;
    int ForcedLod = view->Bands.IsForced ? (int)view->Bands.ForcedLod : -1;
    float BoundsCenter[3] = { view->BoundsCenter.x, view->BoundsCenter.y, view->BoundsCenter.z };
    SetShaderValue(shader, locations->_locLodCount, &LodCount, SHADER_UNIFORM_INT);
    SetShaderValue(shader, locations->_locForcedLod, &ForcedLod, SHADER_UNIFORM_INT);
    SetShaderValueV(shader, locations->_locBandStart, view->Bands.Start, SHADER_UNIFORM_FLOAT,
        (int)WORLD_INSTANCE_MAX_BANDS);
    SetShaderValueV(shader, locations->_locBandEnd, view->Bands.End, SHADER_UNIFORM_FLOAT,
        (int)WORLD_INSTANCE_MAX_BANDS);
    SetShaderValue(shader, locations->_locZoomFactor, &view->ZoomFactor, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locDrawDistance, &view->DrawDistance, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locFadeStart, &view->FadeStart, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locFadeBand, &view->FadeBand, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locObjectOpacity, &view->ObjectOpacity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locBoundsCenter, BoundsCenter, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locBoundsRadius, &view->BoundsRadius, SHADER_UNIFORM_FLOAT);
}

/* Points the instance attributes at a cell's records (from @p firstRecord in @p buffer), one record per copy. The
 * mesh's vertex array must be bound: the pointers are its state. */
static void BindRecords(unsigned int buffer, size_t firstRecord)
{
    int Stride = (int)sizeof(WorldInstanceRecord);
    size_t Offset = firstRecord * sizeof(WorldInstanceRecord);
    rlEnableVertexBuffer(buffer);
    for (unsigned int Row = 0U; Row < INSTANCE_ROW_COUNT; Row++)
    {
        rlSetVertexAttribute(INSTANCE_ROW_LOCATION + Row, 4, RL_FLOAT, false, Stride,
            (int)(Offset + (Row * INSTANCE_ROW_BYTES)));
    }
    rlSetVertexAttribute(INSTANCE_TINT_LOCATION, 4, RL_UNSIGNED_BYTE, true, Stride,
        (int)(Offset + offsetof(WorldInstanceRecord, Tint)));
    rlSetVertexAttribute(INSTANCE_RANDOM_LOCATION, 1, RL_FLOAT, false, Stride,
        (int)(Offset + offsetof(WorldInstanceRecord, Random)));
    for (unsigned int Location = INSTANCE_ROW_LOCATION; Location <= INSTANCE_RANDOM_LOCATION; Location++)
    {
        rlSetVertexAttributeDivisor(Location, 1);
        rlEnableVertexAttribute(Location);
    }
}

/* Takes the instance attributes off the bound vertex array again: disabled, divisor 0 and pointing at no buffer, so the
 * mesh's ordinary draws are untouched and the array keeps no records buffer alive. */
static void UnbindRecords(void)
{
    rlDisableVertexBuffer();
    for (unsigned int Location = INSTANCE_ROW_LOCATION; Location <= INSTANCE_RANDOM_LOCATION; Location++)
    {
        rlDisableVertexAttribute(Location);
        rlSetVertexAttributeDivisor(Location, 0);
        rlSetVertexAttribute(Location, 4, RL_FLOAT, false, 0, 0);
    }
}

/* Draws one mesh once for each of a cell's copies: raylib's DrawMesh (its GL 3.3 path: the material's colours, the
 * matrices with @p transform as the model matrix, the maps on their units), with the cell's records bound as instanced
 * attributes for the draw. False for a mesh without a vertex array (nothing drawn). */
static bool DrawMeshCopies(Mesh mesh, Material material, Matrix transform, unsigned int buffer, size_t firstRecord,
    size_t recordCount)
{
    if (mesh.vaoId == 0U)
    {
        return false;
    }
    rlEnableShader(material.shader.id);
    const int* Locs = material.shader.locs;
    if (Locs[SHADER_LOC_COLOR_DIFFUSE] != -1)
    {
        Color Diffuse = material.maps[MATERIAL_MAP_DIFFUSE].color;
        float Values[4] = { (float)Diffuse.r/255.0f, (float)Diffuse.g/255.0f, (float)Diffuse.b/255.0f,
            (float)Diffuse.a/255.0f };
        rlSetUniform(Locs[SHADER_LOC_COLOR_DIFFUSE], Values, SHADER_UNIFORM_VEC4, 1);
    }
    if (Locs[SHADER_LOC_COLOR_SPECULAR] != -1)
    {
        Color Specular = material.maps[MATERIAL_MAP_SPECULAR].color;
        float Values[4] = { (float)Specular.r/255.0f, (float)Specular.g/255.0f, (float)Specular.b/255.0f,
            (float)Specular.a/255.0f };
        rlSetUniform(Locs[SHADER_LOC_COLOR_SPECULAR], Values, SHADER_UNIFORM_VEC4, 1);
    }

    Matrix MatView = rlGetMatrixModelview();
    Matrix MatProjection = rlGetMatrixProjection();
    if (Locs[SHADER_LOC_MATRIX_VIEW] != -1)
    {
        rlSetUniformMatrix(Locs[SHADER_LOC_MATRIX_VIEW], MatView);
    }
    if (Locs[SHADER_LOC_MATRIX_PROJECTION] != -1)
    {
        rlSetUniformMatrix(Locs[SHADER_LOC_MATRIX_PROJECTION], MatProjection);
    }
    Matrix MatModel = MatrixMultiply(transform, rlGetMatrixTransform());
    if (Locs[SHADER_LOC_MATRIX_MODEL] != -1)
    {
        rlSetUniformMatrix(Locs[SHADER_LOC_MATRIX_MODEL], MatModel);
    }
    if (Locs[SHADER_LOC_MATRIX_NORMAL] != -1)
    {
        rlSetUniformMatrix(Locs[SHADER_LOC_MATRIX_NORMAL], MatrixTranspose(MatrixInvert(MatModel)));
    }

    for (int Map = 0; Map < (int)WORLD_RENDERER_MATERIAL_MAP_COUNT; Map++)
    {
        if (material.maps[Map].texture.id > 0U)
        {
            rlActiveTextureSlot(Map);
            if ((Map == MATERIAL_MAP_IRRADIANCE) || (Map == MATERIAL_MAP_PREFILTER) || (Map == MATERIAL_MAP_CUBEMAP))
            {
                rlEnableTextureCubemap(material.maps[Map].texture.id);
            }
            else
            {
                rlEnableTexture(material.maps[Map].texture.id);
            }
            rlSetUniform(Locs[SHADER_LOC_MAP_DIFFUSE + Map], &Map, SHADER_UNIFORM_INT, 1);
        }
    }

    rlEnableVertexArray(mesh.vaoId);
    BindRecords(buffer, firstRecord);
    rlSetUniformMatrix(Locs[SHADER_LOC_MATRIX_MVP], MatrixMultiply(MatrixMultiply(MatModel, MatView), MatProjection));
    if (mesh.indices != NULL)
    {
        rlDrawVertexArrayElementsInstanced(0, mesh.triangleCount*3, 0, (int)recordCount);
    }
    else
    {
        rlDrawVertexArrayInstanced(0, mesh.vertexCount, (int)recordCount);
    }
    UnbindRecords();

    for (int Map = 0; Map < (int)WORLD_RENDERER_MATERIAL_MAP_COUNT; Map++)
    {
        if (material.maps[Map].texture.id > 0U)
        {
            rlActiveTextureSlot(Map);
            if ((Map == MATERIAL_MAP_IRRADIANCE) || (Map == MATERIAL_MAP_PREFILTER) || (Map == MATERIAL_MAP_CUBEMAP))
            {
                rlDisableTextureCubemap();
            }
            else
            {
                rlDisableTexture();
            }
        }
    }
    // The vertex array first: the element buffer's binding is its state.
    rlDisableVertexArray();
    rlDisableVertexBuffer();
    rlDisableVertexBufferElement();
    rlDisableShader();
    rlSetMatrixModelview(MatView);
    rlSetMatrixProjection(MatProjection);
    return true;
}

/* Draws one LOD of a cell instanced: the LOD's import transform uploaded, then each mesh with its material copy, once
 * for all the cell's copies (the vertex stage drops those this LOD does not show). */
static void DrawCellLodInstanced(WorldRenderer* self, const CopyPass* pass, const WorldRenderModel* model, size_t lod,
    const WorldRenderInstanceCell* cell, unsigned int buffer)
{
    GameModel* LodModel = WorldRenderModel_GetLodModel(model, lod);
    if (LodModel == NULL)
    {
        return;
    }
    Model RayModel = GameModel_GetRaylibModel(LodModel);
    int Lod = (int)lod;
    SetShaderValue(*pass->InstancedShader, pass->Instancing->_locLod, &Lod, SHADER_UNIFORM_INT);
    SetShaderValueMatrix(*pass->InstancedShader, pass->Instancing->_locLodTransform, RayModel.transform);
    for (int MeshIndex = 0; MeshIndex < RayModel.meshCount; MeshIndex++)
    {
        MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
        Material DrawMaterial = WorldRendererGeometry_PrepareMaterial(self, model, RayModel, MeshIndex, lod,
            pass->InstancedShader, pass->InstancedLitProgram, Maps);
        if (!DrawMeshCopies(RayModel.meshes[MeshIndex], DrawMaterial, cell->RenderMatrix, buffer, cell->FirstRecord,
            cell->RecordCount))
        {
            WorldRendererCommon_WarnOnce(self, &self->_instances._isVertexArrayFailureReported, (const unsigned char*)
                u8"WorldRenderer: a world model's mesh has no vertex array, so its instanced copies are not drawn.");
        }
    }
}

/* Draws each of a cell's copies on its own through the pass's ordinary shaders: per copy its state from the C mirror,
 * then every LOD of the cell's mask the pass draws it with, dithered as the instanced draw would. The copy's tint
 * multiplies the object's (its opacity is in the fade). */
static void DrawCellCopies(WorldRenderer* self, const CopyPass* pass, const WorldRenderInstances* entry,
    const WorldRenderInstanceCell* cell, const WorldInstanceView* view)
{
    size_t RecordCount = 0U;
    const WorldInstanceRecord* Records = WorldInstanceCells_GetRecords(entry->Cells, &RecordCount);
    if ((Records == NULL) || ((cell->FirstRecord + cell->RecordCount) > RecordCount))
    {
        return;
    }
    WorldRenderModel Copy = MakeCopyModel(entry);
    for (size_t Record = cell->FirstRecord; Record < (cell->FirstRecord + cell->RecordCount); Record++)
    {
        const WorldInstanceRecord* Source = &Records[Record];
        WorldInstanceState State = WorldInstancing_Evaluate(view, &cell->RenderMatrix, Source);
        Copy.RenderMatrix = MatrixMultiply(WorldInstanceRecord_GetMatrix(Source), cell->RenderMatrix);
        Copy.Tint = WorldRendererGeometry_MultiplyColor(entry->Tint,
            (Color){ Source->Tint.r, Source->Tint.g, Source->Tint.b, 255 });
        for (size_t Lod = 0U; Lod < view->Bands.LodCount; Lod++)
        {
            WorldInstanceDraw Draw = WorldInstancing_GetDraw(&State, pass->Pass, Lod);
            if (!Draw.IsDrawn || ((cell->LodMask & ((uint32_t)1U << Lod)) == 0U))
            {
                continue;
            }
            if ((pass->OrdinaryDither != NULL) && (pass->OrdinaryShaders.Rigid != NULL))
            {
                WorldRendererCommon_SetDither(*pass->OrdinaryShaders.Rigid, pass->OrdinaryDither, Draw.Fade,
                    Draw.Progress, Draw.IsIncoming);
            }
            WorldRendererGeometry_DrawModel(self, &Copy, Lod, &pass->OrdinaryShaders, pass->IsLit);
        }
    }
}

/* This frame's records buffer of a cache entry; 0 when it has none (it then draws one at a time). */
static unsigned int GetEntryBuffer(const WorldRenderer* self, size_t entryIndex)
{
    const unsigned int* Buffers = (const unsigned int*)self->_instances._entryBuffers._data;
    return (entryIndex < self->_instances._entryBuffers._count) ? Buffers[entryIndex] : 0U;
}

/* Draws one cell's copies in a pass: instanced while the pass has its twin and the entry its buffer, one at a time
 * otherwise. Returns whether it drew instanced. */
static bool DrawCell(WorldRenderer* self, const CopyPass* pass, const WorldRenderInstanceCell* cell, float zoomFactor,
    float drawDistanceScale)
{
    size_t EntryCount = 0U;
    const WorldRenderInstances* Entries = WorldRenderCache_GetInstances(&self->_cache, &EntryCount);
    if ((cell->Entry >= EntryCount) || (Entries[cell->Entry].Asset == NULL) || (Entries[cell->Entry].Cells == NULL))
    {
        return false;
    }
    const WorldRenderInstances* Entry = &Entries[cell->Entry];
    WorldInstanceView View = GetView(Entry, zoomFactor, drawDistanceScale);
    unsigned int Buffer = GetEntryBuffer(self, cell->Entry);
    if ((pass->InstancedShader == NULL) || (Buffer == 0U))
    {
        DrawCellCopies(self, pass, Entry, cell, &View);
        return false;
    }
    UploadView(*pass->InstancedShader, pass->Instancing, &View);
    WorldRenderModel Placement = MakeCopyModel(Entry);
    for (size_t Lod = 0U; Lod < View.Bands.LodCount; Lod++)
    {
        if ((cell->LodMask & ((uint32_t)1U << Lod)) != 0U)
        {
            DrawCellLodInstanced(self, pass, &Placement, Lod, cell, Buffer);
        }
    }
    return true;
}

/* Finds the buffer holding a build, looking at @p cursor first (the buffers follow the entries' order) and moving it
 * past a match; SIZE_MAX when none does. */
static size_t FindBuffer(const WorldRendererInstanceState* state, uint64_t buildId, size_t* cursor)
{
    const WorldRendererInstanceBuffer* Buffers = (const WorldRendererInstanceBuffer*)state->_buffers._data;
    size_t Count = state->_buffers._count;
    if ((*cursor < Count) && (Buffers[*cursor]._buildId == buildId))
    {
        return (*cursor)++;
    }
    for (size_t Index = 0U; Index < Count; Index++)
    {
        if (Buffers[Index]._buildId == buildId)
        {
            *cursor = Index + 1U;
            return Index;
        }
    }
    return SIZE_MAX;
}

/* Uploads a build's records into a new buffer, appended to the list (one with buffer 0 when raylib could not make one,
 * which it reports only without a GL context: logged once, and not retried every frame; a GL_OUT_OF_MEMORY from the
 * upload goes unseen, as for every raylib upload); SIZE_MAX when the list could not grow. */
static size_t UploadBuffer(WorldRenderer* self, uint64_t buildId, const WorldInstanceRecord* records, size_t count)
{
    WorldRendererInstanceBuffer Buffer = { ._buildId = buildId, ._buffer = 0U, ._recordCount = count, ._isUsed = true };
    Buffer._buffer = rlLoadVertexBuffer(records, (int)(count * sizeof(WorldInstanceRecord)), false);
    rlDisableVertexBuffer();
    if (Buffer._buffer == 0U)
    {
        WorldRendererCommon_WarnOnce(self, &self->_instances._isUploadFailureReported, (const unsigned char*)
            u8"WorldRenderer: could not make a buffer for a model instances component's copies; they draw one at a "
            u8"time.");
    }
    if (!GenericBuffer_AddLast(&self->_instances._buffers, &Buffer))
    {
        if (Buffer._buffer != 0U)
        {
            rlUnloadVertexBuffer(Buffer._buffer);
        }
        return SIZE_MAX;
    }
    return self->_instances._buffers._count - 1U;
}

/* Unloads the buffers no entry held this frame and closes the gaps. */
static void ReleaseUnusedBuffers(WorldRendererInstanceState* state)
{
    WorldRendererInstanceBuffer* Buffers = (WorldRendererInstanceBuffer*)state->_buffers._data;
    size_t Kept = 0U;
    for (size_t Index = 0U; Index < state->_buffers._count; Index++)
    {
        if (!Buffers[Index]._isUsed)
        {
            if (Buffers[Index]._buffer != 0U)
            {
                rlUnloadVertexBuffer(Buffers[Index]._buffer);
            }
            continue;
        }
        Buffers[Kept++] = Buffers[Index];
    }
    GenericBuffer_SetCount(&state->_buffers, Kept);
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererInstances_Construct(WorldRenderer* self)
{
    GenericBuffer_AllocateVariable(&self->_instances._buffers, INITIAL_BUFFER_CAPACITY,
        sizeof(WorldRendererInstanceBuffer));
    GenericBuffer_AllocateVariable(&self->_instances._entryBuffers, INITIAL_BUFFER_CAPACITY, sizeof(unsigned int));
    self->_instances._isEnabled = true;
    self->_instances._lastZoomFactor = 1.0f;
    self->_instances._lastDrawDistanceScale = 1.0f;
}

void WorldRendererInstances_Deconstruct(WorldRenderer* self)
{
    WorldRendererInstances_ReleaseBuffers(self);
    Memory_Free(self->_instances._buffers._data);
    Memory_Free(self->_instances._entryBuffers._data);
}

void WorldRendererInstances_LoadShaders(WorldRenderer* self)
{
    WorldRendererInstanceState* State = &self->_instances;
    if (self->_pbr._instanced._shader != NULL)
    {
        PrepareInstancing(GameShader_GetRaylibShader(self->_pbr._instanced._shader), WorldInstancePass_Lit,
            &State->_pbrInstancing);
    }
    // Each twin only beside its ordinary shader: without that, the pass does not run.
    State->_normalShader = (self->_gbuffer._shader != NULL) ? LoadTwin(self, NORMAL_INSTANCED_SHADER_ASSET_NAME,
        WorldInstancePass_GBuffer, &State->_normalDither, &State->_normalInstancing) : NULL;
    if (State->_normalShader != NULL)
    {
        State->_normalLocOutlineFlag = GetShaderLocation(GameShader_GetRaylibShader(State->_normalShader),
            "outlineFlag");
        State->_normalLocHazeMultiplier = GetShaderLocation(GameShader_GetRaylibShader(State->_normalShader),
            "hazeMultiplier");
    }
    State->_depthShader = (self->_cascades._depthShader != NULL) ? LoadTwin(self, DEPTH_INSTANCED_SHADER_ASSET_NAME,
        WorldInstancePass_Shadow, &State->_depthDither, &State->_depthInstancing) : NULL;
    State->_cubeDepthShader = (self->_pointShadow._shader != NULL) ? LoadTwin(self,
        CUBE_DEPTH_INSTANCED_SHADER_ASSET_NAME, WorldInstancePass_Shadow, &State->_cubeDither, &State->_cubeInstancing)
        : NULL;
    if (State->_cubeDepthShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(State->_cubeDepthShader);
        State->_cubeLocLightPos = GetShaderLocation(RayShader, "lightPosition");
        State->_cubeLocLightFar = GetShaderLocation(RayShader, "lightFar");
    }
}

void WorldRendererInstances_ReleaseBuffers(WorldRenderer* self)
{
    WorldRendererInstanceBuffer* Buffers = (WorldRendererInstanceBuffer*)self->_instances._buffers._data;
    for (size_t Index = 0U; Index < self->_instances._buffers._count; Index++)
    {
        if (Buffers[Index]._buffer != 0U)
        {
            rlUnloadVertexBuffer(Buffers[Index]._buffer);
        }
    }
    GenericBuffer_Clear(&self->_instances._buffers);
    GenericBuffer_Clear(&self->_instances._entryBuffers);
}

void WorldRendererInstances_PrepareFrame(WorldRenderer* self)
{
    WorldRendererInstanceState* State = &self->_instances;
    State->_wereCellsDrawnInstanced = false;
    WorldRendererInstanceBuffer* Buffers = (WorldRendererInstanceBuffer*)State->_buffers._data;
    for (size_t Index = 0U; Index < State->_buffers._count; Index++)
    {
        Buffers[Index]._isUsed = false;
    }
    GenericBuffer_Clear(&State->_entryBuffers);

    size_t EntryCount = 0U;
    const WorldRenderInstances* Entries = WorldRenderCache_GetInstances(&self->_cache, &EntryCount);
    size_t Cursor = 0U;
    for (size_t Index = 0U; Index < EntryCount; Index++)
    {
        // Every entry with built cells keeps its buffer, drawn or not, so hiding an object does not upload it again.
        const WorldRenderInstances* Entry = &Entries[Index];
        size_t RecordCount = 0U;
        const WorldInstanceRecord* Records = (Entry->AreCellsBuilt && (Entry->Cells != NULL))
            ? WorldInstanceCells_GetRecords(Entry->Cells, &RecordCount) : NULL;
        unsigned int GlBuffer = 0U;
        if (Records != NULL)
        {
            size_t Found = FindBuffer(State, Entry->CellsBuildId, &Cursor);
            if (Found == SIZE_MAX)
            {
                Found = UploadBuffer(self, Entry->CellsBuildId, Records, RecordCount);
                Cursor = State->_buffers._count;
            }
            if (Found != SIZE_MAX)
            {
                WorldRendererInstanceBuffer* Held = &((WorldRendererInstanceBuffer*)State->_buffers._data)[Found];
                Held->_isUsed = true;
                GlBuffer = Held->_buffer;
            }
        }
        // Without its slot an entry draws one at a time (GetEntryBuffer gives 0 past the list).
        if (!GenericBuffer_AddLast(&State->_entryBuffers, &GlBuffer))
        {
            break;
        }
    }
    ReleaseUnusedBuffers(State);
}

void WorldRendererInstances_DrawLit(WorldRenderer* self, const WorldRenderFrame* frame, WorldRendererPixelFilter filter)
{
    float DrawDistanceScale = WorldRendererCommon_ConfigDrawDistanceScale(self);
    self->_instances._lastZoomFactor = frame->ZoomFactor;
    self->_instances._lastDrawDistanceScale = DrawDistanceScale;
    if (filter == WorldRendererPixelFilter_CrispOnly)
    {
        return; // copies are never crisp
    }
    bool IsLit = (self->_pbr._rigid._shader != NULL);
    bool IsInstanced = IsLit && (self->_pbr._instanced._shader != NULL) && self->_instances._isEnabled;
    CopyPass Pass =
    {
        .Pass = IsLit ? WorldInstancePass_Lit : WorldInstancePass_Unlit,
        .InstancedShader = IsInstanced ? &self->_pbr._instanced._passShader : NULL,
        .Instancing = &self->_instances._pbrInstancing,
        .InstancedLitProgram = &self->_pbr._instanced,
        .OrdinaryShaders = { .Rigid = IsLit ? &self->_pbr._rigid._passShader : NULL, .Skinned = NULL, .Palette = NULL,
            .IsCameraView = true },
        .OrdinaryDither = IsLit ? &self->_pbr._rigid._dither : NULL,
        .IsLit = IsLit
    };
    size_t EntryCount = 0U;
    const WorldRenderInstances* Entries = WorldRenderCache_GetInstances(&self->_cache, &EntryCount);
    size_t CellCount = 0U;
    const WorldRenderInstanceCell* Cells = WorldRenderCache_GetInstanceCells(&self->_cache, &CellCount);
    size_t VisibleCount = 0U;
    const size_t* Visible = WorldVisibility_GetInstanceCells(&self->_visibility, &self->_cache,
        WorldVisibilityPass_Camera, &VisibleCount);
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        const WorldRenderInstanceCell* Cell = &Cells[Visible[Position]];
        // The cell's forward lights and ambient zones by its sphere and its component's haze multiplier, into the
        // program that draws it (the twin, or world_pbr one at a time).
        if (IsLit)
        {
            const WorldRendererPbrProgram* Program = (IsInstanced && (GetEntryBuffer(self, Cell->Entry) != 0U))
                ? &self->_pbr._instanced : &self->_pbr._rigid;
            WorldRendererLighting_UploadObjectLights(self, Program->_passShader, &Program->_lit, Cell->RenderCenter,
                Cell->Radius);
            WorldRendererLighting_SetDrawPlace(self, Program->_passShader, &Program->_lit, Cell->RenderCenter,
                Cell->Radius, (Cell->Entry < EntryCount) ? Entries[Cell->Entry].Haze : 1.0f);
        }
        if (DrawCell(self, &Pass, Cell, frame->ZoomFactor, DrawDistanceScale))
        {
            self->_instances._wereCellsDrawnInstanced = true;
        }
    }
}

void WorldRendererInstances_DrawNormals(WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    int locOutlineFlag, int locHazeMultiplier)
{
    WorldRendererInstanceState* State = &self->_instances;
    bool IsInstanced = (State->_normalShader != NULL) && State->_isEnabled;
    Shader Twin = IsInstanced ? GameShader_GetRaylibShader(State->_normalShader) : shader;
    CopyPass Pass =
    {
        .Pass = WorldInstancePass_GBuffer,
        .InstancedShader = IsInstanced ? &Twin : NULL,
        .Instancing = &State->_normalInstancing,
        .InstancedLitProgram = NULL,
        .OrdinaryShaders = { .Rigid = &shader, .Skinned = NULL, .Palette = NULL, .IsCameraView = true },
        .OrdinaryDither = NULL, // solid: the G-buffer's model shader keeps its fade at 0
        .IsLit = false
    };
    float DrawDistanceScale = WorldRendererCommon_ConfigDrawDistanceScale(self);
    size_t EntryCount = 0U;
    const WorldRenderInstances* Entries = WorldRenderCache_GetInstances(&self->_cache, &EntryCount);
    size_t CellCount = 0U;
    const WorldRenderInstanceCell* Cells = WorldRenderCache_GetInstanceCells(&self->_cache, &CellCount);
    size_t VisibleCount = 0U;
    const size_t* Visible = WorldVisibility_GetInstanceCells(&self->_visibility, &self->_cache,
        WorldVisibilityPass_Camera, &VisibleCount);
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        size_t Index = Visible[Position];
        const WorldRenderInstanceCell* Cell = &Cells[Index];
        // No copy of a cell can be nearer, or more opaque, than the cell's sphere: past the limit none would be drawn.
        if (!(WorldVisibility_GetInstanceCellFade(&self->_visibility, Index).Combined
            < WORLD_VISIBILITY_GBUFFER_FADE_LIMIT) || (Cell->Entry >= EntryCount))
        {
            continue;
        }
        float OutlineFlag = Entries[Cell->Entry].HasOutline ? 1.0f : 0.0f;
        SetShaderValue(shader, locOutlineFlag, &OutlineFlag, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locHazeMultiplier, &Entries[Cell->Entry].Haze, SHADER_UNIFORM_FLOAT);
        if (IsInstanced)
        {
            SetShaderValue(Twin, State->_normalLocOutlineFlag, &OutlineFlag, SHADER_UNIFORM_FLOAT);
            SetShaderValue(Twin, State->_normalLocHazeMultiplier, &Entries[Cell->Entry].Haze, SHADER_UNIFORM_FLOAT);
        }
        DrawCell(self, &Pass, Cell, frame->ZoomFactor, DrawDistanceScale);
    }
}

void WorldRendererInstances_DrawCasters(WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererDitherLocations* dither, WorldVisibilityPass pass)
{
    WorldRendererInstanceState* State = &self->_instances;
    bool IsCube = (pass == WorldVisibilityPass_PointShadow);
    GameShader* TwinAsset = IsCube ? State->_cubeDepthShader : State->_depthShader;
    bool IsInstanced = (TwinAsset != NULL) && State->_isEnabled;
    Shader Twin = IsInstanced ? GameShader_GetRaylibShader(TwinAsset) : shader;
    if (IsInstanced && IsCube)
    {
        float LightPosition[3] = { frame->PointShadowPosition.x, frame->PointShadowPosition.y,
            frame->PointShadowPosition.z };
        SetShaderValue(Twin, State->_cubeLocLightPos, LightPosition, SHADER_UNIFORM_VEC3);
        SetShaderValue(Twin, State->_cubeLocLightFar, &frame->PointShadowFar, SHADER_UNIFORM_FLOAT);
    }
    CopyPass Pass =
    {
        .Pass = WorldInstancePass_Shadow,
        .InstancedShader = IsInstanced ? &Twin : NULL,
        .Instancing = IsCube ? &State->_cubeInstancing : &State->_depthInstancing,
        .InstancedLitProgram = NULL,
        .OrdinaryShaders = { .Rigid = &shader, .Skinned = NULL, .Palette = NULL, .IsCameraView = false },
        .OrdinaryDither = dither,
        .IsLit = false
    };
    float DrawDistanceScale = WorldRendererCommon_ConfigDrawDistanceScale(self);
    size_t CellCount = 0U;
    const WorldRenderInstanceCell* Cells = WorldRenderCache_GetInstanceCells(&self->_cache, &CellCount);
    size_t CasterCount = 0U;
    const size_t* Casters = WorldVisibility_GetInstanceCells(&self->_visibility, &self->_cache, pass, &CasterCount);
    for (size_t Position = 0U; Position < CasterCount; Position++)
    {
        DrawCell(self, &Pass, &Cells[Casters[Position]], frame->ZoomFactor, DrawDistanceScale);
    }
}

WorldRendererFeatureState WorldRendererInstances_GetState(const WorldRenderer* self)
{
    if ((self->_pbr._rigid._shader == NULL) || (self->_pbr._instanced._shader == NULL))
    {
        return WorldRendererFeatureState_Unavailable;
    }
    return self->_instances._wereCellsDrawnInstanced ? WorldRendererFeatureState_Active
        : WorldRendererFeatureState_Inactive;
}

void WorldRendererInstances_LogStatus(const WorldRenderer* self)
{
    if (self->_logger == NULL)
    {
        return;
    }
    size_t EntryCount = 0U;
    const WorldRenderInstances* Entries = WorldRenderCache_GetInstances(&self->_cache, &EntryCount);
    if (EntryCount == 0U)
    {
        return;
    }
    size_t CellCount = 0U;
    const WorldRenderInstanceCell* Cells = WorldRenderCache_GetInstanceCells(&self->_cache, &CellCount);
    size_t VisibleCount = 0U;
    const size_t* Visible = WorldVisibility_GetInstanceCells(&self->_visibility, &self->_cache,
        WorldVisibilityPass_Camera, &VisibleCount);

    // The camera's copies as the lit pass shows them: by the LOD with the larger share, those in a band, those faded
    // out.
    size_t ByLod[WORLD_MODEL_MAX_LODS] = { 0U };
    size_t InBand = 0U;
    size_t FadedOut = 0U;
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        const WorldRenderInstanceCell* Cell = &Cells[Visible[Position]];
        if (Cell->Entry >= EntryCount)
        {
            continue;
        }
        const WorldRenderInstances* Entry = &Entries[Cell->Entry];
        size_t RecordCount = 0U;
        const WorldInstanceRecord* Records = (Entry->Cells != NULL)
            ? WorldInstanceCells_GetRecords(Entry->Cells, &RecordCount) : NULL;
        if ((Records == NULL) || ((Cell->FirstRecord + Cell->RecordCount) > RecordCount))
        {
            continue;
        }
        WorldInstanceView View = GetView(Entry, self->_instances._lastZoomFactor,
            self->_instances._lastDrawDistanceScale);
        for (size_t Record = Cell->FirstRecord; Record < (Cell->FirstRecord + Cell->RecordCount); Record++)
        {
            WorldInstanceState State = WorldInstancing_Evaluate(&View, &Cell->RenderMatrix, &Records[Record]);
            if (!(State.Fade < 1.0f))
            {
                FadedOut++;
                continue;
            }
            size_t Lod = WorldInstancing_GetSolidLod(&State);
            ByLod[(Lod < WORLD_MODEL_MAX_LODS) ? Lod : (WORLD_MODEL_MAX_LODS - 1U)]++;
            InBand += State.IsInBand ? 1U : 0U;
        }
    }
    size_t LastLod = 0U;
    for (size_t Lod = 0U; Lod < WORLD_MODEL_MAX_LODS; Lod++)
    {
        LastLod = (ByLod[Lod] > 0U) ? Lod : LastLod;
    }
    char LodText[WORLD_MODEL_MAX_LODS * 24U];
    size_t Length = 0U;
    LodText[0] = '\0';
    for (size_t Lod = 0U; (Lod <= LastLod) && (Length < sizeof(LodText)); Lod++)
    {
        int Written = snprintf(&LodText[Length], sizeof(LodText) - Length, "%s%" PRIu64, (Lod == 0U) ? "" : " / ",
            (uint64_t)ByLod[Lod]);
        Length += (Written > 0) ? (size_t)Written : 0U;
    }

    const WorldRendererStatus* Status = &self->_status;
    const char* How = "one at a time";
    if (Status->Features[WorldRendererFeature_Instancing] == WorldRendererFeatureState_Active)
    {
        How = "instanced";
    }
    else if (!Status->IsInstancingEnabled)
    {
        How = "one at a time: instancing off";
    }
    Error LogResult = Logger_LogInfoFormatted(self->_logger, (const unsigned char*)
        u8"WorldRenderer: instances: %" PRIu64 u8" components (%" PRIu64 u8" without a world model), %" PRIu64
        u8" copies in %" PRIu64 u8" cells; drawn %" PRIu64 u8" cells with %" PRIu64 u8" copies (%s), %" PRIu64
        u8" cells outside the view, %" PRIu64 u8" faded out; the camera's copies by LOD %s (%" PRIu64 u8" in a band, %"
        PRIu64 u8" faded out).",
        (uint64_t)Status->InstanceComponentCount, (uint64_t)Status->InstanceComponentWithoutAssetCount,
        (uint64_t)Status->InstanceCount, (uint64_t)Status->InstanceCellCount, (uint64_t)Status->DrawnInstanceCellCount,
        (uint64_t)Status->DrawnInstanceCount, How,
        (uint64_t)Status->InstanceCellsOutsideViewCount, (uint64_t)Status->InstanceCellsFadedOutCount, LodText,
        (uint64_t)InBand, (uint64_t)FadedOut);
    Error_Deconstruct(&LogResult);
}
