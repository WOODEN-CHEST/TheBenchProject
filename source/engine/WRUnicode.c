#include "WRUnicode.h"
#include <math.h>
#include "WRCompile.h"

// Fields.
const CodePoint CODEPOINT_NONE = -1;


static const CodePointCategory LETTER_CATEGORIES[] = 
{
    CodePointCategory_UppercaseLetter,
    CodePointCategory_LowercaseLetter,
    CodePointCategory_TitlecaseLetter,
    CodePointCategory_ModifiedLetter,
    CodePointCategory_OtherLetter,
};

static const CodePointCategory NUMBER_CATEGORIES[] = 
{
    CodePointCategory_DecimalNumber,
    CodePointCategory_LetterNumber,
    CodePointCategory_OtherNumber,
};

static const CodePointCategory MARK_CATEGORIES[] = 
{
    CodePointCategory_NonspacingMark,
    CodePointCategory_SpacingMark,
    CodePointCategory_EnclosingMark,
};

static const CodePointCategory PUNCTUATION_CATEGORIES[] = 
{

    CodePointCategory_ConnectorPunctuation,
    CodePointCategory_DashPunctuation,
    CodePointCategory_OpenPunctuation,
    CodePointCategory_ClosePunctuation,
    CodePointCategory_InitialPunctuation,
    CodePointCategory_FinalPunctuation,
    CodePointCategory_OtherPunctuation,
};

static const CodePointCategory SYMBOL_CATEGORIES[] = 
{
    CodePointCategory_Math_Symbol,
    CodePointCategory_CurrencySymbol,
    CodePointCategory_ModifierSymbol,
    CodePointCategory_OtherSymbol,
};

static const CodePointCategory SEPARATOR_CATEGORIES[] = 
{
    CodePointCategory_SpaceSeparator,
    CodePointCategory_LineSeparator,
    CodePointCategory_ParagraphSeparator,
};

static const CodePointCategory OTHER_CATEGORIES[] = 
{
    CodePointCategory_Control,
    CodePointCategory_Format,
    CodePointCategory_Surrogate,
    CodePointCategory_Private_Use,
    CodePointCategory_Unassigned,
};

// https://en.wikipedia.org/wiki/Whitespace_character
static const CodePoint WHITESPACE_CHARACTERS[] =
{
    0x9,
    0xA,
    0xB,
    0xC,
    0xD,
    0x20,
    0x85,
    0xA0,
    0x1680,
    0x2000,
    0x2001,
    0x2002,
    0x2003,
    0x2004,
    0x2005,
    0x2006,
    0x2007,
    0x2008,
    0x2009,
    0x200A,
    0x2028,
    0x2029,
    0x202F,
    0x205F,
    0x3000,
};


// Static functions.
static inline bool IsCodepointInDataBounds(UnicodeData* data, CodePoint codepoint)
{
    if (data == NULL)
    {
        return false;
    }

    return (codepoint == 0)
        || ((codepoint >= 0)
        && ((size_t)codepoint < data->_characterCount)
        && (data->_characters[codepoint]._codepoint != CODEPOINT_NONE));
}

static inline UnicodeCharacter* GetCharacter(UnicodeData* data, CodePoint codepoint)
{
    if (!IsCodepointInDataBounds(data, codepoint))
    {
        return NULL;
    }
    return &data->_characters[codepoint];
}

static bool IsInCategory(UnicodeData* data, CodePoint codepoint, const CodePointCategory* categories, size_t count)
{
    CodePointCategory Category = Unicode_GetCategory(data, codepoint);
    for (size_t i = 0; i < count; i++)
    {
        if (categories[i] == Category)
        {
            return true;
        }
    }
    return false;
}

static inline size_t GetCategoryArrayCount(size_t size)
{
    return size / sizeof(CodePointCategory);
}


// Functions.
CodePoint Unicode_ToLower(UnicodeData* data, CodePoint codepoint)
{
    UnicodeCharacter* Character = GetCharacter(data, codepoint);
    if (Character)
    {
        CodePoint LowerMapping = Character->_lowerMapping;
        return IsCodepointInDataBounds(data, LowerMapping) ? LowerMapping : codepoint;
    }
    return CODEPOINT_NONE;
}

CodePoint Unicode_ToUpper(UnicodeData* data, CodePoint codepoint)
{
    UnicodeCharacter* Character = GetCharacter(data, codepoint);
    if (Character)
    {
        CodePoint UpperMapping = Character->_upperMapping;
        return IsCodepointInDataBounds(data, UpperMapping) ? UpperMapping : codepoint;
    }
    return CODEPOINT_NONE;
}

bool Unicode_IsLetter(UnicodeData* data, CodePoint codepoint)
{
    return IsInCategory(data, codepoint, LETTER_CATEGORIES, GetCategoryArrayCount(sizeof(LETTER_CATEGORIES)));
}

bool Unicode_IsDigit(UnicodeData* data, CodePoint codepoint)
{
    UnicodeCharacter* Character = GetCharacter(data, codepoint);
    if (Character)
    {
        return Character->_flags & CharacterFlags_IsDigit;
    }
    return false;
}

bool Unicode_IsNumber(UnicodeData* data, CodePoint codepoint)
{
    return IsInCategory(data, codepoint, NUMBER_CATEGORIES, GetCategoryArrayCount(sizeof(NUMBER_CATEGORIES)));
}

bool Unicode_GetNumericValue(UnicodeData* data, CodePoint codepoint, float* value)
{
    if (value == NULL)
    {
        return false;
    }

    *value = NAN;
    UnicodeCharacter* Character = GetCharacter(data, codepoint);
    if (Character)
    {
        float NumberValue = Character->_numericValue;
        *value = NumberValue;
        return !isnan(NumberValue);
    }
    return false;
}

bool Unicode_IsSymbol(UnicodeData* data, CodePoint codepoint)
{
    return IsInCategory(data, codepoint, SYMBOL_CATEGORIES, GetCategoryArrayCount(sizeof(SYMBOL_CATEGORIES)));
}

bool Unicode_IsMark(UnicodeData* data, CodePoint codepoint)
{
    return IsInCategory(data, codepoint, MARK_CATEGORIES, GetCategoryArrayCount(sizeof(MARK_CATEGORIES)));
}

bool Unicode_IsSeparator(UnicodeData* data, CodePoint codepoint)
{
    return IsInCategory(data, codepoint, SEPARATOR_CATEGORIES, GetCategoryArrayCount(sizeof(SEPARATOR_CATEGORIES)));
}

bool Unicode_IsWhitespace(UnicodeData* data, CodePoint codepoint)
{
    if (!IsCodepointInDataBounds(data, codepoint))
    {
        return false;
    }

    for (size_t i = 0; i < sizeof(WHITESPACE_CHARACTERS) / sizeof(CodePoint); i++)
    {
        if (WHITESPACE_CHARACTERS[i] == codepoint)
        {
            return true;
        }
    }
    return false;
}

bool Unicode_IsPunctuation(UnicodeData* data, CodePoint codepoint)
{
    return IsInCategory(data, codepoint, PUNCTUATION_CATEGORIES, GetCategoryArrayCount(sizeof(PUNCTUATION_CATEGORIES)));
}

bool Unicode_IsUpper(UnicodeData* data, CodePoint codepoint)
{
    return Unicode_GetCategory(data, codepoint) == CodePointCategory_UppercaseLetter;
}

bool Unicode_IsLower(UnicodeData* data, CodePoint codepoint)
{
    return Unicode_GetCategory(data, codepoint) == CodePointCategory_LowercaseLetter;
}

bool Unicode_IsCased(UnicodeData* data, CodePoint codepoint)
{
    return Unicode_IsLower(data, codepoint) || Unicode_IsUpper(data, codepoint);
}

bool Unicode_IsASCII(UnicodeData* data, CodePoint codepoint)
{
    return (codepoint <= 127) && IsCodepointInDataBounds(data, codepoint);
}

bool Unicode_IsASCIILetter(UnicodeData* data, CodePoint codepoint)
{
    bool IsLowerLetter = ('a' <= codepoint) && (codepoint <= 'z');
    bool IsUpperLetter = ('A' <= codepoint) && (codepoint <= 'Z');

    return (IsLowerLetter || IsUpperLetter)
        && IsCodepointInDataBounds(data, codepoint);
}

bool Unicode_IsASCIIDigit(UnicodeData* data, CodePoint codepoint)
{
    bool IsDigit = ('0' <= codepoint) && (codepoint <= '9');
    return IsDigit && IsCodepointInDataBounds(data, codepoint);
}

bool Unicode_IsControl(UnicodeData* data, CodePoint codepoint)
{
    return Unicode_GetCategory(data, codepoint) == CodePointCategory_Control;
}

bool Unicode_IsOtherCategory(UnicodeData* data, CodePoint codepoint)
{
    return IsInCategory(data, codepoint, OTHER_CATEGORIES, GetCategoryArrayCount(sizeof(OTHER_CATEGORIES)));
}

bool Unicode_EqualsCaseIgnore(UnicodeData* data, CodePoint codepoint1, CodePoint codepoint2)
{
    if (!Unicode_IsDefined(data, codepoint1) || !Unicode_IsDefined(data, codepoint2))
    {
        return false;
    }

    if (codepoint1 == codepoint2)
    {
        return true;
    }

    return (Unicode_ToLower(data, codepoint1) == Unicode_ToLower(data, codepoint2))
        || (Unicode_ToUpper(data, codepoint1) == Unicode_ToUpper(data, codepoint2));
}

CodePointCategory Unicode_GetCategory(UnicodeData* data, CodePoint codepoint)
{
    UnicodeCharacter* Character = GetCharacter(data, codepoint);
    if (Character)
    {
        return Character->_category;
    }
    return CodePointCategory_None;
}

bool Unicode_IsDefined(UnicodeData* data, CodePoint codepoint)
{
    return IsCodepointInDataBounds(data, codepoint);
}
