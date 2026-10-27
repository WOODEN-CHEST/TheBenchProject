#pragma once
#include <stdbool.h>
#include "WorldAtmosphere.h"
#include "WorldEnvironment.h"

/*
 * Private header (source/): the sky's light on the world's surfaces as pure maths (T36, decision G39: one light scale for
 * the sky and the surfaces), tested headless (tests/WorldSkyLightTests.c). The renderer's atmosphere pass computes the
 * same light on the GPU, per height, for every lit surface (the irradiance table and the volume's last row,
 * WorldAtmosphere.h has their layout); this file is its C mirror at the camera, which the eye's exposure, its night
 * vision, the choice of the shadow light, the status line and the lit shaders' fallback take.
 *
 * ONE SCALE. The sun above the air lights a surface facing it with WORLD_ATMOSPHERE_SUN_LIGHT x sun_intensity x
 * sun_color (linear): the game's light units, the lamps' and the emitters' as before T36. The air is lit by the same
 * light, so the sky, the haze and the far ground are on the surfaces' scale. The moon above the air is moon_intensity x
 * its phase's brightness x (1 - the daylight factor) x the sun's light, in moon_color.
 *
 * WHAT LIGHTS A SURFACE (WorldSkyLightFrame, at a height above the atmosphere's ground):
 *  - the sun's and the moon's light through the air: the light above the air times the transmittance toward it (the
 *    optical depth of the air to the top of the atmosphere, exactly), times its disc's share above the planet's horizon
 *    (the planet's shadow, soft over the disc): reddened low over the horizon, gone once the disc has set there;
 *  - the sky's irradiance on a surface facing up: the light the air scatters toward it over the upper half of the sky,
 *    the same model as the sky's (single scattering with its phase functions through the transmittance, plus the
 *    multiple scattering table's glow times the multiple_scattering share), lit by the sun, the moon and light
 *    pollution's light in the air, times sky_tint;
 *  - the ground's bounce on a surface facing down: sky_ground_albedo x (the sun's and the moon's light on the ground
 *    and the sky's irradiance);
 *  - the moonless night's own light (night_light_color x night_light_intensity), and the ambient's tint
 *    (ambient_skylight_color x ambient_skylight_intensity) on the sky's part.
 *  A surface with normal n then takes (sky x (1 + n.y) / 2 + ground x (1 - n.y) / 2) / pi x the tint + the night's light
 *  as its ambient (times its albedo), and the sun and the moon through its BRDF.
 *
 * THE C MIRROR. The transmittance at the camera is exact (Gauss-Legendre over the stretches of the ray between the
 * ozone layer's corners, within 1e-9 of the integral). The sky's irradiance is read from the GPU's irradiance table,
 * built in C by the GPU's quadrature (below) and read as the GPU reads it (WorldSkyLight_LookUpSkyIrradiance), from C
 * transmittance and multiple scattering tables of their own (the GPU's layouts and algorithm, at a lower resolution:
 * within 0.3 % of the GPU's in the irradiance). All three are rebuilt when the air changes (WorldSkyLight_BuildTables,
 * or step by step over frames: WorldSkyLight_BuildTablesStep).
 *
 * THE QUADRATURE of the sky's irradiance, the same on the GPU (shaders/atmosphere/irradiance.frag) and here:
 *  - the air's own light (its Rayleigh scattering, the multiple scattering): Gauss-Legendre in the view's zenith cosine
 *    (WORLD_SKY_LIGHT_ZENITH_NODES rings over the upper half) times even azimuths over half a turn
 *    (WORLD_SKY_LIGHT_AZIMUTHS; the sky is symmetric about the light's vertical plane);
 *  - light pollution's, which fills the low air near the camera evenly: the same rings without azimuths, each up to
 *    WORLD_SKY_LIGHT_LIGHT_POLLUTION_DEPTH of its share's own falloff lengths along it;
 *  - the haze's single scattering, whose glow is a narrow lobe around the light: directions around the light by the
 *    inverse of Henyey and Greenstein's phase function with the haze's forward scattering (WORLD_SKY_LIGHT_MIE_RINGS),
 *    weighted by Cornette and Shanks's over it, times even azimuths around the light over half a turn
 *    (WORLD_SKY_LIGHT_MIE_AZIMUTHS), the upper half of the sky only, each up to WORLD_SKY_LIGHT_MIE_DEPTH of the haze's
 *    scale heights above the surface;
 *  - along each direction Simpson's rule over WORLD_SKY_LIGHT_STEPS stretches (the haze's: WORLD_SKY_LIGHT_MIE_STEPS)
 *    growing with the square of their index, the view's optical depth between the points exact for the air's
 *    exponential densities; the multiple scattering's glow once per zenith ring, at the mean of its azimuths' light
 *    angles.
 * Against a fine integral of the same model (the same tables) it is within 2 % from the zenith to 4 degrees below the
 * horizon, in the default 20 km haze (T37) as in the clearest air, and light pollution's within 2 %
 * (tests/WorldSkyLightTests.c).
 *
 * THE EYE. Since T37 the eye adapts to the picture (EyeAdaptation.h). The scene luminance here
 * (WorldSkyLight_GetSceneLuminance) is where it starts before its first measurement, and the split of it into the
 * daylight's share and the night lights' (WorldSkyLight_GetDayLuminance) is how it, and night vision, read the game's
 * light on the real scale.
 */


// Macros.
/** @brief The C transmittance table's size, in texels (the GPU's layout, Bruneton's, at half its size each way: within
 *  0.15 % of its light in the sky's irradiance), and the steps each texel's optical depth takes (growing with the square
 *  of their index, each the exact mean of the air's exponential densities over its heights). */
#define WORLD_SKY_LIGHT_TRANSMITTANCE_WIDTH 128
#define WORLD_SKY_LIGHT_TRANSMITTANCE_HEIGHT 32
#define WORLD_SKY_LIGHT_TRANSMITTANCE_STEPS 16
/** @brief The multiple scattering table: the GPU's algorithm (shaders/atmosphere/multiscatter.frag, its
 *  MULTISCATTER_SQRT_DIRECTIONS squared directions) with half its MULTISCATTER_STEPS steps (within 0.3 % of its light in
 *  the sky's irradiance), in the GPU's layout (WorldAtmosphere_GetMultiscatterUv) at its size. */
#define WORLD_SKY_LIGHT_MULTISCATTER_SIZE 32
#define WORLD_SKY_LIGHT_MULTISCATTER_SQRT_DIRECTIONS 8
#define WORLD_SKY_LIGHT_MULTISCATTER_STEPS 10
/** @brief The sky's irradiance quadrature (the GPU's too: IRRADIANCE_* in shaders/atmosphere/irradiance.frag): the
 *  Gauss-Legendre rings of the zenith cosine, the azimuths over half a turn, the haze's rings around the light and their
 *  azimuths, and Simpson's stretches along the air's directions and along the haze's. */
#define WORLD_SKY_LIGHT_ZENITH_NODES 6
#define WORLD_SKY_LIGHT_AZIMUTHS 6
#define WORLD_SKY_LIGHT_MIE_RINGS 6
#define WORLD_SKY_LIGHT_MIE_AZIMUTHS 6
#define WORLD_SKY_LIGHT_STEPS 8
#define WORLD_SKY_LIGHT_MIE_STEPS 4
/** @brief How far the haze's directions of the quadrature reach above the surface, in the haze's scale heights (its
 *  density is e^-10 there: the rest of the way holds none of its light). */
#define WORLD_SKY_LIGHT_MIE_DEPTH 10.0
/** @brief How far light pollution's directions reach, in the falloff lengths of its share along each (its height's and
 *  its reach's, WorldAtmosphere_GetLightPollutionShare: e^-10 there). */
#define WORLD_SKY_LIGHT_LIGHT_POLLUTION_DEPTH 10.0
/** @brief The steps of a build of the C tables (WorldSkyLight_BuildTablesStep), in order: the transmittance table's rows,
 *  the multiple scattering table's rows, the irradiance table's rows in WORLD_SKY_LIGHT_IRRADIANCE_ROW_PARTS parts each,
 *  then its light pollution column. None takes more than about 0.75 ms (T37's 6 x 6 haze quadrature). */
#define WORLD_SKY_LIGHT_IRRADIANCE_ROW_PARTS 4
#define WORLD_SKY_LIGHT_BUILD_STEP_COUNT (WORLD_SKY_LIGHT_TRANSMITTANCE_HEIGHT + WORLD_SKY_LIGHT_MULTISCATTER_SIZE \
    + WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT * WORLD_SKY_LIGHT_IRRADIANCE_ROW_PARTS + 1)
/** @brief The most light the sun or the moon above the air, the ambient's tint and the night's light of a frame hold
 *  (WorldSkyLight_GetFrameLight), per colour: the largest half float, where the GPU's builders clamp the light above the
 *  air too (their HALF_FLOAT_MAX). So an absurd but valid intensity (up to FLT_MAX) gives the brightest light, never an
 *  infinity, nor an infinity times a zero (NaN). */
#define WORLD_SKY_LIGHT_MAX_LIGHT 65504.0
/** @brief The moon's light counts while the moonlit sky shows (WorldSkyLight_GetMoonGate): it fades out as the sun rises
 *  from the horizon to about 3 degrees (zenith cosines _SUN_FADE_START to _SUN_FADE_END; the day sky drowns it anyway),
 *  and in as the moon rises from about -6 to -3 degrees (_MOON_FADE_START to _MOON_FADE_END; the light of a moon further
 *  down barely reaches the air above). Outside both the renderer skips the moonlit sky's work. */
#define WORLD_SKY_LIGHT_MOON_SUN_FADE_START 0.0f
#define WORLD_SKY_LIGHT_MOON_SUN_FADE_END 0.052f
#define WORLD_SKY_LIGHT_MOON_FADE_START (-0.105f)
#define WORLD_SKY_LIGHT_MOON_FADE_END (-0.052f)
/** @brief The share of a directional light (the sun, the moon) in the scene luminance: a surface facing it gets all of
 *  it, one facing away none. Tuned in T36 on rendered frames, when the eye adapted to this luminance (with nearby lamps
 *  added); since T37 it sets where the eye starts and the night vision's field, whose dusk colours were tuned with it. */
#define WORLD_SKY_LIGHT_DIRECTIONAL_ADAPT_WEIGHT 0.40f


// Types.
/**
 * @brief The C mirror's tables (WorldSkyLight_BuildTables): its own transmittance and multiple scattering tables, which
 *        the sky's irradiance reads along its directions, and the GPU's irradiance table built from them; rebuilt
 *        whenever the air changes (WorldAtmosphere_IsIrradianceStale, as the GPU's irradiance table is). A plain value
 *        type of about 90 KB: keep it in a long-lived heap struct, never on the stack.
 */
typedef struct WorldSkyLightTablesStruct
{
    /** @brief The share of light that crosses the air to the top, by WorldAtmosphere_GetTransmittanceUnit's layout at
     *  WORLD_SKY_LIGHT_TRANSMITTANCE_WIDTH x _HEIGHT texels (rows up the height, columns across the zenith). Linear
     *  RGB. */
    float Transmittance[WORLD_SKY_LIGHT_TRANSMITTANCE_HEIGHT][WORLD_SKY_LIGHT_TRANSMITTANCE_WIDTH][3];
    /** @brief The light scattered twice or more toward a point per unit of scattering, by the GPU's layout
     *  (WorldAtmosphere_GetMultiscatterUv; rows the height, columns the light's zenith cosine). Linear RGB. */
    float Multiscatter[WORLD_SKY_LIGHT_MULTISCATTER_SIZE][WORLD_SKY_LIGHT_MULTISCATTER_SIZE][3];
    /** @brief The GPU's irradiance table (WorldAtmosphere_GetIrradianceUv's layout and size): the sky's irradiance per
     *  unit of a light above the air (WorldSkyLight_GetSkyIrradiance) in the first WORLD_ATMOSPHERE_IRRADIANCE_WIDTH
     *  columns, light pollution's per unit of its radiance (WorldSkyLight_GetLightPollutionIrradiance) in the last; as
     *  natural logarithms, at least WORLD_ATMOSPHERE_IRRADIANCE_LOG_FLOOR. */
    float Irradiance[WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT][WORLD_ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH][3];
} WorldSkyLightTables;

/**
 * @brief The light of the sky at one height (WorldSkyLight_GetFrameLight: the camera's), in the game's light units,
 *        linear RGB. A plain value type.
 */
typedef struct WorldSkyLightFrameStruct
{
    /** @brief The sun's and the moon's light above the air on a surface facing them: what the air is lit by. */
    float SunAboveAir[3];
    float MoonAboveAir[3];
    /** @brief The sun's and the moon's light through the air on a surface facing them (0 once its disc has set). */
    float Sun[3];
    float Moon[3];
    /** @brief The sky's irradiance on a surface facing up (the sun's, the moon's and light pollution's, tinted by
     *  sky_tint), the sun's part of it (the daylight's: WorldSkyLight_GetDayLuminance), and the ground's bounce on a
     *  surface facing down. */
    float Sky[3];
    float SunSky[3];
    float Ground[3];
    /** @brief The ambient's tint and strength on the sky's and the ground's light (ambient_skylight_color x
     *  ambient_skylight_intensity), and the moonless night's own light (night_light_color x night_light_intensity), a
     *  surface's ambient per unit of its albedo. */
    float AmbientTint[3];
    float Night[3];
} WorldSkyLightFrame;

/**
 * @brief The C mirror's tables as the renderer keeps them: the set the frames read and what it holds, and a second set a
 *        rebuild fills step by step when the air changes (WorldSkyLight_BuildTablesStep), swapped in when it is done,
 *        so a change of the air costs a few steps a frame instead of a whole build. WorldSkyLight_ConstructBuilder makes
 *        both sets (about 85 KB each, on the heap). A rebuild is for the air it started with: a change during it waits
 *        for the next one.
 */
typedef struct WorldSkyLightBuilderStruct
{
    /** @brief The set the frames read: built for Record.BuiltFor while Record.IsBuilt. */
    WorldSkyLightTables* Tables;
    /** @brief What Tables holds, and how many builds finished (whole or over frames). */
    WorldAtmosphereTableRecord Record;
    /** @brief The set a rebuild fills. */
    WorldSkyLightTables* Spare;
    /** @brief The air a running rebuild is for, its next step, and whether one runs. */
    WorldAtmosphereMedium BuildingFor;
    int NextStep;
    bool IsBuilding;
} WorldSkyLightBuilder;


// Functions.
/**
 * @brief The share of light that crosses the air from a point to the top of the atmosphere along a ray, exactly: the
 *        optical depth of the air's Rayleigh scattering, the haze's extinction and the ozone layer's absorption, by
 *        Gauss-Legendre over the stretches between the ray's lowest point and the ozone layer's corners (within 1e-9 of
 *        the integral).
 *
 * A ray below the horizon is taken at the horizon (the GPU's table holds the horizon's column there). The height is taken
 * as at least 0.
 * @param medium The air (WorldAtmosphere_GetMedium); must not be NULL.
 * @param ray Where the ray starts (metres above the ground) and its zenith cosine.
 * @param outTransmittance [out] Receives the transmittance per colour, in [0, 1].
 */
void WorldSkyLight_GetTransmittance(const WorldAtmosphereMedium* medium, WorldAtmosphereRay ray,
    double outTransmittance[3]);

/**
 * @brief The share of a light's disc above the planet's horizon seen from a point (the shaders' soft shadow in
 *        atmosphereLightTransmittance): a smooth step of the light's zenith cosine less the horizon's, from half the
 *        disc's diameter (WORLD_ATMOSPHERE_LIGHT_DIAMETER) below to half above.
 * @param ray The point's height and the light's zenith cosine there.
 * @returns The share, in [0, 1].
 */
double WorldSkyLight_GetDiscShare(WorldAtmosphereRay ray);

/**
 * @brief The light of a light (the sun, the moon) reaching a point, per unit of it above the air (the C mirror of
 *        atmosphereLightTransmittance, exactly): WorldSkyLight_GetTransmittance times WorldSkyLight_GetDiscShare.
 * @param medium The air; must not be NULL.
 * @param ray The point's height and the light's zenith cosine there.
 * @param outTransmittance [out] Receives it per colour.
 */
void WorldSkyLight_GetLightTransmittance(const WorldAtmosphereMedium* medium, WorldAtmosphereRay ray,
    double outTransmittance[3]);

/**
 * @brief Builds the C mirror's tables for an air: the transmittance table (each texel's optical depth in
 *        WORLD_SKY_LIGHT_TRANSMITTANCE_STEPS steps), from it the multiple scattering table (the GPU's algorithm, with the
 *        ground's reflection of medium->GroundAlbedo), and from both the irradiance table (the GPU's quadrature, with
 *        medium->MieAnisotropy and medium->MultipleScattering). About 70 ms: call it when the air changes
 *        (WorldAtmosphere_IsIrradianceStale), or spread it over frames with WorldSkyLight_BuildTablesStep (as
 *        WorldSkyLightBuilder does).
 * @param medium The air; must not be NULL.
 * @param outTables [out] Receives the tables; must not be NULL.
 */
void WorldSkyLight_BuildTables(const WorldAtmosphereMedium* medium, WorldSkyLightTables* outTables);

/**
 * @brief One step of WorldSkyLight_BuildTables (at most about 0.75 ms): steps 0 to WORLD_SKY_LIGHT_BUILD_STEP_COUNT - 1,
 *        taken in order for the same air, build the same tables bit for bit. Each step reads only what the steps before
 *        it wrote. Outside the range, nothing.
 * @param medium The air; must not be NULL.
 * @param tables The tables being built; must not be NULL.
 * @param step The step.
 */
void WorldSkyLight_BuildTablesStep(const WorldAtmosphereMedium* medium, WorldSkyLightTables* tables, int step);

/**
 * @brief Makes a builder: both table sets allocated, none built (Record all zero), no rebuild running.
 * @param self The builder; must not be NULL. Release it with WorldSkyLight_DeconstructBuilder.
 */
void WorldSkyLight_ConstructBuilder(WorldSkyLightBuilder* self);

/**
 * @brief Frees a builder's table sets.
 * @param self The builder; must not be NULL.
 */
void WorldSkyLight_DeconstructBuilder(WorldSkyLightBuilder* self);

/**
 * @brief Makes the tables hold an air at once, for a new world's (WorldRenderer_PrepareWorld): builds them whole unless
 *        they already hold it (WorldAtmosphere_IsIrradianceStale), and drops a running rebuild either way.
 * @param self The builder; must not be NULL.
 * @param medium The air; must not be NULL.
 */
void WorldSkyLight_PrepareBuilder(WorldSkyLightBuilder* self, const WorldAtmosphereMedium* medium);

/**
 * @brief Starts what a frame's air needs: a whole build when the tables were never built; a rebuild over frames when none
 *        runs and the air differs from what the tables hold (WorldAtmosphere_IsIrradianceStale).
 * @param self The builder; must not be NULL.
 * @param medium The frame's air; must not be NULL.
 * @returns true while a rebuild runs: the caller takes its steps (WorldSkyLight_RunBuildStep) as its time allows.
 */
bool WorldSkyLight_UpdateBuilder(WorldSkyLightBuilder* self, const WorldAtmosphereMedium* medium);

/**
 * @brief Takes the next step of the running rebuild (at most about 0.75 ms); after the last, swaps the sets, so the frames
 *        read the new tables, records the air they hold and counts the build.
 * @param self The builder; must not be NULL.
 * @returns true while steps remain; false once the rebuild is done, or when none runs.
 */
bool WorldSkyLight_RunBuildStep(WorldSkyLightBuilder* self);

/**
 * @brief A light's light reaching a point per unit of it above the air from the C tables (bilinear, as the GPU's lookup):
 *        the table's transmittance times WorldSkyLight_GetDiscShare.
 * @param tables The tables (WorldSkyLight_BuildTables); must not be NULL.
 * @param ray The point's height and the light's zenith cosine there.
 * @param outTransmittance [out] Receives it per colour.
 */
void WorldSkyLight_LookUpLightTransmittance(const WorldSkyLightTables* tables, WorldAtmosphereRay ray,
    double outTransmittance[3]);

/**
 * @brief The light scattered twice or more toward a point from the C table (bilinear), per unit of the air's scattering
 *        and of the light above the air.
 * @param tables The tables; must not be NULL.
 * @param ray The point's height and the light's zenith cosine there.
 * @param outLight [out] Receives it per colour.
 */
void WorldSkyLight_LookUpMultipleScattering(const WorldSkyLightTables* tables, WorldAtmosphereRay ray,
    double outLight[3]);

/**
 * @brief The sky's irradiance on a surface facing up at a height, per unit of a light above the air at a zenith cosine
 *        (what a texel of the irradiance table holds): single scattering through the transmittance with the phase
 *        functions, plus medium->MultipleScattering of the multiple scattering, by the quadrature in this file's header.
 *        Untinted. 0 for a light below WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION.
 * @param tables The C tables for the same air; must not be NULL.
 * @param medium The air; must not be NULL.
 * @param ray The surface's height above the ground (metres, at least 0) and the light's zenith cosine there.
 * @param outIrradiance [out] Receives it per colour.
 */
void WorldSkyLight_GetSkyIrradiance(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium,
    WorldAtmosphereRay ray, double outIrradiance[3]);

/**
 * @brief Light pollution's irradiance on a surface facing up at a height above the camera's foot, per unit of its
 *        radiance in the air there (WorldAtmosphere_GetLightPollutionRadiance; the GPU's irradiance table's last
 *        column): the town's light the air scatters toward it (WorldAtmosphere_GetLightPollutionShare), by the zenith
 *        rings of the irradiance quadrature, each up to WORLD_SKY_LIGHT_LIGHT_POLLUTION_DEPTH of the share's falloff
 *        lengths. Untinted.
 * @param medium The air; must not be NULL.
 * @param height The surface's height above the ground, in metres (at least 0).
 * @param outIrradiance [out] Receives it per colour.
 */
void WorldSkyLight_GetLightPollutionIrradiance(const WorldAtmosphereMedium* medium, double height,
    double outIrradiance[3]);

/**
 * @brief The sky's irradiance per unit of a light above the air from the irradiance table, as the GPU reads its own
 *        (atmosphereSkyIrradiance): bilinear in the logarithms; 0 for a light below
 *        WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION.
 * @param tables The tables (WorldSkyLight_BuildTables); must not be NULL.
 * @param ray The surface's height above the ground (metres) and the light's zenith cosine there.
 * @param outIrradiance [out] Receives it per colour.
 */
void WorldSkyLight_LookUpSkyIrradiance(const WorldSkyLightTables* tables, WorldAtmosphereRay ray, double outIrradiance[3]);

/**
 * @brief Light pollution's irradiance per unit of its radiance from the irradiance table's last column, as the GPU reads
 *        it (atmosphereLightPollutionIrradiance): linear in the logarithms between its rows.
 * @param tables The tables; must not be NULL.
 * @param height The surface's height above the ground, in metres.
 * @param outIrradiance [out] Receives it per colour.
 */
void WorldSkyLight_LookUpLightPollutionIrradiance(const WorldSkyLightTables* tables, double height,
    double outIrradiance[3]);

/**
 * @brief The sky's light at a height for a frame (the camera's, in the renderer): the lights above the air from the
 *        environment and the sky lights (WorldEnvironment_GetSkyLights), through the air exactly, the sky's irradiance
 *        from the irradiance table (as the lit surfaces take theirs), the ground's bounce, the ambient's tint and the
 *        night's light. A few microseconds. The lights above the air, the ambient's tint and the night's light are at
 *        most WORLD_SKY_LIGHT_MAX_LIGHT, so every value is finite.
 * @param tables The C tables for @p medium; must not be NULL.
 * @param medium The air (WorldAtmosphere_GetMedium of @p environment); must not be NULL.
 * @param environment The environment (its colours, the ambient's and the night's settings); must not be NULL.
 * @param lights The sky's lights (their directions, and the intensities above the air); must not be NULL.
 * @param lightPollution Light pollution's radiance in the air at the camera's foot (WorldAtmosphere_GetLightPollutionRadiance).
 * @param height The height above the atmosphere's ground, in metres (WorldAtmosphere_GetViewHeight's).
 * @param outFrame [out] Receives the light; must not be NULL.
 */
void WorldSkyLight_GetFrameLight(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium,
    const WorldEnvironment* environment, const WorldSkyLights* lights, const float lightPollution[3], double height,
    WorldSkyLightFrame* outFrame);

/**
 * @brief The share of the moon's light above the air that counts (the renderer multiplies the sky lights' MoonIntensity
 *        by it before WorldSkyLight_GetFrameLight, for the surfaces and the air alike): 1 while the moonlit sky shows,
 *        fading out as the sun rises past the horizon and as the moon sinks below it (WORLD_SKY_LIGHT_MOON_SUN_FADE_START
 *        and the rest, Hermite smoothsteps).
 * @param sunCosZenith The sun's zenith cosine (its direction's y); NaN counts as far below the horizon.
 * @param moonCosZenith The moon's zenith cosine; NaN counts as far below the horizon.
 * @returns The share, in [0, 1].
 */
float WorldSkyLight_GetMoonGate(float sunCosZenith, float moonCosZenith);

/**
 * @brief The scene luminance: the luminance of the sky's light on a white surface (the eye starts from it before it
 *        measures the picture, T37): the ambient on one facing up (the sky's irradiance over pi times the tint, plus the
 *        night's light) plus WORLD_SKY_LIGHT_DIRECTIONAL_ADAPT_WEIGHT of the sun's and the moon's through the air.
 * @param frame The light (WorldSkyLight_GetFrameLight); must not be NULL.
 * @returns The luminance (Rec. 709 weights on linear RGB), >= 0 for valid light.
 */
float WorldSkyLight_GetSceneLuminance(const WorldSkyLightFrame* frame);

/**
 * @brief The daylight's share of WorldSkyLight_GetSceneLuminance: the sun's part of the ambient (frame->SunSky, tinted, over
 *        pi) plus WORLD_SKY_LIGHT_DIRECTIONAL_ADAPT_WEIGHT of the sun's light. The rest is the night's lights' (the moon,
 *        the town, the moonless night's own light); night vision and the eye read the two apart
 *        (NightVision_GetFieldLuminance, EyeAdaptation_GetRealScale).
 * @param frame The light; must not be NULL.
 * @returns The luminance, >= 0 for valid light.
 */
float WorldSkyLight_GetDayLuminance(const WorldSkyLightFrame* frame);

/**
 * @brief The twilight's luminance behind the stars (T38): the sky's mean luminance from the sun (the luminance of the
 *        sun's part of its irradiance, frame->SunSky, over pi) on the real scale (WORLD_ATMOSPHERE_SUN_LIGHT_CANDELAS per
 *        WORLD_ATMOSPHERE_SUN_LIGHT). The mean stands for the sky overhead: at dusk the sky is brighter toward the sun,
 *        darker at the zenith.
 * @param frame The light; must not be NULL.
 * @returns The luminance in cd/m^2, >= 0 for valid light.
 */
double WorldSkyLight_GetTwilightLuminance(const WorldSkyLightFrame* frame);

