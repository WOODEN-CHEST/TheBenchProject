// The clouds, for #include "common/clouds": their three layers' shapes as densities in the air, on the curved planet,
// and the light they scatter. The cloud pass marches them for every sky pixel (shaders/clouds/march.frag) and builds the
// map of their shadow from them (shaders/clouds/shadow.frag); the frame's values come from source/WorldClouds.{h,c}
// (WorldRendererClouds.c uploads them).
//
// THE LAYERS (heights above the atmosphere's ground, sea level):
//  - LOW, the weather's clouds (cloud_coverage, cloud_type, cloud_density) between their base and top: a Perlin-Worley
//    volume (Schneider and Vos, "The Real-time Volumetric Cloudscapes of Horizon Zero Dawn", 2015) shaped by a height
//    profile per type (flat layers fill the bottom, cumulus about half with rounded tops, storm clouds all of it), on a
//    ground plan of cells (cumulus) or soft masses (flat layers), eroded at the edges by a finer Worley volume (wisps
//    below, billows above);
//  - MID (altocumulus, altostratus), a thin volume of small puffs or a sheet;
//  - HIGH (cirrus, cirrocumulus), a sheet of thin ice clouds at one height, streaks or small puffs.
// Every layer's clouds gather into fields by a large region pattern: at a low coverage, fields of normal clouds with clear
// sky between (not a thin scatter of small ones everywhere); at a high one, regions with more and fewer.
// The volumes are read twice, the second time turned and a little finer, and the two evenly mixed: the mix repeats only
// over several of their tiles, so no lattice of the same clouds shows.
// The patterns are read at their finest level everywhere (textureLod): the march's loops leave the implicit derivatives
// undefined, which would pick levels at random. Every pattern is anchored to the world: a render-space point plus its
// pattern's anchor (the frame's origin less the wind's drift, wrapped in double on the CPU where all of the pattern's
// reads here repeat) is its place in the pattern, exact far from the world's origin, and the pattern moves with the wind.
//
// THE LIGHT (one light scale): the sun or the moon above the air, through the transmittance table at the cloud's own
// height (so clouds stay lit after sunset on the ground below them) and through the cloud toward it: scattered once by a
// two-lobe Henyey-Greenstein phase (a strong forward lobe: silver linings), a few times as a softer forward glow (one
// octave of weaker extinction and anisotropy: Wrenninge, "Oz: The Great and Volumetric", 2013) and many times as diffuse
// light, dimmed by the two-stream transmission of the depth it crossed (shaders/clouds/march.frag); the sky's irradiance
// from above and the ground's bounce from below, each diffused through the cloud on that side; the town's light from
// below at night; a lightning flash near its strike. Each step's light is integrated over its own depth (Hillaire,
// "Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite", 2016), so thick steps keep their energy.

#include "common/atmosphere"

// ---- The noise ----------------------------------------------------------------------------------------------------
uniform sampler3D cloudShapeNoise;  // the base shape (CloudNoise.h: Perlin-Worley, evenly spread over 0 .. 1)
uniform sampler3D cloudDetailNoise; // the edges' erosion (a Worley sum, evenly spread)
uniform sampler2D cloudWeatherMap;  // flat patterns: R where clouds gather, G a finer variation, B streaks, A cells

// ---- The layers ---------------------------------------------------------------------------------------------------
// Low: base, top (metres above the atmosphere's ground), coverage (0 .. 1), type (0 flat .. 0.5 cumulus .. 1 storm).
uniform vec4 cloudLowLayer;
// Its extinction per metre where its density is 1 (0: no low layer), and how strongly its edges erode (0 .. 2).
uniform float cloudLowExtinction;
uniform float cloudDetailStrength;
// Mid: base, top, coverage, type (0 a sheet .. 1 puffs), and its extinction per metre at density 1 (0: none).
uniform vec4 cloudMidLayer;
uniform float cloudMidExtinction;
// High: height, coverage, type (0 streaks .. 1 puffs), and its optical depth straight up at density 1 (0: none).
uniform vec4 cloudHighLayer;

// ---- The patterns' anchors and tiles ------------------------------------------------------------------------------
// The anchors (metres): a render-space point plus its anchor is its place in that pattern.
uniform vec3 cloudShapeAnchor;     // the low layer's shape
uniform vec3 cloudDetailAnchor;    // the low layer's erosion
uniform vec2 cloudCoverageAnchor;  // the low layer's coverage map
uniform vec3 cloudMidAnchor;       // the mid layer's shape and map
uniform vec2 cloudHighAnchor;      // the high layer's map
// The tiles (metres per repeat): the low layer's shape, its erosion, its coverage map, the mid layer's shape; the mid
// layer's map, the high layer's map.
uniform vec4 cloudTiles;
uniform vec2 cloudMapTiles;

// How much the coverage map moves the coverage around its value: regions and patches with more and fewer clouds.
const float CLOUD_COVERAGE_VARIATION = 0.6;
// Clouds gather in fields: below this coverage a layer's clouds stand in fields of about it with clear sky between (the
// fields over the coverage's share of the sky over this), the fields' edges this soft (a share of the region pattern).
const float CLOUD_FIELD_COVERAGE = 0.45;
const float CLOUD_FIELD_EDGE = 0.1;
// The volumes' second read: turned (cosine 4/5, sine 3/5 about the vertical) and this much finer across, at this offset
// (in tiles); over four tiles both reads repeat (WorldClouds.h wraps the anchors of patterns read twice there).
const mat2 CLOUD_TURN = mat2(0.8, 0.6, -0.6, 0.8);
const float CLOUD_TURN_SCALE = 1.25;
const vec3 CLOUD_TURN_OFFSET = vec3(0.37, 0.0, 0.71);
// The low layer's shape volume is read this many times as finely up as across: bulging sides, no straight walls.
const float CLOUD_SHAPE_UPWARD = 2.0;
// The erosion's depth: the detail noise carves this share of the density at the edges (times cloudDetailStrength), at
// most CLOUD_MAX_EROSION (the remap divides by what it leaves).
const float CLOUD_EROSION = 0.6;
const float CLOUD_MAX_EROSION = 0.9;
// The ground plan: the type range over which the low layer's clouds turn from masses (flat layers: their gaps fall
// irregularly) into cells (cumulus: they stand apart), how many times the plan repeats across the coverage map's tile
// (its anchor is the map's), and its share of the footprint at the layer's base (less higher up).
const float CLOUD_CUMULUS_FROM = 0.2;
const float CLOUD_CUMULUS_TO = 0.5;
const float CLOUD_CELL_REPEATS = 3.0;
const float CLOUD_CELL_SHARE = 0.5;
// How far inside a cloud's outline its density is full, as a share of the coverage (smaller: crisper edges), and over
// what share of the layer's room its base fades in.
const float CLOUD_EDGE_SOFTNESS = 0.6;
const float CLOUD_BASE_SOFTNESS = 0.03;
// The mid layer's puffs' share of its pattern at type 1; the high layer's fibres' and puffs' scales over its map.
const float CLOUD_MID_CELL_SHARE = 0.65;
const float CLOUD_HIGH_FIBRE_SCALE = 2.7;
const float CLOUD_HIGH_PUFF_SCALE = 3.0;
// The high layer's slant path is capped at this many times its straight depth (rays near the horizon).
const float CLOUD_HIGH_MAX_SLANT = 12.0;


// ---- Geometry on the curved planet -------------------------------------------------------------------------------
// The height above the atmosphere's ground of a render-space point (the camera at the origin, atmosphereViewHeight up,
// the planet's centre straight below it): r^2 - R^2 carried without cancelling, as in common/atmosphere.
float cloudHeightAt(vec3 p)
{
    float up = atmosphereViewHeight + p.y;
    float across = dot(p.xz, p.xz);
    float radius = sqrt((ATMOSPHERE_R_PLANET + up)*(ATMOSPHERE_R_PLANET + up) + across);
    return up + across/(radius + ATMOSPHERE_R_PLANET + up);
}

// The local up at a render-space point (the planet's curvature tilts it away from the camera's up far out).
vec3 cloudUpAt(vec3 p)
{
    return normalize(vec3(p.x, ATMOSPHERE_R_PLANET + atmosphereViewHeight + p.y, p.z));
}

// Where a ray from the camera (zenith cosine mu) crosses the sphere `height` metres up: its two distances (near, far;
// near < 0 when the camera is inside the sphere), false when it misses it. The roots are taken in the forms that do not
// cancel: r^2 - R^2 carried as a difference of heights, the near root of a ray climbing out of the sphere as a quotient.
bool cloudSphereDistances(float mu, float height, out float nearDistance, out float farDistance)
{
    float viewHeight = atmosphereViewHeight;
    float viewRadius = ATMOSPHERE_R_PLANET + viewHeight;
    float lift = (height - viewHeight)*(2.0*ATMOSPHERE_R_PLANET + height + viewHeight); // rh^2 - r0^2
    float b = viewRadius*mu;
    float discriminant = b*b + lift;
    if (discriminant < 0.0)
    {
        nearDistance = 0.0;
        farDistance = -1.0;
        return false;
    }
    float root = sqrt(discriminant);
    // t = -b +- root; the root of the same sign as -b by the quotient lift / (b +- root) form.
    if (b > 0.0)
    {
        nearDistance = -b - root;
        farDistance = lift/(b + root);
    }
    else
    {
        nearDistance = (root - b > 0.0) ? -lift/(root - b) : -b - root;
        farDistance = root - b;
    }
    return true;
}

// The stretch of a ray from the camera (unit direction) inside the shell between two heights, the first one only: its
// start and end distance (start < end), false when it does not meet the shell in front of the camera or the ground hides
// it first.
bool cloudShellSegment(vec3 direction, float base, float top, out float start, out float end)
{
    start = 0.0;
    end = -1.0;
    float topNear;
    float topFar;
    if (!cloudSphereDistances(direction.y, top, topNear, topFar) || (topFar <= 0.0))
    {
        return false;
    }
    float baseNear;
    float baseFar;
    bool isBaseMet = cloudSphereDistances(direction.y, base, baseNear, baseFar);
    float viewHeight = atmosphereViewHeight;
    if (viewHeight < base)
    {
        // From below: from the base's far crossing to the top's, unless the ground comes first.
        if (atmosphereDistanceToGround(viewHeight, direction.y) >= 0.0)
        {
            return false;
        }
        start = max(baseFar, 0.0);
        end = topFar;
    }
    else
    {
        // From inside or above: from the camera or where the ray enters the top, to where it leaves through the base
        // (if it meets it in front) or the top.
        start = max(topNear, 0.0);
        end = (isBaseMet && (baseNear > start)) ? baseNear : topFar;
        if (atmosphereDistanceToGround(viewHeight, direction.y) >= 0.0)
        {
            end = min(end, atmosphereDistanceToGround(viewHeight, direction.y));
        }
    }
    return end > start;
}


// ---- The layers' densities ---------------------------------------------------------------------------------------
// remap: where x lies between a and b, mapped onto c .. d (unclamped).
float cloudRemap(float x, float a, float b, float c, float d)
{
    return c + (x - a)/(b - a)*(d - c);
}

// How much the low layer's clouds narrow at a height fraction (0 the base, 1 the top) for its type, 0 .. 1: a footprint
// needs this much more of the way from 1 - the coverage to 1 to reach that height. Flat layers stop near the bottom,
// cumulus round off at about half the room, storm clouds tower to the top.
float cloudLowTaper(float fraction, float type)
{
    // The taper's start and end height fractions for the three types, blended by the type.
    vec2 stratus = vec2(0.02, 0.14);
    vec2 cumulus = vec2(0.04, 0.6);
    vec2 storm = vec2(0.05, 1.0);
    vec2 span = (type < 0.5) ? mix(stratus, cumulus, type*2.0) : mix(cumulus, storm, type*2.0 - 1.0);
    float taper = clamp((fraction - span.x)/(span.y - span.x), 0.0, 1.0);
    // Slow at first, then fast: rounded tops.
    return taper*taper;
}


// A mix of two values each spread evenly over 0 .. 1 (`weight` the second's share), spread evenly again by the mix's
// own distribution (two uniforms' weighted sum: quadratic at its ends, linear between), so a threshold t keeps 1 - t of
// the points, as for either value alone: the coverage stays the coverage.
float cloudEvenMix(float a, float b, float weight)
{
    float w = min(weight, 1.0 - weight);
    float sum = mix(a, b, weight);
    if (w <= 1e-4)
    {
        return sum;
    }
    float spread = 2.0*w*(1.0 - w);
    if (sum < w)
    {
        return sum*sum/spread;
    }
    if (sum > 1.0 - w)
    {
        return 1.0 - (1.0 - sum)*(1.0 - sum)/spread;
    }
    return (sum - 0.5*w)/(1.0 - w);
}

// A layer's coverage at a place of its region pattern (`region`, 0 .. 1, evenly spread) and of a finer patch pattern
// (`patches`): below CLOUD_FIELD_COVERAGE gathered into fields of about it over the coverage's share of the sky, clear sky
// between; above it everywhere, more in some regions and less in others; moved a little by the patches within. The mean
// stays about the coverage (none at 0, all at 1).
float cloudFieldCoverage(float coverage, float region, float patches)
{
    float share = clamp(coverage/CLOUD_FIELD_COVERAGE, 0.0, 1.0);
    float inField = smoothstep(1.0 - share - CLOUD_FIELD_EDGE, 1.0 - share + CLOUD_FIELD_EDGE, region);
    // Toward the fields' coverage they merge into one (no step where they do), and the regions take over.
    float whole = smoothstep(0.7, 1.0, share);
    float field = mix(inField*CLOUD_FIELD_COVERAGE, coverage, whole);
    float spread = CLOUD_COVERAGE_VARIATION*min(field, 1.0 - field);
    return clamp(field + spread*((region - 0.5)*1.5*whole + (patches - 0.5)), 0.0, 1.0);
}

// A volume at a point of its space (in its tiles): read there and turned (CLOUD_TURN), the two evenly mixed.
float cloudTwiceTurned(sampler3D volume, vec3 q)
{
    vec3 turned = vec3(CLOUD_TURN*q.xz*CLOUD_TURN_SCALE, q.y).xzy + CLOUD_TURN_OFFSET;
    return cloudEvenMix(textureLod(volume, q, 0.0).r, textureLod(volume, turned, 0.0).r, 0.5);
}

// The low layer's coverage where a render-space point lies: the weather's, gathered into fields by the coverage map's
// regions and moved by its patches (it changes over kilometres, so the steps toward the light take the view's point's).
float cloudLowCoverageAt(vec3 p)
{
    vec2 map = textureLod(cloudWeatherMap, (p.xz + cloudCoverageAnchor)/cloudTiles.z, 0.0).rg;
    return cloudFieldCoverage(cloudLowLayer.z, map.r, map.g);
}

// The low layer's density at a render-space point `height` metres up (0 .. 1; times cloudLowExtinction per metre),
// eroded at its edges while `withDetail` (the light's far steps and the shadow map leave the erosion out).
// Its clouds are where their footprint passes a threshold that rises with height: 1 - the coverage at the base (so about
// the coverage's share of the ground lies under them), up to 1 at the type's top (cloudLowTaper), so every cloud has a
// flat base and narrows to a rounded top, and the footprints that stand highest tower highest. The footprint is the shape
// volume's masses (bulging in 3D, read twice), mixed near the base with the ground plan: the weather map's round cells
// for cumulus and storm clouds, so they stand apart, its soft masses for flat layers. The density rises steeply inside
// the outline (crisp edges) and the detail volume (read twice) erodes the edges.
// The same with the coverage at the point given (cloudLowCoverageAt; it changes slowly, so the steps toward the light
// take the view's point's).
float cloudLowDensityCovered(vec3 p, float height, float coverage, bool withDetail)
{
    float fraction = (height - cloudLowLayer.x)/(cloudLowLayer.y - cloudLowLayer.x);
    if ((fraction <= 0.0) || (fraction >= 1.0) || (coverage <= 0.0))
    {
        return 0.0;
    }
    float type = cloudLowLayer.w;
    float taper = cloudLowTaper(fraction, type);
    if (taper >= 1.0)
    {
        return 0.0;
    }
    float shape = cloudTwiceTurned(cloudShapeNoise,
        (p + cloudShapeAnchor)*vec3(1.0, CLOUD_SHAPE_UPWARD, 1.0)/cloudTiles.x);
    // The ground plan shapes the clouds' bases; higher up the shape's bulges take over (no straight walls).
    float cumulus = smoothstep(CLOUD_CUMULUS_FROM, CLOUD_CUMULUS_TO, type);
    vec4 plan = textureLod(cloudWeatherMap, (p.xz + cloudCoverageAnchor)*(CLOUD_CELL_REPEATS/cloudTiles.z), 0.0);
    float footprint = cloudEvenMix(shape, cloudEvenMix(plan.g, plan.a, cumulus), CLOUD_CELL_SHARE*(1.0 - fraction));
    float threshold = 1.0 - coverage*(1.0 - taper);
    float inside = footprint - threshold;
    if (inside <= 0.0)
    {
        return 0.0;
    }
    // Full density this far inside the outline (a share of the coverage), and a flat base.
    float density = min(inside/(CLOUD_EDGE_SOFTNESS*coverage), 1.0)*smoothstep(0.0, CLOUD_BASE_SOFTNESS, fraction);
    if (!withDetail)
    {
        return density;
    }
    // The erosion: wisps at the bottom (the detail itself), billows above (its inverse), carving the thin edges most.
    float detail = cloudTwiceTurned(cloudDetailNoise, (p + cloudDetailAnchor)/cloudTiles.y);
    float modifier = mix(detail, 1.0 - detail, clamp(fraction*6.0, 0.0, 1.0));
    float erosion = min(CLOUD_EROSION*cloudDetailStrength*modifier, CLOUD_MAX_EROSION);
    return clamp(cloudRemap(density, erosion, 1.0, 0.0, 1.0), 0.0, 1.0);
}

float cloudLowDensity(vec3 p, float height, bool withDetail)
{
    return cloudLowDensityCovered(p, height, cloudLowCoverageAt(p), withDetail);
}

// The mid layer's density at a render-space point (0 .. 1; times cloudMidExtinction), thickest in its middle: by its
// type a smooth sheet (the shape volume, altostratus) or small puffs (the map's cells, altocumulus; both read twice),
// gathered into fields (the map's regions at four times its scale, its patches at twice it), their outlines soft.
float cloudMidDensity(vec3 p, float height)
{
    float fraction = (height - cloudMidLayer.x)/(cloudMidLayer.y - cloudMidLayer.x);
    if ((fraction <= 0.0) || (fraction >= 1.0))
    {
        return 0.0;
    }
    float profile = smoothstep(0.0, 0.3, fraction)*(1.0 - smoothstep(0.55, 1.0, fraction));
    vec3 q = p + cloudMidAnchor;
    float region = textureLod(cloudWeatherMap, q.xz/(cloudMapTiles.x*4.0), 0.0).r;
    float patches = textureLod(cloudWeatherMap, q.xz/(cloudMapTiles.x*2.0), 0.0).r;
    float coverage = cloudFieldCoverage(cloudMidLayer.z, region, patches);
    if (coverage <= 0.0)
    {
        return 0.0;
    }
    float shape = cloudTwiceTurned(cloudShapeNoise, q/cloudTiles.w);
    vec2 cellsAt = q.xz/cloudMapTiles.x;
    float cells = cloudEvenMix(textureLod(cloudWeatherMap, cellsAt, 0.0).a,
        textureLod(cloudWeatherMap, CLOUD_TURN*cellsAt*CLOUD_TURN_SCALE + CLOUD_TURN_OFFSET.xz, 0.0).a, 0.5);
    float pattern = cloudEvenMix(shape, cells, CLOUD_MID_CELL_SHARE*cloudMidLayer.w);
    float inside = pattern - (1.0 - coverage*profile);
    return clamp(inside/(CLOUD_EDGE_SOFTNESS*coverage), 0.0, 1.0);
}

// The high layer's density where a ray crosses it (render-space point; 0 .. 1): fibres combed along one way (two scales
// of the map's streaks, cirrus) or small ripples of puffs (its cells, cirrocumulus) by its type, gathered into fields (the
// map's regions at twice its scale), thin at their edges.
float cloudHighDensity(vec3 p)
{
    vec2 at = p.xz + cloudHighAnchor;
    vec2 region = textureLod(cloudWeatherMap, at/(cloudMapTiles.y*2.0), 0.0).rg;
    float coverage = cloudFieldCoverage(cloudHighLayer.y, region.x, region.y);
    if (coverage <= 0.0)
    {
        return 0.0;
    }
    vec2 q = at/cloudMapTiles.y;
    vec4 map = textureLod(cloudWeatherMap, q, 0.0);
    float fibres = textureLod(cloudWeatherMap, q*CLOUD_HIGH_FIBRE_SCALE + map.gg*0.02, 0.0).b;
    float puffs = textureLod(cloudWeatherMap, q*CLOUD_HIGH_PUFF_SCALE, 0.0).a;
    float streaks = cloudEvenMix(map.b, fibres, 0.4);
    float pattern = cloudEvenMix(streaks, puffs, cloudHighLayer.z);
    float inside = pattern - (1.0 - coverage);
    // Thin: the density grows slowly inside the outline, the strongest strands densest.
    return clamp(inside/coverage, 0.0, 1.0);
}


// ---- Light --------------------------------------------------------------------------------------------------------
// Henyey and Greenstein's phase function for the cosine of the angle between the light and the view.
float cloudHenyeyGreenstein(float cosAngle, float g)
{
    float gg = g*g;
    return (1.0 - gg)/(4.0*PI*pow(max(1.0 + gg - 2.0*g*cosAngle, 1e-6), 1.5));
}

// The clouds' phase: a strong forward lobe (their droplets' diffraction: silver linings) and a weak backward one.
float cloudPhase(float cosAngle, float forward)
{
    return mix(cloudHenyeyGreenstein(cosAngle, -0.25), cloudHenyeyGreenstein(cosAngle, forward), 0.8);
}
