#version 330

// The clouds' march (common/clouds): per texel of the cloud target, the clouds along that pixel's ray, their three layers
// front to back, each seen through the air in front of it (the haze volume's in-scatter and transmittance at its apparent
// distance). Out: RGB the light the clouds add to what lies behind them (the clouds' own, hazed, plus the air in front of
// them that now hides the sky behind), A the share of the sky behind them that still shows (their transmittance). The sky
// pass composites the sky as sky x A + RGB, before the ground haze in front of it all and the eye's night vision. A texel
// whose scene pixels are all covered by geometry (the G-buffer's flags) is not marched: A is -1 there.
// Drawn after the atmosphere's tables and the haze volume (source/WorldRendererClouds.c).

in vec2 fragTexCoord;
in vec4 fragColor;

uniform vec2 resolution;           // the cloud target's size in texels
uniform mat4 invViewProj;          // clip -> render space (a rotation and the projection): the rays
uniform sampler2D skyMask;         // the G-buffer's normals and flags at the scene's size: alpha 0 where nothing is drawn
uniform bool hasSkyMask;           // false: every texel is marched
uniform vec2 skyMaskResolution;    // the G-buffer's size in pixels

uniform vec3 sunDirection;         // unit directions to the sun and the moon (the haze's ground light; the shadow light)
uniform vec3 moonDirection;
uniform vec3 cloudLightDirection;  // the light that lights the clouds: the sun, or the moon while the sun is far below
uniform vec3 cloudLightRadiance;   // its light above the air (the surfaces' units), colour in
uniform vec3 cloudSunRadiance;     // the sun's and the moon's light above the air, for the sky's irradiance on the clouds
uniform vec3 cloudMoonRadiance;
uniform vec3 cloudGroundIrradiance; // the light the ground below reflects up (the sky's light by height's ground bounce)
uniform vec3 cloudTownIrradiance;  // the town's light on the clouds' undersides above the camera (0 by day)
uniform vec3 cloudTint;            // the clouds' colour (linear)
uniform float cloudForward;        // the forward lobe's asymmetry
uniform vec3 cloudFlashRadiance;   // a lightning flash's light in the clouds near its strike (0: none)
uniform vec3 cloudFlashPosition;   // the strike's place in the clouds, render space
uniform float cloudMaxDistance;    // the farthest a march reaches along its ray, metres
uniform int cloudLowSteps;         // the low layer's most view steps, its light steps, the mid layer's view steps
uniform int cloudLightSteps;
uniform int cloudMidSteps;
uniform float cloudFrame;          // the frame's index (for the steps' jitter), wrapped

out vec4 finalColor;

#include "common/clouds"
#include "common/haze"

// The light scattered many times (the diffuse light inside a cloud): a diffusing medium sends about this many times
// 1 / (4 pi) of the light it takes in back out any way (a thick cloud's sunlit side reflects about three quarters of
// the light falling on it), dimmed by the two-stream transmission of the depth the light crossed to reach the point,
// 1 / (1 + CLOUD_DIFFUSION depth): 0.75 (1 - g) for droplets of g 0.85, as the weather's deck (WorldWeatherLight.h), so a
// cloud's base sends down what the deck's two-stream transmission does.
const float CLOUD_DIFFUSE_GAIN = 3.0;
const float CLOUD_DIFFUSION = 0.1125;
// The light scattered a few times, still forward: a softer forward glow, its extinction and anisotropy a share of the
// single scattering's and its weight.
const float CLOUD_GLOW_EXTINCTION = 0.25;
const float CLOUD_GLOW_ANISOTROPY = 0.5;
const float CLOUD_GLOW_WEIGHT = 0.5;
// The first light step's length, as a share of the layer's thickness; each next one twice the last.
const float CLOUD_LIGHT_FIRST_STEP = 0.025;
// A march stops once less than this share of the sky behind shows.
const float CLOUD_OPAQUE = 0.01;
// The shortest view step, metres (rays straight up through a thin layer).
const float CLOUD_MIN_STEP = 30.0;
// The low layer's march: its coarse steps cover the stretch in this share of its samples, its fine steps (inside clouds)
// are this share of a coarse one, and this many fine steps finding nothing past the coarse step that found the cloud go
// back to coarse ones.
const float CLOUD_COARSE_SHARE = 0.5;
const float CLOUD_FINE_SHARE = 0.25;
const int CLOUD_EMPTY_RUN = 4;
// Inside a cloud a step crosses about this much optical depth, and is at least this long (metres).
const float CLOUD_STEP_DEPTH = 0.7;
const float CLOUD_SHORTEST_STEP = 8.0;
// Where the sky's light's depth through the cloud above a point is sampled, as a share of the way to the layer's top.
const float CLOUD_AMBIENT_PROBE = 0.33;
// A flash lights the clouds within about this many metres of its strike.
const float CLOUD_FLASH_REACH = 1800.0;

// The two-stream transmission of a cloud's diffuse light through an optical depth.
float cloudDiffuseTransmission(float depth)
{
    return 1.0/(1.0 + CLOUD_DIFFUSION*max(depth, 0.0));
}

// The light one point of a layer sends toward the camera per unit of its scattering, before its own extinction: the
// cloud light through the air at the point, through the cloud toward it (`lightDepth`, its optical depth) scattered once
// (the phase's forward lobe: silver linings), a few times (a softer glow) and many times (the diffuse light); the sky's
// light from above and the ground's and the town's from below, diffused through the cloud on their side (`upDepth`,
// `downDepth`); a flash near its strike.
vec3 cloudPointLight(vec3 p, float height, float cosAngle, float lightDepth, float upDepth, float downDepth,
    vec3 skyIrradiance)
{
    vec3 up = cloudUpAt(p);
    vec3 light = cloudLightRadiance*atmosphereLightTransmittance(height, dot(up, cloudLightDirection));
    float scattered = cloudPhase(cosAngle, cloudForward)*exp(-lightDepth)
        + CLOUD_GLOW_WEIGHT*cloudPhase(cosAngle, CLOUD_GLOW_ANISOTROPY*cloudForward)
            *exp(-CLOUD_GLOW_EXTINCTION*lightDepth)
        + CLOUD_DIFFUSE_GAIN/(4.0*PI)*cloudDiffuseTransmission(lightDepth);
    vec3 around = skyIrradiance*cloudDiffuseTransmission(upDepth)
        + (cloudGroundIrradiance + cloudTownIrradiance)*cloudDiffuseTransmission(downDepth);
    vec3 total = light*scattered + around*(CLOUD_DIFFUSE_GAIN/(4.0*PI));
    if (any(greaterThan(cloudFlashRadiance, vec3(0.0))))
    {
        total += cloudFlashRadiance*exp(-distance(p, cloudFlashPosition)/CLOUD_FLASH_REACH);
    }
    return total*cloudTint;
}

// The sky's irradiance on a surface facing up at a height, from the sun's and the moon's light above the air at the
// lights' zenith cosines there.
vec3 cloudSkyIrradianceAt(float height, vec3 up)
{
    return atmosphereSkyIrradiance(height, dot(up, sunDirection))*cloudSunRadiance
        + atmosphereSkyIrradiance(height, dot(up, moonDirection))*cloudMoonRadiance;
}

// One layer's march result: the light it sends toward the camera (not yet hazed), its transmittance, and where its light
// comes from (the weighted sum of distances and the weights).
struct CloudMarch
{
    vec3 light;
    float transmittance;
    float distanceSum;
    float weightSum;
};

// Adds one step's light and extinction: `source` the light per unit of scattering at the step, `stepDepth` its optical
// depth, `along` its distance from the camera.
void cloudAccumulate(inout CloudMarch march, vec3 source, float stepDepth, float along)
{
    float stepTransmittance = exp(-stepDepth);
    float weight = march.transmittance*(1.0 - stepTransmittance);
    march.light += source*weight;
    march.distanceSum += along*weight;
    march.weightSum += weight;
    march.transmittance *= stepTransmittance;
}

// The low layer's optical depth from a point toward the light: steps doubling in length from a share of its thickness,
// the first with its erosion.
float cloudLowLightDepth(vec3 p, float coverage, int steps, float jitter)
{
    float thickness = cloudLowLayer.y - cloudLowLayer.x;
    float stepLength = CLOUD_LIGHT_FIRST_STEP*thickness;
    float along = 0.0;
    float depth = 0.0;
    for (int i = 0; i < steps; i++)
    {
        vec3 q = p + cloudLightDirection*(along + stepLength*jitter);
        float h = cloudHeightAt(q);
        if (h > cloudLowLayer.y)
        {
            break;
        }
        depth += cloudLowDensityCovered(q, h, coverage, i == 0)*stepLength;
        along += stepLength;
        stepLength *= 2.0;
    }
    return depth*cloudLowExtinction;
}

// The low layer's depth straight up from a point to its top (one sample, a third of the way up), and down to its base
// (from the point's own density): what dims the sky's light from above and the ground's from below.
void cloudLowAmbientDepths(vec3 p, float height, float coverage, float density, out float upDepth,
    out float downDepth)
{
    float toTop = cloudLowLayer.y - height;
    float above = cloudLowDensityCovered(p + cloudUpAt(p)*(CLOUD_AMBIENT_PROBE*toTop), height + CLOUD_AMBIENT_PROBE*toTop,
        coverage, false);
    upDepth = above*toTop*cloudLowExtinction;
    downDepth = density*(height - cloudLowLayer.x)*cloudLowExtinction;
}

// The mid and high layers' depth where the light's ray from a point of the low layer crosses them: their shadow on it.
float cloudUpperShadowDepth(vec3 p)
{
    float depth = 0.0;
    float lightUp = max(cloudLightDirection.y, 0.02);
    if (cloudMidExtinction > 0.0)
    {
        float middle = 0.5*(cloudMidLayer.x + cloudMidLayer.y);
        float h = cloudHeightAt(p);
        if (h < middle)
        {
            vec3 q = p + cloudLightDirection*((middle - h)/lightUp);
            depth += cloudMidDensity(q, middle)*(cloudMidLayer.y - cloudMidLayer.x)*0.5*cloudMidExtinction/lightUp;
        }
    }
    if (cloudHighLayer.w > 0.0)
    {
        float h = cloudHeightAt(p);
        if (h < cloudHighLayer.x)
        {
            vec3 q = p + cloudLightDirection*((cloudHighLayer.x - h)/lightUp);
            depth += cloudHighDensity(q)*cloudHighLayer.w*min(1.0/lightUp, CLOUD_HIGH_MAX_SLANT);
        }
    }
    return depth;
}

// Marches the low layer along the ray between two distances: coarse steps through clear air (each tested without the
// erosion, which is cheaper), and once one lands in a cloud a step back and fine steps through it (lit, eroded), on at
// least to where the coarse step landed (a cloud shorter than a coarse step is found again), back to coarse steps after
// a few fine ones past it find nothing. At most cloudLowSteps samples.
CloudMarch cloudMarchLow(vec3 direction, float start, float end, float jitter, float cosAngle)
{
    CloudMarch march = CloudMarch(vec3(0.0), 1.0, 0.0, 0.0);
    float stop = min(end, start + cloudMaxDistance);
    float length = stop - start;
    float coarse = max(length/(CLOUD_COARSE_SHARE*float(cloudLowSteps)), 2.0*CLOUD_MIN_STEP);
    float fine = max(coarse*CLOUD_FINE_SHARE, CLOUD_MIN_STEP);
    // The sky's irradiance at the stretch's two ends (it changes slowly along it).
    vec3 entry = direction*start;
    vec3 exit = direction*stop;
    float entryHeight = cloudHeightAt(entry);
    float exitHeight = cloudHeightAt(exit);
    vec3 entrySky = cloudSkyIrradianceAt(entryHeight, cloudUpAt(entry));
    vec3 exitSky = cloudSkyIrradianceAt(exitHeight, cloudUpAt(exit));
    float along = start + jitter*coarse;
    bool isFine = false;
    int emptyRun = 0;
    float found = start; // where the last coarse step found a cloud
    for (int i = 0; (i < cloudLowSteps) && (along < stop); i++)
    {
        vec3 p = direction*along;
        float height = cloudHeightAt(p);
        if (!isFine)
        {
            if (cloudLowDensity(p, height, false) > 0.0)
            {
                // A cloud: back one coarse step, then fine steps from there.
                isFine = true;
                emptyRun = 0;
                found = along;
                along = max(along - coarse, start + jitter*fine);
            }
            else
            {
                along += coarse;
            }
            continue;
        }
        float coverage = cloudLowCoverageAt(p);
        float density = cloudLowDensityCovered(p, height, coverage, true);
        if (density <= 0.0)
        {
            // Only the empty steps past the coarse step's find count: the cloud it found lies ahead until then.
            emptyRun = (along > found) ? (emptyRun + 1) : 0;
            isFine = emptyRun < CLOUD_EMPTY_RUN;
            along += isFine ? fine : coarse;
            continue;
        }
        emptyRun = 0;
        // A step through dense cloud is shortened to about CLOUD_STEP_DEPTH of optical depth (at least
        // CLOUD_SHORTEST_STEP): the light near a cloud's surface, where most of it comes from, is then sampled finely.
        float stepLength = clamp(CLOUD_STEP_DEPTH/(density*cloudLowExtinction), CLOUD_SHORTEST_STEP, fine);
        float stepDepth = density*cloudLowExtinction*stepLength;
        float lightDepth = cloudLowLightDepth(p, coverage, cloudLightSteps, jitter) + cloudUpperShadowDepth(p);
        float upDepth;
        float downDepth;
        cloudLowAmbientDepths(p, height, coverage, density, upDepth, downDepth);
        // Between the two ends by height (rays down through the layer too).
        float rise = exitHeight - entryHeight;
        vec3 sky = mix(entrySky, exitSky, (abs(rise) > 1.0) ? clamp((height - entryHeight)/rise, 0.0, 1.0) : 0.5);
        vec3 source = cloudPointLight(p, height, cosAngle, lightDepth, upDepth, downDepth, sky);
        cloudAccumulate(march, source, stepDepth, along);
        if (march.transmittance < CLOUD_OPAQUE)
        {
            break;
        }
        along += stepLength;
    }
    return march;
}

// Marches the mid layer the same way, with fewer, coarser steps (its light from its own depth toward the light).
CloudMarch cloudMarchMid(vec3 direction, float start, float end, float jitter, float cosAngle)
{
    CloudMarch march = CloudMarch(vec3(0.0), 1.0, 0.0, 0.0);
    float length = min(end, start + cloudMaxDistance) - start;
    int steps = clamp(int(ceil(length/CLOUD_MIN_STEP)), 4, cloudMidSteps);
    float stepLength = length/float(steps);
    float thickness = cloudMidLayer.y - cloudMidLayer.x;
    vec3 middle = direction*(start + 0.5*length);
    vec3 sky = cloudSkyIrradianceAt(cloudHeightAt(middle), cloudUpAt(middle));
    float lightUp = max(cloudLightDirection.y, 0.05);
    for (int i = 0; i < steps; i++)
    {
        float along = start + (float(i) + jitter)*stepLength;
        vec3 p = direction*along;
        float height = cloudHeightAt(p);
        float density = cloudMidDensity(p, height);
        if (density <= 0.0)
        {
            continue;
        }
        // Toward the light: two samples through the rest of the layer.
        float toTop = (cloudMidLayer.y - height)/lightUp;
        float lightDepth = (cloudMidDensity(p + cloudLightDirection*(0.3*toTop), height + 0.3*toTop*lightUp)
            + cloudMidDensity(p + cloudLightDirection*(0.8*toTop), height + 0.8*toTop*lightUp))*0.5*toTop
            *cloudMidExtinction;
        float fraction = clamp((height - cloudMidLayer.x)/thickness, 0.0, 1.0);
        float upDepth = density*(1.0 - fraction)*thickness*cloudMidExtinction;
        float downDepth = density*fraction*thickness*cloudMidExtinction;
        vec3 source = cloudPointLight(p, height, cosAngle, lightDepth, upDepth, downDepth, sky);
        cloudAccumulate(march, source, density*cloudMidExtinction*stepLength, along);
        if (march.transmittance < CLOUD_OPAQUE)
        {
            break;
        }
    }
    return march;
}

// The high layer where the ray crosses its height: thin, lit once (its light passes it nearly undimmed).
CloudMarch cloudHigh(vec3 direction, float distanceToLayer, float cosAngle)
{
    CloudMarch march = CloudMarch(vec3(0.0), 1.0, 0.0, 0.0);
    vec3 p = direction*distanceToLayer;
    float density = cloudHighDensity(p);
    if (density <= 0.0)
    {
        return march;
    }
    vec3 up = cloudUpAt(p);
    float slant = min(1.0/max(abs(dot(direction, up)), 1e-3), CLOUD_HIGH_MAX_SLANT);
    float depth = density*cloudHighLayer.w*slant;
    float height = cloudHighLayer.x;
    vec3 sky = cloudSkyIrradianceAt(height, up);
    vec3 source = cloudPointLight(p, height, cosAngle, 0.5*density*cloudHighLayer.w, 0.0, 0.0, sky);
    cloudAccumulate(march, source, depth, distanceToLayer);
    return march;
}

// The steps' jitter for a pixel this frame: interleaved gradient noise (Jimenez 2014) over the screen, advanced every
// frame by the golden ratio (the R1 sequence), so each pixel's jitter covers 0 .. 1 evenly over any few frames and their
// average (the accumulation's) keeps no pattern.
float cloudJitter(vec2 pixel)
{
    float noise = fract(52.9829189*fract(0.06711056*pixel.x + 0.00583715*pixel.y));
    return fract(noise + 0.6180339887*cloudFrame);
}

// Whether every scene pixel this texel covers holds drawn geometry.
bool cloudIsCovered(vec2 texel)
{
    if (!hasSkyMask)
    {
        return false;
    }
    vec2 scale = skyMaskResolution/resolution;
    ivec2 first = ivec2(floor(texel*scale));
    ivec2 last = ivec2(ceil((texel + 1.0)*scale)) - 1;
    for (int y = first.y; y <= last.y; y++)
    {
        for (int x = first.x; x <= last.x; x++)
        {
            if (texelFetch(skyMask, clamp(ivec2(x, y), ivec2(0), ivec2(skyMaskResolution) - 1), 0).a <= 0.0)
            {
                return false;
            }
        }
    }
    return true;
}

// One layer's part of the picture seen through the air in front of it, composited behind the layers before it.
void cloudComposite(inout vec3 light, inout float transmittance, CloudMarch march, vec2 screenUv, vec3 direction,
    float fallbackDistance)
{
    if (march.transmittance >= 1.0)
    {
        return;
    }
    float apparent = (march.weightSum > 0.0) ? march.distanceSum/march.weightSum : fallbackDistance;
    vec3 airScatter = vec3(0.0);
    vec3 airTransmittance = vec3(1.0);
    float from = min(hazeStartDistance, apparent);
    if (hazeActive && (hazeDistanceScale*(apparent - from) > 0.0))
    {
        hazeVolumeRead(screenUv, hazeSlice(direction, hazeDistanceScale*(apparent - from)), airScatter,
            airTransmittance);
    }
    light += transmittance*(airTransmittance*march.light + (1.0 - march.transmittance)*airScatter);
    transmittance *= march.transmittance;
}

void main()
{
    vec2 texel = floor(gl_FragCoord.xy);
    if (cloudIsCovered(texel))
    {
        finalColor = vec4(0.0, 0.0, 0.0, -1.0);
        return;
    }
    vec2 uv = gl_FragCoord.xy/resolution;
    vec4 world = invViewProj*vec4(uv*2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(world.xyz/world.w); // the camera sits at the origin of render space
    float jitter = cloudJitter(texel);
    float cosAngle = dot(direction, cloudLightDirection);

    // The layers met along the ray, nearest first (from below: low, mid, high; from above the other way round).
    CloudMarch low = CloudMarch(vec3(0.0), 1.0, 0.0, 0.0);
    CloudMarch mid = CloudMarch(vec3(0.0), 1.0, 0.0, 0.0);
    CloudMarch high = CloudMarch(vec3(0.0), 1.0, 0.0, 0.0);
    float lowStart = 0.0;
    float lowEnd = -1.0;
    float midStart = 0.0;
    float midEnd = -1.0;
    float highNear = 0.0;
    float highFar = -1.0;
    bool hasLow = (cloudLowExtinction > 0.0) && cloudShellSegment(direction, cloudLowLayer.x, cloudLowLayer.y,
        lowStart, lowEnd);
    bool hasMid = (cloudMidExtinction > 0.0) && cloudShellSegment(direction, cloudMidLayer.x, cloudMidLayer.y,
        midStart, midEnd);
    bool hasHigh = (cloudHighLayer.w > 0.0) && cloudSphereDistances(direction.y, cloudHighLayer.x, highNear, highFar)
        && (atmosphereViewHeight < cloudHighLayer.x) && (atmosphereDistanceToGround(atmosphereViewHeight,
        direction.y) < 0.0);
    if (hasLow)
    {
        low = cloudMarchLow(direction, lowStart, lowEnd, jitter, cosAngle);
    }
    if (hasMid && ((low.transmittance > CLOUD_OPAQUE) || (midStart < lowStart)))
    {
        mid = cloudMarchMid(direction, midStart, midEnd, jitter, cosAngle);
    }
    if (hasHigh && (low.transmittance*mid.transmittance > CLOUD_OPAQUE))
    {
        high = cloudHigh(direction, highFar, cosAngle);
    }

    vec3 light = vec3(0.0);
    float transmittance = 1.0;
    vec2 screenUv = uv;
    bool isLowFirst = !hasMid || (lowStart <= midStart);
    if (isLowFirst)
    {
        cloudComposite(light, transmittance, low, screenUv, direction, lowStart);
        cloudComposite(light, transmittance, mid, screenUv, direction, midStart);
    }
    else
    {
        cloudComposite(light, transmittance, mid, screenUv, direction, midStart);
        cloudComposite(light, transmittance, low, screenUv, direction, lowStart);
    }
    cloudComposite(light, transmittance, high, screenUv, direction, highFar);
    // A march that stopped as nearly opaque hides the rest.
    transmittance = (transmittance < CLOUD_OPAQUE) ? 0.0 : transmittance;
    finalColor = vec4(min(light, vec3(65504.0)), transmittance);
}
