# Renderer module (RenderContext)

Files: `include/Renderer.h`, `source/Renderer.c`.

> **This is not the world renderer.** The 3D world is drawn by `WorldRenderer` ([WorldRuntime.md](WorldRuntime.md)),
> which drives raylib and rlgl directly and does not use this module. `Renderer.h` is the low-level, aspect-fitted
> 2D drawing device (with a small 3D passthrough) used by the UI and the text renderer.

## Purpose

A `RenderContext` describes one draw target, a `RenderTexture2D` or the window backbuffer, plus a **target area**: the
largest rectangle with the game's design aspect ratio that fits in it. Draw calls take positions and sizes in pixels or
in normalized units of the buffer or of the target area, so the same draw code fits any window shape. It owns nothing
and is meant to be built on the stack for each render.

## Coordinate spaces

| `RenderValueType` | Constructors | Meaning |
|---|---|---|
| `RenderValueType_Pixel` | `RenderFloat_Window`, `RenderVector2D_Window` | Render-buffer pixels. |
| `RenderValueType_NormalizedRelative` | `RenderFloat_Relative`, `RenderVector2D_Relative` | [0;1] across the whole buffer, (0, 0) at the top left. |
| `RenderValueType_NormalizedFitted` | `RenderFloat_Fitted`, `RenderVector2D_Fitted` | [0;1] across the target area. |

- The target area spans the buffer's full width or full height. `targetRelativePosition` places it in the leftover
  space; (0.5, 0.5) (`RenderTargetPosition_Centered()`) centres it. Example: 16:9 on 1920x1200 is a 1920x1080 area,
  60 px from the top.
- Values outside [0;1] are allowed; nothing is clamped.
- Scalars (`RenderFloat`: font sizes, line thickness) are vertical measures: relative is a fraction of the buffer
  height, fitted a fraction of the target-area height.
- Positions convert with the target area's offset, sizes without it. The `RenderContext_Vector*To*` helpers take an
  `isOffsetIncluded` flag for this; the `RenderContext_Size*To*` helpers convert vertical scalars.

## RenderColor

`RenderColor { Tint, Brightness, Opacity }`, resolved by `RenderColor_GetFinalColor`: the tint's RGB is multiplied by
`Brightness`, and its alpha is **replaced** by `Opacity` (the tint's own alpha is ignored). Both factors are clamped to
[0;1]. Helpers: `RenderColor_White()`, `RenderColor_Black()` (white at brightness 0) and `RenderColor_Transparent()`.
`GameFrame.CompositeColor` uses the same type.

## Usage

Inside a frame's `Render` ([GameFrame.md](GameFrame.md)), draw into the target the frame was given:

```c
RenderContext Context;
RenderContext_Create(&Context, &target, context->TargetAspectRatio, context->TargetAreaPosition);
RenderContext_BeginRendering(&Context);
ClearBackground(BLANK); // transparent, so stacked frames composite correctly

TextureRenderArguments Args =
{
    .Texture = Atlas,
    .Position = RenderVector2D_Fitted((Vector2){ .x = 0.5f, .y = 0.5f }),        // centre of the target area
    .RelativeSourceRectangle = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f },
    .Size = RenderVector2D_Fitted((Vector2){ .x = 0.25f, .y = 0.25f }),          // a quarter of its width and height
    .RelativeOrigin = (Vector2){ .x = 0.5f, .y = 0.5f },
    .RotationRad = 0.0f,
    .TargetColor = RenderColor_White()
};
RenderContext_RenderTexture2D(&Context, &Args);

RenderContext_EndRendering(&Context);
RenderContext_Deconstruct(&Context);
```

- With a target, `RenderContext_Create` copies the `RenderTexture2D` handle. The target must stay loaded while the
  context is used; `RenderContext_Deconstruct` only zeroes the context and never unloads it.
- With a NULL buffer the context targets the window backbuffer (sized by `GetRenderWidth`/`GetRenderHeight`), and
  `RenderContext_BeginRendering`/`EndRendering` call `BeginDrawing`/`EndDrawing`. Frames must not do this: the
  GameFrameManager owns the backbuffer composite and the manual `SwapScreenBuffer` (raylib is built with
  `SUPPORT_CUSTOM_FRAME_CONTROL`, see `BUILDING.md`), and it composites with raylib directly.
- For 3D, open `RenderContext_Begin3DMode(&Context, camera)` / `RenderContext_End3DMode` inside a pass. 3D draws are
  not aspect-fitted: they use the camera over the whole buffer.

## Draw calls

| Call | Arguments | Notes |
|---|---|---|
| `RenderContext_RenderTexture2D` | `TextureRenderArguments` | Normalized source rectangle. `Position` is where `RelativeOrigin` (normalized; also the rotation pivot) lands, `Size` the drawn size, rotation in radians. |
| `RenderContext_RenderText2D` | `TextRenderArguments` | Raw raylib `Font` in `TargetFont` (for a `GameFont`, pass `._rayFont`), NUL-terminated UTF-8 text, `Size` = line height as a `RenderFloat`, `SizeRelativeSpacing` = character spacing as a fraction of the size, origin relative to the whole text block. |
| `RenderContext_RenderRectangle` | `RectangleRenderArguments` | Filled; origin and rotation as for textures. |
| `RenderContext_RenderRectangleOutline` | `RectangleOutlineRenderArguments` | Axis-aligned, border drawn inside the bounds, `Thickness` is a `RenderFloat`. |
| `RenderContext_RenderLine` | `LineRenderArguments` | `Thickness` is a `RenderFloat`. |
| `RenderContext_RenderModel` | `ModelRenderArguments` | raylib `DrawModelEx`: world position, one rotation axis + angle in radians, per-axis scale, tint. Inside a 3D pass. |
| `RenderContext_RenderMesh` | `MeshRenderArguments` | `DrawMesh` with the given material and transform matrix (no tint). Inside a 3D pass. |
| `RenderContext_RenderMeshInstanced` | `MeshInstancedRenderArguments` | `DrawMeshInstanced` with one shared material; does nothing for a count <= 0. Inside a 3D pass. |

- Text measuring: `Renderer_MeasureTextNormalized(font, text, relativeSpacing)` measures at font size 1. Store the
  result in `TextRenderArguments.CachedDrawSize` with `HasCachedDrawSize = true` to skip measuring on every draw; the
  renderer scales it by the size.
- Draw counters (`_textureDrawCount`, `_stringDrawCount`, `_modelDrawCount`, `_meshDrawCount` counting each instance,
  `_primitiveDrawCount`) are reset by `RenderContext_Create` and read directly; there are no getters.

## Who uses it

`UIRenderContext` (the per-widget wrapper, see [UIFramework.md](UIFramework.md)), `UIScreen_Render` (keyboard
navigation outlines), `TextComponentRenderer` and `LabelWidget`. None of them is wired into a running frame yet:
nothing calls `RenderContext_Create` today. `WorldRenderer` and `GameFrameManager` draw with raylib directly.

## Pitfalls

- Main thread only, with the window's GL context alive.
- There is no clip or scissor stack yet; clip push/pop in `RenderContext` is planned for the UI work (Phase 5 of
  `handoff/2026-10_code_review/FIX_PLAN.md`).
- `RenderContext_RenderModel` draws with raylib's `DrawModelEx`, so each mesh uses its material's own shader (the
  WorldRenderer no longer writes its pass shaders into shared materials) and the tint is written into the shared
  material maps for the duration of the draw, then restored. That still breaks "render never writes shared assets";
  it is marked `// TODO:` and must move to per-draw material copies (as `WorldRendererGeometry` does) before the UI
  draws models.
- `TextRenderArguments.TargetFont` is a by-value copy of a font asset's raylib `Font`: use it for the draw, never store
  it past your asset user ([GameAssets.md](GameAssets.md)).
