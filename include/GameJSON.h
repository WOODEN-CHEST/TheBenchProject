#pragma once
#include <stdbool.h>
#include <stdint.h>
// Color/Vector2 come from Raylib; RenderColor from the renderer; the JSON value types from WRJSON; Vector3Double (a
// position far from the world's origin) from WRVectorMath.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "wr/WRJSON.h"
#include "wr/WRError.h"
#include "wr/WRVectorMath.h"


/**
 * @file GameJSON.h
 * @brief Shared parsers and writers for the game's common JSON value shapes (numbers, booleans, strings, colors,
 *        render colors, vectors).
 *
 * These implement the value conventions documented in references/asset_structure.md (and the render color
 * shape in references/text_component_structure.md) in one place, so asset-definition parsing, text component
 * parsing and the property tables share the exact same logic instead of duplicating it. The parsers operate on a
 * WRJSON value tree (produced by a shared JSONObjectPool); they do not consult the pool or the parser. The writers
 * (GameJSON_Create*Value) build values in the same conventions, so whatever they write the parsers read back exactly.
 *
 * Errors: every value that is present but bad (wrong shape, unparsable numeric string, non-finite or out-of-range
 * number) raises ErrorCode_InvalidJSON, whatever the underlying parser reported. Owned strings the helpers produce
 * are heap-allocated and released with Memory_Free.
 */


// Value conversions.
/**
 * @brief Converts a JSON value to an int64 (accepts an integer, a whole real number, or a numeric string
 *        with optional 0x/0b base prefix).
 *
 * A real number is accepted only when it is a whole number within the int64 range: `60.0` converts to
 * 60, while `2.9`, `1e19`, infinities and NaN are rejected rather than truncated (an out-of-range
 * conversion would otherwise be undefined behaviour).
 * @param value The value to convert; must not be NULL.
 * @param outValue [out] Receives the integer. Must not be NULL. Unchanged on failure.
 * @returns Success; ErrorCode_InvalidJSON if the value is not an integer, a whole in-range real number or
 *          a parsable string (a parse failure keeps the parser's message).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ValueToInt64(const JSONObjectValue* value, int64_t* outValue);

/**
 * @brief Converts a JSON value to a uint64 (accepts a non-negative integer, a whole real number within the uint64
 *        range, or a numeric string with optional 0x/0b base prefix).
 *
 * JSON integers only reach INT64_MAX, so values above it are written as strings (see GameJSON_CreateUInt64Value),
 * which this accepts up to UINT64_MAX. Negative values, fractions, infinities and NaN are rejected.
 * @param value The value to convert; must not be NULL.
 * @param outValue [out] Receives the integer. Must not be NULL. Unchanged on failure.
 * @returns Success; ErrorCode_InvalidJSON if the value is not a non-negative integer, a whole in-range real number
 *          or a parsable string (a parse failure keeps the parser's message).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ValueToUInt64(const JSONObjectValue* value, uint64_t* outValue);

/**
 * @brief Converts a JSON value to a finite double (accepts a real number, an integer, or a numeric string).
 * @param value The value to convert; must not be NULL.
 * @param outValue [out] Receives the double. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON if the value is not a number or parsable string, or is infinite or NaN.
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
 * @returns Success; ErrorCode_InvalidJSON on a malformed color: a channel that is not a whole number within [0, 255]
 *          (never clamped), or a hex string with other than 6 or 8 hex digits (0-9, a-f, A-F; no sign or spaces).
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
 * @returns Success; ErrorCode_InvalidJSON on a malformed render color, or a brightness or opacity outside [0, 1].
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ParseRenderColor(const JSONObjectValue* value, RenderColor* outColor);

/**
 * @brief Parses a decimal 2D vector from a JSON value (compound {x,y} or array [x,y]).
 * @param value The JSON value; must not be NULL.
 * @param outVector [out] Receives the vector. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON on a malformed vector, or a component that is not finite or does not fit
 *          a float.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ParseVector2(const JSONObjectValue* value, Vector2* outVector);

/**
 * @brief Parses a decimal 3D vector from a JSON value (compound {x,y,z} or array [x,y,z]).
 *
 * Unlike GameJSON_ParseVector2, an array must have exactly three elements, so a color or a 4D value given by
 * mistake is rejected instead of half-read.
 * @param value The JSON value; must not be NULL.
 * @param outVector [out] Receives the vector. Must not be NULL. Unchanged on failure.
 * @returns Success; ErrorCode_InvalidJSON on a malformed vector, or a component that is not finite or does not fit
 *          a float.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ParseVector3(const JSONObjectValue* value, Vector3* outVector);

/**
 * @brief Parses a 3D vector like GameJSON_ParseVector3, but keeps every component as a double.
 *
 * For values converted before they are stored, where reading a float first would round them twice (angles given
 * in degrees and stored in radians). The components only have to be finite; the caller checks their range.
 * @param value The JSON value; must not be NULL.
 * @param outComponents [out] Receives x, y and z (at least three doubles). Must not be NULL. Unchanged on failure.
 * @returns Success; ErrorCode_InvalidJSON on a malformed vector or a component that is not a finite decimal.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ParseVector3Doubles(const JSONObjectValue* value, double* outComponents);

/**
 * @brief Parses a 3D vector of doubles (a position far from the world's origin): the forms GameJSON_ParseVector3
 *        accepts, each component kept as the finite double its text reads as.
 * @param value The JSON value; must not be NULL.
 * @param outVector [out] Receives the vector. Must not be NULL. Unchanged on failure.
 * @returns Success; ErrorCode_InvalidJSON on a malformed vector or a component that is not a finite decimal.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_ParseVector3Double(const JSONObjectValue* value, Vector3Double* outVector);

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


// Value writers (the parsers above read every value they build back exactly).
/**
 * @brief Makes a JSON number for a float, written as the shortest decimal that reads back to the same float.
 *
 * So 0.1f is written as 0.1, not 0.10000000149011612. A non-finite float stays non-finite, which JSON_Serialize
 * refuses; check values before writing them.
 * @param value The float.
 * @returns A real-number value; it borrows nothing from a pool.
 */
JSONObjectValue GameJSON_CreateFloatValue(float value);

/**
 * @brief Makes a JSON string holding a copy of a NUL-terminated UTF-8 string (without the terminator, like every
 *        WRJSON string).
 * @param pool The pool to borrow the string from; must not be NULL.
 * @param text The string; must not be NULL.
 * @param outValue [out] Receives the string value, owned by the caller (store it in a container or return it to
 *        @p pool). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_BufferTooLarge if the string could
 *          not be built.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_CreateStringValue(JSONObjectPool* pool, const unsigned char* text, JSONObjectValue* outValue);

/**
 * @brief Makes a JSON value for a uint64: an integer when it fits the JSON integer range (up to INT64_MAX), a
 *        decimal string otherwise (GameJSON_ValueToUInt64 reads both).
 * @param pool The pool to borrow the string from, when one is needed; must not be NULL.
 * @param value The integer.
 * @param outValue [out] Receives the value, owned by the caller (store it in a container or return it to
 *        @p pool). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p pool or @p outValue is NULL; ErrorCode_BufferTooLarge if the
 *          string could not be built.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_CreateUInt64Value(JSONObjectPool* pool, uint64_t value, JSONObjectValue* outValue);

/**
 * @brief Makes a JSON string for a color: "#rrggbb" when it is opaque (alpha 255), "#rrggbbaa" otherwise.
 * @param pool The pool to borrow the string from; must not be NULL.
 * @param color The color.
 * @param outValue [out] Receives the string value, owned by the caller (store it in a container or return it to
 *        @p pool). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p pool or @p outValue is NULL; ErrorCode_BufferTooLarge if the
 *          string could not be built.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_CreateColorValue(JSONObjectPool* pool, Color color, JSONObjectValue* outValue);

/**
 * @brief Makes a JSON value for a render color: "#rrggbb" when brightness and opacity are both 1, otherwise
 *        { "tint": [r, g, b], "brightness": number, "opacity": number }.
 *
 * The tint's alpha is not written: a render color ignores it (the opacity carries the alpha).
 * @param pool The pool to borrow the string or compound from; must not be NULL.
 * @param color The render color.
 * @param outValue [out] Receives the value, owned by the caller (store it in a container or return it to
 *        @p pool). Must not be NULL. Nothing stays borrowed on failure.
 * @returns Success; ErrorCode_IllegalArgument if @p pool or @p outValue is NULL; ErrorCode_BufferTooLarge if a
 *          string could not be built.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_CreateRenderColorValue(JSONObjectPool* pool, RenderColor color, JSONObjectValue* outValue);

/**
 * @brief Makes a JSON array [x, y] for a 2D vector (components written as by GameJSON_CreateFloatValue).
 * @param pool The pool to borrow the array from; must not be NULL.
 * @param vector The vector.
 * @param outValue [out] Receives the array value, owned by the caller (store it in a container or return it to
 *        @p pool). Must not be NULL. Nothing stays borrowed on failure.
 * @returns Success; ErrorCode_IllegalArgument if @p pool or @p outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_CreateVector2Value(JSONObjectPool* pool, Vector2 vector, JSONObjectValue* outValue);

/**
 * @brief Makes a JSON array [x, y, z] for a 3D vector (components written as by GameJSON_CreateFloatValue).
 * @param pool The pool to borrow the array from; must not be NULL.
 * @param vector The vector.
 * @param outValue [out] Receives the array value, owned by the caller (store it in a container or return it to
 *        @p pool). Must not be NULL. Nothing stays borrowed on failure.
 * @returns Success; ErrorCode_IllegalArgument if @p pool or @p outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_CreateVector3Value(JSONObjectPool* pool, Vector3 vector, JSONObjectValue* outValue);

/**
 * @brief Makes a JSON array [x, y, z] for a 3D vector of doubles: each component a real number, which JSON_Serialize
 *        writes as the shortest text that reads back to the same double.
 *
 * A float's shortest text (GameJSON_CreateFloatValue) has at most 9 significant digits, so a double read from it
 * prints that same text again: a vector a float writer wrote is written back unchanged.
 * @param pool The pool to borrow the array from; must not be NULL.
 * @param vector The vector (finite components).
 * @param outValue [out] Receives the array value, owned by the caller (store it in a container or return it to
 *        @p pool). Must not be NULL. Nothing stays borrowed on failure.
 * @returns Success; ErrorCode_IllegalArgument if @p pool or @p outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameJSON_CreateVector3DoubleValue(JSONObjectPool* pool, Vector3Double vector, JSONObjectValue* outValue);
