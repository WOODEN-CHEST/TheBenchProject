#include "TextComponentGHDF.h"
#include "wr/WRString.h"
#include "wr/WRMemory.h"


// Macros.
// Component compound entry IDs.
#define FIELD_TYPE ((GHDFEntryID)1)
#define FIELD_CHILDREN ((GHDFEntryID)2)
#define FIELD_TEXT ((GHDFEntryID)10)
#define FIELD_FONT ((GHDFEntryID)11)
#define FIELD_COLOR ((GHDFEntryID)12)
#define FIELD_SIZE ((GHDFEntryID)13)
#define FIELD_SHADOW_ACTIVE ((GHDFEntryID)14)
#define FIELD_SHADOW_COLOR_TYPE ((GHDFEntryID)15)
#define FIELD_SHADOW_COLOR ((GHDFEntryID)16)
#define FIELD_SHADOW_OFFSET ((GHDFEntryID)17)
#define FIELD_UNDERLINE ((GHDFEntryID)18)
#define FIELD_STRIKETHROUGH ((GHDFEntryID)19)
#define FIELD_SPACING ((GHDFEntryID)20)
#define FIELD_ANIMATION ((GHDFEntryID)30)
#define FIELD_SPRITE_COLOR ((GHDFEntryID)31)
#define FIELD_SPRITE_SIZE ((GHDFEntryID)32)

// Render-color sub-compound entry IDs.
#define RC_TINT ((GHDFEntryID)1)
#define RC_BRIGHTNESS ((GHDFEntryID)2)
#define RC_OPACITY ((GHDFEntryID)3)

// Vector sub-compound entry IDs.
#define VEC_X ((GHDFEntryID)1)
#define VEC_Y ((GHDFEntryID)2)

#define COLOR_CHANNEL_MASK (0xFFU)


// Static functions.
static GHDFCompoundEntryType CompoundArrayType(void)
{
    return (GHDFCompoundEntryType){ .ValueType = GHDFValueType_Compound, .IsArray = true };
}

static Error SetEntry(GHDFCompound* compound, GHDFEntryID id, GHDFObjectValue value)
{
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(value.Type), &value);
}


// Forward declaration for the recursion.
static Error SerializeToCompound(const TextComponent* component, GHDFObjectPool* pool, GHDFCompound** outCompound);
static Error DeserializeFromCompound(GHDFCompound* compound, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent);


// Static functions (serialize).
static Error SetStringEntry(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, const unsigned char* text)
{
    GenericBuffer* Buffer = NULL;
    Error BorrowResult = GHDFObjectPool_BorrowString(pool, &Buffer);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }

    if (!GenericBuffer_AppendString(Buffer, text) || !GenericBuffer_NullTerminate(Buffer))
    {
        Error ReturnResult = GHDFObjectPool_ReturnString(pool, Buffer);
        Error_Deconstruct(&ReturnResult);
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentGHDF: could not build a string value.");
    }

    GHDFObjectValue Value = GHDFObjectValue_CreateString(Buffer);
    Error SetResult = GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_String), &Value);
    if (SetResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnString(pool, Buffer);
        Error_Deconstruct(&ReturnResult);
        return SetResult;
    }
    return Error_CreateSuccess();
}

static Error SetRenderColorEntry(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, RenderColor color)
{
    GHDFCompound* ColorCompound = NULL;
    Error BorrowResult = GHDFObjectPool_BorrowCompound(pool, &ColorCompound);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }
    GHDFObjectValue CompoundValue = GHDFObjectValue_CreateCompound(ColorCompound);
    Error SetResult = GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_Compound), &CompoundValue);
    if (SetResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, ColorCompound, true);
        Error_Deconstruct(&ReturnResult);
        return SetResult;
    }

    uint32_t Packed = ((uint32_t)color.Tint.r << 24) | ((uint32_t)color.Tint.g << 16)
        | ((uint32_t)color.Tint.b << 8) | (uint32_t)color.Tint.a;

    Error Result = SetEntry(ColorCompound, RC_TINT, GHDFObjectValue_CreateUInt32(Packed));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetEntry(ColorCompound, RC_BRIGHTNESS, GHDFObjectValue_CreateFloat(color.Brightness));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetEntry(ColorCompound, RC_OPACITY, GHDFObjectValue_CreateFloat(color.Opacity));
}

static Error SetVectorEntry(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, Vector2 vector)
{
    GHDFCompound* VectorCompound = NULL;
    Error BorrowResult = GHDFObjectPool_BorrowCompound(pool, &VectorCompound);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }
    GHDFObjectValue CompoundValue = GHDFObjectValue_CreateCompound(VectorCompound);
    Error SetResult = GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_Compound), &CompoundValue);
    if (SetResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, VectorCompound, true);
        Error_Deconstruct(&ReturnResult);
        return SetResult;
    }

    Error Result = SetEntry(VectorCompound, VEC_X, GHDFObjectValue_CreateFloat(vector.x));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetEntry(VectorCompound, VEC_Y, GHDFObjectValue_CreateFloat(vector.y));
}

static Error FillStringComponent(GHDFObjectPool* pool, GHDFCompound* compound, const StringComponent* stringComponent)
{
    Error Result = SetStringEntry(pool, compound, FIELD_TEXT,
        (stringComponent->_text != NULL) ? stringComponent->_text : (const unsigned char*)u8"");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (stringComponent->_fontName != NULL)
    {
        Result = SetStringEntry(pool, compound, FIELD_FONT, stringComponent->_fontName);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Result = SetRenderColorEntry(pool, compound, FIELD_COLOR, stringComponent->_color);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetEntry(compound, FIELD_SIZE, GHDFObjectValue_CreateFloat(stringComponent->_size));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetEntry(compound, FIELD_SHADOW_ACTIVE, GHDFObjectValue_CreateBoolean(stringComponent->IsShadowActive));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetEntry(compound, FIELD_SHADOW_COLOR_TYPE, GHDFObjectValue_CreateUInt8((uint8_t)stringComponent->_shadowColorType));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (stringComponent->_shadowColorType == TextShadowColorType_Custom)
    {
        Result = SetRenderColorEntry(pool, compound, FIELD_SHADOW_COLOR, stringComponent->_shadowColor);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Result = SetVectorEntry(pool, compound, FIELD_SHADOW_OFFSET, stringComponent->_shadowOffset);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetEntry(compound, FIELD_UNDERLINE, GHDFObjectValue_CreateBoolean(stringComponent->IsUnderlined));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetEntry(compound, FIELD_STRIKETHROUGH, GHDFObjectValue_CreateBoolean(stringComponent->IsStrikethrough));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetEntry(compound, FIELD_SPACING, GHDFObjectValue_CreateFloat(stringComponent->_spacing));
}

static Error FillSpriteComponent(GHDFObjectPool* pool, GHDFCompound* compound, const SpriteComponent* spriteComponent)
{
    if (spriteComponent->_animationName != NULL)
    {
        Error Result = SetStringEntry(pool, compound, FIELD_ANIMATION, spriteComponent->_animationName);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Error ColorResult = SetRenderColorEntry(pool, compound, FIELD_SPRITE_COLOR, spriteComponent->_color);
    if (ColorResult.Code != ErrorCode_Success)
    {
        return ColorResult;
    }
    return SetVectorEntry(pool, compound, FIELD_SPRITE_SIZE, spriteComponent->_size);
}

static Error FillComponentBody(const TextComponent* component, GHDFObjectPool* pool, GHDFCompound* compound)
{
    Error Result = SetEntry(compound, FIELD_TYPE, GHDFObjectValue_CreateUInt8((uint8_t)component->Type));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    switch (component->Type)
    {
        case TextComponentType_String:
            return FillStringComponent(pool, compound, (const StringComponent*)component);
        case TextComponentType_Sprite:
            return FillSpriteComponent(pool, compound, (const SpriteComponent*)component);
        case TextComponentType_Empty:
        default:
            return Error_CreateSuccess();
    }
}

static Error FillChildren(const TextComponent* component, GHDFObjectPool* pool, GHDFCompound* compound)
{
    size_t ChildCount = TextComponent_GetSubComponentCount(component);
    if (ChildCount == 0)
    {
        return Error_CreateSuccess();
    }

    GHDFArray* Array = NULL;
    Error BorrowResult = GHDFObjectPool_BorrowArray(pool, GHDFValueType_Compound, &Array);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }
    GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(Array, GHDFValueType_Compound);
    Error SetResult = GHDFCompound_SetValue(compound, FIELD_CHILDREN, CompoundArrayType(), &ArrayValue);
    if (SetResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
        return SetResult;
    }

    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        const TextComponent* Child = TextComponent_GetSubComponentAt(component, Index);
        GHDFCompound* ChildCompound = NULL;
        Error ChildResult = SerializeToCompound(Child, pool, &ChildCompound);
        if (ChildResult.Code != ErrorCode_Success)
        {
            return ChildResult;
        }
        GHDFObjectValue ChildValue = GHDFObjectValue_CreateCompound(ChildCompound);
        Error AddResult = GHDFArray_AddValue(Array, &ChildValue);
        if (AddResult.Code != ErrorCode_Success)
        {
            Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, ChildCompound, true);
            Error_Deconstruct(&ReturnResult);
            return AddResult;
        }
    }
    return Error_CreateSuccess();
}

static Error SerializeToCompound(const TextComponent* component, GHDFObjectPool* pool, GHDFCompound** outCompound)
{
    *outCompound = NULL;

    GHDFCompound* Compound = NULL;
    Error BorrowResult = GHDFObjectPool_BorrowCompound(pool, &Compound);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }

    Error Result = FillComponentBody(component, pool, Compound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = FillChildren(component, pool, Compound);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, Compound, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    *outCompound = Compound;
    return Error_CreateSuccess();
}


// Static functions (deserialize).
static Error CopyGHDFString(WRBufferPool* stringBufferPool, GenericBuffer* source, const unsigned char** outPointer)
{
    *outPointer = NULL;

    size_t Length = (source->_data != NULL) ? StringUTF8_GetByteLength(source->_data) : 0U;
    GenericBuffer* Destination = NULL;
    Error BorrowResult = BufferPool_Borrow(stringBufferPool, sizeof(unsigned char), &Destination);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }
    if ((Length > 0U) && !GenericBuffer_AppendRangeBytes(Destination, source->_data, Length))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentGHDF: could not copy a string value.");
    }
    if (!GenericBuffer_NullTerminate(Destination))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentGHDF: could not terminate a string value.");
    }
    *outPointer = Destination->_data;
    return Error_CreateSuccess();
}

static Error ReadOptionalFloat(GHDFCompound* compound, GHDFEntryID id, bool* outFound, float* outValue)
{
    GHDFObjectValue Entry;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_Float), &Entry, outFound);
    if ((Result.Code == ErrorCode_Success) && *outFound)
    {
        *outValue = Entry.Value.Float;
    }
    return Result;
}

static Error ReadOptionalBoolean(GHDFCompound* compound, GHDFEntryID id, bool* outFound, bool* outValue)
{
    GHDFObjectValue Entry;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_Boolean), &Entry, outFound);
    if ((Result.Code == ErrorCode_Success) && *outFound)
    {
        *outValue = Entry.Value.Boolean;
    }
    return Result;
}

static Error ReadOptionalUInt8(GHDFCompound* compound, GHDFEntryID id, bool* outFound, uint8_t* outValue)
{
    GHDFObjectValue Entry;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_UInt8), &Entry, outFound);
    if ((Result.Code == ErrorCode_Success) && *outFound)
    {
        *outValue = Entry.Value.UInt8;
    }
    return Result;
}

static Error ReadRenderColor(GHDFCompound* compound, GHDFEntryID id, RenderColor* outColor, bool* outFound)
{
    GHDFObjectValue Entry;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_Compound), &Entry, outFound);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }

    GHDFCompound* ColorCompound = Entry.Value.Compound;
    GHDFObjectValue TintValue;
    Result = GHDFCompound_GetVerified(ColorCompound, RC_TINT, GHDF_CreateRegularType(GHDFValueType_UInt32), &TintValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GHDFObjectValue BrightnessValue;
    Result = GHDFCompound_GetVerified(ColorCompound, RC_BRIGHTNESS, GHDF_CreateRegularType(GHDFValueType_Float), &BrightnessValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GHDFObjectValue OpacityValue;
    Result = GHDFCompound_GetVerified(ColorCompound, RC_OPACITY, GHDF_CreateRegularType(GHDFValueType_Float), &OpacityValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    uint32_t Packed = TintValue.Value.UInt32;
    outColor->Tint = (Color)
    {
        .r = (unsigned char)((Packed >> 24) & COLOR_CHANNEL_MASK),
        .g = (unsigned char)((Packed >> 16) & COLOR_CHANNEL_MASK),
        .b = (unsigned char)((Packed >> 8) & COLOR_CHANNEL_MASK),
        .a = (unsigned char)(Packed & COLOR_CHANNEL_MASK)
    };
    outColor->Brightness = BrightnessValue.Value.Float;
    outColor->Opacity = OpacityValue.Value.Float;
    return Error_CreateSuccess();
}

static Error ReadVector(GHDFCompound* compound, GHDFEntryID id, Vector2* outVector, bool* outFound)
{
    GHDFObjectValue Entry;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_Compound), &Entry, outFound);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }

    GHDFCompound* VectorCompound = Entry.Value.Compound;
    GHDFObjectValue XValue;
    Result = GHDFCompound_GetVerified(VectorCompound, VEC_X, GHDF_CreateRegularType(GHDFValueType_Float), &XValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GHDFObjectValue YValue;
    Result = GHDFCompound_GetVerified(VectorCompound, VEC_Y, GHDF_CreateRegularType(GHDFValueType_Float), &YValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    outVector->x = XValue.Value.Float;
    outVector->y = YValue.Value.Float;
    return Error_CreateSuccess();
}

static Error DeserializeStringComponent(GHDFCompound* compound, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent)
{
    GHDFObjectValue TextValue;
    Error Result = GHDFCompound_GetVerified(compound, FIELD_TEXT, GHDF_CreateRegularType(GHDFValueType_String), &TextValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const unsigned char* Text = NULL;
    Result = CopyGHDFString(stringBufferPool, TextValue.Value.String, &Text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    StringComponent* Component = NULL;
    Result = TextComponentFactory_CreateString(factory, Text, &Component);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    bool Found = false;

    {
        GHDFObjectValue FontValue;
        Result = GHDFCompound_GetOptionalVerified(compound, FIELD_FONT, GHDF_CreateRegularType(GHDFValueType_String), &FontValue, &Found);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found)
        {
            const unsigned char* FontName = NULL;
            Result = CopyGHDFString(stringBufferPool, FontValue.Value.String, &FontName);
            if (Result.Code != ErrorCode_Success)
            {
                goto Cleanup;
            }
            Result = StringComponent_SetFontName(Component, FontName);
            if (Result.Code != ErrorCode_Success)
            {
                goto Cleanup;
            }
        }
    }

    {
        RenderColor Color;
        Result = ReadRenderColor(compound, FIELD_COLOR, &Color, &Found);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found)
        {
            Result = StringComponent_SetColor(Component, Color);
            if (Result.Code != ErrorCode_Success)
            {
                goto Cleanup;
            }
        }
    }

    {
        float Size = 0.0f;
        Result = ReadOptionalFloat(compound, FIELD_SIZE, &Found, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found)
        {
            Result = StringComponent_SetSize(Component, Size);
            if (Result.Code != ErrorCode_Success)
            {
                goto Cleanup;
            }
        }
    }

    {
        bool ShadowActive = false;
        Result = ReadOptionalBoolean(compound, FIELD_SHADOW_ACTIVE, &Found, &ShadowActive);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found)
        {
            Component->IsShadowActive = ShadowActive;
        }
    }

    {
        uint8_t ShadowColorType = 0;
        Result = ReadOptionalUInt8(compound, FIELD_SHADOW_COLOR_TYPE, &Found, &ShadowColorType);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found && (ShadowColorType == (uint8_t)TextShadowColorType_Custom))
        {
            RenderColor ShadowColor;
            bool HasShadowColor = false;
            Result = ReadRenderColor(compound, FIELD_SHADOW_COLOR, &ShadowColor, &HasShadowColor);
            if (Result.Code != ErrorCode_Success)
            {
                goto Cleanup;
            }
            if (HasShadowColor)
            {
                Result = StringComponent_SetShadowColorCustom(Component, ShadowColor);
                if (Result.Code != ErrorCode_Success)
                {
                    goto Cleanup;
                }
            }
        }
    }

    {
        Vector2 Offset;
        Result = ReadVector(compound, FIELD_SHADOW_OFFSET, &Offset, &Found);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found)
        {
            Result = StringComponent_SetShadowOffset(Component, Offset);
            if (Result.Code != ErrorCode_Success)
            {
                goto Cleanup;
            }
        }
    }

    {
        bool Underlined = false;
        Result = ReadOptionalBoolean(compound, FIELD_UNDERLINE, &Found, &Underlined);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found)
        {
            Component->IsUnderlined = Underlined;
        }
    }

    {
        bool Strikethrough = false;
        Result = ReadOptionalBoolean(compound, FIELD_STRIKETHROUGH, &Found, &Strikethrough);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found)
        {
            Component->IsStrikethrough = Strikethrough;
        }
    }

    {
        float Spacing = 0.0f;
        Result = ReadOptionalFloat(compound, FIELD_SPACING, &Found, &Spacing);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found)
        {
            Result = StringComponent_SetSpacing(Component, Spacing);
            if (Result.Code != ErrorCode_Success)
            {
                goto Cleanup;
            }
        }
    }

    *outComponent = &Component->Base;
    return Error_CreateSuccess();

Cleanup:
    {
        Error ReturnResult = TextComponentFactory_ReturnComponentTree(factory, &Component->Base);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

static Error DeserializeSpriteComponent(GHDFCompound* compound, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent)
{
    SpriteComponent* Component = NULL;
    Error Result = TextComponentFactory_CreateSprite(factory, NULL, &Component);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    bool Found = false;

    {
        GHDFObjectValue AnimationValue;
        Result = GHDFCompound_GetOptionalVerified(compound, FIELD_ANIMATION, GHDF_CreateRegularType(GHDFValueType_String), &AnimationValue, &Found);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found)
        {
            const unsigned char* AnimationName = NULL;
            Result = CopyGHDFString(stringBufferPool, AnimationValue.Value.String, &AnimationName);
            if (Result.Code != ErrorCode_Success)
            {
                goto Cleanup;
            }
            Result = SpriteComponent_SetAnimationName(Component, AnimationName);
            if (Result.Code != ErrorCode_Success)
            {
                goto Cleanup;
            }
        }
    }

    {
        RenderColor Color;
        Result = ReadRenderColor(compound, FIELD_SPRITE_COLOR, &Color, &Found);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found)
        {
            Result = SpriteComponent_SetColor(Component, Color);
            if (Result.Code != ErrorCode_Success)
            {
                goto Cleanup;
            }
        }
    }

    {
        Vector2 Size;
        Result = ReadVector(compound, FIELD_SPRITE_SIZE, &Size, &Found);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
        if (Found)
        {
            Result = SpriteComponent_SetSize(Component, Size);
            if (Result.Code != ErrorCode_Success)
            {
                goto Cleanup;
            }
        }
    }

    *outComponent = &Component->Base;
    return Error_CreateSuccess();

Cleanup:
    {
        Error ReturnResult = TextComponentFactory_ReturnComponentTree(factory, &Component->Base);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

static Error DeserializeChildren(GHDFCompound* compound, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent* component)
{
    GHDFObjectValue ChildrenValue;
    bool Found = false;
    Error Result = GHDFCompound_GetOptionalVerified(compound, FIELD_CHILDREN, CompoundArrayType(), &ChildrenValue, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }

    GHDFArray* Array = ChildrenValue.Value.Array;
    size_t Count = GHDFArray_GetElementCount(Array);
    for (size_t Index = 0; Index < Count; Index++)
    {
        GHDFObjectValue Element;
        Result = GHDFArray_Get(Array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        TextComponent* Child = NULL;
        Result = DeserializeFromCompound(Element.Value.Compound, factory, stringBufferPool, &Child);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Result = TextComponent_AddSubComponent(component, Child);
        if (Result.Code != ErrorCode_Success)
        {
            Error ReturnResult = TextComponentFactory_ReturnComponentTree(factory, Child);
            Error_Deconstruct(&ReturnResult);
            return Result;
        }
    }
    return Error_CreateSuccess();
}

static Error DeserializeFromCompound(GHDFCompound* compound, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent)
{
    *outComponent = NULL;

    GHDFObjectValue TypeValue;
    Error Result = GHDFCompound_GetVerified(compound, FIELD_TYPE, GHDF_CreateRegularType(GHDFValueType_UInt8), &TypeValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint8_t Type = TypeValue.Value.UInt8;

    TextComponent* Component = NULL;
    if (Type == (uint8_t)TextComponentType_String)
    {
        Result = DeserializeStringComponent(compound, factory, stringBufferPool, &Component);
    }
    else if (Type == (uint8_t)TextComponentType_Sprite)
    {
        Result = DeserializeSpriteComponent(compound, factory, stringBufferPool, &Component);
    }
    else
    {
        EmptyComponent* Empty = NULL;
        Result = TextComponentFactory_CreateEmpty(factory, &Empty);
        Component = (Empty != NULL) ? &Empty->Base : NULL;
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = DeserializeChildren(compound, factory, stringBufferPool, Component);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = TextComponentFactory_ReturnComponentTree(factory, Component);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    *outComponent = Component;
    return Error_CreateSuccess();
}


// Public functions.
Error TextComponentGHDF_Serialize(const TextComponent* component, GHDFObjectPool* pool, GHDFCompound** outCompound)
{
    if ((component == NULL) || (pool == NULL) || (outCompound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentGHDF_Serialize: component, pool and outCompound must not be NULL.");
    }
    return SerializeToCompound(component, pool, outCompound);
}

Error TextComponentGHDF_Deserialize(GHDFCompound* compound, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent)
{
    if ((compound == NULL) || (factory == NULL) || (stringBufferPool == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentGHDF_Deserialize: compound, factory, stringBufferPool and outComponent must not be NULL.");
    }
    return DeserializeFromCompound(compound, factory, stringBufferPool, outComponent);
}
