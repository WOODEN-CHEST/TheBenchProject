#include "TextComponentJSON.h"
#include "GameJSON.h"
#include "wr/WRString.h"
#include "wr/WRMemory.h"


// Macros.
#define KEY_TYPE ((const unsigned char*)u8"type")
#define KEY_TEXT ((const unsigned char*)u8"text")
#define KEY_FONT ((const unsigned char*)u8"font")
#define KEY_COLOR ((const unsigned char*)u8"color")
#define KEY_SIZE ((const unsigned char*)u8"size")
#define KEY_SHADOW ((const unsigned char*)u8"shadow")
#define KEY_ACTIVE ((const unsigned char*)u8"active")
#define KEY_OFFSET ((const unsigned char*)u8"offset")
#define KEY_UNDERLINE ((const unsigned char*)u8"underline")
#define KEY_STRIKETHROUGH ((const unsigned char*)u8"strikethrough")
#define KEY_SPACING ((const unsigned char*)u8"spacing")
#define KEY_ANIMATION ((const unsigned char*)u8"animation")
#define KEY_CHILDREN ((const unsigned char*)u8"children")
#define KEY_TINT ((const unsigned char*)u8"tint")
#define KEY_BRIGHTNESS ((const unsigned char*)u8"brightness")
#define KEY_OPACITY ((const unsigned char*)u8"opacity")

#define TYPE_STRING ((const unsigned char*)u8"string")
#define TYPE_SPRITE ((const unsigned char*)u8"sprite")
#define TYPE_EMPTY ((const unsigned char*)u8"empty")
#define SHADOW_COLOR_DEFAULT ((const unsigned char*)u8"default")

#define COLOR_CHANNEL_MAX (255)


// Forward declaration for the recursion.
static Error SerializeComponent(const TextComponent* component, JSONObjectPool* pool, JSONObjectValue* outValue);
static Error DeserializeComponent(const JSONObjectValue* value, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent);


// Static functions (serialize).
static Error SetScalarEntry(JSONCompound* compound, const unsigned char* key, JSONObjectValue value)
{
    return JSONCompound_Set(compound, key, &value);
}

static Error AddScalarElement(JSONArray* array, JSONObjectValue value)
{
    return JSONArray_Add(array, &value);
}

// Borrows a string buffer, attaches it to the compound (so failures are cleaned by returning the compound),
// then fills it with the text.
static Error SetStringEntry(JSONObjectPool* pool, JSONCompound* compound, const unsigned char* key, const unsigned char* text)
{
    GenericBuffer* Buffer = NULL;
    Error BorrowResult = JSONObjectPool_BorrowString(pool, &Buffer);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }

    JSONObjectValue Value = JSONObjectValue_CreateString(Buffer);
    Error SetResult = JSONCompound_Set(compound, key, &Value);
    if (SetResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnString(pool, Buffer);
        Error_Deconstruct(&ReturnResult);
        return SetResult;
    }

    if (!GenericBuffer_AppendString(Buffer, text))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentJSON: could not write a string value.");
    }
    return Error_CreateSuccess();
}

static Error SetRenderColorEntry(JSONObjectPool* pool, JSONCompound* compound, const unsigned char* key, RenderColor color)
{
    JSONCompound* ColorCompound = NULL;
    Error BorrowResult = JSONObjectPool_BorrowCompound(pool, &ColorCompound);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }
    JSONObjectValue CompoundValue = JSONObjectValue_CreateCompound(ColorCompound);
    Error SetResult = JSONCompound_Set(compound, key, &CompoundValue);
    if (SetResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnCompound(pool, ColorCompound, true);
        Error_Deconstruct(&ReturnResult);
        return SetResult;
    }

    JSONArray* TintArray = NULL;
    Error ArrayResult = JSONObjectPool_BorrowArray(pool, &TintArray);
    if (ArrayResult.Code != ErrorCode_Success)
    {
        return ArrayResult;
    }
    JSONObjectValue ArrayValue = JSONObjectValue_CreateArray(TintArray);
    Error TintSetResult = JSONCompound_Set(ColorCompound, KEY_TINT, &ArrayValue);
    if (TintSetResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnArray(pool, TintArray, true);
        Error_Deconstruct(&ReturnResult);
        return TintSetResult;
    }

    Error Result = AddScalarElement(TintArray, JSONObjectValue_CreateInteger((int64_t)color.Tint.r));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = AddScalarElement(TintArray, JSONObjectValue_CreateInteger((int64_t)color.Tint.g));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = AddScalarElement(TintArray, JSONObjectValue_CreateInteger((int64_t)color.Tint.b));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = SetScalarEntry(ColorCompound, KEY_BRIGHTNESS, JSONObjectValue_CreateRealNumber((double)color.Brightness));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetScalarEntry(ColorCompound, KEY_OPACITY, JSONObjectValue_CreateRealNumber((double)color.Opacity));
}

static Error SetVector2Entry(JSONObjectPool* pool, JSONCompound* compound, const unsigned char* key, Vector2 vector)
{
    JSONArray* Array = NULL;
    Error BorrowResult = JSONObjectPool_BorrowArray(pool, &Array);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }
    JSONObjectValue ArrayValue = JSONObjectValue_CreateArray(Array);
    Error SetResult = JSONCompound_Set(compound, key, &ArrayValue);
    if (SetResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
        return SetResult;
    }

    Error Result = AddScalarElement(Array, JSONObjectValue_CreateRealNumber((double)vector.x));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return AddScalarElement(Array, JSONObjectValue_CreateRealNumber((double)vector.y));
}

static Error SetShadowEntry(JSONObjectPool* pool, JSONCompound* compound, const StringComponent* stringComponent)
{
    JSONCompound* ShadowCompound = NULL;
    Error BorrowResult = JSONObjectPool_BorrowCompound(pool, &ShadowCompound);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }
    JSONObjectValue CompoundValue = JSONObjectValue_CreateCompound(ShadowCompound);
    Error SetResult = JSONCompound_Set(compound, KEY_SHADOW, &CompoundValue);
    if (SetResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnCompound(pool, ShadowCompound, true);
        Error_Deconstruct(&ReturnResult);
        return SetResult;
    }

    Error Result = SetScalarEntry(ShadowCompound, KEY_ACTIVE, JSONObjectValue_CreateBoolean(stringComponent->IsShadowActive));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (stringComponent->_shadowColorType == TextShadowColorType_Custom)
    {
        Result = SetRenderColorEntry(pool, ShadowCompound, KEY_COLOR, stringComponent->_shadowColor);
    }
    else
    {
        Result = SetStringEntry(pool, ShadowCompound, KEY_COLOR, SHADOW_COLOR_DEFAULT);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return SetVector2Entry(pool, ShadowCompound, KEY_OFFSET, stringComponent->_shadowOffset);
}

static Error FillStringComponent(JSONObjectPool* pool, JSONCompound* compound, const StringComponent* stringComponent)
{
    Error Result = SetStringEntry(pool, compound, KEY_TYPE, TYPE_STRING);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetStringEntry(pool, compound, KEY_TEXT,
        (stringComponent->_text != NULL) ? stringComponent->_text : (const unsigned char*)u8"");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (stringComponent->_fontName != NULL)
    {
        Result = SetStringEntry(pool, compound, KEY_FONT, stringComponent->_fontName);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Result = SetRenderColorEntry(pool, compound, KEY_COLOR, stringComponent->_color);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetScalarEntry(compound, KEY_SIZE, JSONObjectValue_CreateRealNumber((double)stringComponent->_size));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetShadowEntry(pool, compound, stringComponent);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetScalarEntry(compound, KEY_UNDERLINE, JSONObjectValue_CreateBoolean(stringComponent->IsUnderlined));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetScalarEntry(compound, KEY_STRIKETHROUGH, JSONObjectValue_CreateBoolean(stringComponent->IsStrikethrough));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetScalarEntry(compound, KEY_SPACING, JSONObjectValue_CreateRealNumber((double)stringComponent->_spacing));
}

static Error FillSpriteComponent(JSONObjectPool* pool, JSONCompound* compound, const SpriteComponent* spriteComponent)
{
    Error Result = SetStringEntry(pool, compound, KEY_TYPE, TYPE_SPRITE);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (spriteComponent->_animationName != NULL)
    {
        Result = SetStringEntry(pool, compound, KEY_ANIMATION, spriteComponent->_animationName);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Result = SetRenderColorEntry(pool, compound, KEY_COLOR, spriteComponent->_color);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetVector2Entry(pool, compound, KEY_SIZE, spriteComponent->_size);
}

static Error FillComponentBody(const TextComponent* component, JSONObjectPool* pool, JSONCompound* compound)
{
    switch (component->Type)
    {
        case TextComponentType_String:
            return FillStringComponent(pool, compound, (const StringComponent*)component);
        case TextComponentType_Sprite:
            return FillSpriteComponent(pool, compound, (const SpriteComponent*)component);
        case TextComponentType_Empty:
        default:
            return SetStringEntry(pool, compound, KEY_TYPE, TYPE_EMPTY);
    }
}

static Error FillChildren(const TextComponent* component, JSONObjectPool* pool, JSONCompound* compound)
{
    size_t ChildCount = TextComponent_GetSubComponentCount(component);
    if (ChildCount == 0)
    {
        return Error_CreateSuccess();
    }

    JSONArray* Array = NULL;
    Error BorrowResult = JSONObjectPool_BorrowArray(pool, &Array);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }
    JSONObjectValue ArrayValue = JSONObjectValue_CreateArray(Array);
    Error SetResult = JSONCompound_Set(compound, KEY_CHILDREN, &ArrayValue);
    if (SetResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
        return SetResult;
    }

    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        const TextComponent* Child = TextComponent_GetSubComponentAt(component, Index);
        JSONObjectValue ChildValue = JSONObjectValue_CreateNull();
        Error ChildResult = SerializeComponent(Child, pool, &ChildValue);
        if (ChildResult.Code != ErrorCode_Success)
        {
            return ChildResult;
        }
        Error AddResult = JSONArray_Add(Array, &ChildValue);
        if (AddResult.Code != ErrorCode_Success)
        {
            Error ReturnResult = JSONObjectPool_ReturnValue(pool, &ChildValue);
            Error_Deconstruct(&ReturnResult);
            return AddResult;
        }
    }
    return Error_CreateSuccess();
}

static Error SerializeComponent(const TextComponent* component, JSONObjectPool* pool, JSONObjectValue* outValue)
{
    *outValue = JSONObjectValue_CreateNull();

    JSONCompound* Compound = NULL;
    Error BorrowResult = JSONObjectPool_BorrowCompound(pool, &Compound);
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
        Error ReturnResult = JSONObjectPool_ReturnCompound(pool, Compound, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    *outValue = JSONObjectValue_CreateCompound(Compound);
    return Error_CreateSuccess();
}


// Static functions (deserialize).
// Copies a JSON string value's bytes into a byte buffer borrowed from the pool and returns a stable,
// NUL-terminated pointer into it.
static Error CopyJSONStringValue(WRBufferPool* stringBufferPool, const JSONObjectValue* stringValue, const unsigned char** outPointer)
{
    *outPointer = NULL;

    GenericBuffer* Source = stringValue->Value.String;
    size_t Length = (Source != NULL) ? Source->_count : 0U;

    GenericBuffer* Destination = NULL;
    Error BorrowResult = BufferPool_Borrow(stringBufferPool, sizeof(unsigned char), &Destination);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        return BorrowResult;
    }
    if ((Length > 0U) && !GenericBuffer_AppendRangeBytes(Destination, Source->_data, Length))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentJSON: could not copy a string value.");
    }
    if (!GenericBuffer_NullTerminate(Destination))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentJSON: could not terminate a string value.");
    }
    *outPointer = Destination->_data;
    return Error_CreateSuccess();
}

static Error JSONStringEquals(const JSONObjectValue* stringValue, const unsigned char* literal, bool* outEqual)
{
    *outEqual = false;
    unsigned char* Owned = NULL;
    Error OwnResult = GameJSON_ValueToOwnedString(stringValue, &Owned);
    if (OwnResult.Code != ErrorCode_Success)
    {
        return OwnResult;
    }
    Error CompareResult = StringUTF8_EqualsExact(Owned, literal, outEqual);
    Memory_Free(Owned);
    return CompareResult;
}

static Error DeserializeShadow(JSONCompound* shadowCompound, StringComponent* stringComponent)
{
    Error Result = GameJSON_ReadOptionalBoolean(shadowCompound, KEY_ACTIVE, stringComponent->IsShadowActive,
        &stringComponent->IsShadowActive);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    JSONObjectValue ColorValue;
    bool HasColor = false;
    Result = JSONCompound_GetOptional(shadowCompound, KEY_COLOR, &ColorValue, &HasColor);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (HasColor)
    {
        bool IsDefault = false;
        if (ColorValue.Type == JSONValueType_String)
        {
            Result = JSONStringEquals(&ColorValue, SHADOW_COLOR_DEFAULT, &IsDefault);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }

        if (IsDefault)
        {
            Result = StringComponent_SetShadowColorDefault(stringComponent);
        }
        else
        {
            RenderColor ShadowColor;
            Result = GameJSON_ParseRenderColor(&ColorValue, &ShadowColor);
            if (Result.Code == ErrorCode_Success)
            {
                Result = StringComponent_SetShadowColorCustom(stringComponent, ShadowColor);
            }
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    JSONObjectValue OffsetValue;
    bool HasOffset = false;
    Result = JSONCompound_GetOptional(shadowCompound, KEY_OFFSET, &OffsetValue, &HasOffset);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (HasOffset)
    {
        Vector2 Offset;
        Result = GameJSON_ParseVector2(&OffsetValue, &Offset);
        if (Result.Code == ErrorCode_Success)
        {
            Result = StringComponent_SetShadowOffset(stringComponent, Offset);
        }
    }
    return Result;
}

static Error DeserializeStringComponent(JSONCompound* compound, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent)
{
    JSONObjectValue TextValue;
    Error Result = JSONCompound_GetVerified(compound, KEY_TEXT, JSONValueType_String, &TextValue);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_Construct2(ErrorCode_InvalidJSON, "TextComponentJSON: a string component requires a string \"text\".");
    }
    const unsigned char* Text = NULL;
    Result = CopyJSONStringValue(stringBufferPool, &TextValue, &Text);
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

    JSONObjectValue Value;
    bool Found = false;

    Result = JSONCompound_GetOptionalVerified(compound, KEY_FONT, JSONValueType_String, &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        goto Cleanup;
    }
    if (Found)
    {
        const unsigned char* FontName = NULL;
        Result = CopyJSONStringValue(stringBufferPool, &Value, &FontName);
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

    Result = JSONCompound_GetOptional(compound, KEY_COLOR, &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        goto Cleanup;
    }
    if (Found)
    {
        RenderColor Color;
        Result = GameJSON_ParseRenderColor(&Value, &Color);
        if (Result.Code == ErrorCode_Success)
        {
            Result = StringComponent_SetColor(Component, Color);
        }
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
    }

    Result = JSONCompound_GetOptional(compound, KEY_SIZE, &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        goto Cleanup;
    }
    if (Found)
    {
        double Size = 0.0;
        Result = GameJSON_ValueToDouble(&Value, &Size);
        if (Result.Code == ErrorCode_Success)
        {
            Result = StringComponent_SetSize(Component, (float)Size);
        }
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
    }

    Result = JSONCompound_GetOptionalVerified(compound, KEY_SHADOW, JSONValueType_Compound, &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        goto Cleanup;
    }
    if (Found)
    {
        Result = DeserializeShadow(Value.Value.Compound, Component);
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
    }

    Result = GameJSON_ReadOptionalBoolean(compound, KEY_UNDERLINE, Component->IsUnderlined, &Component->IsUnderlined);
    if (Result.Code != ErrorCode_Success)
    {
        goto Cleanup;
    }
    Result = GameJSON_ReadOptionalBoolean(compound, KEY_STRIKETHROUGH, Component->IsStrikethrough, &Component->IsStrikethrough);
    if (Result.Code != ErrorCode_Success)
    {
        goto Cleanup;
    }

    Result = JSONCompound_GetOptional(compound, KEY_SPACING, &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        goto Cleanup;
    }
    if (Found)
    {
        double Spacing = 0.0;
        Result = GameJSON_ValueToDouble(&Value, &Spacing);
        if (Result.Code == ErrorCode_Success)
        {
            Result = StringComponent_SetSpacing(Component, (float)Spacing);
        }
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
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

static Error DeserializeSpriteComponent(JSONCompound* compound, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent)
{
    SpriteComponent* Component = NULL;
    Error Result = TextComponentFactory_CreateSprite(factory, NULL, &Component);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    JSONObjectValue Value;
    bool Found = false;

    Result = JSONCompound_GetOptionalVerified(compound, KEY_ANIMATION, JSONValueType_String, &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        goto Cleanup;
    }
    if (Found)
    {
        const unsigned char* AnimationName = NULL;
        Result = CopyJSONStringValue(stringBufferPool, &Value, &AnimationName);
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

    Result = JSONCompound_GetOptional(compound, KEY_COLOR, &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        goto Cleanup;
    }
    if (Found)
    {
        RenderColor Color;
        Result = GameJSON_ParseRenderColor(&Value, &Color);
        if (Result.Code == ErrorCode_Success)
        {
            Result = SpriteComponent_SetColor(Component, Color);
        }
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
        }
    }

    Result = JSONCompound_GetOptional(compound, KEY_SIZE, &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        goto Cleanup;
    }
    if (Found)
    {
        Vector2 Size;
        Result = GameJSON_ParseVector2(&Value, &Size);
        if (Result.Code == ErrorCode_Success)
        {
            Result = SpriteComponent_SetSize(Component, Size);
        }
        if (Result.Code != ErrorCode_Success)
        {
            goto Cleanup;
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

static Error DeserializeChildren(JSONCompound* compound, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent* component)
{
    JSONObjectValue ChildrenValue;
    bool Found = false;
    Error Result = JSONCompound_GetOptionalVerified(compound, KEY_CHILDREN, JSONValueType_Array, &ChildrenValue, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }

    JSONArray* Array = ChildrenValue.Value.Array;
    size_t Count = JSONArray_GetElementCount(Array);
    for (size_t Index = 0; Index < Count; Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_Get(Array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        TextComponent* Child = NULL;
        Result = DeserializeComponent(&Element, factory, stringBufferPool, &Child);
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

static Error DeserializeObject(JSONCompound* compound, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent)
{
    JSONObjectValue TypeValue;
    Error Result = JSONCompound_GetVerified(compound, KEY_TYPE, JSONValueType_String, &TypeValue);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_Construct2(ErrorCode_InvalidJSON, "TextComponentJSON: a component object requires a string \"type\".");
    }

    bool IsString = false;
    bool IsSprite = false;
    bool IsEmpty = false;
    Result = JSONStringEquals(&TypeValue, TYPE_STRING, &IsString);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONStringEquals(&TypeValue, TYPE_SPRITE, &IsSprite);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONStringEquals(&TypeValue, TYPE_EMPTY, &IsEmpty);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    TextComponent* Component = NULL;
    if (IsString)
    {
        Result = DeserializeStringComponent(compound, factory, stringBufferPool, &Component);
    }
    else if (IsSprite)
    {
        Result = DeserializeSpriteComponent(compound, factory, stringBufferPool, &Component);
    }
    else if (IsEmpty)
    {
        EmptyComponent* Empty = NULL;
        Result = TextComponentFactory_CreateEmpty(factory, &Empty);
        Component = (Empty != NULL) ? &Empty->Base : NULL;
    }
    else
    {
        return Error_Construct2(ErrorCode_InvalidJSON,
            "TextComponentJSON: component \"type\" must be \"string\", \"sprite\" or \"empty\".");
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

static Error DeserializeArrayShorthand(JSONArray* array, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent)
{
    EmptyComponent* Empty = NULL;
    Error Result = TextComponentFactory_CreateEmpty(factory, &Empty);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    size_t Count = JSONArray_GetElementCount(array);
    for (size_t Index = 0; Index < Count; Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_Get(array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        TextComponent* Child = NULL;
        Result = DeserializeComponent(&Element, factory, stringBufferPool, &Child);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        Result = TextComponent_AddSubComponent(&Empty->Base, Child);
        if (Result.Code != ErrorCode_Success)
        {
            Error ReturnResult = TextComponentFactory_ReturnComponentTree(factory, Child);
            Error_Deconstruct(&ReturnResult);
            break;
        }
    }

    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = TextComponentFactory_ReturnComponentTree(factory, &Empty->Base);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    *outComponent = &Empty->Base;
    return Error_CreateSuccess();
}

static Error DeserializeComponent(const JSONObjectValue* value, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent)
{
    *outComponent = NULL;

    if (value->Type == JSONValueType_Array)
    {
        return DeserializeArrayShorthand(value->Value.Array, factory, stringBufferPool, outComponent);
    }
    if (value->Type == JSONValueType_String)
    {
        const unsigned char* Text = NULL;
        Error CopyResult = CopyJSONStringValue(stringBufferPool, value, &Text);
        if (CopyResult.Code != ErrorCode_Success)
        {
            return CopyResult;
        }
        StringComponent* Component = NULL;
        Error CreateResult = TextComponentFactory_CreateString(factory, Text, &Component);
        if (CreateResult.Code != ErrorCode_Success)
        {
            return CreateResult;
        }
        *outComponent = &Component->Base;
        return Error_CreateSuccess();
    }
    if (value->Type == JSONValueType_Compound)
    {
        return DeserializeObject(value->Value.Compound, factory, stringBufferPool, outComponent);
    }

    return Error_Construct2(ErrorCode_InvalidJSON,
        "TextComponentJSON: a component must be an object, an array or a string.");
}


// Public functions.
Error TextComponentJSON_Serialize(const TextComponent* component, JSONObjectPool* pool, JSONObjectValue* outValue)
{
    if ((component == NULL) || (pool == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentJSON_Serialize: component, pool and outValue must not be NULL.");
    }
    return SerializeComponent(component, pool, outValue);
}

Error TextComponentJSON_Deserialize(const JSONObjectValue* value, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent)
{
    if ((value == NULL) || (factory == NULL) || (stringBufferPool == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentJSON_Deserialize: value, factory, stringBufferPool and outComponent must not be NULL.");
    }
    return DeserializeComponent(value, factory, stringBufferPool, outComponent);
}
