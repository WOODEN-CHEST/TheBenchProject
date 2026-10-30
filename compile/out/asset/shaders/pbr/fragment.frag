#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

// Supplied by Raylib's DrawMesh path.
uniform sampler2D texture0;    // MATERIAL_MAP_ALBEDO texture (a 1x1 white texture when the model has none)
uniform vec4 colDiffuse;       // material albedo colour * object tint, sRGB in 0..1

// Surface material parameters, uploaded PER MATERIAL by the renderer's custom mesh draw loop. The scalar
// values are the material's base factors; the optional maps modulate them. A map is only sampled when its
// has*Map flag is set (an unbound sampler would read texture unit 0 = albedo, the classic silent-fallback bug).
uniform float metallic;        // base metallic factor
uniform float roughness;       // base roughness factor
uniform float ao;              // base ambient-occlusion factor
uniform vec3 emissiveColor;    // linear emissive colour
uniform float emissiveIntensity;

uniform sampler2D mraMap;      // packed ORM: R=occlusion, G=roughness, B=metallic; MULTIPLIES the scalars above
uniform sampler2D normalMap;   // tangent-space normal map (RGB in 0..1 -> [-1,1])
uniform sampler2D emissiveMap; // sRGB emissive colour map; MULTIPLIES emissiveColor*emissiveIntensity
uniform bool hasMraMap;
uniform bool hasNormalMap;
uniform bool hasEmissiveMap;

out vec4 finalColor;

// The frame's lights, shadows, ambient, haze and night vision: shared with the sprite shader, which the renderer gives
// the same values.
#include "common/lighting"
// The draw's fade (its draw distance, the object's opacity) as a dither on the scene's pixel grid.
#include "common/dither"

// Perturbs the geometric normal by a tangent-space normal map WITHOUT needing per-vertex tangents: it rebuilds
// a cotangent frame from the screen-space derivatives of the world position and UV (Christian Schuler's
// method). Robust for any mesh (OBJ meshes carry no tangents), at the cost of a per-fragment derivative frame.
// worldPos + uv are the fragment's world position and texture coordinate; n is the interpolated world normal;
// mapN is the sampled normal already remapped to [-1,1].
vec3 PerturbNormal(vec3 n, vec3 worldPos, vec2 uv, vec3 mapN)
{
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 t = dp2perp*duv1.x + dp1perp*duv2.x;
    vec3 b = dp2perp*duv1.y + dp1perp*duv2.y;

    float invMax = inversesqrt(max(dot(t, t), dot(b, b)));
    mat3 tbn = mat3(t*invMax, b*invMax, n);
    return normalize(tbn*mapN);
}

void main()
{
    vec4 texel = texture(texture0, fragTexCoord)*colDiffuse*fragColor;
    vec3 albedo = pow(max(texel.rgb, vec3(0.0)), vec3(2.2)); // sRGB -> linear

    float rough = roughness;
    float metal = metallic;
    float occlusion = ao;
    // ORM map multiplies the scalar factors (glTF metallic-roughness convention, plus occlusion in R).
    if (hasMraMap)
    {
        vec3 orm = texture(mraMap, fragTexCoord).rgb;
        occlusion *= orm.r;
        rough *= orm.g;
        metal *= orm.b;
    }
    rough = clamp(rough, 0.045, 1.0);
    metal = clamp(metal, 0.0, 1.0);

    // The geometric normal places the surface's taps of the sky's visibility (a normal map's bumps would jitter them).
    vec3 surfaceNormal = normalize(fragNormal);
    vec3 n = surfaceNormal;
    if (hasNormalMap)
    {
        vec3 mapN = texture(normalMap, fragTexCoord).xyz*2.0 - 1.0;
        n = PerturbNormal(n, fragPosition, fragTexCoord, mapN);
    }
    vec3 v = normalize(-fragPosition); // render space: the camera at the origin

    // The weather's wetness: darker and glossier, most on surfaces facing up (a metal's albedo is its reflectance: it
    // only turns glossier).
    ApplyWetness(albedo, rough, surfaceNormal, WET_DEFAULT_POROSITY*(1.0 - metal));

    // Base reflectance: 0.04 for dielectrics, the albedo for metals.
    vec3 f0 = mix(vec3(0.04), albedo, metal);

    // The sun and (at night) the moon through the air at the fragment's height, darkened by the shadow levels' shadow, read
    // off the triangle's own face (what the maps hold). Then the point lights, the cube-shadow light read at the fragment
    // itself.
    SkyLightValues light = SkyLightAt(fragPosition);
    float shadowAmount = FaceShadowAmount(fragPosition, FaceNormal(fragPosition, surfaceNormal));
    vec3 direct = SkyLight(light, n, v, shadowAmount, albedo, f0, rough, metal);
    vec3 pointTotal = PointLights(n, v, fragPosition, fragPosition, n, albedo, f0, rough, metal);

    // The sky's own light as the ambient (the sky above, the ground's bounce below, the night's light; under a roof only
    // the share of the sky the surface sees, in a room its ambient zones' light). Screen-space AO darkens ONLY this
    // ambient term (never the direct light).
    float skyVisibility = SkyVisibilityAt(fragPosition, surfaceNormal);
    vec3 ambient = AmbientLight(light, n, fragPosition, surfaceNormal, skyVisibility)*albedo*occlusion*ScreenAo();

    vec3 emissive = emissiveColor*emissiveIntensity;
    if (hasEmissiveMap)
    {
        vec3 em = texture(emissiveMap, fragTexCoord).rgb;
        emissive *= pow(max(em, vec3(0.0)), vec3(2.2)); // sRGB -> linear
    }

    // The eye sees the reflected light through its night vision; emissive light is added after it and keeps its colour.
    vec3 reflected = ambient + direct + pointTotal;
    reflected = NightVisionOf(reflected);
    vec3 color = reflected + emissive;

    // The haze in front of it (aerial perspective, the ground haze), then the debug views of interior lighting and the
    // shadow levels while they are on.
    color = ApplyAerialPerspective(color, fragPosition);
    color = InteriorViewColor(color, fragPosition, surfaceNormal, skyVisibility);
    color = ShadowLevelViewColor(color, fragPosition);

    // The fade is tested last, after every derivative and mipmapped fetch above, so those stay defined for the pixels
    // that are drawn.
    if (DitherDiscard())
    {
        discard;
    }

    // Output LINEAR HDR. Tonemapping + gamma happen later in the tonemap post-pass (after the pixelation
    // upscale), so the scene is composited in high dynamic range and mapped to [0,1] only for display. Opaque: the
    // object's opacity is the dither above, not blending.
    // TODO: a material alpha mode (alpha-tested textures such as leaves on a model); until then a model's texture alpha
    // is ignored.
    finalColor = vec4(color, 1.0);
}
