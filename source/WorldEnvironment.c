#include <stddef.h>
#include <math.h>
#include "WorldEnvironment.h"
#include "WorldShared.h"
#include "raylib/raymath.h"


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
/** The format version of the environment's property table. */
#define ENVIRONMENT_TABLE_VERSION 1U

/* The inspector's categories. */
#define CATEGORY_TIME ((const unsigned char*)u8"Time of day")
#define CATEGORY_SKY ((const unsigned char*)u8"Sky and sun")
#define CATEGORY_NIGHT_SKY ((const unsigned char*)u8"Night sky")
#define CATEGORY_AMBIENT ((const unsigned char*)u8"Ambient light")
#define CATEGORY_FOG ((const unsigned char*)u8"Fog")
#define CATEGORY_EFFECTS ((const unsigned char*)u8"Effects")


// Fields.
/* Every setting, in inspector order. The wire ids are the save schema (the GHDF entry ids the world encoder has always
 * written): never renumber or reuse one; a removed setting's id goes into the table's retired list. */
static const PropertyDescriptor EnvironmentProperties[] =
{
    {
        .Key = u8"time_of_day", .WireId = 1U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, TimeOfDay),
        .Default.FloatValue = WORLD_ENVIRONMENT_TIME_OF_DAY_NOON, .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_TIME, .DisplayName = u8"Time of day",
        .Description = u8"The time of day as a fraction of the day: 0 is midnight, 0.5 noon. It places the sun.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"is_day_night_cycle_enabled", .WireId = 2U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldEnvironment, IsDayNightCycleEnabled), .Default.BoolValue = true,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_TIME, .DisplayName = u8"Day-night cycle",
        .Description = u8"When on, the time of day advances by itself; when off, it stays where it is.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"day_length_seconds", .WireId = 3U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, DayLengthSeconds), .Default.FloatValue = DEFAULT_DAY_LENGTH_SECONDS,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_TIME,
        .DisplayName = u8"Day length", .Description = u8"Real seconds one full day-night cycle takes.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"sun_angle", .WireId = 23U, .Type = PropertyType_Angle, PROPERTY_MEMBER(WorldEnvironment, SunAngle),
        .Default.FloatValue = DEFAULT_SUN_ANGLE, .Rules = PROPERTY_RULES_NONE, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SKY, .DisplayName = u8"Sun angle",
        .Description = u8"How far the noon sun leans from straight overhead (0 = overhead). It also sets how high the "
            u8"sky's pole stands, like a latitude.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"sky_turbidity", .WireId = 4U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, SkyTurbidity),
        .Default.FloatValue = DEFAULT_SKY_TURBIDITY, .Rules = PROPERTY_RULES_AT_LEAST(0.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY, .DisplayName = u8"Haze",
        .Description = u8"Atmospheric turbidity: higher values give a hazier, whiter sky.", .SinceVersion = 1U
    },
    {
        .Key = u8"sky_ground_albedo", .WireId = 5U, .Type = PropertyType_Color,
        PROPERTY_MEMBER(WorldEnvironment, SkyGroundAlbedo), .Default.ColorValue = { 90, 90, 90, 255 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY, .DisplayName = u8"Ground albedo",
        .Description = u8"The ground's colour as the atmosphere sees it; it tints the horizon.", .SinceVersion = 1U
    },
    {
        .Key = u8"sky_tint", .WireId = 6U, .Type = PropertyType_Color, PROPERTY_MEMBER(WorldEnvironment, SkyTint),
        .Default.ColorValue = { 255, 255, 255, 255 }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY,
        .DisplayName = u8"Sky tint", .Description = u8"Multiplies the sky's colour, for alien skies.", .SinceVersion = 1U
    },
    {
        .Key = u8"sun_color", .WireId = 7U, .Type = PropertyType_Color, PROPERTY_MEMBER(WorldEnvironment, SunColor),
        .Default.ColorValue = { 255, 244, 214, 255 }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY,
        .DisplayName = u8"Sun colour", .Description = u8"The colour of the sun's disc and light.", .SinceVersion = 1U
    },
    {
        .Key = u8"sun_intensity", .WireId = 8U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, SunIntensity),
        .Default.FloatValue = DEFAULT_SUN_INTENSITY, .Rules = PROPERTY_RULES_AT_LEAST(0.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY, .DisplayName = u8"Sun intensity",
        .Description = u8"Multiplies the sun's brightness.", .SinceVersion = 1U
    },
    {
        .Key = u8"sun_size_multiplier", .WireId = 9U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, SunSizeMultiplier), .Default.FloatValue = DEFAULT_SUN_SIZE_MULTIPLIER,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY,
        .DisplayName = u8"Sun size", .Description = u8"Multiplies the sun disc's apparent size.", .SinceVersion = 1U
    },
    {
        .Key = u8"star_seed", .WireId = 10U, .Type = PropertyType_UInt64, PROPERTY_MEMBER(WorldEnvironment, StarSeed),
        .Default.UInt64Value = 0U, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_NIGHT_SKY,
        .DisplayName = u8"Star seed", .Description = u8"Which stars the sky has: the same seed gives the same stars.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"star_density", .WireId = 11U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, StarDensity),
        .Default.FloatValue = DEFAULT_STAR_DENSITY, .Rules = PROPERTY_RULES_AT_LEAST(0.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_NIGHT_SKY, .DisplayName = u8"Star density",
        .Description = u8"How many stars there are: 1 is an Earth-like sky (about 9000), 0 none; at most 20000.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"star_brightness", .WireId = 12U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, StarBrightness), .Default.FloatValue = DEFAULT_STAR_BRIGHTNESS,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_NIGHT_SKY,
        .DisplayName = u8"Star brightness", .Description = u8"Multiplies the stars' natural brightness.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"star_twinkle", .WireId = 29U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, StarTwinkle),
        .Default.FloatValue = DEFAULT_STAR_TWINKLE, .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_NIGHT_SKY, .DisplayName = u8"Star twinkle",
        .Description = u8"How strongly stars twinkle, 0 (steady) to 1; more near the horizon.", .SinceVersion = 1U
    },
    {
        .Key = u8"milky_way_brightness", .WireId = 28U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, MilkyWayBrightness), .Default.FloatValue = DEFAULT_MILKY_WAY_BRIGHTNESS,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_NIGHT_SKY,
        .DisplayName = u8"Milky Way brightness", .Description = u8"Multiplies the Milky Way's natural brightness.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"light_pollution", .WireId = 26U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, LightPollution), .Default.FloatValue = DEFAULT_LIGHT_POLLUTION,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_NIGHT_SKY,
        .DisplayName = u8"Light pollution",
        .Description = u8"Light from nearby towns, 0 (a dark site) to 1 (a city centre): it hides faint stars, washes "
            u8"out the Milky Way and makes the horizon glow at night.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"light_pollution_color", .WireId = 27U, .Type = PropertyType_Color,
        PROPERTY_MEMBER(WorldEnvironment, LightPollutionColor), .Default.ColorValue = { 255, 160, 90, 255 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_NIGHT_SKY, .DisplayName = u8"Light pollution colour",
        .Description = u8"The colour of the light-pollution glow (sodium street lamps by default).", .SinceVersion = 1U
    },
    {
        .Key = u8"ambient_skylight_color", .WireId = 13U, .Type = PropertyType_Color,
        PROPERTY_MEMBER(WorldEnvironment, AmbientSkylightColor), .Default.ColorValue = { 255, 255, 255, 255 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_AMBIENT, .DisplayName = u8"Ambient tint",
        .Description = u8"Tints the ambient light, whose hue follows the sky; white leaves it as it is.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"ambient_skylight_intensity", .WireId = 14U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, AmbientSkylightIntensity), .Default.FloatValue = DEFAULT_AMBIENT_INTENSITY,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_AMBIENT,
        .DisplayName = u8"Ambient intensity", .Description = u8"Multiplies the ambient light.", .SinceVersion = 1U
    },
    {
        .Key = u8"fog_color", .WireId = 15U, .Type = PropertyType_Color, PROPERTY_MEMBER(WorldEnvironment, FogColor),
        .Default.ColorValue = { 170, 190, 220, 255 }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_FOG,
        .DisplayName = u8"Fog colour",
        .Description = u8"Not used by the renderer: distant objects fade into the sky's own colour. Kept for saved data.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"fog_strength", .WireId = 16U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, FogStrength),
        .Default.FloatValue = DEFAULT_FOG_STRENGTH, .Rules = PROPERTY_RULES_AT_LEAST(0.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_FOG, .DisplayName = u8"Fog strength",
        .Description = u8"Multiplies the distance fog (with the player's own fog setting).", .SinceVersion = 1U
    },
    {
        .Key = u8"is_bloom_enabled", .WireId = 17U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldEnvironment, IsBloomEnabled), .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_EFFECTS, .DisplayName = u8"Bloom",
        .Description = u8"When off, this world has no bloom whatever the player's settings.", .SinceVersion = 1U
    },
    {
        .Key = u8"bloom_strength", .WireId = 18U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, BloomStrength), .Default.FloatValue = DEFAULT_EFFECT_STRENGTH,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_EFFECTS,
        .DisplayName = u8"Bloom strength", .Description = u8"Multiplies the bloom (with the player's own setting).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"are_sunshafts_enabled", .WireId = 19U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldEnvironment, AreSunshaftsEnabled), .Default.BoolValue = true,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_EFFECTS, .DisplayName = u8"Sun shafts",
        .Description = u8"When off, this world has no sun shafts whatever the player's settings.", .SinceVersion = 1U
    },
    {
        .Key = u8"sunshaft_strength", .WireId = 20U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, SunshaftStrength), .Default.FloatValue = DEFAULT_EFFECT_STRENGTH,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_EFFECTS,
        .DisplayName = u8"Sun shaft strength",
        .Description = u8"Multiplies the sun shafts (with the player's own setting).", .SinceVersion = 1U
    },
    {
        .Key = u8"are_shadows_enabled", .WireId = 21U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldEnvironment, AreShadowsEnabled), .Default.BoolValue = true,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_EFFECTS, .DisplayName = u8"Shadows",
        .Description = u8"When off, objects in this world cast no shadows whatever the player's settings.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"shadow_strength", .WireId = 22U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, ShadowStrength), .Default.FloatValue = DEFAULT_EFFECT_STRENGTH,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_EFFECTS,
        .DisplayName = u8"Shadow strength",
        .Description = u8"Multiplies how dark shadows are (with the player's own setting).", .SinceVersion = 1U
    },
    {
        .Key = u8"is_ambient_occlusion_enabled", .WireId = 24U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldEnvironment, IsAmbientOcclusionEnabled), .Default.BoolValue = true,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_EFFECTS, .DisplayName = u8"Ambient occlusion",
        .Description = u8"When off, this world has no ambient occlusion whatever the player's settings.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"ambient_occlusion_strength", .WireId = 25U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, AmbientOcclusionStrength), .Default.FloatValue = DEFAULT_EFFECT_STRENGTH,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_EFFECTS,
        .DisplayName = u8"Ambient occlusion strength",
        .Description = u8"Multiplies how much ambient occlusion darkens creases and contacts (with the player's own "
            u8"setting).",
        .SinceVersion = 1U
    },
};

static const PropertyTable EnvironmentTable =
{
    .Name = u8"world_environment",
    .Version = ENVIRONMENT_TABLE_VERSION,
    .StructSize = sizeof(WorldEnvironment),
    .Descriptors = EnvironmentProperties,
    .DescriptorCount = sizeof(EnvironmentProperties) / sizeof(EnvironmentProperties[0]),
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = NULL,
    .Migrate = NULL
};


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
const PropertyTable* WorldEnvironment_GetPropertyTable(void)
{
    return &EnvironmentTable;
}

void WorldEnvironment_SetDefaults(WorldEnvironment* self)
{
    if (self == NULL)
    {
        return;
    }
    Error Result = Properties_Construct(&EnvironmentTable, self); // fails only for NULL arguments
    Error_Deconstruct(&Result);
}

Error WorldEnvironment_Validate(const WorldEnvironment* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldEnvironment_Validate: self must not be NULL.");
    }
    return Properties_Validate(&EnvironmentTable, self);
}

Error WorldEnvironment_CopyTo(const WorldEnvironment* self, WorldEnvironment* destination)
{
    if ((self == NULL) || (destination == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldEnvironment_CopyTo: self and destination must not be NULL.");
    }
    return Properties_CopyTo(&EnvironmentTable, self, destination);
}

void WorldEnvironment_Deconstruct(WorldEnvironment* self)
{
    Properties_Deconstruct(&EnvironmentTable, self);
}

Error WorldEnvironment_ReadJSON(WorldEnvironment* self, JSONCompound* compound, const PropertyReadOptions* options)
{
    if ((self == NULL) || (compound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldEnvironment_ReadJSON: self and compound must not be NULL.");
    }
    return Properties_ReadJSON(&EnvironmentTable, compound, self, options);
}

Error WorldEnvironment_WriteJSON(const WorldEnvironment* self, JSONObjectPool* pool, JSONCompound* compound,
    const PropertyWriteOptions* options)
{
    if ((self == NULL) || (pool == NULL) || (compound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldEnvironment_WriteJSON: self, pool and compound must not be NULL.");
    }
    return Properties_WriteJSON(&EnvironmentTable, self, pool, compound, options);
}

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
