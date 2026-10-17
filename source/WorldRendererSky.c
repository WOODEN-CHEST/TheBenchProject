// The atmospheric sky pass.

#include "WorldRendererInternal.h"


// Macros.
/** Asset name of the atmospheric sky shader (full-screen Rayleigh/Mie sky + sun + stars). */
#define SKY_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sky")


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererSky_LoadShader(WorldRenderer* self)
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
    self->_sky._locStarSeed = GetShaderLocation(RayShader, "starSeed");
    self->_sky._locStarDensity = GetShaderLocation(RayShader, "starDensity");
    self->_sky._locStarBrightness = GetShaderLocation(RayShader, "starBrightness");
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

    // The low 24 bits of the seed are enough to vary star placement and stay exactly representable as a float.
    float StarSeed = (float)(Environment->StarSeed & 0xFFFFFFu);
    SetShaderValue(RayShader, self->_sky._locStarSeed, &StarSeed, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sky._locStarDensity, &Environment->StarDensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sky._locStarBrightness, &Environment->StarBrightness, SHADER_UNIFORM_FLOAT);

    BeginShaderMode(RayShader);
    DrawRectangle(0, 0, frame->SceneWidth, frame->SceneHeight, WHITE);
    EndShaderMode();
}
