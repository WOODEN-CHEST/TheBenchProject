#include "WRUnicodeLoader.h"
#include "WRFileSystem.h"
#include "WRMemory.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>


// Macros.


// Types.
typedef struct UnicodeParserStruct
{
    const unsigned char* FilePath;

    GenericBuffer TextBuffer;
    unsigned char* Text;
    size_t TextIndex;
    size_t LineIndex;

    size_t DataCapacity;
    UnicodeCharacter* Data;
    size_t DataCount;

    CodePoint MaxCodePoint;
} UnicodeParser;

typedef Error (*LineParseCallback)(UnicodeParser* parser);

typedef struct LineParseActionStruct
{
    bool IsSkipped;
    LineParseCallback ParseCallback;
} LineParseAction;


static Error ParseCategory(UnicodeParser* parser);

static Error ParseNumericValue(UnicodeParser* parser);

static Error ParseUppercaseMapping(UnicodeParser* parser);

static Error ParseLowercaseMapping(UnicodeParser* parser);

static Error ParseCodePoint(UnicodeParser* parser);

static Error ParseIsDigit(UnicodeParser* parser);


// Fields.
static const size_t UNICODE_DATA_CAPACITY_DEFAULT = 2 << 15; // If the unicode data file text doesn't change much, then this should be large enough.
static const size_t UNICODE_DATA_CAPACITY_GROWTH = 2;
static const size_t UNICODE_TEXT_INITIAL_CAPACITY = 256;
static const unsigned char SEPARATOR = ';';
static const unsigned char NEWLINE = '\n';
static const unsigned char DIVIDER = '/';
static const int32_t NUMBER_BASE = 16;

static const size_t SECTION_COUNT = 15;

static const float DIGIT_VALUE_MIN = 0.0f;
static const float DIGIT_VALUE_MAX = 9.0f;

static const LineParseAction PARSE_ACTIONS[] =
{
    { false, &ParseCodePoint, },
    { false, NULL },
    { false, &ParseCategory, },
    { true, NULL, },
    { true, NULL, },
    { true, NULL, },
    { false, &ParseIsDigit, },
    { true, NULL, },
    { false, &ParseNumericValue, },
    { true, NULL, },
    { true, NULL, },
    { true, NULL, },
    { false, &ParseUppercaseMapping, },
    { false, &ParseLowercaseMapping, },
    { true, NULL, },
};

static const size_t MAX_CODEPOINTS = 1 << 21; // Let's be reasonable with the size.


// Static functions.
static bool UnicodeLoaderTextBufferAllocate(GenericBuffer* destination, size_t requestedCapacity)
{
    unsigned char* NewData = Memory_Reallocate(destination->_data, requestedCapacity);
    if (NewData == NULL)
    {
        return false;
    }

    destination->_data = NewData;
    destination->_capacity = requestedCapacity;
    return true;
}

static void CreateGrowableByteBuffer(GenericBuffer* buffer)
{
    unsigned char* Data = Memory_Allocate(UNICODE_TEXT_INITIAL_CAPACITY);
    GenericBuffer_CreateVariable(buffer,
        Data,
        UNICODE_TEXT_INITIAL_CAPACITY,
        sizeof(unsigned char),
        0,
        NULL,
        &UnicodeLoaderTextBufferAllocate);
}

static inline unsigned char GetParserChar(UnicodeParser* parser)
{
    return parser->Text[parser->TextIndex];
}

static inline bool IsCharSectionEnd(unsigned char character)
{
    return (character == SEPARATOR) || (character == NEWLINE) || (character == '\0');
}

static inline bool ParserHasFilePath(const UnicodeParser* parser)
{
    return (parser->FilePath != NULL) && (parser->FilePath[0] != '\0');
}

static bool IsCategoryText(const unsigned char* sourceText, unsigned char firstCharacter, unsigned char secondCharacter)
{
    return (sourceText[0] == firstCharacter) &&
        (sourceText[1] == secondCharacter) &&
        (sourceText[2] == '\0');
}

static void EnsureUnicodeDataCapacity(UnicodeParser* parser, size_t capacity)
{
    if (parser->DataCapacity >= capacity)
    {
        return;
    }

    size_t NewCapacity = (parser->DataCapacity == 0) ? UNICODE_DATA_CAPACITY_DEFAULT : parser->DataCapacity;
    while (NewCapacity < capacity)
    {
        NewCapacity *= UNICODE_DATA_CAPACITY_GROWTH;
    }

    size_t NewSize = NewCapacity * sizeof(UnicodeCharacter);
    parser->Data = (parser->Data != NULL) ? Memory_Reallocate(parser->Data, NewSize) : Memory_Allocate(NewSize);
    parser->DataCapacity = NewCapacity;
}

static Error InitParser(const unsigned char* filePath, GenericBuffer* textBuffer, UnicodeParser* parser)
{
    if ((textBuffer == NULL) || (parser == NULL))
    {
        return Error_Construct3(
            ErrorCode_IllegalArgument,
            u8"Unicode parser initialization requires both text buffer and parser.");
    }

    Memory_Zero(parser, sizeof(*parser));
    parser->FilePath = filePath;
    parser->TextBuffer = *textBuffer;
    parser->Text = parser->TextBuffer._data;
    parser->MaxCodePoint = CODEPOINT_NONE;
    EnsureUnicodeDataCapacity(parser, UNICODE_DATA_CAPACITY_DEFAULT);
    return Error_CreateSuccess();
}

static void DeinitParser(UnicodeParser* parser)
{
    if (parser == NULL)
    {
        return;
    }

    if (parser->Data != NULL)
    {
        Memory_Free(parser->Data);
        parser->Data = NULL;
    }

    if (parser->TextBuffer._data != NULL)
    {
        Memory_Free(parser->TextBuffer._data);
        parser->TextBuffer._data = NULL;
    }

    parser->Text = NULL;
    parser->DataCapacity = 0;
    parser->DataCount = 0;
}

static Error CreateIncompleteLineError(UnicodeParser* parser, size_t sectionIndex)
{
    if (ParserHasFilePath(parser))
    {
        return Error_Construct3(ErrorCode_InvalidUnicodeData,
            u8"Malformed Unicode file \"%s\", expected %zu sections at line %zu, got %zu instead.",
            parser->FilePath,
            SECTION_COUNT,
            parser->LineIndex + 1,
            sectionIndex + 1);
    }

    return Error_Construct3(ErrorCode_InvalidUnicodeData,
        u8"Malformed Unicode data, expected %zu sections at line %zu, got %zu instead.",
        SECTION_COUNT,
        parser->LineIndex + 1,
        sectionIndex + 1);
}

static Error CreateMalformedHexadecimalError(UnicodeParser* parser, const unsigned char* str, const unsigned char* context)
{
    if (ParserHasFilePath(parser))
    {
        return Error_Construct3(
            ErrorCode_InvalidUnicodeData,
            u8"Malformed Unicode source file \"%s\", expected hexadecimal number at line %zu, got \"%s\" (%s).",
            parser->FilePath,
            parser->LineIndex + 1,
            str,
            context);
    }

    return Error_Construct3(
        ErrorCode_InvalidUnicodeData,
        u8"Malformed Unicode data, expected hexadecimal number at line %zu, got \"%s\" (%s).",
        parser->LineIndex + 1,
        str,
        context);
}

static Error CreateMalformedDecimalError(UnicodeParser* parser, const unsigned char* str, const unsigned char* context)
{
    if (ParserHasFilePath(parser))
    {
        return Error_Construct3(
            ErrorCode_InvalidUnicodeData,
            u8"Malformed Unicode source file \"%s\", expected decimal number at line %zu, got \"%s\" (%s).",
            parser->FilePath,
            parser->LineIndex + 1,
            str,
            context);
    }

    return Error_Construct3(
        ErrorCode_InvalidUnicodeData,
        u8"Malformed Unicode data, expected decimal number at line %zu, got \"%s\" (%s).",
        parser->LineIndex + 1,
        str,
        context);
}

static Error CreateInvalidNumericFormatError(UnicodeParser* parser)
{
    if (ParserHasFilePath(parser))
    {
        return Error_Construct3(
            ErrorCode_InvalidUnicodeData,
            u8"Invalid Unicode file \"%s\" on line %zu, expected either a constant numeric value or division without spaces, got \"%s\".",
            parser->FilePath,
            parser->LineIndex + 1,
            parser->Text + parser->TextIndex);
    }

    return Error_Construct3(
        ErrorCode_InvalidUnicodeData,
        u8"Invalid Unicode data on line %zu, expected either a constant numeric value or division without spaces, got \"%s\".",
        parser->LineIndex + 1,
        parser->Text + parser->TextIndex);
}

static Error LoadTextFromPath(const unsigned char* dataBaseFilePath, GenericBuffer* textBuffer)
{
    Error Result = Error_CreateSuccess();

    CreateGrowableByteBuffer(textBuffer);
    Result = FileSystem_ReadAllText(dataBaseFilePath, textBuffer);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(textBuffer->_data);
        textBuffer->_data = NULL;
        textBuffer->_capacity = 0;
        textBuffer->_count = 0;
    }

    return Result;
}

static Error LoadTextFromStream(IOStream* stream, GenericBuffer* textBuffer)
{
    Error Result = Error_CreateSuccess();

    CreateGrowableByteBuffer(textBuffer);
    Result = IOStream_ReadAll(stream, textBuffer);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(textBuffer->_data);
        textBuffer->_data = NULL;
        textBuffer->_capacity = 0;
        textBuffer->_count = 0;
        return Result;
    }

    if (!GenericBuffer_NullTerminate(textBuffer))
    {
        Memory_Free(textBuffer->_data);
        textBuffer->_data = NULL;
        textBuffer->_capacity = 0;
        textBuffer->_count = 0;
        return Error_Construct3(
            ErrorCode_BufferTooSmall,
            u8"Failed to null terminate the Unicode stream text buffer.");
    }

    return Error_CreateSuccess();
}

static void MarkSectionEnd(UnicodeParser* parser, bool* isFileEnd, bool* isLineEnd, size_t* sectionLength)
{
    size_t LocalIndex = parser->TextIndex;
    unsigned char* Text = parser->Text;

    while (!IsCharSectionEnd(Text[LocalIndex]))
    {
        LocalIndex++;
    }

    *isFileEnd = (parser->Text[LocalIndex] == '\0');
    *isLineEnd = (parser->Text[LocalIndex] == '\n');
    *sectionLength = LocalIndex - parser->TextIndex;
    parser->Text[LocalIndex] = '\0';
}

static inline bool IsLastSection(size_t sectionIndex)
{
    return sectionIndex >= (SECTION_COUNT - 1);
}

static bool SkipSection(UnicodeParser* parser, size_t sectionIndex)
{
    size_t LocalIndex = parser->TextIndex;
    unsigned char* Text = parser->Text;

    while (!IsCharSectionEnd(Text[LocalIndex]))
    {
        LocalIndex++;
    }

    parser->TextIndex = LocalIndex;

    if (!IsLastSection(sectionIndex) && (Text[LocalIndex] != SEPARATOR))
    {
        return false;
    }
    if (Text[parser->TextIndex] == SEPARATOR)
    {
        parser->TextIndex++;
    }

    return true;
}

static bool SkipUntilNonWhitespace(UnicodeParser* parser)
{
    const unsigned char* Text = parser->Text;
    size_t LocalIndex = parser->TextIndex;
    while ((Text[LocalIndex] == ' ') || (Text[LocalIndex] == NEWLINE))
    {
        LocalIndex++;
    }

    parser->TextIndex = LocalIndex;
    return GetParserChar(parser) != '\0';
}

static Error ParseSingleLine(UnicodeParser* parser, bool* isFileEnd, bool* wasLineParsed)
{
    *wasLineParsed = false;

    if (!SkipUntilNonWhitespace(parser))
    {
        *isFileEnd = true;
        return Error_CreateSuccess();
    }

    EnsureUnicodeDataCapacity(parser, parser->DataCount + 1);
    parser->Data[parser->DataCount] = (UnicodeCharacter)
    {
        ._codepoint = CODEPOINT_NONE,
        ._lowerMapping = CODEPOINT_NONE,
        ._upperMapping = CODEPOINT_NONE,
        ._category = CodePointCategory_None,
        ._flags = CharacterFlags_None,
        ._numericValue = NAN,
    };

    for (size_t i = 0; i < SECTION_COUNT; i++)
    {
        LineParseAction Action = PARSE_ACTIONS[i];
        if (Action.IsSkipped || (Action.ParseCallback == NULL))
        {
            bool WasSkipValid = SkipSection(parser, i);
            if (!WasSkipValid)
            {
                *isFileEnd = GetParserChar(parser) == '\0';
                return CreateIncompleteLineError(parser, i);
            }
        }
        else
        {
            bool IsLineEnd = false;
            size_t SectionLength = 0;
            MarkSectionEnd(parser, isFileEnd, &IsLineEnd, &SectionLength);
            if ((IsLineEnd || *isFileEnd) && !IsLastSection(i))
            {
                return CreateIncompleteLineError(parser, i);
            }

            Error Result = (*Action.ParseCallback)(parser);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }

            parser->TextIndex += SectionLength;
            if (!*isFileEnd)
            {
                parser->TextIndex++;
            }
        }
    }

    CodePoint ThisCodepoint = parser->Data[parser->DataCount]._codepoint;
    if (ThisCodepoint > parser->MaxCodePoint)
    {
        parser->MaxCodePoint = ThisCodepoint;
    }

    parser->DataCount++;
    *wasLineParsed = true;
    *isFileEnd = GetParserChar(parser) == '\0';
    return Error_CreateSuccess();
}

static Error ParseUnicodeData(UnicodeParser* parser)
{
    bool IsFileEnd = false;
    do
    {
        bool WasLineParsed = false;
        Error Result = ParseSingleLine(parser, &IsFileEnd, &WasLineParsed);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (WasLineParsed)
        {
            parser->LineIndex++;
        }
    } while (!IsFileEnd);

    return Error_CreateSuccess();
}

static Error ParseCategory(UnicodeParser* parser)
{
    CodePointCategory Category;
    const unsigned char* SourceText = parser->Text + parser->TextIndex;

    if (IsCategoryText(SourceText, 'L', 'u'))
    {
        Category = CodePointCategory_UppercaseLetter;
    }
    else if (IsCategoryText(SourceText, 'L', 'l'))
    {
        Category = CodePointCategory_LowercaseLetter;
    }
    else if (IsCategoryText(SourceText, 'L', 't'))
    {
        Category = CodePointCategory_TitlecaseLetter;
    }
    else if (IsCategoryText(SourceText, 'L', 'm'))
    {
        Category = CodePointCategory_ModifiedLetter;
    }
    else if (IsCategoryText(SourceText, 'L', 'o'))
    {
        Category = CodePointCategory_OtherLetter;
    }
    else if (IsCategoryText(SourceText, 'M', 'n'))
    {
        Category = CodePointCategory_NonspacingMark;
    }
    else if (IsCategoryText(SourceText, 'M', 'c'))
    {
        Category = CodePointCategory_SpacingMark;
    }
    else if (IsCategoryText(SourceText, 'M', 'e'))
    {
        Category = CodePointCategory_EnclosingMark;
    }
    else if (IsCategoryText(SourceText, 'N', 'd'))
    {
        Category = CodePointCategory_DecimalNumber;
    }
    else if (IsCategoryText(SourceText, 'N', 'l'))
    {
        Category = CodePointCategory_LetterNumber;
    }
    else if (IsCategoryText(SourceText, 'N', 'o'))
    {
        Category = CodePointCategory_OtherNumber;
    }
    else if (IsCategoryText(SourceText, 'P', 'c'))
    {
        Category = CodePointCategory_ConnectorPunctuation;
    }
    else if (IsCategoryText(SourceText, 'P', 'd'))
    {
        Category = CodePointCategory_DashPunctuation;
    }
    else if (IsCategoryText(SourceText, 'P', 's'))
    {
        Category = CodePointCategory_OpenPunctuation;
    }
    else if (IsCategoryText(SourceText, 'P', 'e'))
    {
        Category = CodePointCategory_ClosePunctuation;
    }
    else if (IsCategoryText(SourceText, 'P', 'i'))
    {
        Category = CodePointCategory_InitialPunctuation;
    }
    else if (IsCategoryText(SourceText, 'P', 'f'))
    {
        Category = CodePointCategory_FinalPunctuation;
    }
    else if (IsCategoryText(SourceText, 'P', 'o'))
    {
        Category = CodePointCategory_OtherPunctuation;
    }
    else if (IsCategoryText(SourceText, 'S', 'm'))
    {
        Category = CodePointCategory_Math_Symbol;
    }
    else if (IsCategoryText(SourceText, 'S', 'c'))
    {
        Category = CodePointCategory_CurrencySymbol;
    }
    else if (IsCategoryText(SourceText, 'S', 'k'))
    {
        Category = CodePointCategory_ModifierSymbol;
    }
    else if (IsCategoryText(SourceText, 'S', 'o'))
    {
        Category = CodePointCategory_OtherSymbol;
    }
    else if (IsCategoryText(SourceText, 'Z', 's'))
    {
        Category = CodePointCategory_SpaceSeparator;
    }
    else if (IsCategoryText(SourceText, 'Z', 'l'))
    {
        Category = CodePointCategory_LineSeparator;
    }
    else if (IsCategoryText(SourceText, 'Z', 'p'))
    {
        Category = CodePointCategory_ParagraphSeparator;
    }
    else if (IsCategoryText(SourceText, 'C', 'c'))
    {
        Category = CodePointCategory_Control;
    }
    else if (IsCategoryText(SourceText, 'C', 'f'))
    {
        Category = CodePointCategory_Format;
    }
    else if (IsCategoryText(SourceText, 'C', 's'))
    {
        Category = CodePointCategory_Surrogate;
    }
    else if (IsCategoryText(SourceText, 'C', 'o'))
    {
        Category = CodePointCategory_Private_Use;
    }
    else if (IsCategoryText(SourceText, 'C', 'n'))
    {
        Category = CodePointCategory_Unassigned;
    }
    else
    {
        return Error_Construct3(
            ErrorCode_InvalidUnicodeData,
            u8"Invalid unicode category \"%s\" on line %zu.",
            SourceText,
            parser->LineIndex + 1);
    }

    parser->Data[parser->DataCount]._category = Category;
    return Error_CreateSuccess();
}

static Error StringToCodePoint(UnicodeParser* parser,
    const unsigned char* str,
    CodePoint* codepoint,
    const unsigned char* context,
    bool isInvalidAllowed)
{
    unsigned char* End = NULL;

    *codepoint = CODEPOINT_NONE;
    CodePoint Value = (CodePoint)strtol((const char*)str, (char**)&End, NUMBER_BASE);
    if (End == str)
    {
        if (isInvalidAllowed)
        {
            return Error_CreateSuccess();
        }

        return CreateMalformedHexadecimalError(parser, str, context);
    }
    if (*End != '\0')
    {
        return CreateMalformedHexadecimalError(parser, str, context);
    }

    *codepoint = Value;
    return Error_CreateSuccess();
}

static Error StringToFloat(UnicodeParser* parser,
    const unsigned char* str,
    float* value,
    const unsigned char* context)
{
    unsigned char* End = NULL;

    *value = NAN;
    float Value = strtof((const char*)str, (char**)&End);
    if (End == str)
    {
        return CreateMalformedDecimalError(parser, str, context);
    }
    if (*End != '\0')
    {
        return CreateMalformedDecimalError(parser, str, context);
    }

    *value = Value;
    return Error_CreateSuccess();
}

static size_t ReadNumberIntoBuffer(const unsigned char* source, size_t startIndex, unsigned char* buffer, size_t bufferSize)
{
    size_t MaxBufferIndex = bufferSize - 2;
    size_t LocalIndex = startIndex;

    for (size_t i = 0;
        (i < MaxBufferIndex) && !IsCharSectionEnd(source[LocalIndex]) && (source[LocalIndex] != DIVIDER);
        i++, LocalIndex++)
    {
        buffer[i] = source[LocalIndex];
    }

    size_t ReadChars = LocalIndex - startIndex;
    buffer[ReadChars] = '\0';
    return ReadChars;
}

static Error ParseNumericValue(UnicodeParser* parser)
{
    float* Value = &parser->Data[parser->DataCount]._numericValue;
    if (IsCharSectionEnd(GetParserChar(parser)))
    {
        *Value = NAN;
        return Error_CreateSuccess();
    }

    unsigned char Buffer[64];
    size_t LocalIndex = parser->TextIndex;

    LocalIndex += ReadNumberIntoBuffer(parser->Text, LocalIndex, Buffer, sizeof(Buffer));
    float NumberA = NAN;
    Error Result = StringToFloat(parser, Buffer, &NumberA, u8"First or only number value.");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (IsCharSectionEnd(parser->Text[LocalIndex]))
    {
        *Value = NumberA;
        return Error_CreateSuccess();
    }
    if (parser->Text[LocalIndex] != DIVIDER)
    {
        return CreateInvalidNumericFormatError(parser);
    }

    LocalIndex++;
    ReadNumberIntoBuffer(parser->Text, LocalIndex, Buffer, sizeof(Buffer));
    float NumberB = NAN;
    Result = StringToFloat(parser, Buffer, &NumberB, u8"Second number value (denominator).");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *Value = (NumberB == 0.0f) ? NAN : (NumberA / NumberB);
    return Error_CreateSuccess();
}

static Error ParseUppercaseMapping(UnicodeParser* parser)
{
    return StringToCodePoint(parser,
        parser->Text + parser->TextIndex,
        &parser->Data[parser->DataCount]._upperMapping,
        u8"uppercase mapping",
        true);
}

static Error ParseLowercaseMapping(UnicodeParser* parser)
{
    return StringToCodePoint(parser,
        parser->Text + parser->TextIndex,
        &parser->Data[parser->DataCount]._lowerMapping,
        u8"lowercase mapping",
        true);
}

static Error ParseCodePoint(UnicodeParser* parser)
{
    return StringToCodePoint(parser,
        parser->Text + parser->TextIndex,
        &parser->Data[parser->DataCount]._codepoint,
        u8"codepoint",
        false);
}

static void EnsureNullTerminatorInDatabase(UnicodeData* data)
{
    data->_characters[0] = (UnicodeCharacter)
    {
        ._category = CodePointCategory_Control,
        ._codepoint = 0,
        ._flags = CharacterFlags_None,
        ._numericValue = NAN,
        ._lowerMapping = CODEPOINT_NONE,
        ._upperMapping = CODEPOINT_NONE
    };
}

static void LoadParsedUnicodeIntoTable(UnicodeCharacter* characters,
    size_t characterCount,
    CodePoint maxCodePoint,
    UnicodeData* data)
{
    size_t CodepointCount = 0;
    if (maxCodePoint < 1)
    {
        CodepointCount = 1;
    }
    else
    {
        CodepointCount = (size_t)maxCodePoint + 1;
    }
    if (CodepointCount > MAX_CODEPOINTS)
    {
        CodepointCount = MAX_CODEPOINTS;
    }

    data->_characters = Memory_Allocate(CodepointCount * sizeof(UnicodeCharacter));

    for (size_t i = 0; i < CodepointCount; i++)
    {
        UnicodeCharacter* Character = &data->_characters[i];
        Memory_Zero(Character, sizeof(*Character));
        Character->_codepoint = CODEPOINT_NONE;
        Character->_category = CodePointCategory_None;
        Character->_lowerMapping = CODEPOINT_NONE;
        Character->_upperMapping = CODEPOINT_NONE;
        Character->_numericValue = NAN;
    }

    EnsureNullTerminatorInDatabase(data);

    for (size_t i = 0; i < characterCount; i++)
    {
        CodePoint TargetCodePoint = characters[i]._codepoint;
        if ((TargetCodePoint < 0) || ((size_t)TargetCodePoint >= CodepointCount))
        {
            continue;
        }

        data->_characters[(size_t)TargetCodePoint] = characters[i];
    }

    data->_characterCount = CodepointCount;
}

static Error ParseIsDigit(UnicodeParser* parser)
{
    if (IsCharSectionEnd(GetParserChar(parser)))
    {
        return Error_CreateSuccess();
    }

    float DigitValue = NAN;
    Error Result = StringToFloat(parser, parser->Text + parser->TextIndex, &DigitValue, u8"character digit value");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((DigitValue < DIGIT_VALUE_MIN) || (DigitValue > DIGIT_VALUE_MAX))
    {
        return Error_Construct3(
            ErrorCode_InvalidUnicodeData,
            u8"Invalid digit value %f, it must be in the bounds [%f,%f]",
            DigitValue,
            DIGIT_VALUE_MIN,
            DIGIT_VALUE_MAX);
    }

    parser->Data[parser->DataCount]._flags |= CharacterFlags_IsDigit;
    return Error_CreateSuccess();
}

static Error LoadUnicodeDataFromSource(const unsigned char* filePath, GenericBuffer* textBuffer, UnicodeData* data)
{
    UnicodeParser Parser;
    Error Result = InitParser(filePath, textBuffer, &Parser);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(textBuffer->_data);
        return Result;
    }

    Result = ParseUnicodeData(&Parser);
    if (Result.Code != ErrorCode_Success)
    {
        DeinitParser(&Parser);
        return Result;
    }

    LoadParsedUnicodeIntoTable(Parser.Data, Parser.DataCount, Parser.MaxCodePoint, data);
    DeinitParser(&Parser);
    return Error_CreateSuccess();
}


// Public functions.
Error UnicodeData_Load(const unsigned char* dataBaseFilePath, UnicodeData* data)
{
    GenericBuffer TextBuffer;
    Error Result = Error_CreateSuccess();

    if (dataBaseFilePath == NULL)
    {
        return Error_Construct3(
            ErrorCode_IllegalArgument,
            u8"UnicodeData_Load requires a database file path.");
    }
    if (data == NULL)
    {
        return Error_Construct3(
            ErrorCode_IllegalArgument,
            u8"UnicodeData_Load requires destination data.");
    }

    Result = LoadTextFromPath(dataBaseFilePath, &TextBuffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return LoadUnicodeDataFromSource(dataBaseFilePath, &TextBuffer, data);
}

Error UnicodeData_LoadFromStream(IOStream* stream, UnicodeData* data)
{
    GenericBuffer TextBuffer;
    Error Result = Error_CreateSuccess();

    if (stream == NULL)
    {
        return Error_Construct3(
            ErrorCode_IllegalArgument,
            u8"UnicodeData_LoadFromStream requires a stream.");
    }
    if (data == NULL)
    {
        return Error_Construct3(
            ErrorCode_IllegalArgument,
            u8"UnicodeData_LoadFromStream requires destination data.");
    }

    Result = LoadTextFromStream(stream, &TextBuffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return LoadUnicodeDataFromSource(NULL, &TextBuffer, data);
}

Error UnicodeData_CreateEmpty(UnicodeData* data)
{
    if (data == NULL)
    {
        return Error_Construct3(
            ErrorCode_IllegalArgument,
            u8"UnicodeData_CreateEmpty requires destination data.");
    }

    LoadParsedUnicodeIntoTable(NULL, 0, 0, data);
    return Error_CreateSuccess();
}

void UnicodeData_Deconstruct(UnicodeData* data)
{
    if (data == NULL)
    {
        return;
    }

    if (data->_characters != NULL)
    {
        Memory_Free(data->_characters);
    }

    data->_characters = NULL;
    data->_characterCount = 0;
}
