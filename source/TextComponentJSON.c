#include <float.h>
#include <math.h>
#include "TextComponentJSON.h"
#include "TextComponentCodec.h"
#include "GameJSON.h"
#include "wr/WRString.h"
#include "wr/WRMemory.h"


// Macros.
#define KEY_TYPE (u8"type")
#define KEY_CHILDREN (u8"children")
/* The value of a shadow color or offset that asks for the default (derived color, the font's offset). */
#define SHADOW_DEFAULT (u8"default")


// Types.
typedef struct JSONTextReaderStruct
{
    /* NUL-terminated copies of the strings handed to the codec (valid until the next read). */
    GenericBuffer Scratch;
} JSONTextReader;

typedef struct JSONTextWriterStruct
{
    JSONObjectPool* Pool;
} JSONTextWriter;


// Static functions: reading.
// Copies a JSON string value into the scratch as a NUL-terminated string; JSON allows \u0000, which text cannot hold.
static Error CopyStringToScratch(JSONTextReader* reader, const JSONObjectValue* value, const unsigned char** outText)
{
    GenericBuffer* Source = value->Value.String;
    size_t Length = (Source != NULL) ? Source->_count : 0U;
    GenericBuffer_Clear(&reader->Scratch);
    if ((Length > 0U) && !GenericBuffer_AppendRangeBytes(&reader->Scratch, Source->_data, Length))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "could not copy a string value.");
    }
    if (!GenericBuffer_NullTerminate(&reader->Scratch))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "could not terminate a string value.");
    }
    if (StringUTF8_GetByteLength(reader->Scratch._data) != Length)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "a string must not contain NUL characters.");
    }
    *outText = reader->Scratch._data;
    return Error_CreateSuccess();
}

static bool JSONStringEquals(const JSONObjectValue* value, const unsigned char* literal)
{
    GenericBuffer* Buffer = value->Value.String;
    size_t Length = (Buffer != NULL) ? Buffer->_count : 0U;
    return (StringUTF8_GetByteLength(literal) == Length) && ((Length == 0U) || Memory_IsEqual(Buffer->_data, literal, Length));
}

static Error ReadType(JSONCompound* compound, TextComponentType* outType)
{
    JSONObjectValue TypeValue;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(compound, KEY_TYPE, &TypeValue, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsFound)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "\"type\" is required in a component object.");
    }

    GenericBuffer* Name = (TypeValue.Type == JSONValueType_String) ? TypeValue.Value.String : NULL;
    size_t Length = (Name != NULL) ? Name->_count : 0U;
    const unsigned char* Bytes = ((Name != NULL) && (Name->_data != NULL)) ? Name->_data : (const unsigned char*)u8"";
    if ((TypeValue.Type != JSONValueType_String) || !TextComponentCodec_FindTypeByName(Bytes, Length, outType))
    {
        return Error_Construct3(ErrorCode_InvalidJSON, u8"\"type\" must be %s.",
            (const char*)TextComponentCodec_GetTypeNameList());
    }
    return Error_CreateSuccess();
}

// Finds a compound's "children" array; *outArray is NULL when the key is absent.
static Error GetChildrenArray(JSONCompound* compound, JSONArray** outArray)
{
    *outArray = NULL;
    JSONObjectValue ChildrenValue;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(compound, KEY_CHILDREN, &ChildrenValue, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    if (ChildrenValue.Type != JSONValueType_Array)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "\"children\" must be an array of components.");
    }
    *outArray = ChildrenValue.Value.Array;
    return Error_CreateSuccess();
}

static Error Reader_ReadNodeInfo(void* self, const TextCodecNode* node, TextCodecNodeInfo* outInfo)
{
    JSONTextReader* Reader = self;
    const JSONObjectValue* Value = &node->Json;
    *outInfo = (TextCodecNodeInfo){ .Shape = TextCodecNodeShape_Object, .Type = TextComponentType_Empty, .Text = NULL,
        .ChildCount = 0U };

    if (Value->Type == JSONValueType_String)
    {
        outInfo->Shape = TextCodecNodeShape_Text;
        return CopyStringToScratch(Reader, Value, &outInfo->Text);
    }
    if (Value->Type == JSONValueType_Array)
    {
        outInfo->Shape = TextCodecNodeShape_List;
        outInfo->ChildCount = JSONArray_GetElementCount(Value->Value.Array);
        return Error_CreateSuccess();
    }
    if (Value->Type != JSONValueType_Compound)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "a component must be an object, an array or a string.");
    }

    Error Result = ReadType(Value->Value.Compound, &outInfo->Type);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    JSONArray* Children = NULL;
    Result = GetChildrenArray(Value->Value.Compound, &Children);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    outInfo->ChildCount = (Children != NULL) ? JSONArray_GetElementCount(Children) : 0U;
    return Error_CreateSuccess();
}

// Converts a decimal that does not fit a float to NaN, which the codec's range check then rejects.
static float ToFloatChecked(double value)
{
    return (fabs(value) <= (double)FLT_MAX) ? (float)value : NAN;
}

static Error ReadKindValue(JSONTextReader* reader, const TextCodecField* field, const JSONObjectValue* raw,
    TextCodecValue* outValue)
{
    switch (field->Kind)
    {
        case TextCodecValueKind_Text:
        case TextCodecValueKind_Name:
            if (raw->Type != JSONValueType_String)
            {
                return Error_Construct2(ErrorCode_InvalidJSON, "must be a string.");
            }
            return CopyStringToScratch(reader, raw, &outValue->Text);
        case TextCodecValueKind_Decimal:
        {
            double Decimal = 0.0;
            Error Result = GameJSON_ValueToDouble(raw, &Decimal);
            outValue->Decimal = ToFloatChecked(Decimal);
            return Result;
        }
        case TextCodecValueKind_Boolean:
            return GameJSON_ValueToBoolean(raw, &outValue->Boolean);
        case TextCodecValueKind_RenderColor:
            return GameJSON_ParseRenderColor(raw, &outValue->Color);
        case TextCodecValueKind_Vector2:
            return GameJSON_ParseVector2(raw, &outValue->Vector);
        case TextCodecValueKind_ShadowColor:
            if ((raw->Type == JSONValueType_String) && JSONStringEquals(raw, SHADOW_DEFAULT))
            {
                outValue->IsCustom = false;
                return Error_CreateSuccess();
            }
            outValue->IsCustom = true;
            return GameJSON_ParseRenderColor(raw, &outValue->Color);
        case TextCodecValueKind_ShadowOffset:
            if ((raw->Type == JSONValueType_String) && JSONStringEquals(raw, SHADOW_DEFAULT))
            {
                outValue->IsCustom = false;
                return Error_CreateSuccess();
            }
            outValue->IsCustom = true;
            return GameJSON_ParseVector2(raw, &outValue->Vector);
    }
    return Error_Construct2(ErrorCode_InvalidJSON, "has an unknown value kind.");
}

static Error Reader_ReadField(void* self, const TextCodecNode* node, const TextCodecField* field, TextCodecValue* outValue,
    bool* outFound)
{
    JSONTextReader* Reader = self;
    *outFound = false;
    JSONCompound* Compound = node->Json.Value.Compound;

    if (field->JsonGroup != NULL)
    {
        JSONObjectValue GroupValue;
        bool HasGroup = false;
        Error GroupResult = JSONCompound_GetOptional(Compound, field->JsonGroup, &GroupValue, &HasGroup);
        if ((GroupResult.Code != ErrorCode_Success) || !HasGroup)
        {
            return GroupResult;
        }
        if (GroupValue.Type != JSONValueType_Compound)
        {
            return Error_Construct3(ErrorCode_InvalidJSON, u8"\"%s\" must be an object.", (const char*)field->JsonGroup);
        }
        Compound = GroupValue.Value.Compound;
    }

    JSONObjectValue Raw;
    Error Result = JSONCompound_GetOptional(Compound, field->JsonKey, &Raw, outFound);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }
    return ReadKindValue(Reader, field, &Raw, outValue);
}

static Error Reader_GetChild(void* self, const TextCodecNode* node, size_t index, TextCodecNode* outChild)
{
    (void)self;
    outChild->Ghdf = NULL;
    if (node->Json.Type == JSONValueType_Array)
    {
        return JSONArray_Get(node->Json.Value.Array, index, &outChild->Json);
    }

    JSONArray* Children = NULL;
    Error Result = GetChildrenArray(node->Json.Value.Compound, &Children);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Children == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidState, "the component has no children.");
    }
    return JSONArray_Get(Children, index, &outChild->Json);
}

static void Reader_Destroy(void* self)
{
    JSONTextReader* Reader = self;
    Memory_Free(Reader->Scratch._data);
    Reader->Scratch._data = NULL;
}

static const ITextCodecReaderVTable JSONTextReaderVTable =
{
    .ReadNodeInfo = Reader_ReadNodeInfo,
    .ReadField = Reader_ReadField,
    .GetChild = Reader_GetChild,
    .Destroy = Reader_Destroy
};


// Static functions: writing.
// Borrows a pool string holding the given bytes (without a terminator, like every WRJSON string).
static Error CreateStringValue(JSONTextWriter* writer, const unsigned char* bytes, size_t byteCount,
    JSONObjectValue* outValue)
{
    GenericBuffer* Buffer = NULL;
    Error Result = JSONObjectPool_BorrowString(writer->Pool, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // AppendRangeBytes only reads its source; the API is just not declared const.
    if ((byteCount > 0U) && !GenericBuffer_AppendRangeBytes(Buffer, (unsigned char*)bytes, byteCount))
    {
        Error ReturnResult = JSONObjectPool_ReturnString(writer->Pool, Buffer);
        Error_Deconstruct(&ReturnResult);
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentJSON: could not write a string value.");
    }
    *outValue = JSONObjectValue_CreateString(Buffer);
    return Error_CreateSuccess();
}

// Stores value under key; on failure the value is returned to the pool.
static Error SetEntry(JSONTextWriter* writer, JSONCompound* compound, const unsigned char* key, JSONObjectValue value)
{
    Error Result = JSONCompound_Set(compound, key, &value);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnValue(writer->Pool, &value);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

static Error SetStringEntry(JSONTextWriter* writer, JSONCompound* compound, const unsigned char* key, const unsigned char* text)
{
    JSONObjectValue Value;
    Error Result = CreateStringValue(writer, text, StringUTF8_GetByteLength(text), &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetEntry(writer, compound, key, Value);
}

// Gets the compound stored under key, creating (and storing) an empty one when absent.
static Error GetOrCreateCompound(JSONTextWriter* writer, JSONCompound* compound, const unsigned char* key,
    JSONCompound** outCompound)
{
    JSONObjectValue Existing;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptionalVerified(compound, key, JSONValueType_Compound, &Existing, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (IsFound)
    {
        *outCompound = Existing.Value.Compound;
        return Error_CreateSuccess();
    }

    JSONCompound* Created = NULL;
    Result = JSONObjectPool_BorrowCompound(writer->Pool, &Created);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetEntry(writer, compound, key, JSONObjectValue_CreateCompound(Created));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outCompound = Created;
    return Error_CreateSuccess();
}

// Gets the array stored under key, creating (and storing) an empty one when absent.
static Error GetOrCreateArray(JSONTextWriter* writer, JSONCompound* compound, const unsigned char* key, JSONArray** outArray)
{
    JSONObjectValue Existing;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptionalVerified(compound, key, JSONValueType_Array, &Existing, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (IsFound)
    {
        *outArray = Existing.Value.Array;
        return Error_CreateSuccess();
    }

    JSONArray* Created = NULL;
    Result = JSONObjectPool_BorrowArray(writer->Pool, &Created);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = SetEntry(writer, compound, key, JSONObjectValue_CreateArray(Created));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outArray = Created;
    return Error_CreateSuccess();
}

// A plain opaque color is written as "#rrggbb"; anything else as { "tint": [r, g, b], "brightness", "opacity" }.
static Error SetRenderColorEntry(JSONTextWriter* writer, JSONCompound* compound, const unsigned char* key, RenderColor color)
{
    JSONObjectValue Value;
    Error Result = GameJSON_CreateRenderColorValue(writer->Pool, color, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetEntry(writer, compound, key, Value);
}

static Error SetVectorEntry(JSONTextWriter* writer, JSONCompound* compound, const unsigned char* key, Vector2 vector)
{
    JSONObjectValue Value;
    Error Result = GameJSON_CreateVector2Value(writer->Pool, vector, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetEntry(writer, compound, key, Value);
}

static bool Writer_AreShorthandsSupported(void* self)
{
    (void)self;
    return true;
}

static Error Writer_CreateObject(void* self, TextComponentType type, TextCodecNode* outNode)
{
    JSONTextWriter* Writer = self;
    JSONCompound* Compound = NULL;
    Error Result = JSONObjectPool_BorrowCompound(Writer->Pool, &Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    outNode->Json = JSONObjectValue_CreateCompound(Compound);
    outNode->Ghdf = NULL;

    Result = SetStringEntry(Writer, Compound, KEY_TYPE, TextComponentCodec_GetTypeName(type));
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnValue(Writer->Pool, &outNode->Json);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

static Error Writer_CreateTextShorthand(void* self, const unsigned char* text, TextCodecNode* outNode)
{
    JSONTextWriter* Writer = self;
    outNode->Ghdf = NULL;
    return CreateStringValue(Writer, text, StringUTF8_GetByteLength(text), &outNode->Json);
}

static Error Writer_CreateListShorthand(void* self, TextCodecNode* outNode)
{
    JSONTextWriter* Writer = self;
    JSONArray* Array = NULL;
    Error Result = JSONObjectPool_BorrowArray(Writer->Pool, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    outNode->Json = JSONObjectValue_CreateArray(Array);
    outNode->Ghdf = NULL;
    return Error_CreateSuccess();
}

static Error Writer_WriteField(void* self, TextCodecNode* node, const TextCodecField* field, const TextCodecValue* value)
{
    JSONTextWriter* Writer = self;
    JSONCompound* Target = node->Json.Value.Compound;
    if (field->JsonGroup != NULL)
    {
        Error GroupResult = GetOrCreateCompound(Writer, Target, field->JsonGroup, &Target);
        if (GroupResult.Code != ErrorCode_Success)
        {
            return GroupResult;
        }
    }

    switch (field->Kind)
    {
        case TextCodecValueKind_Text:
        case TextCodecValueKind_Name:
            return SetStringEntry(Writer, Target, field->JsonKey, value->Text);
        case TextCodecValueKind_Decimal:
            return SetEntry(Writer, Target, field->JsonKey, GameJSON_CreateFloatValue(value->Decimal));
        case TextCodecValueKind_Boolean:
            return SetEntry(Writer, Target, field->JsonKey, JSONObjectValue_CreateBoolean(value->Boolean));
        case TextCodecValueKind_RenderColor:
            return SetRenderColorEntry(Writer, Target, field->JsonKey, value->Color);
        case TextCodecValueKind_Vector2:
            return SetVectorEntry(Writer, Target, field->JsonKey, value->Vector);
        case TextCodecValueKind_ShadowColor:
            return value->IsCustom ? SetRenderColorEntry(Writer, Target, field->JsonKey, value->Color)
                : SetStringEntry(Writer, Target, field->JsonKey, SHADOW_DEFAULT);
        case TextCodecValueKind_ShadowOffset:
            return value->IsCustom ? SetVectorEntry(Writer, Target, field->JsonKey, value->Vector)
                : SetStringEntry(Writer, Target, field->JsonKey, SHADOW_DEFAULT);
    }
    return Error_Construct2(ErrorCode_InvalidState, "TextComponentJSON: unknown value kind.");
}

static Error Writer_AppendChild(void* self, TextCodecNode* parent, TextCodecNode* child)
{
    JSONTextWriter* Writer = self;
    JSONArray* Target = NULL;
    if (parent->Json.Type == JSONValueType_Array)
    {
        Target = parent->Json.Value.Array;
    }
    else
    {
        Error Result = GetOrCreateArray(Writer, parent->Json.Value.Compound, KEY_CHILDREN, &Target);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return JSONArray_Add(Target, &child->Json);
}

static void Writer_DiscardNode(void* self, TextCodecNode* node)
{
    JSONTextWriter* Writer = self;
    Error Result = JSONObjectPool_ReturnValue(Writer->Pool, &node->Json);
    Error_Deconstruct(&Result);
}

static void Writer_Destroy(void* self)
{
    (void)self;
}

static const ITextCodecWriterVTable JSONTextWriterVTable =
{
    .AreShorthandsSupported = Writer_AreShorthandsSupported,
    .CreateObject = Writer_CreateObject,
    .CreateTextShorthand = Writer_CreateTextShorthand,
    .CreateListShorthand = Writer_CreateListShorthand,
    .WriteField = Writer_WriteField,
    .AppendChild = Writer_AppendChild,
    .DiscardNode = Writer_DiscardNode,
    .Destroy = Writer_Destroy
};


// Public functions.
Error TextComponentJSON_Serialize(const TextComponent* component, JSONObjectPool* pool, JSONObjectValue* outValue)
{
    if ((component == NULL) || (pool == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentJSON_Serialize: component, pool and outValue must not be NULL.");
    }
    *outValue = JSONObjectValue_CreateNull();

    JSONTextWriter Writer = { .Pool = pool };
    ITextCodecWriter Interface = { .VTable = &JSONTextWriterVTable, .Self = &Writer };
    TextCodecNode Root;
    Error Result = TextComponentCodec_Encode(Interface, component, &Root);
    Interface.VTable->Destroy(Interface.Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outValue = Root.Json;
    }
    return Result;
}

Error TextComponentJSON_Deserialize(const JSONObjectValue* value, TextComponentFactory* factory,
    TextComponent** outComponent)
{
    if ((value == NULL) || (factory == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentJSON_Deserialize: value, factory and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    JSONTextReader Reader;
    GenericBuffer_AllocateVariable(&Reader.Scratch, 0U, sizeof(unsigned char));
    ITextCodecReader Interface = { .VTable = &JSONTextReaderVTable, .Self = &Reader };
    TextCodecNode Root = { .Json = *value, .Ghdf = NULL };
    Error Result = TextComponentCodec_Decode(Interface, ErrorCode_InvalidJSON, &Root, factory, outComponent);
    Interface.VTable->Destroy(Interface.Self);
    return Result;
}
