#include "Renderer.h"
#include <limits.h>
#include <stdint.h>
#include "wr/WRMemory.h"
#include "wr/WRMath.h"


// Static functions.
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

    self->_renderBufferSizePixels = RenderBufferSize;
    self->_renderBufferAspectRatio = RenderBufferSize.x / RenderBufferSize.y;

    self->_targetAspectRatio = targetAspectRatio; 
    self->_targetRelativePosition = targetRelativePosition;

    self->_textureDrawCount = 0;
    self->_stringDrawCount = 0;
    self->_modelDrawCount = 0;
    self->_meshDrawCount = 0;
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

void RenderContext_RenderModel(RenderContext* self, const ModelRenderArguments* args)
{
    Color FinalTint = RenderColor_GetFinalColor(args->TargetColor);
    float RotationDeg = Math_RadToDegFloat(args->RotationAngleRad);
    DrawModelEx(args->TargetModel,
        args->Position,
        args->RotationAxis,
        RotationDeg,
        args->Scale,
        FinalTint);
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
    if (self->_hasCustomRenderBuffer)
    {
        EndTextureMode();
    }
    else
    {
        EndDrawing();
    }
}
