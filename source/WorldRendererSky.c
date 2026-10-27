// The atmospheric sky pass, with the moon, the Milky Way and light pollution's light. Its glow comes from the
// atmosphere's tables (WorldRendererAtmosphere.c, built first in the frame), or the direct march while they are off. The
// stars are their own pass (WorldRendererStars.c), drawn right after this one.

#include <math.h>
#include "WorldRendererInternal.h"
#include "NightSky.h"
#include "raylib/raymath.h"
#include "wr/WRString.h"


// Macros.
/** Asset name of the atmospheric sky shader (full-screen Rayleigh/Mie sky, sun disc, moon, Milky Way, skyglow). */
#define SKY_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sky")
/** Initial capacity of a sky texture slot's name buffer, in bytes (it grows for longer names). */
#define SKY_TEXTURE_NAME_CAPACITY 32U
/** Linear radiance of a white Milky Way texel at MilkyWayBrightness 1, in full darkness without light pollution. At
 *  the night exposure the bright core reads as a soft glow and the outer band as a faint one. */
#define MILKY_WAY_BASE_RADIANCE 0.005f
/** Linear radiance of light pollution's skyglow at the horizon at LightPollution 1: a strong orange glow at the night
 *  exposure. Scaled by the light pollution and faded out by daylight. */
#define SKYGLOW_BASE_RADIANCE 0.012f
/** The real moon's angular radius, in degrees: MoonSizeMultiplier multiplies it. */
#define MOON_REAL_ANGULAR_RADIUS_DEGREES 0.26f
/** The largest angular radius the disc is drawn with, in degrees, however large MoonSizeMultiplier is. */
#define MOON_MAX_ANGULAR_RADIUS_DEGREES 45.0f
/** ===== MOON DISC KNOB =====
 *  Linear radiance of a white texel of the moon's face lit head-on, at MoonBrightness 1, before the atmosphere's
 *  transmittance. At the night exposure (24) the lit face reads as a bright, near-white disc with its maria visible;
 *  by day (exposure about 1) it is a pale disc on the blue sky, faint but findable at first quarter in the
 *  afternoon. Raise it for a brighter moon by day and by night; MoonBrightness scales it per world. */
#define MOON_DISC_BASE_RADIANCE 0.1f
/** The dark side's glow (earthshine) as a share of the lit face's radiance near new moon (x (1 - illumination)): the
 *  "old moon in the new moon's arms" of a crescent at night. */
#define MOON_EARTHSHINE_SHARE 0.02f
/** The disc fades out between these elongations from the sun, in degrees: the real moon is lost in the sun's glare
 *  within about 7 degrees (the Danjon limit), and this also keeps the virtual moon from eclipsing the sun. */
#define MOON_FADE_END_ELONGATION_DEGREES 6.0f
#define MOON_FADE_START_ELONGATION_DEGREES 12.0f
/** How far below the horizon (the sine of the elevation) the disc's centre may go, beyond its own radius, before the
 *  disc is skipped: the sun disc is drawn down to the same -0.02. */
#define MOON_HORIZON_MARGIN 0.02f
/** For the stars' limiting magnitude, the moon brightens the sky in full once it stands this high (the sine; about 11.5
 *  degrees), fading from slightly below the horizon. */
#define MOONLIT_STARS_FADE_START (-0.05f)
#define MOONLIT_STARS_FADE_END 0.2f


// Static functions.
static float ClampUnit(float value)
{
    return fminf(fmaxf(value, 0.0f), 1.0f); // fmaxf drops a NaN, so NaN becomes 0
}

/* Hermite smoothstep: 0 at or below edge0, 1 at or above edge1 (edge0 < edge1); NaN gives 0. */
static float SmoothStep(float edge0, float edge1, float value)
{
    float T = ClampUnit((value - edge0) / (edge1 - edge0));
    return T * T * (3.0f - 2.0f * T);
}

/* The panorama's mip level whose texels match one scene pixel: log2 of a pixel's angular size (the vertical field of
 * view over the scene height) over a texel's (a full turn over the texture width), clamped to the texture's levels. */
static float ComputeMilkyWayLod(const WorldRenderFrame* frame, Texture2D texture)
{
    if ((frame->SceneHeight <= 0) || (texture.width <= 0))
    {
        return 0.0f;
    }
    float PixelAngle = frame->RenderCamera.fovy * DEG2RAD / (float)frame->SceneHeight;
    float TexelAngle = 2.0f * PI / (float)texture.width;
    float MaxLod = (texture.mipmaps > 1) ? (float)(texture.mipmaps - 1) : 0.0f;
    return fminf(fmaxf(log2f(PixelAngle / TexelAngle), 0.0f), MaxLod);
}

/* Whether a slot already holds (or has failed) exactly @p name. */
static bool IsSlotFor(const WorldRendererSkyTexture* slot, const unsigned char* name)
{
    if (!slot->_isSynced || (slot->_name._data == NULL))
    {
        return false;
    }
    bool IsEqual = false;
    Error CompareResult = StringUTF8_EqualsExact(slot->_name._data, name, &IsEqual);
    Error_Deconstruct(&CompareResult);
    return IsEqual;
}

/* Points a slot at @p name: releases what it held (its own user's holds only), remembers the name, and loads the
 * texture under the slot's user. A failure is logged by the loader once for this name, and the slot stays empty until
 * the environment names another texture. */
static void SyncSkyTexture(WorldRenderer* self, WorldRendererSkyTexture* slot, const unsigned char* name,
    const unsigned char* featureName)
{
    if ((name == NULL) || IsSlotFor(slot, name))
    {
        return;
    }
    if (slot->_user != ASSET_USER_ID_INVALID)
    {
        Error ReleaseResult = AssetManager_ReleaseAllAssetsForUser(self->_assetManager, slot->_user);
        Error_Deconstruct(&ReleaseResult); // fails only for an unknown user, which a minted one is not
    }
    slot->_texture = NULL;
    slot->_isSynced = true;
    GenericBuffer_Clear(&slot->_name);
    if (!GenericBuffer_AppendString(&slot->_name, name) || !GenericBuffer_NullTerminate(&slot->_name))
    {
        slot->_isSynced = false; // cannot happen short of running out of memory; try again next frame
        return;
    }

    if (slot->_user == ASSET_USER_ID_INVALID)
    {
        Error UserResult = AssetManager_GetNewUserID(self->_assetManager, &slot->_user);
        if (UserResult.Code != ErrorCode_Success)
        {
            slot->_user = ASSET_USER_ID_INVALID;
            WorldRendererCommon_Warn(self, (const unsigned char*)u8"WorldRenderer: no asset user for a sky texture.");
            Error_Deconstruct(&UserResult);
            return;
        }
    }
    slot->_texture = WorldRendererCommon_LoadTexture(self, slot->_user, name, featureName);
}

/* An empty slot: no texture, no user yet, an empty name buffer. */
static void ConstructSkyTexture(WorldRendererSkyTexture* slot)
{
    *slot = (WorldRendererSkyTexture){ ._texture = NULL, ._user = ASSET_USER_ID_INVALID, ._isSynced = false };
    GenericBuffer_AllocateVariable(&slot->_name, SKY_TEXTURE_NAME_CAPACITY, sizeof(unsigned char));
}

/* Retires a slot's asset user (releasing its texture) and frees its name. */
static void DeconstructSkyTexture(WorldRenderer* self, WorldRendererSkyTexture* slot)
{
    if (slot->_user != ASSET_USER_ID_INVALID)
    {
        Error RetireResult = AssetManager_RetireUser(self->_assetManager, slot->_user);
        Error_Deconstruct(&RetireResult);
        slot->_user = ASSET_USER_ID_INVALID;
    }
    slot->_texture = NULL;
    slot->_isSynced = false;
    Memory_Free(slot->_name._data);
    slot->_name = (GenericBuffer){ 0 };
}

/* The moon's light on the atmosphere (the moonlit sky) and its disc. The moonlit sky is lit by all of the moon's
 * light above the air (the frame's MoonIntensity: the phase's brightness and the daylight gate in), the surfaces by the
 * same. */
static void PrepareMoon(WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;
    frame->MoonSkyIntensity = 0.0f;
    frame->MoonDisc = (WorldRendererMoonDisc){ 0 };
    if (!Environment->IsMoonEnabled)
    {
        return;
    }
    // The moonlit sky shows with the sun down and the moon up (WorldSkyLight_GetMoonGate): outside its gates it is
    // skipped, saving its raymarch, its half of the sky view and the moon's light on surfaces where none can be seen.
    float Gate = WorldSkyLight_GetMoonGate(frame->SunDirection.y, frame->MoonDirection.y);
    frame->MoonSkyIntensity = fmaxf(frame->MoonIntensity, 0.0f) * Gate;

    // The disc: its size, its axes (toward the sky's pole, so the face turns as it crosses the sky), its fade near the
    // sun, and nothing once it is wholly below the horizon.
    float RadiusDegrees = fminf(MOON_REAL_ANGULAR_RADIUS_DEGREES * fmaxf(Environment->MoonSizeMultiplier, 0.0f),
        MOON_MAX_ANGULAR_RADIUS_DEGREES);
    WorldRendererMoonDisc* Disc = &frame->MoonDisc;
    Disc->CosRadius = cosf(RadiusDegrees * DEG2RAD);
    Disc->SinRadius = sinf(RadiusDegrees * DEG2RAD);
    Vector3 Moon = frame->MoonDirection;
    float Elongation = acosf(fminf(fmaxf(Vector3DotProduct(Moon, frame->SunDirection), -1.0f), 1.0f)) * RAD2DEG;
    Disc->Opacity = SmoothStep(MOON_FADE_END_ELONGATION_DEGREES, MOON_FADE_START_ELONGATION_DEGREES, Elongation);
    if (!(Disc->SinRadius > 0.0f) || (Moon.y < -(Disc->SinRadius + MOON_HORIZON_MARGIN)))
    {
        Disc->Opacity = 0.0f;
    }
    Disc->Radiance = MOON_DISC_BASE_RADIANCE * fmaxf(Environment->MoonBrightness, 0.0f);
    Disc->Earthshine = MOON_EARTHSHINE_SHARE * (1.0f - ClampUnit(frame->MoonIllumination));

    Vector3 Pole = { 0.0f, sinf(Environment->SunAngle), -cosf(Environment->SunAngle) };
    Vector3 Right = Vector3CrossProduct(Moon, Pole);
    if (Vector3LengthSqr(Right) < 1e-8f)
    {
        Right = Vector3CrossProduct(Moon, (Vector3){ 1.0f, 0.0f, 0.0f }); // a moon on the pole: any axis will do
    }
    Disc->Right = Vector3Normalize(Right);
    Disc->Up = Vector3CrossProduct(Disc->Right, Moon);
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererSky_Construct(WorldRenderer* self)
{
    ConstructSkyTexture(&self->_sky._milkyWay);
    ConstructSkyTexture(&self->_sky._moon);
}

void WorldRendererSky_Deconstruct(WorldRenderer* self)
{
    DeconstructSkyTexture(self, &self->_sky._milkyWay);
    DeconstructSkyTexture(self, &self->_sky._moon);
}

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
    self->_sky._locSunDirection = GetShaderLocation(RayShader, "sunDirection");
    self->_sky._locSunIntensity = GetShaderLocation(RayShader, "sunIntensity");
    self->_sky._locSunDiscIntensity = GetShaderLocation(RayShader, "sunDiscIntensity");
    self->_sky._locSunSize = GetShaderLocation(RayShader, "sunSize");
    self->_sky._locSkyTint = GetShaderLocation(RayShader, "skyTint");
    self->_sky._locSkyglowRadiance = GetShaderLocation(RayShader, "skyglowRadiance");
    self->_sky._locMilkyWayTexture = GetShaderLocation(RayShader, "milkyWayTexture");
    self->_sky._locWorldToGalactic = GetShaderLocation(RayShader, "worldToGalactic");
    self->_sky._locMilkyWayRadiance = GetShaderLocation(RayShader, "milkyWayRadiance");
    self->_sky._locMilkyWayLod = GetShaderLocation(RayShader, "milkyWayLod");
    self->_sky._locMoonDirection = GetShaderLocation(RayShader, "moonDirection");
    self->_sky._locMoonRight = GetShaderLocation(RayShader, "moonRight");
    self->_sky._locMoonUp = GetShaderLocation(RayShader, "moonUp");
    self->_sky._locMoonCosRadius = GetShaderLocation(RayShader, "moonCosRadius");
    self->_sky._locMoonSinRadius = GetShaderLocation(RayShader, "moonSinRadius");
    self->_sky._locMoonColor = GetShaderLocation(RayShader, "moonColor");
    self->_sky._locMoonDiscRadiance = GetShaderLocation(RayShader, "moonDiscRadiance");
    self->_sky._locMoonEarthshine = GetShaderLocation(RayShader, "moonEarthshine");
    self->_sky._locMoonOpacity = GetShaderLocation(RayShader, "moonOpacity");
    self->_sky._locMoonSkyIntensity = GetShaderLocation(RayShader, "moonSkyIntensity");
    self->_sky._locHasMoonTexture = GetShaderLocation(RayShader, "hasMoonTexture");
    self->_sky._locMoonTexture = GetShaderLocation(RayShader, "moonTexture");
    self->_sky._locNightVisionFieldLuminance = GetShaderLocation(RayShader, "nightVisionFieldLuminance");
    self->_sky._locNightVisionHalfLuminance = GetShaderLocation(RayShader, "nightVisionHalfLuminance");
    self->_sky._locNightVisionSteepness = GetShaderLocation(RayShader, "nightVisionSteepness");
    self->_sky._locNightVisionStrength = GetShaderLocation(RayShader, "nightVisionStrength");
    self->_sky._locNightVisionTint = GetShaderLocation(RayShader, "nightVisionTint");
    WorldRendererAtmosphere_PrepareShader(RayShader, &self->_sky._atmosphere);
    WorldRendererAtmosphere_PrepareHazeShader(RayShader, false, &self->_sky._haze);
}

void WorldRendererSky_SyncTextures(WorldRenderer* self, const WorldEnvironment* environment)
{
    if (self->_sky._shader == NULL)
    {
        return; // nothing would draw them
    }
    SyncSkyTexture(self, &self->_sky._milkyWay, environment->MilkyWayTexture,
        (const unsigned char*)u8"the Milky Way is not drawn");
    SyncSkyTexture(self, &self->_sky._moon, environment->MoonTexture,
        (const unsigned char*)u8"the moon is drawn as a flat grey disc");
}

void WorldRendererSky_PrepareFrame(WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;
    PrepareMoon(frame);

    float GlowColor[3];
    WorldRendererCommon_ColorToLinear(Environment->LightPollutionColor, GlowColor);
    float GlowStrength = SKYGLOW_BASE_RADIANCE * ClampUnit(Environment->LightPollution) * (1.0f - frame->Daylight);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        frame->SkyglowRadiance[Channel] = GlowColor[Channel] * GlowStrength;
    }
}

void WorldRendererSky_PrepareNightSky(WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;
    // The moon brightens the sky the stars are seen against: relative to a full moon high up at the default moonlight.
    float MoonlitSky = 0.0f;
    if (Environment->IsMoonEnabled)
    {
        MoonlitSky = fmaxf(Environment->MoonIntensity, 0.0f) / WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY
            * WorldEnvironment_GetMoonBrightness(frame->MoonIllumination)
            * SmoothStep(MOONLIT_STARS_FADE_START, MOONLIT_STARS_FADE_END, frame->MoonDirection.y);
    }
    // The twilight behind the stars: the sky's light from the sun as its mean luminance, on the real scale.
    frame->LimitingMagnitude = NightSky_GetLimitingMagnitude(Environment->LightPollution, MoonlitSky,
        WorldSkyLight_GetTwilightLuminance(&frame->SkyLight));
    frame->MilkyWayRadiance = MILKY_WAY_BASE_RADIANCE * fmaxf(Environment->MilkyWayBrightness, 0.0f)
        * NightSky_GetMilkyWayVisibility(frame->LimitingMagnitude);
}

bool WorldRendererSky_IsMoonVisible(const WorldRenderFrame* frame)
{
    return frame->Environment->IsMoonEnabled && (frame->MoonDisc.Opacity > 0.0f);
}

/* Each pixel's ray is reconstructed from the inverse of the same view-projection BeginMode3D uses (the frame's, in render
 * space: a rotation and the projection, so the ray holds no large numbers however far out the camera is), so the sky
 * lines up with the 3D camera per pixel. Outputs linear HDR. */
void WorldRendererSky_Draw(WorldRenderer* self, const WorldRenderFrame* frame)
{
    Shader RayShader = GameShader_GetRaylibShader(self->_sky._shader);
    const WorldEnvironment* Environment = frame->Environment;

    float Resolution[2] = { (float)frame->SceneWidth, (float)frame->SceneHeight };
    SetShaderValue(RayShader, self->_sky._locResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValueMatrix(RayShader, self->_sky._locInvViewProj, frame->InvViewProjection);

    float SunDir[3] = { frame->SunDirection.x, frame->SunDirection.y, frame->SunDirection.z };
    SetShaderValue(RayShader, self->_sky._locSunDirection, SunDir, SHADER_UNIFORM_VEC3);

    // The sun above the air (its light on the air in the direct march) and its disc, colours in.
    SetShaderValue(RayShader, self->_sky._locSunIntensity, frame->AirSunIntensity, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_sky._locSunDiscIntensity, frame->SunDiscIntensity, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_sky._locSunSize, &Environment->SunSizeMultiplier, SHADER_UNIFORM_FLOAT);
    // The air, and the tables or the direct march; the ground haze in front of the sky.
    WorldRendererAtmosphere_UploadFrame(RayShader, &self->_sky._atmosphere, frame);
    WorldRendererAtmosphere_UploadHaze(RayShader, &self->_sky._haze, frame);

    float SkyTint[3];
    WorldRendererCommon_ColorToLinear(Environment->SkyTint, SkyTint);
    SetShaderValue(RayShader, self->_sky._locSkyTint, SkyTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_sky._locSkyglowRadiance, frame->SkyglowRadiance, SHADER_UNIFORM_VEC3);

    // The eye's night vision for the sky's dim light (strength 0 leaves it as it is), calibrated on the frame's scale.
    SetShaderValue(RayShader, self->_sky._locNightVisionFieldLuminance, &frame->NightVisionFieldLuminance,
        SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sky._locNightVisionHalfLuminance, &frame->NightVision.HalfLuminance,
        SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sky._locNightVisionSteepness, &frame->NightVision.Steepness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sky._locNightVisionStrength, &frame->NightVisionStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sky._locNightVisionTint, frame->NightVisionTint, SHADER_UNIFORM_VEC3);

    // The moon: its direction (the moonlit sky needs it even without the disc), the moonlit sky's light (0 skips its
    // half), and the disc (an opacity of 0 draws none).
    float MoonDir[3] = { frame->MoonDirection.x, frame->MoonDirection.y, frame->MoonDirection.z };
    SetShaderValue(RayShader, self->_sky._locMoonDirection, MoonDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_sky._locMoonSkyIntensity, frame->AirMoonIntensity, SHADER_UNIFORM_VEC3);
    float MoonOpacity = frame->IsMoonActive ? frame->MoonDisc.Opacity : 0.0f;
    SetShaderValue(RayShader, self->_sky._locMoonOpacity, &MoonOpacity, SHADER_UNIFORM_FLOAT);
    Texture2D MoonTexture = { 0 };
    bool HasMoonTexture = false;
    if (frame->IsMoonActive)
    {
        const WorldRendererMoonDisc* Disc = &frame->MoonDisc;
        float Right[3] = { Disc->Right.x, Disc->Right.y, Disc->Right.z };
        float Up[3] = { Disc->Up.x, Disc->Up.y, Disc->Up.z };
        SetShaderValue(RayShader, self->_sky._locMoonRight, Right, SHADER_UNIFORM_VEC3);
        SetShaderValue(RayShader, self->_sky._locMoonUp, Up, SHADER_UNIFORM_VEC3);
        SetShaderValue(RayShader, self->_sky._locMoonCosRadius, &Disc->CosRadius, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, self->_sky._locMoonSinRadius, &Disc->SinRadius, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, self->_sky._locMoonColor, frame->MoonColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(RayShader, self->_sky._locMoonDiscRadiance, &Disc->Radiance, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, self->_sky._locMoonEarthshine, &Disc->Earthshine, SHADER_UNIFORM_FLOAT);
        HasMoonTexture = (self->_sky._moon._texture != NULL);
        if (HasMoonTexture)
        {
            MoonTexture = GameTexture_GetRaylibTexture(self->_sky._moon._texture);
        }
    }
    int HasMoonTextureValue = HasMoonTexture ? 1 : 0;
    SetShaderValue(RayShader, self->_sky._locHasMoonTexture, &HasMoonTextureValue, SHADER_UNIFORM_INT);

    // The shader samples the Milky Way only while its radiance is above 0.
    float MilkyWayRadiance = frame->IsMilkyWayActive ? frame->MilkyWayRadiance : 0.0f;
    SetShaderValue(RayShader, self->_sky._locMilkyWayRadiance, &MilkyWayRadiance, SHADER_UNIFORM_FLOAT);
    Texture2D MilkyWayTexture = { 0 };
    if (frame->IsMilkyWayActive)
    {
        MilkyWayTexture = GameTexture_GetRaylibTexture(self->_sky._milkyWay._texture);
        Matrix WorldToGalactic = MatrixMultiply(MatrixTranspose(frame->StarsToWorld), NightSky_GetCelestialToGalactic());
        SetShaderValueMatrix(RayShader, self->_sky._locWorldToGalactic, WorldToGalactic);
        float Lod = ComputeMilkyWayLod(frame, MilkyWayTexture);
        SetShaderValue(RayShader, self->_sky._locMilkyWayLod, &Lod, SHADER_UNIFORM_FLOAT);
    }

    // The atmosphere's tables stay on their own units (above the 2D batch's), bound once more in case a pass since the
    // atmosphere's moved them.
    WorldRendererAtmosphere_BindTables(self, frame);
    BeginShaderMode(RayShader);
    // A 2D (batch) draw binds extra samplers through SetShaderValueTexture, after BeginShaderMode.
    if (frame->IsMilkyWayActive)
    {
        SetShaderValueTexture(RayShader, self->_sky._locMilkyWayTexture, MilkyWayTexture);
    }
    if (HasMoonTexture)
    {
        SetShaderValueTexture(RayShader, self->_sky._locMoonTexture, MoonTexture);
    }
    WorldRendererAtmosphere_BindVolumeTexture(self, frame, RayShader, &self->_sky._haze);
    DrawRectangle(0, 0, frame->SceneWidth, frame->SceneHeight, WHITE);
    EndShaderMode();
}
