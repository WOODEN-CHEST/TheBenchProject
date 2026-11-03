// The terrain's shadows at any distance, for #include "common/far_shadow" (common/lighting includes it): the lit
// shaders' side of the far shadows (source/WorldFarShadows.h; the renderer's pass is WorldRendererFarShadows.c).
//
// Every terrain's top surface is drawn from above into nested square height maps around the camera (8, 32 and 128 km at
// the default quality), and swept along the shadow light: each texel holds the heights below which it lies in the
// terrain's shadow for the top and the bottom edge of the light's disc. A point lit by the levels reads the finest map
// that holds it: above the bottom edge's height the whole disc shows, below the top edge's none of it, between them the
// share of a disc above a line (the real penumbra: a ridge 15 km away gives an edge about 140 m soft). Its place and
// height come from its flat render-space position, moved into the maps' frame and lowered by its drop from the frame's
// centre (the planet's curve, as the maps' heights are). Casters within a few of a map's texels of the point are left
// out (the shadow levels draw those, and a point's own texel would shade it). Over the outer part of each map the shadow
// blends into the next map's, the last map's into none. WorldFarShadows_GetLookup is the C mirror of the choice,
// WorldFarShadows_GetShadow of the share.

#include "common/math"

uniform bool farShadowActive;          // false: no finished sweep this frame (nothing is read)
uniform sampler2DArray farShadowMaps;  // a layer per map: the shadow heights of the disc's top (R) and bottom (G) edges
uniform vec3 farShadowOffset;          // the camera's world position less the maps' frame's centre (metres)
uniform vec3 farShadowHalfWidths;      // each map's half side (metres)
uniform float farShadowCurvature;      // the curve the maps' heights were lowered with (1 / 2R, or 0)
uniform float farShadowEdge;           // a map is read up to this share of its half side
uniform float farShadowBandStart;      // and blends into the next map from this share on

// The nested maps (WORLD_FAR_SHADOW_MAP_COUNT).
const int FAR_SHADOW_MAP_COUNT = 3;

// The share of a light's disc above a line through it: 0 at its top edge (fraction 0) to 1 at its bottom edge (1).
float FarShadowDiscShare(float fraction)
{
    float c = clamp(1.0 - 2.0*fraction, -1.0, 1.0);
    return (acos(c) - c*sqrt(max(1.0 - c*c, 0.0)))/PI;
}

// One map's shadow at a place of the frame (metres from its centre) and a height there: 0 (lit) to 1. The read sits in a
// branch (neighbouring pixels may read other maps): the maps have no mipmaps, so its implicit derivatives do not matter.
float FarShadowFromMap(int map, vec2 place, float height)
{
    vec2 uv = place/(2.0*farShadowHalfWidths[map]) + 0.5;
    vec2 edges = texture(farShadowMaps, vec3(uv, float(map))).rg;
    float fraction = (height - edges.x)/max(edges.y - edges.x, 1e-3);
    return 1.0 - FarShadowDiscShare(fraction);
}

// The far shadow at a render-space point (the flat world's): 0 (lit) to 1 (in the terrain's shadow), from the finest map
// that holds it, blended across its band into the next map's (or past the last map into none).
float FarShadowAt(vec3 worldPos)
{
    if (!farShadowActive)
    {
        return 0.0;
    }
    vec3 place = worldPos + farShadowOffset;
    float height = place.y - dot(place.xz, place.xz)*farShadowCurvature;
    float reach = max(abs(place.x), abs(place.z));
    for (int map = 0; map < FAR_SHADOW_MAP_COUNT; map++)
    {
        float share = reach/farShadowHalfWidths[map];
        if (share <= farShadowEdge)
        {
            float shadow = FarShadowFromMap(map, place.xz, height);
            float next = clamp((share - farShadowBandStart)/max(farShadowEdge - farShadowBandStart, 1e-6), 0.0, 1.0);
            if (next > 0.0)
            {
                shadow = mix(shadow, ((map + 1) < FAR_SHADOW_MAP_COUNT) ? FarShadowFromMap(map + 1, place.xz, height)
                    : 0.0, next);
            }
            return shadow;
        }
    }
    return 0.0;
}
