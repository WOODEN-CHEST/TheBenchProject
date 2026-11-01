#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Private header (source/): the clouds' noise as pure maths, tested headless (tests/CloudNoiseTests.c). The cloud pass
 * (WorldRendererClouds.c) fills three textures with it when it loads: no art is needed, and the same seed gives the
 * same bytes on every machine (integer hashing, double arithmetic, one rounding per texel).
 *
 * THE NOISES. Both tile: a value at x and at x + period are the same, along every axis, so the textures repeat without a
 * seam on the GPU.
 *  - GRADIENT NOISE (Perlin's improved noise): smooth hills and hollows on an integer lattice, -1 .. 1, 0 at every lattice
 *    point (CloudNoise_Gradient3, _Gradient2).
 *  - WORLEY NOISE (cellular): the distance to the nearest of one feature point per lattice cell, scattered in its cell
 *    by a hash, in cells (CloudNoise_Worley3, _Worley2). Inverted (1 - the distance) it gives round, billowing blobs: a
 *    cloud's cauliflower.
 * Fractal sums of either (octaves of twice the frequency, half the weight) add finer shapes to coarse ones.
 *
 * THE TEXTURES (one byte a channel, 0 .. 255 for 0 .. 1):
 *  - SHAPE (CLOUD_NOISE_SHAPE_SIZE cubed, one channel): the clouds' base shape, Perlin-Worley noise (Schneider and Vos,
 *    "The Real-time Volumetric Cloudscapes of Horizon Zero Dawn", 2015): gradient noise carved by inverted Worley noise
 *    (connected, billowing masses), with a finer Worley sum folded in. The renderer thresholds it by the coverage.
 *  - DETAIL (CLOUD_NOISE_DETAIL_SIZE cubed, one channel): a Worley sum of three octaves, eroding the shapes' edges
 *    (wisps below, billows above).
 *  - WEATHER (CLOUD_NOISE_WEATHER_SIZE squared, four channels): flat patterns over a large area: R where clouds gather
 *    (a gradient sum of large blobs), G a second, finer gradient sum (the local variation of coverage and type), B the
 *    high clouds' fibres (a gradient sum stretched eight to one along one axis and warped), A small cells (an inverted Worley sum:
 *    the mid clouds' and the high clouds' puffs).
 * The renderer samples them with trilinear filtering and wrapping at its own scales (metres per tile).
 */


// Macros.
/** @brief The generator's version: a change of any texture's bytes changes it (tests/CloudNoiseTests.c pins each
 *  texture's fingerprint for it: the clouds' look is tuned on these bytes). */
#define CLOUD_NOISE_VERSION 1
/** @brief The shape texture's side, in texels (a power of two). */
#define CLOUD_NOISE_SHAPE_SIZE 64
/** @brief The detail texture's side, in texels (a power of two). */
#define CLOUD_NOISE_DETAIL_SIZE 32
/** @brief The weather texture's side, in texels (a power of two). */
#define CLOUD_NOISE_WEATHER_SIZE 512
/** @brief The seed every texture is made from. */
#define CLOUD_NOISE_SEED 0x5EEDC10Du


// Functions.
/**
 * @brief Gradient noise in 3D that repeats every @p period lattice cells along each axis.
 * @param x The point, in lattice cells.
 * @param y
 * @param z
 * @param period The lattice's period, at least 1.
 * @param seed Picks the gradients.
 * @returns -1 .. 1 (0 at every lattice point).
 */
double CloudNoise_Gradient3(double x, double y, double z, int32_t period, uint32_t seed);

/**
 * @brief Gradient noise in 2D that repeats every @p period lattice cells along each axis.
 * @param x The point, in lattice cells.
 * @param y
 * @param period The lattice's period, at least 1.
 * @param seed Picks the gradients.
 * @returns -1 .. 1 (0 at every lattice point).
 */
double CloudNoise_Gradient2(double x, double y, int32_t period, uint32_t seed);

/**
 * @brief Worley noise in 3D that repeats every @p period cells along each axis: the distance from a point to the nearest
 *        feature point (one per cell, placed in it by a hash), in cells.
 * @param x The point, in cells.
 * @param y
 * @param z
 * @param period The cells' period, at least 1.
 * @param seed Places the feature points.
 * @returns The distance, 0 .. 1 (clamped: farther than a cell counts as 1).
 */
double CloudNoise_Worley3(double x, double y, double z, int32_t period, uint32_t seed);

/**
 * @brief Worley noise in 2D that repeats every @p period cells along each axis (CloudNoise_Worley3 in the plane).
 * @param x The point, in cells.
 * @param y
 * @param period The cells' period, at least 1.
 * @param seed Places the feature points.
 * @returns The distance, 0 .. 1.
 */
double CloudNoise_Worley2(double x, double y, int32_t period, uint32_t seed);

/**
 * @brief Fills the shape texture (Perlin-Worley; see the file's comment).
 * @param outTexels [out] CLOUD_NOISE_SHAPE_SIZE cubed bytes, x fastest, then y, then z.
 */
void CloudNoise_FillShape(uint8_t* outTexels);

/**
 * @brief Fills the detail texture (a Worley sum; see the file's comment).
 * @param outTexels [out] CLOUD_NOISE_DETAIL_SIZE cubed bytes, x fastest, then y, then z.
 */
void CloudNoise_FillDetail(uint8_t* outTexels);

/**
 * @brief Fills the weather texture (four flat patterns; see the file's comment).
 * @param outTexels [out] CLOUD_NOISE_WEATHER_SIZE squared texels of four bytes (R, G, B, A), x fastest.
 */
void CloudNoise_FillWeather(uint8_t* outTexels);
