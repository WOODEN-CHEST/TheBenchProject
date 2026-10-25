#include "TestSuites.h"
#include "TestTerrain.h"
#include "TerrainBlend.h"
#include "wr/WRFileSystem.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Macros.
#define DEGREES_TO_RADIANS 0.017453292519943295


// Static functions: helpers.
static bool IsNear(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static Vector3 NormalAtSlope(float degrees)
{
    float Radians = (float)((double)degrees * DEGREES_TO_RADIANS);
    return (Vector3){ sinf(Radians), cosf(Radians), 0.0f };
}

static TerrainRuleRange Range(bool hasMinimum, float minimum, bool hasMaximum, float maximum, float blend)
{
    return (TerrainRuleRange){ .IsEnabled = true, .HasMinimum = hasMinimum, .Minimum = minimum,
        .HasMaximum = hasMaximum, .Maximum = maximum, .Blend = blend };
}

/* A rule that applies @p material everywhere at @p opacity, covering every material. */
static TerrainRule Everywhere(uint32_t material, float opacity)
{
    TerrainRule Rule;
    Memory_Zero(&Rule, sizeof(Rule));
    Rule.Material = material;
    Rule.Opacity = opacity;
    Rule.CoveredMaterials = TERRAIN_ALL_MATERIALS;
    return Rule;
}

/* A place whose terrain space is the world's (an unturned terrain object at the origin). */
static TerrainBlendPlace Place(Vector3Double position, Vector3 normal)
{
    return (TerrainBlendPlace){ .TerrainPosition = position, .TerrainNormal = normal, .WorldHeight = position.Y,
        .WorldNormal = normal };
}

static float Sum(const float* values)
{
    float Total = 0.0f;
    for (uint32_t Index = 0U; Index < TERRAIN_MAX_MATERIALS; Index++)
    {
        Total += values[Index];
    }
    return Total;
}

/* A hand-built terrain material: one height map of @p width x 1 texels (@p heights), the rest of its settings at their
 * defaults but for the tile size and the triplanar mode. Release with FreeMaterial. */
static void MakeMaterial(TerrainMaterial* outMaterial, const uint8_t* heights, int32_t width, float tileSize,
    TerrainTriplanarMode triplanar, float heightBlendStrength)
{
    Memory_Zero(outMaterial, sizeof(*outMaterial));
    Error Result = TerrainMaterialSettings_SetDefaults(&outMaterial->_settings);
    Error_Deconstruct(&Result);
    outMaterial->_settings.TileSize = tileSize;
    outMaterial->_settings.Triplanar = triplanar;
    outMaterial->_settings.HeightBlendStrength = heightBlendStrength;
    outMaterial->_pixels = Memory_Allocate((size_t)width * 4U);
    for (int32_t Texel = 0; Texel < width; Texel++)
    {
        uint8_t* Target = &outMaterial->_pixels[Texel * 4];
        Target[0] = 100U;
        Target[1] = 100U;
        Target[2] = 100U;
        Target[3] = heights[Texel];
    }
    outMaterial->_maps[TerrainMaterialMapKind_AlbedoHeight] = (TerrainMaterialMap){ outMaterial->_pixels, width, 1 };
}

static void FreeMaterial(TerrainMaterial* material)
{
    TerrainMaterialSettings_Deconstruct(&material->_settings);
    Memory_Free(material->_pixels);
}

/* Reads a whole repository file as text (NUL-terminated); false with a failed check when it cannot. */
static bool ReadRepositoryText(TestRun* run, const char* relativePath, GenericBuffer* outText)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 256U, sizeof(unsigned char));
    bool IsRead = TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, relativePath, &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, outText));
    Memory_Free(Path._data);
    if (IsRead)
    {
        GenericBuffer_AppendByte(outText, 0U);
    }
    return IsRead;
}

/* The number after "<name> = " in a shader's text (a GLSL constant), or NAN when it is not there. A quotient ("0.5/255.0")
 * is divided out. */
static double ShaderConstant(const GenericBuffer* text, const char* name)
{
    char Pattern[128];
    snprintf(Pattern, sizeof(Pattern), "%s = ", name);
    const char* Found = strstr((const char*)text->_data, Pattern);
    if (Found == NULL)
    {
        return NAN;
    }
    char* End = NULL;
    double Value = strtod(Found + strlen(Pattern), &End);
    if ((End != NULL) && (*End == '/'))
    {
        Value /= strtod(End + 1, NULL);
    }
    return Value;
}


// Tests: the weights.
/* A vertex's sixteen bytes are its weights, byte / 255 for the palette's materials and 0 past it (the four attributes
 * carry bytes 4k to 4k + 3); a palette larger than sixteen is clamped. */
static void WeightsUnpackFromTheirBytes(TestRun* run)
{
    uint8_t Bytes[TERRAIN_MAX_MATERIALS];
    for (uint32_t Index = 0U; Index < TERRAIN_MAX_MATERIALS; Index++)
    {
        Bytes[Index] = (uint8_t)(Index * 17U);
    }
    float Weights[TERRAIN_MAX_MATERIALS];
    TerrainBlend_UnpackWeights(Bytes, 5U, Weights);
    bool IsUnpacked = true;
    for (uint32_t Index = 0U; Index < TERRAIN_MAX_MATERIALS; Index++)
    {
        float Expected = (Index < 5U) ? ((float)(Index * 17U) / 255.0f) : 0.0f;
        IsUnpacked = IsUnpacked && (Weights[Index] == Expected);
    }
    TEST_CHECK(run, IsUnpacked);
    TerrainBlend_UnpackWeights(Bytes, 40U, Weights);
    TEST_CHECK(run, (Weights[15] == 1.0f) && (Weights[12] == (204.0f / 255.0f)));
}

/* Weights are divided by their sum; with none (or a sum below half a byte's step) the first material takes the point. */
static void WeightsNormalizeOrGiveTheFirstMaterial(TestRun* run)
{
    float Weights[TERRAIN_MAX_MATERIALS] = { 0.1f, 0.3f, 0.0f, 0.4f, 9.0f };
    TEST_CHECK(run, TerrainBlend_NormalizeWeights(Weights, 4U));
    TEST_CHECK(run, IsNear(Weights[0], 0.125f, 1e-6f) && IsNear(Weights[1], 0.375f, 1e-6f) && (Weights[2] == 0.0f)
        && IsNear(Weights[3], 0.5f, 1e-6f) && (Weights[4] == 9.0f));
    float Empty[TERRAIN_MAX_MATERIALS] = { 0.0f };
    TEST_CHECK(run, !TerrainBlend_NormalizeWeights(Empty, 4U) && (Empty[0] == 1.0f) && (Sum(Empty) == 1.0f));
    float Faint[TERRAIN_MAX_MATERIALS] = { 0.0f, 0.001f, 0.0005f };
    TEST_CHECK(run, !TerrainBlend_NormalizeWeights(Faint, 3U) && (Faint[0] == 1.0f) && (Faint[1] == 0.0f));
    // One byte's weight is enough.
    float OneByte[TERRAIN_MAX_MATERIALS] = { 0.0f, 1.0f / 255.0f };
    TEST_CHECK(run, TerrainBlend_NormalizeWeights(OneByte, 2U) && (OneByte[1] == 1.0f) && (OneByte[0] == 0.0f));
}


// Tests: the rules.
/* A range is 1 inside, a smooth step across its blend centred on each bound, a hard step without a blend (the bounds
 * inside), and 1 for a rule without the condition. */
static void ARangeRampsAcrossItsBlend(TestRun* run)
{
    TerrainRuleRange Above = Range(true, 30.0f, false, 0.0f, 10.0f);
    TEST_CHECK(run, (TerrainBlend_GetRangeFactor(&Above, 24.0f) == 0.0f)
        && (TerrainBlend_GetRangeFactor(&Above, 25.0f) == 0.0f) && IsNear(TerrainBlend_GetRangeFactor(&Above, 30.0f), 0.5f,
        1e-6f) && IsNear(TerrainBlend_GetRangeFactor(&Above, 27.5f), 0.15625f, 1e-6f)
        && (TerrainBlend_GetRangeFactor(&Above, 35.0f) == 1.0f) && (TerrainBlend_GetRangeFactor(&Above, 170.0f) == 1.0f));
    TerrainRuleRange Below = Range(false, 0.0f, true, -4.5f, 2.0f);
    TEST_CHECK(run, (TerrainBlend_GetRangeFactor(&Below, -10.0f) == 1.0f)
        && IsNear(TerrainBlend_GetRangeFactor(&Below, -4.5f), 0.5f, 1e-6f)
        && (TerrainBlend_GetRangeFactor(&Below, -3.5f) == 0.0f) && (TerrainBlend_GetRangeFactor(&Below, 50.0f) == 0.0f));
    TerrainRuleRange Band = Range(true, 10.0f, true, 20.0f, 4.0f);
    TEST_CHECK(run, (TerrainBlend_GetRangeFactor(&Band, 15.0f) == 1.0f) && (TerrainBlend_GetRangeFactor(&Band, 7.0f) == 0.0f)
        && (TerrainBlend_GetRangeFactor(&Band, 23.0f) == 0.0f) && IsNear(TerrainBlend_GetRangeFactor(&Band, 20.0f), 0.5f,
        1e-6f));
    TerrainRuleRange Hard = Range(true, 30.0f, true, 40.0f, 0.0f);
    TEST_CHECK(run, (TerrainBlend_GetRangeFactor(&Hard, 29.999f) == 0.0f) && (TerrainBlend_GetRangeFactor(&Hard, 30.0f)
        == 1.0f) && (TerrainBlend_GetRangeFactor(&Hard, 40.0f) == 1.0f) && (TerrainBlend_GetRangeFactor(&Hard, 40.001f)
        == 0.0f));
    TerrainRuleRange Off = Hard;
    Off.IsEnabled = false;
    TEST_CHECK(run, TerrainBlend_GetRangeFactor(&Off, -1000.0f) == 1.0f);
}

/* The slope is the angle from straight up: 0 flat, 90 a wall, 180 an underside. */
static void TheSlopeIsTheAngleFromUp(TestRun* run)
{
    TEST_CHECK(run, IsNear(TerrainBlend_GetSlopeDegrees((Vector3){ 0.0f, 1.0f, 0.0f }), 0.0f, 1e-4f)
        && IsNear(TerrainBlend_GetSlopeDegrees(NormalAtSlope(35.0f)), 35.0f, 1e-3f)
        && IsNear(TerrainBlend_GetSlopeDegrees((Vector3){ 0.0f, 0.0f, -1.0f }), 90.0f, 1e-4f)
        && IsNear(TerrainBlend_GetSlopeDegrees((Vector3){ 0.0f, -1.0f, 0.0f }), 180.0f, 1e-4f));
    // A normal a hair longer than one (rounding) still reads.
    TEST_CHECK(run, IsNear(TerrainBlend_GetSlopeDegrees((Vector3){ 0.0f, 1.0000001f, 0.0f }), 0.0f, 1e-3f));
}

/* The noise: the values of an independent implementation (pcg3d with the same seeds, quintic fade), periodic over 256
 * cells, deterministic, between 0 and 1, continuous, centred on 0.5. */
static void TheValueNoiseIsTheShadersNoise(TestRun* run)
{
    // From a Python implementation of the same integers (asset_source holds none: computed once, written here).
    static const struct { float X; float Y; float Z; uint32_t Seed; double Value; } Pinned[] =
    {
        { 0.0f, 0.0f, 0.0f, 0U, 0.6081518893332415 },
        { 3.0f, 7.0f, 11.0f, 0U, 0.6985318481047063 },
        { 3.0f, 7.0f, 11.0f, 5U, 0.07702136498816997 },
        { 0.5f, 0.25f, 0.75f, 11U, 0.5663223036356144 },
        { -3.3f, 12.7f, 200.1f, 7U, 0.8635889982947526 },
        { 255.5f, 255.5f, 255.5f, 4000000000U, 0.3287037434401359 },
    };
    for (size_t Index = 0U; Index < (sizeof(Pinned) / sizeof(Pinned[0])); Index++)
    {
        float Value = TerrainBlend_GetValueNoise((Vector3){ Pinned[Index].X, Pinned[Index].Y, Pinned[Index].Z },
            Pinned[Index].Seed);
        if (!TEST_CHECK(run, fabs((double)Value - Pinned[Index].Value) < 2e-6))
        {
            printf("        sample %zu: %.9f, expected %.9f\n", Index, (double)Value, Pinned[Index].Value);
        }
    }
    // Periodic: a whole period along any axis (also backwards) is the same, exactly (at fractions a float holds either
    // way).
    Vector3 Point = { 17.375f, -4.8125f, 91.0625f };
    float Here = TerrainBlend_GetValueNoise(Point, 3U);
    TEST_CHECK(run, (TerrainBlend_GetValueNoise((Vector3){ Point.x + 256.0f, Point.y, Point.z }, 3U) == Here)
        && (TerrainBlend_GetValueNoise((Vector3){ Point.x, Point.y - 256.0f, Point.z }, 3U) == Here)
        && (TerrainBlend_GetValueNoise((Vector3){ Point.x, Point.y, Point.z + 512.0f }, 3U) == Here));
    // Deterministic, seeded, bounded, continuous, about 0.5 on average.
    double Total = 0.0;
    bool IsBounded = true;
    bool IsContinuous = true;
    bool DoSeedsDiffer = false;
    for (int32_t Step = 0; Step < 4000; Step++)
    {
        Vector3 Sample = { (float)Step * 0.173f, (float)Step * 0.041f, (float)Step * 0.097f };
        float Value = TerrainBlend_GetValueNoise(Sample, 9U);
        float Again = TerrainBlend_GetValueNoise(Sample, 9U);
        float Nudged = TerrainBlend_GetValueNoise((Vector3){ Sample.x + 0.001f, Sample.y, Sample.z }, 9U);
        IsBounded = IsBounded && (Value >= 0.0f) && (Value <= 1.0f) && (Value == Again);
        IsContinuous = IsContinuous && (fabsf(Nudged - Value) < 0.01f);
        DoSeedsDiffer = DoSeedsDiffer || (TerrainBlend_GetValueNoise(Sample, 10U) != Value);
        Total += (double)Value;
    }
    TEST_CHECK(run, IsBounded && IsContinuous && DoSeedsDiffer);
    TEST_CHECK(run, fabs((Total / 4000.0) - 0.5) < 0.05);
}

/* Wrapping keeps a coordinate in [0, period), negative ones and those a rounding away from the period included. */
static void WrappingStaysInOnePeriod(TestRun* run)
{
    TEST_CHECK(run, (TerrainBlend_WrapCoordinate(-0.5, 2.0) == 1.5) && (TerrainBlend_WrapCoordinate(2.0, 2.0) == 0.0)
        && (TerrainBlend_WrapCoordinate(10000000.25, 2.0) == 0.25) && (TerrainBlend_WrapCoordinate(5.0, 2.5) == 0.0));
    double Tiny = TerrainBlend_WrapCoordinate(-1e-300, 2.0);
    TEST_CHECK(run, (Tiny >= 0.0) && (Tiny < 2.0));
    double Thirds = TerrainBlend_WrapCoordinate(-7.0, 0.1);
    TEST_CHECK(run, (Thirds >= 0.0) && (Thirds < 0.1));
    // 1.7 / 0.1 rounds up to 17 although 1.7 lies just under 17 x 0.1: the plain formula gives -2.2e-16 here.
    double RoundedUp = TerrainBlend_WrapCoordinate(1.7, 0.1);
    TEST_CHECK(run, (RoundedUp >= 0.0) && (RoundedUp < 0.1));
}

/* A rule's strength is its opacity times its slope's, height's and noise's factors. */
static void ARuleStrengthMultipliesItsConditions(TestRun* run)
{
    Vector3 Flat = { 0.0f, 1.0f, 0.0f };
    Vector3 Wall = { 1.0f, 0.0f, 0.0f };
    TerrainBlendPlace LowFlat = Place((Vector3Double){ 3.0, -6.0, 2.0 }, Flat);
    TerrainBlendPlace LowWall = Place((Vector3Double){ 3.0, -6.0, 2.0 }, Wall);
    TerrainBlendPlace HighWall = Place((Vector3Double){ 3.0, 4.0, 2.0 }, Wall);
    TerrainRule Rule = Everywhere(1U, 0.5f);
    TEST_CHECK(run, (TerrainBlend_GetRuleStrength(&Rule, &LowFlat) == 0.5f)
        && (TerrainBlend_GetRuleStrength(&Rule, &HighWall) == 0.5f));
    Rule.Slope = Range(true, 30.0f, false, 0.0f, 0.0f);
    TEST_CHECK(run, (TerrainBlend_GetRuleStrength(&Rule, &LowFlat) == 0.0f)
        && (TerrainBlend_GetRuleStrength(&Rule, &LowWall) == 0.5f));
    Rule.Height = Range(false, 0.0f, true, -4.5f, 0.0f);
    TEST_CHECK(run, (TerrainBlend_GetRuleStrength(&Rule, &LowWall) == 0.5f)
        && (TerrainBlend_GetRuleStrength(&Rule, &HighWall) == 0.0f));
    // The slope and the height are the world's, whatever the terrain's own normal and position say.
    TerrainBlendPlace Turned = LowWall;
    Turned.TerrainNormal = Flat;
    Turned.TerrainPosition.Y = 40.0;
    TEST_CHECK(run, TerrainBlend_GetRuleStrength(&Rule, &Turned) == 0.5f);
    Turned.WorldHeight = 40.0;
    TEST_CHECK(run, TerrainBlend_GetRuleStrength(&Rule, &Turned) == 0.0f);
    // Noise of strength 0 changes nothing; of strength 1 it is the noise at the point (its cell: the position in the
    // terrain's space over the cell size, wrapped by the period); in between, 1 - s + s x noise.
    Rule.Noise = (TerrainRuleNoise){ .IsEnabled = true, .Scale = 2.0f, .Strength = 0.0f, .Seed = 21U };
    TEST_CHECK(run, TerrainBlend_GetRuleStrength(&Rule, &LowWall) == 0.5f);
    Rule.Noise.Strength = 1.0f;
    float Noise = TerrainBlend_GetValueNoise((Vector3){ 1.5f, (float)(TerrainBlend_WrapCoordinate(-6.0, 512.0) / 2.0),
        1.0f }, 21U);
    TEST_CHECK(run, IsNear(TerrainBlend_GetRuleStrength(&Rule, &LowWall), 0.5f * Noise, 1e-6f));
    Rule.Noise.Strength = 0.25f;
    TEST_CHECK(run, IsNear(TerrainBlend_GetRuleStrength(&Rule, &LowWall), 0.5f * (0.75f + (0.25f * Noise)), 1e-6f));
    // The noise lies in the terrain's space: a whole period over (here 2048 cells) it is the same.
    TerrainBlendPlace Over = LowWall;
    Over.TerrainPosition = (Vector3Double){ 3.0 + 4096.0, -6.0, 2.0 - 512.0 };
    TEST_CHECK(run, TerrainBlend_GetRuleStrength(&Rule, &Over) == TerrainBlend_GetRuleStrength(&Rule, &LowWall));
    // ... and the terrain's height is its height: raised in the world (still under the height's bound), the same noise.
    TerrainBlendPlace Raised = LowWall;
    Raised.WorldHeight = -5.0;
    TEST_CHECK(run, TerrainBlend_GetRuleStrength(&Rule, &Raised) == TerrainBlend_GetRuleStrength(&Rule, &LowWall));
    Rule.Opacity = 0.0f;
    TEST_CHECK(run, TerrainBlend_GetRuleStrength(&Rule, &LowWall) == 0.0f);
}

/* Rules move weight toward their material in order (the order matters), keeping the sum; "only_on" limits what a rule
 * covers; a rule naming a material past the palette does nothing. */
static void RulesMoveWeightInOrder(TestRun* run)
{
    TerrainBlendPlace Here = Place((Vector3Double){ 0.0, 0.0, 0.0 }, (Vector3){ 0.0f, 1.0f, 0.0f });
    TerrainRule Rules[2] = { Everywhere(1U, 0.5f), Everywhere(2U, 0.5f) };
    float Weights[TERRAIN_MAX_MATERIALS] = { 1.0f };
    TerrainBlend_ApplyRules(Rules, 2U, 3U, &Here, Weights);
    TEST_CHECK(run, IsNear(Weights[0], 0.25f, 1e-6f) && IsNear(Weights[1], 0.25f, 1e-6f) && IsNear(Weights[2], 0.5f, 1e-6f)
        && IsNear(Sum(Weights), 1.0f, 1e-6f));
    TerrainRule Reversed[2] = { Rules[1], Rules[0] };
    float Other[TERRAIN_MAX_MATERIALS] = { 1.0f };
    TerrainBlend_ApplyRules(Reversed, 2U, 3U, &Here, Other);
    TEST_CHECK(run, IsNear(Other[0], 0.25f, 1e-6f) && IsNear(Other[1], 0.5f, 1e-6f) && IsNear(Other[2], 0.25f, 1e-6f));
    // Rock (2) over grass (0) only: the painted gravel (3) keeps its weight.
    TerrainRule Rock = Everywhere(2U, 1.0f);
    Rock.CoveredMaterials = 1U << 0U;
    float Painted[TERRAIN_MAX_MATERIALS] = { 0.5f, 0.0f, 0.0f, 0.5f };
    TerrainBlend_ApplyRules(&Rock, 1U, 4U, &Here, Painted);
    TEST_CHECK(run, (Painted[0] == 0.0f) && (Painted[2] == 0.5f) && (Painted[3] == 0.5f));
    TerrainRule Beyond = Everywhere(5U, 1.0f);
    float Kept[TERRAIN_MAX_MATERIALS] = { 0.5f, 0.5f };
    TerrainBlend_ApplyRules(&Beyond, 1U, 2U, &Here, Kept);
    TEST_CHECK(run, (Kept[0] == 0.5f) && (Kept[1] == 0.5f));
    // A rule never takes its own material's weight.
    TerrainRule Itself = Everywhere(0U, 1.0f);
    float Alone[TERRAIN_MAX_MATERIALS] = { 0.6f, 0.4f };
    TerrainBlend_ApplyRules(&Itself, 1U, 2U, &Here, Alone);
    TEST_CHECK(run, IsNear(Alone[0], 1.0f, 1e-6f) && (Alone[1] == 0.0f));
}


// Tests: height blending.
/* Each material with weight scores its weight plus its height times its strength; those within the band of the best
 * share the point by how far they reach above the band's floor. */
static void HeightBlendingSharesByScore(TestRun* run)
{
    float Strengths[TERRAIN_MAX_MATERIALS];
    for (uint32_t Index = 0U; Index < TERRAIN_MAX_MATERIALS; Index++)
    {
        Strengths[Index] = 1.0f;
    }
    float Shares[TERRAIN_MAX_MATERIALS];
    float Single[TERRAIN_MAX_MATERIALS] = { 0.0f, 1.0f };
    float AnyHeights[TERRAIN_MAX_MATERIALS] = { 0.9f, 0.1f };
    TerrainBlend_GetHeightShares(Single, AnyHeights, Strengths, 2U, Shares);
    TEST_CHECK(run, (Shares[0] == 0.0f) && (Shares[1] == 1.0f));
    // Far apart: the higher stone takes the point alone.
    float Even[TERRAIN_MAX_MATERIALS] = { 0.5f, 0.5f };
    TerrainBlend_GetHeightShares(Even, AnyHeights, Strengths, 2U, Shares);
    TEST_CHECK(run, (Shares[0] == 1.0f) && (Shares[1] == 0.0f));
    // Within the band: 1.05 and 1.0 against a floor of 0.95 share 0.1 : 0.05.
    float Close[TERRAIN_MAX_MATERIALS] = { 0.55f, 0.5f };
    TerrainBlend_GetHeightShares(Even, Close, Strengths, 2U, Shares);
    TEST_CHECK(run, IsNear(Shares[0], 2.0f / 3.0f, 1e-5f) && IsNear(Shares[1], 1.0f / 3.0f, 1e-5f));
    // A tie shares equally.
    float Tied[TERRAIN_MAX_MATERIALS] = { 0.4f, 0.4f };
    TerrainBlend_GetHeightShares(Even, Tied, Strengths, 2U, Shares);
    TEST_CHECK(run, (Shares[0] == 0.5f) && (Shares[1] == 0.5f));
    // A strength of 0 blends by the weights alone.
    float Zero[TERRAIN_MAX_MATERIALS] = { 0.0f, 0.0f };
    float Weighted[TERRAIN_MAX_MATERIALS] = { 0.7f, 0.3f };
    float Highest[TERRAIN_MAX_MATERIALS] = { 0.0f, 1.0f };
    TerrainBlend_GetHeightShares(Weighted, Highest, Zero, 2U, Shares);
    TEST_CHECK(run, (Shares[0] == 1.0f) && (Shares[1] == 0.0f));
    // A material without weight does not take part, however high it stands; with no weight at all the first one does.
    float Faint[TERRAIN_MAX_MATERIALS] = { 1.0f, TERRAIN_BLEND_WEIGHT_EPSILON * 0.5f };
    TerrainBlend_GetHeightShares(Faint, Highest, Strengths, 2U, Shares);
    TEST_CHECK(run, (Shares[0] == 1.0f) && (Shares[1] == 0.0f));
    // ... nor does it lift the band's floor over the others (here its score would stand 3 above the first one's).
    float Strong[TERRAIN_MAX_MATERIALS] = { 1.0f, 4.0f };
    TerrainBlend_GetHeightShares(Faint, Highest, Strong, 2U, Shares);
    TEST_CHECK(run, (Shares[0] == 1.0f) && (Shares[1] == 0.0f));
    float None[TERRAIN_MAX_MATERIALS] = { 0.0f };
    TerrainBlend_GetHeightShares(None, Highest, Strengths, 2U, Shares);
    TEST_CHECK(run, (Shares[0] == 1.0f) && (Shares[1] == 0.0f));
    // Across a transition the incoming material's share never falls as its weight grows, and the shares sum to 1.
    float Previous = -1.0f;
    bool IsMonotonic = true;
    bool AreWhole = true;
    float Bumpy[TERRAIN_MAX_MATERIALS] = { 0.2f, 0.7f };
    for (int32_t Step = 0; Step <= 100; Step++)
    {
        float T = (float)Step / 100.0f;
        float Mixed[TERRAIN_MAX_MATERIALS] = { 1.0f - T, T };
        TerrainBlend_GetHeightShares(Mixed, Bumpy, Strengths, 2U, Shares);
        IsMonotonic = IsMonotonic && (Shares[1] >= Previous);
        AreWhole = AreWhole && IsNear(Sum(Shares), 1.0f, 1e-5f);
        Previous = Shares[1];
    }
    TEST_CHECK(run, IsMonotonic && AreWhole && (Previous == 1.0f));
}

/* The dominant material is the largest share, the lowest index among equal ones. */
static void TheDominantMaterialIsTheLargestShare(TestRun* run)
{
    float Shares[TERRAIN_MAX_MATERIALS] = { 0.2f, 0.4f, 0.4f };
    TEST_CHECK(run, TerrainBlend_GetDominantMaterial(Shares, 3U) == 1U);
    float Last[TERRAIN_MAX_MATERIALS] = { 0.1f, 0.2f, 0.3f, 0.4f };
    TEST_CHECK(run, (TerrainBlend_GetDominantMaterial(Last, 4U) == 3U) && (TerrainBlend_GetDominantMaterial(Last, 3U) == 2U));
}


// Tests: sampling.
/* Planar is from above only; triplanar weighs the sides by the normal's fourth powers; "steep" turns from one to the
 * other across its ramp, centred on its slope. */
static void TriplanarWeightsFollowTheNormal(TestRun* run)
{
    Vector3 Up = { 0.0f, 1.0f, 0.0f };
    Vector3 Wall = { 1.0f, 0.0f, 0.0f };
    Vector3 Diagonal = { 0.70710678f, 0.70710678f, 0.0f };
    float Slope = (float)(40.0 * DEGREES_TO_RADIANS);
    Vector3 Off = TerrainBlend_GetTriplanarWeights(Wall, TerrainTriplanarMode_Off, Slope);
    TEST_CHECK(run, (Off.x == 0.0f) && (Off.y == 1.0f) && (Off.z == 0.0f));
    Vector3 Always = TerrainBlend_GetTriplanarWeights(Wall, TerrainTriplanarMode_Always, Slope);
    TEST_CHECK(run, (Always.x == 1.0f) && (Always.y == 0.0f));
    Always = TerrainBlend_GetTriplanarWeights(Up, TerrainTriplanarMode_Always, Slope);
    TEST_CHECK(run, (Always.y == 1.0f) && (Always.x == 0.0f));
    Always = TerrainBlend_GetTriplanarWeights(Diagonal, TerrainTriplanarMode_Always, Slope);
    TEST_CHECK(run, IsNear(Always.x, 0.5f, 1e-5f) && IsNear(Always.y, 0.5f, 1e-5f) && (Always.z == 0.0f));
    // Fourth powers: at 30 degrees the side gets 0.0625 / (0.0625 + 0.5625) = 0.1 (squares would give it 0.25).
    Always = TerrainBlend_GetTriplanarWeights(NormalAtSlope(30.0f), TerrainTriplanarMode_Always, Slope);
    TEST_CHECK(run, IsNear(Always.x, 0.1f, 1e-5f) && IsNear(Always.y, 0.9f, 1e-5f));
    Vector3 Underside = TerrainBlend_GetTriplanarWeights((Vector3){ 0.0f, -1.0f, 0.0f }, TerrainTriplanarMode_Always,
        Slope);
    TEST_CHECK(run, Underside.y == 1.0f);
    // Steep: planar below 35 degrees, triplanar above 45, half way at 40.
    Vector3 Gentle = TerrainBlend_GetTriplanarWeights(NormalAtSlope(34.0f), TerrainTriplanarMode_Steep, Slope);
    TEST_CHECK(run, (Gentle.x == 0.0f) && (Gentle.y == 1.0f));
    Vector3 Steep = TerrainBlend_GetTriplanarWeights(NormalAtSlope(46.0f), TerrainTriplanarMode_Steep, Slope);
    Vector3 Full = TerrainBlend_GetTriplanarWeights(NormalAtSlope(46.0f), TerrainTriplanarMode_Always, Slope);
    TEST_CHECK(run, IsNear(Steep.x, Full.x, 1e-6f) && IsNear(Steep.y, Full.y, 1e-6f));
    Vector3 Middle = TerrainBlend_GetTriplanarWeights(NormalAtSlope(40.0f), TerrainTriplanarMode_Steep, Slope);
    Vector3 MiddleFull = TerrainBlend_GetTriplanarWeights(NormalAtSlope(40.0f), TerrainTriplanarMode_Always, Slope);
    TEST_CHECK(run, IsNear(Middle.x, 0.5f * MiddleFull.x, 1e-4f) && IsNear(Middle.y, 0.5f + (0.5f * MiddleFull.y), 1e-4f)
        && IsNear(Middle.x + Middle.y + Middle.z, 1.0f, 1e-6f));
}

/* Each side's coordinates: the image upright and unmirrored seen from outside, in repeats, wrapped in double. */
static void ProjectionCoordinatesFollowTheSides(TestRun* run)
{
    Vector3Double Point = { 1.0, 2.0, 3.0 };
    TerrainProjectionCoordinates Up = TerrainBlend_GetProjectionCoordinates(Point, (Vector3){ 0.0f, 1.0f, 0.0f }, 4.0f);
    TEST_CHECK(run, (Up.Y.x == -0.25f) && (Up.Y.y == -0.75f) && (Up.X.x == -0.75f) && (Up.X.y == -0.5f)
        && (Up.Z.x == 0.25f) && (Up.Z.y == -0.5f));
    TerrainProjectionCoordinates Back = TerrainBlend_GetProjectionCoordinates(Point, (Vector3){ -0.6f, -0.6f, -0.53f },
        4.0f);
    TEST_CHECK(run, (Back.X.x == 0.75f) && (Back.Y.x == 0.25f) && (Back.Z.x == -0.25f) && (Back.Y.y == -0.75f));
    // A whole repeat (or many, far out) away: the same coordinates.
    Vector3Double Far = { 1.0 + (4.0 * 25000.0), 2.0 - 4000.0, 3.0 + (4.0 * 12345.0) };
    TerrainProjectionCoordinates Again = TerrainBlend_GetProjectionCoordinates(Far, (Vector3){ 0.0f, 1.0f, 0.0f }, 4.0f);
    TEST_CHECK(run, (Again.Y.x == Up.Y.x) && (Again.Y.y == Up.Y.y) && (Again.X.y == Up.X.y));
}

/* At the edge of a terrain's extent (8 km from its origin) the renderer's sum (a chunk's centre wrapped by the tile size
 * in double, plus the exact offset from it in float) gives the fraction it gives at the origin, within float rounding;
 * rounding the far position to float first would be off by tens of millionths of a repeat (and more, the larger the
 * extent). */
static void TextureCoordinatesStandStillFarOut(TestRun* run)
{
    const double TileSize = 2.0;
    const double Offsets[3] = { 7.25, -13.0625, 0.4 };
    const double Centres[2] = { 16.375, 8000.0 + 16.375 };
    double Fractions[2][3];
    for (int32_t Place = 0; Place < 2; Place++)
    {
        for (int32_t Index = 0; Index < 3; Index++)
        {
            float Coordinate = ((float)Offsets[Index] + (float)TerrainBlend_WrapCoordinate(Centres[Place], TileSize))
                * (float)(1.0 / TileSize);
            Fractions[Place][Index] = (double)Coordinate - floor((double)Coordinate);
        }
    }
    bool IsSame = true;
    for (int32_t Index = 0; Index < 3; Index++)
    {
        IsSame = IsSame && (fabs(Fractions[0][Index] - Fractions[1][Index]) < 1e-6);
    }
    TEST_CHECK(run, IsSame);
    // Rounded to float first, 8 km out: off by about fifty millionths of a repeat (a float's step there is 1/2048 m).
    float Rounded = ((float)(Centres[1] + Offsets[2])) * 0.5f;
    double Naive = (double)Rounded - floor((double)Rounded);
    TEST_CHECK(run, fabs(Naive - Fractions[0][2]) > 2e-5);
    // And the noise the same way: its period wraps the centre, the cells match.
    double Period = (double)TERRAIN_BLEND_NOISE_PERIOD * 2.5;
    float Near = TerrainBlend_GetValueNoise((Vector3){ (float)((Offsets[0] + TerrainBlend_WrapCoordinate(Centres[0],
        Period)) / 2.5), 0.3f, 0.7f }, 4U);
    float Far = TerrainBlend_GetValueNoise((Vector3){ (float)((Offsets[0] + TerrainBlend_WrapCoordinate(Centres[0]
        + (Period * 160.0), Period)) / 2.5), 0.3f, 0.7f }, 4U);
    TEST_CHECK(run, IsNear(Near, Far, 1e-5f));
}


// Tests: a point of a terrain.
/* A point's blend from its triangle: painted weights interpolated by the barycentrics, the rules on them (not on a face
 * material's face), the materials' heights at the point, the dominant material. */
static void APointBlendsAsTheShaderDoes(TestRun* run)
{
    // Two quads of 2 m: the left one painted half grass half rock, ...
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 2U, 1U, 2.0f, 0.0f, 0.0f, NULL);
    for (uint32_t Vertex = 0U; Vertex < Grid.VertexCount; Vertex++)
    {
        Grid.Weights[(Vertex * TERRAIN_MAX_MATERIALS) + 0U] = 128U;
        Grid.Weights[(Vertex * TERRAIN_MAX_MATERIALS) + 2U] = 127U;
    }
    // ... the right one's first triangle a face of gravel.
    uint32_t Face = TestTerrain_Triangle(&Grid, 1U, 0U);
    Grid.Flags[Face] = TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL;
    Grid.Materials[Face] = 3U;
    TerrainMesh* Ground = TestTerrain_CreateMesh(run, &Grid, 4U, 0U, TERRAIN_DEFAULT_CHUNK_SIZE);
    // Grass lies low, rock stands high: the rock wins the point.
    uint8_t Low[2] = { 40U, 40U };
    uint8_t HighStone[2] = { 230U, 230U };
    TerrainMaterial Grass;
    TerrainMaterial Rock;
    MakeMaterial(&Grass, Low, 2, 2.0f, TerrainTriplanarMode_Off, 1.0f);
    MakeMaterial(&Rock, HighStone, 2, 2.0f, TerrainTriplanarMode_Off, 1.0f);
    TerrainAsset Terrain;
    Memory_Zero(&Terrain, sizeof(Terrain));
    Terrain._mesh = Ground;
    Terrain._materialCount = 4U;
    Terrain._materials[0] = &Grass;
    Terrain._materials[2] = &Rock;
    if (Ground != NULL)
    {
        float Barycentrics[3] = { 0.2f, 0.3f, 0.5f };
        Matrix4Double Identity = Matrix4Double_Identity();
        TerrainBlendPlace Here;
        TEST_CHECK(run, TerrainBlend_GetPlace(&Terrain, 0U, Barycentrics, &Identity, &Here));
        TerrainBlendPoint Point;
        TEST_CHECK(run, TerrainBlend_EvaluatePoint(&Terrain, 0U, Barycentrics, &Here, &Point));
        TEST_CHECK(run, !Point.IsFaceMaterial && IsNear(Point.PaintedWeights[0], 128.0f / 255.0f, 1e-5f)
            && IsNear(Point.PaintedWeights[2], 127.0f / 255.0f, 1e-5f) && (Point.PaintedWeights[1] == 0.0f));
        TEST_CHECK(run, (Point.RuleWeights[0] == Point.PaintedWeights[0]) && (Point.DominantMaterial == 2U)
            && (Point.Shares[2] == 1.0f) && IsNear(Sum(Point.Shares), 1.0f, 1e-6f));
        // A rule putting dirt (1) everywhere over grass only: the rock keeps its weight, the grass goes to dirt; dirt has
        // no material here (flat, mid-height) and the rock still stands higher.
        TEST_CHECK(run, TerrainBlend_SampleMaterialHeight(NULL, &Here) == (float)TERRAIN_MATERIAL_FLAT_HEIGHT / 255.0f);
        TerrainRule Dirt = Everywhere(1U, 1.0f);
        Dirt.CoveredMaterials = 1U << 0U;
        Terrain._rules[0] = Dirt;
        Terrain._ruleCount = 1U;
        TEST_CHECK(run, TerrainBlend_EvaluatePoint(&Terrain, 0U, Barycentrics, &Here, &Point));
        TEST_CHECK(run, (Point.RuleWeights[0] == 0.0f) && IsNear(Point.RuleWeights[1], 128.0f / 255.0f, 1e-5f)
            && IsNear(Point.RuleWeights[2], 127.0f / 255.0f, 1e-5f) && (Point.DominantMaterial == 2U));
        // The face material's face is gravel alone, whatever the rules say.
        TerrainBlendPlace There;
        TEST_CHECK(run, TerrainBlend_GetPlace(&Terrain, Face, Barycentrics, &Identity, &There));
        TEST_CHECK(run, TerrainBlend_EvaluatePoint(&Terrain, Face, Barycentrics, &There, &Point));
        TEST_CHECK(run, Point.IsFaceMaterial && (Point.PaintedWeights[3] == 1.0f) && (Point.RuleWeights[3] == 1.0f)
            && (Point.Shares[3] == 1.0f) && (Point.DominantMaterial == 3U) && (Point.RuleWeights[1] == 0.0f));
        // A triangle past the mesh answers nothing.
        TEST_CHECK(run, !TerrainBlend_EvaluatePoint(&Terrain, 1000U, Barycentrics, &Here, &Point)
            && (Point.DominantMaterial == 0U) && (Point.Shares[0] == 0.0f));
        TEST_CHECK(run, !TerrainBlend_GetPlace(&Terrain, 1000U, Barycentrics, &Identity, &There)
            && (There.WorldHeight == 0.0) && (There.TerrainNormal.y == 0.0f));
    }
    FreeMaterial(&Grass);
    FreeMaterial(&Rock);
    TerrainMesh_Destroy(Ground);
    TestTerrain_FreeGrid(&Grid);
}

/* A point's place: the triangle's vertices and normals interpolated in the terrain's space (exact, whatever the object's
 * placement), its height and normal in the world through the object's matrix (turned a quarter about Y, raised and
 * moved 100 km out here). */
static void APlaceIsInTheTerrainsSpaceAndTheWorlds(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 2U, 2U, 4.0f, 0.0f, 0.0f, NULL);
    // One corner raised: the triangle there slopes.
    Grid.Positions[TestTerrain_Vertex(&Grid, 1U, 0U)].y = 4.0f;
    TerrainMesh* Ground = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, TERRAIN_DEFAULT_CHUNK_SIZE);
    TerrainAsset Terrain;
    Memory_Zero(&Terrain, sizeof(Terrain));
    Terrain._mesh = Ground;
    Terrain._materialCount = 1U;
    if (Ground != NULL)
    {
        Matrix4Double Turn = Matrix4Double_FromRotationY(1.5707963267948966);
        Matrix4Double Move = Matrix4Double_FromTranslation((Vector3Double){ 100000.0, 25.0, -70000.0 });
        Matrix4Double World = Matrix4Double_Multiply(&Turn, &Move);
        const TerrainTriangle* Triangle = TerrainMesh_GetTriangle(Ground, 0U);
        float Barycentrics[3] = { 0.25f, 0.25f, 0.5f };
        TerrainBlendPlace Here;
        TEST_CHECK(run, TerrainBlend_GetPlace(&Terrain, 0U, Barycentrics, &World, &Here));
        Vector3Double Expected = { 0.0, 0.0, 0.0 };
        Vector3 Normal = { 0.0f, 0.0f, 0.0f };
        for (uint32_t Corner = 0U; Corner < 3U; Corner++)
        {
            Vector3 Position = TerrainMesh_GetPosition(Ground, Triangle->_indices[Corner]);
            Vector3 CornerNormal = TerrainMesh_GetNormal(Ground, Triangle->_indices[Corner]);
            Expected.X += (double)Position.x * (double)Barycentrics[Corner];
            Expected.Y += (double)Position.y * (double)Barycentrics[Corner];
            Expected.Z += (double)Position.z * (double)Barycentrics[Corner];
            Normal.x += CornerNormal.x * Barycentrics[Corner];
            Normal.y += CornerNormal.y * Barycentrics[Corner];
            Normal.z += CornerNormal.z * Barycentrics[Corner];
        }
        float Length = sqrtf((Normal.x * Normal.x) + (Normal.y * Normal.y) + (Normal.z * Normal.z));
        TEST_CHECK(run, (Here.TerrainPosition.X == Expected.X) && (Here.TerrainPosition.Y == Expected.Y)
            && (Here.TerrainPosition.Z == Expected.Z));
        TEST_CHECK(run, IsNear(Here.TerrainNormal.x, Normal.x / Length, 1e-6f)
            && IsNear(Here.TerrainNormal.y, Normal.y / Length, 1e-6f) && IsNear(Here.TerrainNormal.z, Normal.z / Length,
            1e-6f));
        // Turned about Y: the height is the terrain's plus 25 m; the normal keeps its Y and turns its X and Z.
        TEST_CHECK(run, fabs(Here.WorldHeight - (Expected.Y + 25.0)) < 1e-9);
        TEST_CHECK(run, IsNear(Here.WorldNormal.y, Here.TerrainNormal.y, 1e-6f)
            && IsNear(Here.WorldNormal.x, Here.TerrainNormal.z, 1e-6f)
            && IsNear(Here.WorldNormal.z, -Here.TerrainNormal.x, 1e-6f));
        // Squashed to half its height the ground is flatter: its normal leans up.
        Matrix4Double Squash = Matrix4Double_FromScale((Vector3Double){ 1.0, 0.5, 1.0 });
        TerrainBlendPlace Squashed;
        TEST_CHECK(run, TerrainBlend_GetPlace(&Terrain, 0U, Barycentrics, &Squash, &Squashed));
        TEST_CHECK(run, (Squashed.WorldNormal.y > Here.TerrainNormal.y) && (Squashed.WorldHeight == Expected.Y * 0.5));
    }
    TerrainMesh_Destroy(Ground);
    TestTerrain_FreeGrid(&Grid);
}

/* The shaders' constants are the C mirror's (the terrain shader's blend, the noise's period). */
static void TheShadersConstantsAreTheMirrors(TestRun* run)
{
    GenericBuffer Fragment;
    GenericBuffer_AllocateVariable(&Fragment, 0U, sizeof(unsigned char));
    if (ReadRepositoryText(run, "compile/out/asset/shaders/terrain/fragment.frag", &Fragment))
    {
        TEST_CHECK(run, (float)ShaderConstant(&Fragment, "HEIGHT_BLEND_BAND") == TERRAIN_BLEND_HEIGHT_BAND);
        TEST_CHECK(run, (float)ShaderConstant(&Fragment, "WEIGHT_EPSILON") == TERRAIN_BLEND_WEIGHT_EPSILON);
        TEST_CHECK(run, (float)ShaderConstant(&Fragment, "TRIPLANAR_RAMP_DEGREES") == TERRAIN_BLEND_TRIPLANAR_RAMP_DEGREES);
        TEST_CHECK(run, (float)ShaderConstant(&Fragment, "FLAT_HEIGHT") == ((float)TERRAIN_MATERIAL_FLAT_HEIGHT / 255.0f));
        // The rule arrays hold TERRAIN_MAX_RULES.
        char Rules[64];
        snprintf(Rules, sizeof(Rules), "uniform ivec4 ruleIds[%u];", TERRAIN_MAX_RULES);
        TEST_CHECK(run, strstr((const char*)Fragment._data, Rules) != NULL);
    }
    GenericBuffer Noise;
    GenericBuffer_AllocateVariable(&Noise, 0U, sizeof(unsigned char));
    if (ReadRepositoryText(run, "compile/out/asset/shaders/common/value_noise.glsl", &Noise))
    {
        TEST_CHECK(run, ShaderConstant(&Noise, "VALUE_NOISE_PERIOD") == (double)TERRAIN_BLEND_NOISE_PERIOD);
    }
    Memory_Free(Noise._data);
    Memory_Free(Fragment._data);
}


// Public functions.
void TerrainBlendTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "weights_unpack_from_their_bytes", WeightsUnpackFromTheirBytes },
        { "weights_normalize_or_give_the_first_material", WeightsNormalizeOrGiveTheFirstMaterial },
        { "a_range_ramps_across_its_blend", ARangeRampsAcrossItsBlend },
        { "the_slope_is_the_angle_from_up", TheSlopeIsTheAngleFromUp },
        { "the_value_noise_is_the_shaders_noise", TheValueNoiseIsTheShadersNoise },
        { "wrapping_stays_in_one_period", WrappingStaysInOnePeriod },
        { "a_rule_strength_multiplies_its_conditions", ARuleStrengthMultipliesItsConditions },
        { "rules_move_weight_in_order", RulesMoveWeightInOrder },
        { "height_blending_shares_by_score", HeightBlendingSharesByScore },
        { "the_dominant_material_is_the_largest_share", TheDominantMaterialIsTheLargestShare },
        { "triplanar_weights_follow_the_normal", TriplanarWeightsFollowTheNormal },
        { "projection_coordinates_follow_the_sides", ProjectionCoordinatesFollowTheSides },
        { "texture_coordinates_stand_still_far_out", TextureCoordinatesStandStillFarOut },
        { "a_place_is_in_the_terrains_space_and_the_worlds", APlaceIsInTheTerrainsSpaceAndTheWorlds },
        { "a_point_blends_as_the_shader_does", APointBlendsAsTheShaderDoes },
        { "the_shaders_constants_are_the_mirrors", TheShadersConstantsAreTheMirrors },
    };
    TestRun_RunSuite(run, "TerrainBlend", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
