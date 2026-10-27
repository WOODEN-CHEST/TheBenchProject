# Animation

Skeletal animation: 3D models that bend with a skeleton and play clips, blended and cross-faded, with **a pose
of their own per placed object**, so a hundred NPCs on one model each walk at their own phase. The renderer draws the
poses in every pass. An **animation graph** decides what a character plays (states, transitions, layers,
look-at), **special actions** (sitting on a bench, picking up a cup) are started the same way for the player and NPCs,
objects **hang on sockets** of a posed model (a cup in a hand), and the player sees **its own body in first person**.

The design behind it: raylib is built with **GPU skinning** (`BUILDING.md`); clips are read by **our own glTF
reader** (cgltf); clips live in a **separate asset type, `animation_library`**, so one set of clips plays on every model
of a skeleton layout (humans of every size); playback is an **animator component**, apart from the model component. The
graph is **its own asset type**, `animation_graph`, that creators edit; **no root motion** (clips move in place, an
action's anchor slides the character); **foot IK is not built yet**; **first person shows the real body without its
head**; skeletons are **posed once per rendered frame**, in a step the frame manager runs right before a render.

| Module | Files | What it is |
|---|---|---|
| AnimationLibrary | `include/AnimationLibrary.h`, `source/AnimationLibrary.c` (sampling), `source/AnimationLibraryGltf.c` (the reader), `source/AnimationLibraryDefinition.c` (the asset type) | The `animation_library` asset: a file's bones (names, parents, rest poses) and its clips (tracks of keys), read from glTF treated as hostile; sampling a track at a time. |
| GameSkeleton | `include/GameSkeleton.h`, `source/GameSkeleton.c` | A skinned model's skeleton, built once when the model loads (`GameModel_GetSkeleton`): bones in evaluation order, bind and inverse bind matrices, rest poses, which meshes the GPU skins, each bone's vertex box. |
| SkeletalAnimation | `include/SkeletalAnimation.h`, `source/SkeletalAnimation.c` | The runtime maths: a library bound to a skeleton (retargeting), clips sampled onto it, poses blended, the mixer (clips with times, weights and fades), bone matrices, the skinning palette, the pose's bounds; transforms composed and taken apart. |
| AnimationGraph | `include/AnimationGraph.h`, `source/AnimationGraph.c` (the block and its accessors), `source/AnimationGraphReader.c` (the JSON reader), `source/AnimationGraphDefinition.c` (the asset type) | The `animation_graph` asset: parameters, events, layers of states and transitions, actions, look-at, as one immutable block of indices (no pointers: it copies as bytes). |
| AnimationGraphPlayer | `include/AnimationGraphPlayer.h`, `source/AnimationGraphPlayer.c` | Playing a graph on one skeleton, pure functions over caller memory: the binding (clips, masks, look bones), the instance (parameters, events, per-layer states with fades), the tick's advance and the pose. |
| WorldAnimatorComponent | `include/WorldAnimatorComponent.h`, `source/WorldAnimatorComponent.c` | Component type 5 (`animator`): the clip row and how it plays (phase, speed, pause, fade, loop override, the placement's own libraries and its own graph), and the pose the system publishes (with its sockets). |
| WorldAnimation | `include/WorldAnimation.h`, `source/WorldAnimation.c` (binding, the tick, the pose step), `source/WorldAnimationGraph.c` (graphs: the character's state in, parameters, triggers), `source/WorldAnimationActions.c` (actions, anchors, attach and detach), `source/WorldAnimationInternal.h` (private: the per-animator runtime) | The animation system: each tick it binds, applies the rows, runs the graphs and actions and advances every animator; once per shown frame it poses them; code plays clips, sets parameters and starts actions through it; its events queue for the game. |
| Skinned passes | `shaders/common/skinning.glsl`, `shaders/*/skinned.vert` and their definitions; `source/WorldRendererGeometry.c` and the pass files | The renderer's side: every geometry pass has a skinned twin of its shader and uploads each posed object's palette (WorldRuntime.md, "Renderer structure"); the camera's passes draw the first-person object without its head. |
| WorldTestAnimation, WorldTestCharacters (test content) | `source/WorldTestAnimation.{h,c}`, `source/WorldTestCharacters.{h,c}` | The test frame's animation system and its debug keys (5, 6, 7); the humans' bodies, the use key starting actions, and keys 8, 9, 0. |

## Assets

**Skinned models are ordinary `model` assets** whose glTF file has a skin: raylib loads the skeleton and the bone
indices and weights (at most four per vertex), and the model's definition builds its `GameSkeleton` at load
(`source/ModelDefinition.c`; a skeleton that fails its checks fails the model). raylib's own animation functions are
**not used**: they pose the shared model in place (every placement would show one pose and drawing would write the
asset), and its glTF clip import resamples to 60 frames a second with a wrong last frame.

**Animation libraries** (`animation_library`, JSON in `animation_libraries/`, the file `.glb` or `.gltf`; every key in
`references/asset_structure.md`, "Animation libraries"): a file of clips, with or without a mesh (a Blender export of
the actions alone is a library: its armature object becomes a bone the skeleton simply lacks), and per clip its name,
the clip in the file (`source`, default the name), whether it loops (default no), its speed (default 1) and
the metres it covers per cycle (`distance`, none by default; a graph's `match_speed` plays a gait at the character's real
speed from it, scaled by the height ratio). A clip the
definition does not name keeps the file's name when that is a valid clip name (`[a-z][a-z0-9_.-]*`), does not loop and
plays at speed 1. **The reader treats the file as hostile.** cgltf's validation does its sums without overflow checks
and its loader allocates each buffer's declared length, so before cgltf loads or validates anything the reader checks
the structure itself, in overflow-safe sums (`CheckBuffers`, `CheckBufferViews`, `CheckAccessors`; in the
private `source/GltfChecks.c`, which the terrain's import shares): buffers of 1 byte or more, at most 256 MiB together,
a data URI no longer than its base64; views inside their buffers with glTF's strides (none, or 4 to 252 bytes in
fours) and no meshopt compression; accessors with a type and an element, inside their views, aligned to their
components, a sparse one's replacements (1 to its count) inside theirs. A buffer file's URI is checked as cgltf opens it, with its percent escapes decoded
(`%2e%2e` is `..`): beside the file, no absolute path, `..`, backslash, drive or NUL; the file must hold the buffer's
declared length (only that is kept), and loaded buffers must start aligned (a `.glb`'s chunks padded to four).
cgltf's parse allocates every JSON array's elements before any check can run, so its allocations have a budget:
64 MiB beyond the file's size for the parse, the 256 MiB limit on top for the buffers; past it the read
fails with a message instead of exhausting memory. Then
cgltf validates (the node tree, sampler counts), and key times must be finite, at least 0, rising and at most an hour;
values finite, rotations of a length above 0 (normalized on reading, the length reckoned in double); limits on bones
(256), clips (256) and keys (4 194 304 floats' worth). The interpolations are glTF's: STEP, LINEAR (rotations along
the shorter arc) and CUBICSPLINE; the first key holds before it, the last from its own time on, exactly.

**A world model names the libraries its placements play** (`animation_libraries`, at most 16; `WorldModels.md`), and
its skinned LODs share one skeleton (every LOD with bones must have the first one's bones and bind poses: a world model
that breaks it is refused). Two of its libraries may not hold a clip of the same name. A placement may play its own
libraries instead (the animator's `overrides_animation_libraries`).

## One clip on many models (retargeting)

`SkeletalBinding_Build` binds a library to a skeleton **by bone name**. A skeleton bone the library lacks keeps its
rest pose; a library bone the skeleton lacks (the armature object, a twist bone) folds its rest pose into its nearest
matched descendants, so a clip authored under it still lands right. Sampling a clip onto the skeleton:

- **rotations** are the clip's (the shape of a motion is in its turns);
- **translations** move each bone from *its own* rest by the clip's change from the library's rest, scaled by the
  **height ratio**: the skeleton's height over the library's, measured as the highest rest point of the bones they
  share. A short figure's hips bob and stride in proportion; its bone lengths stay its own;
- **scales** are relative to the library's rest scale.

So **the rigs must share one skeleton layout** (bone names and how they hang), not one size: a tall, a short and a
stocky human play the same walk. A clip played on another kind of skeleton matches nothing and moves nothing (the
system reports a library that matches no bone, once).

## Playback (`SkeletalAnimation.h`)

The **mixer** (`SkeletalMixer`, a plain value) plays up to 8 clips at once, each with its time, weight and fade.
`SkeletalMixer_Play` cross-fades: the clip rises to full weight while every other falls to 0, all sharing one progress
(the weights keep their sum); a clip already playing keeps its time unless restarted or ended. `SkeletalMixer_SetWeight`
moves one clip alone (blends). Looping clips wrap; others stop at their end and hold their last
pose; clips faded to 0 stop. Poses blend as weighted sums (rotations turned into the running sum's hemisphere); a total
below 1 is filled with the rest pose, above 1 divided out. The pose becomes each bone's matrix in mesh space, the
**palette** (per bone the first three rows of its skinning matrix, 12 floats) and the **bounds** (each bone's vertex box
moved by its skinning matrix, united with the box of the meshes that do not bend).

## The animator component (`animator_component`)

Component type 5, **not** a transform driver: it poses the object's model and moves no object. Its rows (every one
saved; the playback itself, times and weights, is runtime state):

| Key | Wire | Type | Default | Meaning |
|---|---|---|---|---|
| `plays_clip` | 1 | Bool | false | Plays `clip` from the start (off: only what code plays). |
| `clip` | 2 | String (≤ 64 bytes) | null | The clip, by name, from the libraries; a change cross-fades. |
| `phase` | 3 | Float [0, 1) | 0 | Where the clip starts, a share of its length, so placements of one model move out of step. |
| `speed` | 4 | Float [0, 100] | 1 | Times every clip's own speed (0 holds the pose while fades go on). |
| `is_paused` | 5 | Bool | false | Holds clips and fades alike. |
| `fade_seconds` | 6 | Float [0, 60] | 0.2 | How long a change of `clip` cross-fades. |
| `overrides_is_looped`, `is_looped` | 7, 8 | Bool | false | Whether the clip loops instead of its library's setting. |
| `overrides_animation_libraries`, `animation_libraries` | 9, 10 | Bool; AssetNameList (16 names of ≤ 128 bytes) | false, none | The placement's own libraries instead of its world model's. |
| `overrides_animation_graph`, `animation_graph` | 11, 12 | Bool; AssetName (≤ 128 bytes) | false, null | The placement's own graph instead of its world model's (on with no name: no graph). |

The next free wire id is 13. The pose the system publishes (`WorldAnimatorComponent_GetPose`: the world model it was
made for, the skeleton, the palette, the bone matrices, the bounds, a revision and the world model's sockets
with their frames in the pose and the first_person block's eyes socket) is valid until the system's next pose step;
NULL while the animator has none.

## The animation system (`WorldAnimation.h`)

`WorldAnimation_Create(world, assets, logger)` after the world; `WorldAnimation_Destroy` before it (it ends every action,
freeing the characters they held, releases every animator's state and pose, and its asset user). Two steps:

`WorldAnimation_Update(seconds)` runs in **the tick's animation step** (WorldData.md, "The tick order": after the
collision step and its events, before `World_UpdateTransforms`), for each animator of an effectively enabled object:

1. **binds** when needed: to the world model its object's model component names (loaded under the system's own asset
   user; again whenever the name changes), its skeleton, its libraries (the world model's, or the placement's own),
   each bound to the skeleton by bone name, and its graph (the world model's, or the placement's own). A bind starts
   from the rest pose, ends the action it played (its character is freed) and applies the rows at once: the clip row
   plays at full weight from its phase;
2. **applies the rows** when they changed since: a new clip cross-fades in over `fade_seconds` (a clip already playing
   keeps its time), or out when `plays_clip` goes off; a loop override reaches the playing clip;
3. **runs its graph**: the object's character's state into the built-in parameters and its events, the action's
   progress (an approach that arrived enters the action's first state), then the graph's advance by the tick's seconds
   times the animator's speed: transitions, times, timed events and their effects;
4. **advances** its clips by the tick's seconds times their speeds and the animator's (nothing while paused).

`WorldAnimation_Pose()` is **the pose step**, once per shown frame: the frame's `PrepareRender` (GameFrame.md) calls it,
then `World_UpdateTransforms`. Each animator whose playback changed since its last pose is sampled (the graph's layers,
then the code's and the row's clips over them by their weight, then look-at), and its bone matrices, palette, bounds
and sockets are computed and published; what hangs on its sockets is marked dirty. A resting animator (its rest pose,
nothing playing) is not posed again. The status counts the animators posed by the last pose step and its time
(`RecomputedCount`, `PoseMicroseconds`), the graphs and the actions.

A disabled object's animator keeps its pose. Every problem (a missing world model, one without a skeleton, a library
that does not load or matches no bone, an unknown clip) is logged once per animator, naming the object, never per
tick. `WorldAnimation_GetStatus` counts the animators, those posed, the clips playing, the bones posed and the
update's time.

**Code plays clips** through the system (scripts later):
`WorldAnimation_PlayClip(object, clip, fade, isRestarted)` (a cross-fade), `_SetClipWeight` (one clip alone) and
`_StopClips` (back to the rest pose). They bind the animator first if needed, so a clip played right after the object
was made is not undone by its rows. A clip played from code fades the row's clip out like any other; the rows apply
again only when they change.

**Bones.** `WorldAnimation_TryFindBone(object, name)` gives a bone's index; `_TryGetBoneWorldMatrix(object, bone)` its
world matrix in the last pose, as a `Matrix4Double`: the object's world matrix times the skeleton LOD's import transform
times the bone's matrix, so a held item does not wobble far from the world's origin (the far test suite checks it 173 km
out).

## Animation graphs

A graph (`animation_graph`, JSON in `animation_graphs/`; every key in `references/asset_structure.md`, "Animation
graphs") is what a character plays when. A world model names its graph (`animation_graph`, a dependency); an animator
may name its own. It names **clips, not libraries**: the animator plays it with the clips of its libraries, so one
graph serves every model of a skeleton layout.

- **Parameters.** The **built-in** ones, filled every tick from the object's character (zero without one): `speed`,
  `speed_3d`, `forward_speed`, `side_speed`, `vertical_speed`, `is_grounded`, `is_touching_ground`, `is_flying`,
  `is_crouching`, `crouch_amount`, `time_in_air`, `turn_rate`, `look_yaw` and `look_pitch` (relative to the body, in
  degrees), `is_stand_up_blocked`, `landing_speed`, `is_in_action`, `move_amount`. **Custom** floats (with bounds),
  booleans and triggers (at most 32) that code, scripts and the graph's own events set
  (`WorldAnimation_SetParameter`, `_FireTrigger`).
- **Events** are true for one tick: the character's (`jumped`, `landed`, `left_ground`, `crouched`, `stood_up`,
  `took_off`, `stopped_flying`, `used`), `end_action` (which `WorldAnimation_EndAction` fires) and the triggers.
- **Layers.** The first is the body; each further one plays over the layers before it on its **mask** (bones with
  their subtrees; none: every bone) by its weight and by how much of it plays something (an empty state plays nothing).
- **States** play nothing, one clip, or a **blend** of up to 8 clips by a parameter, kept in step by phase.
  `match_speed` plays them at the character's real speed (the parameter over the clips' natural speed on this
  skeleton, within `min_rate` and `max_rate`, default 0.25 and 4); a state also has a `speed`, a `speed_parameter`, a
  loop override and a **look-at weight**. Its **timed events** (by `time` into a one-clip state, or by `phase`) fire as
  its playback passes them (also at its very start; a clip of one pose fires them all once, as the state starts), only
  while it is the layer's current state: `attach` hangs the action's target on a socket of this object (gripped by the
  target's own socket, `grip`), `detach` puts what hangs on a socket back in the world where it is, `set` changes
  custom parameters; every one is queued for the game too.
- **Transitions** go from a state, a list of states or `any`, when all their conditions hold (a parameter `above`,
  `below`, `at_least`, `at_most` or `is` a value; an `event`; a `socket` holding something or not) and their
  `exit_time` (cycles of the current state) has passed; the first that holds wins, one per layer per tick, and
  cross-fades over its `fade_seconds` (the graph's default, 0.2). A transition from `any` into the state the layer is
  in is passed over unless it `restarts` it.
- **Look-at** turns its bones (shares that add up to at most 1: the neck and the head) toward where the character
  looks, within `max_yaw`, `max_pitch_up` and `max_pitch_down`, easing at `turn_speed`, by the playing states' weights.

The reader treats the definition as hostile (downloaded content): every count has a limit (`AnimationGraph.h`), every
name a pattern and a length, every number a range, and every reference must resolve. A graph is parsed once when the
definitions are read and each load copies the block. **The player** (`AnimationGraphPlayer.h`) binds it to a skeleton
(a clip no library has plays nothing for a second, so exit times pass; a bone the skeleton lacks is left out; both
warned once per animator) and keeps the playback (up to 4 states cross-fading per layer). The code's and the row's
clips (the mixer) play **over** the graph by their weight, so a script can still play a clip on a character.

`WorldAnimation_TryGetState(object, layer, &state)` reads a layer's current state (NULL: the first layer),
`_TryGetParameter` a parameter.

## Actions

An action is an entry into a layer's states with rules: `WorldAnimation_StartAction(object, action, target)` starts it
for the player and NPCs alike (the target: none, optional or required, as the action says). It replaces an action that
approaches or plays on the same layer; one playing on another layer must end first (it is refused: that layer would
stay in the old action's states with nothing to take it out), so a seated character stands up before it waves. The action says how the character's movement is locked while it plays (`free`, `no_moving`,
`no_turning`, `locked`; WorldCharacterComponent's action control). With an **anchor** (a socket of the target, within
the action's `max_distance` of the character's feet, default 1.5 m) the character first slides there and turns to the
socket's facing (+Z) over the `approach_seconds` (default 0.5), swept by the character system; it is then held there
(not pushable) while its movement is locked. An approach that arrives enters the action's first state; one that
fails (blocked, or not there 0.5 s past its time) ends the action as failed.

An action **ends when its layer leaves its states** (the graph's own transitions take it out). A driver asks for that
with `WorldAnimation_EndAction` (it fires `end_action`; during an approach the action stops at once).
`WorldAnimation_TryGetAction` gives the action an object plays and its target. A rebind, an animator removed from its
object and the system's end each end the action and free the character.

**Events.** Starts, ends and failures are queued with the graphs' named events (`WorldAnimationEvent`: kind, object,
name, target, tick; `WorldAnimation_GetEvents`, cleared by `WorldAnimation_ClearEvents` once handled): the game reacts
to them (the test frame swings its door on a `push`, sets a released cup down on what is under it).

## Sockets and held items

A world model's **sockets** (`WorldModels.md`) are named places on a bone (a hand: an offset in metres along the bone's
axes; the frame is rigid, so a scaled import scales neither the offset nor what hangs there) or fixed in the object's
space (a bench's seat). The pose publishes each socket's frame in the object's space. An object
**hangs on a socket** of its parent (`World_SetParentSocket`, the `parent_socket` row; WorldData.md): its world matrix
is its local matrix times the socket's frame times the parent's world matrix, and each pose step marks such children
dirty, so a held cup follows the hand. A socket the parent's pose lacks is the parent's own space (an animator removed
from the holder marks its socket children dirty as it goes).
`World_TryGetSocketWorldMatrix(object, socket)` reads a socket's world matrix (in double). An `attach` effect puts the
action's target on a socket so that its grip socket lands on it (the target's own scale kept; it becomes movable); a
held pushable is carried by its holder in the collision step (not pushed out of the hand). A held item's collider
still collides, so it pushes its holder off what it touches: clips that carry things keep them clear (the placeholder
reaches pass above a table's edge and keep the cup upright).

## First person

The player's camera rides its body's **eyes socket** (`PlayerController_ApplyCamera`: the world model's first_person
block names it; without one the character's eye height), so every step, crouch and seat moves the view. The renderer's
**first-person object** (`WorldRenderer_SetFirstPersonObject`) draws, in the camera's passes (the G-buffer, the lit pass
and the unlit fallback), a copy of its pose in which the bones the first_person block hides, and their subtrees, are
collapsed onto the hidden bone's joint; its shadows keep the whole body. Looking down you see your chest, arms and
feet, and your shadow has a head.

## Drawing (the renderer's side)

The render cache gives each model entry its object's pose when the pose was made for the entry's world model
(`WorldRenderModel.Pose`; a pose for another world model, before the system rebinds, is not taken). Unless the world
model gives bounds of its own (`WorldModel_HasOwnBounds`), the entry's bounding sphere is the pose's box through the
skeleton LOD's import transform, so culling, shadows and LOD distances follow the pose.

Every geometry pass has a **skinned twin** of its shader: the same fragment stage over a vertex stage that includes
`shaders/common/skinning.glsl` (`world_pbr_skinned`, `world_normal_skinned`, `world_depth_skinned`,
`world_cube_depth_skinned`, and `world_unlit_skinned` for the unlit fallback; the crisp overlay uses the PBR pair). A
posed object's skinned meshes (`GameSkeleton_IsMeshSkinned`) draw with the twin, after its palette is uploaded (once per
pose per pass: `WorldRendererPalette`), and its other meshes with the rigid shader; every per-object uniform the pass
sets (dither, outline flag, forward lights, the PBR factors) goes to the program the mesh draws with. Without a pose, a
twin or room in its palette, skinned meshes draw in their bind pose. The palette holds 128 bones
(`SKINNING_MAX_BONES`; the renderer reads the array's size from the compiled program, so the C side never disagrees
with the shader); a skeleton with more draws its bind pose. `WorldRenderer_GetStatus` reports the posed models and the
`skinning` feature.

## Limits and costs

Bones: 256 per skeleton and per library, 128 per skinned draw, 4 per vertex. Libraries: 16 per world model and per
animator. Clips: 256 per library, 8 playing per animator. Graphs: `AnimationGraph.h`'s limits (8 layers, 64 states and
256 transitions a layer, 64 actions, 64 custom parameters). Measured on the test machine (i7-9700K, -O2): one
19-bone pose of a single clip costs about 1.75 µs (sampling 1.05, bone matrices 0.3, palette 0.13, bounds 0.27); two
clips blended about 3.3 µs. Poses are computed once per shown frame, not every 480 Hz tick: the tick only advances
times and graphs. With the crowd out and the NPCs (105 animators, 1 995 bones) the tick's update takes about 21 µs and
the pose step about 381 µs a shown frame (3.6 µs a figure: a figure under a full-weight code or row clip still samples
its graph first), about 33 ms of a core a second at 60 frames, against 87 ms posing single clips every 480 Hz tick.

## Test content

`asset_source/scripts/placeholder_humanoid.py` (Python's standard library only) writes the placeholder figure: a box
human of 19 bones (hips, spine, chest, neck, head; per side shoulder, upper and lower arm, hand, upper and lower leg,
foot; +Y up, facing +Z, standing on the origin), as `models/placeholder_humanoid/tall.glb` (1.78 m), `tall_lod1.glb`
(fewer boxes) and `short.glb` (85% of the height, 15% wider), and its clips as
`animation_libraries/placeholder_humanoid/motions.glb`: idle 3 s, walk 1 s, run 0.68 s, crouch_idle 2 s, crouch_walk
1.4 s, jump 0.6 s, fall 1 s, land 0.5 s and wave 2 s, keyed at 24 a second, in place (the legs placed by a two-bone IK).
`tests/data/placeholder_humanoid_with_clips.glb` is the tall model with its clips in one file. The library
also has the actions' clips (sit_down 1.3 s, sit_idle 4 s, stand_up 1.3 s, pick_up 1.5 s, hold_r 2 s, put_down 1.6 s,
open_door 1.4 s), made for the props and the anchors that place the character: their arms are placed by an IK and the
script checks by forward kinematics that the hand's socket meets the cup's grip and the door at the grab, the release
and the push (it prints where across, for the props' sockets). It also writes `models/placeholder_props/bench.glb` and
`cup.glb`. The definitions: models `test_humanoid`, `test_humanoid_lod1`, `test_humanoid_short`, `test_bench`,
`test_cup`; the library `test_humanoid_motions` (the gaits with their metres per cycle: walk 1.1, run 1.5789,
crouch_walk 0.7333); the graph `test_humanoid` (`animation_graphs/test_world/`: a body layer with idle, a walk-run blend
matched to the speed, the crouches, jump, fall, land, flight and the actions' states; a right-arm layer that holds what
the hand holds and waves; look-at on the neck and head; the actions `sit`, `pick_up`, `put_down`, `open_door`, `wave`);
world models `test_humanoid` (LOD 1 from 12 m) and `test_humanoid_short`, both with the graph, the sockets `hand_r`,
`hand_l` and `eyes`, and a first_person block hiding the neck and head; `test_bench` (its `seat` socket) and `test_cup`
(`grip` and `reach`); `test_door` has an `opener` socket. The test world has four figures (ids 45 to 48):
`humanoid_showcase` (idle, a 0.3 s fade), `humanoid_short_waver` (wave, looped by the placement's override),
`humanoid_walker` (walk from a quarter of the way in) and `humanoid_runner` (run at half speed); it also has a bench (49)
and a coffee cup on the desk (50), which with the door offer their actions (`tbp:action`). The test frame's keys: **5**
steps the showcase through every clip, cross-fading; **6** brings out a crowd of 100 figures walking at their own
phases in front of the camera (runtime objects, never saved) and takes it away; **7** slow motion (a quarter speed);
**I** adds the animation status. The player as a human and the NPC human wear the humanoid (WorldRuntime.md:
**E** uses what you look at, **8** sends the NPC human to the bench, **9** puts down, **0** waves).

## Tests

`AnimationLibrary` (the reader's rules and hostile files, sampling, the definition, the shipped files),
`SkeletalAnimation` (transforms, retargeting, blending, the mixer, palette and bounds against every vertex, blending over
by mask, look-at), `WorldAnimation` and `WorldAnimationFar` (the system end to end, sockets 173 km out, the pose step),
`AnimationGraph` (the reader's rules and refusals), `AnimationGraphPlayer` (binding, transitions, blends,
events, layers, look-at, and the shipped humanoid graph on the shipped library), `WorldAnimationGraph` and
`WorldAnimationGraphFar` (graphs following characters, parameters, sockets, actions with anchors, attach and detach,
first-person eyes, a removed animator), the render cache's `posed_objects_take_their_pose_and_its_bounds` and
`the_first_person_object_hides_its_bones_from_the_camera`, the frame manager's PrepareRender, the shader suite's
`shipped_skinned_stages_share_one_skinning`, the property descriptors' asset name lists and the world files' round trip
of the animator and of `parent_socket`. `tests/TestAnimation.{h,c}`
writes glTF files in memory and builds skinned models in CPU memory; `TestAssets_AddSkinnedModel` and
`TestAssets_AddAnimationLibrary` register them with an asset manager.
