// The screen-space reflections (the march's maths are WorldReflections.h's): the scene target's reflection attachments
// the opaque world's lit surfaces hand their data to (shaders/common/reflection_output.glsl), the switches around those
// draws, the block image (each block's nearest surface: shaders/reflections/blocks.frag), the pass that marches every
// mirroring pixel's ray over the scene's depth block by block (shaders/reflections/fragment.frag) and adds what it
// mirrors to the scene's colour, its debug views and its status.

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"


// Macros.
/** Asset names of the reflection pass's program and of its block image's. */
#define REFLECTIONS_SHADER_ASSET_NAME ((const unsigned char*)u8"world_reflections")
#define REFLECTION_BLOCKS_SHADER_ASSET_NAME ((const unsigned char*)u8"world_reflection_blocks")
/** The draw buffers the opaque world draws into while the pass runs: the colour and the reflection attachments. */
#define REFLECTIONS_SURFACE_DRAW_BUFFERS (1 + WORLD_RENDERER_REFLECTION_DATA_COUNT)
/** A disc's cosine that no direction reaches: the moon's while it is not drawn. */
#define REFLECTIONS_NO_DISC_COSINE 2.0f


// Static functions.
/* Points a sampler uniform at a fixed texture unit, once at load. */
static void AssignSamplerUnit(Shader shader, int location, WorldRendererTextureUnit unit)
{
    int Unit = (int)unit;
    SetShaderValue(shader, location, &Unit, SHADER_UNIFORM_INT);
}

/* Binds a 2D texture on a fixed unit for the rectangle drawn next (the 2D batch binds only units 0 to 4). */
static void BindTexture2D(WorldRendererTextureUnit unit, unsigned int id)
{
    rlActiveTextureSlot((int)unit);
    rlEnableTexture(id);
    rlActiveTextureSlot(0);
}

/* The disc's cosine the sky's answer leaves out: the disc's angular radius plus the margin. */
static float DiscCosine(float radiusDegrees)
{
    float Degrees = fminf(fmaxf(radiusDegrees, 0.0f) + WORLD_REFLECTIONS_DISC_MARGIN_DEGREES, 90.0f);
    return cosf(Degrees * DEG2RAD);
}

/* Unloads the attachments' textures (the scene's framebuffer keeps their slots until it goes). */
static void UnloadData(WorldRendererReflectionState* state)
{
    for (int Index = 0; Index < WORLD_RENDERER_REFLECTION_DATA_COUNT; Index++)
    {
        if (state->_dataTextures[Index] != 0U)
        {
            rlUnloadTexture(state->_dataTextures[Index]);
            state->_dataTextures[Index] = 0U;
        }
    }
    state->_hasData = false;
}

/* A texture of the attachments' format at a size, read pixel by pixel (point, clamped); 0 when it could not be made. */
static unsigned int LoadDataTexture(int width, int height)
{
    unsigned int Id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    if (Id != 0U)
    {
        Texture2D Wrapper = { .id = Id, .width = width, .height = height, .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16 };
        SetTextureFilter(Wrapper, TEXTURE_FILTER_POINT);
        SetTextureWrap(Wrapper, TEXTURE_WRAP_CLAMP);
    }
    return Id;
}

/* Takes the reflection attachments off the scene's framebuffer (its colour attachment 0 stays) and unloads them. */
static void DetachData(WorldRenderer* self)
{
    WorldRendererReflectionState* State = &self->_reflections;
    for (int Index = 0; Index < WORLD_RENDERER_REFLECTION_DATA_COUNT; Index++)
    {
        if ((State->_dataTextures[Index] != 0U) && (self->_scene._target.id != 0U))
        {
            rlFramebufferAttach(self->_scene._target.id, 0U, RL_ATTACHMENT_COLOR_CHANNEL1 + Index,
                RL_ATTACHMENT_TEXTURE2D, 0);
        }
    }
    UnloadData(State);
}

/* Gives the scene's framebuffer the reflection attachments (colour attachments 1 to 3); false (and nothing attached
 * that stays) when one could not be made or the framebuffer is not complete with them. */
static bool AttachData(WorldRenderer* self, int width, int height)
{
    WorldRendererReflectionState* State = &self->_reflections;
    for (int Index = 0; Index < WORLD_RENDERER_REFLECTION_DATA_COUNT; Index++)
    {
        State->_dataTextures[Index] = LoadDataTexture(width, height);
        if (State->_dataTextures[Index] == 0U)
        {
            DetachData(self);
            return false;
        }
        rlFramebufferAttach(self->_scene._target.id, State->_dataTextures[Index], RL_ATTACHMENT_COLOR_CHANNEL1 + Index,
            RL_ATTACHMENT_TEXTURE2D, 0);
    }
    if (!rlFramebufferComplete(self->_scene._target.id))
    {
        DetachData(self);
        return false;
    }
    State->_hasData = true;
    return true;
}

/* Makes the block image: a texel for every block of the scene (rounded up), one float each; false when it could not be
 * made. */
static bool MakeBlocks(WorldRendererReflectionState* state, int width, int height)
{
    WorldRendererCommon_UnloadTarget(&state->_blocks, &state->_hasBlocks);
    int Columns = (width + WORLD_REFLECTIONS_BLOCK_SIZE - 1) / WORLD_REFLECTIONS_BLOCK_SIZE;
    int Rows = (height + WORLD_REFLECTIONS_BLOCK_SIZE - 1) / WORLD_REFLECTIONS_BLOCK_SIZE;
    state->_blocks = WorldRendererCommon_CreateColorTarget(Columns, Rows, PIXELFORMAT_UNCOMPRESSED_R32);
    state->_hasBlocks = (state->_blocks.id != 0U);
    if (state->_hasBlocks)
    {
        SetTextureFilter(state->_blocks.texture, TEXTURE_FILTER_POINT);
        SetTextureWrap(state->_blocks.texture, TEXTURE_WRAP_CLAMP);
    }
    return state->_hasBlocks;
}

/* Makes the pass's target at the scene's size over a divisor (rounded up); false when it could not be made. */
static bool MakeTarget(WorldRendererReflectionState* state, int width, int height, int32_t divisor)
{
    WorldRendererCommon_UnloadTarget(&state->_target, &state->_hasTarget);
    int32_t Divisor = (divisor < 1) ? 1 : divisor;
    int TargetWidth = (width + Divisor - 1) / Divisor;
    int TargetHeight = (height + Divisor - 1) / Divisor;
    state->_target = WorldRendererCommon_CreateColorTarget(TargetWidth, TargetHeight,
        PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    state->_hasTarget = (state->_target.id != 0U);
    if (!state->_hasTarget)
    {
        return false;
    }
    state->_targetDivisor = Divisor;
    SetTextureFilter(state->_target.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(state->_target.texture, TEXTURE_WRAP_CLAMP);
    return true;
}

/* Makes what the pass needs for the scene's size: the attachments, the block image and the target at the quality's
 * divisor (in the scene's HDR tier with a samplable depth, with both programs); a failure is logged once and marks the
 * size failed. */
static void MakeAll(WorldRenderer* self)
{
    WorldRendererReflectionState* State = &self->_reflections;
    if ((State->_shader == NULL) || (State->_blockShader == NULL) || !self->_scene._isHdr
        || !self->_scene._isDepthSamplable)
    {
        return;
    }
    if (!AttachData(self, State->_width, State->_height))
    {
        State->_isMakeFailed = true;
        WorldRendererCommon_WarnOnce(self, &State->_isFailureReported, (const unsigned char*)u8"WorldRenderer: the scene "
            u8"target could not take its reflection attachments; water and mirroring objects mirror the sky alone.");
        return;
    }
    int32_t Divisor = WorldReflections_GetMarchSettings(State->_quality).ResolutionDivisor;
    if (!MakeBlocks(State, State->_width, State->_height) || !MakeTarget(State, State->_width, State->_height, Divisor))
    {
        State->_isMakeFailed = true;
        WorldRendererCommon_WarnOnce(self, &State->_isFailureReported, (const unsigned char*)u8"WorldRenderer: the "
            u8"reflection pass's targets could not be made; water and mirroring objects mirror the sky alone.");
    }
}

/* Whether a debug view the lit shaders draw over a surface's colour shows this frame (interior lighting's sky view, the
 * shadow levels' view, the rain's views): the pass would add reflections over it. */
static bool IsSurfaceViewShowing(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    return (frame->IsInteriorActive && (self->_interior._view == WorldRendererInteriorView_Sky))
        || self->_shadowLevels._isLevelViewEnabled || (self->_rain._view != WorldRendererRainView_Off);
}

/* The shown models and copies' cells the scene pass draws that mirror their surroundings. */
static size_t CountMirroringObjects(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    size_t Count = 0U;
    size_t ModelCount = 0U;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    size_t VisibleCount = 0U;
    const size_t* Visible = WorldVisibility_GetModels(&self->_visibility, &self->_cache, WorldVisibilityPass_Camera,
        &VisibleCount);
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        const WorldRenderModel* Entry = &Models[Visible[Position]];
        if (Entry->HasReflections && WorldRendererGeometry_PassesFilter(Entry->OmitsPixelation, frame->SceneFilter))
        {
            Count++;
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
        if ((Entry < EntryCount) && Entries[Entry].HasReflections)
        {
            Count++;
        }
    }
    return Count;
}

/* Sets the pass's uniforms for the frame. */
static void UploadPass(const WorldRenderer* self, const WorldRenderFrame* frame, Shader program)
{
    const WorldRendererReflectionState* State = &self->_reflections;
    SetShaderValueMatrix(program, State->_locViewProjection, frame->ViewProjection);
    SetShaderValueMatrix(program, State->_locInvViewProjection, frame->InvViewProjection);
    Vector3 Forward = Vector3Normalize(Vector3Subtract(frame->RenderCamera.target, frame->RenderCamera.position));
    SetShaderValue(program, State->_locCameraForward, &Forward, SHADER_UNIFORM_VEC3);
    float NearFar[2] = { (float)SCENE_NEAR_PLANE, (float)SCENE_FAR_PLANE };
    SetShaderValue(program, State->_locNearFar, NearFar, SHADER_UNIFORM_VEC2);
    int SceneSize[2] = { frame->SceneWidth, frame->SceneHeight };
    SetShaderValue(program, State->_locSceneSize, SceneSize, SHADER_UNIFORM_IVEC2);
    int BlockCount[2] = { State->_blocks.texture.width, State->_blocks.texture.height };
    SetShaderValue(program, State->_locBlockCount, BlockCount, SHADER_UNIFORM_IVEC2);
    int Divisor = (int)State->_targetDivisor;
    SetShaderValue(program, State->_locResolutionDivisor, &Divisor, SHADER_UNIFORM_INT);
    int MaxBlocks = (int)frame->ReflectionMarch.MaxBlocks;
    SetShaderValue(program, State->_locMaxBlocks, &MaxBlocks, SHADER_UNIFORM_INT);
    SetShaderValue(program, State->_locSunDirection, &frame->SunDirection, SHADER_UNIFORM_VEC3);
    // The sun's disc as the sky draws it: SunSizeMultiplier is its diameter in degrees.
    float SunCos = DiscCosine(0.5f * fmaxf(frame->Environment->SunSizeMultiplier, 0.01f));
    SetShaderValue(program, State->_locSunDiscCos, &SunCos, SHADER_UNIFORM_FLOAT);
    SetShaderValue(program, State->_locMoonDirection, &frame->MoonDirection, SHADER_UNIFORM_VEC3);
    float MoonCos = REFLECTIONS_NO_DISC_COSINE;
    if (frame->IsMoonActive)
    {
        MoonCos = DiscCosine(acosf(fminf(fmaxf(frame->MoonDisc.CosRadius, -1.0f), 1.0f)) * RAD2DEG);
    }
    SetShaderValue(program, State->_locMoonDiscCos, &MoonCos, SHADER_UNIFORM_FLOAT);
    int View = (int)State->_view;
    SetShaderValue(program, State->_locView, &View, SHADER_UNIFORM_INT);
    float ViewScale = 1.0f / fmaxf(frame->LastExposure, 1e-6f);
    SetShaderValue(program, State->_locViewScale, &ViewScale, SHADER_UNIFORM_FLOAT);
    WorldRendererCommon_SetDepthConvention(program, State->_locDepthReversed, frame);
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererReflections_Construct(WorldRenderer* self)
{
    WorldRendererReflectionState* State = &self->_reflections;
    *State = (WorldRendererReflectionState){ 0 };
    State->_isEnabled = true;
    State->_quality = (self->_config != NULL) ? self->_config->ReflectionQuality : CONFIG_REFLECTION_QUALITY_HIGH;
    State->_view = WorldRendererReflectionView_Off;
}

void WorldRendererReflections_LoadShader(WorldRenderer* self)
{
    WorldRendererReflectionState* State = &self->_reflections;
    State->_shader = WorldRendererCommon_LoadShader(self, REFLECTIONS_SHADER_ASSET_NAME);
    State->_blockShader = WorldRendererCommon_LoadShader(self, REFLECTION_BLOCKS_SHADER_ASSET_NAME);
    if ((State->_shader == NULL) || (State->_blockShader == NULL))
    {
        return; // one without the other cannot run (WorldRendererReflections_IsAvailable)
    }
    Shader Blocks = GameShader_GetRaylibShader(State->_blockShader);
    AssignSamplerUnit(Blocks, GetShaderLocation(Blocks, "sceneDepth"), WorldRendererTextureUnit_ReflectionSceneDepth);
    State->_blockLocSceneSize = GetShaderLocation(Blocks, "sceneSize");
    State->_blockLocDepthReversed = GetShaderLocation(Blocks, "depthReversed");

    Shader Program = GameShader_GetRaylibShader(State->_shader);
    AssignSamplerUnit(Program, GetShaderLocation(Program, "reflectionWeightMap"),
        WorldRendererTextureUnit_ReflectionWeights);
    AssignSamplerUnit(Program, GetShaderLocation(Program, "reflectionSkyMap"), WorldRendererTextureUnit_ReflectionSky);
    AssignSamplerUnit(Program, GetShaderLocation(Program, "reflectionDirectionMap"),
        WorldRendererTextureUnit_ReflectionDirections);
    AssignSamplerUnit(Program, GetShaderLocation(Program, "sceneColor"), WorldRendererTextureUnit_ReflectionSceneColor);
    AssignSamplerUnit(Program, GetShaderLocation(Program, "sceneDepth"), WorldRendererTextureUnit_ReflectionSceneDepth);
    AssignSamplerUnit(Program, GetShaderLocation(Program, "reflectionBlocks"), WorldRendererTextureUnit_ReflectionBlocks);
    State->_locViewProjection = GetShaderLocation(Program, "viewProjection");
    State->_locInvViewProjection = GetShaderLocation(Program, "invViewProjection");
    State->_locCameraForward = GetShaderLocation(Program, "cameraForward");
    State->_locNearFar = GetShaderLocation(Program, "nearFar");
    State->_locSceneSize = GetShaderLocation(Program, "sceneSize");
    State->_locBlockCount = GetShaderLocation(Program, "blockCount");
    State->_locResolutionDivisor = GetShaderLocation(Program, "resolutionDivisor");
    State->_locMaxBlocks = GetShaderLocation(Program, "maxBlocks");
    State->_locSunDirection = GetShaderLocation(Program, "sunDirection");
    State->_locSunDiscCos = GetShaderLocation(Program, "sunDiscCos");
    State->_locMoonDirection = GetShaderLocation(Program, "moonDirection");
    State->_locMoonDiscCos = GetShaderLocation(Program, "moonDiscCos");
    State->_locView = GetShaderLocation(Program, "reflectionView");
    State->_locViewScale = GetShaderLocation(Program, "viewScale");
    State->_locDepthReversed = GetShaderLocation(Program, "depthReversed");
}

void WorldRendererReflections_CreateTargets(WorldRenderer* self, int width, int height)
{
    WorldRendererReflectionState* State = &self->_reflections;
    State->_width = width;
    State->_height = height;
    State->_isMakeFailed = false;
    if (State->_quality != CONFIG_REFLECTION_QUALITY_OFF)
    {
        MakeAll(self);
    }
}

void WorldRendererReflections_ReleaseTargets(WorldRenderer* self)
{
    WorldRendererReflectionState* State = &self->_reflections;
    WorldRendererCommon_UnloadTarget(&State->_target, &State->_hasTarget);
    WorldRendererCommon_UnloadTarget(&State->_blocks, &State->_hasBlocks);
    DetachData(self);
}

void WorldRendererReflections_EnsureTarget(WorldRenderer* self)
{
    WorldRendererReflectionState* State = &self->_reflections;
    if (State->_quality == CONFIG_REFLECTION_QUALITY_OFF)
    {
        // Nothing held while the player wants no reflections; a quality chosen again tries anew.
        WorldRendererReflections_ReleaseTargets(self);
        State->_isMakeFailed = false;
        return;
    }
    if (State->_isMakeFailed)
    {
        return;
    }
    if (!State->_hasData)
    {
        MakeAll(self);
        return;
    }
    int32_t Divisor = WorldReflections_GetMarchSettings(State->_quality).ResolutionDivisor;
    if (State->_hasTarget && (State->_targetDivisor == Divisor))
    {
        return;
    }
    if (!MakeTarget(State, State->_width, State->_height, Divisor))
    {
        State->_isMakeFailed = true;
        WorldRendererCommon_WarnOnce(self, &State->_isFailureReported, (const unsigned char*)u8"WorldRenderer: the "
            u8"reflection pass's target could not be made; water and mirroring objects mirror the sky alone.");
    }
}

bool WorldRendererReflections_IsAvailable(const WorldRenderer* self)
{
    const WorldRendererReflectionState* State = &self->_reflections;
    return (State->_shader != NULL) && (State->_blockShader != NULL) && (self->_pbr._rigid._shader != NULL)
        && (self->_terrain._shader != NULL) && self->_scene._isHdr && self->_scene._isDepthSamplable
        && !State->_isMakeFailed;
}

void WorldRendererReflections_Decide(WorldRenderer* self, WorldRenderFrame* frame)
{
    WorldRendererReflectionState* State = &self->_reflections;
    frame->ReflectionMarch = WorldReflections_GetMarchSettings(State->_quality);
    frame->AreWetSurfacesMirroring = frame->IsRainActive && (frame->Weather.Wetness > 0.0f);
    frame->MirroringObjectCount = CountMirroringObjects(self, frame) + frame->WaterMirrorCount;
    frame->AreReflectionsActive = WorldRendererReflections_IsAvailable(self) && State->_hasData && State->_hasBlocks
        && State->_hasTarget && State->_isEnabled && (State->_quality != CONFIG_REFLECTION_QUALITY_OFF)
        && !IsSurfaceViewShowing(self, frame) && ((frame->MirroringObjectCount > 0U) || frame->AreWetSurfacesMirroring);
}

void WorldRendererReflections_ClearData(const WorldRenderFrame* frame)
{
    if (!frame->AreReflectionsActive)
    {
        return;
    }
    rlDrawRenderBatchActive();
    rlActiveDrawBuffers(REFLECTIONS_SURFACE_DRAW_BUFFERS);
    ClearBackground(BLANK);
    rlActiveDrawBuffers(1);
}

void WorldRendererReflections_BeginSurfaces(WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (!frame->AreReflectionsActive)
    {
        return;
    }
    rlDrawRenderBatchActive();
    rlActiveDrawBuffers(REFLECTIONS_SURFACE_DRAW_BUFFERS);
    self->_reflections._areSurfacesWriting = true;
    self->_reflections._areSurfacesPaused = false;
}

void WorldRendererReflections_EndSurfaces(WorldRenderer* self)
{
    WorldRendererReflectionState* State = &self->_reflections;
    if (State->_areSurfacesWriting || State->_areSurfacesPaused)
    {
        rlDrawRenderBatchActive();
        rlActiveDrawBuffers(1);
    }
    State->_areSurfacesWriting = false;
    State->_areSurfacesPaused = false;
}

void WorldRendererReflections_PauseSurfaces(WorldRenderer* self)
{
    WorldRendererReflectionState* State = &self->_reflections;
    if (!State->_areSurfacesWriting)
    {
        return;
    }
    rlDrawRenderBatchActive();
    rlActiveDrawBuffers(1);
    State->_areSurfacesWriting = false;
    State->_areSurfacesPaused = true;
}

void WorldRendererReflections_ResumeSurfaces(WorldRenderer* self)
{
    WorldRendererReflectionState* State = &self->_reflections;
    if (!State->_areSurfacesPaused)
    {
        return;
    }
    rlDrawRenderBatchActive();
    rlActiveDrawBuffers(REFLECTIONS_SURFACE_DRAW_BUFFERS);
    State->_areSurfacesWriting = true;
    State->_areSurfacesPaused = false;
}

void WorldRendererReflections_Render(WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (!frame->AreReflectionsActive)
    {
        return;
    }
    WorldRendererReflectionState* State = &self->_reflections;

    // Each block's nearest surface, from the scene's depth.
    Shader Blocks = GameShader_GetRaylibShader(State->_blockShader);
    int SceneSize[2] = { frame->SceneWidth, frame->SceneHeight };
    SetShaderValue(Blocks, State->_blockLocSceneSize, SceneSize, SHADER_UNIFORM_IVEC2);
    WorldRendererCommon_SetDepthConvention(Blocks, State->_blockLocDepthReversed, frame);
    BeginTextureMode(State->_blocks);
    rlDisableColorBlend(); // every texel as computed
    BeginShaderMode(Blocks);
    BindTexture2D(WorldRendererTextureUnit_ReflectionSceneDepth, self->_scene._target.depth.id);
    DrawRectangle(0, 0, State->_blocks.texture.width, State->_blocks.texture.height, WHITE);
    EndShaderMode();
    rlEnableColorBlend();
    EndTextureMode();

    // Every pixel of the pass's target: its scene pixel's ray, or nothing where no surface mirrors. The shader reads
    // every input by its pixel (texelFetch).
    Shader Program = GameShader_GetRaylibShader(State->_shader);
    UploadPass(self, frame, Program);
    BeginTextureMode(State->_target);
    rlDisableColorBlend(); // every pixel as computed (its alpha is 0: the composite leaves the scene's alpha)
    BeginShaderMode(Program);
    BindTexture2D(WorldRendererTextureUnit_ReflectionWeights, State->_dataTextures[0]);
    BindTexture2D(WorldRendererTextureUnit_ReflectionSky, State->_dataTextures[1]);
    BindTexture2D(WorldRendererTextureUnit_ReflectionDirections, State->_dataTextures[2]);
    BindTexture2D(WorldRendererTextureUnit_ReflectionSceneColor, self->_scene._target.texture.id);
    BindTexture2D(WorldRendererTextureUnit_ReflectionSceneDepth, self->_scene._target.depth.id);
    BindTexture2D(WorldRendererTextureUnit_ReflectionBlocks, State->_blocks.texture.id);
    DrawRectangle(0, 0, State->_target.texture.width, State->_target.texture.height, WHITE);
    EndShaderMode();
    rlEnableColorBlend();
    EndTextureMode();

    // Added to the scene's colour (point-sampled: a low quality's pixel covers its block of scene pixels, the one it was
    // computed at in its bottom left corner; past an odd size's last column and top row the blocks hang over the edge
    // and are cut off). The pass wrote 0 in alpha, so the scene's alpha stays.
    BeginTextureMode(self->_scene._target);
    BeginBlendMode(BLEND_ADD_COLORS);
    Rectangle Flipped = { 0.0f, 0.0f, (float)State->_target.texture.width, -(float)State->_target.texture.height };
    float CoveredWidth = (float)(State->_target.texture.width * State->_targetDivisor);
    float CoveredHeight = (float)(State->_target.texture.height * State->_targetDivisor);
    Rectangle Scene = { 0.0f, (float)frame->SceneHeight - CoveredHeight, CoveredWidth, CoveredHeight };
    DrawTexturePro(State->_target.texture, Flipped, Scene, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndBlendMode();
    EndTextureMode();
}

void WorldRendererReflections_UpdateStatus(WorldRenderer* self, const WorldRenderFrame* frame)
{
    const WorldRendererReflectionState* State = &self->_reflections;
    WorldRendererStatus* Status = &self->_status;
    bool IsAvailable = WorldRendererReflections_IsAvailable(self);
    Status->Features[WorldRendererFeature_Reflections] = !IsAvailable ? WorldRendererFeatureState_Unavailable
        : (frame->AreReflectionsActive ? WorldRendererFeatureState_Active : WorldRendererFeatureState_Inactive);
    Status->AreReflectionsEnabled = State->_isEnabled;
    Status->ReflectionQuality = State->_quality;
    Status->ReflectionTargetWidth = State->_hasTarget ? (int32_t)State->_target.texture.width : 0;
    Status->ReflectionTargetHeight = State->_hasTarget ? (int32_t)State->_target.texture.height : 0;
    Status->MirroringObjectCount = frame->MirroringObjectCount;
    Status->AreWetSurfacesMirroring = frame->AreWetSurfacesMirroring;
    Status->ReflectionView = State->_view;
}

void WorldRendererReflections_LogStatus(const WorldRenderer* self)
{
    if (self->_logger == NULL)
    {
        return;
    }
    const WorldRendererStatus* Status = &self->_status;
    static const char* const QualityNames[] = { "off", "low", "high" };
    const char* Quality = ((Status->ReflectionQuality >= CONFIG_REFLECTION_QUALITY_OFF)
        && (Status->ReflectionQuality <= CONFIG_REFLECTION_QUALITY_HIGH))
        ? QualityNames[Status->ReflectionQuality] : "unknown";
    Error LogResult = Logger_LogInfoFormatted(self->_logger, (const unsigned char*)u8"WorldRenderer: reflections %s, "
        u8"quality %s, target %" PRId32 u8"x%" PRId32 u8", %zu mirroring objects and cells in view, wet surfaces %s, view %s.",
        Status->AreReflectionsEnabled ? "on" : "off", Quality, Status->ReflectionTargetWidth,
        Status->ReflectionTargetHeight, Status->MirroringObjectCount, Status->AreWetSurfacesMirroring ? "mirror" : "dry",
        (const char*)WorldRenderer_GetReflectionViewName(Status->ReflectionView));
    Error_Deconstruct(&LogResult);
}
