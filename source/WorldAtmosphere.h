#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "WorldEnvironment.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): the atmosphere as the world renderer models it (T15), pure maths tested headless
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
 * The defaults are the sky of T34 (single scattering, the same coefficients) plus the ozone and multiple scattering.
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
/** @brief The haze's extinction per metre at sea level at turbidity 3: 1.1 times its scattering of 21e-6 in the sky before
 *  T15 (which held its albedo at 1 / 1.1). */
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
