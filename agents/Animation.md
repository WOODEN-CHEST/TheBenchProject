# Animation

Skeletal animation (T29): 3D models that bend with a skeleton and play clips, blended and cross-faded, with **a pose
of their own per placed object**, so a hundred NPCs on one model each walk at their own phase. The renderer draws the
poses in every pass, and a bone's world matrix can be read (a socket: T30 hangs held items on it).

The owner's decisions behind it (`handoff/2026-10_graphics_and_world/DECISIONS.md`, G27 to G30): raylib is built with
**GPU skinning** (`BUILDING.md`); clips are read by **our own glTF reader** (cgltf); clips live in a **separate asset
type, `animation_library`**, so one set of clips plays on every model of a skeleton layout (humans of every size);
playback is an **animator component**, apart from the model component. T30 builds the character state graph, sockets
with held items and special actions on top of it.

| Module | Files | What it is |
|---|---|---|
| AnimationLibrary | `include/AnimationLibrary.h`, `source/AnimationLibrary.c` (sampling), `source/AnimationLibraryGltf.c` (the reader), `source/AnimationLibraryDefinition.c` (the asset type) | The `animation_library` asset: a file's bones (names, parents, rest poses) and its clips (tracks of keys), read from glTF treated as hostile; sampling a track at a time. |
| GameSkeleton | `include/GameSkeleton.h`, `source/GameSkeleton.c` | A skinned model's skeleton, built once when the model loads (`GameModel_GetSkeleton`): bones in evaluation order, bind and inverse bind matrices, rest poses, which meshes the GPU skins, each bone's vertex box. |
| SkeletalAnimation | `include/SkeletalAnimation.h`, `source/SkeletalAnimation.c` | The runtime maths: a library bound to a skeleton (retargeting), clips sampled onto it, poses blended, the mixer (clips with times, weights and fades), bone matrices, the skinning palette, the pose's bounds; transforms composed and taken apart. |
| WorldAnimatorComponent | `include/WorldAnimatorComponent.h`, `source/WorldAnimatorComponent.c` | Component type 5 (`animator`): the clip row and how it plays (phase, speed, pause, fade, loop override, the placement's own libraries), and the pose the system publishes. |
| WorldAnimation | `include/WorldAnimation.h`, `source/WorldAnimation.c`, `source/WorldAnimationInternal.h` (private: the per-animator runtime) | The animation system: once a tick it binds, applies the rows, advances and poses every animator; code plays clips through it; sockets. |
| Skinned passes | `shaders/common/skinning.glsl`, `shaders/*/skinned.vert` and their definitions; `source/WorldRendererGeometry.c` and the pass files | The renderer's side: every geometry pass has a skinned twin of its shader and uploads each posed object's palette (WorldRuntime.md, "Renderer structure"). |
| WorldTestAnimation (test content) | `source/WorldTestAnimation.{h,c}` | The test frame's animation system and its debug keys (5, 6, 7). |

## Assets

**Skinned models are ordinary `model` assets** whose glTF file has a skin: raylib loads the skeleton and the bone
indices and weights (at most four per vertex), and the model's definition builds its `GameSkeleton` at load
(`source/ModelDefinition.c`; a skeleton that fails its checks fails the model). raylib's own animation functions are
**not used**: they pose the shared model in place (every placement would show one pose and drawing would write the
asset), and its glTF clip import resamples to 60 frames a second with a wrong last frame.

**Animation libraries** (`animation_library`, JSON in `animation_libraries/`, the file `.glb` or `.gltf`; every key in
`references/asset_structure.md`, "Animation libraries"): a file of clips, with or without a mesh (a Blender export of
the actions alone is a library: its armature object becomes a bone the skeleton simply lacks), and per clip its name,
the clip in the file (`source`, default the name), whether it loops (default no) and its speed (default 1). A clip the
definition does not name keeps the file's name when that is a valid clip name (`[a-z][a-z0-9_.-]*`), does not loop and
plays at speed 1. **The reader treats the file as hostile.** cgltf's validation does its sums without overflow checks
and its loader allocates each buffer's declared length, so before cgltf loads or validates anything the reader checks
the structure itself, in overflow-safe sums (`CheckBuffers`, `CheckBufferViews`, `CheckAccessors` in
`source/AnimationLibraryGltf.c`): buffers of 1 byte or more, at most 256 MiB together, a data URI no longer than its
base64; views inside their buffers with glTF's strides (none, or 4 to 252 bytes in fours) and no meshopt compression;
accessors with a type and an element, inside their views, aligned to their components, a sparse one's replacements
(1 to its count) inside theirs. A buffer file's URI is checked as cgltf opens it, with its percent escapes decoded
(`%2e%2e` is `..`): beside the file, no absolute path, `..`, backslash, drive or NUL; the file must hold the buffer's
declared length (only that is kept), and loaded buffers must start aligned (a `.glb`'s chunks padded to four). Then
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
moves one clip alone (blends, T30's blend spaces). Looping clips wrap; others stop at their end and hold their last
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

The next free wire id is 11. The pose the system publishes (`WorldAnimatorComponent_GetPose`: the world model it was
made for, the skeleton, the palette, the bone matrices, the bounds and a revision) is valid until the system's next
update; NULL while the animator has none.

## The animation system (`WorldAnimation.h`)

`WorldAnimation_Create(world, assets, logger)` after the world; `WorldAnimation_Destroy` before it (it releases every
animator's state and pose, and its asset user). `WorldAnimation_Update(seconds)` runs in **the tick's animation step**
(WorldData.md, "The tick order": after the collision step and its events, before `World_UpdateTransforms`), for each
animator of an effectively enabled object:

1. **binds** when needed: to the world model its object's model component names (loaded under the system's own asset
   user; again whenever the name changes), its skeleton and its libraries (the world model's, or the placement's own),
   each bound to the skeleton by bone name. A bind starts from the rest pose and applies the rows at once: the clip row
   plays at full weight from its phase;
2. **applies the rows** when they changed since: a new clip cross-fades in over `fade_seconds` (a clip already playing
   keeps its time), or out when `plays_clip` goes off; a loop override reaches the playing clip;
3. **advances** its clips by the tick's seconds times their speeds and the animator's (nothing while paused);
4. **poses** the skeleton: the local pose, the bone matrices, the palette and the bounds. A resting animator (its rest
   pose, nothing playing) is not posed again.

A disabled object's animator keeps its pose. Every problem (a missing world model, one without a skeleton, a library
that does not load or matches no bone, an unknown clip) is logged once per animator, naming the object, never per
tick. `WorldAnimation_GetStatus` counts the animators, those posed, the clips playing, the bones posed and the
update's time.

**Code plays clips** through the system (the test frame's 5 key, T30's state graph, scripts later):
`WorldAnimation_PlayClip(object, clip, fade, isRestarted)` (a cross-fade), `_SetClipWeight` (one clip alone) and
`_StopClips` (back to the rest pose). They bind the animator first if needed, so a clip played right after the object
was made is not undone by its rows. A clip played from code fades the row's clip out like any other; the rows apply
again only when they change.

**Sockets.** `WorldAnimation_TryFindBone(object, name)` gives a bone's index; `_TryGetBoneWorldMatrix(object, bone)` its
world matrix in the last pose, as a `Matrix4Double`: the object's world matrix times the skeleton LOD's import transform
times the bone's matrix, so a held item does not wobble far from the world's origin (the far test suite checks it 173 km
out).

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
animator. Clips: 256 per library, 8 playing per animator. Measured on the test machine (i7-9700K, -O2): one 19-bone
pose of a single clip costs about 1.75 µs (sampling 1.05, bone matrices 0.3, palette 0.13, bounds 0.27); two clips
blended about 3.3 µs. The test world with the crowd out (104 animators) updates in about 182 µs. The game ticks at
480 Hz (`main.c`), so that is about 9% of a core: every pose is computed each tick, though a frame shows one in four to
eight. Posing once per rendered frame would need a step the
frame manager runs just before a render (an update-side hook, since rendering never writes): an open question for the
owner (STATUS.md).

## Test content

`asset_source/scripts/placeholder_humanoid.py` (Python's standard library only) writes the placeholder figure: a box
human of 19 bones (hips, spine, chest, neck, head; per side shoulder, upper and lower arm, hand, upper and lower leg,
foot; +Y up, facing +Z, standing on the origin), as `models/placeholder_humanoid/tall.glb` (1.78 m), `tall_lod1.glb`
(fewer boxes) and `short.glb` (85% of the height, 15% wider), and its clips as
`animation_libraries/placeholder_humanoid/motions.glb`: idle 3 s, walk 1 s, run 0.68 s, crouch_idle 2 s, crouch_walk
1.4 s, jump 0.6 s, fall 1 s, land 0.5 s and wave 2 s, keyed at 24 a second, in place (the legs placed by a two-bone IK).
`tests/data/placeholder_humanoid_with_clips.glb` is the tall model with its clips in one file. The definitions:
models `test_humanoid`, `test_humanoid_lod1`, `test_humanoid_short`; the library `test_humanoid_motions` (idle, walk,
run, the crouches and fall loop); world models `test_humanoid` (LOD 1 from 12 m) and `test_humanoid_short`. The test
world has four figures (ids 45 to 48): `humanoid_showcase` (idle, a 0.3 s fade), `humanoid_short_waver` (wave, looped by
the placement's override), `humanoid_walker` (walk from a quarter of the way in) and `humanoid_runner` (run at half
speed). The test frame's keys: **5** steps the showcase through every clip, cross-fading; **6** brings out a crowd of
100 figures walking at their own phases in front of the camera (runtime objects, never saved) and takes it away;
**7** slow motion (a quarter speed); **I** adds the animation status.

## Tests

`AnimationLibrary` (the reader's rules and hostile files, sampling, the definition, the shipped files),
`SkeletalAnimation` (transforms, retargeting, blending, the mixer, palette and bounds against every vertex),
`WorldAnimation` and `WorldAnimationFar` (the system end to end, sockets 173 km out), the render cache's
`posed_objects_take_their_pose_and_its_bounds`, the shader suite's `shipped_skinned_stages_share_one_skinning`, the
property descriptors' asset name lists and the world files' round trip of the animator. `tests/TestAnimation.{h,c}`
writes glTF files in memory and builds skinned models in CPU memory; `TestAssets_AddSkinnedModel` and
`TestAssets_AddAnimationLibrary` register them with an asset manager.
