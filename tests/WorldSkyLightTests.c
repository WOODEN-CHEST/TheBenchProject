#include "TestSuites.h"
#include "EyeAdaptation.h"
#include "NightSky.h"
#include "NightVision.h"
#include "WorldAtmosphere.h"
#include "WorldEnvironment.h"
#include "WorldSkyLight.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>


/**
 * The sky's light on the world's surfaces (T36, source/WorldSkyLight.h): the C mirror at the camera that the renderer's
 * exposure, night vision, shadow light, status line and fallback uniforms take, and through it the GPU's irradiance
 * quadrature (the same in shaders/atmosphere/irradiance.frag; the shader source tests pin its constants). The
 * transmittance is exact, the C tables hold it, the sky's and light pollution's irradiance are within 2 % of fine
 * integrals of the same model, the irradiance table holds them (built whole or step by step), the frame's light is made
 * of them as the lit shaders make it, the shadow light hands over at dusk without a jump, the light falls continuously
 * into the night, and the night vision's anchors are this scale's.
 */


// Macros.
/** Pi in double. */
#define TEST_SKY_PI 3.14159265358979323846
/** The camera's height above the ground the frame tests take (a standing player's eyes), in metres. */
#define TEST_EYE_HEIGHT 1.7
/** How close the exact transmittance must come to the long double integral (absolute, per colour). */
#define TRANSMITTANCE_TOLERANCE 1e-6
/** The long double integral's Simpson panels along t = L u^2. */
#define REFERENCE_TRANSMITTANCE_PANELS 16384
/** How close the sky's irradiance must come to the fine integral (relative, per colour). */
#define IRRADIANCE_TOLERANCE 0.02
/** The fine integral of the sky's irradiance: Gauss-Legendre nodes in the zenith cosine, azimuths over half a turn and
 *  midpoint steps along each direction for the air's own light; rings around the light, their azimuths and steps for the
 *  haze's. Within 0.11 % of one about four times finer on the default air (measured when T36 was written). */
#define REFERENCE_ZENITH_NODES 16
#define REFERENCE_AZIMUTHS 16
#define REFERENCE_STEPS 128
#define REFERENCE_MIE_RINGS 32
#define REFERENCE_MIE_AZIMUTHS 32
#define REFERENCE_MIE_STEPS 64
/** How close the night vision's anchors must come to the scene luminances they stand for (relative). */
#define ANCHOR_TOLERANCE 0.02


// Static functions: helpers.
static WorldAtmosphereRay SkyRay(double height, double cosZenith)
{
    return (WorldAtmosphereRay){ .Height = height, .CosZenith = cosZenith };
}

/* The air of @p environment and the C tables built for it, on the heap (about 200 KB; the caller frees them). */
static WorldSkyLightTables* CreateTables(const WorldEnvironment* environment, WorldAtmosphereMedium* outMedium)
{
    WorldAtmosphere_GetMedium(environment, outMedium);
    WorldSkyLightTables* Tables = Memory_Allocate(sizeof(WorldSkyLightTables));
    WorldSkyLight_BuildTables(outMedium, Tables);
    return Tables;
}

static double Luminance(const double rgb[3])
{
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
}

static double LuminanceOf(const float rgb[3])
{
    return 0.2126 * (double)rgb[0] + 0.7152 * (double)rgb[1] + 0.0722 * (double)rgb[2];
}

/* The worst channel's difference relative to @p want. */
static double RelativeError(const double got[3], const double want[3])
{
    double Worst = 0.0;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Worst = fmax(Worst, fabs(got[Channel] - want[Channel]) / fmax(fabs(want[Channel]), 1e-30));
    }
    return Worst;
}

/* Whether a float colour is @p want's within @p tolerance (relative, per channel; absolute 1e-12 near 0). */
static bool IsColourClose(const float got[3], const double want[3], double tolerance)
{
    for (int Channel = 0; Channel < 3; Channel++)
    {
        if (fabs((double)got[Channel] - want[Channel]) > tolerance * fabs(want[Channel]) + 1e-12)
        {
            return false;
        }
    }
    return true;
}

/* An environment colour in the game's linear light (sRGB gamma 2.2, as the renderer takes it). */
static void ToLinear(Color color, double out[3])
{
    out[0] = pow((double)color.r / 255.0, 2.2);
    out[1] = pow((double)color.g / 255.0, 2.2);
    out[2] = pow((double)color.b / 255.0, 2.2);
}

static long double ReferenceOzone(long double height)
{
    long double Density = 1.0L - fabsl(height - (long double)WORLD_ATMOSPHERE_OZONE_CENTER)
        / (long double)WORLD_ATMOSPHERE_OZONE_HALF_WIDTH;
    return (Density > 0.0L) ? Density : 0.0L;
}

/* The transmittance from a point to the top of the atmosphere the slow way: the optical depth by Simpson's rule over
 * REFERENCE_TRANSMITTANCE_PANELS panels of t = L u^2 in long double, every height from the point's distance to the
 * planet's centre. A ray below the horizon is taken at the horizon, as WorldSkyLight_GetTransmittance takes it. */
static void ReferenceTransmittance(const WorldAtmosphereMedium* medium, double height, double cosZenith, double out[3])
{
    long double Planet = (long double)WORLD_ATMOSPHERE_PLANET_RADIUS;
    long double Top = (long double)WORLD_ATMOSPHERE_TOP_RADIUS;
    long double Radius = Planet + (long double)height;
    long double Mu = (long double)fmax(cosZenith, WorldAtmosphere_GetHorizonCos(height));
    long double SinZenith = sqrtl(fmaxl(1.0L - Mu * Mu, 0.0L));
    long double Length = -Radius * Mu + sqrtl((Top - Radius) * (Top + Radius) + Radius * Radius * Mu * Mu);
    long double Depth[3] = { 0.0L, 0.0L, 0.0L };
    long Points = 2L * REFERENCE_TRANSMITTANCE_PANELS;
    for (long Index = 0; Index <= Points; Index++)
    {
        long double U = (long double)Index / (long double)Points;
        long double Distance = Length * U * U;
        long double Slope = 2.0L * Length * U; // dt / du
        long double X = Distance * SinZenith;
        long double Y = Radius + Distance * Mu;
        long double PointHeight = sqrtl(X * X + Y * Y) - Planet;
        long double Weight = ((Index == 0) || (Index == Points)) ? 1.0L : (((Index % 2) == 1) ? 4.0L : 2.0L);
        long double Rayleigh = expl(-PointHeight / (long double)WORLD_ATMOSPHERE_RAYLEIGH_HEIGHT);
        long double Mie = expl(-PointHeight / (long double)medium->MieHeight);
        long double Ozone = ReferenceOzone(PointHeight);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            Depth[Channel] += Weight * Slope * ((long double)medium->RayleighScattering[Channel] * Rayleigh
                + (long double)medium->MieExtinction[Channel] * Mie + (long double)medium->OzoneAbsorption[Channel] * Ozone);
        }
    }
    for (int Channel = 0; Channel < 3; Channel++)
    {
        out[Channel] = (double)expl(-Depth[Channel] / (3.0L * (long double)Points));
    }
}

/* The Gauss-Legendre nodes and weights of @p count points on [0, 1] (Newton's method on the Legendre polynomial). */
static void GetGaussLegendre(int count, double* outNodes, double* outWeights)
{
    for (int Index = 0; Index < count; Index++)
    {
        double Z = cos(TEST_SKY_PI * ((double)Index + 0.75) / ((double)count + 0.5));
        double Derivative = 1.0;
        for (int Iteration = 0; Iteration < 100; Iteration++)
        {
            double Value = 1.0;
            double Before = 0.0;
            for (int Order = 0; Order < count; Order++)
            {
                double Older = Before;
                Before = Value;
                Value = ((2.0 * (double)Order + 1.0) * Z * Before - (double)Order * Older) / ((double)Order + 1.0);
            }
            Derivative = (double)count * (Z * Value - Before) / (Z * Z - 1.0);
            double Previous = Z;
            Z = Previous - Value / Derivative;
            if (fabs(Z - Previous) < 1e-15)
            {
                break;
            }
        }
        outNodes[Index] = 0.5 * (1.0 - Z);
        outWeights[Index] = 1.0 / ((1.0 - Z * Z) * Derivative * Derivative);
    }
}

static double CornetteShanks(double cosAngle, double g)
{
    double G2 = g * g;
    return 3.0 / (8.0 * TEST_SKY_PI) * ((1.0 - G2) * (cosAngle * cosAngle + 1.0))
        / (pow(1.0 + G2 - 2.0 * cosAngle * g, 1.5) * (2.0 + G2));
}

static double HenyeyGreenstein(double cosAngle, double g)
{
    double G2 = g * g;
    return (1.0 - G2) / (4.0 * TEST_SKY_PI * pow(1.0 + G2 - 2.0 * g * cosAngle, 1.5));
}

/* The share of an exponential density's mean over a step whose heights differ by @p rise, @p scale its scale height:
 * (1 - e^-x) / x for x = rise / scale. */
static double ExponentialMeanShare(double rise, double scale)
{
    double X = fabs(rise) / scale;
    return (X > 1e-4) ? (1.0 - exp(-X)) / X : 1.0 - 0.5 * X;
}

/* One direction of the fine integral from a surface at @p height (zenith cosine @p cosZenith), lit by a light at the
 * zenith cosine @p lightCos at the surface, @p cosAngle the cosine between the direction and the light: the midpoints of
 * @p steps stretches of t = L u^2, each lit through the C tables (the light's transmittance, the multiple scattering's
 * glow) and seen through the air in front of it (each stretch's optical depth from the mean densities between its ends).
 * The air's own light (Rayleigh with its phase function, the glow times the share of multiple scattering) unless
 * @p isHaze; the haze's single scattering per unit of its phase function then. */
static void ReferenceDirection(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium, double height,
    double cosZenith, double lightCos, double cosAngle, int steps, bool isHaze, double out[3])
{
    WorldAtmosphereRay View = SkyRay(height, cosZenith);
    double Length = WorldAtmosphere_GetDistanceToTop(View);
    double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + height;
    double RayleighPhase = 3.0 / (16.0 * TEST_SKY_PI) * (1.0 + cosAngle * cosAngle);
    double MieHeight = fmax((double)medium->MieHeight, 1.0);
    double Depth[3] = { 0.0, 0.0, 0.0 };
    double StartHeight = height;
    out[0] = out[1] = out[2] = 0.0;
    for (int Step = 0; Step < steps; Step++)
    {
        double From = Length * ((double)Step / (double)steps) * ((double)Step / (double)steps);
        double To = Length * ((double)(Step + 1) / (double)steps) * ((double)(Step + 1) / (double)steps);
        double StepLength = To - From;
        double Middle = 0.5 * (From + To);
        double MiddleHeight = WorldAtmosphere_GetHeightAt(View, Middle);
        double EndHeight = WorldAtmosphere_GetHeightAt(View, To);
        double Lowest = fmax(fmin(StartHeight, EndHeight), 0.0);
        double Rise = EndHeight - StartHeight;
        double RayleighDensity = exp(-Lowest / WORLD_ATMOSPHERE_RAYLEIGH_HEIGHT)
            * ExponentialMeanShare(Rise, WORLD_ATMOSPHERE_RAYLEIGH_HEIGHT);
        double MieDensity = exp(-Lowest / MieHeight) * ExponentialMeanShare(Rise, MieHeight);
        double Ozone = (double)ReferenceOzone((long double)MiddleHeight);
        StartHeight = EndHeight;
        double LightCosHere = fmin(fmax((Radius * lightCos + Middle * cosAngle)
            / (WORLD_ATMOSPHERE_PLANET_RADIUS + MiddleHeight), -1.0), 1.0);
        double Light[3];
        double Glow[3];
        WorldSkyLight_LookUpLightTransmittance(tables, SkyRay(MiddleHeight, LightCosHere), Light);
        WorldSkyLight_LookUpMultipleScattering(tables, SkyRay(MiddleHeight, LightCosHere), Glow);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            double Rayleigh = (double)medium->RayleighScattering[Channel] * RayleighDensity;
            double Mie = (double)medium->MieScattering[Channel] * MieDensity;
            double Extinction = Rayleigh + (double)medium->MieExtinction[Channel] * MieDensity
                + (double)medium->OzoneAbsorption[Channel] * Ozone;
            double Source = isHaze ? Mie * Light[Channel]
                : Rayleigh * RayleighPhase * Light[Channel]
                    + (Rayleigh + Mie) * Glow[Channel] * (double)medium->MultipleScattering;
            double StepDepth = Extinction * StepLength;
            double Leaving = (StepDepth > 1e-3) ? (1.0 - exp(-StepDepth)) / StepDepth : 1.0 - 0.5 * StepDepth;
            out[Channel] += exp(-Depth[Channel]) * Source * StepLength * Leaving;
            Depth[Channel] += StepDepth;
        }
    }
}

/* The sky's irradiance on a surface facing up at @p height per unit of a light above the air at the zenith cosine
 * @p lightCos, the slow way: the air's own light over Gauss-Legendre rings of the zenith cosine and even azimuths, the
 * haze's over rings around the light (stratified by Henyey and Greenstein's phase function, weighted by Cornette and
 * Shanks's) and even azimuths around it, the upper half of the sky only. */
static void ReferenceSkyIrradiance(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium, double height,
    double lightCos, double out[3])
{
    double Nodes[REFERENCE_ZENITH_NODES];
    double Weights[REFERENCE_ZENITH_NODES];
    GetGaussLegendre(REFERENCE_ZENITH_NODES, Nodes, Weights);
    double LightSin = sqrt(fmax(1.0 - lightCos * lightCos, 0.0));
    out[0] = out[1] = out[2] = 0.0;
    for (int Ring = 0; Ring < REFERENCE_ZENITH_NODES; Ring++)
    {
        double Mu = Nodes[Ring];
        double Sin = sqrt(fmax(1.0 - Mu * Mu, 0.0));
        for (int Azimuth = 0; Azimuth < REFERENCE_AZIMUTHS; Azimuth++)
        {
            double Angle = TEST_SKY_PI * ((double)Azimuth + 0.5) / (double)REFERENCE_AZIMUTHS;
            double CosAngle = Sin * LightSin * cos(Angle) + Mu * lightCos;
            double Light[3];
            ReferenceDirection(tables, medium, height, Mu, lightCos, CosAngle, REFERENCE_STEPS, false, Light);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                out[Channel] += Light[Channel] * Mu * Weights[Ring] * 2.0 * TEST_SKY_PI / (double)REFERENCE_AZIMUTHS;
            }
        }
    }
    double G = (double)medium->MieAnisotropy;
    double Count = (double)(REFERENCE_MIE_RINGS * REFERENCE_MIE_AZIMUTHS);
    for (int Ring = 0; Ring < REFERENCE_MIE_RINGS; Ring++)
    {
        double Quantile = ((double)Ring + 0.5) / (double)REFERENCE_MIE_RINGS;
        double Q = (1.0 - G * G) / (1.0 - G + 2.0 * G * Quantile);
        double CosAngle = fmin(fmax((1.0 + G * G - Q * Q) / (2.0 * G), -1.0), 1.0);
        double SinAngle = sqrt(fmax(1.0 - CosAngle * CosAngle, 0.0));
        double PhaseWeight = CornetteShanks(CosAngle, G) / HenyeyGreenstein(CosAngle, G);
        for (int Azimuth = 0; Azimuth < REFERENCE_MIE_AZIMUTHS; Azimuth++)
        {
            double Angle = TEST_SKY_PI * ((double)Azimuth + 0.5) / (double)REFERENCE_MIE_AZIMUTHS;
            double Mu = CosAngle * lightCos + SinAngle * cos(Angle) * LightSin;
            if (!(Mu > 0.0))
            {
                continue;
            }
            double Light[3];
            ReferenceDirection(tables, medium, height, Mu, lightCos, CosAngle, REFERENCE_MIE_STEPS, true, Light);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                out[Channel] += PhaseWeight * Mu * Light[Channel] / Count;
            }
        }
    }
}

/* Light pollution's irradiance on a surface facing up at @p height per unit of its radiance in the air, the slow way:
 * Gauss-Legendre rings of the zenith cosine, midpoint steps of t = L u^2 along each, the town's light at each step
 * (WorldAtmosphere_GetLightPollutionShare) scattered toward the surface through the air in front of it. */
static void ReferenceLightPollutionIrradiance(const WorldAtmosphereMedium* medium, double height, double out[3])
{
    double Nodes[REFERENCE_ZENITH_NODES];
    double Weights[REFERENCE_ZENITH_NODES];
    GetGaussLegendre(REFERENCE_ZENITH_NODES, Nodes, Weights);
    double MieHeight = fmax((double)medium->MieHeight, 1.0);
    out[0] = out[1] = out[2] = 0.0;
    for (int Ring = 0; Ring < REFERENCE_ZENITH_NODES; Ring++)
    {
        double Mu = Nodes[Ring];
        double Sin = sqrt(fmax(1.0 - Mu * Mu, 0.0));
        WorldAtmosphereRay View = SkyRay(height, Mu);
        double Length = WorldAtmosphere_GetDistanceToTop(View);
        double Depth[3] = { 0.0, 0.0, 0.0 };
        for (int Step = 0; Step < REFERENCE_STEPS; Step++)
        {
            double From = Length * ((double)Step / REFERENCE_STEPS) * ((double)Step / REFERENCE_STEPS);
            double To = Length * ((double)(Step + 1) / REFERENCE_STEPS) * ((double)(Step + 1) / REFERENCE_STEPS);
            double Middle = 0.5 * (From + To);
            double PointHeight = WorldAtmosphere_GetHeightAt(View, Middle);
            double Share = WorldAtmosphere_GetLightPollutionShare(PointHeight, Middle * Sin);
            double RayleighDensity = exp(-PointHeight / WORLD_ATMOSPHERE_RAYLEIGH_HEIGHT);
            double MieDensity = exp(-PointHeight / MieHeight);
            double Ozone = (double)ReferenceOzone((long double)PointHeight);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                double Scattering = (double)medium->RayleighScattering[Channel] * RayleighDensity
                    + (double)medium->MieScattering[Channel] * MieDensity;
                double Extinction = (double)medium->RayleighScattering[Channel] * RayleighDensity
                    + (double)medium->MieExtinction[Channel] * MieDensity
                    + (double)medium->OzoneAbsorption[Channel] * Ozone;
                double StepDepth = Extinction * (To - From);
                out[Channel] += Weights[Ring] * Mu * 2.0 * TEST_SKY_PI
                    * exp(-Depth[Channel] - 0.5 * StepDepth) * Scattering * Share * (To - From);
                Depth[Channel] += StepDepth;
            }
        }
    }
}

/* The sky's lights for the environment's date and time as the renderer takes them: the moon's light counts while the
 * moonlit sky shows (WorldSkyLight_GetMoonGate). */
static void GetRendererLights(const WorldEnvironment* environment, WorldSkyLights* outLights)
{
    WorldEnvironment_GetSkyLights(environment, outLights);
    outLights->MoonIntensity = fmaxf(outLights->MoonIntensity, 0.0f)
        * WorldSkyLight_GetMoonGate(outLights->SunDirection.y, outLights->MoonDirection.y);
}

/* The frame's light at the camera for the environment's date and time (the renderer's lights above the air, light
 * pollution's radiance), from the default eye height. */
static void GetFrameLight(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium,
    const WorldEnvironment* environment, WorldSkyLights* outLights, WorldSkyLightFrame* outFrame)
{
    GetRendererLights(environment, outLights);
    float LightPollution[3];
    WorldAtmosphere_GetLightPollutionRadiance(environment, outLights->Daylight, LightPollution);
    WorldSkyLight_GetFrameLight(tables, medium, environment, outLights, LightPollution, TEST_EYE_HEIGHT, outFrame);
}

/* The sun's and the moon's light through the air on a surface facing them at the eye height (what the frame's Sun and
 * Moon hold), as luminances: the light above the air in its colour times the exact transmittance toward it. */
static void GetDirectLuminances(const WorldAtmosphereMedium* medium, const WorldEnvironment* environment,
    const WorldSkyLights* lights, double* outSun, double* outMoon)
{
    double SunColor[3];
    double MoonColor[3];
    ToLinear(environment->SunColor, SunColor);
    ToLinear(environment->MoonColor, MoonColor);
    double SunThrough[3];
    double MoonThrough[3];
    WorldSkyLight_GetLightTransmittance(medium, SkyRay(TEST_EYE_HEIGHT, (double)lights->SunDirection.y), SunThrough);
    WorldSkyLight_GetLightTransmittance(medium, SkyRay(TEST_EYE_HEIGHT, (double)lights->MoonDirection.y), MoonThrough);
    double Sun[3];
    double Moon[3];
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Sun[Channel] = WORLD_ATMOSPHERE_SUN_LIGHT * (double)lights->SunIntensity * SunColor[Channel] * SunThrough[Channel];
        Moon[Channel] = WORLD_ATMOSPHERE_SUN_LIGHT * (double)lights->MoonIntensity * MoonColor[Channel]
            * MoonThrough[Channel];
    }
    *outSun = Luminance(Sun);
    *outMoon = Luminance(Moon);
}

/* The exposure the eye starts at under a frame's light (T37: its anchor, read on the real scale at the frame's mix). */
static float GetAnchorExposure(const WorldSkyLightFrame* frame)
{
    float Scene = WorldSkyLight_GetSceneLuminance(frame);
    float Day = WorldSkyLight_GetDayLuminance(frame);
    return EyeAdaptation_GetExposure(EyeAdaptation_GetAnchor(Scene), EyeAdaptation_GetRealScale(Day, Scene - Day));
}


// Static functions: tests.
/* The transmittance is exact: within 1e-6 of a long double integral from the ground to 99 km, straight up to along the
 * horizon; a ray below the horizon takes the horizon's; it falls toward the horizon, faster for blue. */
static void TransmittanceIsExact(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium Medium;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    const double Heights[] = { 0.0, TEST_EYE_HEIGHT, 300.0, 5000.0, 20000.0, 60000.0, 99000.0 };
    const double Cosines[] = { 1.0, 0.5, 0.2, 0.05, 0.01 };
    double Worst = 0.0;
    for (size_t Height = 0U; Height < (sizeof(Heights) / sizeof(Heights[0])); Height++)
    {
        double Horizon = WorldAtmosphere_GetHorizonCos(Heights[Height]);
        for (size_t Cosine = 0U; Cosine <= (sizeof(Cosines) / sizeof(Cosines[0])); Cosine++)
        {
            double Mu = (Cosine < (sizeof(Cosines) / sizeof(Cosines[0]))) ? Cosines[Cosine] : Horizon;
            double Got[3];
            double Want[3];
            WorldSkyLight_GetTransmittance(&Medium, SkyRay(Heights[Height], Mu), Got);
            ReferenceTransmittance(&Medium, Heights[Height], Mu, Want);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                Worst = fmax(Worst, fabs(Got[Channel] - Want[Channel]));
            }
        }
        double AtHorizon[3];
        double Below[3];
        WorldSkyLight_GetTransmittance(&Medium, SkyRay(Heights[Height], Horizon), AtHorizon);
        WorldSkyLight_GetTransmittance(&Medium, SkyRay(Heights[Height], Horizon - 0.2), Below);
        TEST_CHECK(run, (Below[0] == AtHorizon[0]) && (Below[1] == AtHorizon[1]) && (Below[2] == AtHorizon[2]));
    }
    if (!TEST_CHECK(run, Worst < TRANSMITTANCE_TOLERANCE))
    {
        printf("    worst difference from the long double integral %.3g\n", Worst);
    }

    // Lower toward the horizon, and redder: blue light is scattered out of the beam first.
    double Previous[3] = { 1.0, 1.0, 1.0 };
    bool IsFalling = true;
    for (int Step = 0; Step <= 50; Step++)
    {
        double Value[3];
        WorldSkyLight_GetTransmittance(&Medium, SkyRay(TEST_EYE_HEIGHT, 1.0 - (double)Step / 50.0), Value);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            IsFalling = IsFalling && (Value[Channel] <= Previous[Channel]) && (Value[Channel] >= 0.0);
            Previous[Channel] = Value[Channel];
        }
    }
    double Low[3];
    WorldSkyLight_GetTransmittance(&Medium, SkyRay(TEST_EYE_HEIGHT, 0.05), Low);
    TEST_CHECK(run, IsFalling && (Low[0] > Low[1]) && (Low[1] > Low[2]));
    WorldEnvironment_Deconstruct(&Environment);
}

/* A light sets over its disc: all of it reaches a point while the disc stands above the planet's horizon there, half
 * with its centre on it, none once it has set, smoothly between; the light through the air is that share of the
 * transmittance. */
static void LightSetsOverItsDisc(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium Medium;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    const double Heights[] = { 0.0, TEST_EYE_HEIGHT, 5000.0 };
    double HalfDisc = 0.5 * WORLD_ATMOSPHERE_LIGHT_DIAMETER;
    for (size_t Index = 0U; Index < (sizeof(Heights) / sizeof(Heights[0])); Index++)
    {
        double Horizon = WorldAtmosphere_GetHorizonCos(Heights[Index]);
        TEST_CHECK(run, WorldSkyLight_GetDiscShare(SkyRay(Heights[Index], Horizon + HalfDisc)) == 1.0);
        TEST_CHECK(run, WorldSkyLight_GetDiscShare(SkyRay(Heights[Index], Horizon - HalfDisc)) == 0.0);
        TEST_CHECK(run, fabs(WorldSkyLight_GetDiscShare(SkyRay(Heights[Index], Horizon)) - 0.5) < 1e-9);
        bool IsRising = true;
        double Previous = 0.0;
        for (int Step = 0; Step <= 40; Step++)
        {
            double Share = WorldSkyLight_GetDiscShare(SkyRay(Heights[Index],
                Horizon - HalfDisc + (double)Step / 40.0 * WORLD_ATMOSPHERE_LIGHT_DIAMETER));
            IsRising = IsRising && (Share >= Previous) && (Share - Previous < 0.06);
            Previous = Share;
        }
        TEST_CHECK(run, IsRising);

        double Through[3];
        double Transmittance[3];
        double Mu = Horizon + 0.25 * HalfDisc;
        WorldSkyLight_GetLightTransmittance(&Medium, SkyRay(Heights[Index], Mu), Through);
        WorldSkyLight_GetTransmittance(&Medium, SkyRay(Heights[Index], Mu), Transmittance);
        double Share = WorldSkyLight_GetDiscShare(SkyRay(Heights[Index], Mu));
        TEST_CHECK(run, (Share > 0.5) && (Share < 1.0) && (fabs(Through[1] - Share * Transmittance[1]) < 1e-15));
        WorldSkyLight_GetLightTransmittance(&Medium, SkyRay(Heights[Index], Horizon - 2.0 * HalfDisc), Through);
        TEST_CHECK(run, (Through[0] == 0.0) && (Through[1] == 0.0) && (Through[2] == 0.0));
    }
    WorldEnvironment_Deconstruct(&Environment);
}

/* The worst errors of the C tables' light against the exact light: up to 5 km (absolute), and relative from 2 and from 4
 * degrees above the horizon at heights up to 60 km. */
static void MeasureTableLight(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium, double* outWorstLow,
    double* outWorstFrom2, double* outWorstFrom4)
{
    const double Heights[] = { 0.0, TEST_EYE_HEIGHT, 300.0, 5000.0, 20000.0, 60000.0 };
    double WorstLow = 0.0;
    double WorstFrom2 = 0.0;
    double WorstFrom4 = 0.0;
    for (size_t Index = 0U; Index < (sizeof(Heights) / sizeof(Heights[0])); Index++)
    {
        double Horizon = WorldAtmosphere_GetHorizonCos(Heights[Index]);
        for (int Step = 0; Step <= 200; Step++)
        {
            double Above = (1.0 - Horizon) * pow((double)Step / 200.0, 2.0);
            double Got[3];
            double Want[3];
            WorldSkyLight_LookUpLightTransmittance(tables, SkyRay(Heights[Index], Horizon + Above), Got);
            WorldSkyLight_GetLightTransmittance(medium, SkyRay(Heights[Index], Horizon + Above), Want);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                double Difference = fabs(Got[Channel] - Want[Channel]);
                if (Heights[Index] <= 5000.0)
                {
                    WorstLow = fmax(WorstLow, Difference);
                }
                if (Above >= 0.035)
                {
                    WorstFrom2 = fmax(WorstFrom2, Difference / Want[Channel]);
                }
                if (Above >= 0.07)
                {
                    WorstFrom4 = fmax(WorstFrom4, Difference / Want[Channel]);
                }
            }
        }
    }
    *outWorstLow = WorstLow;
    *outWorstFrom2 = WorstFrom2;
    *outWorstFrom4 = WorstFrom4;
}

/* The C tables hold the light in the GPU's layout at half its size. In the reference haze (turbidity 3, 108 km of
 * visibility): within 0.005 of the exact light up to 5 km, within 4.5 % from 2 degrees above the horizon at every height
 * and 2 % from 4 degrees (nearer the horizon the light falls faster than the table's texels; the sky's irradiance it
 * feeds is within 0.15 % of the full-size table's). In the default haze (24, 20 km) the light near the horizon falls
 * faster still: within 0.007, 19 % and 8.5 % (where it is a few thousandths of the light above the air; a full-size
 * table would halve that). The multiple scattering's glow is finite and positive, larger with the light higher. */
static void TablesHoldTheLight(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    typedef struct AirBoundsStruct
    {
        float Turbidity;
        double Low;
        double From2;
        double From4;
    } AirBounds;
    const AirBounds Airs[] = { { 3.0f, 0.005, 0.045, 0.02 }, { Environment.SkyTurbidity, 0.007, 0.19, 0.085 } };
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Tables = Memory_Allocate(sizeof(WorldSkyLightTables));
    for (size_t Air = 0U; Air < (sizeof(Airs) / sizeof(Airs[0])); Air++)
    {
        Environment.SkyTurbidity = Airs[Air].Turbidity;
        WorldAtmosphere_GetMedium(&Environment, &Medium);
        WorldSkyLight_BuildTables(&Medium, Tables);
        double WorstLow = 0.0;
        double WorstFrom2 = 0.0;
        double WorstFrom4 = 0.0;
        MeasureTableLight(Tables, &Medium, &WorstLow, &WorstFrom2, &WorstFrom4);
        if (!TEST_CHECK(run, (WorstLow < Airs[Air].Low) && (WorstFrom2 < Airs[Air].From2)
            && (WorstFrom4 < Airs[Air].From4)))
        {
            printf("    turbidity %g: up to 5 km %.4f; from 2 degrees %.2f %%, from 4 degrees %.2f %%\n",
                (double)Airs[Air].Turbidity, WorstLow, 100.0 * WorstFrom2, 100.0 * WorstFrom4);
        }
    }

    bool IsGlowSane = true;
    for (int Row = 0; Row < WORLD_SKY_LIGHT_MULTISCATTER_SIZE; Row++)
    {
        for (int Column = 0; Column < WORLD_SKY_LIGHT_MULTISCATTER_SIZE; Column++)
        {
            for (int Channel = 0; Channel < 3; Channel++)
            {
                float Value = Tables->Multiscatter[Row][Column][Channel];
                IsGlowSane = IsGlowSane && isfinite(Value) && (Value >= 0.0f);
            }
        }
    }
    double Noon[3];
    double Dusk[3];
    WorldSkyLight_LookUpMultipleScattering(Tables, SkyRay(TEST_EYE_HEIGHT, 1.0), Noon);
    WorldSkyLight_LookUpMultipleScattering(Tables, SkyRay(TEST_EYE_HEIGHT, 0.05), Dusk);
    TEST_CHECK(run, IsGlowSane && (Noon[1] > Dusk[1]) && (Dusk[1] > 0.0));
    Memory_Free(Tables);
    WorldEnvironment_Deconstruct(&Environment);
}

/* The sky's irradiance on a surface facing up is within 2 % of a fine integral of the same model (the same tables) from
 * 1.7 m, 300 m and 5 km, for the sun at the zenith, 5 degrees up, on the horizon and 4 degrees below it; none for a
 * light below the table's lowest elevation; brighter with the light higher; and the share of multiple scattering adds
 * its glow in proportion. */
static void SkyIrradianceMatchesAFineIntegral(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Tables = CreateTables(&Environment, &Medium);
    const double Heights[] = { TEST_EYE_HEIGHT, 300.0, 5000.0 };
    const double Elevations[] = { 90.0, 5.0, 0.0, -4.0 };
    for (size_t Height = 0U; Height < (sizeof(Heights) / sizeof(Heights[0])); Height++)
    {
        for (size_t Elevation = 0U; Elevation < (sizeof(Elevations) / sizeof(Elevations[0])); Elevation++)
        {
            double LightCos = sin(Elevations[Elevation] * TEST_SKY_PI / 180.0);
            double Got[3];
            double Want[3];
            WorldSkyLight_GetSkyIrradiance(Tables, &Medium, SkyRay(Heights[Height], LightCos), Got);
            ReferenceSkyIrradiance(Tables, &Medium, Heights[Height], LightCos, Want);
            double Difference = RelativeError(Got, Want);
            if (!TEST_CHECK(run, Difference < IRRADIANCE_TOLERANCE))
            {
                printf("    from %.1f m with the light %.0f degrees up: %.3g %.3g %.3g against %.3g %.3g %.3g (%.2f %%)\n",
                    Heights[Height], Elevations[Elevation], Got[0], Got[1], Got[2], Want[0], Want[1], Want[2],
                    100.0 * Difference);
            }
        }
    }

    double Below[3];
    WorldSkyLight_GetSkyIrradiance(Tables, &Medium,
        SkyRay(TEST_EYE_HEIGHT, sin(WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION) - 1e-3), Below);
    TEST_CHECK(run, (Below[0] == 0.0) && (Below[1] == 0.0) && (Below[2] == 0.0));
    double Previous = 0.0;
    bool IsRising = true;
    for (int Step = 0; Step <= 20; Step++)
    {
        double Value[3];
        WorldSkyLight_GetSkyIrradiance(Tables, &Medium, SkyRay(TEST_EYE_HEIGHT, -0.1 + 1.1 * (double)Step / 20.0), Value);
        IsRising = IsRising && (Luminance(Value) > Previous);
        Previous = Luminance(Value);
    }
    TEST_CHECK(run, IsRising);

    double With[3];
    double Without[3];
    WorldSkyLight_GetSkyIrradiance(Tables, &Medium, SkyRay(TEST_EYE_HEIGHT, 0.5), With);
    Medium.MultipleScattering = 0.0f;
    WorldSkyLight_GetSkyIrradiance(Tables, &Medium, SkyRay(TEST_EYE_HEIGHT, 0.5), Without);
    TEST_CHECK(run, (Without[1] > 0.0) && (With[1] > 1.05 * Without[1]));
    // The share scales the glow: half of it gives the mean of none and all.
    double Half[3];
    Medium.MultipleScattering = 0.5f;
    WorldSkyLight_GetSkyIrradiance(Tables, &Medium, SkyRay(TEST_EYE_HEIGHT, 0.5), Half);
    TEST_CHECK(run, fabs(Half[1] - 0.5 * (With[1] + Without[1])) < 1e-9 * With[1]);
    Memory_Free(Tables);
    WorldEnvironment_Deconstruct(&Environment);
}

/* Light pollution's irradiance on a surface facing up is within 2 % of a fine integral from 1.7 m, 300 m and 5 km, and
 * fades with the height (the town's light fills the low air). */
static void LightPollutionIrradianceMatchesAFineIntegral(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium Medium;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    const double Heights[] = { TEST_EYE_HEIGHT, 300.0, 5000.0 };
    double Previous = INFINITY;
    for (size_t Index = 0U; Index < (sizeof(Heights) / sizeof(Heights[0])); Index++)
    {
        double Got[3];
        double Want[3];
        WorldSkyLight_GetLightPollutionIrradiance(&Medium, Heights[Index], Got);
        ReferenceLightPollutionIrradiance(&Medium, Heights[Index], Want);
        double Difference = RelativeError(Got, Want);
        if (!TEST_CHECK(run, (Difference < IRRADIANCE_TOLERANCE) && (Got[1] > 0.0) && (Got[1] < Previous)))
        {
            printf("    from %.1f m: %.4g %.4g %.4g against %.4g %.4g %.4g (%.2f %%)\n", Heights[Index], Got[0], Got[1],
                Got[2], Want[0], Want[1], Want[2], 100.0 * Difference);
        }
        Previous = Got[1];
    }
    WorldEnvironment_Deconstruct(&Environment);
}

/* The worst errors of the irradiance table read between its texels against the quadrature: from 2 degrees below the
 * horizon up, from 6 degrees below, and light pollution's up to 5 km. */
static void MeasureIrradianceTable(const WorldSkyLightTables* tables, const WorldAtmosphereMedium* medium,
    double* outWorstHigh, double* outWorstTwilight, double* outWorstTown)
{
    const double Heights[] = { 0.0, TEST_EYE_HEIGHT, 30.0, 300.0, 1200.0, 5000.0, 15000.0 };
    double WorstHigh = 0.0;
    double WorstTwilight = 0.0;
    double WorstTown = 0.0;
    for (size_t Height = 0U; Height < (sizeof(Heights) / sizeof(Heights[0])); Height++)
    {
        for (int Step = 0; Step <= 96; Step++)
        {
            double Elevation = (-6.0 + 96.0 * (double)Step / 96.0 + 0.173) * TEST_SKY_PI / 180.0;
            if (Elevation > 0.5 * TEST_SKY_PI)
            {
                continue;
            }
            double Got[3];
            double Want[3];
            WorldSkyLight_LookUpSkyIrradiance(tables, SkyRay(Heights[Height], sin(Elevation)), Got);
            WorldSkyLight_GetSkyIrradiance(tables, medium, SkyRay(Heights[Height], sin(Elevation)), Want);
            double Difference = RelativeError(Got, Want);
            if (Elevation >= -2.0 * TEST_SKY_PI / 180.0)
            {
                WorstHigh = fmax(WorstHigh, Difference);
            }
            WorstTwilight = fmax(WorstTwilight, Difference);
        }
        if (Heights[Height] <= 5000.0)
        {
            double Got[3];
            double Want[3];
            WorldSkyLight_LookUpLightPollutionIrradiance(tables, Heights[Height] + 17.0, Got);
            WorldSkyLight_GetLightPollutionIrradiance(medium, Heights[Height] + 17.0, Want);
            WorstTown = fmax(WorstTown, RelativeError(Got, Want));
        }
    }
    *outWorstHigh = WorstHigh;
    *outWorstTwilight = WorstTwilight;
    *outWorstTown = WorstTown;
}

/* The irradiance table holds the quadrature: at its texels exactly (to its float logarithms); between them, in the
 * reference haze (turbidity 3), within 1 % from 2 degrees below the horizon up and 5 % from 6 degrees below (the twilight
 * falls fastest there; lower, the sky's light is a thousandth of the night's own), light pollution's within 2 % up to 5
 * km; in the default haze (24) within 2 %, 5 % and 2 %; none below the lowest elevation. */
static void IrradianceTableHoldsTheQuadrature(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Tables = CreateTables(&Environment, &Medium);
    double WorstTexel = 0.0;
    for (int Row = 0; Row < WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT; Row += 5)
    {
        for (int Column = 0; Column < WORLD_ATMOSPHERE_IRRADIANCE_WIDTH; Column += 7)
        {
            WorldAtmosphereRay Texel = WorldAtmosphere_GetIrradianceRay(Vector2Double_Create(
                (double)Column / (double)(WORLD_ATMOSPHERE_IRRADIANCE_WIDTH - 1),
                (double)Row / (double)(WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT - 1)));
            double Got[3];
            double Want[3];
            WorldSkyLight_LookUpSkyIrradiance(Tables, Texel, Got);
            WorldSkyLight_GetSkyIrradiance(Tables, &Medium, Texel, Want);
            if (Want[1] > 1e-15)
            {
                WorstTexel = fmax(WorstTexel, RelativeError(Got, Want));
            }
        }
    }
    TEST_CHECK(run, WorstTexel < 2e-6);

    typedef struct AirBoundsStruct
    {
        float Turbidity;
        double High;
        double Twilight;
        double Town;
    } AirBounds;
    const float DefaultTurbidity = Environment.SkyTurbidity;
    const AirBounds Airs[] = { { 3.0f, 0.01, 0.05, 0.02 }, { DefaultTurbidity, 0.02, 0.05, 0.02 } };
    WorldSkyLightTables* AirTables = Memory_Allocate(sizeof(WorldSkyLightTables));
    for (size_t Air = 0U; Air < (sizeof(Airs) / sizeof(Airs[0])); Air++)
    {
        WorldAtmosphereMedium AirMedium;
        Environment.SkyTurbidity = Airs[Air].Turbidity;
        WorldAtmosphere_GetMedium(&Environment, &AirMedium);
        WorldSkyLight_BuildTables(&AirMedium, AirTables);
        double WorstHigh = 0.0;
        double WorstTwilight = 0.0;
        double WorstTown = 0.0;
        MeasureIrradianceTable(AirTables, &AirMedium, &WorstHigh, &WorstTwilight, &WorstTown);
        if (!TEST_CHECK(run, (WorstHigh < Airs[Air].High) && (WorstTwilight < Airs[Air].Twilight)
            && (WorstTown < Airs[Air].Town)))
        {
            printf("    turbidity %g: from 2 degrees below %.2f %%, from 6 below %.2f %%, light pollution %.2f %%\n",
                (double)Airs[Air].Turbidity, 100.0 * WorstHigh, 100.0 * WorstTwilight, 100.0 * WorstTown);
        }
    }
    Environment.SkyTurbidity = DefaultTurbidity;
    Memory_Free(AirTables);
    double Below[3];
    WorldSkyLight_LookUpSkyIrradiance(Tables, SkyRay(TEST_EYE_HEIGHT, sin(WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION) - 1e-3),
        Below);
    TEST_CHECK(run, (Below[0] == 0.0) && (Below[1] == 0.0) && (Below[2] == 0.0));
    Memory_Free(Tables);
    WorldEnvironment_Deconstruct(&Environment);
}

/* The steps of a build, taken in order, build the very tables a whole build does (the renderer spreads them over frames
 * when the air changes); a step outside the range writes nothing. */
static void TableStepsBuildTheSameTables(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.SkyTurbidity = 6.0f;
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Whole = CreateTables(&Environment, &Medium);
    WorldSkyLightTables* Stepped = Memory_Allocate(sizeof(WorldSkyLightTables));
    Memory_Zero(Stepped, sizeof(WorldSkyLightTables));
    for (int Step = 0; Step < WORLD_SKY_LIGHT_BUILD_STEP_COUNT; Step++)
    {
        WorldSkyLight_BuildTablesStep(&Medium, Stepped, Step);
    }
    TEST_CHECK(run, memcmp(Whole, Stepped, sizeof(WorldSkyLightTables)) == 0);
    // Tables of a pattern no step writes: the steps out of the range leave them as they are.
    memset(Stepped, 0x5A, sizeof(WorldSkyLightTables));
    memset(Whole, 0x5A, sizeof(WorldSkyLightTables));
    WorldSkyLight_BuildTablesStep(&Medium, Stepped, -1);
    WorldSkyLight_BuildTablesStep(&Medium, Stepped, WORLD_SKY_LIGHT_BUILD_STEP_COUNT);
    TEST_CHECK(run, memcmp(Whole, Stepped, sizeof(WorldSkyLightTables)) == 0);
    Memory_Free(Stepped);
    Memory_Free(Whole);
    WorldEnvironment_Deconstruct(&Environment);
}

/* The renderer's tables follow the air (WorldSkyLightBuilder): the first update builds them whole; a change of the air
 * starts a rebuild whose steps fill the spare set while the frames read the old one, and its last step swaps in the very
 * tables a whole build makes; a change during a rebuild waits for the next one; preparing a world builds whole only for
 * air the tables do not hold, and drops a running rebuild either way. */
static void BuilderRebuildsOverFrames(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium First;
    WorldAtmosphere_GetMedium(&Environment, &First);
    Environment.SkyTurbidity = 6.0f;
    WorldAtmosphereMedium Second;
    WorldAtmosphere_GetMedium(&Environment, &Second);
    Environment.MultipleScattering = 0.5f;
    WorldAtmosphereMedium Third;
    WorldAtmosphere_GetMedium(&Environment, &Third);
    WorldSkyLightTables* Expected = Memory_Allocate(sizeof(WorldSkyLightTables));
    WorldSkyLightBuilder Builder;
    WorldSkyLight_ConstructBuilder(&Builder);

    // The first frame builds whole, at once; the same air again needs nothing.
    TEST_CHECK(run, !WorldSkyLight_UpdateBuilder(&Builder, &First) && Builder.Record.IsBuilt
        && (Builder.Record.BuildCount == 1U) && !Builder.IsBuilding);
    WorldSkyLight_BuildTables(&First, Expected);
    TEST_CHECK(run, memcmp(Builder.Tables, Expected, sizeof(WorldSkyLightTables)) == 0);
    TEST_CHECK(run, !WorldSkyLight_UpdateBuilder(&Builder, &First) && !WorldSkyLight_RunBuildStep(&Builder)
        && (Builder.Record.BuildCount == 1U));

    // New air: a rebuild, the frames reading the old tables until its last step; a third air meanwhile waits.
    const WorldSkyLightTables* Old = Builder.Tables;
    TEST_CHECK(run, WorldSkyLight_UpdateBuilder(&Builder, &Second) && Builder.IsBuilding);
    int Calls = 0;
    bool IsOldKept = true;
    bool HasMore = true;
    while (HasMore)
    {
        IsOldKept = IsOldKept && (Builder.Tables == Old) && (Builder.Record.BuildCount == 1U);
        HasMore = WorldSkyLight_RunBuildStep(&Builder);
        Calls++;
        if (Calls == 10)
        {
            TEST_CHECK(run, WorldSkyLight_UpdateBuilder(&Builder, &Third)
                && !WorldAtmosphere_IsIrradianceStale(&Builder.BuildingFor, &Second));
        }
    }
    WorldSkyLight_BuildTables(&Second, Expected);
    TEST_CHECK(run, IsOldKept && (Calls == WORLD_SKY_LIGHT_BUILD_STEP_COUNT) && (Builder.Tables != Old)
        && !Builder.IsBuilding && (Builder.Record.BuildCount == 2U)
        && !WorldAtmosphere_IsIrradianceStale(&Builder.Record.BuiltFor, &Second)
        && (memcmp(Builder.Tables, Expected, sizeof(WorldSkyLightTables)) == 0));
    TEST_CHECK(run, WorldSkyLight_UpdateBuilder(&Builder, &Third));
    while (WorldSkyLight_RunBuildStep(&Builder))
    {
    }
    WorldSkyLight_BuildTables(&Third, Expected);
    TEST_CHECK(run, (Builder.Record.BuildCount == 3U) && (memcmp(Builder.Tables, Expected, sizeof(WorldSkyLightTables)) == 0));

    // Preparing a world whose air the tables hold builds nothing and drops the running rebuild.
    TEST_CHECK(run, WorldSkyLight_UpdateBuilder(&Builder, &First) && WorldSkyLight_RunBuildStep(&Builder));
    WorldSkyLight_PrepareBuilder(&Builder, &Third);
    TEST_CHECK(run, !Builder.IsBuilding && (Builder.Record.BuildCount == 3U) && !WorldSkyLight_RunBuildStep(&Builder)
        && (memcmp(Builder.Tables, Expected, sizeof(WorldSkyLightTables)) == 0));
    // New air builds whole at once, dropping the running rebuild.
    TEST_CHECK(run, WorldSkyLight_UpdateBuilder(&Builder, &Second) && WorldSkyLight_RunBuildStep(&Builder));
    WorldSkyLight_PrepareBuilder(&Builder, &First);
    WorldSkyLight_BuildTables(&First, Expected);
    TEST_CHECK(run, !Builder.IsBuilding && (Builder.Record.BuildCount == 4U)
        && !WorldAtmosphere_IsIrradianceStale(&Builder.Record.BuiltFor, &First)
        && (memcmp(Builder.Tables, Expected, sizeof(WorldSkyLightTables)) == 0));

    WorldSkyLight_DeconstructBuilder(&Builder);
    Memory_Free(Expected);
    WorldEnvironment_Deconstruct(&Environment);
}

/* The moon's light counts while the moonlit sky shows: all of it with the sun at or below the horizon and the moon at or
 * above -3 degrees, none with the sun 3 degrees up or the moon 6 below, half with the sun half way, smoothly between;
 * a NaN counts as far below the horizon. */
static void MoonGateFollowsTheSky(TestRun* run)
{
    TEST_CHECK(run, (WorldSkyLight_GetMoonGate(-0.5f, 0.5f) == 1.0f)
        && (WorldSkyLight_GetMoonGate(WORLD_SKY_LIGHT_MOON_SUN_FADE_START, WORLD_SKY_LIGHT_MOON_FADE_END) == 1.0f)
        && (WorldSkyLight_GetMoonGate(WORLD_SKY_LIGHT_MOON_SUN_FADE_END, 0.5f) == 0.0f)
        && (WorldSkyLight_GetMoonGate(-0.5f, WORLD_SKY_LIGHT_MOON_FADE_START) == 0.0f));
    float Half = WorldSkyLight_GetMoonGate(0.5f * WORLD_SKY_LIGHT_MOON_SUN_FADE_END, 0.5f);
    TEST_CHECK(run, fabsf(Half - 0.5f) < 1e-6f);
    bool IsSmooth = true;
    float PreviousSun = WorldSkyLight_GetMoonGate(-0.1f, 0.5f);
    float PreviousMoon = WorldSkyLight_GetMoonGate(-0.5f, -0.2f);
    for (int Step = 1; Step <= 400; Step++)
    {
        float Sun = WorldSkyLight_GetMoonGate(-0.1f + 0.2f * (float)Step / 400.0f, 0.5f);
        float Moon = WorldSkyLight_GetMoonGate(-0.5f, -0.2f + 0.2f * (float)Step / 400.0f);
        IsSmooth = IsSmooth && (Sun <= PreviousSun) && (PreviousSun - Sun < 0.02f) && (Moon >= PreviousMoon)
            && (Moon - PreviousMoon < 0.02f);
        PreviousSun = Sun;
        PreviousMoon = Moon;
    }
    TEST_CHECK(run, IsSmooth && (PreviousSun == 0.0f) && (PreviousMoon == 1.0f));
    TEST_CHECK(run, (WorldSkyLight_GetMoonGate(NAN, 0.5f) == 1.0f) && (WorldSkyLight_GetMoonGate(-0.5f, NAN) == 0.0f));
}

/* The frame's light is made of its parts as the lit shaders make it: the sun and the moon above the air (the sun's
 * light x its intensity x its colour), through the exact transmittance at the eye; the sky's irradiance of each light
 * and light pollution's from the irradiance table, tinted by the sky; the ground's bounce; the ambient's tint and the
 * night's light; at noon, and at a full-moon midnight in a lit town. */
static void FrameLightIsMadeOfItsParts(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.MoonPhase = 0.5f;
    Environment.LightPollution = 0.5f;
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Tables = CreateTables(&Environment, &Medium);
    const float Times[] = { 0.5f, 0.0f };
    for (size_t Index = 0U; Index < (sizeof(Times) / sizeof(Times[0])); Index++)
    {
        Environment.TimeOfDay = Times[Index];
        WorldSkyLights Lights;
        WorldSkyLightFrame Frame;
        GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
        float LightPollution[3];
        WorldAtmosphere_GetLightPollutionRadiance(&Environment, Lights.Daylight, LightPollution);

        double SunColor[3];
        double MoonColor[3];
        double SkyTint[3];
        double AmbientColor[3];
        double NightColor[3];
        ToLinear(Environment.SunColor, SunColor);
        ToLinear(Environment.MoonColor, MoonColor);
        ToLinear(Environment.SkyTint, SkyTint);
        ToLinear(Environment.AmbientSkylightColor, AmbientColor);
        ToLinear(Environment.NightLightColor, NightColor);
        double SunThrough[3];
        double MoonThrough[3];
        double SunSky[3];
        double MoonSky[3];
        double TownSky[3];
        WorldSkyLight_GetLightTransmittance(&Medium, SkyRay(TEST_EYE_HEIGHT, (double)Lights.SunDirection.y), SunThrough);
        WorldSkyLight_GetLightTransmittance(&Medium, SkyRay(TEST_EYE_HEIGHT, (double)Lights.MoonDirection.y), MoonThrough);
        WorldSkyLight_LookUpSkyIrradiance(Tables, SkyRay(TEST_EYE_HEIGHT, (double)Lights.SunDirection.y), SunSky);
        WorldSkyLight_LookUpSkyIrradiance(Tables, SkyRay(TEST_EYE_HEIGHT, (double)Lights.MoonDirection.y), MoonSky);
        WorldSkyLight_LookUpLightPollutionIrradiance(Tables, TEST_EYE_HEIGHT, TownSky);
        double SunAbove[3];
        double MoonAbove[3];
        double Sun[3];
        double Moon[3];
        double Sky[3];
        double SunPart[3];
        double Ground[3];
        double Tint[3];
        double Night[3];
        for (int Channel = 0; Channel < 3; Channel++)
        {
            SunAbove[Channel] = WORLD_ATMOSPHERE_SUN_LIGHT * (double)Lights.SunIntensity * SunColor[Channel];
            MoonAbove[Channel] = WORLD_ATMOSPHERE_SUN_LIGHT * (double)Lights.MoonIntensity * MoonColor[Channel];
            Sun[Channel] = SunAbove[Channel] * SunThrough[Channel];
            Moon[Channel] = MoonAbove[Channel] * MoonThrough[Channel];
            Sky[Channel] = (SunSky[Channel] * SunAbove[Channel] + MoonSky[Channel] * MoonAbove[Channel]
                + TownSky[Channel] * (double)LightPollution[Channel]) * SkyTint[Channel];
            SunPart[Channel] = SunSky[Channel] * SunAbove[Channel] * SkyTint[Channel];
            Ground[Channel] = (double)Medium.GroundAlbedo[Channel] * (Sun[Channel] * fmax((double)Lights.SunDirection.y, 0.0)
                + Moon[Channel] * fmax((double)Lights.MoonDirection.y, 0.0) + Sky[Channel]);
            Tint[Channel] = AmbientColor[Channel] * (double)Environment.AmbientSkylightIntensity;
            Night[Channel] = NightColor[Channel] * (double)Environment.NightLightIntensity;
        }
        TEST_CHECK(run, IsColourClose(Frame.SunAboveAir, SunAbove, 1e-6) && IsColourClose(Frame.MoonAboveAir, MoonAbove,
            1e-6));
        TEST_CHECK(run, IsColourClose(Frame.Sun, Sun, 1e-6) && IsColourClose(Frame.Moon, Moon, 1e-6));
        TEST_CHECK(run, IsColourClose(Frame.Sky, Sky, 1e-5) && IsColourClose(Frame.SunSky, SunPart, 1e-5)
            && IsColourClose(Frame.Ground, Ground, 1e-5));
        TEST_CHECK(run, IsColourClose(Frame.AmbientTint, Tint, 1e-6) && IsColourClose(Frame.Night, Night, 1e-6));
        // The lights shine: the sun at noon (the moon is down), the moon at midnight (the sun is down) with the town's.
        bool IsNoon = Index == 0U;
        TEST_CHECK(run, IsNoon ? ((Frame.Sun[1] > 0.65f) && (Frame.Moon[1] == 0.0f))
            : ((Frame.Sun[1] == 0.0f) && (Frame.Moon[1] > 0.005f) && (TownSky[1] * (double)LightPollution[1] > 0.0)));

        // The scene luminance the eye adapts from: the ambient on a white surface facing up and half of each light.
        double Ambient[3];
        for (int Channel = 0; Channel < 3; Channel++)
        {
            Ambient[Channel] = (double)Frame.Sky[Channel] * (double)Frame.AmbientTint[Channel] / TEST_SKY_PI
                + (double)Frame.Night[Channel];
        }
        double Weight = (double)WORLD_SKY_LIGHT_DIRECTIONAL_ADAPT_WEIGHT;
        double Scene = Luminance(Ambient) + Weight * (LuminanceOf(Frame.Sun) + LuminanceOf(Frame.Moon));
        TEST_CHECK(run, fabs((double)WorldSkyLight_GetSceneLuminance(&Frame) - Scene) < 1e-6 * Scene);
        // The daylight's share: the sun's part of the ambient and its weighted light.
        double DayAmbient[3];
        for (int Channel = 0; Channel < 3; Channel++)
        {
            DayAmbient[Channel] = (double)Frame.SunSky[Channel] * (double)Frame.AmbientTint[Channel] / TEST_SKY_PI;
        }
        double Day = Luminance(DayAmbient) + Weight * LuminanceOf(Frame.Sun);
        TEST_CHECK(run, fabs((double)WorldSkyLight_GetDayLuminance(&Frame) - Day) < 1e-6 * Scene);
        TEST_CHECK(run, IsNoon ? (Day > 0.95 * Scene) : (Day == 0.0));
    }
    Memory_Free(Tables);
    WorldEnvironment_Deconstruct(&Environment);
}

/* The settings change the light as they say: the sun's colour and intensity, the sky's tint on its irradiance alone,
 * the ambient's tint and strength, the night's light, the moon's intensity; the ground's albedo its bounce. */
static void FrameLightFollowsTheSettings(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.TimeOfDay = 0.5f;
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Tables = CreateTables(&Environment, &Medium);
    WorldSkyLights Lights;
    WorldSkyLightFrame Base;
    GetFrameLight(Tables, &Medium, &Environment, &Lights, &Base);

    WorldSkyLightFrame Frame;
    Environment.SunColor = (Color){ 255, 128, 64, 255 };
    Environment.SunIntensity = 2.0f;
    GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
    double Red = pow(128.0 / 255.0, 2.2);
    TEST_CHECK(run, (fabsf(Frame.Sun[0] - 2.0f * Base.Sun[0]) < 1e-5f)
        && (fabs((double)Frame.Sun[1] - 2.0 * Red * (double)Base.Sun[1]) < 1e-5));
    TEST_CHECK(run, fabsf(Frame.Sky[0] - 2.0f * Base.Sky[0]) < 1e-5f);
    Environment.SunColor = (Color){ 255, 255, 255, 255 };
    Environment.SunIntensity = 1.0f;

    Environment.SkyTint = (Color){ 0, 255, 255, 255 };
    GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
    TEST_CHECK(run, (Frame.Sky[0] == 0.0f) && (Frame.Sky[1] == Base.Sky[1]) && (Frame.Sun[0] == Base.Sun[0]));
    Environment.SkyTint = (Color){ 255, 255, 255, 255 };

    Environment.AmbientSkylightColor = (Color){ 255, 0, 255, 255 };
    Environment.AmbientSkylightIntensity = 0.5f;
    Environment.NightLightIntensity = 0.0f;
    GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
    TEST_CHECK(run, (Frame.AmbientTint[0] == 0.5f) && (Frame.AmbientTint[1] == 0.0f) && (Frame.Sky[1] == Base.Sky[1]));
    TEST_CHECK(run, (Frame.Night[0] == 0.0f) && (Frame.Night[1] == 0.0f) && (Frame.Night[2] == 0.0f));
    TEST_CHECK(run, WorldSkyLight_GetSceneLuminance(&Frame) < WorldSkyLight_GetSceneLuminance(&Base));
    Environment.AmbientSkylightColor = (Color){ 255, 255, 255, 255 };
    Environment.AmbientSkylightIntensity = 1.0f;
    Environment.NightLightIntensity = 0.02f;
    GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
    double NightBlue = pow((double)Environment.NightLightColor.b / 255.0, 2.2) * 0.02;
    TEST_CHECK(run, fabs((double)Frame.Night[2] - NightBlue) < 1e-7);

    // The moon's intensity scales its light (a full moon at midnight).
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.MoonPhase = 0.5f;
    Environment.TimeOfDay = 0.0f;
    GetFrameLight(Tables, &Medium, &Environment, &Lights, &Base);
    Environment.MoonIntensity = 2.0f * WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY;
    GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
    TEST_CHECK(run, (Base.Moon[2] > 0.0f) && (fabsf(Frame.Moon[2] - 2.0f * Base.Moon[2]) < 1e-6f));

    // A darker ground bounces less.
    Environment.TimeOfDay = 0.5f;
    GetFrameLight(Tables, &Medium, &Environment, &Lights, &Base);
    Medium.GroundAlbedo[1] *= 0.5f;
    WorldSkyLight_GetFrameLight(Tables, &Medium, &Environment, &Lights, (const float[3]){ 0.0f, 0.0f, 0.0f },
        TEST_EYE_HEIGHT, &Frame);
    TEST_CHECK(run, (Frame.Ground[1] > 0.4f * Base.Ground[1]) && (Frame.Ground[1] < 0.6f * Base.Ground[1]));
    Memory_Free(Tables);
    WorldEnvironment_Deconstruct(&Environment);
}

/* Broken values give no light instead of a broken one: a NaN or negative intensity lights nothing, a NaN height is the
 * ground's, and the scene luminance and exposure stay finite. Absurd but valid ones (FLT_MAX, an infinity from a product
 * of them) give at most WORLD_SKY_LIGHT_MAX_LIGHT above the air and nothing where a factor is 0 (a colour's channel, the
 * transmittance under the horizon): every value finite, by day, at sunset and at midnight. */
static void BrokenValuesGiveNoLight(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.TimeOfDay = 0.5f;
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Tables = CreateTables(&Environment, &Medium);
    WorldSkyLights Lights;
    WorldEnvironment_GetSkyLights(&Environment, &Lights);
    const float None[3] = { 0.0f, 0.0f, 0.0f };
    const float Broken[] = { NAN, -1.0f };
    for (size_t Index = 0U; Index < (sizeof(Broken) / sizeof(Broken[0])); Index++)
    {
        WorldSkyLights Bad = Lights;
        Bad.SunIntensity = Broken[Index];
        Bad.MoonIntensity = Broken[Index];
        WorldSkyLightFrame Frame;
        WorldSkyLight_GetFrameLight(Tables, &Medium, &Environment, &Bad, None, TEST_EYE_HEIGHT, &Frame);
        TEST_CHECK(run, (Frame.Sun[1] == 0.0f) && (Frame.Moon[1] == 0.0f) && (Frame.Sky[1] == 0.0f)
            && (Frame.SunAboveAir[1] == 0.0f));
        float Scene = WorldSkyLight_GetSceneLuminance(&Frame);
        TEST_CHECK(run, isfinite(Scene) && isfinite(GetAnchorExposure(&Frame)));
    }
    const float Times[] = { 0.5f, 0.75f, 0.0f };
    const float Absurd[] = { FLT_MAX, INFINITY };
    for (size_t Time = 0U; Time < (sizeof(Times) / sizeof(Times[0])); Time++)
    {
        for (size_t Index = 0U; Index < (sizeof(Absurd) / sizeof(Absurd[0])); Index++)
        {
            WorldEnvironment Bright;
            WorldEnvironment_SetDefaults(&Bright);
            Bright.TimeOfDay = Times[Time];
            Bright.SunColor = (Color){ 255, 0, 255, 255 };
            Bright.AmbientSkylightIntensity = Absurd[Index];
            Bright.NightLightIntensity = Absurd[Index];
            WorldSkyLights Many;
            WorldEnvironment_GetSkyLights(&Bright, &Many);
            Many.SunIntensity = Absurd[Index];
            Many.MoonIntensity = Absurd[Index];
            const float Town[3] = { 0.05f, 0.03f, 0.01f };
            WorldSkyLightFrame Frame;
            WorldSkyLight_GetFrameLight(Tables, &Medium, &Bright, &Many, Town, TEST_EYE_HEIGHT, &Frame);
            const float* Values[] = { Frame.SunAboveAir, Frame.MoonAboveAir, Frame.Sun, Frame.Moon, Frame.Sky,
                Frame.SunSky, Frame.Ground, Frame.AmbientTint, Frame.Night };
            bool IsFinite = true;
            for (size_t Value = 0U; Value < (sizeof(Values) / sizeof(Values[0])); Value++)
            {
                for (int Channel = 0; Channel < 3; Channel++)
                {
                    IsFinite = IsFinite && isfinite(Values[Value][Channel]) && (Values[Value][Channel] >= 0.0f);
                }
            }
            float Scene = WorldSkyLight_GetSceneLuminance(&Frame);
            float Day = WorldSkyLight_GetDayLuminance(&Frame);
            if (!TEST_CHECK(run, IsFinite && !isnan(Scene) && !isnan(Day)
                && isfinite(GetAnchorExposure(&Frame))
                && (Frame.SunAboveAir[0] == (float)WORLD_SKY_LIGHT_MAX_LIGHT) && (Frame.SunAboveAir[1] == 0.0f)
                && (Frame.Sun[1] == 0.0f) && (Frame.AmbientTint[1] == (float)WORLD_SKY_LIGHT_MAX_LIGHT)))
            {
                printf("    at time %.2f with %g\n", (double)Times[Time], (double)Absurd[Index]);
            }
            WorldEnvironment_Deconstruct(&Bright);
        }
    }

    WorldSkyLightFrame Ground;
    WorldSkyLightFrame Unknown;
    WorldSkyLight_GetFrameLight(Tables, &Medium, &Environment, &Lights, None, 0.0, &Ground);
    WorldSkyLight_GetFrameLight(Tables, &Medium, &Environment, &Lights, None, NAN, &Unknown);
    TEST_CHECK(run, (Unknown.Sun[1] == Ground.Sun[1]) && (Unknown.Sky[1] == Ground.Sky[1]));
    Memory_Free(Tables);
    WorldEnvironment_Deconstruct(&Environment);
}

/* Dusk to night with a full moon rising, by the light that reaches the eye through the air from the renderer's lights
 * (its shadow choice): the sun's shadows, then none, then the moon's, without a jump, every switch while the shadows are
 * faded out. */
static void ShadowLightHandsOverAtDusk(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.MoonPhase = 0.5f;
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Tables = CreateTables(&Environment, &Medium);

    // The luminances the shadow choice takes are the frame's direct lights'.
    Environment.TimeOfDay = 0.76f;
    WorldSkyLights Lights;
    WorldSkyLightFrame Frame;
    GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
    double SunLuminance = 0.0;
    double MoonLuminance = 0.0;
    GetDirectLuminances(&Medium, &Environment, &Lights, &SunLuminance, &MoonLuminance);
    TEST_CHECK(run, (fabs(LuminanceOf(Frame.Moon) - MoonLuminance) < 1e-6 * MoonLuminance) && (MoonLuminance > 0.0));

    bool HasSeenNone = false;
    bool IsOrderRight = true;
    bool IsSmooth = true;
    bool AreSwitchesHidden = true;
    WorldShadowLight Previous = WorldShadowLight_Sun;
    float PreviousFade = 1.0f;
    for (int Step = 0; Step <= 75000; Step++)
    {
        Environment.TimeOfDay = 0.70f + (float)Step * 2e-6f; // steps of 0.17 game seconds
        GetRendererLights(&Environment, &Lights);
        GetDirectLuminances(&Medium, &Environment, &Lights, &SunLuminance, &MoonLuminance);
        WorldShadowLightChoice Choice = WorldEnvironment_ChooseShadowLight(Lights.SunDirection, (float)SunLuminance,
            Lights.MoonDirection, (float)MoonLuminance);
        IsSmooth = IsSmooth && (fabsf(Choice.Fade - PreviousFade) < 0.01f);
        if (Choice.Light != Previous)
        {
            AreSwitchesHidden = AreSwitchesHidden && (PreviousFade < 0.01f) && (Choice.Fade < 0.01f);
            IsOrderRight = IsOrderRight && ((Choice.Light == WorldShadowLight_None)
                || ((Choice.Light == WorldShadowLight_Moon) && HasSeenNone));
        }
        HasSeenNone = HasSeenNone || (Choice.Light == WorldShadowLight_None);
        Previous = Choice.Light;
        PreviousFade = Choice.Fade;
    }
    TEST_CHECK(run, IsSmooth && AreSwitchesHidden && IsOrderRight && HasSeenNone);
    TEST_CHECK(run, (Previous == WorldShadowLight_Moon) && (PreviousFade > 0.99f));
    Memory_Free(Tables);
    WorldEnvironment_Deconstruct(&Environment);
}

/* From noon into a moonless night the scene luminance falls without a jump (less than 10 % in any 10 game seconds) to the
 * night's own light, never rising by more than 0.1 % in 10 seconds (light pollution's glow comes on as the daylight
 * fades: in the default haze it rises by about 0.01 % a step while the twilight is about 6 degrees down). The eye at
 * its anchor follows it (T37): the screen level of the scene luminance falls steadily too (never rising by more than
 * 0.1 %) from noon's to the night's; the exposure, which rises through the dusk and falls back as the twilight's real
 * light fades, changes by less than 10 % in 10 seconds. */
static void LightFallsContinuouslyIntoTheNight(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.IsMoonEnabled = false;
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Tables = CreateTables(&Environment, &Medium);
    double PreviousScene = INFINITY;
    double PreviousScreen = INFINITY;
    double PreviousExposure = NAN;
    double FirstScreen = NAN;
    double WorstDrop = 1.0;
    double WorstExposureStep = 1.0;
    bool IsFalling = true;
    bool IsScreenFalling = true;
    float Scene = 0.0f;
    for (int Step = 0; Step <= 8 * 360; Step++) // 10-second steps from 12:00 to 20:00
    {
        Environment.TimeOfDay = 0.5f + (float)Step / 8640.0f;
        WorldSkyLights Lights;
        WorldSkyLightFrame Frame;
        GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
        Scene = WorldSkyLight_GetSceneLuminance(&Frame);
        double Exposure = (double)GetAnchorExposure(&Frame);
        double Screen = Exposure * (double)Scene;
        IsFalling = IsFalling && ((double)Scene <= PreviousScene * (1.0 + 1e-3));
        IsScreenFalling = IsScreenFalling && (Screen <= PreviousScreen * (1.0 + 1e-3));
        if (isfinite(PreviousScene))
        {
            WorstDrop = fmin(WorstDrop, (double)Scene / PreviousScene);
            WorstExposureStep = fmin(WorstExposureStep, fmin(Exposure / PreviousExposure, PreviousExposure / Exposure));
        }
        FirstScreen = isnan(FirstScreen) ? Screen : FirstScreen;
        PreviousScene = (double)Scene;
        PreviousScreen = Screen;
        PreviousExposure = Exposure;
    }
    double Night[3];
    ToLinear(Environment.NightLightColor, Night);
    double NightLuminance = Luminance(Night) * (double)Environment.NightLightIntensity;
    if (!TEST_CHECK(run, IsFalling && IsScreenFalling && (WorstDrop > 0.9) && (WorstExposureStep > 0.9)))
    {
        printf("    falling %d, screen falling %d, worst drop in 10 s %.4f, worst exposure step %.4f\n", (int)IsFalling,
            (int)IsScreenFalling, WorstDrop, WorstExposureStep);
    }
    // At 20:00 the night's own light and the town's are all there is: the eye at its anchor shows a white surface at
    // about two fifths of the noon's (the curve's calibration, T37: the open field's views keep their exposures from
    // before T37 at noon and at night).
    TEST_CHECK(run, ((double)Scene >= NightLuminance) && ((double)Scene < 1.5 * NightLuminance));
    double NightShare = PreviousScreen / FirstScreen;
    if (!TEST_CHECK(run, (NightShare > 0.38) && (NightShare < 0.42)))
    {
        printf("    the night's screen level is %.4f of the noon's\n", NightShare);
    }
    Memory_Free(Tables);
    WorldEnvironment_Deconstruct(&Environment);
}

/* The stars come out through the dusk (T38): against the twilight's real luminance (the sky's light from the sun) and the
 * night's in the default air, a new moon and the default light pollution, the faintest star seen rises steadily as the
 * sun sinks: none with the sun 5 degrees up or higher, about 3.5 at the end of the civil twilight, the night's limit at
 * full night; the Milky Way shows only once the sun is 10 degrees down, and most of its night's strength by 14. */
static void StarsComeOutThroughTheDusk(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.MoonPhase = 0.0f;
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Tables = CreateTables(&Environment, &Medium);
    double MaxSine = sin(67.07 * TEST_SKY_PI / 180.0); // the default sun's height at noon
    float NightLimit = NightSky_GetNightLimitingMagnitude(Environment.LightPollution, 0.0f);
    bool IsRising = true;
    float Previous = -INFINITY;
    float AtCivilEnd = NAN;
    float MilkyWayAtTen = NAN;
    float MilkyWayAtFourteen = NAN;
    float Last = NAN;
    for (int Degrees = 10; Degrees >= -20; Degrees--)
    {
        double Sine = sin((double)Degrees * TEST_SKY_PI / 180.0);
        Environment.TimeOfDay = (float)(1.0 - acos(-Sine / MaxSine) / (2.0 * TEST_SKY_PI));
        WorldSkyLights Lights;
        WorldSkyLightFrame Frame;
        GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
        float Limit = NightSky_GetLimitingMagnitude(Environment.LightPollution, 0.0f,
            WorldSkyLight_GetTwilightLuminance(&Frame));
        IsRising = IsRising && (Limit >= Previous - 1e-4f);
        Previous = Limit;
        if (Degrees >= 5)
        {
            TEST_CHECK(run, Limit < NIGHT_SKY_BRIGHTEST_MAGNITUDE);
        }
        AtCivilEnd = (Degrees == -6) ? Limit : AtCivilEnd;
        MilkyWayAtTen = (Degrees == -10) ? NightSky_GetMilkyWayVisibility(Limit) : MilkyWayAtTen;
        MilkyWayAtFourteen = (Degrees == -14) ? NightSky_GetMilkyWayVisibility(Limit) : MilkyWayAtFourteen;
        Last = Limit;
    }
    if (!TEST_CHECK(run, IsRising && (AtCivilEnd > 3.2f) && (AtCivilEnd < 3.8f) && (fabsf(Last - NightLimit) < 2e-3f)
        && (MilkyWayAtTen == 0.0f) && (MilkyWayAtFourteen > 0.8f * NightSky_GetMilkyWayVisibility(NightLimit))))
    {
        printf("    rising %d, at -6 degrees %.2f, at -20 %.3f (night %.3f), the Milky Way at -10 %.2f, at -14 %.2f\n",
            (int)IsRising, (double)AtCivilEnd, (double)Last, (double)NightLimit, (double)MilkyWayAtTen,
            (double)MilkyWayAtFourteen);
    }
    // No daylight, no twilight; the twilight is the sun's sky alone (the night's lights do not count).
    TEST_CHECK(run, WorldSkyLight_GetTwilightLuminance(&(WorldSkyLightFrame){ 0 }) == 0.0);
    Memory_Free(Tables);
    WorldEnvironment_Deconstruct(&Environment);
}

/* Night vision reads the daylight on the real scale (T36): from noon into a moonless night the field luminance keeps the
 * rods out (the colours stay) until the sun is 4 degrees below the horizon, lets them in through the nautical twilight,
 * and leaves them most of vision by 12 degrees below, as the eye does; the field never rises on the way. Without the split
 * the night's own light alone would grey the sunset. */
static void TwilightKeepsItsColours(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.MoonPhase = 0.0f;
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Tables = CreateTables(&Environment, &Medium);
    double MaxSine = sin(67.07 * TEST_SKY_PI / 180.0); // the default sun's height at noon
    bool IsFalling = true;
    float Previous = INFINITY;
    for (int Degrees = 10; Degrees >= -15; Degrees--)
    {
        // The afternoon's time of day with the sun this high: its height is MaxSine x -cos(2 pi t).
        double Sine = sin((double)Degrees * TEST_SKY_PI / 180.0);
        Environment.TimeOfDay = (float)(1.0 - acos(-Sine / MaxSine) / (2.0 * TEST_SKY_PI));
        WorldSkyLights Lights;
        WorldSkyLightFrame Frame;
        GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
        float Scene = WorldSkyLight_GetSceneLuminance(&Frame);
        float Day = WorldSkyLight_GetDayLuminance(&Frame);
        float Field = NightVision_GetFieldLuminance(Day, Scene - Day);
        float Rods = NightVision_GetRodShare(0.0f, Field, 1.0f);
        IsFalling = IsFalling && (Field <= Previous * 1.0001f);
        Previous = Field;
        if (Degrees >= -4)
        {
            TEST_CHECK(run, Rods < 0.02f);
        }
        if (Degrees == 0)
        {
            TEST_CHECK(run, NightVision_GetRodShare(0.0f, Scene, 1.0f) > 0.5f); // the scene luminance alone: rods
        }
        if (Degrees <= -12)
        {
            TEST_CHECK(run, Rods > 0.9f);
        }
    }
    TEST_CHECK(run, IsFalling);
    Memory_Free(Tables);
    WorldEnvironment_Deconstruct(&Environment);
}

/* The night vision's anchors are this light scale's: its noon the scene luminance of a clear noon with the default
 * environment, its night a new-moon midnight's with the default light pollution (the C mirror's, from the eye's
 * height). */
static void NightVisionAnchorsAreThisScale(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.MoonPhase = 0.0f;
    WorldAtmosphereMedium Medium;
    WorldSkyLightTables* Tables = CreateTables(&Environment, &Medium);
    WorldSkyLights Lights;
    WorldSkyLightFrame Frame;
    Environment.TimeOfDay = 0.5f;
    GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
    double Noon = (double)WorldSkyLight_GetSceneLuminance(&Frame);
    Environment.TimeOfDay = 0.0f;
    GetFrameLight(Tables, &Medium, &Environment, &Lights, &Frame);
    double Night = (double)WorldSkyLight_GetSceneLuminance(&Frame);
    if (!TEST_CHECK(run, (fabs(NIGHT_VISION_NOON_LUMINANCE - Noon) < ANCHOR_TOLERANCE * Noon)
        && (fabs(NIGHT_VISION_NIGHT_LUMINANCE - Night) < ANCHOR_TOLERANCE * Night)))
    {
        printf("    noon %.5g (anchor %.5g), night %.5g (anchor %.5g)\n", Noon, NIGHT_VISION_NOON_LUMINANCE, Night,
            NIGHT_VISION_NIGHT_LUMINANCE);
    }
    Memory_Free(Tables);
    WorldEnvironment_Deconstruct(&Environment);
}


// Public functions.
void WorldSkyLightTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "transmittance_is_exact", TransmittanceIsExact },
        { "light_sets_over_its_disc", LightSetsOverItsDisc },
        { "tables_hold_the_light", TablesHoldTheLight },
        { "sky_irradiance_matches_a_fine_integral", SkyIrradianceMatchesAFineIntegral },
        { "light_pollution_irradiance_matches_a_fine_integral", LightPollutionIrradianceMatchesAFineIntegral },
        { "irradiance_table_holds_the_quadrature", IrradianceTableHoldsTheQuadrature },
        { "table_steps_build_the_same_tables", TableStepsBuildTheSameTables },
        { "builder_rebuilds_over_frames", BuilderRebuildsOverFrames },
        { "moon_gate_follows_the_sky", MoonGateFollowsTheSky },
        { "frame_light_is_made_of_its_parts", FrameLightIsMadeOfItsParts },
        { "frame_light_follows_the_settings", FrameLightFollowsTheSettings },
        { "broken_values_give_no_light", BrokenValuesGiveNoLight },
        { "shadow_light_hands_over_at_dusk", ShadowLightHandsOverAtDusk },
        { "light_falls_continuously_into_the_night", LightFallsContinuouslyIntoTheNight },
        { "twilight_keeps_its_colours", TwilightKeepsItsColours },
        { "stars_come_out_through_the_dusk", StarsComeOutThroughTheDusk },
        { "night_vision_anchors_are_this_scale", NightVisionAnchorsAreThisScale },
    };
    TestRun_RunSuite(run, "WorldSkyLight", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
