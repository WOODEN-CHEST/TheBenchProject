// Interior lighting, for #include "common/interior" (common/lighting includes it): what keeps the sky's light out of
// rooms, and what lights them instead. Its C mirror is source/WorldInterior.{h,c} (tests/ShaderSourceTests.c pins the
// constants the two share).
//
// THE SKY'S VISIBILITY. An occupancy volume around the camera holds where the things that keep the sky out stand (roofs,
// walls, tree canopies, the terrain): on the map's grid from above, a column of INTERIOR_SKY_SLICE_COUNT height slices per
// texel, one bit each (an RGBA32UI texel), solid where an odd number of surfaces lie above a slice's centre (so walls under
// a roof are solid and the air under it is not) or where a model's surface passes through it (so a thin slab is not lost;
// above the window its top slice, so a column solid at the window's top goes on up), with an OR pyramid over it. A surface
// reads it in directions around it, from INTERIOR_SKY_MARCH_START metres out to INTERIOR_SKY_MARCH_END, through every
// texel the direction crosses, one after another (no wall falls between two reads; never the surface's own column), at
// the level whose texels are INTERIOR_SKY_CELL_RATIO of the distance wide (finer near, coarser far). Each texel stands for
// the distances the direction spends in it, and tests INTERIOR_SKY_BANDS bands of elevation against the slices their rays
// pass over those distances: what it finds solid hides the elevations from its bottom seen from the farthest to its top
// seen from the nearest, and a band keeps open the share its blockers leave below and above them. The bands start a lift
// above the surface, placed by its geometric normal (never a normal map's), so its own slices are no roof, nor are curbs
// and bumps; a surface facing up goes on around itself as its plane (a slope's own ground is no roof, and where that plane
// rises past the window nothing is known); and what is solid in the surface's own slice alone, solid below it too, is
// what it stands over (the ground at a wall's foot), no wall. The zenith's cap above the bands is read in the
// surface's own column (an upright surface's just in front of it). The visibility is the bands' and the cap's open share,
// each weighed by its solid angle and the surface's cosine to it: a pebble hides a sliver of sky, a wall half of it at its
// foot, a roof all of it, a doorway lets in the sky below its lintel, and a surface facing down is covered by what lies
// above it, itself too. The ambient pre-pass reads
// it per pixel in INTERIOR_SKY_PATTERN_AZIMUTHS directions turned by its pattern and blurs them; where it holds no
// surface's own (blended sprites, the crisp overlay, a frame without the pre-pass), a lit shader reads only what lies
// straight above (InteriorSkyOverhead). The sky's ambient takes
// InteriorSkyShare of the visibility, never less than INTERIOR_SKY_FLOOR, and so do the ground's bounce and the night's
// own light. Toward the map's edge and the window's top and bottom the visibility fades to open (the volume moves with
// the camera), and outside the map everything is open.
//
// AMBIENT ZONES. A draw takes up to MAX_AMBIENT_ZONES zones (boxes or spheres in render space), sorted by priority: each
// group of equal priority (the same group index), its members blended by their weights, covers what lies under it by
// its strongest member's weight. A zone's weight rises inward from 0 at its surface to 1 at its blend distance, and a
// surface on or inside it that faces into it takes it whole: a room's walls, floor and ceiling lie on its zone's surface
// and are lit by it, while a wall's outside face, beyond the zone, takes nothing (its light never leaks out). Inside, a
// surface takes the zones' own light and the sky's ambient times their sky scale.
//
// THE HAZE INSIDE. The part of the view from the camera to a point that lies inside a zone is hazed by the zone's haze:
// the haze is read as if the point were that much nearer (a room's still air has none). A zone counts for a point only
// when the point's draw takes it.
//
// RENDER SPACE: every position here is relative to the camera, which sits at the origin.

#define MAX_AMBIENT_ZONES 4
#define INTERIOR_SKY_SLICE_COUNT 128
#define INTERIOR_SKY_PATTERN_AZIMUTHS 2
#define INTERIOR_SKY_MAX_CELLS 24
#define INTERIOR_SKY_LEVEL_COUNT 8
#define INTERIOR_SKY_BANDS 5
#define INTERIOR_SKY_MAX_RUNS 4
const float INTERIOR_SKY_MAP_RESOLUTION = 1024.0;
const float INTERIOR_SKY_SLICE_HEIGHT = 0.25;
const float INTERIOR_SKY_MARCH_START = 0.14;  // the march's first distance: past the surface's own texel (its diagonal)
const float INTERIOR_SKY_MARCH_END = 24.0;    // and its last, metres
const float INTERIOR_SKY_CELL_RATIO = 0.35;   // a texel the march reads is at least this share of its distance wide
const float INTERIOR_SKY_BAND_DEGREES = 15.0;
const float INTERIOR_SKY_UPRIGHT_LIFT = 0.05; // upright: the bands start just above the surface's own height
const float INTERIOR_SKY_LIFT = 0.3;          // facing up: a slice and a hair (curbs, pebbles, a slope's bumps are no roof)
const float INTERIOR_SKY_ZENITH_FLOOR = 0.02; // the zenith's weight whatever the facing: a surface facing down takes it
const float INTERIOR_SKY_SLOPE_UP = 0.5;      // facing up more than this, a surface goes on around itself as its plane
const float INTERIOR_SKY_MAX_RISE = 1.732;    // the steepest rise of that plane counted, a metre
const float INTERIOR_SKY_TEXEL = 0.09375;     // the volume's texel at level 0, metres
const float INTERIOR_SKY_NO_BLOCKER = 1e30;   // the tangent of an elevation beyond every band's: no blocker met yet
const float INTERIOR_SKY_FLOOR = 0.15;
// Toward the map's edge the visibility fades to open, over INTERIOR_SKY_FADE_WIDTH metres from the camera (horizontally)
// up to INTERIOR_SKY_FADE_END, and over INTERIOR_SKY_WINDOW_FADE metres toward the window's top and bottom: no hard edge
// sweeps over shaded ground.
const float INTERIOR_SKY_FADE_END = 45.0;
const float INTERIOR_SKY_FADE_WIDTH = 12.0;
const float INTERIOR_SKY_WINDOW_FADE = 2.0;
const float INTERIOR_TWO_PI = 6.28318531;
// How far beyond a zone's surface a surface still counts as on it (a room's walls, built on it, within rounding).
const float INTERIOR_ZONE_SURFACE_TOLERANCE = 0.01;
// The debug view shows a surface that takes all of the sky's light at this level on screen (a light grey after the
// tonemap; the floor's share a dark grey).
const float INTERIOR_VIEW_LEVEL = 0.4;
const float INTERIOR_MIN_DIRECTION = 1e-8;

uniform bool interiorActive;               // interior lighting runs this frame (false: every ambient as in the open)
uniform bool skyVisibilityActive;          // the volume holds this frame's occluders (false: every surface sees the sky)
uniform usampler2D skyVolume;              // the occupancy volume: per texel its slices' bits, an OR pyramid in its mips
uniform mat4 skyVisibilityViewProjection;  // render space -> the map's clip space (orthographic, looking down)
uniform float skyVolumeBottom;             // the window's bottom, render space (its slices from there up)
uniform int ambientZoneCount;              // the draw's zones (0 to MAX_AMBIENT_ZONES), set per draw
uniform vec4 ambientZoneCenters[MAX_AMBIENT_ZONES];  // xyz: the centre (render space); w: 0 a box, 1 a sphere
uniform vec4 ambientZoneAxisX[MAX_AMBIENT_ZONES];    // xyz: the zone's unit x axis; w: the half size along it (a
                                                     // sphere: its radius)
uniform vec4 ambientZoneAxisY[MAX_AMBIENT_ZONES];    // the same for y and z (a sphere's unused)
uniform vec4 ambientZoneAxisZ[MAX_AMBIENT_ZONES];
uniform vec4 ambientZoneLights[MAX_AMBIENT_ZONES];   // rgb: its own light per unit of albedo; a: its blend distance
uniform vec4 ambientZoneSettings[MAX_AMBIENT_ZONES]; // x: the sky's scale; y: the haze; z: its priority's group
uniform int interiorView;                  // the debug view: 0 the scene, 1 the sky's share
uniform float interiorViewScale;           // 1 / the exposure, so the view shows its value on screen

// A point's signed distance from a zone's surface (below 0 inside).
float InteriorZoneDistance(int zone, vec3 p)
{
    vec3 offset = p - ambientZoneCenters[zone].xyz;
    if (ambientZoneCenters[zone].w > 0.5)
    {
        return length(offset) - ambientZoneAxisX[zone].w;
    }
    vec3 local = vec3(dot(offset, ambientZoneAxisX[zone].xyz), dot(offset, ambientZoneAxisY[zone].xyz),
        dot(offset, ambientZoneAxisZ[zone].xyz));
    vec3 beyond = abs(local) - vec3(ambientZoneAxisX[zone].w, ambientZoneAxisY[zone].w, ambientZoneAxisZ[zone].w);
    return length(max(beyond, 0.0)) + min(max(beyond.x, max(beyond.y, beyond.z)), 0.0);
}

// The direction into a zone from a point: away from its nearest face (a box), toward its centre (a sphere).
vec3 InteriorZoneInward(int zone, vec3 p)
{
    vec3 offset = p - ambientZoneCenters[zone].xyz;
    if (ambientZoneCenters[zone].w > 0.5)
    {
        float toCenter = length(offset);
        return (toCenter > 0.0) ? -offset/toCenter : vec3(0.0);
    }
    vec3 local = vec3(dot(offset, ambientZoneAxisX[zone].xyz), dot(offset, ambientZoneAxisY[zone].xyz),
        dot(offset, ambientZoneAxisZ[zone].xyz));
    vec3 beyond = abs(local) - vec3(ambientZoneAxisX[zone].w, ambientZoneAxisY[zone].w, ambientZoneAxisZ[zone].w);
    // The nearest face: the axis the point lies furthest out along (the first on a tie).
    if ((beyond.x >= beyond.y) && (beyond.x >= beyond.z))
    {
        return -sign(local.x)*ambientZoneAxisX[zone].xyz;
    }
    return (beyond.y >= beyond.z) ? -sign(local.y)*ambientZoneAxisY[zone].xyz : -sign(local.z)*ambientZoneAxisZ[zone].xyz;
}

// How much a zone weighs for a surface at p with the normal n (zero for no surface): 0 beyond its surface; otherwise the
// larger of the fade inward from its surface and how much the surface faces into it.
float InteriorZoneWeight(int zone, vec3 p, vec3 n)
{
    float signedDistance = InteriorZoneDistance(zone, p);
    if (!(signedDistance < INTERIOR_ZONE_SURFACE_TOLERANCE))
    {
        return 0.0;
    }
    float blend = ambientZoneLights[zone].a;
    float fade = (blend > 0.0) ? clamp(-signedDistance/blend, 0.0, 1.0) : ((signedDistance < 0.0) ? 1.0 : 0.0);
    return max(fade, clamp(dot(n, InteriorZoneInward(zone, p)), 0.0, 1.0));
}

// What the draw's zones give a point.
struct InteriorZoneLight
{
    vec3 ambient;    // their own light, per unit of albedo
    float skyScale;  // what the sky's ambient is multiplied by (1 outside every zone)
    float weight;    // how much of the point they cover
};

// The draw's zones laid over each other for a surface at p with the normal n, in their order (priority ascending): each
// group of equal priority, its members blended by their weights, covers what lies under it by its strongest member's
// weight.
InteriorZoneLight InteriorZonesAt(vec3 p, vec3 n)
{
    InteriorZoneLight light = InteriorZoneLight(vec3(0.0), 1.0, 0.0);
    float uncovered = 1.0;
    int count = min(ambientZoneCount, MAX_AMBIENT_ZONES);
    int first = 0;
    while (first < count)
    {
        float group = ambientZoneSettings[first].z;
        float groupWeight = 0.0;
        float weightSum = 0.0;
        vec3 ambient = vec3(0.0);
        float skyScale = 0.0;
        int next = first;
        while ((next < count) && (ambientZoneSettings[next].z == group))
        {
            float weight = InteriorZoneWeight(next, p, n);
            groupWeight = max(groupWeight, weight);
            weightSum += weight;
            ambient += weight*ambientZoneLights[next].rgb;
            skyScale += weight*ambientZoneSettings[next].x;
            next++;
        }
        if (weightSum > 0.0)
        {
            light.ambient = mix(light.ambient, ambient/weightSum, groupWeight);
            light.skyScale = mix(light.skyScale, skyScale/weightSum, groupWeight);
            uncovered *= 1.0 - groupWeight;
        }
        first = next;
    }
    light.weight = 1.0 - uncovered;
    return light;
}

// The slices below slice k (0 to 128) as a mask: in each word as many of its lowest bits as it has slices below k (no
// shift by 32: the shifted word lacks its lowest bit).
uvec4 InteriorSlicesBelow(int k)
{
    ivec4 count = clamp(ivec4(k) - ivec4(0, 32, 64, 96), 0, 32);
    return (~(uvec4(0xFFFFFFFEu) << uvec4(max(count - 1, 0))))*uvec4(greaterThan(count, ivec4(0)));
}

// The slices from first to last (inclusive, each within the column) as a mask (slice k is word k / 32's bit k % 32).
uvec4 InteriorSliceRun(int first, int last)
{
    return InteriorSlicesBelow(last + 1) & ~InteriorSlicesBelow(first);
}

// The slices a range of heights passes through, first to last: from the slice holding the lower to the one holding the
// higher (clamped to the window), and the top slice whenever the range reaches above it (a column solid at its top goes
// on up); false for a range wholly below the window or a broken one.
bool InteriorSliceRange(float low, float high, out int first, out int last)
{
    float top = skyVolumeBottom + float(INTERIOR_SKY_SLICE_COUNT)*INTERIOR_SKY_SLICE_HEIGHT;
    first = INTERIOR_SKY_SLICE_COUNT - 1;
    last = INTERIOR_SKY_SLICE_COUNT - 1;
    if (!(high >= low) || !(high >= skyVolumeBottom))
    {
        return false;
    }
    if (low < top)
    {
        first = int(floor((max(low, skyVolumeBottom) - skyVolumeBottom)/INTERIOR_SKY_SLICE_HEIGHT));
        last = (high < top) ? min(int(floor((high - skyVolumeBottom)/INTERIOR_SKY_SLICE_HEIGHT)), last) : last;
    }
    return true;
}

// The slices a range of heights passes through (InteriorSliceRange) as a mask; none for none.
uvec4 InteriorSliceRangeMask(float low, float high)
{
    int first;
    int last;
    return InteriorSliceRange(low, high, first, last) ? InteriorSliceRun(first, last) : uvec4(0u);
}

// Where a render-space point (x, z) lands on the map, in [0, 1) across it (linear in the point).
vec2 InteriorSkyUv(vec2 xz)
{
    return (skyVisibilityViewProjection*vec4(xz.x, 0.0, xz.y, 1.0)).xy*0.5 + 0.5;
}

// The volume's texel a place on the map lands on at a level; false off the map (nothing known there: open).
bool InteriorSkyTexelAt(vec2 uv, int level, out ivec2 texel)
{
    texel = ivec2(0);
    if (!(uv.x >= 0.0) || !(uv.x < 1.0) || !(uv.y >= 0.0) || !(uv.y < 1.0))
    {
        return false;
    }
    texel = ivec2(floor(uv*INTERIOR_SKY_MAP_RESOLUTION)) >> level;
    return true;
}

// The volume's texel a render-space point (x, z) lands on at a level; false off the map.
bool InteriorSkyTexel(vec2 xz, int level, out ivec2 texel)
{
    return InteriorSkyTexelAt(InteriorSkyUv(xz), level, texel);
}

// The lowest and the highest slice set in a column masked to a band (first and last); false for none. The lowest set
// bit of a word, alone, is a power of two (exact as a float); the highest is read off the float and corrected for its
// rounding.
bool InteriorFindSolidSlices(uvec4 shared, out int first, out int last)
{
    first = -1;
    last = -1;
    for (int word = 0; word < 4; word++)
    {
        uint bits = shared[word];
        if (bits == 0u)
        {
            continue;
        }
        if (first < 0)
        {
            first = 32*word + int(round(log2(float(bits & (~bits + 1u)))));
        }
        int high = clamp(int(floor(log2(float(bits)))), 0, 31);
        high = ((bits >> uint(high)) == 0u) ? (high - 1) : (((high < 31) && ((bits >> uint(high + 1)) != 0u))
            ? (high + 1) : high);
        last = 32*word + high;
    }
    return first >= 0;
}

// The lowest set bit of a mask that has one, alone in its word (by vector operations and selects: a variable index into
// the mask can put it in slow local memory).
uvec4 InteriorLowestBit(uvec4 bits)
{
    bvec4 isSet = notEqual(bits, uvec4(0u));
    bvec4 isFirst = bvec4(isSet.x, isSet.y && !isSet.x, isSet.z && !any(isSet.xy), isSet.w && !any(isSet.xyz));
    return (bits & (~bits + 1u))*uvec4(isFirst);
}

// The slice of a mask's lowest set bit (InteriorLowestBit's).
int InteriorLowestSlice(uvec4 lowestBit)
{
    // A power of two is exact as a float; the empty words' sum is 0.
    vec4 exponents = log2(max(vec4(lowestBit), vec4(1.0)));
    return int(round(dot(exponents, vec4(1.0)))) + (lowestBit.x != 0u ? 0 : (lowestBit.y != 0u ? 32
        : (lowestBit.z != 0u ? 64 : 96)));
}

// The pyramid's level the march reads at a distance: the finest whose texels are INTERIOR_SKY_CELL_RATIO of it wide.
int InteriorSkyLevelAt(float distance)
{
    return clamp(int(ceil(log2(INTERIOR_SKY_CELL_RATIO*distance/INTERIOR_SKY_TEXEL))), 0, INTERIOR_SKY_LEVEL_COUNT - 1);
}

// A texel of the volume a march crosses: whether it lies on the map (off it nothing is known: open), its column, its width
// in metres, and how far along the direction it ends.
struct InteriorSkyCell
{
    bool isOnMap;
    uvec4 column;
    float texel;
    float farthest;
};

// The texel the direction from the map place uvAt (its place changing by uvAlong a metre) crosses at a distance, at the
// level for that distance (InteriorSkyLevelAt), up to where the direction leaves it (the nearer of its two far sides).
InteriorSkyCell InteriorSkyCellAt(vec2 uvAt, vec2 uvAlong, float distance)
{
    InteriorSkyCell cell;
    int level = InteriorSkyLevelAt(distance);
    float cellsAcross = INTERIOR_SKY_MAP_RESOLUTION/float(1 << level);
    vec2 at = (uvAt + uvAlong*distance)*cellsAcross;
    vec2 corner = floor(at);
    cell.isOnMap = all(greaterThanEqual(corner, vec2(0.0))) && all(lessThan(corner, vec2(cellsAcross)));
    cell.column = cell.isOnMap ? texelFetch(skyVolume, ivec2(corner), level) : uvec4(0u);
    cell.texel = INTERIOR_SKY_TEXEL*float(1 << level);
    vec2 rate = uvAlong*cellsAcross;
    vec2 toSide = mix(at - corner, corner + 1.0 - at, step(0.0, rate));
    vec2 exits = toSide/max(abs(rate), vec2(1e-9));
    cell.farthest = min(distance + min(exits.x, exits.y), INTERIOR_SKY_MARCH_END);
    return cell;
}

// The sine of the elevation whose tangent is t.
float InteriorSineOfTangent(float t)
{
    return t*inversesqrt(1.0 + t*t);
}

// The share of a band of elevations (by their tangents, low to high) left open around the tangents its blockers span
// (lowest to highest; none while lowest is above highest), by solid angle: what lies below them and above them.
float InteriorBandOpenShare(float low, float high, float lowest, float highest)
{
    if (!(lowest <= highest))
    {
        return 1.0;
    }
    float sineLow = InteriorSineOfTangent(low);
    float sineHigh = InteriorSineOfTangent(high);
    float blockedLow = InteriorSineOfTangent(clamp(lowest, low, high));
    float blockedHigh = InteriorSineOfTangent(clamp(highest, low, high));
    return ((blockedLow - sineLow) + (sineHigh - blockedHigh))/(sineHigh - sineLow);
}


// Whether a column's slice is solid: none below the window, every one above it (a column solid at the window's top goes on
// up, so this is asked only of the slice above a solid one).
bool InteriorSliceSolid(uvec4 column, int slice)
{
    if (slice >= INTERIOR_SKY_SLICE_COUNT)
    {
        return true;
    }
    // The word picked by selects, not by a variable index (which can put the column in slow local memory).
    uint word = (slice < 32) ? column.x : ((slice < 64) ? column.y : ((slice < 96) ? column.z : column.w));
    return (slice >= 0) && (((word >> uint(slice & 31)) & 1u) != 0u);
}

// Whether the solid slices found in a column up to the last are only the top of what the surface stands over: solid in
// its own slice alone, below it too, not above it (the ground at a wall's foot shares the foot's slice; a slab within the
// slice, air below it, is no such top: a ceiling just above).
bool InteriorUnderfoot(uvec4 column, int last, int ownSlice)
{
    return (last == ownSlice) && !InteriorSliceSolid(column, last + 1) && InteriorSliceSolid(column, last - 1);
}

// Whether nothing the volume holds lies straight above a surface at p with the normal n, its bands' base and its own
// slice given: read in its own column (an upright surface's a texel and a half in front of it, not in its own wall's;
// less so as it turns down, so what lies above an underside covers it), the top of what it stands over aside.
bool InteriorIsOpenOverhead(vec3 p, vec3 n, float base, int ownSlice)
{
    ivec2 texel;
    if (!InteriorSkyTexel(p.xz + n.xz*(1.5*INTERIOR_SKY_TEXEL*clamp(1.0 + n.y, 0.0, 1.0)), 0, texel))
    {
        return true; // off the map: nothing known, open
    }
    uvec4 column = texelFetch(skyVolume, texel, 0);
    int first;
    int last;
    return !InteriorFindSolidSlices(column & InteriorSliceRangeMask(base, 1e30), first, last)
        || InteriorUnderfoot(column, last, ownSlice);
}

// How far a surface at p is faded to open toward the volume's edges: from the camera's distance (horizontally), and toward
// the window's top and bottom; the larger of the two (1 for a broken position).
float InteriorSkyEdgeFade(vec3 p)
{
    float distanceToCamera = length(p.xz);
    if (!(distanceToCamera >= 0.0) || isnan(p.y))
    {
        return 1.0;
    }
    float top = skyVolumeBottom + float(INTERIOR_SKY_SLICE_COUNT)*INTERIOR_SKY_SLICE_HEIGHT;
    float edge = (distanceToCamera - (INTERIOR_SKY_FADE_END - INTERIOR_SKY_FADE_WIDTH))/INTERIOR_SKY_FADE_WIDTH;
    float below = (skyVolumeBottom + INTERIOR_SKY_WINDOW_FADE - p.y)/INTERIOR_SKY_WINDOW_FADE;
    float above = (p.y - (top - INTERIOR_SKY_WINDOW_FADE))/INTERIOR_SKY_WINDOW_FADE;
    float fade = max(edge, max(below, above));
    return (fade < 1.0) ? max(fade, 0.0) : 1.0;
}

// A surface's visibility of the sky at the point p with the geometric normal n: the open shares of its march's bands (in
// azimuthCount directions, turned by `turn` of the step between two; its steps shifted out by `shift` of a step, so the
// pre-pass's pattern spreads where they cross the coarse texels and its blur averages that out) and of the zenith's cap,
// weighed by their solid angle and its cosine to them, as a share of all of them; faded to open toward the volume's
// edges (exactly 1 when nothing hides any of them).
float InteriorSkyVisibilityTurned(vec3 p, vec3 n, float turn, int azimuthCount)
{
    float fade = InteriorSkyEdgeFade(p);
    if (!skyVisibilityActive || (fade >= 1.0))
    {
        return 1.0;
    }
    float base = p.y + mix(INTERIOR_SKY_UPRIGHT_LIFT, INTERIOR_SKY_LIFT, clamp(n.y, 0.0, 1.0));
    // A surface facing up goes on around itself as its plane: how fast that plane rises, at its steepest.
    bool isSloped = n.y > INTERIOR_SKY_SLOPE_UP;
    float maxRise = isSloped ? min(length(n.xz)/n.y, INTERIOR_SKY_MAX_RISE) : 0.0;
    float bandStep = radians(INTERIOR_SKY_BAND_DEGREES);
    // The bands' edges, as the tangents of their elevations; each band's middle (its cosine and sine) and its solid
    // angle per unit of azimuth.
    float tangents[INTERIOR_SKY_BANDS + 1];
    vec2 middles[INTERIOR_SKY_BANDS];
    float spans[INTERIOR_SKY_BANDS];
    for (int band = 0; band < INTERIOR_SKY_BANDS; band++)
    {
        tangents[band] = tan(float(band)*bandStep);
        float middle = (float(band) + 0.5)*bandStep;
        middles[band] = vec2(cos(middle), sin(middle));
        spans[band] = sin(float(band + 1)*bandStep) - sin(float(band)*bandStep);
    }
    tangents[INTERIOR_SKY_BANDS] = tan(float(INTERIOR_SKY_BANDS)*bandStep);
    // The slice the surface's own height lies in, where it lies on the map, and the window's top.
    int ownSlice = int(floor((p.y - skyVolumeBottom)/INTERIOR_SKY_SLICE_HEIGHT));
    vec2 uvAt = InteriorSkyUv(p.xz);
    float windowTop = skyVolumeBottom + float(INTERIOR_SKY_SLICE_COUNT)*INTERIOR_SKY_SLICE_HEIGHT;
    float open = 0.0;
    float total = 0.0;
    for (int azimuth = 0; azimuth < azimuthCount; azimuth++)
    {
        float angle = ((float(azimuth) + turn)/float(azimuthCount))*INTERIOR_TWO_PI;
        vec2 direction = vec2(cos(angle), sin(angle));
        vec2 uvAlong = InteriorSkyUv(p.xz + direction) - uvAt;
        // How fast the surface's plane rises along this direction (uphill only).
        float uphill = isSloped ? max(-dot(n.xz, direction), 0.0)/n.y : 0.0;
        // The bands the surface faces (its cosine to their middle above 0: their weights are taken after the march), and
        // the tangents of the elevations each one's blockers span, lowest and highest (none yet).
        float lowest[INTERIOR_SKY_BANDS];
        float highest[INTERIOR_SKY_BANDS];
        uint weighted = 0u;
        for (int band = 0; band < INTERIOR_SKY_BANDS; band++)
        {
            vec3 toBand = vec3(middles[band].x*direction.x, middles[band].y, middles[band].x*direction.y);
            weighted |= (dot(n, toBand) > 0.0) ? (1u << uint(band)) : 0u;
            lowest[band] = INTERIOR_SKY_NO_BLOCKER;
            highest[band] = -INTERIOR_SKY_NO_BLOCKER;
        }
        uint closed = 0u;
        // The tangents the bands still open span, lowest and highest (again whenever one closes; every band loop runs
        // over all the bands, so the compiler keeps their arrays in registers).
        float lowTangent = INTERIOR_SKY_NO_BLOCKER;
        float highTangent = 0.0;
        for (int band = 0; band < INTERIOR_SKY_BANDS; band++)
        {
            bool isWeighted = (weighted & (1u << uint(band))) != 0u;
            lowTangent = isWeighted ? min(lowTangent, tangents[band]) : lowTangent;
            highTangent = isWeighted ? max(highTangent, tangents[band + 1]) : highTangent;
        }
        // Through the texels the direction crosses, one after another (no wall between two reads), at the level whose
        // texels are INTERIOR_SKY_CELL_RATIO of the distance wide: each read over the distances it spans, nearest to
        // farthest.
        float nearest = INTERIOR_SKY_MARCH_START;
        for (int cell = 0; (cell < INTERIOR_SKY_MAX_CELLS) && (nearest < INTERIOR_SKY_MARCH_END)
            && ((closed & weighted) != weighted); cell++)
        {
            InteriorSkyCell current = InteriorSkyCellAt(uvAt, uvAlong, nearest);
            if (!current.isOnMap)
            {
                break; // off the map: nothing known beyond, open
            }
            float from = nearest;
            float farthest = current.farthest;
            float texel = current.texel;
            uvec4 column = current.column;
            // The surface's own plane there, as high as it reaches across the texel.
            float plane = uphill*farthest + maxRise*texel*1.41421356;
            // What is solid over the heights the bands still open pass there, above that plane: mostly nothing (masked
            // below the range; its runs stop at the range's top). Where the plane reaches the window's top slice, what
            // is solid there may be the surface's own ground going on up: nothing is known.
            int firstSlice;
            int lastSlice;
            bool hasRange = (base + plane < windowTop - INTERIOR_SKY_SLICE_HEIGHT)
                && InteriorSliceRange(base + max(from*lowTangent, plane), base + farthest*highTangent, firstSlice,
                lastSlice);
            uvec4 solid = hasRange ? (column & ~InteriorSlicesBelow(firstSlice)) : uvec4(0u);
            // On into the next texel after this one, just past its side (a hair of the texel: never stuck on it).
            nearest = farthest + 1e-3*texel;
            if (all(equal(solid, uvec4(0u))))
            {
                continue; // nothing here
            }
            // Its runs of solid slices, each hiding the elevations from its bottom seen from the farthest of those
            // distances to its top seen from the nearest (a column solid at the window's top goes on up) in the bands
            // they reach into; past the first few, the rest count as one.
            uvec4 starts = solid & ~((solid << 1u) | uvec4(0u, solid.xyz >> 31u));
            uvec4 ends = solid & ~((solid >> 1u) | uvec4(solid.yzw << 31u, 0u));
            for (int run = 0; (run < INTERIOR_SKY_MAX_RUNS) && any(notEqual(starts, uvec4(0u))); run++)
            {
                uvec4 startBit = InteriorLowestBit(starts);
                uvec4 endBit = InteriorLowestBit(ends);
                int first = InteriorLowestSlice(startBit);
                if (first > lastSlice)
                {
                    break; // above the range
                }
                int last = InteriorLowestSlice(endBit);
                if (run == INTERIOR_SKY_MAX_RUNS - 1)
                {
                    int ignored;
                    InteriorFindSolidSlices(solid & InteriorSlicesBelow(lastSlice + 1), ignored, last);
                }
                last = min(last, lastSlice);
                starts &= ~startBit;
                ends &= ~endBit;
                if (InteriorUnderfoot(column, last, ownSlice))
                {
                    continue; // the top of what the surface stands over: no wall
                }
                float bottom = (skyVolumeBottom + float(first)*INTERIOR_SKY_SLICE_HEIGHT - base)/farthest;
                float top = (last == INTERIOR_SKY_SLICE_COUNT - 1) ? INTERIOR_SKY_NO_BLOCKER
                    : (skyVolumeBottom + float(last + 1)*INTERIOR_SKY_SLICE_HEIGHT - base)/from;
                uint wasClosed = closed;
                for (int band = 0; band < INTERIOR_SKY_BANDS; band++)
                {
                    uint bit = 1u << uint(band);
                    if (((weighted & ~closed & bit) == 0u) || !(top > tangents[band]) || !(bottom < tangents[band + 1]))
                    {
                        continue;
                    }
                    lowest[band] = min(lowest[band], bottom);
                    highest[band] = max(highest[band], top);
                    bool isWhole = (lowest[band] <= tangents[band]) && (highest[band] >= tangents[band + 1]);
                    closed |= isWhole ? bit : 0u;
                }
                if (closed != wasClosed)
                {
                    lowTangent = INTERIOR_SKY_NO_BLOCKER;
                    highTangent = 0.0;
                    for (int band = 0; band < INTERIOR_SKY_BANDS; band++)
                    {
                        bool isOpen = (weighted & ~closed & (1u << uint(band))) != 0u;
                        lowTangent = isOpen ? min(lowTangent, tangents[band]) : lowTangent;
                        highTangent = isOpen ? max(highTangent, tangents[band + 1]) : highTangent;
                    }
                }
            }
        }
        // Each band's weight: its solid angle per unit of azimuth times the surface's cosine to its middle.
        for (int band = 0; band < INTERIOR_SKY_BANDS; band++)
        {
            vec3 toBand = vec3(middles[band].x*direction.x, middles[band].y, middles[band].x*direction.y);
            float weight = max(dot(n, toBand), 0.0)*spans[band];
            total += weight;
            open += weight*InteriorBandOpenShare(tangents[band], tangents[band + 1], lowest[band], highest[band]);
        }
    }
    // The zenith's cap above the bands, open when nothing lies straight above: weighed as the bands are, plus a floor so
    // a surface facing down takes its openness.
    float cap = float(azimuthCount)*((1.0 - sin(float(INTERIOR_SKY_BANDS)*bandStep))*max(n.y, 0.0)
        + INTERIOR_SKY_ZENITH_FLOOR);
    total += cap;
    open += InteriorIsOpenOverhead(p, n, base, ownSlice) ? cap : 0.0;
    float visibility = open/total;
    return visibility + (1.0 - visibility)*fade;
}

// A surface's visibility of the sky as a lit shader reads it itself, where the pre-pass holds none of its own (blended
// sprites, the crisp overlay, a frame without the pre-pass): open or covered by what the volume holds straight above it,
// read as the zenith's cap is (an upright surface's just in front of it); faded to open toward the volume's edges. One
// read: it knows a roof, not the sky that comes in sideways.
float InteriorSkyOverhead(vec3 p, vec3 n)
{
    float fade = InteriorSkyEdgeFade(p);
    if (!skyVisibilityActive || (fade >= 1.0))
    {
        return 1.0;
    }
    float base = p.y + mix(INTERIOR_SKY_UPRIGHT_LIFT, INTERIOR_SKY_LIFT, clamp(n.y, 0.0, 1.0));
    int ownSlice = int(floor((p.y - skyVolumeBottom)/INTERIOR_SKY_SLICE_HEIGHT));
    return InteriorIsOpenOverhead(p, n, base, ownSlice) ? 1.0 : fade;
}

// The share of the sky's light a surface takes at a visibility: exactly all of it in the open, INTERIOR_SKY_FLOOR
// fully covered.
float InteriorSkyShare(float visibility)
{
    return 1.0 - (1.0 - INTERIOR_SKY_FLOOR)*(1.0 - clamp(visibility, 0.0, 1.0));
}

// x unless its size is below INTERIOR_MIN_DIRECTION, which keeps its sign (0 counts as positive): no slab divides by 0.
float InteriorAwayFromZero(float x)
{
    return (abs(x) >= INTERIOR_MIN_DIRECTION) ? x : ((x < 0.0) ? -INTERIOR_MIN_DIRECTION : INTERIOR_MIN_DIRECTION);
}

// How long the camera's line of sight to a point `toPoint` metres away along the unit `direction` lies inside a zone.
float InteriorZonePathLength(int zone, vec3 direction, float toPoint)
{
    vec3 start = -ambientZoneCenters[zone].xyz;
    if (ambientZoneCenters[zone].w > 0.5)
    {
        float along = dot(start, direction);
        float radius = ambientZoneAxisX[zone].w;
        float discriminant = along*along - (dot(start, start) - radius*radius);
        if (!(discriminant > 0.0))
        {
            return 0.0;
        }
        float root = sqrt(discriminant);
        return max(min(-along + root, toPoint) - max(-along - root, 0.0), 0.0);
    }
    vec3 axisX = ambientZoneAxisX[zone].xyz;
    vec3 axisY = ambientZoneAxisY[zone].xyz;
    vec3 axisZ = ambientZoneAxisZ[zone].xyz;
    vec3 localStart = vec3(dot(start, axisX), dot(start, axisY), dot(start, axisZ));
    vec3 localDirection = vec3(InteriorAwayFromZero(dot(direction, axisX)),
        InteriorAwayFromZero(dot(direction, axisY)), InteriorAwayFromZero(dot(direction, axisZ)));
    vec3 halves = vec3(ambientZoneAxisX[zone].w, ambientZoneAxisY[zone].w, ambientZoneAxisZ[zone].w);
    vec3 inverse = 1.0/localDirection;
    vec3 a = (-halves - localStart)*inverse;
    vec3 b = (halves - localStart)*inverse;
    vec3 nearT = min(a, b);
    vec3 farT = max(a, b);
    float tNear = max(nearT.x, max(nearT.y, nearT.z));
    float tFar = min(farT.x, min(farT.y, farT.z));
    return max(min(tFar, toPoint) - max(tNear, 0.0), 0.0);
}

// The distance the haze in front of a point `toPoint` metres away (render space p) is read at: less each of the draw's
// zones' part of the line of sight times (1 - its haze), at least 0. Exactly `toPoint` while no zone applies.
float InteriorHazeDistance(vec3 p, float toPoint)
{
    int count = min(ambientZoneCount, MAX_AMBIENT_ZONES);
    if (!interiorActive || (count <= 0) || !(toPoint > 0.0))
    {
        return toPoint;
    }
    vec3 direction = p/toPoint;
    float hazed = toPoint;
    for (int zone = 0; zone < count; zone++)
    {
        hazed -= InteriorZonePathLength(zone, direction, toPoint)*(1.0 - ambientZoneSettings[zone].y);
    }
    return max(hazed, 0.0);
}

// The debug view in place of a lit surface's colour while it is on: the share of the sky's light the surface takes
// (grey) at its visibility, plus the zones' light's hue where they cover it, shown at its value whatever the exposure.
vec3 InteriorViewColor(vec3 color, vec3 p, vec3 n, float skyVisibility)
{
    if (!interiorActive || (interiorView == 0))
    {
        return color;
    }
    InteriorZoneLight zones = InteriorZonesAt(p, n);
    float share = InteriorSkyShare(skyVisibility)*zones.skyScale;
    float brightest = max(zones.ambient.r, max(zones.ambient.g, zones.ambient.b));
    vec3 hue = (brightest > 0.0) ? zones.ambient/brightest : vec3(0.0);
    return (vec3(share) + 0.5*zones.weight*hue)*(INTERIOR_VIEW_LEVEL*interiorViewScale);
}
