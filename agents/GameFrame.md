# GameFrame module

Files: `include/GameFrame.h`, `source/GameFrame.c`.

## Purpose

`GameFrame` is the **abstract base** for a "game frame" — one self-contained section of the game (an
in-game level, the editor, the main menu, a loading screen, …). A concrete frame supplies a vtable of
behavior; the [GameFrameManager](GameFrameManager.md) owns frames and drives them through their
lifecycle. Every method that can fail returns an `Error` so failures propagate like exceptions.

## The two lifecycles

The manager runs each frame through two parallel lifecycles:

- **Resource lifecycle (stepped / cooperative):** `BeginLoad` → `LoadStep`* (until `IsLoaded`) →
  `BeginUnload` → `UnloadStep`* (until `IsUnloaded`). Each step does a bounded chunk of work and returns,
  so the loop keeps rendering (e.g. a loading screen) between steps. This mirrors the AssetManager's
  stepped `AssetBulkOperation`.
- **Logic lifecycle:** `Start` (once, after loading) → `Update`* (while active) → `End` (once, before
  unloading).

Full order applied by the manager:

```
BeginLoad → [LoadStep … until IsLoaded] → (OnLoad) → Start → [Update / Render …]
    → End → BeginUnload → [UnloadStep … until IsUnloaded] → (OnUnload) → Destroy
```

`Start`/`End` are logic; `Load`/`Unload` are resources. Keeping them separate lets a frame be
**loaded-but-not-started** (preload the next scene, then activate it instantly for a smooth transition).

## Base struct fields (shared, manager-visible)

- `IsUpdated` (default `true`) — gate for `Update`. Set `false` to freeze a frame (e.g. the game under a
  pause menu) without removing it.
- `IsRendered` (default `true`) — gate for `Render`/compositing. Set `false` to hide a frame while
  keeping it resident.
- `CompositeColor` (default opaque white) — the tint the manager composites this frame's target with.
  Animate its `Opacity` to fade the **whole frame** in/out for transitions.
- `OnLoad` / `OnUnload` — `WREvent`s the manager raises when loading/unloading finish; the event payload
  is the `GameFrame*`.
- `DebugName` — borrowed UTF-8 name used in crash/log messages; may be `NULL`.

The lifecycle **state** (Loading/Loaded/Active/…) is tracked by the manager, not stored here.

## Optional vtable slots

`GetLoadProgress` (fraction in `[0;1]` for a loading bar) and `OnResize` may be `NULL`. The
`GameFrame_GetLoadProgress` wrapper returns a negative value ("unknown") when unimplemented; the
`GameFrame_OnResize` wrapper returns success. `Destroy` and all core slots must be non-NULL.

## Rendering contract

Each rendered frame is handed **its own** render target and draws into it; the manager then composites
every target onto the screen using `CompositeColor` (a straight 1:1 blit). Inside `Render`, build the
renderer's `RenderContext` from the target you were given (using the fitting config in the
`FrameRenderContext`), begin a pass, **clear to transparent**, draw, and end the pass.

> Note the naming: the `FrameRenderContext` you receive is just the per-render payload (time + fitting
> config). The renderer's `RenderContext` (from `Renderer.h`) is the drawing device you build from the
> target.

## Implementing a concrete frame (skeleton)

Embed `GameFrame` as the **first** member, provide a static vtable, and construct/destroy via the base
helpers. Loading maps naturally onto the AssetManager's stepped bulk op:

```c
typedef struct MainMenuFrameStruct
{
    GameFrame Base;                 // MUST be first.
    AssetManager* _assets;          // borrowed
    AssetUserID _assetUser;
    AssetBulkOperation* _loadOp;    // owned during loading
    bool _assetsReleased;
} MainMenuFrame;

static Error MainMenuFrame_BeginLoad(void* self)
{
    MainMenuFrame* Frame = self;
    Error Result = AssetManager_GetNewUserID(Frame->_assets, &Frame->_assetUser);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = AssetManager_CreateAssetBulkOperation(Frame->_assets, Frame->_assetUser, &Frame->_loadOp);
    if (Result.Code != ErrorCode_Success) { return Result; }
    // ... AssetBulkOperation_AddEntry(...) for each asset ...
    return Error_CreateSuccess();
}

static Error MainMenuFrame_LoadStep(void* self)
{
    MainMenuFrame* Frame = self;
    return AssetBulkOperation_CompleteStep(Frame->_loadOp, NULL); // one asset per call
}

static bool MainMenuFrame_IsLoaded(void* self)
{
    return AssetBulkOperation_IsComplete(((MainMenuFrame*)self)->_loadOp);
}

static float MainMenuFrame_GetLoadProgress(void* self)          // optional
{
    double Factor = 0.0;
    AssetLoadProgress_GetProgressFactor(AssetBulkOperation_GetProgress(((MainMenuFrame*)self)->_loadOp), &Factor);
    return (float)Factor;
}

static Error MainMenuFrame_Render(void* self, const FrameRenderContext* context, RenderTexture2D target)
{
    UNUSED(self);
    RenderContext Rc;
    RenderContext_Create(&Rc, &target, context->TargetAspectRatio, context->TargetAreaPosition);
    RenderContext_BeginRendering(&Rc);
    ClearBackground(BLANK);                                     // transparent → layers composite correctly
    // ... RenderContext_RenderTexture2D / RenderContext_RenderText2D ...
    RenderContext_EndRendering(&Rc);
    RenderContext_Deconstruct(&Rc);
    return Error_CreateSuccess();
}

static Error MainMenuFrame_BeginUnload(void* self)
{
    MainMenuFrame* Frame = self;
    Error Result = AssetManager_ReleaseAllAssetsForUser(Frame->_assets, Frame->_assetUser);
    Frame->_assetsReleased = true;
    return Result;
}
static Error MainMenuFrame_UnloadStep(void* self) { UNUSED(self); return Error_CreateSuccess(); }
static bool  MainMenuFrame_IsUnloaded(void* self) { UNUSED(self); return true; }

static void MainMenuFrame_Destroy(void* self)
{
    MainMenuFrame* Frame = self;
    if (Frame->_loadOp != NULL) { Error e = AssetBulkOperation_Deconstruct(Frame->_loadOp); Error_Deconstruct(&e); }
    if (!Frame->_assetsReleased)                                // also release if a forced shutdown skipped unload
    {
        Error e = AssetManager_ReleaseAllAssetsForUser(Frame->_assets, Frame->_assetUser);
        Error_Deconstruct(&e);
    }
    GameFrame_Deconstruct(&Frame->Base);                       // frees the base events
    Memory_Free(Frame);
}

static const GameFrameVTable MAIN_MENU_VTABLE =
{
    .Start = MainMenuFrame_Start, .End = MainMenuFrame_End, .Update = MainMenuFrame_Update,
    .BeginLoad = MainMenuFrame_BeginLoad, .LoadStep = MainMenuFrame_LoadStep, .IsLoaded = MainMenuFrame_IsLoaded,
    .BeginUnload = MainMenuFrame_BeginUnload, .UnloadStep = MainMenuFrame_UnloadStep, .IsUnloaded = MainMenuFrame_IsUnloaded,
    .Render = MainMenuFrame_Render, .GetLoadProgress = MainMenuFrame_GetLoadProgress, .OnResize = NULL,
    .Destroy = MainMenuFrame_Destroy
};

// Factory: allocate, GameFrame_Construct(&frame->Base, &MAIN_MENU_VTABLE, u8"MainMenu"), fill fields,
// return &frame->Base. Hand the returned GameFrame* to GameFrameManager_AddFrame.
```

## Notes / contracts

- Any vtable method returning a non-success `Error` is treated as **fatal**: the manager logs it CRITICAL
  and shuts down. Return errors only for genuine failures.
- `Destroy` must fully release everything (assets, GPU objects, allocations) even if the stepped unload
  never ran — it is also called on forced teardown. The `_assetsReleased` guard above keeps that
  idempotent.
- `Start`/`End` are only called if the frame actually reached the active state; a frame removed while
  still loading goes straight to unload.
