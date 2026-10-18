#pragma once
#include <stdbool.h>
#include "TestFramework.h"
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "wr/WRJSON.h"
#include "wr/WRError.h"


/**
 * Helpers shared by the text suites (components, codec, document, layout, label): parsing JSON text, comparing
 * whole component trees, and checking error messages.
 */


// Functions.
/**
 * @brief Parses @p text (UTF-8 JSON) into a value tree owned by @p pool.
 * @returns true on success (a failed check is recorded otherwise).
 */
bool TestText_ParseJSON(TestRun* run, JSONObjectPool* pool, const char* text, JSONObjectValue* outValue);

/**
 * @brief Reports whether two trees have the same types, properties (every field) and children, recursively.
 */
bool TestText_AreTreesEqual(const TextComponent* a, const TextComponent* b);

/**
 * @brief Checks that @p result failed with @p expectedCode and that its message contains every fragment in
 *        @p fragments (NULL-terminated array of string literals); always deconstructs @p result.
 * @returns true if every check passed.
 */
bool TestText_CheckErrorMessage(TestRun* run, Error result, ErrorCode expectedCode, const char* const* fragments);

/**
 * @brief Creates a string component with @p text and adds it as the last child of @p parent.
 * @returns The new child (owned by @p parent), or NULL with a failed check.
 */
StringComponent* TestText_AddString(TestRun* run, TextComponentFactory* factory, TextComponent* parent, const char* text);
