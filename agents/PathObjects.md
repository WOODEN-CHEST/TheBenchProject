# Path objects

A fence, hedge, railing or row of lamps as one reusable asset, filling a path ([Paths.md](Paths.md)) with pieces. Every setting has
a default on the asset and an override per placement, so a floating fence needs no second asset, and everything rebuilds live when a
setting, the path or the ground under it changes.

| Module | Files | What it is |
|---|---|---|
| PathObject | `include/PathObject.h`, `source/PathObject.c`, `source/PathObjectDefinition.c`, `source/PathObjectInternal.h` | The `path_object` asset type (`path_objects/`): the settings, the slots' and models' record tables, which setting is in effect. |
| WorldPathObjectComponent | `include/WorldPathObjectComponent.h`, `source/WorldPathObjectComponent.c` | Component type 11, `path_object`: the asset's name and the same settings as overrides. Holds settings only. |
| PathObjectLayout | `include/PathObjectLayout.h`, `source/PathObjectLayout.c` | Where pieces lie along a path, as pure maths: runs, spans, the fit, the walk. No world, no assets, no GL. |
| PathObjectPlacement | `include/PathObjectPlacement.h`, `source/PathObjectPlacement.c` | What a piece turns into, as pure maths: a mesh's measure, a rigid copy, a bent mesh. |
| WorldPathObjects | `include/WorldPathObjects.h`, `source/WorldPathObjects.c` (the components and the rebuilds), `source/WorldPathObjectsRigid.c` (points on the ground, rigid copies, their collision), `source/WorldPathObjectsBent.c` (slices, parts, generated models), `source/WorldPathObjectsChildren.c` (what came out, on the runtime children), `source/WorldPathObjectsInternal.h` | The system: it keeps what every component generates up to date, once a tick and only when something changed. |
| WorldGeneratedModel | `include/WorldGeneratedModel.h`, `source/WorldGeneratedModel.c`, `source/WorldRendererGenerated.c` | Meshes a generator made while the game runs, drawn by a model component in every pass ([WorldRuntime.md](WorldRuntime.md), "Generated models"). |
| Test content | objects 99 to 110 of the test world, `source/WorldTestPathObjects.{h,c}`, `asset_source/scripts/placeholder_path_objects.py` | Fences, a hedge, a railing, a row of lamps; the keys that change the walk's fence. |

The asset's format: `references/asset_structure.md`, "Path objects". The component's: `references/world_structure.md`, "The path
object block". Tests: `tests/PathObjectLayoutTests.c`, `tests/PathObjectTests.c`, `tests/WorldPathObjectTests.c` (also 173 km out).

## The sequence

A run along a path is filled with this sequence of slots:

```
start_cap  middle  separator  middle  separator  middle  end_cap
```

Only `middle` is required; every other slot has an explicit flag (`has_start_cap`, `has_end_cap`, `has_separator`, `has_single`,
`has_corner`), and a slot in use needs a model. One segment is `start_cap middle end_cap`, with nothing special about it.

- `single` replaces a whole span, caps too, where exactly one middle fits.
- `corner` replaces the separator at a corner node (an L-shaped wall block).
- `separator_every` puts a separator after every this many middles.

A plain fence names one post in three slots and one panel in `middle`. A row of lamps is a `middle` of length 0 with a gap and a side.

**Every slot takes the same settings** (the `path_object_slot` table; `PathObjectSlotRecord`), held in the `slots` list, a record per
slot. Its weighted models are records of the flat `models` list, each naming its slot (the `path_object_model` table;
`PathObjectModelRecord`), because a record list holds no list of its own.

## What is in effect

`PathObject_ResolveSettings` and `PathObject_ResolveSlot`, pure functions: the built-in defaults, then the asset's rows whose flags
are on, then the placement's. A placement's `slots` records add to the asset's, slot by slot and setting by setting. While a placement's
`overrides_models` is on, a slot its `models` list names takes those models and the other slots keep the asset's
(`PathObject_GetSlotModels`). Multipliers multiply.

Two slot defaults depend on the slot (`PathObjectSlotRecord_CreateDefault`): the middle and the single stretch, and the middle shears
while the others step. Collision defaults to the world model's set; `snaps_to_terrain` to on. An asset's file may leave a setting's
flag out: writing a setting sets it (`PathObjectDefinition.c`). A placement writes its flags, as every placement does.

## Layout

`PathObjectLayout_Build` takes lengths as numbers and gives distances along the path.

- **Runs.** Consecutive sections that are not skipped form a run. A skipped section ends the run before it with its caps and starts the
  next with its own. A closed path with no skipped section is one run without ends: no caps, and a joint where it closes.
- **Spans.** While `fits_each_section` is on, each section of a run is a span that fits on its own, so a corner never falls in the
  middle of a panel; off, the run is one span. A section of no length holds nothing.
- **Joints.** Where two spans of a run meet lies a piece centred on their node: the corner slot's at a corner node, else the separator's.
  It takes half its length from each span, takes the tangent it arrives with, stays rigid and keeps its own length.
- **A piece's length** is its mesh's size along X (LOD 0's, under its import transform) less its entry depths, or the `length` setting,
  times the node's scale factor where it starts. Pieces lie end to end by their lengths. A separator of length 0 with panels modelled
  post centre to post centre needs no entry depth: the rails end inside the post, at corners too. `entry_depth_start` and
  `entry_depth_end` say how far a mesh reaches past its length into its neighbour.

**The fit** (`fit`) finds how many middles and what fills the rest. Every item of the sequence has a length `A + B * x`, and `x` is solved so
the sequence ends exactly at the span's end:

- `stretch`: a whole number of middles (`rounding`: nearest, up or down), `x` scaling the slots that stretch;
- `resize_ends`: whole pieces, `x` resizing the first, the last or both middles (`resized_ends`), by scaling them or by cutting
  the mesh off with the count rounded up (`resize_method`);
- `fixed`: whole pieces, `x` being the leftover, shared out by `align`:

```
start    ######............   gap at the end
end      ............######   gap at the start
center   ......######......   equal gaps at both ends
between  ##......##......##   equal gaps between pieces, flush at both ends
around   ..##....##....##..   half a gap at each end
evenly   ...##...##...##...   the same gap everywhere
```

`uses_count` and `count` ask for exactly so many middles, as far as they fit (a count that does not fit comes down to what does, by what it overran or how far under its
smallest size it came, not one at a time, and at most `MAX_COUNT_RETRIES` tries: past that the span shows only its caps). `uses_gap` and
`gap` keep a free space between one middle and the next ("a lamp every 12 m"), a separator's length not counted, with as many pieces as
that leaves room for. `caps_at` says whether caps sit at the ends of the `path` or next to the pieces of the `run` when a fixed run
leaves a gap. A middle of no length fills nothing, unless a fixed fit spaces it with a gap or a count.

**When one middle does not fit** (`when_too_short`): `squeeze` it down to `minimum_stretch`, the `single` slot, `caps_only` or `nothing`.

**The walk.** A rigid piece's two ends lie on the curve, so the next piece starts where a chord of its length ends: chords are shorter than
arcs, and `x` is solved over the real walk (a secant kept between what is known to end short and what is known to overrun). A bent piece and a
gap are measured along the curve. The last piece lands exactly on the span's end, but where chords outgrow a path's hairpins: a walk's end jumps
there, and it may end short by less than a piece's share.

**Picks.** `pick`: `random` by weight, from the seed, or `in_order`. `PathObjectLayout_GetRandom` gives a value from the seed, the slot, the
span's first section, the piece's index and a salt, so the same inputs give the same fence on every machine and every load, and a change in one
section leaves the others' choices alone.

## Pieces

**The mesh's convention:** it runs along its own +X, stands on its Y = 0 and is centred on its Z = 0, with its +Z to the right of the path's
direction; where it sits along X does not matter (`references/asset_structure.md`, "Path objects", has the Blender side). Its body's middle
lands on the piece's middle. A stretched piece scales along X.

**A piece stands by the object's +Y,** as near as the frame's right lets it. The curve's own up is square to its tangent, so on a climbing path it leans along the path:
a post would lean with it, and a shear would be a tilt. Only a roll leans a piece, to its side. A frame whose right lies along +Y keeps the curve's up.

A turned-round piece (`flip`, or the left side) is the same piece turned 180 degrees about its up through its middle, so its faces never
mirror. A `rotation_offset` and a `yaw_jitter` turn it through its middle too, wherever the mesh's origin lies. `side` stands a piece `on_path`,
`left`, `right`, `both` or `alternating`, `side_offset` metres off (times the node's width). `aligned_to` is the `path`, or the `world`: the object's
axes with only the place from the path (lamps all facing one way). `position_offset`, `position_jitter` and `scale_jitter` do what they say, from the seed.

**At a corner** a side turns by its mitre (the two rights' middle, as long as makes the two sides meet, at most 4 times): what stands on either side of
the corner meets at the point where the side's two lines cross, on the outside of a turn and the inside. A corner is any node where two sections of a run
meet, a closed run's closing node too (the span's `HasSectionBefore` and `HasSectionAfter`, `WorldPathObjectsRigid_FindCorner`). Past that point on the
inside a bent piece's slices are left out, so its legs do not run over each other.

**`curve`: rigid** (`PathObjectPlacement_MakeCopy`): a straight copy with its ends on the curve, drawn as a `model_instances` copy with its LODs,
tints and bands. How it takes a slope is its `slope`:

| `slope` | What it does | For |
|---|---|---|
| `shear` | The up stays as it is and the length climbs: no vertical moves out of vertical (`PropertyInstanceEntry.Shear`). A chord steeper than `PATH_OBJECT_MAX_SHEAR_SLOPE` (80 degrees) tilts instead. | Panels: they stay level with their posts. |
| `tilt` | The piece turns with the slope. | What lies on the ground. |
| `step` | The piece stays level, at the height `step_anchor` says: half way between its ends, the higher's or the lower's. If it does not stretch it keeps its own length, so a post stays as thick on a slope. | Posts, lamps, stepped panels. |

**`curve`: bent** (`PathObjectBend_AppendMesh`): the game slices the mesh along its length, so a plain box rail bends with no edge loops and one
model works both ways. Slices lie `slice_spacing` apart at LOD 0, closer where the path bends, every section's samples included, so a corner in a
fence that fits as one run comes out as a slice with a mitre. Two pieces that share an end share that slice exactly, on the ground too, so they meet.
Each LOD of the world model is bent on its own over every 2^LOD-th slice. A point of the mesh between two slices goes to the lerp of their positions by its X, plus its Y and Z along the lerp
of their axes, so neighbouring slabs meet exactly. What reaches past the first or the last slice (an entry depth) carries on straight. Normals and
tangents go through the same frame by cofactors. Output is unindexed. A bent piece takes no rotation offset or yaw jitter: a frame per slice has
no one place to turn about.

**A cut end** makes a generated mesh even in a rigid slot: only a mesh can be cut, and the cut plane hides in the cap. A cut lies at the piece's own end
along the path, so on a turned piece it takes the other end of the mesh. A model's `tint` reaches a part through its child's tint.

**A prefab in a slot.** A model record names a `prefab` in place of a `world_model`, with its `values` and `randomizes_enums`: a lamp with its light. Each such piece is a runtime child with a prefab component ([Prefabs.md](Prefabs.md)), an object rather than an instance copy, its id derived from the path object's, its slot, its model and its index. It takes a rigid piece's place, a shear taken as a step since an object has no shear, at an even scale however the slot stretches or the node widens; its length is the slot's `length` setting or 0, it never bends or is cut, and a slot's collision and a record's `tint` do not reach it: it collides and looks as its prefab says. The prefabs system makes its objects at its next update.

**A changed definition.** A loaded asset never changes by itself, so `WorldPathObjects_Rebuild` lets go of what the system holds and builds every path object again, at its next update, from the definitions as they are now: what a changed path object says, and the picks of a changed prefab's pieces. It writes only what came out different, like any rebuild.

## Snapping

`snaps_to_terrain`, `ground_offset`, `ground_search_height`, `ground_search_depth`, `ground_target` (`terrain` alone, or anything solid on
`ground_layers`) go through `PathGround_Project`. A section's node can say otherwise for its section
(`PathCurveSection_ResolveSnapsToTerrain`). Rigid pieces snap their ends and bent pieces their slices, off to the piece's side first. The
ground is looked for along the world's Y, from a point's place in the world, and the search ignores the path object's own subtree.

## Collision

Per slot (`collision`, with the layer and the mask as override pairs, so posts can block while rails do not; a world model's trigger flag holds for its
copies):

| `collision` | Rigid piece | Bent piece |
|---|---|---|
| `none` | Nothing. | Nothing. |
| `model` | The world model's set (`collision_set` or its default) under the copy's matrix. | The same, along the piece's chord, by the piece's own jitter. |
| `mesh` | As `model`: a rigid piece has no mesh of its own. | The generated mesh, one two-sided mesh shape per part. |
| `boxes` | One box around the copy (`box_size`, `box_offset`). | Boxes along the piece, `box_spacing` apart. |

A copy's shapes go to its child's collider as **placed shapes** (`WorldColliderPlacedShapes`,
[WorldCollision.md](WorldCollision.md), "Placed shapes"): a matrix per shape, borrowing the world model's hulls and meshes. A generator that
needs collision per copy uses the same block.

## The system

`WorldPathObjects_Update`, once a tick, after the transform drivers and before `WorldCollision_Step`. At load, after a first
`WorldCollision_Sync`, so the first frame stands on the ground.

**What it makes.** Under each path object's object, runtime children (`WorldObject_SetPersistent` false: world files never save them, though they
do use up object ids):

- `path_object_copies`, one per slot and model: a `model_instances` component with the rigid copies and a collider with their placed shapes.
- `path_object_part`, one per slot, model and about 32 m of path (`WORLD_PATH_OBJECTS_PART_LENGTH`): a model component with a generated model, which has
  its own lights and culling, and a collider when its slot collides as `mesh`.
- `path_object_piece_<slot>_<model>_<index>`, one per piece of a model that names a prefab: a prefab component and the piece's place.

The children take the settings in effect as ordinary rows (the render and LOD rows a slot shares by key with `model_instances`) and the object's tint,
so the renderer, the collision world and the status know nothing of path objects.

**Space.** What it makes lies in its own object's space, as if the path's nodes were given there: a child with no transform of its own lies exactly on its
parent's path, and moving it moves everything along. A path object reads the nearest path at or above its object
(`WorldPathComponent_FindForObject`): a lone fence keeps its path and its settings on one object; when one path feeds several things, each is a child.

**What it watches:**

| Change | Noticed by | Costs |
|---|---|---|
| A row of the component | its revision | A rebuild. |
| The path: a node, a setting, found, lost or replaced | `WorldPathWatch` | A rebuild. |
| The object's tint | a comparison | A rebuild that writes only the tint. |
| The object or the path's object moved, while anything is snapped | its transform revision (the path's object lies at or above it, so that counts too) | A rebuild. |
| The static world under its points, while anything is snapped | `WorldCollision_HasGroundChanged` (terrain alone, for what snaps to terrain alone) | A rebuild. |

A quiet path object costs a few integer comparisons. **A rebuild writes only what came out different** (copies, shapes, generated models, rows), so the
renderer's and the collision world's own rebuilds follow real changes only. That is also what keeps a snapped object from looping on its
own colliders: they change the ground under it, one rebuild follows, it writes nothing, and it rests. Shapes compare by their fields
(`CollisionShape_Equals`): their padding bytes differ from build to build. A disabled object waits until it is enabled.

**Going.** `WorldPathObjects_Destroy` removes what it made and syncs the collision world: the placed shapes borrow from world models held under its asset
user. It goes before the collision world and the world. A child removed from outside comes back at the next rebuild. A path object under another's runtime
child goes with that child.

**Warnings**, once per path object: a missing asset (not retried until its name changes), a missing model, each limit reached, a middle of no length. All of a system's path objects give at most `WORLD_PATH_OBJECTS_MAX_WARNINGS` (1024) together: past them it stays quiet, and says so once.

## Limits

| Limit | Value | Past it |
|---|---|---|
| Pieces of one path object | `WORLD_PATH_OBJECTS_MAX_PIECES` 262 144 | The rest are left out; pieces already laid lie at their own lengths, and the rest of the path is not walked at all. |
| Items one layout walks, in every try | `PATH_OBJECT_LAYOUT_MAX_WALKED_ITEMS` 16 000 000 | The same: a hostile count that fits nothing cannot walk a path of many sections for ever. |
| Of a world (`WorldPathObjectsLimits`, `WorldPathObjects_SetLimits`) | 4 000 000 pieces, 32 000 000 vertices, 4 000 000 placed shapes, 65 536 children by default | More path objects make less or nothing, until a change rebuilds them. A small hostile file cannot ask for any amount of memory. |
| Points of a curve a rebuild's bent pieces look at | `WORLD_PATH_OBJECTS_MAX_BENT_WORK` 8 000 000 | The rest stay unbent. |
| Samples of a section | `WORLD_PATH_OBJECTS_MAX_SECTION_SAMPLES` 65 536 | A tinier spacing is coarsened to it. |
| Looks for the ground, as rays times the object's own placed shapes | `WORLD_PATH_OBJECTS_MAX_GROUND_WORK` 400 000 000 | The rest stay off the ground. A ray may have to pass over each of them: about a minute at worst, under colliders made huge on purpose, and nothing at all under ordinary ones. |
| A copy past `WORLD_POSITION_LIMIT`, or past a list's 262 144 | | It is left out alone; so is a part whose place lies past the limit. |
| Vertices of one generated mesh, of one rebuild | 1 048 576, 8 388 608 | The rest of the mesh is left out. |
| Placed shapes of a child | `WORLD_PATH_OBJECTS_MAX_PLACED_SHAPES` 65 536 | The rest do not collide. |
| Slices of a bent piece | `PATH_OBJECT_MAX_SLICES` 4096 | Every n-th is kept, the last always. |
| Models of a slot | 64 | The rest are left out. |
| Models and slot records of a list | `PATH_OBJECT_MAX_MODELS` 256, `PATH_OBJECT_MAX_SLOT_RECORDS` 64 | The list is refused. |

Two path objects snapping on each other's colliders (target `layers`) can climb: each rebuild moves the other's ground. Give one of them the terrain
target, or masks that leave the other out.

## Not built

Removing or swapping one piece by hand, kept by its distance along the path. Bending in the vertex shader: it would save memory, but
needs new shader variants in every pass.

## Test content

Objects 99 to 110 of the test world, generated by `asset_source/scripts/placeholder_path_objects.py`:

| Object | What it shows |
|---|---|
| 99 `walk_fence`, 100 `walk_lamps` (children of the walk, object 95) | The fence and the lamps over the test terrain: corners, curves, a slope. |
| 101 `park_hedge` (child of the park, object 96) | A bent hedge on a closed ring, colliding as its mesh. |
| 102 `floating_fence` | The walk's asset with `snaps_to_terrain` off in its placement alone. |
| 103 `bent_railing` | A plain rail bent along a tight curve, with posts every third metre and boxes. |
| 104 `slope_shear`, 105 `slope_tilt`, 106 `slope_step` | Three fences down the valley's side. |
| 107 `fixed_between`, 108 `fixed_center`, 109 `cut_ends` | Two alignments and cut ends. |
| 110 `corner_fence` | A heavier post at its corners, in its placement alone, and a skipped section with caps on both sides. |

**Keys** (`WorldTestPathObjects`, listed with the test frame's keys in [WorldRuntime.md](WorldRuntime.md)): Control + Backspace steps the walk's fence's fit;
Control + Shift + Backspace how its panels take a slope; Shift + Backspace moves a node of the walk, and the fence and the lamps follow. I logs the counts.
