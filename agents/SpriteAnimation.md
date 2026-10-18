# SpriteAnimation module

Files: `include/SpriteAnimation.h`, `source/SpriteAnimation.c`. The asset loader that builds animations from
definitions is `source/SpriteAnimationDefinition.c` (JSON format: `references/asset_structure.md`, "Sprite
animations").

## Purpose

2D frame animations, split in two:
- **`SpriteAnimation`**: the shared, immutable data. An ordered list of frames plus default playback parameters.
  A loaded animation is an asset (`AssetManager_LoadSpriteAnimation`), shared by every holder.
- **`SpriteAnimationInstance`**: one consumer's playback state. Current frame, timing, fps, frame step,
  running/looping flags, and an event raised when playback ends or loops. Each sprite object or text sprite has
  its own instance, advanced with `SpriteAnimationInstance_Update`.

## Types

- `SpriteAnimationFrame`: `_texture` (a `Texture2D` handle copied by value; never owned by the animation),
  `_areaInTexture` (the frame's rectangle in texture **pixels**), `_isTextureStandalone` (true: a texture of its own;
  false: a sprite sheet's atlas).
- `SpriteAnimation`: `_frames` (borrowed `GenericBuffer` of `SpriteAnimationFrame`), `_defaultFPS`,
  `_defaultFrameStep`, `_defaultIsRunning`, `_defaultIsLooped`.
- `SpriteAnimationInstance`: `_source` (borrowed `SpriteAnimation*`, NULL = no animation), `_frameIndex`,
  `_timeSinceFrameChangeSeconds`, `_fps`, `_frameStep` (its sign is the direction), `_isRunning`, `_isLooped`, and
  `_stateReachEvent` (an embedded `WREvent`).
- Event payload `SpriteAnimationStateReachEventArgs*`: `_animationInstance`, `_reachedState`
  (`SpriteAnimationReachedState_End` or `_Loop`), and `_args._endArgs._direction` / `_args._loopArgs._direction`
  (`SpriteAnimationStateDirection_Forwards` or `_Backwards`).

Every field is read-only to other modules: use the inline getters (`SpriteAnimationInstance_GetFPS`,
`GetFrameIndex`, `GetFrameStep`, `GetSecondsSinceFrameChange`, `GetIsRunning`, `GetIsLooped`, `GetSource`, `HasSource`,
`GetStateReachEvent`) and the validating setters.

## Ownership and lifetime

- A `SpriteAnimation` owns nothing: its frame buffer and textures belong to whoever built it, and
  `SpriteAnimation_Deconstruct` only zeroes the struct. For a loaded animation the asset owns them (see below).
- An instance **borrows** its `_source`. It must not outlive the asset user that loaded the animation (the project
  rule in [AssetManager.md](AssetManager.md)): bind an animation only into an instance whose owner holds that
  animation under its own user, and clear or deconstruct the instance before that user releases it.
  `SpriteAnimationInstance_Deconstruct` never touches the source, so nothing detects a stale one.
- World sprites follow this: a `World` built with `World_Construct2` binds each sprite's animation under its own
  user and destroys its objects before retiring that user (see [WorldData.md](WorldData.md)).
- **An instance must not move.** Once constructed, its embedded `WREvent` points into the instance itself. Never copy
  an instance by value, `Memory_Copy` it, or keep instances in a buffer that can reallocate. Keep them at a stable
  address (a member of a heap object, an `ObjectPool` slot) and copy state with
  `SpriteAnimationInstance_CopyAnimationStateTo` (playback state only) or `SpriteAnimationInstance_CopyEntireStateTo`
  (also the source). Neither touches the destination's event subscribers.
- Frames returned by `SpriteAnimation_GetFrameAt`, `SpriteAnimationInstance_GetFrameAt` and
  `SpriteAnimationInstance_GetCurrentFrame` are copies carrying the asset's texture handle: use them for the current
  draw, don't store them.
- Treat a constructed animation's frame buffer as immutable: instances keep indices into it.

## Creating and binding instances

- `SpriteAnimationInstance_Construct1(&Instance)`: an empty instance (no source; `Update` does nothing).
- `SpriteAnimation_CreateInstance(Animation, &Instance)`: constructs the instance **from scratch**, sets its source
  and applies the animation's defaults (`SpriteAnimationInstance_ResetProperties`).
- Never call either constructor on a live instance: both re-zero it and discard its event and subscribers without
  deconstructing them. Release every instance with `SpriteAnimationInstance_Deconstruct`.
- Rebinding by deconstruct + create (what `WorldSpriteObject_SetAnimation` does) resets playback and drops the
  event's subscribers. To switch animations and keep the subscribers, copy from a fresh instance:

```c
SpriteAnimationInstance Fresh;
Error Result = SpriteAnimation_CreateInstance(NextAnimation, &Fresh);
if (Result.Code != ErrorCode_Success)
{
    return Result;
}

// Copies the source and the default playback state; LiveInstance keeps its event subscribers.
Result = SpriteAnimationInstance_CopyEntireStateTo(&Fresh, LiveInstance);
Error DeconstructResult = SpriteAnimationInstance_Deconstruct(&Fresh);
Error_Deconstruct(&DeconstructResult);
return Result;
```

## Playback

`SpriteAnimationInstance_Update(Instance, elapsedSeconds)`:
- `elapsedSeconds` must be finite and >= 0 (`ErrorCode_ArgumentOutOfRange` otherwise).
- Does nothing, and accumulates no time, without a source, with no frames, when not running, at fps 0 or at frame
  step 0.
- Otherwise it adds the time to the accumulated time, applies one frame change (moving `frameStep` frames) per whole
  frame duration (1 / fps), and keeps the remainder.
- **Not looped:** when a change would step past the last frame (forwards) or frame 0 (backwards), the instance stops:
  the index becomes the last frame or 0 (whatever the step size), the accumulated time 0 and `IsRunning` false, then
  the event fires with `SpriteAnimationReachedState_End`. With a step of 1, the last frame stays up for one full frame
  duration before the end fires.
- **Looped:** the index wraps modulo the frame count. If the advance crossed an end, the event fires once with
  `SpriteAnimationReachedState_Loop` (once per call, however many times it wrapped).
- The event fires after the new state is applied, so handlers see the final state and may change the instance. A
  handler's error is returned by `Update`.
- `SpriteAnimationInstance_ResetAnimation`: accumulated time 0; index 0, or the last frame when the step is negative.
- `SpriteAnimationInstance_ResetProperties`: the source's default fps, step, running and looping, then
  `ResetAnimation`. `ErrorCode_InvalidOperation` without a source.

## Validation

| Value | Rule | Checked by |
|---|---|---|
| fps | finite, within [`SPRITE_ANIMATION_FPS_MIN`, `SPRITE_ANIMATION_FPS_MAX`] = [0, 1e30] | `SpriteAnimationInstance_SetFPS`, `SpriteAnimation_Construct2` |
| frame step | within [`SPRITE_ANIMATION_FRAME_STEP_MIN`, `SPRITE_ANIMATION_FRAME_STEP_MAX`] = [-1e9, 1e9] | `SpriteAnimationInstance_SetFrameStep`, `SpriteAnimation_Construct2` |
| seconds | finite, >= 0 | `SpriteAnimationInstance_SetSecondsSinceFrameChange`, `SpriteAnimationInstance_Update` |
| frame index | below the frame count, or 0 when there are no frames | `SpriteAnimationInstance_SetFrameIndex` |
| frame buffer | not NULL, element size `sizeof(SpriteAnimationFrame)` | `SpriteAnimation_Construct1`, `SpriteAnimation_Construct2` |

NaN and infinity are rejected wherever a double is set. A setter given a bad value returns
`ErrorCode_ArgumentOutOfRange` (`ErrorCode_IndexOutOfBounds` for a frame index past the end) and changes nothing.
`SpriteAnimation_Construct1` uses the `SPRITE_ANIMATION_*_DEFAULT` values (fps 0, step 0, not running, not looped);
`SpriteAnimation_Construct2` takes explicit, validated defaults.

## Drawing a frame

Read the frame with `SpriteAnimationInstance_GetCurrentFrame`. `RenderContext_RenderTexture2D` takes a normalized
source rectangle, so divide `_areaInTexture` by the texture's width and height (`TextLayout.c` does this).

## Loaded animations (SpriteAnimationDefinition.c)

- Definition keys: `frames` (array; may be empty), `fps` and `frame_step` (integers, default 0), `is_running` and
  `is_looped` (booleans, default false).
- A `"source": "texture"` frame needs a file location (a reference is rejected when the definition is read). It is
  loaded with `LoadTexture` (texture extensions, see [AssetManager.md](AssetManager.md)) with
  `texture_properties.filtering` applied (default bilinear). Its area is the whole texture, and the loaded asset owns
  the texture.
- A `"source": "spritesheet"` frame uses its location's value as the **sprite sheet asset name**, and `"name"` picks
  the image in that sheet. The sheet (the type named `sprite_sheet`) is loaded as a dependency under the animation's
  dependency user, so it stays loaded exactly as long as the animation. The frame copies the sheet's atlas `Texture2D`
  handle and the image's area ([SpriteSheet.md](SpriteSheet.md)).
- fps and frame step are checked by `SpriteAnimation_Construct2` at load time: a negative fps or an out-of-range step
  fails the load, not the definition read.
- The loaded asset owns the frame buffer (borrowed from the manager's pool) and the standalone textures. Unloading
  unloads those textures and returns the buffer; the manager then releases the sheets. Every failure path unloads the
  textures loaded so far.

## Current users

- `WorldSpriteObject` embeds an instance (`WorldSpriteObject_GetAnimationInstance`). `WorldTestFrame` advances every
  sprite each update tick (`AdvanceSpriteAnimations`); the WorldRenderer binds the animations (the known violation
  above) and draws each sprite's current frame.
- Text `SpriteComponent`s get their instances from `TextComponentResolver`'s `ObjectPool`.
