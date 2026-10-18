// The atmospheric sky pass, with the Milky Way and light pollution's skyglow. The stars are their own pass
// (WorldRendererStars.c), drawn right after this one.

#include <math.h>
#include "WorldRendererInternal.h"
#include "NightSky.h"
#include "raylib/raymath.h"


// Macros.
/** Asset name of the atmospheric sky shader (full-screen Rayleigh/Mie sky, sun disc, Milky Way, skyglow). */
#define SKY_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sky")
/** Asset name of the Milky Way panorama; a resource pack can override it. */
// TODO: a per-world Milky Way texture needs an owned string in WorldEnvironment, which is easier after the property
// descriptors (T04).
#define SKY_MILKY_WAY_ASSET_NAME ((const unsigned char*)u8"sky_milky_way")
/** Linear radiance of a white Milky Way texel at MilkyWayBrightness 1, in full darkness without light pollution. At
 *  the night exposure the bright core reads as a soft glow and the outer band as a faint one. */
#define MILKY_WAY_BASE_RADIANCE 0.005f
/** Linear radiance of light pollution's skyglow at the horizon at LightPollution 1: a strong orange glow at the night
 *  exposure. Scaled by the light pollution and faded out by daylight. */
#define SKYGLOW_BASE_RADIANCE 0.012f


// Static functions.
static float ClampUnit(float value)
{
    return fminf(fmaxf(value, 0.0f), 1.0f); // fmaxf drops a NaN, so NaN becomes 0
}

/* The panorama's mip level whose texels match one scene pixel: log2 of a pixel's angular size (the vertical field of
 * view over the scene height) over a texel's (a full turn over the texture width), clamped to the texture's levels. */
static float ComputeMilkyWayLod(const WorldRenderFrame* frame, Texture2D texture)
{
    if ((frame->SceneHeight <= 0) || (texture.width <= 0))
    {
        return 0.0f;
    }
    float PixelAngle = frame->RayCamera.fovy * DEG2RAD / (float)frame->SceneHeight;
    float TexelAngle = 2.0f * PI / (float)texture.width;
    float MaxLod = (texture.mipmaps > 1) ? (float)(texture.mipmaps - 1) : 0.0f;
    return fminf(fmaxf(log2f(PixelAngle / TexelAngle), 0.0f), MaxLod);
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererSky_LoadAssets(WorldRenderer* self)
{
    self->_sky._shader = WorldRendererCommon_LoadShader(self, SKY_SHADER_ASSET_NAME);
    if (self->_sky._shader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(self->_sky._shader);
    self->_sky._locResolution = GetShaderLocation(RayShader, "resolution");
    self->_sky._locInvViewProj = GetShaderLocation(RayShader, "invViewProj");
    self->_sky._locCameraPos = GetShaderLocation(RayShader, "cameraPos");
    self->_sky._locSunDirection = GetShaderLocation(RayShader, "sunDirection");
    self->_sky._locSunColor = GetShaderLocation(RayShader, "sunColor");
    self->_sky._locSunIntensity = GetShaderLocation(RayShader, "sunIntensity");
    self->_sky._locSunSize = GetShaderLocation(RayShader, "sunSize");
    self->_sky._locTurbidity = GetShaderLocation(RayShader, "turbidity");
    self->_sky._locSkyTint = GetShaderLocation(RayShader, "skyTint");
    self->_sky._locSkyglowRadiance = GetShaderLocation(RayShader, "skyglowRadiance");
    self->_sky._locMilkyWayTexture = GetShaderLocation(RayShader, "milkyWayTexture");
    self->_sky._locWorldToGalactic = GetShaderLocation(RayShader, "worldToGalactic");
    self->_sky._locMilkyWayRadiance = GetShaderLocation(RayShader, "milkyWayRadiance");
    self->_sky._locMilkyWayLod = GetShaderLocation(RayShader, "milkyWayLod");

    self->_sky._milkyWay = WorldRendererCommon_LoadTexture(self, SKY_MILKY_WAY_ASSET_NAME,
        (const unsigned char*)u8"the Milky Way");
}

void WorldRendererSky_PrepareFrame(WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;

    float SunElevationDegrees = asinf(fminf(fmaxf(frame->SunDirection.y, -1.0f), 1.0f)) * RAD2DEG;
    frame->LimitingMagnitude = NightSky_GetLimitingMagnitude(Environment->LightPollution, SunElevationDegrees);
    frame->MilkyWayRadiance = MILKY_WAY_BASE_RADIANCE * fmaxf(Environment->MilkyWayBrightness, 0.0f)
        * NightSky_GetMilkyWayVisibility(frame->LimitingMagnitude);

    float GlowColor[3];
    WorldRendererCommon_ColorToLinear(Environment->LightPollutionColor, GlowColor);
    float GlowStrength = SKYGLOW_BASE_RADIANCE * ClampUnit(Environment->LightPollution) * (1.0f - frame->Daylight);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        frame->SkyglowRadiance[Channel] = GlowColor[Channel] * GlowStrength;
    }
}

/* Each pixel's world ray is reconstructed from the inverse of the same view-projection BeginMode3D uses (the
 * frame's), so the sky lines up with the 3D camera per pixel. Outputs linear HDR. */
void WorldRendererSky_Draw(WorldRenderer* self, const WorldRenderFrame* frame)
{
    Shader RayShader = GameShader_GetRaylibShader(self->_sky._shader);
    const WorldEnvironment* Environment = frame->Environment;

    float Resolution[2] = { (float)frame->SceneWidth, (float)frame->SceneHeight };
    SetShaderValue(RayShader, self->_sky._locResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValueMatrix(RayShader, self->_sky._locInvViewProj, frame->InvViewProjection);

    float CameraPos[3] = { frame->Camera->Position.x, frame->Camera->Position.y, frame->Camera->Position.z };
    SetShaderValue(RayShader, self->_sky._locCameraPos, CameraPos, SHADER_UNIFORM_VEC3);

    float SunDir[3] = { frame->SunDirection.x, frame->SunDirection.y, frame->SunDirection.z };
    SetShaderValue(RayShader, self->_sky._locSunDirection, SunDir, SHADER_UNIFORM_VEC3);

    SetShaderValue(RayShader, self->_sky._locSunColor, frame->SunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_sky._locSunIntensity, &Environment->SunIntensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sky._locSunSize, &Environment->SunSizeMultiplier, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sky._locTurbidity, &Environment->SkyTurbidity, SHADER_UNIFORM_FLOAT);

    float SkyTint[3];
    WorldRendererCommon_ColorToLinear(Environment->SkyTint, SkyTint);
    SetShaderValue(RayShader, self->_sky._locSkyTint, SkyTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_sky._locSkyglowRadiance, frame->SkyglowRadiance, SHADER_UNIFORM_VEC3);

    // The shader samples the Milky Way only while its radiance is above 0.
    float MilkyWayRadiance = frame->IsMilkyWayActive ? frame->MilkyWayRadiance : 0.0f;
    SetShaderValue(RayShader, self->_sky._locMilkyWayRadiance, &MilkyWayRadiance, SHADER_UNIFORM_FLOAT);
    Texture2D MilkyWayTexture = { 0 };
    if (frame->IsMilkyWayActive)
    {
        MilkyWayTexture = GameTexture_GetRaylibTexture(self->_sky._milkyWay);
        Matrix WorldToGalactic = MatrixMultiply(MatrixTranspose(frame->CelestialToWorld), NightSky_GetCelestialToGalactic());
        SetShaderValueMatrix(RayShader, self->_sky._locWorldToGalactic, WorldToGalactic);
        float Lod = ComputeMilkyWayLod(frame, MilkyWayTexture);
        SetShaderValue(RayShader, self->_sky._locMilkyWayLod, &Lod, SHADER_UNIFORM_FLOAT);
    }

    BeginShaderMode(RayShader);
    if (frame->IsMilkyWayActive)
    {
        // A 2D (batch) draw binds extra samplers through SetShaderValueTexture, after BeginShaderMode.
        SetShaderValueTexture(RayShader, self->_sky._locMilkyWayTexture, MilkyWayTexture);
    }
    DrawRectangle(0, 0, frame->SceneWidth, frame->SceneHeight, WHITE);
    EndShaderMode();
}
