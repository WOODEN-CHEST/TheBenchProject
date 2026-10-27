// The eye's adaptation to the picture: the meter pass over the HDR scene, its readback a frame or two later, and
// the exposure the tonemap takes (the eye's model is EyeAdaptation.h's, the readback GLReadback.h's).

#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"


// Macros.
/** Asset name of the meter's shader: the scene's mean log2 luminance in blocks. */
#define LUMINANCE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_luminance")


// Static functions.
/* How many cd/m^2 one of the game's light units stands for in the light the eye is in at the camera (the sky's, as
 * the camera's openness and the zones at it leave it, plus the zones' own). */
static double GetRealScale(const WorldRenderFrame* frame)
{
    return EyeAdaptation_GetRealScale(frame->CameraDayLuminance,
        frame->CameraSceneLuminance - frame->CameraDayLuminance);
}

/* The anchor: the level a view has in the light the eye is in at the camera (indoors, the room's). */
static float GetAnchor(const WorldRenderFrame* frame)
{
    return EyeAdaptation_GetAnchor(frame->CameraSceneLuminance);
}

/* The eye's level, or the anchor's before the eye has started. */
static float GetLevel(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    float Level = EyeAdaptation_GetLevel(&self->_exposure._eye);
    return isfinite(Level) ? Level : GetAnchor(frame);
}

/* The exposure for a level, with the zones' bias at the camera. */
static float GetExposure(float level, const WorldRenderFrame* frame)
{
    return EyeAdaptation_GetBiasedExposure(EyeAdaptation_GetExposure(level, GetRealScale(frame)), frame->ExposureBias);
}

/* Draws the meter's blocks of @p source, each pixel held within the window around the anchor (of the light the eye is in
 * at the camera), the sky's at least the night's own light (Rec. 709 luminance, as the meter's). */
static void RenderMeter(WorldRenderer* self, const WorldRenderFrame* frame, RenderTexture2D source)
{
    WorldRendererExposureState* State = &self->_exposure;
    Shader RayShader = GameShader_GetRaylibShader(State->_shader);
    const float* Night = frame->SkyLight.Night;
    float NightLuminance = 0.2126f * Night[0] + 0.7152f * Night[1] + 0.0722f * Night[2];
    EyeAdaptationWindow Window = EyeAdaptation_GetWindow(GetAnchor(frame), NightLuminance);
    float Edges[3] = { Window.Low, Window.SkyLow, Window.High };
    SetShaderValue(RayShader, State->_locWindow, Edges, SHADER_UNIFORM_VEC3);
    float DepthMask = self->_scene._isDepthSamplable ? 1.0f : 0.0f;
    SetShaderValue(RayShader, State->_locDepthMask, &DepthMask, SHADER_UNIFORM_FLOAT);
    WorldRendererCommon_SetDepthConvention(RayShader, State->_locDepthReversed, frame);
    Rectangle SourceRect = { 0.0f, 0.0f, (float)source.texture.width, (float)source.texture.height };
    Rectangle Destination = { 0.0f, 0.0f, (float)EYE_ADAPTATION_METER_WIDTH, (float)EYE_ADAPTATION_METER_HEIGHT };
    // Every block is written, so nothing is cleared. The shader reads by gl_FragCoord and texelFetch: the quad only has to
    // cover the target (DrawTexturePro binds the scene to unit 0, texture0). The depth goes in through
    // SetShaderValueTexture after BeginShaderMode, into the batch's texture table (as the bloom's prefilter binds it),
    // and only when it is read.
    BeginTextureMode(State->_target);
    BeginShaderMode(RayShader);
    if (self->_scene._isDepthSamplable)
    {
        SetShaderValueTexture(RayShader, State->_locDepthTexture, self->_scene._target.depth);
    }
    DrawTexturePro(source.texture, SourceRect, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererExposure_Load(WorldRenderer* self)
{
    WorldRendererExposureState* State = &self->_exposure;
    EyeAdaptation_Construct(&State->_eye);
    // Without any of the shader, the readback's functions, the target or its buffers the eye adapts to the light it is in
    // at the camera (the anchor) instead: lamps then count for nothing.
    State->_shader = WorldRendererCommon_LoadShader(self, LUMINANCE_SHADER_ASSET_NAME);
    if (State->_shader == NULL)
    {
        return; // logged by the shader load
    }
    Shader RayShader = GameShader_GetRaylibShader(State->_shader);
    State->_locWindow = GetShaderLocation(RayShader, "window");
    State->_locDepthTexture = GetShaderLocation(RayShader, "depthTexture");
    State->_locDepthMask = GetShaderLocation(RayShader, "depthMask");
    State->_locDepthReversed = GetShaderLocation(RayShader, "depthReversed");
    if (!GLReadback_LoadApi(&State->_readbackApi))
    {
        char Message[256];
        snprintf(Message, sizeof(Message), "WorldRenderer: readbacks are unavailable (%s is missing); the eye adapts to "
            "light at the camera, not to the picture.",
            (State->_readbackApi._missing != NULL) ? State->_readbackApi._missing : "?");
        WorldRendererCommon_WarnOnce(self, &State->_isFailureReported, (const unsigned char*)Message);
        return;
    }
    State->_target = WorldRendererCommon_CreateColorTarget(EYE_ADAPTATION_METER_WIDTH, EYE_ADAPTATION_METER_HEIGHT,
        PIXELFORMAT_UNCOMPRESSED_R32);
    State->_hasTarget = State->_target.id != 0;
    if (!State->_hasTarget)
    {
        WorldRendererCommon_WarnOnce(self, &State->_isFailureReported, (const unsigned char*)u8"WorldRenderer: could not "
            u8"create the eye's meter target; the eye adapts to the light at the camera, not to the picture.");
        return;
    }
    Error Result = GLReadback_Construct(&State->_readback, &State->_readbackApi, EYE_ADAPTATION_METER_WIDTH,
        EYE_ADAPTATION_METER_HEIGHT);
    if (Result.Code != ErrorCode_Success)
    {
        char Message[384];
        snprintf(Message, sizeof(Message), "WorldRenderer: the eye's meter cannot be read back (%s); the eye adapts to the "
            "light at the camera, not to the picture.",
            (Result.Message != NULL) ? (const char*)Result.Message : "an error");
        WorldRendererCommon_WarnOnce(self, &State->_isFailureReported, (const unsigned char*)Message);
        Error_Deconstruct(&Result);
        WorldRendererCommon_UnloadTarget(&State->_target, &State->_hasTarget);
        return;
    }
    State->_isAvailable = true;
}

void WorldRendererExposure_Unload(WorldRenderer* self)
{
    WorldRendererExposureState* State = &self->_exposure;
    GLReadback_Deconstruct(&State->_readback);
    WorldRendererCommon_UnloadTarget(&State->_target, &State->_hasTarget);
    State->_isAvailable = false;
}

bool WorldRendererExposure_IsAvailable(const WorldRenderer* self)
{
    return self->_exposure._isAvailable;
}

float WorldRendererExposure_GetCurrent(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    return GetExposure(GetLevel(self, frame), frame);
}

float WorldRendererExposure_Update(WorldRenderer* self, WorldRenderFrame* frame, RenderTexture2D source,
    float deltaSeconds)
{
    WorldRendererExposureState* State = &self->_exposure;
    // The newest blocks the GPU has finished (a frame or two old), then this frame's for the frames to come.
    float Measurement = NAN;
    if (State->_isAvailable && GLReadback_Poll(&State->_readback, State->_blocks))
    {
        float Aspect = (frame->SceneHeight > 0) ? (float)frame->SceneWidth / (float)frame->SceneHeight : 0.0f;
        Measurement = EyeAdaptation_Measure(State->_blocks, Aspect);
    }
    EyeAdaptation_Update(&State->_eye, GetAnchor(frame), Measurement, deltaSeconds);
    float Level = GetLevel(self, frame);
    if (State->_isAvailable)
    {
        RenderMeter(self, frame, source);
        GLReadback_Request(&State->_readback, State->_target.id);
    }
    double Scale = GetRealScale(frame);
    frame->IsExposureMetered = EyeAdaptation_IsMeasured(&State->_eye);
    frame->AdaptedLuminance = isfinite(Level) ? exp2f(Level) : 0.0f;
    frame->AdaptedRealLuminance = (float)((double)frame->AdaptedLuminance * Scale);
    return GetExposure(Level, frame);
}
