// The water (the moving patterns' maths are WorldWater.h's): the world models' water slots drawn over the opaque world in
// the scene pass with their water materials (shaders/water/, shaders/common/water.glsl), from copies of the scene's
// colour and depth (what lies under the water), lit as the PBR shader lights and handing their reflections to the
// reflection pass; the drift of every water's patterns; the status.

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "GameTexture.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"


// Macros.
/** Asset names of the water's program and of its instanced twin. */
#define WATER_SHADER_ASSET_NAME ((const unsigned char*)u8"world_water")
#define WATER_INSTANCED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_water_instanced")
/** The buffers rlBlitFramebuffer copies (GL_COLOR_BUFFER_BIT, GL_DEPTH_BUFFER_BIT): rlgl takes GL's mask as it is. */
#define WATER_BLIT_COLOR_BIT 0x00004000
#define WATER_BLIT_DEPTH_BIT 0x00000100


// Static functions.
/* Points a sampler uniform at a fixed texture unit, once at load. */
static void AssignSamplerUnit(Shader shader, int location, WorldRendererTextureUnit unit)
{
    int Unit = (int)unit;
    SetShaderValue(shader, location, &Unit, SHADER_UNIFORM_INT);
}

/* Binds a 2D texture (0: none) on a fixed unit. */
static void BindTexture2D(WorldRendererTextureUnit unit, unsigned int id)
{
    rlActiveTextureSlot((int)unit);
    rlEnableTexture(id);
    rlActiveTextureSlot(0);
}

/* Caches a water program's own uniforms and points its samplers at the water's units. */
static void PrepareWaterLocations(Shader shader, WorldRendererWaterLocations* outLocations)
{
    AssignSamplerUnit(shader, GetShaderLocation(shader, "waterNormalA"), WorldRendererTextureUnit_WaterNormalA);
    AssignSamplerUnit(shader, GetShaderLocation(shader, "waterNormalB"), WorldRendererTextureUnit_WaterNormalB);
    AssignSamplerUnit(shader, GetShaderLocation(shader, "waterFoam"), WorldRendererTextureUnit_WaterFoam);
    AssignSamplerUnit(shader, GetShaderLocation(shader, "waterSceneColor"), WorldRendererTextureUnit_WaterSceneColor);
    AssignSamplerUnit(shader, GetShaderLocation(shader, "waterSceneDepth"), WorldRendererTextureUnit_WaterSceneDepth);
    outLocations->_locHasNormalA = GetShaderLocation(shader, "waterHasNormalA");
    outLocations->_locHasNormalB = GetShaderLocation(shader, "waterHasNormalB");
    outLocations->_locHasFoam = GetShaderLocation(shader, "waterHasFoam");
    outLocations->_locMapA = GetShaderLocation(shader, "waterMapA");
    outLocations->_locMapB = GetShaderLocation(shader, "waterMapB");
    outLocations->_locMapFoam = GetShaderLocation(shader, "waterMapFoam");
    outLocations->_locInverseTiles = GetShaderLocation(shader, "waterInverseTiles");
    outLocations->_locIsAlongUv = GetShaderLocation(shader, "waterIsAlongUv");
    outLocations->_locWaveStrength = GetShaderLocation(shader, "waterWaveStrength");
    outLocations->_locColor = GetShaderLocation(shader, "waterColor");
    outLocations->_locAbsorption = GetShaderLocation(shader, "waterAbsorption");
    outLocations->_locRoughness = GetShaderLocation(shader, "waterRoughness");
    outLocations->_locReflectance = GetShaderLocation(shader, "waterReflectance");
    outLocations->_locFoamShape = GetShaderLocation(shader, "waterFoamShape");
    outLocations->_locHasRipples = GetShaderLocation(shader, "waterHasRipples");
    outLocations->_locHasReflections = GetShaderLocation(shader, "waterHasReflections");
    outLocations->_locHasBottom = GetShaderLocation(shader, "waterHasBottom");
    outLocations->_locInvViewProjection = GetShaderLocation(shader, "waterInvViewProjection");
    outLocations->_locDepthReversed = GetShaderLocation(shader, "depthReversed");
}

/* Loads one water program: a lit program with the water's uniforms. */
static void LoadProgram(WorldRenderer* self, WorldRendererWaterProgram* program, const unsigned char* assetName)
{
    WorldRendererLighting_LoadPbrProgram(self, &program->_program, assetName);
    if (program->_program._shader != NULL)
    {
        PrepareWaterLocations(GameShader_GetRaylibShader(program->_program._shader), &program->_water);
    }
}

/* Whether a cached model's world model has water slots. */
static bool HasWater(const WorldModel* asset)
{
    return (asset != NULL) && (WorldModel_GetWaterCount(asset) > 0U);
}

/* Whether any of a world model's water slots mirrors its surroundings with the water material a variant gives it. */
static bool DoesWaterMirror(const WorldModel* asset, bool hasVariant, size_t variantIndex)
{
    uint32_t Count = HasWater(asset) ? WorldModel_GetWaterCount(asset) : 0U;
    for (uint32_t Water = 0U; Water < Count; Water++)
    {
        const WaterMaterial* Look = WorldModel_GetWaterMaterial(asset, hasVariant, variantIndex, Water);
        if ((Look != NULL) && WaterMaterial_GetSettings(Look)->HasReflections)
        {
            return true;
        }
    }
    return false;
}

/* Makes the copies of the scene's colour and depth for its size, as the scene target is made (the same formats, so one
 * blit copies both); a failure is logged once and marks the size failed. */
static void MakeCopies(WorldRenderer* self)
{
    WorldRendererWaterState* State = &self->_water;
    State->_copies = WorldRendererCommon_TryCreateHdrTarget(self, State->_width, State->_height, true);
    State->_hasCopies = (State->_copies.id != 0U);
    if (!State->_hasCopies)
    {
        State->_isCopyFailed = true;
        WorldRendererCommon_WarnOnce(self, &State->_isCopyFailureReported, (const unsigned char*)u8"WorldRenderer: the "
            u8"water's copies of the scene could not be made; water shows no bottom, as deep water.");
        return;
    }
    SetTextureFilter(State->_copies.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(State->_copies.texture, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(State->_copies.depth, TEXTURE_FILTER_POINT);
}

/* Copies the scene's colour and depth as the opaque world left them into the copies (the scene target stays bound
 * after it), and binds them for the water's programs. */
static void CopyScene(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererWaterState* State = &self->_water;
    rlDrawRenderBatchActive();
    rlBindFramebuffer(RL_READ_FRAMEBUFFER, self->_scene._target.id);
    rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, State->_copies.id);
    rlBlitFramebuffer(0, 0, frame->SceneWidth, frame->SceneHeight, 0, 0, frame->SceneWidth, frame->SceneHeight,
        WATER_BLIT_COLOR_BIT | WATER_BLIT_DEPTH_BIT);
    rlEnableFramebuffer(self->_scene._target.id);
    BindTexture2D(WorldRendererTextureUnit_WaterSceneColor, State->_copies.texture.id);
    BindTexture2D(WorldRendererTextureUnit_WaterSceneDepth, State->_copies.depth.id);
}

/* Sets a water program's pass uniforms: whether the copies hold what lies under the water, the scene's inverse
 * view-projection and the depth convention. */
static void UploadPass(const WorldRendererWaterProgram* program, const WorldRenderFrame* frame)
{
    if (program->_program._shader == NULL)
    {
        return;
    }
    Shader Program = GameShader_GetRaylibShader(program->_program._shader);
    int HasBottom = frame->IsWaterBottomActive ? 1 : 0;
    SetShaderValue(Program, program->_water._locHasBottom, &HasBottom, SHADER_UNIFORM_INT);
    SetShaderValueMatrix(Program, program->_water._locInvViewProjection, frame->InvViewProjection);
    WorldRendererCommon_SetDepthConvention(Program, program->_water._locDepthReversed, frame);
}

/* Sets a water material's uniforms on a program: its maps' frames for the frame's origin and drift, its look. */
static void UploadMaterial(const WorldRenderer* self, const WorldRendererWaterProgram* program,
    const WaterMaterial* water)
{
    Shader Program = GameShader_GetRaylibShader(program->_program._shader);
    const WorldRendererWaterLocations* Locations = &program->_water;
    const WaterMaterialSettings* Settings = WaterMaterial_GetSettings(water);
    WorldWaterFrame Frame;
    WorldWater_GetFrame(Settings, &self->_water._drift, self->_water._origin, self->_water._windSpeed, &Frame);
    int HasMaps[WaterMaterialMap_Count];
    for (uint32_t Map = 0U; Map < (uint32_t)WaterMaterialMap_Count; Map++)
    {
        HasMaps[Map] = (WaterMaterial_GetMap(water, (WaterMaterialMap)Map) != NULL) ? 1 : 0;
    }
    SetShaderValue(Program, Locations->_locHasNormalA, &HasMaps[WaterMaterialMap_NormalA], SHADER_UNIFORM_INT);
    SetShaderValue(Program, Locations->_locHasNormalB, &HasMaps[WaterMaterialMap_NormalB], SHADER_UNIFORM_INT);
    SetShaderValue(Program, Locations->_locHasFoam, &HasMaps[WaterMaterialMap_Foam], SHADER_UNIFORM_INT);
    const int MapLocations[WaterMaterialMap_Count] = { Locations->_locMapA, Locations->_locMapB, Locations->_locMapFoam };
    float InverseTiles[WaterMaterialMap_Count];
    for (uint32_t Map = 0U; Map < (uint32_t)WaterMaterialMap_Count; Map++)
    {
        const WorldWaterMapFrame* MapFrame = &Frame.Maps[Map];
        float Values[4] = { MapFrame->Offset[0], MapFrame->Offset[1], MapFrame->Cos, MapFrame->Sin };
        SetShaderValue(Program, MapLocations[Map], Values, SHADER_UNIFORM_VEC4);
        InverseTiles[Map] = MapFrame->InverseTile;
    }
    SetShaderValue(Program, Locations->_locInverseTiles, InverseTiles, SHADER_UNIFORM_VEC3);
    int IsAlongUv = Frame.IsAlongUv ? 1 : 0;
    SetShaderValue(Program, Locations->_locIsAlongUv, &IsAlongUv, SHADER_UNIFORM_INT);
    SetShaderValue(Program, Locations->_locWaveStrength, &Frame.WaveStrength, SHADER_UNIFORM_FLOAT);
    float Scatter[3];
    WorldRendererCommon_ColorToLinear(Settings->ScatterColor, Scatter);
    SetShaderValue(Program, Locations->_locColor, Scatter, SHADER_UNIFORM_VEC3);
    float Absorption[3] = { Settings->Absorption.x, Settings->Absorption.y, Settings->Absorption.z };
    SetShaderValue(Program, Locations->_locAbsorption, Absorption, SHADER_UNIFORM_VEC3);
    SetShaderValue(Program, Locations->_locRoughness, &Settings->Roughness, SHADER_UNIFORM_FLOAT);
    float Reflectance = WATER_MATERIAL_WATER_REFLECTANCE * Settings->Reflectance;
    SetShaderValue(Program, Locations->_locReflectance, &Reflectance, SHADER_UNIFORM_FLOAT);
    float FoamShape[2] = { Settings->FoamDepth, Settings->FoamStrength };
    SetShaderValue(Program, Locations->_locFoamShape, FoamShape, SHADER_UNIFORM_VEC2);
    int HasRipples = Settings->HasRipples ? 1 : 0;
    SetShaderValue(Program, Locations->_locHasRipples, &HasRipples, SHADER_UNIFORM_INT);
    int HasReflections = Settings->HasReflections ? 1 : 0;
    SetShaderValue(Program, Locations->_locHasReflections, &HasReflections, SHADER_UNIFORM_INT);
}

/* Binds a water material's maps on the water's units (none where it has no map: the shader reads only those it has). */
static void BindMaps(const WaterMaterial* water)
{
    const WorldRendererTextureUnit Units[WaterMaterialMap_Count] =
    {
        WorldRendererTextureUnit_WaterNormalA, WorldRendererTextureUnit_WaterNormalB, WorldRendererTextureUnit_WaterFoam
    };
    for (uint32_t Map = 0U; Map < (uint32_t)WaterMaterialMap_Count; Map++)
    {
        const GameTexture* MapTexture = WaterMaterial_GetMap(water, (WaterMaterialMap)Map);
        BindTexture2D(Units[Map], (MapTexture != NULL) ? GameTexture_GetRaylibTexture(MapTexture).id : 0U);
    }
}

/* Draws the camera's models' water slots: each model with water, with its forward lights and its draw place, its LODs
 * dithered by its fade and its part of a cross-fade (a model with water is never crisp: WorldRenderCache.h).
 * Returns the draws. */
static size_t DrawModels(WorldRenderer* self)
{
    WorldRendererWaterProgram* Program = &self->_water._rigid;
    Shader Pass = Program->_program._passShader;
    WorldRendererMeshShaders Shaders =
    {
        .Rigid = &Program->_program._passShader, .Skinned = NULL, .Palette = NULL, .IsCameraView = true,
        .Meshes = WorldRendererMeshSet_Water, .Water = Program
    };
    size_t Draws = 0U;
    size_t ModelCount = 0U;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    size_t VisibleCount = 0U;
    const size_t* Visible = WorldVisibility_GetModels(&self->_visibility, &self->_cache, WorldVisibilityPass_Camera,
        &VisibleCount);
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        size_t Index = Visible[Position];
        const WorldRenderModel* Entry = &Models[Index];
        if (!HasWater(Entry->Asset))
        {
            continue;
        }
        float Fade = WorldVisibility_GetModelFade(&self->_visibility, Index).Combined;
        // Past half its fade the G-buffer left it out: the ambient pre-pass saw what lies behind it. Its own water
        // material says whether it mirrors.
        WorldRendererLighting_UploadObjectLights(self, Pass, &Program->_program._lit, Entry->RenderCenter, Entry->Radius);
        WorldRendererLighting_SetDrawPlace(self, Pass, &Program->_program._lit, Entry->RenderCenter, Entry->Radius,
            Entry->Haze, Fade < WORLD_VISIBILITY_GBUFFER_FADE_LIMIT, false);
        WorldLodDraw LodDraws[WORLD_LOD_MAX_DRAWS];
        size_t DrawCount = WorldLodState_GetDraws(&Entry->Lod, LodDraws);
        for (size_t Draw = 0U; Draw < DrawCount; Draw++)
        {
            WorldRendererCommon_SetDither(Pass, &Program->_program._dither, Fade, LodDraws[Draw].Progress,
                LodDraws[Draw].IsIncoming);
            WorldRendererGeometry_DrawModel(self, Entry, LodDraws[Draw].Lod, &Shaders, true);
            Draws++;
        }
    }
    return Draws;
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererWater_Construct(WorldRenderer* self)
{
    self->_water = (WorldRendererWaterState){ 0 };
    WorldWaterDrift_Reset(&self->_water._drift);
}

void WorldRendererWater_LoadShaders(WorldRenderer* self)
{
    LoadProgram(self, &self->_water._rigid, WATER_SHADER_ASSET_NAME);
    if (self->_water._rigid._program._shader == NULL)
    {
        return; // the copies' water draws one at a time through the rigid one: nothing without it
    }
    LoadProgram(self, &self->_water._instanced, WATER_INSTANCED_SHADER_ASSET_NAME);
}

void WorldRendererWater_CreateTargets(WorldRenderer* self, int width, int height)
{
    WorldRendererWater_ReleaseTargets(self);
    self->_water._width = width;
    self->_water._height = height;
    self->_water._isCopyFailed = false;
}

void WorldRendererWater_ReleaseTargets(WorldRenderer* self)
{
    WorldRendererCommon_UnloadTarget(&self->_water._copies, &self->_water._hasCopies);
}

bool WorldRendererWater_IsAvailable(const WorldRenderer* self)
{
    return self->_water._rigid._program._shader != NULL;
}

void WorldRendererWater_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame, double deltaSeconds)
{
    WorldRendererWaterState* State = &self->_water;
    const WorldEnvironment* Environment = frame->Environment;
    float Wind = Environment->WindSpeed + Environment->WindGust;
    WorldWaterDrift_Advance(&State->_drift, Environment->WindDirection, Wind, deltaSeconds);
    State->_origin = frame->Origin;
    State->_windSpeed = isfinite(Wind) ? Wind : 0.0f;
    frame->WaterWindSpeed = State->_windSpeed;

    // The models and the copies' cells in view with water slots, and those of them that mirror their surroundings.
    size_t Objects = 0U;
    size_t Mirrors = 0U;
    size_t ModelCount = 0U;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    size_t VisibleCount = 0U;
    const size_t* Visible = WorldVisibility_GetModels(&self->_visibility, &self->_cache, WorldVisibilityPass_Camera,
        &VisibleCount);
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        const WorldRenderModel* Entry = &Models[Visible[Position]];
        if (HasWater(Entry->Asset))
        {
            Objects++;
            Mirrors += DoesWaterMirror(Entry->Asset, Entry->HasVariant, Entry->VariantIndex) ? 1U : 0U;
        }
    }
    size_t EntryCount = 0U;
    const WorldRenderInstances* Entries = WorldRenderCache_GetInstances(&self->_cache, &EntryCount);
    size_t CellCount = 0U;
    const WorldRenderInstanceCell* Cells = WorldRenderCache_GetInstanceCells(&self->_cache, &CellCount);
    const size_t* VisibleCells = WorldVisibility_GetInstanceCells(&self->_visibility, &self->_cache,
        WorldVisibilityPass_Camera, &VisibleCount);
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        size_t Entry = Cells[VisibleCells[Position]].Entry;
        if ((Entry < EntryCount) && HasWater(Entries[Entry].Asset))
        {
            Objects++;
            Mirrors += DoesWaterMirror(Entries[Entry].Asset, Entries[Entry].HasVariant, Entries[Entry].VariantIndex)
                ? 1U : 0U;
        }
    }
    frame->IsWaterActive = WorldRendererWater_IsAvailable(self) && (Objects > 0U);
    frame->WaterObjectCount = frame->IsWaterActive ? Objects : 0U;
    frame->WaterMirrorCount = frame->IsWaterActive ? Mirrors : 0U;

    // What lies under the water, from copies of the scene: only in the scene's HDR tier with a samplable depth.
    if (frame->IsWaterActive && !State->_hasCopies && !State->_isCopyFailed && self->_scene._isHdr
        && self->_scene._isDepthSamplable)
    {
        MakeCopies(self);
    }
    frame->IsWaterBottomActive = frame->IsWaterActive && State->_hasCopies;
}

Material WorldRendererWater_PrepareMaterial(WorldRenderer* self, const WorldRendererWaterProgram* program,
    const WorldRenderModel* model, int materialIndex, MaterialMap* outMaps)
{
    Memory_Zero(outMaps, WORLD_RENDERER_MATERIAL_MAP_COUNT * sizeof(MaterialMap));
    Material DrawMaterial = { .shader = { 0 }, .maps = outMaps };
    uint32_t Water = 0U;
    if ((model->Asset == NULL) || !WorldModel_TryFindWater(model->Asset, (int32_t)materialIndex, &Water))
    {
        return DrawMaterial;
    }
    const WaterMaterial* WaterLook = WorldModel_GetWaterMaterial(model->Asset, model->HasVariant, model->VariantIndex,
        Water);
    if (WaterLook == NULL)
    {
        return DrawMaterial;
    }
    WorldRendererWaterProgram* Program = (program == &self->_water._instanced) ? &self->_water._instanced
        : &self->_water._rigid;
    if (Program->_uploadedMaterial != WaterLook)
    {
        UploadMaterial(self, Program, WaterLook);
        Program->_uploadedMaterial = WaterLook;
    }
    BindMaps(WaterLook);
    DrawMaterial.shader = Program->_program._passShader;
    return DrawMaterial;
}

void WorldRendererWater_Render(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererWaterState* State = &self->_water;
    State->_drawCount = 0U;
    if (!frame->IsWaterActive)
    {
        return;
    }
    // The maps' frames move every frame: every material is uploaded afresh.
    State->_rigid._uploadedMaterial = NULL;
    State->_instanced._uploadedMaterial = NULL;
    if (frame->IsWaterBottomActive)
    {
        CopyScene(self, frame);
    }
    UploadPass(&State->_rigid, frame);
    UploadPass(&State->_instanced, frame);
    State->_drawCount = DrawModels(self) + WorldRendererInstances_DrawWater(self, frame);
}

void WorldRendererWater_UpdateStatus(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererStatus* Status = &self->_status;
    bool IsAvailable = WorldRendererWater_IsAvailable(self);
    Status->Features[WorldRendererFeature_Water] = !IsAvailable ? WorldRendererFeatureState_Unavailable
        : (frame->IsWaterActive ? WorldRendererFeatureState_Active : WorldRendererFeatureState_Inactive);
    Status->WaterObjectCount = frame->WaterObjectCount;
    Status->WaterMirrorCount = frame->WaterMirrorCount;
    Status->WaterDrawCount = self->_water._drawCount;
    Status->IsWaterBottomActive = frame->IsWaterBottomActive;
    Status->WaterWindSpeed = frame->WaterWindSpeed;
}

void WorldRendererWater_LogStatus(const WorldRenderer* self)
{
    if (self->_logger == NULL)
    {
        return;
    }
    const WorldRendererStatus* Status = &self->_status;
    Error LogResult = Logger_LogInfoFormatted(self->_logger, (const unsigned char*)u8"WorldRenderer: water: %zu objects "
        u8"and cells in view with water (%zu mirroring their surroundings), %zu draws, its bottom %s, waves in a %.1f m/s "
        u8"wind.", Status->WaterObjectCount, Status->WaterMirrorCount, Status->WaterDrawCount,
        Status->IsWaterBottomActive ? "seen" : "not seen (deep water)", (double)Status->WaterWindSpeed);
    Error_Deconstruct(&LogResult);
}
