#include <stddef.h>
#include "WorldEnvironment.h"
#include "wr/WRCompile.h"


// Macros.
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
