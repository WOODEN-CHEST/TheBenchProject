#pragma once
#include <stdint.h>
#include <stddef.h>

typedef int32_t CodePoint;

typedef enum UnicodeCharacterFlagsEnum
{
    UnicodeCharacterFlag_None = 0
} UnicodeCharacterFlagsEnum;

typedef struct UnicodeCharacterStruct
{
    CodePoint _lowerCodePoint;
    CodePoint _upperCodePoint;
    UnicodeCharacterFlagsEnum _flags;
} UnicodeCharacter;

typedef struct UnicodeDataStruct
{
    UnicodeCharacter* _characters;
    size_t _characterCount;
} UnicodeData;