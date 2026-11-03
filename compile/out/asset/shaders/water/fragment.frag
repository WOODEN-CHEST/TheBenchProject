#version 330

// The water (world_water over pbr/vertex, world_water_instanced over pbr/instanced): a world model's water slot drawn with
// its water material over the opaque world, in the scene pass before the reflection pass. Its waves are two normal maps
// moving along its flow (common/water: anchored to the world, or laid on a river's texture coordinates) and the rain's
// rings; it mirrors what lies around it by water's Fresnel reflectance (handed to the reflection pass while it runs, the
// sky's otherwise), glints in the sun, the moon and the lamps, and over the view's path through it shows the bottom (the
// copy of the scene's colour behind the pixel: no refraction) dimmed by its absorption while its own colour, lit as a
// surface is, scatters in; deep water, or nothing under it, shows that colour alone. Foam lies where it is shallow.

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

layout(location = 0) out vec4 finalColor;

// The frame's lights, shadows, ambient, haze and night vision, as every lit shader takes them.
#include "common/lighting"
// What the surface hands the reflection pass while it runs.
#include "common/reflection_output"
// The draw's fade as a dither on the scene's pixel grid.
#include "common/dither"
// The copies' depth as the camera's passes store it.
#include "common/depth"
// The bottom read back from the copy of the camera's depth is where the picture's curve drew it.
#include "common/curvature"
#include "common/water"

// The water material, set per material (WorldRendererWater.c). Its maps: whether it has each, the samplers, each map's
// frame (xy the offset, zw the turn's cosine and sine) and one over each tile (A, B, the foam); whether they lie on the
// texture coordinates (a river) rather than the world's x and z.
uniform bool waterHasNormalA;
uniform bool waterHasNormalB;
uniform bool waterHasFoam;
uniform sampler2D waterNormalA;
uniform sampler2D waterNormalB;
uniform sampler2D waterFoam;
uniform vec4 waterMapA;
uniform vec4 waterMapB;
uniform vec4 waterMapFoam;
uniform vec3 waterInverseTiles;
uniform bool waterIsAlongUv;
// The waves' strength (the wind in), the colour it scatters (linear), its absorption per metre, its roughness, its
// reflectance facing it, the foam's depth and strength, whether it shows the rain's rings and whether it hands its
// reflection to the reflection pass.
uniform float waterWaveStrength;
uniform vec3 waterColor;
uniform vec3 waterAbsorption;
uniform float waterRoughness;
uniform float waterReflectance;
uniform vec2 waterFoamShape;
uniform bool waterHasRipples;
uniform bool waterHasReflections;

// The pass: whether the copies of the scene's colour and depth hold what lies under the water this frame, the copies,
// and the scene's inverse view-projection (clip -> render space: the bottom's point from its depth).
uniform bool waterHasBottom;
uniform sampler2D waterSceneColor;
uniform sampler2D waterSceneDepth;
uniform mat4 waterInvViewProjection;

// Foam: a white, rough surface over the water.
const float WATER_FOAM_ALBEDO = 0.8;
const float WATER_FOAM_ROUGHNESS = 0.6;
// The least the waves' normal faces the eye (a wave's back seen at a grazing angle is turned toward it).
const float WATER_MIN_FACING = 0.02;

void main()
{
    vec3 surfaceNormal = normalize(fragNormal);
    // Where the maps are laid: the world's x and z (render space; the frames carry the origin), or a river's texture
    // coordinates as authored (V growing along the flow: raylib's v runs the other way).
    vec2 place = waterIsAlongUv ? vec2(fragTexCoord.x, -fragTexCoord.y) : fragPosition.xz;
    vec3 axisU = vec3(1.0, 0.0, 0.0);
    vec3 axisV = vec3(0.0, 0.0, 1.0);
    // The surface's directions of the texture coordinates' growth (a cotangent frame from the screen's derivatives),
    // taken in uniform control flow.
    vec3 dp1 = dFdx(fragPosition);
    vec3 dp2 = dFdy(fragPosition);
    vec2 duv1 = dFdx(fragTexCoord);
    vec2 duv2 = dFdy(fragTexCoord);
    if (waterIsAlongUv)
    {
        vec3 dp2perp = cross(dp2, surfaceNormal);
        vec3 dp1perp = cross(surfaceNormal, dp1);
        vec3 growU = dp2perp*duv1.x + dp1perp*duv2.x;
        vec3 growV = dp2perp*duv1.y + dp1perp*duv2.y;
        if ((dot(growU, growU) > 1e-20) && (dot(growV, growV) > 1e-20))
        {
            axisU = normalize(growU);
            axisV = -normalize(growV);
        }
    }

    // The waves: the two maps' tilts summed, times the strength; the rain's rings where the rain reaches.
    vec3 tilt = vec3(0.0);
    if (waterHasNormalA)
    {
        tilt += WaterMapTilt(texture(waterNormalA, WaterMapCoordinate(place, waterMapA, waterInverseTiles.x)).xyz,
            waterMapA, axisU, axisV);
    }
    if (waterHasNormalB)
    {
        tilt += WaterMapTilt(texture(waterNormalB, WaterMapCoordinate(place, waterMapB, waterInverseTiles.y)).xyz,
            waterMapB, axisU, axisV);
    }
    tilt *= waterWaveStrength;
    if (waterHasRipples && rainActive)
    {
        vec2 ripple = RainRippleSlope(fragPosition)*RainOpenness(fragPosition, surfaceNormal)
            *RainTopOpenness(fragPosition.y);
        tilt += vec3(-ripple.x, 0.0, -ripple.y);
    }
    tilt -= dot(tilt, surfaceNormal)*surfaceNormal;
    vec3 n = normalize(surfaceNormal + tilt);
    vec3 v = normalize(-fragPosition); // render space: the camera at the origin
    float facing = dot(n, v);
    if (facing < WATER_MIN_FACING)
    {
        n = normalize(n + v*(WATER_MIN_FACING - facing));
    }

    // Under the surface: the bottom's point from the copy of the scene's depth, the view's path through the water to it
    // and how deep the water is above it (both in the flat world: the depth holds the bottom where the planet's curve
    // drew it); nothing under the water (the sky), or no copies, is deep water.
    bool hasBottom = false;
    float path = 0.0;
    float depthBelow = 0.0;
    vec3 bottomColor = vec3(0.0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (waterHasBottom)
    {
        float depth = texelFetch(waterSceneDepth, pixel, 0).r;
        if (!depthIsSky(depth))
        {
            vec2 ndc = gl_FragCoord.xy/vec2(textureSize(waterSceneDepth, 0))*2.0 - 1.0;
            vec4 point = waterInvViewProjection*vec4(ndc, depthToNdc(depth), 1.0);
            vec3 bottom = CurvatureFlatten(point.xyz/point.w);
            path = max(length(bottom) - length(fragPosition), 0.0);
            depthBelow = max(dot(fragPosition - bottom, surfaceNormal), 0.0);
            bottomColor = texelFetch(waterSceneColor, pixel, 0).rgb;
            hasBottom = true;
        }
    }
    vec3 transmit = hasBottom ? WaterTransmittance(waterAbsorption, path) : vec3(0.0);
    float foam = 0.0;
    if (waterHasFoam)
    {
        float texel = texture(waterFoam, WaterMapCoordinate(place, waterMapFoam, waterInverseTiles.z)).r;
        foam = hasBottom ? WaterFoam(texel, depthBelow, waterFoamShape.x, waterFoamShape.y) : 0.0;
    }

    // The surface: water's reflectance and roughness, the colour scattered over the part of the path the bottom does not
    // show through, and foam lying over it as a white, rough surface.
    float rough = max(mix(waterRoughness, WATER_FOAM_ROUGHNESS, foam), WATER_MIN_ROUGHNESS);
    float f0 = waterReflectance*(1.0 - foam);
    vec3 albedo = mix(waterColor*(vec3(1.0) - transmit), vec3(WATER_FOAM_ALBEDO), foam);
    float mirror = WaterFresnel(dot(n, v), waterReflectance)*(1.0 - foam);
    vec3 through = (1.0 - mirror)*(1.0 - foam)*transmit;

    // The sun and the moon through the air with the levels' shadow (their glints and the light they scatter in), the
    // point lights and a screen's light, and the sky's own light, what mirrors taken from it.
    SkyLightValues light = SkyLightAt(fragPosition);
    float shadowAmount = FaceShadowAmount(fragPosition, FaceNormal(fragPosition, surfaceNormal));
    vec3 direct = SkyLight(light, n, v, shadowAmount, albedo, vec3(f0), rough, 0.0);
    vec3 pointTotal = PointLights(n, v, fragPosition, fragPosition, surfaceNormal, albedo, vec3(f0), rough, 0.0)
        + ScreenLight(n, v, fragPosition, albedo, vec3(f0), rough, 0.0);
    float skyVisibility = SkyVisibilityAt(fragPosition, surfaceNormal);
    vec3 ambient = AmbientLight(light, surfaceNormal, fragPosition, surfaceNormal, skyVisibility)*albedo*ScreenAo()
        *(1.0 - mirror);

    // What it mirrors: the reflection pass's while it runs and the material lets it, the sky's (and the ground's
    // bounce) otherwise.
    SurfaceReflection reflection;
    reflection.direction = RainWaterDirection(n, v);
    reflection.weight = vec3(mirror);
    reflection.sky = EnvironmentRadiance(light, reflection.direction, fragPosition, surfaceNormal, skyVisibility);
    reflection.screenShare = ReflectionScreenShare(rough);
    vec3 reflected = direct + pointTotal + ambient;
    if (reflectionsDeferred && waterHasReflections)
    {
        WriteReflection(reflection, fragPosition);
    }
    else
    {
        reflected += reflection.weight*reflection.sky;
        WriteNoReflection();
    }
    vec3 own = NightVisionOf(reflected);

    // The haze in front of it. The bottom in the copy is hazed already (from the bottom to the eye, all but the same path
    // as the surface's), so it lies under the water's own light as under a blended surface: the water's light seen
    // through the haze, the haze's own light by the share the water covers.
    vec3 inScatter = vec3(0.0);
    vec3 airTransmittance = vec3(1.0);
    float toSurface = length(fragPosition);
    if (toSurface > 0.0)
    {
        hazeInFront(HazeScreenUv(fragPosition), fragPosition/toSurface, InteriorHazeDistance(fragPosition, toSurface),
            hazeMultiplier, normalize(sunDirection), normalize(moonDirection), inScatter, airTransmittance);
    }
    vec3 color = bottomColor*through + own*airTransmittance + NightVisionOf(inScatter)*(vec3(1.0) - through);

    // The debug views of interior lighting, the shadow levels and the rain while they are on (water counts as pooled).
    color = InteriorViewColor(color, fragPosition, surfaceNormal, skyVisibility);
    color = ShadowLevelViewColor(color, fragPosition);
    color = RainViewColor(color, fragPosition, surfaceNormal, 1.0);

    // The fade is tested last, after every derivative and mipmapped fetch above.
    if (DitherDiscard())
    {
        discard;
    }
    finalColor = vec4(color, 1.0);
}
