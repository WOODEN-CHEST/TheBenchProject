# WorldLightCulling

Selects the handful of point lights that most affect a given object, so a forward renderer can shade with a
small fixed number of lights while a world may hold hundreds.

## Files
- `include/WorldLightCulling.h` — public API.
- `source/WorldLightCulling.c` — implementation (pure selection logic; no rendering dependency, so it is
  headless-testable).

## API
- `#define WORLD_MAX_FORWARD_LIGHTS 8` — the hard cap on lights shaded per object. **MUST equal the
  `MAX_POINT_LIGHTS` constant in the PBR fragment shader** (`compile/out/asset/shaders/pbr/fragment.frag`),
  which declares point-light uniform arrays of exactly this length.
- `size_t WorldLightCulling_SelectForSphere(World* world, Vector3 center, float radius,
   const WorldLight** outLights, size_t capacity)` — walks every object in `world`, keeps only
  `WorldObjectType_Light` objects with positive intensity and size whose reach sphere overlaps the sphere at
  `(center, radius)` (distance from the light to the sphere surface `< light size`), ranks them by influence,
  and writes the strongest up to `min(capacity, WORLD_MAX_FORWARD_LIGHTS)` into `outLights`, most-influential
  first. Returns the count. The returned pointers are **borrowed** into the world (valid until its object set
  changes).

## Influence metric
`influence = intensity * (1 - surfaceDistance / size)`, where `surfaceDistance = max(0, distance(light, center)
- radius)`. So brighter lights and lights whose reach penetrates deeper into the object's sphere rank higher;
a light whose reach only grazes the sphere ranks lowest; a light that does not reach it at all is excluded.

## How the renderer uses it (WorldRenderer)
Per model object in the scene (PBR) pass, the renderer computes the object's **world bounding sphere** (local
mesh AABB — cached per asset to avoid re-scanning vertices every frame — transformed by the object's world
matrix, radius scaled by the matrix's max axis scale), calls `SelectForSphere`, converts each selected light's
colour to linear × intensity, and uploads position/radiance/range arrays + the count to the PBR shader before
`DrawModel`. The shader forward-shades each with the same Cook-Torrance BRDF as the sun, using a windowed
inverse-square falloff that reaches 0 at the light's range (so the shaded set matches what was culled). Per-
light shadows are still deferred (`WorldLight.CastsShadows` is stored but unused by the shader).

## Notes / future
- The per-asset bounds cache is keyed by `GameModel*` (stable per asset) with `BOUNDS_CACHE_CAPACITY` slots;
  beyond that, uncached assets recompute bounds each frame (still correct, just slower).
- The bounding sphere is conservative (over-includes) so a light that reaches any part of a large object is
  kept, and the shader's per-fragment attenuation handles the actual spatial falloff.
