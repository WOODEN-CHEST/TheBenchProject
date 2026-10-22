# WorldLightCulling

Selects the handful of point lights that most affect a given object, so a forward renderer can shade with a
small fixed number of lights while a world may hold hundreds.

## Files
- `include/WorldLightCulling.h` — public API.
- `source/WorldLightCulling.c` — implementation (pure selection logic over an array; no rendering or world
  dependency, so it is headless-testable: `tests/WorldLightCullingTests.c`).

## API
- `#define WORLD_MAX_FORWARD_LIGHTS 8` — the cap on lights shaded per object. The PBR fragment shader
  (`compile/out/asset/shaders/pbr/fragment.frag`) declares its point-light arrays with `MAX_POINT_LIGHTS` entries,
  which should equal it. A mismatch is safe: the shader clamps its loop to its arrays, and the renderer checks the
  arrays when it loads the shader, logs if they hold fewer, and never uploads more than fit.
- `WorldCullingLight` — `{ Position, Range, Intensity }`: a light as the culling sees it. The caller keeps any other
  per-light data in a parallel array with the same indices.
- `size_t WorldLightCulling_SelectForSphere(const WorldCullingLight* lights, size_t lightCount, Vector3 center,
  float radius, size_t* outIndices, size_t capacity)` — keeps the lights with positive range and intensity whose
  reach sphere overlaps the sphere at `(center, radius)` (distance from the light to the sphere surface
  `< range`), ranks them by influence, and writes the indices of the strongest, up to
  `min(capacity, WORLD_MAX_FORWARD_LIGHTS)`, most influential first. Returns the count. Does not allocate.

## Influence metric
`influence = intensity * (1 - surfaceDistance / range)`, where `surfaceDistance = max(0, distance(light, center)
- radius)`. Brighter lights and lights whose reach penetrates deeper into the object's sphere rank higher; a light
whose reach only grazes the sphere ranks lowest; a light that does not reach it at all is excluded.

## How the renderer uses it
Once per frame, `WorldRendererLighting_PrepareFrame` builds the candidate array from the render cache's lights:
every light except the one casting the cube shadow (the shader shades that light separately, with its shadow),
plus a parallel array of linear radiance (colour → linear × intensity, once per light per frame). For each model
object drawn by a lit pass, `WorldRendererLighting_UploadObjectLights` culls the candidates against the object's
bounding sphere (from the render cache: the model's mesh-space bounds, computed once at load, transformed
by the object's matrix, radius scaled by the matrix's largest axis scale) and uploads the selected positions,
radiances and ranges. Since T32 the lights and the spheres are in render space (relative to the camera: the render
cache's `RenderPosition` and `RenderCenter`, see WorldRuntime.md, "Render space"); the culling itself works in any
space its inputs share. The shader shades each with the same Cook-Torrance BRDF as the sun, with a windowed
inverse-square falloff that reaches 0 at the light's range. The cost is O(lights) per drawn object.

## Notes / future
- The bounding sphere is conservative (over-includes), so a light that reaches any part of a large object is
  kept; the shader's per-fragment attenuation handles the actual falloff.
- A spatial index for lights (and objects) comes with the culling/LOD work, after the render cache.
