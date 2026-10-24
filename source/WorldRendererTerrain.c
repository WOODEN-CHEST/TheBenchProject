// The terrain's draws (B01): each chunk of the render cache's terrains as GPU meshes the renderer keeps (the terrain asset
// stays CPU data, which render only reads), uploaded again when a chunk's revision changes; the lit terrain shader in the
// scene pass, the shared G-buffer and depth shaders in the others, and the chunk key (each chunk tinted by its cell, and
// the wireframe). A chunk's vertices are its surface's (TerrainChunk_GetPositions: relative to its centre, which its
// render-space matrix places), cut into parts of at most 65536 vertices: raylib's indices are unsigned shorts.
// TerrainMesh_SplitChunkSurface cuts them as it does for collision.

#include "WorldRendererInternal.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"


// Macros.
/** Asset name of the lit terrain shader. */
#define TERRAIN_SHADER_ASSET_NAME ((const unsigned char*)u8"world_terrain")
/** The most vertices one part holds: raylib's mesh indices are unsigned shorts. */
#define PART_MAX_VERTICES ((uint32_t)UINT16_MAX + 1U)
/** The first attribute location of the weights (raylib's instance matrix's: a terrain is never instanced), and how many
 *  locations they take (four palette weights each). */
#define WEIGHT_ATTRIBUTE_LOCATION 9U
#define WEIGHT_ATTRIBUTE_COUNT 4U
/** How much of the chunk key's colour shows in the lit terrain. */
#define CHUNK_KEY_STRENGTH 0.6f
/** The key's wireframe: its colour (linear HDR) and how far it is drawn toward the camera (a scale about the camera, at
 *  the origin of render space), so it lies over the surface it outlines. */
#define WIREFRAME_COLOR ((Color){ 16, 16, 16, 255 })
#define WIREFRAME_SCALE 0.999f
/** The unlit fallback's colour (multiplied by the object's tint): a plain green-grey ground. */
#define UNLIT_COLOR ((Color){ 110, 130, 90, 255 })
/** The chunk key's colours, by the cell's X and Z each modulo 3: neighbouring chunks always differ. */
#define CHUNK_KEY_COLOR_COUNT 9U


// Fields.
static const Color ChunkKeyColors[CHUNK_KEY_COLOR_COUNT] =
{
    { 230, 80, 70, 255 }, { 80, 200, 90, 255 }, { 70, 120, 230, 255 },
    { 235, 200, 60, 255 }, { 190, 90, 220, 255 }, { 60, 210, 210, 255 },
    { 240, 140, 50, 255 }, { 150, 150, 150, 255 }, { 250, 120, 180, 255 }
};


// Static functions: GPU meshes.
static void FreeChunkMesh(WorldRendererTerrainChunkMesh* mesh)
{
    for (uint32_t Part = 0U; Part < mesh->_partCount; Part++)
    {
        rlUnloadVertexBuffer(mesh->_parts[Part]._weightBuffer);
        // Its index array is ours (UploadPart): freed here, not by raylib.
        Memory_Free(mesh->_parts[Part]._mesh.indices);
        mesh->_parts[Part]._mesh.indices = NULL;
        UnloadMesh(mesh->_parts[Part]._mesh);
    }
    Memory_Free(mesh->_parts);
    mesh->_parts = NULL;
    mesh->_partCount = 0U;
    mesh->_revision = 0U;
}

static void FreeMeshes(WorldRendererTerrainMeshes* meshes)
{
    for (uint32_t Chunk = 0U; Chunk < meshes->_chunkCount; Chunk++)
    {
        FreeChunkMesh(&meshes->_chunks[Chunk]);
    }
    Memory_Free(meshes->_chunks);
    meshes->_chunks = NULL;
    meshes->_chunkCount = 0U;
}

/* Uploads one part: its vertices' positions (relative to the chunk's centre), normals (their terrain vertices') and
 * weights (their terrain vertices' painted ones, or a face material's alone), and its triangles. */
static void UploadPart(const TerrainMesh* mesh, const TerrainChunk* chunk, const TerrainChunkPart* cut,
    const uint32_t* sources, const uint32_t* locals, WorldRendererTerrainPart* outPart)
{
    uint32_t VertexCount = cut->VertexCount;
    float* Positions = Memory_Allocate((size_t)VertexCount * 3U * sizeof(float));
    float* Normals = Memory_Allocate((size_t)VertexCount * 3U * sizeof(float));
    uint8_t* Weights = Memory_Allocate((size_t)VertexCount * TERRAIN_MAX_MATERIALS);
    unsigned short* Indices = Memory_Allocate((size_t)cut->TriangleCount * 3U * sizeof(unsigned short));
    const Vector3* ChunkPositions = TerrainChunk_GetPositions(chunk);
    const uint32_t* VertexSources = TerrainChunk_GetVertexSources(chunk);
    const uint8_t* VertexMaterials = TerrainChunk_GetVertexMaterials(chunk);
    for (uint32_t Vertex = 0U; Vertex < VertexCount; Vertex++)
    {
        uint32_t Surface = sources[cut->FirstVertex + Vertex];
        uint32_t Terrain = VertexSources[Surface];
        Vector3 Position = ChunkPositions[Surface];
        Vector3 Normal = TerrainMesh_GetNormal(mesh, Terrain);
        Positions[(Vertex * 3U) + 0U] = Position.x;
        Positions[(Vertex * 3U) + 1U] = Position.y;
        Positions[(Vertex * 3U) + 2U] = Position.z;
        Normals[(Vertex * 3U) + 0U] = Normal.x;
        Normals[(Vertex * 3U) + 1U] = Normal.y;
        Normals[(Vertex * 3U) + 2U] = Normal.z;
        uint8_t* Own = &Weights[(size_t)Vertex * TERRAIN_MAX_MATERIALS];
        uint8_t FaceMaterial = VertexMaterials[Surface];
        if (FaceMaterial == TERRAIN_CHUNK_BLEND_MATERIAL)
        {
            Memory_Copy(TerrainMesh_GetWeights(mesh, Terrain), Own, TERRAIN_MAX_MATERIALS);
        }
        else
        {
            Memory_Zero(Own, TERRAIN_MAX_MATERIALS);
            Own[FaceMaterial] = UINT8_MAX;
        }
    }
    for (uint32_t Index = 0U; Index < (cut->TriangleCount * 3U); Index++)
    {
        Indices[Index] = (unsigned short)locals[((size_t)cut->FirstTriangle * 3U) + Index];
    }
    Mesh Part = { 0 };
    Part.vertexCount = (int)VertexCount;
    Part.triangleCount = (int)cut->TriangleCount;
    Part.vertices = Positions;
    Part.normals = Normals;
    Part.indices = Indices;
    UploadMesh(&Part, false);
    // The weights join the part's vertex array at their own locations.
    rlEnableVertexArray(Part.vaoId);
    outPart->_weightBuffer = rlLoadVertexBuffer(Weights, (int)(VertexCount * TERRAIN_MAX_MATERIALS), false);
    for (uint32_t Attribute = 0U; Attribute < WEIGHT_ATTRIBUTE_COUNT; Attribute++)
    {
        rlSetVertexAttribute(WEIGHT_ATTRIBUTE_LOCATION + Attribute, 4, RL_UNSIGNED_BYTE, true,
            (int)TERRAIN_MAX_MATERIALS, (int)(Attribute * 4U));
        rlEnableVertexAttribute(WEIGHT_ATTRIBUTE_LOCATION + Attribute);
    }
    rlDisableVertexArray();
    // The GPU has them now: the CPU arrays go, except the indices: raylib's DrawMesh draws a mesh whose index array is
    // gone as if it had none (each three vertices in a row a triangle). FreeChunkMesh frees them.
    Memory_Free(Positions);
    Memory_Free(Normals);
    Memory_Free(Weights);
    Part.vertices = NULL;
    Part.normals = NULL;
    outPart->_mesh = Part;
}

/* Builds a chunk's GPU mesh from the terrain's chunk (replacing the one it had). */
static void BuildChunkMesh(const TerrainMesh* mesh, uint32_t chunk, WorldRendererTerrainChunkMesh* outMesh)
{
    FreeChunkMesh(outMesh);
    const TerrainChunk* Source = TerrainMesh_GetChunk(mesh, chunk);
    outMesh->_revision = TerrainChunk_GetRevision(Source);
    GenericBuffer Parts;
    GenericBuffer_AllocateVariable(&Parts, 1U, sizeof(TerrainChunkPart));
    GenericBuffer Vertices;
    GenericBuffer_AllocateVariable(&Vertices, TerrainChunk_GetVertexCount(Source), sizeof(uint32_t));
    GenericBuffer Indices;
    GenericBuffer_AllocateVariable(&Indices, (size_t)TerrainChunk_GetSurfaceTriangleCount(Source) * 3U,
        sizeof(uint32_t));
    Error Result = TerrainMesh_SplitChunkSurface(mesh, chunk, PART_MAX_VERTICES, &Parts, &Vertices, &Indices);
    if ((Result.Code == ErrorCode_Success) && (Parts._count > 0U))
    {
        const TerrainChunkPart* Cut = (const TerrainChunkPart*)(const void*)Parts._data;
        outMesh->_parts = Memory_Allocate(Parts._count * sizeof(WorldRendererTerrainPart));
        Memory_Zero(outMesh->_parts, Parts._count * sizeof(WorldRendererTerrainPart));
        for (size_t Part = 0U; Part < Parts._count; Part++)
        {
            UploadPart(mesh, Source, &Cut[Part], (const uint32_t*)(const void*)Vertices._data,
                (const uint32_t*)(const void*)Indices._data, &outMesh->_parts[Part]);
        }
        outMesh->_partCount = (uint32_t)Parts._count;
    }
    // A split that could not grow its buffers leaves the chunk without a mesh this time; it is tried again when it
    // changes.
    Error_Deconstruct(&Result);
    Memory_Free(Indices._data);
    Memory_Free(Vertices._data);
    Memory_Free(Parts._data);
}

/* The GPU meshes of a terrain, made (empty, for its current layout) the first time it is asked for. */
static WorldRendererTerrainMeshes* GetMeshes(WorldRenderer* self, const TerrainAsset* asset)
{
    WorldRendererTerrainMeshes* Entries = (WorldRendererTerrainMeshes*)(void*)self->_terrain._meshes._data;
    for (size_t Index = 0U; Index < self->_terrain._meshes._count; Index++)
    {
        if (Entries[Index]._asset == asset)
        {
            return &Entries[Index];
        }
    }
    WorldRendererTerrainMeshes Fresh = { ._asset = asset, ._layoutRevision = 0U, ._chunks = NULL, ._chunkCount = 0U,
        ._isUsed = false };
    if (!GenericBuffer_AddLast(&self->_terrain._meshes, &Fresh))
    {
        return NULL;
    }
    return &((WorldRendererTerrainMeshes*)(void*)self->_terrain._meshes._data)[self->_terrain._meshes._count - 1U];
}

/* A listed chunk's GPU mesh, when it has been built; NULL otherwise. */
static const WorldRendererTerrainChunkMesh* FindChunkMesh(const WorldRenderer* self,
    const WorldRenderTerrainChunk* chunk)
{
    const WorldRendererTerrainMeshes* Entries = (const WorldRendererTerrainMeshes*)(const void*)
        self->_terrain._meshes._data;
    for (size_t Index = 0U; Index < self->_terrain._meshes._count; Index++)
    {
        if ((Entries[Index]._asset == chunk->Asset) && (chunk->ChunkIndex < Entries[Index]._chunkCount))
        {
            const WorldRendererTerrainChunkMesh* Gpu = &Entries[Index]._chunks[chunk->ChunkIndex];
            return (Gpu->_revision != 0U) ? Gpu : NULL;
        }
    }
    return NULL;
}


// Static functions: drawing.
/* Draws every part of a listed chunk's GPU mesh with @p material at @p transform (nothing before its mesh is built). */
static void DrawChunk(const WorldRenderer* self, const WorldRenderTerrainChunk* chunk, Material material,
    Matrix transform)
{
    const WorldRendererTerrainChunkMesh* Gpu = FindChunkMesh(self, chunk);
    for (uint32_t Part = 0U; (Gpu != NULL) && (Part < Gpu->_partCount); Part++)
    {
        DrawMesh(Gpu->_parts[Part]._mesh, material, transform);
    }
}

/* The chunk key's colour of a listed chunk: by its cell, so neighbours differ. */
static Color GetChunkKeyColor(const WorldRenderTerrainChunk* chunk)
{
    int32_t CellX = 0;
    int32_t CellZ = 0;
    TerrainChunk_GetCell(TerrainMesh_GetChunk(TerrainAsset_GetMesh(chunk->Asset), chunk->ChunkIndex), &CellX, &CellZ);
    uint32_t Column = (uint32_t)(((CellX % 3) + 3) % 3);
    uint32_t Row = (uint32_t)(((CellZ % 3) + 3) % 3);
    return ChunkKeyColors[Column + (3U * Row)];
}

static Color MultiplyColor(Color a, Color b)
{
    return (Color){ (unsigned char)(((int)a.r * (int)b.r) / 255), (unsigned char)(((int)a.g * (int)b.g) / 255),
        (unsigned char)(((int)a.b * (int)b.b) / 255), (unsigned char)(((int)a.a * (int)b.a) / 255) };
}

/* A material for raylib's default shader (the unlit fallback, the wireframe): its 1x1 white texture times @p color. */
static Material MakeDefaultMaterial(MaterialMap* maps, Color color)
{
    Memory_Zero(maps, sizeof(MaterialMap) * WORLD_RENDERER_MATERIAL_MAP_COUNT);
    maps[MATERIAL_MAP_ALBEDO].texture = (Texture2D){ .id = rlGetTextureIdDefault(), .width = 1, .height = 1,
        .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    maps[MATERIAL_MAP_ALBEDO].color = color;
    return (Material){ .shader = { .id = rlGetShaderIdDefault(), .locs = rlGetShaderLocsDefault() }, .maps = maps };
}

/* The chunk key's wireframe over the camera's chunks, drawn a little toward the camera so it lies over the surface. */
static void DrawWireframes(WorldRenderer* self, const WorldRenderTerrainChunk* chunks, const size_t* visible,
    size_t visibleCount)
{
    MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
    Material Wire = MakeDefaultMaterial(Maps, WIREFRAME_COLOR);
    Matrix Toward = MatrixScale(WIREFRAME_SCALE, WIREFRAME_SCALE, WIREFRAME_SCALE);
    rlEnableWireMode();
    for (size_t Position = 0U; Position < visibleCount; Position++)
    {
        const WorldRenderTerrainChunk* Entry = &chunks[visible[Position]];
        DrawChunk(self, Entry, Wire, MatrixMultiply(Entry->RenderMatrix, Toward));
    }
    rlDisableWireMode();
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererTerrain_Construct(WorldRenderer* self)
{
    GenericBuffer_AllocateVariable(&self->_terrain._meshes, 4U, sizeof(WorldRendererTerrainMeshes));
}

void WorldRendererTerrain_Deconstruct(WorldRenderer* self)
{
    WorldRendererTerrain_ReleaseMeshes(self);
    Memory_Free(self->_terrain._meshes._data);
    self->_terrain._meshes._data = NULL;
}

void WorldRendererTerrain_LoadShader(WorldRenderer* self)
{
    self->_terrain._shader = WorldRendererCommon_LoadShader(self, TERRAIN_SHADER_ASSET_NAME);
    if (self->_terrain._shader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(self->_terrain._shader);
    WorldRendererLighting_PrepareLitShader(self, RayShader, TERRAIN_SHADER_ASSET_NAME, &self->_terrain._lit);
    WorldRendererCommon_PrepareDither(RayShader, &self->_terrain._dither);
    self->_terrain._locChunkTint = GetShaderLocation(RayShader, "chunkTint");
}

void WorldRendererTerrain_ReleaseMeshes(WorldRenderer* self)
{
    WorldRendererTerrainMeshes* Entries = (WorldRendererTerrainMeshes*)(void*)self->_terrain._meshes._data;
    for (size_t Index = 0U; Index < self->_terrain._meshes._count; Index++)
    {
        FreeMeshes(&Entries[Index]);
    }
    GenericBuffer_Clear(&self->_terrain._meshes);
}

void WorldRendererTerrain_PrepareFrame(WorldRenderer* self)
{
    WorldRendererTerrainMeshes* Entries = (WorldRendererTerrainMeshes*)(void*)self->_terrain._meshes._data;
    for (size_t Index = 0U; Index < self->_terrain._meshes._count; Index++)
    {
        Entries[Index]._isUsed = false;
    }
    // Every terrain an entry resolves keeps its meshes (a hidden object's too: it may show again); a new chunk layout
    // starts its meshes afresh.
    size_t TerrainCount = 0U;
    const WorldRenderTerrain* Terrains = WorldRenderCache_GetTerrains(&self->_cache, &TerrainCount);
    for (size_t Index = 0U; Index < TerrainCount; Index++)
    {
        if (Terrains[Index].Asset == NULL)
        {
            continue;
        }
        WorldRendererTerrainMeshes* Meshes = GetMeshes(self, Terrains[Index].Asset);
        if (Meshes == NULL)
        {
            continue;
        }
        Meshes->_isUsed = true;
        const TerrainMesh* Ground = TerrainAsset_GetMesh(Meshes->_asset);
        if ((Meshes->_chunks == NULL) || (Meshes->_layoutRevision != TerrainMesh_GetLayoutRevision(Ground)))
        {
            FreeMeshes(Meshes);
            uint32_t ChunkCount = TerrainMesh_GetChunkCount(Ground);
            Meshes->_chunks = Memory_Allocate(((ChunkCount > 0U) ? (size_t)ChunkCount : 1U)
                * sizeof(WorldRendererTerrainChunkMesh));
            Memory_Zero(Meshes->_chunks, ((ChunkCount > 0U) ? (size_t)ChunkCount : 1U)
                * sizeof(WorldRendererTerrainChunkMesh));
            Meshes->_chunkCount = ChunkCount;
            Meshes->_layoutRevision = TerrainMesh_GetLayoutRevision(Ground);
        }
    }
    // The meshes of terrains no entry resolves any more go (backwards: removing keeps the earlier ones in place).
    for (size_t Index = self->_terrain._meshes._count; Index > 0U; Index--)
    {
        WorldRendererTerrainMeshes* Meshes = &((WorldRendererTerrainMeshes*)(void*)self->_terrain._meshes._data)[Index
            - 1U];
        if (!Meshes->_isUsed)
        {
            FreeMeshes(Meshes);
            GenericBuffer_RemoveAt(&self->_terrain._meshes, Index - 1U);
        }
    }
    // The listed chunks (those of shown terrains) are uploaded when they are new or changed.
    size_t ChunkCount = 0U;
    const WorldRenderTerrainChunk* Chunks = WorldRenderCache_GetTerrainChunks(&self->_cache, &ChunkCount);
    for (size_t Index = 0U; Index < ChunkCount; Index++)
    {
        WorldRendererTerrainMeshes* Meshes = GetMeshes(self, Chunks[Index].Asset);
        if ((Meshes == NULL) || (Chunks[Index].ChunkIndex >= Meshes->_chunkCount))
        {
            continue;
        }
        const TerrainMesh* Ground = TerrainAsset_GetMesh(Meshes->_asset);
        WorldRendererTerrainChunkMesh* Gpu = &Meshes->_chunks[Chunks[Index].ChunkIndex];
        if (Gpu->_revision != TerrainChunk_GetRevision(TerrainMesh_GetChunk(Ground, Chunks[Index].ChunkIndex)))
        {
            BuildChunkMesh(Ground, Chunks[Index].ChunkIndex, Gpu);
        }
    }
}

void WorldRendererTerrain_DrawLit(WorldRenderer* self)
{
    size_t ChunkCount = 0U;
    const WorldRenderTerrainChunk* Chunks = WorldRenderCache_GetTerrainChunks(&self->_cache, &ChunkCount);
    size_t VisibleCount = 0U;
    const size_t* Visible = WorldVisibility_GetTerrainChunks(&self->_visibility, &self->_cache,
        WorldVisibilityPass_Camera, &VisibleCount);
    if (VisibleCount == 0U)
    {
        return;
    }
    bool IsLit = self->_terrain._shader != NULL;
    bool IsKeyed = self->_terrain._isDebugEnabled;
    MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
    Material Draw = MakeDefaultMaterial(Maps, UNLIT_COLOR);
    if (IsLit)
    {
        Draw.shader = GameShader_GetRaylibShader(self->_terrain._shader);
        Maps[MATERIAL_MAP_ALBEDO].texture.id = 0U; // the terrain shader samples no texture
    }
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        size_t Index = Visible[Position];
        const WorldRenderTerrainChunk* Entry = &Chunks[Index];
        Color Key = IsKeyed ? GetChunkKeyColor(Entry) : WHITE;
        if (IsLit)
        {
            // Each chunk takes the forward lights its sphere reaches, and dithers by its fade (its draw distance, its
            // object's opacity).
            WorldRendererLighting_UploadObjectLights(self, Draw.shader, &self->_terrain._lit, Entry->RenderCenter,
                Entry->Radius);
            WorldRendererCommon_SetDither(Draw.shader, &self->_terrain._dither,
                WorldVisibility_GetTerrainChunkFade(&self->_visibility, Index).Combined, 0.0f, false);
            float Tint[4] = { (float)Key.r / 255.0f, (float)Key.g / 255.0f, (float)Key.b / 255.0f,
                IsKeyed ? CHUNK_KEY_STRENGTH : 0.0f };
            SetShaderValue(Draw.shader, self->_terrain._locChunkTint, Tint, SHADER_UNIFORM_VEC4);
            Maps[MATERIAL_MAP_ALBEDO].color = Entry->Tint;
        }
        else
        {
            // Unlit and solid, as models are without their lit shader.
            Maps[MATERIAL_MAP_ALBEDO].color = MultiplyColor(MultiplyColor(UNLIT_COLOR, Key), Entry->Tint);
        }
        DrawChunk(self, Entry, Draw, Entry->RenderMatrix);
    }
    if (IsKeyed)
    {
        DrawWireframes(self, Chunks, Visible, VisibleCount);
    }
}

void WorldRendererTerrain_DrawNormals(WorldRenderer* self, Shader shader, int locOutlineFlag)
{
    size_t ChunkCount = 0U;
    const WorldRenderTerrainChunk* Chunks = WorldRenderCache_GetTerrainChunks(&self->_cache, &ChunkCount);
    size_t VisibleCount = 0U;
    const size_t* Visible = WorldVisibility_GetTerrainChunks(&self->_visibility, &self->_cache,
        WorldVisibilityPass_Camera, &VisibleCount);
    MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
    Memory_Zero(Maps, sizeof(Maps));
    Material Draw = { .shader = shader, .maps = Maps };
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        size_t Index = Visible[Position];
        // Solid below the G-buffer's limit, left out from there on (outlines and AO switch off once, at mid-fade).
        if (!(WorldVisibility_GetTerrainChunkFade(&self->_visibility, Index).Combined
            < WORLD_VISIBILITY_GBUFFER_FADE_LIMIT))
        {
            continue;
        }
        float OutlineFlag = Chunks[Index].HasOutline ? 1.0f : 0.0f;
        SetShaderValue(shader, locOutlineFlag, &OutlineFlag, SHADER_UNIFORM_FLOAT);
        DrawChunk(self, &Chunks[Index], Draw, Chunks[Index].RenderMatrix);
    }
}

void WorldRendererTerrain_DrawCasters(WorldRenderer* self, Shader shader, const WorldRendererDitherLocations* dither,
    WorldVisibilityPass pass)
{
    size_t ChunkCount = 0U;
    const WorldRenderTerrainChunk* Chunks = WorldRenderCache_GetTerrainChunks(&self->_cache, &ChunkCount);
    size_t CasterCount = 0U;
    const size_t* Casters = WorldVisibility_GetTerrainChunks(&self->_visibility, &self->_cache, pass, &CasterCount);
    MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
    Memory_Zero(Maps, sizeof(Maps));
    Material Draw = { .shader = shader, .maps = Maps };
    for (size_t Position = 0U; Position < CasterCount; Position++)
    {
        size_t Index = Casters[Position];
        WorldRendererCommon_SetDither(shader, dither, WorldVisibility_GetTerrainChunkFade(&self->_visibility,
            Index).Combined, 0.0f, false);
        DrawChunk(self, &Chunks[Index], Draw, Chunks[Index].RenderMatrix);
    }
}
