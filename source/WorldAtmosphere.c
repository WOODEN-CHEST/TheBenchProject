// The atmosphere's model as pure maths (T15): the air's coefficients, the height the sky is seen from, and the lookup
// tables' layout, the C mirror of shaders/common/atmosphere.glsl (WorldAtmosphere.h).

#include <math.h>
#include "WorldAtmosphere.h"


// Macros.
/** Pi in double. */
#define ATMOSPHERE_PI 3.14159265358979323846
/** sRGB gamma, for the colours the environment stores (the renderer's own conversion). */
#define ATMOSPHERE_SRGB_GAMMA 2.2f
/** The default turbidity, haze height and haze forward scattering, for a non-finite setting (WorldEnvironment.c's). */
#define ATMOSPHERE_FALLBACK_TURBIDITY 24.0f
#define ATMOSPHERE_FALLBACK_HAZE_HEIGHT 1200.0f
#define ATMOSPHERE_FALLBACK_SUN_GLOW 0.758f
/** The turbidity the haze's extinction is given for. */
#define ATMOSPHERE_REFERENCE_TURBIDITY 3.0f
/** The defaults of the ground haze's thickness and glow, for a non-finite setting (WorldEnvironment.c's). */
#define ATMOSPHERE_FALLBACK_GROUND_HAZE_THICKNESS 20.0f
#define ATMOSPHERE_FALLBACK_GROUND_HAZE_GLOW 0.8f
/** Below this optical depth a step's share of its own light is taken from its series ((1 - e^-x) / x = 1 - x / 2):
 *  atmosphereStepShare's threshold. */
#define ATMOSPHERE_SHARE_SERIES_LIMIT 1e-3
/** Below this (the climb across a stretch of the ray, in ground haze thicknesses) the ground haze's density counts as
 *  even along the stretch when its depth is inverted (common/haze's HAZE_SLOPE_LIMIT). */
#define ATMOSPHERE_GROUND_HAZE_SLOPE_LIMIT 1e-3


// Fields.
/* Earth's air at sea level: its Rayleigh scattering per metre (red, green, blue), the sky's coefficients before T15. */
static const float RayleighScattering[3] = { 5.5e-6f, 13.0e-6f, 22.4e-6f };
/* The ozone layer's absorption per metre at its densest (Bruneton 2017, Hillaire 2020: its Chappuis band, mostly orange
 * and red). */
static const float OzoneAbsorption[3] = { 0.650e-6f, 1.881e-6f, 0.085e-6f };


// Static functions.
/* A value in [minimum, maximum], or the fallback when it is not finite. */
static float ClampFinite(float value, float minimum, float maximum, float fallback)
{
    if (!isfinite(value))
    {
        return fallback;
    }
    return fminf(fmaxf(value, minimum), maximum);
}

/* An sRGB colour's linear red, green and blue (alpha ignored). */
static void ColorToLinear(Color color, float outRgb[3])
{
    outRgb[0] = powf((float)color.r / 255.0f, ATMOSPHERE_SRGB_GAMMA);
    outRgb[1] = powf((float)color.g / 255.0f, ATMOSPHERE_SRGB_GAMMA);
    outRgb[2] = powf((float)color.b / 255.0f, ATMOSPHERE_SRGB_GAMMA);
}

static bool AreColorsDifferent(const float a[3], const float b[3])
{
    return (a[0] != b[0]) || (a[1] != b[1]) || (a[2] != b[2]);
}

static double ClampUnit(double value)
{
    return fmin(fmax(value, 0.0), 1.0);
}

/* The distance from the ground to the top of the atmosphere along the horizon: the longest ray the transmittance table
 * holds from the ground (the shaders' ATMOSPHERE_TOP_FROM_HORIZON). */
static double GetHorizonToTop(void)
{
    return sqrt((WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS)
        * (WORLD_ATMOSPHERE_TOP_RADIUS + WORLD_ATMOSPHERE_PLANET_RADIUS));
}

/* The distance from a point at @p height to the planet's axis through its horizon point: sqrt(r^2 - R^2), taken as
 * sqrt(h (2R + h)). */
static double GetHorizonDistance(double height)
{
    double Height = fmax(height, 0.0);
    return sqrt(Height * (2.0 * WORLD_ATMOSPHERE_PLANET_RADIUS + Height));
}

/* (1 - e^-x) / x: the share of a stretch's own light that leaves it, or of an exponential's mean over a stretch; 1 for
 * x = 0 (the shaders' atmosphereStepShare). */
static double GetExponentialShare(double depth)
{
    return (depth > ATMOSPHERE_SHARE_SERIES_LIMIT) ? (1.0 - exp(-depth)) / depth : 1.0 - 0.5 * depth;
}

/* Where in a stretch [a, b] of a ray, on one side of the ground haze's base, the depth from @p a reaches @p depth
 * (@p stretchDepth: the whole stretch's): linear in depth where the density is even (below the base, or a nearly level
 * ray), else the exponential's inverse from the stretch's denser end (the shaders' hazeStretchDistance). */
static double GetGroundHazeStretchDistance(double density, double thickness, double heightAboveBase, double rayY,
    double a, double b, double depth, double stretchDepth)
{
    if (!(depth > 0.0))
    {
        return a;
    }
    if (!(depth < stretchDepth))
    {
        return b;
    }
    double StartHeight = heightAboveBase + rayY * a;
    double EndHeight = heightAboveBase + rayY * b;
    double Slope = fabs(rayY) * (b - a) / thickness;
    if (!((StartHeight + EndHeight) > 0.0) || (Slope < ATMOSPHERE_GROUND_HAZE_SLOPE_LIMIT))
    {
        return a + depth * (b - a) / stretchDepth;
    }
    if (rayY > 0.0)
    {
        // Thinning from a: depth = density(a) thickness / rayY (1 - e^(-rayY (t - a) / thickness)).
        double DensityAtStart = density * exp(-StartHeight / thickness);
        if (!(DensityAtStart > 0.0))
        {
            return a;
        }
        double U = depth * rayY / (DensityAtStart * thickness);
        double Factor = (U > ATMOSPHERE_SHARE_SERIES_LIMIT) ? -log(fmax(1.0 - U, 1e-30)) / U : 1.0 + 0.5 * U;
        return fmin(fmax(a + depth / DensityAtStart * Factor, a), b);
    }
    // Thickening toward b: depth = density(b) thickness / -rayY (e^(rayY (b - t) / thickness) - e^(-slope)).
    double DensityAtEnd = density * exp(-EndHeight / thickness);
    if (!(DensityAtEnd > 0.0))
    {
        return a;
    }
    double V = depth * (-rayY) / (DensityAtEnd * thickness) + exp(-Slope);
    return fmin(fmax(b + thickness * log(fmin(V, 1.0)) / (-rayY), a), b);
}

/* The ground haze's optical depth over @p along metres on one side of a curved ray's lowest point, @p lowest above the
 * base there, the ray rising around it as lowest + s^2 / (2 curvatureRadius): the stretch below the base at the base's
 * density, the rest a Gaussian in s (the shaders' hazeGroundCurveSide). */
static double GetGroundHazeCurveSide(double density, double thickness, double lowest, double curvatureRadius,
    double along)
{
    double Width = sqrt(2.0 * curvatureRadius * thickness);
    double BelowBase = sqrt(fmax(-2.0 * curvatureRadius * lowest, 0.0)); // where the ray crosses the base
    double Depth = density * fmin(along, BelowBase);
    if (along > BelowBase)
    {
        double A = BelowBase / Width;
        double B = along / Width;
        // The tail from the base's crossing to @p along: e^(a^2) erfc(a) - e^(a^2 - b^2) e^(b^2) erfc(b), times the
        // density at the crossing (the base's) or at the lowest point (above the base).
        Depth += density * exp(-fmax(lowest, 0.0) / thickness) * 0.5 * sqrt(ATMOSPHERE_PI) * Width
            * (WorldAtmosphere_GetScaledErfc(A) - exp(A * A - B * B) * WorldAtmosphere_GetScaledErfc(B));
    }
    return Depth;
}

/* The ratio between two neighbouring distance slices of the volume. */
static double GetVolumeSliceRatio(void)
{
    return pow(WORLD_ATMOSPHERE_VOLUME_FARTHEST / WORLD_ATMOSPHERE_VOLUME_NEAREST,
        1.0 / (double)WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE);
}

/* The angles of the sky-view table seen from @p viewHeight: from the zenith to the horizon (beyond 90 degrees: the
 * ground falls away), and from the horizon to the nadir. */
static void GetSkyViewHorizon(double viewHeight, double* outZenithToHorizon, double* outHorizonToNadir)
{
    double Height = fmax(viewHeight, 0.0);
    double CosHorizonToNadir = GetHorizonDistance(Height) / (WORLD_ATMOSPHERE_PLANET_RADIUS + Height);
    *outHorizonToNadir = acos(fmin(CosHorizonToNadir, 1.0));
    *outZenithToHorizon = ATMOSPHERE_PI - *outHorizonToNadir;
}


// Functions (declared in WorldAtmosphere.h).
void WorldAtmosphere_GetMedium(const WorldEnvironment* environment, WorldAtmosphereMedium* outMedium)
{
    float Turbidity = ClampFinite(environment->SkyTurbidity, 0.0f, INFINITY, ATMOSPHERE_FALLBACK_TURBIDITY);
    float MieExtinction = (float)WORLD_ATMOSPHERE_MIE_EXTINCTION * Turbidity / ATMOSPHERE_REFERENCE_TURBIDITY;
    float AirDensity = ClampFinite(environment->AirDensity, 0.0f, (float)WORLD_ENVIRONMENT_MAX_AIR_MULTIPLE, 1.0f);
    float Ozone = ClampFinite(environment->Ozone, 0.0f, (float)WORLD_ENVIRONMENT_MAX_AIR_MULTIPLE, 1.0f);
    float Albedo[3];
    ColorToLinear(environment->HazeAlbedo, Albedo);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        outMedium->RayleighScattering[Channel] = RayleighScattering[Channel] * AirDensity;
        outMedium->MieScattering[Channel] = MieExtinction * Albedo[Channel];
        outMedium->MieExtinction[Channel] = MieExtinction;
        outMedium->OzoneAbsorption[Channel] = OzoneAbsorption[Channel] * Ozone;
    }
    outMedium->MieHeight = ClampFinite(environment->HazeHeight, (float)WORLD_ENVIRONMENT_MIN_HAZE_HEIGHT,
        (float)WORLD_ENVIRONMENT_MAX_HAZE_HEIGHT, ATMOSPHERE_FALLBACK_HAZE_HEIGHT);
    outMedium->MieAnisotropy = ClampFinite(environment->SunGlow, 0.0f, (float)WORLD_ENVIRONMENT_MAX_SUN_GLOW,
        ATMOSPHERE_FALLBACK_SUN_GLOW);
    outMedium->MultipleScattering = ClampFinite(environment->MultipleScattering, 0.0f, 1.0f, 1.0f);
    ColorToLinear(environment->SkyGroundAlbedo, outMedium->GroundAlbedo);
}

bool WorldAtmosphere_AreTablesStale(const WorldAtmosphereMedium* builtFor, const WorldAtmosphereMedium* medium)
{
    return AreColorsDifferent(builtFor->RayleighScattering, medium->RayleighScattering)
        || AreColorsDifferent(builtFor->MieScattering, medium->MieScattering)
        || AreColorsDifferent(builtFor->MieExtinction, medium->MieExtinction)
        || (builtFor->MieHeight != medium->MieHeight)
        || AreColorsDifferent(builtFor->OzoneAbsorption, medium->OzoneAbsorption)
        || AreColorsDifferent(builtFor->GroundAlbedo, medium->GroundAlbedo);
}

bool WorldAtmosphere_UpdateTableRecord(WorldAtmosphereTableRecord* record, const WorldAtmosphereMedium* medium)
{
    if (record->IsBuilt && !WorldAtmosphere_AreTablesStale(&record->BuiltFor, medium))
    {
        return false;
    }
    record->BuiltFor = *medium;
    record->IsBuilt = true;
    record->BuildCount++;
    return true;
}

bool WorldAtmosphere_IsIrradianceStale(const WorldAtmosphereMedium* builtFor, const WorldAtmosphereMedium* medium)
{
    return WorldAtmosphere_AreTablesStale(builtFor, medium) || (builtFor->MieAnisotropy != medium->MieAnisotropy)
        || (builtFor->MultipleScattering != medium->MultipleScattering);
}

bool WorldAtmosphere_UpdateIrradianceRecord(WorldAtmosphereTableRecord* record, const WorldAtmosphereMedium* medium)
{
    if (record->IsBuilt && !WorldAtmosphere_IsIrradianceStale(&record->BuiltFor, medium))
    {
        return false;
    }
    record->BuiltFor = *medium;
    record->IsBuilt = true;
    record->BuildCount++;
    return true;
}

double WorldAtmosphere_GetViewHeight(double cameraHeight)
{
    if (!isfinite(cameraHeight))
    {
        return WORLD_ATMOSPHERE_MIN_VIEW_HEIGHT;
    }
    return fmin(fmax(cameraHeight, WORLD_ATMOSPHERE_MIN_VIEW_HEIGHT), WORLD_ATMOSPHERE_MAX_VIEW_HEIGHT);
}

double WorldAtmosphere_GetHeightAt(WorldAtmosphereRay ray, double distance)
{
    // r^2 - R^2 at the point, from the start's h (2R + h) without subtracting two radii.
    double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + ray.Height;
    double RhoSquared = ray.Height * (2.0 * WORLD_ATMOSPHERE_PLANET_RADIUS + ray.Height)
        + distance * (distance + 2.0 * Radius * ray.CosZenith);
    double PointRadius = sqrt(fmax(RhoSquared + WORLD_ATMOSPHERE_PLANET_RADIUS * WORLD_ATMOSPHERE_PLANET_RADIUS, 0.0));
    return RhoSquared / (PointRadius + WORLD_ATMOSPHERE_PLANET_RADIUS);
}

double WorldAtmosphere_GetHorizonCos(double height)
{
    return -GetHorizonDistance(height) / (WORLD_ATMOSPHERE_PLANET_RADIUS + fmax(height, 0.0));
}

double WorldAtmosphere_GetDistanceToTop(WorldAtmosphereRay ray)
{
    double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + ray.Height;
    // R_top^2 - r^2 (1 - mu^2), with R_top^2 - r^2 taken as a product of the difference and the sum.
    double TopSquared = (WORLD_ATMOSPHERE_TOP_RADIUS - Radius) * (WORLD_ATMOSPHERE_TOP_RADIUS + Radius)
        + Radius * Radius * ray.CosZenith * ray.CosZenith;
    return fmax(-Radius * ray.CosZenith + sqrt(fmax(TopSquared, 0.0)), 0.0);
}

double WorldAtmosphere_GetDistanceToGround(WorldAtmosphereRay ray)
{
    double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + ray.Height;
    double Height = fmax(ray.Height, 0.0);
    double Discriminant = Radius * Radius * ray.CosZenith * ray.CosZenith
        - Height * (2.0 * WORLD_ATMOSPHERE_PLANET_RADIUS + Height);
    if ((ray.CosZenith >= 0.0) || (Discriminant < 0.0))
    {
        return -1.0;
    }
    return fmax(-Radius * ray.CosZenith - sqrt(Discriminant), 0.0);
}

double WorldAtmosphere_UnitToTexel(double unit, int size)
{
    double Size = (double)size;
    return 0.5 / Size + unit * (Size - 1.0) / Size;
}

double WorldAtmosphere_TexelToUnit(double coordinate, int size)
{
    double Size = (double)size;
    return (coordinate - 0.5 / Size) * Size / (Size - 1.0);
}

Vector2Double WorldAtmosphere_GetTransmittanceUv(WorldAtmosphereRay ray)
{
    Vector2Double Unit = WorldAtmosphere_GetTransmittanceUnit(ray);
    return Vector2Double_Create(WorldAtmosphere_UnitToTexel(Unit.X, WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH),
        WorldAtmosphere_UnitToTexel(Unit.Y, WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT));
}

WorldAtmosphereRay WorldAtmosphere_GetTransmittanceRay(Vector2Double uv)
{
    return WorldAtmosphere_GetTransmittanceUnitRay(Vector2Double_Create(
        WorldAtmosphere_TexelToUnit(uv.X, WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH),
        WorldAtmosphere_TexelToUnit(uv.Y, WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT)));
}

Vector2Double WorldAtmosphere_GetTransmittanceUnit(WorldAtmosphereRay ray)
{
    double Height = fmax(ray.Height, 0.0);
    double HorizonToTop = GetHorizonToTop();
    double Rho = GetHorizonDistance(Height);
    double Distance = WorldAtmosphere_GetDistanceToTop((WorldAtmosphereRay){ .Height = Height, .CosZenith = ray.CosZenith });
    double MinDistance = (WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS) - Height;
    double MaxDistance = Rho + HorizonToTop;
    return Vector2Double_Create(ClampUnit((Distance - MinDistance) / (MaxDistance - MinDistance)),
        ClampUnit(Rho / HorizonToTop));
}

WorldAtmosphereRay WorldAtmosphere_GetTransmittanceUnitRay(Vector2Double unit)
{
    double UnitAcross = unit.X;
    double UnitUp = unit.Y;
    double HorizonToTop = GetHorizonToTop();
    double Rho = HorizonToTop * UnitUp;
    double Radius = sqrt(Rho * Rho + WORLD_ATMOSPHERE_PLANET_RADIUS * WORLD_ATMOSPHERE_PLANET_RADIUS);
    double Height = Rho * Rho / (Radius + WORLD_ATMOSPHERE_PLANET_RADIUS);
    double MinDistance = (WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS) - Height;
    double MaxDistance = Rho + HorizonToTop;
    double Distance = MinDistance + UnitAcross * (MaxDistance - MinDistance);
    // The law of cosines in the triangle of the planet's centre, the point and where the ray leaves the atmosphere:
    // R_top^2 - r^2 is H^2 - rho^2.
    double CosZenith = (Distance <= 0.0) ? 1.0
        : fmin(fmax((HorizonToTop * HorizonToTop - Rho * Rho - Distance * Distance) / (2.0 * Radius * Distance), -1.0),
            1.0);
    return (WorldAtmosphereRay){ .Height = Height, .CosZenith = CosZenith };
}

Vector2Double WorldAtmosphere_GetMultiscatterUv(WorldAtmosphereRay ray)
{
    double UnitAcross = ClampUnit(0.5 * ray.CosZenith + 0.5);
    double UnitUp = ClampUnit(ray.Height / (WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS));
    return Vector2Double_Create(WorldAtmosphere_UnitToTexel(UnitAcross, WORLD_ATMOSPHERE_MULTISCATTER_SIZE),
        WorldAtmosphere_UnitToTexel(UnitUp, WORLD_ATMOSPHERE_MULTISCATTER_SIZE));
}

WorldAtmosphereRay WorldAtmosphere_GetMultiscatterRay(Vector2Double uv)
{
    double UnitAcross = WorldAtmosphere_TexelToUnit(uv.X, WORLD_ATMOSPHERE_MULTISCATTER_SIZE);
    double UnitUp = WorldAtmosphere_TexelToUnit(uv.Y, WORLD_ATMOSPHERE_MULTISCATTER_SIZE);
    return (WorldAtmosphereRay)
    {
        .Height = UnitUp * (WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS),
        .CosZenith = 2.0 * UnitAcross - 1.0
    };
}

Vector2Double WorldAtmosphere_GetSkyViewUnit(double viewHeight, WorldAtmosphereView view)
{
    double ZenithToHorizon = 0.0;
    double HorizonToNadir = 0.0;
    GetSkyViewHorizon(viewHeight, &ZenithToHorizon, &HorizonToNadir);
    double ZenithAngle = acos(fmin(fmax(view.CosZenith, -1.0), 1.0));
    double UnitUp;
    if (ZenithAngle <= ZenithToHorizon)
    {
        UnitUp = 0.5 * (1.0 - sqrt(fmax(1.0 - ZenithAngle / ZenithToHorizon, 0.0)));
    }
    else
    {
        UnitUp = 0.5 + 0.5 * sqrt(fmin((ZenithAngle - ZenithToHorizon) / HorizonToNadir, 1.0));
    }
    double UnitAcross = sqrt(ClampUnit(0.5 * (1.0 - view.CosLightAzimuth)));
    return Vector2Double_Create(UnitAcross, UnitUp);
}

WorldAtmosphereView WorldAtmosphere_GetSkyViewDirection(double viewHeight, Vector2Double unit)
{
    double ZenithToHorizon = 0.0;
    double HorizonToNadir = 0.0;
    GetSkyViewHorizon(viewHeight, &ZenithToHorizon, &HorizonToNadir);
    double ZenithAngle;
    if (unit.Y < 0.5)
    {
        double Coordinate = 1.0 - 2.0 * unit.Y;
        ZenithAngle = ZenithToHorizon * (1.0 - Coordinate * Coordinate);
    }
    else
    {
        double Coordinate = 2.0 * unit.Y - 1.0;
        ZenithAngle = ZenithToHorizon + HorizonToNadir * Coordinate * Coordinate;
    }
    return (WorldAtmosphereView){ .CosZenith = cos(ZenithAngle), .CosLightAzimuth = 1.0 - 2.0 * unit.X * unit.X };
}

double WorldAtmosphere_GetCosLightAzimuth(Vector3Double direction, Vector3Double lightDirection)
{
    double Lengths = sqrt(direction.X * direction.X + direction.Z * direction.Z)
        * sqrt(lightDirection.X * lightDirection.X + lightDirection.Z * lightDirection.Z);
    if (!(Lengths > WORLD_ATMOSPHERE_VERTICAL_EPSILON))
    {
        return 1.0;
    }
    return fmin(fmax((direction.X * lightDirection.X + direction.Z * lightDirection.Z) / Lengths, -1.0), 1.0);
}

Vector2Double WorldAtmosphere_GetSkyViewUv(double viewHeight, Vector3Double direction, Vector3Double lightDirection,
    WorldAtmosphereSkyViewHalf half)
{
    WorldAtmosphereView View =
    {
        .CosZenith = direction.Y,
        .CosLightAzimuth = WorldAtmosphere_GetCosLightAzimuth(direction, lightDirection)
    };
    Vector2Double Unit = WorldAtmosphere_GetSkyViewUnit(viewHeight, View);
    return Vector2Double_Create(WorldAtmosphere_UnitToTexel(Unit.X, WORLD_ATMOSPHERE_SKY_VIEW_WIDTH),
        ((double)half + WorldAtmosphere_UnitToTexel(Unit.Y, WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT))
            / (double)WORLD_ATMOSPHERE_SKY_VIEW_HALF_COUNT);
}

void WorldAtmosphere_GetHaze(const WorldEnvironment* environment, float configFogStrength, WorldAtmosphereHaze* outHaze)
{
    float AerialScale = ClampFinite(environment->AerialDistanceScale, 0.0f,
        (float)WORLD_ENVIRONMENT_MAX_AERIAL_DISTANCE_SCALE, 1.0f);
    float WorldStrength = ClampFinite(environment->FogStrength, 0.0f, (float)WORLD_ATMOSPHERE_MAX_HAZE_SCALE, 1.0f);
    float PlayerStrength = ClampFinite(configFogStrength, 0.0f, (float)WORLD_ATMOSPHERE_MAX_HAZE_SCALE, 1.0f);
    // In double: the product of three clamped factors may pass a float's range before it is clamped.
    double Scale = (double)AerialScale * (double)WorldStrength * (double)PlayerStrength;
    outHaze->DistanceScale = (float)fmin(Scale, WORLD_ATMOSPHERE_MAX_HAZE_SCALE);
    outHaze->StartDistance = ClampFinite(environment->HazeStartDistance, 0.0f, (float)WORLD_ENVIRONMENT_MAX_DISTANCE,
        0.0f);
    outHaze->GroundHazeDensity = ClampFinite(environment->GroundHazeDensity, 0.0f,
        (float)WORLD_ENVIRONMENT_MAX_GROUND_HAZE_DENSITY, 0.0f);
    outHaze->GroundHazeBase = ClampFinite(environment->GroundHazeBase, -(float)WORLD_ENVIRONMENT_MAX_DISTANCE,
        (float)WORLD_ENVIRONMENT_MAX_DISTANCE, 0.0f);
    outHaze->GroundHazeThickness = ClampFinite(environment->GroundHazeThickness,
        (float)WORLD_ENVIRONMENT_MIN_GROUND_HAZE_THICKNESS, (float)WORLD_ENVIRONMENT_MAX_GROUND_HAZE_THICKNESS,
        ATMOSPHERE_FALLBACK_GROUND_HAZE_THICKNESS);
    ColorToLinear(environment->GroundHazeAlbedo, outHaze->GroundHazeAlbedo);
    outHaze->GroundHazeGlow = ClampFinite(environment->GroundHazeGlow, 0.0f, (float)WORLD_ENVIRONMENT_MAX_SUN_GLOW,
        ATMOSPHERE_FALLBACK_GROUND_HAZE_GLOW);
    outHaze->GroundHeight = ClampFinite(environment->HorizonGroundHeight, -(float)WORLD_ENVIRONMENT_MAX_DISTANCE,
        (float)WORLD_ENVIRONMENT_MAX_DISTANCE, 0.0f);
}

double WorldAtmosphere_GetVolumeSliceDistance(int slice)
{
    int Slice = (slice < 0) ? 0
        : ((slice > WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE) ? WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE : slice);
    return WORLD_ATMOSPHERE_VOLUME_NEAREST * pow(WORLD_ATMOSPHERE_VOLUME_FARTHEST / WORLD_ATMOSPHERE_VOLUME_NEAREST,
        (double)Slice / (double)WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE);
}

double WorldAtmosphere_GetVolumeSlice(double distance, double distanceToTop)
{
    if (!(distance > 0.0))
    {
        return -1.0;
    }
    if (distance < WORLD_ATMOSPHERE_VOLUME_NEAREST)
    {
        return -1.0 + distance / WORLD_ATMOSPHERE_VOLUME_NEAREST;
    }
    if (distance <= WORLD_ATMOSPHERE_VOLUME_FARTHEST)
    {
        // The slice below the distance (its rounding does not matter: the share of the way to the next slice makes up for
        // it), then the share of the way, linear in distance.
        double Index = log(distance / WORLD_ATMOSPHERE_VOLUME_NEAREST) / log(GetVolumeSliceRatio());
        int Lower = (int)floor(Index);
        Lower = (Lower < 0) ? 0 : ((Lower > WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE - 1)
            ? (WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE - 1) : Lower);
        double From = WorldAtmosphere_GetVolumeSliceDistance(Lower);
        double To = WorldAtmosphere_GetVolumeSliceDistance(Lower + 1);
        return (double)Lower + (distance - From) / (To - From);
    }
    double Remaining = distanceToTop - WORLD_ATMOSPHERE_VOLUME_FARTHEST;
    double Share = (Remaining > 0.0) ? fmin((distance - WORLD_ATMOSPHERE_VOLUME_FARTHEST) / Remaining, 1.0) : 1.0;
    return (double)WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE + Share;
}

double WorldAtmosphere_GetVolumeDistance(double slice, double distanceToTop)
{
    if (!(slice > -1.0))
    {
        return 0.0;
    }
    if (slice < 0.0)
    {
        return (slice + 1.0) * WORLD_ATMOSPHERE_VOLUME_NEAREST;
    }
    if (slice <= (double)WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE)
    {
        int Lower = (int)floor(slice);
        Lower = (Lower > WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE - 1) ? (WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE - 1)
            : Lower;
        double From = WorldAtmosphere_GetVolumeSliceDistance(Lower);
        double To = WorldAtmosphere_GetVolumeSliceDistance(Lower + 1);
        return From + (slice - (double)Lower) * (To - From);
    }
    double Share = fmin(slice - (double)WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE, 1.0);
    return WORLD_ATMOSPHERE_VOLUME_FARTHEST + Share * fmax(distanceToTop - WORLD_ATMOSPHERE_VOLUME_FARTHEST, 0.0);
}

Vector2Double WorldAtmosphere_GetVolumeUv(Vector2Double screenUv, int slice, WorldAtmosphereVolumePart part)
{
    double Size = (double)WORLD_ATMOSPHERE_VOLUME_SIZE;
    double Across = fmin(fmax(screenUv.X * Size, 0.5), Size - 0.5);
    double Up = fmin(fmax(screenUv.Y * Size, 0.5), Size - 0.5);
    return Vector2Double_Create(((double)slice * Size + Across) / (double)WORLD_ATMOSPHERE_VOLUME_WIDTH,
        ((double)part * Size + Up) / (double)WORLD_ATMOSPHERE_VOLUME_HEIGHT);
}

Vector2Double WorldAtmosphere_GetVolumeValueUv(WorldAtmosphereVolumeValue value)
{
    return Vector2Double_Create(((double)value + 0.5) / (double)WORLD_ATMOSPHERE_VOLUME_WIDTH,
        (2.0 * (double)WORLD_ATMOSPHERE_VOLUME_SIZE + 0.5) / (double)WORLD_ATMOSPHERE_VOLUME_HEIGHT);
}

double WorldAtmosphere_GetSkyLightCoordinate(double height)
{
    double Share = ClampUnit(height / WORLD_ATMOSPHERE_SKY_LIGHT_HIGHEST); // ClampUnit drops a NaN
    return (double)(WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT - 1) * sqrt(Share);
}

double WorldAtmosphere_GetSkyLightHeight(int index)
{
    int Index = (index < 0) ? 0
        : ((index > WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT - 1) ? (WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT - 1) : index);
    double Unit = (double)Index / (double)(WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT - 1);
    return WORLD_ATMOSPHERE_SKY_LIGHT_HIGHEST * Unit * Unit;
}

int WorldAtmosphere_GetSkyLightColumn(WorldAtmosphereSkyLight value, int index)
{
    int Index = (index < 0) ? 0
        : ((index > WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT - 1) ? (WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT - 1) : index);
    return WORLD_ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN + (int)value * WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT + Index;
}

/* The irradiance table's mappings (WorldAtmosphere_GetIrradianceUnit): the asinh of the lowest elevation's and the
 * zenith's over the elevation scale, and the log of the top's height over the height scale. */
static double GetIrradianceLowestAcross(void)
{
    return asinh(WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION / WORLD_ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE);
}

static double GetIrradianceHighestAcross(void)
{
    return asinh(0.5 * ATMOSPHERE_PI / WORLD_ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE);
}

static double GetIrradianceTopUp(void)
{
    return log1p((WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS) / WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE);
}

Vector2Double WorldAtmosphere_GetIrradianceUnit(WorldAtmosphereRay ray)
{
    double Elevation = asin(fmin(fmax(ray.CosZenith, -1.0), 1.0));
    double Lowest = GetIrradianceLowestAcross();
    double Across = ClampUnit((asinh(Elevation / WORLD_ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE) - Lowest)
        / (GetIrradianceHighestAcross() - Lowest));
    double Up = ClampUnit(log1p(fmax(ray.Height, 0.0) / WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE) / GetIrradianceTopUp());
    return Vector2Double_Create(Across, Up); // fmax and ClampUnit drop a NaN
}

WorldAtmosphereRay WorldAtmosphere_GetIrradianceRay(Vector2Double unit)
{
    double Lowest = GetIrradianceLowestAcross();
    double Elevation = WORLD_ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE
        * sinh(Lowest + ClampUnit(unit.X) * (GetIrradianceHighestAcross() - Lowest));
    return (WorldAtmosphereRay)
    {
        .Height = WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE * expm1(ClampUnit(unit.Y) * GetIrradianceTopUp()),
        .CosZenith = sin(Elevation)
    };
}

Vector2Double WorldAtmosphere_GetIrradianceUv(WorldAtmosphereRay ray)
{
    // The lights' part: the first WORLD_ATMOSPHERE_IRRADIANCE_WIDTH columns of a texture one column wider.
    Vector2Double Unit = WorldAtmosphere_GetIrradianceUnit(ray);
    double Width = (double)WORLD_ATMOSPHERE_IRRADIANCE_WIDTH;
    return Vector2Double_Create((0.5 + Unit.X * (Width - 1.0)) / (double)WORLD_ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH,
        WorldAtmosphere_UnitToTexel(Unit.Y, WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT));
}

Vector2Double WorldAtmosphere_GetLightPollutionIrradianceUv(double height)
{
    Vector2Double Unit = WorldAtmosphere_GetIrradianceUnit((WorldAtmosphereRay){ .Height = height, .CosZenith = 1.0 });
    return Vector2Double_Create(((double)WORLD_ATMOSPHERE_IRRADIANCE_WIDTH + 0.5)
        / (double)WORLD_ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH,
        WorldAtmosphere_UnitToTexel(Unit.Y, WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT));
}

double WorldAtmosphere_GetGroundHazeDepth(double density, double thickness, double heightAboveBase, double rayY,
    double from, double to)
{
    if (!(density > 0.0) || !(to > from))
    {
        return 0.0;
    }
    // The mean density over the heights the part crosses, times its length: the integral of the density over the
    // heights divided by the height crossed, taken so that no two near values are subtracted (a level ray crosses none).
    double Start = heightAboveBase + rayY * from;
    double End = heightAboveBase + rayY * to;
    double Low = fmin(Start, End);
    double High = fmax(Start, End);
    double Mean;
    if (High <= 0.0)
    {
        Mean = density; // all of it at or below the base
    }
    else if (Low >= 0.0)
    {
        Mean = density * exp(-Low / thickness) * GetExponentialShare((High - Low) / thickness);
    }
    else
    {
        // From below the base to above it: the part below at the base's density, the part above thinning.
        Mean = density * (thickness * (1.0 - exp(-High / thickness)) - Low) / (High - Low);
    }
    return Mean * (to - from);
}

double WorldAtmosphere_GetGroundHazeDistance(double density, double thickness, double heightAboveBase, double rayY,
    double from, double to, double depth)
{
    if (!(depth > 0.0) || !(to > from))
    {
        return from;
    }
    double Crossing = (rayY != 0.0) ? -heightAboveBase / rayY : -1.0;
    double Split = ((Crossing > from) && (Crossing < to)) ? Crossing : to;
    double FirstDepth = WorldAtmosphere_GetGroundHazeDepth(density, thickness, heightAboveBase, rayY, from, Split);
    if (depth <= FirstDepth)
    {
        return GetGroundHazeStretchDistance(density, thickness, heightAboveBase, rayY, from, Split, depth, FirstDepth);
    }
    return GetGroundHazeStretchDistance(density, thickness, heightAboveBase, rayY, Split, to, depth - FirstDepth,
        WorldAtmosphere_GetGroundHazeDepth(density, thickness, heightAboveBase, rayY, Split, to));
}

double WorldAtmosphere_GetGroundHazeDepthCurved(double density, double thickness, double heightAboveBase, double rayY,
    double radius, double from, double to)
{
    if (!(density > 0.0) || !(to > from))
    {
        return 0.0;
    }
    // The ray's lowest point (its closest approach to the planet's centre), how far below the start it lies (taken as
    // r (1 - sqrt(1 - mu^2)) without cancellation) and its distance from the centre (the parabola's radius there).
    double Closest = -radius * rayY;
    double SinZenith = sqrt(fmax(1.0 - rayY * rayY, 0.0));
    double Lowest = heightAboveBase - radius * rayY * rayY / (1.0 + SinZenith);
    double CurvatureRadius = radius * SinZenith;
    // The depth is odd in the signed distance from the lowest point: the difference of the two ends' sides.
    double SFrom = from - Closest;
    double STo = to - Closest;
    double DepthTo = GetGroundHazeCurveSide(density, thickness, Lowest, CurvatureRadius, fabs(STo));
    double DepthFrom = GetGroundHazeCurveSide(density, thickness, Lowest, CurvatureRadius, fabs(SFrom));
    return fmax(((STo < 0.0) ? -DepthTo : DepthTo) - ((SFrom < 0.0) ? -DepthFrom : DepthFrom), 0.0);
}

double WorldAtmosphere_GetScaledErfc(double x)
{
    double T = 1.0 / (1.0 + 0.5 * fmax(x, 0.0));
    return T * exp(-1.26551223 + T * (1.00002368 + T * (0.37409196 + T * (0.09678418 + T * (-0.18628806
        + T * (0.27886807 + T * (-1.13520398 + T * (1.48851587 + T * (-0.82215223 + T * 0.17087277)))))))));
}

double WorldAtmosphere_GetLightCosAt(WorldAtmosphereRay ray, double distance, double lightCosZenith,
    double cosLightAngle)
{
    // The point's own zenith is its direction from the planet's centre: the start's radius up, plus the distance along
    // the ray; its cosine with the light is that over the point's radius.
    double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + ray.Height;
    double PointRadius = WORLD_ATMOSPHERE_PLANET_RADIUS + WorldAtmosphere_GetHeightAt(ray, distance);
    return fmin(fmax((Radius * lightCosZenith + distance * cosLightAngle) / PointRadius, -1.0), 1.0);
}

double WorldAtmosphere_GetLightPollutionShare(double height, double horizontalDistance)
{
    return exp(-fmax(height, 0.0) / WORLD_ATMOSPHERE_LIGHT_POLLUTION_HEIGHT)
        * exp(-fmax(horizontalDistance, 0.0) / WORLD_ATMOSPHERE_LIGHT_POLLUTION_REACH);
}

void WorldAtmosphere_GetLightPollutionRadiance(const WorldEnvironment* environment, float daylight,
    float outRadiance[3])
{
    float Tint[3];
    ColorToLinear(environment->LightPollutionColor, Tint);
    // fminf(fmaxf(...)) drops a NaN: a broken value gives no light pollution.
    float Pollution = fminf(fmaxf(environment->LightPollution, 0.0f), 1.0f);
    float Night = 1.0f - fminf(fmaxf(daylight, 0.0f), 1.0f);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        outRadiance[Channel] = Tint[Channel] * Pollution * Night * (float)WORLD_ATMOSPHERE_LIGHT_POLLUTION_RADIANCE;
    }
}
