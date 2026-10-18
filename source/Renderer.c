#include "Renderer.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include "wr/WRMemory.h"
#include "wr/WRMath.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"


// Static functions.
/* Sets the size-derived fields and resets the counters and the clip stack. */
static void InitializeContext(RenderContext* self, Vector2 renderBufferSize, float targetAspectRatio,
    Vector2 targetRelativePosition)
{
    self->_renderBufferSizePixels = renderBufferSize;
    self->_renderBufferAspectRatio = renderBufferSize.x / renderBufferSize.y;

    self->_targetAspectRatio = targetAspectRatio;
    self->_targetRelativePosition = targetRelativePosition;

    self->_textureDrawCount = 0;
    self->_stringDrawCount = 0;
    self->_glyphDrawCount = 0;
    self->_modelDrawCount = 0;
    self->_meshDrawCount = 0;
    self->_primitiveDrawCount = 0;

    Memory_Zero(self->_clipStack, sizeof(self->_clipStack));
    self->_clipDepth = 0;
}

/* Clamps a pixel coordinate into [0; limit], so the int conversions in ApplyClip stay defined; NaN becomes 0. */
static float ClampToBuffer(float value, float limit)
{
    // The !(> 0) form also catches NaN.
    if (!(value > 0.0f))
    {
        return 0.0f;
    }
    return (value < limit) ? value : limit;
}

/* Applies the active clip to raylib's scissor, rounded outwards so it never cuts into a covered pixel. */
static void ApplyClip(const RenderContext* self)
{
    size_t StoredDepth = (self->_clipDepth < RENDER_CONTEXT_MAX_CLIP_DEPTH) ? self->_clipDepth : RENDER_CONTEXT_MAX_CLIP_DEPTH;
    Rectangle Clip = self->_clipStack[StoredDepth - 1U];
    Vector2 BufferSize = self->_renderBufferSizePixels;
    int Left = (int)floorf(ClampToBuffer(Clip.x, BufferSize.x));
    int Top = (int)floorf(ClampToBuffer(Clip.y, BufferSize.y));
    int Right = (int)ceilf(ClampToBuffer(Clip.x + Clip.width, BufferSize.x));
    int Bottom = (int)ceilf(ClampToBuffer(Clip.y + Clip.height, BufferSize.y));
    BeginScissorMode(Left, Top, (Right > Left) ? (Right - Left) : 0, (Bottom > Top) ? (Bottom - Top) : 0);
}

/* The overlap of two rectangles; an empty overlap has zero size. */
static Rectangle IntersectRectangles(Rectangle a, Rectangle b)
{
    float Left = Math_MaxFloat(a.x, b.x);
    float Top = Math_MaxFloat(a.y, b.y);
    float Right = Math_MinFloat(a.x + a.width, b.x + b.width);
    float Bottom = Math_MinFloat(a.y + a.height, b.y + b.height);
    return (Rectangle)
    {
        .x = Left,
        .y = Top,
        .width = Math_MaxFloat(0.0f, Right - Left),
        .height = Math_MaxFloat(0.0f, Bottom - Top)
    };
}

/* Componentwise color product, as raylib's DrawModelEx tints a material's albedo. */
static Color MultiplyColors(Color a, Color b)
{
    return (Color)
    {
        .r = (unsigned char)(((int)a.r * (int)b.r) / 255),
        .g = (unsigned char)(((int)a.g * (int)b.g) / 255),
        .b = (unsigned char)(((int)a.b * (int)b.b) / 255),
        .a = (unsigned char)(((int)a.a * (int)b.a) / 255)
    };
}

static inline Vector2 RenderContext_GetPixelVector(RenderContext* self, RenderVector2D vector, bool isOffsetIncluded)
{
    if (vector.Type == RenderValueType_NormalizedFitted)
    {
        return RenderContext_VectorFittedToPixel(self, vector.Value, isOffsetIncluded);
    }
    if (vector.Type == RenderValueType_NormalizedRelative)
    {
        return RenderContext_VectorRelativeToPixel(self, vector.Value);
    }
    return vector.Value;
}

static inline float RenderContext_GetPixelFloat(RenderContext* self, RenderFloat value)
{
    if (value.Type == RenderValueType_NormalizedFitted)
    {
        return RenderContext_SizeFittedToPixel(self, value.Value);
    }
    if (value.Type == RenderValueType_NormalizedRelative)
    {
        return RenderContext_SizeRelativeToPixel(self, value.Value);
    }
    return value.Value;
}


// Functions.
Color RenderColor_GetFinalColor(RenderColor color)
{
    Color FinalColor = color.Tint;

    float Opacity = Math_ClampFloat(color.Opacity, RENDER_COLOR_OPACITY_MIN, RENDER_COLOR_OPACITY_MAX);
    float Brightness = Math_ClampFloat(color.Brightness, RENDER_COLOR_BRIGHTNESS_MIN, RENDER_COLOR_BRIGHTNESS_MAX);

    FinalColor.a = (unsigned char)(UCHAR_MAX * Opacity);

    FinalColor.r = (unsigned char)(FinalColor.r * Brightness);
    FinalColor.g = (unsigned char)(FinalColor.g * Brightness);
    FinalColor.b = (unsigned char)(FinalColor.b * Brightness);

    return FinalColor;
}

void RenderContext_Deconstruct(RenderContext* self)
{
    Memory_Zero(self, sizeof(*self));
}

void RenderContext_Create(RenderContext* self,
    RenderTexture2D* renderBuffer,
    float targetAspectRatio,
    Vector2 targetRelativePosition)
{
    Vector2 RenderBufferSize;

    if (renderBuffer)
    {
        self->_renderBuffer = *renderBuffer;
        self->_hasCustomRenderBuffer = true;
        RenderBufferSize = (Vector2)
        {
            .x = (float)renderBuffer->texture.width,
            .y = (float)renderBuffer->texture.height
        };
    }
    else
    {
        Memory_Zero(&self->_renderBuffer, sizeof(self->_renderBuffer));
        self->_hasCustomRenderBuffer = false;
        RenderBufferSize = (Vector2)
        {
            .x = (float)GetRenderWidth(),
            .y = (float)GetRenderHeight(),
        };
    }

    InitializeContext(self, RenderBufferSize, targetAspectRatio, targetRelativePosition);
}

void RenderContext_CreateMeasuring(RenderContext* self,
    Vector2 renderBufferSizePixels,
    float targetAspectRatio,
    Vector2 targetRelativePosition)
{
    Memory_Zero(&self->_renderBuffer, sizeof(self->_renderBuffer));
    self->_hasCustomRenderBuffer = false;

    // The !(> 0) form also rejects NaN.
    Vector2 Size = renderBufferSizePixels;
    if (!(Size.x > 0.0f) || Math_IsInfinityFloat(Size.x))
    {
        Size.x = 1.0f;
    }
    if (!(Size.y > 0.0f) || Math_IsInfinityFloat(Size.y))
    {
        Size.y = 1.0f;
    }
    InitializeContext(self, Size, targetAspectRatio, targetRelativePosition);
}

void RenderContext_PushClip(RenderContext* self, Rectangle pixelRectangle)
{
    if (self->_clipDepth >= RENDER_CONTEXT_MAX_CLIP_DEPTH)
    {
        self->_clipDepth++; // Too deep to store: counted so the pops stay balanced, the clip stays as it is.
        return;
    }

    Rectangle Clip = pixelRectangle;
    Clip.width = Math_MaxFloat(0.0f, Clip.width);
    Clip.height = Math_MaxFloat(0.0f, Clip.height);
    if (self->_clipDepth > 0U)
    {
        Clip = IntersectRectangles(self->_clipStack[self->_clipDepth - 1U], Clip);
    }
    self->_clipStack[self->_clipDepth] = Clip;
    self->_clipDepth++;
    ApplyClip(self);
}

void RenderContext_PopClip(RenderContext* self)
{
    if (self->_clipDepth == 0U)
    {
        return;
    }
    self->_clipDepth--;
    if (self->_clipDepth >= RENDER_CONTEXT_MAX_CLIP_DEPTH)
    {
        return; // Still beyond the stored pushes: the clip did not change for this one.
    }
    if (self->_clipDepth == 0U)
    {
        EndScissorMode();
        return;
    }
    ApplyClip(self);
}

void RenderContext_RenderTexture2D(RenderContext* self, const TextureRenderArguments* args)
{
    Vector2 TextureSize = (Vector2) 
    {
        .x = (float)args->Texture.width,
        .y = (float)args->Texture.height,
    };
    Vector2 PixelPosition = RenderContext_GetPixelVector(self, args->Position, true);
    Vector2 PixelSize = RenderContext_GetPixelVector(self, args->Size, false);
    Rectangle Source = (Rectangle) 
    {
        .x = args->RelativeSourceRectangle.x * TextureSize.x,
        .y = args->RelativeSourceRectangle.y * TextureSize.y,
        .width = args->RelativeSourceRectangle.width * TextureSize.x,
        .height = args->RelativeSourceRectangle.height * TextureSize.y,
    };
    Rectangle Destination = (Rectangle) 
    {
        .x = PixelPosition.x,
        .y = PixelPosition.y,
        .width = PixelSize.x,
        .height = PixelSize.y
    };
    Vector2 PixelOrigin = (Vector2) 
    {
        .x = PixelSize.x * args->RelativeOrigin.x,
        .y = PixelSize.y * args->RelativeOrigin.y,
    };
    Color FinalTint = RenderColor_GetFinalColor(args->TargetColor);
    float RotationDeg = Math_RadToDegFloat(args->RotationRad);
    DrawTexturePro(args->Texture,
        Source,
        Destination,
        PixelOrigin,
        RotationDeg,
        FinalTint);
    self->_textureDrawCount++;
}

void RenderContext_RenderText2D(RenderContext* self, const TextRenderArguments* args)
{
    Vector2 DrawSize;
    if (args->HasCachedDrawSize)
    {
        DrawSize = args->CachedDrawSize;
    }
    else
    {
        DrawSize = Renderer_MeasureTextNormalized(args->TargetFont, args->Text, args->SizeRelativeSpacing);
    }

    float PixelSize = RenderContext_GetPixelFloat(self, args->Size);
    Vector2 PixelPosition = RenderContext_GetPixelVector(self, args->Position, true);
    Vector2 PixelOrigin = (Vector2)
    {
        .x = args->RelativeOrigin.x * DrawSize.x * PixelSize,
        .y = args->RelativeOrigin.y * DrawSize.y * PixelSize,
    };
    Color FinalTint = RenderColor_GetFinalColor(args->TargetColor);
    float RotationDeg = Math_RadToDegFloat(args->RotationRad);
    float Spacing = PixelSize * args->SizeRelativeSpacing;

    DrawTextPro(args->TargetFont,
        (const char*)args->Text,
        PixelPosition,
        PixelOrigin,
        RotationDeg,
        PixelSize,
        Spacing,
        FinalTint);
    self->_stringDrawCount++;
}

void RenderContext_RenderGlyph(RenderContext* self, const GlyphRenderArguments* args)
{
    const Font* TargetFont = args->TargetFont;
    const GlyphInfo* Glyph = &TargetFont->glyphs[args->GlyphIndex];
    Rectangle AtlasArea = TargetFont->recs[args->GlyphIndex];
    float Padding = (float)TargetFont->glyphPadding;
    float Scale = args->FontSizePixels / (float)TargetFont->baseSize;

    // Same quad as raylib's DrawTextCodepoint: the glyph's atlas area plus its padding, offset from the pen.
    Rectangle Source =
    {
        .x = AtlasArea.x - Padding,
        .y = AtlasArea.y - Padding,
        .width = AtlasArea.width + (2.0f * Padding),
        .height = AtlasArea.height + (2.0f * Padding)
    };
    Vector2 Offset = { .x = ((float)Glyph->offsetX - Padding) * Scale, .y = ((float)Glyph->offsetY - Padding) * Scale };
    Vector2 QuadPosition = { .x = args->PenPosition.x + Offset.x, .y = args->PenPosition.y + Offset.y };
    if (args->RotationRad != 0.0f)
    {
        float CosValue = Math_CosFloat(args->RotationRad);
        float SinValue = Math_SinFloat(args->RotationRad);
        QuadPosition = (Vector2)
        {
            .x = args->PenPosition.x + ((Offset.x * CosValue) - (Offset.y * SinValue)),
            .y = args->PenPosition.y + ((Offset.x * SinValue) + (Offset.y * CosValue))
        };
    }
    Rectangle Destination =
    {
        .x = QuadPosition.x,
        .y = QuadPosition.y,
        .width = Source.width * Scale,
        .height = Source.height * Scale
    };

    DrawTexturePro(TargetFont->texture, Source, Destination, (Vector2){ .x = 0.0f, .y = 0.0f },
        Math_RadToDegFloat(args->RotationRad), RenderColor_GetFinalColor(args->TargetColor));
    self->_glyphDrawCount++;
}

void RenderContext_RenderModel(RenderContext* self, const ModelRenderArguments* args)
{
    // DrawModelEx's transform order (scale, rotate, translate, inside the model's own transform) and GPU skinning, but
    // each mesh draws with a material copied on the stack: DrawModelEx writes the tint into the shared asset's maps.
    Color FinalTint = RenderColor_GetFinalColor(args->TargetColor);
    Model TargetModel = args->TargetModel;
    Matrix Placement = MatrixMultiply(MatrixMultiply(MatrixScale(args->Scale.x, args->Scale.y, args->Scale.z),
        MatrixRotate(args->RotationAxis, args->RotationAngleRad)),
        MatrixTranslate(args->Position.x, args->Position.y, args->Position.z));
    Matrix MeshTransform = MatrixMultiply(TargetModel.transform, Placement);

    for (int MeshIndex = 0; MeshIndex < TargetModel.meshCount; MeshIndex++)
    {
        const Material* AssetMaterial = &TargetModel.materials[TargetModel.meshMaterial[MeshIndex]];
        MaterialMap Maps[RENDERER_MATERIAL_MAP_COUNT];
        Memory_Copy(AssetMaterial->maps, Maps, sizeof(Maps));
        Maps[MATERIAL_MAP_ALBEDO].color = MultiplyColors(Maps[MATERIAL_MAP_ALBEDO].color, FinalTint);

        Material DrawMaterial = *AssetMaterial;
        DrawMaterial.maps = Maps;
        if ((DrawMaterial.shader.locs != NULL) && (DrawMaterial.shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS] != -1)
            && (TargetModel.boneMatrices != NULL))
        {
            rlEnableShader(DrawMaterial.shader.id);
            rlSetUniformMatrices(DrawMaterial.shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS], TargetModel.boneMatrices,
                TargetModel.skeleton.boneCount);
        }
        DrawMesh(TargetModel.meshes[MeshIndex], DrawMaterial, MeshTransform);
    }
    self->_modelDrawCount++;
}

void RenderContext_RenderMesh(RenderContext* self, const MeshRenderArguments* args)
{
    DrawMesh(args->TargetMesh, args->TargetMaterial, args->Transform);
    self->_meshDrawCount++;
}

void RenderContext_RenderMeshInstanced(RenderContext* self, const MeshInstancedRenderArguments* args)
{
    if (args->InstanceCount <= 0)
    {
        return;
    }

    DrawMeshInstanced(args->TargetMesh, args->TargetMaterial, args->Transforms, args->InstanceCount);
    self->_meshDrawCount += (size_t)args->InstanceCount;
}

void RenderContext_RenderRectangle(RenderContext* self, const RectangleRenderArguments* args)
{
    Vector2 PixelPosition = RenderContext_GetPixelVector(self, args->Position, true);
    Vector2 PixelSize = RenderContext_GetPixelVector(self, args->Size, false);
    Rectangle Destination = (Rectangle)
    {
        .x = PixelPosition.x,
        .y = PixelPosition.y,
        .width = PixelSize.x,
        .height = PixelSize.y
    };
    Vector2 PixelOrigin = (Vector2)
    {
        .x = PixelSize.x * args->RelativeOrigin.x,
        .y = PixelSize.y * args->RelativeOrigin.y,
    };
    Color FinalTint = RenderColor_GetFinalColor(args->TargetColor);
    float RotationDeg = Math_RadToDegFloat(args->RotationRad);
    DrawRectanglePro(Destination, PixelOrigin, RotationDeg, FinalTint);
    self->_primitiveDrawCount++;
}

void RenderContext_RenderRectangleOutline(RenderContext* self, const RectangleOutlineRenderArguments* args)
{
    Vector2 PixelPosition = RenderContext_GetPixelVector(self, args->Position, true);
    Vector2 PixelSize = RenderContext_GetPixelVector(self, args->Size, false);
    Rectangle Destination = (Rectangle)
    {
        .x = PixelPosition.x,
        .y = PixelPosition.y,
        .width = PixelSize.x,
        .height = PixelSize.y
    };
    float PixelThickness = RenderContext_GetPixelFloat(self, args->Thickness);
    Color FinalTint = RenderColor_GetFinalColor(args->TargetColor);
    DrawRectangleLinesEx(Destination, PixelThickness, FinalTint);
    self->_primitiveDrawCount++;
}

void RenderContext_RenderLine(RenderContext* self, const LineRenderArguments* args)
{
    Vector2 PixelStart = RenderContext_GetPixelVector(self, args->StartPosition, true);
    Vector2 PixelEnd = RenderContext_GetPixelVector(self, args->EndPosition, true);
    float PixelThickness = RenderContext_GetPixelFloat(self, args->Thickness);
    Color FinalTint = RenderColor_GetFinalColor(args->TargetColor);
    DrawLineEx(PixelStart, PixelEnd, PixelThickness, FinalTint);
    self->_primitiveDrawCount++;
}

void RenderContext_Begin3DMode(RenderContext* self, Camera3D camera)
{
    UNUSED(self);
    BeginMode3D(camera);
}

void RenderContext_End3DMode(RenderContext* self)
{
    UNUSED(self);
    EndMode3D();
}

void RenderContext_BeginRendering(RenderContext* self)
{
    if (self->_hasCustomRenderBuffer)
    {
        BeginTextureMode(self->_renderBuffer);
    }
    else
    {
        BeginDrawing();
    }
}

void RenderContext_EndRendering(RenderContext* self)
{
    // An unbalanced push would leave raylib's scissor on for whatever draws next.
    if (self->_clipDepth > 0U)
    {
        EndScissorMode();
        self->_clipDepth = 0;
    }
    if (self->_hasCustomRenderBuffer)
    {
        EndTextureMode();
    }
    else
    {
        EndDrawing();
    }
}
