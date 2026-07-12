#pragma once
#include <stdbool.h>
#include <stdint.h>
// Color/Vector2 come from Raylib; RenderColor from the renderer; the JSON value types from WRJSON.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "wr/WRJSON.h"
#include "wr/WRError.h"


/**
 * @file GameJSON.h
 * @brief Shared parsers for the game's common JSON value shapes (numbers, booleans, strings, colors,
 *        render colors, vectors).
 *
 * These implement the value conventions documented in references/asset_structure.md (and the render color
 * shape in references/text_component_structure.md) in one place, so asset-definition parsing and text
 * component parsing share the exact same logic instead of duplicating it. The functions operate on a
 * WRJSON value tree (produced by a shared JSONObjectPool); they do not consult the pool or the parser.
 *
 * Errors: a value that is present but of the wrong shape raises ErrorCode_InvalidJSON. Owned strings the
 * helpers produce are heap-allocated and released with Memory_Free.
 */


// Value conversions.
/**
 * @brief Converts a JSON value to an int64 (accepts an integer, a real number truncated to integer, or a
 *        numeric string with optional 0x/0b base prefix).
 * @param value The value to convert; must not be NULL.
 * @param outValue [out] Receives the integer. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON if the value is not an integer/number/parsable string.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ValueToInt64(const JSONObjectValue* value, int64_t* outValue);

/**
 * @brief Converts a JSON value to a double (accepts a real number, an integer, or a numeric string).
 * @param value The value to convert; must not be NULL.
 * @param outValue [out] Receives the double. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON if the value is not a number/parsable string.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ValueToDouble(const JSONObjectValue* value, double* outValue);

/**
 * @brief Converts a JSON value to a boolean (accepts a JSON boolean or the string "true"/"false").
 * @param value The value to convert; must not be NULL.
 * @param outValue [out] Receives the boolean. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON if the value is not a boolean/parsable string.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ValueToBoolean(const JSONObjectValue* value, bool* outValue);

/**
 * @brief Copies a JSON string value's bytes into an owned, NUL-terminated string.
 * @param value The value; must not be NULL and must be of type JSONValueType_String.
 * @param outString [out] Receives the owned copy (release with Memory_Free). Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON if @p value is not a string.
 */
Error GameJSON_ValueToOwnedString(const JSONObjectValue* value, unsigned char** outString);


// Structured value parsers.
/**
 * @brief Parses a color from a JSON value (hex string "#rrggbb[aa]", array [r,g,b,(a)], or compound
 *        {r,g,b,(a)}). Missing alpha is opaque (255).
 * @param value The JSON value; must not be NULL.
 * @param outColor [out] Receives the color. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON on a malformed color.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ParseColor(const JSONObjectValue* value, Color* outColor);

/**
 * @brief Parses a render color from a JSON value.
 *
 * Accepts either a plain color (any GameJSON_ParseColor form) — whose RGB becomes the tint, alpha becomes
 * the opacity, and brightness is 1.0 — or a compound with a @c tint / @c brightness / @c opacity key:
 * `{ "tint": color, "brightness": number, "opacity": number }` (brightness defaults to 1.0, opacity
 * defaults to the tint's alpha).
 * @param value The JSON value; must not be NULL.
 * @param outColor [out] Receives the render color. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON on a malformed render color.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ParseRenderColor(const JSONObjectValue* value, RenderColor* outColor);

/**
 * @brief Parses a decimal 2D vector from a JSON value (compound {x,y} or array [x,y]).
 * @param value The JSON value; must not be NULL.
 * @param outVector [out] Receives the vector. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON on a malformed vector.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ParseVector2(const JSONObjectValue* value, Vector2* outVector);

/**
 * @brief Parses an integer 2D vector from a JSON value (compound {x,y} or array [x,y]).
 * @param value The JSON value; must not be NULL.
 * @param outX [out] Receives x. Must not be NULL.
 * @param outY [out] Receives y. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON on a malformed vector.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ParseVector2Int(const JSONObjectValue* value, int64_t* outX, int64_t* outY);


// Optional compound readers (default when the key is absent).
/**
 * @brief Reads an optional integer key from a compound (see GameJSON_ValueToInt64 for accepted forms).
 * @param compound The compound; must not be NULL.
 * @param key The key; must not be NULL.
 * @param defaultValue Value used when the key is absent.
 * @param outValue [out] Receives the value. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON if present but not an integer form.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ReadOptionalInteger(JSONCompound* compound, const unsigned char* key,
    int64_t defaultValue, int64_t* outValue);

/**
 * @brief Reads an optional decimal key from a compound (see GameJSON_ValueToDouble for accepted forms).
 * @param compound The compound; must not be NULL.
 * @param key The key; must not be NULL.
 * @param defaultValue Value used when the key is absent.
 * @param outValue [out] Receives the value. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON if present but not a number form.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ReadOptionalDouble(JSONCompound* compound, const unsigned char* key,
    double defaultValue, double* outValue);

/**
 * @brief Reads an optional boolean key from a compound (see GameJSON_ValueToBoolean for accepted forms).
 * @param compound The compound; must not be NULL.
 * @param key The key; must not be NULL.
 * @param defaultValue Value used when the key is absent.
 * @param outValue [out] Receives the value. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON if present but not a boolean form.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ReadOptionalBoolean(JSONCompound* compound, const unsigned char* key,
    bool defaultValue, bool* outValue);

/**
 * @brief Reads an optional integer vector key from a compound (compound {x,y} or array [x,y]).
 * @param compound The compound; must not be NULL.
 * @param key The key; must not be NULL.
 * @param defaultX Default x when absent.
 * @param defaultY Default y when absent.
 * @param outX [out] Receives x. Must not be NULL.
 * @param outY [out] Receives y. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON if present but malformed.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ReadOptionalVector2Int(JSONCompound* compound, const unsigned char* key,
    int64_t defaultX, int64_t defaultY, int64_t* outX, int64_t* outY);
