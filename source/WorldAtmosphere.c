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
#define ATMOSPHERE_FALLBACK_TURBIDITY 3.0f
#define ATMOSPHERE_FALLBACK_HAZE_HEIGHT 1200.0f
#define ATMOSPHERE_FALLBACK_SUN_GLOW 0.758f
/** The turbidity the haze's extinction is given for. */
#define ATMOSPHERE_REFERENCE_TURBIDITY 3.0f


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
    double Height = fmax(ray.Height, 0.0);
    double HorizonToTop = GetHorizonToTop();
    double Rho = GetHorizonDistance(Height);
    double Distance = WorldAtmosphere_GetDistanceToTop((WorldAtmosphereRay){ .Height = Height, .CosZenith = ray.CosZenith });
    double MinDistance = (WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS) - Height;
    double MaxDistance = Rho + HorizonToTop;
    double UnitAcross = ClampUnit((Distance - MinDistance) / (MaxDistance - MinDistance));
    double UnitUp = ClampUnit(Rho / HorizonToTop);
    return Vector2Double_Create(WorldAtmosphere_UnitToTexel(UnitAcross, WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH),
        WorldAtmosphere_UnitToTexel(UnitUp, WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT));
}

WorldAtmosphereRay WorldAtmosphere_GetTransmittanceRay(Vector2Double uv)
{
    double UnitAcross = WorldAtmosphere_TexelToUnit(uv.X, WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH);
    double UnitUp = WorldAtmosphere_TexelToUnit(uv.Y, WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT);
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
