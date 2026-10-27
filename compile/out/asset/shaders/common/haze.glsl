// The haze in front of what the camera sees, for #include "common/haze": the readers' side
// of the aerial-perspective volume the atmosphere pass builds every frame (shaders/atmosphere/volume.frag; its layout is
// in common/atmosphere), and the ground haze, a low layer of mist integrated per pixel in closed form. The lit shaders
// apply it through common/lighting (ApplyAerialPerspective), the sky pass and the stars take the ground haze in front of
// the sky, and the outlines fade by its transmittance.
//
// A point some distance away is seen as its own light times the haze's transmittance plus the haze's in-scatter:
//  - THE AIR'S, from the volume at the point's screen direction and its distance, linear between the two slices around
//    it;
//  - THE GROUND HAZE'S: density x exp(-max(height above its base, 0) / thickness), its optical depth along the ray in
//    closed form (hazeGroundDepth), lit by the light at its base (the volume's row of values) through its glow toward
//    the sun and the moon. Its light is seen through the air in front of it: the air's haze is read once more where
//    half of the mist's light comes from (hazeGroundDistance), so a far misty valley hazes like everything else at its
//    distance, and a mist around the camera does not.
// The frame's haze (WorldAtmosphere_GetHaze) multiplies every distance and depth by hazeDistanceScale (and a draw's by
// its placement's multiplier), and leaves the first hazeStartDistance metres clear. Heights are the flat world's: the
// camera stands groundHazeHeight above the mist's base, and a ray climbs direction.y per metre.
//
// The closed forms' C mirror is source/WorldAtmosphere.c (WorldAtmosphere_GetGroundHazeDepth, _GetGroundHazeDistance):
// keep the two in step.

#include "common/atmosphere"

// Whether the volume holds this frame's haze (the atmosphere's tables run); false: no haze at all.
uniform bool hazeActive;
// The aerial-perspective volume (the lit programs read it on WorldRendererTextureUnit_HazeVolume, 8).
uniform sampler2D atmosphereVolume;
// The frame's haze: every distance x this (0: no haze), none nearer than this many metres.
uniform float hazeDistanceScale;
uniform float hazeStartDistance;
// The ground haze: its density per metre at its base and below (0: none), how fast it thins above it (metres), the
// camera's height above the base (metres), its albedo (linear) and its glow toward the lights (the phase function's g).
uniform float groundHazeDensity;
uniform float groundHazeThickness;
uniform float groundHazeHeight;
uniform vec3 groundHazeAlbedo;
uniform float groundHazeGlow;

// Below this the closed forms take their series (the C mirror's ATMOSPHERE_SHARE_SERIES_LIMIT and
// ATMOSPHERE_GROUND_HAZE_SLOPE_LIMIT).
const float HAZE_SERIES_LIMIT = 0.001;
const float HAZE_SLOPE_LIMIT = 0.001;
// The largest light the scene's target (RGBA16F) holds: the ground haze's light saturates there instead of turning
// infinite (a glow of 0.99 toward a bright sun).
const float HAZE_LIGHT_MAX = 65504.0;


// ---- The ground haze's closed forms ----------------------------------------------------------------------------
// (1 - e^-x) / x: an exponential's mean over a stretch, relative to its start; 1 for x = 0.
float hazeExponentialShare(float x)
{
    return (x > HAZE_SERIES_LIMIT) ? (1.0 - exp(-x))/x : 1.0 - 0.5*x;
}

// The ground haze's optical depth from `from` to `to` metres along a ray that starts `heightAboveBase` above the base
// and climbs `rayY` per metre (WorldAtmosphere_GetGroundHazeDepth): the mean density over the heights the stretch
// crosses times its length, so a level ray needs no special case.
float hazeGroundDepth(float density, float thickness, float heightAboveBase, float rayY, float from, float to)
{
    if (!(density > 0.0) || !(to > from))
    {
        return 0.0;
    }
    float start = heightAboveBase + rayY*from;
    float end = heightAboveBase + rayY*to;
    float low = min(start, end);
    float high = max(start, end);
    float mean;
    if (high <= 0.0)
    {
        mean = density; // all of it at or below the base
    }
    else if (low >= 0.0)
    {
        mean = density*exp(-low/thickness)*hazeExponentialShare((high - low)/thickness);
    }
    else
    {
        // From below the base to above it: the part below at the base's density, the part above thinning.
        mean = density*(thickness*(1.0 - exp(-high/thickness)) - low)/(high - low);
    }
    return mean*(to - from);
}

// Where in a stretch [a, b] of the ray, on one side of the base, the depth from `a` reaches `depth` (`stretchDepth`:
// the whole stretch's): linear in depth where the density is even (below the base, or a nearly level ray), else the
// exponential's inverse from the stretch's denser end.
float hazeStretchDistance(float density, float thickness, float heightAboveBase, float rayY, float a, float b,
    float depth, float stretchDepth)
{
    if (!(depth > 0.0))
    {
        return a;
    }
    if (!(depth < stretchDepth))
    {
        return b;
    }
    float startHeight = heightAboveBase + rayY*a;
    float endHeight = heightAboveBase + rayY*b;
    float slope = abs(rayY)*(b - a)/thickness;
    if (!((startHeight + endHeight) > 0.0) || (slope < HAZE_SLOPE_LIMIT))
    {
        return a + depth*(b - a)/stretchDepth;
    }
    if (rayY > 0.0)
    {
        // Thinning from a: depth = density(a) thickness / rayY (1 - e^(-rayY (t - a) / thickness)).
        float densityAtStart = density*exp(-startHeight/thickness);
        if (!(densityAtStart > 0.0))
        {
            return a;
        }
        float u = depth*rayY/(densityAtStart*thickness);
        float factor = (u > HAZE_SERIES_LIMIT) ? -log(max(1.0 - u, 1e-30))/u : 1.0 + 0.5*u;
        return clamp(a + depth/densityAtStart*factor, a, b);
    }
    // Thickening toward b: depth = density(b) thickness / -rayY (e^(rayY (b - t) / thickness) - e^(-slope)).
    float densityAtEnd = density*exp(-endHeight/thickness);
    if (!(densityAtEnd > 0.0))
    {
        return a;
    }
    float v = depth*(-rayY)/(densityAtEnd*thickness) + exp(-slope);
    return clamp(b + thickness*log(min(v, 1.0))/(-rayY), a, b);
}

// e^(x^2) erfc(x) for x >= 0 (Numerical Recipes' erfcc without its e^(-x^2): a fractional error below 1.2e-7), for the
// ground haze along the curved planet (WorldAtmosphere_GetScaledErfc).
float hazeErfcx(float x)
{
    float t = 1.0/(1.0 + 0.5*max(x, 0.0));
    return t*exp(-1.26551223 + t*(1.00002368 + t*(0.37409196 + t*(0.09678418 + t*(-0.18628806 + t*(0.27886807
        + t*(-1.13520398 + t*(1.48851587 + t*(-0.82215223 + t*0.17087277)))))))));
}

// The ground haze's optical depth over `along` metres on one side of a curved ray's lowest point, `lowest` above the base
// there, the ray rising around it as lowest + s^2 / (2 curvatureRadius): the stretch below the base at the base's
// density, the rest a Gaussian in s (the C mirror's GetGroundHazeCurveSide).
float hazeGroundCurveSide(float density, float thickness, float lowest, float curvatureRadius, float along)
{
    float width = sqrt(2.0*curvatureRadius*thickness);
    float belowBase = sqrt(max(-2.0*curvatureRadius*lowest, 0.0)); // where the ray crosses the base
    float depth = density*min(along, belowBase);
    if (along > belowBase)
    {
        float a = belowBase/width;
        float b = along/width;
        depth += density*exp(-max(lowest, 0.0)/thickness)*0.5*sqrt(PI)*width
            *(hazeErfcx(a) - exp(a*a - b*b)*hazeErfcx(b));
    }
    return depth;
}

// The ground haze's optical depth from `from` to `to` metres along a ray over the curved planet, its start `radius` from
// the planet's centre and climbing with the zenith cosine `rayY` (WorldAtmosphere_GetGroundHazeDepthCurved): its height
// a parabola around its lowest point, so a ray that passes over the horizon climbs out of the haze again.
float hazeGroundDepthCurved(float density, float thickness, float heightAboveBase, float rayY, float radius, float from,
    float to)
{
    if (!(density > 0.0) || !(to > from))
    {
        return 0.0;
    }
    float closest = -radius*rayY;
    float sinZenith = sqrt(max(1.0 - rayY*rayY, 0.0));
    float lowest = heightAboveBase - radius*rayY*rayY/(1.0 + sinZenith);
    float curvatureRadius = radius*sinZenith;
    // The depth is odd in the signed distance from the lowest point: the difference of the two ends' sides.
    float sFrom = from - closest;
    float sTo = to - closest;
    float depthTo = hazeGroundCurveSide(density, thickness, lowest, curvatureRadius, abs(sTo));
    float depthFrom = hazeGroundCurveSide(density, thickness, lowest, curvatureRadius, abs(sFrom));
    return max(((sTo < 0.0) ? -depthTo : depthTo) - ((sFrom < 0.0) ? -depthFrom : depthFrom), 0.0);
}

// The distance from `from` (at most `to`) at which the ground haze's optical depth along the ray reaches `depth`
// (WorldAtmosphere_GetGroundHazeDistance): the inverse of hazeGroundDepth in its end, the ray split where it crosses the
// base.
float hazeGroundDistance(float density, float thickness, float heightAboveBase, float rayY, float from, float to,
    float depth)
{
    if (!(depth > 0.0) || !(to > from))
    {
        return from;
    }
    float crossing = (rayY != 0.0) ? -heightAboveBase/rayY : -1.0;
    float split = ((crossing > from) && (crossing < to)) ? crossing : to;
    float firstDepth = hazeGroundDepth(density, thickness, heightAboveBase, rayY, from, split);
    if (depth <= firstDepth)
    {
        return hazeStretchDistance(density, thickness, heightAboveBase, rayY, from, split, depth, firstDepth);
    }
    return hazeStretchDistance(density, thickness, heightAboveBase, rayY, split, to, depth - firstDepth,
        hazeGroundDepth(density, thickness, heightAboveBase, rayY, split, to));
}


// ---- Reading the volume ----------------------------------------------------------------------------------------
// The air's haze along a screen direction at a slice coordinate (atmosphereVolumeSlice), linear between the two slices
// around it; from -1 to 0, between the camera's clear air and the first slice.
void hazeVolumeRead(vec2 screenUv, float slice, out vec3 inScatter, out vec3 transmittance)
{
    float lower = floor(slice);
    float share = slice - lower;
    vec3 lowerScatter = vec3(0.0);
    vec3 lowerTransmittance = vec3(1.0);
    if (lower >= 0.0)
    {
        lowerScatter = textureLod(atmosphereVolume, atmosphereVolumeUv(screenUv, lower, 0.0), 0.0).rgb;
        lowerTransmittance = textureLod(atmosphereVolume, atmosphereVolumeUv(screenUv, lower, 1.0), 0.0).rgb;
    }
    float upper = min(lower + 1.0, ATMOSPHERE_VOLUME_SLICE_COUNT - 1.0);
    vec3 upperScatter = textureLod(atmosphereVolume, atmosphereVolumeUv(screenUv, upper, 0.0), 0.0).rgb;
    vec3 upperTransmittance = textureLod(atmosphereVolume, atmosphereVolumeUv(screenUv, upper, 1.0), 0.0).rgb;
    inScatter = mix(lowerScatter, upperScatter, share);
    transmittance = mix(lowerTransmittance, upperTransmittance, share);
}

// The same for the transmittance alone.
vec3 hazeVolumeTransmittance(vec2 screenUv, float slice)
{
    float lower = floor(slice);
    float share = slice - lower;
    vec3 lowerTransmittance = (lower >= 0.0)
        ? textureLod(atmosphereVolume, atmosphereVolumeUv(screenUv, lower, 1.0), 0.0).rgb : vec3(1.0);
    float upper = min(lower + 1.0, ATMOSPHERE_VOLUME_SLICE_COUNT - 1.0);
    vec3 upperTransmittance = textureLod(atmosphereVolume, atmosphereVolumeUv(screenUv, upper, 1.0), 0.0).rgb;
    return mix(lowerTransmittance, upperTransmittance, share);
}

// The volume's slice coordinate for `hazeDistance` (a distance already scaled) along `direction`.
float hazeSlice(vec3 direction, float hazeDistance)
{
    // The distance to the top matters only past the last distance slice.
    float toTop = (hazeDistance > ATMOSPHERE_VOLUME_FARTHEST)
        ? atmosphereDistanceToTop(atmosphereViewHeight, direction.y) : 0.0;
    return atmosphereVolumeSlice(hazeDistance, toTop);
}

// The light the ground haze sends toward the camera along `direction` where it is infinitely thick: its albedo times
// the light at its base (the volume's values), the sun's and the moon's through its glow toward them, plus the light
// reaching it from all around.
vec3 hazeGroundLight(vec3 direction, vec3 sunDirection, vec3 moonDirection)
{
    vec3 sun = texelFetch(atmosphereVolume, atmosphereVolumeValueTexel(ATMOSPHERE_VOLUME_GROUND_HAZE_SUN), 0).rgb;
    vec3 moon = texelFetch(atmosphereVolume, atmosphereVolumeValueTexel(ATMOSPHERE_VOLUME_GROUND_HAZE_MOON), 0).rgb;
    vec3 around = texelFetch(atmosphereVolume, atmosphereVolumeValueTexel(ATMOSPHERE_VOLUME_GROUND_HAZE_AMBIENT), 0).rgb;
    return groundHazeAlbedo*(sun*atmosphereCornetteShanksPhase(dot(direction, sunDirection), groundHazeGlow)
        + moon*atmosphereCornetteShanksPhase(dot(direction, moonDirection), groundHazeGlow) + around);
}

// Where along the ray half of the ground haze's light comes from, for its (scaled) depth: nearer than where its depth is
// -ln((1 + e^-depth) / 2), about half its depth in a thin mist, the first ln 2 of it in a thick one; along the flat world's
// straight ray, at least `from`.
float hazeGroundMedianDistance(vec3 direction, float toPoint, float scale, float from, float depth)
{
    return hazeGroundDistance(groundHazeDensity, groundHazeThickness, groundHazeHeight, direction.y, from, toPoint,
        -log(0.5*(1.0 + exp(-depth)))/scale);
}

// The ground haze's light toward the camera along `direction` as the camera sees it: the light of the haze placed
// `hazeDistance` (scaled) along the ray, seen through the air's haze in front of it; at most HAZE_LIGHT_MAX.
vec3 hazeGroundLightSeen(vec2 screenUv, vec3 direction, float hazeDistance, vec3 sunDirection, vec3 moonDirection)
{
    vec3 airScatter;
    vec3 airTransmittance;
    hazeVolumeRead(screenUv, hazeSlice(direction, hazeDistance), airScatter, airTransmittance);
    return min(airScatter + airTransmittance*hazeGroundLight(direction, sunDirection, moonDirection),
        vec3(HAZE_LIGHT_MAX));
}

// The ground haze between `from` and `toPoint` along `direction` (seen at `screenUv`), its depth times `scale`: its
// transmittance, and in `mistLight` its light toward the camera as the camera sees it (through the air in front of where
// half of it comes from). 1 and no light without ground haze.
float hazeGroundInFront(vec2 screenUv, vec3 direction, float toPoint, float scale, float from, vec3 sunDirection,
    vec3 moonDirection, out vec3 mistLight)
{
    mistLight = vec3(0.0);
    float depth = scale*hazeGroundDepth(groundHazeDensity, groundHazeThickness, groundHazeHeight, direction.y, from,
        toPoint);
    if (!(depth > 0.0))
    {
        return 1.0;
    }
    float mistDistance = hazeGroundMedianDistance(direction, toPoint, scale, from, depth);
    mistLight = hazeGroundLightSeen(screenUv, direction, scale*(mistDistance - from), sunDirection, moonDirection);
    return exp(-depth);
}

// The ground haze in front of the sky (the sky pass, at the frame's haze), up to `toPoint`: the far ground's distance
// below the horizon, WORLD_ATMOSPHERE_SKY_DISTANCE above it. As in front of a surface, except for a ray that looks down
// and passes over the planet's horizon (`isOverTheHorizon`): beyond its lowest point it climbs out of the haze again,
// where the flat world's straight ray would sink into it (from up high, a band of mist between level and the horizon),
// so its depth follows the curved planet, and its light is taken no farther out than its lowest point.
float hazeGroundInFrontOfSky(vec2 screenUv, vec3 direction, float toPoint, bool isOverTheHorizon, vec3 sunDirection,
    vec3 moonDirection, out vec3 mistLight)
{
    float scale = hazeDistanceScale;
    float from = min(hazeStartDistance, toPoint);
    if (!isOverTheHorizon)
    {
        return hazeGroundInFront(screenUv, direction, toPoint, scale, from, sunDirection, moonDirection, mistLight);
    }
    mistLight = vec3(0.0);
    float radius = ATMOSPHERE_R_PLANET + atmosphereViewHeight;
    float depth = scale*hazeGroundDepthCurved(groundHazeDensity, groundHazeThickness, groundHazeHeight, direction.y,
        radius, from, toPoint);
    if (!(depth > 0.0))
    {
        return 1.0;
    }
    float mistDistance = min(hazeGroundMedianDistance(direction, toPoint, scale, from, depth),
        max(-radius*direction.y, from));
    mistLight = hazeGroundLightSeen(screenUv, direction, scale*(mistDistance - from), sunDirection, moonDirection);
    return exp(-depth);
}


// ---- The haze in front of a point -------------------------------------------------------------------------------
// The haze between the camera and a point `toPoint` metres away along `direction` (unit, render space), seen at
// `screenUv` (0 at the scene's bottom left), for a draw whose haze is `multiplier` times the frame's: what lies there
// reaches the camera times `transmittance`, plus `inScatter` (the sky's light units, before the eye's night vision).
// `sunDirection` and `moonDirection` (unit) light the ground haze. No haze while the volume is off.
void hazeInFront(vec2 screenUv, vec3 direction, float toPoint, float multiplier, vec3 sunDirection,
    vec3 moonDirection, out vec3 inScatter, out vec3 transmittance)
{
    inScatter = vec3(0.0);
    transmittance = vec3(1.0);
    float scale = hazeDistanceScale*max(multiplier, 0.0);
    float from = min(hazeStartDistance, toPoint);
    if (!hazeActive || !(scale*(toPoint - from) > 0.0))
    {
        return;
    }
    hazeVolumeRead(screenUv, hazeSlice(direction, scale*(toPoint - from)), inScatter, transmittance);
    if (groundHazeDensity > 0.0)
    {
        vec3 mistLight;
        float mistTransmittance = hazeGroundInFront(screenUv, direction, toPoint, scale, from, sunDirection,
            moonDirection, mistLight);
        inScatter = mix(mistLight, inScatter, mistTransmittance);
        transmittance *= mistTransmittance;
    }
}

// The haze's transmittance alone (what hazeInFront gives), for an emitter seen through it and the outlines' fade.
vec3 hazeTransmittance(vec2 screenUv, vec3 direction, float toPoint, float multiplier)
{
    float scale = hazeDistanceScale*max(multiplier, 0.0);
    float from = min(hazeStartDistance, toPoint);
    if (!hazeActive || !(scale*(toPoint - from) > 0.0))
    {
        return vec3(1.0);
    }
    vec3 transmittance = hazeVolumeTransmittance(screenUv, hazeSlice(direction, scale*(toPoint - from)));
    return transmittance*exp(-scale*hazeGroundDepth(groundHazeDensity, groundHazeThickness, groundHazeHeight,
        direction.y, from, toPoint));
}

// The ground haze's transmittance alone toward something `toPoint` metres away (the stars: no volume read), at the
// frame's haze.
float hazeGroundTransmittance(vec3 direction, float toPoint)
{
    float from = min(hazeStartDistance, toPoint);
    if (!hazeActive)
    {
        return 1.0;
    }
    return exp(-hazeDistanceScale*hazeGroundDepth(groundHazeDensity, groundHazeThickness, groundHazeHeight,
        direction.y, from, toPoint));
}
