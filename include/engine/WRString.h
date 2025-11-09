#pragma once
#include "WRUnicode.h"
#include "WRChar.h"
#include <stddef.h>
#include "WRComparasionResult.h"


// Types.
typedef enum StringCaseRuleEnum
{
    StringCaseRule_MatchCase,
    StringCaseRule_CaseIgnore,
} StringCaseRule;

typedef struct StringBufferStruct StringBuffer;

typedef bool (*StringAllocateCallback)(StringBuffer* destination);

struct StringBufferStruct
{
    void* _buffer;
    size_t _bufferSize;
    void* _userData;
    StringAllocateCallback _requestMoreSpaceCallback;
};

typedef enum StringSplitTypeEnum
{
    StringSplitType_None = 0,
    StringSplitType_SkipEmpty = (1 << 0),
    StringSplitType_None = (1 << 1)
} StringSplitType;

typedef struct StringSplitOptionsStruct
{
    StringSplitType _splitType;
    size_t _stringCountLimit;
} StringSplitOptions;

typedef enum StringMoveDirectionEnum
{
    StringMoveDirection_Forwards,
    StringMoveDirection_Backwards,
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

bool StringUTF8_IsNullOrWhitespace(const unsigned char* str, UnicodeData* unicode);

StringBuffer String_CreateBufferVariable(unsigned char* destination,
    size_t size,
    void* userData,
    StringAllocateCallback callback);

StringBuffer String_CreateBufferConstant(unsigned char* destination, size_t size);

void StringUTF8_ToLower(const unsigned char* str, UnicodeData* unicode, StringBuffer destination);

void StringUTF8_ToUpper(const unsigned char* str, UnicodeData* unicode, StringBuffer destination);

void StringUTF8_InvertCase(const unsigned char* str, UnicodeData* unicode, StringBuffer destination);

size_t StringUTF8_GetByteLength(const unsigned char* str);

size_t StringUTF8_GetCodePointLength(const unsigned char* str);

bool StringUTF8_Equals(const unsigned char* a, const unsigned char* b, StringCaseRule caseRule);

void StringUTF8_CopyTo(const unsigned char* source, StringBuffer destination);

StringSplitOptions String_CreateSplitOptionsNormal();

StringSplitOptions String_CreateSplitOptionsTyped(StringSplitType type);

StringSplitOptions String_CreateSplitOptionsAll(StringSplitType type, size_t maxSplits);

void StringUTF8_Split(unsigned char* str,
    unsigned char* delimeter,
    StringSplitOptions splitOptions,
    StringBuffer* resultPointers);

StringIndexOfOptions String_CreateIndexOptionsNormal();

StringIndexOfOptions String_CreateIndexOptionsFromEnd();

StringIndexOfOptions String_CreateIndexOptions(StringCaseRule caseRule,
    StringMoveDirection direction,
    size_t startIndex,
    bool isStartIndexFromEnd);

bool StringUTF8_IndexOf(const unsigned char* str, const unsigned char* target, StringIndexOfOptions options);

void StringUTF8_Concat(const unsigned char* strA, const unsigned char* strB, StringBuffer destination);

bool StringUTF8_Contains(const unsigned char* str, const unsigned char* target, StringCaseRule caseRule);

size_t StringUTF8_Count(const unsigned char* str, const unsigned char* target, StringCaseRule caseRule);

bool StringUTF8_EndsWith(const unsigned char* str, const unsigned char* target, StringCaseRule caseRule);

bool StringUTF8_StartsWith(const unsigned char* str, const unsigned char* target, StringCaseRule caseRule);

void StringUTF8_Format(const unsigned char* str, StringBuffer destination);

void StringUTF8_Join(const unsigned char* separator,
    const unsigned char** sources,
    size_t sourcesSize,
    StringBuffer destination);

void StringUTF8_Replace(const unsigned char* str,
    const unsigned char* searchTarget,
    const unsigned char* replaceValue,
    StringBuffer destination);

void StringUTF8_Substring(const unsigned char* str,
    size_t startIndex,
    size_t endIndex,
    StringBuffer destination);

void StringUTF8_Trim(const unsigned char* str,
    bool isStartTrimmed,
    bool isEndTrimmed,
    StringBuffer destination);

ComparisonResult StringUTF8_Compare(const unsigned char* strA, const unsigned char* strB);

void StringUTF8_Remove(const unsigned char* str, const unsigned char* target, StringCaseRule caseRule, StringBuffer destination);

void StringUTF8_Insert(const unsigned char* str, size_t index, const unsigned char* substring, StringBuffer destination);