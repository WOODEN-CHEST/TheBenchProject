#include <stddef.h>
#include <math.h>
#include "WorldEnvironment.h"
#include "wr/WRCompile.h"


// Macros.
/** Pi as a float (M_PI is not standard C, so it is defined locally for -Wpedantic). */
#define ENVIRONMENT_PI 3.14159265358979323846f
/** Default real-time length of a full day-night cycle, in seconds (20 minutes). */
#define DEFAULT_DAY_LENGTH_SECONDS 1200.0f
/** Default atmospheric turbidity (a clear-ish sky). */
#define DEFAULT_SKY_TURBIDITY 3.0f
/** Default sun intensity multiplier. */
#define DEFAULT_SUN_INTENSITY 1.0f
/** Default sun disc size multiplier. */
#define DEFAULT_SUN_SIZE_MULTIPLIER 1.0f
/** Default star field density. */
#define DEFAULT_STAR_DENSITY 1.0f
/** Default star brightness multiplier. */
#define DEFAULT_STAR_BRIGHTNESS 1.0f
/** Default ambient skylight intensity multiplier. */
#define DEFAULT_AMBIENT_INTENSITY 1.0f
/** Default fog strength multiplier. */
#define DEFAULT_FOG_STRENGTH 1.0f
/** Default post-effect strength multiplier applied to bloom/sunshafts/shadows. */
#define DEFAULT_EFFECT_STRENGTH 1.0f


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

    self->SkyTurbidity = DEFAULT_SKY_TURBIDITY;
    self->SkyGroundAlbedo = (Color){ .r = 90, .g = 90, .b = 90, .a = 255 };
    self->SkyTint = WHITE;

    self->SunColor = (Color){ .r = 255, .g = 244, .b = 214, .a = 255 };
    self->SunIntensity = DEFAULT_SUN_INTENSITY;
    self->SunSizeMultiplier = DEFAULT_SUN_SIZE_MULTIPLIER;

    self->StarSeed = 0;
    self->StarDensity = DEFAULT_STAR_DENSITY;
    self->StarBrightness = DEFAULT_STAR_BRIGHTNESS;

    self->AmbientSkylightColor = (Color){ .r = 130, .g = 160, .b = 200, .a = 255 };
    self->AmbientSkylightIntensity = DEFAULT_AMBIENT_INTENSITY;

    self->FogColor = (Color){ .r = 170, .g = 190, .b = 220, .a = 255 };
    self->FogStrength = DEFAULT_FOG_STRENGTH;

    self->IsBloomEnabled = true;
    self->BloomStrength = DEFAULT_EFFECT_STRENGTH;

    self->AreSunshaftsEnabled = true;
    self->SunshaftStrength = DEFAULT_EFFECT_STRENGTH;

    self->AreShadowsEnabled = true;
    self->ShadowStrength = DEFAULT_EFFECT_STRENGTH;
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
    // Angle sweeps so that noon (0.5) is straight up, dawn (0.25)/dusk (0.75) are on the horizon.
    float Angle = (self->TimeOfDay - 0.25f) * 2.0f * ENVIRONMENT_PI;
    return (Vector3)
    {
        .x = cosf(Angle),
        .y = sinf(Angle),
        .z = 0.0f
    };
}

void WorldEnvironment_Advance(WorldEnvironment* self, float deltaSeconds)
{
    if ((self == NULL) || !self->IsDayNightCycleEnabled || (self->DayLengthSeconds <= 0.0f))
    {
        return;
    }

    self->TimeOfDay += deltaSeconds / self->DayLengthSeconds;
    self->TimeOfDay -= floorf(self->TimeOfDay); // wrap into [0, 1)
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
