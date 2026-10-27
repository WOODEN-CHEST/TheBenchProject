// Periodic value noise for patterns anchored to the world, for #include "common/value_noise" (the terrain's rules).
// Random values at the integer cells, hashed with pcg3d (Jarzynski and Olano 2020, "Hash Functions for GPU Rendering"),
// blended with a quintic fade; the cells wrap every VALUE_NOISE_PERIOD along each axis, so the noise repeats there.
//
// Its C mirror is TerrainBlend_GetValueNoise (source/TerrainBlend.c): the same integers in the same order, so the two
// agree within float rounding. Change both together.
//
// RENDER SPACE. A pattern anchored to the world never receives a far position as floats: the renderer wraps the place it
// is drawn at (the camera, a terrain chunk's centre) by the pattern's period, VALUE_NOISE_PERIOD cells times the cell size,
// in double, and the shader adds that to its small local offset (agents/WorldRuntime.md, "Render space"). The lattice
// wraps too, so the sum may run past one period.

// The period in cells along each axis (a power of two: the wrap is a mask). TerrainBlend.h's TERRAIN_BLEND_NOISE_PERIOD.
const int VALUE_NOISE_PERIOD = 256;

// Three words to three well-mixed ones.
uvec3 ValueNoisePcg3d(uvec3 v)
{
    v = v*1664525u + 1013904223u;
    v.x += v.y*v.z;
    v.y += v.z*v.x;
    v.z += v.x*v.y;
    v ^= v >> 16u;
    v.x += v.y*v.z;
    v.y += v.z*v.x;
    v.z += v.x*v.y;
    return v;
}

// A cell's random value (the cell already wrapped onto the period), 0 to 1 in 2^24 steps.
float ValueNoiseLattice(ivec3 cell, uint seed)
{
    uvec3 v = uvec3(cell) ^ uvec3(seed, seed*0x9E3779B9u, seed*0x85EBCA6Bu);
    return float(ValueNoisePcg3d(v).x >> 8u)/16777215.0;
}

// The noise at a point given in cells, 0 to 1.
float ValueNoise(vec3 p, uint seed)
{
    vec3 base = floor(p);
    vec3 f = p - base;
    vec3 u = f*f*f*(f*(f*6.0 - 15.0) + 10.0);
    ivec3 c0 = ivec3(base) & ivec3(VALUE_NOISE_PERIOD - 1);
    ivec3 c1 = (ivec3(base) + ivec3(1)) & ivec3(VALUE_NOISE_PERIOD - 1);
    float nearValue = mix(
        mix(ValueNoiseLattice(ivec3(c0.x, c0.y, c0.z), seed), ValueNoiseLattice(ivec3(c1.x, c0.y, c0.z), seed), u.x),
        mix(ValueNoiseLattice(ivec3(c0.x, c1.y, c0.z), seed), ValueNoiseLattice(ivec3(c1.x, c1.y, c0.z), seed), u.x),
        u.y);
    float farValue = mix(
        mix(ValueNoiseLattice(ivec3(c0.x, c0.y, c1.z), seed), ValueNoiseLattice(ivec3(c1.x, c0.y, c1.z), seed), u.x),
        mix(ValueNoiseLattice(ivec3(c0.x, c1.y, c1.z), seed), ValueNoiseLattice(ivec3(c1.x, c1.y, c1.z), seed), u.x),
        u.y);
    return mix(nearValue, farValue, u.z);
}
