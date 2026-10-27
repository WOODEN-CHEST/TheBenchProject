#version 330

// The terrain's lit fragment stage: up to sixteen terrain materials blended per pixel from texture arrays, lit as
// world_pbr lights a dielectric (the sun and the moon with the cascades'
// shadow, the point lights, the ambient with the screen's AO, the night vision and the haze), dithered by the draw's
// fade. The blend, in order (its C mirror is source/TerrainBlend.c: change both together):
// 1. the painted weights, interpolated and divided by their sum (none at all: the first material);
// 2. the terrain's rules, in order: each moves its strength (opacity x slope x height x noise) of every covered
//    material's weight to its own; a face material's faces are left as they are;
// 3. height blending: each material with weight scores its weight plus its height times its strength, and those within
//    HEIGHT_BLEND_BAND of the best share the pixel, so where stones meet grass the stones' tops win first;
// 4. sampling: each material's maps laid along the terrain's own axes (they move with its object, as a model's textures
//    do), planar from above or triplanar where it asks, only for the materials with a share left; the normals reoriented
//    onto the surface (whiteout blend) in the terrain's space, then turned into the world; no mesh tangents.
// The rules' slope and height are the world's (a snow line stays where it is when a terrain moves); their noise lies in
// the terrain's space, like the maps.
// Without the arrays (texture arrays unavailable) each material shows its mean colour. terrainView shows the steps.
// Steps 3 and 4 stream over the materials (the best score, then each material's share and surface summed), holding no
// array per material but the weights: arrays of scores, shares and samples nearly double this pass's cost (3.3 to 3.7
// against 1.5 to 2.0 ms for views full of terrain at 1280 x 720 on a GTX 980 Ti). Keep it that way.

in vec3 fragPosition;
in vec3 fragNormal;
in vec3 fragTerrainOffset;
in vec3 fragTerrainNormal;
in float fragHeightOffset;
in vec4 fragWeights0;
in vec4 fragWeights1;
in vec4 fragWeights2;
in vec4 fragWeights3;
in float fragFaceMaterial;

// Supplied by raylib's DrawMesh path: the object's tint (its opacity is the dither's), sRGB in 0..1, and the normal
// matrix (transpose(inverse(matModel))), which turns the terrain's normals into the world's.
uniform vec4 colDiffuse;
uniform mat4 matNormal;
// The chunk key's colour (rgb, sRGB) and how much of it shows (a: 0 while the key is off).
uniform vec4 chunkTint;
// What is shown (WorldRendererTerrainView): 0 shaded, 1 the painted weights, 2 the weights after the rules, 3 the dominant
// material; the last three in the debug palette's colours.
uniform int terrainView;

// The terrain's materials (per terrain), in palette order; materialCount of the arrays are used.
uniform int materialCount;
uniform bool hasMaterialArrays;        // false: the materials' mean colours, no maps
uniform vec4 materialTint[16];         // rgb: the albedo tint, linear
uniform vec4 materialSurface[16];      // x: 1 / tile size, y: roughness, z: normal strength, w: height blend strength
uniform vec4 materialProjection[16];   // x: triplanar (0 off, 1 steep, 2 always), y: triplanar slope (degrees),
                                       // z: puddles, w: porosity (both for the weather's wetness, not built yet)
uniform vec4 materialMeanColor[16];    // rgb: the albedo's mean times the tint, linear
uniform sampler2DArray materialAlbedoHeight; // a layer per material: rgb the albedo (sRGB), a the height
uniform sampler2DArray materialNormal;       // rg: the normal map's x and y (OpenGL's convention)
uniform sampler2DArray materialRoughnessAo;  // r: roughness, g: ambient occlusion

// The terrain's rules (per terrain), in order; ruleCount of them.
uniform int ruleCount;
uniform ivec4 ruleIds[8];              // x: material, y: the covered materials' bits, z: the noise's seed (its bits),
                                       // w: conditions: 1 slope, 2 height, 4 noise, 8 slope min, 16 slope max,
                                       // 32 height min, 64 height max
uniform vec4 ruleSlope[8];             // x: min, y: max, z: blend (degrees), w: opacity
uniform vec4 ruleHeight[8];            // x: min, y: max, z: blend (metres)
uniform vec4 ruleNoise[8];             // x: 1 / cell size, y: strength

// Per chunk: its centre in the terrain's space, wrapped by each material's tile size and by each rule's noise period (in
// double on the CPU), and its centre's height in the world.
uniform vec3 materialUvOffset[16];
uniform vec3 ruleNoiseOffset[8];
uniform float chunkHeight;

out vec4 finalColor;

// The frame's lights, shadows, ambient, haze and night vision, as world_pbr has them.
#include "common/lighting"
// The draw's fade (its draw distance, the object's opacity) as a dither on the scene's pixel grid.
#include "common/dither"
// The rules' noise.
#include "common/value_noise"

// TerrainBlend.h's TERRAIN_BLEND_HEIGHT_BAND, TERRAIN_BLEND_WEIGHT_EPSILON and TERRAIN_BLEND_TRIPLANAR_RAMP_DEGREES (a
// test reads them from this file).
const float HEIGHT_BLEND_BAND = 0.1;
const float WEIGHT_EPSILON = 0.5/255.0;
const float TRIPLANAR_RAMP_DEGREES = 10.0;
// A material without a height map is flat at mid-height (TERRAIN_MATERIAL_FLAT_HEIGHT).
const float FLAT_HEIGHT = 128.0/255.0;
// The debug views' roughness.
const float DEBUG_ROUGHNESS = 0.9;

// The debug colour of each palette entry, sRGB: distinct and earthy, in palette order.
const vec3 DEBUG_PALETTE[16] = vec3[16](
    vec3(0.33, 0.52, 0.22),  // 0: grass green
    vec3(0.47, 0.35, 0.22),  // 1: earth brown
    vec3(0.52, 0.51, 0.48),  // 2: rock grey
    vec3(0.80, 0.73, 0.52),  // 3: sand
    vec3(0.20, 0.33, 0.16),  // 4: dark moss
    vec3(0.64, 0.40, 0.26),  // 5: clay
    vec3(0.86, 0.87, 0.89),  // 6: snow
    vec3(0.30, 0.27, 0.25),  // 7: gravel
    vec3(0.62, 0.62, 0.30),  // 8: dry grass
    vec3(0.36, 0.22, 0.30),  // 9: heather
    vec3(0.25, 0.37, 0.42),  // 10: slate
    vec3(0.72, 0.56, 0.33),  // 11: ochre
    vec3(0.42, 0.48, 0.36),  // 12: lichen
    vec3(0.18, 0.18, 0.20),  // 13: asphalt
    vec3(0.68, 0.30, 0.22),  // 14: brick red
    vec3(0.55, 0.62, 0.68)); // 15: ice


// ---- The rules ------------------------------------------------------------------------------------------------
// 1 above a bound, a smooth step across its blend (centred on it), a hard step without one.
float Rising(float value, float bound, float blend)
{
    return (blend > 0.0) ? smoothstep(bound - 0.5*blend, bound + 0.5*blend, value) : ((value >= bound) ? 1.0 : 0.0);
}

// 1 below a bound, likewise.
float Falling(float value, float bound, float blend)
{
    return (blend > 0.0) ? (1.0 - smoothstep(bound - 0.5*blend, bound + 0.5*blend, value))
        : ((value <= bound) ? 1.0 : 0.0);
}

// A rule's strength here: its opacity times its conditions.
float RuleStrength(int rule, float slopeDegrees, float height)
{
    int conditions = ruleIds[rule].w;
    float strength = ruleSlope[rule].w;
    if ((conditions & 1) != 0)
    {
        if ((conditions & 8) != 0) { strength *= Rising(slopeDegrees, ruleSlope[rule].x, ruleSlope[rule].z); }
        if ((conditions & 16) != 0) { strength *= Falling(slopeDegrees, ruleSlope[rule].y, ruleSlope[rule].z); }
    }
    if ((conditions & 2) != 0)
    {
        if ((conditions & 32) != 0) { strength *= Rising(height, ruleHeight[rule].x, ruleHeight[rule].z); }
        if ((conditions & 64) != 0) { strength *= Falling(height, ruleHeight[rule].y, ruleHeight[rule].z); }
    }
    if (((conditions & 4) != 0) && (strength > 0.0))
    {
        vec3 cell = (fragTerrainOffset + ruleNoiseOffset[rule])*ruleNoise[rule].x;
        float noise = ValueNoise(cell, uint(ruleIds[rule].z));
        strength *= (1.0 - ruleNoise[rule].y) + ruleNoise[rule].y*noise;
    }
    return strength;
}

// The rules, in order, on normalized weights.
void ApplyRules(inout float weights[16], float slopeDegrees, float height)
{
    int rules = min(ruleCount, 8);
    for (int rule = 0; rule < rules; rule++)
    {
        int material = ruleIds[rule].x;
        if ((material < 0) || (material >= materialCount))
        {
            continue;
        }
        float strength = RuleStrength(rule, slopeDegrees, height);
        if (!(strength > 0.0))
        {
            continue;
        }
        float moved = 0.0;
        for (int i = 0; i < 16; i++)
        {
            if (i >= materialCount) { break; }
            if ((i == material) || (((ruleIds[rule].y >> i) & 1) == 0)) { continue; }
            float given = weights[i]*strength;
            weights[i] -= given;
            moved += given;
        }
        weights[material] += moved;
    }
}


// ---- Sampling -------------------------------------------------------------------------------------------------
// How much each projection counts (the side along X, the top, the side along Z), as TerrainBlend_GetTriplanarWeights.
vec3 TriplanarWeights(vec3 n, int material, float slopeDegrees)
{
    float mode = materialProjection[material].x;
    if (mode < 0.5)
    {
        return vec3(0.0, 1.0, 0.0);
    }
    vec3 sides = n*n;
    sides *= sides;
    float sum = sides.x + sides.y + sides.z;
    if (!(sum > 0.0))
    {
        return vec3(0.0, 1.0, 0.0);
    }
    sides /= sum;
    if (mode > 1.5)
    {
        return sides;
    }
    float threshold = materialProjection[material].y;
    float ramp = smoothstep(threshold - 0.5*TRIPLANAR_RAMP_DEGREES, threshold + 0.5*TRIPLANAR_RAMP_DEGREES,
        slopeDegrees);
    return mix(vec3(0.0, 1.0, 0.0), sides, ramp);
}

// A material's texture coordinates on its three projections (TerrainBlend_GetProjectionCoordinates): each side's image
// upright and unmirrored seen from outside. q is the position in repeats of its maps, s the normal's signs.
vec2 UvX(vec3 q, vec3 s) { return vec2(-s.x*q.z, -q.y); }
vec2 UvY(vec3 q, vec3 s) { return vec2(-s.y*q.x, -q.z); }
vec2 UvZ(vec3 q, vec3 s) { return vec2(s.z*q.x, -q.y); }

// One array's layer blended over the projections, with explicit gradients (the sampling sits in branches that differ
// between neighbouring pixels).
vec4 SampleArray(sampler2DArray maps, float layer, vec3 projections, vec3 q, vec3 qdx, vec3 qdy, vec3 s)
{
    vec4 result = vec4(0.0);
    if (projections.x > 0.0)
    {
        result += projections.x*textureGrad(maps, vec3(UvX(q, s), layer), UvX(qdx, s), UvX(qdy, s));
    }
    if (projections.y > 0.0)
    {
        result += projections.y*textureGrad(maps, vec3(UvY(q, s), layer), UvY(qdx, s), UvY(qdy, s));
    }
    if (projections.z > 0.0)
    {
        result += projections.z*textureGrad(maps, vec3(UvZ(q, s), layer), UvZ(qdx, s), UvZ(qdy, s));
    }
    return result;
}

// A normal map's texel (OpenGL's convention) as a tangent-space normal, tilted by the material's strength.
vec3 DecodeNormal(vec2 texel, float strength)
{
    vec2 xy = clamp((texel*255.0 - 128.0)/127.0, -1.0, 1.0)*strength;
    float lengthSquared = dot(xy, xy);
    if (lengthSquared > 1.0)
    {
        xy /= sqrt(lengthSquared);
        lengthSquared = 1.0;
    }
    return vec3(xy, sqrt(1.0 - lengthSquared));
}

// A detail normal on one projection, reoriented onto the surface (the whiteout blend in the projection's frame: tangent
// t along its u, bitangent b toward its image's top, axis a toward the viewer), in the terrain's space.
vec3 Whiteout(vec3 n, vec3 detail, vec3 t, vec3 b, vec3 a)
{
    vec3 base = vec3(dot(n, t), dot(n, b), dot(n, a));
    vec3 blended = normalize(vec3(base.xy + detail.xy, base.z*detail.z));
    return t*blended.x + b*blended.y + a*blended.z;
}

// A material's normal: its map on each projection, reoriented onto the surface, blended by the projections.
vec3 SampleNormal(int material, vec3 n, vec3 projections, vec3 q, vec3 qdx, vec3 qdy, vec3 s)
{
    float layer = float(material);
    float strength = materialSurface[material].z;
    vec3 result = vec3(0.0);
    if (projections.x > 0.0)
    {
        vec2 texel = textureGrad(materialNormal, vec3(UvX(q, s), layer), UvX(qdx, s), UvX(qdy, s)).rg;
        result += projections.x*Whiteout(n, DecodeNormal(texel, strength), vec3(0.0, 0.0, -s.x), vec3(0.0, 1.0, 0.0),
            vec3(s.x, 0.0, 0.0));
    }
    if (projections.y > 0.0)
    {
        vec2 texel = textureGrad(materialNormal, vec3(UvY(q, s), layer), UvY(qdx, s), UvY(qdy, s)).rg;
        result += projections.y*Whiteout(n, DecodeNormal(texel, strength), vec3(-s.y, 0.0, 0.0), vec3(0.0, 0.0, 1.0),
            vec3(0.0, s.y, 0.0));
    }
    if (projections.z > 0.0)
    {
        vec2 texel = textureGrad(materialNormal, vec3(UvZ(q, s), layer), UvZ(qdx, s), UvZ(qdy, s)).rg;
        result += projections.z*Whiteout(n, DecodeNormal(texel, strength), vec3(s.z, 0.0, 0.0), vec3(0.0, 1.0, 0.0),
            vec3(0.0, 0.0, s.z));
    }
    return result;
}

// A material's albedo (sRGB, rgb) and height (a) at this pixel, as the arrays hold them; flat without the arrays.
vec4 MaterialAlbedoHeight(int material, vec3 terrainNormal, float terrainSlopeDegrees, vec3 offsetDx, vec3 offsetDy,
    vec3 signs)
{
    if (!hasMaterialArrays)
    {
        return vec4(0.0, 0.0, 0.0, FLAT_HEIGHT);
    }
    vec3 projections = TriplanarWeights(terrainNormal, material, terrainSlopeDegrees);
    float scale = materialSurface[material].x;
    vec3 q = (fragTerrainOffset + materialUvOffset[material])*scale;
    return SampleArray(materialAlbedoHeight, float(material), projections, q, offsetDx*scale, offsetDy*scale, signs);
}

// The debug palette mixed by weights in linear light.
vec3 DebugMix(float weights[16])
{
    vec3 mixed = vec3(0.0);
    for (int i = 0; i < 16; i++)
    {
        mixed += pow(DEBUG_PALETTE[i], vec3(2.2))*weights[i];
    }
    return mixed;
}


void main()
{
    vec3 geometric = normalize(fragNormal);
    // The terrain's own normal places the projections; the world's gives the slope.
    vec3 terrainNormal = normalize(fragTerrainNormal);
    // Derivatives first, in uniform control flow: the maps are sampled in branches with explicit gradients.
    vec3 offsetDx = dFdx(fragTerrainOffset);
    vec3 offsetDy = dFdy(fragTerrainOffset);
    float slopeDegrees = degrees(acos(clamp(geometric.y, -1.0, 1.0)));
    float terrainSlopeDegrees = degrees(acos(clamp(terrainNormal.y, -1.0, 1.0)));
    vec3 signs = vec3((terrainNormal.x < 0.0) ? -1.0 : 1.0, (terrainNormal.y < 0.0) ? -1.0 : 1.0,
        (terrainNormal.z < 0.0) ? -1.0 : 1.0);
    int materials = clamp(materialCount, 1, 16);

    // 1. The painted weights, divided by their sum; without any, the first material.
    float weights[16] = float[16](fragWeights0.x, fragWeights0.y, fragWeights0.z, fragWeights0.w,
        fragWeights1.x, fragWeights1.y, fragWeights1.z, fragWeights1.w,
        fragWeights2.x, fragWeights2.y, fragWeights2.z, fragWeights2.w,
        fragWeights3.x, fragWeights3.y, fragWeights3.z, fragWeights3.w);
    float total = 0.0;
    for (int i = 0; i < 16; i++)
    {
        if (i >= materials) { weights[i] = 0.0; continue; }
        total += weights[i];
    }
    if (!(total >= WEIGHT_EPSILON))
    {
        for (int i = 0; i < 16; i++) { weights[i] = (i == 0) ? 1.0 : 0.0; }
    }
    else
    {
        for (int i = 0; i < 16; i++) { weights[i] /= total; }
    }
    // The painted view's colours, before the rules (only the debug views mix the palette).
    vec3 debugColor = (terrainView == 1) ? DebugMix(weights) : vec3(0.0);

    // 2. The rules (not on a face material's faces).
    if (fragFaceMaterial < 0.5)
    {
        ApplyRules(weights, slopeDegrees, chunkHeight + fragHeightOffset);
    }
    if (terrainView == 2)
    {
        debugColor = DebugMix(weights);
    }

    // 3. Height blending, streamed over the materials (arrays of scores, shares or samples per material would cost the
    // GPU most of its parallelism): the best score first (a material's height is its albedo map's alpha), then each
    // material's share of the pixel, its surface summed by share and divided by the shares' total.
    float best = 0.0;
    bool hasAny = false;
    for (int i = 0; i < 16; i++)
    {
        if ((i >= materials) || !(weights[i] >= WEIGHT_EPSILON))
        {
            continue;
        }
        float height = MaterialAlbedoHeight(i, terrainNormal, terrainSlopeDegrees, offsetDx, offsetDy, signs).a;
        float score = weights[i] + height*materialSurface[i].w;
        best = hasAny ? max(best, score) : score;
        hasAny = true;
    }
    float floorScore = best - HEIGHT_BLEND_BAND;

    // 4. The surface: the shared materials' maps (or mean colours), or a debug view's colour.
    float shareTotal = 0.0;
    vec3 albedoSum = vec3(0.0);
    float roughnessSum = 0.0;
    float occlusionSum = 0.0;
    vec3 normalSum = vec3(0.0);
    for (int i = 0; i < 16; i++)
    {
        if ((i >= materials) || !(weights[i] >= WEIGHT_EPSILON))
        {
            continue;
        }
        vec4 albedoHeight = MaterialAlbedoHeight(i, terrainNormal, terrainSlopeDegrees, offsetDx, offsetDy, signs);
        float share = max((weights[i] + albedoHeight.a*materialSurface[i].w) - floorScore, 0.0);
        if (!(share > 0.0))
        {
            continue;
        }
        shareTotal += share;
        if (terrainView != 0)
        {
            continue;
        }
        if (!hasMaterialArrays)
        {
            albedoSum += share*materialMeanColor[i].rgb;
            roughnessSum += share*materialSurface[i].y;
            continue;
        }
        vec3 projections = TriplanarWeights(terrainNormal, i, terrainSlopeDegrees);
        float scale = materialSurface[i].x;
        vec3 q = (fragTerrainOffset + materialUvOffset[i])*scale;
        vec3 qdx = offsetDx*scale;
        vec3 qdy = offsetDy*scale;
        albedoSum += share*pow(max(albedoHeight.rgb, vec3(0.0)), vec3(2.2))*materialTint[i].rgb;
        vec2 surface = SampleArray(materialRoughnessAo, float(i), projections, q, qdx, qdy, signs).rg;
        roughnessSum += share*materialSurface[i].y*surface.r;
        occlusionSum += share*surface.g;
        normalSum += share*SampleNormal(i, terrainNormal, projections, q, qdx, qdy, signs);
    }
    // The dominant material: the largest share, the first among equal ones (only its view asks).
    int dominant = 0;
    if ((terrainView == 3) && (shareTotal > 0.0))
    {
        float dominantShare = -1.0;
        for (int i = 0; i < 16; i++)
        {
            if ((i >= materials) || !(weights[i] >= WEIGHT_EPSILON))
            {
                continue;
            }
            float height = MaterialAlbedoHeight(i, terrainNormal, terrainSlopeDegrees, offsetDx, offsetDy, signs).a;
            float share = max((weights[i] + height*materialSurface[i].w) - floorScore, 0.0)/shareTotal;
            if (share > dominantShare)
            {
                dominant = i;
                dominantShare = share;
            }
        }
    }

    vec3 albedo = vec3(0.0);
    vec3 n = geometric;
    float roughness = DEBUG_ROUGHNESS;
    float occlusion = 1.0;
    if (terrainView == 3)
    {
        albedo = pow(DEBUG_PALETTE[dominant], vec3(2.2));
    }
    else if ((terrainView == 1) || (terrainView == 2))
    {
        albedo = debugColor;
    }
    else if (!(shareTotal > 0.0))
    {
        albedo = materialMeanColor[0].rgb;
        roughness = materialSurface[0].y;
    }
    else if (!hasMaterialArrays)
    {
        albedo = albedoSum/shareTotal;
        roughness = roughnessSum/shareTotal;
    }
    else
    {
        albedo = albedoSum/shareTotal;
        roughness = roughnessSum/shareTotal;
        occlusion = occlusionSum/shareTotal;
        normalSum /= shareTotal;
        n = (dot(normalSum, normalSum) > 1e-12) ? normalize(mat3(matNormal)*normalSum) : geometric;
    }
    albedo = mix(albedo, pow(chunkTint.rgb, vec3(2.2)), chunkTint.a);
    albedo *= pow(max(colDiffuse.rgb, vec3(0.0)), vec3(2.2));
    roughness = clamp(roughness, 0.045, 1.0);

    vec3 v = normalize(-fragPosition); // render space: the camera at the origin
    vec3 f0 = vec3(0.04);

    // The sun and the moon, darkened by the cascades' shadow (its slope bias from the geometry, which the shadow map
    // holds); then the point lights, the cube-shadow light read at the fragment itself.
    SkyLightValues light = SkyLightAt(fragPosition);
    float shadowAmount = SkyShadowAmount(fragPosition, max(dot(geometric, normalize(shadowLightDirection)), 0.0));
    vec3 direct = SkyLight(light, n, v, shadowAmount, albedo, f0, roughness, 0.0);
    vec3 pointTotal = PointLights(n, v, fragPosition, fragPosition, geometric, albedo, f0, roughness, 0.0);
    vec3 ambient = AmbientLight(light, n)*albedo*occlusion*ScreenAo();

    vec3 color = NightVisionOf(ambient + direct + pointTotal);
    color = ApplyAerialPerspective(color, fragPosition);

    // The fade is tested last, after every derivative and mipmapped fetch above, so those stay defined for the pixels
    // that are drawn.
    if (DitherDiscard())
    {
        discard;
    }
    finalColor = vec4(color, 1.0);
}
