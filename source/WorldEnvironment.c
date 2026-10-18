#include <stddef.h>
#include <math.h>
#include "WorldEnvironment.h"
#include "WorldShared.h"
#include "raylib/raymath.h"
#include "wr/WRCompile.h"


// Macros.
/** Pi as a float (M_PI is not standard C, so it is defined locally for -Wpedantic). */
#define ENVIRONMENT_PI 3.14159265358979323846f
/** Default real-time length of a full day-night cycle, in seconds (20 minutes). */
#define DEFAULT_DAY_LENGTH_SECONDS 1200.0f
/** Default sun-arc tilt from the zenith at noon, in radians (~23 degrees, so the sun is not straight up). */
#define DEFAULT_SUN_ANGLE 0.4f
/** Default atmospheric turbidity (a clear-ish sky). */
#define DEFAULT_SKY_TURBIDITY 3.0f
/** Default sun intensity multiplier. */
#define DEFAULT_SUN_INTENSITY 1.0f
/** Default sun disc size multiplier. */
#define DEFAULT_SUN_SIZE_MULTIPLIER 1.0f
/** Default star density: 1 is an Earth-like sky (about 9000 stars, the naked-eye count from a dark site). */
#define DEFAULT_STAR_DENSITY 1.0f
/** Default star brightness multiplier. */
#define DEFAULT_STAR_BRIGHTNESS 1.0f
/** Default star twinkle strength (gentle). */
#define DEFAULT_STAR_TWINKLE 0.25f
/** Default Milky Way brightness multiplier. */
#define DEFAULT_MILKY_WAY_BRIGHTNESS 1.0f
/** Default light pollution: a small town nearby. */
#define DEFAULT_LIGHT_POLLUTION 0.35f
/** Default ambient skylight intensity multiplier. */
#define DEFAULT_AMBIENT_INTENSITY 1.0f
/** Default fog strength multiplier. */
#define DEFAULT_FOG_STRENGTH 1.0f
/** Default post-effect strength multiplier applied to bloom/sunshafts/shadows. */
#define DEFAULT_EFFECT_STRENGTH 1.0f
/** Sun elevation (GetSunDirection().y) at/below which daylight is fully gone (night). */
#define DAYLIGHT_SET_ELEVATION (-0.12f)
/** Sun elevation at/above which daylight is at full strength (day). Twilight ramps between the two. */
#define DAYLIGHT_RISE_ELEVATION (0.10f)


// Types.
/* A float field with a lower bound of 0, for WorldEnvironment_Validate: inclusive, or exclusive when 0 itself
 * breaks the maths (a zero day length or sun size). */
typedef struct FloatRuleStruct
{
    const char* Name;
    float Value;
    bool IsZeroExcluded;
} FloatRule;


// Public functions.
void WorldEnvironment_SetDefaults(WorldEnvironment* self)
{
    if (self == NULL)
    {
        return;
    }

    self->TimeOfDay = WORLD_ENVIRONMENT_TIME_OF_DAY_NOON;
    self->IsDayNightCycleEnabled = true;
    self->DayLengthSeconds = DEFAULT_DAY_LENGTH_SECONDS;
    self->SunAngle = DEFAULT_SUN_ANGLE;

    self->SkyTurbidity = DEFAULT_SKY_TURBIDITY;
    self->SkyGroundAlbedo = (Color){ .r = 90, .g = 90, .b = 90, .a = 255 };
    self->SkyTint = WHITE;

    self->SunColor = (Color){ .r = 255, .g = 244, .b = 214, .a = 255 };
    self->SunIntensity = DEFAULT_SUN_INTENSITY;
    self->SunSizeMultiplier = DEFAULT_SUN_SIZE_MULTIPLIER;

    self->StarSeed = 0;
    self->StarDensity = DEFAULT_STAR_DENSITY;
    self->StarBrightness = DEFAULT_STAR_BRIGHTNESS;
    self->StarTwinkle = DEFAULT_STAR_TWINKLE;
    self->MilkyWayBrightness = DEFAULT_MILKY_WAY_BRIGHTNESS;
    self->LightPollution = DEFAULT_LIGHT_POLLUTION;
    self->LightPollutionColor = (Color){ .r = 255, .g = 160, .b = 90, .a = 255 }; // sodium street lamps

    // Neutral (white) by default: the renderer derives the ambient HUE from the sky colour, so this acts as a
    // tint on top of it (white = pure sky-driven ambient). A world can push it to warm/cool its fill light.
    self->AmbientSkylightColor = WHITE;
    self->AmbientSkylightIntensity = DEFAULT_AMBIENT_INTENSITY;

    self->FogColor = (Color){ .r = 170, .g = 190, .b = 220, .a = 255 };
    self->FogStrength = DEFAULT_FOG_STRENGTH;

    self->IsBloomEnabled = true;
    self->BloomStrength = DEFAULT_EFFECT_STRENGTH;

    self->AreSunshaftsEnabled = true;
    self->SunshaftStrength = DEFAULT_EFFECT_STRENGTH;

    self->AreShadowsEnabled = true;
    self->ShadowStrength = DEFAULT_EFFECT_STRENGTH;

    self->IsAmbientOcclusionEnabled = true;
    self->AmbientOcclusionStrength = DEFAULT_EFFECT_STRENGTH;
}

Error WorldEnvironment_Validate(const WorldEnvironment* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldEnvironment_Validate: self must not be NULL.");
    }

    if (!WorldShared_IsFloatFinite(self->TimeOfDay) || (self->TimeOfDay < 0.0f) || (self->TimeOfDay > 1.0f))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldEnvironment: TimeOfDay must be finite and in [0, 1].");
    }
    if (!WorldShared_IsFloatFinite(self->SunAngle))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "WorldEnvironment: SunAngle must be finite.");
    }
    if (!WorldShared_IsFloatFinite(self->StarTwinkle) || (self->StarTwinkle < 0.0f) || (self->StarTwinkle > 1.0f))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldEnvironment: StarTwinkle must be finite and in [0, 1].");
    }
    if (!WorldShared_IsFloatFinite(self->LightPollution) || (self->LightPollution < 0.0f) || (self->LightPollution > 1.0f))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldEnvironment: LightPollution must be finite and in [0, 1].");
    }

    // Every other float has a lower bound: a minimum, or a strict minimum where 0 itself breaks the maths.
    const FloatRule Rules[] =
    {
        { "DayLengthSeconds", self->DayLengthSeconds, true },
        { "SkyTurbidity", self->SkyTurbidity, false },
        { "SunIntensity", self->SunIntensity, false },
        { "SunSizeMultiplier", self->SunSizeMultiplier, true },
        { "StarDensity", self->StarDensity, false },
        { "StarBrightness", self->StarBrightness, false },
        { "MilkyWayBrightness", self->MilkyWayBrightness, false },
        { "AmbientSkylightIntensity", self->AmbientSkylightIntensity, false },
        { "FogStrength", self->FogStrength, false },
        { "BloomStrength", self->BloomStrength, false },
        { "SunshaftStrength", self->SunshaftStrength, false },
        { "ShadowStrength", self->ShadowStrength, false },
        { "AmbientOcclusionStrength", self->AmbientOcclusionStrength, false },
    };
    for (size_t Index = 0; Index < sizeof(Rules) / sizeof(Rules[0]); Index++)
    {
        const FloatRule* Rule = &Rules[Index];
        bool IsAboveMinimum = Rule->IsZeroExcluded ? (Rule->Value > 0.0f) : (Rule->Value >= 0.0f);
        if (!WorldShared_IsFloatFinite(Rule->Value) || !IsAboveMinimum)
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange, "WorldEnvironment: %s must be finite and %s.",
                Rule->Name, Rule->IsZeroExcluded ? "> 0" : ">= 0");
        }
    }
    return Error_CreateSuccess();
}

Error WorldEnvironment_CopyTo(const WorldEnvironment* self, WorldEnvironment* destination)
{
    if ((self == NULL) || (destination == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldEnvironment_CopyTo: self and destination must not be NULL.");
    }
    if (self == destination)
    {
        return Error_CreateSuccess();
    }

    // No owned fields yet, so a plain copy is a full copy. Once one exists: release the destination's, copy the
    // plain fields, then clone the owned ones.
    *destination = *self;
    return Error_CreateSuccess();
}

void WorldEnvironment_Deconstruct(WorldEnvironment* self)
{
    UNUSED(self);
}


// Static functions.
/* Linearly interpolates one 8-bit channel, clamped to [0, 255]. */
static unsigned char LerpChannel(unsigned char from, unsigned char to, float factor)
{
    float Value = (float)from + ((float)to - (float)from) * factor;
    if (Value < 0.0f) { Value = 0.0f; }
    if (Value > 255.0f) { Value = 255.0f; }
    return (unsigned char)lroundf(Value);
}

/* Linearly interpolates an opaque RGB color. */
static Color LerpColor(Color from, Color to, float factor)
{
    return (Color)
    {
        .r = LerpChannel(from.r, to.r, factor),
        .g = LerpChannel(from.g, to.g, factor),
        .b = LerpChannel(from.b, to.b, factor),
        .a = 255
    };
}

/* Multiplies an RGB color by a tint color (each channel scaled by tint/255). */
static Color TintColor(Color base, Color tint)
{
    return (Color)
    {
        .r = (unsigned char)(((int)base.r * (int)tint.r) / 255),
        .g = (unsigned char)(((int)base.g * (int)tint.g) / 255),
        .b = (unsigned char)(((int)base.b * (int)tint.b) / 255),
        .a = 255
    };
}


// Public functions.
Vector3 WorldEnvironment_GetSunDirection(const WorldEnvironment* self)
{
    // Angle sweeps so that dawn (0.25)/dusk (0.75) are on the horizon and the arc peaks at noon (0.5).
    float Angle = (self->TimeOfDay - 0.25f) * 2.0f * ENVIRONMENT_PI;
    float BaseX = cosf(Angle);
    float BaseY = sinf(Angle);
    // Tilt the arc out of vertical by SunAngle (rotation about the X axis, into Z), so the noon sun sits at
    // an angle from the zenith instead of straight up. BaseY is 0 at dawn/dusk, so those stay on the horizon.
    float Tilt = self->SunAngle;
    return (Vector3)
    {
        .x = BaseX,
        .y = BaseY * cosf(Tilt),
        .z = BaseY * sinf(Tilt)
    };
}

Matrix WorldEnvironment_GetCelestialToWorld(const WorldEnvironment* self)
{
    // The same angle as GetSunDirection: R_z turns the sky through the day, R_x tilts its pole down to SunAngle.
    // raymath's MatrixMultiply(a, b) applies a first, so this is R_x(SunAngle) . R_z(Angle).
    // TODO: the stars should also drift about 1 degree a day against the sun (sidereal time), which needs the
    // in-game date: T03 adds it once the game clock and calendar (T23) exist.
    float Angle = (self->TimeOfDay - 0.25f) * 2.0f * ENVIRONMENT_PI;
    return MatrixMultiply(MatrixRotateZ(Angle), MatrixRotateX(self->SunAngle));
}

float WorldEnvironment_GetDaylightFactor(const WorldEnvironment* self)
{
    float Elevation = WorldEnvironment_GetSunDirection(self).y; // [-1, 1]
    // Smoothstep from "set" to "rise": 0 below the horizon band, 1 above it, a smooth twilight in between.
    float Range = DAYLIGHT_RISE_ELEVATION - DAYLIGHT_SET_ELEVATION;
    float T = (Elevation - DAYLIGHT_SET_ELEVATION) / Range;
    if (T < 0.0f) { T = 0.0f; }
    if (T > 1.0f) { T = 1.0f; }
    return T * T * (3.0f - 2.0f * T);
}

void WorldEnvironment_Advance(WorldEnvironment* self, float deltaSeconds)
{
    if ((self == NULL) || !self->IsDayNightCycleEnabled)
    {
        return;
    }
    if (!WorldShared_IsFloatFinite(self->DayLengthSeconds) || (self->DayLengthSeconds <= 0.0f)
        || !WorldShared_IsFloatFinite(deltaSeconds) || (deltaSeconds < 0.0f))
    {
        return;
    }

    float NextTimeOfDay = self->TimeOfDay + (deltaSeconds / self->DayLengthSeconds);
    NextTimeOfDay -= floorf(NextTimeOfDay); // wrap into [0, 1)
    if (!WorldShared_IsFloatFinite(NextTimeOfDay))
    {
        return; // an overflowing step (a tiny day length) must not leave the time of day NaN
    }
    self->TimeOfDay = NextTimeOfDay;
}

Color WorldEnvironment_ComputeSkyColor(const WorldEnvironment* self)
{
    const Color DayColor = { .r = 108, .g = 158, .b = 224, .a = 255 };
    const Color HorizonColor = { .r = 232, .g = 138, .b = 88, .a = 255 };
    const Color NightColor = { .r = 9, .g = 12, .b = 28, .a = 255 };

    float Elevation = WorldEnvironment_GetSunDirection(self).y; // [-1, 1]

    Color Base;
    if (Elevation >= 0.0f)
    {
        Base = LerpColor(HorizonColor, DayColor, Elevation);
    }
    else
    {
        Base = LerpColor(HorizonColor, NightColor, -Elevation);
    }

    return TintColor(Base, self->SkyTint);
}
