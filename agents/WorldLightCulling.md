# WorldLightCulling

Selects the handful of point and spot lights that most affect a given object, so a forward renderer can shade with a
small fixed number of lights while a world may hold hundreds.

## Files
- `include/WorldLightCulling.h` — public API.
- `source/WorldLightCulling.c` — implementation (pure selection logic over an array; no rendering or world
  dependency, so it is headless-testable: `tests/WorldLightCullingTests.c`).

## API
- `#define WORLD_MAX_FORWARD_LIGHTS 8` — the cap on lights shaded per object. The lit shaders
  (`compile/out/asset/shaders/common/lighting.glsl`) declare their forward-light arrays with `MAX_FORWARD_LIGHTS`
  entries, which should equal it (`tests/ShaderSourceTests.c` checks). A mismatch is safe: the shaders clamp their loops
  to their arrays, and the renderer checks the arrays when it loads a shader, logs if they hold fewer, and never uploads
  more than fit.
- `WorldCullingLight` — `{ Position, Range, Intensity, IsSpot, Direction, CosOuter }`: a light as the culling sees it. A
  spot (`IsSpot`) shines along `Direction` (unit) within its outer angle (`CosOuter`, its cosine, in (0, 1): spots stay
  below 90 degrees); a zero-initialized light is a point. The caller keeps any other per-light data in a parallel array
  with the same indices.
- `bool WorldLightCulling_DoesLightReachSphere(const WorldCullingLight* light, Vector3 center, float radius)` — whether
  the light may light some part of the sphere: its reach sphere overlaps it (distance from the light to the sphere's
  surface `< range`) and, for a spot, the sphere reaches into its cone and in front of its apex. Ignores the intensity.
- `size_t WorldLightCulling_SelectForSphere(const WorldCullingLight* lights, size_t lightCount, Vector3 center,
  float radius, size_t* outIndices, size_t capacity, size_t* outReachingCount)` — keeps the lights with positive range
  and intensity that reach the sphere, ranks them by influence, and writes the indices of the strongest, up to
  `min(capacity, WORLD_MAX_FORWARD_LIGHTS)`, most influential first. Returns the count; `outReachingCount` (may be NULL)
  receives how many reached the sphere, also past the capacity. Does not allocate.

## The cone test
In the cone's frame (the apex at the origin, `along` the distance along the axis, `across` the distance from it), a
sphere's centre lies outside the infinite cone by `across cos(outer) - along sin(outer)`. That never exceeds its true
distance (behind the apex the apex itself is nearer than the cone's surface), so the test may keep a sphere near the apex
but never drops one the cone reaches. A sphere wholly behind the apex's plane (`along < -radius`) is dropped: the cone
opens less than 90 degrees, so nothing behind that plane is in it.

## Influence metric
`influence = intensity * (1 - surfaceDistance / range)`, where `surfaceDistance = max(0, distance(light, center)
- radius)`, for points and spots alike. Brighter lights and lights whose reach penetrates deeper into the object's
sphere rank higher; a light whose reach only grazes the sphere ranks lowest; a light that does not reach it at all is
excluded.

## How the renderer uses it
Once per frame, `WorldRendererLighting_PrepareFrame` builds the candidates from the render cache's lights
(`WorldRendererLighting_BuildForwardLights`): every light that is on and lights anything except the one casting the
cube shadow (the shader shades that light separately, with its shadow), each with its shading values: its linear
radiance (colour → linear × intensity × its flicker this frame; a light that flickers to darkness is left out), a
spot's axis and its cone's falloff, and a shadowed spot's map layer. For each drawn object (a model, a sprite, a
terrain chunk, a cell of model instances), `WorldRendererLighting_UploadObjectLights` culls the candidates against its
bounding sphere and uploads the selected ones (`WorldRendererLighting_PackForwardLights`). The lights and the spheres
are in render space (relative to the camera: the render cache's `RenderPosition` and `RenderCenter`, see
WorldRuntime.md, "Render space"); the culling itself works in any space its inputs share. The shader shades each with
the same Cook-Torrance BRDF as the sun, with a windowed inverse-square falloff that reaches 0 at the light's range and a
spot's smooth cone, and darkens a shadowed spot by its map. The cost is O(lights) per drawn object. When more lights
reach a draw than it takes, its object is named in the log once (`WorldRenderer: 11 lights reach a draw of object 51
("terrain"), more than the 8 one draw takes; ...`), at most 32 objects, so a scene that needs more lights per draw
shows up. The visibility uses `WorldLightCulling_DoesLightReachSphere` for the casters a shadowed spot's map draws.

## Notes / future
- The bounding sphere is conservative (over-includes), so a light that reaches any part of a large object is
  kept; the shader's per-fragment attenuation handles the actual falloff.
- A spatial index for lights (and objects), or clustered lighting (lights per screen tile rather than per draw), is
  future work.
