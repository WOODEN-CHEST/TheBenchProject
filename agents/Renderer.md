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
| `RenderContext_RenderGlyph` | `GlyphRenderArguments` | One font glyph by glyph index (no codepoint lookup), in pixels: `PenPosition` is the pen origin (left edge of the advance, top of the line box), `FontSizePixels` the line height; the quad is offset and padded as raylib's `DrawTextCodepoint` does, then rotated around the pen. To rotate a whole text block, rotate every pen position around the block's pivot and pass the same angle. `TextLayout` draws all text this way. |
| `RenderContext_RenderRectangle` | `RectangleRenderArguments` | Filled; origin and rotation as for textures. |
| `RenderContext_RenderRectangleOutline` | `RectangleOutlineRenderArguments` | Axis-aligned, border drawn inside the bounds, `Thickness` is a `RenderFloat`. |
| `RenderContext_RenderLine` | `LineRenderArguments` | `Thickness` is a `RenderFloat`. |
| `RenderContext_RenderModel` | `ModelRenderArguments` | World position, one rotation axis + angle in radians, per-axis scale, tint (raylib `DrawModelEx`'s transform order), drawn mesh by mesh with a per-draw material copy whose albedo color carries the tint: nothing is written to the shared model. Uploads the model's bone matrices for GPU skinning like `DrawModelEx` (when the shader has a bone-transforms location and the model has bone matrices). Inside a 3D pass. |
| `RenderContext_RenderMesh` | `MeshRenderArguments` | `DrawMesh` with the given material and transform matrix (no tint). Inside a 3D pass. |
| `RenderContext_RenderMeshInstanced` | `MeshInstancedRenderArguments` | `DrawMeshInstanced` with one shared material; does nothing for a count <= 0. Inside a 3D pass. |

- Text measuring: `Renderer_MeasureTextNormalized(font, text, relativeSpacing)` measures at font size 1. Store the
  result in `TextRenderArguments.CachedDrawSize` with `HasCachedDrawSize = true` to skip measuring on every draw; the
  renderer scales it by the size.
- Draw counters (`_textureDrawCount`, `_stringDrawCount`, `_glyphDrawCount`, `_modelDrawCount`, `_meshDrawCount`
  counting each instance, `_primitiveDrawCount`) are reset by `RenderContext_Create` and read directly; there are no
  getters.

## Clipping

`RenderContext_PushClip(context, pixelRectangle)` restricts drawing to a rectangle intersected with the current clip,
until the matching `RenderContext_PopClip`, which restores the clip below it (or turns clipping off). Clips nest, so a
widget's clip stays inside its ancestors' (raylib's own scissor does not nest: never call `BeginScissorMode` in drawing
code). Rectangles are in render-buffer pixels, rounded outwards and limited to the buffer when applied (so a huge or
NaN rectangle cannot overflow the integer scissor). The stack holds
`RENDER_CONTEXT_MAX_CLIP_DEPTH` (32) clips; deeper pushes are counted (pops stay balanced) but keep the deepest stored
clip. `RenderContext_EndRendering` turns off a clip left pushed. Users: the UI screen (`AreSubWidgetsCut`) and
`TextLayout_Draw` (a label's cut).

## Measuring without a target

`RenderContext_CreateMeasuring(context, sizePixels, targetAspectRatio, targetRelativePosition)` builds a context for a
buffer of a known size with no target, for unit conversions outside rendering: the UI's Layout pass (in update) uses one
for its viewport. Never begin rendering or draw through it.

## Who uses it

`TextLayout_Draw` ([TextDocument.md](TextDocument.md)), `UIRenderContext` (the per-widget wrapper, see
[UIFramework.md](UIFramework.md)), `UIScreen_Render` (widgets, clipping, keyboard navigation outlines) and `LabelWidget`.
Running users: `WorldTestFrame`'s text sample (a `RenderContext` on its frame target after the world is drawn) and
`UITestFrame` (its screen, on its own transparent target). `WorldRenderer` and `GameFrameManager` draw with raylib
directly.

## Pitfalls

- Main thread only, with the window's GL context alive.
- Clip only through `RenderContext_PushClip` / `PopClip`, balanced within one rendering pass.
- `RenderContext_RenderModel` uses each material's own shader and copies `RENDERER_MATERIAL_MAP_COUNT` maps per mesh
  (raylib's private `MAX_MATERIAL_MAPS`, 12 in the pinned build; see BUILDING.md). The world renderer's per-draw copies
  use the same constant.
- `TextRenderArguments.TargetFont` is a by-value copy of a font asset's raylib `Font`: use it for the draw, never store
  it past your asset user ([GameAssets.md](GameAssets.md)).
