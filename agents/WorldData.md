# World data layer

The rendering-independent model of a 3D world: its objects, their hierarchy, their components, their tags and data, and
its environment settings. No renderer is required to build, change or tear down a world; the world renderer reads it
separately and never writes to it. Worlds are saved and loaded as JSON and GHDF by [WorldFile.md](WorldFile.md) (the
format: `references/world_structure.md`), built on the property tables. Tags, typed object data and queries have
their own doc: [ObjectData.md](ObjectData.md).

This one doc covers the whole cohesive layer; every symbol has full Doxygen in its header.

## Modules

| Module | Files | What it is |
|---|---|---|
| WorldObject | `include/WorldObject.h`, `source/WorldObject.c` | The one object type: id, name, parent, transform (relative to the parent), tint, enabled, visible, mobility, and its component slots. |
| WorldComponent | `include/WorldComponent.h`, `source/WorldComponent.c` | The component base, the component types and the registry entry that describes each (`WorldComponentTypeInfo`). |
| WorldModelComponent | `include/WorldModelComponent.h`, `source/WorldModelComponent.c` | A world model by asset name ([WorldModels.md](WorldModels.md)), the variant drawn, and the settings this placement overrides (explicit flags next to its own values: outline, crisp, casts shadows, shadow distance, fixed shadow distance, shadow only, two-sided shadows, shadow LOD, haze, keeps the sky out, draw distance, fixed draw distance; draw and shadow distance multipliers), with the pure function that resolves the settings in effect. |
| WorldModelInstancesComponent | `include/WorldModelInstancesComponent.h`, `source/WorldModelInstancesComponent.c`, `source/WorldModelComponentInternal.h` (the rows it shares with the model component) | Many copies of one world model: the model component's settings, shared through macros (all but `omit_pixelation`: copies are never crisp), and the copies as an `InstanceList` (each a double position, a rotation, a scale and a tint), with the pure functions that resolve the settings and the LOD settings in effect. |
| WorldSpriteComponent | `include/WorldSpriteComponent.h`, `source/WorldSpriteComponent.c` | A world sprite by asset name ([WorldSprites.md](WorldSprites.md)), bound by the world with its animation's playback instance, and every setting this placement overrides (explicit flags next to its own values: orientation, pivot, size, flips, alpha mode, lighting, decal, render flags, shadow distance, fixed shadow distance, shadow only, draw distance, fixed draw distance; draw and shadow distance multipliers), with the pure function that resolves the settings in effect. |
| WorldLightComponent | `include/WorldLightComponent.h`, `source/WorldLightComponent.c` | A point light (colour, intensity, range, casts shadows). |
| WorldAmbientZoneComponent | `include/WorldAmbientZoneComponent.h`, `source/WorldAmbientZoneComponent.c` | A volume that sets a room's own light (a box or a sphere: its light, the sky's share in it, the eye's exposure bias, its priority and its air's haze). |
| WorldColliderComponent | `include/WorldColliderComponent.h`, `source/WorldColliderComponent.c` | How the object collides ([WorldCollision.md](WorldCollision.md)): its world model's collision set (or one primitive of its own), its layer, mask and trigger flag (each overriding the world model's default with an explicit flag), a local offset, rotation and scale, and a rotation mode, with the pure function that resolves the settings in effect. |
| WorldCharacterComponent | `include/WorldCharacterComponent.h`, `source/WorldCharacterComponent.c` | The player or an NPC ([Characters.md](Characters.md)): a character asset by name and every setting this placement overrides (explicit flags next to its own values), the input its driver gives it and the state it is in; the first transform driver. |
| WorldAnimatorComponent | `include/WorldAnimatorComponent.h`, `source/WorldAnimatorComponent.c` | What plays the object's skeletal animation ([Animation.md](Animation.md)): the clip row and how it plays, the placement's own libraries, and the pose the animation system publishes. |
| WorldTerrainComponent | `include/WorldTerrainComponent.h`, `source/WorldTerrainComponent.c` | A terrain asset by name ([Terrain.md](Terrain.md)) and the settings this placement overrides (explicit flags next to its own values: outline, casts shadows, shadow distance, fixed shadow distance, haze, draw distance, fixed draw distance, collision enabled, layer and mask; draw and shadow distance multipliers), with the pure function that resolves the settings in effect. |
| WorldEnvironment | `include/WorldEnvironment.h`, `source/WorldEnvironment.c` | Per-world sky/sun/moon/haze/effect settings, described by a property table (defaults, validation, copying, JSON, GHDF), and the sky's model: the sun, the moon, the stars' rotation, the two lights. |
| World | `include/World.h`, `source/World.c`, `source/WorldData.c` (tags and data, the tag index), `source/WorldHierarchy.c` (parents, world matrices, effective flags, subtrees; also the `WorldObject_` functions about them) | Owns the objects and their components, mints ids, keeps the id map, the hierarchy and the per-type component arrays, borrows the key registry, keeps the tag index and the data byte count, holds the environment and its own asset user. |
| WorldTransform (private) | `source/WorldTransform.h`, `source/WorldTransform.c` | The pure maths of transforms, in double: compose (DrawModelEx's order), the one decomposition back to position / Euler XYZ / scale, a local matrix under a parent, a bound on a matrix's stretch. Used by the hierarchy, the renderer's cache, collision and the characters. |
| WorldQuery | `include/WorldQuery.h`, `source/WorldQuery.c` | Searches over the objects by tags, data, components, flags and place ([ObjectData.md](ObjectData.md)). |

`source/WorldInternal.h` is the layer's private header: the object's construction (objects are made by the world only),
the object lookup (`WorldInternal_FindObject`), `WorldInternal_ComponentAfterSet` (the hook every component table calls
after a change), the data bookkeeping (`WorldInternal_SyncObjectData` after a table write replaced an object's data,
`_ForgetObjectData` before removal, `_GetTagMembers` for queries), the hierarchy's bookkeeping
(`WorldInternal_MarkTransformChanged`, `_SyncObjectParent` after a read or copy, `_CollectSubtree`, `_DetachFromParent`,
`WorldObject_CheckTransform`), the world's asset manager, user and logger for components that load assets, small
helpers, and two test seams: `WorldInternal_ConstructWithTypes` (below) and `WorldInternal_GetWorldMatrixRecomputeCount`.
Not public API.

## Objects

`WorldObject` is one concrete struct (no subclasses: models, sprites and lights are components). Every object has:

- `_id` (unique in its world, never 0, assigned at creation, never changed), `_name` (owned, may be NULL, at most
  `WORLD_OBJECT_NAME_MAX_LENGTH` = 256 bytes of valid UTF-8);
- `_parentId` (0 for a root) and the hierarchy's links (`_parent`, `_firstChild`, `_lastChild`, `_previousSibling`,
  `_nextSibling`, `_childCount`); see "Hierarchy";
- the transform, **relative to the parent** (world values for a root): `_position` (a `Vector3Double`, see "Positions
  are doubles"), `_rotation` (Euler XYZ in radians, degrees in JSON), `_scale`; the cached `_worldMatrix` (a
  `Matrix4Double`) with `_isWorldMatrixDirty`, and `_transformRevision`;
- `_tint`: a `RenderColor` (brightness and opacity in [0, 1]; the renderer draws the opacity as a dither, and blended or
  additive sprites as alpha: WorldRuntime.md, "Visibility");
- `_isEnabled` (default true): false makes **every system** skip the object and its subtree: the renderer,
  collision and scripts' searches;
- `_isVisible` (default true): false makes **the renderer** skip it and its subtree (models, sprites and lights); they
  still exist for collision and interaction (invisible walls, hidden triggers);
- `_mobility`: `WorldMobility_Static` (default: scenery that does not move in play) or `WorldMobility_Movable`
  (see "Ready for things that move");
- `_revision`: advances on every property change (runtime only), except the tags and data;
- `_data`: its tags and custom data, a `DataMap` (NULL while empty; `WorldObject_GetData`, `WorldObject_HasTag`),
  changed only through the world's data functions ([ObjectData.md](ObjectData.md)); `_dataRevision` advances on every
  change of them (`WorldObject_GetDataRevision`), so script data never makes the renderer recompute anything;
  `_countedDataBytes` and `_indexedTagCount` are the world's bookkeeping;
- `_components[WORLD_MAX_COMPONENT_TYPES]`: one slot per component type, NULL when absent
  (`WorldObject_GetComponent`);
- `_world`: the world that owns it. **A World must never be moved or copied once constructed** (objects and
  components point back to it);
- `_isPersistent` (default true): whether world files save it. A runtime flag, not a property (not saved, no
  revision): objects made for the session (the player, spawned NPCs, effects) turn it off with
  `WorldObject_SetPersistent`, and the writers leave them out with their subtrees ([WorldFile.md](WorldFile.md)).

Every field is reached through getters and validating setters (`WorldObject_SetPosition`, `_SetEnabled`, ...). The
setters check through the object's property table (`Properties_CheckValue`), so the rules live in the table only, and
`Properties_Set` / the JSON and GHDF readers / `Properties_CopyTo` work on objects too; either way the revision
advances (the table's `AfterSet`). The `data` row is `ReadOnly` for `Properties_Set`; a read or a copy (whose
`AfterSet` gets no row) advances both revisions and has the world recount the object's bytes and re-index its tags.

## Hierarchy

An object may have a **parent**; its position, rotation and scale are then relative to
the parent's, so it follows it: moving a desk moves the cup on it, a lamp carries its light, a door's parts move as one.
The world keeps the links (`source/WorldHierarchy.c`); the persisted value is the object table's `parent` row.

- **`World_SetParent(world, childId, parentId, keepWorldTransform)`** places an object under a parent (it becomes the
  parent's last child), or makes it a root (`parentId` 0); its subtree comes with it. Setting the parent it already has
  changes nothing; otherwise the object's revision advances and its subtree is marked dirty. Refused, with nothing
  changed (`ErrorCode_InvalidOperation`): an unknown object or parent; the object itself; a parent inside the object's
  own subtree (a cycle); a hierarchy of more than **`WORLD_HIERARCHY_MAX_DEPTH` = 64 levels** (a root is level 1; a
  moved subtree counts with its own height; AGENTS.md rule 5). With `keepWorldTransform` (the editor's default) the
  object's local transform is rewritten so its world placement stays (within a few of a double's steps: 2 nm at
  10 000 km; under a parent that would shear it, its nearest unsheared form); that is refused for a new parent whose
  world matrix cannot be inverted (a zero scale on an axis), and for one so far away that the local position would pass
  `WORLD_POSITION_LIMIT`. Without it the local values stay, so the object moves with the change.
- **Reading it:** `World_GetParent(world, id, &parentId)` (0 for a root), `World_GetChildren(world, id, outIds)`
  (appends the ids in attach order), and the pointer walk `WorldObject_GetParent`, `_GetFirstChild`,
  `_GetNextSibling`, `_GetChildCount` (valid until the hierarchy changes; do not change it while walking).
- **World matrices.** `WorldObject_GetWorldMatrix(object)` (a `Matrix4Double`) is the object's local matrix
  (`WorldTransform_Compose`: scale, then the Euler XYZ rotation, then the translation, raylib's `DrawModelEx` order)
  times its parent's world matrix (`Matrix4Double_Multiply(&local, &parentWorld)`, raylib's order, in double); a root's
  is its local one. Each object caches its own:
  - a change of a transform row (a setter or any table write: `Properties_Set`, a read, a copy) or of the parent marks
    the object and its **whole subtree** dirty and advances each one's `_transformRevision`
    (`WorldObject_GetTransformRevision`: equal values mean the world matrix is the one a system saw; the collision
    world uses it);
  - **`World_UpdateTransforms(world)`** recomputes the dirty ones, each once, parents first, and stores them. It is the
    tick's step 7 (after everything that moves objects, before the renderer); with nothing dirty it returns at once,
    otherwise it is one pass over the objects;
  - **reading never writes**: a dirty matrix is computed on the fly from the nearest clean ancestor down and not
    stored (at most 64 products), so the renderer may read it (AGENTS.md rule 3) and every read is correct whether the
    update ran or not.
  `WorldObject_GetWorldPosition` (a root's own position), `WorldObject_GetWorldTransform` (position, Euler XYZ, scale:
  a root's own values exactly, a child's through `WorldTransform_Decompose`), and the id-based `World_GetWorldMatrix`,
  `_GetWorldPosition`, `_GetWorldRotation`.
- **One decomposition.** Rotations stay Euler XYZ in the data; everything composes with matrices, and the way back
  (a world rotation, keeping the place on reparenting) is `WorldTransform_Decompose` only: the scale is each world
  axis's length, the rotation that of the axes made orthonormal (X, then Y; Z their cross product; zero-length axes
  completed from the others), read as Euler XYZ with no loss at gimbal lock. A matrix fixes only whether it mirrors,
  not which axis carries a negative scale ((-1, -1, 1) is a half turn about Z), so the decomposition takes a sign hint:
  the product of the scales' signs along the object's chain, axis by axis. A child then keeps the signs a root with its
  scales would have (billboards, which drop the turn, flip by them), and `keepWorldTransform` keeps an object's world
  signs (less its new parent's). Without a hint the mirror goes onto X. A
  child turned inside a parent scaled unevenly is **sheared**, which a position, rotation and scale cannot hold: the
  matrices keep the shear (models draw it exactly), the decomposition gives the nearest placement without it (sprites,
  `keepWorldTransform`). Give groups no uneven scale of their own (the test world's desk is an empty object holding
  its scaled parts).
- **Effective flags.** `WorldObject_IsEffectivelyEnabled` / `_IsEffectivelyVisible` (and `World_IsEffectivelyEnabled`
  / `_IsEffectivelyVisible` by id): the object and every ancestor. A disabled parent disables its subtree, an invisible
  one hides it; the children's own flags stay. Systems read these (the render cache, queries, the test frame's
  animation step), not the object's own flags. Opacity and mobility do not propagate.
- **Removal.** `World_RemoveObject` removes the object's **whole subtree, children first**, like the UI's
  `Widget_Destroy`: every component of the subtree is detached (its `OnDetach` hook; each object after its
  descendants) while the hierarchy is still in place, so a hook sees its object where it is; then the subtree is
  unlinked, the per-type arrays and the object list drop it in one pass each (the others keep their order), and the
  objects are destroyed. One call costs one pass over the object list and over each array the subtree used, whatever
  its size. `World_Deconstruct` likewise runs every hook (each root's subtree children first) before destroying
  anything. Hooks must not change the hierarchy (`WorldComponent.h`).
- **Sockets.** An object may hang on a named **socket** of its parent's world model (a hand, a hook, a seat;
  [WorldModels.md](WorldModels.md)): **`World_SetParentSocket(world, childId, parentId, socket, keepWorldTransform)`**
  is `World_SetParent` with a socket (NULL: the parent itself; `World_SetParent` clears it). The object's local matrix
  is then its transform times the socket's frame in the parent's current pose (its animator publishes the frames,
  [Animation.md](Animation.md)), so a held cup follows the hand; `keepWorldTransform` keeps the placement relative to
  the socket's frame as it is now. Each pose step marks the parent's socket children dirty
  (`WorldInternal_MarkSocketChildrenChanged`). A socket the parent's pose lacks (no animator, no pose yet, another
  name) is the parent's own space. `World_TryGetSocketWorldMatrix(world, id, socket, &matrix)` reads a socket's world
  matrix in double. The row is `parent_socket` (wire id 13, an asset name of at most
  `WORLD_OBJECT_SOCKET_NAME_MAX_LENGTH` = 64 bytes, saved with the object); the next free wire id of the object table is
  14. In the collision step a pushable on a socket is its holder's (carried, not pushed out of the hand).
- **The `parent` row** (wire id 12, an `ObjectId`) is `ReadOnly` for `Properties_Set`: scripts and the editor go
  through `World_SetParent`. JSON and GHDF reads and `Properties_CopyTo` still fill it (a copy brings the parent: the
  copy becomes the original's sibling); the object table's `AfterSet` then has the world attach the object to that
  parent, keeping the local transform it read, with `World_SetParent`'s checks. A parent the world cannot take is
  **refused**: the object keeps its parent, the row goes back to it, `World_GetRefusedParentCount` rises and the first
  refusal is logged (once per world). Tables check each value alone, so this is where a loader learns of a bad parent.
  A copy from an object of **another** world brings that world's parent id, which here names another object or
  none: copy within one world, or set the parent afterwards.

## Positions are doubles

A float's step is 8 mm at 100 km and a metre at 10 000 km, so
everything that says **where** something is in the world is a double: an object's `_position` (`Vector3Double`), its
world matrix (`Matrix4Double`), and everything systems derive from them (the render cache's placements, query origins
and results, collision placements and poses, contact and hit points, characters' feet, ground points and eyes, the
camera's position). The types and their operations are WRFramework's (`wr/WRVectorMath.h`); `GameMath` converts to
and from raylib's types ([GameMath.md](GameMath.md)). **Rotations, scales, directions, normals, velocities, sizes and
offsets stay floats**: they mean the same anywhere.

- **The limit.** A position is refused beyond **`WORLD_POSITION_LIMIT` = 1e7 m** (10 000 km) on any axis, by the
  `position` row's rule, so setters, table writes, file reads and `World_SetParent`'s keeping of the place all check
  it. It applies to the local value (a child's position relative to its parent); a world 10 000 km across is far
  larger than anything the game builds, and doubles keep it to 2 nm there.
- **Converting to float.** Code that needs floats (the GPU, the narrowphase, a character's move) takes positions
  **relative to an origin near what it computes**, subtracted in double and rounded once
  (`GameMath_ToVector3Relative`, `GameMath_ToMatrixRelative`): the renderer relative to the camera
  ([WorldRuntime.md](WorldRuntime.md)), the collision world relative to a query's start or a pair's first body
  ([WorldCollision.md](WorldCollision.md)), the characters relative to their feet ([Characters.md](Characters.md)).
  `GameMath_ToVector3` of an absolute position is only for values known to be small (a child's local position).
- **Files** write positions as doubles: JSON with the shortest text that reads back the same double (a position written
  as a float's shortest text prints the same), GHDF as Double arrays; Float arrays from older saves still read
  ([WorldFile.md](WorldFile.md)). `vector3` data values are doubles too ([ObjectData.md](ObjectData.md)).
- **Far from the origin** a world plays as it does at it: the test frame's F11 moves everything 28 km and 141 km out,
  and the headless suites of the hierarchy, world files, collision, motion and characters run a second time shifted
  about 100 km along each axis (see "Tests").

## Components

A component is a struct that embeds `WorldComponent` first (`_type`, `_owner`, `_revision`, `_serial`). An object
carries **at most one component of each type**; a street lamp is one object with a model and a light.

| Type | Value | Name | Struct |
|---|---|---|---|
| `WorldComponentType_Model` | 0 | `model` | `WorldModelComponent` |
| `WorldComponentType_Sprite` | 1 | `sprite` | `WorldSpriteComponent` |
| `WorldComponentType_Light` | 2 | `light` | `WorldLightComponent` |
| `WorldComponentType_Collider` | 3 | `collider` | `WorldColliderComponent` |
| `WorldComponentType_Character` | 4 | `character` | `WorldCharacterComponent` (a transform driver; [Characters.md](Characters.md)) |
| `WorldComponentType_Animator` | 5 | `animator` | `WorldAnimatorComponent` ([Animation.md](Animation.md)) |
| `WorldComponentType_Terrain` | 6 | `terrain` | `WorldTerrainComponent` ([Terrain.md](Terrain.md)) |
| `WorldComponentType_ModelInstances` | 7 | `model_instances` | `WorldModelInstancesComponent` (the renderer's side in [WorldRuntime.md](WorldRuntime.md), "Model instances") |
| `WorldComponentType_AmbientZone` | 8 | `ambient_zone` | `WorldAmbientZoneComponent` (the renderer's side in [WorldRuntime.md](WorldRuntime.md), "Interior lighting") |

The values are explicit and never renumbered (world files save them). `WorldComponentType_Count` is the number
of standard types; `WORLD_MAX_COMPONENT_TYPES` (16) is how many a world can hold, room for the types still to come
(9 onward: the screen) and for test-only types.

**The registry.** Each type has one `WorldComponentTypeInfo` (a static constant of its module, handed out by
`Xxx_GetTypeInfo`; `WorldComponent_GetStandardTypeInfo(type)` maps the standard types): `Type`, `Name` (the key of its
block in world files and its label in the editor), `Size`, `Table` (its property table, whose `AfterSet` must be
`WorldInternal_ComponentAfterSet`), `IsTransformDriver`, and the hooks `Construct` (must call `Properties_Construct`,
which zeroes the whole struct, then set up runtime state), `Deconstruct`, `OnAttach(world, object, component)`,
`OnDetach` and `OnPropertySet(world, object, component, descriptor)` (descriptor NULL after a read or a copy).
`WorldComponentTypeInfo_Check` verifies an entry (a test runs it for every standard type). **A new component type is
one enum value, one registry entry (and its case in `WorldComponent_GetStandardTypeInfo`), a table and the hooks.**

**Lifetime.** Components exist only inside a world: `World_AddComponent` allocates the type's `Size`, constructs it,
sets the base, appends it to the world's array of that type, puts it in the object's slot, then calls `OnAttach`.
`World_RemoveComponent`, removing the object, or destroying the world calls `OnDetach`, then removes and destroys it.

**Changes.** Every property change advances the component's revision (`WorldComponent_GetRevision`), from the typed
setters and from every table write alike: they all end in `WorldInternal_ComponentAfterSet`, which advances the
revision and then calls the type's `OnPropertySet` with the world. That is how a component keeps runtime state in step
with its properties, whichever way they were written.

**Identity.** A revision restarts at 0 for every new component, and a removed component's memory is usually reused by
the next one added, so neither its address nor its revision tells a replacement from the component before it.
`WorldComponent_GetSerial` does: `World_AddComponent` gives each component a number no other component of its world
ever had (from 1, in add order). The collision world compares serials; a cache that keeps component pointers
across component-set changes should too.

**Downcasts.** `WorldModelComponent_FromComponent(component)` (and the sprite's and light's) returns the concrete
struct, or NULL for NULL or another type; `..._AsComponent` upcasts.

### Model component (`model_component`)
The component names a **world model** ([WorldModels.md](WorldModels.md)): the reusable definition of the thing
placed (its LOD models, draw distance, render defaults and variants). `_worldModel` (an asset name, NULL draws nothing;
changing it swaps the model and keeps the object), `_usesVariant` (false) and `_variant` (a variant name, kept while
`_usesVariant` is off; an unknown one draws the model's own materials and the renderer warns once).

The settings a placement can override (`WorldModelSetting`) are each a pair: an override flag, `_overrides...` (false:
the world model's value applies), and the placement's own value, kept while the flag is off: `_hasOutline` (true),
`_omitPixelation` (false: drawn crisp at full resolution over the pixelated image), `_castsShadows` (true;
false skips it in every shadow pass, it still receives shadows), `_shadowDistance` (600 m, above 0: how far from the
camera it still casts; while not overridden the world model's own applies, or without one its draw distance),
`_isShadowDistanceFixed` (false: the player's `shadow_distance_scale` applies), `_isShadowOnly` (false: true casts
without being drawn), `_hasTwoSidedShadows` (false: true casts from both faces of its triangles), `_shadowLod` (a
`uint32_t`, 0: the LOD it casts with in every shadow level while overridden; past the world model's last LOD, its last;
[WorldModels.md](WorldModels.md), "Shadows"), `_haze` ([0, 1000], 1: how hazy it looks), `_occludesSky`
(true: it keeps the sky's light off what lies under it, in the sky's visibility map of [WorldRuntime.md](WorldRuntime.md),
"Interior lighting"; off for grass, small plants, people and poles), `_drawDistance` (600 m, above 0) and
`_isDrawDistanceFixed` (false: the player's `draw_distance_scale` applies). `_drawDistanceMultiplier` (above 0, default
1) multiplies whichever draw distance is in effect, `_shadowDistanceMultiplier` (likewise) whichever shadow distance. The
flags are set with `WorldModelComponent_SetOverridden(self,
setting, isOverridden)` and read with `_IsOverridden`; `WorldModelComponent_ResolveSettings(self, modelSettings)` is the
pure function that gives the `WorldModelSettings` in effect. The getters of the values (`WorldModelComponent_HasOutline`
and the others) return the placement's own values, not the ones in effect.

**LODs** ([WorldModels.md](WorldModels.md), "LODs"): `_lodDistances` (the placement's own switch distances, a
`PropertyFloatList`: finite, above 0, rising, at most `WORLD_MODEL_MAX_LODS`; empty by default) behind its flag
`_overridesLodDistances` (`WorldModelSetting_LodDistances`: it applies only while the list holds one distance per LOD of
the world model; otherwise the world model's distances, and the renderer warns once), `_lodDistanceMultiplier` (above 0,
default 1: 2 keeps each LOD twice as far, more detail), `_isLodForced` (false) with `_forcedLod` (a `uint32_t`, any value:
past the world model's last LOD it draws the last; kept while not forced). Setters: `WorldModelComponent_SetLodDistances
(self, distances, count)` (a copy, checked by the table's rule), `_SetLodDistanceMultiplier`, `_SetIsLodForced`,
`_SetForcedLod`; `WorldModelComponent_GetLodDistances(self, &count)` borrows the list.
**`WorldModelComponent_ResolveLodSettings(self, worldModel, &settings)`** is the pure function that gives the
`WorldModelLodSettings` in effect (the switch distances times the multiplier, the forced LOD clamped, the world model's
hysteresis and fade time); it returns false when the placement's own list is on but does not fit.

The component holds no asset: the renderer resolves the world model by name and resolves it again when the name
changes (it watches the revision, then compares the name), and the variant again when `uses_variant` or `variant`
changes.

### Model instances component (`model_instances_component`)
Places many **copies** of one world model: trees, bushes, posts, fence pieces, later scattered things and grass. Its
table shares the model component's rows through the `WORLD_MODEL_PLACEMENT_ROW_*` macros of
`source/WorldModelComponentInternal.h` (the names, wire ids, defaults, rules and categories are the model component's):
`world_model`, `uses_variant` / `variant`, the override pairs `has_outline`, `casts_shadows`, `shadow_distance`,
`is_shadow_distance_fixed`, `is_shadow_only`, `has_two_sided_shadows`, `shadow_lod`, `haze`, `occludes_sky`,
`draw_distance`, `is_draw_distance_fixed`, the multipliers `draw_distance_multiplier` and `shadow_distance_multiplier`,
the LOD rows (`lod_distances`
with its flag, `lod_distance_multiplier`, `is_lod_forced` / `forced_lod`), **all but `omit_pixelation`**: copies are
never crisp (`WorldModelInstancesComponent_SetOverridden(self, WorldModelSetting_OmitPixelation, ...)` is refused, and
`ResolveSettings` gives `OmitsPixelation` false). Every setting applies to every copy; the variant too.

`instances` (wire 22) is an `InstanceList` ([PropertyDescriptors.md](PropertyDescriptors.md)): each copy's **position**
relative to the object, in double ("Positions are doubles": floats only once the renderer makes it relative to a cell),
its **rotation** (Euler XYZ in radians, degrees in JSON), its **scale** (three factors) and its **tint** (a colour whose
alpha is the copy's opacity, multiplied by the object's). At most `WORLD_MODEL_INSTANCES_MAX_COUNT` (262 144) copies;
every position within `WORLD_POSITION_LIMIT` of the object, every rotation and scale finite. Defaults: none.
`WorldModelInstancesComponent_SetInstances(self, copies, count)` copies the list (checked by the row's rules), `_GetInstances
(self, &count)` borrows it, and `_GetInstancesRevision` advances with every change of it (a typed set, a table write, a
read or a copy), so the renderer sorts the copies into cells again only then. `ResolveSettings` and
`ResolveLodSettings` are the model component's functions for this table.

Not a transform driver. The object's transform places all the copies (moving it moves them without sorting them again),
its tint and opacity multiply every copy's, and hiding or disabling it hides them all. Generated copies go in
runtime objects (`WorldObject_SetPersistent(object, false)`), so they are never saved; the test frame's pebble field is
one.

### Sprite component (`sprite_component`)
The component names a **world sprite** ([WorldSprites.md](WorldSprites.md)): the sprite animation to play and
how it stands, is lit and is seen. `_worldSprite` (an asset name, NULL shows nothing; changing it swaps the sprite and
keeps the object), the runtime `_animationInstance` (`SpriteAnimationInstance` by value: the playback state game logic advances and the renderer reads;
`WorldSpriteComponent_GetAnimationInstance`) and the runtime `_boundWorldSprite` (the world sprite it plays,
`WorldSpriteComponent_GetBoundWorldSprite`).

**Every setting can be overridden**: for each `WorldSpriteSetting` an override flag (the
array `_overrides`, false: the world sprite's value applies) and the placement's own value, kept while the flag is off.
The values and their defaults (the renderer's side is [WorldRuntime.md](WorldRuntime.md), "Sprites"):
- **Shape:** `_orientation` (`WorldSpriteOrientation`, declared in `WorldSprite.h`: `billboard` faces the
  camera, the default; `axis_billboard` turns only around the object's local Y axis; `fixed` uses the object's rotation,
  the image in its local XY plane facing +Z), `_pivot` (the point of the image at the object's position: (0, 0) bottom
  left, (1, 1) top right; default the centre, (0.5, 0) for things standing on the ground), `_pixelsPerUnit` (> 0,
  default 32: the world size is the frame's pixels divided by it, times the object's world X and Y scale; the Z scale
  does nothing; position, axes and scale are the object's world ones, through its parents), `_isFlippedX` /
  `_isFlippedY` (mirror the image about the pivot). The image is two-sided.
- **Transparency:** `_alphaMode` (`WorldSpriteAlphaMode`: `cutout`, the default: opaque where the texture's alpha
  reaches `_alphaCutoff` (0.5), depth-correct, casts shadows, outlined; `blend`: soft edges, sorted, its alpha times the
  object's opacity; `additive`: adds its light).
- **Lighting:** `_isLit` (true: lit like the models; false: its colours glow as they are, times `_emission` (>= 0,
  default 1), still hazed), `_lightWrap` ([0, 1], default 1: how far light wraps around a billboard or axis billboard,
  0 a flat card, 1 a round thing half lit from the side; fixed sprites ignore it), `_castsShadows` (true; only cutout
  sprites that are not decals cast).
- **Decal:** `_isDecal` (false; takes effect only with the fixed orientation, `WorldSpriteSettings_IsDrawnAsDecal` on
  the settings in effect: drawn over the opaque world, no shadow, no outline) and `_decalOffset` (>= 0, default 0.01
  metres: lifted off its surface along its normal, toward the viewer's side).
- `_hasOutline` (true) and `_omitPixelation` (false) apply to cutout sprites that are not decals.
- **Draw distance:** `_drawDistance` (150 m, above 0) and `_isDrawDistanceFixed` (false: the player's
  `draw_distance_scale` applies); `_drawDistanceMultiplier` (above 0, default 1) multiplies whichever draw distance is in
  effect, the placement's, the world sprite's or its kind's (150 m, decals 60 m).
The flags are set with `WorldSpriteComponent_SetOverridden(self, setting, isOverridden)` and read with
`_IsOverridden`; `WorldSpriteComponent_ResolveSettings(self, spriteSettings)` is the pure function that gives the
`WorldSpriteSettings` in effect. The getters of the values (`WorldSpriteComponent_GetPivot` and the others) return the
placement's own values, not the ones in effect. There are no cross-field rules in the tables, so a combination that
does not apply (a "decal" billboard, crisp smoke) is stored as set and simply has no effect.

**Binding.** The playing sprite always matches the name. `OnAttach` binds it, and `OnPropertySet` binds it again
whenever the name differs from the one it was bound for (`_boundName`, an owned copy): after
`WorldSpriteComponent_SetWorldSprite`, `Properties_Set`, a JSON or GHDF read, or `Properties_CopyTo`. Binding loads the
world sprite (and with it, as its dependency, its animation) under the world's asset user and restarts the instance
from the animation's defaults (dropping its event subscriptions: subscribe after setting the name). Setting the name it
already has changes nothing (playback goes on). A world without an asset manager, a NULL name or a failed load leaves
the instance empty and the bound world sprite NULL (it draws nothing); a failure is logged once, when it happens
(`World: the sprite of object N ("name") shows nothing: world sprite ...`). The world sprites stay held until the world
is destroyed, after every component (asset holds are not counted, so releasing one on a rename could unload it under
another sprite; a `// TODO:` in `source/WorldSpriteComponent.c` marks the release pass that long-lived edited worlds may
need).

### Collider component (`collider_component`)
How the object takes part in collision (the shapes, the layers and the collision world are in
[WorldCollision.md](WorldCollision.md)). The rows, in order: `is_enabled` (true); `overrides_collision_set` /
`collision_set` (off: the world model's default set; a door swaps sets to swap hitboxes); `uses_own_shape` (one
primitive of its own instead of the sets) with `shape_type` (`sphere`, `capsule`, `box` (the default), `cylinder`,
`plane`), `shape_center`, `shape_rotation`, `shape_radius`, `shape_half_height`, `shape_half_extents`,
`shape_is_bounded`, `shape_plane_half_extents` and `shape_depth`; `overrides_layer` / `layer` (`prop`),
`overrides_collides_with` / `collides_with` (an `EnumSet` of layers: prop's default mask), `overrides_is_trigger` /
`is_trigger`; `offset`, `rotation`, `scale` (a local transform of all its shapes, the scale above 0); `rotation_mode`
(`full`, `yaw_only`, `none`). In the category "Motion": `is_pushable` (false: a movable object's collider is
kinematic, it pushes and is never pushed), `overrides_mass` / `mass` (1 kg, above 0, at most `COLLISION_MAX_MASS`),
`when_blocked` (`stop` or `push_through`), `is_continuous` (true: its motion is swept) and
`reports_persisting_contacts` (false). Without the flags the world model's `collision` section gives the set, layer,
mask, trigger flag and mass (`prop`, prop's mask, no trigger and 1 kg without one).
`WorldColliderComponent_ResolveSettings(collider, collision)` is the pure function that gives the settings in effect;
`SetOwnShape` sets the own primitive from a `CollisionShape`, all or nothing. No hooks: the collision world watches the
revision.

### Character component (`character_component`)
The player or an NPC (everything about characters is in [Characters.md](Characters.md)): `character` (the asset
name of a character, `characters/`), then for each of the character's 36 settings an override flag and the placement's
own value (`overrides_radius` / `radius`, ..., `overrides_interaction_layers` / `interaction_layers`), whose rows it
shares with the character asset's table. Its input (`CharacterInput`) and its state are runtime only. It is the first
**transform driver**: the character system (`WorldCharacters`) writes its object's position (the feet) and yaw once a
tick, and gives the object a capsule collider whose shape and motion rows it keeps in step.

### Animator component (`animator_component`)
What plays the object's skeletal animation (everything about animation is in [Animation.md](Animation.md)):
`plays_clip` and `clip` (the clip it plays from the start), `phase` (where it starts, a share of its length), `speed`,
`is_paused`, `fade_seconds` (a clip change's cross-fade), `overrides_is_looped` / `is_looped`,
`overrides_animation_libraries` / `animation_libraries` (the placement's own libraries instead of its world model's)
and `overrides_animation_graph` / `animation_graph` (its own graph).
The playback (clip times, weights, fades, the graph's states, the action) and the pose are runtime only: the animation
system (`WorldAnimation`) keeps them in the component (its runtime, freed with the component; removing the component
ends its action first, freeing its character) and publishes the pose for the renderer and sockets. Not a transform
driver: it bends the object's model, never moves the object.

### Terrain component (`terrain_component`)
Places a terrain (everything about terrain is in [Terrain.md](Terrain.md)): `terrain` (the asset name of a
terrain, `terrains/`; none places nothing), then the override pairs over the asset's defaults, as the model
component's: `overrides_has_outline` / `has_outline`, `overrides_casts_shadows` / `casts_shadows`,
`overrides_shadow_distance` / `shadow_distance`, `shadow_distance_multiplier`, `overrides_is_shadow_distance_fixed` /
`is_shadow_distance_fixed` (wires 21 to 25), `overrides_haze` / `haze` (wires 19 and 20), `overrides_draw_distance` /
`draw_distance`, `draw_distance_multiplier`
(above 0, default 1), `overrides_is_draw_distance_fixed` / `is_draw_distance_fixed`, and the collision's,
`overrides_is_collision_enabled` / `is_collision_enabled`, `overrides_collision_layer` / `collision_layer`,
`overrides_collides_with` / `collides_with`. `WorldTerrainComponent_ResolveSettings` gives the settings in effect (a
mask neither the asset nor the placement gives is the layer in effect's default). The object's transform places the
terrain's space; the object is static by default, and moving it works (the collision world moves the chunks, with the
static-move warning). Not a transform driver. An object with a collider as well collides through its collider alone.

### Light component (`light_component`)
`_color` (white), `_intensity` (>= 0, default 1), `_range` (>= 0, default 1: how far the light reaches),
`_castsShadows` (false). It uses its object's world position (a lamp carries its light); rotation, scale
and tint do not affect it. A hidden or disabled object's light is off, also under a hidden or disabled ancestor.

### Ambient zone component (`ambient_zone_component`)
A volume that sets a room's own light (the warm office, the dim storeroom, the cellar no daylight reaches); how the
renderer applies it is in [WorldRuntime.md](WorldRuntime.md), "Interior lighting". `_shape` (`WorldAmbientZoneShape`:
`box` with `_halfExtents` (2, 1.5, 2), or `sphere` with `_radius` 2; each >= 0, in the object's own units, the other
kept while unused), around the object's position: the object's world placement moves, turns and scales it, so a zone
that is a child of a building moves with it. `_blendDistance` (0.1 m, >= 0) is how far inward from its surface it
fades in, from nothing at its surface (0: a hard edge), so its light never leaks out of it; a surface on or inside it
that faces into it takes it whole. A room's zone that reaches its blend distance into the walls, floor and ceiling (to
the walls' middle) lights everything in the room whole and none of the walls' outside faces. Inside it every lit surface
takes `_ambientColor` (a warm white, sRGB) x `_ambientIntensity` (>= 0, default `WORLD_AMBIENT_ZONE_DEFAULT_INTENSITY`
0.02: about a lit office's 500 lux; the light a white surface shows, in the game's light units), the sky's ambient
times `_skyAmbientScale` (>= 0, 1; 0 no sky light at all), and the part of a view inside it is hazed times `_haze`
([0, 1000], 1 the outside air's, 0 a room's still air). While the camera is inside, the eye's exposure takes
`_exposureBias` stops more (within ±8, default 0). Where zones overlap, the higher `_priority` (an `int32_t`, 0) wins
and equal ones blend by their weights. A hidden or disabled object's zone is off, as a light is, also under a hidden or
disabled ancestor, and so is a zone with no inside (a zero half size or radius, or a zero scale).

## World

Holds the objects in creation order (`_objects`) with an id -> object map (`_objectsById`, a `WRHashMap`), the
hierarchy's bookkeeping (how many world matrices are dirty, the recompute counter for tests, the refused parents, the
removal scratch), the registry entries of its component types, one dense array of components per type (`_components[type]`, in add order),
the id counter (`_nextId`, starts at 1; 0 once every id was handed out: ids run from 1 to `WORLD_OBJECT_ID_MAX`,
`UINT64_MAX - 1`), two versions, an owned name, an embedded
`WorldEnvironment`, the borrowed key registry (`World_GetKeys`), the tag index and data byte count
([ObjectData.md](ObjectData.md)), the query scratch, and (optionally) a borrowed asset manager with its own asset user.

- **Construction:** every world borrows the program's key registry ([NamespacedKey.md](NamespacedKey.md);
  `Services.Keys`). `World_Construct1(world, keys)` makes a world without assets (tools, tests, a headless server): its
  sprites stay empty. `World_Construct2(world, keys, assets, logger)` makes a world whose components load their assets
  (the sprites' world sprites, and with them their animations) under its own asset user, which `World_Deconstruct`
  retires after destroying every object and component. The logger also hears of each data limit once.
- **Objects:** `World_CreateObject(world, name, &object)` (the next id) and `World_CreateObjectWithId(world, name, id,
  &object)` (the loader's version: refuses 0, ids above `WORLD_OBJECT_ID_MAX` and used ids, and advances the counter past
  the id). `UINT64_MAX` is never an id, so a world file always has a next id to record; an object with the last id
  uses the counter up, as does `World_EnsureNextObjectIdAtLeast` past it. `World_RemoveObject
  (world, id)` (its components first, then the object; ordered removal, so the other objects keep their order);
  `World_TryGetObject(world, id, &object)` (a hash lookup); `World_GetObjectCount` / `World_GetObjectByIndex` (a const
  world) for iteration in creation order. A new object is a root at the default transform. Removal takes the whole subtree (see
  "Hierarchy"). `World_EnsureNextObjectIdAtLeast` and `World_GetNextObjectId` are for world files.
- **Name:** `World_SetName` / `World_GetName`: NULL or at most `WORLD_NAME_MAX_LENGTH` (256) bytes of valid UTF-8
  (a longer or broken name is refused, as an object's is); world files save it.
- **Components:** `World_AddComponent(world, id, type, &component)`, `World_RemoveComponent(world, id, type)`,
  `World_TryGetComponent(world, id, type, &component)`, and **`World_GetComponents(world, type, &count)`**: the world's
  own dense array of every component of a type, in add order (removal keeps the order), valid until a component of that
  type is added or removed. Systems iterate these instead of walking objects (the renderer's cache does); they still
  check each owner's flags.
- **Component types:** `World_GetComponentTypeCount`, `World_GetComponentTypeInfo(world, type)` and
  `World_TryFindComponentType(world, name, &type)` (world files and the editor name types).
- **Change tracking:** `World_GetObjectSetVersion` changes on every object creation and removal,
  `World_GetComponentSetVersion` on every component addition and removal (also with its object); property changes
  advance the object's and the component's own revisions instead, and data changes the object's data revision.
- **Tags and data:** `World_SetData`, `World_TryGetData`, `World_RemoveData`, `World_AddTag`, `World_RemoveTag`,
  `World_HasTag`, `World_GetDataByteSize`, with the limits `WORLD_OBJECT_DATA_MAX_ENTRIES` (256),
  `WORLD_OBJECT_DATA_MAX_BYTES` (64 KiB) and `WORLD_DATA_MAX_BYTES` (16 MiB). **Queries:** `World_Query`,
  `World_DoesObjectMatch` (`WorldQuery.h`). Both in [ObjectData.md](ObjectData.md).
- **The test seam:** `WorldInternal_ConstructWithTypes` (private header) builds a world whose component types are a
  given list: the standard ones first, as their own entries, then test-only types. `tests/WorldTests.c` adds two
  transform drivers that way to test the one-driver rule.

## Ready for things that move

The game has objects that move and react to each other: the player and NPCs (the `character` component: the first
transform driver), cars (a `vehicle` component, later), physics objects (a `rigid_body` component, later).
The model is ready for them:

- **One driver per object.** A component type flagged `IsTransformDriver` writes its object's transform every tick.
  `World_AddComponent` refuses a second driver on the same object (`ErrorCode_InvalidOperation`), so two systems never
  fight over one transform. Adding a driver makes the object movable. A driven object is normally a root object; with
  a parent the driver writes the local transform through the parent's inverse (`WorldTransform_TryGetLocalMatrix`,
  then `WorldTransform_Decompose`).
- **The collision step corrects drivers.** It is the one system besides an object's driver that writes the
  object's transform: a driver writes where its object wants to be, `WorldCollision_Step` moves a pushed object's
  position by its push (a child's through its parent's inverse) and sets a stopped mover's chain back to where it was
  at the start of the tick, and the driver reads the correction next tick (`WorldCollision_GetMotionResult`). Drivers
  must therefore read their object's transform each tick rather than keep their own copy (the test frame's door and
  pushing wall do).
- **Moving with a parent.** An object under a movable parent moves in play although its own mobility may be static:
  systems that cache static objects must watch the transform revision (it advances when an ancestor moves), not the
  mobility alone.
- **Mobility is a hint for performance, never a lock.** Collision keeps static colliders in a structure built
  once and movable ones in one updated every tick; the renderer can later cache static shadows. Moving a static object
  still works (the editor does it constantly): it costs a rebuild of whatever cached it, and in play the system that
  rebuilds logs one warning per object. A driven object may even be set back to static; it still moves, at that cost.
  The collision world's static tree is the first such cache: it sees the move through the object's transform revision,
  re-inserts the collider and warns once ([WorldCollision.md](WorldCollision.md)).
- **The player is an ordinary object** with a character component. NPCs, triggers, vehicles and physics then all
  treat it like any other object; no frame special-cases it.
- **The tick order.** Each game tick runs the systems that change a world in this order, so each later system knows
  its slot:
  1. input (`GameInput`, polled by the frame manager);
  2. scripts (later);
  3. transform drivers: scripted movers (the test frame's desk, door and pushing wall), then **the characters**
     (`WorldCharacters_Update`) last among them, so a platform moved this tick carries the characters on it
     exactly; vehicles and physics later;
  4. the collision step: `WorldCollision_Step`, which syncs (the world matrices first), sweeps every
     collider that moved, pushes apart what overlaps and writes the pushes back;
  5. queued events: trigger enter and exit (`WorldCollision_GetTriggerEvents`, then `_ClearTriggerEvents`), contacts
     and motion events (`_GetContactEvents`, `_GetMotionEvents`, then their clears; the queued-event rule);
  6. animation, effectively enabled objects only: the skeletons' playback (`WorldAnimation_Update`: the animators'
     rows and clips, their graphs, fed the characters' state, and their actions, [Animation.md](Animation.md)),
     then its queued events (`WorldAnimation_GetEvents`, then `_ClearEvents`), then sprite playback
     (`AdvanceSpriteAnimations` in the test frame);
  7. `World_UpdateTransforms`: the world matrices of everything that moved this tick;
  8. once per shown frame, after the loop's ticks, the frame's **PrepareRender** ([GameFrame.md](GameFrame.md)):
     the pose step (`WorldAnimation_Pose`: poses, sockets, the socket children marked dirty), `World_UpdateTransforms`
     again, then whatever rides a posed body (the first-person camera on the eyes socket);
  9. the renderer, which only reads (AGENTS.md rule 3).

## Property tables

Every object and component property is one row of its type's table ([PropertyDescriptors.md](PropertyDescriptors.md));
a test checks every table. Wire ids are the save schema: never renumber or reuse one.

| Table | Key | Wire | Type | Default |
|---|---|---|---|---|
| `world_object` | `name` | 3 | String (256, null allowed) | null |
| | `parent` | 12 | ObjectId, ReadOnly for `Properties_Set` (reads and copies relink through the world) | 0 (a root) |
| | `position` | 4 | Vector3Double, each axis within ±`WORLD_POSITION_LIMIT` (1e7 m) | (0, 0, 0) |
| | `rotation` | 5 | AngleVector3 (degrees in JSON) | (0, 0, 0) |
| | `scale` | 6 | Vector3 | (1, 1, 1) |
| | `tint` | 7 | RenderColor | white |
| | `is_enabled` | 8 | Bool | true |
| | `is_visible` | 9 | Bool | true |
| | `mobility` | 10 | Enum `static` / `movable` | `static` |
| | `data` (JSON: `data` and `tags`) | 11 | DataMap, ReadOnly for `Properties_Set`; 256 entries, 64 KiB | empty (NULL) |
| `model_component` | `world_model` | 1 | AssetName (128, null allowed): a world model | null |
| | `uses_variant` | 6 | Bool | false |
| | `variant` | 7 | String (128, null allowed) | null |
| | `overrides_has_outline` | 8 | Bool | false |
| | `has_outline` | 2 | Bool | true |
| | `overrides_omit_pixelation` | 9 | Bool | false |
| | `omit_pixelation` | 3 | Bool | false |
| | `overrides_casts_shadows` | 11 | Bool | false |
| | `casts_shadows` | 5 | Bool | true |
| | `overrides_shadow_distance` | 27 | Bool | false |
| | `shadow_distance` | 28 | Float > 0 | 600 |
| | `shadow_distance_multiplier` | 29 | Float > 0 | 1 |
| | `overrides_is_shadow_distance_fixed` | 30 | Bool | false |
| | `is_shadow_distance_fixed` | 31 | Bool | false |
| | `overrides_is_shadow_only` | 32 | Bool | false |
| | `is_shadow_only` | 33 | Bool | false |
| | `overrides_has_two_sided_shadows` | 34 | Bool | false |
| | `has_two_sided_shadows` | 35 | Bool | false |
| | `overrides_shadow_lod` | 36 | Bool | false |
| | `shadow_lod` | 37 | UInt32 | 0 |
| | `overrides_haze` | 23 | Bool | false |
| | `haze` | 24 | Float in [0, 1000] | 1 |
| | `overrides_occludes_sky` | 25 | Bool | false |
| | `occludes_sky` | 26 | Bool | true |
| | `overrides_draw_distance` | 12 | Bool | false |
| | `draw_distance` | 13 | Float > 0 | 600 |
| | `draw_distance_multiplier` | 14 | Float > 0 | 1 |
| | `overrides_is_draw_distance_fixed` | 15 | Bool | false |
| | `is_draw_distance_fixed` | 16 | Bool | false |
| | `overrides_lod_distances` | 17 | Bool | false |
| | `lod_distances` | 18 | FloatList: at most 8, each > 0, rising | empty (NULL) |
| | `lod_distance_multiplier` | 19 | Float > 0 | 1 |
| | `is_lod_forced` | 20 | Bool | false |
| | `forced_lod` | 21 | UInt32 | 0 |
| `sprite_component` | `world_sprite` | 1 | AssetName (128, null allowed): a world sprite | null |
| | `overrides_orientation` | 17 | Bool | false |
| | `orientation` | 4 | Enum `billboard` / `axis_billboard` / `fixed` | `billboard` |
| | `overrides_pivot` | 18 | Bool | false |
| | `pivot` | 5 | Vector2 | (0.5, 0.5) |
| | `overrides_pixels_per_unit` | 19 | Bool | false |
| | `pixels_per_unit` | 6 | Float > 0 | 32 |
| | `overrides_flip_x` | 20 | Bool | false |
| | `flip_x` | 7 | Bool | false |
| | `overrides_flip_y` | 21 | Bool | false |
| | `flip_y` | 8 | Bool | false |
| | `overrides_alpha_mode` | 22 | Bool | false |
| | `alpha_mode` | 11 | Enum `cutout` / `blend` / `additive` | `cutout` |
| | `overrides_alpha_cutoff` | 23 | Bool | false |
| | `alpha_cutoff` | 12 | Float in [0, 1] | 0.5 |
| | `overrides_is_lit` | 24 | Bool | false |
| | `is_lit` | 9 | Bool | true |
| | `overrides_emission` | 25 | Bool | false |
| | `emission` | 10 | Float >= 0 | 1 |
| | `overrides_light_wrap` | 26 | Bool | false |
| | `light_wrap` | 16 | Float in [0, 1] | 1 |
| | `overrides_casts_shadows` | 27 | Bool | false |
| | `casts_shadows` | 15 | Bool | true |
| | `overrides_is_decal` | 28 | Bool | false |
| | `is_decal` | 13 | Bool | false |
| | `overrides_decal_offset` | 29 | Bool | false |
| | `decal_offset` | 14 | Float >= 0 | 0.01 |
| | `overrides_has_outline` | 30 | Bool | false |
| | `has_outline` | 2 | Bool | true |
| | `overrides_omit_pixelation` | 31 | Bool | false |
| | `omit_pixelation` | 3 | Bool | false |
| | `overrides_haze` | 37 | Bool | false |
| | `haze` | 38 | Float in [0, 1000] | 1 |
| | `overrides_draw_distance` | 32 | Bool | false |
| | `draw_distance` | 33 | Float > 0 | 150 |
| | `draw_distance_multiplier` | 34 | Float > 0 | 1 |
| | `overrides_is_draw_distance_fixed` | 35 | Bool | false |
| | `is_draw_distance_fixed` | 36 | Bool | false |
| | `overrides_shadow_distance` | 39 | Bool | false |
| | `shadow_distance` | 40 | Float > 0 | 150 |
| | `shadow_distance_multiplier` | 41 | Float > 0 | 1 |
| | `overrides_is_shadow_distance_fixed` | 42 | Bool | false |
| | `is_shadow_distance_fixed` | 43 | Bool | false |
| | `overrides_is_shadow_only` | 44 | Bool | false |
| | `is_shadow_only` | 45 | Bool | false |
| `light_component` | `color` | 1 | Color | white |
| | `intensity` | 2 | Float >= 0 | 1 |
| | `range` | 3 | Float >= 0 | 1 |
| | `casts_shadows` | 4 | Bool | false |
| `ambient_zone_component` | `shape` | 1 | Enum `box` / `sphere` | `box` |
| | `half_extents` | 2 | Vector3, each >= 0 | (2, 1.5, 2) |
| | `radius` | 3 | Float >= 0 | 2 |
| | `blend_distance` | 4 | Float >= 0 | 0.1 |
| | `ambient_color` | 5 | Color | (255, 200, 150) |
| | `ambient_intensity` | 6 | Float >= 0 | 0.02 |
| | `sky_ambient_scale` | 7 | Float >= 0 | 1 |
| | `exposure_bias` | 8 | Float in [-8, 8] | 0 |
| | `priority` | 9 | Int32 | 0 |
| | `haze` | 10 | Float in [0, 1000] | 1 |

The model component's wire 1 is `world_model` and wires 2, 3 and 5 are the placement's own values behind their override
flags (8, 9 and 11); the LOD rows are 17 to 21, 22 is the model instances component's `instances` (the two share their
wire ids), 23 and 24 are the haze's pair, 25 and 26 the sky's (`occludes_sky`, whose default is the world model's own
`render.occludes_sky`, true) and 27 to 37 the shadow rows; 4 and 10 are retired (the shadow tier pair of the tables'
version 1, which their migrations read: [WorldFile.md](WorldFile.md), "Versions"). Both tables are at version 2, and
their next free wire id is 38. The sprite component's wire 1 is `world_sprite`, wires 2 to 16 the placement's own values
behind their flags (wires 17 to 31), 32 to 36 the draw distance rows, 37 and 38 the haze's pair and 39 to 45 the shadow
distance's rows, so its next free wire id is 46. The haze multiplies the haze in front of the placement
([WorldRuntime.md](WorldRuntime.md), "Haze"): 1 the real air's, 0 as if the air were clear (a sign that must read from far
away); its default is the world model's or world sprite's own `render.haze` (1). The sprite's value rows
share their key, type, default, rules and texts with the world sprite's tables (`source/WorldSpriteInternal.h`,
[WorldSprites.md](WorldSprites.md)). Each override flag sits right before its value, in table order (the inspector's and
the JSON writer's order), and `PropertyTable_Check` checks the pairing. The collider's table is in
[WorldCollision.md](WorldCollision.md), the character's in [Characters.md](Characters.md) and the animator's in
[Animation.md](Animation.md). The ambient zone component's next free wire id is 11.

The object table's wire ids start at 3: 1 and 2 are the world file's own entries per object (the id and the
components, [WorldFile.md](WorldFile.md)). `data` is one row (the tags and the values are one map, written as two JSON
keys, `tags` and `data`, and as one GHDF entry); `parent` (12) and `parent_socket` (13) are placed after `name` in the
table's order. The next free object wire id is 14.

## Environment

`WorldEnvironment` is a struct with the date and time of day, sky, sun, night sky, moon, ambient, haze, weather and
effect settings, and the world's gravity (`gravity`, 9.81 m/s² straight down by default, in [0, 1000]; characters
multiply it by their own `gravity_multiplier`).
**Every field is one row of its property table** (`WorldEnvironment_GetPropertyTable`, in `source/WorldEnvironment.c`; module doc
[PropertyDescriptors.md](PropertyDescriptors.md)): the key (the field name in snake_case, e.g. `light_pollution`), the
GHDF wire id, the type, the default, the rules, the inspector text. `WorldEnvironment_SetDefaults`, `_Validate`,
`_CopyTo` and `_Deconstruct` are thin wrappers over the table, and `WorldEnvironment_ReadJSON` / `_WriteJSON` are the
JSON entry points for the world files. **To add a setting, add the field and one table row** with a new,
never-used wire id (the next free one is 79; 2 and 3 are retired); nothing else needs writing. The field comments state
each value's rule; validation enforces them (finite, `TimeOfDay`, `LightPollution`, `StarTwinkle`, `MoonPhase`,
`NightVisionStrength` and `NightVisionBlueShift` in [0, 1], `SunSizeMultiplier`, `MoonSizeMultiplier` and `MoonCycleDays` > 0, every strength/intensity/density/brightness
>= 0, `Date` inside the calendar, the texture and climate names valid asset names of at most 128 bytes,
`MoonPhaseSource` one of its names, the weather's values in their ranges below) and names the first broken field by
key and C name: `world_environment.light_pollution (LightPollution) must be finite and in [0, 1].` `SunAngle` and `MoonOrbitTilt` are `Angle`s: radians in memory and GHDF, degrees in
JSON.

**Date and time of day are what the sky shows, not a clock.** `TimeOfDay` (a fraction of the day) and `Date` (a day
number of [GameCalendar.md](GameCalendar.md), a `Date` property: `"2026-10-18"` in JSON, default 2026-10-18 like a new
game clock) never advance by themselves. While a game runs, the game clock ([GameClock.md](GameClock.md)) writes both
every tick; in a world file they are where the sky starts (the editor's preview uses them, and the test frame starts
its clock from them). The environment table's wire ids 2 and 3 are retired (never reused), so GHDF that holds them
still reads (the entries are skipped).

**The environment owns its asset names** (`MilkyWayTexture`, `MoonTexture`, `Climate`): `WorldEnvironment_SetDefaults`
constructs and allocates them (call it on a new environment only; to reset one, `Deconstruct` first) and pairs with
`WorldEnvironment_Deconstruct`; copies go through `CopyTo` (never assignment: a copied pointer would be freed twice);
a name is changed through the table (`Properties_Set`), which clones the new one and frees the old. `World` pairs
them.

The night sky settings: `StarSeed` (which stars), `StarDensity` (1 = an Earth-like sky of about 9000 stars, the
default; 0 none; at most 20000 stars), `StarBrightness`, `StarTwinkle` (0..1, default 0.25), `MilkyWayBrightness`
(default 1), `MilkyWayTexture` (default `sky_milky_way`), `LightPollution` (0 = a dark site .. 1 = a city centre,
default 0.35, a small town) and `LightPollutionColor` (sodium orange by default).

The moon settings (story creators get as many settings as possible):

| Key | Rule | Default | Meaning |
|---|---|---|---|
| `is_moon_enabled` | bool | true | false: no moon at all (no disc, moonlight or moonlit sky). |
| `moon_phase_source` | `calendar` / `fixed` | `calendar` | The phase from the date, or always `moon_phase` (a story pins a full moon). |
| `moon_phase` | [0, 1] | 0.5 | The fixed phase: 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter. |
| `moon_phase_offset_days` | finite | 0 | Shifts the calendar phase (7.4 days is a quarter). |
| `moon_cycle_days` | > 0 | 29.530588 | One cycle of phases. |
| `moon_orbit_tilt` | finite (an angle) | 0.09 rad (5.2 degrees) | How far the moon's path strays from the sun's. |
| `moon_size_multiplier` | > 0 | 5 | The disc x the real 0.52 degrees (5: about 16 pixels at 640x360). |
| `moon_brightness` | >= 0 | 1 | Multiplies the disc's brightness in the sky. |
| `moon_intensity` | >= 0 | 0.04 | The full moon's light above the air relative to the sun's (`WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY`). The phase scales it, it fades out by day, the air dims it toward the horizon, and the moonlit sky is lit by all of it. |
| `moon_color` | colour | (200, 214, 255) | The moonlight's colour above the air (the air reddens it low over the horizon) and the disc's. Real moonlight is the sun's light, a little warmer; the blue of the night is the eye's (night vision). Open: the owner decides whether the default stays this blue or becomes white (255, 255, 255). |
| `moon_texture` | asset name | `sky_moon` | The moon's face. |

And `are_seasons_enabled` (default false): the sun's declination follows the date (23.44 degrees x the sine of its
place in the year, 0 at the March equinox; a uniform year, so the September equinox falls about two days early), so
with `SunAngle` > 0 summer days are longer and the noon sun higher, and the moon follows the ecliptic (a full moon low
in summer, high in winter). Off by default: it changes how much daylight the working hours get.

The air's settings (category "Sky and sun"; the renderer turns them into the atmosphere's
coefficients with `WorldAtmosphere_GetMedium` and builds its tables from them, [WorldRuntime.md](WorldRuntime.md),
"Atmosphere"). Absent keys read as their defaults, so older world files keep loading (and are written the same).

| Key | Wire id | Rule | Default | Meaning |
|---|---|---|---|---|
| `sky_turbidity` | 4 | >= 0 | 24 | The haze's amount: its density is the reference haze x turbidity / 3 (an extinction of 23.1e-6 per metre at sea level at 3). The default sees about 20 km (a hazy summer day); 11 about 40 km; 3 about 108 km, the clearest air. |
| `haze_height` | 47 | [10, 100000] m | 1200 | The haze's scale height: low haze hugs the ground and lets hills stand out above it. |
| `haze_albedo` | 48 | colour | (244, 244, 244): linear 0.907 | The share of the light the haze takes that it scatters (the rest it absorbs); darker or tinted for smog and dust. |
| `sun_glow` | 49 | [0, 0.99] | 0.758 | The haze's forward scattering (the Mie phase function's g): how large and bright the glow around the sun and the moon is; it shapes the sky's light on surfaces too (the irradiance table). |
| `air_density` | 50 | [0, 100] | 1 | The air's Rayleigh scattering (the blue) as a multiple of Earth's. `sky_tint` changes the sky's colour. |
| `ozone` | 51 | [0, 100] | 1 | The ozone layer's absorption as a multiple of Earth's: it keeps the twilight sky blue overhead (violet in this three-colour model); 0 none. |
| `multiple_scattering` | 52 | [0, 1] | 1 | The share of the light scattered more than once that the sky draws (0: single scattering, for comparing); the same share of the sky's light on surfaces. |
| `sky_ground_albedo` | 5 | colour | (90, 90, 90): linear 0.10 | The ground's albedo as the air sees it: the light it reflects feeds the multiple scattering, the far ground's colour below the horizon, and the ground's bounce that lights every surface from below. |

`WORLD_ENVIRONMENT_MIN_HAZE_HEIGHT`, `_MAX_HAZE_HEIGHT`, `_MAX_SUN_GLOW` and `_MAX_AIR_MULTIPLE` (WorldEnvironment.h)
are the rules' bounds; the renderer clamps to the same bounds and takes a non-finite value as its default, so the shaders
only get finite air within them. `sky_turbidity` has no maximum: any finite haze is safe in the shaders (one too thick to
see through gives a dark sky, never a NaN), and so is any sun or moon intensity in the sky-view table, which saturates at
its half floats' largest value (65504).

The haze's settings (category "Haze"; the renderer turns them into the frame's haze with
`WorldAtmosphere_GetHaze`, [WorldRuntime.md](WorldRuntime.md), "Haze"). The defaults are the real air on a clear day:
no ground haze, every distance as it is. Absent keys read as their defaults.

| Key | Wire id | Rule | Default | Meaning |
|---|---|---|---|---|
| `fog_strength` | 16 | >= 0 | 1 | "Haze strength": multiplies the haze on everything and the ground haze, with the player's own `FogStrength` ([Config.md](Config.md)): 1 the real air, 0 clear air, 2 twice the haze. The sky stays as it is. |
| `fog_color` | 15 | colour | (170, 190, 220) | Not used by the renderer (distant things fade into the haze of the air in front of them); kept for saved data. |
| `aerial_distance_scale` | 53 | [0, 1000] | 1 | Treats every distance as this many times longer for the haze, so a small town reads as bigger and hazier; far things still meet the sky at the horizon. |
| `haze_start_distance` | 54 | [0, 1e7] m | 0 | No haze nearer than this: an artistic choice (the real air starts at 0). |
| `ground_haze_density` | 55 | [0, 100] per metre | 0 | A low layer of mist: how much light it takes per metre at its base (0 none; 0.02 a morning mist hiding things beyond about 200 m). |
| `ground_haze_base` | 56 | [-1e7, 1e7] m | 0 | The world Y of the mist's base: as dense as its density says below it, thinning above it. |
| `ground_haze_thickness` | 57 | [0.1, 100000] m | 20 | How quickly the mist thins above its base: by e every this many metres. |
| `ground_haze_albedo` | 58 | colour | (250, 250, 250) | The mist's colour: the share of the light it takes that it scatters (near white for mist, darker or tinted for smoke and dust). |
| `ground_haze_glow` | 59 | [0, 0.99] | 0.8 | How strongly the mist scatters light forward (its phase function's g): its glow around the sun and the moon. |
| `horizon_ground_height` | 60 | [-1e7, 1e7] m | 0 | "Far ground height": the world Y of the far ground the sky shows below the horizon, beyond the world's own ground; the sky is seen from the camera's height above it. |

`WORLD_ENVIRONMENT_MAX_AERIAL_DISTANCE_SCALE`, `_MAX_DISTANCE`, `_MAX_GROUND_HAZE_DENSITY`, `_MIN_GROUND_HAZE_THICKNESS`
and `_MAX_GROUND_HAZE_THICKNESS` (WorldEnvironment.h) are the rules' bounds; `WorldAtmosphere_GetHaze` clamps to them and
takes a non-finite value as its default. The far ground is lit like every surface: the sun's and the moon's
light through the air at each far point, the sky's irradiance there and the moonless night's own light.

The light's settings (one light scale for the sky and the surfaces; [WorldRuntime.md](WorldRuntime.md),
"Sky's light on surfaces"). The sun, the moon and the sky light every surface through the air on the surfaces' scale,
so the sun's colour is its colour above the air and the ambient is the sky's own light.

| Key | Wire id | Rule | Default | Meaning |
|---|---|---|---|---|
| `sun_color` | 7 | colour | (255, 255, 255) | The sun's colour above the air: its light, its disc and the light it gives the sky, the haze and the mist. The air reddens it toward the horizon by itself. |
| `sun_intensity` | 8 | >= 0 | 1 | Multiplies the sun's brightness (its light above the air is 1.078 x this x the colour: `WORLD_ATMOSPHERE_SUN_LIGHT`). |
| `ambient_skylight_color` | 13 | colour | (255, 255, 255) | Tints the sky's light on surfaces (the sky's own light from above, the ground's bounce from below); white leaves it as it is. |
| `ambient_skylight_intensity` | 14 | >= 0 | 1 | Multiplies the sky's light on surfaces: 1 the sky's own, 0 leaves shade lit only by the night's light and lamps. |
| `night_light_color` | 61 | colour | (161, 178, 255) | The colour of the moonless night's own light on surfaces (starlight and the air's glow, raised for play as the moonlight is). |
| `night_light_intensity` | 62 | >= 0 | 0.011 | How bright the moonless night's own light is (its linear colour times this, added to every surface's ambient): 0 gives a pitch-black moonless night outside the moon's and the town's light. |

The weather's values (category "Weather"; [WeatherSystem.md](WeatherSystem.md)): what the weather is now. The weather
system writes them every tick from the climate the environment names; in a world file they are the world's starting
weather, and what the weather's off mode leaves as it is. The renderer shows them ([WorldRuntime.md](WorldRuntime.md),
"Weather"). The defaults are no weather: clear, dry and still.

| Key | Wire id | Rule | Default | Meaning |
|---|---|---|---|---|
| `cloud_coverage` | 64 | [0, 1] | 0 | How much of the sky has clouds. |
| `cloud_type` | 65 | [0, 1] | 0 | 0 thin, flat layers; 0.5 cumulus; 1 towering storm clouds. |
| `cloud_density` | 66 | [0, 1] | 0 | How thick the clouds are: rain clouds are dense and dark. |
| `precipitation` | 67 | [0, 1] | 0 | Rain, 0 none to 1 a downpour. |
| `wetness` | 68 | [0, 1] | 0 | How wet surfaces are: it follows the rain and dries afterwards. |
| `wind_direction` | 69 | finite (an angle) | 0 | Where the wind blows toward: 0 toward +Z, 90 degrees toward +X (the direction (sin, 0, cos)). |
| `wind_speed` | 70 | [0, 150] m/s | 0 | The mean wind (`WORLD_ENVIRONMENT_MAX_WIND_SPEED`). |
| `wind_gust` | 71 | [0, 150] m/s | 0 | The current gust on top of it. |
| `fog_density` | 72 | [0, 100] per metre | 0 | The weather's ground fog, added to the ground haze at its base (0.02: a morning mist). |
| `fog_height` | 73 | [0.1, 100000] m | 20 | How quickly the weather's fog thins above the ground haze's base. |
| `temperature` | 74 | [-273.15, 1000] degrees C | 15 | The air's temperature. |
| `climate` | 75 | asset name, at most 128 bytes | `default` | The climate asset the weather follows (Climate.h). |
| `lightning_flash` | 76 | [0, 1] | 0 | Runtime only, never saved: the current flash (the weather sets it). |
| `lightning_direction` | 77 | finite vector | (0, 0, 1) | Runtime only: the level direction of the current strike from the viewer. |
| `lightning_distance` | 78 | [0, 1e7] m | 1000 | Runtime only: how far away the current strike is. |

The runtime rows are shown and reachable by scripts but not `Persistent`: files never carry them, and a read leaves the
reader's own. `WorldEnvironment_GetDaylightFactorAt(environment, date, timeOfDay)` is the daylight factor at another
time for this environment's sun (the weather's drying reads the sun ahead of the clock).

The sun's and the moon's shadows ([WorldRuntime.md](WorldRuntime.md), "Sun and moon shadows"): `are_shadows_enabled`
(default true) and `shadow_reach` (wire id 63, [0, 20000] m, default 4000: how far from the camera they reach, fading out
over the last tenth; 0 none; `WORLD_ENVIRONMENT_MAX_SHADOW_REACH`, the levels' depth precision). How sharp they are is
the player's ([Config.md](Config.md)).

The night vision settings (the eye's dim-light vision, [WorldRuntime.md](WorldRuntime.md), "Night vision"):
`night_vision_strength` ([0, 1], default 1: the eye; 0 keeps every colour, a long-exposure photo's night) and
`night_vision_blue_shift` ([0, 1], default 0.5: how blue the rods' grey is, from a neutral grey to the night blue
painters use). Neither changes anything by day.

**The sky's model** (pure functions; the renderer only draws their results, [WorldRuntime.md](WorldRuntime.md), "Moon"
and "Night sky"):
- `WorldEnvironment_GetSunDirection`, `_GetDaylightFactor` (with the seasons when on).
- `WorldEnvironment_GetCelestialToWorld`: the sun's frame for the time of day (the sun at (1,0,0), at (cos d, 0,
  -sin d) with the seasons; the pole above the horizon stays at world (0, sin SunAngle, -cos SunAngle)).
- `WorldEnvironment_GetStarsToWorld`: the stars' frame, the celestial frame of the June solstice turned ahead of the
  sun's by its progress through the tropical year (about 0.99 degrees a day, continuous): the stars rise about 4
  minutes earlier each day.
- `WorldEnvironment_GetMoonPhase` (from the date: `frac((Date + TimeOfDay - WORLD_ENVIRONMENT_MOON_EPOCH_DAY +
  offset) / cycle)` in double, the epoch being the real new moon of 2000-01-06 18:14 UTC, so with the defaults the
  phase is the real moon's within about a day; or fixed), `_GetMoonDirection` (east of the sun by 2pi x phase, the tilt
  term north of it), `_GetMoonIllumination` ((1 - dot(sun, moon)) / 2), `_GetMoonBrightness` (the total light for an
  illumination: the opposition-surge fit of Krisciunas and Schaefer, a half moon gives about a tenth of the full
  moon's) and `_GetMoonlightFactor` (brightness x (1 - daylight); 0 without a moon; not its height: the air
  dims a low moon, in the renderer).
- `WorldEnvironment_GetSkyLights`: both lights at once above the air (directions, the daylight factor, the sun's
  intensity, the moon's intensity with its phase and the daylight in, the moon's phase and illumination).
- `WorldEnvironment_ChooseShadowLight` (from the light each gives a surface facing it through the air, which the
  renderer computes): the brighter by luminance once it stands high enough, with a fade (in from 0.05 to 0.15 of
  elevation, out around the hand-over between the two and for a light too dim to show a shadow), so the hand-over from
  the sun to the moon never jumps.

Validate anything that came from outside the program before using it.

## Persistence

**World files** ([WorldFile.md](WorldFile.md), format in `references/world_structure.md`) save and load a whole
world as JSON or GHDF through the tables: the environment's, each object's and each component's, with the root ids
1 to 5 (format version 1, name 2, next id 3, environment 4, objects 5). The loader creates every object first
(`World_CreateObjectWithId`), then reads their tables, so each `parent` names an object that exists; it compares
`World_GetRefusedParentCount` around each object's read to refuse a bad parent and name the object, checks
`World_GetDataByteSize` against `WORLD_DATA_MAX_BYTES` after each object, reads data maps with
`PropertyReadOptions.Keys` (`World_GetKeys`) and `DefaultNamespace`, and truncates the key registry when it refuses a
file. `tests/PropertyDescriptorTests.c` compares the environment's GHDF tree with a literal copy of what a hand-written
encoder wrote (`environment_ghdf_matches_the_old_encoder`; integers take the smallest GHDF type, so the seed's entry
differs there).

Runtime state is not saved: playback positions, revisions, data scripts change in play (game saves as differences from
the world come later).

## Tests

`tests/ObjectDataTests.c` covers tags, data and queries ([ObjectData.md](ObjectData.md)); `tests/TestWorld.h` builds a
world with its own key registry for tests. `tests/WorldFileTests.c` covers the world files ([WorldFile.md](WorldFile.md)). `tests/WorldHierarchyTests.c` covers the hierarchy: `World_SetParent`'s
refusals (unknown ids, self, cycles two and three deep, 64 levels with a moved subtree's height), world matrices three
levels deep (on the fly and cached, the same products), reparenting with and without keeping the world placement (and
the refusal under a flattened parent), dirty propagation counted through `WorldInternal_GetWorldMatrixRecomputeCount`,
reads that never store, the attach order, subtree removal (components, arrays in order, dirty counts) with the detach
hooks children first, effective flags, and the `parent` row (ReadOnly, JSON reads attach or are refused and logged
once, a copy brings the parent), and far placements (a child 0.5 mm from its parent at 10 000 km keeps it;
re-parenting there keeps the place within 5e-9 m, 100 km out within 1e-9 m; a place past the limit is refused).
`tests/WorldTransformTests.c` covers the maths (round trips of 2000 random transforms, mirrors, zero scales, gimbal
lock, shear, local matrices, singular parents, the stretch bound, far placements). The hierarchy, world file,
collision, motion and character suites run **twice** (`TestWorld_RunSuite`, `tests/TestWorld.h`): as they are, then
as `<Suite>Far` with every scene shifted by (100 000.37, 100 000.21, -100 000.53) m, with the same tolerances: about
100 km along each axis and off the float grid, since a whole number of float steps (100 000 is one) would carry scene
values on a 1/128 m grid out there without any rounding and hide a forgotten float. A test places its scene
through `TestWorld_At` (root objects, query origins, rays, casts, teleports) and compares what the world reports after
`TestWorld_ToScene`; children's local positions are not shifted. It is the test that finds a forgotten float.
`tests/WorldTests.c`: objects (ids unique and never 0, loader ids advance the counter, an exhausted counter, the
object-set version, validated setters and table writes advancing the revision, rotation in degrees in JSON),
components (add, find, remove; one per type; removing an object removes its components; the per-type arrays stay in
order across interleaved adds and removes; types by name; revisions), the sprite binding (no assets, a shared world
sprite whose animation is released with the world last, a missing world sprite and one with a missing animation logged
once, renaming, the same name keeps playing, table writes rebind), a placement's own sprite settings (defaults with
every flag off, rules, the flags and enum names in JSON, the round trip), flags, and mobility and drivers (through two test-only driver types: adding one makes the object
movable, a second is refused; the test seam refuses impostors). `tests/WorldEnvironmentTests.c` (validation including
the moon settings and texture names, copying, the date and time the clock writes, the retired ids, the celestial frame,
the moon's phase against real dates, its motion and lit fraction, the moonlight, the stars' drift and its direction, the
shadow light's hand-over, the seasons' day lengths). `tests/PropertyDescriptorTests.c` (every table passes its check,
the environment's GHDF tree matches the hand-written encoder's, its JSON round trip). `tests/WorldRenderCacheTests.c` covers the
renderer's side ([WorldRuntime.md](WorldRuntime.md)). They run headless with `sh tests/run_linux.sh`;
`tests/TestAssets.h` provides code-built sprite animation, model and texture definitions that load without files or a
GL context, and world models and world sprites built from JSON over them. `tests/WorldModelTests.c` covers the world
models and the model component's overrides ([WorldModels.md](WorldModels.md)), `tests/WorldSpriteTests.c` the world
sprites and the sprite component's overrides ([WorldSprites.md](WorldSprites.md)), `tests/WorldColliderTests.c` the
collider component and the world models' collision, `tests/WorldCollisionTests.c` the collision world and
`tests/WorldCollisionMotionTests.c` its step ([WorldCollision.md](WorldCollision.md)).
