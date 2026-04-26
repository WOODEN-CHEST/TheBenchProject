#pragma once
#include "WRUnicode.h"
#include "WRChar.h"
#include <stddef.h>
#include "WRComparator.h"
#include "WRMemory.h"


#define STRING_INDEX_INVALID (~((size_t)0))


// Types.
typedef enum StringCaseRuleEnum
{
    StringCaseRule_MatchCase,
    StringCaseRule_CaseIgnore,
} StringCaseRule;

typedef enum StringSplitTypeEnum
{
    StringSplitType_None = 0,
    StringSplitType_SkipEmptyEntries = (1 << 0),
    StringSplitType_TrimEntries = (1 << 1)
} StringSplitType;

typedef struct StringSplitOptionsStruct
{
    StringSplitType _splitType;
    size_t _stringCountLimit;
    StringCaseRule _caseRule;
} StringSplitOptions;

typedef enum StringMoveDirectionEnum
{
    StringMoveDirection_Backwards = -1,
    StringMoveDirection_Forwards = 1,
} StringMoveDirection;

typedef struct StringIndexOfOptionsStruct
{
    StringCaseRule _caseRule;
    StringMoveDirection _direction;
    int64_t _startIndex; 
}
StringIndexOfOptions;


// Fields.
extern const unsigned char* const STRING_EMPTY;


// Functions.
bool StringUTF8_IsEncodingValid(const unsigned char* str);

bool StringUTF8_AreCodepointsValid(const unsigned char* str, UnicodeData* unicode);

bool StringUTF8_IsNullOrEmpty(const unsigned char* str);

Error StringUTF8_IsNullOrWhitespace(const unsigned char* str, UnicodeData* unicode, bool* outValue);

Error StringUTF8_ToLower(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination);

Error StringUTF8_ToUpper(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination);

Error StringUTF8_InvertCase(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination);

size_t StringUTF8_GetByteLength(const unsigned char* str);

Error StringUTF8_GetCodePointLength(const unsigned char* str, size_t* outLength);

Error StringUTF8_Equals(const unsigned char* a,
    const unsigned char* b,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue);

Error StringUTF8_CopyTo(const unsigned char* source, GenericBuffer* destination);

Error StringUTF8_CopyToBySize(const unsigned char* source, size_t size, GenericBuffer* destination);

StringSplitOptions String_CreateSplitOptionsNormal();

StringSplitOptions String_CreateSplitOptionsTyped(StringSplitType type);

StringSplitOptions String_CreateSplitOptions(StringSplitType type, size_t maxSplits, StringCaseRule caseRule);

Error StringUTF8_Split(const unsigned char* str,
    const unsigned char** delimeters,
    size_t delimeterCount,
    StringSplitOptions splitOptions,
    GenericBuffer* stringBuffer,
    GenericBuffer* resultPointers,
    UnicodeData* unicode);

StringIndexOfOptions String_CreateIndexOptionsNormal(void);

StringIndexOfOptions String_CreateIndexOptionsFromEnd(void);

StringIndexOfOptions String_CreateIndexOptions(StringCaseRule caseRule,
    StringMoveDirection direction,
    int64_t startIndex);

Error StringUTF8_IndexOf(const unsigned char* str,
    const unsigned char* target,
    StringIndexOfOptions options,
    UnicodeData* unicode,
    size_t* outIndex);

Error StringUTF8_Concat(const unsigned char* strA,
    const unsigned char* strB,
    GenericBuffer* destination);

Error StringUTF8_Contains(const unsigned char* str,
    const unsigned char* target,
    size_t startIndex,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue);

Error StringUTF8_Count(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    size_t* count);

Error StringUTF8_EndsWith(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue);

Error StringUTF8_StartsWith(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue);

Error StringUTF8_Format(const unsigned char* str,
    GenericBuffer* destination,
    ...);

Error StringUTF8_Join(const unsigned char* separator,
    const unsigned char** sources,
    size_t sourcesSize,
    GenericBuffer* destination);

Error StringUTF8_Replace(const unsigned char* str,
    const unsigned char* searchTarget,
    const unsigned char* replaceValue,
    GenericBuffer* destination);

Error StringUTF8_Substring(const unsigned char* str,
    size_t startIndex,
    size_t endIndex,
    GenericBuffer* destination);

Error StringUTF8_Trim(const unsigned char* str,
    bool isStartTrimmed,
    bool isEndTrimmed,
    GenericBuffer* destination,
    UnicodeData* unicode);

Error StringUTF8_GetTrimIndices(const unsigned char* str,
    bool isStartTrimmed,
    bool isEndTrimmed,
    size_t* startIndex,
    size_t* outLength,
    UnicodeData* unicode);

Error StringUTF8_Compare(const unsigned char* strA,
    const unsigned char* strB,
    ComparisonResult* result);

Error StringUTF8_Remove(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    GenericBuffer* destination);

Error StringUTF8_Insert(const unsigned char* str,
    size_t index,
    const unsigned char* substring,
    GenericBuffer* destination);

Error StringUTF8_Reverse(const unsigned char* str, GenericBuffer* destination);

Error StringUTF8_Repeat(const unsigned char* str, GenericBuffer* destination, size_t count);

Error StringUTF8_GetCharacterIndexArray(const unsigned char* str, GenericBuffer* indexArray);
