# World collision

Collision for the 3D world, built in T12 and T27 (decisions G13, G18–G20). The modules:
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
  - `source/WorldCollisionTree.{h,c}`: the dynamic AABB tree (its boxes in double, T33);
  - `source/WorldCollisionStep.c` (T27): the step: motions, bodies, the wake walk, blocked movers, the write-back,
    results;
  - `source/WorldCollisionSolver.c` (T27): the push-out solver, its sweeps and the depenetration;
  - `source/WorldCollisionEvents.c` (T27): contacts, contact events, motion events.
- **Debug wireframes and contact points:** `source/WorldRendererCollision.c`.
- **Terrain** (B01, [Terrain.md](Terrain.md)): `source/TerrainMeshCollision.c` builds a chunk's collision mesh
  (`TerrainMesh_CreateChunkCollisionMesh`); the collision world's terrain instances live in `source/WorldCollision.c`
  (see "Terrain").

Tests:
- `tests/CollisionGeometryTests.c`: the maths, with seeded fuzz tests that casts never miss a contact;
- `tests/WorldColliderTests.c`: the `collision` section and the collider;
- `tests/WorldCollisionTests.c`: the collision world, including the trees against brute force on a moving random scene
  (the brute force places the shapes as the world does, in double, and works relative to each query's origin, so the
  distances agree to the bit), and a ray finding a 1 cm wall to a micrometre;
- `tests/WorldCollisionMotionTests.c` (T27): the step: tunnelling, push-out at once, masses, sliding, blocked movers,
  teleports, the hierarchy, contact events, results, sleeping and determinism, and spheres resting a skin from a box to
  a micrometre;
- `tests/WorldTerrainCollisionTests.c` (B01): terrain chunks: rays and casts naming the chunk and the terrain's
  triangle, holes, seams that do not catch, T13's slope limit, rebuilds of only the changed chunks, a collider winning
  over a terrain, the warnings;
- all three run twice, the second time as `WorldCollisionFar`, `WorldCollisionMotionFar` and
  `WorldTerrainCollisionFar` with every scene shifted about 100 km along each axis ([WorldData.md](WorldData.md),
  "Tests").

The formats: `references/asset_structure.md` ("World models", `collision`; "Terrains", `collision`) and
`references/world_structure.md` (the `collider` and `terrain` components).

**What T12 is and is not.** It gives every collider a place in world space, and queries. The owner's requirements, as
T12 covers them:
1. **Nothing passes through anything:** casts whose time of impact is exact for linear motion and conservative for
   turning motion.
2. **Push-out:** contact manifolds with penetration depths and push-out normals.
3. **Shapes from primitives:** compound shapes made of primitives.
4. **The fullest contact data:** every result says which object, shape, part and triangle.

**T27** applies them every tick ("Moving and pushing" below): it sweeps the moving colliders, pushes apart what
overlaps, writes the pushes into the objects and reports contacts and their events. **T28** adds rigid bodies.

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
    take the face's normal and shapes slide over seams without catching. A contact's witness point lies exactly on the
    edge it touches, which the closest-point region test calls inside (its tests tie there): the contact reads its
    feature from the point's barycentric weights (a weight of at most 1e-5 puts it on the opposite edge,
    `TRIANGLE_BOUNDARY_WEIGHT`), so a contact on an active edge keeps its own normal (T13's first tuning: read as
    inside, a capsule beside a mesh box's bottom or top edge took that face's normal and plane distance, a push of
    most of its height, and the test world's glowing cube threw a player jumping against it up and down);
  - a median-split BVH is built.

  Each triangle keeps its model part and its index there (`_part`, `_sourceIndex`; with a hit's barycentrics, T19 finds
  the UVs).
- **Given edges and indices** (B01). A `CollisionMeshPart` may bring each triangle's edge flags (`ActiveEdges`: bit 0
  for (v0, v1), bit 1 for (v1, v2), bit 2 for (v2, v0)) and its own source index (`SourceIndices`) instead: the mesh
  keeps them as given (the parts' welding and degenerate triangles change neither) and computes nothing for them. The
  terrain passes the flags it computed over the whole terrain, so a chunk's border edges are active only where the
  terrain itself has an edge, and its triangle indices, so a hit names the terrain's triangle.
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
| `is_pushable` (T27) | On a movable object: the step may push it. Off: a movable object's collider is kinematic. |
| `overrides_mass` / `mass` (T27) | The mass over the world model's (`COLLISION_DEFAULT_MASS`, 1 kg, without one); above 0, at most `COLLISION_MAX_MASS` (1e6: a larger ratio would lose the lighter one's push to rounding). |
| `when_blocked` (T27) | A kinematic mover's: `stop` (the default) or `push_through`. |
| `is_continuous` (T27) | Off: the collider's own motion is not swept, only held by its contacts. |
| `reports_persisting_contacts` (T27) | On: a persist event every step while it is in contact. |

- **Pure resolution.** `WorldColliderComponent_ResolveSettings(collider, collision)` turns the override flags and the
  world model's defaults into `WorldColliderSettings`. Without a world model's collision the defaults are `prop`, prop's
  mask and no trigger. A named set the world model lacks gives no shapes and `IsSetMissing`.
- **Setters.** Each setter validates through the table and advances the revision. `SetOwnShape` takes a whole
  `CollisionShape`, all or nothing. `GetLocalTransform` composes scale, then rotation, then offset.
- **Exact under any transform.** Every scale applies exactly to every shape: the object's, its parents' and the
  collider's. An unevenly scaled sphere is an ellipsoid; a sheared box is a parallelepiped.

## Terrain (B01)

A terrain component ([Terrain.md](Terrain.md)) is the collision world's second source, next to colliders.
- **One instance per terrain object without a collider.** An object with both collides through its collider alone (the
  terrain is ignored, warned once): a terrain is never a collider's shape.
- **One shape per chunk,** in chunk order (the shape's index is the chunk's): a one-sided `Mesh` shape at the chunk's
  centre, its mesh from `TerrainMesh_CreateChunkCollisionMesh` (the chunk's surface, holes left out, relative to the
  centre, cut into parts of at most 65536 vertices as the renderer cuts it). The placement is the object's world matrix
  times the chunk's translation, in double as every placement is.
- **Seams do not catch.** Each part brings the terrain's own edge flags (computed over the whole terrain, "Given edges
  and indices" above): an edge on a chunk's border is inactive where the terrain is smooth across it, so a capsule
  sliding across it takes the face's normal, exactly as inside a chunk (the tests check a ball's contacts across seams
  and compare a capsule's walk across them with the same walk on a box).
- **Hits name the terrain's triangle:** `MeshPart` is the chunk and `SourceTriangle` the terrain's triangle index (T19's
  picking, B09 and the editor read its weights from it).
- **One-sided:** the front (counter-clockwise) side collides; rays and casts from below pass through. An overhang's
  underside faces down, so it is a roof.
- **Settings:** the layer, the mask and whether it collides are the settings in effect
  (`WorldTerrainComponent_ResolveSettings`): `world` by default, the layer's default mask.
- **Static or kinematic, never pushable.** A static terrain object sits in the static tree; a movable one pushes as a
  kinematic mover (T27) and stops when blocked.
- **Rebuilds.** The meshes are cut again when the terrain (its name, its asset) or its chunk layout changes; a chunk's
  mesh is rebuilt alone when its revision moves (`TerrainMesh_RebuildDirtyChunks`, the editor); the shapes are rebuilt
  from the kept meshes when a setting changes, the object switches between static and movable, or the instance comes
  back; only placed again when the object moves. `WorldCollisionStats.TerrainChunkBuildCount` counts the chunk meshes
  built.
- **Warned once per instance:** a terrain that does not load; a chunk whose mesh cannot be built (more than 2^20
  triangles: everything falls through it); a transform that flattens it; a collider and a terrain on one object; a
  static terrain that moved.

## The collision world (`WorldCollision.h`)

**Lifetime.** `WorldCollision_Create(world, assets, logger, options, &collision)` borrows the world, which must outlive
it. It resolves world models by name under its own asset user (rule 1), as the render cache does; with `assets` NULL
only own shapes collide. `WorldCollision_Destroy` releases them. The world models it resolved stay held until then, like
the render cache's.

### Doubles outside, floats inside (T33)

As Jolt's double-precision build does it, so a world 100 km across collides as it does at its origin.
- **Outside**, every position the collision world takes or gives is a double (`Vector3Double`): query origins and
  positions, hit and cast points, contact positions (`WorldContactPoint.PositionA` and `PositionB`), a result's
  `GroundPoint`, the debug list's transforms (`Matrix4Double`) and debug contact points. Directions, normals, distances,
  separations, velocities, pushes, sizes and local positions (`LocalPositionA` and `B`) stay floats.
- **Inside**, each shape's placement is float axes and a double translation, each motion pose a double position and a
  float turn, and the trees' and the step's swept boxes are doubles. The narrowphase (`CollisionGeometry`) stays in
  float: before each call the world layer picks an **origin** near what it tests (a ray's or cast's start, an overlap's
  position, a pair's first body's start in the step) and hands it the placements relative to it, subtracted in double
  and rounded once; what comes back has the origin added back in double. A step's manifold keeps its origin.
- **What it buys.** The skin (1 mm), the 2 cm sweep threshold, the speculative distances and the characters' 2 mm rest
  mean the same anywhere: 173 km out, a ray finds a 1 cm wall to a micrometre and a pushed sphere rests on a box a skin
  above it to a micrometre, as at the origin. Nearby things that differ by rounding (a double's step is 1.5e-11 m at
  100 km) may still make a decision on a threshold go the other way a tick later: a character sliding along a wall sits
  exactly at the 2 mm contact distance, so a contact event can begin a tick apart in two runs at different places.
- **Meshes keep their vertices as floats in their own space,** so a very large mesh loses precision at its far end
  (1 mm at 8 km from its object's origin; see "Known limits"). A terrain's chunks are meshes of their own, their
  vertices relative to each chunk's centre on the terrain's 1/1024 m grid, where float subtraction is exact (B01): a
  terrain is exact across its whole extent (8 km each way).
  Planes are placed by their object's origin too: a level unbounded plane (the usual ground) is exact anywhere, a
  tilted one far from its object's origin is not (see "Known limits").

### Sync

The sync is the first part of `WorldCollision_Step`, the tick's collision step (see "Tick order").
`WorldCollision_Sync(collision)` runs it alone, for tools, the editor and queries after a change in the middle of a
tick: it moves and pushes nothing, and leaves the motions alone (the next step sweeps from where each collider was at
the last step).
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
- **Terrains** (B01): see "Terrain" below; a terrain instance rebuilds only the chunks that changed.
- **Counts:** `WorldCollision_GetStats` gives instances (static, movable), shapes, mesh triangles, planes, trigger
  pairs, rebuilds, moves, syncs and (B01) terrain chunks built.

### Queries

Every query takes a `WorldCollisionFilter` (`WorldCollisionFilter_Create(layerMask, triggerMode)`):
- **layers:** the layers it sees;
- **triggers:** ignored (the default of rays, casts and contacts), included (the default of overlaps) or the only ones;
- **an ignored object:** `IgnoredObjectId`, optionally with its whole subtree (`IsIgnoredSubtree`: what a character
  carries);
- **pushables** (T13): `ArePushablesIgnored` skips pushable colliders and the movable colliders a pushable carries (its
  children's, which its push moves: a crate's handle, later a held item). A character's own moves sweep past other
  characters and crates and leave them to the collision step, which pushes both by mass ([Characters.md](Characters.md));
- **as a layer** (T13): with `HasOwnLayer` the query collides as a collider on `OwnLayer` would, so it also skips
  colliders whose mask lacks that layer (the other half of `CollisionLayer_DoCollide`'s symmetric rule). A character's
  sweeps collide as the `character` layer: a prop set not to collide with characters lets it through.

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
- **A ray hit:** point (in double), normal facing the ray, distance, fraction, and whether the ray started inside.
- **A cast hit:** which cast shape hit, fraction and distance, the point on each surface (in double), and the hit
  surface's normal facing the cast shape. When the cast started inside: the depth, with the normal as the push-out.
- **A manifold** (A the query's shape or the object's collider, B the world's):
  - the normal from A to B and the deepest separation (negative: penetrating; A moves by -separation along -normal to
    separate);
  - 1 to 4 points from clipping the supporting faces (a box resting on a box gets its 4 corners), each with its world
    position on both surfaces (in double), its local position in A's and in B's object space (floats, for anchoring
    and matching across ticks: T27), its own separation and a feature id that stays the same while the contact keeps
    its topology.

**Pointers** in results (part names) and the debug list are valid until the next sync.

### Trigger events

After each sync the pairs (trigger, other collider) that touch are compared with the previous sync's: an **enter** for
each new pair, an **exit** for each that ended.
- **Exits:** an object removed or disabled, a layer change, or moving apart. A pair ends only once the two are
  `WORLD_COLLISION_TRIGGER_EXIT_MARGIN` (1 cm) apart, so a collider resting on a boundary does not flicker.
- **Pairs that never form:** two triggers, a static trigger with a static collider, layers that do not collide.
- **Order:** events queue by trigger id, then other id, enters before exits, each with the sync's tick. They stay until
  `WorldCollision_ClearTriggerEvents`: game logic drains them at one point of the tick (A4).

Solid contacts have their own events since T27 ("Contacts and their events").

## Moving and pushing (T27)

`WorldCollision_Step(collision, deltaSeconds)` is the tick's collision step. It syncs, sweeps every collider that moved
since the last step, pushes apart what overlaps at once, writes the pushes into the objects, then finds the contacts,
their events, the motion events, the results and the trigger pairs.

### Who moves and who gives way

| The collider of | Is | In the step |
|---|---|---|
| a static object | static | Never moves in play (a move is a teleport, warned once as in T12) and never gives way. |
| a movable object, `is_pushable` off | **kinematic**: doors, elevators, whatever scripts and animations move | Pushes, is never pushed; `when_blocked` says what it does when it cannot. |
| a movable object, `is_pushable` on | **pushable**: characters, NPCs, crates | Pushed: pushables share a push by inverse mass (equal masses give way equally, a heavy one barely moves). |

- **Carried colliders.** A collider under a pushable object (its children's, unless pushable themselves) belongs to
  that pushable, its **owner**: it moves with its push and its contacts push it.
- **A pushable under a pushable** is its own owner. It wakes with the one above it and is pushed on its own: it does
  not follow that one's push (the write-back takes it out), as a box on a frictionless cart.
- **Pushes only translate**, along each contact's normal: a pushed collider slides along walls. Rotation from contacts
  is T28's.
- **Triggers** are never pushed and push nothing. A pushed collider's planes and meshes take no part (warned once): its
  spheres, capsules, boxes, cylinders and hulls do.
- **Contacts need a pushable.** The solver looks at the pairs around pushed colliders only: a kinematic door against a
  static wall has no contact, nothing to push.

### Motions

- **The pose** of a collider is its rigid frame: its object's world position and its turn by the rotation mode
  (`full`: the decomposed world rotation; `yaw_only`: the yaw; `none`: none). Its shapes keep their place relative to
  it, so a motion from pose to pose is rigid.
- **A motion** runs from the pose at the end of the last step (whatever syncs happened between) to the pose now.
- **No sweep**, the collider starting where it is: its first step, a **teleport** (`WorldCollision_Teleport(collision,
  objectId)`, before the step: the object and everything under it; spawning, the editor, a script's "teleport to"), a
  static object's move, and every move while `WorldCollisionOptions.AreMotionsSwept` is off (the editor, which places
  rather than moves). The jump still wakes what is around it, to be pushed out.
- **`is_continuous` off** skips the sweep of the collider's own motion, its pushes included: its contacts still hold it
  (within the speculative distance, plus how far it can close), enough for slow movers, and whatever moves against it
  is still swept, against where it ends up.
- **A changed shape.** A collider whose shapes change their place relative to its pose without the pose moving (its
  object's scale) counts as changed: what it now reaches is woken and pushed out, and a contact it shrinks away from
  ends.

### The step

1. **Sync** (T12), then the motions and each collider's owner.
2. **Bodies:** every collider that moves, and every pushable that one of them, a changed collider (rebuilt, placed,
   teleported) or a pushable still awake wakes, in a walk outward (a crowd pressed together wakes as one). A woken
   pushable brings what it carries. **Pushables at rest sleep:** a pile nothing comes near costs nothing, and its
   contacts with the floor carry over unchanged.
3. **The solve** (below).
4. **Blocked movers.** A blocked kinematic mover with `when_blocked` `stop` is set back: the local transforms that
   changed since the last step along its object's chain (recorded at the last step's end), so a group its parent moves
   stops as one. The step then syncs and solves again, at most `WORLD_COLLISION_MAX_BLOCK_ROUNDS` times: movers
   pushing into each other through pushables stop one more each round (a train of doors), and past the last round the
   movers still blocked push through for that step, the overlap they leave reported. A mover whose chain changed
   hierarchy since the last step (reparented) cannot be set back and pushes through. With `push_through` it moves on,
   and the pushables are left overlapping it as little as they can.
5. **Write back:** a pushed object's position moves by its push less the push of the pushable it hangs under (which
   moves it already), through its parent's inverse. This is the one place where a system besides an object's driver
   writes its transform; the driver reads the correction next tick ([WorldData.md](WorldData.md), the driver rule).
6. **Contacts, events and results** (below), then the trigger pairs.

### The solve

- **Substeps.** A step whose largest turn passes `WORLD_COLLISION_MAX_SUBSTEP_TURN` (15 degrees) is solved in equal
  substeps, at most `WORLD_COLLISION_MAX_SUBSTEPS`. At 480 Hz this is rare.
- **Speculative contacts.** T12's manifolds at the substep's start, out to how far each pair can close in it (its
  translation and how far its turn reaches) plus `WORLD_COLLISION_SPECULATIVE_DISTANCE`.
- **Rows.** Each contact point's separation after the motions (the two material points carried by their bodies'
  motions, along the normal) must stay at least `WORLD_COLLISION_SKIN`; the owners' pushes along the normal change it.
- **Projected Gauss-Seidel.** Each row pushes its owners apart by its violation, shared by inverse mass (static and
  kinematic: 0), its accumulated push clamped at 0 (a push never pulls). The sweeps over the rows alternate forward and
  backward. Rows against what neither moves nor gives way (static colliders, kinematic ones standing still) close last
  in every iteration, so what cannot fit is left between pushables and movers, never in a wall. It stops once no pushed
  body moves more than a hundredth of the skin in an iteration (the bodies, not the rows: a box resting on a box has
  four rows trading shares of one push), or after `WORLD_COLLISION_SOLVER_ITERATIONS`. **No warm starting:** every
  solve starts from no push (last step's pushes, re-applied row by row, threw a light pushable pinned by a heavy one
  around). Very different masses converge slowly: a heavy pushable pressing a light one into a wall rests up to the
  slop into it, steadily.
- **Sweeps: nothing tunnels.** Every continuous pushed motion longer than the speculative distance is checked with
  exact casts against everything near it: the relative motion's linear cast when neither side turns (both may move),
  the pushed body's straight path at its final turn against what stands still; a side whose motion is not continuous
  stands where it ends. A pair is swept once, by a side that sweeps itself (the lower body when both do). A hit becomes
  a new row at its time of impact and the rows are solved again, at most `WORLD_COLLISION_SWEEP_ROUNDS` times; a motion
  that still hits stops just before its time of impact, or blocks the kinematic mover it ran into. Turns are the
  substeps' job: a turning cast would see the pushed body's straight path cut the turning one, which no push can mend.
- **Blocked.** The pushed bodies joined by rows form islands. An island with a row still violated by more than
  `WORLD_COLLISION_SLOP` could not make room: every kinematic mover whose row presses into it (pushes, or is violated
  itself) is blocked. A mover moving away from it, or along it, is not.
- **Depenetration.** At the final poses, solves without motion push out what still overlaps (spawns, teleports, set
  switches), each with its own contacts, at most `WORLD_COLLISION_DEPENETRATION_ROUNDS`, until nothing overlaps beyond
  the slop or a round gains too little. A pushable still overlapping beyond the slop is **crushed**, with the depth and
  what it is pressed into most deeply.
- **Determinism.** The same scene stepped twice gives the same poses bit for bit (everything runs in id order; on one
  build and machine: floats differ across compilers and CPUs).

### Contacts and their events

- **Contacts** (`WorldCollision_GetContacts`, sorted by key): one per pair of shapes (and mesh triangle) of every pair
  of colliders, one of them pushed, within the speculative reach at the step's end. Each is T12's manifold at the final
  poses (A the lower object id) plus:
  - per point: the relative velocity's normal speed and tangent velocity (from the motions over the step), the push
    (metres, and as a speed), its age (matched across steps by feature id) and whether it touches
    (`WORLD_COLLISION_CONTACT_DISTANCE`) or is speculative;
  - the approach speed (the closing the drivers gave, before the pushes: impact sounds), the total push on the pair of
    shapes (metres: how far the solver moved the two apart), the contact's age.

  A pair nothing moved near keeps its contact from the last step, a step older, with no velocity or push.
- **Contact events** per object pair (`WorldCollision_GetContactEvents`):
  - **begin** when one of its contacts first touches, with the approach speed and the time of impact within the step
    (from the pair's separation and closing at the step's start);
  - **persist** every step while in contact, only when either collider has `reports_persisting_contacts` (a pile at
    480 Hz does not flood scripts);
  - **end** once no contact is left within `WORLD_COLLISION_CONTACT_END_MARGIN` past touching (a resting crate does not
    flicker), or either collider is removed, disabled or stops qualifying.

  Each carries copies of the pair's contacts (`WorldCollision_GetContactEventContacts` from `FirstContact`; an end
  the last step's). Begins and persists come in pair order, then ends; they stay until
  `WorldCollision_ClearContactEvents`.
- **Motion events** (`WorldCollision_GetMotionEvents`, until `_ClearMotionEvents`), when a state begins and ends:
  blocked (the mover, the pushable that blocked it, whether it stopped) and crushed (the pushable, what it is pressed
  into, the depth). A collider removed while blocked or crushed ends it.
- **Motion results** (`WorldCollision_GetMotionResult`), for drivers (T13's character): the collider's velocity over
  the step, its correction (a pushable's push, the motion a stopped mover lost), its touching contacts (a pushable's
  with those of the colliders it carries), its ground (the most upward-facing of them: the object, the normal into the
  collider, the point and that point's velocity, for moving platforms), blocked (by what, whether it stopped) and
  crushed (a pushable's: by what, how deep; the colliders it carries do not repeat it). A collider the step did not
  reach reads as at rest, touching nothing.

### Tolerances

| Constant | Value | |
|---|---|---|
| `WORLD_COLLISION_SKIN` | 1 mm | The gap the solver keeps: resting shapes lie this far apart, so sweeps start clear. |
| `WORLD_COLLISION_SLOP` | 1 mm | The overlap left without a report (beyond it: blocked, crushed). |
| `WORLD_COLLISION_SPECULATIVE_DISTANCE` | 2 cm | Contacts this far past how far a pair can close; shorter motions are not swept (9.6 m/s at 480 Hz). |
| `WORLD_COLLISION_CONTACT_DISTANCE` | 2 mm | A point this close touches. |
| `WORLD_COLLISION_CONTACT_END_MARGIN` | 5 mm | How far past touching a pair stays in contact. |
| `WORLD_COLLISION_MAX_SUBSTEP_TURN` | 15 degrees | The largest turn of one substep. |
| `WORLD_COLLISION_MAX_SUBSTEPS` | 8 | Substeps of one step. |
| `WORLD_COLLISION_SOLVER_ITERATIONS` | 64 | Gauss-Seidel iterations of one solve, at most. |
| `WORLD_COLLISION_SWEEP_ROUNDS` | 4 | Rounds of sweeps and solves, then the motions that still hit stop. |
| `WORLD_COLLISION_DEPENETRATION_ROUNDS` | 4 | Solves at the final poses. |
| `WORLD_COLLISION_MAX_BLOCK_ROUNDS` | 8 | Times a step sets stopped movers back and solves again; then the rest push through. |

### Cost

Microseconds per step, `-O3`, on the development machine (a 480 Hz tick has 2083), from a scratch bench: a kinematic
wall pushes 0.2 m crates at 2 m/s against a static wall, then pushes on against them, stopped every tick.

| Scene | At rest | Pushing | Pushing on, blocked | The plain sync |
|---|---|---|---|---|
| A row of 5 crates | 0.8 | 40 | 41 | 0.1 |
| A 7 x 7 grid of crates | 7.3 | 459 | 413 | 0.4 |
| The grid and 900 static boxes | 26.5 | 520 | 469 | 13.2 |

The passes over every collider stop early at a static one, and a collider's result is emptied only when a step writes
it, so colliders at rest cost little beyond the sync.

## The narrowphase (private)

- **Convex views** (`CollisionConvex`): a point, segment, box, cylinder, hull or triangle under any affine transform,
  with a radius for the rounded shapes. A sphere is a point plus a radius and a capsule a segment plus a radius, but only
  under a similarity; under an uneven scale they become exact supports of the transformed shape. A mesh triangle carries
  its active edges and whether it is one-sided. Planes have their own view.
- **GJK** finds distances (Johnson's sub-algorithm). It tracks its best simplex and stops when it makes no progress,
  which keeps smooth shapes from cycling.
  - **Never overstated.** Every support also bounds the distance from below, and an exit before convergence reports
    the tightest such bound, never more. The last simplex's direction is one of them (T13's review): above the middle of
    a large face the first directions lean a hair toward corners tens of metres away and bound the gap millimetres
    short, and the step that reaches the face itself is no closer than the edge before it, so the search used to end
    on the leaning bound (a capsule resting 1 mm above a 40 m floor read 1.5 mm inside it, above a 120 m one 4.4 mm).
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
4. **`WorldCollision_Step`** (T27; T12's sync is its first part);
5. the queued events: trigger, contact and motion events;
6. animation;
7. `World_UpdateTransforms`;
8. render.

The test frame follows it: the desk slides, the door swings and the pushing wall moves, the collision world steps, its
events are logged, then the sprites animate.

## Debugging and the test content

**Wireframes.**
- `WorldCollision_SetDebugListEnabled(collision, true)` makes each sync list every active shape with its world
  transform (a `Matrix4Double`), layer, trigger flag and side (`WorldCollision_GetDebugShapes`); the renderer draws it
  relative to the camera, so far out the wireframes sit as exactly as at the origin.
- `WorldRenderer_SetCollisionDebug(renderer, collision)` draws that list in the scene pass, read-only, after the light
  gizmos:
  - colours by layer: world grey, prop orange, character green, vehicle blue, dynamic yellow, interaction cyan;
    triggers violet whatever their layer; static shapes darker;
  - a mesh's inactive edges darker;
  - drawn twice, faint without the depth test (hidden parts, and lines that lie on a surface), then solid with it.

**Contact points** (T27). `WorldCollision_SetDebugContactsEnabled(collision, true)` makes each step list every contact
point (`WorldCollision_GetDebugContacts`): midway between the two surfaces, with the normal, the push as a speed and
whether it touches. The renderer draws them while a collision world is set: a 2 cm cross, white touching and grey
near, a 12 cm cyan normal, and a red line for the push (its speed times 0.05 s, at most 0.5 m).

**In the test frame** ([WorldRuntime.md](WorldRuntime.md)): F7 toggles the wireframes, F8 the contact points, F6
swings the door, F9 sends the pushing wall out and back, F10 teleports the door's crate aside and back; the contact and
motion events are logged, and the I key's status lines add the counts and the last step's.

**The test world** (`compile/out/worlds/test.world.json`) has 34 colliders:
- **`"bounds"`:** the slab (a `world` layer override), the cubes, the desk's parts and the variant cubes, through
  `test`, `pbr_metal` and `painted_cube`;
- **a hull:** the LOD spheres, built from `lod_test_lod0` (64 vertices);
- **a mesh:** the PBR cube, from its own model, layer `world`;
- **cylinders:** the cup (an own shape inside its scaled cube) and the tree's trunk;
- **a bounded plane:** the sign, 3 cm deep;
- **a door** (`test_door`, the test cube with an import transform, hinged at its origin) with a `closed` and an `open`
  set; movable since T27, a kinematic mover that stops when blocked;
- **a trigger zone** at the end of the desk's slide: the desk (now movable) and its parts log enter and exit;
- **T27's movers** (own boxes, the crates pushable): a pushing wall (kinematic) behind a row of five crates, a static
  stop at the row's far end (the wall pushes the row against it and is blocked), and a crate pinned in a corner of two
  static posts in the door's swing (the door stops on it; closing pushes it along).

Since B01 it also has a terrain (object 51, `placeholder_terrain`): the hills around the slab, one static instance of
256 chunk meshes (61 168 triangles: the terrain's 61 174 less its 6 hole faces; [Terrain.md](Terrain.md), "The test
terrain").

## Known limits

- **Shapes hundreds of metres across lose millimetres in GJK.** A direction off by float rounding (about 1e-7 of the
  coordinates) bounds a gap short by that times the shape's size: 2.8 mm next to a box 400 m wide. Up to 120 m the
  distance is right to 0.04 mm. Make a huge ground a plane or a mesh, not one box.
- **A triangle mesh much larger than a few kilometres loses precision at its far end** (T33): its vertices are floats in
  its own space, 1 mm apart at 8 km from its object's origin. Nothing splits a model's mesh automatically: split big
  architecture into objects. Terrain needs nothing: its chunks are meshes of their own (B01).
- **A tilted unbounded plane far from its object's origin** (T33): the narrowphase takes a plane's point from its
  object's origin, made relative to the query and rounded once, so 100 km from that origin the point is off by up to
  a float's step there (8 mm) and a plane tilted from level moves by that times the sine of its tilt. A level one (the
  usual ground) is exact anywhere. Put a tilted half-space's object near where it is used. A bounded plane (a
  rectangle) is placed by its centre, like a mesh: one much larger than a few kilometres loses precision at its edges.
- **Queries see the colliders of the last sync.** A driver that queries (the characters, T13) sees a platform moved by
  an earlier driver in the same tick where the last step left it: a sync between them would also advance the tick
  stamped on every event.
- **`WorldCollision_CollideObject` skips an object's own plane and mesh shapes.** It is marked `// TODO:`; T27's movers
  are convex.
- **A mesh's debug wireframe draws every edge every frame:** fine for test content; the test terrain's 61 000 triangles
  are drawn whole with F7 too (the renderer's chunk key, the minus key, draws only the chunks in view).
- **A terrain chunk with more than 2^20 triangles** gets no collision (warned once: everything falls through it). A
  32 m chunk holds that many only at triangles a few centimetres across.
- **World models stay held** by the collision world until it is destroyed, as the render cache holds its own.
- **Pushes translate only, without friction** (T28): a crate pushed by a turning door slides off it rather than
  turning.
- **A turning body is not swept against a moving one:** a fast turn and a fast motion meeting in the same step rely on
  the substeps' speculative contacts.
- **Contacts and contact events need a pushable on one side:** a door against a wall, or two kinematic movers, have
  none.
- **A large crowd pushed costs:** a 7 x 7 grid pushed against a wall takes about half a millisecond a step (the Cost
  table): under a 480 Hz tick, but a hundred pushables pressed together would not be.
- **Blocking is per island:** a mover whose row pushes into an island that cannot make room is blocked even when its
  own push could be met (an elevator lifting a crate that a door squeezes sideways against a wall).
- **A pushable wedged between two things that do not give way** is pushed out of the one solved last, into the other,
  rather than halfway (it is reported crushed either way).
- **Only the blocked mover is reported stopped:** movers that its chain's set-back moves back with it (its parent's
  other children) are not.
- **A stopped mover undoes its chain's whole tick:** every local position, rotation and scale that changed along its
  object's chain since the last step goes back, a script's other change to those objects (a new scale, a reparent's
  new local transform) included.
