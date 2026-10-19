// Shared helpers of the world renderer: config multipliers, colour conversion, shader loading, one-time warnings
// and render-target constructors.

#include <math.h>
#include "WorldRendererInternal.h"
#include "Config.h"
#include "Logger.h"
#include "raylib/rlgl.h"


// Internal functions (declared in WorldRendererInternal.h).
float WorldRendererCommon_ConfigShadowStrength(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->ShadowStrength : 1.0f;
}

float WorldRendererCommon_ConfigAoStrength(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->AmbientOcclusionStrength : 1.0f;
}

float WorldRendererCommon_ConfigFogStrength(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->FogStrength : 1.0f;
}

float WorldRendererCommon_ConfigBloomStrength(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->BloomStrength : 1.0f;
}

float WorldRendererCommon_ConfigSunshaftStrength(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->SunshaftStrength : 1.0f;
}

/* Light/ambient colours must be linear before they enter the PBR maths; the shader linearizes textured albedo
 * itself. */
void WorldRendererCommon_ColorToLinear(Color color, float outRGB[3])
{
    outRGB[0] = powf((float)color.r / 255.0f, SRGB_GAMMA);
    outRGB[1] = powf((float)color.g / 255.0f, SRGB_GAMMA);
    outRGB[2] = powf((float)color.b / 255.0f, SRGB_GAMMA);
}

GameShader* WorldRendererCommon_LoadShader(WorldRenderer* self, const unsigned char* assetName)
{
    GameShader* LoadedShader = NULL;
    Error LoadResult = AssetManager_LoadShader(self->_assetManager, assetName, self->_shaderUser, &LoadedShader);
    if (LoadResult.Code != ErrorCode_Success)
    {
        if (self->_logger != NULL)
        {
            Error LogResult = Logger_LogWarningFormatted(self->_logger,
                (const unsigned char*)u8"WorldRenderer: shader \"%s\" unavailable (%s).",
                (const char*)assetName,
                (LoadResult.Message != NULL) ? (const char*)LoadResult.Message : "no details");
            Error_Deconstruct(&LogResult);
        }
        Error_Deconstruct(&LoadResult);
        return NULL;
    }
    return LoadedShader;
}

GameTexture* WorldRendererCommon_LoadTexture(WorldRenderer* self, AssetUserID user, const unsigned char* assetName,
    const unsigned char* featureName)
{
    GameTexture* LoadedTexture = NULL;
    Error LoadResult = AssetManager_LoadTexture(self->_assetManager, assetName, user, &LoadedTexture);
    if (LoadResult.Code != ErrorCode_Success)
    {
        if (self->_logger != NULL)
        {
            Error LogResult = Logger_LogWarningFormatted(self->_logger,
                (const unsigned char*)u8"WorldRenderer: texture \"%s\" unavailable, so %s (%s).",
                (const char*)assetName, (const char*)featureName,
                (LoadResult.Message != NULL) ? (const char*)LoadResult.Message : "no details");
            Error_Deconstruct(&LogResult);
        }
        Error_Deconstruct(&LoadResult);
        return NULL;
    }
    return LoadedTexture;
}

void WorldRendererCommon_Warn(WorldRenderer* self, const unsigned char* message)
{
    if (self->_logger != NULL)
    {
        Error LogResult = Logger_LogWarning(self->_logger, message);
        Error_Deconstruct(&LogResult);
    }
}

void WorldRendererCommon_WarnOnce(WorldRenderer* self, bool* isReported, const unsigned char* message)
{
    if (*isReported)
    {
        return;
    }
    *isReported = true;
    WorldRendererCommon_Warn(self, message);
}

RenderTexture2D WorldRendererCommon_TryCreateHdrTarget(int width, int height, bool samplableDepth)
{
    RenderTexture2D Target = { 0 };
    Target.id = rlLoadFramebuffer();
    if (Target.id == 0)
    {
        return Target;
    }

    rlEnableFramebuffer(Target.id);

    Target.texture.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    Target.texture.width = width;
    Target.texture.height = height;
    Target.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    Target.texture.mipmaps = 1;

    Target.depth.id = rlLoadTextureDepth(width, height, !samplableDepth);
    Target.depth.width = width;
    Target.depth.height = height;
    Target.depth.format = SCENE_DEPTH_FORMAT;
    Target.depth.mipmaps = 1;

    rlFramebufferAttach(Target.id, Target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(Target.id, Target.depth.id, RL_ATTACHMENT_DEPTH,
        samplableDepth ? RL_ATTACHMENT_TEXTURE2D : RL_ATTACHMENT_RENDERBUFFER, 0);

    bool Complete = (Target.texture.id != 0) && rlFramebufferComplete(Target.id);
    rlDisableFramebuffer();

    if (!Complete)
    {
        UnloadRenderTexture(Target); // frees the framebuffer and its attachments
        Target = (RenderTexture2D){ 0 };
    }
    return Target;
}

RenderTexture2D WorldRendererCommon_CreateColorTarget(int width, int height)
{
    RenderTexture2D Target = { 0 };
    Target.id = rlLoadFramebuffer();
    if (Target.id == 0)
    {
        return Target;
    }

    rlEnableFramebuffer(Target.id);
    Target.texture.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    Target.texture.width = width;
    Target.texture.height = height;
    Target.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    Target.texture.mipmaps = 1;
    rlFramebufferAttach(Target.id, Target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

    bool Complete = (Target.texture.id != 0) && rlFramebufferComplete(Target.id);
    rlDisableFramebuffer();
    if (!Complete)
    {
        UnloadRenderTexture(Target);
        Target = (RenderTexture2D){ 0 };
    }
    return Target;
}

void WorldRendererCommon_UnloadTarget(RenderTexture2D* target, bool* hasTarget)
{
    if (*hasTarget)
    {
        UnloadRenderTexture(*target);
        *hasTarget = false;
    }
    *target = (RenderTexture2D){ 0 };
}
