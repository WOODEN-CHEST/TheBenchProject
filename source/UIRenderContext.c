#include "UIRenderContext.h"


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
