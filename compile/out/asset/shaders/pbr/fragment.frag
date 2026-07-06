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

// Sun shadow map (directional). shadowStrength 0 disables it (night, disabled, or shader unavailable).
uniform mat4 lightVP;          // world -> sun light-clip space
uniform sampler2D shadowMap;   // directional depth map rendered from the sun
uniform float shadowStrength;  // 0..1, how much the sun contribution is darkened in shadow
uniform float shadowBias;      // base depth bias to combat shadow acne
uniform float shadowTexelSize; // 1.0 / shadow-map resolution, for PCF tap offsets

// Surface material parameters. Scalar for now; per-material PBR texture maps land in a later step.
uniform float metallic;
uniform float roughness;
uniform float ao;
uniform vec3 emissiveColor;    // linear
uniform float emissiveIntensity;

// Point lights, culled + uploaded per object by the renderer (nearest/strongest that reach this object).
// MAX_POINT_LIGHTS MUST match WORLD_MAX_FORWARD_LIGHTS in WorldLightCulling.h.
#define MAX_POINT_LIGHTS 8
uniform int pointLightCount;                          // active entries in the arrays below (0..MAX)
uniform vec3 pointLightPositions[MAX_POINT_LIGHTS];   // world space
uniform vec3 pointLightRadiances[MAX_POINT_LIGHTS];   // linear colour * intensity
uniform float pointLightRanges[MAX_POINT_LIGHTS];     // reach radius (attenuation falls to 0 here)

// Distance fog. fogColor is LINEAR and already day-night scaled by the renderer; fogDensity 0 disables fog.
uniform vec3 fogColor;
uniform float fogDensity;

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

// Returns 0 (fully lit) .. 1 (fully shadowed) for the fragment. Crisp, hard-edged pixel-art shadow: a 2x2 PCF
// (the minimal filtering that stops a single-tap edge from crawling pixel-to-pixel as the camera moves, while
// staying sharp at the low pixel-art resolution) with a slope-scaled bias to combat acne. Fragments outside
// the shadow frustum are treated as lit (a hard cutoff, no soft edge fade).
float ComputeShadow(vec3 worldPos, float nDotL)
{
    if (shadowStrength <= 0.0)
    {
        return 0.0;
    }
    vec4 clip = lightVP*vec4(worldPos, 1.0);
    vec3 proj = clip.xyz/clip.w;
    proj = proj*0.5 + 0.5; // NDC -> [0,1]
    if ((proj.z > 1.0) || (proj.x < 0.0) || (proj.x > 1.0) || (proj.y < 0.0) || (proj.y > 1.0))
    {
        return 0.0;
    }

    float bias = max(shadowBias*(1.0 - nDotL), shadowBias*0.15);
    // 2x2 PCF: four taps half a texel off-centre, averaged. Reads as a hard edge after the point-upscale but
    // dithers the boundary just enough to stop sub-pixel shimmer.
    float shadow = 0.0;
    shadow += (proj.z - bias > texture(shadowMap, proj.xy + vec2(-0.5, -0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - bias > texture(shadowMap, proj.xy + vec2( 0.5, -0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - bias > texture(shadowMap, proj.xy + vec2(-0.5,  0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - bias > texture(shadowMap, proj.xy + vec2( 0.5,  0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    return shadow*0.25;
}

void main()
{
    vec4 texel = texture(texture0, fragTexCoord)*colDiffuse*fragColor;
    vec3 albedo = pow(max(texel.rgb, vec3(0.0)), vec3(2.2)); // sRGB -> linear
    float alpha = texel.a;

    float rough = clamp(roughness, 0.045, 1.0);
    float metal = clamp(metallic, 0.0, 1.0);

    vec3 n = normalize(fragNormal);
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

    // Flat ambient skylight stand-in (proper image-based lighting lands with the atmospheric sky step).
    vec3 ambient = ambientColor*ambientIntensity*albedo*ao;

    vec3 color = ambient + direct + pointTotal + emissiveColor*emissiveIntensity;

    // Distance fog: fade toward the (day-night-scaled, linear) fog colour with an exponential falloff, so
    // distant geometry dissolves into the atmosphere. fogDensity 0 leaves the colour untouched.
    float fragDistance = length(fragPosition - viewPos);
    float fogFactor = clamp(1.0 - exp(-fragDistance*fogDensity), 0.0, 1.0);
    color = mix(color, fogColor, fogFactor);

    // Output LINEAR HDR. Tonemapping + gamma happen later in the tonemap post-pass (after the pixelation
    // upscale), so the scene is composited in high dynamic range and mapped to [0,1] only for display.
    finalColor = vec4(color, alpha);
}
