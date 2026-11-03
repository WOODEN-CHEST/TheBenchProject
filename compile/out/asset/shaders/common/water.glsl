// Water, for #include "common/water" (the water's fragment stage, shaders/water): where a point lies on a water material's
// maps, a map's tilt of the surface, the bottom's absorption, the foam and water's Fresnel reflectance. The pure maths
// and the constants' C twins are source/WorldWater.h (tests/ShaderSourceTests.c pins them).
//
// RENDER SPACE. The maps are anchored to the world: a point's place is its render-space x and z (or, along a river, the
// mesh's texture coordinates), and each map's frame carries the camera's position turned into the map's layout, less the
// map's drift, wrapped by its tile in double on the CPU.

// The foam threshold's soft edge (WORLD_WATER_FOAM_EDGE), the least roughness of the surface
// (WORLD_WATER_MIN_ROUGHNESS), and the least a map's normal stands up from its surface when it is read as a slope (a
// normal map's texel lying flat would tilt the surface without bound).
const float WATER_FOAM_EDGE = 0.05;
const float WATER_MIN_ROUGHNESS = 0.045;
const float WATER_MIN_MAP_UP = 0.2;

// Where a place lies on a map (WorldWater_GetMapCoordinate): turned by the map's turn (frame.zw: its cosine and sine),
// moved by its offset (frame.xy), over its tile (1 a repeat).
vec2 WaterMapCoordinate(vec2 place, vec4 frame, float inverseTile)
{
    vec2 turned = vec2(frame.z*place.x + frame.w*place.y, -frame.w*place.x + frame.z*place.y);
    return (turned + frame.xy)*inverseTile;
}

// How a map's texel (a tangent-space normal, OpenGL's convention: x toward the image's right, y toward its top) tilts the
// surface, as a slope along the surface: axisU and axisV are the world's directions in which the place's x and y grow
// (the frame's turn takes the map's axes onto them; its rows run down the image, so its top lies toward -V).
vec3 WaterMapTilt(vec3 texel, vec4 frame, vec3 axisU, vec3 axisV)
{
    vec3 n = texel*2.0 - 1.0;
    vec2 slope = n.xy/max(n.z, WATER_MIN_MAP_UP);
    vec3 right = frame.z*axisU + frame.w*axisV;
    vec3 top = frame.w*axisU - frame.z*axisV;
    return slope.x*right + slope.y*top;
}

// How much of what lies under the water still comes through over a path through it (WorldWater_GetTransmittance).
vec3 WaterTransmittance(vec3 absorption, float path)
{
    return exp(-max(absorption, vec3(0.0))*max(path, 0.0));
}

// How much foam covers a point (WorldWater_GetFoam): the pattern's texel shows foam while the water's depth there is under
// the foam depth times its value, softened at the edge, times the strength.
float WaterFoam(float texel, float depth, float foamDepth, float foamStrength)
{
    if (!(foamDepth > 0.0) || !(foamStrength > 0.0))
    {
        return 0.0;
    }
    float ratio = max(depth, 0.0)/foamDepth;
    return smoothstep(ratio - WATER_FOAM_EDGE, ratio + WATER_FOAM_EDGE, texel)*min(foamStrength, 1.0);
}

// Water's Fresnel reflectance facing it f0, seen at the cosine nDotV: Schlick's.
float WaterFresnel(float nDotV, float f0)
{
    float facing = 1.0 - clamp(nDotV, 0.0, 1.0);
    return f0 + (1.0 - f0)*facing*facing*facing*facing*facing;
}
