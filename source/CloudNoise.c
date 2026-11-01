// The clouds' noise as pure maths (CloudNoise.h): tileable gradient and Worley noise, and the three textures the cloud
// pass is made from. The fills read each lattice's gradients and feature points from tables built once per period (the
// same hashes CloudNoise_Gradient3 and the rest compute on the fly), so a texel costs no hashing.

#include <math.h>
#include "CloudNoise.h"
#include "wr/WRMemory.h"


// Macros.
/** The seeds' offsets per texture and per pattern, so no two patterns share their gradients or points. */
#define SEED_SHAPE_GRADIENT 0x1000U
#define SEED_SHAPE_WORLEY 0x2000U
#define SEED_DETAIL_WORLEY 0x3000U
#define SEED_WEATHER_COVERAGE 0x4000U
#define SEED_WEATHER_VARIATION 0x5000U
#define SEED_WEATHER_STREAKS 0x6000U
#define SEED_WEATHER_WARP 0x7000U
#define SEED_WEATHER_CELLS 0x8000U
/** 2D gradient noise reaches only 1 / sqrt(2) with unit gradients: this scales it to -1 .. 1. */
#define GRADIENT2_SCALE 1.4142135623730951
/** The most texels a fill's Worley octave puts along a cell's side (the largest texture over its coarsest period). */
#define MAX_TEXELS_PER_CELL 64
/** The histogram's bins when a fill spreads its values evenly over the bytes (EqualizeToBytes). */
#define EQUALIZE_BIN_COUNT 65536U


// Types.
/** One lattice's gradients (an index into the gradient directions per lattice point) for a period per axis, x fastest. */
typedef struct GradientTableStruct
{
    int32_t PeriodX;
    int32_t PeriodY;
    int32_t PeriodZ;
    uint8_t* Indices;
} GradientTable;

/** One lattice's feature points (each within its cell, 0 .. 1 per axis; z unused in 2D), x fastest. */
typedef struct PointTableStruct
{
    int32_t PeriodX;
    int32_t PeriodY;
    int32_t PeriodZ;
    float* Points;
} PointTable;


// Fields.
/* Perlin's improved noise's twelve gradient directions (the cube's edge midpoints), four of them again to make sixteen:
 * a hash picks one by its low four bits. */
static const int8_t Gradients3[16][3] =
{
    { 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 }, { 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 },
    { 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 }, { 1, 1, 0 }, { -1, 1, 0 }, { 0, -1, 1 }, { 0, -1, -1 }
};

/* The 2D gradients: sixteen unit vectors at even angles (cos, sin of k x 22.5 degrees). */
static const double Gradients2[16][2] =
{
    { 1.0, 0.0 }, { 0.9238795325112867, 0.3826834323650898 }, { 0.7071067811865476, 0.7071067811865476 },
    { 0.3826834323650898, 0.9238795325112867 }, { 0.0, 1.0 }, { -0.3826834323650898, 0.9238795325112867 },
    { -0.7071067811865476, 0.7071067811865476 }, { -0.9238795325112867, 0.3826834323650898 }, { -1.0, 0.0 },
    { -0.9238795325112867, -0.3826834323650898 }, { -0.7071067811865476, -0.7071067811865476 },
    { -0.3826834323650898, -0.9238795325112867 }, { 0.0, -1.0 }, { 0.3826834323650898, -0.9238795325112867 },
    { 0.7071067811865476, -0.7071067811865476 }, { 0.9238795325112867, -0.3826834323650898 }
};

/* The 27 cells around a point's own, nearest first (its own, the six faces, the twelve edges, the eight corners): the
 * Worley search visits them in this order and skips a cell that cannot hold a point nearer than the best so far. */
static const int8_t Neighbours3[27][3] =
{
    { 0, 0, 0 },
    { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 },
    { -1, -1, 0 }, { 1, -1, 0 }, { -1, 1, 0 }, { 1, 1, 0 }, { -1, 0, -1 }, { 1, 0, -1 }, { -1, 0, 1 }, { 1, 0, 1 },
    { 0, -1, -1 }, { 0, 1, -1 }, { 0, -1, 1 }, { 0, 1, 1 },
    { -1, -1, -1 }, { 1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 }, { -1, -1, 1 }, { 1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 }
};

/* The nine cells around a point's own in the plane (z 0), nearest first. */
static const int8_t Neighbours2[9][3] =
{
    { 0, 0, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { -1, -1, 0 }, { 1, -1, 0 }, { -1, 1, 0 }, { 1, 1, 0 }
};


// Static functions.
/* A 32-bit integer hash with good avalanche (lowbias32). */
static uint32_t HashInteger(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    value *= 0x846CA68BU;
    value ^= value >> 16;
    return value;
}

/* A lattice point's hash for a seed, its coordinates already wrapped into the period. */
static uint32_t HashLattice(int32_t x, int32_t y, int32_t z, uint32_t seed)
{
    return HashInteger(seed ^ HashInteger((uint32_t)x ^ HashInteger((uint32_t)y ^ HashInteger((uint32_t)z
        + 0x9E3779B9U))));
}

/* An integer wrapped into [0, period). */
static int32_t Wrap(int64_t value, int32_t period)
{
    int64_t Remainder = value % (int64_t)period;
    return (int32_t)((Remainder < 0) ? (Remainder + period) : Remainder);
}

/* A hash's share in [0, 1): its top 24 bits. */
static double HashUnit(uint32_t hash)
{
    return (double)(hash >> 8) / 16777216.0;
}

/* floor() as an integer, without a library call (the baseline x86-64 target has no instruction for it): the fills call
 * it millions of times. Exact for every value the noises take (well inside an int64's range). */
static int64_t FloorToInteger(double value)
{
    int64_t Truncated = (int64_t)value;
    return ((double)Truncated > value) ? (Truncated - 1) : Truncated;
}

/* The quintic fade 6t^5 - 15t^4 + 10t^3: zero first and second derivatives at the lattice points. */
static double Fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

static double Lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

static double Clamp(double value, double minimum, double maximum)
{
    return (value < minimum) ? minimum : ((value > maximum) ? maximum : value);
}

/* A value in [0, 1] as a byte, rounded. */
static uint8_t ToByte(double value)
{
    return (uint8_t)lround(Clamp(value, 0.0, 1.0) * 255.0);
}

/* Where `value` lies between `from` and `to`, mapped onto 0 .. 1 and clamped. */
static double Remap01(double value, double from, double to)
{
    return Clamp((value - from) / (to - from), 0.0, 1.0);
}

/* A cell's feature point within it (each axis 0 .. 1), the cell already wrapped into the period. */
static void GetFeaturePoint(int32_t x, int32_t y, int32_t z, uint32_t seed, double outPoint[3])
{
    uint32_t Hash = HashLattice(x, y, z, seed);
    outPoint[0] = HashUnit(Hash);
    Hash = HashInteger(Hash ^ 0xA511E9B3U);
    outPoint[1] = HashUnit(Hash);
    Hash = HashInteger(Hash ^ 0x63D83595U);
    outPoint[2] = HashUnit(Hash);
}

/* The 3D gradient noise's value from its eight corners' gradient indices (Gradients3) and the point's place in the cell. */
static double Gradient3FromCorners(const uint32_t indices[8], double fractionX, double fractionY, double fractionZ)
{
    double Corners[8];
    for (int32_t Corner = 0; Corner < 8; Corner++)
    {
        int32_t StepX = Corner & 1;
        int32_t StepY = (Corner >> 1) & 1;
        int32_t StepZ = (Corner >> 2) & 1;
        const int8_t* Gradient = Gradients3[indices[Corner] & 15U];
        Corners[Corner] = Gradient[0] * (fractionX - StepX) + Gradient[1] * (fractionY - StepY)
            + Gradient[2] * (fractionZ - StepZ);
    }
    double U = Fade(fractionX);
    double V = Fade(fractionY);
    double W = Fade(fractionZ);
    double Near = Lerp(Lerp(Corners[0], Corners[1], U), Lerp(Corners[2], Corners[3], U), V);
    double Far = Lerp(Lerp(Corners[4], Corners[5], U), Lerp(Corners[6], Corners[7], U), V);
    return Clamp(Lerp(Near, Far, W), -1.0, 1.0);
}

/* The 2D gradient noise's value from its four corners' gradient indices (Gradients2). */
static double Gradient2FromCorners(const uint32_t indices[4], double fractionX, double fractionY)
{
    double Corners[4];
    for (int32_t Corner = 0; Corner < 4; Corner++)
    {
        int32_t StepX = Corner & 1;
        int32_t StepY = (Corner >> 1) & 1;
        const double* Gradient = Gradients2[indices[Corner] & 15U];
        Corners[Corner] = Gradient[0] * (fractionX - StepX) + Gradient[1] * (fractionY - StepY);
    }
    double U = Fade(fractionX);
    double V = Fade(fractionY);
    return Clamp(Lerp(Lerp(Corners[0], Corners[1], U), Lerp(Corners[2], Corners[3], U), V) * GRADIENT2_SCALE, -1.0, 1.0);
}

/* A lattice's gradient indices for its periods (1 along z for 2D), from the same hashes as CloudNoise_Gradient3/2. */
static void BuildGradientTable(int32_t periodX, int32_t periodY, int32_t periodZ, uint32_t seed,
    GradientTable* outTable)
{
    outTable->PeriodX = periodX;
    outTable->PeriodY = periodY;
    outTable->PeriodZ = periodZ;
    outTable->Indices = Memory_Allocate((size_t)periodX * (size_t)periodY * (size_t)periodZ);
    for (int32_t Z = 0; Z < periodZ; Z++)
    {
        for (int32_t Y = 0; Y < periodY; Y++)
        {
            for (int32_t X = 0; X < periodX; X++)
            {
                size_t Index = ((size_t)Z * (size_t)periodY + (size_t)Y) * (size_t)periodX + (size_t)X;
                outTable->Indices[Index] = (uint8_t)(HashLattice(X, Y, Z, seed) & 15U);
            }
        }
    }
}

/* A lattice's feature points for its periods (1 along z for 2D), from the same hashes as CloudNoise_Worley3/2. */
static void BuildPointTable(int32_t periodX, int32_t periodY, int32_t periodZ, uint32_t seed, PointTable* outTable)
{
    outTable->PeriodX = periodX;
    outTable->PeriodY = periodY;
    outTable->PeriodZ = periodZ;
    size_t CellCount = (size_t)periodX * (size_t)periodY * (size_t)periodZ;
    outTable->Points = Memory_Allocate(CellCount * 3U * sizeof(float));
    for (int32_t Z = 0; Z < periodZ; Z++)
    {
        for (int32_t Y = 0; Y < periodY; Y++)
        {
            for (int32_t X = 0; X < periodX; X++)
            {
                double Point[3];
                GetFeaturePoint(X, Y, Z, seed, Point);
                size_t Index = (((size_t)Z * (size_t)periodY + (size_t)Y) * (size_t)periodX + (size_t)X) * 3U;
                outTable->Points[Index + 0U] = (float)Point[0];
                outTable->Points[Index + 1U] = (float)Point[1];
                outTable->Points[Index + 2U] = (float)Point[2];
            }
        }
    }
}

/* 3D gradient noise at a point (in lattice cells) from a table: CloudNoise_Gradient3 without hashing. */
static double Gradient3FromTable(const GradientTable* table, double x, double y, double z)
{
    int64_t CellX = FloorToInteger(x);
    int64_t CellY = FloorToInteger(y);
    int64_t CellZ = FloorToInteger(z);
    uint32_t Indices[8];
    for (int32_t Corner = 0; Corner < 8; Corner++)
    {
        int32_t X = (int32_t)((CellX + (Corner & 1)) & (table->PeriodX - 1));
        int32_t Y = (int32_t)((CellY + ((Corner >> 1) & 1)) & (table->PeriodY - 1));
        int32_t Z = (int32_t)((CellZ + ((Corner >> 2) & 1)) & (table->PeriodZ - 1));
        Indices[Corner] = table->Indices[((size_t)Z * (size_t)table->PeriodY + (size_t)Y) * (size_t)table->PeriodX
            + (size_t)X];
    }
    return Gradient3FromCorners(Indices, x - (double)CellX, y - (double)CellY, z - (double)CellZ);
}

/* 2D gradient noise at a point (in lattice cells) from a table: CloudNoise_Gradient2 without hashing, with its own
 * period per axis (a stretched pattern). */
static double Gradient2FromTable(const GradientTable* table, double x, double y)
{
    int64_t CellX = FloorToInteger(x);
    int64_t CellY = FloorToInteger(y);
    uint32_t Indices[4];
    for (int32_t Corner = 0; Corner < 4; Corner++)
    {
        int32_t X = (int32_t)((CellX + (Corner & 1)) & (table->PeriodX - 1));
        int32_t Y = (int32_t)((CellY + ((Corner >> 1) & 1)) & (table->PeriodY - 1));
        Indices[Corner] = table->Indices[(size_t)Y * (size_t)table->PeriodX + (size_t)X];
    }
    return Gradient2FromCorners(Indices, x - (double)CellX, y - (double)CellY);
}

/* Inverted Worley noise (1 - the distance to the nearest feature point) for every texel of a grid of `size` texels per
 * side (cubed, or squared when flat) tiled once by `table`'s cells: the texel (x, y, z) sits at ((x + 0.5) / size) x
 * period. A cell at a time: its 27 (or 9) neighbours' points are gathered once, relative to the cell, and every texel in
 * it takes the nearest of them in one branch-free loop. The period divides the size. */
static void FillInvertedWorley(const PointTable* table, int32_t size, bool isFlat, float* outValues)
{
    int32_t Period = table->PeriodX;
    int32_t Texels = size / Period;
    int32_t CellsZ = isFlat ? 1 : Period;
    int32_t TexelsZ = isFlat ? 1 : Texels;
    int32_t Count = isFlat ? 9 : 27;
    float PointX[27];
    float PointY[27];
    float PointZ[27];
    float Fractions[MAX_TEXELS_PER_CELL];
    for (int32_t Index = 0; Index < Texels; Index++)
    {
        Fractions[Index] = ((float)Index + 0.5f) / (float)Texels;
    }
    for (int32_t CellZ = 0; CellZ < CellsZ; CellZ++)
    {
        for (int32_t CellY = 0; CellY < Period; CellY++)
        {
            for (int32_t CellX = 0; CellX < Period; CellX++)
            {
                for (int32_t Neighbour = 0; Neighbour < Count; Neighbour++)
                {
                    const int8_t* Offset = isFlat ? Neighbours2[Neighbour] : Neighbours3[Neighbour];
                    int32_t X = (CellX + Offset[0]) & (Period - 1);
                    int32_t Y = (CellY + Offset[1]) & (Period - 1);
                    int32_t Z = isFlat ? 0 : ((CellZ + Offset[2]) & (Period - 1));
                    const float* Point = &table->Points[(((size_t)Z * (size_t)table->PeriodY + (size_t)Y)
                        * (size_t)table->PeriodX + (size_t)X) * 3U];
                    PointX[Neighbour] = (float)Offset[0] + Point[0];
                    PointY[Neighbour] = (float)Offset[1] + Point[1];
                    PointZ[Neighbour] = isFlat ? 0.0f : ((float)Offset[2] + Point[2]);
                }
                for (int32_t InZ = 0; InZ < TexelsZ; InZ++)
                {
                    float FractionZ = isFlat ? 0.0f : Fractions[InZ];
                    for (int32_t InY = 0; InY < Texels; InY++)
                    {
                        float FractionY = Fractions[InY];
                        size_t Row = ((size_t)(CellZ * TexelsZ + InZ) * (size_t)size + (size_t)(CellY * Texels + InY))
                            * (size_t)size + (size_t)(CellX * Texels);
                        for (int32_t InX = 0; InX < Texels; InX++)
                        {
                            float FractionX = Fractions[InX];
                            float Best = 4.0f;
                            for (int32_t Neighbour = 0; Neighbour < Count; Neighbour++)
                            {
                                float DeltaX = PointX[Neighbour] - FractionX;
                                float DeltaY = PointY[Neighbour] - FractionY;
                                float DeltaZ = PointZ[Neighbour] - FractionZ;
                                float Distance = DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ;
                                Best = (Distance < Best) ? Distance : Best;
                            }
                            float Nearest = sqrtf(Best);
                            outValues[Row + (size_t)InX] = 1.0f - ((Nearest > 1.0f) ? 1.0f : Nearest);
                        }
                    }
                }
            }
        }
    }
}

/* A sum of gradient-noise octaves: the first lattice of each table set (`tables`, `count` octaves, each twice the
 * frequency of the one before), weighted by `gain` per octave, at the tile point (u, v, w): -1 .. 1. */
static double GradientSum3(const GradientTable* tables, int32_t count, double gain, double u, double v, double w)
{
    double Sum = 0.0;
    double Weight = 1.0;
    double Total = 0.0;
    for (int32_t Octave = 0; Octave < count; Octave++)
    {
        const GradientTable* Table = &tables[Octave];
        Sum += Gradient3FromTable(Table, u * Table->PeriodX, v * Table->PeriodY, w * Table->PeriodZ) * Weight;
        Total += Weight;
        Weight *= gain;
    }
    return Sum / Total;
}

static double GradientSum2(const GradientTable* tables, int32_t count, double gain, double u, double v)
{
    double Sum = 0.0;
    double Weight = 1.0;
    double Total = 0.0;
    for (int32_t Octave = 0; Octave < count; Octave++)
    {
        const GradientTable* Table = &tables[Octave];
        Sum += Gradient2FromTable(Table, u * Table->PeriodX, v * Table->PeriodY) * Weight;
        Total += Weight;
        Weight *= gain;
    }
    return Sum / Total;
}

/* Builds `count` octaves of gradient tables, the first with the periods given, each next twice them, seeds in a row. */
static void BuildOctaves(GradientTable* outTables, int32_t count, int32_t periodX, int32_t periodY, int32_t periodZ,
    uint32_t seed)
{
    for (int32_t Octave = 0; Octave < count; Octave++)
    {
        int32_t Factor = 1 << Octave;
        BuildGradientTable(periodX * Factor, periodY * Factor, (periodZ > 1) ? (periodZ * Factor) : 1,
            seed + (uint32_t)Octave, &outTables[Octave]);
    }
}

static void ReleaseGradientTables(GradientTable* tables, int32_t count)
{
    for (int32_t Octave = 0; Octave < count; Octave++)
    {
        Memory_Free(tables[Octave].Indices);
        tables[Octave].Indices = NULL;
    }
}

static void ReleasePointTables(PointTable* tables, int32_t count)
{
    for (int32_t Octave = 0; Octave < count; Octave++)
    {
        Memory_Free(tables[Octave].Points);
        tables[Octave].Points = NULL;
    }
}


/* Writes `count` values as bytes spread evenly over 0 .. 255 by their rank (histogram equalization), every
 * `outStride`-th byte of `outBytes`: a value's byte is the share of the values below it (half its own bin's in), so
 * each byte holds about the same number of texels and a threshold t keeps about 1 - t of them, whatever the noise's own
 * distribution. A monotonic remap: the patterns' shapes stay as they are. */
static void EqualizeToBytes(const float* values, size_t count, uint8_t* outBytes, size_t outStride)
{
    float Lowest = values[0];
    float Highest = values[0];
    for (size_t Index = 1U; Index < count; Index++)
    {
        Lowest = (values[Index] < Lowest) ? values[Index] : Lowest;
        Highest = (values[Index] > Highest) ? values[Index] : Highest;
    }
    uint32_t* Bins = Memory_Allocate(EQUALIZE_BIN_COUNT * sizeof(uint32_t));
    Memory_Zero(Bins, EQUALIZE_BIN_COUNT * sizeof(uint32_t));
    double Range = (double)Highest - (double)Lowest;
    double Scale = (Range > 0.0) ? ((double)(EQUALIZE_BIN_COUNT - 1U) / Range) : 0.0;
    for (size_t Index = 0U; Index < count; Index++)
    {
        Bins[(size_t)(((double)values[Index] - (double)Lowest) * Scale)]++;
    }
    // Each bin's byte from the count below it plus half its own.
    uint8_t* Bytes = Memory_Allocate(EQUALIZE_BIN_COUNT);
    double Below = 0.0;
    for (size_t Bin = 0U; Bin < EQUALIZE_BIN_COUNT; Bin++)
    {
        Bytes[Bin] = ToByte((Below + 0.5 * (double)Bins[Bin]) / (double)count);
        Below += (double)Bins[Bin];
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        outBytes[Index * outStride] = Bytes[(size_t)(((double)values[Index] - (double)Lowest) * Scale)];
    }
    Memory_Free(Bytes);
    Memory_Free(Bins);
}


// Internal functions (declared in CloudNoise.h).
double CloudNoise_Gradient3(double x, double y, double z, int32_t period, uint32_t seed)
{
    int32_t Period = (period < 1) ? 1 : period;
    double FloorX = floor(x);
    double FloorY = floor(y);
    double FloorZ = floor(z);
    int64_t CellX = (int64_t)FloorX;
    int64_t CellY = (int64_t)FloorY;
    int64_t CellZ = (int64_t)FloorZ;
    uint32_t Indices[8];
    for (int32_t Corner = 0; Corner < 8; Corner++)
    {
        Indices[Corner] = HashLattice(Wrap(CellX + (Corner & 1), Period), Wrap(CellY + ((Corner >> 1) & 1), Period),
            Wrap(CellZ + ((Corner >> 2) & 1), Period), seed);
    }
    return Gradient3FromCorners(Indices, x - FloorX, y - FloorY, z - FloorZ);
}

double CloudNoise_Gradient2(double x, double y, int32_t period, uint32_t seed)
{
    int32_t Period = (period < 1) ? 1 : period;
    double FloorX = floor(x);
    double FloorY = floor(y);
    int64_t CellX = (int64_t)FloorX;
    int64_t CellY = (int64_t)FloorY;
    uint32_t Indices[4];
    for (int32_t Corner = 0; Corner < 4; Corner++)
    {
        Indices[Corner] = HashLattice(Wrap(CellX + (Corner & 1), Period), Wrap(CellY + ((Corner >> 1) & 1), Period), 0,
            seed);
    }
    return Gradient2FromCorners(Indices, x - FloorX, y - FloorY);
}

double CloudNoise_Worley3(double x, double y, double z, int32_t period, uint32_t seed)
{
    int32_t Period = (period < 1) ? 1 : period;
    double FloorX = floor(x);
    double FloorY = floor(y);
    double FloorZ = floor(z);
    int64_t CellX = (int64_t)FloorX;
    int64_t CellY = (int64_t)FloorY;
    int64_t CellZ = (int64_t)FloorZ;
    double Best = 4.0;
    for (int32_t Neighbour = 0; Neighbour < 27; Neighbour++)
    {
        const int8_t* Offset = Neighbours3[Neighbour];
        double Point[3];
        GetFeaturePoint(Wrap(CellX + Offset[0], Period), Wrap(CellY + Offset[1], Period), Wrap(CellZ + Offset[2], Period),
            seed, Point);
        double DeltaX = (FloorX + Offset[0] + Point[0]) - x;
        double DeltaY = (FloorY + Offset[1] + Point[1]) - y;
        double DeltaZ = (FloorZ + Offset[2] + Point[2]) - z;
        double Distance = DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ;
        Best = (Distance < Best) ? Distance : Best;
    }
    double Distance = sqrt(Best);
    return (Distance > 1.0) ? 1.0 : Distance;
}

double CloudNoise_Worley2(double x, double y, int32_t period, uint32_t seed)
{
    int32_t Period = (period < 1) ? 1 : period;
    double FloorX = floor(x);
    double FloorY = floor(y);
    int64_t CellX = (int64_t)FloorX;
    int64_t CellY = (int64_t)FloorY;
    double Best = 4.0;
    for (int32_t Neighbour = 0; Neighbour < 27; Neighbour++)
    {
        const int8_t* Offset = Neighbours3[Neighbour];
        if (Offset[2] != 0)
        {
            continue;
        }
        double Point[3];
        GetFeaturePoint(Wrap(CellX + Offset[0], Period), Wrap(CellY + Offset[1], Period), 0, seed, Point);
        double DeltaX = (FloorX + Offset[0] + Point[0]) - x;
        double DeltaY = (FloorY + Offset[1] + Point[1]) - y;
        double Distance = DeltaX * DeltaX + DeltaY * DeltaY;
        Best = (Distance < Best) ? Distance : Best;
    }
    double Distance = sqrt(Best);
    return (Distance > 1.0) ? 1.0 : Distance;
}

void CloudNoise_FillShape(uint8_t* outTexels)
{
    const int32_t Size = CLOUD_NOISE_SHAPE_SIZE;
    // The Worley octaves: the coarse sum (the first three) carves the gradient noise into masses, the finer one (the
    // last three) adds billows to them. The finest octave has four texels a cell.
    enum { WorleyOctaves = 4, GradientOctaves = 3 };
    static const int32_t WorleyPeriods[WorleyOctaves] = { 2, 4, 8, 16 };
    size_t Count = (size_t)Size * (size_t)Size * (size_t)Size;
    float* Worley = Memory_Allocate(Count * WorleyOctaves * sizeof(float));
    for (int32_t Octave = 0; Octave < WorleyOctaves; Octave++)
    {
        PointTable Points;
        BuildPointTable(WorleyPeriods[Octave], WorleyPeriods[Octave], WorleyPeriods[Octave],
            CLOUD_NOISE_SEED + SEED_SHAPE_WORLEY + (uint32_t)Octave, &Points);
        FillInvertedWorley(&Points, Size, false, &Worley[(size_t)Octave * Count]);
        ReleasePointTables(&Points, 1);
    }
    GradientTable Gradients[GradientOctaves];
    BuildOctaves(Gradients, GradientOctaves, 2, 2, 2, CLOUD_NOISE_SEED + SEED_SHAPE_GRADIENT);
    float* Values = Memory_Allocate(Count * sizeof(float));
    for (int32_t Z = 0; Z < Size; Z++)
    {
        double W = (Z + 0.5) / Size;
        for (int32_t Y = 0; Y < Size; Y++)
        {
            double V = (Y + 0.5) / Size;
            for (int32_t X = 0; X < Size; X++)
            {
                double U = (X + 0.5) / Size;
                size_t Texel = ((size_t)Z * (size_t)Size + (size_t)Y) * (size_t)Size + (size_t)X;
                double Octaves[WorleyOctaves];
                for (int32_t Octave = 0; Octave < WorleyOctaves; Octave++)
                {
                    Octaves[Octave] = (double)Worley[(size_t)Octave * Count + Texel];
                }
                double Gradient = 0.5 + 0.5 * GradientSum3(Gradients, GradientOctaves, 0.5, U, V, W);
                double Coarse = 0.625 * Octaves[0] + 0.25 * Octaves[1] + 0.125 * Octaves[2];
                double Fine = 0.625 * Octaves[1] + 0.25 * Octaves[2] + 0.125 * Octaves[3];
                // Perlin-Worley: the gradient noise, its low values carved away where the coarse Worley sum is low;
                // then the finer sum's billows folded in the same way.
                double PerlinWorley = Remap01(Gradient, Coarse - 1.0, 1.0);
                Values[Texel] = (float)Remap01(PerlinWorley, Fine - 1.0, 1.0);
            }
        }
    }
    EqualizeToBytes(Values, Count, outTexels, 1U);
    Memory_Free(Values);
    Memory_Free(Worley);
    ReleaseGradientTables(Gradients, GradientOctaves);
}

void CloudNoise_FillDetail(uint8_t* outTexels)
{
    const int32_t Size = CLOUD_NOISE_DETAIL_SIZE;
    enum { Octaves = 3 };
    static const int32_t Periods[Octaves] = { 2, 4, 8 };
    static const float Weights[Octaves] = { 0.625f, 0.25f, 0.125f };
    size_t Count = (size_t)Size * (size_t)Size * (size_t)Size;
    float* Octave = Memory_Allocate(Count * sizeof(float));
    float* Values = Memory_Allocate(Count * sizeof(float));
    Memory_Zero(Values, Count * sizeof(float));
    for (int32_t Index = 0; Index < Octaves; Index++)
    {
        PointTable Points;
        BuildPointTable(Periods[Index], Periods[Index], Periods[Index],
            CLOUD_NOISE_SEED + SEED_DETAIL_WORLEY + (uint32_t)Index, &Points);
        FillInvertedWorley(&Points, Size, false, Octave);
        ReleasePointTables(&Points, 1);
        for (size_t Texel = 0U; Texel < Count; Texel++)
        {
            Values[Texel] += Weights[Index] * Octave[Texel];
        }
    }
    EqualizeToBytes(Values, Count, outTexels, 1U);
    Memory_Free(Values);
    Memory_Free(Octave);
}

void CloudNoise_FillWeather(uint8_t* outTexels)
{
    const int32_t Size = CLOUD_NOISE_WEATHER_SIZE;
    enum { CoverageOctaves = 5, VariationOctaves = 4, WarpOctaves = 3, StreakOctaves = 4, CellOctaves = 3, Channels = 4 };
    GradientTable Coverage[CoverageOctaves];
    GradientTable Variation[VariationOctaves];
    GradientTable WarpU[WarpOctaves];
    GradientTable WarpV[WarpOctaves];
    GradientTable Streaks[StreakOctaves];
    BuildOctaves(Coverage, CoverageOctaves, 4, 4, 1, CLOUD_NOISE_SEED + SEED_WEATHER_COVERAGE);
    BuildOctaves(Variation, VariationOctaves, 16, 16, 1, CLOUD_NOISE_SEED + SEED_WEATHER_VARIATION);
    BuildOctaves(WarpU, WarpOctaves, 4, 4, 1, CLOUD_NOISE_SEED + SEED_WEATHER_WARP);
    BuildOctaves(WarpV, WarpOctaves, 4, 4, 1, CLOUD_NOISE_SEED + SEED_WEATHER_WARP + 16U);
    // The streaks' lattice is eight times as fine across them (v) as along them (u): long, thin fibres.
    BuildOctaves(Streaks, StreakOctaves, 4, 32, 1, CLOUD_NOISE_SEED + SEED_WEATHER_STREAKS);
    static const float CellWeights[CellOctaves] = { 0.625f, 0.25f, 0.125f };
    size_t Count = (size_t)Size * (size_t)Size;
    float* Values = Memory_Allocate(Count * Channels * sizeof(float));
    // A: small round cells, a sum of inverted Worley octaves.
    float* Cells = &Values[3U * Count];
    float* Octave = Memory_Allocate(Count * sizeof(float));
    Memory_Zero(Cells, Count * sizeof(float));
    for (int32_t Index = 0; Index < CellOctaves; Index++)
    {
        int32_t Period = 32 << Index;
        PointTable Points;
        BuildPointTable(Period, Period, 1, CLOUD_NOISE_SEED + SEED_WEATHER_CELLS + (uint32_t)Index, &Points);
        FillInvertedWorley(&Points, Size, true, Octave);
        ReleasePointTables(&Points, 1);
        for (size_t Texel = 0U; Texel < Count; Texel++)
        {
            Cells[Texel] += CellWeights[Index] * Octave[Texel];
        }
    }
    Memory_Free(Octave);
    for (int32_t Y = 0; Y < Size; Y++)
    {
        double V = (Y + 0.5) / Size;
        for (int32_t X = 0; X < Size; X++)
        {
            double U = (X + 0.5) / Size;
            // A periodic warp keeps the tile periodic.
            double OffsetU = 0.06 * GradientSum2(WarpU, WarpOctaves, 0.5, U, V);
            double OffsetV = 0.06 * GradientSum2(WarpV, WarpOctaves, 0.5, U, V);
            size_t Texel = ((size_t)Y * (size_t)Size + (size_t)X);
            Values[Texel] = (float)GradientSum2(Coverage, CoverageOctaves, 0.5, U, V);
            Values[Count + Texel] = (float)GradientSum2(Variation, VariationOctaves, 0.5, U, V);
            Values[(2U * Count) + Texel] = (float)GradientSum2(Streaks, StreakOctaves, 0.6, U + OffsetU, V + OffsetV);
        }
    }
    for (size_t Channel = 0U; Channel < Channels; Channel++)
    {
        EqualizeToBytes(&Values[Channel * Count], Count, &outTexels[Channel], Channels);
    }
    Memory_Free(Values);
    ReleaseGradientTables(Coverage, CoverageOctaves);
    ReleaseGradientTables(Variation, VariationOctaves);
    ReleaseGradientTables(WarpU, WarpOctaves);
    ReleaseGradientTables(WarpV, WarpOctaves);
    ReleaseGradientTables(Streaks, StreakOctaves);
}
