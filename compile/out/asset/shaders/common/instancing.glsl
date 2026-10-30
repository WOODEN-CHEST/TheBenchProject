// The copies of an instanced draw, for #include "common/instancing" in the instanced twins' vertex stages, whose
// definitions define WORLD_INSTANCED (common/dither then reads the copy's dither from the flat outputs below). One draw is
// one LOD of one cell of a model instances component: raylib's matrices place the cell (matModel: the object's world
// matrix times the translation to the cell's centre, in render space; mvp: that times the pass's view and projection),
// and each copy's record (source/WorldInstancing.h, WorldInstanceRecord) comes as instanced attributes: the first three
// rows of its matrix relative to the cell's centre, its tint and a stable random value.
//
// Per copy, the vertex stage works out what source/WorldInstancing.c works out on the CPU, its C mirror for the tests and
// the A/B path (tests/ShaderSourceTests.c pins this file's constants to it):
// - its sphere: the world model's (instanceBoundsCenter, instanceBoundsRadius) under its matrix (the radius times its
//   longest axis), placed by matModel; its LOD distance is the camera's (render space's origin) to the centre, its fade
//   distance to the nearest point, both times the zoom factor;
// - its fade: the distance fade over the last band of the draw distance in effect, against 1 - its opacity (the
//   object's times its tint's alpha): the larger counts; in the shadow passes also its shadow distance's fade (the same
//   rule), and none of it (the copy left out) when its sphere is smaller than the pass's size cut;
// - its LOD: before a band, the band's outgoing LOD; inside it both, the incoming one's share rising linearly across it;
//   in the shadow passes at least the LOD it would show at the pass's LOD distance (a far shadow level's), or the
//   component's own shadow LOD;
// - whether this draw (its pass, its LOD) shows it, and with which dither (WorldInstancing_GetDraw).
// A copy this draw does not show gets every vertex at one point outside the clip volume: its triangles are degenerate and
// make no fragments.
//
// Attribute locations (agents/WorldRuntime.md, "Vertex attributes"): 9 to 11 the rows, 12 the tint, 13 the random value.

#ifndef WORLD_INSTANCED
#error common/instancing needs WORLD_INSTANCED in its shader definition's defines
#endif

layout(location = 9) in vec4 instanceRow0;
layout(location = 10) in vec4 instanceRow1;
layout(location = 11) in vec4 instanceRow2;
layout(location = 12) in vec4 instanceTint;    // normalized bytes: red, green and blue multiply its colour, alpha is its opacity
layout(location = 13) in float instanceRandom; // stable per copy, in [0, 1); unused: for grass and wind, not built yet

// The passes (WorldInstancePass in source/WorldInstancing.h).
#define INSTANCE_PASS_LIT 0
#define INSTANCE_PASS_GBUFFER 1
#define INSTANCE_PASS_SHADOW 2
// The most LOD bands (WORLD_INSTANCE_MAX_BANDS), the share of its band from which the incoming LOD is the one a pass that
// draws one LOD draws (WORLD_LOD_SOLID_SHARE) and the fade from which the G-buffer leaves a copy out
// (WORLD_VISIBILITY_GBUFFER_FADE_LIMIT).
#define INSTANCE_MAX_BANDS 7
#define INSTANCE_SOLID_SHARE 0.5
#define INSTANCE_GBUFFER_FADE_LIMIT 0.5

uniform int instancePass;                            // the pass this program draws (INSTANCE_PASS_*), set once at load
uniform int instanceLod;                             // the LOD this draw draws
uniform int instanceLodCount;                        // the world model's LODs (1 to 8)
uniform int instanceForcedLod;                       // the LOD every copy shows, -1 when none is forced
uniform float instanceBandStart[INSTANCE_MAX_BANDS]; // band i hands a copy from LOD i to i + 1 between these zoomed
uniform float instanceBandEnd[INSTANCE_MAX_BANDS];   // distances (instanceLodCount - 1 bands)
uniform float instanceZoomFactor;                    // the camera's zoom factor: distances are multiplied by it
uniform float instanceDrawDistance;                  // the draw distance in effect (3.4e38 for none: nothing reaches it)
uniform float instanceFadeStart;                     // where its fade band starts
uniform float instanceFadeBand;                      // the band's length (above 0)
uniform float instanceObjectOpacity;                 // the object's opacity: every copy's is multiplied by it
uniform vec3 instanceBoundsCenter;                   // the world model's sphere in a copy's space
uniform float instanceBoundsRadius;                  // its radius, times the object's own stretch
uniform mat4 instanceLodTransform;                   // the LOD's import transform, applied before the copy's matrix
// The shadow passes' own (the other passes ignore them): the shadow distance in effect (3.4e38 for none), where its fade
// band starts and its length; the smallest copy the pass takes (its sphere's radius); the zoomed distance it picks LODs
// at (0: none); the LOD every copy casts with, -1 when none.
uniform float instanceShadowDistance;
uniform float instanceShadowFadeStart;
uniform float instanceShadowFadeBand;
uniform float instanceShadowMinRadius;
uniform float instanceShadowLodDistance;
uniform int instanceShadowLod;

flat out float fragDitherFade;
flat out float fragDitherLodProgress;
flat out float fragDitherIsIncomingLod;

// A point of the copy's own space moved by its matrix into the cell's space.
vec3 Instancing_MovePoint(vec3 point)
{
    vec4 p = vec4(point, 1.0);
    return vec3(dot(instanceRow0, p), dot(instanceRow1, p), dot(instanceRow2, p));
}

// A mesh vertex's position in the cell's space: the LOD's import transform, then the copy's matrix.
vec3 Instancing_MoveVertex(vec3 position)
{
    return Instancing_MovePoint((instanceLodTransform*vec4(position, 1.0)).xyz);
}

// A mesh normal turned like Instancing_MoveVertex turns the mesh: by the inverse transpose of the LOD's and the copy's
// linear parts, here their cofactors (the inverse transpose times the determinant) with the determinant's sign put back:
// the same direction, unnormalized. The caller turns it on by matNormal and normalizes it.
vec3 Instancing_TurnNormal(vec3 normal)
{
    mat3 m = transpose(mat3(instanceRow0.xyz, instanceRow1.xyz, instanceRow2.xyz))*mat3(instanceLodTransform);
    vec3 c0 = cross(m[1], m[2]);
    vec3 c1 = cross(m[2], m[0]);
    vec3 c2 = cross(m[0], m[1]);
    return ((dot(m[0], c0) < 0.0) ? -1.0 : 1.0)*(mat3(c0, c1, c2)*normal);
}

// The copy's draw-distance fade at a zoomed distance to its sphere's nearest point (WorldInstancing_GetDistanceFade).
float Instancing_GetDistanceFade(float distance)
{
    if (!(distance < instanceDrawDistance))
    {
        return 1.0;
    }
    if (distance <= instanceFadeStart)
    {
        return 0.0;
    }
    return min((distance - instanceFadeStart)/instanceFadeBand, 1.0);
}

// The copy's shadow-distance fade at a zoomed distance to its sphere's nearest point (the draw distance's rule).
float Instancing_GetShadowDistanceFade(float distance)
{
    if (!(distance < instanceShadowDistance))
    {
        return 1.0;
    }
    if (distance <= instanceShadowFadeStart)
    {
        return 0.0;
    }
    return min((distance - instanceShadowFadeStart)/instanceShadowFadeBand, 1.0);
}

// Whether this draw shows the copy (WorldInstancing_Evaluate, or WorldInstancing_EvaluateShadow in the shadow passes,
// then WorldInstancing_GetDraw for instancePass and instanceLod) and, when it does, writes its dither to the flat
// outputs. matModel is the cell's render-space matrix.
bool Instancing_IsShown(mat4 matModel)
{
    vec3 center = vec3(matModel*vec4(Instancing_MovePoint(instanceBoundsCenter), 1.0));
    float longest = max(max(length(vec3(instanceRow0.x, instanceRow1.x, instanceRow2.x)),
        length(vec3(instanceRow0.y, instanceRow1.y, instanceRow2.y))), length(vec3(instanceRow0.z, instanceRow1.z,
        instanceRow2.z)));
    float centerDistance = length(center);
    float radius = instanceBoundsRadius*longest;
    float lodDistance = centerDistance*instanceZoomFactor;
    float fadeDistance = max(centerDistance - radius, 0.0)*instanceZoomFactor;
    float opacity = clamp(instanceObjectOpacity*instanceTint.a, 0.0, 1.0);
    float fade = max(Instancing_GetDistanceFade(fadeDistance), 1.0 - opacity);
    int forcedLod = instanceForcedLod;
    if (instancePass == INSTANCE_PASS_SHADOW)
    {
        fade = max(fade, Instancing_GetShadowDistanceFade(fadeDistance));
        fade = (radius >= instanceShadowMinRadius) ? fade : 1.0;
        lodDistance = max(lodDistance, instanceShadowLodDistance);
        forcedLod = (instanceShadowLod >= 0) ? instanceShadowLod : forcedLod;
    }
    if (!(fade < 1.0))
    {
        return false;
    }

    int lod = instanceLodCount - 1;
    int incomingLod = lod;
    float progress = 0.0;
    bool isInBand = false;
    if (forcedLod >= 0)
    {
        lod = forcedLod;
        incomingLod = lod;
    }
    else
    {
        for (int band = 0; (band < INSTANCE_MAX_BANDS) && ((band + 1) < instanceLodCount); band++)
        {
            if (lodDistance < instanceBandStart[band])
            {
                lod = band;
                incomingLod = band;
                break;
            }
            if (lodDistance < instanceBandEnd[band])
            {
                lod = band;
                incomingLod = band + 1;
                isInBand = true;
                progress = (lodDistance - instanceBandStart[band])/(instanceBandEnd[band] - instanceBandStart[band]);
                break;
            }
        }
    }

    int solidLod = (isInBand && (progress >= INSTANCE_SOLID_SHARE)) ? incomingLod : lod;
    fragDitherFade = 0.0;
    fragDitherLodProgress = 0.0;
    fragDitherIsIncomingLod = 0.0;
    if (instancePass == INSTANCE_PASS_LIT)
    {
        // Both LODs of a band, each with its part of the dither.
        bool isIncoming = isInBand && (instanceLod == incomingLod);
        fragDitherFade = fade;
        fragDitherLodProgress = progress;
        fragDitherIsIncomingLod = isIncoming ? 1.0 : 0.0;
        return (instanceLod == lod) || isIncoming;
    }
    if (instancePass == INSTANCE_PASS_GBUFFER)
    {
        // The LOD with the larger share, solid, until mid-fade.
        return (instanceLod == solidLod) && (fade < INSTANCE_GBUFFER_FADE_LIMIT);
    }
    // The shadows: the LOD with the larger share, dithered by the copy's own shadow fade.
    fragDitherFade = fade;
    return instanceLod == solidLod;
}

// Where a copy this draw does not show puts all its vertices: one point outside the clip volume.
const vec4 INSTANCE_DROPPED_POSITION = vec4(2.0, 2.0, 2.0, 1.0);
