#include "WRError.h"
#include "WRUnicode.h"
#include "WRUnicodeLoader.h"
#include "WRMemory.h"
#include <stdint.h>
#include <stddef.h>
#include "raylib.h"
#include <stdlib.h>
#include <stdio.h>


// Types.
typedef struct UnicodeParserStruct
{
    const unsigned char* FilePath;

    unsigned char* Text;
    size_t TextIndex;
    size_t LineIndex;

    size_t DataCapacity;
    UnicodeCharacter* Data;
    size_t DataCount;

    ErrorMessagePool* ErrorPool;
} UnicodeParser;

typedef Error (*LineParseCallback)(UnicodeParser* parser);

typedef struct LineParseActionStruct
{
    bool IsSkipped;
    LineParseCallback ParseCallback;
} LineParseAction;


// Function declarations.
static Error ParseCategory(UnicodeParser* parser);

static Error ParseNumericValue(UnicodeParser* parser);

static Error ParseUppercaseMapping(UnicodeParser* parser);

static Error ParseLowercaseMapping(UnicodeParser* parser);

static Error ParseCodePoint(UnicodeParser* parser);


// Fields.
static const size_t UNICODE_DATA_CAPACITY_DEFAULT = 2 << 15; // If the unicode data file text doesn't change much, then this should be large enough.
static const size_t UNICODE_DATA_CAPACITY_GROWTH = 2;
static const char SEPARATOR = ';';
static const char NEWLINE = '\n';
static const int32_t NUMBER_BASE = 16;

static const size_t SECTION_COUNT = 14;

static LineParseAction PARSE_ACTIONS[] = 
{
    { false, &ParseCodePoint, },
    { false, NULL },
    { false, &ParseCategory, },
    { true, NULL, },
    { true, NULL, },
    { true, NULL, },
    { true, NULL, },
    { true, NULL, },
    { false, &ParseNumericValue, },
    { true, NULL, },
    { true, NULL, },
    { true, NULL, },
    { false, &ParseUppercaseMapping, },
    { false, &ParseLowercaseMapping, },
    { true, NULL, },
};


// Static functions.
static inline char GetParserChar(UnicodeParser* parser)
{
    return parser->Text[parser->TextIndex];
}

static inline bool IsCharSectionEnd(char character)
{
    return (character == SEPARATOR) || (character == NEWLINE) || (character == '\0');
}

static void EnsureUnicodeDataCapacity(UnicodeParser* parser, size_t capacity)
{
    if (parser->DataCapacity >= capacity)
    {
        return;
    }

    size_t NewCapacity = parser->DataCapacity == 0 ? UNICODE_DATA_CAPACITY_DEFAULT : parser->DataCapacity;
    while (NewCapacity < capacity)
    {
        NewCapacity *= UNICODE_DATA_CAPACITY_GROWTH;
    }

    size_t NewSize = NewCapacity * sizeof(UnicodeCharacter);
    parser->Data = parser->Data ? Memory_Reallocate(parser->Data, NewSize) : Memory_Allocate(NewSize);
    parser->DataCapacity = NewCapacity;
}

static Error InitParser(ErrorMessagePool* errorPool, const unsigned char* dataBaseFilePath, UnicodeParser* parser)
{
    const char* CharFilePath = (const char*)dataBaseFilePath;
    if (!FileExists(CharFilePath))
    {
        return Error_Construct3(errorPool, 
            ErrorCode_FileNotFound, 
            u8"Unicode data file \"%s\" not found.");
    }

    char* Text = LoadFileText(CharFilePath);
    if (!Text)
    {
        return Error_Construct3(errorPool, 
            ErrorCode_IO, 
            u8"Failed to read Unicode data file \"%s\" due to an unknown reason.");
    }

    parser->Text = (unsigned char*)Text;
    parser->TextIndex = 0;
    parser->Data = NULL;
    parser->DataCapacity = 0;
    parser->DataCount = 0;
    parser->ErrorPool = errorPool;
    parser->FilePath = dataBaseFilePath;
    parser->LineIndex = 0;
    EnsureUnicodeDataCapacity(parser, UNICODE_DATA_CAPACITY_DEFAULT);

    return Error_CreateSuccess();
}

static void MarkSectionEnd(UnicodeParser* parser, bool* isFileEnd, bool* isLineEnd)
{
    size_t LocalIndex = parser->TextIndex;
    unsigned char* Text = parser->Text;

    while (!IsCharSectionEnd(Text[LocalIndex]))
    {
        LocalIndex++;
    }

    *isFileEnd = parser->Text[LocalIndex] == '\0';
    *isLineEnd = parser->Text[LocalIndex] == '\n';
    parser->Text[LocalIndex] = '\0';
}

static inline bool IsLastSection(size_t sectionIndex)
{
    return sectionIndex >= SECTION_COUNT - 1;
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

static Error CreateIncompleteLineError(UnicodeParser* parser, size_t sectionIndex)
{
    return Error_Construct3(parser->ErrorPool, ErrorCode_InvalidUnicodeData,
        u8"Malformed Unicode file \"%s\", expected %zu sections at line %zu, got %zu instead.",
        parser->FilePath, SECTION_COUNT, parser->LineIndex + 1, sectionIndex + 1);
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

static Error ParseSingleLine(UnicodeParser* parser, bool* isFileEnd)
{
    SkipUntilNonWhitespace(parser);
    if (GetParserChar(parser) == '\0')
    {
        return Error_CreateSuccess();
    }

    size_t SectionIndex = 0;
    for (size_t i = 0; i < SECTION_COUNT; i++)
    {
        LineParseAction Action = PARSE_ACTIONS[i];
        if (Action.IsSkipped || !Action.ParseCallback)
        {
            bool WasSkipValid = SkipSection(parser, SectionIndex);
            if (!WasSkipValid)
            {
                *isFileEnd = GetParserChar(parser) == '\0';
                return CreateIncompleteLineError(parser, SectionIndex);
            }
        }
        else
        {
            bool IsLineEnd;
            MarkSectionEnd(parser, isFileEnd, &IsLineEnd);
            if ((IsLineEnd || *isFileEnd) && !IsLastSection(SectionIndex))
            {
                return CreateIncompleteLineError(parser, SectionIndex);
            }
            Error Result = (*Action.ParseCallback)(parser);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }

            if (!*isFileEnd)
            {
                parser->TextIndex++;
            }
        }
        SectionIndex++;
    }

    *isFileEnd = GetParserChar(parser) == '\0';
    return Error_CreateSuccess();
}

static Error ParseUnicodeData(UnicodeParser* parser)
{
    bool IsFileEnd;
    do
    {
        Error Result = ParseSingleLine(parser, &IsFileEnd);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        parser->LineIndex++;
        parser->DataCount++;
    } while (!IsFileEnd);

    return Error_CreateSuccess();
}

static Error ParseCategory(UnicodeParser* parser)
{
    CodePointCategory Category;
    const char* SourceText= (const char*)(parser->Text + parser->TextIndex);

    if (TextIsEqual(SourceText, "Lu"))
    {
        Category = CodePointCategory_UppercaseLetter;
    }
    else if (TextIsEqual(SourceText, "Ll"))
    {
        Category = CodePointCategory_LowercaseLetter;
    }
    else if (TextIsEqual(SourceText, "Lt"))
    {
        Category = CodePointCategory_TitlecaseLetter;
    }
    else if (TextIsEqual(SourceText, "Lm"))
    {
        Category = CodePointCategory_ModifiedLetter;
    }
    else if (TextIsEqual(SourceText, "Lo"))
    {
        Category = CodePointCategory_OtherLetter;
    }
    else if (TextIsEqual(SourceText, "Mn"))
    {
        Category = CodePointCategory_NonspacingMark;
    }
    else if (TextIsEqual(SourceText, "Mc"))
    {
        Category = CodePointCategory_SpacingMark;
    }
    else if (TextIsEqual(SourceText, "Me"))
    {
        Category = CodePointCategory_EnclosingMark;
    }
    else if (TextIsEqual(SourceText, "Nd"))
    {
        Category = CodePointCategory_DecimalNumber;
    }
    else if (TextIsEqual(SourceText, "Nl"))
    {
        Category = CodePointCategory_LetterNumber;
    }
    else if (TextIsEqual(SourceText, "No"))
    {
        Category = CodePointCategory_OtherNumber;
    }
    else if (TextIsEqual(SourceText, "Pc"))
    {
        Category = CodePointCategory_ConnectorPunctuation;
    }
    else if (TextIsEqual(SourceText, "Pd"))
    {
        Category = CodePointCategory_DashPunctuation;
    }
    else if (TextIsEqual(SourceText, "Ps"))
    {
        Category = CodePointCategory_OpenPunctuation;
    }
    else if (TextIsEqual(SourceText, "Pe"))
    {
        Category = CodePointCategory_ClosePunctuation;
    }
    else if (TextIsEqual(SourceText, "Pi"))
    {
        Category = CodePointCategory_InitialPunctuation;
    }
    else if (TextIsEqual(SourceText, "Pf"))
    {
        Category = CodePointCategory_FinalPunctuation;
    }
    else if (TextIsEqual(SourceText, "Po"))
    {
        Category = CodePointCategory_OtherPunctuation;
    }
    else if (TextIsEqual(SourceText, "Sm"))
    {
        Category = CodePointCategory_Math_Symbol;
    }
    else if (TextIsEqual(SourceText, "Sc"))
    {
        Category = CodePointCategory_CurrencySymbol;
    }
    else if (TextIsEqual(SourceText, "Sk"))
    {
        Category = CodePointCategory_ModifierSymbol;
    }
    else if (TextIsEqual(SourceText, "So"))
    {
        Category = CodePointCategory_OtherSymbol;
    }
    else if (TextIsEqual(SourceText, "Zs"))
    {
        Category = CodePointCategory_SpaceSeparator;
    }
    else if (TextIsEqual(SourceText, "Zl"))
    {
        Category = CodePointCategory_LineSeparator;
    }
    else if (TextIsEqual(SourceText, "Zp"))
    {
        Category = CodePointCategory_ParagraphSeparator;
    }
    else if (TextIsEqual(SourceText, "Cc"))
    {
        Category = CodePointCategory_Control;
    }
    else if (TextIsEqual(SourceText, "Cf"))
    {
        Category = CodePointCategory_Format;
    }
    else if (TextIsEqual(SourceText, "Cs"))
    {
        Category = CodePointCategory_Surrogate;
    }
    else if (TextIsEqual(SourceText, "Co"))
    {
        Category = CodePointCategory_Private_Use;
    }
    else if (TextIsEqual(SourceText, "Cn"))
    {
        Category = CodePointCategory_Unassigned;
    }
    else
    {
        return Error_Construct3(parser->ErrorPool, 
            ErrorCode_InvalidUnicodeData,
            u8"Invalid unicode category \"%s\" on line %zu.",
            SourceText,
            parser->LineIndex);
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
    unsigned char* End;
    int32_t Value = strtol((const char*)str, (char**)&End, NUMBER_BASE);
    if (End == str)
    {
        if (isInvalidAllowed)
        {
            *codepoint = CODEPOINT_NONE;
            return Error_CreateSuccess();
        }

        return Error_Construct3(parser->ErrorPool,
            ErrorCode_InvalidUnicodeData,
            u8"Malformed Unicode source file \"%s\", expected number at line %zu, got \"%s\" (%s).",
            parser->FilePath,
            parser->LineIndex + 1,
            str,
            context);
    }
    *codepoint = Value;
    return Error_CreateSuccess();
}

static Error ParseNumericValue(UnicodeParser* parser)
{
    parser->Data[parser->DataCount]._numericValue = 0.0f;
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


// Functions.
Error UnicodeData_Load(ErrorMessagePool* errorPool, const unsigned char* dataBaseFilePath, UnicodeData* data)
{
    UnicodeParser Parser;
    Error Result = InitParser(errorPool, dataBaseFilePath, &Parser);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ParseUnicodeData(&Parser);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    data->_characters = Parser.Data;
    data->_characterCount = Parser.DataCount;

    return Error_CreateSuccess();
}

void UnicodeData_Deconstruct(UnicodeData* data)
{
    if (data->_characters)
    {
        Memory_Free(data->_characters);
    }
}