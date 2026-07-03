#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"

// https://www.unicode.org/reports/tr44/


// Types.

/**
 * @brief A single Unicode scalar value (codepoint).
 *
 * A signed 32-bit integer holding a codepoint. The well-formed Unicode range is [0, 0x10FFFF]
 * (excluding the surrogate range 0xD800-0xDFFF for encoding purposes). The sentinel value
 * CODEPOINT_NONE (-1) denotes "no codepoint" / invalid. Being signed, it can also temporarily
 * hold out-of-range values which the API treats as undefined/invalid.
 */
typedef int32_t CodePoint;

/**
 * @brief The Unicode general category of a codepoint.
 *
 * Mirrors the standard Unicode general category values (TR44) as parsed from the database.
 * CodePointCategory_None is used for codepoints that are not defined in the active database.
 */
typedef enum CodePointCategoryEnum
{
    /** @brief No category; reported for codepoints that are not defined in the database. */
    CodePointCategory_None,

    /** @brief Uppercase letter (Lu). */

    CodePointCategory_UppercaseLetter,
    /** @brief Lowercase letter (Ll). */
    CodePointCategory_LowercaseLetter,
    /** @brief Titlecase letter (Lt). */
    CodePointCategory_TitlecaseLetter,
    /** @brief Modifier letter (Lm). */
    CodePointCategory_ModifiedLetter,
    /** @brief Other letter (Lo). */
    CodePointCategory_OtherLetter,

    /** @brief Nonspacing mark (Mn). */
    CodePointCategory_NonspacingMark,
    /** @brief Spacing combining mark (Mc). */
    CodePointCategory_SpacingMark,
    /** @brief Enclosing mark (Me). */
    CodePointCategory_EnclosingMark,

    /** @brief Decimal digit number (Nd). */
    CodePointCategory_DecimalNumber,
    /** @brief Letter number (Nl). */
    CodePointCategory_LetterNumber,
    /** @brief Other number (No). */
    CodePointCategory_OtherNumber,

    /** @brief Connector punctuation (Pc). */
    CodePointCategory_ConnectorPunctuation,
    /** @brief Dash punctuation (Pd). */
    CodePointCategory_DashPunctuation,
    /** @brief Open/opening punctuation (Ps). */
    CodePointCategory_OpenPunctuation,
    /** @brief Close/closing punctuation (Pe). */
    CodePointCategory_ClosePunctuation,
    /** @brief Initial quote punctuation (Pi). */
    CodePointCategory_InitialPunctuation,
    /** @brief Final quote punctuation (Pf). */
    CodePointCategory_FinalPunctuation,
    /** @brief Other punctuation (Po). */
    CodePointCategory_OtherPunctuation,

    /** @brief Math symbol (Sm). */
    CodePointCategory_Math_Symbol,
    /** @brief Currency symbol (Sc). */
    CodePointCategory_CurrencySymbol,
    /** @brief Modifier symbol (Sk). */
    CodePointCategory_ModifierSymbol,
    /** @brief Other symbol (So). */
    CodePointCategory_OtherSymbol,

    /** @brief Space separator (Zs). */
    CodePointCategory_SpaceSeparator,
    /** @brief Line separator (Zl). */
    CodePointCategory_LineSeparator,
    /** @brief Paragraph separator (Zp). */
    CodePointCategory_ParagraphSeparator,

    /** @brief Control character (Cc). */
    CodePointCategory_Control,
    /** @brief Format character (Cf). */
    CodePointCategory_Format,
    /** @brief Surrogate codepoint (Cs). */
    CodePointCategory_Surrogate,
    /** @brief Private-use codepoint (Co). */
    CodePointCategory_Private_Use,
    /** @brief Unassigned / reserved codepoint (Cn). */
    CodePointCategory_Unassigned,
} CodePointCategory;

/**
 * @brief Bit flags describing extra boolean properties of a codepoint.
 *
 * Stored as a bitmask in UnicodeCharacter::_flags; combine with bitwise OR.
 */
typedef enum CharacterFlagsEnum
{
    /** @brief No flags set. */
    CharacterFlags_None = 0,
    /** @brief The codepoint has a decimal digit value (digit field [0,9] present in the database). */
    CharacterFlags_IsDigit = 1
} CharacterFlags;


/**
 * @brief A single row of the Unicode character table holding all decoded properties of one codepoint.
 *
 * Populated by the loader from a UnicodeData.txt record. A slot whose _codepoint equals CODEPOINT_NONE
 * is an unpopulated / undefined entry. Instances live inside UnicodeData::_characters and are indexed
 * directly by codepoint value; callers normally interact with them only through the Unicode_* functions.
 */
typedef struct UnicodeCharacterStruct
{
    /** @brief The codepoint this entry describes, or CODEPOINT_NONE if the slot is undefined/unpopulated. */
    CodePoint _codepoint;
    /** @brief Lowercase mapping codepoint, or CODEPOINT_NONE when no simple lowercase mapping exists. */
    CodePoint _lowerMapping;
    /** @brief Uppercase mapping codepoint, or CODEPOINT_NONE when no simple uppercase mapping exists. */
    CodePoint _upperMapping;
    /** @brief The Unicode general category of this codepoint. */
    CodePointCategory _category;
    /** @brief Bit flags describing extra boolean properties (see CharacterFlags). */
    CharacterFlags _flags;
    /** @brief The numeric value of this codepoint, set to NaN if the codepoint does not have a numeric value. */
    float _numericValue;
} UnicodeCharacter;

/**
 * @brief The in-memory Unicode database: a codepoint-indexed table of character properties.
 *
 * Holds the table that the Unicode_* query functions consult. A codepoint counts as "defined" in those
 * functions when the database is non-NULL AND the codepoint is in range (0 <= codepoint < _characterCount)
 * AND the corresponding table slot is populated (its _codepoint is not CODEPOINT_NONE). As a special case
 * the codepoint 0x0 is always treated as defined regardless of its presence in the source data.
 *
 * The table is owned by this struct; it is produced by the WRUnicodeLoader entry points and must be
 * released with UnicodeData_Deconstruct.
 */
typedef struct UnicodeDataStruct
{
    /** @brief The Unicode character table, heap-allocated and indexable directly by codepoint value. NULL when empty/released. */
    UnicodeCharacter* _characters;
    /** @brief The number of entries (slots) in the table; also the exclusive upper bound for valid codepoint indices. */
    size_t _characterCount;
} UnicodeData;


// Fields.

/**
 * @brief Sentinel codepoint value (-1) representing an invalid or not-set codepoint.
 *
 * Returned by conversion functions to signal "undefined input" and used as the marker for
 * unpopulated table slots and absent case mappings.
 */
extern const CodePoint CODEPOINT_NONE;


// Functions.

/**
 * @brief Converts the given codepoint to its simple lowercase variant.
 *
 * The lowercase mapping is only applied when the mapping target is itself a defined codepoint in
 * @p data; otherwise the input codepoint is returned unchanged.
 * @param data The unicode database to use. May be NULL, in which case the input is treated as undefined.
 * @param codepoint The codepoint to convert.
 * @returns The lowercase codepoint if a defined mapping exists, otherwise the same passed-in codepoint.
 *          If the passed-in codepoint isn't defined (or @p data is NULL), returns CODEPOINT_NONE.
 */
CodePoint Unicode_ToLower(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Converts the given codepoint to its simple uppercase variant.
 *
 * The uppercase mapping is only applied when the mapping target is itself a defined codepoint in
 * @p data; otherwise the input codepoint is returned unchanged.
 * @param data The unicode database to use. May be NULL, in which case the input is treated as undefined.
 * @param codepoint The codepoint to convert.
 * @returns The uppercase codepoint if a defined mapping exists, otherwise the same passed-in codepoint.
 *          If the passed-in codepoint isn't defined (or @p data is NULL), returns CODEPOINT_NONE.
 */
CodePoint Unicode_ToUpper(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is a letter.
 *
 * True when the codepoint's general category is one of the letter categories
 * (uppercase, lowercase, titlecase, modifier, or other letter).
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is a letter; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsLetter(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is a decimal digit.
 *
 * This reflects the codepoint's digit flag (a digit value in [0,9] was present in the source data),
 * which is distinct from the decimal-number general category.
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is a digit; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsDigit(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is a number.
 *
 * True when the codepoint's general category is one of the number categories
 * (decimal number, letter number, or other number).
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is a number; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsNumber(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Gets the numeric value associated with the given codepoint.
 *
 * On every call where @p value is non-NULL it is first set to NaN, then overwritten with the codepoint's
 * numeric value when one is available.
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to get the numeric value of.
 * @param value [out] Receives the numeric value of the codepoint, or NaN if it has none. Must not be NULL;
 *        if NULL the function does nothing and returns false.
 * @returns true if the codepoint is defined and has a (non-NaN) numeric value; false otherwise
 *          (including when @p value is NULL or the codepoint isn't defined).
 */
bool Unicode_GetNumericValue(UnicodeData* data, CodePoint codepoint, float* value);

/**
 * @brief Determines whether the given codepoint is a symbol.
 *
 * True when the codepoint's general category is one of the symbol categories
 * (math, currency, modifier, or other symbol).
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is a symbol; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsSymbol(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is a combining mark.
 *
 * True when the codepoint's general category is one of the mark categories
 * (nonspacing, spacing, or enclosing mark).
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is a mark; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsMark(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is a separator.
 *
 * True when the codepoint's general category is one of the separator categories
 * (space, line, or paragraph separator).
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is a separator; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsSeparator(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is whitespace.
 *
 * Whitespace membership is decided by a fixed built-in set of whitespace codepoints and does NOT depend on
 * the contents of the passed-in database; however, the codepoint must still be defined in @p data to qualify.
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is whitespace and defined; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsWhitespace(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is punctuation.
 *
 * True when the codepoint's general category is one of the punctuation categories
 * (connector, dash, open, close, initial, final, or other punctuation).
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is punctuation; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsPunctuation(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is an uppercase letter.
 *
 * True only when the codepoint's general category is exactly uppercase letter (Lu).
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is an uppercase character; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsUpper(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is a lowercase letter.
 *
 * True only when the codepoint's general category is exactly lowercase letter (Ll).
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is a lowercase character; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsLower(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is a cased character (uppercase or lowercase letter).
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is cased; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsCased(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is an ASCII character (U+0000..U+007F).
 *
 * Combines a range check (codepoint <= 0x7F) with the requirement that the codepoint be defined in @p data.
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is in the ASCII range and defined; false otherwise.
 */
bool Unicode_IsASCII(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is an ASCII letter ('A'..'Z' or 'a'..'z').
 *
 * Combines the ASCII-letter range check with the requirement that the codepoint be defined in @p data.
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is an ASCII letter and defined; false otherwise.
 */
bool Unicode_IsASCIILetter(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is an ASCII digit ('0'..'9').
 *
 * Combines the ASCII-digit range check with the requirement that the codepoint be defined in @p data.
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is an ASCII digit and defined; false otherwise.
 */
bool Unicode_IsASCIIDigit(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is a control character.
 *
 * True only when the codepoint's general category is exactly control (Cc).
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is a control character; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsControl(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint belongs to an "other" (C*) category.
 *
 * True when the codepoint's general category is one of control, format, surrogate, private-use, or unassigned.
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is in an "other" category; false if it isn't or the codepoint isn't defined.
 */
bool Unicode_IsOtherCategory(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Compares two codepoints for equality while ignoring case.
 *
 * Both codepoints must be defined. Equal codepoints compare equal immediately; otherwise they are considered
 * equal when they share a common lowercase mapping or a common uppercase mapping. For uncased codepoints this
 * effectively reduces to a plain equality test.
 * @param data The unicode database to use. May be NULL (treated as: codepoints undefined).
 * @param codepoint1 The first codepoint.
 * @param codepoint2 The second codepoint.
 * @returns true if the codepoints are equal ignoring case; false if they aren't, or if either codepoint isn't defined.
 */
bool Unicode_EqualsCaseIgnore(UnicodeData* data, CodePoint codepoint1, CodePoint codepoint2);

/**
 * @brief Gets the Unicode general category of the given codepoint.
 * @param data The unicode database to use. May be NULL (treated as: codepoint undefined).
 * @param codepoint The codepoint to get the category of.
 * @returns The codepoint's category if it is defined; otherwise CodePointCategory_None.
 */
CodePointCategory Unicode_GetCategory(UnicodeData* data, CodePoint codepoint);

/**
 * @brief Determines whether the given codepoint is defined in the database.
 *
 * A codepoint is defined when @p data is non-NULL and either the codepoint is 0x0 (always defined) or it is
 * within range (0 <= codepoint < UnicodeData::_characterCount) and its table slot is populated.
 * @param data The unicode database to use. May be NULL, in which case the result is false.
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is defined; false otherwise.
 */
bool Unicode_IsDefined(UnicodeData* data, CodePoint codepoint);