#include "TextComponentGHDF.h"
#include "TextComponentCodec.h"
#include "wr/WRString.h"
#include "wr/WRMemory.h"


// Macros.
/* Structural entries of a component compound; the field entries are listed in TextComponentCodec.c. */
#define GHDF_ID_TYPE ((GHDFEntryID)1)
#define GHDF_ID_CHILDREN ((GHDFEntryID)2)

/* Render-color sub-compound: tint packed as 0xRRGGBBAA, brightness, opacity. */
#define GHDF_COLOR_TINT ((GHDFEntryID)1)
#define GHDF_COLOR_BRIGHTNESS ((GHDFEntryID)2)
#define GHDF_COLOR_OPACITY ((GHDFEntryID)3)

/* Vector sub-compound. */
#define GHDF_VECTOR_X ((GHDFEntryID)1)
#define GHDF_VECTOR_Y ((GHDFEntryID)2)

#define GHDF_COLOR_CHANNEL_MASK (0xFFU)


// Types.
typedef struct GHDFTextWriterStruct
{
    GHDFObjectPool* Pool;
} GHDFTextWriter;


// Static functions: shared.
static GHDFCompoundEntryType CompoundArrayType(void)
{
    return GHDF_CreateArrayType(GHDFValueType_Compound);
}


// Static functions: reading.
static Error GetChildrenArray(GHDFCompound* compound, GHDFArray** outArray)
{
    *outArray = NULL;
    GHDFObjectValue ChildrenValue;
    bool IsFound = false;
    Error Result = GHDFCompound_GetOptionalVerified(compound, GHDF_ID_CHILDREN, CompoundArrayType(), &ChildrenValue, &IsFound);
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        *outArray = ChildrenValue.Value.Array;
    }
    return Result;
}

static Error Reader_ReadNodeInfo(void* self, const TextCodecNode* node, TextCodecNodeInfo* outInfo)
{
    (void)self;
    *outInfo = (TextCodecNodeInfo){ .Shape = TextCodecNodeShape_Object, .Type = TextComponentType_Empty, .Text = NULL,
        .ChildCount = 0U };
    if (node->Ghdf == NULL)
    {
        return Error_Construct2(ErrorCode_Deserialize, "a component must be a compound.");
    }

    GHDFObjectValue TypeValue;
    bool IsFound = false;
    Error Result = GHDFCompound_GetOptionalVerified(node->Ghdf, GHDF_ID_TYPE, GHDF_CreateRegularType(GHDFValueType_UInt8),
        &TypeValue, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsFound)
    {
        return Error_Construct2(ErrorCode_Deserialize, "the component has no type entry.");
    }
    if (!TextComponentCodec_FindTypeByWireId(TypeValue.Value.UInt8, &outInfo->Type))
    {
        return Error_Construct3(ErrorCode_Deserialize, u8"type id %u is not a known component type.",
            (unsigned int)TypeValue.Value.UInt8);
    }

    GHDFArray* Children = NULL;
    Result = GetChildrenArray(node->Ghdf, &Children);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    outInfo->ChildCount = (Children != NULL) ? GHDFArray_GetElementCount(Children) : 0U;
    return Error_CreateSuccess();
}

// GHDF strings end in a NUL terminator that their count includes; a NUL before it would cut the text short.
static Error ReadStringEntry(GHDFCompound* compound, GHDFEntryID id, const unsigned char** outText, bool* outFound)
{
    GHDFObjectValue Entry;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_String), &Entry, outFound);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }

    GenericBuffer* Buffer = Entry.Value.String;
    if ((Buffer == NULL) || (Buffer->_count == 0U) || (StringUTF8_GetByteLength(Buffer->_data) != (Buffer->_count - 1U)))
    {
        return Error_Construct2(ErrorCode_Deserialize, "must be a string without NUL characters.");
    }
    *outText = Buffer->_data;
    return Error_CreateSuccess();
}

static Error ReadFloatEntry(GHDFCompound* compound, GHDFEntryID id, float* outValue, bool* outFound)
{
    GHDFObjectValue Entry;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_Float), &Entry, outFound);
    if ((Result.Code == ErrorCode_Success) && *outFound)
    {
        *outValue = Entry.Value.Float;
    }
    return Result;
}

static Error ReadRequiredFloat(GHDFCompound* compound, GHDFEntryID id, float* outValue)
{
    bool IsFound = false;
    Error Result = ReadFloatEntry(compound, id, outValue, &IsFound);
    if ((Result.Code == ErrorCode_Success) && !IsFound)
    {
        return Error_Construct2(ErrorCode_Deserialize, "is missing a component of its value.");
    }
    return Result;
}

static Error ReadSubCompound(GHDFCompound* compound, GHDFEntryID id, GHDFCompound** outCompound, bool* outFound)
{
    GHDFObjectValue Entry;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_Compound), &Entry,
        outFound);
    if ((Result.Code == ErrorCode_Success) && *outFound)
    {
        *outCompound = Entry.Value.Compound;
    }
    return Result;
}

static Error ReadRenderColor(GHDFCompound* compound, GHDFEntryID id, RenderColor* outColor, bool* outFound)
{
    GHDFCompound* ColorCompound = NULL;
    Error Result = ReadSubCompound(compound, id, &ColorCompound, outFound);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }

    GHDFObjectValue TintValue;
    Result = GHDFCompound_GetVerified(ColorCompound, GHDF_COLOR_TINT, GHDF_CreateRegularType(GHDFValueType_UInt32), &TintValue);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRequiredFloat(ColorCompound, GHDF_COLOR_BRIGHTNESS, &outColor->Brightness);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRequiredFloat(ColorCompound, GHDF_COLOR_OPACITY, &outColor->Opacity);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    uint32_t Packed = TintValue.Value.UInt32;
    outColor->Tint = (Color)
    {
        .r = (unsigned char)((Packed >> 24) & GHDF_COLOR_CHANNEL_MASK),
        .g = (unsigned char)((Packed >> 16) & GHDF_COLOR_CHANNEL_MASK),
        .b = (unsigned char)((Packed >> 8) & GHDF_COLOR_CHANNEL_MASK),
        .a = (unsigned char)(Packed & GHDF_COLOR_CHANNEL_MASK)
    };
    return Error_CreateSuccess();
}

static Error ReadVector(GHDFCompound* compound, GHDFEntryID id, Vector2* outVector, bool* outFound)
{
    GHDFCompound* VectorCompound = NULL;
    Error Result = ReadSubCompound(compound, id, &VectorCompound, outFound);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }
    Result = ReadRequiredFloat(VectorCompound, GHDF_VECTOR_X, &outVector->x);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRequiredFloat(VectorCompound, GHDF_VECTOR_Y, &outVector->y);
    }
    return Result;
}

static Error Reader_ReadField(void* self, const TextCodecNode* node, const TextCodecField* field, TextCodecValue* outValue,
    bool* outFound)
{
    (void)self;
    *outFound = false;
    GHDFCompound* Compound = node->Ghdf;

    switch (field->Kind)
    {
        case TextCodecValueKind_Text:
        case TextCodecValueKind_Name:
            return ReadStringEntry(Compound, field->GhdfId, &outValue->Text, outFound);
        case TextCodecValueKind_Decimal:
            return ReadFloatEntry(Compound, field->GhdfId, &outValue->Decimal, outFound);
        case TextCodecValueKind_Boolean:
        {
            GHDFObjectValue Entry;
            Error Result = GHDFCompound_GetOptionalVerified(Compound, field->GhdfId,
                GHDF_CreateRegularType(GHDFValueType_Boolean), &Entry, outFound);
            if ((Result.Code == ErrorCode_Success) && *outFound)
            {
                outValue->Boolean = Entry.Value.Boolean;
            }
            return Result;
        }
        case TextCodecValueKind_RenderColor:
            return ReadRenderColor(Compound, field->GhdfId, &outValue->Color, outFound);
        case TextCodecValueKind_Vector2:
            return ReadVector(Compound, field->GhdfId, &outValue->Vector, outFound);
        case TextCodecValueKind_ShadowColor:
            // Only a custom shadow color is stored; its absence means the default.
            outValue->IsCustom = true;
            return ReadRenderColor(Compound, field->GhdfId, &outValue->Color, outFound);
        case TextCodecValueKind_ShadowOffset:
            // Likewise for the shadow offset: its absence means the font's.
            outValue->IsCustom = true;
            return ReadVector(Compound, field->GhdfId, &outValue->Vector, outFound);
    }
    return Error_Construct2(ErrorCode_Deserialize, "has an unknown value kind.");
}

static Error Reader_GetChild(void* self, const TextCodecNode* node, size_t index, TextCodecNode* outChild)
{
    (void)self;
    outChild->Json = JSONObjectValue_CreateNull();
    outChild->Ghdf = NULL;

    GHDFArray* Children = NULL;
    Error Result = GetChildrenArray(node->Ghdf, &Children);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Children == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidState, "the component has no children.");
    }

    GHDFObjectValue Element;
    Result = GHDFArray_Get(Children, index, &Element);
    if (Result.Code == ErrorCode_Success)
    {
        outChild->Ghdf = Element.Value.Compound;
    }
    return Result;
}

static void Reader_Destroy(void* self)
{
    (void)self;
}

static const ITextCodecReaderVTable GHDFTextReaderVTable =
{
    .ReadNodeInfo = Reader_ReadNodeInfo,
    .ReadField = Reader_ReadField,
    .GetChild = Reader_GetChild,
    .Destroy = Reader_Destroy
};


// Static functions: writing.
static Error SetEntry(GHDFCompound* compound, GHDFEntryID id, GHDFObjectValue value)
{
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(value.Type), &value);
}

static Error SetStringEntry(GHDFTextWriter* writer, GHDFCompound* compound, GHDFEntryID id, const unsigned char* text)
{
    GenericBuffer* Buffer = NULL;
    Error Result = GHDFObjectPool_BorrowString(writer->Pool, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_AppendString(Buffer, text) || !GenericBuffer_NullTerminate(Buffer))
    {
        Result = Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentGHDF: could not write a string value.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetEntry(compound, id, GHDFObjectValue_CreateString(Buffer));
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnString(writer->Pool, Buffer);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

// Borrows an empty sub-compound and stores it under id; on failure nothing stays borrowed.
static Error AddSubCompound(GHDFTextWriter* writer, GHDFCompound* compound, GHDFEntryID id, GHDFCompound** outCompound)
{
    GHDFCompound* Created = NULL;
    Error Result = GHDFObjectPool_BorrowCompound(writer->Pool, &Created);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetEntry(compound, id, GHDFObjectValue_CreateCompound(Created));
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnCompound(writer->Pool, Created, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    *outCompound = Created;
    return Error_CreateSuccess();
}

static Error SetRenderColorEntry(GHDFTextWriter* writer, GHDFCompound* compound, GHDFEntryID id, RenderColor color)
{
    GHDFCompound* ColorCompound = NULL;
    Error Result = AddSubCompound(writer, compound, id, &ColorCompound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    uint32_t Packed = ((uint32_t)color.Tint.r << 24) | ((uint32_t)color.Tint.g << 16) | ((uint32_t)color.Tint.b << 8)
        | (uint32_t)color.Tint.a;
    Result = SetEntry(ColorCompound, GHDF_COLOR_TINT, GHDFObjectValue_CreateUInt32(Packed));
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetEntry(ColorCompound, GHDF_COLOR_BRIGHTNESS, GHDFObjectValue_CreateFloat(color.Brightness));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetEntry(ColorCompound, GHDF_COLOR_OPACITY, GHDFObjectValue_CreateFloat(color.Opacity));
    }
    return Result;
}

static Error SetVectorEntry(GHDFTextWriter* writer, GHDFCompound* compound, GHDFEntryID id, Vector2 vector)
{
    GHDFCompound* VectorCompound = NULL;
    Error Result = AddSubCompound(writer, compound, id, &VectorCompound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetEntry(VectorCompound, GHDF_VECTOR_X, GHDFObjectValue_CreateFloat(vector.x));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetEntry(VectorCompound, GHDF_VECTOR_Y, GHDFObjectValue_CreateFloat(vector.y));
    }
    return Result;
}

static bool Writer_AreShorthandsSupported(void* self)
{
    (void)self;
    return false;
}

static Error Writer_CreateObject(void* self, TextComponentType type, TextCodecNode* outNode)
{
    GHDFTextWriter* Writer = self;
    GHDFCompound* Compound = NULL;
    Error Result = GHDFObjectPool_BorrowCompound(Writer->Pool, &Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetEntry(Compound, GHDF_ID_TYPE, GHDFObjectValue_CreateUInt8(TextComponentCodec_GetTypeWireId(type)));
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnCompound(Writer->Pool, Compound, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    outNode->Json = JSONObjectValue_CreateNull();
    outNode->Ghdf = Compound;
    return Error_CreateSuccess();
}

static Error Writer_CreateShorthandUnsupported(void* self, TextCodecNode* outNode)
{
    (void)self;
    (void)outNode;
    return Error_Construct2(ErrorCode_InvalidOperation, "TextComponentGHDF: GHDF has no shorthand forms.");
}

static Error Writer_CreateTextShorthand(void* self, const unsigned char* text, TextCodecNode* outNode)
{
    (void)text;
    return Writer_CreateShorthandUnsupported(self, outNode);
}

static Error Writer_WriteField(void* self, TextCodecNode* node, const TextCodecField* field, const TextCodecValue* value)
{
    GHDFTextWriter* Writer = self;
    GHDFCompound* Compound = node->Ghdf;

    switch (field->Kind)
    {
        case TextCodecValueKind_Text:
        case TextCodecValueKind_Name:
            return SetStringEntry(Writer, Compound, field->GhdfId, value->Text);
        case TextCodecValueKind_Decimal:
            return SetEntry(Compound, field->GhdfId, GHDFObjectValue_CreateFloat(value->Decimal));
        case TextCodecValueKind_Boolean:
            return SetEntry(Compound, field->GhdfId, GHDFObjectValue_CreateBoolean(value->Boolean));
        case TextCodecValueKind_RenderColor:
            return SetRenderColorEntry(Writer, Compound, field->GhdfId, value->Color);
        case TextCodecValueKind_Vector2:
            return SetVectorEntry(Writer, Compound, field->GhdfId, value->Vector);
        case TextCodecValueKind_ShadowColor:
            // The default shadow color is the entry's absence.
            return value->IsCustom ? SetRenderColorEntry(Writer, Compound, field->GhdfId, value->Color) : Error_CreateSuccess();
        case TextCodecValueKind_ShadowOffset:
            // So is the default shadow offset.
            return value->IsCustom ? SetVectorEntry(Writer, Compound, field->GhdfId, value->Vector) : Error_CreateSuccess();
    }
    return Error_Construct2(ErrorCode_InvalidState, "TextComponentGHDF: unknown value kind.");
}

static Error Writer_AppendChild(void* self, TextCodecNode* parent, TextCodecNode* child)
{
    GHDFTextWriter* Writer = self;
    GHDFArray* Children = NULL;
    Error Result = GetChildrenArray(parent->Ghdf, &Children);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Children == NULL)
    {
        Result = GHDFObjectPool_BorrowArray(Writer->Pool, GHDFValueType_Compound, &Children);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(Children, GHDFValueType_Compound);
        Result = GHDFCompound_SetValue(parent->Ghdf, GHDF_ID_CHILDREN, CompoundArrayType(), &ArrayValue);
        if (Result.Code != ErrorCode_Success)
        {
            Error ReturnResult = GHDFObjectPool_ReturnArray(Writer->Pool, Children, true);
            Error_Deconstruct(&ReturnResult);
            return Result;
        }
    }

    GHDFObjectValue ChildValue = GHDFObjectValue_CreateCompound(child->Ghdf);
    return GHDFArray_AddValue(Children, &ChildValue);
}

static void Writer_DiscardNode(void* self, TextCodecNode* node)
{
    GHDFTextWriter* Writer = self;
    if (node->Ghdf != NULL)
    {
        Error Result = GHDFObjectPool_ReturnCompound(Writer->Pool, node->Ghdf, true);
        Error_Deconstruct(&Result);
        node->Ghdf = NULL;
    }
}

static void Writer_Destroy(void* self)
{
    (void)self;
}

static const ITextCodecWriterVTable GHDFTextWriterVTable =
{
    .AreShorthandsSupported = Writer_AreShorthandsSupported,
    .CreateObject = Writer_CreateObject,
    .CreateTextShorthand = Writer_CreateTextShorthand,
    .CreateListShorthand = Writer_CreateShorthandUnsupported,
    .WriteField = Writer_WriteField,
    .AppendChild = Writer_AppendChild,
    .DiscardNode = Writer_DiscardNode,
    .Destroy = Writer_Destroy
};


// Public functions.
Error TextComponentGHDF_Serialize(const TextComponent* component, GHDFObjectPool* pool, GHDFCompound** outCompound)
{
    if ((component == NULL) || (pool == NULL) || (outCompound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentGHDF_Serialize: component, pool and outCompound must not be NULL.");
    }
    *outCompound = NULL;

    GHDFTextWriter Writer = { .Pool = pool };
    ITextCodecWriter Interface = { .VTable = &GHDFTextWriterVTable, .Self = &Writer };
    TextCodecNode Root = { .Json = JSONObjectValue_CreateNull(), .Ghdf = NULL };
    Error Result = TextComponentCodec_Encode(Interface, component, &Root);
    Interface.VTable->Destroy(Interface.Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outCompound = Root.Ghdf;
    }
    return Result;
}

Error TextComponentGHDF_Deserialize(GHDFCompound* compound, TextComponentFactory* factory, TextComponent** outComponent)
{
    if ((compound == NULL) || (factory == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentGHDF_Deserialize: compound, factory and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    ITextCodecReader Interface = { .VTable = &GHDFTextReaderVTable, .Self = NULL };
    TextCodecNode Root = { .Json = JSONObjectValue_CreateNull(), .Ghdf = compound };
    Error Result = TextComponentCodec_Decode(Interface, ErrorCode_Deserialize, &Root, factory, outComponent);
    Interface.VTable->Destroy(Interface.Self);
    return Result;
}
