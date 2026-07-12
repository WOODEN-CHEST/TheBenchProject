#include "TextComponent.h"
#include "wr/WRString.h"
#include <math.h>


// Static functions.
static bool IsFiniteFloat(float value)
{
    return isfinite((double)value);
}

static bool IsRenderColorFinite(RenderColor color)
{
    return (IsFiniteFloat(color.Brightness) && IsFiniteFloat(color.Opacity));
}

static bool IsVector2Finite(Vector2 value)
{
    return (IsFiniteFloat(value.x) && IsFiniteFloat(value.y));
}

// Shared vtable Destroy for all current component types: none own resources beyond the base.
// self points at a concrete component whose first member is the base, so it is a valid TextComponent*.
static void Component_DestroyShared(void* self)
{
    TextComponent* Base = self;
    TextComponent_Deconstruct(Base);
}

// One static vtable per concrete type (per the OOP convention), all sharing the same Destroy.
static const TextComponentVTable StringComponentVTable = { .Destroy = Component_DestroyShared };
static const TextComponentVTable SpriteComponentVTable = { .Destroy = Component_DestroyShared };
static const TextComponentVTable EmptyComponentVTable = { .Destroy = Component_DestroyShared };

// Reads the child pointer stored at the given index, or NULL if out of range.
static TextComponent* GetChildAt(const TextComponent* self, size_t index)
{
    if ((self->_subComponents == NULL) || (index >= self->_subComponents->_count))
    {
        return NULL;
    }

    TextComponent* Child = NULL;
    if (!GenericBuffer_GetAt(self->_subComponents, index, &Child))
    {
        return NULL;
    }
    return Child;
}


// Public functions.
Error TextComponent_Construct(TextComponent* self,
    const TextComponentVTable* vtable,
    TextComponentType type,
    GenericBuffer* subComponentBuffer)
{
    if ((self == NULL) || (vtable == NULL) || (vtable->Destroy == NULL) || (subComponentBuffer == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponent_Construct: self, vtable, vtable->Destroy and subComponentBuffer must not be NULL.");
    }
    if (subComponentBuffer->_elementSize != sizeof(TextComponent*))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "TextComponent_Construct: subComponentBuffer element size must equal sizeof(TextComponent*).");
    }

    self->VTable = vtable;
    self->Type = type;
    self->_subComponents = subComponentBuffer;
    return Error_CreateSuccess();
}

void TextComponent_Deconstruct(TextComponent* self)
{
    if (self == NULL)
    {
        return;
    }

    // The buffer itself is owned by the factory (returned separately); only drop our references to it.
    if (self->_subComponents != NULL)
    {
        GenericBuffer_Clear(self->_subComponents);
    }
    self->VTable = NULL;
    self->_subComponents = NULL;
}

bool TextComponent_IsInSubtree(const TextComponent* root, const TextComponent* target)
{
    if ((root == NULL) || (target == NULL))
    {
        return false;
    }
    if (root == target)
    {
        return true;
    }

    size_t ChildCount = TextComponent_GetSubComponentCount(root);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        const TextComponent* Child = GetChildAt(root, Index);
        if (TextComponent_IsInSubtree(Child, target))
        {
            return true;
        }
    }
    return false;
}

Error TextComponent_AddSubComponent(TextComponent* self, TextComponent* child)
{
    if ((self == NULL) || (child == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponent_AddSubComponent: self and child must not be NULL.");
    }
    // Adding child under self is a cycle if self is child itself or lies inside child's subtree.
    if (TextComponent_IsInSubtree(child, self))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "TextComponent_AddSubComponent: a component cannot be added to itself or its own subtree.");
    }

    TextComponent* ChildPointer = child;
    if (!GenericBuffer_AddLast(self->_subComponents, &ChildPointer))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge,
            "TextComponent_AddSubComponent: could not grow the subcomponent list.");
    }
    return Error_CreateSuccess();
}

Error TextComponent_InsertSubComponent(TextComponent* self, size_t index, TextComponent* child)
{
    if ((self == NULL) || (child == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponent_InsertSubComponent: self and child must not be NULL.");
    }
    if (index > self->_subComponents->_count)
    {
        return Error_Construct2(ErrorCode_IndexOutOfBounds,
            "TextComponent_InsertSubComponent: index exceeds the subcomponent count.");
    }
    if (TextComponent_IsInSubtree(child, self))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "TextComponent_InsertSubComponent: a component cannot be added to itself or its own subtree.");
    }

    TextComponent* ChildPointer = child;
    if (!GenericBuffer_Insert(self->_subComponents, &ChildPointer, index))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge,
            "TextComponent_InsertSubComponent: could not grow the subcomponent list.");
    }
    return Error_CreateSuccess();
}

Error TextComponent_RemoveSubComponentAt(TextComponent* self, size_t index)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponent_RemoveSubComponentAt: self must not be NULL.");
    }
    if (index >= self->_subComponents->_count)
    {
        return Error_Construct2(ErrorCode_IndexOutOfBounds,
            "TextComponent_RemoveSubComponentAt: index is not less than the subcomponent count.");
    }

    GenericBuffer_RemoveAt(self->_subComponents, index);
    return Error_CreateSuccess();
}

Error TextComponent_RemoveSubComponent(TextComponent* self, const TextComponent* child)
{
    if ((self == NULL) || (child == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponent_RemoveSubComponent: self and child must not be NULL.");
    }

    size_t ChildCount = TextComponent_GetSubComponentCount(self);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        if (GetChildAt(self, Index) == child)
        {
            GenericBuffer_RemoveAt(self->_subComponents, Index);
            return Error_CreateSuccess();
        }
    }
    return Error_Construct2(ErrorCode_InvalidOperation,
        "TextComponent_RemoveSubComponent: child is not a direct child of self.");
}

Error TextComponent_ClearSubComponents(TextComponent* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponent_ClearSubComponents: self must not be NULL.");
    }

    GenericBuffer_Clear(self->_subComponents);
    return Error_CreateSuccess();
}

size_t TextComponent_GetSubComponentCount(const TextComponent* self)
{
    if ((self == NULL) || (self->_subComponents == NULL))
    {
        return 0;
    }
    return self->_subComponents->_count;
}

TextComponent* TextComponent_GetSubComponentAt(const TextComponent* self, size_t index)
{
    if (self == NULL)
    {
        return NULL;
    }
    return GetChildAt(self, index);
}


// Concrete constructors.
Error StringComponent_Construct(StringComponent* self, GenericBuffer* subComponentBuffer, const unsigned char* text)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_Construct: self must not be NULL.");
    }
    if ((text != NULL) && !StringUTF8_IsEncodingValid(text))
    {
        return Error_Construct2(ErrorCode_InvalidTextEncoding,
            "StringComponent_Construct: text must be valid UTF-8.");
    }

    Error BaseResult = TextComponent_Construct(&self->Base, &StringComponentVTable,
        TextComponentType_String, subComponentBuffer);
    if (BaseResult.Code != ErrorCode_Success)
    {
        return BaseResult;
    }

    self->_text = text;
    self->_font = (GameFont){ ._rayFont = GetFontDefault() };
    self->_fontName = NULL;
    self->_color = RenderColor_White();
    self->_size = STRING_COMPONENT_DEFAULT_SIZE;
    self->IsShadowActive = STRING_COMPONENT_DEFAULT_IS_SHADOW_ACTIVE;
    self->_shadowColorType = TextShadowColorType_Default;
    self->_shadowColor = RenderColor_White();
    self->_shadowOffset = (Vector2){ .x = STRING_COMPONENT_DEFAULT_SHADOW_OFFSET_X,
        .y = STRING_COMPONENT_DEFAULT_SHADOW_OFFSET_Y };
    self->IsUnderlined = false;
    self->IsStrikethrough = false;
    self->_spacing = STRING_COMPONENT_DEFAULT_SPACING;
    return Error_CreateSuccess();
}

Error SpriteComponent_Construct(SpriteComponent* self, GenericBuffer* subComponentBuffer,
    SpriteAnimationInstance* animationInstance)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "SpriteComponent_Construct: self must not be NULL.");
    }

    Error BaseResult = TextComponent_Construct(&self->Base, &SpriteComponentVTable,
        TextComponentType_Sprite, subComponentBuffer);
    if (BaseResult.Code != ErrorCode_Success)
    {
        return BaseResult;
    }

    self->_animationInstance = animationInstance;
    self->_animationName = NULL;
    self->_color = RenderColor_White();
    self->_size = (Vector2){ .x = 0.0f, .y = 0.0f };
    return Error_CreateSuccess();
}

Error EmptyComponent_Construct(EmptyComponent* self, GenericBuffer* subComponentBuffer)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "EmptyComponent_Construct: self must not be NULL.");
    }

    return TextComponent_Construct(&self->Base, &EmptyComponentVTable,
        TextComponentType_Empty, subComponentBuffer);
}


// StringComponent accessors.
Error StringComponent_SetText(StringComponent* self, const unsigned char* text)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetText: self must not be NULL.");
    }
    if ((text != NULL) && !StringUTF8_IsEncodingValid(text))
    {
        return Error_Construct2(ErrorCode_InvalidTextEncoding, "StringComponent_SetText: text must be valid UTF-8.");
    }

    self->_text = text;
    return Error_CreateSuccess();
}

Error StringComponent_SetFont(StringComponent* self, GameFont font)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetFont: self must not be NULL.");
    }

    self->_font = font;
    return Error_CreateSuccess();
}

Error StringComponent_SetFontName(StringComponent* self, const unsigned char* fontName)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetFontName: self must not be NULL.");
    }

    self->_fontName = fontName;
    return Error_CreateSuccess();
}

Error StringComponent_SetColor(StringComponent* self, RenderColor color)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetColor: self must not be NULL.");
    }
    if (!IsRenderColorFinite(color))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "StringComponent_SetColor: color brightness and opacity must be finite.");
    }

    self->_color = color;
    return Error_CreateSuccess();
}

Error StringComponent_SetSize(StringComponent* self, float size)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetSize: self must not be NULL.");
    }
    if (!IsFiniteFloat(size))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "StringComponent_SetSize: size must be finite.");
    }

    self->_size = size;
    return Error_CreateSuccess();
}

Error StringComponent_SetShadowColorCustom(StringComponent* self, RenderColor color)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "StringComponent_SetShadowColorCustom: self must not be NULL.");
    }
    if (!IsRenderColorFinite(color))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "StringComponent_SetShadowColorCustom: color brightness and opacity must be finite.");
    }

    self->_shadowColor = color;
    self->_shadowColorType = TextShadowColorType_Custom;
    return Error_CreateSuccess();
}

Error StringComponent_SetShadowColorDefault(StringComponent* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "StringComponent_SetShadowColorDefault: self must not be NULL.");
    }

    self->_shadowColorType = TextShadowColorType_Default;
    return Error_CreateSuccess();
}

Error StringComponent_SetShadowOffset(StringComponent* self, Vector2 offset)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "StringComponent_SetShadowOffset: self must not be NULL.");
    }
    if (!IsVector2Finite(offset))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "StringComponent_SetShadowOffset: offset components must be finite.");
    }

    self->_shadowOffset = offset;
    return Error_CreateSuccess();
}

Error StringComponent_SetSpacing(StringComponent* self, float spacing)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetSpacing: self must not be NULL.");
    }
    if (!IsFiniteFloat(spacing))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "StringComponent_SetSpacing: spacing must be finite.");
    }

    self->_spacing = spacing;
    return Error_CreateSuccess();
}


// SpriteComponent accessors.
Error SpriteComponent_SetAnimationInstance(SpriteComponent* self, SpriteAnimationInstance* animationInstance)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "SpriteComponent_SetAnimationInstance: self must not be NULL.");
    }

    self->_animationInstance = animationInstance;
    return Error_CreateSuccess();
}

Error SpriteComponent_SetAnimationName(SpriteComponent* self, const unsigned char* animationName)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "SpriteComponent_SetAnimationName: self must not be NULL.");
    }

    self->_animationName = animationName;
    return Error_CreateSuccess();
}

Error SpriteComponent_SetColor(SpriteComponent* self, RenderColor color)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "SpriteComponent_SetColor: self must not be NULL.");
    }
    if (!IsRenderColorFinite(color))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "SpriteComponent_SetColor: color brightness and opacity must be finite.");
    }

    self->_color = color;
    return Error_CreateSuccess();
}

Error SpriteComponent_SetSize(SpriteComponent* self, Vector2 size)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "SpriteComponent_SetSize: self must not be NULL.");
    }
    if (!IsVector2Finite(size))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "SpriteComponent_SetSize: size components must be finite.");
    }

    self->_size = size;
    return Error_CreateSuccess();
}
