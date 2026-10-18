#include "TextStyle.h"


// Static functions.
// Keeps the first failure in *first and releases every later error so none leak.
static void AccumulateError(Error* first, Error incoming)
{
    if (incoming.Code == ErrorCode_Success)
    {
        Error_Deconstruct(&incoming);
        return;
    }
    if (first->Code == ErrorCode_Success)
    {
        *first = incoming;
    }
    else
    {
        Error_Deconstruct(&incoming);
    }
}

static Error ApplyToStringComponent(const TextStyle* self, StringComponent* component)
{
    Error First = Error_CreateSuccess();

    if (self->_hasFontName)
    {
        AccumulateError(&First, StringComponent_SetFontName(component, self->_fontName));
    }
    if (self->_hasColor)
    {
        AccumulateError(&First, StringComponent_SetColor(component, self->_color));
    }
    if (self->_hasStringSize)
    {
        AccumulateError(&First, StringComponent_SetSize(component, self->_stringSize));
    }
    if (self->_hasIsShadowActive)
    {
        AccumulateError(&First, StringComponent_SetIsShadowActive(component, self->_isShadowActive));
    }
    if (self->_hasShadowColor)
    {
        if (self->_shadowColorType == TextShadowColorType_Custom)
        {
            AccumulateError(&First, StringComponent_SetShadowColorCustom(component, self->_shadowColor));
        }
        else
        {
            AccumulateError(&First, StringComponent_SetShadowColorDefault(component));
        }
    }
    if (self->_hasShadowOffset)
    {
        AccumulateError(&First, (self->_shadowOffsetType == TextShadowOffsetType_Custom)
            ? StringComponent_SetShadowOffsetCustom(component, self->_shadowOffset)
            : StringComponent_SetShadowOffsetDefault(component));
    }
    if (self->_hasIsUnderlined)
    {
        AccumulateError(&First, StringComponent_SetIsUnderlined(component, self->_isUnderlined));
    }
    if (self->_hasIsStrikethrough)
    {
        AccumulateError(&First, StringComponent_SetIsStrikethrough(component, self->_isStrikethrough));
    }
    if (self->_hasSpacing)
    {
        AccumulateError(&First, StringComponent_SetSpacing(component, self->_spacing));
    }

    return First;
}

static Error ApplyToSpriteComponent(const TextStyle* self, SpriteComponent* component)
{
    Error First = Error_CreateSuccess();

    if (self->_hasColor)
    {
        AccumulateError(&First, SpriteComponent_SetColor(component, self->_color));
    }
    if (self->_hasSpriteSize)
    {
        AccumulateError(&First, SpriteComponent_SetSize(component, self->_spriteSize));
    }

    return First;
}


// Public functions.
void TextStyle_Construct(TextStyle* self)
{
    if (self == NULL)
    {
        return;
    }

    self->_hasFontName = false;
    self->_fontName = NULL;
    self->_hasColor = false;
    self->_hasStringSize = false;
    self->_hasSpriteSize = false;
    self->_hasIsShadowActive = false;
    self->_hasShadowColor = false;
    self->_hasShadowOffset = false;
    self->_hasIsUnderlined = false;
    self->_hasIsStrikethrough = false;
    self->_hasSpacing = false;
}

void TextStyle_Deconstruct(TextStyle* self)
{
    (void)self;
}

void TextStyle_SetFontName(TextStyle* self, const unsigned char* fontName)
{
    self->_fontName = fontName;
    self->_hasFontName = true;
}

bool TextStyle_GetFontName(const TextStyle* self, const unsigned char** outFontName)
{
    if (!self->_hasFontName)
    {
        return false;
    }
    *outFontName = self->_fontName;
    return true;
}

void TextStyle_ClearFontName(TextStyle* self)
{
    self->_hasFontName = false;
    self->_fontName = NULL;
}

void TextStyle_SetColor(TextStyle* self, RenderColor color)
{
    self->_color = color;
    self->_hasColor = true;
}

bool TextStyle_GetColor(const TextStyle* self, RenderColor* outColor)
{
    if (!self->_hasColor)
    {
        return false;
    }
    *outColor = self->_color;
    return true;
}

void TextStyle_ClearColor(TextStyle* self)
{
    self->_hasColor = false;
}

void TextStyle_SetStringSize(TextStyle* self, float size)
{
    self->_stringSize = size;
    self->_hasStringSize = true;
}

bool TextStyle_GetStringSize(const TextStyle* self, float* outSize)
{
    if (!self->_hasStringSize)
    {
        return false;
    }
    *outSize = self->_stringSize;
    return true;
}

void TextStyle_ClearStringSize(TextStyle* self)
{
    self->_hasStringSize = false;
}

void TextStyle_SetSpriteSize(TextStyle* self, Vector2 size)
{
    self->_spriteSize = size;
    self->_hasSpriteSize = true;
}

bool TextStyle_GetSpriteSize(const TextStyle* self, Vector2* outSize)
{
    if (!self->_hasSpriteSize)
    {
        return false;
    }
    *outSize = self->_spriteSize;
    return true;
}

void TextStyle_ClearSpriteSize(TextStyle* self)
{
    self->_hasSpriteSize = false;
}

void TextStyle_SetIsShadowActive(TextStyle* self, bool isShadowActive)
{
    self->_isShadowActive = isShadowActive;
    self->_hasIsShadowActive = true;
}

bool TextStyle_GetIsShadowActive(const TextStyle* self, bool* outIsShadowActive)
{
    if (!self->_hasIsShadowActive)
    {
        return false;
    }
    *outIsShadowActive = self->_isShadowActive;
    return true;
}

void TextStyle_ClearIsShadowActive(TextStyle* self)
{
    self->_hasIsShadowActive = false;
}

void TextStyle_SetShadowColorCustom(TextStyle* self, RenderColor color)
{
    self->_shadowColor = color;
    self->_shadowColorType = TextShadowColorType_Custom;
    self->_hasShadowColor = true;
}

void TextStyle_SetShadowColorDefault(TextStyle* self)
{
    self->_shadowColorType = TextShadowColorType_Default;
    self->_hasShadowColor = true;
}

bool TextStyle_GetShadowColor(const TextStyle* self, TextShadowColorType* outType, RenderColor* outColor)
{
    if (!self->_hasShadowColor)
    {
        return false;
    }
    *outType = self->_shadowColorType;
    *outColor = self->_shadowColor;
    return true;
}

void TextStyle_ClearShadowColor(TextStyle* self)
{
    self->_hasShadowColor = false;
}

void TextStyle_SetShadowOffsetCustom(TextStyle* self, Vector2 offset)
{
    self->_shadowOffset = offset;
    self->_shadowOffsetType = TextShadowOffsetType_Custom;
    self->_hasShadowOffset = true;
}

void TextStyle_SetShadowOffsetDefault(TextStyle* self)
{
    self->_shadowOffsetType = TextShadowOffsetType_Default;
    self->_hasShadowOffset = true;
}

bool TextStyle_GetShadowOffset(const TextStyle* self, TextShadowOffsetType* outType, Vector2* outOffset)
{
    if (!self->_hasShadowOffset)
    {
        return false;
    }
    *outType = self->_shadowOffsetType;
    *outOffset = self->_shadowOffset;
    return true;
}

void TextStyle_ClearShadowOffset(TextStyle* self)
{
    self->_hasShadowOffset = false;
}

void TextStyle_SetIsUnderlined(TextStyle* self, bool isUnderlined)
{
    self->_isUnderlined = isUnderlined;
    self->_hasIsUnderlined = true;
}

bool TextStyle_GetIsUnderlined(const TextStyle* self, bool* outIsUnderlined)
{
    if (!self->_hasIsUnderlined)
    {
        return false;
    }
    *outIsUnderlined = self->_isUnderlined;
    return true;
}

void TextStyle_ClearIsUnderlined(TextStyle* self)
{
    self->_hasIsUnderlined = false;
}

void TextStyle_SetIsStrikethrough(TextStyle* self, bool isStrikethrough)
{
    self->_isStrikethrough = isStrikethrough;
    self->_hasIsStrikethrough = true;
}

bool TextStyle_GetIsStrikethrough(const TextStyle* self, bool* outIsStrikethrough)
{
    if (!self->_hasIsStrikethrough)
    {
        return false;
    }
    *outIsStrikethrough = self->_isStrikethrough;
    return true;
}

void TextStyle_ClearIsStrikethrough(TextStyle* self)
{
    self->_hasIsStrikethrough = false;
}

void TextStyle_SetSpacing(TextStyle* self, float spacing)
{
    self->_spacing = spacing;
    self->_hasSpacing = true;
}

bool TextStyle_GetSpacing(const TextStyle* self, float* outSpacing)
{
    if (!self->_hasSpacing)
    {
        return false;
    }
    *outSpacing = self->_spacing;
    return true;
}

void TextStyle_ClearSpacing(TextStyle* self)
{
    self->_hasSpacing = false;
}

Error TextStyle_ApplyToComponent(const TextStyle* self, TextComponent* component)
{
    if ((self == NULL) || (component == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextStyle_ApplyToComponent: self and component must not be NULL.");
    }

    switch (TextComponent_GetType(component))
    {
        case TextComponentType_String:
        case TextComponentType_Translate:
            return ApplyToStringComponent(self, TextComponent_AsStringComponent(component));
        case TextComponentType_Sprite:
            return ApplyToSpriteComponent(self, TextComponent_AsSpriteComponent(component));
        case TextComponentType_Empty:
            return Error_CreateSuccess();
    }
    return Error_Construct2(ErrorCode_InvalidState, "TextStyle_ApplyToComponent: unknown component type.");
}
