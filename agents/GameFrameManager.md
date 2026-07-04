# GameFrameManager module

Files: `include/GameFrameManager.h`, `source/GameFrameManager.c`.

## Purpose

The core of the game-frame system. It owns a z-stack of [GameFrame](GameFrame.md)s, drives each through
its full lifecycle (load → start → update → render → end → unload → destroy), and composites all rendered
frames onto the screen. The caller owns the main loop and timing; the manager owns the per-frame logic.

## Ownership

A frame handed to `GameFrameManager_AddFrame` is **owned** by the manager. Once it finishes unloading (or
when the manager is deconstructed) the manager calls the frame's `Destroy`. **Do not free an added frame
yourself.** On a failed add, the caller keeps ownership.

## Lifecycle driving

`GameFrameManager_Update` performs **one** lifecycle action per frame per tick: a load step, a start, an
update (active frames whose `IsUpdated` is set), an unload step, or destruction once unloaded. It raises
each frame's `OnLoad` / `OnUnload` at the load/unload transitions. State progresses:

```
Loading → Loaded → Active → Unloading → Unloaded
```

- `AddFrame` begins loading immediately (calls `BeginLoad`). With `StartAutomatically`, the frame is
  `Start`ed as soon as it finishes loading; otherwise it waits in `Loaded` until
  `GameFrameManager_ActivateFrame` (preload-then-activate).
- `RemoveFrame` never frees immediately — it flags the frame; the next update ends it (if active), begins
  its unload, and drives `UnloadStep` to completion, then destroys it. Safe to call from within a frame's
  `Update`.
- `GameFrameManager_TryGetFrameState` reads a frame's current state (e.g. "is the next scene loaded yet?").

## Multiple active frames & transitions

Frames live in an ordered **z-stack** (add to top or bottom via `GameFrameAddOptions`). Several can be
active at once:

- **Pause menu:** add the menu on top; set the game frame's `IsUpdated = false` (frozen) but leave
  `IsRendered = true` (visible behind). The game never leaves the manager.
- **Loading screen:** the loading screen is its own active frame; the next scene loads (Loading state)
  while it renders.
- **Cross-fade:** keep both scenes active and animate the outgoing frame's `CompositeColor.Opacity` down
  (and/or the incoming one's up); remove the outgoing frame when the fade completes.

## Rendering / compositing

`GameFrameManager_Render`:

1. If the window size changed, recreates render targets and notifies frames via `OnResize`.
2. **Pass 1** — each active, `IsRendered` frame draws into **its own** `RenderTexture2D`.
3. **Pass 2** — composites the targets onto the backbuffer bottom-to-top, tinted by each frame's
   `CompositeColor` (a straight 1:1 blit with the vertical flip render textures need — done via Raylib
   directly, not the aspect-fitting renderer path).

Targets are created lazily per rendered frame and sized to the window. With zero frames, Render still
clears the screen to black.

## Crash-on-error

If any frame method fails, the manager logs it **CRITICAL** (with the frame's `DebugName`), sets its stop
flag (`GameFrameManager_ShouldStop`), and returns the error. The main loop turns that into a graceful
shutdown.

## Main-loop integration

The caller owns the loop and timing (a single-threaded fixed-timestep accumulator — Raylib rendering and
the AssetManager are both main-thread-only). Update runs at a fixed rate, render at the frame rate:

```c
GameFrameManager Manager;
GameFrameManager_Construct(&Manager, &Log, 16.0f / 9.0f, RenderTargetPosition_Centered());
// ... add initial frames ...

const double UpdateDelta = 1.0 / 480.0;                 // UPDATE_RATE_HZ is a code constant
double PreviousTime = GetTime(), Accumulator = 0.0, TotalUpdateTime = 0.0;

while (!WindowShouldClose() && !GameFrameManager_ShouldStop(&Manager))
{
    double Now = GetTime();
    double FrameTime = Now - PreviousTime;
    PreviousTime = Now;
    if (FrameTime > 0.25) { FrameTime = 0.25; }         // clamp: anti spiral-of-death
    Accumulator += FrameTime;

    while (Accumulator >= UpdateDelta)
    {
        TotalUpdateTime += UpdateDelta;
        Error e = GameFrameManager_Update(&Manager, ProgramTime_Create(TotalUpdateTime, UpdateDelta));
        if (e.Code != ErrorCode_Success) { Error_Deconstruct(&e); /* stop */ break; }
        Accumulator -= UpdateDelta;
    }

    Error r = GameFrameManager_Render(&Manager, ProgramTime_Create(Now, FrameTime));
    if (r.Code != ErrorCode_Success) { Error_Deconstruct(&r); /* stop */ break; }
}

GameFrameManager_Deconstruct(&Manager);   // BEFORE CloseWindow — it frees GPU render targets
CloseWindow();
```

(See `source/main.c` for the shipped version.)

## Notes

- All manager calls must be on the **main thread** (they drive Raylib and, via frames, the AssetManager).
- `GameFrameManager` is a value type the caller holds (like `Logger`/`GameConfig`); it stores a borrowed
  `Logger*` that must outlive it.
- `GameFrameManager_Deconstruct` must run while the window/GL context still exists (it unloads render
  targets); it ends still-active frames and destroys all of them best-effort.
- The render composite order is the z-stack order; update order is unspecified (input routing is not
  modelled yet — a future addition would poll input on the main thread and expose it to `Update`).
