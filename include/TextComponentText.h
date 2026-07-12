#pragma once
#include "TextComponent.h"
#include "wr/WRMemory.h"
#include "wr/WRError.h"


/**
 * @file TextComponentText.h
 * @brief Plain-text (UTF-8) serialization of a text component tree.
 *
 * Flattens a component and its whole subtree into a single UTF-8 string containing only the string
 * content — no styling. Depth-first, matching the render order: a component's own string is appended first,
 * then each child in order. Sprite and empty components contribute nothing. There is no plain-text
 * deserializer (a plain string carries no component structure).
 */


/**
 * @brief Appends the plain-text representation of a component tree to a byte buffer.
 *
 * Appends the text of every string component in the tree (depth-first) to @p destination; sprite and empty
 * components contribute nothing. Following the project convention, this does NOT clear @p destination first
 * and does NOT append a NUL terminator — pass a buffer whose contents do not end mid-string (typically a
 * freshly cleared one) and call GenericBuffer_NullTerminate afterwards if a C string is wanted.
 * @param component The component tree to serialize; must not be NULL.
 * @param destination [out] Byte buffer (element size 1) the text is appended to; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p component or @p destination is NULL or @p destination is
 *          not a byte buffer; ErrorCode_BufferTooLarge if the buffer could not grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentText_Serialize(const TextComponent* component, GenericBuffer* destination);
