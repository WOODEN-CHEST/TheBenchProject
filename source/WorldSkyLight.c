// The sky's light on the world's surfaces as pure maths (T36): the light through the air at the camera, the C mirror's
// own transmittance and multiple scattering tables and the GPU's irradiance table built from them by the GPU's
// quadrature, their rebuild over frames, the frame's light and the eye's exposure on that scale (WorldSkyLight.h).

#include <math.h>
#include "WorldSkyLight.h"
#include "wr/WRMemory.h"


// Macros.
/** Pi in double. */
#define SKY_LIGHT_PI 3.14159265358979323846
/** sRGB gamma, for the colours the environment stores (the renderer's own conversion). */
#define SKY_LIGHT_SRGB_GAMMA 2.2f
/** The ozone layer's corners above the ground, in metres: where its tent's density bends (the exact transmittance
 *  splits its rays there, so each stretch is smooth). */
#define SKY_LIGHT_OZONE_LOW (WORLD_ATMOSPHERE_OZONE_CENTER - WORLD_ATMOSPHERE_OZONE_HALF_WIDTH)
#define SKY_LIGHT_OZONE_HIGH (WORLD_ATMOSPHERE_OZONE_CENTER + WORLD_ATMOSPHERE_OZONE_HALF_WIDTH)
/** The most stretches a ray of the exact transmittance has: its start, its lowest point, two crossings of each of the
 *  ozone layer's three corners, its end. */
#define SKY_LIGHT_MAX_BREAKS 9
/** The Gauss-Legendre nodes of each stretch of the exact transmittance. */
#define SKY_LIGHT_GAUSS_NODES 16
/** Below this ((1 - e^-x) / x's argument) the series is used: atmosphereStepShare's threshold. */
#define SKY_LIGHT_SHARE_SERIES_LIMIT 1e-3
/** The most points along one direction of the irradiance quadrature: Simpson's stretches' ends and middles. */
#define SKY_LIGHT_RAY_POINTS (2 * WORLD_SKY_LIGHT_STEPS + 1)
_Static_assert(WORLD_SKY_LIGHT_MIE_STEPS <= WORLD_SKY_LIGHT_STEPS, "the haze's directions take no more points");
/** Below this the haze's forward scattering counts as none (Henyey and Greenstein's inverse divides by it). */
#define SKY_LIGHT_ISOTROPIC_LIMIT 1e-4


// Types.
/** What a point's lookups in the C tables need that does not depend on the light: its height and the horizon's zenith
 *  cosine from it, its distance to the planet's centre and what its distance to the top along any direction needs, and
 *  its row in each table. */
typedef struct TablePlaceStruct
{
    double Height;
    double HorizonCos;
    double Radius;
    /** (R_top - r)(R_top + r): the distance to the top along a zenith cosine mu is -r mu + sqrt(this + r^2 mu^2). */
    double TopSquared;
    /** The shortest and longest distance to the top from the point (straight up, along the horizon), as the transmittance
     *  table's columns span them: its lowest and the reciprocal of the span. */
    double MinToTop;
    double InverseToTopSpan;
    /** The transmittance table's and the multiple scattering table's lower rows and the share of the way to the next. */
    int TransmittanceRow;
    double TransmittanceRowShare;
    int MultiscatterRow;
    double MultiscatterRowShare;
} TablePlace;

/** One point along a direction of the irradiance quadrature: where it is, the air's scattering there, and what reaches
 *  the surface of its light (Simpson's weight times the view's transmittance to it). */
typedef struct IrradiancePointStruct
{
    double Distance;
    TablePlace Place;
    double Rayleigh[3];
    double Mie[3];
    double Weight[3];
} IrradiancePoint;

/** One direction of the irradiance quadrature from a surface: its zenith cosine and its points. */
typedef struct IrradianceRayStruct
{
    double CosZenith;
    double SinZenith;
    int PointCount;
    IrradiancePoint Points[SKY_LIGHT_RAY_POINTS];
} IrradianceRay;

/** One direction of the multiple scattering's march (the GPU's MarchDirection) from a texel's height: its points and the
 *  light-independent part of their light (the air's scattering times what reaches the texel's point), its transfer and
 *  its hit on the ground. Shared by every light zenith and azimuth of the texel's row. */
typedef struct MultiscatterDirectionStruct
{
    double CosZenith;
    double SinZenith;
    double Distance[WORLD_SKY_LIGHT_MULTISCATTER_STEPS];
    TablePlace Place[WORLD_SKY_LIGHT_MULTISCATTER_STEPS];
    double Through[WORLD_SKY_LIGHT_MULTISCATTER_STEPS][3];
    double Transfer[3];
    bool IsGroundHit;
    double PathLength;
    double GroundThrough[3];
} MultiscatterDirection;


// Fields.
/* Gauss-Legendre on [-1, 1], 16 nodes (the exact transmittance's stretches) and 6 (the irradiance's zenith rings). */
static const double GaussNodes16[SKY_LIGHT_GAUSS_NODES] =
{
    -0.9894009349916499, -0.9445750230732326, -0.8656312023878318, -0.7554044083550030, -0.6178762444026438,
    -0.4580167776572274, -0.2816035507792589, -0.0950125098376374, 0.0950125098376374, 0.2816035507792589,
    0.4580167776572274, 0.6178762444026438, 0.7554044083550030, 0.8656312023878318, 0.9445750230732326,
    0.9894009349916499
};
static const double GaussWeights16[SKY_LIGHT_GAUSS_NODES] =
{
    0.0271524594117541, 0.0622535239386479, 0.0951585116824928, 0.1246289712555339, 0.1495959888165767,
    0.1691565193950025, 0.1826034150449236, 0.1894506104550685, 0.1894506104550685, 0.1826034150449236,
    0.1691565193950025, 0.1495959888165767, 0.1246289712555339, 0.0951585116824928, 0.0622535239386479,
    0.0271524594117541
};
static const double GaussNodes6[WORLD_SKY_LIGHT_ZENITH_NODES] =
{
    -0.9324695142031521, -0.6612093864662645, -0.2386191860831969, 0.2386191860831969, 0.6612093864662645,
    0.9324695142031521
};
static const double GaussWeights6[WORLD_SKY_LIGHT_ZENITH_NODES] =
{
    0.1713244923791704, 0.3607615730481386, 0.4679139345726910, 0.4679139345726910, 0.3607615730481386,
    0.1713244923791704
};


// Static functions: the air.
/* The hot paths compare instead of calling fmin and fmax (which GCC does not inline without -ffinite-math-only). */
static inline double Lesser(double a, double b)
{
    return (a < b) ? a : b;
}

static inline double Greater(double a, double b)
{
    return (a > b) ? a : b;
}

/* A value in [0, 1]; NaN gives 0. */
static inline double ClampUnit(double value)
{
    return (value > 0.0) ? ((value < 1.0) ? value : 1.0) : 0.0;
}

/* A cosine in [-1, 1]; NaN gives -1. */
static inline double ClampCos(double value)
{
    return (value > -1.0) ? ((value < 1.0) ? value : 1.0) : -1.0;
}

static inline double OzoneDensity(double height)
{
    return Greater(1.0 - fabs(height - WORLD_ATMOSPHERE_OZONE_CENTER) / WORLD_ATMOSPHERE_OZONE_HALF_WIDTH, 0.0);
}

/* The air's extinction per metre at a height (below the ground: as on it). */
static void Extinction(const WorldAtmosphereMedium* medium, double height, double out[3])
{
    double Height = fmax(height, 0.0);
    double Rayleigh = exp(-Height / WORLD_ATMOSPHERE_RAYLEIGH_HEIGHT);
    double Mie = exp(-Height / fmax((double)medium->MieHeight, 1.0));
    double Ozone = OzoneDensity(Height);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        out[Channel] = (double)medium->RayleighScattering[Channel] * Rayleigh
            + (double)medium->MieExtinction[Channel] * Mie + (double)medium->OzoneAbsorption[Channel] * Ozone;
    }
}

/* (1 - e^-x) / x: the share of a step's own light that leaves it; 1 for x = 0. */
static double ExponentialShare(double x)
{
    return (x > SKY_LIGHT_SHARE_SERIES_LIMIT) ? (1.0 - exp(-x)) / x : 1.0 - 0.5 * x;
}

/* The mean of an exponential density between two points from its values there (@p densityA and @p densityB, at heights
 * @p heightA and @p heightB, both at least 0), its scale height @p scale: exact for a height changing linearly between
 * them, without another exponential (the higher one's ratio to the lower one's is e^-x). */
static inline double MeanDensity(double densityA, double densityB, double heightA, double heightB, double scale)
{
    double Low = Greater(densityA, densityB);
    double High = Lesser(densityA, densityB);
    double X = fabs(heightA - heightB) / scale;
    return (X > SKY_LIGHT_SHARE_SERIES_LIMIT) ? (Low - High) / X : Low * (1.0 - 0.5 * X);
}

/* The air's densities at a height (at least 0): the Rayleigh's, the haze's and the ozone's. */
static inline void Densities(const WorldAtmosphereMedium* medium, double height, double out[3])
{
    out[0] = exp(-height / WORLD_ATMOSPHERE_RAYLEIGH_HEIGHT);
    out[1] = exp(-height / Greater((double)medium->MieHeight, 1.0));
    out[2] = OzoneDensity(height);
}

/* The optical depth of a stretch @p length long between two points from their densities (MeanDensity; the ozone's tent
 * from its ends). */
static void StretchDepth(const WorldAtmosphereMedium* medium, const double densitiesA[3], const double densitiesB[3],
    double heightA, double heightB, double length, double out[3])
{
    double Rayleigh = MeanDensity(densitiesA[0], densitiesB[0], heightA, heightB, WORLD_ATMOSPHERE_RAYLEIGH_HEIGHT);
    double Mie = MeanDensity(densitiesA[1], densitiesB[1], heightA, heightB, Greater((double)medium->MieHeight, 1.0));
    double Ozone = 0.5 * (densitiesA[2] + densitiesB[2]);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        out[Channel] = ((double)medium->RayleighScattering[Channel] * Rayleigh
            + (double)medium->MieExtinction[Channel] * Mie + (double)medium->OzoneAbsorption[Channel] * Ozone) * length;
    }
}

/* Rayleigh's phase function and Cornette and Shanks's, and Henyey and Greenstein's (the haze's quadrature's proposal),
 * for the cosine between the view and the light. */
static double RayleighPhase(double cosAngle)
{
    return 3.0 / (16.0 * SKY_LIGHT_PI) * (1.0 + cosAngle * cosAngle);
}

static double CornetteShanksPhase(double cosAngle, double g)
{
    double G2 = g * g;
    return 3.0 / (8.0 * SKY_LIGHT_PI) * ((1.0 - G2) * (cosAngle * cosAngle + 1.0))
        / (pow(1.0 + G2 - 2.0 * cosAngle * g, 1.5) * (2.0 + G2));
}

static double HenyeyGreensteinPhase(double cosAngle, double g)
{
    double G2 = g * g;
    return (1.0 - G2) / (4.0 * SKY_LIGHT_PI * pow(1.0 + G2 - 2.0 * g * cosAngle, 1.5));
}

/* The cosine from the light at a stratified quantile of Henyey and Greenstein's phase function: its inverse CDF. */
static double HenyeyGreensteinCos(double quantile, double g)
{
    if (fabs(g) < SKY_LIGHT_ISOTROPIC_LIMIT)
    {
        return 1.0 - 2.0 * quantile;
    }
    double Q = (1.0 - g * g) / (1.0 - g + 2.0 * g * quantile);
    return fmin(fmax((1.0 + g * g - Q * Q) / (2.0 * g), -1.0), 1.0);
}

static void ColorToLinear(Color color, float out[3])
{
    out[0] = powf((float)color.r / 255.0f, SKY_LIGHT_SRGB_GAMMA);
    out[1] = powf((float)color.g / 255.0f, SKY_LIGHT_SRGB_GAMMA);
    out[2] = powf((float)color.b / 255.0f, SKY_LIGHT_SRGB_GAMMA);
}

static float Luminance(const float rgb[3])
{
    return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}

/* Hermite smoothstep: 0 at or below edge0, 1 at or above edge1 (edge0 < edge1); NaN gives 0. */
static float SmoothStep(float edge0, float edge1, float value)
{
    float T = (float)ClampUnit((double)((value - edge0) / (edge1 - edge0)));
    return T * T * (3.0f - 2.0f * T);
}


// Static functions: the exact transmittance.
/* Adds the ray's distance @p distance to @p breaks when it lies strictly inside (0, @p length). */
static void AddBreak(double distance, double length, double breaks[SKY_LIGHT_MAX_BREAKS], int* count)
{
    if ((distance > 0.0) && (distance < length) && (*count < SKY_LIGHT_MAX_BREAKS))
    {
        breaks[(*count)++] = distance;
    }
}

/* Adds where a ray crosses a height: r(t) = R + height, t = -r mu +- sqrt(r^2 mu^2 - (r^2 - (R + height)^2)), with
 * r^2 - (R + height)^2 taken as a product. */
static void AddCrossings(WorldAtmosphereRay ray, double height, double length, double breaks[SKY_LIGHT_MAX_BREAKS],
    int* count)
{
    double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + ray.Height;
    double Discriminant = Radius * Radius * ray.CosZenith * ray.CosZenith
        - (ray.Height - height) * (2.0 * WORLD_ATMOSPHERE_PLANET_RADIUS + ray.Height + height);
    if (!(Discriminant > 0.0))
    {
        return;
    }
    double Root = sqrt(Discriminant);
    AddBreak(-Radius * ray.CosZenith - Root, length, breaks, count);
    AddBreak(-Radius * ray.CosZenith + Root, length, breaks, count);
}

static void SortBreaks(double breaks[SKY_LIGHT_MAX_BREAKS], int count)
{
    for (int Index = 1; Index < count; Index++)
    {
        double Value = breaks[Index];
        int Hole = Index;
        while ((Hole > 0) && (breaks[Hole - 1] > Value))
        {
            breaks[Hole] = breaks[Hole - 1];
            Hole--;
        }
        breaks[Hole] = Value;
    }
}


// Static functions: the C tables.
/* A table's lower row and the share of the way to the next for a unit coordinate (texel centres from 0 to 1). */
static void GetRow(double unit, int rows, int* outRow, double* outShare)
{
    double Y = ClampUnit(unit) * (double)(rows - 1);
    int Row = (int)Y;
    if (Row > rows - 2)
    {
        Row = rows - 2;
    }
    *outRow = Row;
    *outShare = Y - (double)Row;
}

/* What a point's lookups need that does not depend on the light (TablePlace). */
static void PreparePlace(double height, TablePlace* out)
{
    double Height = Greater(height, 0.0);
    double HorizonToTop = sqrt((WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS)
        * (WORLD_ATMOSPHERE_TOP_RADIUS + WORLD_ATMOSPHERE_PLANET_RADIUS));
    double HorizonDistance = sqrt(Height * (2.0 * WORLD_ATMOSPHERE_PLANET_RADIUS + Height));
    double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + Height;
    out->Height = Height;
    out->Radius = Radius;
    out->HorizonCos = -HorizonDistance / Radius;
    out->TopSquared = (WORLD_ATMOSPHERE_TOP_RADIUS - Radius) * (WORLD_ATMOSPHERE_TOP_RADIUS + Radius);
    out->MinToTop = (WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS) - Height;
    out->InverseToTopSpan = 1.0 / ((HorizonDistance + HorizonToTop) - out->MinToTop);
    GetRow(HorizonDistance / HorizonToTop, WORLD_SKY_LIGHT_TRANSMITTANCE_HEIGHT, &out->TransmittanceRow,
        &out->TransmittanceRowShare);
    GetRow(Height / (WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS), WORLD_SKY_LIGHT_MULTISCATTER_SIZE,
        &out->MultiscatterRow, &out->MultiscatterRowShare);
}

/* A bilinear read of a table's RGB texels at a unit coordinate across, between a row and the next. */
static void SampleRows(const float* table, int width, double unitAcross, int row, double rowShare, double out[3])
{
    double X = ClampUnit(unitAcross) * (double)(width - 1);
    int Column = (int)X;
    if (Column > width - 2)
    {
        Column = width - 2;
    }
    double Share = X - (double)Column;
    const float* A = table + ((size_t)row * (size_t)width + (size_t)Column) * 3U;
    const float* C = A + (size_t)width * 3U;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        double Lower = (double)A[Channel] + ((double)A[Channel + 3] - (double)A[Channel]) * Share;
        double Upper = (double)C[Channel] + ((double)C[Channel + 3] - (double)C[Channel]) * Share;
        out[Channel] = Lower + (Upper - Lower) * rowShare;
    }
}

/* A light's light reaching a point per unit of it above the air, from the transmittance table, times the light's disc
 * share above the planet's horizon (the shaders' atmosphereLightTransmittance). */
static void LookUpLightAt(const WorldSkyLightTables* tables, const TablePlace* place, double cosZenith, double out[3])
{
    double X = ClampUnit((cosZenith - place->HorizonCos + 0.5 * WORLD_ATMOSPHERE_LIGHT_DIAMETER)
        / WORLD_ATMOSPHERE_LIGHT_DIAMETER);
    double Share = X * X * (3.0 - 2.0 * X);
    if (!(Share > 0.0))
    {
        out[0] = out[1] = out[2] = 0.0;
        return;
    }
    double ToTop = Greater(-place->Radius * cosZenith
        + sqrt(Greater(place->TopSquared + place->Radius * place->Radius * cosZenith * cosZenith, 0.0)), 0.0);
    SampleRows(&tables->Transmittance[0][0][0], WORLD_SKY_LIGHT_TRANSMITTANCE_WIDTH,
        (ToTop - place->MinToTop) * place->InverseToTopSpan, place->TransmittanceRow, place->TransmittanceRowShare, out);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        out[Channel] *= Share;
    }
}

/* The multiple scattering table's glow at a point for a light at a zenith cosine there. */
static void LookUpGlowAt(const WorldSkyLightTables* tables, const TablePlace* place, double cosZenith, double out[3])
{
    SampleRows(&tables->Multiscatter[0][0][0], WORLD_SKY_LIGHT_MULTISCATTER_SIZE, 0.5 * cosZenith + 0.5,
        place->MultiscatterRow, place->MultiscatterRowShare, out);
}

/* One texel of the transmittance table: the optical depth from its ray's start to the top in
 * WORLD_SKY_LIGHT_TRANSMITTANCE_STEPS steps growing with the square of their index, each the mean densities over its
 * heights. */
static void BuildTransmittanceTexel(const WorldAtmosphereMedium* medium, WorldAtmosphereRay ray, float out[3])
{
    double Length = WorldAtmosphere_GetDistanceToTop(ray);
    double Depth[3] = { 0.0, 0.0, 0.0 };
    double Steps = (double)WORLD_SKY_LIGHT_TRANSMITTANCE_STEPS;
    double FromHeight = fmax(ray.Height, 0.0);
    double FromDensities[3];
    Densities(medium, FromHeight, FromDensities);
    for (int Step = 0; Step < WORLD_SKY_LIGHT_TRANSMITTANCE_STEPS; Step++)
    {
        double From = (double)Step / Steps;
        double To = (double)(Step + 1) / Steps;
        double ToHeight = fmax(WorldAtmosphere_GetHeightAt(ray, Length * To * To), 0.0);
        double ToDensities[3];
        Densities(medium, ToHeight, ToDensities);
        double StepDepth[3];
        StretchDepth(medium, FromDensities, ToDensities, FromHeight, ToHeight, Length * (To * To - From * From),
            StepDepth);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            Depth[Channel] += StepDepth[Channel];
            FromDensities[Channel] = ToDensities[Channel];
        }
        FromHeight = ToHeight;
    }
    for (int Channel = 0; Channel < 3; Channel++)
    {
        out[Channel] = (float)exp(-Depth[Channel]);
    }
}

/* One row of the transmittance table. */
static void BuildTransmittanceRow(const WorldAtmosphereMedium* medium, int row,
    float out[WORLD_SKY_LIGHT_TRANSMITTANCE_WIDTH][3])
{
    for (int Column = 0; Column < WORLD_SKY_LIGHT_TRANSMITTANCE_WIDTH; Column++)
    {
        WorldAtmosphereRay Texel = WorldAtmosphere_GetTransmittanceUnitRay(Vector2Double_Create(
            (double)Column / (double)(WORLD_SKY_LIGHT_TRANSMITTANCE_WIDTH - 1),
            (double)row / (double)(WORLD_SKY_LIGHT_TRANSMITTANCE_HEIGHT - 1)));
        BuildTransmittanceTexel(medium, Texel, out[Column]);
    }
}

static void MarchMultiscatterDirection(const WorldAtmosphereMedium* medium, double height, double cosZenith,
    MultiscatterDirection* out)
{
    WorldAtmosphereRay Path = { .Height = height, .CosZenith = cosZenith };
    out->CosZenith = cosZenith;
    out->SinZenith = sqrt(fmax(1.0 - cosZenith * cosZenith, 0.0));
    double PathLength = WorldAtmosphere_GetDistanceToTop(Path);
    double ToGround = WorldAtmosphere_GetDistanceToGround(Path);
    out->IsGroundHit = ToGround >= 0.0;
    if (out->IsGroundHit)
    {
        PathLength = fmin(PathLength, ToGround);
    }
    out->PathLength = PathLength;
    double Steps = (double)WORLD_SKY_LIGHT_MULTISCATTER_STEPS;
    double Depth[3] = { 0.0, 0.0, 0.0 };
    out->Transfer[0] = out->Transfer[1] = out->Transfer[2] = 0.0;
    for (int Step = 0; Step < WORLD_SKY_LIGHT_MULTISCATTER_STEPS; Step++)
    {
        double From = (double)Step / Steps;
        double To = (double)(Step + 1) / Steps;
        double StepLength = PathLength * (To * To - From * From);
        double Distance = PathLength * From * From + 0.5 * StepLength;
        double PointHeight = WorldAtmosphere_GetHeightAt(Path, Distance);
        double Air[3];
        Densities(medium, fmax(PointHeight, 0.0), Air);
        out->Distance[Step] = Distance;
        PreparePlace(PointHeight, &out->Place[Step]);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            double Scattering = (double)medium->RayleighScattering[Channel] * Air[0]
                + (double)medium->MieScattering[Channel] * Air[1];
            double ExtinctionValue = (double)medium->RayleighScattering[Channel] * Air[0]
                + (double)medium->MieExtinction[Channel] * Air[1] + (double)medium->OzoneAbsorption[Channel] * Air[2];
            double StepDepth = ExtinctionValue * StepLength;
            double Through = exp(-Depth[Channel]) * StepLength * ExponentialShare(StepDepth);
            out->Through[Step][Channel] = Through * Scattering;
            out->Transfer[Channel] += Through * Scattering;
            Depth[Channel] += StepDepth;
        }
    }
    for (int Channel = 0; Channel < 3; Channel++)
    {
        out->GroundThrough[Channel] = exp(-Depth[Channel]) * fmax((double)medium->GroundAlbedo[Channel], 0.0)
            / SKY_LIGHT_PI;
    }
}

/* One row of the multiple scattering table (one height): its directions marched once, then each light zenith's second
 * order over them, and the geometric series of the higher orders (the GPU's algorithm). */
static void BuildMultiscatterRow(const WorldAtmosphereMedium* medium, const WorldSkyLightTables* tables, int row,
    float out[WORLD_SKY_LIGHT_MULTISCATTER_SIZE][3])
{
    enum { DirectionCount = WORLD_SKY_LIGHT_MULTISCATTER_SQRT_DIRECTIONS };
    double Height = (double)row / (double)(WORLD_SKY_LIGHT_MULTISCATTER_SIZE - 1)
        * (WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS);
    MultiscatterDirection Directions[DirectionCount];
    double Transfer[3] = { 0.0, 0.0, 0.0 };
    for (int Zenith = 0; Zenith < DirectionCount; Zenith++)
    {
        double CosZenith = 1.0 - 2.0 * ((double)Zenith + 0.5) / (double)DirectionCount;
        MarchMultiscatterDirection(medium, Height, CosZenith, &Directions[Zenith]);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            Transfer[Channel] += Directions[Zenith].Transfer[Channel] * (double)DirectionCount; // every azimuth's
        }
    }
    TablePlace Ground;
    PreparePlace(0.0, &Ground);
    double Count = (double)(DirectionCount * DirectionCount);
    double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + Height;
    for (int Column = 0; Column < WORLD_SKY_LIGHT_MULTISCATTER_SIZE; Column++)
    {
        double LightCosZenith = 2.0 * (double)Column / (double)(WORLD_SKY_LIGHT_MULTISCATTER_SIZE - 1) - 1.0;
        double LightSinZenith = sqrt(fmax(1.0 - LightCosZenith * LightCosZenith, 0.0));
        double Second[3] = { 0.0, 0.0, 0.0 };
        // The azimuths come in pairs with the same cosine (the light lies in the plane of azimuth 0).
        for (int Azimuth = 0; Azimuth < DirectionCount / 2; Azimuth++)
        {
            double CosAzimuth = cos(2.0 * SKY_LIGHT_PI * ((double)Azimuth + 0.5) / (double)DirectionCount);
            for (int Zenith = 0; Zenith < DirectionCount; Zenith++)
            {
                const MultiscatterDirection* Direction = &Directions[Zenith];
                double CosLightAngle = CosAzimuth * Direction->SinZenith * LightSinZenith
                    + Direction->CosZenith * LightCosZenith;
                for (int Step = 0; Step < WORLD_SKY_LIGHT_MULTISCATTER_STEPS; Step++)
                {
                    const TablePlace* Place = &Direction->Place[Step];
                    double LightCos = ClampCos((Radius * LightCosZenith + Direction->Distance[Step] * CosLightAngle)
                        / Place->Radius);
                    double Light[3];
                    LookUpLightAt(tables, Place, LightCos, Light);
                    for (int Channel = 0; Channel < 3; Channel++)
                    {
                        Second[Channel] += 2.0 * Direction->Through[Step][Channel] * Light[Channel]
                            / (4.0 * SKY_LIGHT_PI);
                    }
                }
                if (Direction->IsGroundHit)
                {
                    // The ground reflects the light that reaches it, evenly (a Lambertian surface of the ground's albedo).
                    double GroundCos = fmin(fmax((Radius * LightCosZenith + Direction->PathLength * CosLightAngle)
                        / WORLD_ATMOSPHERE_PLANET_RADIUS, -1.0), 1.0);
                    double Light[3];
                    LookUpLightAt(tables, &Ground, GroundCos, Light);
                    for (int Channel = 0; Channel < 3; Channel++)
                    {
                        Second[Channel] += 2.0 * Direction->GroundThrough[Channel] * Light[Channel] * fmax(GroundCos, 0.0);
                    }
                }
            }
        }
        for (int Channel = 0; Channel < 3; Channel++)
        {
            out[Column][Channel] = (float)((Second[Channel] / Count) / fmax(1.0 - Transfer[Channel] / Count, 1e-3));
        }
    }
}


// Static functions: the sky's irradiance.
/* Where stretch @p stretch of a direction @p length long in @p stretches stretches starts: they grow with the square of
 * their index (stretch @p stretches starts at the end). */
static double GetStretchStart(double length, int stretch, int stretches)
{
    double Unit = (double)stretch / (double)stretches;
    return length * Unit * Unit;
}

/* The distance along a ray, climbing from @p height with the zenith cosine @p cosZenith (> 0), to @p rise metres above
 * its start: -r mu + sqrt(r^2 mu^2 + 2 r rise + rise^2). */
static double GetDistanceToRise(double height, double cosZenith, double rise)
{
    double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + height;
    return -Radius * cosZenith + sqrt(Radius * Radius * cosZenith * cosZenith + rise * (2.0 * Radius + rise));
}

/* The points of one direction of the irradiance quadrature from a surface at @p height, @p length long: Simpson's
 * stretches of t = L u^2 (their ends and middles), the air's scattering at each, and Simpson's weight times the view's
 * transmittance to it (its optical depth exact for the exponential densities between the points). */
static void BuildRay(const WorldAtmosphereMedium* medium, double height, double cosZenith, double length,
    int stretches, IrradianceRay* out)
{
    WorldAtmosphereRay View = { .Height = height, .CosZenith = cosZenith };
    out->CosZenith = cosZenith;
    out->SinZenith = sqrt(fmax(1.0 - cosZenith * cosZenith, 0.0));
    out->PointCount = 2 * stretches + 1;
    double Transmittance[3] = { 1.0, 1.0, 1.0 };
    double PreviousDistance = 0.0;
    double PreviousHeight = 0.0;
    double PreviousDensities[3] = { 0.0, 0.0, 0.0 };
    for (int Index = 0; Index < out->PointCount; Index++)
    {
        // Point 2i is stretch i's start, 2i + 1 its middle (in t), 2i + 2 its end and stretch i + 1's start. Simpson's
        // weights: a middle 4/6 of its stretch, an end 1/6 of each stretch it ends or starts.
        int Stretch = Index / 2;
        double From = GetStretchStart(length, Stretch, stretches);
        double To = GetStretchStart(length, Stretch + 1, stretches);
        double Distance = ((Index % 2) == 0) ? From : 0.5 * (From + To);
        double Weight;
        if ((Index % 2) == 1)
        {
            Weight = 4.0 * (To - From) / 6.0;
        }
        else
        {
            double Before = (Stretch > 0) ? (From - GetStretchStart(length, Stretch - 1, stretches)) / 6.0 : 0.0;
            double After = (Stretch < stretches) ? (To - From) / 6.0 : 0.0;
            Weight = Before + After;
        }
        double PointHeight = Greater((Index == 0) ? height : WorldAtmosphere_GetHeightAt(View, Distance), 0.0);
        double Air[3];
        Densities(medium, PointHeight, Air);
        if (Index > 0)
        {
            double Depth[3];
            StretchDepth(medium, PreviousDensities, Air, PreviousHeight, PointHeight, Distance - PreviousDistance, Depth);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                Transmittance[Channel] *= exp(-Depth[Channel]);
            }
        }
        IrradiancePoint* Point = &out->Points[Index];
        Point->Distance = Distance;
        PreparePlace(PointHeight, &Point->Place);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            Point->Rayleigh[Channel] = (double)medium->RayleighScattering[Channel] * Air[0];
            Point->Mie[Channel] = (double)medium->MieScattering[Channel] * Air[1];
            Point->Weight[Channel] = Weight * Transmittance[Channel];
            PreviousDensities[Channel] = Air[Channel];
        }
        PreviousDistance = Distance;
        PreviousHeight = PointHeight;
    }
}

/* The single scattering along one direction from a light at the zenith cosine @p lightCosZenith at the surface
 * (radius @p radius from the planet's centre), @p cosLightAngle the cosine between them: the air's Rayleigh scattering
 * times @p rayleighPhase and the haze's times @p miePhase, through the light's transmittance. */
static void RayLight(const WorldSkyLightTables* tables, const IrradianceRay* ray, double radius, double lightCosZenith,
    double cosLightAngle, double rayleighPhase, double miePhase, double out[3])
{
    out[0] = out[1] = out[2] = 0.0;
    for (int Index = 0; Index < ray->PointCount; Index++)
    {
        const IrradiancePoint* Point = &ray->Points[Index];
        double LightCos = ClampCos((radius * lightCosZenith + Point->Distance * cosLightAngle) / Point->Place.Radius);
        double Light[3];
        LookUpLightAt(tables, &Point->Place, LightCos, Light);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            out[Channel] += Point->Weight[Channel]
                * (Point->Rayleigh[Channel] * rayleighPhase + Point->Mie[Channel] * miePhase) * Light[Channel];
        }
    }
}

/* The multiple scattering's glow along one direction, its light's zenith cosine at each point taken for the mean of the
 * ring's azimuths (the cosine between the direction and the light averages cosZenith x lightCosZenith over them; the
 * glow changes slowly with it), so one ring takes it once. */
static void RayGlow(const WorldSkyLightTables* tables, const IrradianceRay* ray, double radius, double lightCosZenith,
    double out[3])
{
    out[0] = out[1] = out[2] = 0.0;
    double MeanCosLightAngle = ray->CosZenith * lightCosZenith;
    for (int Index = 0; Index < ray->PointCount; Index++)
    {
        const IrradiancePoint* Point = &ray->Points[Index];
        double LightCos = ClampCos((radius * lightCosZenith + Point->Distance * MeanCosLightAngle) / Point->Place.Radius);
        double Glow[3];
        LookUpGlowAt(tables, &Point->Place, LightCos, Glow);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            out[Channel] += Point->Weight[Channel] * (Point->Rayleigh[Channel] + Point->Mie[Channel]) * Glow[Channel];
        }
    }
}

/* The air's own part of the irradiance for one light over the zenith rings and azimuths (Rayleigh and the multiple
 * scattering; the rays are the caller's, shared by every light). */
static void IrradianceOfTheAir(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium,
    const IrradianceRay rays[WORLD_SKY_LIGHT_ZENITH_NODES], double height, double lightCosZenith, double out[3])
{
    out[0] = out[1] = out[2] = 0.0;
    double LightSin = sqrt(fmax(1.0 - lightCosZenith * lightCosZenith, 0.0));
    double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + height;
    double Share = ClampUnit((double)medium->MultipleScattering);
    for (int Ring = 0; Ring < WORLD_SKY_LIGHT_ZENITH_NODES; Ring++)
    {
        const IrradianceRay* View = &rays[Ring];
        // Gauss-Legendre over the zenith cosine in [0, 1] of mu L, times the azimuths over half a turn (doubled).
        double RingWeight = 0.5 * GaussWeights6[Ring] * View->CosZenith * 2.0 * SKY_LIGHT_PI;
        double Weight = RingWeight / (double)WORLD_SKY_LIGHT_AZIMUTHS;
        for (int Azimuth = 0; Azimuth < WORLD_SKY_LIGHT_AZIMUTHS; Azimuth++)
        {
            double Angle = SKY_LIGHT_PI * ((double)Azimuth + 0.5) / (double)WORLD_SKY_LIGHT_AZIMUTHS;
            double CosLightAngle = View->SinZenith * LightSin * cos(Angle) + View->CosZenith * lightCosZenith;
            double Light[3];
            RayLight(tables, View, Radius, lightCosZenith, CosLightAngle, RayleighPhase(CosLightAngle), 0.0, Light);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                out[Channel] += Weight * Light[Channel];
            }
        }
        if (Share > 0.0)
        {
            double Glow[3];
            RayGlow(tables, View, Radius, lightCosZenith, Glow);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                out[Channel] += RingWeight * Share * Glow[Channel];
            }
        }
    }
}

/* The haze's single scattering part of the irradiance for one light: directions around the light by Henyey and
 * Greenstein's inverse with the haze's forward scattering, weighted by Cornette and Shanks's over it, the upper half of
 * the sky only; each up to WORLD_SKY_LIGHT_MIE_DEPTH of the haze's scale heights above the surface. */
static void IrradianceOfTheHaze(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium, double height,
    double lightCosZenith, double out[3])
{
    out[0] = out[1] = out[2] = 0.0;
    double G = fmin(fmax((double)medium->MieAnisotropy, 0.0), WORLD_ENVIRONMENT_MAX_SUN_GLOW);
    double LightSin = sqrt(fmax(1.0 - lightCosZenith * lightCosZenith, 0.0));
    double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + height;
    double Rise = WORLD_SKY_LIGHT_MIE_DEPTH * fmax((double)medium->MieHeight, 1.0);
    double Count = (double)(WORLD_SKY_LIGHT_MIE_RINGS * WORLD_SKY_LIGHT_MIE_AZIMUTHS);
    for (int Ring = 0; Ring < WORLD_SKY_LIGHT_MIE_RINGS; Ring++)
    {
        double CosAngle = HenyeyGreensteinCos(((double)Ring + 0.5) / (double)WORLD_SKY_LIGHT_MIE_RINGS, G);
        double SinAngle = sqrt(fmax(1.0 - CosAngle * CosAngle, 0.0));
        double PhaseWeight = CornetteShanksPhase(CosAngle, G) / HenyeyGreensteinPhase(CosAngle, G);
        for (int Azimuth = 0; Azimuth < WORLD_SKY_LIGHT_MIE_AZIMUTHS; Azimuth++)
        {
            // Around the light, from its vertical plane toward the zenith's side (symmetric: half a turn).
            double Angle = SKY_LIGHT_PI * ((double)Azimuth + 0.5) / (double)WORLD_SKY_LIGHT_MIE_AZIMUTHS;
            double CosZenith = CosAngle * lightCosZenith + SinAngle * cos(Angle) * LightSin;
            if (!(CosZenith > 0.0))
            {
                continue;
            }
            double Length = fmin(WorldAtmosphere_GetDistanceToTop((WorldAtmosphereRay){ .Height = height,
                .CosZenith = CosZenith }), GetDistanceToRise(height, CosZenith, Rise));
            IrradianceRay View;
            BuildRay(medium, height, CosZenith, Length, WORLD_SKY_LIGHT_MIE_STEPS, &View);
            double Light[3];
            RayLight(tables, &View, Radius, lightCosZenith, CosAngle, 0.0, 1.0, Light);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                out[Channel] += PhaseWeight * CosZenith * Light[Channel] / Count;
            }
        }
    }
}

/* The zenith rings' rays from a surface at @p height, each to the top of the atmosphere (shared by the sun and the moon). */
static void BuildZenithRays(const WorldAtmosphereMedium* medium, double height,
    IrradianceRay out[WORLD_SKY_LIGHT_ZENITH_NODES])
{
    for (int Ring = 0; Ring < WORLD_SKY_LIGHT_ZENITH_NODES; Ring++)
    {
        double CosZenith = 0.5 * (GaussNodes6[Ring] + 1.0);
        BuildRay(medium, height, CosZenith, WorldAtmosphere_GetDistanceToTop((WorldAtmosphereRay){ .Height = height,
            .CosZenith = CosZenith }), WORLD_SKY_LIGHT_STEPS, &out[Ring]);
    }
}

/* The irradiance of one light from the zenith rays and the haze's directions, per unit of it above the air. */
static void IrradianceOfLight(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium,
    const IrradianceRay rays[WORLD_SKY_LIGHT_ZENITH_NODES], double height, double lightCosZenith, double out[3])
{
    out[0] = out[1] = out[2] = 0.0;
    if (!(lightCosZenith >= sin(WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION)))
    {
        return;
    }
    double Air[3];
    double Haze[3];
    IrradianceOfTheAir(tables, medium, rays, height, lightCosZenith, Air);
    IrradianceOfTheHaze(tables, medium, height, lightCosZenith, Haze);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        out[Channel] = Air[Channel] + Haze[Channel];
    }
}

/* Light pollution's irradiance (its light fills the low air around the camera, scattered evenly): the zenith rings
 * without azimuths, each ray up to WORLD_SKY_LIGHT_LIGHT_POLLUTION_DEPTH of the share's falloff lengths along it (the
 * light falls off within a few hundred metres up; to the top, the first stretches would overshoot it). */
static void LightPollutionIrradiance(const WorldAtmosphereMedium* medium, double height, double out[3])
{
    out[0] = out[1] = out[2] = 0.0;
    for (int Ring = 0; Ring < WORLD_SKY_LIGHT_ZENITH_NODES; Ring++)
    {
        double CosZenith = 0.5 * (GaussNodes6[Ring] + 1.0);
        double SinZenith = sqrt(fmax(1.0 - CosZenith * CosZenith, 0.0));
        double Falloff = 1.0 / (CosZenith / WORLD_ATMOSPHERE_LIGHT_POLLUTION_HEIGHT
            + SinZenith / WORLD_ATMOSPHERE_LIGHT_POLLUTION_REACH);
        double Length = fmin(WorldAtmosphere_GetDistanceToTop((WorldAtmosphereRay){ .Height = height,
            .CosZenith = CosZenith }), WORLD_SKY_LIGHT_LIGHT_POLLUTION_DEPTH * Falloff);
        IrradianceRay Path;
        BuildRay(medium, height, CosZenith, Length, WORLD_SKY_LIGHT_STEPS, &Path);
        const IrradianceRay* View = &Path;
        double Weight = 0.5 * GaussWeights6[Ring] * View->CosZenith * 2.0 * SKY_LIGHT_PI;
        for (int Index = 0; Index < View->PointCount; Index++)
        {
            const IrradiancePoint* Point = &View->Points[Index];
            double Share = WorldAtmosphere_GetLightPollutionShare(Point->Place.Height, Point->Distance * View->SinZenith);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                out[Channel] += Weight * Point->Weight[Channel] * (Point->Rayleigh[Channel] + Point->Mie[Channel]) * Share;
            }
        }
    }
}


// Static functions: the irradiance table.
/* A texel of the irradiance table: the natural logarithm of each colour's value, at least the floor (no light, or a NaN
 * from absurd air, gives the floor: never log(0)). */
static void StoreLogarithms(const double value[3], float out[3])
{
    for (int Channel = 0; Channel < 3; Channel++)
    {
        out[Channel] = (value[Channel] > exp(WORLD_ATMOSPHERE_IRRADIANCE_LOG_FLOOR))
            ? (float)log(value[Channel]) : (float)WORLD_ATMOSPHERE_IRRADIANCE_LOG_FLOOR;
    }
}

/* The height a row of the irradiance table holds (WorldAtmosphere_GetIrradianceRay's). */
static double GetIrradianceRowHeight(int row)
{
    return WorldAtmosphere_GetIrradianceRay(Vector2Double_Create(0.0,
        (double)row / (double)(WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT - 1))).Height;
}

/* One part of a row of the irradiance table: WORLD_ATMOSPHERE_IRRADIANCE_WIDTH / WORLD_SKY_LIGHT_IRRADIANCE_ROW_PARTS of
 * its lights' columns (the GPU's texels: the light's elevation even in angle). */
static void BuildIrradiancePart(const WorldAtmosphereMedium* medium, WorldSkyLightTables* tables, int row, int part)
{
    enum { ColumnsPerPart = WORLD_ATMOSPHERE_IRRADIANCE_WIDTH / WORLD_SKY_LIGHT_IRRADIANCE_ROW_PARTS };
    _Static_assert((WORLD_ATMOSPHERE_IRRADIANCE_WIDTH % WORLD_SKY_LIGHT_IRRADIANCE_ROW_PARTS) == 0,
        "the parts cover the row");
    double Height = GetIrradianceRowHeight(row);
    IrradianceRay Rays[WORLD_SKY_LIGHT_ZENITH_NODES];
    BuildZenithRays(medium, Height, Rays);
    for (int Column = part * ColumnsPerPart; Column < (part + 1) * ColumnsPerPart; Column++)
    {
        WorldAtmosphereRay Texel = WorldAtmosphere_GetIrradianceRay(Vector2Double_Create(
            (double)Column / (double)(WORLD_ATMOSPHERE_IRRADIANCE_WIDTH - 1), 0.0));
        double Irradiance[3];
        IrradianceOfLight(tables, medium, Rays, Height, Texel.CosZenith, Irradiance);
        StoreLogarithms(Irradiance, tables->Irradiance[row][Column]);
    }
}

/* The irradiance table's last column: light pollution's irradiance at each row's height. */
static void BuildLightPollutionColumn(const WorldAtmosphereMedium* medium, WorldSkyLightTables* tables)
{
    for (int Row = 0; Row < WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT; Row++)
    {
        double Irradiance[3];
        LightPollutionIrradiance(medium, GetIrradianceRowHeight(Row), Irradiance);
        StoreLogarithms(Irradiance, tables->Irradiance[Row][WORLD_ATMOSPHERE_IRRADIANCE_WIDTH]);
    }
}

/* The irradiance table's value at a texel coordinate (@p across from 0 to @p lastColumn's columns, @p up from 0 to the
 * last row; clamped): bilinear in the logarithms, as the GPU's filtering reads the texture, then exp. */
static void SampleIrradiance(const WorldSkyLightTables* tables, double across, int firstColumn, int lastColumn,
    double up, double out[3])
{
    double X = fmin(fmax(across, 0.0), (double)(lastColumn - firstColumn));
    int Column = (int)X;
    if (Column > lastColumn - firstColumn - 1)
    {
        Column = (lastColumn > firstColumn) ? (lastColumn - firstColumn - 1) : 0;
    }
    double ColumnShare = X - (double)Column;
    int Next = (lastColumn > firstColumn) ? (Column + 1) : Column;
    double Y = fmin(fmax(up, 0.0), (double)(WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT - 1));
    int Row = (int)Y;
    if (Row > WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT - 2)
    {
        Row = WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT - 2;
    }
    double RowShare = Y - (double)Row;
    const float* A = tables->Irradiance[Row][firstColumn + Column];
    const float* B = tables->Irradiance[Row][firstColumn + Next];
    const float* C = tables->Irradiance[Row + 1][firstColumn + Column];
    const float* D = tables->Irradiance[Row + 1][firstColumn + Next];
    for (int Channel = 0; Channel < 3; Channel++)
    {
        double Lower = (double)A[Channel] + ((double)B[Channel] - (double)A[Channel]) * ColumnShare;
        double Upper = (double)C[Channel] + ((double)D[Channel] - (double)C[Channel]) * ColumnShare;
        out[Channel] = exp(Lower + (Upper - Lower) * RowShare);
    }
}


// Functions (declared in WorldSkyLight.h).
void WorldSkyLight_GetTransmittance(const WorldAtmosphereMedium* medium, WorldAtmosphereRay ray,
    double outTransmittance[3])
{
    // At the horizon for a ray below it (the table's horizon column), from at least the ground.
    double Height = fmax(ray.Height, 0.0);
    WorldAtmosphereRay Path = { .Height = Height,
        .CosZenith = fmax(fmin(ray.CosZenith, 1.0), WorldAtmosphere_GetHorizonCos(Height)) };
    double Length = WorldAtmosphere_GetDistanceToTop(Path);
    // The stretches: split at the ray's lowest point and where it crosses the ozone layer's corners.
    double Breaks[SKY_LIGHT_MAX_BREAKS];
    int BreakCount = 0;
    AddBreak(-(WORLD_ATMOSPHERE_PLANET_RADIUS + Height) * Path.CosZenith, Length, Breaks, &BreakCount);
    AddCrossings(Path, SKY_LIGHT_OZONE_LOW, Length, Breaks, &BreakCount);
    AddCrossings(Path, WORLD_ATMOSPHERE_OZONE_CENTER, Length, Breaks, &BreakCount);
    AddCrossings(Path, SKY_LIGHT_OZONE_HIGH, Length, Breaks, &BreakCount);
    SortBreaks(Breaks, BreakCount);
    double Depth[3] = { 0.0, 0.0, 0.0 };
    double From = 0.0;
    for (int Stretch = 0; Stretch <= BreakCount; Stretch++)
    {
        double To = (Stretch < BreakCount) ? Breaks[Stretch] : Length;
        double Half = 0.5 * (To - From);
        double Middle = 0.5 * (To + From);
        for (int Node = 0; Node < SKY_LIGHT_GAUSS_NODES; Node++)
        {
            double Value[3];
            Extinction(medium, WorldAtmosphere_GetHeightAt(Path, Middle + Half * GaussNodes16[Node]), Value);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                Depth[Channel] += Half * GaussWeights16[Node] * Value[Channel];
            }
        }
        From = To;
    }
    for (int Channel = 0; Channel < 3; Channel++)
    {
        outTransmittance[Channel] = exp(-Depth[Channel]);
    }
}

double WorldSkyLight_GetDiscShare(WorldAtmosphereRay ray)
{
    double Above = ray.CosZenith - WorldAtmosphere_GetHorizonCos(ray.Height);
    double X = ClampUnit((Above + 0.5 * WORLD_ATMOSPHERE_LIGHT_DIAMETER) / WORLD_ATMOSPHERE_LIGHT_DIAMETER);
    return X * X * (3.0 - 2.0 * X);
}

void WorldSkyLight_GetLightTransmittance(const WorldAtmosphereMedium* medium, WorldAtmosphereRay ray,
    double outTransmittance[3])
{
    double Share = WorldSkyLight_GetDiscShare(ray);
    if (!(Share > 0.0))
    {
        outTransmittance[0] = outTransmittance[1] = outTransmittance[2] = 0.0;
        return;
    }
    WorldSkyLight_GetTransmittance(medium, ray, outTransmittance);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        outTransmittance[Channel] *= Share;
    }
}

void WorldSkyLight_BuildTables(const WorldAtmosphereMedium* medium, WorldSkyLightTables* outTables)
{
    for (int Step = 0; Step < WORLD_SKY_LIGHT_BUILD_STEP_COUNT; Step++)
    {
        WorldSkyLight_BuildTablesStep(medium, outTables, Step);
    }
}

void WorldSkyLight_BuildTablesStep(const WorldAtmosphereMedium* medium, WorldSkyLightTables* tables, int step)
{
    int Step = step;
    if (Step < 0)
    {
        return;
    }
    if (Step < WORLD_SKY_LIGHT_TRANSMITTANCE_HEIGHT)
    {
        BuildTransmittanceRow(medium, Step, tables->Transmittance[Step]);
        return;
    }
    Step -= WORLD_SKY_LIGHT_TRANSMITTANCE_HEIGHT;
    if (Step < WORLD_SKY_LIGHT_MULTISCATTER_SIZE)
    {
        BuildMultiscatterRow(medium, tables, Step, tables->Multiscatter[Step]);
        return;
    }
    Step -= WORLD_SKY_LIGHT_MULTISCATTER_SIZE;
    if (Step < WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT * WORLD_SKY_LIGHT_IRRADIANCE_ROW_PARTS)
    {
        BuildIrradiancePart(medium, tables, Step / WORLD_SKY_LIGHT_IRRADIANCE_ROW_PARTS,
            Step % WORLD_SKY_LIGHT_IRRADIANCE_ROW_PARTS);
        return;
    }
    if (Step == WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT * WORLD_SKY_LIGHT_IRRADIANCE_ROW_PARTS)
    {
        BuildLightPollutionColumn(medium, tables);
    }
}

void WorldSkyLight_ConstructBuilder(WorldSkyLightBuilder* self)
{
    *self = (WorldSkyLightBuilder){ 0 };
    self->Tables = Memory_Allocate(sizeof(WorldSkyLightTables));
    self->Spare = Memory_Allocate(sizeof(WorldSkyLightTables));
}

void WorldSkyLight_DeconstructBuilder(WorldSkyLightBuilder* self)
{
    Memory_Free(self->Tables);
    Memory_Free(self->Spare);
    *self = (WorldSkyLightBuilder){ 0 };
}

void WorldSkyLight_PrepareBuilder(WorldSkyLightBuilder* self, const WorldAtmosphereMedium* medium)
{
    self->IsBuilding = false;
    if (self->Record.IsBuilt && !WorldAtmosphere_IsIrradianceStale(&self->Record.BuiltFor, medium))
    {
        return;
    }
    WorldSkyLight_BuildTables(medium, self->Tables);
    self->Record.BuiltFor = *medium;
    self->Record.IsBuilt = true;
    self->Record.BuildCount++;
}

bool WorldSkyLight_UpdateBuilder(WorldSkyLightBuilder* self, const WorldAtmosphereMedium* medium)
{
    if (!self->Record.IsBuilt)
    {
        WorldSkyLight_PrepareBuilder(self, medium);
        return false;
    }
    if (!self->IsBuilding && WorldAtmosphere_IsIrradianceStale(&self->Record.BuiltFor, medium))
    {
        self->BuildingFor = *medium;
        self->NextStep = 0;
        self->IsBuilding = true;
    }
    return self->IsBuilding;
}

bool WorldSkyLight_RunBuildStep(WorldSkyLightBuilder* self)
{
    if (!self->IsBuilding)
    {
        return false;
    }
    WorldSkyLight_BuildTablesStep(&self->BuildingFor, self->Spare, self->NextStep);
    self->NextStep++;
    if (self->NextStep < WORLD_SKY_LIGHT_BUILD_STEP_COUNT)
    {
        return true;
    }
    WorldSkyLightTables* Built = self->Spare;
    self->Spare = self->Tables;
    self->Tables = Built;
    self->Record.BuiltFor = self->BuildingFor;
    self->Record.BuildCount++;
    self->IsBuilding = false;
    return false;
}

void WorldSkyLight_LookUpLightTransmittance(const WorldSkyLightTables* tables, WorldAtmosphereRay ray,
    double outTransmittance[3])
{
    TablePlace Place;
    PreparePlace(ray.Height, &Place);
    LookUpLightAt(tables, &Place, fmin(fmax(ray.CosZenith, -1.0), 1.0), outTransmittance);
}

void WorldSkyLight_LookUpMultipleScattering(const WorldSkyLightTables* tables, WorldAtmosphereRay ray,
    double outLight[3])
{
    TablePlace Place;
    PreparePlace(ray.Height, &Place);
    LookUpGlowAt(tables, &Place, ray.CosZenith, outLight);
}

void WorldSkyLight_GetSkyIrradiance(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium,
    WorldAtmosphereRay ray, double outIrradiance[3])
{
    double Height = fmax(ray.Height, 0.0);
    IrradianceRay Rays[WORLD_SKY_LIGHT_ZENITH_NODES];
    BuildZenithRays(medium, Height, Rays);
    IrradianceOfLight(tables, medium, Rays, Height, fmin(fmax(ray.CosZenith, -1.0), 1.0), outIrradiance);
}

void WorldSkyLight_GetLightPollutionIrradiance(const WorldAtmosphereMedium* medium, double height,
    double outIrradiance[3])
{
    LightPollutionIrradiance(medium, fmax(height, 0.0), outIrradiance);
}

void WorldSkyLight_LookUpSkyIrradiance(const WorldSkyLightTables* tables, WorldAtmosphereRay ray, double outIrradiance[3])
{
    if (!(ray.CosZenith >= sin(WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION)))
    {
        outIrradiance[0] = outIrradiance[1] = outIrradiance[2] = 0.0;
        return;
    }
    Vector2Double Unit = WorldAtmosphere_GetIrradianceUnit(ray);
    SampleIrradiance(tables, Unit.X * (double)(WORLD_ATMOSPHERE_IRRADIANCE_WIDTH - 1), 0,
        WORLD_ATMOSPHERE_IRRADIANCE_WIDTH - 1, Unit.Y * (double)(WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT - 1), outIrradiance);
}

void WorldSkyLight_LookUpLightPollutionIrradiance(const WorldSkyLightTables* tables, double height,
    double outIrradiance[3])
{
    Vector2Double Unit = WorldAtmosphere_GetIrradianceUnit((WorldAtmosphereRay){ .Height = height, .CosZenith = 1.0 });
    SampleIrradiance(tables, 0.0, WORLD_ATMOSPHERE_IRRADIANCE_WIDTH, WORLD_ATMOSPHERE_IRRADIANCE_WIDTH,
        Unit.Y * (double)(WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT - 1), outIrradiance);
}

void WorldSkyLight_GetFrameLight(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium,
    const WorldEnvironment* environment, const WorldSkyLights* lights, const float lightPollution[3], double height,
    WorldSkyLightFrame* outFrame)
{
    double Height = fmax(height, 0.0);
    float SunColor[3];
    float MoonColor[3];
    ColorToLinear(environment->SunColor, SunColor);
    ColorToLinear(environment->MoonColor, MoonColor);
    float SkyTint[3];
    ColorToLinear(environment->SkyTint, SkyTint);
    float AmbientTint[3];
    ColorToLinear(environment->AmbientSkylightColor, AmbientTint);
    float NightColor[3];
    ColorToLinear(environment->NightLightColor, NightColor);
    // fmaxf drops a NaN: a broken value gives no light. An absurd one gives the most light: clamped (fminf takes an
    // infinity to the limit) before the colours multiply it, since an infinity times a colour's 0 would be a NaN.
    const float MaxLight = (float)WORLD_SKY_LIGHT_MAX_LIGHT;
    float SunIntensity = fminf(fmaxf(lights->SunIntensity, 0.0f) * (float)WORLD_ATMOSPHERE_SUN_LIGHT, MaxLight);
    float MoonIntensity = fminf(fmaxf(lights->MoonIntensity, 0.0f) * (float)WORLD_ATMOSPHERE_SUN_LIGHT, MaxLight);
    float AmbientStrength = fminf(fmaxf(environment->AmbientSkylightIntensity, 0.0f), MaxLight);
    float NightStrength = fminf(fmaxf(environment->NightLightIntensity, 0.0f), MaxLight);

    double SunTransmittance[3];
    double MoonTransmittance[3];
    WorldSkyLight_GetLightTransmittance(medium, (WorldAtmosphereRay){ .Height = Height,
        .CosZenith = (double)lights->SunDirection.y }, SunTransmittance);
    WorldSkyLight_GetLightTransmittance(medium, (WorldAtmosphereRay){ .Height = Height,
        .CosZenith = (double)lights->MoonDirection.y }, MoonTransmittance);

    // The sky's irradiance from the irradiance table: each light's only while it lights anything, light pollution's only
    // at night.
    double SunSky[3] = { 0.0, 0.0, 0.0 };
    double MoonSky[3] = { 0.0, 0.0, 0.0 };
    double PollutionSky[3] = { 0.0, 0.0, 0.0 };
    if (SunIntensity > 0.0f)
    {
        WorldSkyLight_LookUpSkyIrradiance(tables, (WorldAtmosphereRay){ .Height = Height,
            .CosZenith = (double)lights->SunDirection.y }, SunSky);
    }
    if (MoonIntensity > 0.0f)
    {
        WorldSkyLight_LookUpSkyIrradiance(tables, (WorldAtmosphereRay){ .Height = Height,
            .CosZenith = (double)lights->MoonDirection.y }, MoonSky);
    }
    if ((lightPollution[0] > 0.0f) || (lightPollution[1] > 0.0f) || (lightPollution[2] > 0.0f))
    {
        WorldSkyLight_LookUpLightPollutionIrradiance(tables, Height, PollutionSky);
    }

    for (int Channel = 0; Channel < 3; Channel++)
    {
        outFrame->SunAboveAir[Channel] = SunIntensity * SunColor[Channel];
        outFrame->MoonAboveAir[Channel] = MoonIntensity * MoonColor[Channel];
        outFrame->Sun[Channel] = (float)((double)outFrame->SunAboveAir[Channel] * SunTransmittance[Channel]);
        outFrame->Moon[Channel] = (float)((double)outFrame->MoonAboveAir[Channel] * MoonTransmittance[Channel]);
        double Tint = (double)fmaxf(SkyTint[Channel], 0.0f);
        double SunPart = SunSky[Channel] * (double)outFrame->SunAboveAir[Channel];
        outFrame->Sky[Channel] = (float)((SunPart + MoonSky[Channel] * (double)outFrame->MoonAboveAir[Channel]
            + PollutionSky[Channel] * (double)lightPollution[Channel]) * Tint);
        outFrame->SunSky[Channel] = (float)(SunPart * Tint);
        outFrame->Ground[Channel] = fmaxf(medium->GroundAlbedo[Channel], 0.0f)
            * (outFrame->Sun[Channel] * fmaxf(lights->SunDirection.y, 0.0f)
                + outFrame->Moon[Channel] * fmaxf(lights->MoonDirection.y, 0.0f) + outFrame->Sky[Channel]);
        outFrame->AmbientTint[Channel] = AmbientTint[Channel] * AmbientStrength;
        outFrame->Night[Channel] = NightColor[Channel] * NightStrength;
    }
}

float WorldSkyLight_GetMoonGate(float sunCosZenith, float moonCosZenith)
{
    float SunGate = 1.0f - SmoothStep(WORLD_SKY_LIGHT_MOON_SUN_FADE_START, WORLD_SKY_LIGHT_MOON_SUN_FADE_END,
        sunCosZenith);
    float MoonGate = SmoothStep(WORLD_SKY_LIGHT_MOON_FADE_START, WORLD_SKY_LIGHT_MOON_FADE_END, moonCosZenith);
    return SunGate * MoonGate;
}

float WorldSkyLight_GetSceneLuminance(const WorldSkyLightFrame* frame)
{
    float Ambient[3];
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Ambient[Channel] = frame->Sky[Channel] * frame->AmbientTint[Channel] / (float)SKY_LIGHT_PI + frame->Night[Channel];
    }
    return Luminance(Ambient) + WORLD_SKY_LIGHT_DIRECTIONAL_ADAPT_WEIGHT * (Luminance(frame->Sun) + Luminance(frame->Moon));
}

double WorldSkyLight_GetTwilightLuminance(const WorldSkyLightFrame* frame)
{
    return (double)Luminance(frame->SunSky) / SKY_LIGHT_PI * WORLD_ATMOSPHERE_SUN_LIGHT_CANDELAS
        / WORLD_ATMOSPHERE_SUN_LIGHT;
}

float WorldSkyLight_GetDayLuminance(const WorldSkyLightFrame* frame)
{
    float Ambient[3];
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Ambient[Channel] = frame->SunSky[Channel] * frame->AmbientTint[Channel] / (float)SKY_LIGHT_PI;
    }
    return Luminance(Ambient) + WORLD_SKY_LIGHT_DIRECTIONAL_ADAPT_WEIGHT * Luminance(frame->Sun);
}
