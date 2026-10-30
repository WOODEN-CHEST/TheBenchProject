#include <stddef.h>
#include <math.h>
#include "WorldEnvironment.h"
#include "GameClock.h"
#include "raylib/raymath.h"


// Macros.
/** Pi as a float (M_PI is not standard C, so it is defined locally for -Wpedantic). */
#define ENVIRONMENT_PI 3.14159265358979323846f
/** Default sun-arc tilt from the zenith at noon, in radians (~23 degrees, so the sun is not straight up). */
#define DEFAULT_SUN_ANGLE 0.4f
/** Default atmospheric turbidity (a clear-ish sky). */
#define DEFAULT_SKY_TURBIDITY 24.0f
/** Default scale height of the haze, in metres (Earth's typical aerosol layer). */
#define DEFAULT_HAZE_HEIGHT 1200.0f
/** Default single-scattering albedo of the haze, sRGB: 244 is linear 0.907, about 1 / 1.1 (the haze's extinction is 1.1
 *  times its scattering: WORLD_ATMOSPHERE_MIE_EXTINCTION). */
#define DEFAULT_HAZE_ALBEDO 244U
/** Default forward scattering of the haze (the Mie anisotropy g). */
#define DEFAULT_SUN_GLOW 0.758f
/** Default air density, ozone and multiple scattering: Earth's, and all of it. */
#define DEFAULT_AIR_DENSITY 1.0f
#define DEFAULT_OZONE 1.0f
#define DEFAULT_MULTIPLE_SCATTERING 1.0f
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
/** ===== NIGHT DARKNESS KNOB =====
 *  Default colour (sRGB) and strength (linear) of the moonless night's own light on surfaces: a faint blue, linear
 *  (0.0040, 0.0050, 0.0110), the dark-site floor of the night's ambient. The eye adapts to most of it: the night's
 *  brightness on screen follows the eye's curve (EyeAdaptation.h, EYE_ADAPTATION_POWER), and night vision's anchors are
 *  set by it. */
#define DEFAULT_NIGHT_LIGHT_RED 161U
#define DEFAULT_NIGHT_LIGHT_GREEN 178U
#define DEFAULT_NIGHT_LIGHT_BLUE 255U
#define DEFAULT_NIGHT_LIGHT_INTENSITY 0.011f
/** Default fog strength multiplier: the physical haze. */
#define DEFAULT_FOG_STRENGTH 1.0f
/** Default aerial distance scale: distances as they are. */
#define DEFAULT_AERIAL_DISTANCE_SCALE 1.0f
/** Default ground haze thickness, in metres: a mist thins by e every 20 m above its base. */
#define DEFAULT_GROUND_HAZE_THICKNESS 20.0f
/** Default ground haze albedo, sRGB: 250 is linear 0.957, a near-white mist (water droplets scatter almost all they
 *  take). */
#define DEFAULT_GROUND_HAZE_ALBEDO 250U
/** Default forward scattering of the ground haze (the phase function's g): a mist's droplets are larger than the air's
 *  haze, and their glow around the sun tighter. */
#define DEFAULT_GROUND_HAZE_GLOW 0.8f
/** How far the sun's and the moon's shadows reach by default, in metres. */
#define DEFAULT_SHADOW_REACH 4000.0f
/** Default post-effect strength multiplier applied to bloom/sunshafts/shadows. */
#define DEFAULT_EFFECT_STRENGTH 1.0f
/** Default night vision strength: the eye's. */
#define DEFAULT_NIGHT_VISION_STRENGTH 1.0f
/** Default night vision blue shift: half way to the night blue, a faint blue cast. */
#define DEFAULT_NIGHT_VISION_BLUE_SHIFT 0.5f
/** The format version of the environment's property table. */
#define ENVIRONMENT_TABLE_VERSION 1U
/** Pi in double precision, for the calendar maths. */
#define ENVIRONMENT_PI_DOUBLE 3.14159265358979323846

/** Default moon phase while the phase source is Fixed: a full moon. */
#define DEFAULT_MOON_PHASE 0.5f
/** Default tilt of the moon's path against the sun's, in radians (the real moon's is 5.1 degrees). */
#define DEFAULT_MOON_ORBIT_TILT 0.09f
/** Default moon disc size: 5 times the real moon's 0.52 degrees, about 16 pixels wide at 640x360. */
#define DEFAULT_MOON_SIZE_MULTIPLIER 5.0f
/** Default moon disc brightness multiplier. */
#define DEFAULT_MOON_BRIGHTNESS 1.0f
/** The lunar phase law (Krisciunas and Schaefer 1991, after Allen): how many magnitudes fainter than full the moon is
 *  per degree of phase angle, and per degree to the fourth power. */
#define MOON_MAGNITUDES_PER_DEGREE 0.026
#define MOON_MAGNITUDES_PER_DEGREE_TO_THE_FOURTH 4.0e-9
/** Days in a tropical year: the sun's year through the seasons and among the stars. */
#define TROPICAL_YEAR_DAYS 365.2422
/** Day number (GameCalendar.h) of the March equinox of 2000, 2000-03-20 07:35 UTC: the sun's mean right ascension and
 *  its seasonal declination are 0 there and a whole number of tropical years later. */
#define MARCH_EQUINOX_DAY 730198.316
/** The sun's right ascension, in turns, at which the star frame equals the celestial frame: 0.25 (90 degrees, the
 *  June solstice). NightSky_GetCelestialToGalactic (source/NightSky.c) places the galaxy in that frame, with the same
 *  right ascension: the two must agree. */
#define STAR_FRAME_SUN_RIGHT_ASCENSION_TURNS 0.25
/** The Earth's axial tilt, 23.44 degrees in radians: how far the seasons move the sun north and south. */
#define AXIAL_TILT_RADIANS 0.40910518f
/** The shadow light casts no shadows below this elevation (the sine; about 3 degrees: shadows that long only streak),
 *  and full shadows from SHADOW_LIGHT_FULL_ELEVATION (about 8.6 degrees), fading in between. */
#define SHADOW_LIGHT_MIN_ELEVATION 0.05f
#define SHADOW_LIGHT_FULL_ELEVATION 0.15f
/** The shadows reach full strength once the brighter light is this many times the other's luminance, and fade to 0
 *  where the two are equal (smoothly in the logarithm of their ratio), so the switch from one light to the other never
 *  jumps. A ratio rather than a difference: the sun's twilight light falls off steeply at its end, where the moon
 *  takes over, and a band over the ratio's logarithm keeps the fade from being squeezed into a few game seconds. */
#define SHADOW_LIGHT_HANDOVER_RATIO 4.0f
/** A shadow light dimmer than this luminance (relative: the sun at noon is about 0.9, the default full moon high up
 *  about 0.025) casts no shadows, fading in to full at twice it: a thin crescent's shadow could not show against the
 *  night's ambient light, and skipping it saves the shadow levels' passes. */
#define SHADOW_LIGHT_MIN_LUMINANCE 0.0006f

/* The inspector's categories. */
#define CATEGORY_TIME ((const unsigned char*)u8"Date and time")
#define CATEGORY_SKY ((const unsigned char*)u8"Sky and sun")
#define CATEGORY_NIGHT_SKY ((const unsigned char*)u8"Night sky")
#define CATEGORY_MOON ((const unsigned char*)u8"Moon")
#define CATEGORY_AMBIENT ((const unsigned char*)u8"Ambient light")
#define CATEGORY_HAZE ((const unsigned char*)u8"Haze")
#define CATEGORY_EFFECTS ((const unsigned char*)u8"Effects")
#define CATEGORY_PHYSICS ((const unsigned char*)u8"Physics")


// Fields.
static const PropertyEnumEntry MoonPhaseSourceEntries[] =
{
    { .Name = u8"calendar", .Value = WorldMoonPhaseSource_Calendar },
    { .Name = u8"fixed", .Value = WorldMoonPhaseSource_Fixed },
};

static const PropertyEnumTable MoonPhaseSourceEnum =
{
    .Name = u8"moon_phase_source", .Entries = MoonPhaseSourceEntries,
    .EntryCount = sizeof(MoonPhaseSourceEntries) / sizeof(MoonPhaseSourceEntries[0])
};

/* Every setting, in inspector order. The wire ids are the save schema (the GHDF entry ids the world encoder has always
 * written): never renumber or reuse one; a removed setting's id goes into the table's retired list. */
static const PropertyDescriptor EnvironmentProperties[] =
{
    {
        .Key = u8"time_of_day", .WireId = 1U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, TimeOfDay),
        .Default.FloatValue = WORLD_ENVIRONMENT_TIME_OF_DAY_NOON, .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_TIME, .DisplayName = u8"Time of day",
        .Description = u8"The time of day the sky shows, as a fraction of the day: 0 is midnight, 0.5 noon. A running "
            u8"game's clock sets it; in a world file it is where the sky starts.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"date", .WireId = 30U, .Type = PropertyType_Date, PROPERTY_MEMBER(WorldEnvironment, Date),
        .Default.DateValue = GAME_CLOCK_DEFAULT_DAY_NUMBER, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_TIME,
        .DisplayName = u8"Date",
        .Description = u8"The date the sky shows: the moon's phase and the stars' place in the year follow it. A "
            u8"running game's clock sets it; in a world file it is where the sky starts.",
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
        .Key = u8"are_seasons_enabled", .WireId = 43U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldEnvironment, AreSeasonsEnabled), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY, .DisplayName = u8"Seasons",
        .Description = u8"When on, the sun follows the seasons: longer days and a higher noon sun in summer, shorter and "
            u8"lower in winter (with a sun angle above 0). Off by default: it changes how much daylight the working "
            u8"hours get.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"sky_turbidity", .WireId = 4U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, SkyTurbidity),
        .Default.FloatValue = DEFAULT_SKY_TURBIDITY, .Rules = PROPERTY_RULES_AT_LEAST(0.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY, .DisplayName = u8"Haze",
        .Description = u8"Atmospheric turbidity: higher values give a hazier, whiter sky (the haze's density is Earth's "
            u8"reference haze times turbidity / 3). The default, 24, sees about 20 km (a hazy summer day); 11 about 40 km, "
            u8"3 about 108 km (the clearest air).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"haze_height", .WireId = 47U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, HazeHeight),
        .Default.FloatValue = DEFAULT_HAZE_HEIGHT, .Rules = PROPERTY_RULES_BETWEEN(WORLD_ENVIRONMENT_MIN_HAZE_HEIGHT,
            WORLD_ENVIRONMENT_MAX_HAZE_HEIGHT),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY, .DisplayName = u8"Haze height",
        .Description = u8"How high the haze reaches, in metres: its density falls by e with every this many metres of "
            u8"height. Low haze hugs the ground and lets hills stand out above it.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"haze_albedo", .WireId = 48U, .Type = PropertyType_Color, PROPERTY_MEMBER(WorldEnvironment, HazeAlbedo),
        .Default.ColorValue = { DEFAULT_HAZE_ALBEDO, DEFAULT_HAZE_ALBEDO, DEFAULT_HAZE_ALBEDO, 255 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY, .DisplayName = u8"Haze albedo",
        .Description = u8"How much of the light the haze takes it scatters (the rest it absorbs): a light grey for clean "
            u8"air, darker or tinted for smog and dust.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"sun_glow", .WireId = 49U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, SunGlow),
        .Default.FloatValue = DEFAULT_SUN_GLOW, .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_SUN_GLOW),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY, .DisplayName = u8"Sun glow",
        .Description = u8"How strongly the haze scatters light forward, 0 to 0.99: how large and bright the glow around "
            u8"the sun and the moon is (0 none, 0.99 a tight, bright halo). It shapes the sky's light on surfaces too.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"air_density", .WireId = 50U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, AirDensity),
        .Default.FloatValue = DEFAULT_AIR_DENSITY,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_AIR_MULTIPLE),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY, .DisplayName = u8"Air density",
        .Description = u8"How strongly the air scatters light (what makes the sky blue), as a multiple of Earth's: "
            u8"thinner or denser air for alien skies.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"ozone", .WireId = 51U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, Ozone),
        .Default.FloatValue = DEFAULT_OZONE,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_AIR_MULTIPLE),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY, .DisplayName = u8"Ozone",
        .Description = u8"The ozone layer's absorption as a multiple of Earth's (0 none): it keeps the twilight sky "
            u8"blue overhead.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"multiple_scattering", .WireId = 52U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, MultipleScattering), .Default.FloatValue = DEFAULT_MULTIPLE_SCATTERING,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY,
        .DisplayName = u8"Multiple scattering",
        .Description = u8"How much of the light the air scatters more than once is drawn: 1 all of it (a brighter, paler "
            u8"horizon, a fuller twilight, more of the sky's light on surfaces), 0 single scattering only, for comparing.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"sky_ground_albedo", .WireId = 5U, .Type = PropertyType_Color,
        PROPERTY_MEMBER(WorldEnvironment, SkyGroundAlbedo), .Default.ColorValue = { 90, 90, 90, 255 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY, .DisplayName = u8"Ground albedo",
        .Description = u8"The ground's colour as the atmosphere sees it: the far ground below the horizon shows it, the "
            u8"light it reflects up feeds the sky's multiple scattering, and it lights every surface from below (the "
            u8"ground's bounce).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"sky_tint", .WireId = 6U, .Type = PropertyType_Color, PROPERTY_MEMBER(WorldEnvironment, SkyTint),
        .Default.ColorValue = { 255, 255, 255, 255 }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY,
        .DisplayName = u8"Sky tint",
        .Description = u8"Multiplies the sky's colour, for alien skies: the sky, the haze and the sky's light on surfaces.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"sun_color", .WireId = 7U, .Type = PropertyType_Color, PROPERTY_MEMBER(WorldEnvironment, SunColor),
        .Default.ColorValue = { 255, 255, 255, 255 }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SKY,
        .DisplayName = u8"Sun colour",
        .Description = u8"The sun's colour above the air: its light, its disc and the light it gives the sky and the haze. "
            u8"The air reddens it toward the horizon by itself, so white is a sun like ours.",
        .SinceVersion = 1U
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
        .Key = u8"milky_way_texture", .WireId = 42U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(WorldEnvironment, MilkyWayTexture), .Default.StringValue = u8"sky_milky_way",
        .Rules = PROPERTY_RULES_STRING(WORLD_ENVIRONMENT_TEXTURE_NAME_MAX_LENGTH, false), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_NIGHT_SKY, .DisplayName = u8"Milky Way texture",
        .Description = u8"The texture the Milky Way is drawn from: a panorama in galactic coordinates.",
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
        .Key = u8"is_moon_enabled", .WireId = 31U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WorldEnvironment, IsMoonEnabled),
        .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOON, .DisplayName = u8"Moon",
        .Description = u8"When off, the world has no moon: no disc, no moonlight, no moonlit sky.", .SinceVersion = 1U
    },
    {
        .Key = u8"moon_phase_source", .WireId = 32U, .Type = PropertyType_Enum,
        PROPERTY_MEMBER(WorldEnvironment, MoonPhaseSource), .Default.EnumValue = WorldMoonPhaseSource_Calendar,
        .EnumTable = &MoonPhaseSourceEnum, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOON,
        .DisplayName = u8"Moon phase source",
        .Description = u8"Where the moon's phase comes from: \"calendar\" follows the date (the real moon's phase by "
            u8"default), \"fixed\" always shows the moon phase below.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"moon_phase", .WireId = 33U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, MoonPhase),
        .Default.FloatValue = DEFAULT_MOON_PHASE, .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOON, .DisplayName = u8"Fixed moon phase",
        .Description = u8"The phase shown while the source is \"fixed\": 0 new, 0.25 first quarter, 0.5 full, 0.75 last "
            u8"quarter, 1 new again.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"moon_phase_offset_days", .WireId = 34U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, MoonPhaseOffsetDays), .Default.FloatValue = 0.0f, .Rules = PROPERTY_RULES_NONE,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOON, .DisplayName = u8"Moon phase offset",
        .Description = u8"Days added to the date before the moon's phase is taken from it: shifts this world's moon "
            u8"against the real one (about 7.4 days is a quarter phase).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"moon_cycle_days", .WireId = 35U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, MoonCycleDays), .Default.FloatValue = WORLD_ENVIRONMENT_MOON_CYCLE_DAYS,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOON,
        .DisplayName = u8"Moon cycle",
        .Description = u8"Days from one new moon to the next (the real moon's 29.53 by default).", .SinceVersion = 1U
    },
    {
        .Key = u8"moon_orbit_tilt", .WireId = 36U, .Type = PropertyType_Angle,
        PROPERTY_MEMBER(WorldEnvironment, MoonOrbitTilt), .Default.FloatValue = DEFAULT_MOON_ORBIT_TILT,
        .Rules = PROPERTY_RULES_NONE, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOON,
        .DisplayName = u8"Moon orbit tilt",
        .Description = u8"How far the moon's path strays north and south of the sun's (about the real moon's 5 degrees "
            u8"by default).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"moon_size_multiplier", .WireId = 37U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, MoonSizeMultiplier), .Default.FloatValue = DEFAULT_MOON_SIZE_MULTIPLIER,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOON,
        .DisplayName = u8"Moon size",
        .Description = u8"The disc's size as a multiple of the real moon's (5 by default, so it shows at the game's "
            u8"resolution).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"moon_brightness", .WireId = 38U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, MoonBrightness), .Default.FloatValue = DEFAULT_MOON_BRIGHTNESS,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOON,
        .DisplayName = u8"Moon brightness",
        .Description = u8"Multiplies the brightness of the moon's disc in the sky (not its light: that is the moonlight "
            u8"below).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"moon_intensity", .WireId = 39U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, MoonIntensity), .Default.FloatValue = WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOON,
        .DisplayName = u8"Moonlight",
        .Description = u8"The light of a full moon above the air, relative to the sun's; the phase scales it, the air dims "
            u8"it toward the horizon, and the moonlit sky is lit by all of it. It fades out by day. 0 gives a moon that "
            u8"lights nothing.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"moon_color", .WireId = 40U, .Type = PropertyType_Color, PROPERTY_MEMBER(WorldEnvironment, MoonColor),
        .Default.ColorValue = { 200, 214, 255, 255 }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MOON,
        .DisplayName = u8"Moon colour",
        .Description = u8"The colour of the moonlight and the moon's disc; blue-white gives the night look.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"moon_texture", .WireId = 41U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(WorldEnvironment, MoonTexture), .Default.StringValue = u8"sky_moon",
        .Rules = PROPERTY_RULES_STRING(WORLD_ENVIRONMENT_TEXTURE_NAME_MAX_LENGTH, false), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MOON, .DisplayName = u8"Moon texture",
        .Description = u8"The texture of the moon's face: the full moon, flat-lit (the game shades the phases).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"ambient_skylight_color", .WireId = 13U, .Type = PropertyType_Color,
        PROPERTY_MEMBER(WorldEnvironment, AmbientSkylightColor), .Default.ColorValue = { 255, 255, 255, 255 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_AMBIENT, .DisplayName = u8"Ambient tint",
        .Description = u8"Tints the sky's light on surfaces (the ambient: the sky's own light from above, the ground's from "
            u8"below); white leaves it as it is.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"ambient_skylight_intensity", .WireId = 14U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, AmbientSkylightIntensity), .Default.FloatValue = DEFAULT_AMBIENT_INTENSITY,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_AMBIENT,
        .DisplayName = u8"Ambient intensity",
        .Description = u8"Multiplies the sky's light on surfaces: 1 is the sky's own, 0 leaves shade lit only by the "
            u8"night's light and lamps.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"night_light_color", .WireId = 61U, .Type = PropertyType_Color,
        PROPERTY_MEMBER(WorldEnvironment, NightLightColor),
        .Default.ColorValue = { DEFAULT_NIGHT_LIGHT_RED, DEFAULT_NIGHT_LIGHT_GREEN, DEFAULT_NIGHT_LIGHT_BLUE, 255 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_AMBIENT, .DisplayName = u8"Night light colour",
        .Description = u8"The colour of the moonless night's own light on surfaces: starlight and the air's faint glow, "
            u8"raised for play as the moonlight is.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"night_light_intensity", .WireId = 62U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, NightLightIntensity), .Default.FloatValue = DEFAULT_NIGHT_LIGHT_INTENSITY,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_AMBIENT,
        .DisplayName = u8"Night light",
        .Description = u8"How bright the moonless night's own light is (its colour times this, linear): 0 gives a "
            u8"pitch-black moonless night outside the moon's and the town's light.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"fog_color", .WireId = 15U, .Type = PropertyType_Color, PROPERTY_MEMBER(WorldEnvironment, FogColor),
        .Default.ColorValue = { 170, 190, 220, 255 }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_HAZE,
        .DisplayName = u8"Fog colour",
        .Description = u8"Not used by the renderer: distant things fade into the haze of the air in front of them. Kept "
            u8"for saved data.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"fog_strength", .WireId = 16U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, FogStrength),
        .Default.FloatValue = DEFAULT_FOG_STRENGTH, .Rules = PROPERTY_RULES_AT_LEAST(0.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_HAZE, .DisplayName = u8"Haze strength",
        .Description = u8"Multiplies the haze on everything in the world and the ground haze (with the player's own "
            u8"setting): 1 is the real air, 0 clear air, 2 twice the haze. The sky stays as it is.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"aerial_distance_scale", .WireId = 53U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, AerialDistanceScale), .Default.FloatValue = DEFAULT_AERIAL_DISTANCE_SCALE,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_AERIAL_DISTANCE_SCALE),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_HAZE, .DisplayName = u8"Aerial distance scale",
        .Description = u8"Treats every distance as this many times longer for the haze, so a small town reads as bigger "
            u8"and hazier; far things still meet the sky at the horizon. 1 is the real distance.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"haze_start_distance", .WireId = 54U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, HazeStartDistance), .Default.FloatValue = 0.0f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_DISTANCE), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_HAZE, .DisplayName = u8"Haze start distance",
        .Description = u8"No haze nearer than this many metres: an artistic choice (the real air starts at 0).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"ground_haze_density", .WireId = 55U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, GroundHazeDensity), .Default.FloatValue = 0.0f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_GROUND_HAZE_DENSITY), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_HAZE, .DisplayName = u8"Ground haze",
        .Description = u8"A low layer of mist: how much of the light it takes per metre at its base (0 none; 0.02 a "
            u8"morning mist that hides things beyond about 200 m).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"ground_haze_base", .WireId = 56U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, GroundHazeBase), .Default.FloatValue = 0.0f,
        .Rules = PROPERTY_RULES_BETWEEN(-WORLD_ENVIRONMENT_MAX_DISTANCE, WORLD_ENVIRONMENT_MAX_DISTANCE),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_HAZE, .DisplayName = u8"Ground haze base",
        .Description = u8"The height of the mist's base, in metres: as dense as its density says below it, thinning above "
            u8"it.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"ground_haze_thickness", .WireId = 57U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, GroundHazeThickness), .Default.FloatValue = DEFAULT_GROUND_HAZE_THICKNESS,
        .Rules = PROPERTY_RULES_BETWEEN(WORLD_ENVIRONMENT_MIN_GROUND_HAZE_THICKNESS,
            WORLD_ENVIRONMENT_MAX_GROUND_HAZE_THICKNESS),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_HAZE, .DisplayName = u8"Ground haze thickness",
        .Description = u8"How quickly the mist thins above its base: by e (to about a third) every this many metres.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"ground_haze_albedo", .WireId = 58U, .Type = PropertyType_Color,
        PROPERTY_MEMBER(WorldEnvironment, GroundHazeAlbedo),
        .Default.ColorValue = { DEFAULT_GROUND_HAZE_ALBEDO, DEFAULT_GROUND_HAZE_ALBEDO, DEFAULT_GROUND_HAZE_ALBEDO, 255 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_HAZE, .DisplayName = u8"Ground haze colour",
        .Description = u8"The mist's colour: how much of the light it takes it scatters (near white for mist, darker or "
            u8"tinted for smoke and dust).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"ground_haze_glow", .WireId = 59U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, GroundHazeGlow), .Default.FloatValue = DEFAULT_GROUND_HAZE_GLOW,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_SUN_GLOW), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_HAZE, .DisplayName = u8"Ground haze glow",
        .Description = u8"How strongly the mist scatters light forward, 0 to 0.99: how large and bright its glow around the "
            u8"sun and the moon is.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"horizon_ground_height", .WireId = 60U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, HorizonGroundHeight), .Default.FloatValue = 0.0f,
        .Rules = PROPERTY_RULES_BETWEEN(-WORLD_ENVIRONMENT_MAX_DISTANCE, WORLD_ENVIRONMENT_MAX_DISTANCE),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_HAZE, .DisplayName = u8"Far ground height",
        .Description = u8"The height of the far ground the sky shows below the horizon, beyond the world's own ground, in "
            u8"metres; the sky is seen from the camera's height above it.",
        .SinceVersion = 1U
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
        .Key = u8"shadow_reach", .WireId = 63U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, ShadowReach), .Default.FloatValue = DEFAULT_SHADOW_REACH,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_SHADOW_REACH), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_EFFECTS, .DisplayName = u8"Shadow reach",
        .Description = u8"How far from the camera the sun's and the moon's shadows reach, in metres (they fade out over "
            u8"its last tenth; 0: none). The player's shadow quality may end them nearer.", .SinceVersion = 1U
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
    {
        .Key = u8"night_vision_strength", .WireId = 44U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, NightVisionStrength), .Default.FloatValue = DEFAULT_NIGHT_VISION_STRENGTH,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_EFFECTS,
        .DisplayName = u8"Night vision",
        .Description = u8"How much dim light loses its colour the way the eye's does at night: 1 is the eye (a moonlit "
            u8"sky looks grey-blue), 0 keeps every colour (a long-exposure photo's night). Changes nothing by day.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"night_vision_blue_shift", .WireId = 45U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldEnvironment, NightVisionBlueShift), .Default.FloatValue = DEFAULT_NIGHT_VISION_BLUE_SHIFT,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_EFFECTS,
        .DisplayName = u8"Night vision blue shift",
        .Description = u8"How blue the night's grey looks: 0 a neutral grey (the eye's night vision sees no colour), 1 "
            u8"the deep blue painters give night scenes; 0.5 is a faint blue cast. Changes nothing by day.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"gravity", .WireId = 46U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment, Gravity),
        .Default.FloatValue = WORLD_ENVIRONMENT_DEFAULT_GRAVITY,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_GRAVITY), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_PHYSICS, .DisplayName = u8"Gravity",
        .Description = u8"How fast things fall, in metres per second squared: 9.81 on Earth, 1.62 on the moon. Characters "
            u8"multiply it by their own gravity setting.",
        .SinceVersion = 1U
    },
};

/* Removed settings, never to be used again: 2 is_day_night_cycle_enabled and 3 day_length_seconds (the game clock's
 * pause and time scale do their work). */
static const uint16_t EnvironmentRetiredWireIds[] = { 2U, 3U };

static const PropertyTable EnvironmentTable =
{
    .Name = u8"world_environment",
    .Version = ENVIRONMENT_TABLE_VERSION,
    .StructSize = sizeof(WorldEnvironment),
    .Descriptors = EnvironmentProperties,
    .DescriptorCount = sizeof(EnvironmentProperties) / sizeof(EnvironmentProperties[0]),
    .RetiredWireIds = EnvironmentRetiredWireIds,
    .RetiredWireIdCount = sizeof(EnvironmentRetiredWireIds) / sizeof(EnvironmentRetiredWireIds[0]),
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

/* Hermite smoothstep: 0 at or below edge0, 1 at or above edge1 (edge0 < edge1); NaN gives 0. */
static float SmoothStep(float edge0, float edge1, float value)
{
    float T = (value - edge0) / (edge1 - edge0);
    T = fminf(fmaxf(T, 0.0f), 1.0f); // fmaxf drops a NaN
    return T * T * (3.0f - 2.0f * T);
}

/* The sky's daily angle: 0 at dawn, pi/2 at noon (the sun's angle along its path). */
static float GetDayAngle(const WorldEnvironment* self)
{
    return (self->TimeOfDay - 0.25f) * 2.0f * ENVIRONMENT_PI;
}

/* The date and time of day as one number of days since 0001-01-01 (in double, so a fraction of a minute survives). */
static double GetTotalDays(const WorldEnvironment* self)
{
    return (double)self->Date + (double)self->TimeOfDay;
}

/* The sun's progress through the tropical year since the March equinox, in turns in [0, 1): its mean right ascension
 * and its place in the seasons. Continuous across days and New Year. */
static double GetYearTurns(const WorldEnvironment* self)
{
    double Years = (GetTotalDays(self) - MARCH_EQUINOX_DAY) / TROPICAL_YEAR_DAYS;
    double Turns = Years - floor(Years);
    return isfinite(Turns) ? Turns : 0.0;
}

/* The declination the seasons give a point of the sun's yearly path that is @p turnsAhead of the sun (radians): the
 * sun's own for 0, the moon's ecliptic share for its elongation. 23.44 degrees x sin(longitude), 0 at the March
 * equinox. */
static float GetSeasonalDeclination(const WorldEnvironment* self, double turnsAhead)
{
    return AXIAL_TILT_RADIANS * (float)sin(2.0 * ENVIRONMENT_PI_DOUBLE * (GetYearTurns(self) + turnsAhead));
}

/* A direction of the celestial frame: @p angle along the sun's path (0 at the sun, negative east of it, as the daily
 * rotation carries the sky west) and @p declination toward the pole above the horizon (celestial -z). */
static Vector3 GetCelestialPoint(float angle, float declination)
{
    return (Vector3)
    {
        .x = cosf(angle) * cosf(declination),
        .y = sinf(angle) * cosf(declination),
        .z = -sinf(declination)
    };
}

/* Wraps a number of cycles to a phase in [0, 1): NaN, infinities and a fraction that rounds up to 1 give 0. */
static float WrapPhase(double cycles)
{
    double Fraction = cycles - floor(cycles);
    float Phase = (float)Fraction;
    return ((Phase >= 0.0f) && (Phase < 1.0f)) ? Phase : 0.0f;
}

/* The calendar phase as a number of cycles since the epoch new moon (not yet wrapped). */
static double GetCalendarMoonCycles(const WorldEnvironment* self)
{
    double Cycle = (isfinite(self->MoonCycleDays) && (self->MoonCycleDays > 0.0f))
        ? (double)self->MoonCycleDays : (double)WORLD_ENVIRONMENT_MOON_CYCLE_DAYS;
    double Offset = isfinite(self->MoonPhaseOffsetDays) ? (double)self->MoonPhaseOffsetDays : 0.0;
    return (GetTotalDays(self) - WORLD_ENVIRONMENT_MOON_EPOCH_DAY + Offset) / Cycle;
}

/* The moon's direction for a phase (see WorldEnvironment_GetMoonDirection). */
static Vector3 GetMoonDirectionForPhase(const WorldEnvironment* self, float phase)
{
    float Elongation = 2.0f * ENVIRONMENT_PI * phase;
    float Tilt = isfinite(self->MoonOrbitTilt) ? self->MoonOrbitTilt : 0.0f;
    float Declination = Tilt * sinf(Elongation);
    if (self->AreSeasonsEnabled)
    {
        // On the ecliptic: the seasons' declination of the moon's place in the year, which is the sun's plus the
        // elongation (a full moon stands where the sun stands half a year later: low in summer, high in winter).
        Declination += GetSeasonalDeclination(self, (double)phase);
    }
    Vector3 Celestial = GetCelestialPoint(-Elongation, Declination);
    return Vector3Normalize(Vector3Transform(Celestial, WorldEnvironment_GetCelestialToWorld(self)));
}

/* The lit fraction of the moon for the two directions: (1 - cos(elongation)) / 2. */
static float GetIlluminationFor(Vector3 sunDirection, Vector3 moonDirection)
{
    float Illumination = 0.5f * (1.0f - Vector3DotProduct(sunDirection, moonDirection));
    return fminf(fmaxf(Illumination, 0.0f), 1.0f);
}

/* The moonlight factor (see WorldEnvironment_GetMoonlightFactor) from values the caller already has. */
static float GetMoonlightFactorFor(const WorldEnvironment* self, float illumination, float daylight)
{
    if (!self->IsMoonEnabled)
    {
        return 0.0f;
    }
    float Factor = WorldEnvironment_GetMoonBrightness(illumination) * (1.0f - daylight);
    return fminf(fmaxf(Factor, 0.0f), 1.0f);
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
    if (self->AreSeasonsEnabled)
    {
        // Off the celestial equator by the seasons' declination: the same frame, a point north or south of (1,0,0).
        Vector3 Celestial = GetCelestialPoint(0.0f, GetSeasonalDeclination(self, 0.0));
        return Vector3Normalize(Vector3Transform(Celestial, WorldEnvironment_GetCelestialToWorld(self)));
    }

    // Angle sweeps so that dawn (0.25)/dusk (0.75) are on the horizon and the arc peaks at noon (0.5).
    float Angle = GetDayAngle(self);
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
    return MatrixMultiply(MatrixRotateZ(GetDayAngle(self)), MatrixRotateX(self->SunAngle));
}

Matrix WorldEnvironment_GetStarsToWorld(const WorldEnvironment* self)
{
    // The sun moves east among the stars one turn a year, so the stars gain one turn a year on the sun's daily motion:
    // the star frame turns ahead of the celestial frame by the sun's right ascension since the star frame's own (the
    // June solstice's). The tests check the direction of the drift by its effect: the stars rise earlier every day.
    double DriftTurns = GetYearTurns(self) - STAR_FRAME_SUN_RIGHT_ASCENSION_TURNS;
    DriftTurns -= floor(DriftTurns);
    float Angle = GetDayAngle(self) + (float)(2.0 * ENVIRONMENT_PI_DOUBLE * DriftTurns);
    return MatrixMultiply(MatrixRotateZ(Angle), MatrixRotateX(self->SunAngle));
}

float WorldEnvironment_GetDaylightForElevation(float sunElevation)
{
    return SmoothStep(WORLD_ENVIRONMENT_DAYLIGHT_SET_ELEVATION, WORLD_ENVIRONMENT_DAYLIGHT_RISE_ELEVATION, sunElevation);
}

float WorldEnvironment_GetDaylightFactor(const WorldEnvironment* self)
{
    // Smoothstep from "set" to "rise": 0 below the horizon band, 1 above it, a smooth twilight in between.
    return WorldEnvironment_GetDaylightForElevation(WorldEnvironment_GetSunDirection(self).y);
}

float WorldEnvironment_GetMoonPhase(const WorldEnvironment* self)
{
    switch (self->MoonPhaseSource)
    {
        case WorldMoonPhaseSource_Fixed:    return WrapPhase((double)self->MoonPhase);
        case WorldMoonPhaseSource_Calendar: return WrapPhase(GetCalendarMoonCycles(self));
    }
    return WrapPhase(GetCalendarMoonCycles(self)); // only a corrupted source gets here: the calendar is the default
}

Vector3 WorldEnvironment_GetMoonDirection(const WorldEnvironment* self)
{
    return GetMoonDirectionForPhase(self, WorldEnvironment_GetMoonPhase(self));
}

float WorldEnvironment_GetMoonIllumination(const WorldEnvironment* self)
{
    return GetIlluminationFor(WorldEnvironment_GetSunDirection(self), WorldEnvironment_GetMoonDirection(self));
}

float WorldEnvironment_GetMoonBrightness(float illumination)
{
    double Illumination = (double)fminf(fmaxf(illumination, 0.0f), 1.0f); // fmaxf drops a NaN
    double PhaseAngle = acos(2.0 * Illumination - 1.0) * 180.0 / ENVIRONMENT_PI_DOUBLE; // degrees, 0 at full moon
    double Squared = PhaseAngle * PhaseAngle;
    double Magnitudes = MOON_MAGNITUDES_PER_DEGREE * PhaseAngle + MOON_MAGNITUDES_PER_DEGREE_TO_THE_FOURTH * Squared * Squared;
    return (float)pow(10.0, -0.4 * Magnitudes);
}

float WorldEnvironment_GetMoonlightFactor(const WorldEnvironment* self)
{
    Vector3 Sun = WorldEnvironment_GetSunDirection(self);
    Vector3 Moon = WorldEnvironment_GetMoonDirection(self);
    return GetMoonlightFactorFor(self, GetIlluminationFor(Sun, Moon), WorldEnvironment_GetDaylightForElevation(Sun.y));
}

void WorldEnvironment_GetSkyLights(const WorldEnvironment* self, WorldSkyLights* outLights)
{
    WorldSkyLights Lights = { 0 };
    Lights.SunDirection = WorldEnvironment_GetSunDirection(self);
    Lights.Daylight = WorldEnvironment_GetDaylightForElevation(Lights.SunDirection.y);
    Lights.SunIntensity = fmaxf(self->SunIntensity, 0.0f); // fmaxf drops a NaN
    Lights.MoonPhase = WorldEnvironment_GetMoonPhase(self);
    Lights.MoonDirection = GetMoonDirectionForPhase(self, Lights.MoonPhase);
    Lights.MoonIllumination = GetIlluminationFor(Lights.SunDirection, Lights.MoonDirection);
    Lights.MoonIntensity = fmaxf(self->MoonIntensity, 0.0f)
        * GetMoonlightFactorFor(self, Lights.MoonIllumination, Lights.Daylight);
    *outLights = Lights;
}

WorldShadowLightChoice WorldEnvironment_ChooseShadowLight(Vector3 sunDirection, float sunLuminance, Vector3 moonDirection,
    float moonLuminance)
{
    WorldShadowLightChoice Choice = { .Light = WorldShadowLight_None, .Direction = sunDirection, .Fade = 0.0f };
    float Brighter = fmaxf(sunLuminance, moonLuminance);
    float Dimmer = fminf(sunLuminance, moonLuminance);
    if (!(Brighter > 0.0f))
    {
        return Choice; // both dark (or broken values)
    }
    bool IsSunBrighter = sunLuminance >= moonLuminance;
    Vector3 Direction = IsSunBrighter ? sunDirection : moonDirection;
    float Rise = SmoothStep(SHADOW_LIGHT_MIN_ELEVATION, SHADOW_LIGHT_FULL_ELEVATION, Direction.y);
    float HandOver = (Dimmer > 0.0f)
        ? SmoothStep(0.0f, logf(SHADOW_LIGHT_HANDOVER_RATIO), logf(Brighter / Dimmer)) : 1.0f;
    float Strength = SmoothStep(SHADOW_LIGHT_MIN_LUMINANCE, 2.0f * SHADOW_LIGHT_MIN_LUMINANCE, Brighter);
    float Fade = Rise * HandOver * Strength;
    if (!(Fade > 0.0f))
    {
        return Choice;
    }
    Choice.Light = IsSunBrighter ? WorldShadowLight_Sun : WorldShadowLight_Moon;
    Choice.Direction = Direction;
    Choice.Fade = Fade;
    return Choice;
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
