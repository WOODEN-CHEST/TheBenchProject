#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WRUnicode.h"


// Macros.
#define CODEPOINT_BYTE_COUNT_MAX 4


// Functions.
bool CharUTF8_IsCharValid(const unsigned char* character);

bool CharUTF8_IsCharBufferValid(const unsigned char* character, size_t bufferLength);

bool CharUTF8_IsCodePointValid(CodePoint codepoint);

size_t CharUTF8_GetByteCountChar(const unsigned char* character);

size_t CharUTF8_GetByteCountCodepoint(CodePoint codepoint);

size_t CharUTF8_WriteCodePoint(unsigned char* character, CodePoint codepoint);

CodePoint CharUTF8_GetCodePoint(const unsigned char* character);