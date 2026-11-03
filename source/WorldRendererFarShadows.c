// The terrain's shadows at any distance (WorldFarShadows.h for the maths and the schedule): every terrain's top surface
// drawn from above into nested height maps around the camera, swept along the shadow light over a few frames, and the
// finished sweep read by every lit surface (shaders/common/far_shadow.glsl). The maps live in float texture arrays
// (GLFloatArray.h, direct GL calls); the passes run before the lit passes, after the shadow levels.

#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "GameMath.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"


// Macros.
/** Asset names of the heights' and the sweep's programs. */
#define HEIGHTS_SHADER_ASSET_NAME ((const unsigned char*)u8"world_far_shadow_heights")
#define SWEEP_SHADER_ASSET_NAME ((const unsigned char*)u8"world_far_shadow_sweep")
/** Room for a warning that quotes an error. */
#define WARNING_CAPACITY 512


// Static functions.
/* Points a sampler uniform at a fixed texture unit (once, at load: sampler values stay with the program). */
static void AssignSamplerUnit(Shader shader, const char* name, WorldRendererTextureUnit unit)
{
    int UnitIndex = (int)unit;
    SetShaderValue(shader, GetShaderLocation(shader, name), &UnitIndex, SHADER_UNIFORM_INT);
}

/* Releases every array (none bound afterwards). */
static void ReleaseTextures(WorldRenderer* self)
{
    WorldRendererFarShadowState* State = &self->_farShadows;
    GLFloatArray_Destroy(&State->_api, &State->_heights);
    for (uint32_t Map = 0U; Map < WORLD_FAR_SHADOW_MAP_COUNT; Map++)
    {
        GLFloatArray_Destroy(&State->_api, &State->_sweeps[Map]);
    }
    GLFloatArray_Destroy(&State->_api, &State->_scratch);
    GLFloatArray_Destroy(&State->_api, &State->_result);
    State->_hasTextures = false;
    State->_textureSize = 0;
}

/* Makes one array, or keeps the first error; true while every one so far was made. */
static bool MakeArray(WorldRenderer* self, int size, int layers, int channels, bool hasDepth, GLFloatArray* outArray,
    Error* ioFirstError)
{
    if (ioFirstError->Code != ErrorCode_Success)
    {
        return false;
    }
    *ioFirstError = GLFloatArray_Create(&self->_farShadows._api, size, layers, channels, hasDepth, outArray);
    return ioFirstError->Code == ErrorCode_Success;
}

/* Makes the arrays for a map size when there are none, or of another size: the heights (a layer per map, with a depth),
 * each map's sweep, the scratch and the result (a layer per map). A failure is logged once per size and leaves none.
 * Returns whether there are arrays of that size; new ones start the schedule afresh. */
static bool EnsureTextures(WorldRenderer* self, int size)
{
    WorldRendererFarShadowState* State = &self->_farShadows;
    if (State->_hasTextures && (State->_textureSize == size))
    {
        return true;
    }
    if (State->_failedTextureSize == size)
    {
        return false;
    }
    ReleaseTextures(self);
    Error First = Error_CreateSuccess();
    int Layers = (int)WORLD_FAR_SHADOW_MAP_COUNT;
    MakeArray(self, size, Layers, 1, true, &State->_heights, &First);
    for (uint32_t Map = 0U; Map < WORLD_FAR_SHADOW_MAP_COUNT; Map++)
    {
        MakeArray(self, size, 1, 2, false, &State->_sweeps[Map], &First);
    }
    MakeArray(self, size, 1, 2, false, &State->_scratch, &First);
    MakeArray(self, size, Layers, 2, false, &State->_result, &First);
    if (First.Code != ErrorCode_Success)
    {
        ReleaseTextures(self);
        char Message[WARNING_CAPACITY];
        snprintf(Message, sizeof(Message), "WorldRenderer: could not make the far shadows' maps of %d texels; terrain "
            "casts no shadows past the shadow levels' reach (%s).", size,
            (First.Message != NULL) ? (const char*)First.Message : "no reason given");
        WorldRendererCommon_Warn(self, (const unsigned char*)Message);
        Error_Deconstruct(&First);
        State->_failedTextureSize = size;
        return false;
    }
    State->_failedTextureSize = 0;
    State->_hasTextures = true;
    State->_textureSize = size;
    WorldFarShadows_Invalidate(&State->_schedule);
    return true;
}

/* Whether the frame wants the far shadows: switched on, the player's quality and the world allow them, the world's
 * shadows run with a strength above 0, there is a shadow light and a terrain is listed. */
static bool AreWanted(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;
    const GameConfig* Config = self->_config;
    bool IsQualityOn = (Config == NULL) || Config->AreFarShadowsEnabled;
    bool AreShadowsWanted = Environment->AreShadowsEnabled
        && ((Environment->ShadowStrength * WorldRendererCommon_ConfigShadowStrength(self)) > 0.0f);
    size_t ChunkCount = 0U;
    WorldRenderCache_GetTerrainChunks(&self->_cache, &ChunkCount);
    return self->_farShadows._isEnabled && IsQualityOn && Environment->AreFarTerrainShadowsEnabled && AreShadowsWanted
        && (frame->ShadowLight != WorldShadowLight_None) && (frame->ShadowFade > 0.0f) && (ChunkCount > 0U);
}

/* The shadow light's disc's angular radius: the sun's, or the moon's as the sky draws it (its size multiplier in). */
static float GetDiscRadius(const WorldRenderFrame* frame)
{
    if (frame->ShadowLight == WorldShadowLight_Moon)
    {
        return asinf(fminf(fmaxf(frame->MoonDisc.SinRadius, 0.0f), 1.0f));
    }
    return WORLD_FAR_SHADOW_SUN_RADIUS;
}

/* A RenderTexture2D for BeginTextureMode over an array's framebuffer (the viewport its size). */
static RenderTexture2D GetTarget(const GLFloatArray* array)
{
    RenderTexture2D Target = { 0 };
    Target.id = GLFloatArray_GetFramebuffer(array);
    Target.texture.width = array->_size;
    Target.texture.height = array->_size;
    return Target;
}

/* Draws the height maps for the frame the schedule chose: each map cleared to no ground, then every terrain chunk that
 * casts within its shadow distance over the map, from above, the depth test keeping the highest surface. */
static void DrawMaps(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererFarShadowState* State = &self->_farShadows;
    const WorldFarShadowFrame* DrawFrame = &frame->FarShadowWork.DrawFrame;
    Shader Heights = GameShader_GetRaylibShader(State->_heightShader);
    float ShadowDistanceScale = WorldRendererCommon_ConfigShadowDistanceScale(self);
    for (uint32_t Map = 0U; Map < WORLD_FAR_SHADOW_MAP_COUNT; Map++)
    {
        Matrix View;
        Matrix Projection;
        WorldFarShadows_GetMapMatrices(Map, &View, &Projection);
        // The heights fall by their drop from the frame's centre: the pass's curve is the frame's, its up the map's.
        float Scale = DrawFrame->CurvatureScale;
        Vector4 ClipUp = WorldRenderSpace_GetClipUp(MatrixMultiply(View, Projection));
        SetShaderValue(Heights, State->_heightCurvature._locScale, &Scale, SHADER_UNIFORM_FLOAT);
        float Up[4] = { ClipUp.x, ClipUp.y, ClipUp.z, ClipUp.w };
        SetShaderValue(Heights, State->_heightCurvature._locClipUp, Up, SHADER_UNIFORM_VEC4);

        GLFloatArray_AttachLayer(&State->_api, &State->_heights, (int)Map);
        BeginTextureMode(GetTarget(&State->_heights));
        GLFloatArray_Clear(&State->_api, &State->_heights, WORLD_FAR_SHADOW_NO_GROUND, 0.0f);
        rlDisableColorBlend(); // the heights as they are (a blend could keep an old NaN)
        rlEnableDepthTest();
        rlDisableBackfaceCulling(); // the view swaps two axes: every face counts, the highest one wins
        rlSetMatrixProjection(Projection);
        rlSetMatrixModelview(View);
        WorldRendererTerrain_DrawFarShadowHeights(self, Heights, DrawFrame->Center,
            WorldFarShadows_GetMapWidth(Map) * 0.5, ShadowDistanceScale, &State->_mapChunkCounts[Map]);
        rlEnableBackfaceCulling();
        rlDisableDepthTest();
        rlEnableColorBlend();
        EndTextureMode();
    }
    State->_mapDrawCount++;
}

/* Runs one pass of the sweep into its target: a full-screen rectangle with the sweep program, its inputs bound on their
 * units by hand (the 2D batch binds only 0 to 4), blending off (every texel as computed). */
static void RunPass(WorldRenderer* self, const WorldRenderFrame* frame, uint32_t index)
{
    WorldRendererFarShadowState* State = &self->_farShadows;
    const WorldFarShadowLight* Light = &frame->FarShadowWork.SweepLight;
    int Size = State->_textureSize;
    WorldFarShadowPass Pass = WorldFarShadows_GetPass(index, Size);
    const GLFloatArray* Target = NULL;
    const GLFloatArray* Source = NULL;
    int TargetLayer = 0;
    if (Pass.IsCopy)
    {
        Target = &State->_result;
        TargetLayer = (int)Pass.Map;
        Source = &State->_sweeps[Pass.Map];
    }
    else
    {
        // The passes take turns between the map's own target and the scratch; the map's last pass writes its own.
        Target = Pass.WritesMapTarget ? &State->_sweeps[Pass.Map] : &State->_scratch;
        Source = Pass.WritesMapTarget ? &State->_scratch : &State->_sweeps[Pass.Map];
    }
    Shader Sweep = GameShader_GetRaylibShader(State->_sweepShader);
    int Layer = (int)Pass.Map;
    int IsFirst = (!Pass.IsCopy && (Pass.Step == 0U)) ? 1 : 0;
    int IsCopy = Pass.IsCopy ? 1 : 0;
    int HasCoarser = Pass.HasCoarser ? 1 : 0;
    float SizeValue = (float)Size;
    float Toward[2] = { Light->Toward.x, Light->Toward.y };
    float Slopes[2] = { Light->TopSlope, Light->BottomSlope };
    SetShaderValue(Sweep, State->_locLayer, &Layer, SHADER_UNIFORM_INT);
    SetShaderValue(Sweep, State->_locIsFirst, &IsFirst, SHADER_UNIFORM_INT);
    SetShaderValue(Sweep, State->_locIsCopy, &IsCopy, SHADER_UNIFORM_INT);
    SetShaderValue(Sweep, State->_locHasCoarser, &HasCoarser, SHADER_UNIFORM_INT);
    SetShaderValue(Sweep, State->_locSize, &SizeValue, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Sweep, State->_locToward, Toward, SHADER_UNIFORM_VEC2);
    SetShaderValue(Sweep, State->_locSlopes, Slopes, SHADER_UNIFORM_VEC2);
    SetShaderValue(Sweep, State->_locWidth, &Pass.Width, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Sweep, State->_locRun, &Pass.Run, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Sweep, State->_locCoarserWidth, &Pass.CoarserWidth, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Sweep, State->_locCoarserExclusion, &Pass.CoarserExclusion, SHADER_UNIFORM_FLOAT);

    GLFloatArray_AttachLayer(&State->_api, Target, TargetLayer);
    BeginTextureMode(GetTarget(Target));
    rlDisableColorBlend();
    BeginShaderMode(Sweep);
    // The inputs: never the target itself (GL leaves reading an attached array undefined).
    GLFloatArray_Bind(&State->_api, &State->_heights, WorldRendererTextureUnit_FarSweepHeights);
    GLFloatArray_Bind(&State->_api, Source, WorldRendererTextureUnit_FarSweepSource);
    const GLFloatArray* Coarser = Pass.HasCoarser ? &State->_sweeps[Pass.Map + 1U] : Source;
    GLFloatArray_Bind(&State->_api, Coarser, WorldRendererTextureUnit_FarSweepCoarser);
    DrawRectangle(0, 0, Size, Size, WHITE);
    EndShaderMode();
    rlEnableColorBlend();
    EndTextureMode();
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererFarShadows_Load(WorldRenderer* self)
{
    WorldRendererFarShadowState* State = &self->_farShadows;
    State->_heightShader = WorldRendererCommon_LoadShader(self, HEIGHTS_SHADER_ASSET_NAME);
    if (State->_heightShader != NULL)
    {
        WorldRendererCommon_PrepareCurvature(GameShader_GetRaylibShader(State->_heightShader), &State->_heightCurvature);
    }
    State->_sweepShader = WorldRendererCommon_LoadShader(self, SWEEP_SHADER_ASSET_NAME);
    if (State->_sweepShader != NULL)
    {
        Shader Sweep = GameShader_GetRaylibShader(State->_sweepShader);
        AssignSamplerUnit(Sweep, "farSweepHeights", WorldRendererTextureUnit_FarSweepHeights);
        AssignSamplerUnit(Sweep, "farSweepSource", WorldRendererTextureUnit_FarSweepSource);
        AssignSamplerUnit(Sweep, "farSweepCoarser", WorldRendererTextureUnit_FarSweepCoarser);
        State->_locLayer = GetShaderLocation(Sweep, "farSweepLayer");
        State->_locIsFirst = GetShaderLocation(Sweep, "farSweepIsFirst");
        State->_locIsCopy = GetShaderLocation(Sweep, "farSweepIsCopy");
        State->_locHasCoarser = GetShaderLocation(Sweep, "farSweepHasCoarser");
        State->_locSize = GetShaderLocation(Sweep, "farSweepSize");
        State->_locToward = GetShaderLocation(Sweep, "farSweepToward");
        State->_locSlopes = GetShaderLocation(Sweep, "farSweepSlopes");
        State->_locWidth = GetShaderLocation(Sweep, "farSweepWidth");
        State->_locRun = GetShaderLocation(Sweep, "farSweepRun");
        State->_locCoarserWidth = GetShaderLocation(Sweep, "farSweepCoarserWidth");
        State->_locCoarserExclusion = GetShaderLocation(Sweep, "farSweepCoarserExclusion");
    }
    if ((State->_heightShader != NULL) && (State->_sweepShader != NULL) && !GLFloatArray_LoadApi(&State->_api))
    {
        char Message[WARNING_CAPACITY];
        snprintf(Message, sizeof(Message), "WorldRenderer: the GPU lacks %s; terrain casts no shadows past the shadow "
            "levels' reach.", (State->_api._missing != NULL) ? State->_api._missing : "a function the far shadows need");
        WorldRendererCommon_Warn(self, (const unsigned char*)Message);
    }
}

void WorldRendererFarShadows_Unload(WorldRenderer* self)
{
    if (self->_farShadows._api._isAvailable)
    {
        ReleaseTextures(self);
    }
    WorldFarShadows_Invalidate(&self->_farShadows._schedule);
}

bool WorldRendererFarShadows_IsAvailable(const WorldRenderer* self)
{
    const WorldRendererFarShadowState* State = &self->_farShadows;
    return (State->_heightShader != NULL) && (State->_sweepShader != NULL) && State->_api._isAvailable
        && (self->_pbr._rigid._shader != NULL);
}

void WorldRendererFarShadows_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame)
{
    WorldRendererFarShadowState* State = &self->_farShadows;
    frame->AreFarShadowsRunning = false;
    frame->AreFarShadowsActive = false;
    if (!WorldRendererFarShadows_IsAvailable(self) || !AreWanted(self, frame))
    {
        WorldFarShadows_SkipFrame(&State->_schedule);
        return;
    }
    int Size = WorldFarShadows_SanitizeMapSize((self->_config != NULL) ? self->_config->FarShadowMapSize
        : WORLD_FAR_SHADOW_DEFAULT_MAP_SIZE);
    if (!EnsureTextures(self, Size))
    {
        return;
    }
    // Maps of another world hold what was there.
    if (State->_world != frame->World)
    {
        WorldFarShadows_Invalidate(&State->_schedule);
        State->_world = frame->World;
    }
    WorldFarShadowsInput Input =
    {
        .Origin = frame->Origin,
        .TowardLight = frame->ShadowDirection,
        .DiscRadius = GetDiscRadius(frame),
        .MapSize = Size,
        .CurvatureScale = frame->CurvatureScale,
        .TerrainListingCount = WorldRenderCache_GetTerrainListingCount(&self->_cache),
        .DrawDistanceScale = WorldRendererCommon_ConfigDrawDistanceScale(self),
        .ShadowDistanceScale = WorldRendererCommon_ConfigShadowDistanceScale(self)
    };
    WorldFarShadows_BeginFrame(&State->_schedule, &Input, &frame->FarShadowWork);
    frame->AreFarShadowsRunning = true;
    frame->AreFarShadowsActive = State->_schedule._hasResult;
    frame->FarShadowFrame = State->_schedule._resultFrame;
}

void WorldRendererFarShadows_Render(WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (!frame->AreFarShadowsRunning)
    {
        return;
    }
    const WorldFarShadowsWork* Work = &frame->FarShadowWork;
    if (Work->DrawsMaps)
    {
        DrawMaps(self, frame);
    }
    for (uint32_t Pass = Work->FirstPass; Pass < (Work->FirstPass + Work->PassCount); Pass++)
    {
        RunPass(self, frame, Pass);
    }
    if (Work->FinishesSweep)
    {
        self->_farShadows._sweepCount++;
    }
}

void WorldRendererFarShadows_Bind(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (frame->AreFarShadowsActive)
    {
        GLFloatArray_Bind(&self->_farShadows._api, &self->_farShadows._result, WorldRendererTextureUnit_FarShadows);
    }
}

void WorldRendererFarShadows_PrepareLitShader(Shader shader, WorldRendererFarShadowLocations* outLocations)
{
    outLocations->_locActive = GetShaderLocation(shader, "farShadowActive");
    outLocations->_locMaps = GetShaderLocation(shader, "farShadowMaps");
    outLocations->_locOffset = GetShaderLocation(shader, "farShadowOffset");
    outLocations->_locHalfWidths = GetShaderLocation(shader, "farShadowHalfWidths");
    outLocations->_locCurvature = GetShaderLocation(shader, "farShadowCurvature");
    outLocations->_locEdge = GetShaderLocation(shader, "farShadowEdge");
    outLocations->_locBandStart = GetShaderLocation(shader, "farShadowBandStart");
    int Unit = (int)WorldRendererTextureUnit_FarShadows;
    SetShaderValue(shader, outLocations->_locMaps, &Unit, SHADER_UNIFORM_INT);
}

void WorldRendererFarShadows_UploadLitFrame(Shader shader, const WorldRendererFarShadowLocations* locations,
    const WorldRenderFrame* frame)
{
    int IsActive = frame->AreFarShadowsActive ? 1 : 0;
    SetShaderValue(shader, locations->_locActive, &IsActive, SHADER_UNIFORM_INT);
    if (!frame->AreFarShadowsActive)
    {
        return;
    }
    const WorldFarShadowFrame* Maps = &frame->FarShadowFrame;
    // The camera's offset from the maps' centre, subtracted in double and rounded once.
    Vector3 Offset = GameMath_ToVector3Relative(frame->Origin, Maps->Center);
    float OffsetValue[3] = { Offset.x, Offset.y, Offset.z };
    float HalfWidths[3];
    for (uint32_t Map = 0U; Map < WORLD_FAR_SHADOW_MAP_COUNT; Map++)
    {
        HalfWidths[Map] = (float)(WorldFarShadows_GetMapWidth(Map) * 0.5);
    }
    float Edge = WorldFarShadows_GetEdge(Maps->MapSize);
    float BandStart = Edge * (1.0f - WORLD_FAR_SHADOW_BLEND_SHARE);
    SetShaderValue(shader, locations->_locOffset, OffsetValue, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locHalfWidths, HalfWidths, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locCurvature, &Maps->CurvatureScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locEdge, &Edge, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locBandStart, &BandStart, SHADER_UNIFORM_FLOAT);
}

void WorldRendererFarShadows_UpdateStatus(WorldRenderer* self, const WorldRenderFrame* frame)
{
    const WorldRendererFarShadowState* State = &self->_farShadows;
    WorldRendererStatus* Status = &self->_status;
    Status->AreFarShadowsEnabled = State->_isEnabled;
    Status->FarShadowMapSize = State->_hasTextures ? State->_textureSize : 0;
    Status->FarShadowMapDrawCount = State->_mapDrawCount;
    Status->FarShadowSweepCount = State->_sweepCount;
    Status->IsFarShadowSweeping = frame->AreFarShadowsRunning && State->_schedule._isSweeping;
    Status->FarShadowSweepPassesDone = State->_schedule._sweepPassesDone;
    Status->FarShadowSweepPassCount = State->_hasTextures ? WorldFarShadows_GetSweepPassCount(State->_textureSize) : 0U;
    for (uint32_t Map = 0U; Map < WORLD_FAR_SHADOW_MAP_COUNT; Map++)
    {
        Status->FarShadowMapChunkCounts[Map] = State->_mapChunkCounts[Map];
    }
    Status->FarShadowCenterDistance = 0.0;
    if (frame->AreFarShadowsActive)
    {
        double X = frame->Origin.X - frame->FarShadowFrame.Center.X;
        double Z = frame->Origin.Z - frame->FarShadowFrame.Center.Z;
        Status->FarShadowCenterDistance = sqrt((X * X) + (Z * Z));
    }
    Status->CurvatureScale = frame->CurvatureScale;
}

void WorldRendererFarShadows_LogStatus(const WorldRenderer* self)
{
    if ((self->_logger == NULL) || !self->_isStatusReported)
    {
        return;
    }
    const WorldRendererStatus* Status = &self->_status;
    char Sweep[64];
    if (Status->IsFarShadowSweeping)
    {
        snprintf(Sweep, sizeof(Sweep), "sweeping (%u of %u passes)", (unsigned int)Status->FarShadowSweepPassesDone,
            (unsigned int)Status->FarShadowSweepPassCount);
    }
    else
    {
        snprintf(Sweep, sizeof(Sweep), "no sweep running");
    }
    Error LogResult = Logger_LogInfoFormatted(self->_logger, (const unsigned char*)u8"WorldRenderer: far shadows: %s, "
        u8"maps of %d texels (%.1f, %.1f and %.1f km), drawn %llu times (last %llu, %llu and %llu chunks), %llu sweeps "
        u8"finished, %s, the maps' centre %.0f m from the camera; the picture's curve %s.",
        Status->AreFarShadowsEnabled ? "on" : "off (switched off)", Status->FarShadowMapSize,
        WorldFarShadows_GetMapWidth(0U) / 1000.0, WorldFarShadows_GetMapWidth(1U) / 1000.0,
        WorldFarShadows_GetMapWidth(2U) / 1000.0, (unsigned long long)Status->FarShadowMapDrawCount,
        (unsigned long long)Status->FarShadowMapChunkCounts[0], (unsigned long long)Status->FarShadowMapChunkCounts[1],
        (unsigned long long)Status->FarShadowMapChunkCounts[2], (unsigned long long)Status->FarShadowSweepCount, Sweep,
        Status->FarShadowCenterDistance, (Status->CurvatureScale > 0.0f) ? "on" : "off");
    Error_Deconstruct(&LogResult);
}
