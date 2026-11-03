#include <math.h>
#include "WorldWater.h"
#include "wr/WRMemory.h"

/*
 * Water as pure maths (source/WorldWater.h): the drift, the maps' frames, the waves' strength in the wind, the
 * absorption and the foam, as shaders/common/water.glsl takes them.
 */


// Static functions.
/* A value wrapped into [0, period) in double (the period above 0). */
static double Wrap(double value, double period)
{
    double Wrapped = value - (period * floor(value / period));
    return ((Wrapped >= period) || (Wrapped < 0.0)) ? 0.0 : Wrapped;
}

/* A tile size as the shader takes it: above 0 and finite, else 1. */
static double SanitizeTile(float tile)
{
    return (isfinite(tile) && (tile > 0.0f)) ? (double)tile : 1.0;
}

static float Smoothstep(float edge0, float edge1, float x)
{
    float T = fminf(fmaxf((x - edge0) / (edge1 - edge0), 0.0f), 1.0f);
    return T * T * (3.0f - (2.0f * T));
}

/* One map's frame: its turn (as floats, which the shader gets; the origin is turned with the same values in double), its
 * offset (the turned origin, less the drift, wrapped by the tile) and one over its tile. */
static WorldWaterMapFrame MakeMapFrame(Vector2Double place, Vector2Double drift, float turn, float tile)
{
    WorldWaterMapFrame Frame;
    float Turn = isfinite(turn) ? turn : 0.0f;
    Frame.Cos = cosf(Turn);
    Frame.Sin = sinf(Turn);
    double Cos = (double)Frame.Cos;
    double Sin = (double)Frame.Sin;
    double Tile = SanitizeTile(tile);
    double X = (Cos * place.X) + (Sin * place.Y) - drift.X;
    double Y = (-Sin * place.X) + (Cos * place.Y) - drift.Y;
    Frame.Offset[0] = (float)Wrap(isfinite(X) ? X : 0.0, Tile);
    Frame.Offset[1] = (float)Wrap(isfinite(Y) ? Y : 0.0, Tile);
    Frame.InverseTile = (float)(1.0 / Tile);
    return Frame;
}


// Functions (declared in WorldWater.h).
void WorldWaterDrift_Reset(WorldWaterDrift* self)
{
    Memory_Zero(self, sizeof(*self));
}

void WorldWaterDrift_Advance(WorldWaterDrift* self, float windDirection, float windSpeed, double seconds)
{
    if (!isfinite(seconds) || (seconds <= 0.0))
    {
        return;
    }
    self->Seconds += seconds;
    if (!isfinite(windDirection) || !isfinite(windSpeed) || (windSpeed <= 0.0f))
    {
        return;
    }
    double Travel = (double)windSpeed * seconds;
    self->Wind.X += sin((double)windDirection) * Travel;
    self->Wind.Y += cos((double)windDirection) * Travel;
}

Vector2Double WorldWater_GetDrift(const WaterMaterialSettings* settings, const WorldWaterDrift* drift, double speedShare)
{
    Vector2Double Result = { 0.0, 0.0 };
    if (!isfinite(speedShare))
    {
        return Result;
    }
    switch (settings->Flow)
    {
        case WaterFlow_Wind:
        {
            double Scale = speedShare * (double)settings->WindResponse * WORLD_WATER_WIND_DRIFT_SHARE;
            Result = (Vector2Double){ Scale * drift->Wind.X, Scale * drift->Wind.Y };
            break;
        }
        case WaterFlow_Fixed:
        {
            double Travel = speedShare * (double)settings->FlowSpeed * drift->Seconds;
            Result = (Vector2Double){ sin((double)settings->FlowAngle) * Travel, cos((double)settings->FlowAngle) * Travel };
            break;
        }
        case WaterFlow_Uv:
            Result = (Vector2Double){ 0.0, speedShare * (double)settings->FlowSpeed * drift->Seconds };
            break;
    }
    if (!isfinite(Result.X) || !isfinite(Result.Y))
    {
        return (Vector2Double){ 0.0, 0.0 };
    }
    return Result;
}

float WorldWater_GetWaveStrength(float waveStrength, float windResponse, float windSpeed)
{
    double Wind = isfinite(windSpeed) ? fmin(fmax((double)windSpeed, 0.0), WORLD_WATER_MAX_WIND) : 0.0;
    double Growth = sqrt((WORLD_WATER_CALM_SLOPE + (WORLD_WATER_SLOPE_PER_WIND * Wind))
        / (WORLD_WATER_CALM_SLOPE + (WORLD_WATER_SLOPE_PER_WIND * WORLD_WATER_REFERENCE_WIND)));
    double Mixed = 1.0 + ((double)windResponse * (Growth - 1.0));
    double Strength = (double)waveStrength * Mixed;
    return (isfinite(Strength) && (Strength > 0.0)) ? (float)Strength : 0.0f;
}

void WorldWater_GetFrame(const WaterMaterialSettings* settings, const WorldWaterDrift* drift, Vector3Double origin,
    float windSpeed, WorldWaterFrame* outFrame)
{
    Memory_Zero(outFrame, sizeof(*outFrame));
    outFrame->IsAlongUv = (settings->Flow == WaterFlow_Uv);
    // Along a river the maps lie on the mesh's coordinates, which carry no origin.
    Vector2Double Place = outFrame->IsAlongUv ? (Vector2Double){ 0.0, 0.0 } : (Vector2Double){ origin.X, origin.Z };
    outFrame->Maps[WaterMaterialMap_NormalA] = MakeMapFrame(Place,
        WorldWater_GetDrift(settings, drift, (double)settings->NormalASpeed), 0.0f, settings->NormalATileSize);
    outFrame->Maps[WaterMaterialMap_NormalB] = MakeMapFrame(Place,
        WorldWater_GetDrift(settings, drift, (double)settings->NormalBSpeed), settings->NormalBTurn,
        settings->NormalBTileSize);
    // The foam floats on the water: it moves with the flow itself.
    outFrame->Maps[WaterMaterialMap_Foam] = MakeMapFrame(Place, WorldWater_GetDrift(settings, drift, 1.0), 0.0f,
        settings->FoamTileSize);
    outFrame->WaveStrength = WorldWater_GetWaveStrength(settings->WaveStrength, settings->WindResponse, windSpeed);
}

Vector2 WorldWater_GetMapCoordinate(const WorldWaterMapFrame* frame, Vector2 place)
{
    float X = (frame->Cos * place.x) + (frame->Sin * place.y);
    float Y = (-frame->Sin * place.x) + (frame->Cos * place.y);
    return (Vector2){ (X + frame->Offset[0]) * frame->InverseTile, (Y + frame->Offset[1]) * frame->InverseTile };
}

Vector3 WorldWater_GetTransmittance(Vector3 absorption, float path)
{
    if (!(path < INFINITY))
    {
        return (Vector3){ 0.0f, 0.0f, 0.0f };
    }
    float Path = fmaxf(path, 0.0f);
    return (Vector3){ expf(-fmaxf(absorption.x, 0.0f) * Path), expf(-fmaxf(absorption.y, 0.0f) * Path),
        expf(-fmaxf(absorption.z, 0.0f) * Path) };
}

float WorldWater_GetFoam(float texel, float depth, float foamDepth, float foamStrength)
{
    if (!(foamDepth > 0.0f) || !(foamStrength > 0.0f) || !(depth < INFINITY))
    {
        return 0.0f;
    }
    float Ratio = fmaxf(depth, 0.0f) / foamDepth;
    return Smoothstep(Ratio - WORLD_WATER_FOAM_EDGE, Ratio + WORLD_WATER_FOAM_EDGE, texel) * fminf(foamStrength, 1.0f);
}
