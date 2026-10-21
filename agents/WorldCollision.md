# World collision

Collision for the 3D world, built in T12 (decisions G13, G18–G20). The modules:
- **Shapes and layers:** `include/CollisionShape.h` with `source/CollisionShape.c`, `source/CollisionHull.c` and
  `source/CollisionMesh.c`; `include/CollisionLayer.h` with `source/CollisionLayer.c`.
- **The narrowphase**, all private:
  - `source/CollisionMath.{h,c}`: affine transforms, poses, closest points;
  - `source/CollisionGeometry.h`, the narrowphase's API and its tolerances;
  - `source/CollisionSupport.c`: support functions;
  - `source/CollisionGjk.c`: GJK, EPA and the linear cast;
  - `source/CollisionContact.c`: manifolds;
  - `source/CollisionRay.c`: rays and mesh queries;
  - `source/CollisionCast.c`: rotating casts and plane casts.
- **The collider component:** `include/WorldColliderComponent.h`, `source/WorldColliderComponent.c`.
- **A world model's collision:** the `collision` section, parsed and loaded by `source/WorldModelCollisionDefinition.c`
  (private header `source/WorldModelCollisionInternal.h`) and held in `WorldModel.h`'s `WorldModelCollision`.
- **The collision world:** `include/WorldCollision.h`. Its sources share the private `source/WorldCollisionInternal.h`:
  - `source/WorldCollision.c`: lifetime, sync, the debug list;
  - `source/WorldCollisionQuery.c`: the queries;
  - `source/WorldCollisionTrigger.c`: trigger events;
  - `source/WorldCollisionTree.{h,c}`: the dynamic AABB tree.
- **Debug wireframes:** `source/WorldRendererCollision.c`.

Tests:
- `tests/CollisionGeometryTests.c`: the maths, with seeded fuzz tests that casts never miss a contact;
- `tests/WorldColliderTests.c`: the `collision` section and the collider;
- `tests/WorldCollisionTests.c`: the collision world, including the trees against brute force on a moving random scene.

The formats: `references/asset_structure.md` ("World models", `collision`) and `references/world_structure.md` (the
`collider` component).

**What T12 is and is not.** It gives every collider a place in world space, and queries. The owner's requirements, as
T12 covers them:
1. **Nothing passes through anything:** casts whose time of impact is exact for linear motion and conservative for
   turning motion.
2. **Push-out:** contact manifolds with penetration depths and push-out normals.
3. **Shapes from primitives:** compound shapes made of primitives.
4. **The fullest contact data:** every result says which object, shape, part and triangle.

**T27** applies these every tick: it sweeps the moving colliders and solves the push-outs. **T28** adds rigid bodies.
Nothing here moves an object.

## Shapes (`CollisionShape.h`)

`CollisionShape` is a plain value: `Type`, `Center` and `Rotation` (Euler XYZ in radians; degrees in JSON) in its owner's
space, an optional part `Name`, and the sizes its type uses. Build one with `CollisionShape_Create(type)` (sizes
`COLLISION_SHAPE_DEFAULT_SIZE`, 0.5) and check it with `CollisionShape_Validate`.

| Type | Sizes | Notes |
|---|---|---|
| `Sphere` | `Radius` | |
| `Capsule` | `Radius`, `HalfHeight` (>= 0) | A segment along local Y, swept by the radius; half height 0 is a sphere. |
| `Box` | `HalfExtents` | |
| `Cylinder` | `Radius`, `HalfHeight` (> 0) | Along local Y. |
| `ConvexHull` | `Hull` | A `CollisionHull`, owned elsewhere (a world model's block). |
| `Plane` | `IsBounded`, `PlaneHalfExtents` (x, z), `Depth` (>= 0) | Its front is local +Y. Bounded (the default): a rectangle in local XZ, solid `Depth` behind it inside the rectangle; depth 0 is a flat rectangle, one-way: a shape whose centre lies behind it never touches it. Unbounded: an infinite half-space, solid below. Contacts and casts push to the front; rays hit the front only. |
| `Mesh` | `Mesh`, `IsTwoSided` | A `CollisionMesh`, owned elsewhere. Static colliders only: T27 never pushes one. |

- **Hulls** (`CollisionHull_Create(points, count, maxVertices)`, `_CreateFromModel(model, maxVertices)`): quickhull in
  double precision with coplanar faces merged into polygons (in linear time: the faces are bucketed by group once).
  - **Reduction.** A hull finer than `maxVertices` (4 to 256, default 64) is rebuilt from its six axis extremes and its
    farthest vertices along directions spread over the sphere. The first pass has exactly as many directions as picks
    are left, so the picks cover the whole hull (a longer pass once filled the budget from the top down and left a cone
    below); denser, turned passes fill what repeated picks left. The directions are scaled by the inverse of the hull's
    half extents, which spreads the picks evenly over a thin hull; plain directions pick its rim and lose its thickness.
  - **Limits.** At most 65536 input points, which build in about 0.2 s; flat or too few points are refused.
  - `_CreateFromModel` reads every mesh's vertices through the model's import transform.
- **Meshes** (`CollisionMesh_Create(parts, count, transform)`, `_CreateFromModel`):
  - each part's triangles go through the transform;
  - vertices with exactly equal positions are welded and degenerate triangles skipped (at most 2^20 triangles);
  - each edge is marked **active** or not: an edge shared across a flat or concave seam is inactive, so contacts on it
    take the face's normal and shapes slide over seams without catching;
  - a median-split BVH is built.

  Each triangle keeps its model part and its index there (`_part`, `_sourceIndex`; with a hit's barycentrics, T19 finds
  the UVs).
- **Sets** (`CollisionSet`): a named compound of shapes. Limits: 32 sets in a world model, 128 shapes in a set, 1024 in
  a world model. Names follow `[a-z][a-z0-9_.-]*`, at most 128 bytes (`CollisionShape_IsValidName`).
- `CollisionShape_GetTransform` gives the shape's own transform (rotation, then centre).
  `CollisionShape_TryGetLocalBounds` is false for an unbounded plane; a bounded plane's box is its slab.

## Layers (`CollisionLayer.h`)

`world`, `prop`, `character`, `vehicle`, `dynamic` (physics objects, later), `trigger` and `interaction`. The values
are saved: never renumber them.
- **A mask** is a `uint32_t` with `COLLISION_LAYER_BIT(layer)` per layer.
- **Blocking is symmetric:** `CollisionLayer_DoCollide(layerA, maskA, layerB, maskB)` needs each mask to hold the other's
  layer.
- **The default masks** (`CollisionLayer_GetDefaultMask`):
  - the five solid layers collide with each other, and all but `world` also with `trigger`;
  - `trigger` sees `prop`, `character`, `vehicle` and `dynamic`;
  - `interaction` collides with nothing: only queries that ask for it see it.
- **Names:** `COLLISION_LAYER_ENUM` holds them; `CollisionLayer_GetName` and `_TryParse` convert.

## A world model's collision

The `collision` section of a world model definition holds:
- **its sets;**
- **the placements' defaults:** `default_set`, `layer`, `collides_with` (left out: the layer's default mask) and
  `is_trigger`;
- or **`"bounds"`:** one set, `bounds`, holding a box around LOD 0, exact under its import transform. When the import
  transform shears, the box becomes a hull of its 8 corners.

No section means no collision.

**Parsing.** Every rule is checked when the definition is read, with messages that name the place
(`collision.sets[0].shapes[2].radius is -1; it must be above 0.`). Each shape type refuses the other types' keys. Point
hulls are built once at parse time to refuse flat ones.

**Loading.** A hull or mesh from a `model` loads that model as a **dependency** of the world model. Loading a model needs
GL, so headless tools can use point hulls only. One block holds the sets, the shapes, the names and the built hulls and
meshes; the loaded world model owns it. `WorldModel_GetCollision` returns it, or NULL.

## The collider component (`WorldColliderComponent.h`)

Component type 3, `collider`, with the table `collider_component`. Its rows:

| Rows | Meaning |
|---|---|
| `is_enabled` | Off: not in the collision world. |
| `overrides_collision_set` / `collision_set` | Off: the world model's `default_set`. On: the set named here (a door swaps its hitboxes this way). |
| `uses_own_shape` | One primitive of its own instead of the world model's sets: invisible walls, trigger zones, objects without a model. |
| `shape_type`, `shape_center`, `shape_rotation` | The own shape: `sphere`, `capsule`, `box`, `cylinder` or `plane`, and where it sits in the object's space. |
| `shape_radius`, `shape_half_height`, `shape_half_extents`, `shape_is_bounded`, `shape_plane_half_extents`, `shape_depth` | The own shape's sizes. |
| `overrides_layer` / `layer`, `overrides_collides_with` / `collides_with`, `overrides_is_trigger` / `is_trigger` | Each overrides the world model's default. `collides_with` is an `EnumSet` of layers ([PropertyDescriptors.md](PropertyDescriptors.md)). |
| `offset`, `rotation`, `scale` | A local transform of all its shapes on top of the object's ("distorting" hitboxes). The scale is above 0. |
| `rotation_mode` | `full` turns with the object. `yaw_only` keeps only the yaw of the object's forward and stays upright: characters, posts. `none` stays axis-aligned. Both rebuild the frame from the world transform's decomposition (a mirror stays in the scale), with the object's scale along the kept axes: under `none` along the world's, so a crate stretched along its own X and turned 90 degrees gets a box along the world's X (use `full` for turned, stretched objects). |

- **Pure resolution.** `WorldColliderComponent_ResolveSettings(collider, collision)` turns the override flags and the
  world model's defaults into `WorldColliderSettings`. Without a world model's collision the defaults are `prop`, prop's
  mask and no trigger. A named set the world model lacks gives no shapes and `IsSetMissing`.
- **Setters.** Each setter validates through the table and advances the revision. `SetOwnShape` takes a whole
  `CollisionShape`, all or nothing. `GetLocalTransform` composes scale, then rotation, then offset.
- **Exact under any transform.** Every scale applies exactly to every shape: the object's, its parents' and the
  collider's. An unevenly scaled sphere is an ellipsoid; a sheared box is a parallelepiped.

## The collision world (`WorldCollision.h`)

**Lifetime.** `WorldCollision_Create(world, assets, logger, options, &collision)` borrows the world, which must outlive
it. It resolves world models by name under its own asset user (rule 1), as the render cache does; with `assets` NULL
only own shapes collide. `WorldCollision_Destroy` releases them. The world models it resolved stay held until then, like
the render cache's.

### Sync

`WorldCollision_Sync(collision)` runs once a tick, in the collision step (see "Tick order").
- **World matrices** are brought up to date first: `World_UpdateTransforms`.
- **Instances follow the colliders.** When the world's component set changed, instances are re-matched by object id. A
  replaced collider is told apart by its `WorldComponent_GetSerial`: memory freed by a removal is usually reused by the
  next component added, so its address proves nothing.
- **Then, for each collider:**
  - its object or an ancestor disabled, or the collider off: it leaves the trees;
  - its revision, its model component's world model name, or its static/movable side changed, or it was just enabled:
    it is **rebuilt**;
  - only its object's transform revision changed (a parent moving changes it too): it is **moved**. A movable shape
    is re-inserted only once it leaves its fattened box.
- **The trees.**
  - Static colliders (the object and every ancestor `static`) sit in one dynamic AABB tree, movable ones in another
    whose boxes are fattened by `WORLD_COLLISION_MOVABLE_MARGIN` (0.1 m). The tree is a port of Box2D's
    `b2DynamicTree`.
  - Unbounded planes sit in a list every query checks.
  - A static object that moves is re-inserted and warned about once (`WorldCollisionOptions.WarnsWhenStaticObjectsMove`
    turns that off for the editor).
- **Warned once per instance:**
  - a world model that does not load;
  - a missing set;
  - a world model without a `collision` section under a collider;
  - a shape flattened by a zero scale (it collides with nothing until unflattened);
  - a static object that moved.
- **Counts:** `WorldCollision_GetStats` gives instances (static, movable), shapes, mesh triangles, planes, trigger
  pairs, rebuilds, moves and syncs.

### Queries

Every query takes a `WorldCollisionFilter` (`WorldCollisionFilter_Create(layerMask, triggerMode)`):
- **layers:** the layers it sees;
- **triggers:** ignored (the default of rays, casts and contacts), included (the default of overlaps) or the only ones;
- **an ignored object:** `IgnoredObjectId`, optionally with its whole subtree (`IsIgnoredSubtree`: what a character
  carries).

| Query | Gives |
|---|---|
| `WorldCollision_Raycast` / `_RaycastAll` | The nearest hit / every hit (one per shape), nearest first. |
| `WorldCollision_CastShape(shapes, count, start pose, end pose)` | The first contact of up to `WORLD_COLLISION_MAX_QUERY_SHAPES` (32) shapes moving between two poses. A motion without turning is a GJK ray cast, exact. A turning motion uses conservative advancement: it stops a hair (`COLLISION_TOI_TARGET`, 0.5 mm) before touching and never skips a contact. A shape inside something at the start is a hit at fraction 0 with its depth and push-out normal. Spheres, capsules, boxes, cylinders and hulls can be cast. |
| `WorldCollision_SweepSphere` / `_SweepCapsule` / `_SweepBox` | Linear casts of one shape. A capsule is upright and given by its whole height. |
| `WorldCollision_OverlapShape` / `_OverlapSphere` / `_OverlapCapsule` / `_OverlapBox` | The ids of the objects touched, ascending, each once. A bounded plane can be a query shape here. |
| `WorldCollision_CollideShape` / `_CollideObject` | Every contact manifold out to a separation (0 or a small skin for speculative contacts). `_CollideObject` uses an object's own collider as synced and applies the layer rule both ways. |

**What a result holds (the owner's fourth requirement).**
- **Every result** has a `WorldCollisionShapeInfo`: object id, shape index, part name, shape type, layer and trigger
  flag. On a mesh it adds the triangle index, the model part and its triangle, the material index and the barycentric
  weights of the triangle's corners.
- **A ray hit:** point, normal facing the ray, distance, fraction, and whether the ray started inside.
- **A cast hit:** which cast shape hit, fraction and distance, the point on each surface, and the hit surface's normal
  facing the cast shape. When the cast started inside: the depth, with the normal as the push-out.
- **A manifold** (A the query's shape or the object's collider, B the world's):
  - the normal from A to B and the deepest separation (negative: penetrating; A moves by -separation along -normal to
    separate);
  - 1 to 4 points from clipping the supporting faces (a box resting on a box gets its 4 corners), each with its world
    position on both surfaces, its local position in A's and in B's object space (for anchoring and matching across
    ticks: T27), its own separation and a feature id that stays the same while the contact keeps its topology.

**Pointers** in results (part names) and the debug list are valid until the next sync.

### Trigger events

After each sync the pairs (trigger, other collider) that touch are compared with the previous sync's: an **enter** for
each new pair, an **exit** for each that ended.
- **Exits:** an object removed or disabled, a layer change, or moving apart. A pair ends only once the two are
  `WORLD_COLLISION_TRIGGER_EXIT_MARGIN` (1 cm) apart, so a collider resting on a boundary does not flicker.
- **Pairs that never form:** two triggers, a static trigger with a static collider, layers that do not collide.
- **Order:** events queue by trigger id, then other id, enters before exits, each with the sync's tick. They stay until
  `WorldCollision_ClearTriggerEvents`: game logic drains them at one point of the tick (A4).

Solid contact events (begin, persist, end) come in T27.

## The narrowphase (private)

- **Convex views** (`CollisionConvex`): a point, segment, box, cylinder, hull or triangle under any affine transform,
  with a radius for the rounded shapes. A sphere is a point plus a radius and a capsule a segment plus a radius, but only
  under a similarity; under an uneven scale they become exact supports of the transformed shape. A mesh triangle carries
  its active edges and whether it is one-sided. Planes have their own view.
- **GJK** finds distances (Johnson's sub-algorithm). It tracks its best simplex and stops when it makes no progress,
  which keeps smooth shapes from cycling.
  - **Never overstated.** Every support also bounds the distance from below, and an exit before convergence reports
    that bound, never more.
  - **Exact near a face.** Near a face the closest point is the origin moved along the face's normal, which keeps the
    direction exact for a large face next to the origin; the old form read a resting plank as 0.6 mm apart.
  - **Double precision for triangles.** The triangle's region tests and weights run in double precision, because GJK
    builds slivers (two support points of a smooth shape a millimetre apart) whose float weights placed witness points
    millimetres off.
  - **Robust at resting distances.** Below 1 mm the normal is checked against the simplex face's normal, both ways,
    keeping the direction of the largest gap, since a resting gap is within the coordinates' rounding.
- **EPA** finds penetrations. For rounded shapes it runs on the cores, then adds the radii; rounded shapes whose cores
  are apart overlap by exactly their roundings, along the distance's normal.
  - **Bounded from above.** Every direction bounds the depth from above (moving B that far along it separates the
    shapes), so the result is checked against the nearest support EPA examined and GJK's own directions, and the
    smallest is kept.
  - **Why.** Shapes that only touch could otherwise lose their nearly flat face during the expansion and read millimetres
    deep.
- **Manifolds** clip the supporting faces (Sutherland–Hodgman), reduce to 4 points (the deepest, the farthest, then the
  largest area) and add GJK's own point when the clipped ones are shallower by more than 5 mm. Two segments (capsule
  sides) give their overlap's two ends only when they are within about 8 degrees of parallel; crossed ones touch at one
  point.
- **Rays** are analytic in each shape's own space. A triangle is hit within 1e-5 of its edges beyond them, so rays never
  slip between two triangles sharing an edge.
- **Casts:**
  - **Linear casts** are a GJK ray cast, exact in time. The hit's normal and points come from a distance query 0.2 mm
    before the contact, where the direction is well conditioned; at the contact it is lost in rounding for shapes
    without rounding. A linear cast that stalls falls back to conservative advancement.
  - **Turning casts** use conservative advancement. The bound on how fast the shape closes is the translation along
    the normal plus the turn times the core's largest distance from the turn's axis times the sine between the axis and
    the normal. A turn about the normal moves nothing toward the target, nor does a capsule spinning about its own axis.
    A step that lands in an overlap (rounding) falls back to the last pose found apart, so every hit has a real normal
    and real points.
  - **Starting at rest.** A cast that starts within its stopping gap, or overlapping by at most
    `COLLISION_SHALLOW_START_DEPTH`, is a hit only when the motion closes on the target or goes deeper. A cast from
    where the last one stopped can move away and along.
  - **One-sided triangles** are passed from behind by every cast, as by rays and contacts.
- **Tolerances** are named constants in `CollisionGeometry.h`:

  | Constant | Value |
  |---|---|
  | `COLLISION_GJK_RELATIVE_TOLERANCE` | 1e-5 |
  | `COLLISION_GJK_OVERLAP_DISTANCE` | 1e-5 m |
  | `COLLISION_EPA_TOLERANCE` | 1e-4 m |
  | `COLLISION_CAST_TOLERANCE` | 1e-4 m |
  | `COLLISION_TOI_TARGET` | 5e-4 m |
  | `COLLISION_SHALLOW_START_DEPTH` | 1e-4 m |

  Iteration limits: GJK 64, EPA 128, time of impact 64.

## Tick order

From [WorldData.md](WorldData.md) ("Ready for things that move"):
1. input;
2. scripts;
3. the transform drivers;
4. **`WorldCollision_Sync`**;
5. the queued events (trigger events among them);
6. animation;
7. `World_UpdateTransforms`;
8. render.

The test frame follows it: the desk slides, the collision world syncs, the trigger events are logged, then the sprites
animate.

## Debugging and the test content

**Wireframes.**
- `WorldCollision_SetDebugListEnabled(collision, true)` makes each sync list every active shape with its world
  transform, layer, trigger flag and side (`WorldCollision_GetDebugShapes`).
- `WorldRenderer_SetCollisionDebug(renderer, collision)` draws that list in the scene pass, read-only, after the light
  gizmos:
  - colours by layer: world grey, prop orange, character green, vehicle blue, dynamic yellow, interaction cyan;
    triggers violet whatever their layer; static shapes darker;
  - a mesh's inactive edges darker;
  - drawn twice, faint without the depth test (hidden parts, and lines that lie on a surface), then solid with it.

**In the test frame** ([WorldRuntime.md](WorldRuntime.md)): R toggles the wireframes, F6 swaps the door's hitbox, and the
I key's status line adds the counts.

**The test world** (`compile/out/worlds/test.world.json`) has 24 colliders:
- **`"bounds"`:** the slab (a `world` layer override), the cubes, the desk's parts and the variant cubes, through
  `test`, `pbr_metal` and `painted_cube`;
- **a hull:** the LOD spheres, built from `lod_test_lod0` (64 vertices);
- **a mesh:** the PBR cube, from its own model, layer `world`;
- **cylinders:** the cup (an own shape inside its scaled cube) and the tree's trunk;
- **a bounded plane:** the sign, 3 cm deep;
- **a door** (`test_door`, the test cube with an import transform, hinged at its origin) with a `closed` and an `open`
  set;
- **a trigger zone** at the end of the desk's slide: the desk (now movable) and its parts log enter and exit.

## Known limits

- **`WorldCollision_CollideObject` skips an object's own plane and mesh shapes.** It is marked `// TODO:`; T27's movers
  are convex.
- **No per-tick sweeping, pushing or contact events:** that is T27.
- **A mesh's debug wireframe draws every edge every frame:** fine for test content, slow for a large terrain.
- **World models stay held** by the collision world until it is destroyed, as the render cache holds its own.
