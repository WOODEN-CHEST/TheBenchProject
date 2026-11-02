#version 330

// A CRT's glass (world_crt, and world_crt_skinned over the PBR's skinned vertex stage): its material lit as the PBR shader
// lights a model (a dielectric glass over the phosphor's colour, its reflectance the coating's share of a bare glass's),
// what the glass mirrors (the sky, the ground, a room's light), and behind it the picture a CRT shows (common/crt: the
// curvature, the scanlines, the mask, the glow, the vignette and the power animation), the share of its light the glass
// lets through.

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

// Supplied by Raylib's DrawMesh path.
uniform sampler2D texture0;    // MATERIAL_MAP_ALBEDO texture: the phosphor's colour under the glass (white without one)
uniform vec4 colDiffuse;       // material albedo colour * object tint, sRGB in 0..1

// The material, uploaded per material as for the PBR shader (WorldRendererLighting_UploadMaterial): its factors, its packed
// ORM map, the picture as its emissive map (sRGB, its colour and intensity the screen's glow), wetness.
uniform float metallic;
uniform float roughness;
uniform float ao;
uniform vec3 emissiveColor;
uniform float emissiveIntensity;
uniform float materialPorosity;
uniform float materialPuddles;
uniform sampler2D mraMap;
uniform sampler2D emissiveMap;
uniform bool hasMraMap;
uniform bool hasEmissiveMap;   // false: no picture (off, or none handed over), the glass alone
uniform bool isEmissiveMapBottomUp;
// The share of a bare glass's reflectance the CRT's glass keeps (its anti-glare coating takes the rest), per CRT draw.
uniform float crtReflection;

out vec4 finalColor;

#include "common/lighting"
#include "common/dither"
#include "common/crt"

// A bare glass's reflectance head-on (a dielectric's); the CRT's coating keeps crtReflection of it.
const float CRT_GLASS_F0 = 0.04;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord)*colDiffuse*fragColor;
    vec3 albedo = pow(max(texel.rgb, vec3(0.0)), vec3(2.2)); // sRGB -> linear

    float rough = roughness;
    float metal = metallic;
    float occlusion = ao;
    if (hasMraMap)
    {
        vec3 orm = texture(mraMap, fragTexCoord).rgb;
        occlusion *= orm.r;
        rough *= orm.g;
        metal *= orm.b;
    }
    rough = clamp(rough, 0.045, 1.0);
    metal = clamp(metal, 0.0, 1.0);

    vec3 surfaceNormal = normalize(fragNormal);
    vec3 n = surfaceNormal;
    vec3 v = normalize(-fragPosition); // render space: the camera at the origin

    float water;
    ApplyWetness(albedo, rough, n, water, fragPosition, surfaceNormal, materialPorosity*(1.0 - metal), materialPuddles);

    // The glass's reflectance: its coating's share of a bare glass's (a metal's albedo, were it one).
    vec3 f0 = mix(vec3(CRT_GLASS_F0*clamp(crtReflection, 0.0, 1.0)), albedo, metal);

    // The lights through the glass's BRDF (their highlights on it, their light on the phosphor under it), the sky's
    // ambient on the phosphor, and what the glass mirrors all around.
    SkyLightValues light = SkyLightAt(fragPosition);
    float shadowAmount = FaceShadowAmount(fragPosition, FaceNormal(fragPosition, surfaceNormal));
    vec3 direct = SkyLight(light, n, v, shadowAmount, albedo, f0, rough, metal);
    vec3 pointTotal = PointLights(n, v, fragPosition, fragPosition, n, albedo, f0, rough, metal)
        + ScreenLight(n, v, fragPosition, albedo, f0, rough, metal);
    float skyVisibility = SkyVisibilityAt(fragPosition, surfaceNormal);
    vec3 glass = FresnelSchlickRoughness(clamp(dot(n, v), 0.0, 1.0), f0, rough);
    vec3 ambient = AmbientLight(light, n, fragPosition, surfaceNormal, skyVisibility)*albedo*occlusion*ScreenAo()
        *(vec3(1.0) - glass);
    vec3 mirrored = glass*EnvironmentRadiance(light, reflect(-v, n), fragPosition, surfaceNormal, skyVisibility)
        *occlusion;

    // The picture behind the glass: what the glass lets through of it. Without a picture, the material's own emission
    // (black for a dark screen; the LOD tinting's glow).
    vec3 emissive = emissiveColor*emissiveIntensity*(vec3(1.0) - glass);
    if (hasEmissiveMap)
    {
        emissive = CrtGlow(emissiveMap, isEmissiveMapBottomUp, fragTexCoord)*emissiveColor*emissiveIntensity
            *(vec3(1.0) - glass);
    }

    // Reflected light through the eye's night vision; the picture's light keeps its colour.
    vec3 reflected = ambient + direct + pointTotal + mirrored + WaterReflection(light, n, v, water);
    vec3 color = NightVisionOf(reflected) + emissive;

    color = ApplyAerialPerspective(color, fragPosition);
    color = InteriorViewColor(color, fragPosition, surfaceNormal, skyVisibility);
    color = ShadowLevelViewColor(color, fragPosition);
    color = RainViewColor(color, fragPosition, surfaceNormal, water);

    // The fade is tested last, after every derivative and mipmapped fetch above.
    if (DitherDiscard())
    {
        discard;
    }
    finalColor = vec4(color, 1.0);
}
