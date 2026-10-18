#include <inttypes.h>
#include <math.h>
#include "TextComponentCodec.h"
#include "AssetManager.h"
#include "wr/WRString.h"
#include "wr/WRMemory.h"


// Macros.
/* GHDF wire ids of the component types: explicit constants, so reordering the enum never changes saved data. */
#define TEXT_CODEC_WIRE_TYPE_EMPTY ((uint8_t)1)
#define TEXT_CODEC_WIRE_TYPE_STRING ((uint8_t)2)
#define TEXT_CODEC_WIRE_TYPE_SPRITE ((uint8_t)3)
#define TEXT_CODEC_WIRE_TYPE_TRANSLATE ((uint8_t)4)

/* GHDF entry ids of the fields inside a component compound (ids 1 and 2, the type and the children, belong to the
 * adapter). Id 15 held the shadow color type in the first layout; a present shadow color (16) now means custom, and
 * likewise a present shadow offset (17). */
#define TEXT_CODEC_GHDF_TEXT ((GHDFEntryID)10)
#define TEXT_CODEC_GHDF_FONT ((GHDFEntryID)11)
#define TEXT_CODEC_GHDF_COLOR ((GHDFEntryID)12)
#define TEXT_CODEC_GHDF_SIZE ((GHDFEntryID)13)
#define TEXT_CODEC_GHDF_SHADOW_ACTIVE ((GHDFEntryID)14)
#define TEXT_CODEC_GHDF_SHADOW_COLOR ((GHDFEntryID)16)
#define TEXT_CODEC_GHDF_SHADOW_OFFSET ((GHDFEntryID)17)
#define TEXT_CODEC_GHDF_UNDERLINE ((GHDFEntryID)18)
#define TEXT_CODEC_GHDF_STRIKETHROUGH ((GHDFEntryID)19)
#define TEXT_CODEC_GHDF_SPACING ((GHDFEntryID)20)
#define TEXT_CODEC_GHDF_KEY ((GHDFEntryID)21)
#define TEXT_CODEC_GHDF_ANIMATION ((GHDFEntryID)30)
#define TEXT_CODEC_GHDF_SPRITE_COLOR ((GHDFEntryID)31)
#define TEXT_CODEC_GHDF_SPRITE_SIZE ((GHDFEntryID)32)

#define TEXT_CODEC_SHADOW_GROUP (u8"shadow")
#define TEXT_CODEC_COLOR_CHANNEL_MAX (255U)

/* The styling fields every string-bearing component (string and translate) shares; listed once here. */
#define TEXT_CODEC_STRING_STYLE_FIELDS \
    { TextCodecFieldId_Font, TextCodecValueKind_Name, NULL, u8"font", u8"font", TEXT_CODEC_GHDF_FONT, false, 0.0f, 0.0f }, \
    { TextCodecFieldId_Color, TextCodecValueKind_RenderColor, NULL, u8"color", u8"color", TEXT_CODEC_GHDF_COLOR, false, \
        0.0f, 0.0f }, \
    { TextCodecFieldId_Size, TextCodecValueKind_Decimal, NULL, u8"size", u8"size", TEXT_CODEC_GHDF_SIZE, false, 0.0f, \
        TEXT_COMPONENT_SIZE_MAX }, \
    { TextCodecFieldId_ShadowActive, TextCodecValueKind_Boolean, TEXT_CODEC_SHADOW_GROUP, u8"active", u8"shadow.active", \
        TEXT_CODEC_GHDF_SHADOW_ACTIVE, false, 0.0f, 0.0f }, \
    { TextCodecFieldId_ShadowColor, TextCodecValueKind_ShadowColor, TEXT_CODEC_SHADOW_GROUP, u8"color", u8"shadow.color", \
        TEXT_CODEC_GHDF_SHADOW_COLOR, false, 0.0f, 0.0f }, \
    { TextCodecFieldId_ShadowOffset, TextCodecValueKind_ShadowOffset, TEXT_CODEC_SHADOW_GROUP, u8"offset", u8"shadow.offset", \
        TEXT_CODEC_GHDF_SHADOW_OFFSET, false, -STRING_COMPONENT_SHADOW_OFFSET_LIMIT, STRING_COMPONENT_SHADOW_OFFSET_LIMIT }, \
    { TextCodecFieldId_Underline, TextCodecValueKind_Boolean, NULL, u8"underline", u8"underline", TEXT_CODEC_GHDF_UNDERLINE, \
        false, 0.0f, 0.0f }, \
    { TextCodecFieldId_Strikethrough, TextCodecValueKind_Boolean, NULL, u8"strikethrough", u8"strikethrough", \
        TEXT_CODEC_GHDF_STRIKETHROUGH, false, 0.0f, 0.0f }, \
    { TextCodecFieldId_Spacing, TextCodecValueKind_Decimal, NULL, u8"spacing", u8"spacing", TEXT_CODEC_GHDF_SPACING, false, \
        STRING_COMPONENT_SPACING_MIN, STRING_COMPONENT_SPACING_MAX }


// Types.
typedef struct TextCodecTypeStruct
{
    TextComponentType Type;
    const unsigned char* JsonName;
    uint8_t WireId;
    const TextCodecField* Fields;
    size_t FieldCount;
} TextCodecType;

typedef struct DecodeContextStruct
{
    ITextCodecReader Reader;
    ErrorCode FailureCode;
    TextComponentFactory* Factory;
    /* Level of the node being decoded: 1 for the root. */
    size_t Depth;
    /* For the node at level L >= 2: the child index taken in its parent (PathIndices[L - 2]) and whether the parent
     * was a list shorthand (an entry "/<i>") rather than an object (a child "/children/<i>"). */
    size_t PathIndices[TEXT_COMPONENT_MAX_DEPTH];
    bool PathIsListEntry[TEXT_COMPONENT_MAX_DEPTH];
    /* Scratch for error messages. */
    GenericBuffer Message;
} DecodeContext;

typedef struct EncodeContextStruct
{
    ITextCodecWriter Writer;
    bool AreShorthandsSupported;
} EncodeContext;


// Fields.
static const TextCodecField StringFields[] =
{
    { TextCodecFieldId_Text, TextCodecValueKind_Text, NULL, u8"text", u8"text", TEXT_CODEC_GHDF_TEXT, true, 0.0f, 0.0f },
    TEXT_CODEC_STRING_STYLE_FIELDS
};

/* The key comes first: a translate component is created with it before the other fields are applied. */
static const TextCodecField TranslateFields[] =
{
    { TextCodecFieldId_Key, TextCodecValueKind_Name, NULL, u8"key", u8"key", TEXT_CODEC_GHDF_KEY, true, 0.0f, 0.0f },
    { TextCodecFieldId_Fallback, TextCodecValueKind_Text, NULL, u8"fallback", u8"fallback", TEXT_CODEC_GHDF_TEXT, false,
        0.0f, 0.0f },
    TEXT_CODEC_STRING_STYLE_FIELDS
};

static const TextCodecField SpriteFields[] =
{
    { TextCodecFieldId_Animation, TextCodecValueKind_Name, NULL, u8"animation", u8"animation", TEXT_CODEC_GHDF_ANIMATION,
        false, 0.0f, 0.0f },
    { TextCodecFieldId_SpriteColor, TextCodecValueKind_RenderColor, NULL, u8"color", u8"color", TEXT_CODEC_GHDF_SPRITE_COLOR,
        false, 0.0f, 0.0f },
    { TextCodecFieldId_SpriteSize, TextCodecValueKind_Vector2, NULL, u8"size", u8"size", TEXT_CODEC_GHDF_SPRITE_SIZE, false,
        0.0f, TEXT_COMPONENT_SIZE_MAX }
};

static const TextCodecType Types[] =
{
    { TextComponentType_Empty, u8"empty", TEXT_CODEC_WIRE_TYPE_EMPTY, NULL, 0U },
    { TextComponentType_String, u8"string", TEXT_CODEC_WIRE_TYPE_STRING, StringFields,
        sizeof(StringFields) / sizeof(StringFields[0]) },
    { TextComponentType_Sprite, u8"sprite", TEXT_CODEC_WIRE_TYPE_SPRITE, SpriteFields,
        sizeof(SpriteFields) / sizeof(SpriteFields[0]) },
    { TextComponentType_Translate, u8"translate", TEXT_CODEC_WIRE_TYPE_TRANSLATE, TranslateFields,
        sizeof(TranslateFields) / sizeof(TranslateFields[0]) }
};

#define TEXT_CODEC_TYPE_COUNT (sizeof(Types) / sizeof(Types[0]))


// Static functions: the type table.
// The switch has no default, so a new component type fails the build here until it has a table entry.
static const TextCodecType* GetTypeDescriptor(TextComponentType type)
{
    switch (type)
    {
        case TextComponentType_Empty:
            return &Types[0];
        case TextComponentType_String:
            return &Types[1];
        case TextComponentType_Sprite:
            return &Types[2];
        case TextComponentType_Translate:
            return &Types[3];
    }
    return NULL;
}


// Static functions: reading and writing component properties by field.
static bool IsFloatInRange(float value, float minimum, float maximum)
{
    return isfinite((double)value) && (value >= minimum) && (value <= maximum);
}

static bool IsRenderColorValid(RenderColor color)
{
    return IsFloatInRange(color.Brightness, RENDER_COLOR_BRIGHTNESS_MIN, RENDER_COLOR_BRIGHTNESS_MAX)
        && IsFloatInRange(color.Opacity, RENDER_COLOR_OPACITY_MIN, RENDER_COLOR_OPACITY_MAX);
}

// The tint's alpha is ignored (opacity carries alpha), so it does not count.
static bool IsDefaultColor(RenderColor color)
{
    return (color.Tint.r == TEXT_CODEC_COLOR_CHANNEL_MAX) && (color.Tint.g == TEXT_CODEC_COLOR_CHANNEL_MAX)
        && (color.Tint.b == TEXT_CODEC_COLOR_CHANNEL_MAX) && (color.Brightness == 1.0f) && (color.Opacity == 1.0f);
}

static Error FieldMismatchError(void)
{
    return Error_Construct2(ErrorCode_InvalidState, "TextComponentCodec: a field does not belong to its component's type.");
}

static Error GetFieldValue(const TextComponent* component, TextCodecFieldId id, TextCodecValue* outValue)
{
    const StringComponent* String = TextComponent_AsConstStringComponent(component);
    const SpriteComponent* Sprite = TextComponent_AsConstSpriteComponent(component);
    const TranslateComponent* Translate = TextComponent_AsConstTranslateComponent(component);
    *outValue = (TextCodecValue){ 0 };

    switch (id)
    {
        case TextCodecFieldId_Text:
        case TextCodecFieldId_Fallback:
            if (String == NULL)
            {
                break;
            }
            outValue->Text = StringComponent_GetText(String);
            return Error_CreateSuccess();
        case TextCodecFieldId_Key:
            if (Translate == NULL)
            {
                break;
            }
            outValue->Text = TranslateComponent_GetKey(Translate);
            return Error_CreateSuccess();
        case TextCodecFieldId_Font:
            if (String == NULL)
            {
                break;
            }
            outValue->Text = StringComponent_GetFontName(String);
            return Error_CreateSuccess();
        case TextCodecFieldId_Color:
            if (String == NULL)
            {
                break;
            }
            outValue->Color = StringComponent_GetColor(String);
            return Error_CreateSuccess();
        case TextCodecFieldId_Size:
            if (String == NULL)
            {
                break;
            }
            outValue->Decimal = StringComponent_GetSize(String);
            return Error_CreateSuccess();
        case TextCodecFieldId_ShadowActive:
            if (String == NULL)
            {
                break;
            }
            outValue->Boolean = StringComponent_IsShadowActive(String);
            return Error_CreateSuccess();
        case TextCodecFieldId_ShadowColor:
            if (String == NULL)
            {
                break;
            }
            outValue->IsCustom = (StringComponent_GetShadowColorType(String) == TextShadowColorType_Custom);
            outValue->Color = StringComponent_GetShadowColor(String);
            return Error_CreateSuccess();
        case TextCodecFieldId_ShadowOffset:
            if (String == NULL)
            {
                break;
            }
            outValue->IsCustom = (StringComponent_GetShadowOffsetType(String) == TextShadowOffsetType_Custom);
            outValue->Vector = StringComponent_GetShadowOffset(String);
            return Error_CreateSuccess();
        case TextCodecFieldId_Underline:
            if (String == NULL)
            {
                break;
            }
            outValue->Boolean = StringComponent_IsUnderlined(String);
            return Error_CreateSuccess();
        case TextCodecFieldId_Strikethrough:
            if (String == NULL)
            {
                break;
            }
            outValue->Boolean = StringComponent_IsStrikethrough(String);
            return Error_CreateSuccess();
        case TextCodecFieldId_Spacing:
            if (String == NULL)
            {
                break;
            }
            outValue->Decimal = StringComponent_GetSpacing(String);
            return Error_CreateSuccess();
        case TextCodecFieldId_Animation:
            if (Sprite == NULL)
            {
                break;
            }
            outValue->Text = SpriteComponent_GetAnimationName(Sprite);
            return Error_CreateSuccess();
        case TextCodecFieldId_SpriteColor:
            if (Sprite == NULL)
            {
                break;
            }
            outValue->Color = SpriteComponent_GetColor(Sprite);
            return Error_CreateSuccess();
        case TextCodecFieldId_SpriteSize:
            if (Sprite == NULL)
            {
                break;
            }
            outValue->Vector = SpriteComponent_GetSize(Sprite);
            return Error_CreateSuccess();
    }
    return FieldMismatchError();
}

static Error SetFieldValue(TextComponent* component, TextCodecFieldId id, const TextCodecValue* value)
{
    StringComponent* String = TextComponent_AsStringComponent(component);
    SpriteComponent* Sprite = TextComponent_AsSpriteComponent(component);
    TranslateComponent* Translate = TextComponent_AsTranslateComponent(component);

    switch (id)
    {
        case TextCodecFieldId_Text:
        case TextCodecFieldId_Fallback:
            if (String == NULL)
            {
                break;
            }
            return StringComponent_SetText(String, value->Text);
        case TextCodecFieldId_Key:
            if (Translate == NULL)
            {
                break;
            }
            return TranslateComponent_SetKey(Translate, value->Text);
        case TextCodecFieldId_Font:
            if (String == NULL)
            {
                break;
            }
            return StringComponent_SetFontName(String, value->Text);
        case TextCodecFieldId_Color:
            if (String == NULL)
            {
                break;
            }
            return StringComponent_SetColor(String, value->Color);
        case TextCodecFieldId_Size:
            if (String == NULL)
            {
                break;
            }
            return StringComponent_SetSize(String, value->Decimal);
        case TextCodecFieldId_ShadowActive:
            if (String == NULL)
            {
                break;
            }
            return StringComponent_SetIsShadowActive(String, value->Boolean);
        case TextCodecFieldId_ShadowColor:
            if (String == NULL)
            {
                break;
            }
            return value->IsCustom ? StringComponent_SetShadowColorCustom(String, value->Color)
                : StringComponent_SetShadowColorDefault(String);
        case TextCodecFieldId_ShadowOffset:
            if (String == NULL)
            {
                break;
            }
            return value->IsCustom ? StringComponent_SetShadowOffsetCustom(String, value->Vector)
                : StringComponent_SetShadowOffsetDefault(String);
        case TextCodecFieldId_Underline:
            if (String == NULL)
            {
                break;
            }
            return StringComponent_SetIsUnderlined(String, value->Boolean);
        case TextCodecFieldId_Strikethrough:
            if (String == NULL)
            {
                break;
            }
            return StringComponent_SetIsStrikethrough(String, value->Boolean);
        case TextCodecFieldId_Spacing:
            if (String == NULL)
            {
                break;
            }
            return StringComponent_SetSpacing(String, value->Decimal);
        case TextCodecFieldId_Animation:
            if (Sprite == NULL)
            {
                break;
            }
            return SpriteComponent_SetAnimationName(Sprite, value->Text);
        case TextCodecFieldId_SpriteColor:
            if (Sprite == NULL)
            {
                break;
            }
            return SpriteComponent_SetColor(Sprite, value->Color);
        case TextCodecFieldId_SpriteSize:
            if (Sprite == NULL)
            {
                break;
            }
            return SpriteComponent_SetSize(Sprite, value->Vector);
    }
    return FieldMismatchError();
}

static bool IsFieldDefault(TextCodecFieldId id, const TextCodecValue* value)
{
    switch (id)
    {
        case TextCodecFieldId_Text:
        case TextCodecFieldId_Fallback:
            return value->Text[0] == (unsigned char)0;
        case TextCodecFieldId_Key:
            return false;
        case TextCodecFieldId_Font:
        case TextCodecFieldId_Animation:
            return value->Text == NULL;
        case TextCodecFieldId_Color:
        case TextCodecFieldId_SpriteColor:
            return IsDefaultColor(value->Color);
        case TextCodecFieldId_Size:
            return value->Decimal == STRING_COMPONENT_DEFAULT_SIZE;
        case TextCodecFieldId_ShadowActive:
            return value->Boolean == STRING_COMPONENT_DEFAULT_IS_SHADOW_ACTIVE;
        case TextCodecFieldId_ShadowColor:
        case TextCodecFieldId_ShadowOffset:
            return !value->IsCustom;
        case TextCodecFieldId_Underline:
        case TextCodecFieldId_Strikethrough:
            return !value->Boolean;
        case TextCodecFieldId_Spacing:
            return value->Decimal == STRING_COMPONENT_DEFAULT_SPACING;
        case TextCodecFieldId_SpriteSize:
            return (value->Vector.x == 0.0f) && (value->Vector.y == 0.0f);
    }
    return false;
}


// Static functions: decode errors.
// Writes "Text component at <where>[: "<field>"]: " into the message scratch, where is the root or a JSON-pointer path.
static void BeginNodeMessage(DecodeContext* context, const TextCodecField* field)
{
    GenericBuffer* Message = &context->Message;
    GenericBuffer_Clear(Message);

    Error Result = Error_CreateSuccess();
    if (context->Depth <= 1U)
    {
        Result = StringUTF8_Format(u8"Text component at the root: ", Message);
    }
    else
    {
        Result = StringUTF8_Format(u8"Text component at \"", Message);
        for (size_t Level = 2U; (Level <= context->Depth) && (Result.Code == ErrorCode_Success); Level++)
        {
            uint64_t Index = (uint64_t)context->PathIndices[Level - 2U];
            Result = context->PathIsListEntry[Level - 2U]
                ? StringUTF8_Format(u8"/%" PRIu64, Message, Index)
                : StringUTF8_Format(u8"/children/%" PRIu64, Message, Index);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = StringUTF8_Format(u8"\": ", Message);
        }
    }
    if ((Result.Code == ErrorCode_Success) && (field != NULL))
    {
        Result = StringUTF8_Format(u8"\"%s\": ", Message, (const char*)field->DisplayName);
    }
    // A failed append only shortens the message.
    Error_Deconstruct(&Result);
}

static Error FinishNodeMessage(DecodeContext* context)
{
    if (!GenericBuffer_NullTerminate(&context->Message))
    {
        return Error_Construct2(context->FailureCode, "Text component: invalid value.");
    }
    return Error_Construct3(context->FailureCode, u8"%s", (const char*)context->Message._data);
}

static Error MakeNodeError(DecodeContext* context, const TextCodecField* field, const unsigned char* problem)
{
    BeginNodeMessage(context, field);
    Error AppendResult = StringUTF8_Format(u8"%s", &context->Message, (const char*)problem);
    Error_Deconstruct(&AppendResult);
    return FinishNodeMessage(context);
}

// Re-raises an adapter or setter error as a failure of this node (and field), keeping its message.
static Error WrapNodeError(DecodeContext* context, const TextCodecField* field, Error* inner)
{
    const unsigned char* Problem = (inner->Message != NULL) ? (const unsigned char*)inner->Message
        : (const unsigned char*)u8"invalid value.";
    BeginNodeMessage(context, field);
    Error AppendResult = StringUTF8_Format(u8"%s", &context->Message, (const char*)Problem);
    Error_Deconstruct(&AppendResult);
    Error_Deconstruct(inner);
    return FinishNodeMessage(context);
}

static Error MakeRangeError(DecodeContext* context, const TextCodecField* field, const unsigned char* format)
{
    BeginNodeMessage(context, field);
    Error AppendResult = StringUTF8_Format(format, &context->Message, (double)field->Minimum, (double)field->Maximum);
    Error_Deconstruct(&AppendResult);
    return FinishNodeMessage(context);
}


// Static functions: decoding.
static Error ValidateFieldValue(DecodeContext* context, const TextCodecField* field, const TextCodecValue* value)
{
    switch (field->Kind)
    {
        case TextCodecValueKind_Text:
            if (!StringUTF8_IsEncodingValid(value->Text))
            {
                return MakeNodeError(context, field, u8"must be valid UTF-8.");
            }
            return Error_CreateSuccess();
        case TextCodecValueKind_Name:
            if (!AssetManager_IsValidAssetName(value->Text))
            {
                return MakeNodeError(context, field, u8"must be a name matching [a-z][a-z0-9_.-]*.");
            }
            return Error_CreateSuccess();
        case TextCodecValueKind_Decimal:
            if (!IsFloatInRange(value->Decimal, field->Minimum, field->Maximum))
            {
                return MakeRangeError(context, field, u8"must be a decimal within [%g, %g].");
            }
            return Error_CreateSuccess();
        case TextCodecValueKind_Vector2:
            if (!IsFloatInRange(value->Vector.x, field->Minimum, field->Maximum)
                || !IsFloatInRange(value->Vector.y, field->Minimum, field->Maximum))
            {
                return MakeRangeError(context, field, u8"must be two decimals, each within [%g, %g].");
            }
            return Error_CreateSuccess();
        case TextCodecValueKind_Boolean:
            return Error_CreateSuccess();
        case TextCodecValueKind_RenderColor:
            if (!IsRenderColorValid(value->Color))
            {
                return MakeNodeError(context, field, u8"brightness and opacity must be within [0, 1].");
            }
            return Error_CreateSuccess();
        case TextCodecValueKind_ShadowColor:
            if (value->IsCustom && !IsRenderColorValid(value->Color))
            {
                return MakeNodeError(context, field, u8"brightness and opacity must be within [0, 1].");
            }
            return Error_CreateSuccess();
        case TextCodecValueKind_ShadowOffset:
            if (value->IsCustom && (!IsFloatInRange(value->Vector.x, field->Minimum, field->Maximum)
                || !IsFloatInRange(value->Vector.y, field->Minimum, field->Maximum)))
            {
                return MakeRangeError(context, field, u8"must be \"default\" or two decimals, each within [%g, %g].");
            }
            return Error_CreateSuccess();
    }
    return MakeNodeError(context, field, u8"has an unknown value kind.");
}

// Reads and validates one field; a missing required field is an error, a missing optional one leaves *outFound false.
static Error ReadFieldValue(DecodeContext* context, const TextCodecNode* node, const TextCodecField* field,
    TextCodecValue* outValue, bool* outFound)
{
    *outValue = (TextCodecValue){ 0 };
    Error Result = context->Reader.VTable->ReadField(context->Reader.Self, node, field, outValue, outFound);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapNodeError(context, field, &Result);
    }
    if (!*outFound)
    {
        return field->IsRequired ? MakeNodeError(context, field, u8"is required.") : Error_CreateSuccess();
    }
    return ValidateFieldValue(context, field, outValue);
}

// Reads every field of the type's table (except skipField, already applied at creation) into the component.
static Error ApplyFields(DecodeContext* context, const TextCodecNode* node, const TextCodecType* type,
    const TextCodecField* skipField, TextComponent* component)
{
    for (size_t Index = 0; Index < type->FieldCount; Index++)
    {
        const TextCodecField* Field = &type->Fields[Index];
        if (Field == skipField)
        {
            continue;
        }

        TextCodecValue Value;
        bool IsFound = false;
        Error Result = ReadFieldValue(context, node, Field, &Value, &IsFound);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (!IsFound)
        {
            continue;
        }
        Result = SetFieldValue(component, Field->Id, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return WrapNodeError(context, Field, &Result);
        }
    }
    return Error_CreateSuccess();
}

// Creates the component of an object node (a translate component needs its key first) and applies its fields.
static Error CreateObjectComponent(DecodeContext* context, const TextCodecNode* node, TextComponentType typeId,
    TextComponent** outComponent)
{
    *outComponent = NULL;
    const TextCodecType* Type = GetTypeDescriptor(typeId);
    if (Type == NULL)
    {
        return MakeNodeError(context, NULL, u8"has an unknown component type.");
    }

    TextComponent* Component = NULL;
    const TextCodecField* AppliedField = NULL;
    Error Result = Error_CreateSuccess();
    switch (typeId)
    {
        case TextComponentType_Empty:
        {
            EmptyComponent* Empty = NULL;
            Result = TextComponentFactory_CreateEmpty(context->Factory, &Empty);
            Component = (Empty != NULL) ? EmptyComponent_AsComponent(Empty) : NULL;
            break;
        }
        case TextComponentType_String:
        {
            StringComponent* String = NULL;
            Result = TextComponentFactory_CreateString(context->Factory, NULL, &String);
            Component = (String != NULL) ? StringComponent_AsComponent(String) : NULL;
            break;
        }
        case TextComponentType_Sprite:
        {
            SpriteComponent* Sprite = NULL;
            Result = TextComponentFactory_CreateSprite(context->Factory, NULL, &Sprite);
            Component = (Sprite != NULL) ? SpriteComponent_AsComponent(Sprite) : NULL;
            break;
        }
        case TextComponentType_Translate:
        {
            AppliedField = &Type->Fields[0];
            TextCodecValue Key;
            bool IsFound = false;
            Result = ReadFieldValue(context, node, AppliedField, &Key, &IsFound);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            TranslateComponent* Translate = NULL;
            Result = TextComponentFactory_CreateTranslate(context->Factory, Key.Text, NULL, &Translate);
            Component = (Translate != NULL) ? TranslateComponent_AsComponent(Translate) : NULL;
            break;
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        return WrapNodeError(context, AppliedField, &Result);
    }

    Result = ApplyFields(context, node, Type, AppliedField, Component);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = TextComponentFactory_ReturnTree(context->Factory, Component);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    *outComponent = Component;
    return Error_CreateSuccess();
}

// Recursion depth is bounded: a node deeper than TEXT_COMPONENT_MAX_DEPTH fails before anything below it is read.
static Error DecodeNode(DecodeContext* context, const TextCodecNode* node, TextComponent** outComponent)
{
    *outComponent = NULL;
    if (context->Depth > TEXT_COMPONENT_MAX_DEPTH)
    {
        BeginNodeMessage(context, NULL);
        Error AppendResult = StringUTF8_Format(u8"the tree is nested deeper than %d levels.", &context->Message,
            TEXT_COMPONENT_MAX_DEPTH);
        Error_Deconstruct(&AppendResult);
        return FinishNodeMessage(context);
    }

    TextCodecNodeInfo Info;
    Error Result = context->Reader.VTable->ReadNodeInfo(context->Reader.Self, node, &Info);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapNodeError(context, NULL, &Result);
    }

    TextComponent* Component = NULL;
    switch (Info.Shape)
    {
        case TextCodecNodeShape_Text:
        {
            StringComponent* String = NULL;
            Result = TextComponentFactory_CreateString(context->Factory, Info.Text, &String);
            if (Result.Code != ErrorCode_Success)
            {
                return WrapNodeError(context, &StringFields[0], &Result);
            }
            Component = StringComponent_AsComponent(String);
            break;
        }
        case TextCodecNodeShape_List:
        {
            EmptyComponent* Empty = NULL;
            Result = TextComponentFactory_CreateEmpty(context->Factory, &Empty);
            if (Result.Code != ErrorCode_Success)
            {
                return WrapNodeError(context, NULL, &Result);
            }
            Component = EmptyComponent_AsComponent(Empty);
            break;
        }
        case TextCodecNodeShape_Object:
            Result = CreateObjectComponent(context, node, Info.Type, &Component);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            break;
    }
    if (Component == NULL)
    {
        return MakeNodeError(context, NULL, u8"has an unknown shape.");
    }

    for (size_t Index = 0; Index < Info.ChildCount; Index++)
    {
        context->Depth++;
        context->PathIndices[context->Depth - 2U] = Index;
        context->PathIsListEntry[context->Depth - 2U] = (Info.Shape == TextCodecNodeShape_List);

        TextCodecNode ChildNode;
        TextComponent* Child = NULL;
        Result = context->Reader.VTable->GetChild(context->Reader.Self, node, Index, &ChildNode);
        if (Result.Code != ErrorCode_Success)
        {
            Result = WrapNodeError(context, NULL, &Result);
        }
        else
        {
            Result = DecodeNode(context, &ChildNode, &Child);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = TextComponent_AddSubComponent(Component, Child);
            if (Result.Code != ErrorCode_Success)
            {
                Error ChildCleanup = TextComponentFactory_ReturnTree(context->Factory, Child);
                Error_Deconstruct(&ChildCleanup);
                Result = WrapNodeError(context, NULL, &Result);
            }
        }
        context->Depth--;

        if (Result.Code != ErrorCode_Success)
        {
            Error Cleanup = TextComponentFactory_ReturnTree(context->Factory, Component);
            Error_Deconstruct(&Cleanup);
            return Result;
        }
    }

    *outComponent = Component;
    return Error_CreateSuccess();
}


// Static functions: encoding.
// A default-styled string component without children can be written as its bare text.
static Error CanUseTextShorthand(const TextComponent* component, bool* outResult)
{
    *outResult = false;
    if ((TextComponent_GetType(component) != TextComponentType_String) || (TextComponent_GetSubComponentCount(component) > 0U))
    {
        return Error_CreateSuccess();
    }

    const TextCodecType* Type = GetTypeDescriptor(TextComponentType_String);
    for (size_t Index = 0; Index < Type->FieldCount; Index++)
    {
        const TextCodecField* Field = &Type->Fields[Index];
        if (Field->Id == TextCodecFieldId_Text)
        {
            continue;
        }
        TextCodecValue Value;
        Error Result = GetFieldValue(component, Field->Id, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (!IsFieldDefault(Field->Id, &Value))
        {
            return Error_CreateSuccess();
        }
    }
    *outResult = true;
    return Error_CreateSuccess();
}

static Error WriteFields(EncodeContext* context, const TextComponent* component, const TextCodecType* type,
    TextCodecNode* node)
{
    for (size_t Index = 0; Index < type->FieldCount; Index++)
    {
        const TextCodecField* Field = &type->Fields[Index];
        TextCodecValue Value;
        Error Result = GetFieldValue(component, Field->Id, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (!Field->IsRequired && IsFieldDefault(Field->Id, &Value))
        {
            continue;
        }
        Result = context->Writer.VTable->WriteField(context->Writer.Self, node, Field, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

// Recursion depth is bounded by the tree's depth limit.
static Error EncodeNode(EncodeContext* context, const TextComponent* component, TextCodecNode* outNode)
{
    void* Writer = context->Writer.Self;
    const ITextCodecWriterVTable* VTable = context->Writer.VTable;
    TextComponentType TypeId = TextComponent_GetType(component);
    const TextCodecType* Type = GetTypeDescriptor(TypeId);
    if (Type == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidState, "TextComponentCodec: unknown component type.");
    }

    if (context->AreShorthandsSupported)
    {
        bool IsTextShorthand = false;
        Error ShorthandResult = CanUseTextShorthand(component, &IsTextShorthand);
        if (ShorthandResult.Code != ErrorCode_Success)
        {
            return ShorthandResult;
        }
        if (IsTextShorthand)
        {
            const unsigned char* Text = StringComponent_GetText(TextComponent_AsConstStringComponent(component));
            return VTable->CreateTextShorthand(Writer, Text, outNode);
        }
    }

    TextCodecNode Node;
    Error Result = Error_CreateSuccess();
    if (context->AreShorthandsSupported && (TypeId == TextComponentType_Empty))
    {
        Result = VTable->CreateListShorthand(Writer, &Node);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    else
    {
        Result = VTable->CreateObject(Writer, TypeId, &Node);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Result = WriteFields(context, component, Type, &Node);
    }

    size_t ChildCount = TextComponent_GetSubComponentCount(component);
    for (size_t Index = 0; (Index < ChildCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        TextCodecNode ChildNode;
        Result = EncodeNode(context, TextComponent_GetSubComponentAt(component, Index), &ChildNode);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        Result = VTable->AppendChild(Writer, &Node, &ChildNode);
        if (Result.Code != ErrorCode_Success)
        {
            VTable->DiscardNode(Writer, &ChildNode);
        }
    }

    if (Result.Code != ErrorCode_Success)
    {
        VTable->DiscardNode(Writer, &Node);
        return Result;
    }
    *outNode = Node;
    return Error_CreateSuccess();
}


// Internal functions (TextComponentCodec.h).
Error TextComponentCodec_Decode(ITextCodecReader reader, ErrorCode failureCode, const TextCodecNode* root,
    TextComponentFactory* factory, TextComponent** outRoot)
{
    if ((reader.VTable == NULL) || (root == NULL) || (factory == NULL) || (outRoot == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentCodec_Decode: reader, root, factory and outRoot must not be NULL.");
    }
    *outRoot = NULL;

    DecodeContext Context;
    Memory_Zero(&Context, sizeof(Context));
    Context.Reader = reader;
    Context.FailureCode = failureCode;
    Context.Factory = factory;
    Context.Depth = 1U;
    GenericBuffer_AllocateVariable(&Context.Message, 0U, sizeof(unsigned char));

    Error Result = DecodeNode(&Context, root, outRoot);
    Memory_Free(Context.Message._data);
    return Result;
}

Error TextComponentCodec_Encode(ITextCodecWriter writer, const TextComponent* root, TextCodecNode* outNode)
{
    if ((writer.VTable == NULL) || (root == NULL) || (outNode == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentCodec_Encode: writer, root and outNode must not be NULL.");
    }

    EncodeContext Context =
    {
        .Writer = writer,
        .AreShorthandsSupported = writer.VTable->AreShorthandsSupported(writer.Self)
    };
    return EncodeNode(&Context, root, outNode);
}

const unsigned char* TextComponentCodec_GetTypeName(TextComponentType type)
{
    const TextCodecType* Type = GetTypeDescriptor(type);
    return (Type != NULL) ? Type->JsonName : (const unsigned char*)u8"";
}

bool TextComponentCodec_FindTypeByName(const unsigned char* name, size_t byteCount, TextComponentType* outType)
{
    for (size_t Index = 0; Index < TEXT_CODEC_TYPE_COUNT; Index++)
    {
        const unsigned char* Candidate = Types[Index].JsonName;
        if ((StringUTF8_GetByteLength(Candidate) == byteCount) && Memory_IsEqual(Candidate, name, byteCount))
        {
            *outType = Types[Index].Type;
            return true;
        }
    }
    return false;
}

uint8_t TextComponentCodec_GetTypeWireId(TextComponentType type)
{
    const TextCodecType* Type = GetTypeDescriptor(type);
    return (Type != NULL) ? Type->WireId : (uint8_t)0;
}

bool TextComponentCodec_FindTypeByWireId(uint8_t wireId, TextComponentType* outType)
{
    for (size_t Index = 0; Index < TEXT_CODEC_TYPE_COUNT; Index++)
    {
        if (Types[Index].WireId == wireId)
        {
            *outType = Types[Index].Type;
            return true;
        }
    }
    return false;
}

const unsigned char* TextComponentCodec_GetTypeNameList(void)
{
    return u8"\"empty\", \"string\", \"sprite\" or \"translate\"";
}
