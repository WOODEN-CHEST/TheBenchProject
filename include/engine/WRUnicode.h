#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"

// https://www.unicode.org/reports/tr44/


// Types.

/** Represents a character codepoint. */
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

typedef enum CharacterFlagsEnum
{
    CharacterFlags_None = 0,
    CharacterFlags_IsDigit = 1
} CharacterFlags;


/* Holds information about a single codepoint. */
typedef struct UnicodeCharacterStruct
{
    CodePoint _codepoint;
    CodePoint _lowerMapping;
    CodePoint _upperMapping;
    CodePointCategory _category;
    CharacterFlags _flags;
    float _numericValue; /** The numeric value of this codepoint, set to NaN if the codepoint does not have a numeric value. */
} UnicodeCharacter;

/** 
 * Contains the unicode database.
 * A codepoint counts as defined in the unicode functions if the passed in database contains the codepoint AND
 * the codepoint is in the unicode codepoint range.
 * Note that the codepoint 0x0 is always defined regardless of its existence in the unicode database.
 */
typedef struct UnicodeDataStruct
{
    UnicodeCharacter* _characters; /** The unicode character table, ready to be indexed with codepoints. */
    size_t _characterCount; /** The number of characters in the table. */
} UnicodeData;


// Fields.

/** A codepoint which represents an invalid or not set codepoint. */
extern const CodePoint CODEPOINT_NONE;


// Functions.

/**
 * Attempts to convert the given codepoint to its lowercase variant.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to convert.
 * @return The lowercase codepoint if one exists, otherwise the same passed in codepoint.
 * If the passed in codepoint isn't defined, this returns CODEPOINT_NONE.
 */
CodePoint Unicode_ToLower(UnicodeData* data, CodePoint codepoint);

/**
 * Attempts to convert the given codepoint to its uppercase variant.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to convert.
 * @return The uppercase codepoint if one exists, otherwise the same passed in codepoint.
 * If the passed in codepoint isn't defined, this returns CODEPOINT_NONE.
 */
CodePoint Unicode_ToUpper(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is a letter.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is a letter, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsLetter(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is a digit.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is a digit, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsDigit(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is a number.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is a number, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsNumber(UnicodeData* data, CodePoint codepoint);

/**
 * Gets the numeric value of the given codepoint.
 * @param [in] data The unicode database to use.
 * @param codepoint The codepoint to get the numeric value of.
 * @param [out] value The numeric value of the codepoint, or NaN if it has no value.
 * @return true if the codepoint has a numeric value, false otherwise.
 */
bool Unicode_GetNumericValue(UnicodeData* data, CodePoint codepoint, float* value);

/**
 * Determines whether the given codepoint is a symbol.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is a symbol, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsSymbol(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is a mark codepoint.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is a mark codepoint, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsMark(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is a separator.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is a separator, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsSeparator(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is whitespace.
 *
 * Note that a character's whitespace status is not determined by the passed in unicode database,
 * however, the given codepoint still has to be defined in the database to pass as a whitespace character.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is whitespace, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsWhitespace(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is a punctuation.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is a punctuation, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsPunctuation(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is an upper case character.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is an upper case character, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsUpper(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is a lower case character.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is a lower case character, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsLower(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is a cased character (upper or lower case).
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is a cased character, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsCased(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is an ASCII character.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is an ASCII character, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsASCII(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is an ASCII letter.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is an ASCII letter, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsASCIILetter(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is an ASCII digit.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is an ASCII digit, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsASCIIDigit(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is a control character.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is a control character, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsControl(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint's category is the "other" category.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to test.
 * @return true if the codepoint is an "other" codepoint, false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsOtherCategory(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoints are equal while ignoring their casing.
 * 
 * If the given codepoints are not cased, then this simply compares if the codepoints are equal.
 * @param data The unicode database to use.
 * @param codepoint1 The first codepoint.
 * @param codepoint2 The second codepoint.
 * @return true if codepoints are the same while ignoring the case, false if they aren't or the codepoint isn't defined.
 */
bool Unicode_EqualsCaseIgnore(UnicodeData* data, CodePoint codepoint1, CodePoint codepoint2);

/**
 * Gets the category of the given codepoint.
 * @param data The unicode database to use.
 * @param codepoint The codepoint to get the category of.
 * @return The codepoint's category if it is defined, or the "none" category if is isn't/
 */
CodePointCategory Unicode_GetCategory(UnicodeData* data, CodePoint codepoint);

/**
 * Determines whether the given codepoint is defined.
 * 
 * For a codepoint to be defined, it must >= 0, and it must be defined in the given unicode database.
 * @return true if the codepoint is defined, false otherwise.
 */
bool Unicode_IsDefined(UnicodeData* data, CodePoint codepoint);