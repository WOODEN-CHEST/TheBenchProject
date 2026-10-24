# World data layer

The rendering-independent model of a 3D world: its objects, their hierarchy, their components, their tags and data, and
its environment settings. No renderer is required to build, change or tear down a world; the world renderer reads it
separately and never writes to it. Worlds are saved and loaded as JSON and GHDF by [WorldFile.md](WorldFile.md) (the
format: `references/world_structure.md`), built on the property tables. Tags, typed object data and queries (T24) have
their own doc: [ObjectData.md](ObjectData.md).

This one doc covers the whole cohesive layer; every symbol has full Doxygen in its header.

## Modules

| Module | Files | What it is |
|---|---|---|
| WorldObject | `include/WorldObject.h`, `source/WorldObject.c` | The one object type: id, name, parent, transform (relative to the parent), tint, enabled, visible, mobility, and its component slots. |
| WorldComponent | `include/WorldComponent.h`, `source/WorldComponent.c` | The component base, the component types and the registry entry that describes each (`WorldComponentTypeInfo`). |
| WorldModelComponent | `include/WorldModelComponent.h`, `source/WorldModelComponent.c` | A world model by asset name ([WorldModels.md](WorldModels.md)), the variant drawn, and the settings this placement overrides (explicit flags next to its own values: outline, crisp, shadow tier, casts shadows, draw distance, fixed draw distance; a draw distance multiplier), with the pure function that resolves the settings in effect. |
| WorldSpriteComponent | `include/WorldSpriteComponent.h`, `source/WorldSpriteComponent.c` | A world sprite by asset name ([WorldSprites.md](WorldSprites.md)), bound by the world with its animation's playback instance, and every setting this placement overrides (explicit flags next to its own values: orientation, pivot, size, flips, alpha mode, lighting, decal, render flags, draw distance, fixed draw distance; a draw distance multiplier), with the pure function that resolves the settings in effect. |
| WorldLightComponent | `include/WorldLightComponent.h`, `source/WorldLightComponent.c` | A point light (colour, intensity, range, casts shadows). |
| WorldColliderComponent | `include/WorldColliderComponent.h`, `source/WorldColliderComponent.c` | How the object collides (T12, [WorldCollision.md](WorldCollision.md)): its world model's collision set (or one primitive of its own), its layer, mask and trigger flag (each overriding the world model's default with an explicit flag), a local offset, rotation and scale, and a rotation mode, with the pure function that resolves the settings in effect. |
| WorldCharacterComponent | `include/WorldCharacterComponent.h`, `source/WorldCharacterComponent.c` | The player or an NPC (T13, [Characters.md](Characters.md)): a character asset by name and every setting this placement overrides (explicit flags next to its own values), the input its driver gives it and the state it is in; the first transform driver. |
| WorldAnimatorComponent | `include/WorldAnimatorComponent.h`, `source/WorldAnimatorComponent.c` | What plays the object's skeletal animation (T29, [Animation.md](Animation.md)): the clip row and how it plays, the placement's own libraries, and the pose the animation system publishes. |
| WorldTerrainComponent | `include/WorldTerrainComponent.h`, `source/WorldTerrainComponent.c` | A terrain asset by name (B01, [Terrain.md](Terrain.md)) and the settings this placement overrides (explicit flags next to its own values: outline, shadow tier, casts shadows, draw distance, fixed draw distance, collision enabled, layer and mask; a draw distance multiplier), with the pure function that resolves the settings in effect. |
| WorldEnvironment | `include/WorldEnvironment.h`, `source/WorldEnvironment.c` | Per-world sky/sun/moon/fog/effect settings, described by a property table (defaults, validation, copying, JSON, GHDF), and the sky's model: the sun, the moon, the stars' rotation, the two lights. |
| World | `include/World.h`, `source/World.c`, `source/WorldData.c` (tags and data, the tag index), `source/WorldHierarchy.c` (parents, world matrices, effective flags, subtrees; also the `WorldObject_` functions about them) | Owns the objects and their components, mints ids, keeps the id map, the hierarchy and the per-type component arrays, borrows the key registry, keeps the tag index and the data byte count, holds the environment and its own asset user. |
| WorldTransform (private) | `source/WorldTransform.h`, `source/WorldTransform.c` | The pure maths of transforms, in double (T33): compose (DrawModelEx's order), the one decomposition back to position / Euler XYZ / scale, a local matrix under a parent, a bound on a matrix's stretch. Used by the hierarchy, the renderer's cache, collision and the characters. |
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

`WorldObject` is one concrete struct (T05 replaced the three subclasses: model, sprite and light objects are now
components). Every object has:

- `_id` (unique in its world, never 0, assigned at creation, never changed), `_name` (owned, may be NULL, at most
  `WORLD_OBJECT_NAME_MAX_LENGTH` = 256 bytes of valid UTF-8);
- `_parentId` (0 for a root) and the hierarchy's links (`_parent`, `_firstChild`, `_lastChild`, `_previousSibling`,
  `_nextSibling`, `_childCount`); see "Hierarchy";
- the transform, **relative to the parent** (world values for a root): `_position` (a `Vector3Double`, see "Positions
  are doubles"), `_rotation` (Euler XYZ in radians, degrees in JSON), `_scale`; the cached `_worldMatrix` (a
  `Matrix4Double`) with `_isWorldMatrixDirty`, and `_transformRevision`;
- `_tint`: a `RenderColor` (brightness and opacity in [0, 1]; the renderer draws the opacity as a dither, and blended or
  additive sprites as alpha: WorldRuntime.md, "Visibility");
- `_isEnabled` (default true): false makes **every system** skip the object and its subtree: the renderer, later
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
- `_isPersistent` (default true, T13): whether world files save it. A runtime flag, not a property (not saved, no
  revision): objects made for the session (the player, spawned NPCs, effects) turn it off with
  `WorldObject_SetPersistent`, and the writers leave them out with their subtrees ([WorldFile.md](WorldFile.md)).

Every field is reached through getters and validating setters (`WorldObject_SetPosition`, `_SetEnabled`, ...). The
setters check through the object's property table (`Properties_CheckValue`), so the rules live in the table only, and
`Properties_Set` / the JSON and GHDF readers / `Properties_CopyTo` work on objects too; either way the revision
advances (the table's `AfterSet`). The `data` row is `ReadOnly` for `Properties_Set`; a read or a copy (whose
`AfterSet` gets no row) advances both revisions and has the world recount the object's bytes and re-index its tags.

## Hierarchy

Decided as G3 (a), built in T08. An object may have a **parent**; its position, rotation and scale are then relative to
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
    (`WorldObject_GetTransformRevision`: equal values mean the world matrix is the one a system saw; T12's collision
    uses it);
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
- **Sockets (T30).** An object may hang on a named **socket** of its parent's world model (a hand, a hook, a seat;
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
  refusal is logged (once per world). Tables check each value alone, so this is where a loader learns of a bad parent
  (T09). A copy from an object of **another** world brings that world's parent id, which here names another object or
  none: copy within one world, or set the parent afterwards.

## Positions are doubles

Decided as G25 and G26, built in T31 to T33. A float's step is 8 mm at 100 km and a metre at 10 000 km, so
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
  (`GameMath_ToVector3Relative`, `GameMath_ToMatrixRelative`): the renderer relative to the camera (T32,
  [WorldRuntime.md](WorldRuntime.md)), the collision world relative to a query's start or a pair's first body
  ([WorldCollision.md](WorldCollision.md)), the characters relative to their feet ([Characters.md](Characters.md)).
  `GameMath_ToVector3` of an absolute position is only for values known to be small (a child's local position).
- **Files** write positions as doubles: JSON with the shortest text that reads back the same double (a float written
  before T33 prints the same), GHDF as Double arrays; Float arrays from older saves still read
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
| `WorldComponentType_Collider` | 3 | `collider` | `WorldColliderComponent` (T12) |
| `WorldComponentType_Character` | 4 | `character` | `WorldCharacterComponent` (T13, a transform driver; [Characters.md](Characters.md)) |
| `WorldComponentType_Animator` | 5 | `animator` | `WorldAnimatorComponent` (T29; [Animation.md](Animation.md)) |
| `WorldComponentType_Terrain` | 6 | `terrain` | `WorldTerrainComponent` (B01; [Terrain.md](Terrain.md)) |

The values are explicit and never renumbered (world files will save them). `WorldComponentType_Count` is the number
of standard types; `WORLD_MAX_COMPONENT_TYPES` (16) is how many a world can hold, room for the types later tasks add
(7 is B03's, then the ambient zone T14 and the screen T19) and for test-only types.

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
ever had (from 1, in add order). The collision world compares serials (T12); a cache that keeps component pointers
across component-set changes should too.

**Downcasts.** `WorldModelComponent_FromComponent(component)` (and the sprite's and light's) returns the concrete
struct, or NULL for NULL or another type; `..._AsComponent` upcasts.

### Model component (`model_component`)
Since T10 the component names a **world model** ([WorldModels.md](WorldModels.md)): the reusable definition of the thing
placed (its LOD models, draw distance, render defaults and variants). `_worldModel` (an asset name, NULL draws nothing;
changing it swaps the model and keeps the object), `_usesVariant` (false) and `_variant` (a variant name, kept while
`_usesVariant` is off; an unknown one draws the model's own materials and the renderer warns once).

The settings a placement can override (`WorldModelSetting`) are each a pair: an override flag, `_overrides...` (false:
the world model's value applies), and the placement's own value, kept while the flag is off: `_hasOutline` (true),
`_omitPixelation` (false: drawn crisp at full resolution over the pixelated image), `_shadowTier` (`WorldShadowTier`,
declared in `WorldModel.h` since T10: Both / Near / Far, the shadow cascade(s) it casts into), `_castsShadows` (true;
false skips it in every shadow pass, it still receives shadows), `_drawDistance` (600 m, above 0) and
`_isDrawDistanceFixed` (false: the player's `draw_distance_scale` applies). `_drawDistanceMultiplier` (above 0, default
1) multiplies whichever draw distance is in effect. The flags are set with `WorldModelComponent_SetOverridden(self,
setting, isOverridden)` and read with `_IsOverridden`; `WorldModelComponent_ResolveSettings(self, modelSettings)` is the
pure function that gives the `WorldModelSettings` in effect. The getters of the values (`WorldModelComponent_HasOutline`
and the others) return the placement's own values, not the ones in effect.

**LODs** (T11, [WorldModels.md](WorldModels.md), "LODs"): `_lodDistances` (the placement's own switch distances, a
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

### Sprite component (`sprite_component`)
Since T26 the component names a **world sprite** ([WorldSprites.md](WorldSprites.md)): the sprite animation to play and
how it stands, is lit and is seen (T06's settings, which used to live here and are now the world sprite's defaults).
`_worldSprite` (an asset name, NULL shows nothing; changing it swaps the sprite and keeps the object), the runtime
`_animationInstance` (`SpriteAnimationInstance` by value: the playback state game logic advances and the renderer reads;
`WorldSpriteComponent_GetAnimationInstance`) and the runtime `_boundWorldSprite` (the world sprite it plays,
`WorldSpriteComponent_GetBoundWorldSprite`).

**Every setting can be overridden** (the owner's answer for T26): for each `WorldSpriteSetting` an override flag (the
array `_overrides`, false: the world sprite's value applies) and the placement's own value, kept while the flag is off.
The values and their defaults are T06's (the renderer's side is [WorldRuntime.md](WorldRuntime.md), "Sprites"):
- **Shape:** `_orientation` (`WorldSpriteOrientation`, declared in `WorldSprite.h` since T26: `billboard` faces the
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
  default 1), still fogged), `_lightWrap` ([0, 1], default 1: how far light wraps around a billboard or axis billboard,
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
How the object takes part in collision (T12; the shapes, the layers and the collision world are in
[WorldCollision.md](WorldCollision.md)). The rows, in order: `is_enabled` (true); `overrides_collision_set` /
`collision_set` (off: the world model's default set; a door swaps sets to swap hitboxes); `uses_own_shape` (one
primitive of its own instead of the sets) with `shape_type` (`sphere`, `capsule`, `box` (the default), `cylinder`,
`plane`), `shape_center`, `shape_rotation`, `shape_radius`, `shape_half_height`, `shape_half_extents`,
`shape_is_bounded`, `shape_plane_half_extents` and `shape_depth`; `overrides_layer` / `layer` (`prop`),
`overrides_collides_with` / `collides_with` (an `EnumSet` of layers: prop's default mask), `overrides_is_trigger` /
`is_trigger`; `offset`, `rotation`, `scale` (a local transform of all its shapes, the scale above 0); `rotation_mode`
(`full`, `yaw_only`, `none`). Since T27, in the category "Motion": `is_pushable` (false: a movable object's collider is
kinematic, it pushes and is never pushed), `overrides_mass` / `mass` (1 kg, above 0, at most `COLLISION_MAX_MASS`),
`when_blocked` (`stop` or `push_through`), `is_continuous` (true: its motion is swept) and
`reports_persisting_contacts` (false). Without the flags the world model's `collision` section gives the set, layer,
mask, trigger flag and mass (`prop`, prop's mask, no trigger and 1 kg without one).
`WorldColliderComponent_ResolveSettings(collider, collision)` is the pure function that gives the settings in effect;
`SetOwnShape` sets the own primitive from a `CollisionShape`, all or nothing. No hooks: the collision world watches the
revision.

### Character component (`character_component`)
The player or an NPC (T13; everything about characters is in [Characters.md](Characters.md)): `character` (the asset
name of a character, `characters/`), then for each of the character's 36 settings an override flag and the placement's
own value (`overrides_radius` / `radius`, ..., `overrides_interaction_layers` / `interaction_layers`), whose rows it
shares with the character asset's table. Its input (`CharacterInput`) and its state are runtime only. It is the first
**transform driver**: the character system (`WorldCharacters`) writes its object's position (the feet) and yaw once a
tick, and gives the object a capsule collider whose shape and motion rows it keeps in step.

### Animator component (`animator_component`)
What plays the object's skeletal animation (T29; everything about animation is in [Animation.md](Animation.md)):
`plays_clip` and `clip` (the clip it plays from the start), `phase` (where it starts, a share of its length), `speed`,
`is_paused`, `fade_seconds` (a clip change's cross-fade), `overrides_is_looped` / `is_looped`,
`overrides_animation_libraries` / `animation_libraries` (the placement's own libraries instead of its world model's)
and, since T30, `overrides_animation_graph` / `animation_graph` (its own graph).
The playback (clip times, weights, fades, the graph's states, the action) and the pose are runtime only: the animation
system (`WorldAnimation`) keeps them in the component (its runtime, freed with the component; removing the component
ends its action first, freeing its character) and publishes the pose for the renderer and sockets. Not a transform
driver: it bends the object's model, never moves the object.

### Terrain component (`terrain_component`)
Places a terrain (B01; everything about terrain is in [Terrain.md](Terrain.md)): `terrain` (the asset name of a
terrain, `terrains/`; none places nothing), then the override pairs over the asset's defaults, as the model
component's: `overrides_has_outline` / `has_outline`, `overrides_shadow_tier` / `shadow_tier`,
`overrides_casts_shadows` / `casts_shadows`, `overrides_draw_distance` / `draw_distance`, `draw_distance_multiplier`
(above 0, default 1), `overrides_is_draw_distance_fixed` / `is_draw_distance_fixed`, and the collision's,
`overrides_is_collision_enabled` / `is_collision_enabled`, `overrides_collision_layer` / `collision_layer`,
`overrides_collides_with` / `collides_with`. `WorldTerrainComponent_ResolveSettings` gives the settings in effect (a
mask neither the asset nor the placement gives is the layer in effect's default). The object's transform places the
terrain's space; the object is static by default, and moving it works (the collision world moves the chunks, with the
static-move warning). Not a transform driver. An object with a collider as well collides through its collider alone.

### Light component (`light_component`)
`_color` (white), `_intensity` (>= 0, default 1), `_range` (>= 0, default 1: how far the light reaches; T05 renamed it
from `Size`), `_castsShadows` (false). It uses its object's world position (a lamp carries its light); rotation, scale
and tint do not affect it. A hidden or disabled object's light is off, also under a hidden or disabled ancestor.

## World

Holds the objects in creation order (`_objects`) with an id -> object map (`_objectsById`, a `WRHashMap`), the
hierarchy's bookkeeping (how many world matrices are dirty, the recompute counter for tests, the refused parents, the
removal scratch), the registry entries of its component types, one dense array of components per type (`_components[type]`, in add order),
the id counter (`_nextId`, starts at 1; 0 once every id was handed out: ids run from 1 to `WORLD_OBJECT_ID_MAX`,
`UINT64_MAX - 1`, since T09), two versions, an owned name, an embedded
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
  world since T09) for iteration in creation order. A new object is a root at the default transform. Removal takes the whole subtree (see
  "Hierarchy"). `World_EnsureNextObjectIdAtLeast` and `World_GetNextObjectId` are for world files.
- **Name:** `World_SetName` / `World_GetName`: NULL or at most `WORLD_NAME_MAX_LENGTH` (256) bytes of valid UTF-8
  (since T09 a longer or broken name is refused, as an object's is); world files save it.
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
  transform drivers that way to test the one-driver rule before the game has a real driver.

## Ready for things that move

The game has objects that move and react to each other: the player and NPCs (the `character` component, T13: the first
transform driver), cars (a `vehicle` component, later), physics objects (a `rigid_body` component, later, decision G13).
The model is ready for them:

- **One driver per object.** A component type flagged `IsTransformDriver` writes its object's transform every tick.
  `World_AddComponent` refuses a second driver on the same object (`ErrorCode_InvalidOperation`), so two systems never
  fight over one transform. Adding a driver makes the object movable. A driven object is normally a root object; with
  a parent the driver writes the local transform through the parent's inverse (`WorldTransform_TryGetLocalMatrix`,
  then `WorldTransform_Decompose`).
- **The collision step corrects drivers (T27).** It is the one system besides an object's driver that writes the
  object's transform: a driver writes where its object wants to be, `WorldCollision_Step` moves a pushed object's
  position by its push (a child's through its parent's inverse) and sets a stopped mover's chain back to where it was
  at the start of the tick, and the driver reads the correction next tick (`WorldCollision_GetMotionResult`). Drivers
  must therefore read their object's transform each tick rather than keep their own copy (the test frame's door and
  pushing wall do).
- **Moving with a parent.** An object under a movable parent moves in play although its own mobility may be static:
  systems that cache static objects must watch the transform revision (it advances when an ancestor moves), not the
  mobility alone (T12).
- **Mobility is a hint for performance, never a lock.** Collision (T12) keeps static colliders in a structure built
  once and movable ones in one updated every tick; the renderer can later cache static shadows. Moving a static object
  still works (the editor does it constantly): it costs a rebuild of whatever cached it, and in play the system that
  rebuilds logs one warning per object. A driven object may even be set back to static; it still moves, at that cost.
  T12's static tree is the first such cache: it sees the move through the object's transform revision, re-inserts the
  collider and warns once ([WorldCollision.md](WorldCollision.md)).
- **The player is an ordinary object** with a character component. NPCs, triggers, vehicles and physics then all
  treat it like any other object; no frame special-cases it.
- **The tick order.** Each game tick runs the systems that change a world in this order, so each later system knows
  its slot:
  1. input (`GameInput`, polled by the frame manager);
  2. scripts (later);
  3. transform drivers: scripted movers (the test frame's desk, door and pushing wall), then **the characters**
     (`WorldCharacters_Update`, T13) last among them, so a platform moved this tick carries the characters on it
     exactly; vehicles and physics later;
  4. the collision step: `WorldCollision_Step` (T27), which syncs (T12; the world matrices first), sweeps every
     collider that moved, pushes apart what overlaps and writes the pushes back;
  5. queued events: trigger enter and exit (`WorldCollision_GetTriggerEvents`, then `_ClearTriggerEvents`), contacts
     and motion events (T27: `_GetContactEvents`, `_GetMotionEvents`, then their clears; the queued-event rule,
     architecture A4);
  6. animation, effectively enabled objects only: the skeletons' playback (`WorldAnimation_Update`, T29: the animators'
     rows and clips; since T30 their graphs, fed the characters' state, and their actions, [Animation.md](Animation.md)),
     then its queued events (`WorldAnimation_GetEvents`, then `_ClearEvents`), then sprite playback
     (`AdvanceSpriteAnimations` in the test frame);
  7. `World_UpdateTransforms`: the world matrices of everything that moved this tick (T08);
  8. once per shown frame, after the loop's ticks, the frame's **PrepareRender** (G35, [GameFrame.md](GameFrame.md)):
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
| | `overrides_shadow_tier` | 10 | Bool | false |
| | `shadow_tier` | 4 | Enum `both` / `near` / `far` | `both` |
| | `overrides_casts_shadows` | 11 | Bool | false |
| | `casts_shadows` | 5 | Bool | true |
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
| | `overrides_draw_distance` | 32 | Bool | false |
| | `draw_distance` | 33 | Float > 0 | 150 |
| | `draw_distance_multiplier` | 34 | Float > 0 | 1 |
| | `overrides_is_draw_distance_fixed` | 35 | Bool | false |
| | `is_draw_distance_fixed` | 36 | Bool | false |
| `light_component` | `color` | 1 | Color | white |
| | `intensity` | 2 | Float >= 0 | 1 |
| | `range` | 3 | Float >= 0 | 1 |
| | `casts_shadows` | 4 | Bool | false |

T10 renamed the model component's wire 1 from `model` (a model asset) to `world_model` and made wires 2 to 5 the
placement's own values behind their override flags; T11 added the LOD rows (17 to 21); its next free wire id is 22. T26
did the same to the sprite component: wire 1 `sprite_animation` became `world_sprite`, wires 2 to 16 the placement's own
values behind their flags (wires 17 to 31), and the draw distance rows came new (32 to 36); its next free wire id is 37. The sprite's value rows
share their key, type, default, rules and texts with the world sprite's tables (`source/WorldSpriteInternal.h`,
[WorldSprites.md](WorldSprites.md)). Each override flag sits right before its value, in table order (the inspector's and
the JSON writer's order), and `PropertyTable_Check` checks the pairing. The collider's table is in
[WorldCollision.md](WorldCollision.md), the character's in [Characters.md](Characters.md) and the animator's (T29) in
[Animation.md](Animation.md).

The object table's wire ids start at 3: 1 and 2 are the world file's own entries per object (the id and the
components, [WorldFile.md](WorldFile.md)). T24 added `data` (one row: the tags and the values are one map, written as two JSON keys, `tags` and
`data`, and as one GHDF entry); T08 added `parent` (12, placed after `name` in the table's order). The next free object
wire id is 13.

## Environment

`WorldEnvironment` is a struct with the date and time of day, sky, sun, night sky, moon, ambient, fog and effect
settings, and the world's gravity (T13: `gravity`, 9.81 m/s² straight down by default, in [0, 1000]; characters
multiply it by their own `gravity_multiplier`).
**Every field is one row of its property table** (`WorldEnvironment_GetPropertyTable`, in `source/WorldEnvironment.c`; module doc
[PropertyDescriptors.md](PropertyDescriptors.md)): the key (the field name in snake_case, e.g. `light_pollution`), the
GHDF wire id, the type, the default, the rules, the inspector text. `WorldEnvironment_SetDefaults`, `_Validate`,
`_CopyTo` and `_Deconstruct` are thin wrappers over the table, and `WorldEnvironment_ReadJSON` / `_WriteJSON` are the
JSON entry points for the world files (T09). **To add a setting, add the field and one table row** with a new,
never-used wire id (the next free one is 47; 2 and 3 are retired); nothing else needs writing. The field comments state
each value's rule; validation enforces them (finite, `TimeOfDay`, `LightPollution`, `StarTwinkle`, `MoonPhase`,
`NightVisionStrength` and `NightVisionBlueShift` in [0, 1], `SunSizeMultiplier`, `MoonSizeMultiplier` and `MoonCycleDays` > 0, every strength/intensity/density/brightness
>= 0, `Date` inside the calendar, the texture names valid asset names of at most 128 bytes, `MoonPhaseSource` one of
its names) and names the first broken field by key and C name: `world_environment.light_pollution (LightPollution)
must be finite and in [0, 1].` `SunAngle` and `MoonOrbitTilt` are `Angle`s: radians in memory and GHDF, degrees in
JSON.

**Date and time of day are what the sky shows, not a clock.** `TimeOfDay` (a fraction of the day) and `Date` (a day
number of [GameCalendar.md](GameCalendar.md), a `Date` property: `"2026-10-18"` in JSON, default 2026-10-18 like a new
game clock) never advance by themselves. While a game runs, the game clock ([GameClock.md](GameClock.md)) writes both
every tick; in a world file they are where the sky starts (the editor's preview uses them, and the test frame starts
its clock from them). T23 removed the old day-night timer (`is_day_night_cycle_enabled`, `day_length_seconds` and
`WorldEnvironment_Advance`; the clock's pause and time scale replace them). Their wire ids 2 and 3 are retired, so GHDF
written before still reads (the entries are skipped). The table stayed at version 1: no environment JSON had been
written yet (world files come with T09), so no migration is needed.

**The environment owns its texture names** (`MilkyWayTexture`, `MoonTexture`; T03): `WorldEnvironment_SetDefaults`
constructs and allocates them (call it on a new environment only; to reset one, `Deconstruct` first) and pairs with
`WorldEnvironment_Deconstruct`; copies go through `CopyTo` (never assignment: a copied pointer would be freed twice);
a name is changed through the table (`Properties_Set`), which clones the new one and frees the old. `World` pairs
them.

The night sky settings: `StarSeed` (which stars), `StarDensity` (1 = an Earth-like sky of about 9000 stars, the
default; 0 none; at most 20000 stars), `StarBrightness`, `StarTwinkle` (0..1, default 0.25), `MilkyWayBrightness`
(default 1), `MilkyWayTexture` (default `sky_milky_way`), `LightPollution` (0 = a dark site .. 1 = a city centre,
default 0.35, a small town) and `LightPollutionColor` (sodium orange by default).

The moon settings (T03; the owner's rule: as many settings for story creators as possible):

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
| `moon_intensity` | >= 0 | 0.02 | The full moon's light relative to the sun's (`WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY`). |
| `moon_color` | colour | (200, 214, 255) | The moonlight's and the disc's colour. |
| `moon_texture` | asset name | `sky_moon` | The moon's face. |

And `are_seasons_enabled` (default false): the sun's declination follows the date (23.44 degrees x the sine of its
place in the year, 0 at the March equinox; a uniform year, so the September equinox falls about two days early), so
with `SunAngle` > 0 summer days are longer and the noon sun higher, and the moon follows the ecliptic (a full moon low
in summer, high in winter). Off by default: it changes how much daylight the working hours get.

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
  moon's) and `_GetMoonlightFactor` (brightness x its height x (1 - daylight); 0 without a moon).
- `WorldEnvironment_GetSkyLights`: both lights at once (directions, intensities, the moon's phase and illumination)
  and the shadow light: the brighter by luminance once it stands high enough, with a fade (in from 0.05 to 0.15 of
  elevation, out around the hand-over between the two and for a light too dim to show a shadow), so the hand-over from
  the sun to the moon never jumps.

Validate anything that came from outside the program before using it.

## Persistence

**World files** ([WorldFile.md](WorldFile.md), format in `references/world_structure.md`, T09) save and load a whole
world as JSON or GHDF through the tables: the environment's, each object's and each component's, with the root ids the
old encoder used (format version 1, name 2, next id 3, environment 4, objects 5). The loader creates every object first
(`World_CreateObjectWithId`), then reads their tables, so each `parent` names an object that exists; it compares
`World_GetRefusedParentCount` around each object's read to refuse a bad parent and name the object, checks
`World_GetDataByteSize` against `WORLD_DATA_MAX_BYTES` after each object, reads data maps with
`PropertyReadOptions.Keys` (`World_GetKeys`) and `DefaultNamespace`, and truncates the key registry when it refuses a
file. T05 had removed the hand-written snapshot (`WorldDTO`) and its GHDF encoder (`WorldEncoder`); nothing replaces them
but the tables. `tests/PropertyDescriptorTests.c` still compares the environment's GHDF tree with a literal copy of what
the old encoder wrote (since T09 integers take the smallest GHDF type, so the seed's entry differs there).

Runtime state is not saved: playback positions, revisions, data scripts change in play (game saves as differences from
the world come with the save round, architecture 3.7).

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
the environment's GHDF tree matches the old encoder's, its JSON round trip). `tests/WorldRenderCacheTests.c` covers the
renderer's side ([WorldRuntime.md](WorldRuntime.md)). They run headless with `sh tests/run_linux.sh`;
`tests/TestAssets.h` provides code-built sprite animation, model and texture definitions that load without files or a
GL context, and world models and world sprites built from JSON over them. `tests/WorldModelTests.c` covers the world
models and the model component's overrides ([WorldModels.md](WorldModels.md)), `tests/WorldSpriteTests.c` the world
sprites and the sprite component's overrides ([WorldSprites.md](WorldSprites.md)), `tests/WorldColliderTests.c` the
collider component and the world models' collision, `tests/WorldCollisionTests.c` the collision world and
`tests/WorldCollisionMotionTests.c` its step ([WorldCollision.md](WorldCollision.md)).
