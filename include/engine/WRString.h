#pragma once
#include "WRUnicode.h"
#include "WRChar.h"
#include <stddef.h>
#include "WRComparasionResult.h"
#include "WRMemory.h"



// Fields.
extern const size_t INDEX_INVALID = 0xFFFFFFFFFFFFFFFF;


// Types.
typedef enum StringCaseRuleEnum
{
    StringCaseRule_MatchCase,
    StringCaseRule_CaseIgnore,
} StringCaseRule;

typedef enum StringSplitTypeEnum
{
    StringSplitType_None = 0,
    StringSplitType_SkipEmpty = (1 << 0),
    StringSplitType_Trim = (1 << 1)
} StringSplitType;

typedef struct StringSplitOptionsStruct
{
    StringSplitType _splitType;
    size_t _stringCountLimit;
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
    size_t _startIndex; 
    bool _isStartIndexFromEnd;
}
StringIndexOfOptions;


// Fields.
extern const unsigned char* const STRING_EMPTY;


// Functions.
bool StringUTF8_IsEncodingValid(const unsigned char* str);

bool StringUTF8_IsCodepointsValid(const unsigned char* str, UnicodeData* unicode);

bool StringUTF8_IsValid(const unsigned char* str, UnicodeData* unicode);

bool StringUTF8_IsNullOrEmpty(const unsigned char* str);

Error StringUTF8_IsNullOrWhitespace(const unsigned char* str, UnicodeData* unicode, ErrorMessagePool* errorPool, bool* outValue);

Error StringUTF8_ToLower(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination, ErrorMessagePool* errorPool);

Error StringUTF8_ToUpper(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination, ErrorMessagePool* errorPool);

Error StringUTF8_InvertCase(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination, ErrorMessagePool* errorPool);

size_t StringUTF8_GetByteLength(const unsigned char* str);

size_t StringUTF8_GetCodePointLength(const unsigned char* str);

Error StringUTF8_Equals(const unsigned char* a,
    const unsigned char* b,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool,
    bool* outValue);

Error StringUTF8_CopyTo(const unsigned char* source, GenericBuffer* destination, ErrorMessagePool* errorPool);

Error StringUTF8_CopyToBySize(const unsigned char* source, size_t size, GenericBuffer* destination, ErrorMessagePool* errorPool);

StringSplitOptions String_CreateSplitOptionsNormal();

StringSplitOptions String_CreateSplitOptionsTyped(StringSplitType type);

StringSplitOptions String_CreateSplitOptionsAll(StringSplitType type, size_t maxSplits);

Error StringUTF8_Split(unsigned char* str,
    const unsigned char* delimeter,
    StringSplitOptions splitOptions,
    GenericBuffer* resultPointers,
    ErrorMessagePool* errorPool,
    UnicodeData* unicode);

StringIndexOfOptions String_CreateIndexOptionsNormal();

StringIndexOfOptions String_CreateIndexOptionsFromEnd();

StringIndexOfOptions String_CreateIndexOptions(StringCaseRule caseRule,
    StringMoveDirection direction,
    size_t startIndex,
    bool isStartIndexFromEnd);

Error StringUTF8_IndexOf(const unsigned char* str,
    const unsigned char* target,
    StringIndexOfOptions options,
    ErrorMessagePool* errorPool,
    UnicodeData* unicode,
    size_t* outIndex);

Error StringUTF8_Concat(const unsigned char* strA,
    const unsigned char* strB,
    GenericBuffer* destination,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool);

Error StringUTF8_Contains(const unsigned char* str,
    const unsigned char* target,
    size_t startIndex,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool,
    bool* outValue);

Error StringUTF8_Count(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool,
    size_t* count);

Error StringUTF8_EndsWith(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool,
    bool* outValue);

Error StringUTF8_StartsWith(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool,
    bool* outValue);

Error StringUTF8_Format(const unsigned char* str,
    GenericBuffer* destination,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool,
    ...);

Error StringUTF8_Join(const unsigned char* separator,
    const unsigned char** sources,
    size_t sourcesSize,
    GenericBuffer* destination,
    ErrorMessagePool* errorPool);

Error StringUTF8_Replace(const unsigned char* str,
    const unsigned char* searchTarget,
    const unsigned char* replaceValue,
    GenericBuffer* destination,
    ErrorMessagePool* errorPool);

Error StringUTF8_Substring(const unsigned char* str,
    size_t startIndex,
    size_t endIndex,
    GenericBuffer* destination,
    ErrorMessagePool* errorPool);

Error StringUTF8_Trim(const unsigned char* str,
    bool isStartTrimmed,
    bool isEndTrimmed,
    GenericBuffer* destination,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool);

Error StringUTF8_GetTrimIndices(unsigned char* str,
    bool isStartTrimmed,
    bool isEndTrimmed,
    size_t* startIndex,
    size_t* endIndex,
    UnicodeData* unicode);

Error StringUTF8_Compare(const unsigned char* strA,
    const unsigned char* strB,
    ErrorMessagePool* errorPool,
    ComparisonResult* result);

Error StringUTF8_Remove(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    GenericBuffer* destination);

Error StringUTF8_Insert(const unsigned char* str,
    size_t index,
    const unsigned char* substring,
    GenericBuffer* destination,
    ErrorMessagePool* errorPool);

bool StringUTF8_WriteCodePointToBuffer(GenericBuffer* buffer, CodePoint codePoint);

Error StringUTF8_Reverse(const unsigned char* str, GenericBuffer* destination, UnicodeData* unicode, ErrorMessagePool* errorPool);

Error StringUTF8_Repeat(const unsigned char* str, GenericBuffer* destination, size_t count, ErrorMessagePool* errorPool);

Error StringUTF8_GetCharacterIndexArray(const unsigned char* str, GenericBuffer* indexArray, ErrorMessagePool* errorPool);