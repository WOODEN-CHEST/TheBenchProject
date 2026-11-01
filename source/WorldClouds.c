// The clouds as pure maths (WorldClouds.h): the frame's layers, their patterns' drift and anchors, the shadow map's frame
// and the ray's stretch through a layer.

#include <math.h>
#include "WorldClouds.h"
#include "WorldAtmosphere.h"
#include "wr/WRMemory.h"


// Static functions.
/* A finite value in [minimum, maximum]; a non-finite one counts as the minimum. */
static float ClampFinite(float value, float minimum, float maximum)
{
    return isfinite(value) ? fminf(fmaxf(value, minimum), maximum) : minimum;
}

/* A value wrapped into [0, period), in double, then rounded once: an anchor's component. */
static float WrapAnchor(double value, double period)
{
    double Wrapped = fmod(value, period);
    if (Wrapped < 0.0)
    {
        Wrapped += period;
    }
    // fmod of a huge value is exact but may land on the period itself after the addition; keep it inside.
    return (Wrapped >= period) ? 0.0f : (float)Wrapped;
}

/* Adds a drift's step: the level wind's direction times a speed and the seconds, and a rise. */
static void AddDrift(Vector3Double* drift, double windX, double windZ, double speed, double rise, double seconds)
{
    drift->X += windX * speed * seconds;
    drift->Y += rise * seconds;
    drift->Z += windZ * speed * seconds;
}

/* One volumetric layer from its settings: none without coverage or density. */
static WorldCloudLayer MakeLayer(float base, float thickness, float coverage, float type, float extinction)
{
    WorldCloudLayer Layer;
    Layer.Base = ClampFinite(base, 0.0f, (float)WORLD_ENVIRONMENT_MAX_CLOUD_HEIGHT);
    Layer.Top = Layer.Base + ClampFinite(thickness, (float)WORLD_ENVIRONMENT_MIN_CLOUD_THICKNESS,
        (float)WORLD_ENVIRONMENT_MAX_CLOUD_HEIGHT);
    Layer.Coverage = ClampFinite(coverage, 0.0f, 1.0f);
    Layer.Type = ClampFinite(type, 0.0f, 1.0f);
    Layer.Extinction = (Layer.Coverage > 0.0f) ? fmaxf(extinction, 0.0f) : 0.0f;
    return Layer;
}

/* The roots of a ray from the camera's height with the zenith cosine mu through the sphere `height` up: near and far
 * (near < 0 inside it), in the forms that do not cancel (shaders/common/clouds.glsl's cloudSphereDistances). */
static bool GetSphereDistances(double viewHeight, double mu, double height, double* outNear, double* outFar)
{
    double ViewRadius = WORLD_ATMOSPHERE_PLANET_RADIUS + viewHeight;
    double Lift = (height - viewHeight) * (2.0 * WORLD_ATMOSPHERE_PLANET_RADIUS + height + viewHeight);
    double B = ViewRadius * mu;
    double Discriminant = B * B + Lift;
    if (Discriminant < 0.0)
    {
        *outNear = 0.0;
        *outFar = -1.0;
        return false;
    }
    double Root = sqrt(Discriminant);
    if (B > 0.0)
    {
        *outNear = -B - Root;
        *outFar = Lift / (B + Root);
    }
    else
    {
        *outNear = ((Root - B) > 0.0) ? (-Lift / (Root - B)) : (-B - Root);
        *outFar = Root - B;
    }
    return true;
}


// Internal functions (declared in WorldClouds.h).
void WorldCloudsDrift_Reset(WorldCloudsDrift* self)
{
    Memory_Zero(self, sizeof(*self));
}

void WorldCloudsDrift_Advance(WorldCloudsDrift* self, const WorldEnvironment* environment, double seconds)
{
    if (!isfinite(seconds) || (seconds <= 0.0))
    {
        return;
    }
    double Angle = environment->WindDirection;
    double Speed = (double)environment->WindSpeed + (double)environment->WindGust;
    double Scale = environment->CloudWindScale;
    double Evolution = environment->CloudEvolution;
    if (!isfinite(Angle) || !isfinite(Speed) || !isfinite(Scale) || !isfinite(Evolution))
    {
        return;
    }
    double WindX = sin(Angle);
    double WindZ = cos(Angle);
    double LowSpeed = fmax(Speed, 0.0) * fmax(Scale, 0.0);
    Evolution = fmax(Evolution, 0.0);
    AddDrift(&self->LowCoverage, WindX, WindZ, LowSpeed, 0.0, seconds);
    AddDrift(&self->LowShape, WindX, WindZ, LowSpeed * (1.0 + WORLD_CLOUDS_SHAPE_DRIFT_SHARE * Evolution),
        WORLD_CLOUDS_SHAPE_RISE * Evolution, seconds);
    AddDrift(&self->LowDetail, WindX, WindZ, LowSpeed * (1.0 + WORLD_CLOUDS_DETAIL_DRIFT_SHARE * Evolution),
        WORLD_CLOUDS_DETAIL_RISE * Evolution, seconds);
    AddDrift(&self->Mid, WindX, WindZ, LowSpeed * WORLD_CLOUDS_MID_DRIFT, 0.0, seconds);
    AddDrift(&self->High, WindX, WindZ, LowSpeed * WORLD_CLOUDS_HIGH_DRIFT, 0.0, seconds);
}

void WorldClouds_GetFrame(const WorldEnvironment* environment, const WorldCloudsDrift* drift, Vector3Double origin,
    WorldCloudsFrame* outFrame)
{
    Memory_Zero(outFrame, sizeof(*outFrame));
    const WorldEnvironment* Environment = environment;
    float Density = ClampFinite(Environment->CloudDensity, 0.0f, 1.0f);
    outFrame->Low = MakeLayer(Environment->CloudBase, Environment->CloudThickness, Environment->CloudCoverage,
        Environment->CloudType, WORLD_CLOUDS_LOW_EXTINCTION * Density);
    float MidDensity = ClampFinite(Environment->CloudMidDensity, 0.0f, 1.0f);
    outFrame->Mid = MakeLayer(Environment->CloudMidBase, Environment->CloudMidThickness, Environment->CloudMidCoverage,
        Environment->CloudMidType, WORLD_CLOUDS_MID_EXTINCTION * MidDensity);
    outFrame->HighHeight = ClampFinite(Environment->CloudHighHeight, 0.0f, (float)WORLD_ENVIRONMENT_MAX_CLOUD_HEIGHT);
    outFrame->HighCoverage = ClampFinite(Environment->CloudHighCoverage, 0.0f, 1.0f);
    outFrame->HighType = ClampFinite(Environment->CloudHighType, 0.0f, 1.0f);
    outFrame->HighDepth = (outFrame->HighCoverage > 0.0f)
        ? (WORLD_CLOUDS_HIGH_DEPTH * ClampFinite(Environment->CloudHighDensity, 0.0f, 1.0f)) : 0.0f;
    outFrame->DetailStrength = ClampFinite(Environment->CloudDetail, 0.0f, 2.0f);
    outFrame->Forward = ClampFinite(Environment->CloudForwardScattering, 0.0f, 0.95f);
    Color Tint = Environment->CloudTint;
    outFrame->Tint[0] = powf((float)Tint.r / 255.0f, 2.2f);
    outFrame->Tint[1] = powf((float)Tint.g / 255.0f, 2.2f);
    outFrame->Tint[2] = powf((float)Tint.b / 255.0f, 2.2f);
    outFrame->LightPollution = ClampFinite(Environment->CloudLightPollution, 0.0f, 10.0f);

    double Scale = ClampFinite(Environment->CloudScale, (float)WORLD_ENVIRONMENT_MIN_CLOUD_SCALE,
        (float)WORLD_ENVIRONMENT_MAX_CLOUD_SCALE);
    double ShapeTile = WORLD_CLOUDS_SHAPE_TILE * Scale;
    double DetailTile = WORLD_CLOUDS_DETAIL_TILE * Scale;
    double CoverageTile = WORLD_CLOUDS_COVERAGE_TILE * Scale;
    double MidShapeTile = WORLD_CLOUDS_MID_SHAPE_TILE * Scale;
    double MidMapTile = WORLD_CLOUDS_MID_MAP_TILE * Scale;
    double HighMapTile = WORLD_CLOUDS_HIGH_MAP_TILE * Scale;
    outFrame->Tiles[0] = (float)ShapeTile;
    outFrame->Tiles[1] = (float)DetailTile;
    outFrame->Tiles[2] = (float)CoverageTile;
    outFrame->Tiles[3] = (float)MidShapeTile;
    outFrame->MapTiles[0] = (float)MidMapTile;
    outFrame->MapTiles[1] = (float)HighMapTile;
    // The anchors: the origin less the drift (a feature of a pattern stands at its place plus the drift: it moves with the
    // wind), wrapped where every read of the pattern repeats (the low layer's shape and erosion, read twice, across by
    // WORLD_CLOUDS_TURNED_TILES of their tiles and up by one; its coverage map at its tile; the mid layer's shape and
    // map share one anchor, wrapped across by WORLD_CLOUDS_MID_ANCHOR_TILES of its map, up by its shape's tile; the high
    // layer's by WORLD_CLOUDS_HIGH_ANCHOR_TILES of its map).
    double ShapeAcross = ShapeTile * (double)WORLD_CLOUDS_TURNED_TILES;
    double DetailAcross = DetailTile * (double)WORLD_CLOUDS_TURNED_TILES;
    outFrame->ShapeAnchor[0] = WrapAnchor(origin.X - drift->LowShape.X, ShapeAcross);
    outFrame->ShapeAnchor[1] = WrapAnchor(origin.Y - drift->LowShape.Y, ShapeTile);
    outFrame->ShapeAnchor[2] = WrapAnchor(origin.Z - drift->LowShape.Z, ShapeAcross);
    outFrame->DetailAnchor[0] = WrapAnchor(origin.X - drift->LowDetail.X, DetailAcross);
    outFrame->DetailAnchor[1] = WrapAnchor(origin.Y - drift->LowDetail.Y, DetailTile);
    outFrame->DetailAnchor[2] = WrapAnchor(origin.Z - drift->LowDetail.Z, DetailAcross);
    outFrame->CoverageAnchor[0] = WrapAnchor(origin.X - drift->LowCoverage.X, CoverageTile);
    outFrame->CoverageAnchor[1] = WrapAnchor(origin.Z - drift->LowCoverage.Z, CoverageTile);
    double MidPeriod = MidMapTile * (double)WORLD_CLOUDS_MID_ANCHOR_TILES;
    outFrame->MidAnchor[0] = WrapAnchor(origin.X - drift->Mid.X, MidPeriod);
    outFrame->MidAnchor[1] = WrapAnchor(origin.Y - drift->Mid.Y, MidShapeTile);
    outFrame->MidAnchor[2] = WrapAnchor(origin.Z - drift->Mid.Z, MidPeriod);
    double HighPeriod = HighMapTile * (double)WORLD_CLOUDS_HIGH_ANCHOR_TILES;
    outFrame->HighAnchor[0] = WrapAnchor(origin.X - drift->High.X, HighPeriod);
    outFrame->HighAnchor[1] = WrapAnchor(origin.Z - drift->High.Z, HighPeriod);
}

bool WorldClouds_HasAny(const WorldCloudsFrame* self)
{
    return (self->Low.Extinction > 0.0f) || (self->Mid.Extinction > 0.0f) || (self->HighDepth > 0.0f);
}

void WorldClouds_GetShadowFrame(const WorldCloudsFrame* self, Vector3Double origin, float viewHeight,
    Vector3 lightDirection, int32_t mapSize, WorldCloudShadowFrame* outFrame)
{
    double Plane = (double)self->Low.Base - (double)viewHeight;
    double Up = fmax((double)lightDirection.y, (double)WORLD_CLOUDS_SHADOW_MIN_UP);
    // Where the camera's ray toward the light crosses the plane, in the world, snapped to the map's texels.
    double Texel = WORLD_CLOUDS_SHADOW_EXTENT / (double)((mapSize < 1) ? 1 : mapSize);
    double CrossX = origin.X + (double)lightDirection.x * Plane / Up;
    double CrossZ = origin.Z + (double)lightDirection.z * Plane / Up;
    double CenterX = floor(CrossX / Texel + 0.5) * Texel;
    double CenterZ = floor(CrossZ / Texel + 0.5) * Texel;
    outFrame->Center[0] = (float)(CenterX - origin.X);
    outFrame->Center[1] = (float)(CenterZ - origin.Z);
    outFrame->Extent = (float)WORLD_CLOUDS_SHADOW_EXTENT;
    outFrame->Plane = (float)Plane;
    outFrame->Top = (float)((double)self->Low.Top - (double)viewHeight);
}

bool WorldClouds_GetShellSegment(double viewHeight, double cosZenith, double base, double top, double* outStart,
    double* outEnd)
{
    *outStart = 0.0;
    *outEnd = -1.0;
    double TopNear = 0.0;
    double TopFar = -1.0;
    if (!GetSphereDistances(viewHeight, cosZenith, top, &TopNear, &TopFar) || (TopFar <= 0.0))
    {
        return false;
    }
    double BaseNear = 0.0;
    double BaseFar = -1.0;
    bool IsBaseMet = GetSphereDistances(viewHeight, cosZenith, base, &BaseNear, &BaseFar);
    WorldAtmosphereRay ViewRay = { .Height = viewHeight, .CosZenith = cosZenith };
    double ToGround = WorldAtmosphere_GetDistanceToGround(ViewRay);
    if (viewHeight < base)
    {
        if (ToGround >= 0.0)
        {
            return false;
        }
        *outStart = fmax(BaseFar, 0.0);
        *outEnd = TopFar;
    }
    else
    {
        *outStart = fmax(TopNear, 0.0);
        *outEnd = (IsBaseMet && (BaseNear > *outStart)) ? BaseNear : TopFar;
        if (ToGround >= 0.0)
        {
            *outEnd = fmin(*outEnd, ToGround);
        }
    }
    return *outEnd > *outStart;
}
