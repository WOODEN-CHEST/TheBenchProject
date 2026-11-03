// Screen-space reflections, for #include "common/reflections": the share of a surface's reflection the screen may answer
// by its roughness, and the packing of the direction it mirrors along, shared by the lit shaders (which hand each
// mirroring pixel's data to the reflection pass: common/reflection_output) and the pass (shaders/reflections). The pure
// maths and the constants' C twins are source/WorldReflections.h (tests/ShaderSourceTests.c pins them).

// The roughness up to which a surface mirrors the screen in full, and from which its reflection is the sky's alone.
const float REFLECTION_SHARP_ROUGHNESS = 0.1;
const float REFLECTION_ROUGH_ROUGHNESS = 0.4;

// How much of a surface's reflection the screen may answer by its roughness (WorldReflections_GetScreenShare).
float ReflectionScreenShare(float roughness)
{
    return 1.0 - smoothstep(REFLECTION_SHARP_ROUGHNESS, REFLECTION_ROUGH_ROUGHNESS, roughness);
}

// 1 for a part at or above 0, -1 below (the octahedral map's sign: zero counts as positive).
vec2 ReflectionSignNotZero(vec2 v)
{
    return vec2((v.x >= 0.0) ? 1.0 : -1.0, (v.y >= 0.0) ? 1.0 : -1.0);
}

// A unit direction packed into two numbers in [-1, 1], an octahedral map (WorldReflections_EncodeDirection).
vec2 ReflectionEncodeDirection(vec3 direction)
{
    float sum = abs(direction.x) + abs(direction.y) + abs(direction.z);
    if (!(sum > 0.0))
    {
        return vec2(0.0);
    }
    vec3 n = direction/sum;
    return (n.z >= 0.0) ? n.xy : (1.0 - abs(n.yx))*ReflectionSignNotZero(n.xy);
}

// The direction ReflectionEncodeDirection packed (WorldReflections_DecodeDirection).
vec3 ReflectionDecodeDirection(vec2 encoded)
{
    vec3 n = vec3(encoded, 1.0 - abs(encoded.x) - abs(encoded.y));
    if (n.z < 0.0)
    {
        n.xy = (1.0 - abs(encoded.yx))*ReflectionSignNotZero(encoded);
    }
    return normalize(n);
}
