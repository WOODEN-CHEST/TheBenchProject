// Interior lighting, for #include "common/interior" (common/lighting includes it): what keeps the sky's light out of
// rooms, and what lights them instead. Its C mirror is source/WorldInterior.{h,c} (tests/ShaderSourceTests.c pins the
// constants the two share).
//
// THE SKY'S VISIBILITY. A top-down depth map around the camera holds the topmost surface of everything that keeps the sky
// out (roofs, walls, tree canopies, the terrain where it overhangs). A surface reads it around the place it faces (in
// front of a steep surface, whose sky lies on its side; the point itself on a flat one), placed by its geometric normal,
// not a normal map's (it never jitters with a texture's bumps): a centre tap with an inner ring of taps close around it,
// and an outer ring further out. A tap is covered when the map holds something above the surface there: above both the
// surface's own height and its tangent plane extended to the tap, plus a bias (so the uphill side of a slope is no roof,
// nor is a curb); a surface facing down is covered by anything at or above its own height, itself too (a roof's
// underside lies under the roof). The visibility is the open share of the inner taps (with the centre) times the open
// share of the ring: the ring tells how much sky lies around the place, the inner taps whether the place itself is open.
// So a room's floor, under its roof, keeps none of the sky beyond its walls (the map cannot tell a wall from an open side
// under the same roof), while a facade under its eaves, whose place lies in front of them, keeps most of it; at a roof's
// edge the inner taps soften the step. The map sees only what is overhead: light that comes in sideways, through doors
// and windows, is not in it. The sky's ambient takes InteriorSkyShare of the visibility, never less than
// INTERIOR_SKY_FLOOR (that light from doors and windows), and so do the ground's bounce and the night's own light: a
// roof keeps out the light from the sunlit ground around it as much as the sky's. Toward the map's edge the visibility
// fades to open (the map moves with the camera), and outside it everything is open.
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
#define INTERIOR_SKY_INNER_TAPS 4
#define INTERIOR_SKY_RING_TAPS 8
#define INTERIOR_SKY_TAP_COUNT (1 + INTERIOR_SKY_INNER_TAPS + INTERIOR_SKY_RING_TAPS)
const float INTERIOR_SKY_INNER_RADIUS = 0.25;
const float INTERIOR_SKY_RING_RADIUS = 1.5;
const float INTERIOR_SKY_BIAS = 0.25;         // facing up (curbs, pebbles, a slope's bumps are no roof)
const float INTERIOR_SKY_UPRIGHT_BIAS = 0.05; // upright (its taps lie in front of it, away from itself)
const float INTERIOR_SKY_FLOOR = 0.15;
const float INTERIOR_SKY_MIN_UP = 0.001;
const float INTERIOR_SKY_SELF = 0.02;
const float INTERIOR_SKY_DOWN_SCALE = 2.0;
// Toward the map's edge the visibility fades to open, over INTERIOR_SKY_FADE_WIDTH metres from the camera (horizontally)
// up to INTERIOR_SKY_FADE_END, where every tap still lies inside the map: no hard edge sweeps over shaded ground.
const float INTERIOR_SKY_FADE_END = 45.0;
const float INTERIOR_SKY_FADE_WIDTH = 12.0;
// How far beyond a zone's surface a surface still counts as on it (a room's walls, built on it, within rounding).
const float INTERIOR_ZONE_SURFACE_TOLERANCE = 0.01;
// The debug view shows a surface that takes all of the sky's light at this level on screen (a light grey after the
// tonemap; the floor's share a dark grey).
const float INTERIOR_VIEW_LEVEL = 0.4;
const float INTERIOR_MIN_DIRECTION = 1e-8;
// The taps' offsets from the place a surface faces, per unit of their ring's radius: the centre, the inner ring (the
// diagonals), then the outer ring from +X toward +Z.
const vec2 INTERIOR_SKY_TAPS[INTERIOR_SKY_TAP_COUNT] = vec2[]
(
    vec2(0.0, 0.0),
    vec2(0.70710678, 0.70710678), vec2(-0.70710678, 0.70710678), vec2(-0.70710678, -0.70710678),
    vec2(0.70710678, -0.70710678),
    vec2(1.0, 0.0), vec2(0.70710678, 0.70710678), vec2(0.0, 1.0), vec2(-0.70710678, 0.70710678),
    vec2(-1.0, 0.0), vec2(-0.70710678, -0.70710678), vec2(0.0, -1.0), vec2(0.70710678, -0.70710678)
);

uniform bool interiorActive;               // interior lighting runs this frame (false: every ambient as in the open)
uniform bool skyVisibilityActive;          // the map holds this frame's roofs (false: every surface sees the sky)
uniform sampler2D skyVisibilityMap;        // depth from above (0 at the map's top), the topmost surface at each texel
uniform mat4 skyVisibilityViewProjection;  // render space -> the map's clip space (orthographic, looking down)
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

// Whether the map holds something above a surface at the tap `at` (render space x, z): above the height it must top
// there (the surface's own or its tangent plane's plus a bias by its facing, up or upright; just below its own, facing
// down). `base` is
// the surface's point through the map's view-projection; outside the map, or below its box (it cannot tell), nothing is.
bool InteriorIsSkyCovered(vec3 p, vec3 n, vec2 at, vec4 base, float up, float down)
{
    float rise = -dot(n.xz, at - p.xz)/up;
    float bias = mix(INTERIOR_SKY_UPRIGHT_BIAS, INTERIOR_SKY_BIAS, clamp(n.y, 0.0, 1.0));
    float reference = mix(p.y + max(rise, 0.0) + bias, p.y - INTERIOR_SKY_SELF, down);
    vec4 clip = base + skyVisibilityViewProjection[0]*(at.x - p.x) + skyVisibilityViewProjection[1]*(reference - p.y)
        + skyVisibilityViewProjection[2]*(at.y - p.z);
    vec2 uv = clip.xy*0.5 + 0.5;
    float depth = clip.z*0.5 + 0.5;
    return (uv.x >= 0.0) && (uv.x <= 1.0) && (uv.y >= 0.0) && (uv.y <= 1.0) && (depth <= 1.0)
        && (textureLod(skyVisibilityMap, uv, 0.0).r < depth);
}

// A surface's visibility of the sky at the point p with the geometric normal n: the open share of the centre and the
// inner taps times the open share of the outer ring, faded to open toward the map's edge (exactly 1 when every tap is
// open).
float InteriorSkyVisibility(vec3 p, vec3 n)
{
    float edge = clamp((length(p.xz) - (INTERIOR_SKY_FADE_END - INTERIOR_SKY_FADE_WIDTH))/INTERIOR_SKY_FADE_WIDTH, 0.0,
        1.0);
    if (!skyVisibilityActive || (edge >= 1.0))
    {
        return 1.0;
    }
    vec2 center = p.xz + n.xz*INTERIOR_SKY_RING_RADIUS;
    float up = max(abs(n.y), INTERIOR_SKY_MIN_UP);
    float down = clamp(-INTERIOR_SKY_DOWN_SCALE*n.y, 0.0, 1.0);
    vec4 base = skyVisibilityViewProjection*vec4(p, 1.0);
    float nearOpen = 0.0;
    float ringOpen = 0.0;
    for (int tap = 0; tap < INTERIOR_SKY_TAP_COUNT; tap++)
    {
        bool isInner = (tap <= INTERIOR_SKY_INNER_TAPS);
        vec2 at = center + INTERIOR_SKY_TAPS[tap]*(isInner ? INTERIOR_SKY_INNER_RADIUS : INTERIOR_SKY_RING_RADIUS);
        float open = InteriorIsSkyCovered(p, n, at, base, up, down) ? 0.0 : 1.0;
        nearOpen += isInner ? open : 0.0;
        ringOpen += isInner ? 0.0 : open;
    }
    float visibility = (nearOpen/float(1 + INTERIOR_SKY_INNER_TAPS))*(ringOpen/float(INTERIOR_SKY_RING_TAPS));
    return visibility + (1.0 - visibility)*edge;
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
// (grey), plus the zones' light's hue where they cover it, shown at its value whatever the exposure.
vec3 InteriorViewColor(vec3 color, vec3 p, vec3 n)
{
    if (!interiorActive || (interiorView == 0))
    {
        return color;
    }
    InteriorZoneLight zones = InteriorZonesAt(p, n);
    float share = InteriorSkyShare(InteriorSkyVisibility(p, n))*zones.skyScale;
    float brightest = max(zones.ambient.r, max(zones.ambient.g, zones.ambient.b));
    vec3 hue = (brightest > 0.0) ? zones.ambient/brightest : vec3(0.0);
    return (vec3(share) + 0.5*zones.weight*hue)*(INTERIOR_VIEW_LEVEL*interiorViewScale);
}
