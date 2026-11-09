#include "UIRenderContext.h"
#include "wr/WRMath.h"


// Static functions.
/* Builds a screen-normalized RenderVector2D (NormalizedRelative) from a raw vector. */
static inline RenderVector2D ToScreenRelative(Vector2 value)
{
    return RenderVector2D_Relative(value);
}


// Public functions.
void UIRenderContext_Create(UIRenderContext* self,
    RenderContext* renderContext,
    Vector2 absolutePosition,
    Vector2 absoluteSize,
    RenderColor tint)
{
    self->_renderContext = renderContext;
    self->_absolutePosition = absolutePosition;
    self->_absoluteSize = absoluteSize;
    self->_tint = tint;
}

Rectangle UIRenderContext_GetPixelBox(UIRenderContext* self)
{
    Vector2 TopLeft = RenderContext_VectorRelativeToPixel(self->_renderContext, self->_absolutePosition);
    Vector2 Size = RenderContext_VectorRelativeToPixel(self->_renderContext, self->_absoluteSize);
    float Left = Math_FloorFloat(TopLeft.x + 0.5f);
    float Top = Math_FloorFloat(TopLeft.y + 0.5f);
    float Right = Math_FloorFloat(TopLeft.x + Size.x + 0.5f);
    float Bottom = Math_FloorFloat(TopLeft.y + Size.y + 0.5f);
    return (Rectangle){ .x = Left, .y = Top, .width = Right - Left, .height = Bottom - Top };
}

RenderColor UIRenderColor_FromColor(Color color)
{
    return (RenderColor)
    {
        .Tint = { .r = color.r, .g = color.g, .b = color.b, .a = 255U },
        .Brightness = 1.0f,
        .Opacity = (float)color.a / 255.0f
    };
}

void UIRenderContext_RenderPixelRectangle(UIRenderContext* self, Rectangle box, Color color)
{
    RectangleRenderArguments Arguments =
    {
        .Position = RenderVector2D_Window((Vector2){ .x = box.x, .y = box.y }),
        .Size = RenderVector2D_Window((Vector2){ .x = box.width, .y = box.height }),
        .RelativeOrigin = (Vector2){ .x = 0.0f, .y = 0.0f },
        .RotationRad = 0.0f,
        .TargetColor = UIRenderColor_Multiply(self->_tint, UIRenderColor_FromColor(color))
    };
    RenderContext_RenderRectangle(self->_renderContext, &Arguments);
}

void UIRenderContext_RenderPixelGradient(UIRenderContext* self, Rectangle box, Color topLeft, Color topRight, Color bottomLeft, Color bottomRight)
{
    RectangleGradientRenderArguments Arguments =
    {
        .Position = RenderVector2D_Window((Vector2){ .x = box.x, .y = box.y }),
        .Size = RenderVector2D_Window((Vector2){ .x = box.width, .y = box.height }),
        .TopLeft = UIRenderColor_Multiply(self->_tint, UIRenderColor_FromColor(topLeft)),
        .TopRight = UIRenderColor_Multiply(self->_tint, UIRenderColor_FromColor(topRight)),
        .BottomLeft = UIRenderColor_Multiply(self->_tint, UIRenderColor_FromColor(bottomLeft)),
        .BottomRight = UIRenderColor_Multiply(self->_tint, UIRenderColor_FromColor(bottomRight))
    };
    RenderContext_RenderRectangleGradient(self->_renderContext, &Arguments);
}

void UIRenderContext_RenderPixelOutline(UIRenderContext* self, Rectangle box, float thicknessPixels, Color color)
{
    RectangleOutlineRenderArguments Arguments =
    {
        .Position = RenderVector2D_Window((Vector2){ .x = box.x, .y = box.y }),
        .Size = RenderVector2D_Window((Vector2){ .x = box.width, .y = box.height }),
        .Thickness = RenderFloat_Window(thicknessPixels),
        .TargetColor = UIRenderColor_Multiply(self->_tint, UIRenderColor_FromColor(color))
    };
    RenderContext_RenderRectangleOutline(self->_renderContext, &Arguments);
}

void UIRenderContext_RenderPixelTexture(UIRenderContext* self, Texture2D texture, Rectangle sourceTexels, Rectangle box, Color color)
{
    float Width = (texture.width > 0) ? (float)texture.width : 1.0f;
    float Height = (texture.height > 0) ? (float)texture.height : 1.0f;
    TextureRenderArguments Arguments =
    {
        .Texture = texture,
        .Position = RenderVector2D_Window((Vector2){ .x = box.x, .y = box.y }),
        .RelativeSourceRectangle = { .x = sourceTexels.x / Width, .y = sourceTexels.y / Height,
            .width = sourceTexels.width / Width, .height = sourceTexels.height / Height },
        .Size = RenderVector2D_Window((Vector2){ .x = box.width, .y = box.height }),
        .RelativeOrigin = (Vector2){ .x = 0.0f, .y = 0.0f },
        .RotationRad = 0.0f,
        .TargetColor = UIRenderColor_Multiply(self->_tint, UIRenderColor_FromColor(color))
    };
    RenderContext_RenderTexture2D(self->_renderContext, &Arguments);
}

Error UIRenderContext_RenderRectangle(UIRenderContext* self,
    Vector2 position,
    Vector2 size,
    Vector2 relativeOrigin,
    float rotationRad,
    RenderColor color)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderContext_RenderRectangle: self must not be NULL.");
    }

    Vector2 ScreenPosition = UIRenderContext_LocalToScreen(self, position);
    Vector2 ScreenSize = UIRenderContext_LocalSizeToScreen(self, size);
    RectangleRenderArguments Arguments =
    {
        .Position = ToScreenRelative(ScreenPosition),
        .Size = ToScreenRelative(ScreenSize),
        .RelativeOrigin = relativeOrigin,
        .RotationRad = rotationRad,
        .TargetColor = UIRenderColor_Multiply(self->_tint, color)
    };
    RenderContext_RenderRectangle(self->_renderContext, &Arguments);
    return Error_CreateSuccess();
}

Error UIRenderContext_RenderRectangleOutline(UIRenderContext* self,
    Vector2 position,
    Vector2 size,
    RenderFloat thickness,
    RenderColor color)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderContext_RenderRectangleOutline: self must not be NULL.");
    }

    Vector2 ScreenPosition = UIRenderContext_LocalToScreen(self, position);
    Vector2 ScreenSize = UIRenderContext_LocalSizeToScreen(self, size);
    RectangleOutlineRenderArguments Arguments =
    {
        .Position = ToScreenRelative(ScreenPosition),
        .Size = ToScreenRelative(ScreenSize),
        .Thickness = thickness,
        .TargetColor = UIRenderColor_Multiply(self->_tint, color)
    };
    RenderContext_RenderRectangleOutline(self->_renderContext, &Arguments);
    return Error_CreateSuccess();
}

Error UIRenderContext_RenderLine(UIRenderContext* self,
    Vector2 start,
    Vector2 end,
    RenderFloat thickness,
    RenderColor color)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderContext_RenderLine: self must not be NULL.");
    }

    Vector2 ScreenStart = UIRenderContext_LocalToScreen(self, start);
    Vector2 ScreenEnd = UIRenderContext_LocalToScreen(self, end);
    LineRenderArguments Arguments =
    {
        .StartPosition = ToScreenRelative(ScreenStart),
        .EndPosition = ToScreenRelative(ScreenEnd),
        .Thickness = thickness,
        .TargetColor = UIRenderColor_Multiply(self->_tint, color)
    };
    RenderContext_RenderLine(self->_renderContext, &Arguments);
    return Error_CreateSuccess();
}

Error UIRenderContext_RenderTexture(UIRenderContext* self, const UITextureDrawInfo* info)
{
    if ((self == NULL) || (info == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderContext_RenderTexture: self and info must not be NULL.");
    }

    Vector2 ScreenPosition = UIRenderContext_LocalToScreen(self, info->Position);
    Vector2 ScreenSize = UIRenderContext_LocalSizeToScreen(self, info->Size);
    TextureRenderArguments Arguments =
    {
        .Texture = info->Texture,
        .Position = ToScreenRelative(ScreenPosition),
        .RelativeSourceRectangle = info->RelativeSourceRectangle,
        .Size = ToScreenRelative(ScreenSize),
        .RelativeOrigin = info->RelativeOrigin,
        .RotationRad = info->RotationRad,
        .TargetColor = UIRenderColor_Multiply(self->_tint, info->Color)
    };
    RenderContext_RenderTexture2D(self->_renderContext, &Arguments);
    return Error_CreateSuccess();
}

Error UIRenderContext_RenderText(UIRenderContext* self, const UITextDrawInfo* info)
{
    if ((self == NULL) || (info == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderContext_RenderText: self and info must not be NULL.");
    }

    Vector2 ScreenPosition = UIRenderContext_LocalToScreen(self, info->Position);
    TextRenderArguments Arguments =
    {
        .Text = info->Text,
        .TargetFont = info->TargetFont,
        .Position = ToScreenRelative(ScreenPosition),
        .Size = info->Size,
        .SizeRelativeSpacing = info->SizeRelativeSpacing,
        .RelativeOrigin = info->RelativeOrigin,
        .RotationRad = info->RotationRad,
        .TargetColor = UIRenderColor_Multiply(self->_tint, info->Color),
        .CachedDrawSize = info->CachedDrawSize,
        .HasCachedDrawSize = info->HasCachedDrawSize
    };
    RenderContext_RenderText2D(self->_renderContext, &Arguments);
    return Error_CreateSuccess();
}

Error UIRenderContext_RenderModel(UIRenderContext* self, const ModelRenderArguments* args)
{
    if ((self == NULL) || (args == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIRenderContext_RenderModel: self and args must not be NULL.");
    }

    ModelRenderArguments Tinted = *args;
    Tinted.TargetColor = UIRenderColor_Multiply(self->_tint, args->TargetColor);
    RenderContext_RenderModel(self->_renderContext, &Tinted);
    return Error_CreateSuccess();
}
