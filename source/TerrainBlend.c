#include <math.h>
#include "TerrainBlend.h"
#include "wr/WRMemory.h"

/*
 * The terrain's material blend in C (include/TerrainBlend.h): the terrain shader's steps (shaders/terrain/fragment.frag
 * and shaders/common/value_noise.glsl), written in the same order with the same constants and formulas, so the two agree
 * within float rounding. A change here is a change there, and the other way round.
 */


// Macros.
/** Degrees per radian. */
#define DEGREES_PER_RADIAN 57.29577951308232f
/** The noise's lattice: cells wrap by the period (a power of two, so the wrap is a mask). */
#define NOISE_CELL_MASK ((int32_t)TERRAIN_BLEND_NOISE_PERIOD - 1)
/** The hash's constants (pcg3d, Jarzynski and Olano 2020: "Hash Functions for GPU Rendering") and the seed's mixers. */
#define PCG_MULTIPLIER 1664525U
#define PCG_INCREMENT 1013904223U
#define SEED_MIX_Y 0x9E3779B9U
#define SEED_MIX_Z 0x85EBCA6BU


// Static functions.
static float Clamp01(float value)
{
    return (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
}

/* GLSL's smoothstep for edge0 < edge1. */
static float SmoothStep(float edge0, float edge1, float value)
{
    float T = Clamp01((value - edge0) / (edge1 - edge0));
    return T * T * (3.0f - (2.0f * T));
}

/* 1 above a bound, ramping over its blend (a hard step without one). */
static float Rising(float value, float bound, float blend)
{
    if (blend > 0.0f)
    {
        return SmoothStep(bound - (0.5f * blend), bound + (0.5f * blend), value);
    }
    return (value >= bound) ? 1.0f : 0.0f;
}

/* 1 below a bound, ramping over its blend (a hard step without one). */
static float Falling(float value, float bound, float blend)
{
    if (blend > 0.0f)
    {
        return 1.0f - SmoothStep(bound - (0.5f * blend), bound + (0.5f * blend), value);
    }
    return (value <= bound) ? 1.0f : 0.0f;
}

/* pcg3d: three 32-bit words to three well-mixed ones (the shader's Pcg3d, operation for operation). */
static void Pcg3d(uint32_t* x, uint32_t* y, uint32_t* z)
{
    uint32_t X = (*x * PCG_MULTIPLIER) + PCG_INCREMENT;
    uint32_t Y = (*y * PCG_MULTIPLIER) + PCG_INCREMENT;
    uint32_t Z = (*z * PCG_MULTIPLIER) + PCG_INCREMENT;
    X += Y * Z;
    Y += Z * X;
    Z += X * Y;
    X ^= X >> 16U;
    Y ^= Y >> 16U;
    Z ^= Z >> 16U;
    X += Y * Z;
    Y += Z * X;
    Z += X * Y;
    *x = X;
    *y = Y;
    *z = Z;
}

/* The random value of a lattice cell (already wrapped onto the period), 0 to 1 in 2^24 steps. */
static float LatticeValue(int32_t cellX, int32_t cellY, int32_t cellZ, uint32_t seed)
{
    uint32_t X = (uint32_t)cellX ^ seed;
    uint32_t Y = (uint32_t)cellY ^ (seed * SEED_MIX_Y);
    uint32_t Z = (uint32_t)cellZ ^ (seed * SEED_MIX_Z);
    Pcg3d(&X, &Y, &Z);
    return (float)(X >> 8U) / 16777215.0f;
}

/* GLSL's mix. */
static float Mix(float a, float b, float t)
{
    return (a * (1.0f - t)) + (b * t);
}

/* The quintic fade of the noise's blend. */
static float NoiseFade(float t)
{
    return t * t * t * ((t * ((t * 6.0f) - 15.0f)) + 10.0f);
}

/* x^4, written out (the shader's too: pow would differ between them). */
static float FourthPower(float value)
{
    float Square = value * value;
    return Square * Square;
}


// Public functions: the steps.
void TerrainBlend_UnpackWeights(const uint8_t* bytes, uint32_t materialCount, float* outWeights)
{
    uint32_t Count = (materialCount > TERRAIN_MAX_MATERIALS) ? TERRAIN_MAX_MATERIALS : materialCount;
    for (uint32_t Index = 0U; Index < TERRAIN_MAX_MATERIALS; Index++)
    {
        outWeights[Index] = (Index < Count) ? ((float)bytes[Index] / 255.0f) : 0.0f;
    }
}

bool TerrainBlend_NormalizeWeights(float* ioWeights, uint32_t materialCount)
{
    uint32_t Count = (materialCount > TERRAIN_MAX_MATERIALS) ? TERRAIN_MAX_MATERIALS : materialCount;
    float Sum = 0.0f;
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        Sum += ioWeights[Index];
    }
    if (!(Sum >= TERRAIN_BLEND_WEIGHT_EPSILON))
    {
        for (uint32_t Index = 0U; Index < TERRAIN_MAX_MATERIALS; Index++)
        {
            ioWeights[Index] = (Index == 0U) ? 1.0f : 0.0f;
        }
        return false;
    }
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        ioWeights[Index] /= Sum;
    }
    return true;
}

float TerrainBlend_GetRangeFactor(const TerrainRuleRange* range, float value)
{
    if (!range->IsEnabled)
    {
        return 1.0f;
    }
    float Factor = 1.0f;
    if (range->HasMinimum)
    {
        Factor *= Rising(value, range->Minimum, range->Blend);
    }
    if (range->HasMaximum)
    {
        Factor *= Falling(value, range->Maximum, range->Blend);
    }
    return Factor;
}

float TerrainBlend_GetSlopeDegrees(Vector3 normal)
{
    float Up = (normal.y < -1.0f) ? -1.0f : ((normal.y > 1.0f) ? 1.0f : normal.y);
    return acosf(Up) * DEGREES_PER_RADIAN;
}

float TerrainBlend_GetValueNoise(Vector3 cellPosition, uint32_t seed)
{
    float BaseX = floorf(cellPosition.x);
    float BaseY = floorf(cellPosition.y);
    float BaseZ = floorf(cellPosition.z);
    float Ux = NoiseFade(cellPosition.x - BaseX);
    float Uy = NoiseFade(cellPosition.y - BaseY);
    float Uz = NoiseFade(cellPosition.z - BaseZ);
    int32_t X0 = (int32_t)BaseX & NOISE_CELL_MASK;
    int32_t Y0 = (int32_t)BaseY & NOISE_CELL_MASK;
    int32_t Z0 = (int32_t)BaseZ & NOISE_CELL_MASK;
    int32_t X1 = ((int32_t)BaseX + 1) & NOISE_CELL_MASK;
    int32_t Y1 = ((int32_t)BaseY + 1) & NOISE_CELL_MASK;
    int32_t Z1 = ((int32_t)BaseZ + 1) & NOISE_CELL_MASK;
    float Near = Mix(Mix(LatticeValue(X0, Y0, Z0, seed), LatticeValue(X1, Y0, Z0, seed), Ux),
        Mix(LatticeValue(X0, Y1, Z0, seed), LatticeValue(X1, Y1, Z0, seed), Ux), Uy);
    float Far = Mix(Mix(LatticeValue(X0, Y0, Z1, seed), LatticeValue(X1, Y0, Z1, seed), Ux),
        Mix(LatticeValue(X0, Y1, Z1, seed), LatticeValue(X1, Y1, Z1, seed), Ux), Uy);
    return Mix(Near, Far, Uz);
}

double TerrainBlend_WrapCoordinate(double value, double period)
{
    double Wrapped = value - (period * floor(value / period));
    // The division may round to the next whole number either way: keep the result inside [0, period).
    if (Wrapped < 0.0)
    {
        Wrapped += period;
    }
    if (Wrapped >= period)
    {
        Wrapped -= period;
    }
    return Wrapped;
}

float TerrainBlend_GetRuleStrength(const TerrainRule* rule, const TerrainBlendPlace* place)
{
    float Strength = rule->Opacity;
    if (!(Strength > 0.0f))
    {
        return 0.0f;
    }
    Strength *= TerrainBlend_GetRangeFactor(&rule->Slope, TerrainBlend_GetSlopeDegrees(place->WorldNormal));
    Strength *= TerrainBlend_GetRangeFactor(&rule->Height, (float)place->WorldHeight);
    if (rule->Noise.IsEnabled && (rule->Noise.Scale > 0.0f))
    {
        double Scale = (double)rule->Noise.Scale;
        double Period = (double)TERRAIN_BLEND_NOISE_PERIOD * Scale;
        Vector3 Cell =
        {
            (float)(TerrainBlend_WrapCoordinate(place->TerrainPosition.X, Period) / Scale),
            (float)(TerrainBlend_WrapCoordinate(place->TerrainPosition.Y, Period) / Scale),
            (float)(TerrainBlend_WrapCoordinate(place->TerrainPosition.Z, Period) / Scale)
        };
        float Noise = TerrainBlend_GetValueNoise(Cell, rule->Noise.Seed);
        Strength *= (1.0f - rule->Noise.Strength) + (rule->Noise.Strength * Noise);
    }
    return Strength;
}

void TerrainBlend_ApplyRules(const TerrainRule* rules, uint32_t ruleCount, uint32_t materialCount,
    const TerrainBlendPlace* place, float* ioWeights)
{
    uint32_t Count = (materialCount > TERRAIN_MAX_MATERIALS) ? TERRAIN_MAX_MATERIALS : materialCount;
    for (uint32_t Index = 0U; Index < ruleCount; Index++)
    {
        const TerrainRule* Rule = &rules[Index];
        if (Rule->Material >= Count)
        {
            continue;
        }
        float Strength = TerrainBlend_GetRuleStrength(Rule, place);
        if (!(Strength > 0.0f))
        {
            continue;
        }
        float Moved = 0.0f;
        for (uint32_t Covered = 0U; Covered < Count; Covered++)
        {
            if ((Covered == Rule->Material) || ((Rule->CoveredMaterials & (1U << Covered)) == 0U))
            {
                continue;
            }
            float Given = ioWeights[Covered] * Strength;
            ioWeights[Covered] -= Given;
            Moved += Given;
        }
        ioWeights[Rule->Material] += Moved;
    }
}

void TerrainBlend_GetHeightShares(const float* weights, const float* heights, const float* strengths,
    uint32_t materialCount, float* outShares)
{
    uint32_t Count = (materialCount > TERRAIN_MAX_MATERIALS) ? TERRAIN_MAX_MATERIALS : materialCount;
    float Scores[TERRAIN_MAX_MATERIALS];
    float Best = 0.0f;
    bool HasAny = false;
    for (uint32_t Index = 0U; Index < TERRAIN_MAX_MATERIALS; Index++)
    {
        outShares[Index] = 0.0f;
        Scores[Index] = 0.0f;
        if ((Index >= Count) || !(weights[Index] >= TERRAIN_BLEND_WEIGHT_EPSILON))
        {
            continue;
        }
        Scores[Index] = weights[Index] + (heights[Index] * strengths[Index]);
        Best = HasAny ? fmaxf(Best, Scores[Index]) : Scores[Index];
        HasAny = true;
    }
    if (!HasAny)
    {
        outShares[0] = 1.0f;
        return;
    }
    float Floor = Best - TERRAIN_BLEND_HEIGHT_BAND;
    float Total = 0.0f;
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        if (weights[Index] >= TERRAIN_BLEND_WEIGHT_EPSILON)
        {
            outShares[Index] = fmaxf(Scores[Index] - Floor, 0.0f);
            Total += outShares[Index];
        }
    }
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        outShares[Index] /= Total;
    }
}

uint32_t TerrainBlend_GetDominantMaterial(const float* shares, uint32_t materialCount)
{
    uint32_t Count = (materialCount > TERRAIN_MAX_MATERIALS) ? TERRAIN_MAX_MATERIALS : materialCount;
    uint32_t Dominant = 0U;
    for (uint32_t Index = 1U; Index < Count; Index++)
    {
        if (shares[Index] > shares[Dominant])
        {
            Dominant = Index;
        }
    }
    return Dominant;
}

Vector3 TerrainBlend_GetTriplanarWeights(Vector3 normal, TerrainTriplanarMode mode, float triplanarSlope)
{
    Vector3 Planar = { 0.0f, 1.0f, 0.0f };
    if ((mode != TerrainTriplanarMode_Steep) && (mode != TerrainTriplanarMode_Always))
    {
        return Planar;
    }
    Vector3 Sides = { FourthPower(normal.x), FourthPower(normal.y), FourthPower(normal.z) };
    float Sum = Sides.x + Sides.y + Sides.z;
    if (!(Sum > 0.0f))
    {
        return Planar;
    }
    Sides = (Vector3){ Sides.x / Sum, Sides.y / Sum, Sides.z / Sum };
    if (mode == TerrainTriplanarMode_Always)
    {
        return Sides;
    }
    float Threshold = triplanarSlope * DEGREES_PER_RADIAN;
    float Ramp = SmoothStep(Threshold - (0.5f * TERRAIN_BLEND_TRIPLANAR_RAMP_DEGREES),
        Threshold + (0.5f * TERRAIN_BLEND_TRIPLANAR_RAMP_DEGREES), TerrainBlend_GetSlopeDegrees(normal));
    return (Vector3){ Mix(Planar.x, Sides.x, Ramp), Mix(Planar.y, Sides.y, Ramp), Mix(Planar.z, Sides.z, Ramp) };
}

TerrainProjectionCoordinates TerrainBlend_GetProjectionCoordinates(Vector3Double terrainPosition, Vector3 terrainNormal,
    float tileSize)
{
    double Tile = (double)tileSize;
    float Qx = (float)(TerrainBlend_WrapCoordinate(terrainPosition.X, Tile) / Tile);
    float Qy = (float)(TerrainBlend_WrapCoordinate(terrainPosition.Y, Tile) / Tile);
    float Qz = (float)(TerrainBlend_WrapCoordinate(terrainPosition.Z, Tile) / Tile);
    float Sx = (terrainNormal.x < 0.0f) ? -1.0f : 1.0f;
    float Sy = (terrainNormal.y < 0.0f) ? -1.0f : 1.0f;
    float Sz = (terrainNormal.z < 0.0f) ? -1.0f : 1.0f;
    return (TerrainProjectionCoordinates)
    {
        .X = { -Sx * Qz, -Qy },
        .Y = { -Sy * Qx, -Qz },
        .Z = { Sz * Qx, -Qy }
    };
}

float TerrainBlend_SampleMaterialHeight(const TerrainMaterial* material, const TerrainBlendPlace* place)
{
    if (material == NULL)
    {
        return (float)TERRAIN_MATERIAL_FLAT_HEIGHT / 255.0f;
    }
    const TerrainMaterialSettings* Settings = TerrainMaterial_GetSettings(material);
    Vector3 Weights = TerrainBlend_GetTriplanarWeights(place->TerrainNormal, Settings->Triplanar,
        Settings->TriplanarSlope);
    TerrainProjectionCoordinates Coordinates = TerrainBlend_GetProjectionCoordinates(place->TerrainPosition,
        place->TerrainNormal, Settings->TileSize);
    float Height = 0.0f;
    if (Weights.x > 0.0f)
    {
        Height += Weights.x * TerrainMaterial_SampleHeight(material, Coordinates.X.x, Coordinates.X.y);
    }
    if (Weights.y > 0.0f)
    {
        Height += Weights.y * TerrainMaterial_SampleHeight(material, Coordinates.Y.x, Coordinates.Y.y);
    }
    if (Weights.z > 0.0f)
    {
        Height += Weights.z * TerrainMaterial_SampleHeight(material, Coordinates.Z.x, Coordinates.Z.y);
    }
    return Height;
}


// Public functions: a point of a terrain.
bool TerrainBlend_GetPlace(const TerrainAsset* terrain, uint32_t triangle, const float* barycentrics,
    const Matrix4Double* worldMatrix, TerrainBlendPlace* outPlace)
{
    Memory_Zero(outPlace, sizeof(*outPlace));
    const TerrainMesh* Ground = TerrainAsset_GetMesh(terrain);
    if (triangle >= TerrainMesh_GetTriangleCount(Ground))
    {
        return false;
    }
    const TerrainTriangle* Triangle = TerrainMesh_GetTriangle(Ground, triangle);
    Vector3Double Position = Vector3Double_Zero();
    Vector3 Normal = { 0.0f, 0.0f, 0.0f };
    for (uint32_t Corner = 0U; Corner < 3U; Corner++)
    {
        Vector3 CornerPosition = TerrainMesh_GetPosition(Ground, Triangle->_indices[Corner]);
        Vector3 CornerNormal = TerrainMesh_GetNormal(Ground, Triangle->_indices[Corner]);
        double Weight = (double)barycentrics[Corner];
        Position = Vector3Double_Add(Position, Vector3Double_Create((double)CornerPosition.x * Weight,
            (double)CornerPosition.y * Weight, (double)CornerPosition.z * Weight));
        Normal.x += CornerNormal.x * barycentrics[Corner];
        Normal.y += CornerNormal.y * barycentrics[Corner];
        Normal.z += CornerNormal.z * barycentrics[Corner];
    }
    Vector3Double Unit = Vector3Double_NormalizeOr(Vector3Double_Create((double)Normal.x, (double)Normal.y,
        (double)Normal.z), Vector3Double_Create(0.0, 1.0, 0.0));
    outPlace->TerrainPosition = Position;
    outPlace->TerrainNormal = (Vector3){ (float)Unit.X, (float)Unit.Y, (float)Unit.Z };
    outPlace->WorldHeight = Matrix4Double_TransformPoint(worldMatrix, Position).Y;
    // Normals go through the inverse transpose (an uneven scale tilts them as it tilts the surface).
    Matrix4Double Inverse;
    Matrix4Double NormalMatrix = *worldMatrix;
    if (Matrix4Double_TryInvertAffine(worldMatrix, &Inverse))
    {
        NormalMatrix = Matrix4Double_Transpose(&Inverse);
    }
    Vector3Double World = Vector3Double_NormalizeOr(Matrix4Double_TransformDirection(&NormalMatrix, Unit),
        Vector3Double_Create(0.0, 1.0, 0.0));
    outPlace->WorldNormal = (Vector3){ (float)World.X, (float)World.Y, (float)World.Z };
    return true;
}

bool TerrainBlend_EvaluatePoint(const TerrainAsset* terrain, uint32_t triangle, const float* barycentrics,
    const TerrainBlendPlace* place, TerrainBlendPoint* outPoint)
{
    Memory_Zero(outPoint, sizeof(*outPoint));
    const TerrainMesh* Ground = TerrainAsset_GetMesh(terrain);
    if (triangle >= TerrainMesh_GetTriangleCount(Ground))
    {
        return false;
    }
    const TerrainTriangle* Triangle = TerrainMesh_GetTriangle(Ground, triangle);
    uint32_t MaterialCount = TerrainMesh_GetMaterialCount(Ground);
    if ((Triangle->_flags & TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL) != 0U)
    {
        uint32_t Face = Triangle->_material;
        outPoint->IsFaceMaterial = true;
        outPoint->PaintedWeights[Face] = 1.0f;
        outPoint->RuleWeights[Face] = 1.0f;
        outPoint->Shares[Face] = 1.0f;
        outPoint->DominantMaterial = Face;
        return true;
    }
    for (uint32_t Corner = 0U; Corner < 3U; Corner++)
    {
        float Corners[TERRAIN_MAX_MATERIALS];
        TerrainBlend_UnpackWeights(TerrainMesh_GetWeights(Ground, Triangle->_indices[Corner]), MaterialCount, Corners);
        for (uint32_t Index = 0U; Index < TERRAIN_MAX_MATERIALS; Index++)
        {
            outPoint->PaintedWeights[Index] += barycentrics[Corner] * Corners[Index];
        }
    }
    TerrainBlend_NormalizeWeights(outPoint->PaintedWeights, MaterialCount);
    Memory_Copy(outPoint->PaintedWeights, outPoint->RuleWeights, sizeof(outPoint->RuleWeights));
    uint32_t RuleCount = 0U;
    const TerrainRule* Rules = TerrainAsset_GetRules(terrain, &RuleCount);
    TerrainBlend_ApplyRules(Rules, RuleCount, MaterialCount, place, outPoint->RuleWeights);
    float Heights[TERRAIN_MAX_MATERIALS];
    float Strengths[TERRAIN_MAX_MATERIALS];
    for (uint32_t Index = 0U; Index < TERRAIN_MAX_MATERIALS; Index++)
    {
        const TerrainMaterial* Source = TerrainAsset_GetMaterial(terrain, Index);
        bool HasWeight = outPoint->RuleWeights[Index] >= TERRAIN_BLEND_WEIGHT_EPSILON;
        Heights[Index] = HasWeight ? TerrainBlend_SampleMaterialHeight(Source, place) : 0.0f;
        Strengths[Index] = (Source != NULL) ? TerrainMaterial_GetSettings(Source)->HeightBlendStrength : 1.0f;
    }
    TerrainBlend_GetHeightShares(outPoint->RuleWeights, Heights, Strengths, MaterialCount, outPoint->Shares);
    outPoint->DominantMaterial = TerrainBlend_GetDominantMaterial(outPoint->Shares, MaterialCount);
    return true;
}
