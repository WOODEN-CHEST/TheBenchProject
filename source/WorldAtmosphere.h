#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "WorldEnvironment.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): the atmosphere as the world renderer models it, pure maths tested headless
 * (tests/WorldAtmosphereTests.c): the air's coefficients from a world's environment, the height the sky is seen from,
 * and the layout of the atmosphere's lookup tables. The renderer's atmosphere pass (WorldRendererAtmosphere.c) builds
 * the tables on the GPU with shaders/common/atmosphere.glsl, whose mappings are the formulas below in float: this file
 * is their C mirror. Keep the two in step (tests/ShaderSourceTests.c pins the constants to these macros).
 *
 * THE AIR. Three things in it scatter or absorb light, each with a density falling with height above sea level:
 *  - the air itself (Rayleigh scattering, much stronger for blue light: the blue sky), with a scale height of 8 km,
 *    times the environment's AirDensity;
 *  - the haze (Mie scattering by aerosols: the white glow near the horizon and around the sun), Earth's times
 *    SkyTurbidity / 3, thinning by e every HazeHeight metres; it takes WORLD_ATMOSPHERE_MIE_EXTINCTION per metre at sea
 *    level at turbidity 3, scatters HazeAlbedo of that and absorbs the rest, and scatters forward by SunGlow (the phase
 *    function's g);
 *  - the ozone layer (absorption only, mostly of orange and red light), a tent from 10 to 40 km densest at 25 km, times
 *    Ozone: the light that reaches the sky overhead at twilight crosses it, which keeps that sky blue.
 *
 * THE TABLES (Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020):
 *  - TRANSMITTANCE (WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH x _HEIGHT): the share of light that crosses the air from a
 *    point to the top of the atmosphere, by the point's height and the ray's zenith angle, in Bruneton's
 *    parameterization (2017: rows by the distance to the planet's axis, so dense near the ground; columns by the
 *    distance to the top, from straight up to the horizon). Rays below the horizon meet the ground and are not in it.
 *  - MULTIPLE SCATTERING (WORLD_ATMOSPHERE_MULTISCATTER_SIZE squared): the light scattered twice or more toward a point,
 *    as an even glow, by its height and the light's zenith angle: Hillaire's approximation, a geometric series of the
 *    second order, with the light the ground (SkyGroundAlbedo) reflects.
 *  - SKY VIEW (WORLD_ATMOSPHERE_SKY_VIEW_WIDTH x _HEIGHT, twice: the sun's in the texture's first rows, the moon's in
 *    the rows after them, WorldAtmosphereSkyViewHalf): the light the sky sends
 *    toward the camera, by direction, from the camera's height. Across, the azimuth from the light (the sky is
 *    symmetric about the light's vertical plane), as the sine of half the angle: 0.6 degrees a texel next to the light,
 *    where its glow is. Up, from the zenith to the horizon (top half) and from the horizon down (bottom half), each half
 *    quadratic toward the horizon, where the sky changes fastest: from 1.7 m the two rows next to it are 0.008 degrees
 *    from it.
 * The renderer keeps them on fixed texture units for every reader (WorldRendererTextureUnit_Atmosphere*).
 *
 * THE HAZE. Far things fade into the haze of the air in front of them, from the same
 * atmosphere as the sky (aerial perspective):
 *  - THE VOLUME (Hillaire's aerial-perspective volume): for WORLD_ATMOSPHERE_VOLUME_SIZE squared screen directions, at
 *    WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT distances, the light the air scattered toward the camera along the path so far
 *    and the path's transmittance (both RGB), built every frame from the tables. Its slices grow by a fixed ratio from
 *    WORLD_ATMOSPHERE_VOLUME_NEAREST to WORLD_ATMOSPHERE_VOLUME_FARTHEST; the last holds the whole ray, to the top of the
 *    atmosphere, so every distance has an answer. Its rays pass through the planet's surface (the air below it as dense
 *    as on the ground): the world's ground is flat and goes on where the planet curves away. One texture holds it: the
 *    slices side by side, the in-scatter's rows below the transmittance's, then a row of the frame's values (the ground
 *    haze's light: WorldAtmosphereVolumeValue).
 *  - THE GROUND HAZE: a low layer of mist, density x exp(-max(height - base, 0) / thickness), integrated per pixel in
 *    closed form along each ray (WorldAtmosphere_GetGroundHazeDepth).
 *  - THE FRAME'S HAZE (WorldAtmosphere_GetHaze): every haze distance is multiplied by the environment's
 *    AerialDistanceScale and FogStrength and the player's fog strength; none nearer than HazeStartDistance.
 *  - LIGHT POLLUTION is the town's light scattered by the air: the air below about WORLD_ATMOSPHERE_LIGHT_POLLUTION_HEIGHT
 *    and within about WORLD_ATMOSPHERE_LIGHT_POLLUTION_REACH of the camera is lit by it
 *    (WorldAtmosphere_GetLightPollutionShare), so the glow grows with the haze.
 *
 * PRECISION. Heights are taken as heights above the ground, never as a radius minus the planet's: a float holds a
 * radius of 6.4e6 m only to half a metre, and r^2 - R^2 near the ground loses most of its digits. The formulas below
 * avoid that cancellation in the same way the shaders do.
 */


// Macros.
/** @brief The planet's radius and the top of its atmosphere, in metres (ATMOSPHERE_R_PLANET, ATMOSPHERE_R_ATMOS). */
#define WORLD_ATMOSPHERE_PLANET_RADIUS 6371000.0
#define WORLD_ATMOSPHERE_TOP_RADIUS 6471000.0
/** @brief The air's (Rayleigh) scale height, in metres (ATMOSPHERE_RAYLEIGH_HEIGHT). */
#define WORLD_ATMOSPHERE_RAYLEIGH_HEIGHT 8000.0
/** @brief The haze's extinction per metre at sea level at turbidity 3: 1.1 times 21e-6, so the default HazeAlbedo
 *  (about 1 / 1.1) scatters about 21e-6 of it. */
#define WORLD_ATMOSPHERE_MIE_EXTINCTION 23.1e-6
/** @brief The ozone layer, a tent: densest at this height, gone this far above and below it (metres;
 *  ATMOSPHERE_OZONE_CENTER, ATMOSPHERE_OZONE_HALF_WIDTH). */
#define WORLD_ATMOSPHERE_OZONE_CENTER 25000.0
#define WORLD_ATMOSPHERE_OZONE_HALF_WIDTH 15000.0
/** @brief The transmittance table's size in texels: zenith angles across, heights up (ATMOSPHERE_TRANSMITTANCE_*). */
#define WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH 256
#define WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT 64
/** @brief The multiple scattering table's size in texels, both ways: the light's zenith angle across, heights up
 *  (ATMOSPHERE_MULTISCATTER_SIZE). */
#define WORLD_ATMOSPHERE_MULTISCATTER_SIZE 32
/** @brief One half of the sky-view table, in texels: azimuths across, zenith angles up (ATMOSPHERE_SKY_VIEW_*); the
 *  texture holds WORLD_ATMOSPHERE_SKY_VIEW_HALF_COUNT halves one above the other. */
#define WORLD_ATMOSPHERE_SKY_VIEW_WIDTH 192
#define WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT 108
#define WORLD_ATMOSPHERE_SKY_VIEW_HALF_COUNT 2
/** @brief The heights above sea level the sky is seen from, in metres: at least 1 m (the camera below sea level, in a
 *  valley under world Y 0, sees the sky from just above it), at most 1 km below the top of the atmosphere (the tables'
 *  rays start inside the air). */
#define WORLD_ATMOSPHERE_MIN_VIEW_HEIGHT 1.0
#define WORLD_ATMOSPHERE_MAX_VIEW_HEIGHT 99000.0
/** @brief Below this product of the horizontal lengths of a direction and a light, one of them is vertical and their
 *  azimuth undefined (WorldAtmosphere_GetCosLightAzimuth; ATMOSPHERE_VERTICAL_EPSILON). */
#define WORLD_ATMOSPHERE_VERTICAL_EPSILON 1e-9
/** @brief The angular diameter of the sun's disc (and the moon's), radians: the planet's shadow on the air is soft over
 *  it, a smooth step of the light's zenith cosine from half of it below the horizon's to half of it above
 *  (ATMOSPHERE_LIGHT_DIAMETER). */
#define WORLD_ATMOSPHERE_LIGHT_DIAMETER 0.0093
/** @brief The aerial-perspective volume: its screen directions across and up, and its slices (distances along
 *  each direction; ATMOSPHERE_VOLUME_SIZE, _SLICE_COUNT). */
#define WORLD_ATMOSPHERE_VOLUME_SIZE 32
#define WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT 32
/** @brief The first slice's distance and the last distance slice's, in metres (ATMOSPHERE_VOLUME_NEAREST, _FARTHEST): the
 *  slices between grow by a fixed ratio (1.50), so 21 of them lie within 4 km. */
#define WORLD_ATMOSPHERE_VOLUME_NEAREST 1.0
#define WORLD_ATMOSPHERE_VOLUME_FARTHEST 200000.0
/** @brief The last distance slice (WORLD_ATMOSPHERE_VOLUME_FARTHEST); the slice after it holds the whole ray. */
#define WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE (WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT - 2)
/** @brief The volume's texture in texels: the slices side by side, two rows of tiles (the in-scatter, the transmittance)
 *  and one row of the frame's values (ATMOSPHERE_VOLUME_WIDTH, _HEIGHT). */
#define WORLD_ATMOSPHERE_VOLUME_WIDTH (WORLD_ATMOSPHERE_VOLUME_SIZE * WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT)
#define WORLD_ATMOSPHERE_VOLUME_HEIGHT (2 * WORLD_ATMOSPHERE_VOLUME_SIZE + 1)
/** @brief The steps each slice's march takes (growing with the square of their index): within this distance, beyond
 *  it, and for the whole ray (the sky view's: ATMOSPHERE_VOLUME_NEAR_DISTANCE, _NEAR_STEPS, _FAR_STEPS, _RAY_STEPS).
 *  Measured against 512 steps: within 4 km the near steps are 0.13 % off at worst, beyond it the
 *  far steps 1.2 % (of the light of the whole ray). */
#define WORLD_ATMOSPHERE_VOLUME_NEAR_DISTANCE 4096.0
#define WORLD_ATMOSPHERE_VOLUME_NEAR_STEPS 8
#define WORLD_ATMOSPHERE_VOLUME_FAR_STEPS 24
#define WORLD_ATMOSPHERE_VOLUME_RAY_STEPS 32
/** @brief Light pollution's light in the air: the town's light fills the air below about this height and within
 *  about this horizontal distance of the camera, in metres (scale lengths of its exponential falloffs:
 *  ATMOSPHERE_LIGHT_POLLUTION_HEIGHT, _REACH). Chosen so the glow's shape over the sky is the tuned skyglow profile's
 *  (common/night_sky's): at the default air the zenith gets 5.5 % of the horizon's glow (the profile: 6 %). */
#define WORLD_ATMOSPHERE_LIGHT_POLLUTION_HEIGHT 500.0
#define WORLD_ATMOSPHERE_LIGHT_POLLUTION_REACH 10000.0
/** @brief The radiance of light pollution's light in the air at the camera's foot, at LightPollution 1 at night, in the
 *  sky's light units (times the linear LightPollutionColor): the glow it gives at the horizon in the default air is
 *  the tuned skyglow's there (0.012, WorldRendererSky.c's SKYGLOW_BASE_RADIANCE). */
#define WORLD_ATMOSPHERE_LIGHT_POLLUTION_RADIANCE 0.049
/** @brief How far a ray of the sky goes through the ground haze, in metres: past every mist, and finite, so no infinity
 *  meets a zero (ATMOSPHERE_SKY_DISTANCE). */
#define WORLD_ATMOSPHERE_SKY_DISTANCE 1.0e8
/** @brief The largest haze distance scale (WorldAtmosphereHaze.DistanceScale): any more and every surface is the sky
 *  already; the bound keeps hostile multipliers finite. */
#define WORLD_ATMOSPHERE_MAX_HAZE_SCALE 1.0e9
/** @brief The sun's light above the air on a surface facing it, at sun_intensity 1 and a white sun_color, in the game's
 *  light units (one scale for the sky and the surfaces; ATMOSPHERE_SUN_LIGHT). Chosen so that the sun 60 degrees up
 *  lights the ground at sea level in the default air with a luminance of 0.911, that of (255, 244, 214) linear, the
 *  sunlight the lamps and the surfaces' look are tuned against: the transmittance toward it is (0.910, 0.831, 0.786),
 *  luminance 0.845, and 1.078 times that is 0.911. */
#define WORLD_ATMOSPHERE_SUN_LIGHT 1.078
/** @brief What WORLD_ATMOSPHERE_SUN_LIGHT stands for on the real scale: the luminance of a white surface facing the
 *  sun above the air, the solar illuminance (128 klux) over pi, in cd/m^2. So one of the game's light units is about
 *  37800 cd/m^2 of radiance: the sky's luminance on the real scale (the stars' background). Night vision and the eye
 *  read the game's light at the eye's adaptation level instead (NightVision.h: a clear noon's scene luminance for 5000
 *  cd/m^2, the light of a sunlit view, not of a white surface). */
#define WORLD_ATMOSPHERE_SUN_LIGHT_CANDELAS 40743.665431525208
/** @brief The sun's disc's radiance per unit of the sky pass's sunDiscIntensity (the C twin of sky/fragment.frag's
 *  SUN_DISC_RADIANCE): a constant of its own, which the disc's look is tuned with (the renderer scales the intensity
 *  by the exposure). */
#define WORLD_ATMOSPHERE_SUN_DISC_RADIANCE 22.0
/** @brief The sky's irradiance table: the light the sky sends a surface facing up, per unit of the light above the
 *  air, by the light's elevation across (WORLD_ATMOSPHERE_IRRADIANCE_WIDTH columns from
 *  WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION to the zenith, even in asinh(elevation / _ELEVATION_SCALE): about 0.4
 *  degrees apart at the horizon, where the twilight's light falls fastest, 9 at the zenith) and the height up
 *  (WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT rows from the ground to the top of the atmosphere, even in log(1 + height /
 *  _HEIGHT_SCALE): even in the height near the ground, where light falls off exponentially with it, about 160 m apart
 *  there), then one more column of light pollution's irradiance per unit of its radiance by height
 *  (ATMOSPHERE_IRRADIANCE_*). Each texel holds the natural logarithm of its value (at least
 *  WORLD_ATMOSPHERE_IRRADIANCE_LOG_FLOOR), so the bilinear read follows light falling off exponentially. */
#define WORLD_ATMOSPHERE_IRRADIANCE_WIDTH 64
#define WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT 32
#define WORLD_ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH (WORLD_ATMOSPHERE_IRRADIANCE_WIDTH + 1)
/** @brief The irradiance table's mappings' scales: the elevation in radians (about 4 degrees) and the height in metres
 *  over which their spacing stays even (ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE, _HEIGHT_SCALE). */
#define WORLD_ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE 0.07
#define WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE 1000.0
/** @brief The lowest light elevation the irradiance table holds, in radians (-20 degrees): from there down the sky
 *  sends no light of the sun's or the moon's that shows. */
#define WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION (-0.34906585039886590)
/** @brief The logarithm the irradiance table stores for no light (e^-46: about 1e-20). */
#define WORLD_ATMOSPHERE_IRRADIANCE_LOG_FLOOR (-46.0)
/** @brief The sky's light by height in the volume's last row: WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT heights from
 *  the ground to WORLD_ATMOSPHERE_SKY_LIGHT_HIGHEST metres, even in the height's square root (5 m, 20 m, 45 m, ... apart
 *  from the ground: the sun's light changes fastest there at sunset), for each value (WorldAtmosphereSkyLight) a block of
 *  that many columns from WORLD_ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN on (ATMOSPHERE_SKY_LIGHT_*). */
#define WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT 64
#define WORLD_ATMOSPHERE_SKY_LIGHT_HIGHEST 20000.0
#define WORLD_ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN 64


// Types.
/** @brief The halves of the sky-view table: the sky lit by the sun (built every frame) and by the moon (built while the
 *  moonlit sky shows). The values are the shaders' half indices. */
typedef enum WorldAtmosphereSkyViewHalfEnum
{
    /** @brief The sun's: its glow with light pollution's skyglow, in the texture's lower rows (0 to 107). */
    WorldAtmosphereSkyViewHalf_Sun = 0,
    /** @brief The moon's: the moonlit sky, in the upper rows (108 to 215). */
    WorldAtmosphereSkyViewHalf_Moon = 1,
} WorldAtmosphereSkyViewHalf;

/**
 * @brief The air's coefficients for one environment (WorldAtmosphere_GetMedium), as the atmosphere's shaders take them.
 *
 * Linear values, per metre at sea level for the coefficients (red, green, blue). A plain value type.
 */
typedef struct WorldAtmosphereMediumStruct
{
    /** @brief The air's Rayleigh scattering (its extinction too: the air absorbs nothing), Earth's x AirDensity. */
    float RayleighScattering[3];
    /** @brief The haze's scattering: its extinction x HazeAlbedo (linear). */
    float MieScattering[3];
    /** @brief The haze's extinction: WORLD_ATMOSPHERE_MIE_EXTINCTION x SkyTurbidity / 3, the same for every colour. */
    float MieExtinction[3];
    /** @brief The haze's scale height, in metres (HazeHeight). */
    float MieHeight;
    /** @brief The haze's forward scattering, the Mie phase function's g (SunGlow). */
    float MieAnisotropy;
    /** @brief The ozone layer's absorption at its densest, Earth's x Ozone. */
    float OzoneAbsorption[3];
    /** @brief The share of the multiple scattering drawn, 0..1 (MultipleScattering). */
    float MultipleScattering;
    /** @brief The ground's albedo (SkyGroundAlbedo, linear), which feeds the multiple scattering. */
    float GroundAlbedo[3];
} WorldAtmosphereMedium;

/** @brief A ray in the atmosphere as the tables index it: where it starts and how steeply it climbs. A plain value. */
typedef struct WorldAtmosphereRayStruct
{
    /** @brief The height above sea level it starts at, in metres. */
    double Height;
    /** @brief The cosine of its angle from the zenith: 1 straight up, 0 level, -1 straight down. */
    double CosZenith;
} WorldAtmosphereRay;

/** @brief A direction in the sky-view table: its zenith angle's cosine and its azimuth's cosine from the light's (1
 *  toward the light, -1 away from it). A plain value. */
typedef struct WorldAtmosphereViewStruct
{
    double CosZenith;
    double CosLightAzimuth;
} WorldAtmosphereView;

/** @brief The parts of the aerial-perspective volume's texture: its rows of tiles. The values are the shaders' part
 *  indices. */
typedef enum WorldAtmosphereVolumePartEnum
{
    /** @brief The light the air scattered toward the camera along the path so far (RGB, the sky's light units). */
    WorldAtmosphereVolumePart_InScatter = 0,
    /** @brief The path's transmittance (RGB). */
    WorldAtmosphereVolumePart_Transmittance = 1,
} WorldAtmosphereVolumePart;

/** @brief The frame's values in the volume's last row, one texel each (the values are their columns): the light the
 *  ground haze scatters, per unit of its scattering, at its base. */
typedef enum WorldAtmosphereVolumeValueEnum
{
    /** @brief The sun's light reaching the mist's base (through the air, in the planet's shadow once the sun has set
     *  there), to be multiplied by the mist's phase function toward the sun. */
    WorldAtmosphereVolumeValue_GroundHazeSun = 0,
    /** @brief The moon's light there (the moonlit sky's strength), to be multiplied by the phase toward the moon. */
    WorldAtmosphereVolumeValue_GroundHazeMoon = 1,
    /** @brief The light reaching it from all around, scattered evenly: the sky's (its multiple scattering) and light
     *  pollution's. */
    WorldAtmosphereVolumeValue_GroundHazeAmbient = 2,
    /** @brief The number of values; not a value. */
    WorldAtmosphereVolumeValue_Count
} WorldAtmosphereVolumeValue;

/** @brief The sky's light by height in the volume's last row, one block of columns each (the values are the
 *  shaders' block indices): what lights a surface at that height, in the game's light units. */
typedef enum WorldAtmosphereSkyLightEnum
{
    /** @brief The sun's light through the air on a surface facing it (the light above the air times the transmittance,
     *  with its disc's share above the planet's horizon). */
    WorldAtmosphereSkyLight_Sun = 0,
    /** @brief The moon's, the same way. */
    WorldAtmosphereSkyLight_Moon = 1,
    /** @brief The sky's irradiance on a surface facing up: the sun's, the moon's and light pollution's light scattered
     *  by the air, tinted by sky_tint. */
    WorldAtmosphereSkyLight_Sky = 2,
    /** @brief The ground's bounce: the irradiance on a surface facing down, the ground below it (sky_ground_albedo) lit
     *  by the sun, the moon and the sky. */
    WorldAtmosphereSkyLight_Ground = 3,
    /** @brief The number of values; not a value. */
    WorldAtmosphereSkyLight_Count
} WorldAtmosphereSkyLight;

/**
 * @brief The haze of a frame (WorldAtmosphere_GetHaze), as every reader of the aerial perspective and the ground haze
 *        takes it. Linear colours. A plain value type.
 */
typedef struct WorldAtmosphereHazeStruct
{
    /** @brief Every haze distance is multiplied by it (the environment's AerialDistanceScale and FogStrength and the
     *  player's fog strength): 1 the real air, 0 none on surfaces. In [0, WORLD_ATMOSPHERE_MAX_HAZE_SCALE]. */
    float DistanceScale;
    /** @brief No haze nearer than this, in metres (>= 0). */
    float StartDistance;
    /** @brief The ground haze's density per metre at its base (>= 0; 0: none), its base (world Y, metres) and how
     *  quickly it thins above it (metres, > 0). */
    float GroundHazeDensity;
    float GroundHazeBase;
    float GroundHazeThickness;
    /** @brief The share of the light it takes that the ground haze scatters (linear), and its forward scattering (the
     *  phase function's g, [0, 0.99]). */
    float GroundHazeAlbedo[3];
    float GroundHazeGlow;
    /** @brief The atmosphere's ground: the world Y the far ground lies at and the sky's heights are measured from. */
    float GroundHeight;
} WorldAtmosphereHaze;

/**
 * @brief What the transmittance and multiple scattering tables were last built for (the renderer keeps one; all zero:
 *        never built). WorldAtmosphere_UpdateTableRecord decides each frame whether they are built again. A plain value.
 */
typedef struct WorldAtmosphereTableRecordStruct
{
    /** @brief The air the tables hold, valid while IsBuilt. */
    WorldAtmosphereMedium BuiltFor;
    /** @brief Whether the tables hold BuiltFor's air; cleared when their targets are dropped. */
    bool IsBuilt;
    /** @brief How many times they were built (the renderer's status line). */
    uint32_t BuildCount;
} WorldAtmosphereTableRecord;


// Functions.
/**
 * @brief The air's coefficients for an environment's settings (SkyTurbidity, HazeHeight, HazeAlbedo, SunGlow,
 *        AirDensity, Ozone, MultipleScattering, SkyGroundAlbedo).
 *
 * A value outside its table rule (which validated environments never hold) is clamped to it, and a non-finite one is
 * taken as its default, so every coefficient is finite and at least 0. SkyTurbidity's rule has no maximum: the shaders
 * take any finite haze (a step too thick to cross gives no light, never a NaN).
 * @param environment The environment; must not be NULL.
 * @param outMedium [out] Receives the coefficients.
 */
void WorldAtmosphere_GetMedium(const WorldEnvironment* environment, WorldAtmosphereMedium* outMedium);

/**
 * @brief Whether the transmittance and multiple scattering tables built for one medium are out of date for another:
 *        whether anything they depend on differs (every coefficient, the haze's height and the ground's albedo; not the
 *        haze's forward scattering nor the share of multiple scattering, which only the sky-view table uses).
 * @param builtFor The medium the tables were built for.
 * @param medium The medium now.
 * @returns true when they must be built again.
 */
bool WorldAtmosphere_AreTablesStale(const WorldAtmosphereMedium* builtFor, const WorldAtmosphereMedium* medium);

/**
 * @brief Whether the transmittance and multiple scattering tables must be built for @p medium this frame: when they
 *        were never built, or were built for air they no longer match (WorldAtmosphere_AreTablesStale). When they must,
 *        records them as built for it and counts the build, so the caller builds them at once.
 * @param record The tables' record; must not be NULL.
 * @param medium The frame's air; must not be NULL.
 * @returns true when the caller must build the tables now.
 */
bool WorldAtmosphere_UpdateTableRecord(WorldAtmosphereTableRecord* record, const WorldAtmosphereMedium* medium);

/**
 * @brief Whether the irradiance table built for one medium is out of date for another: when the transmittance and
 *        multiple scattering tables it reads are (WorldAtmosphere_AreTablesStale), or the haze's forward scattering or
 *        the share of multiple scattering differs (its single scattering's phase and its glow's share).
 */
bool WorldAtmosphere_IsIrradianceStale(const WorldAtmosphereMedium* builtFor, const WorldAtmosphereMedium* medium);

/**
 * @brief Whether the irradiance table must be built for @p medium this frame (never built, or
 *        WorldAtmosphere_IsIrradianceStale); when it must, records it as built for it and counts the build. Call after
 *        the transmittance and multiple scattering tables are up to date.
 * @param record The irradiance table's record; must not be NULL.
 * @param medium The frame's air; must not be NULL.
 * @returns true when the caller must build it now.
 */
bool WorldAtmosphere_UpdateIrradianceRecord(WorldAtmosphereTableRecord* record, const WorldAtmosphereMedium* medium);

/**
 * @brief The height above sea level the sky is seen from, for a camera at @p cameraHeight (world Y, metres): clamped to
 *        [WORLD_ATMOSPHERE_MIN_VIEW_HEIGHT, WORLD_ATMOSPHERE_MAX_VIEW_HEIGHT]; a non-finite height gives the lowest.
 */
double WorldAtmosphere_GetViewHeight(double cameraHeight);

/**
 * @brief The height above the ground of the point @p distance metres along a ray, without the cancellation of a radius
 *        minus the planet's (precise to a millimetre near the ground).
 * @param ray Where the ray starts and how it climbs.
 * @param distance How far along it, in metres (>= 0).
 * @returns The height, in metres; negative inside the planet.
 */
double WorldAtmosphere_GetHeightAt(WorldAtmosphereRay ray, double distance);

/**
 * @brief The zenith cosine of the horizon seen from @p height (metres): 0 on the ground, below 0 higher up (the ground
 *        falls away); rays below it meet the ground.
 */
double WorldAtmosphere_GetHorizonCos(double height);

/** @brief The distance along a ray to the top of the atmosphere, in metres (0 for a ray starting above it and going up
 *  no further). */
double WorldAtmosphere_GetDistanceToTop(WorldAtmosphereRay ray);

/**
 * @brief The distance along a ray to the ground, in metres, or -1 when it misses the ground (it climbs, or passes above
 *        the horizon).
 */
double WorldAtmosphere_GetDistanceToGround(WorldAtmosphereRay ray);

/** @brief A coordinate in [0, 1] across a table of @p size texels, from the first texel's centre (0) to the last's (1),
 *  as a texture coordinate: 0.5 / size + unit x (size - 1) / size. */
double WorldAtmosphere_UnitToTexel(double unit, int size);

/** @brief The inverse of WorldAtmosphere_UnitToTexel. */
double WorldAtmosphere_TexelToUnit(double coordinate, int size);

/**
 * @brief Where the transmittance table holds a ray (the shaders' atmosphereTransmittanceUv): texture coordinates (X
 *        across, Y up). A ray below the horizon gets the horizon's column.
 */
Vector2Double WorldAtmosphere_GetTransmittanceUv(WorldAtmosphereRay ray);

/** @brief The ray a point of the transmittance table holds (the inverse of WorldAtmosphere_GetTransmittanceUv; the
 *  shaders' atmosphereTransmittanceRay). */
WorldAtmosphereRay WorldAtmosphere_GetTransmittanceRay(Vector2Double uv);

/**
 * @brief Where a transmittance table of any size holds a ray, in unit coordinates ([0, 1] both ways: across the distance
 *        to the top, from straight up to the horizon; up the distance to the planet's axis) before
 *        WorldAtmosphere_UnitToTexel. WorldAtmosphere_GetTransmittanceUv is this for the GPU's table; the C mirror's own
 *        smaller one (WorldSkyLight.h) maps rays the same way. A ray below the horizon gets the horizon's.
 */
Vector2Double WorldAtmosphere_GetTransmittanceUnit(WorldAtmosphereRay ray);

/** @brief The ray a unit point of a transmittance table holds (the inverse of WorldAtmosphere_GetTransmittanceUnit). */
WorldAtmosphereRay WorldAtmosphere_GetTransmittanceUnitRay(Vector2Double unit);

/**
 * @brief Where the multiple scattering table holds a point at a height with the light at a zenith angle (the shaders'
 *        atmosphereMultiscatterUv): the light's zenith cosine across (-1 to 1), the height up (0 to the top).
 * @param ray The point's height, and the zenith cosine of the direction to the light.
 */
Vector2Double WorldAtmosphere_GetMultiscatterUv(WorldAtmosphereRay ray);

/** @brief The inverse of WorldAtmosphere_GetMultiscatterUv. */
WorldAtmosphereRay WorldAtmosphere_GetMultiscatterRay(Vector2Double uv);

/**
 * @brief Where one half of the sky-view table holds a direction seen from @p viewHeight: unit coordinates in [0, 1]
 *        (X the azimuth from the light, Y from the zenith, 0.5 at the horizon, to the nadir), before
 *        WorldAtmosphere_UnitToTexel.
 * @param viewHeight The height the sky is seen from (WorldAtmosphere_GetViewHeight), in metres.
 * @param view The direction.
 */
Vector2Double WorldAtmosphere_GetSkyViewUnit(double viewHeight, WorldAtmosphereView view);

/** @brief The direction a point of one half of the sky-view table holds, seen from @p viewHeight (the inverse of
 *  WorldAtmosphere_GetSkyViewUnit). */
WorldAtmosphereView WorldAtmosphere_GetSkyViewDirection(double viewHeight, Vector2Double unit);

/**
 * @brief The cosine of the azimuth between a direction and a light, around world up (+Y): 1 when they share a vertical
 *        plane on the same side, -1 opposite. 1 when either is vertical (the sky is then the same all around).
 */
double WorldAtmosphere_GetCosLightAzimuth(Vector3Double direction, Vector3Double lightDirection);

/**
 * @brief Where the sky-view texture holds a world direction for one light: texture coordinates in the whole texture
 *        (both halves), as the shaders sample it (atmosphereSkyView).
 * @param viewHeight The height the sky is seen from, in metres.
 * @param direction The direction (unit, world space, +Y up).
 * @param lightDirection The direction to the half's light (unit).
 * @param half The half: the sun's or the moon's.
 */
Vector2Double WorldAtmosphere_GetSkyViewUv(double viewHeight, Vector3Double direction, Vector3Double lightDirection,
    WorldAtmosphereSkyViewHalf half);

/**
 * @brief The frame's haze from an environment's settings (AerialDistanceScale, FogStrength, HazeStartDistance, the ground
 *        haze's five, HorizonGroundHeight) and the player's fog strength (GameConfig.FogStrength).
 *
 * A value outside its rule (which validated environments never hold) is clamped to it, and a non-finite one is taken as
 * its default; the distance scale's product is clamped to WORLD_ATMOSPHERE_MAX_HAZE_SCALE, so every value is finite.
 * @param environment The environment; must not be NULL.
 * @param configFogStrength The player's fog strength (1 when there is no config).
 * @param outHaze [out] Receives the haze.
 */
void WorldAtmosphere_GetHaze(const WorldEnvironment* environment, float configFogStrength, WorldAtmosphereHaze* outHaze);

/**
 * @brief The distance of one of the volume's distance slices, in metres: WORLD_ATMOSPHERE_VOLUME_NEAREST x (FARTHEST /
 *        NEAREST)^(slice / WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE).
 * @param slice The slice, 0 to WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE (clamped).
 * @returns The distance.
 */
double WorldAtmosphere_GetVolumeSliceDistance(int slice);

/**
 * @brief Where along the volume's slices a distance lies (the shaders' atmosphereVolumeSlice): -1 at the camera, rising
 *        linearly to 0 at the first slice; between two distance slices their index plus the share of the way from one
 *        to the next (linear in distance, so the shaders' blend of the two is linear in distance); past the last
 *        distance slice, linearly on toward the whole ray's slice, reached at the top of the atmosphere.
 * @param distance The distance along the ray, in metres (a non-positive one is the camera's).
 * @param distanceToTop The ray's distance to the top of the atmosphere (WorldAtmosphere_GetDistanceToTop, without the
 *        ground: the volume's rays pass through it).
 * @returns The slice coordinate, in [-1, WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT - 1].
 */
double WorldAtmosphere_GetVolumeSlice(double distance, double distanceToTop);

/** @brief The inverse of WorldAtmosphere_GetVolumeSlice: the distance at a slice coordinate (0 at -1 and below). */
double WorldAtmosphere_GetVolumeDistance(double slice, double distanceToTop);

/**
 * @brief Where the volume's texture holds a screen direction's value at one slice (the shaders' atmosphereVolumeUv):
 *        texture coordinates, kept half a texel inside the slice's tile so bilinear sampling never reads its
 *        neighbour.
 * @param screenUv The direction's place on the screen, [0, 1] both ways (0 at the scene's bottom left), as the camera's
 *        view-projection puts it.
 * @param slice The slice, 0 to WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT - 1.
 * @param part The in-scatter's or the transmittance's tile.
 */
Vector2Double WorldAtmosphere_GetVolumeUv(Vector2Double screenUv, int slice, WorldAtmosphereVolumePart part);

/** @brief The texture coordinates of one of the frame's values in the volume's last row (its texel's centre). */
Vector2Double WorldAtmosphere_GetVolumeValueUv(WorldAtmosphereVolumeValue value);

/**
 * @brief Where the sky's light by height holds a height (the shaders' atmosphereSkyLightCoordinate): the height's
 *        place among the WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT heights, (count - 1) x sqrt(height /
 *        WORLD_ATMOSPHERE_SKY_LIGHT_HIGHEST), clamped: below the ground counts as on it, above the highest as there.
 *        Between two whole coordinates the readers blend the two heights linearly.
 * @param height The height above the atmosphere's ground, in metres (NaN counts as 0).
 * @returns The coordinate, in [0, count - 1].
 */
double WorldAtmosphere_GetSkyLightCoordinate(double height);

/** @brief The height a whole coordinate of the sky's light by height stands for, in metres (the inverse of
 *  WorldAtmosphere_GetSkyLightCoordinate; the index is clamped to the heights). */
double WorldAtmosphere_GetSkyLightHeight(int index);

/** @brief The column of the volume's last row that holds one value of the sky's light at one of its heights: the
 *  value's block from WORLD_ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN on, the height's index within it (clamped to the heights). */
int WorldAtmosphere_GetSkyLightColumn(WorldAtmosphereSkyLight value, int index);

/**
 * @brief Where the irradiance table holds a light's zenith cosine and a height, in unit coordinates before
 *        WorldAtmosphere_UnitToTexel: across, the light's elevation from WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION to
 *        the zenith, even in asinh(elevation / WORLD_ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE) (lower lights get the
 *        lowest); up, log(1 + height / WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE) over the atmosphere's (clamped).
 * @param ray The height above the ground (metres) and the zenith cosine toward the light.
 */
Vector2Double WorldAtmosphere_GetIrradianceUnit(WorldAtmosphereRay ray);

/** @brief The height and the light's zenith cosine a unit point of the irradiance table holds (the inverse of
 *  WorldAtmosphere_GetIrradianceUnit). */
WorldAtmosphereRay WorldAtmosphere_GetIrradianceRay(Vector2Double unit);

/** @brief Where the irradiance texture holds the light's irradiance for a ray (the shaders' atmosphereIrradianceUv):
 *  texture coordinates in its first WORLD_ATMOSPHERE_IRRADIANCE_WIDTH columns. */
Vector2Double WorldAtmosphere_GetIrradianceUv(WorldAtmosphereRay ray);

/** @brief Where the irradiance texture holds light pollution's irradiance at a height (the shaders'
 *  atmosphereLightPollutionIrradianceUv): texture coordinates in its last column. */
Vector2Double WorldAtmosphere_GetLightPollutionIrradianceUv(double height);

/**
 * @brief The ground haze's optical depth along part of a ray (the shaders' hazeGroundDepth): the integral of
 *        density x exp(-max(y, 0) / thickness) over the ray from @p from to @p to metres along it, y the height above the
 *        haze's base, in closed form.
 *
 * Exact for any slope, level rays included (computed as the mean density over the heights crossed, without cancellation).
 * @param density The density at the base and below it, per metre (<= 0: none).
 * @param thickness How quickly it thins above the base, in metres (> 0).
 * @param heightAboveBase The ray's start's height above the base, in metres.
 * @param rayY How much the ray climbs per metre along it (its unit direction's Y; negative going down).
 * @param from The distance along the ray the part starts at, in metres.
 * @param to The distance it ends at (no depth unless above @p from).
 * @returns The optical depth (>= 0).
 */
double WorldAtmosphere_GetGroundHazeDepth(double density, double thickness, double heightAboveBase, double rayY,
    double from, double to);

/**
 * @brief Where along a ray the ground haze's optical depth from @p from reaches @p depth (the shaders'
 *        hazeGroundDistance): the inverse of WorldAtmosphere_GetGroundHazeDepth in its end, in closed form (the ray split
 *        where it crosses the base; linear in depth where the density is even, below the base or along a nearly level
 *        ray).
 *
 * The shaders read the air's haze there once more: where half of the mist's light comes from, so the air in front of
 * a far mist hazes it.
 * @param density The density at the base and below it, per metre (<= 0: none).
 * @param thickness How quickly it thins above the base, in metres (> 0).
 * @param heightAboveBase The ray's start's height above the base, in metres.
 * @param rayY How much the ray climbs per metre along it.
 * @param from The distance along the ray the depth is counted from, in metres.
 * @param to The farthest distance returned (where the depth is counted to at most).
 * @param depth The optical depth sought (<= 0: @p from).
 * @returns The distance, in [@p from, @p to]: @p to when the depth from @p from to it is no more than @p depth.
 */
double WorldAtmosphere_GetGroundHazeDistance(double density, double thickness, double heightAboveBase, double rayY,
    double from, double to, double depth);

/**
 * @brief The ground haze's optical depth along part of a ray over the curved planet (the shaders' hazeGroundDepthCurved),
 *        for the sky pass's rays that pass over the horizon: beyond their lowest point they climb out of the haze again,
 *        where the flat world's straight ray (WorldAtmosphere_GetGroundHazeDepth) would sink into it.
 *
 * The ray's height is taken as a parabola around its lowest point (its true height and curvature there): exact to a
 * millimetre within 100 km of that point. The stretch below the base counts at the base's density, the rest is a
 * Gaussian in the distance from the lowest point, in closed form (WorldAtmosphere_GetScaledErfc).
 * @param density The density at the base and below it, per metre (<= 0: none).
 * @param thickness How quickly it thins above the base, in metres (> 0).
 * @param heightAboveBase The ray's start's height above the base, in metres.
 * @param rayY The cosine of the ray's zenith angle at its start.
 * @param radius The start's distance from the planet's centre, in metres (> 0): the planet's radius plus the view height.
 * @param from The distance along the ray the part starts at, in metres.
 * @param to The distance it ends at (no depth unless above @p from).
 * @returns The optical depth (>= 0).
 */
double WorldAtmosphere_GetGroundHazeDepthCurved(double density, double thickness, double heightAboveBase, double rayY,
    double radius, double from, double to);

/** @brief e^(x^2) erfc(x) for x >= 0 (the shaders' hazeErfcx: Numerical Recipes' erfcc without its e^(-x^2), a fractional
 *  error below 1.2e-7 everywhere). */
double WorldAtmosphere_GetScaledErfc(double x);

/**
 * @brief The zenith cosine of a light at the point @p distance along a ray (the shaders' march: that point's own zenith,
 *        on the curved planet): how high the sun stands over a far point of the ground, 0 where it is setting there.
 * @param ray Where the ray starts and how it climbs.
 * @param distance How far along it, in metres.
 * @param lightCosZenith The light's zenith cosine at the ray's start.
 * @param cosLightAngle The cosine of the angle between the ray and the light.
 * @returns The cosine, clamped to [-1, 1].
 */
double WorldAtmosphere_GetLightCosAt(WorldAtmosphereRay ray, double distance, double lightCosZenith,
    double cosLightAngle);

/**
 * @brief How much of light pollution's light (the town's, scattered by the air) the air holds at a point:
 *        exp(-height / WORLD_ATMOSPHERE_LIGHT_POLLUTION_HEIGHT) x exp(-horizontalDistance /
 *        WORLD_ATMOSPHERE_LIGHT_POLLUTION_REACH), 1 at the camera's foot.
 * @param height The point's height above the ground, in metres (below it counts as on it).
 * @param horizontalDistance Its horizontal distance from the camera, in metres.
 */
double WorldAtmosphere_GetLightPollutionShare(double height, double horizontalDistance);

/**
 * @brief Light pollution's light in the air at the camera's foot for an environment (LightPollution, clamped to [0, 1],
 *        times its linear LightPollutionColor, times WORLD_ATMOSPHERE_LIGHT_POLLUTION_RADIANCE), faded out by daylight:
 *        what the atmosphere's tables and the aerial-perspective volume are lit by besides the sun and the moon.
 * @param environment The environment; must not be NULL.
 * @param daylight How much daylight there is, 1 day .. 0 night (WorldEnvironment_GetDaylightFactor; clamped).
 * @param outRadiance [out] Receives the radiance (linear RGB, >= 0).
 */
void WorldAtmosphere_GetLightPollutionRadiance(const WorldEnvironment* environment, float daylight,
    float outRadiance[3]);
