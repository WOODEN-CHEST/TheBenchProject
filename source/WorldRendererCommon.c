// Shared helpers of the world renderer: config multipliers, colour conversion, shader loading, one-time warnings
// and render-target constructors.

#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "Config.h"
#include "GameSkeleton.h"
#include "Logger.h"
#include "SkeletalAnimation.h"
#include "raylib/rlgl.h"


// Macros.
/** Room for the name of one palette element ("bonePalette[N]") when probing a shader. */
#define PALETTE_UNIFORM_NAME_CAPACITY 32
/** The palette rows a shader could hold at most: three per bone of the largest skeleton a model may have. */
#define PALETTE_MAX_ROWS ((int)(GAME_SKELETON_MAX_BONES * 3U))


// Static functions.
/* Whether the shader's palette array has an element @p row (the array is indexed dynamically, so every element of its
 * declared size is active). */
static bool HasPaletteRow(Shader shader, int row)
{
    char Name[PALETTE_UNIFORM_NAME_CAPACITY];
    snprintf(Name, sizeof(Name), "bonePalette[%d]", row);
    return GetShaderLocation(shader, Name) >= 0;
}


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

float WorldRendererCommon_ConfigDrawDistanceScale(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->DrawDistanceScale : 1.0f;
}

float WorldRendererCommon_ConfigLodDistanceScale(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->LodDistanceScale : 1.0f;
}

void WorldRendererCommon_PrepareDither(Shader shader, WorldRendererDitherLocations* outLocations)
{
    outLocations->_locFade = GetShaderLocation(shader, "ditherFade");
    outLocations->_locLodProgress = GetShaderLocation(shader, "ditherLodProgress");
    outLocations->_locIsIncomingLod = GetShaderLocation(shader, "ditherIsIncomingLod");
    outLocations->_locPixelScale = GetShaderLocation(shader, "ditherPixelScale");
    WorldRendererCommon_SetDither(shader, outLocations, 0.0f, 0.0f, false);
    WorldRendererCommon_SetDitherPixelScale(shader, outLocations, (Vector2){ 1.0f, 1.0f });
}

void WorldRendererCommon_SetDither(Shader shader, const WorldRendererDitherLocations* locations, float fade,
    float lodProgress, bool isIncomingLod)
{
    int IsIncoming = isIncomingLod ? 1 : 0;
    SetShaderValue(shader, locations->_locFade, &fade, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locLodProgress, &lodProgress, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locIsIncomingLod, &IsIncoming, SHADER_UNIFORM_INT);
}

void WorldRendererCommon_SetDitherPixelScale(Shader shader, const WorldRendererDitherLocations* locations,
    Vector2 pixelScale)
{
    float Scale[2] = { pixelScale.x, pixelScale.y };
    SetShaderValue(shader, locations->_locPixelScale, Scale, SHADER_UNIFORM_VEC2);
}

void WorldRendererCommon_PreparePalette(Shader shader, WorldRendererPalette* outPalette)
{
    *outPalette = (WorldRendererPalette){ ._location = GetShaderLocation(shader, "bonePalette"), ._maxBones = 0U,
        ._pose = NULL, ._revision = 0U };
    if ((outPalette->_location < 0) || !HasPaletteRow(shader, 0))
    {
        outPalette->_location = -1;
        return;
    }
    // The array's length, by halving the range between a row it has and one past the most a shader could hold.
    int Present = 0;
    int Absent = PALETTE_MAX_ROWS;
    if (HasPaletteRow(shader, Absent - 1))
    {
        Present = Absent - 1;
    }
    while ((Absent - Present) > 1)
    {
        int Middle = Present + ((Absent - Present) / 2);
        if (HasPaletteRow(shader, Middle))
        {
            Present = Middle;
        }
        else
        {
            Absent = Middle;
        }
    }
    outPalette->_maxBones = (uint32_t)(Present + 1) / 3U;
}

bool WorldRendererCommon_UploadPalette(Shader shader, WorldRendererPalette* palette, const WorldAnimatorPose* pose)
{
    if ((palette->_location < 0) || (pose->_boneCount == 0U) || (pose->_boneCount > palette->_maxBones))
    {
        return false;
    }
    if ((palette->_pose == pose) && (palette->_revision == pose->_revision))
    {
        return true;
    }
    SetShaderValueV(shader, palette->_location, pose->_palette, SHADER_UNIFORM_VEC4,
        (int)(pose->_boneCount * (SKELETAL_PALETTE_FLOATS_PER_BONE / 4U)));
    palette->_pose = pose;
    palette->_revision = pose->_revision;
    return true;
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

unsigned int WorldRendererCommon_LoadCameraDepth(WorldRenderer* self, int width, int height, bool samplable)
{
    if (!self->_depth._isReversed)
    {
        return rlLoadTextureDepth(width, height, !samplable);
    }
    unsigned int Id = 0U;
    Error Result = samplable ? GLDepth_CreateTexture(&self->_depth._api, width, height, &Id)
        : GLDepth_CreateRenderbuffer(&self->_depth._api, width, height, &Id);
    if (Result.Code != ErrorCode_Success)
    {
        char Message[384];
        snprintf(Message, sizeof(Message), "WorldRenderer: a camera pass's 32-bit depth could not be made (%s).",
            (Result.Message != NULL) ? (const char*)Result.Message : "an error");
        WorldRendererCommon_WarnOnce(self, &self->_depth._isFailureReported, (const unsigned char*)Message);
        Error_Deconstruct(&Result);
        return 0U;
    }
    return Id;
}

void WorldRendererCommon_SetDepthConvention(Shader shader, int location, const WorldRenderFrame* frame)
{
    float Reversed = frame->IsDepthReversed ? 1.0f : 0.0f;
    SetShaderValue(shader, location, &Reversed, SHADER_UNIFORM_FLOAT);
}

void WorldRendererCommon_BeginCameraDepth(WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (frame->IsDepthReversed)
    {
        GLDepth_BeginReversed(&self->_depth._api);
    }
}

void WorldRendererCommon_LoadCameraProjection(const WorldRenderFrame* frame)
{
    if (frame->IsDepthReversed)
    {
        rlDrawRenderBatchActive();
        rlSetMatrixProjection(frame->Projection);
    }
}

void WorldRendererCommon_EndCameraDepth(WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (frame->IsDepthReversed)
    {
        GLDepth_EndReversed(&self->_depth._api);
    }
}

RenderTexture2D WorldRendererCommon_TryCreateHdrTarget(WorldRenderer* self, int width, int height, bool samplableDepth)
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

    Target.depth.id = WorldRendererCommon_LoadCameraDepth(self, width, height, samplableDepth);
    Target.depth.width = width;
    Target.depth.height = height;
    Target.depth.format = SCENE_DEPTH_FORMAT;
    Target.depth.mipmaps = 1;

    rlFramebufferAttach(Target.id, Target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(Target.id, Target.depth.id, RL_ATTACHMENT_DEPTH,
        samplableDepth ? RL_ATTACHMENT_TEXTURE2D : RL_ATTACHMENT_RENDERBUFFER, 0);

    bool Complete = (Target.texture.id != 0) && (Target.depth.id != 0) && rlFramebufferComplete(Target.id);
    rlDisableFramebuffer();

    if (!Complete)
    {
        UnloadRenderTexture(Target); // frees the framebuffer and its attachments
        Target = (RenderTexture2D){ 0 };
    }
    return Target;
}

RenderTexture2D WorldRendererCommon_CreateColorTarget(int width, int height, PixelFormat format)
{
    RenderTexture2D Target = { 0 };
    Target.id = rlLoadFramebuffer();
    if (Target.id == 0)
    {
        return Target;
    }

    rlEnableFramebuffer(Target.id);
    Target.texture.id = rlLoadTexture(NULL, width, height, (int)format, 1);
    Target.texture.width = width;
    Target.texture.height = height;
    Target.texture.format = (int)format;
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

RenderTexture2D WorldRendererCommon_CreateDepthTarget(int width, int height)
{
    RenderTexture2D Target = { 0 };
    Target.id = rlLoadFramebuffer();
    if (Target.id == 0)
    {
        return Target;
    }

    rlEnableFramebuffer(Target.id);
    // No colour attachment (depth-only). BeginTextureMode reads .texture size for the viewport, so set it.
    Target.texture.width = width;
    Target.texture.height = height;
    Target.depth.id = rlLoadTextureDepth(width, height, false); // false => a samplable depth TEXTURE
    Target.depth.width = width;
    Target.depth.height = height;
    Target.depth.format = SCENE_DEPTH_FORMAT;
    Target.depth.mipmaps = 1;
    rlFramebufferAttach(Target.id, Target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    bool Complete = (Target.depth.id != 0) && rlFramebufferComplete(Target.id);
    rlDisableFramebuffer();

    if (!Complete)
    {
        rlUnloadFramebuffer(Target.id); // also deletes the attached depth texture
        return (RenderTexture2D){ 0 };
    }
    // Readers compare raw depth texels (a shadow test, a tap of the sky's visibility): interpolated depths would blur
    // the comparison.
    SetTextureFilter(Target.depth, TEXTURE_FILTER_POINT);
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
