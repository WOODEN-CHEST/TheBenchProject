#version 330

// The terrain's lit fragment stage (B01): the weighted mix of a debug colour per palette entry (B02 replaces it with the
// terrain materials), lit as world_pbr lights a dielectric of roughness 0.9 without maps: the sun and the moon with the
// cascades' shadow, the point lights, the ambient with the screen's AO, the night vision and the fog. The draw's fade
// dithers it.

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragWeights0;
in vec4 fragWeights1;
in vec4 fragWeights2;
in vec4 fragWeights3;

// Supplied by raylib's DrawMesh path: the object's tint (its opacity is the dither's), sRGB in 0..1.
uniform vec4 colDiffuse;
// The chunk key's colour (rgb, sRGB) and how much of it shows (a: 0 while the key is off).
uniform vec4 chunkTint;

out vec4 finalColor;

// The frame's lights, shadows, ambient, fog and night vision, as world_pbr has them.
#include "common/lighting"
// The draw's fade (its draw distance, the object's opacity) as a dither on the scene's pixel grid.
#include "common/dither"

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
const float ROUGHNESS = 0.9;

void main()
{
    // The palette's colours mixed by the weights in linear light; a vertex without any weight shows the first one.
    float weights[16] = float[16](fragWeights0.x, fragWeights0.y, fragWeights0.z, fragWeights0.w,
        fragWeights1.x, fragWeights1.y, fragWeights1.z, fragWeights1.w,
        fragWeights2.x, fragWeights2.y, fragWeights2.z, fragWeights2.w,
        fragWeights3.x, fragWeights3.y, fragWeights3.z, fragWeights3.w);
    vec3 mixed = vec3(0.0);
    float total = 0.0;
    for (int i = 0; i < 16; i++)
    {
        mixed += pow(DEBUG_PALETTE[i], vec3(2.2))*weights[i];
        total += weights[i];
    }
    vec3 albedo = (total > (0.5/255.0)) ? (mixed/total) : pow(DEBUG_PALETTE[0], vec3(2.2));
    albedo = mix(albedo, pow(chunkTint.rgb, vec3(2.2)), chunkTint.a);
    albedo *= pow(max(colDiffuse.rgb, vec3(0.0)), vec3(2.2));

    vec3 n = normalize(fragNormal);
    vec3 v = normalize(-fragPosition); // render space: the camera at the origin
    vec3 f0 = vec3(0.04);

    // The sun and the moon, darkened by the cascades' shadow; then the point lights, the cube-shadow light read at the
    // fragment itself.
    float shadowAmount = SkyShadowAmount(fragPosition, max(dot(n, normalize(shadowLightDirection)), 0.0));
    vec3 direct = SkyLight(n, v, shadowAmount, albedo, f0, ROUGHNESS, 0.0);
    vec3 pointTotal = PointLights(n, v, fragPosition, fragPosition, n, albedo, f0, ROUGHNESS, 0.0);
    vec3 ambient = ambientColor*ambientIntensity*albedo*ScreenAo();

    vec3 color = NightVisionOf(ambient + direct + pointTotal);
    color = ApplyFog(color, fragPosition);

    // The fade is tested last, so every value above stays defined for the pixels that are drawn.
    if (DitherDiscard())
    {
        discard;
    }
    finalColor = vec4(color, 1.0);
}
