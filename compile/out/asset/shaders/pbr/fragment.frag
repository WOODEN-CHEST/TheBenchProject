#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

// Supplied by Raylib's DrawMesh path.
uniform sampler2D texture0;    // MATERIAL_MAP_ALBEDO texture (a 1x1 white texture when the model has none)
uniform vec4 colDiffuse;       // material albedo colour * object tint, sRGB in 0..1

// Camera and lighting, set by WorldRenderer each frame. Light colours are LINEAR.
uniform vec3 viewPos;          // camera world position
uniform vec3 sunDirection;     // unit direction TO the sun, world space
uniform vec3 sunColor;         // linear
uniform float sunIntensity;
uniform vec3 ambientColor;     // linear
uniform float ambientIntensity;

// Sun shadow map — TWO cascades. shadowStrength 0 disables shadows (night, disabled, or shader unavailable).
// The NEAR cascade is a small, sharp map for close detail; the FAR cascade is a large, coarse map for distant /
// large casters (each object's WorldShadowTier decides which cascade(s) it was rendered into). A fragment
// samples the near cascade first and falls back to the far cascade when it lies beyond the near frustum.
uniform mat4 lightVP;          // world -> near-cascade light-clip space
uniform sampler2D shadowMap;   // near-cascade directional depth map
uniform mat4 lightVPFar;       // world -> far-cascade light-clip space
uniform sampler2D shadowMapFar;// far-cascade directional depth map
uniform bool shadowFarActive;  // whether the far cascade is available this frame
uniform float shadowStrength;  // 0..1, how much the sun contribution is darkened in shadow
uniform float shadowBias;      // base depth bias to combat shadow acne
uniform float shadowTexelSize; // 1.0 / shadow-map resolution, for PCF tap offsets

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

// Screen-space ambient occlusion, computed in a pre-pass (shaders/ao) and applied to ONLY the ambient term
// here (so it never dims the direct sun / point lights). aoMapActive false => no SSAO this frame.
uniform sampler2D ambientOcclusionMap; // AO multiplier in R (1 = open, < 1 = occluded)
uniform bool aoMapActive;
uniform vec2 aoResolution;             // scene-target size in pixels, to map gl_FragCoord -> AO map uv

// Point lights, culled + uploaded per object by the renderer (nearest/strongest that reach this object).
// MAX_POINT_LIGHTS MUST match WORLD_MAX_FORWARD_LIGHTS in WorldLightCulling.h.
#define MAX_POINT_LIGHTS 8
uniform int pointLightCount;                          // active entries in the arrays below (0..MAX)
uniform vec3 pointLightPositions[MAX_POINT_LIGHTS];   // world space
uniform vec3 pointLightRadiances[MAX_POINT_LIGHTS];   // linear colour * intensity
uniform float pointLightRanges[MAX_POINT_LIGHTS];     // reach radius (attenuation falls to 0 here)

// ONE shadow-casting point light, shaded SEPARATELY from the culled set above (which excludes it) so it can
// sample its own omnidirectional (cube) shadow map. pointShadowActive false => no shadow-casting point light.
uniform bool pointShadowActive;
uniform vec3 pointShadowLightPos;       // world space
uniform vec3 pointShadowLightRadiance;  // linear colour * intensity
uniform float pointShadowLightRange;    // reach radius
uniform samplerCube pointShadowCube;    // packed (RGBA8) linear distance from the light, per direction
uniform float pointShadowFar;           // far range the stored distance was normalized by
uniform float pointShadowBias;          // world-space depth bias to combat shadow acne

// Atmospheric distance fog: distant geometry fades into the SAME sky the sky pass draws, evaluated along the
// fragment's own view ray, so far objects dissolve seamlessly into the sky (not a flat "close but not quite"
// colour). The sky parameters below MUST stay in sync with shaders/sky/fragment.frag (identical constants +
// atmosphere maths) or a distant silhouette against the sky will show a seam. fogDensity 0 disables fog.
uniform float fogDensity;
uniform float skyTurbidity;     // atmospheric haze; == WorldEnvironment.SkyTurbidity (same value the sky pass gets)
uniform vec3  skyTint;          // linear sky tint multiplier (same value the sky pass gets)
uniform float skySunIntensity;  // RAW sun intensity (NOT day-scaled); == WorldEnvironment.SunIntensity

out vec4 finalColor;

const float PI = 3.14159265358979323846;

// GGX / Trowbridge-Reitz normal distribution.
float DistributionGGX(vec3 n, vec3 h, float rough)
{
    float a = rough*rough;
    float a2 = a*a;
    float nDotH = max(dot(n, h), 0.0);
    float nDotH2 = nDotH*nDotH;
    float denom = nDotH2*(a2 - 1.0) + 1.0;
    denom = PI*denom*denom;
    return a2/max(denom, 1e-5);
}

// Schlick-GGX geometry term (direct lighting k).
float GeometrySchlickGGX(float nDotV, float rough)
{
    float r = rough + 1.0;
    float k = (r*r)/8.0;
    return nDotV/(nDotV*(1.0 - k) + k);
}

// Smith's method: geometry for both view and light directions.
float GeometrySmith(vec3 n, vec3 v, vec3 l, float rough)
{
    return GeometrySchlickGGX(max(dot(n, v), 0.0), rough)*GeometrySchlickGGX(max(dot(n, l), 0.0), rough);
}

vec3 FresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0)*pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

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

// The far cascade's texels cover ~5x more world than the near cascade's, so it needs a proportionally larger
// depth bias to avoid self-shadow acne; the near cascade uses the base bias.
const float SHADOW_FAR_BIAS_SCALE = 4.0;

// Samples ONE shadow cascade. Returns -1.0 when the fragment falls outside this cascade's frustum (so the caller
// can fall back to the next cascade / treat it as lit), else 0 (fully lit) .. 1 (fully shadowed). Crisp
// pixel-art shadow: a 2x2 PCF (the minimal filtering that stops a single-tap edge crawling pixel-to-pixel as the
// camera moves, while staying sharp at the low resolution) with a slope-scaled bias to combat acne.
float SampleShadowCascade(sampler2D map, mat4 vp, vec3 worldPos, float nDotL, float biasScale)
{
    vec4 clip = vp*vec4(worldPos, 1.0);
    vec3 proj = clip.xyz/clip.w;
    proj = proj*0.5 + 0.5; // NDC -> [0,1]
    if ((proj.z > 1.0) || (proj.x < 0.0) || (proj.x > 1.0) || (proj.y < 0.0) || (proj.y > 1.0))
    {
        return -1.0; // outside this cascade
    }

    float bias = max(shadowBias*biasScale*(1.0 - nDotL), shadowBias*biasScale*0.15);
    // 2x2 PCF: four taps half a texel off-centre, averaged. Reads as a hard edge after the point-upscale but
    // dithers the boundary just enough to stop sub-pixel shimmer.
    float shadow = 0.0;
    shadow += (proj.z - bias > texture(map, proj.xy + vec2(-0.5, -0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - bias > texture(map, proj.xy + vec2( 0.5, -0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - bias > texture(map, proj.xy + vec2(-0.5,  0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - bias > texture(map, proj.xy + vec2( 0.5,  0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    return shadow*0.25;
}

// Returns 0 (lit) .. 1 (shadowed) for the fragment, sampling the NEAR cascade first (sharp) and falling back to
// the FAR cascade (coarse, long-range) when the fragment lies beyond the near frustum. Outside both = lit.
float ComputeShadow(vec3 worldPos, float nDotL)
{
    if (shadowStrength <= 0.0)
    {
        return 0.0;
    }
    float shadow = SampleShadowCascade(shadowMap, lightVP, worldPos, nDotL, 1.0);
    if ((shadow < 0.0) && shadowFarActive)
    {
        shadow = SampleShadowCascade(shadowMapFar, lightVPFar, worldPos, nDotL, SHADOW_FAR_BIAS_SCALE);
    }
    return max(shadow, 0.0);
}

// ---- Atmospheric sky (distance-fog target) -----------------------------------------------------------------
// A trimmed copy of shaders/sky/fragment.frag's Rayleigh+Mie single-scattering sky, WITHOUT the sun disc and
// stars (distant terrain should fade to the sky's colour, not sprout a second sun or stars). KEEP THESE
// CONSTANTS AND skyAtmosphere() BYTE-FOR-BYTE IN SYNC WITH THE SKY SHADER, or the horizon where a fogged
// object meets the sky will not match.
const int   SKY_PRIMARY_STEPS = 16;
const int   SKY_LIGHT_STEPS   = 8;
const float SKY_R_PLANET = 6371000.0;
const float SKY_R_ATMOS  = 6471000.0;
const vec3  SKY_K_RAYLEIGH = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float SKY_K_MIE_BASE = 21e-6;
const float SKY_H_RAYLEIGH = 8000.0;
const float SKY_H_MIE      = 1200.0;
const float SKY_MIE_G      = 0.758;
const float SKY_SUN_RADIANCE = 22.0;

vec2 skyRaySphere(vec3 origin, vec3 dir, float sr)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - sr*sr;
    float d = b*b - c;
    if (d < 0.0) return vec2(1e9, -1e9);
    d = sqrt(d);
    return vec2(-b - d, -b + d);
}

vec3 skyAtmosphere(vec3 rayDir, vec3 rayOrigin, vec3 sunDir, float iSun, float kMie)
{
    vec2 atmos = skyRaySphere(rayOrigin, rayDir, SKY_R_ATMOS);
    if (atmos.y < 0.0) return vec3(0.0);
    float tStart = max(atmos.x, 0.0);
    float tEnd = atmos.y;
    vec2 planet = skyRaySphere(rayOrigin, rayDir, SKY_R_PLANET);
    if (planet.x > 0.0) tEnd = min(tEnd, planet.x);
    float segLen = (tEnd - tStart)/float(SKY_PRIMARY_STEPS);
    float t = tStart;

    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);
    float odR = 0.0;
    float odM = 0.0;

    float mu = dot(rayDir, sunDir);
    float mumu = mu*mu;
    float gg = SKY_MIE_G*SKY_MIE_G;
    float phaseR = 3.0/(16.0*PI)*(1.0 + mumu);
    float phaseM = 3.0/(8.0*PI)*((1.0 - gg)*(mumu + 1.0))/(pow(1.0 + gg - 2.0*mu*SKY_MIE_G, 1.5)*(2.0 + gg));

    for (int i = 0; i < SKY_PRIMARY_STEPS; i++)
    {
        vec3 pos = rayOrigin + rayDir*(t + segLen*0.5);
        float h = length(pos) - SKY_R_PLANET;
        float hr = exp(-h/SKY_H_RAYLEIGH)*segLen;
        float hm = exp(-h/SKY_H_MIE)*segLen;
        odR += hr;
        odM += hm;

        vec2 lightAtmos = skyRaySphere(pos, sunDir, SKY_R_ATMOS);
        float lSeg = lightAtmos.y/float(SKY_LIGHT_STEPS);
        float lt = 0.0;
        float lOdR = 0.0;
        float lOdM = 0.0;
        for (int j = 0; j < SKY_LIGHT_STEPS; j++)
        {
            vec3 lpos = pos + sunDir*(lt + lSeg*0.5);
            float lh = length(lpos) - SKY_R_PLANET;
            lOdR += exp(-lh/SKY_H_RAYLEIGH)*lSeg;
            lOdM += exp(-lh/SKY_H_MIE)*lSeg;
            lt += lSeg;
        }

        vec3 tau = SKY_K_RAYLEIGH*(odR + lOdR) + kMie*1.1*(odM + lOdM);
        vec3 attn = exp(-tau);
        sumR += hr*attn;
        sumM += hm*attn;
        t += segLen;
    }

    return iSun*(phaseR*SKY_K_RAYLEIGH*sumR + phaseM*kMie*sumM);
}

// The sky colour along a world-space view ray (the sky pass's result minus the sun disc + stars). This is the
// colour distant fogged geometry fades toward, so it matches the visible sky in that direction.
vec3 fogSkyColor(vec3 rayDir)
{
    vec3 rayOrigin = vec3(0.0, SKY_R_PLANET + 1.0, 0.0);
    vec3 sunDir = normalize(sunDirection);
    float kMie = SKY_K_MIE_BASE*max(skyTurbidity, 0.0)/3.0;
    float iSun = SKY_SUN_RADIANCE*max(skySunIntensity, 0.0);

    // Mirror the ray into the upper hemisphere and darken below the horizon, exactly like the sky pass.
    vec3 skyRay = vec3(rayDir.x, abs(rayDir.y), rayDir.z);
    vec3 sky = skyAtmosphere(skyRay, rayOrigin, sunDir, iSun, kMie);
    float belowFactor = smoothstep(0.0, -0.12, rayDir.y);
    sky *= mix(1.0, 0.5, belowFactor);
    sky *= max(skyTint, vec3(0.0));
    return sky;
}

// Unpacks a distance packed by the cube-depth shader's packDistance (RGBA8 -> [0,1]).
float unpackDistance(vec4 rgba)
{
    return dot(rgba, vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0));
}

// 20 sample directions for cube-map PCF (a small 3D disk around the light->fragment ray).
const vec3 CUBE_PCF_OFFSETS[20] = vec3[]
(
    vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1),
    vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
    vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
    vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
    vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);

// 0 (lit) .. 1 (shadowed) for the shadow-casting point light. Samples the light's cube map along the
// light->fragment direction with soft PCF, comparing the stored nearest-occluder distance to this fragment's
// distance from the light. The bias is SLOPE-SCALED (much larger where the light grazes the surface) — a
// constant bias leaves grazing-angle self-shadow acne (the streaky bands). PCF also blends the cube's face
// seams so they do not show as a hard cut through the shadow.
float ComputePointShadow(vec3 fragPos, vec3 nrm)
{
    vec3 toFrag = fragPos - pointShadowLightPos;
    float current = length(toFrag);
    vec3 toLight = (current > 1e-4) ? (-toFrag/current) : vec3(0.0, 1.0, 0.0);
    float ndotl = max(dot(normalize(nrm), toLight), 0.0);

    // Slope-scaled world-space bias: full at grazing incidence, a small floor head-on.
    float bias = max(pointShadowBias*(1.0 - ndotl), pointShadowBias*0.2);

    // PCF disk widens a little with distance (the cube texel covers more world far from the light).
    float diskRadius = 0.02 + 0.02*current;
    float shadow = 0.0;
    for (int i = 0; i < 20; i++)
    {
        float stored = unpackDistance(texture(pointShadowCube, toFrag + CUBE_PCF_OFFSETS[i]*diskRadius))*pointShadowFar;
        if ((current - bias) > stored) { shadow += 1.0; }
    }
    return shadow/20.0;
}

void main()
{
    vec4 texel = texture(texture0, fragTexCoord)*colDiffuse*fragColor;
    vec3 albedo = pow(max(texel.rgb, vec3(0.0)), vec3(2.2)); // sRGB -> linear
    float alpha = texel.a;

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

    vec3 n = normalize(fragNormal);
    if (hasNormalMap)
    {
        vec3 mapN = texture(normalMap, fragTexCoord).xyz*2.0 - 1.0;
        n = PerturbNormal(n, fragPosition, fragTexCoord, mapN);
    }
    vec3 v = normalize(viewPos - fragPosition);
    vec3 l = normalize(sunDirection);
    vec3 h = normalize(v + l);

    // Base reflectance: 0.04 for dielectrics, the albedo for metals.
    vec3 f0 = mix(vec3(0.04), albedo, metal);

    // Cook-Torrance specular for the single directional sun.
    float ndf = DistributionGGX(n, h, rough);
    float g = GeometrySmith(n, v, l, rough);
    vec3 f = FresnelSchlick(max(dot(h, v), 0.0), f0);

    vec3 numerator = ndf*g*f;
    float denom = 4.0*max(dot(n, v), 0.0)*max(dot(n, l), 0.0) + 1e-4;
    vec3 specular = numerator/denom;

    // Diffuse energy is what is not reflected specularly; metals have no diffuse.
    vec3 kD = (vec3(1.0) - f)*(1.0 - metal);

    float nDotL = max(dot(n, l), 0.0);
    vec3 radiance = sunColor*sunIntensity;
    // Sun shadow: darken the direct (sun) term only; ambient still lights shadowed surfaces.
    float shadow = ComputeShadow(fragPosition, nDotL);
    float sunVisibility = 1.0 - shadow*clamp(shadowStrength, 0.0, 1.0);
    vec3 direct = (kD*albedo/PI + specular)*radiance*nDotL*sunVisibility;

    // Point lights: same Cook-Torrance BRDF per light, with a windowed inverse-square falloff. No per-light
    // shadows (deferred). The count + arrays are the reach-culled set the renderer chose for this object.
    vec3 pointTotal = vec3(0.0);
    for (int i = 0; i < pointLightCount; i++)
    {
        vec3 toLight = pointLightPositions[i] - fragPosition;
        float dist = length(toLight);
        vec3 lp = toLight/max(dist, 1e-4);
        vec3 hp = normalize(v + lp);

        float ndfp = DistributionGGX(n, hp, rough);
        float gp = GeometrySmith(n, v, lp, rough);
        vec3 fp = FresnelSchlick(max(dot(hp, v), 0.0), f0);
        vec3 specp = (ndfp*gp*fp)/(4.0*max(dot(n, v), 0.0)*max(dot(n, lp), 0.0) + 1e-4);
        vec3 kDp = (vec3(1.0) - fp)*(1.0 - metal);
        float ndlp = max(dot(n, lp), 0.0);

        // Physical 1/d^2 falloff, smoothly windowed to exactly 0 at the light's range so the shaded set
        // agrees with what the CPU reach-culler decided affects this object (no hard popping at the edge).
        float range = max(pointLightRanges[i], 1e-3);
        float window = clamp(1.0 - pow(dist/range, 4.0), 0.0, 1.0);
        float atten = (window*window)/(dist*dist + 1.0);

        pointTotal += (kDp*albedo/PI + specp)*pointLightRadiances[i]*ndlp*atten;
    }

    // The shadow-casting point light (same Cook-Torrance BRDF), darkened by its own cube shadow map. Shaded
    // here rather than in the culled loop so it can sample the cube; the renderer excludes it from that loop.
    if (pointShadowActive)
    {
        vec3 toLight = pointShadowLightPos - fragPosition;
        float dist = length(toLight);
        vec3 lp = toLight/max(dist, 1e-4);
        vec3 hp = normalize(v + lp);

        float ndfp = DistributionGGX(n, hp, rough);
        float gp = GeometrySmith(n, v, lp, rough);
        vec3 fp = FresnelSchlick(max(dot(hp, v), 0.0), f0);
        vec3 specp = (ndfp*gp*fp)/(4.0*max(dot(n, v), 0.0)*max(dot(n, lp), 0.0) + 1e-4);
        vec3 kDp = (vec3(1.0) - fp)*(1.0 - metal);
        float ndlp = max(dot(n, lp), 0.0);

        float range = max(pointShadowLightRange, 1e-3);
        float window = clamp(1.0 - pow(dist/range, 4.0), 0.0, 1.0);
        float atten = (window*window)/(dist*dist + 1.0);

        float shadow = ComputePointShadow(fragPosition, n);
        float visibility = 1.0 - shadow*clamp(shadowStrength, 0.0, 1.0);
        pointTotal += (kDp*albedo/PI + specp)*pointShadowLightRadiance*ndlp*atten*visibility;
    }

    // Flat ambient skylight stand-in (proper image-based lighting lands with the atmospheric sky step).
    // Screen-space AO darkens ONLY this ambient term (never the direct light below).
    float screenAo = aoMapActive ? texture(ambientOcclusionMap, gl_FragCoord.xy/aoResolution).r : 1.0;
    vec3 ambient = ambientColor*ambientIntensity*albedo*occlusion*screenAo;

    vec3 emissive = emissiveColor*emissiveIntensity;
    if (hasEmissiveMap)
    {
        vec3 em = texture(emissiveMap, fragTexCoord).rgb;
        emissive *= pow(max(em, vec3(0.0)), vec3(2.2)); // sRGB -> linear
    }

    vec3 color = ambient + direct + pointTotal + emissive;

    // Distance fog: fade toward the atmospheric sky colour along this fragment's own view ray with an
    // exponential falloff, so distant geometry dissolves into the exact sky behind it. The sky is only
    // evaluated when the fog actually contributes (near fragments skip the raymarch). fogDensity 0 = no fog.
    if (fogDensity > 0.0)
    {
        float fragDistance = length(fragPosition - viewPos);
        float fogFactor = clamp(1.0 - exp(-fragDistance*fogDensity), 0.0, 1.0);
        if (fogFactor > 0.001)
        {
            vec3 viewRay = normalize(fragPosition - viewPos);
            color = mix(color, fogSkyColor(viewRay), fogFactor);
        }
    }

    // Output LINEAR HDR. Tonemapping + gamma happen later in the tonemap post-pass (after the pixelation
    // upscale), so the scene is composited in high dynamic range and mapped to [0,1] only for display.
    finalColor = vec4(color, alpha);
}
