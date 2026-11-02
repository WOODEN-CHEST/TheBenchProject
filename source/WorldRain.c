// The rain and the lightning's bolts as pure maths (WorldRain.h): the rain's rate and mist, the streaks' drift and their
// copies around the camera, the rain map's box and lookups, the splashes' grid and landing, the puddles' coverage, the
// bolt; the C mirror of shaders/common/rain.glsl, shaders/common/puddles.glsl and shaders/rain/.

#include <math.h>
#include <string.h>
#include "WorldRain.h"
#include "WorldWeatherLight.h"
#include "wr/WRMemory.h"


// Macros.
/** The largest multiplier a rain setting takes (the environment's rule). */
#define RAIN_MAX_MULTIPLE ((float)WORLD_ENVIRONMENT_MAX_RAIN_MULTIPLE)
/** sRGB gamma used to make the rain's colour linear. */
#define RAIN_SRGB_GAMMA 2.2f


// Static functions.
/* A finite value in [minimum, maximum]; a non-finite one counts as @p fallback. */
static float ClampFinite(float value, float minimum, float maximum, float fallback)
{
    return isfinite(value) ? fminf(fmaxf(value, minimum), maximum) : fallback;
}

/* x wrapped into [0, period), in double (period > 0); a result that rounds to the period itself is 0. */
static double Wrap(double x, double period)
{
    double Wrapped = x - period * floor(x / period);
    return ((Wrapped >= 0.0) && (Wrapped < period)) ? Wrapped : 0.0;
}

/* GLSL's mod: x - y floor(x / y), in float. */
static float GlslMod(float x, float y)
{
    return x - y * floorf(x / y);
}

/* GLSL's smoothstep. */
static float Smoothstep(float edge0, float edge1, float x)
{
    float T = fminf(fmaxf((x - edge0) / (edge1 - edge0), 0.0f), 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

/* A float's bits as a whole number (the bolt's variant hash). */
static uint32_t GetFloatBits(float value)
{
    uint32_t Bits = 0U;
    memcpy(&Bits, &value, sizeof(Bits));
    return Bits;
}

/* The splashes' line at a height (WorldRain_FindLanding). */
static Vector3 GetLinePoint(float x, float z, Vector2 slant, float height)
{
    return (Vector3){ x + slant.x * height, height, z + slant.y * height };
}


// Internal functions (declared in WorldRain.h).
float WorldRain_GetRainRate(float precipitation)
{
    float P = ClampFinite(precipitation, 0.0f, 1.0f, 0.0f);
    return WORLD_RAIN_DOWNPOUR_RATE * P * P * P;
}

float WorldRain_GetMistExtinction(float precipitation)
{
    float Rate = WorldRain_GetRainRate(precipitation);
    return (Rate > 0.0f) ? (WORLD_RAIN_EXTINCTION_SCALE * powf(Rate, WORLD_RAIN_EXTINCTION_POWER)) : 0.0f;
}

float WorldRain_GetMistThickness(float cloudBaseY, float mistBaseY)
{
    float Thickness = cloudBaseY - mistBaseY;
    if (!isfinite(Thickness))
    {
        return WORLD_RAIN_MIN_MIST_THICKNESS;
    }
    return fminf(fmaxf(Thickness, WORLD_RAIN_MIN_MIST_THICKNESS), (float)WORLD_ENVIRONMENT_MAX_GROUND_HAZE_THICKNESS);
}

void WorldRain_GetFrame(const WorldEnvironment* environment, WorldRainFrame* outFrame)
{
    Memory_Zero(outFrame, sizeof(*outFrame));
    float Precipitation = ClampFinite(environment->Precipitation, 0.0f, 1.0f, 0.0f);
    outFrame->Precipitation = Precipitation;
    outFrame->RainRate = WorldRain_GetRainRate(Precipitation);

    float StreakDensity = ClampFinite(environment->RainStreakDensity, 0.0f, RAIN_MAX_MULTIPLE, 1.0f);
    float SplashDensity = ClampFinite(environment->RainSplashDensity, 0.0f, RAIN_MAX_MULTIPLE, 1.0f);
    outFrame->StreakShare = fminf(Precipitation * StreakDensity, 1.0f);
    outFrame->SplashShare = fminf(Precipitation * SplashDensity, 1.0f);
    outFrame->AreStreaksVisible = outFrame->StreakShare > 0.0f;
    outFrame->AreSplashesVisible = outFrame->SplashShare > 0.0f;
    // A downpour splashes every cell about every WORLD_RAIN_SPLASH_PERIOD seconds, a light rain half as often (and in
    // fewer cells: the share).
    outFrame->SplashPeriod = WORLD_RAIN_SPLASH_PERIOD * (2.0f - Precipitation);

    outFrame->FallSpeed = ClampFinite(environment->RainFallSpeed, (float)WORLD_ENVIRONMENT_MIN_RAIN_FALL_SPEED,
        (float)WORLD_ENVIRONMENT_MAX_RAIN_FALL_SPEED, WORLD_ENVIRONMENT_DEFAULT_RAIN_FALL_SPEED);
    float WindAngle = isfinite(environment->WindDirection) ? environment->WindDirection : 0.0f;
    Vector3 WindDirection = { sinf(WindAngle), 0.0f, cosf(WindAngle) };
    float WindSpeed = ClampFinite(environment->WindSpeed, 0.0f, (float)WORLD_ENVIRONMENT_MAX_WIND_SPEED, 0.0f);
    float WindGust = ClampFinite(environment->WindGust, 0.0f, (float)WORLD_ENVIRONMENT_MAX_WIND_SPEED, 0.0f);
    outFrame->Wind = (Vector3){ WindDirection.x * (WindSpeed + WindGust), 0.0f, WindDirection.z * (WindSpeed + WindGust) };
    outFrame->MeanWind = (Vector3){ WindDirection.x * WindSpeed, 0.0f, WindDirection.z * WindSpeed };
    // Up and upwind, against the mean fall: the fall speed is at least the rule's minimum, so it never lies level.
    float TowardLength = sqrtf(outFrame->MeanWind.x * outFrame->MeanWind.x + outFrame->FallSpeed * outFrame->FallSpeed
        + outFrame->MeanWind.z * outFrame->MeanWind.z);
    outFrame->TowardRain = (Vector3){ -outFrame->MeanWind.x / TowardLength, outFrame->FallSpeed / TowardLength,
        -outFrame->MeanWind.z / TowardLength };

    outFrame->StreakExposure = WORLD_RAIN_EXPOSURE
        * ClampFinite(environment->RainStreakLength, 0.0f, RAIN_MAX_MULTIPLE, 1.0f);
    outFrame->StreakBrightness = ClampFinite(environment->RainStreakBrightness, 0.0f, RAIN_MAX_MULTIPLE, 1.0f);
    Color RainColor = environment->RainColor;
    outFrame->Color[0] = powf((float)RainColor.r / 255.0f, RAIN_SRGB_GAMMA);
    outFrame->Color[1] = powf((float)RainColor.g / 255.0f, RAIN_SRGB_GAMMA);
    outFrame->Color[2] = powf((float)RainColor.b / 255.0f, RAIN_SRGB_GAMMA);

    outFrame->Wetness = ClampFinite(environment->Wetness, 0.0f, 1.0f, 0.0f);
    outFrame->IsMapNeeded = outFrame->AreStreaksVisible || outFrame->AreSplashesVisible || (outFrame->Wetness > 0.0f);
    outFrame->PuddleCoverage = WorldRain_GetPuddleCoverage(outFrame->Wetness,
        ClampFinite(environment->PuddleAmount, 0.0f, RAIN_MAX_MULTIPLE, 1.0f));
    outFrame->PuddleTile = (float)WORLD_RAIN_PUDDLE_TILE * ClampFinite(environment->PuddleScale,
        (float)WORLD_ENVIRONMENT_MIN_PUDDLE_SCALE, (float)WORLD_ENVIRONMENT_MAX_PUDDLE_SCALE, 1.0f);
    // Rings on the puddles while it rains: more and stronger the harder it rains.
    outFrame->RippleStrength = sqrtf(Precipitation);
    outFrame->RippleRate = 0.5f + 2.5f * Precipitation;
}

void WorldRain_AdvanceDrift(WorldRainDrift* drift, const WorldRainFrame* frame, double seconds)
{
    if (!isfinite(seconds) || (seconds <= 0.0))
    {
        return;
    }
    // Every anchor wraps by the box sideways and by the fall's wrap downward, so the drift wraps there too: it stays a
    // small number however long the rain falls.
    drift->Wind.X = Wrap(drift->Wind.X + (double)frame->Wind.x * seconds, WORLD_RAIN_BOX_SIZE);
    drift->Wind.Z = Wrap(drift->Wind.Z + (double)frame->Wind.z * seconds, WORLD_RAIN_BOX_SIZE);
    drift->Fall = Wrap(drift->Fall + (double)frame->FallSpeed * seconds, WORLD_RAIN_FALL_WRAP_BOXES * WORLD_RAIN_BOX_SIZE);
    // The cycles run at the frame's rates: a changing rain changes how fast they run, never where they are.
    if (frame->SplashPeriod > 0.0f)
    {
        drift->SplashCycles = Wrap(drift->SplashCycles + seconds / (double)frame->SplashPeriod, WORLD_RAIN_CYCLE_WRAP);
    }
    drift->RippleCycles = Wrap(drift->RippleCycles + seconds * (double)fmaxf(frame->RippleRate, 0.0f),
        WORLD_RAIN_CYCLE_WRAP);
}

void WorldRain_GetStreakAnchor(const WorldRainDrift* drift, Vector3Double origin, float outAnchor[3], float* outFall)
{
    outAnchor[0] = (float)Wrap(origin.X - drift->Wind.X, WORLD_RAIN_BOX_SIZE);
    outAnchor[1] = (float)Wrap(origin.Y, WORLD_RAIN_BOX_SIZE);
    outAnchor[2] = (float)Wrap(origin.Z - drift->Wind.Z, WORLD_RAIN_BOX_SIZE);
    *outFall = (float)Wrap(drift->Fall, WORLD_RAIN_FALL_WRAP_BOXES * WORLD_RAIN_BOX_SIZE);
}

float WorldRain_GetSpeedFactor(uint32_t speedClass)
{
    uint32_t Class = (speedClass < WORLD_RAIN_SPEED_CLASS_COUNT) ? speedClass : (WORLD_RAIN_SPEED_CLASS_COUNT - 1U);
    return (float)(WORLD_RAIN_SLOWEST_QUARTERS + Class) / 4.0f;
}

Vector3 WorldRain_GetStreakPosition(Vector3 seed, uint32_t speedClass, const float anchor[3], float fall)
{
    // A streak falls by its class's share of the fall: its anchor's y is the camera's height plus that much.
    float Box = (float)WORLD_RAIN_BOX_SIZE;
    float Half = 0.5f * Box;
    float AnchorY = anchor[1] + WorldRain_GetSpeedFactor(speedClass) * fall;
    return (Vector3)
    {
        GlslMod(seed.x * Box - anchor[0] + Half, Box) - Half,
        GlslMod(seed.y * Box - AnchorY + Half, Box) - Half,
        GlslMod(seed.z * Box - anchor[2] + Half, Box) - Half
    };
}

float WorldRain_GetBoxFade(Vector3 position)
{
    float Largest = fmaxf(fmaxf(fabsf(position.x), fabsf(position.y)), fabsf(position.z));
    float Half = 0.5f * (float)WORLD_RAIN_BOX_SIZE;
    return 1.0f - Smoothstep(Half - WORLD_RAIN_BOX_FADE, Half, Largest);
}

WorldRenderCascadeShape WorldRain_GetMapShape(void)
{
    return (WorldRenderCascadeShape)
    {
        .Size = WORLD_RAIN_MAP_SIZE,
        .LightDistance = WORLD_RAIN_MAP_EYE_DISTANCE,
        .NearPlane = 0.0f,
        .FarPlane = WORLD_RAIN_MAP_DEPTH,
        .MapSize = WORLD_RAIN_MAP_TEXELS
    };
}

bool WorldRain_IsMapDue(WorldRainMapKeep* keep, Vector3Double origin, Vector3 towardRain)
{
    if (keep->FramesSince < UINT32_MAX)
    {
        keep->FramesSince++;
    }
    if (!keep->HasMap || (keep->FramesSince >= WORLD_RAIN_MAP_PERIOD))
    {
        return true;
    }
    double X = origin.X - keep->Center.X;
    double Y = origin.Y - keep->Center.Y;
    double Z = origin.Z - keep->Center.Z;
    float Turn = (towardRain.x * keep->TowardRain.x) + (towardRain.y * keep->TowardRain.y)
        + (towardRain.z * keep->TowardRain.z);
    // Written so a NaN draws the map again.
    return !(((X * X) + (Y * Y) + (Z * Z)) < (WORLD_RAIN_MAP_MOVE * WORLD_RAIN_MAP_MOVE))
        || !(Turn >= WORLD_RAIN_MAP_TURN_COSINE);
}

void WorldRain_KeepMap(WorldRainMapKeep* keep, Vector3Double center, Vector3 towardRain)
{
    keep->HasMap = true;
    keep->Center = center;
    keep->TowardRain = towardRain;
    keep->FramesSince = 0U;
}

void WorldRain_GetMapOffsets(float* outDepthBias, float* outNormalOffset)
{
    float Texel = WORLD_RAIN_MAP_SIZE / (float)WORLD_RAIN_MAP_TEXELS;
    *outDepthBias = WORLD_RAIN_MAP_DEPTH_BIAS_TEXELS * Texel / WORLD_RAIN_MAP_DEPTH;
    *outNormalOffset = WORLD_RAIN_MAP_NORMAL_OFFSET_TEXELS * Texel;
}

float WorldRain_GetMapEdgeOpenness(float clipX, float clipY, float clipZ)
{
    if (!isfinite(clipX) || !isfinite(clipY) || !(clipZ < 1.0f))
    {
        return 1.0f;
    }
    return Smoothstep(WORLD_RAIN_MAP_EDGE_FADE_START, 1.0f, fmaxf(fabsf(clipX), fabsf(clipY)));
}

Vector2 WorldRain_GetSlant(Vector3 towardRain)
{
    if (!isfinite(towardRain.x) || !isfinite(towardRain.z) || !(towardRain.y > 0.0f) || !isfinite(towardRain.y))
    {
        return (Vector2){ 0.0f, 0.0f };
    }
    return (Vector2){ towardRain.x / towardRain.y, towardRain.z / towardRain.y };
}

bool WorldRain_FindLanding(WorldRainOpenFunction isOpen, const void* context, float x, float z, Vector2 slant, float top,
    float bottom, Vector3* outLanding)
{
    if (!isOpen(context, GetLinePoint(x, z, slant, top)) || isOpen(context, GetLinePoint(x, z, slant, bottom)))
    {
        return false;
    }
    float High = top;
    float Low = bottom;
    for (int Step = 0; Step < WORLD_RAIN_SPLASH_STEPS; Step++)
    {
        float Middle = 0.5f * (High + Low);
        if (isOpen(context, GetLinePoint(x, z, slant, Middle)))
        {
            High = Middle;
        }
        else
        {
            Low = Middle;
        }
    }
    *outLanding = GetLinePoint(x, z, slant, 0.5f * (High + Low));
    return true;
}

void WorldRain_GetSplashGrid(Vector3Double origin, float outCell[2], float outCorner[2])
{
    double Half = (double)(WORLD_RAIN_SPLASH_GRID / 2);
    double FirstX = floor(origin.X / WORLD_RAIN_SPLASH_CELL) - Half;
    double FirstZ = floor(origin.Z / WORLD_RAIN_SPLASH_CELL) - Half;
    outCell[0] = (float)Wrap(FirstX, WORLD_RAIN_SPLASH_HASH_PERIOD);
    outCell[1] = (float)Wrap(FirstZ, WORLD_RAIN_SPLASH_HASH_PERIOD);
    outCorner[0] = (float)(FirstX * WORLD_RAIN_SPLASH_CELL - origin.X);
    outCorner[1] = (float)(FirstZ * WORLD_RAIN_SPLASH_CELL - origin.Z);
}

uint32_t WorldRain_Hash(uint32_t x, uint32_t y, uint32_t z)
{
    // Three odd multipliers mix the inputs, then the "lowbias32" finalizer spreads every bit.
    uint32_t Hash = (x * 0x8DA6B343U) ^ (y * 0xD8163841U) ^ (z * 0xCB1AB31FU);
    Hash ^= Hash >> 16;
    Hash *= 0x7FEB352DU;
    Hash ^= Hash >> 15;
    Hash *= 0x846CA68BU;
    Hash ^= Hash >> 16;
    return Hash;
}

void WorldRain_GetPatternOrigin(Vector3Double origin, double period, float outWrapped[2])
{
    outWrapped[0] = (float)Wrap(origin.X, period);
    outWrapped[1] = (float)Wrap(origin.Z, period);
}

float WorldRain_GetPuddleCoverage(float wetness, float amount)
{
    float Wetness = ClampFinite(wetness, 0.0f, 1.0f, 0.0f);
    float Amount = ClampFinite(amount, 0.0f, RAIN_MAX_MULTIPLE, 0.0f);
    float Ramp = fminf(fmaxf((Wetness - WORLD_RAIN_PUDDLE_WETNESS) / (1.0f - WORLD_RAIN_PUDDLE_WETNESS), 0.0f), 1.0f);
    return fminf(WORLD_RAIN_PUDDLE_MAX_COVERAGE * Ramp * Amount, 1.0f);
}

float WorldRain_GetPuddleThreshold(float coverage)
{
    // The mean of two even reads has a triangular distribution: a share 2 (1 - t)^2 of it lies above t >= 1/2, and
    // 1 - 2 t^2 above t <= 1/2.
    float Coverage = ClampFinite(coverage, 0.0f, 1.0f, 0.0f);
    return (Coverage <= 0.5f) ? (1.0f - sqrtf(0.5f * Coverage)) : sqrtf(0.5f * (1.0f - Coverage));
}

void WorldRain_GetBolt(const WorldEnvironment* environment, const float flashIrradiance[3], double originY,
    WorldRainBolt* outBolt)
{
    Memory_Zero(outBolt, sizeof(*outBolt));
    float Flash = ClampFinite(environment->LightningFlash, 0.0f, 1.0f, 0.0f);
    float Brightness = ClampFinite(environment->LightningBoltBrightness, 0.0f, RAIN_MAX_MULTIPLE, 0.0f);
    if ((Flash < WORLD_RAIN_BOLT_MIN_FLASH) || !(Brightness > 0.0f))
    {
        return;
    }
    Vector3 Level = environment->LightningDirection;
    float LevelLength = sqrtf(Level.x * Level.x + Level.z * Level.z);
    Level = (isfinite(LevelLength) && (LevelLength > 0.0f))
        ? (Vector3){ Level.x / LevelLength, 0.0f, Level.z / LevelLength } : (Vector3){ 0.0f, 0.0f, 1.0f };
    float Distance = ClampFinite(environment->LightningDistance, WORLD_WEATHER_FLASH_NEAREST_DISTANCE,
        (float)WORLD_ENVIRONMENT_MAX_DISTANCE, WORLD_WEATHER_FLASH_NEAREST_DISTANCE);
    double GroundY = (double)ClampFinite(environment->HorizonGroundHeight, -(float)WORLD_ENVIRONMENT_MAX_DISTANCE,
        (float)WORLD_ENVIRONMENT_MAX_DISTANCE, 0.0f);
    float Height = ClampFinite(environment->CloudBase, 1.0f, (float)WORLD_ENVIRONMENT_MAX_CLOUD_HEIGHT, 1.0f);

    outBolt->IsVisible = true;
    outBolt->Foot = (Vector3){ Level.x * Distance, (float)(GroundY - originY), Level.z * Distance };
    outBolt->Top = (Vector3){ outBolt->Foot.x, (float)(GroundY + (double)Height - originY), outBolt->Foot.z };
    outBolt->Width = Height * WORLD_RAIN_BOLT_ASPECT;
    uint32_t Hash = WorldRain_Hash(GetFloatBits(Level.x), GetFloatBits(Level.z), GetFloatBits(Distance));
    outBolt->Variant = Hash % WORLD_RAIN_BOLT_VARIANTS;
    outBolt->IsMirrored = ((Hash >> 8) & 1U) != 0U;
    outBolt->Radiance = WORLD_RAIN_BOLT_RADIANCE * Flash * Brightness;
    Vector3 Middle = { outBolt->Foot.x, 0.5f * (outBolt->Foot.y + outBolt->Top.y), outBolt->Foot.z };
    float MiddleLength = sqrtf(Middle.x * Middle.x + Middle.y * Middle.y + Middle.z * Middle.z);
    outBolt->Direction = (MiddleLength > 0.0f)
        ? (Vector3){ Middle.x / MiddleLength, Middle.y / MiddleLength, Middle.z / MiddleLength } : Level;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        float Light = isfinite(flashIrradiance[Channel]) ? fmaxf(flashIrradiance[Channel], 0.0f) : 0.0f;
        outBolt->Irradiance[Channel] = Light * WORLD_RAIN_BOLT_LIGHT_SHARE * Brightness;
    }
}
