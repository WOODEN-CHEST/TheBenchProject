#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "WRUnicode.h"


// Macros.
#define CODEPOINT_BYTE_COUNT_MAX 4
#define CODEPOINT_WORD16_COUNT_MAX 2


// Functions.
bool CharUTF8_IsCharValid(const unsigned char* character);

bool CharUTF8_IsCharBufferValid(const unsigned char* character, size_t bufferLength);

bool CharUTF8_IsCodePointValid(CodePoint codepoint);

size_t CharUTF8_GetByteCountChar(const unsigned char* character);

size_t CharUTF8_GetByteCountCharFromEnd(const unsigned char* characterLastByte, size_t remainingPreBytes);

size_t CharUTF8_GetByteCountCodepoint(CodePoint codepoint);

CodePoint CharUTF8_GetCodePointFromEnd(const unsigned char* characterLastByte, size_t remainingPreBytes);

size_t CharUTF8_WriteCodePoint(unsigned char* character, CodePoint codepoint);

CodePoint CharUTF8_GetCodePoint(const unsigned char* character);



bool CharUTF16_IsCharValid(const uint16_t* character);

bool CharUTF16_IsCharBufferValid(const uint16_t* character, size_t bufferLength);

bool CharUTF16_IsCodePointValid(CodePoint codepoint);

size_t CharUTF16_GetWordCountChar(const uint16_t* character);

size_t CharUTF16_GetWordCountCharFromEnd(const uint16_t* characterLastWord, size_t remainingPreWords);

size_t CharUTF16_GetWordCountCodepoint(CodePoint codepoint);

CodePoint CharUTF16_GetCodePointFromEnd(const uint16_t* characterLastWord, size_t remainingPreWords);

size_t CharUTF16_WriteCodePoint(uint16_t* character, CodePoint codepoint);

CodePoint CharUTF16_GetCodePoint(const uint16_t* character);
