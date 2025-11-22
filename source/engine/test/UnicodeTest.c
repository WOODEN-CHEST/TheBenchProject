#include "test/UnicodeTest.h"
#include "test/EngineTest.h"
#include "WRUnicode.h"
#include <stddef.h>


// Fields.

static const CodePoint IS_LETTER_TRUE[] =
{
    0x0041,  // 'A'
    0x0062,  // 'b'
    0x0394,  // Greek capital delta
    0x03C0   // Greek small pi
};

static const CodePoint IS_LETTER_FALSE[] =
{
    0x0030,  // '0'
    0x0020,  // space
    0x0021,  // '!'
    0x1F600  // grinning face emoji
};


static const CodePoint IS_DIGIT_TRUE[] =
{
    0x0030,  // '0'
    0x0039,  // '9'
    0x0665,  // arabic-indic digit five
    0x06F2   // extended arabic-indic digit two
};

static const CodePoint IS_DIGIT_FALSE[] =
{
    0x0041,  // 'A'
    0x005A,  // 'Z'
    0x0020,  // space
    0x1F600  // grinning face emoji
};


static const CodePoint IS_NUMBER_TRUE[] =
{
    0x0034,  // '4'
    0x0BF0,  // tamil number ten
    0x2153,  // vulgar fraction one third
    0x16EE   // runic numeral seventeen
};

static const CodePoint IS_NUMBER_FALSE[] =
{
    0x0057,  // 'W'
    0x002C,  // ','
    0x0020,  // space
    0x1F923  // rolling on floor laughing emoji
};


static const CodePoint IS_SYMBOL_TRUE[] =
{
    0x002B,  // '+'
    0x00A9,  // copyright sign
    0x20AC,  // euro sign
    0x2605   // black star
};

static const CodePoint IS_SYMBOL_FALSE[] =
{
    0x0043,  // 'C'
    0x0069,  // 'i'
    0x0035,  // '5'
};


static const CodePoint IS_MARK_TRUE[] =
{
    0x0301,  // combining acute accent
    0x094D,  // devanagari sign virama
    0x0E31,  // thai vowel sign mai han-akat
    0x1AB0   // combining arrowhead above
};

static const CodePoint IS_MARK_FALSE[] =
{
    0x0061,  // 'a'
    0x0034,  // '4'
    0x0020,  // space
    0x1F9D0  // face with monocle emoji
};


static const CodePoint IS_SEPARATOR_TRUE[] =
{
    0x0020,  // space
    0x00A0,  // no-break space
    0x1680,  // ogham space mark
    0x2029   // paragraph separator
};

static const CodePoint IS_SEPARATOR_FALSE[] =
{
    0x0048,  // 'H'
    0x0031,  // '1'
    0x0021,  // '!'
    0x1F480  // skull emoji
};


static const CodePoint IS_WHITESPACE_TRUE[] =
{
    0x0009,  // tab
    0x000A,  // line feed
    0x000D,  // carriage return
    0x0020   // space
};

static const CodePoint IS_WHITESPACE_FALSE[] =
{
    0x0041,  // 'A'
    0x0036,  // '6'
    0x002E,  // '.'
    0x1F60E  // smiling face with sunglasses emoji
};


static const CodePoint IS_PUNCTUATION_TRUE[] =
{
    0x002C,  // ','
    0x003F,  // '?'
    0x2014,  // em dash
    0x3001   // ideographic comma
};

static const CodePoint IS_PUNCTUATION_FALSE[] =
{
    0x004B,  // 'K'
    0x0034,  // '4'
    0x0020,  // space
    0x1F642  // slight smile emoji
};


static const CodePoint IS_UPPER_TRUE[] =
{
    0x0041,  // 'A'
    0x005A,  // 'Z'
    0x0391,  // greek capital alpha
    0x0416   // cyrillic capital zhe
};

static const CodePoint IS_UPPER_FALSE[] =
{
    0x0061,  // 'a'
    0x00E9,  // latin small letter e with acute
    0x0032,  // '2'
    0x1F937  // shrug emoji
};


static const CodePoint IS_LOWER_TRUE[] =
{
    0x0061,  // 'a'
    0x007A,  // 'z'
    0x03B1,  // greek small alpha
    0x0430   // cyrillic small a
};

static const CodePoint IS_LOWER_FALSE[] =
{
    0x0042,  // 'B'
    0x0393,  // greek capital gamma
    0x0038,  // '8'
    0x1F47D  // alien emoji
};


static const CodePoint IS_CASED_TRUE[] =
{
    0x0041,  // 'A'
    0x0062,  // 'b'
    0x03A3,  // greek capital sigma
    0x03C3   // greek small sigma
};

static const CodePoint IS_CASED_FALSE[] =
{
    0x0033,  // '3'
    0x00A9,  // copyright sign
    0x2603,  // snowman
    0x1F436  // dog face emoji
};


static const CodePoint IS_ASCII_TRUE[] =
{
    0x0000,
    0x0041,  // 'A'
    0x006F,  // 'o'
    0x007F   // delete
};

static const CodePoint IS_ASCII_FALSE[] =
{
    0x0080,
    0x0394,  // greek capital delta
    0x20AC,  // euro sign
    0x1F984  // unicorn emoji
};


static const CodePoint IS_ASCII_LETTER_TRUE[] =
{
    0x0041,  // 'A'
    0x0058,  // 'X'
    0x0061,  // 'a'
    0x007A   // 'z'
};

static const CodePoint IS_ASCII_LETTER_FALSE[] =
{
    0x0030,  // '0'
    0x005F,  // '_'
    0x00E9,  // latin small e with acute
    0x1F680  // rocket emoji
};


static const CodePoint IS_ASCII_DIGIT_TRUE[] =
{
    0x0030,  // '0'
    0x0035,  // '5'
    0x0039   // '9'
};

static const CodePoint IS_ASCII_DIGIT_FALSE[] =
{
    0x0041,  // 'A'
    0x0067,  // 'g'
    0x002D,  // '-'
    0x1F984  // unicorn emoji
};


static const CodePoint IS_CONTROL_TRUE[] =
{
    0x0000,  // null
    0x0007,  // bell
    0x001B,  // escape
    0x009F   // application program command
};

static const CodePoint IS_CONTROL_FALSE[] =
{
    0x0042,  // 'B'
    0x0064,  // 'd'
    0x0020,  // space
    0x1F920  // cowboy emoji
};


static const CodePoint IS_OTHER_CATEGORY_TRUE[] =
{
    0x200C, // zero width non-joiner (format)
    0x200D, // zero width joiner (format)
    0x2060, // word joiner (format)
    0xFEFF  // zero width no-break space (format)
};

static const CodePoint IS_OTHER_CATEGORY_FALSE[] =
{
    0x0041, // 'A'
    0x0062, // 'b'
    0x0035, // '5'
    0x2605  // black star
};


static const CodePoint IS_DEFINED_TRUE[] =
{
    0x0041,  // 'A'
    0x03BB,  // greek small lambda
    0x2603,  // snowman
    0x1F4A9  // pile of poo emoji
};

static const CodePoint IS_DEFINED_FALSE[] =
{
    0x0378,  // unassigned
    -1   // invalid
};

typedef  bool (*CodePointPredicate)(UnicodeData* unicode, CodePoint codepoint);

typedef struct BoolUnicodeUnitTestStruct
{
    CodePointPredicate _predicate;
    const CodePoint* _codepointsToTest;
    size_t _codePointArraySize;
    bool _expectedResult;
    const unsigned char* _testName;
} BoolUnicodeUnitTest;

BoolUnicodeUnitTest BOOL_UNIT_TESTS[] = 
{
    {
        ._predicate = &Unicode_IsLetter,
        ._codepointsToTest = IS_LETTER_TRUE,
        ._codePointArraySize = sizeof(IS_LETTER_TRUE),
        ._expectedResult = true,
        ._testName = u8"is letter true"
    },
    {
        ._predicate = &Unicode_IsLetter,
        ._codepointsToTest = IS_LETTER_FALSE,
        ._codePointArraySize = sizeof(IS_LETTER_FALSE),
        ._expectedResult = false,
        ._testName = u8"is letter false"
    },
    {
        ._predicate = &Unicode_IsDigit,
        ._codepointsToTest = IS_DIGIT_TRUE,
        ._codePointArraySize = sizeof(IS_DIGIT_TRUE),
        ._expectedResult = true,
        ._testName = u8"is digit true"
    },
    {
        ._predicate = &Unicode_IsDigit,
        ._codepointsToTest = IS_DIGIT_FALSE,
        ._codePointArraySize = sizeof(IS_DIGIT_FALSE),
        ._expectedResult = false,
        ._testName = u8"is digit false"
    },
    {
        ._predicate = &Unicode_IsNumber,
        ._codepointsToTest = IS_NUMBER_TRUE,
        ._codePointArraySize = sizeof(IS_NUMBER_TRUE),
        ._expectedResult = true,
        ._testName = u8"is number true"
    },
    {
        ._predicate = &Unicode_IsNumber,
        ._codepointsToTest = IS_NUMBER_FALSE,
        ._codePointArraySize = sizeof(IS_NUMBER_FALSE),
        ._expectedResult = false,
        ._testName = u8"is number false"
    },
    {
        ._predicate = &Unicode_IsSymbol,
        ._codepointsToTest = IS_SYMBOL_TRUE,
        ._codePointArraySize = sizeof(IS_SYMBOL_TRUE),
        ._expectedResult = true,
        ._testName = u8"is symbol true"
    },
    {
        ._predicate = &Unicode_IsSymbol,
        ._codepointsToTest = IS_SYMBOL_FALSE,
        ._codePointArraySize = sizeof(IS_SYMBOL_FALSE),
        ._expectedResult = false,
        ._testName = u8"is symbol false"
    },
    {
        ._predicate = &Unicode_IsMark,
        ._codepointsToTest = IS_MARK_TRUE,
        ._codePointArraySize = sizeof(IS_MARK_TRUE),
        ._expectedResult = true,
        ._testName = u8"is mark true"
    },
    {
        ._predicate = &Unicode_IsMark,
        ._codepointsToTest = IS_MARK_FALSE,
        ._codePointArraySize = sizeof(IS_MARK_FALSE),
        ._expectedResult = false,
        ._testName = u8"is mark false"
    },
    {
        ._predicate = &Unicode_IsSeparator,
        ._codepointsToTest = IS_SEPARATOR_TRUE,
        ._codePointArraySize = sizeof(IS_SEPARATOR_TRUE),
        ._expectedResult = true,
        ._testName = u8"is separator true"
    },
    {
        ._predicate = &Unicode_IsSeparator,
        ._codepointsToTest = IS_SEPARATOR_FALSE,
        ._codePointArraySize = sizeof(IS_SEPARATOR_FALSE),
        ._expectedResult = false,
        ._testName = u8"is separator false"
    },
    {
        ._predicate = &Unicode_IsWhitespace,
        ._codepointsToTest = IS_WHITESPACE_TRUE,
        ._codePointArraySize = sizeof(IS_WHITESPACE_TRUE),
        ._expectedResult = true,
        ._testName = u8"is whitespace true"
    },
    {
        ._predicate = &Unicode_IsWhitespace,
        ._codepointsToTest = IS_WHITESPACE_FALSE,
        ._codePointArraySize = sizeof(IS_WHITESPACE_FALSE),
        ._expectedResult = false,
        ._testName = u8"is whitespace false"
    },
    {
        ._predicate = &Unicode_IsPunctuation,
        ._codepointsToTest = IS_PUNCTUATION_TRUE,
        ._codePointArraySize = sizeof(IS_PUNCTUATION_TRUE),
        ._expectedResult = true,
        ._testName = u8"is punctuation true"
    },
    {
        ._predicate = &Unicode_IsPunctuation,
        ._codepointsToTest = IS_PUNCTUATION_FALSE,
        ._codePointArraySize = sizeof(IS_PUNCTUATION_FALSE),
        ._expectedResult = false,
        ._testName = u8"is punctuation false"
    },
    {
        ._predicate = &Unicode_IsUpper,
        ._codepointsToTest = IS_UPPER_TRUE,
        ._codePointArraySize = sizeof(IS_UPPER_TRUE),
        ._expectedResult = true,
        ._testName = u8"is upper true"
    },
    {
        ._predicate = &Unicode_IsUpper,
        ._codepointsToTest = IS_UPPER_FALSE,
        ._codePointArraySize = sizeof(IS_UPPER_FALSE),
        ._expectedResult = false,
        ._testName = u8"is upper false"
    },
    {
        ._predicate = &Unicode_IsLower,
        ._codepointsToTest = IS_LOWER_TRUE,
        ._codePointArraySize = sizeof(IS_LOWER_TRUE),
        ._expectedResult = true,
        ._testName = u8"is lower true"
    },
    {
        ._predicate = &Unicode_IsLower,
        ._codepointsToTest = IS_LOWER_FALSE,
        ._codePointArraySize = sizeof(IS_LOWER_FALSE),
        ._expectedResult = false,
        ._testName = u8"is lower false"
    },
    {
        ._predicate = &Unicode_IsCased,
        ._codepointsToTest = IS_CASED_TRUE,
        ._codePointArraySize = sizeof(IS_CASED_TRUE),
        ._expectedResult = true,
        ._testName = u8"is cased true"
    },
    {
        ._predicate = &Unicode_IsCased,
        ._codepointsToTest = IS_CASED_FALSE,
        ._codePointArraySize = sizeof(IS_CASED_FALSE),
        ._expectedResult = false,
        ._testName = u8"is cased false"
    },
    {
        ._predicate = &Unicode_IsASCII,
        ._codepointsToTest = IS_ASCII_TRUE,
        ._codePointArraySize = sizeof(IS_ASCII_TRUE),
        ._expectedResult = true,
        ._testName = u8"is ASCII true"
    },
    {
        ._predicate = &Unicode_IsASCII,
        ._codepointsToTest = IS_ASCII_FALSE,
        ._codePointArraySize = sizeof(IS_ASCII_FALSE),
        ._expectedResult = false,
        ._testName = u8"is ASCII false"
    },
    {
        ._predicate = &Unicode_IsASCIILetter,
        ._codepointsToTest = IS_ASCII_LETTER_TRUE,
        ._codePointArraySize = sizeof(IS_ASCII_LETTER_TRUE),
        ._expectedResult = true,
        ._testName = u8"is ASCII letter true"
    },
    {
        ._predicate = &Unicode_IsASCIILetter,
        ._codepointsToTest = IS_ASCII_LETTER_FALSE,
        ._codePointArraySize = sizeof(IS_ASCII_LETTER_FALSE),
        ._expectedResult = false,
        ._testName = u8"is ASCII letter false"
    },
    {
        ._predicate = &Unicode_IsASCIIDigit,
        ._codepointsToTest = IS_ASCII_DIGIT_TRUE,
        ._codePointArraySize = sizeof(IS_ASCII_DIGIT_TRUE),
        ._expectedResult = true,
        ._testName = u8"is ASCII digit true"
    },
    {
        ._predicate = &Unicode_IsASCIIDigit,
        ._codepointsToTest = IS_ASCII_DIGIT_FALSE,
        ._codePointArraySize = sizeof(IS_ASCII_DIGIT_FALSE),
        ._expectedResult = false,
        ._testName = u8"is ASCII digit false"
    },
    {
        ._predicate = &Unicode_IsControl,
        ._codepointsToTest = IS_CONTROL_TRUE,
        ._codePointArraySize = sizeof(IS_CONTROL_TRUE),
        ._expectedResult = true,
        ._testName = u8"is control true"
    },
    {
        ._predicate = &Unicode_IsControl,
        ._codepointsToTest = IS_CONTROL_FALSE,
        ._codePointArraySize = sizeof(IS_CONTROL_FALSE),
        ._expectedResult = false,
        ._testName = u8"is control false"
    },
    {
        ._predicate = &Unicode_IsOtherCategory,
        ._codepointsToTest = IS_OTHER_CATEGORY_TRUE,
        ._codePointArraySize = sizeof(IS_OTHER_CATEGORY_TRUE),
        ._expectedResult = true,
        ._testName = u8"is other category true"
    },
    {
        ._predicate = &Unicode_IsOtherCategory,
        ._codepointsToTest = IS_OTHER_CATEGORY_FALSE,
        ._codePointArraySize = sizeof(IS_OTHER_CATEGORY_FALSE),
        ._expectedResult = false,
        ._testName = u8"is other category false"
    },
    {
        ._predicate = &Unicode_IsDefined,
        ._codepointsToTest = IS_DEFINED_TRUE,
        ._codePointArraySize = sizeof(IS_DEFINED_TRUE),
        ._expectedResult = true,
        ._testName = u8"is defined true"
    },
    {
        ._predicate = &Unicode_IsDefined,
        ._codepointsToTest = IS_DEFINED_FALSE,
        ._codePointArraySize = sizeof(IS_DEFINED_FALSE),
        ._expectedResult = false,
        ._testName = u8"is defined false"
    }
};

static const CodePoint TO_UPPER_TRUE[] =
{
    0x0061, // 'a'
    0x0062, // 'b'
    0x03BB  // greek small lambda
};

static const CodePoint TO_UPPER_EXPECTED[] =
{
    0x0041, // 'A'
    0x0042, // 'B'
    0x039B  // greek capital lambda
};

static const CodePoint TO_LOWER_TRUE[] =
{
    0x0041, // 'A'
    0x0042, // 'B'
    0x039B  // greek capital lambda
};

static const CodePoint TO_LOWER_EXPECTED[] =
{
    0x0061, // 'a'
    0x0062, // 'b'
    0x03BB  // greek small lambda
};

static const CodePoint NUMERIC_TRUE[] =
{
    0x0030, // '0'
    0x0031, // '1'
    0x00BD  // vulgar fraction one half
};

static const float NUMERIC_EXPECTED[] =
{
    0.0f, // '0'
    1.0f, // '1'
    0.5f  // 1/2 fraction
};

static const CodePoint NUMERIC_FALSE[] =
{
    0x0041, // 'A'
    0x0062, // 'b'
    0x03BB  // greek small lambda
};



// Static functions.
static float FloatAbs(float value)
{
    if (value < 0.0f)
    {
        return -value;
    }
    return value;
}

static bool CompareWithMarginOfError(float a, float b)
{
    const float MARGIN_OF_ERROR = 0.0001f;
    return FloatAbs(a - b) <= MARGIN_OF_ERROR;
}

static inline size_t GetArrayLength(size_t size)
{
    return size / sizeof(CodePoint);
}

static bool TestCodepointPredicate(UnicodeData* unicode, 
    BoolUnicodeUnitTest* test,
    TestErrorMessage* errorMsg)
{
    size_t CodepointCount = GetArrayLength(test->_codePointArraySize);
    for (size_t i = 0; i < CodepointCount; i++)
    {
        CodePoint TargetCodePoint = test->_codepointsToTest[i];
        bool Result = (*test->_predicate)(unicode, TargetCodePoint);
        if (Result != test->_expectedResult)
        {
            Test_FormatErrorMessage(errorMsg,
                u8"Codepoint %d failed test \"%s\", expected %d, got %d.",
                TargetCodePoint, test->_testName, test->_expectedResult, Result);
            return false;
        }
    }
    return true;
}


// Functions.   
bool Test_TestUnicodeBooleans(TestErrorMessage* errorMsg, void* userData)
{
    UnicodeData* Unicode = ((UnicodeTestContext*)userData)->_unicode;

    size_t TestCount = sizeof(BOOL_UNIT_TESTS) / sizeof(BOOL_UNIT_TESTS[0]);
    for (size_t i = 0; i < TestCount; i++)
    {
        if (!TestCodepointPredicate(Unicode, &BOOL_UNIT_TESTS[i], errorMsg))
        {
            return false;
        }
    }

    return true;
}


bool Test_TestUnicodeCodePointValidation(TestErrorMessage* errorMsg, void* userData)
{
    UnicodeData* Unicode = ((UnicodeTestContext*)userData)->_unicode;

    CodePoint InvalidCodePoint = CODEPOINT_NONE - 1;
    if (Unicode_IsDefined(Unicode, InvalidCodePoint))
    {
        Test_FormatErrorMessage(errorMsg, 
            u8"Invalid codepoint is said to be defined by the IsDefined function despite being invalid.");
            return false;
    }

    CodePointPredicate Predicates[] = 
    {
        &Unicode_IsLetter,
        &Unicode_IsDigit,
        &Unicode_IsNumber,
        &Unicode_IsSymbol,
        &Unicode_IsMark,
        &Unicode_IsSeparator,
        &Unicode_IsWhitespace,
        &Unicode_IsPunctuation,
        &Unicode_IsUpper,
        &Unicode_IsLower,
        &Unicode_IsCased,
        &Unicode_IsASCII,
        &Unicode_IsASCIILetter,
        &Unicode_IsASCIIDigit,
        &Unicode_IsControl,
        &Unicode_IsOtherCategory
    };

    for (size_t i = 0 ; i < (sizeof(Predicates) / sizeof(Predicates[0])); i++)
    {
        if ((*Predicates[i])(Unicode, InvalidCodePoint))
        {
            Test_FormatErrorMessage(errorMsg, 
                u8"Invalid codepoint passed as valid in one of the predicates.");
                return false;
        }
    }

    return true;
}

bool Test_TestUnicodeConversions(TestErrorMessage* errorMsg, void* userData)
{
    UnicodeData* Unicode = ((UnicodeTestContext*)userData)->_unicode;

    for (size_t i = 0; i < (sizeof(TO_UPPER_TRUE) / sizeof(CodePoint)); i++)
    {
        CodePoint TargetCodePoint = TO_UPPER_TRUE[i];
        CodePoint UpperCodePoint = Unicode_ToUpper(Unicode, TargetCodePoint);
        if (UpperCodePoint != TO_UPPER_EXPECTED[i])
        {
            Test_FormatErrorMessage(errorMsg,
                u8"Codepoint %d failed ToUpper conversion, expected %d, got %d.",
                TargetCodePoint, TO_UPPER_EXPECTED[i], UpperCodePoint);
            return false;
        }
    }

    for (size_t i = 0; i < (sizeof(TO_LOWER_TRUE )/ sizeof(CodePoint)); i++)
    {
        CodePoint TargetCodePoint = TO_LOWER_TRUE[i];
        CodePoint LowerCodePoint = Unicode_ToLower(Unicode, TargetCodePoint);
        if (LowerCodePoint != TO_LOWER_EXPECTED[i])
        {
            Test_FormatErrorMessage(errorMsg,
                u8"Codepoint %d failed ToLower conversion, expected %d, got %d.",
                TargetCodePoint, TO_LOWER_EXPECTED[i], LowerCodePoint);
            return false;
        }
    }

    return true;
}

bool Test_TestUnicodeNumericValues(TestErrorMessage* errorMsg, void* userData)
{
    UnicodeData* Unicode = ((UnicodeTestContext*)userData)->_unicode;

    for (size_t i = 0; i < (sizeof(NUMERIC_TRUE) / sizeof(CodePoint)); i++)
    {
        CodePoint TargetCodePoint = NUMERIC_TRUE[i];
        float NumericValue;
        bool HasNumericValue = Unicode_GetNumericValue(Unicode, TargetCodePoint, &NumericValue);
        if (!HasNumericValue || !CompareWithMarginOfError(NumericValue, NUMERIC_EXPECTED[i]))
        {
            Test_FormatErrorMessage(errorMsg,
                u8"Codepoint %d failed numeric value test, expected %f, got %f.",
                TargetCodePoint, NUMERIC_EXPECTED[i], NumericValue);
            return false;
        }
    }

    for (size_t i = 0; i < (sizeof(NUMERIC_FALSE) / sizeof(CodePoint)); i++)
    {
        CodePoint TargetCodePoint = NUMERIC_FALSE[i];
        float NumericValue;
        bool HasNumericValue = Unicode_GetNumericValue(Unicode, TargetCodePoint, &NumericValue);
        if (HasNumericValue)
        {
            Test_FormatErrorMessage(errorMsg,
                u8"Codepoint %d incorrectly returned numeric value of %f, expected no numeric value.",
                NumericValue, TargetCodePoint);
            return false;
        }
    }

    return true;
}