#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"

// https://www.unicode.org/reports/tr44/


// Types.
typedef int32_t CodePoint;

typedef enum CodePointCategoryEnum
{
    CodePointCategory_None,

    CodePointCategory_UppercaseLetter,
    CodePointCategory_LowercaseLetter,
    CodePointCategory_TitlecaseLetter,
    CodePointCategory_ModifiedLetter,
    CodePointCategory_OtherLetter,

    CodePointCategory_NonspacingMark,
    CodePointCategory_SpacingMark,
    CodePointCategory_EnclosingMark,

    CodePointCategory_DecimalNumber,
    CodePointCategory_LetterNumber,
    CodePointCategory_OtherNumber,

    CodePointCategory_ConnectorPunctuation,
    CodePointCategory_DashPunctuation,
    CodePointCategory_OpenPunctuation,
    CodePointCategory_ClosePunctuation,
    CodePointCategory_InitialPunctuation,
    CodePointCategory_FinalPunctuation,
    CodePointCategory_OtherPunctuation,

    CodePointCategory_Math_Symbol,
    CodePointCategory_CurrencySymbol,
    CodePointCategory_ModifierSymbol,
    CodePointCategory_OtherSymbol,

    CodePointCategory_SpaceSeparator,
    CodePointCategory_LineSeparator,
    CodePointCategory_ParagraphSeparator,

    CodePointCategory_Control,
    CodePointCategory_Format,
    CodePointCategory_Surrogate,
    CodePointCategory_Private_Use,
    CodePointCategory_Unassigned,
} CodePointCategory;

typedef struct UnicodeCharacterStruct
{
    CodePoint _codepoint;
    CodePoint _lowerMapping;
    CodePoint _upperMapping;
    CodePointCategory _category;
    float _numericValue;
} UnicodeCharacter;

typedef struct UnicodeDataStruct
{
    UnicodeCharacter* _characters;
    size_t _characterCount;
} UnicodeData;


// Constants.
extern const CodePoint CODEPOINT_NONE;


// Functions.
CodePoint Unicode_ToLower(UnicodeData* data, CodePoint codepoint);

CodePoint Unicode_ToUpper(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsLetter(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsDigit(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsNumber(UnicodeData* data, CodePoint codepoint);

bool Unicode_GetNumericValue(UnicodeData* data, CodePoint codepoint, float* value);

bool Unicode_IsSymbol(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsMark(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsSeparator(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsWhitespace(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsPunctuation(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsUpper(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsLower(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsCased(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsASCII(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsASCIILetter(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsASCIIDigit(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsControl(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsOtherCategory(UnicodeData* data, CodePoint codepoint);

bool Unicode_EqualsCaseIgnore(UnicodeData* data, CodePoint codepoint1, CodePoint codepoint2);

CodePointCategory Unicode_GetCategory(UnicodeData* data, CodePoint codepoint);

bool Unicode_IsDefined(UnicodeData* data, CodePoint codepoint);