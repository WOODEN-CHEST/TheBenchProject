// The terrain's draws: each chunk of the render cache's terrains as GPU meshes the renderer keeps (the terrain asset
// stays CPU data, which render only reads), uploaded again when a chunk's revision changes; the lit terrain shader in the
// scene pass, the shared G-buffer and depth shaders in the others, and the chunk key (each chunk tinted by its cell, and
// the wireframe). A chunk's vertices are its surface's (TerrainChunk_GetPositions: relative to its centre, which its
// render-space matrix places), cut into parts of at most 65536 vertices: raylib's indices are unsigned shorts.
// TerrainMesh_SplitChunkSurface cuts them as it does for collision.
//
// THE MATERIALS. Each terrain the renderer keeps gets three texture arrays, one layer per palette material
// (GLTextureArray.h): the albedo with the height in its alpha, the normal maps, the roughness and AO; made once from
// the materials' CPU pixels (the asset is read, never written) and bound to their units before the terrain's chunks draw,
// with the materials' and rules' uniforms. Per chunk the shader gets the chunk's centre in the terrain's own space
// wrapped by each material's tile size and by each rule's noise period, in double (the maps and the noise lie in the
// terrain's space, so they move with its object; the shader adds each vertex's exact offset from the centre), and its
// centre's height in the world (the rules' height is absolute).

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "WorldRendererInternal.h"
#include "Logger.h"
#include "TerrainBlend.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"
#include "wr/WRStringBuilder.h"


// Macros.
/** Asset name of the lit terrain shader. */
#define TERRAIN_SHADER_ASSET_NAME ((const unsigned char*)u8"world_terrain")
/** The most vertices one part holds: raylib's mesh indices are unsigned shorts. */
#define PART_MAX_VERTICES ((uint32_t)UINT16_MAX + 1U)
/** The first attribute location of the weights (raylib's instance matrix's: a terrain is never instanced), and how many
 *  locations they take (four palette weights each). */
#define WEIGHT_ATTRIBUTE_LOCATION 9U
#define WEIGHT_ATTRIBUTE_COUNT 4U
/** The vertex flags' attribute location (free on terrain, which is never instanced) and its bytes: x the face
 *  material (255 on a face material's vertices), the rest 0. */
#define FLAG_ATTRIBUTE_LOCATION 13U
#define FLAG_ATTRIBUTE_BYTES 4U
#define FLAG_FACE_MATERIAL UINT8_MAX
/** Degrees per radian (a material's triplanar slope goes up in degrees). */
#define DEGREES_PER_RADIAN 57.29577951308232
/** What a material without a normal map, or without a roughness and AO map, fills its array layer with: a flat normal
 *  (x and y 0) and roughness 1 with no occlusion. */
#define FLAT_NORMAL_TEXEL { TERRAIN_MATERIAL_FLAT_NORMAL, TERRAIN_MATERIAL_FLAT_NORMAL }
#define PLAIN_SURFACE_TEXEL { UINT8_MAX, UINT8_MAX }
/** The rule conditions' bits, as the shader reads them (ruleIds[].w). */
#define RULE_CONDITION_SLOPE 1
#define RULE_CONDITION_HEIGHT 2
#define RULE_CONDITION_NOISE 4
#define RULE_CONDITION_SLOPE_MINIMUM 8
#define RULE_CONDITION_SLOPE_MAXIMUM 16
#define RULE_CONDITION_HEIGHT_MINIMUM 32
#define RULE_CONDITION_HEIGHT_MAXIMUM 64
/** Room for a terrain's line in the status log: sixteen palette names at their longest with their separators, and 512
 *  bytes for its own name, the counts and the arrays' sizes. */
#define MATERIALS_LINE_CAPACITY (512U + (TERRAIN_MAX_MATERIALS * (TERRAIN_NAME_MAX_LENGTH + 2U)))
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
/* The units of the arrays, by TerrainMaterialMapKind. */
static const WorldRendererTextureUnit ArrayUnits[TerrainMaterialMapKind_Count] =
{
    WorldRendererTextureUnit_TerrainAlbedoHeight, WorldRendererTextureUnit_TerrainNormal,
    WorldRendererTextureUnit_TerrainRoughnessAo
};
/* The arrays' samplers, by TerrainMaterialMapKind. */
static const char* const ArraySamplers[TerrainMaterialMapKind_Count] =
{
    "materialAlbedoHeight", "materialNormal", "materialRoughnessAo"
};
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
        rlUnloadVertexBuffer(mesh->_parts[Part]._flagBuffer);
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

/* The chunks' meshes of a terrain's record (its arrays stay: they do not depend on the chunk layout). */
static void FreeChunkMeshes(WorldRendererTerrainMeshes* meshes)
{
    for (uint32_t Chunk = 0U; Chunk < meshes->_chunkCount; Chunk++)
    {
        FreeChunkMesh(&meshes->_chunks[Chunk]);
    }
    Memory_Free(meshes->_chunks);
    meshes->_chunks = NULL;
    meshes->_chunkCount = 0U;
}

/* Everything a terrain's record holds on the GPU: its chunks' meshes and its arrays. */
static void FreeRecord(WorldRenderer* self, WorldRendererTerrainMeshes* meshes)
{
    FreeChunkMeshes(meshes);
    for (uint32_t Kind = 0U; Kind < (uint32_t)TerrainMaterialMapKind_Count; Kind++)
    {
        if (meshes->_arrays[Kind] != 0U)
        {
            GLTextureArray_Delete(&self->_terrain._arrayApi, meshes->_arrays[Kind]);
            meshes->_arrays[Kind] = 0U;
        }
    }
    meshes->_hasArrays = false;
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
    uint8_t* Flags = Memory_Allocate((size_t)VertexCount * FLAG_ATTRIBUTE_BYTES);
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
        uint8_t* OwnFlags = &Flags[(size_t)Vertex * FLAG_ATTRIBUTE_BYTES];
        Memory_Zero(OwnFlags, FLAG_ATTRIBUTE_BYTES);
        uint8_t FaceMaterial = VertexMaterials[Surface];
        if (FaceMaterial == TERRAIN_CHUNK_BLEND_MATERIAL)
        {
            Memory_Copy(TerrainMesh_GetWeights(mesh, Terrain), Own, TERRAIN_MAX_MATERIALS);
        }
        else
        {
            Memory_Zero(Own, TERRAIN_MAX_MATERIALS);
            Own[FaceMaterial] = UINT8_MAX;
            OwnFlags[0] = FLAG_FACE_MATERIAL;
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
    // The flags in a buffer of their own, bound after it: rlSetVertexAttribute reads the bound array buffer.
    outPart->_flagBuffer = rlLoadVertexBuffer(Flags, (int)(VertexCount * FLAG_ATTRIBUTE_BYTES), false);
    rlSetVertexAttribute(FLAG_ATTRIBUTE_LOCATION, 4, RL_UNSIGNED_BYTE, true, (int)FLAG_ATTRIBUTE_BYTES, 0);
    rlEnableVertexAttribute(FLAG_ATTRIBUTE_LOCATION);
    rlDisableVertexArray();
    // The GPU has them now: the CPU arrays go, except the indices: raylib's DrawMesh draws a mesh whose index array is
    // gone as if it had none (each three vertices in a row a triangle). FreeChunkMesh frees them.
    Memory_Free(Positions);
    Memory_Free(Normals);
    Memory_Free(Weights);
    Memory_Free(Flags);
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

// Static functions: the materials.
static float ToLinear(uint8_t value)
{
    return powf((float)value / 255.0f, SRGB_GAMMA);
}

/* A uint32's bits as an int32, for a uniform the shader reads back as a uint (the noise's seed). */
static int AsInt(uint32_t value)
{
    int32_t Bits = 0;
    Memory_Copy(&value, &Bits, sizeof(Bits));
    return (int)Bits;
}

/* The shader's view of a terrain's materials and rules (the asset never changes while it is loaded). A material the
 * terrain was built without (headless tests) gets the defaults' look: white, no maps. */
static void BuildUniforms(const TerrainAsset* asset, WorldRendererTerrainUniforms* outUniforms)
{
    Memory_Zero(outUniforms, sizeof(*outUniforms));
    uint32_t MaterialCount = TerrainAsset_GetMaterialCount(asset);
    outUniforms->_materialCount = (int)MaterialCount;
    for (uint32_t Index = 0U; Index < MaterialCount; Index++)
    {
        const TerrainMaterial* Source = TerrainAsset_GetMaterial(asset, Index);
        TerrainMaterialSettings Defaults =
        {
            .TileSize = 2.0f, .AlbedoTint = WHITE, .Roughness = 0.9f, .NormalStrength = 1.0f,
            .HeightBlendStrength = 1.0f, .Triplanar = TerrainTriplanarMode_Off
        };
        const TerrainMaterialSettings* Settings = (Source != NULL) ? TerrainMaterial_GetSettings(Source) : &Defaults;
        Vector3 Mean = (Source != NULL) ? TerrainMaterial_GetMeanColor(Source) : (Vector3){ 1.0f, 1.0f, 1.0f };
        float* Tint = &outUniforms->_tint[Index * 4U];
        Tint[0] = ToLinear(Settings->AlbedoTint.r);
        Tint[1] = ToLinear(Settings->AlbedoTint.g);
        Tint[2] = ToLinear(Settings->AlbedoTint.b);
        float* Surface = &outUniforms->_surface[Index * 4U];
        Surface[0] = 1.0f / Settings->TileSize;
        Surface[1] = Settings->Roughness;
        Surface[2] = Settings->NormalStrength;
        Surface[3] = Settings->HeightBlendStrength;
        float* Projection = &outUniforms->_projection[Index * 4U];
        Projection[0] = (float)Settings->Triplanar;
        Projection[1] = (float)((double)Settings->TriplanarSlope * DEGREES_PER_RADIAN);
        Projection[2] = Settings->Puddles;
        Projection[3] = Settings->Porosity;
        float* MeanColor = &outUniforms->_meanColor[Index * 4U];
        MeanColor[0] = Mean.x;
        MeanColor[1] = Mean.y;
        MeanColor[2] = Mean.z;
        outUniforms->_tileSizes[Index] = (double)Settings->TileSize;
    }
    uint32_t RuleCount = 0U;
    const TerrainRule* Rules = TerrainAsset_GetRules(asset, &RuleCount);
    RuleCount = (RuleCount > WORLD_RENDERER_TERRAIN_MAX_RULES) ? WORLD_RENDERER_TERRAIN_MAX_RULES : RuleCount;
    outUniforms->_ruleCount = (int)RuleCount;
    for (uint32_t Index = 0U; Index < RuleCount; Index++)
    {
        const TerrainRule* Rule = &Rules[Index];
        int Conditions = 0;
        Conditions |= Rule->Slope.IsEnabled ? RULE_CONDITION_SLOPE : 0;
        Conditions |= (Rule->Slope.IsEnabled && Rule->Slope.HasMinimum) ? RULE_CONDITION_SLOPE_MINIMUM : 0;
        Conditions |= (Rule->Slope.IsEnabled && Rule->Slope.HasMaximum) ? RULE_CONDITION_SLOPE_MAXIMUM : 0;
        Conditions |= Rule->Height.IsEnabled ? RULE_CONDITION_HEIGHT : 0;
        Conditions |= (Rule->Height.IsEnabled && Rule->Height.HasMinimum) ? RULE_CONDITION_HEIGHT_MINIMUM : 0;
        Conditions |= (Rule->Height.IsEnabled && Rule->Height.HasMaximum) ? RULE_CONDITION_HEIGHT_MAXIMUM : 0;
        bool HasNoise = Rule->Noise.IsEnabled && (Rule->Noise.Scale > 0.0f);
        Conditions |= HasNoise ? RULE_CONDITION_NOISE : 0;
        int* Ids = &outUniforms->_ruleIds[Index * 4U];
        Ids[0] = (int)Rule->Material;
        Ids[1] = AsInt(Rule->CoveredMaterials);
        Ids[2] = AsInt(Rule->Noise.Seed);
        Ids[3] = Conditions;
        float* Slope = &outUniforms->_ruleSlope[Index * 4U];
        Slope[0] = Rule->Slope.Minimum;
        Slope[1] = Rule->Slope.Maximum;
        Slope[2] = Rule->Slope.Blend;
        Slope[3] = Rule->Opacity;
        float* Height = &outUniforms->_ruleHeight[Index * 4U];
        Height[0] = Rule->Height.Minimum;
        Height[1] = Rule->Height.Maximum;
        Height[2] = Rule->Height.Blend;
        float* Noise = &outUniforms->_ruleNoise[Index * 4U];
        Noise[0] = HasNoise ? (1.0f / Rule->Noise.Scale) : 0.0f;
        Noise[1] = Rule->Noise.Strength;
        outUniforms->_noisePeriods[Index] = HasNoise ? ((double)TERRAIN_BLEND_NOISE_PERIOD * (double)Rule->Noise.Scale)
            : 0.0;
    }
}

/* The size a terrain's array of a map kind takes: its materials' (one size, checked when the terrain loaded); 1 x 1 when
 * none of them has the map. */
static void GetArraySize(const TerrainAsset* asset, TerrainMaterialMapKind kind, int32_t* outWidth, int32_t* outHeight)
{
    *outWidth = 1;
    *outHeight = 1;
    for (uint32_t Index = 0U; Index < TerrainAsset_GetMaterialCount(asset); Index++)
    {
        int32_t Width = 0;
        int32_t Height = 0;
        if (TerrainMaterial_GetMap(TerrainAsset_GetMaterial(asset, Index), kind, &Width, &Height) != NULL)
        {
            *outWidth = Width;
            *outHeight = Height;
            return;
        }
    }
}

/* Makes a terrain's three arrays from its materials' pixels (once per record; a failure is logged once and the terrain
 * shows its materials' mean colours). Needs every material: a terrain built without them (headless tests) gets none. */
static void BuildArrays(WorldRenderer* self, WorldRendererTerrainMeshes* record)
{
    record->_isArrayBuildTried = true;
    const GLTextureArrayApi* Api = &self->_terrain._arrayApi;
    if ((self->_terrain._shader == NULL) || !GLTextureArray_IsAvailable(Api))
    {
        return;
    }
    const TerrainAsset* Asset = record->_asset;
    uint32_t MaterialCount = TerrainAsset_GetMaterialCount(Asset);
    for (uint32_t Index = 0U; Index < MaterialCount; Index++)
    {
        if (TerrainAsset_GetMaterial(Asset, Index) == NULL)
        {
            return;
        }
    }
    static const uint8_t FlatNormal[2] = FLAT_NORMAL_TEXEL;
    static const uint8_t PlainSurface[2] = PLAIN_SURFACE_TEXEL;
    const uint8_t* Fills[TerrainMaterialMapKind_Count] = { NULL, FlatNormal, PlainSurface };
    GLTextureArrayFormat Formats[TerrainMaterialMapKind_Count] =
    {
        GLTextureArrayFormat_Rgba8, GLTextureArrayFormat_Rg8, GLTextureArrayFormat_Rg8
    };
    Error Result = Error_CreateSuccess();
    for (uint32_t Kind = 0U; (Kind < (uint32_t)TerrainMaterialMapKind_Count) && (Result.Code == ErrorCode_Success);
        Kind++)
    {
        const uint8_t* Layers[TERRAIN_MAX_MATERIALS];
        for (uint32_t Index = 0U; Index < MaterialCount; Index++)
        {
            Layers[Index] = TerrainMaterial_GetMap(TerrainAsset_GetMaterial(Asset, Index), (TerrainMaterialMapKind)Kind,
                NULL, NULL);
        }
        GetArraySize(Asset, (TerrainMaterialMapKind)Kind, &record->_arrayWidths[Kind], &record->_arrayHeights[Kind]);
        Result = GLTextureArray_Create(Api, Formats[Kind], record->_arrayWidths[Kind], record->_arrayHeights[Kind],
            (int32_t)MaterialCount, Layers, Fills[Kind], &record->_arrays[Kind]);
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (!self->_terrain._isArrayFailureReported && (self->_logger != NULL))
        {
            const unsigned char* Name = TerrainAsset_GetName(Asset);
            Error LogResult = Logger_LogWarningFormatted(self->_logger, (const unsigned char*)u8"WorldRenderer: the "
                u8"texture arrays of terrain \"%s\" could not be made (%s); it shows its materials' mean colours. "
                u8"(Logged once.)", (Name != NULL) ? (const char*)Name : "?",
                (Result.Message != NULL) ? (const char*)Result.Message : "an error");
            Error_Deconstruct(&LogResult);
        }
        self->_terrain._isArrayFailureReported = true;
        Error_Deconstruct(&Result);
        for (uint32_t Kind = 0U; Kind < (uint32_t)TerrainMaterialMapKind_Count; Kind++)
        {
            GLTextureArray_Delete(Api, record->_arrays[Kind]);
            record->_arrays[Kind] = 0U;
        }
        return;
    }
    record->_hasArrays = true;
}

/* Binds a terrain's arrays and uploads its materials and rules (the lit pass turning to its chunks). */
static void UploadTerrain(WorldRenderer* self, Shader shader, const WorldRendererTerrainMeshes* record)
{
    const WorldRendererTerrainState* State = &self->_terrain;
    const WorldRendererTerrainUniforms* Uniforms = &record->_uniforms;
    int HasArrays = record->_hasArrays ? 1 : 0;
    SetShaderValue(shader, State->_locHasMaterialArrays, &HasArrays, SHADER_UNIFORM_INT);
    SetShaderValue(shader, State->_locMaterialCount, &Uniforms->_materialCount, SHADER_UNIFORM_INT);
    int MaterialCount = (Uniforms->_materialCount > 0) ? Uniforms->_materialCount : 1;
    SetShaderValueV(shader, State->_locMaterialTint, Uniforms->_tint, SHADER_UNIFORM_VEC4, MaterialCount);
    SetShaderValueV(shader, State->_locMaterialSurface, Uniforms->_surface, SHADER_UNIFORM_VEC4, MaterialCount);
    SetShaderValueV(shader, State->_locMaterialProjection, Uniforms->_projection, SHADER_UNIFORM_VEC4, MaterialCount);
    SetShaderValueV(shader, State->_locMaterialMeanColor, Uniforms->_meanColor, SHADER_UNIFORM_VEC4, MaterialCount);
    SetShaderValue(shader, State->_locRuleCount, &Uniforms->_ruleCount, SHADER_UNIFORM_INT);
    if (Uniforms->_ruleCount > 0)
    {
        SetShaderValueV(shader, State->_locRuleIds, Uniforms->_ruleIds, SHADER_UNIFORM_IVEC4, Uniforms->_ruleCount);
        SetShaderValueV(shader, State->_locRuleSlope, Uniforms->_ruleSlope, SHADER_UNIFORM_VEC4, Uniforms->_ruleCount);
        SetShaderValueV(shader, State->_locRuleHeight, Uniforms->_ruleHeight, SHADER_UNIFORM_VEC4, Uniforms->_ruleCount);
        SetShaderValueV(shader, State->_locRuleNoise, Uniforms->_ruleNoise, SHADER_UNIFORM_VEC4, Uniforms->_ruleCount);
    }
    if (record->_hasArrays)
    {
        for (uint32_t Kind = 0U; Kind < (uint32_t)TerrainMaterialMapKind_Count; Kind++)
        {
            GLTextureArray_Bind(&State->_arrayApi, (int32_t)ArrayUnits[Kind], record->_arrays[Kind]);
        }
    }
}

/* A chunk's place for its maps and its rules' noise: its centre in the terrain's space wrapped by each material's tile
 * size and by each rule's noise period (in double: what the shader adds its offset from the centre to), and its centre's
 * height in the world. */
static void UploadChunkPlace(const WorldRenderer* self, Shader shader, const WorldRendererTerrainMeshes* record,
    const WorldRenderTerrainChunk* chunk)
{
    const WorldRendererTerrainState* State = &self->_terrain;
    const WorldRendererTerrainUniforms* Uniforms = &record->_uniforms;
    Vector3Double Own = TerrainChunk_GetCenter(TerrainMesh_GetChunk(TerrainAsset_GetMesh(chunk->Asset),
        chunk->ChunkIndex));
    double Center[3] = { Own.X, Own.Y, Own.Z };
    float UvOffsets[TERRAIN_MAX_MATERIALS * 3U];
    int MaterialCount = (Uniforms->_materialCount > 0) ? Uniforms->_materialCount : 1;
    for (int Index = 0; Index < MaterialCount; Index++)
    {
        for (int Axis = 0; Axis < 3; Axis++)
        {
            UvOffsets[(Index * 3) + Axis] = (float)TerrainBlend_WrapCoordinate(Center[Axis],
                Uniforms->_tileSizes[Index]);
        }
    }
    SetShaderValueV(shader, State->_locMaterialUvOffset, UvOffsets, SHADER_UNIFORM_VEC3, MaterialCount);
    if (Uniforms->_ruleCount > 0)
    {
        float NoiseOffsets[WORLD_RENDERER_TERRAIN_MAX_RULES * 3U];
        for (int Index = 0; Index < Uniforms->_ruleCount; Index++)
        {
            for (int Axis = 0; Axis < 3; Axis++)
            {
                double Period = Uniforms->_noisePeriods[Index];
                NoiseOffsets[(Index * 3) + Axis] = (Period > 0.0)
                    ? (float)TerrainBlend_WrapCoordinate(Center[Axis], Period) : 0.0f;
            }
        }
        SetShaderValueV(shader, State->_locRuleNoiseOffset, NoiseOffsets, SHADER_UNIFORM_VEC3, Uniforms->_ruleCount);
    }
    float Height = (float)chunk->WorldMatrix.M[13];
    SetShaderValue(shader, State->_locChunkHeight, &Height, SHADER_UNIFORM_FLOAT);
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
    WorldRendererTerrainMeshes Fresh;
    Memory_Zero(&Fresh, sizeof(Fresh));
    Fresh._asset = asset;
    BuildUniforms(asset, &Fresh._uniforms);
    if (!GenericBuffer_AddLast(&self->_terrain._meshes, &Fresh))
    {
        return NULL;
    }
    return &((WorldRendererTerrainMeshes*)(void*)self->_terrain._meshes._data)[self->_terrain._meshes._count - 1U];
}

/* The record of a listed chunk's terrain, NULL before it has one. */
static const WorldRendererTerrainMeshes* FindRecord(const WorldRenderer* self, const TerrainAsset* asset)
{
    const WorldRendererTerrainMeshes* Entries = (const WorldRendererTerrainMeshes*)(const void*)
        self->_terrain._meshes._data;
    for (size_t Index = 0U; Index < self->_terrain._meshes._count; Index++)
    {
        if (Entries[Index]._asset == asset)
        {
            return &Entries[Index];
        }
    }
    return NULL;
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
    WorldRendererTerrainState* State = &self->_terrain;
    State->_locChunkTint = GetShaderLocation(RayShader, "chunkTint");
    State->_locTerrainView = GetShaderLocation(RayShader, "terrainView");
    State->_locMaterialCount = GetShaderLocation(RayShader, "materialCount");
    State->_locHasMaterialArrays = GetShaderLocation(RayShader, "hasMaterialArrays");
    State->_locMaterialTint = GetShaderLocation(RayShader, "materialTint");
    State->_locMaterialSurface = GetShaderLocation(RayShader, "materialSurface");
    State->_locMaterialProjection = GetShaderLocation(RayShader, "materialProjection");
    State->_locMaterialMeanColor = GetShaderLocation(RayShader, "materialMeanColor");
    State->_locRuleCount = GetShaderLocation(RayShader, "ruleCount");
    State->_locRuleIds = GetShaderLocation(RayShader, "ruleIds");
    State->_locRuleSlope = GetShaderLocation(RayShader, "ruleSlope");
    State->_locRuleHeight = GetShaderLocation(RayShader, "ruleHeight");
    State->_locRuleNoise = GetShaderLocation(RayShader, "ruleNoise");
    State->_locMaterialUvOffset = GetShaderLocation(RayShader, "materialUvOffset");
    State->_locRuleNoiseOffset = GetShaderLocation(RayShader, "ruleNoiseOffset");
    State->_locChunkHeight = GetShaderLocation(RayShader, "chunkHeight");
    // The arrays' samplers on their units, once (never left on unit 0, where another sampler type may sit).
    for (uint32_t Kind = 0U; Kind < (uint32_t)TerrainMaterialMapKind_Count; Kind++)
    {
        int Location = GetShaderLocation(RayShader, ArraySamplers[Kind]);
        int Unit = (int)ArrayUnits[Kind];
        SetShaderValue(RayShader, Location, &Unit, SHADER_UNIFORM_INT);
    }
    if (!GLTextureArray_LoadApi(&State->_arrayApi))
    {
        char Message[256];
        snprintf(Message, sizeof(Message), "WorldRenderer: texture arrays are unavailable (%s is missing); terrains show "
            "their materials' mean colours.", (State->_arrayApi._missing != NULL) ? State->_arrayApi._missing : "?");
        WorldRendererCommon_WarnOnce(self, &State->_isArrayApiFailureReported, (const unsigned char*)Message);
    }
}

void WorldRendererTerrain_ReleaseMeshes(WorldRenderer* self)
{
    WorldRendererTerrainMeshes* Entries = (WorldRendererTerrainMeshes*)(void*)self->_terrain._meshes._data;
    for (size_t Index = 0U; Index < self->_terrain._meshes._count; Index++)
    {
        FreeRecord(self, &Entries[Index]);
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
        if (!Meshes->_isArrayBuildTried)
        {
            BuildArrays(self, Meshes);
        }
        const TerrainMesh* Ground = TerrainAsset_GetMesh(Meshes->_asset);
        if ((Meshes->_chunks == NULL) || (Meshes->_layoutRevision != TerrainMesh_GetLayoutRevision(Ground)))
        {
            FreeChunkMeshes(Meshes);
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
            FreeRecord(self, Meshes);
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
    self->_terrain._wereArraysDrawn = false;
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
        Maps[MATERIAL_MAP_ALBEDO].texture.id = 0U; // the terrain shader samples no raylib map: its arrays sit on 9-11
        int View = (int)self->_terrain._view;
        SetShaderValue(Draw.shader, self->_terrain._locTerrainView, &View, SHADER_UNIFORM_INT);
    }
    // Chunks come terrain by terrain: each terrain's arrays and uniforms go up when the pass turns to its chunks.
    const TerrainAsset* Current = NULL;
    const WorldRendererTerrainMeshes* Record = NULL;
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        size_t Index = Visible[Position];
        const WorldRenderTerrainChunk* Entry = &Chunks[Index];
        Color Key = IsKeyed ? GetChunkKeyColor(Entry) : WHITE;
        if (IsLit && (Entry->Asset != Current))
        {
            Current = Entry->Asset;
            Record = FindRecord(self, Current);
            if (Record != NULL)
            {
                UploadTerrain(self, Draw.shader, Record);
                self->_terrain._wereArraysDrawn = self->_terrain._wereArraysDrawn || Record->_hasArrays;
            }
        }
        if (IsLit && (Record != NULL))
        {
            UploadChunkPlace(self, Draw.shader, Record, Entry);
        }
        if (IsLit)
        {
            // Each chunk takes the forward lights and the ambient zones its sphere reaches, its haze multiplier, and
            // dithers by its fade (its draw distance, its object's opacity).
            WorldRendererLighting_UploadObjectLights(self, Draw.shader, &self->_terrain._lit, Entry->RenderCenter,
                Entry->Radius);
            float Fade = WorldVisibility_GetTerrainChunkFade(&self->_visibility, Index).Combined;
            WorldRendererLighting_SetDrawPlace(self, Draw.shader, &self->_terrain._lit, Entry->RenderCenter,
                Entry->Radius, Entry->Haze, Fade < WORLD_VISIBILITY_GBUFFER_FADE_LIMIT);
            WorldRendererCommon_SetDither(Draw.shader, &self->_terrain._dither, Fade, 0.0f, false);
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

void WorldRendererTerrain_DrawNormals(WorldRenderer* self, Shader shader, int locOutlineFlag, int locHazeMultiplier)
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
        SetShaderValue(shader, locHazeMultiplier, &Chunks[Index].Haze, SHADER_UNIFORM_FLOAT);
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
    // The shadow passes dither by the chunks' shadow fade (their shadow distance too), the sky's volume by their fade.
    // One program for every chunk: bound once.
    bool IsShadowPass = (pass != WorldVisibilityPass_SkyVisibility);
    WorldRendererDitherRun Run = { 0 };
    rlEnableShader(shader.id);
    for (size_t Position = 0U; Position < CasterCount; Position++)
    {
        size_t Index = Casters[Position];
        WorldVisibilityFade Fade = WorldVisibility_GetTerrainChunkFade(&self->_visibility, Index);
        WorldRendererCommon_SetRunDither(shader, dither, &Run, IsShadowPass ? Fade.Shadow : Fade.Combined);
        const WorldRendererTerrainChunkMesh* Gpu = FindChunkMesh(self, &Chunks[Index]);
        for (uint32_t Part = 0U; (Gpu != NULL) && (Part < Gpu->_partCount); Part++)
        {
            WorldRendererCommon_DrawDepthMesh(shader, Gpu->_parts[Part]._mesh, Chunks[Index].RenderMatrix);
        }
    }
    WorldRendererCommon_EndDepthMeshes();
}

WorldRendererFeatureState WorldRendererTerrain_GetMaterialsState(const WorldRenderer* self)
{
    if ((self->_terrain._shader == NULL) || !GLTextureArray_IsAvailable(&self->_terrain._arrayApi))
    {
        return WorldRendererFeatureState_Unavailable;
    }
    return self->_terrain._wereArraysDrawn ? WorldRendererFeatureState_Active : WorldRendererFeatureState_Inactive;
}

void WorldRendererTerrain_LogMaterials(const WorldRenderer* self)
{
    if (self->_logger == NULL)
    {
        return;
    }
    static const char* const KindNames[TerrainMaterialMapKind_Count] =
    {
        "albedo and height", "normal", "roughness and AO"
    };
    const WorldRendererTerrainMeshes* Entries = (const WorldRendererTerrainMeshes*)(const void*)
        self->_terrain._meshes._data;
    for (size_t Index = 0U; Index < self->_terrain._meshes._count; Index++)
    {
        const WorldRendererTerrainMeshes* Record = &Entries[Index];
        char Line[MATERIALS_LINE_CAPACITY];
        size_t Used = 0U;
        const unsigned char* Name = TerrainAsset_GetName(Record->_asset);
        uint32_t MaterialCount = TerrainAsset_GetMaterialCount(Record->_asset);
        int Written = snprintf(Line, sizeof(Line), "WorldRenderer: terrain \"%s\": %u materials (",
            (Name != NULL) ? (const char*)Name : "?", MaterialCount);
        Used = (Written > 0) ? (size_t)Written : 0U;
        for (uint32_t Palette = 0U; (Palette < MaterialCount) && (Used < sizeof(Line)); Palette++)
        {
            const unsigned char* MaterialName = TerrainAsset_GetMaterialName(Record->_asset, Palette);
            Written = snprintf(&Line[Used], sizeof(Line) - Used, "%s%s", (Palette > 0U) ? ", " : "",
                (MaterialName != NULL) ? (const char*)MaterialName : "?");
            Used += (Written > 0) ? (size_t)Written : 0U;
        }
        uint32_t RuleCount = 0U;
        TerrainAsset_GetRules(Record->_asset, &RuleCount);
        if (Used < sizeof(Line))
        {
            Written = snprintf(&Line[Used], sizeof(Line) - Used, "), %u rules; ", RuleCount);
            Used += (Written > 0) ? (size_t)Written : 0U;
        }
        if (!Record->_hasArrays && (Used < sizeof(Line)))
        {
            snprintf(&Line[Used], sizeof(Line) - Used, "no texture arrays (its materials' mean colours).");
        }
        for (uint32_t Kind = 0U; Record->_hasArrays && (Kind < (uint32_t)TerrainMaterialMapKind_Count)
            && (Used < sizeof(Line)); Kind++)
        {
            Written = snprintf(&Line[Used], sizeof(Line) - Used, "%s%s %d x %d", (Kind > 0U) ? ", " : "arrays ",
                KindNames[Kind], (int)Record->_arrayWidths[Kind], (int)Record->_arrayHeights[Kind]);
            Used += (Written > 0) ? (size_t)Written : 0U;
        }
        if (Record->_hasArrays && (Used < sizeof(Line)))
        {
            snprintf(&Line[Used], sizeof(Line) - Used, ", %u layers each.", MaterialCount);
        }
        Error LogResult = Logger_LogInfo(self->_logger, (const unsigned char*)Line);
        Error_Deconstruct(&LogResult);
    }
}
