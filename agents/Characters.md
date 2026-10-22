# Characters

The player and NPCs (T13): what makes an object walk, run, crouch, jump, fly, look around and use things. A character
is **the same for the player and every NPC**, whether a human, a dog or a pigeon: a component on an ordinary world
object, set up by a character asset, driven by a plain input that the player controller fills from the keyboard and an
NPC's brain (later scripts) fills the same way. A character nobody drives still stands, falls, rides platforms and is
pushed.

The owner's decisions behind it (`handoff/2026-10_graphics_and_world/DECISIONS.md`, G21 to G24): every character is an
**upright capsule** sized by its asset (a dog a short wide one, a pigeon a tiny one); characters **walk** (fall, crouch,
jump) and **fly**; a **`character` asset** holds every default of a kind of character and a placement may override
**every setting**; animated models, held items and special actions come next, in T29 and T30.

| Module | Files | What it is |
|---|---|---|
| CharacterAsset | `include/CharacterAsset.h`, `source/CharacterAsset.c`, `source/CharacterDefinition.c`, `source/CharacterInternal.h` (private) | The `character` asset type: `CharacterSettings`, their property table and defaults, the definition's parser and loader. |
| WorldCharacterComponent | `include/WorldCharacterComponent.h`, `source/WorldCharacterComponent.c` | Component type 4 (`character`), a transform driver: the asset's name, the placement's overrides, the input, the state. |
| WorldCharacters | `include/WorldCharacters.h`, `source/WorldCharacters.c`, `source/WorldCharacterMotion.c` and `source/WorldCharacterInternal.h` (private) | The character system: the tick's update, the capsule's motion, the collider, events, teleports, the look ray. |
| PlayerController | `include/PlayerController.h`, `source/PlayerController.c` | The player's side: input into a character, the camera at its eyes, what the player looks at. |

## The character asset (`CharacterAsset.h`)

A standard asset type (`character`, JSON definitions in `characters/`; the format and every key with its range is in
`references/asset_structure.md`, "Characters"). Load one with `AssetManager_LoadCharacter`; it has no dependencies.
The C type is `CharacterAsset` (not `Character`: that name would shadow the many locals called `Character` in text
code), its settings `CharacterAsset_GetSettings(asset)`.

`CharacterSettings` has one field per `CharacterSetting` (42), in groups:

| Group | Settings |
|---|---|
| Body | `Radius` 0.3 m, `Height` 1.75 m (caps included), `EyeHeight` 1.62 m, `Mass` 75 kg, `IsPushable`, `CollidesWith` (the character layer's mask) |
| Walking | `WalkSpeed` 1.4, `CanRun`, `RunSpeed` 3.5, `Acceleration` 8, `GroundFriction` 10, `GroundControl` (steer), `StepHeight` 0.35 m, `SlopeLimit` 45°, `GroundSnapDistance` 0 (it falls off every step) |
| Turning | `TurnSpeed` 360°/s, `IsTurnInstant` |
| Crouching | `CanCrouch`, `CrouchHeight` 1.15 m, `CrouchEyeHeight` 1.0 m, `CrouchSpeed` 1.0, `CrouchTransitionSeconds` 0.2 |
| Jumping | `CanJump` (false), `JumpSpeed` 3.2 m/s, `CanJumpWhileCrouched`, `CoyoteSeconds` 0.1, `JumpBufferSeconds` 0.1, `WalkJumpBoost` 0, `RunJumpBoost` 0, `CanAutoJump` (true), `AutoJumpDelaySeconds` 0.25 |
| Falling | `GravityMultiplier` 1, `AirAcceleration` 0 (no air control), `AirDrag` 0.004 per metre, `MaxFallSpeed` 55 m/s |
| Flying | `CanFly` (false), `FlySpeed` 5, `FlyFastSpeed` 10, `FlyAcceleration` 6, `FlyDeceleration` 6 |
| Interaction | `InteractionReach` 2.5 m, `InteractionLayers` (every layer but trigger) |

- **One copy of every rule.** The settings table (`CharacterSettings_GetPropertyTable`, "character") and the
  component's table are built from the same row macros (`source/CharacterInternal.h`): one key, default, rule and
  description per setting. A setting's value row has the same wire id in both (`CHARACTER_VALUE_WIRE_ID`: 3 + 2i); the
  component's override flag sits just before it (`CHARACTER_FLAG_WIRE_ID`: 2 + 2i). Upper bounds keep hostile content
  sane (`CHARACTER_MAX_SIZE` 100 m, `CHARACTER_MAX_SPEED` 1000 m/s, `CHARACTER_MAX_ACCELERATION` 10000 m/s²,
  `CHARACTER_MAX_SECONDS` 60, turn speeds up to 1000 rad/s, gravity multipliers up to 100).
- **Rules across settings** that a table cannot hold: the height and the crouch height are at least twice the radius,
  the crouch height at most the height (`CharacterSettings_CheckBody`: a definition breaking them is refused). A
  placement's overrides may break them; `CharacterSettings_GetBodyHeights` then gives the nearest heights that fit,
  and that is what the system uses.
- **Gravity is the world's**: the environment's `gravity` (`WORLD_ENVIRONMENT_DEFAULT_GRAVITY` unless the world sets
  it, [WorldData.md](WorldData.md)), times the character's `GravityMultiplier`.
- **Settings added at the end.** `CharacterSetting`'s values are the wire ids' schema: the settings T13's first tuning
  (2026-10-22) added (`GroundControl` to `AirDrag`) follow the first 36, and `GroundFriction` took `Deceleration`'s
  place and wire id (its JSON key changed: a pre-release rename, no saved data used it).
- **Test content** (`characters/test_world/`): `test_human` (the owner's tuning: 1.8 m tall, its eyes at 1.68 m, walks
  at 2 and runs at 5 m/s, jumps at 5 m/s with a 1 m/s running boost, acceleration 64, friction 16, air control 4),
  `test_dog` (0.28 by 0.7 m, 25 kg, runs at 6 m/s, lies down as its crouch: 0.56 m), `test_pigeon` (0.1 by 0.3 m,
  0.35 kg, flies, cannot crouch).

## The component (`WorldCharacterComponent.h`)

Added with `World_AddComponent(world, id, WorldComponentType_Character, &component)`. **A transform driver**: an object
carries at most one, and adding it makes the object movable.

- **Rows** (`character_component`, 85): `character` (the asset's name; none: the defaults, a human), then for each
  setting in `CharacterSetting`'s order `overrides_<key>` and `<key>`. A flag that is off keeps its value, saved.
- **Binding.** The asset is loaded under the world's asset user on attach and whenever the name changes (also through
  the table). Without an asset manager, without a name, or after a failed load (logged once) the defaults apply, so
  tests and quick scripts build characters from overrides alone.
- **Settings in effect:** `WorldCharacterComponent_ResolveSettings(component, assetSettings)` (pure: the placement's
  value wherever its flag is on, copied field by field through the shared rows' offsets) and
  `WorldCharacterComponent_GetSettings` (with the bound asset).
- **Generic setters** instead of 42 typed ones: `WorldCharacterComponent_OverrideSetting(component, setting, &value)`
  (checks the value against the row, writes it and turns its flag on), `_SetOverridden`, `_IsOverridden`. Scripts use
  the table (`Properties_Set` by key).
- **Input** (`CharacterInput`, `WorldCharacterComponent_SetInput`, kept until changed):
  - `MoveDirection`: world space, its length up to 1 scaling the speed (an analog stick, a strolling NPC); walking uses
    its horizontal part, flying all of it;
  - `IsRunning`, `WantsCrouch`, `WantsJump`, `WantsToFly`, `WantsUse`: held states. A jump and a use happen on the
    press (the flag turning on), so a driver that keeps sending the same input does not use again and again; a jump
    held down jumps again on the ground once `AutoJumpDelaySeconds` have passed since the last (`CanAutoJump`: hopping,
    as in Minecraft);
  - `LookYaw`, `LookPitch`: where it looks (yaw 0 along +Z, growing toward +X, as `GameCamera`'s; pitch within
    `CHARACTER_MAX_LOOK_PITCH`, 89°); `CharacterInput_GetLookDirection` / `_SetLookDirection` convert;
  - `Facing`: how the body turns: toward its movement (NPCs, animals), toward its look (the player), or not at all.

  `SetInput` refuses values that are not finite and a facing outside its enum, shortens a longer move direction to 1,
  wraps the yaw and clamps the pitch. Attaching a character makes it face and look the way its object already faces.
- **State** (`WorldCharacterComponent_GetState`, runtime only): `Mode` (walking, flying), `Stance` (standing,
  crouching), `IsGrounded` and `IsTouchingGround` (ground right below, also while flying: brains land birds by it),
  `GroundObjectId`, `GroundNormal`, `GroundPoint` (where it touches its ground: the plane its step height is measured
  from), `Velocity` (how fast it actually moved in its last update, relative to its ground:
  its own move after walls, plus what the collision step pushed it since; two characters pressing into each other
  read 0), `GroundVelocity` (the platform's), `FacingYaw`, `TurnRate`, `Height` (the stance's), `EyeHeight` and
  `CrouchAmount` (eased), `IsStandUpBlocked`, `TimeInAir`. `WorldCharacterComponent_GetEyePosition` is the object's
  world position plus the eye height.
- **Runtime bookkeeping** (`WorldCharacterRuntime`, private to the system): the cached settings (by component revision
  and bound asset), the velocity it drives with, the input's last held requests, a buffered jump, the time since its
  last jump, whether it is climbing a step, the ground's last world matrix, where the update left the feet.

## The system (`WorldCharacters.h`)

`WorldCharacters_Create(world, collision, logger, &characters)` (after the collision world, which it borrows),
`WorldCharacters_Update(characters, deltaSeconds)` once a tick in the **drivers slot, after the other drivers** (so a
platform moved this tick carries its riders exactly) and before `WorldCollision_Step`. For each effectively enabled
character, in the world's component order:

1. **Settings and body.** The settings in effect (cached), the stance's height, the capsule (`CharacterBody`): the move
   filter (static and kinematic colliders on its mask, colliding as the `character` layer, without triggers, itself or
   what it carries, pushables or what they carry) and the ground filter (the same with pushables). The first update
   settles it on the ground within its step height (or its step-down distance, when longer) below.
2. **Pushes read back.** Where the collision step left it versus where the last update did: part of the reported
   velocity, never of the velocity it drives with (a 200 kg character shoving a 20 kg one walks on at most of its
   speed; equal ones walking into each other stop).
3. **Presses.** A jump press buffers a jump for `JumpBufferSeconds`, and so does a jump held down on the ground once
   `AutoJumpDelaySeconds` have passed since the last jump (`CanAutoJump`; with hops longer than the delay it hops again
   as it lands); a use press is answered at the end.
4. **Mode.** Flying while the input asks and `CanFly` allows; taking off keeps the ground's velocity; landing from
   flight walks again (no late jump out of a flight).
5. **Platforms.** A grounded character whose ground object's world matrix changed since the last update is moved by
   that change (the old matrix undone, the new one applied: exact for turning platforms) and turned by its yaw, its
   facing and its look too; that change over the tick is its `GroundVelocity`.
6. **Stance.** Crouching while asked and allowed, on the ground; standing up once the input lets go (or crouching is
   no longer allowed, or it flies), only where the standing capsule fits (`CharacterMotion_DoesFit`: an overlap of the
   standing capsule a skin narrower, its bottom two skins up so the ground it rests on does not count, pushables
   included): otherwise it stays crouched with `IsStandUpBlocked`. The
   capsule changes at once, its feet in place.
7. **The collider.** The object gets a collider if it has none (`World_AddComponent`, outside any hook) and the rows
   the character owns are written when they differ: its own capsule (centre at half the stance's height, its
   radius), layer `character`, the mask, not a trigger, no local transform, rotation mode `yaw_only`, pushable or not,
   the mass, `when_blocked` `stop`, continuous. Every write advances the collider's revision (the collision world
   rebuilds it), so nothing is written while nothing changed.
8. **Velocity.** The wish: the walk, run or crouch speed times the move direction's length, along its horizontal
   direction. **On the ground** (`GroundControl`, T13's first tuning): **steer**, the default, moves the whole velocity
   toward the wished one at `Acceleration` (a turn back, a quarter turn and a wall pressed into at a slant alike: along
   the wall at the wish's share along it); **push** (Quake's way) adds along the keys' direction at `Acceleration` up to
   the wished speed along it, and the friction takes the motion across their direction or against it (a quarter turn
   slides; a turn back meets both). Either way the keys never raise the speed past the wished speed (or past what it
   had), and **`GroundFriction`**, a constant deceleration, takes what the keys do not drive: everything when they let
   go, the speed above the wish (after a sprint-jump, or a run let go to a walk). **In the air**: the keys push along
   their direction at `AirAcceleration` (0 by default: no air control, only gravity and the air move it) up to the
   wished speed along it, never raising the speed across the ground past the wish or past what it had, so they steer
   and brake but never speed it up; gravity; the **air's drag**, `AirDrag` times the speed squared along the motion
   (solved over the tick; 0.004 gives a person's 50 m/s terminal speed on Earth); the fall limit. A buffered jump starts
   on the ground or within the coyote time after walking off (not after a jump), keeping the ground's velocity, and
   adds the **jump boost** (`RunJumpBoost` running, `WalkJumpBoost` otherwise, times the keys' length) along the keys,
   never past the wished speed plus the boost or the speed it had: sprint-jumps in a row do not add up. Flying:
   accelerate toward the 3D wish (fly speed, fast when running), no gravity.
9. **Move and slide** (`CharacterMotion_MoveAndSlide`). Walking on the ground, the move follows the ground's plane
   (its horizontal part, the height the ground rises or falls by). Up to `CHARACTER_MAX_SLIDE_ITERATIONS` (4) sweeps,
   each sliding the rest along every plane hit so far (one plane, the crease of two, or nothing in a corner of three);
   the velocity loses its parts into them. **Every sweep moves the capsule a skin wider** (`CHARACTER_SKIN`, the
   collision step's 1 mm): where that touches something the body is a skin from it, so a move keeps the skin to
   everything it passes, not only to what it stops at, and the collision step (which pushes whatever comes within the
   skin) never has to push a character off a wall or a curb. A move stops `CHARACTER_REST_GAP` (1 mm) short of that
   touch, along the contact's normal, so the next sweep starts clear of it (a capsule left touching takes any tiny move
   that rounding tilts a hair into a contact as a hit, and stalls on a stair's edge); the sweep looks past the move as
   far as that back-off can reach (the rest gap over `CHARACTER_MIN_APPROACH`), so a grazing approach stops at the gap
   too. One closer than the gap already (a run along a wall drifts toward it by rounding, a hair a tick) is backed out
   to it, never closer than `CHARACTER_MIN_REST_GAP`. Within the skin at the start (a spawn, something moved into it):
   out along the push first. While walking on the ground, a contact steeper than the slope limit (a wall, a riser,
   steep ground), or an **edge higher than the step height** (below), first tries a **step up** (once a move), then
   counts as an **upright wall** (its normal made horizontal, the walker backed out of it level): steep ground and
   curbs never lift a walker.
10. **Step up.** Up by the step height (less a ceiling), forward by what the move had left but at least past the gap
    to the riser (`CHARACTER_STEP_ENGAGE_DISTANCE` further: a riser met with a hair of the tick's move left, or by a
    slow walker, is climbed too), down again: accepted when it lands supported, higher than it started, on something no
    higher than the step height above the ground it walks on. With the small moves of a 480 Hz tick it lands on the
    step's **edge**: the capsule's round bottom then rolls over it in the following ticks, a smooth climb (a 0.3 m step
    takes about 0.2 s at 1.4 m/s).
11. **Ground.** On the ground and not stepped: follow the ground down (`CharacterMotion_FollowGround`) as far as a
    slope at the slope limit drops over the distance it went across the ground since the last look (its move and the
    collision step's push; the tangent at most `CHARACTER_MAX_FOLLOW_SLOPE`), plus the ground's own move in the tick
    (the collision world holds a lift where the last step left it) and the landing probe's 4 mm, or by its step-down
    distance (`GroundSnapDistance`) when that is longer: sweep down, slide off rims (an edge higher than the step height
    is one), until something supports it. Found: there it stands, the rest gap above it. Not found: **it falls** (an
    event), keeping the ground's velocity and the **vertical speed its move had** (down a slope, a stair's roll), so a
    fall goes on smoothly; but off an edge with no walkable ground within its step height below the face it walked on
    (a ledge, not a stair) no faster than dropping from that face would have made it (its round bottom rolls over the
    rim faster than gravity at a run: a 5 m/s run off a 1 m ledge dived, landing in half a fall's time). So by default
    a walker **falls off every step**, stairs too, and only walkable slopes are followed (T13's first tuning: walking
    off a block, the round bottom rolled most of its radius down the edge on the ground and the old 0.35 m snap dropped
    it the rest in one tick). A walker whose look down an edge above its step cut short (pressing against a low curb)
    keeps the ground it rests on when a ray along the ground's normal from its bottom cap's centre still finds it within
    resting distance. In the air and not rising: landing on support within a few millimetres (an event with the impact
    speed), also on a pushable it fell into (a crate, a head: the ground sweep takes an overlap whose push-out points up
    as ground it sank into and lifts it out). Flying: `IsTouchingGround` from a 2 cm probe.
12. **Facing** turned at `TurnSpeed` (or at once) toward its velocity (if moving), its look, or not at all.
13. **Eyes** eased toward the stance's eye height: fast at first, slowing as they near it (each moment they close the
    same share of what is left), there after `CrouchTransitionSeconds` (within 1% of the way, `CHARACTER_EYE_EASE_
    REMAINDER`, they snap there); half the time leaves a tenth of the way. Eyes outside both heights (the settings
    changed: another kind of character) move into their range at once.
14. **The transform**: the feet's position and the yaw (upright, scale 1), only what changed; a child's through its
    parent's inverse. A scaled character object is warned about once (its sweeps use the character's own sizes).
15. **Use**: a press casts the look ray and queues `used` with what it hit (or 0, nothing within reach).

**Support** (what a capsule stands on): a contact whose normal is within the slope limit; and, while it **climbs a
step**, the step's **edge** under its round bottom: the contact's own normal is then steep, so a short ray down just
inside the contact (`CHARACTER_EDGE_PROBE_INSET` 1 cm in, from 2 cm above, 3 cm down) looks at the face the edge belongs
to; walkable means supported (`CharacterSupport.IsSteepEdge`). A climb is a step up that landed on such an edge, until
the walker stands on walkable ground: only a ground that is a steep edge lets the ground follow take another, so a
walker going off a ledge or down a stair falls instead of rolling down the edge. Contacts on the rim of the bottom cap
(normal within 0.6° of horizontal) never support. A walkable contact on an edge lets a character perch on a step or a
ledge with its centre up to r sin(slope limit) past the edge (21 cm for a human), its feet up to r (1 - cos(slope
limit)) below the top (8.8 cm), as capsule characters do.

**Steps** (T13's review). The round bottom would roll over any edge up to radius x (1 - cos slope_limit) high (8.8 cm
for a human), walking up it as up a slope, whatever the step height. So a walker's ground is also the plane its steps
are measured from (`GroundPoint` along `GroundNormal`, carried with a moving ground): an **edge** (a contact whose face
under it, from the edge probe, is turned from the contact by more than 2°) higher than the step height above that
plane, measured straight up, is a wall to the slide and no support to the ground follow, whatever its normal. A slope's
or a crest's edge lies on that plane and an edge a walker rolls down off is level with it, so neither counts; with a
step height of 0 a walker climbs nothing higher than its skin and still walks over crests and down curbs.

**Pushables are the collision step's.** Moves sweep past other characters and crates and what they carry (the
filter's `ArePushablesIgnored`: a crate's handle too), walking or in the air, so T27 pushes both by mass: walking or
jumping into a light crate shoves it, into an equal character stops both, into a pinned crate stops the walker. The
ground filter sees pushables (a crate to stand on): a sweep that starts inside one from above (a falling character's
move took it into a crate) is ground it sank into, and from the side (a character it walks into) it looks again
without them, so a side overlap the step will mend is neither ground nor a reason to move.

**Events** (`WorldCharacters_GetEvents`, until `_ClearEvents`; kinds saved by value): `Jumped`, `Landed` (what it landed
on, the impact speed), `LeftGround`, `Crouched`, `StoodUp`, `TookOff`, `StoppedFlying`, `Used` (what it used, its
distance). Each names the character and the update's tick.

**Teleport** (`WorldCharacters_Teleport(characters, id, feet, yaw)`): places a character at rest, facing and looking
along the yaw, settles it on the ground within its step height below (a spawn point set a little high stands on the
floor at once; further up it falls), and tells the collision world (`WorldCollision_Teleport`), so the jump is not
swept.

**The look ray** (`WorldCharacters_FindLookTarget(characters, id, &hit)`): from the eyes along the input's look, out to
the reach, on its interaction layers, never triggers, never itself or what it carries. Any character may ask; the
player controller does every tick.

## The player controller (`PlayerController.h`)

`PlayerController_SetCharacter(controller, world, id, lookYaw, lookPitch)` takes control (the look starts where the
camera looked), `PlayerController_Update(controller, input, world, isMouseLookActive)` reads the tick's `GameInput` into
the character's input: `move_*` relative to the look's yaw (along the full look direction while flying, plus
`move_up` / `move_down`), `sprint` runs, `jump`, `crouch` (held, or toggled with `IsCrouchToggled`), `use` (its press is
consumed, so nothing below acts on it), the mouse turns the look (`MouseSensitivity`), the body faces the look. The look
is read back from the character every tick, so a turning platform turns the view. `PlayerController_UpdateLookTarget`
(after the collision step) says when what the player looks at changed; `PlayerController_ApplyCamera` puts the camera at
the eyes, with an optional head bob (`IsHeadBobEnabled`, `HeadBobAmplitude` 3 cm, `HeadBobStepLength` 0.7 m: a bob a
step on the ground, fading in and out; a placeholder with a `// TODO:` to rework it, the owner finds it reads badly:
tie it to the footfalls once characters are animated). Input only through `GameInput`, only in `Update` (AGENTS.md
rule 9).

New actions (`GameInput.h`): `jump` (Space), `crouch` (Left Control), `use` (E); `jump` and `crouch` share their keys
with `move_up` and `move_down`, which the flycam and flight read.

## Tick order

From [WorldData.md](WorldData.md): input; scripts (an NPC's brain sets its character's input; the test frame's
`WorldTestCharacters_UpdateBrains`); the drivers, **characters last** (`WorldCharacters_Update`); the collision step;
the queued events (collision, then characters'); animation; `World_UpdateTransforms`; the camera (`PlayerController_
ApplyCamera`) and the renderer.

## Tolerances

| Constant | Value | |
|---|---|---|
| `CHARACTER_SKIN` | 1 mm (`WORLD_COLLISION_SKIN`) | How much wider every sweep's capsule is: the gap kept to everything a move passes. |
| `CHARACTER_REST_GAP` | 1 mm | How far beyond the skin a move stops: a character rests 2 mm from the ground and walls. |
| `CHARACTER_MIN_REST_GAP` | 0.5 mm | Closer than this (a grazing slide's drift), a character is backed out to it. |
| `CHARACTER_MIN_APPROACH` | 0.1 | A grazing hit backs off at most the rest gap over this along the move. |
| `CHARACTER_MAX_SLIDE_ITERATIONS` | 4 | Planes one move slides along. |
| `CHARACTER_EDGE_MIN_NORMAL_Y` | 0.01 | The steepest edge contact under the bottom cap that may support. |
| `CHARACTER_EDGE_PROBE_INSET` / `_LIFT` / `_DEPTH` | 1 / 2 / 3 cm | The edge probe's ray. |
| `CHARACTER_SAME_FACE_COSINE` | cos 2° | A contact turned from the face under it by more is an edge. |
| `CHARACTER_MIN_STEP_CLIMB` / `_RISE` | 1 / 0.1 mm | A step up must climb and land at least this much higher. |
| `CHARACTER_STEP_ENGAGE_DISTANCE` | 0.1 mm | How far past the riser's gap a step up always reaches forward. |
| `CHARACTER_LANDING_PROBE_DISTANCE` | 4 mm | How far down a falling character looks for ground to land on, and the shortest look for the ground a walker stands on. |
| `CHARACTER_TOUCH_PROBE_DISTANCE` | 2 cm | How close below ground counts as touching (flying). |
| `CHARACTER_MAX_FOLLOW_SLOPE` | 4 (76°) | The steepest drop per metre moved the ground follow reaches down for. |
| `CHARACTER_EYE_EASE_REMAINDER` | 1% | The share of the eyes' way left after the crouch time; within it they snap there. |
| `CHARACTER_MAX_DELTA_SECONDS` | 0.1 s | The longest tick stepped. |

## Cost

An idle character costs one capsule sweep a tick (its ground) and the collider comparison; a walking one two or three
(the move, the ground), more on stairs (the step up's three) and when sliding along walls. Leaving the ground off an
edge adds two rays (the face under the edge, ground within the step height below). The update writes the
object and the collider only when they change, so a character standing still never wakes the collision world.

## Debugging and the test content

The test frame ([WorldRuntime.md](WorldRuntime.md), "WorldTestFrame"): F1 walk mode (the player, a runtime object,
below the flycam), F2 the test NPCs (a human, a dog and a pigeon with placeholder bodies and scripted brains), F3
flight, F4 the player's kind, F12 head bob; the log names what the player looks at when it changes and every character
event; I adds the player's state.

## Known limits

- **Moving platforms are seen a tick late by the sweeps.** The collision world syncs in the collision step, so during
  the characters' update a platform an earlier driver moved this tick is found where the last step left it (a sync in
  between would also advance the tick every collision event is stamped with; WorldCollision.md). The carry uses the
  live matrix, but the ground follow snaps a rider to the platform's old place: on a lift moving vertically the rider
  rests off its rest gap by the lift's move in a tick (4 mm at 2 m/s) and the collision step lifts it back; the
  `Velocity` reported in the tick a rider jumps off a rising lift includes that lift (its real velocity is right). The
  ground follow's reach adds the ground's move in the tick, so riders keep a fast lift (without it, a lift rising
  faster than about 1.4 m/s lost them every tick once the reach no longer had the old 35 cm snap).
- **Pushing (`ground_control` push) against a wall at a slant** slides along it at the wished speed, or sticks when
  pushing nearly straight in: friction takes the motion the wall turns aside as undriven (`// TODO:` in
  `PushOnGround`: telling the wall's part apart needs the walls it touches). Steering, the default, slides at the
  wish's share along the wall.
- **Falling off every step** makes stairs a run of short falls: each step queues `LeftGround` and `Landed`. T30's
  animations should treat a short time in the air as a step, not a fall (a step-down distance keeps a kind on its
  stairs instead).
- **The narrowest gap a character passes is its width plus 3 mm** (the skin and at least half the rest gap each side;
  it was 2 mm before the skin was kept to everything).
- **A walker cannot step onto a pushable** (a 10 cm pallet, however heavy): the step up sweeps past pushables like every
  move. A jump gets it on top.
- **A kinematic character (`IsPushable` off) blocked by a pinned crate has its turn undone** by the collision step's
  set-back, which reverts its whole local transform (T27); its facing state still turns. Pushable characters, the
  default, are not affected.
- **Two kinematic characters meeting fast overlap briefly** (up to 2 cm at 10 m/s, never through each other): the step
  never pushes kinematic against kinematic, and each one's sweep sees the other where the last step left it.
- **A radius grown wider than the corridor it stands in** (a kind change) leaves the character crushed and airborne
  until it gets out.
- The capsule's round bottom perches on edges: a character may stand with its centre up to r sin(slope limit) past a
  ledge (21 cm for a human), its feet (the object's position) up to 8.8 cm below the edge's top. Foot placement (IK)
  is T30's question.
- The platform carry is applied without a sweep: a platform carrying a character into a wall leaves the overlap to the
  collision step (a pushable character is pushed out of the wall, off the platform).
- Velocity on landing is not made relative to a moving platform it lands on (it slows to the wish within a tenth of a
  second).
- A character under a moving parent reads the parent's move as a push (characters are normally roots).
- A character's visual model is not its own yet: today another component (a world model) or child objects; T29 and
  T30 give characters animated models, held items on bone sockets and special actions.
