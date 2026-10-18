#pragma once
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "wr/WRJSON.h"
#include "wr/WRError.h"


/**
 * @file TextComponentJSON.h
 * @brief JSON (value-tree) serialization and deserialization of text components.
 *
 * Converts between a text component tree and a WRJSON value tree (JSONCompound / JSONArray / values), NOT JSON text:
 * use JSON_Serialize / JSON_Deserialize to go to and from bytes. The JSON shape is documented in
 * references/text_component_structure.md. JSON and GHDF share one codec, so both forms accept exactly the same
 * fields, defaults and ranges.
 *
 * Serializing writes only the fields that differ from their defaults and uses the shorthands where they fit (a
 * default-styled string component without children becomes a bare string; an empty component becomes an array of
 * its children). Deserializing ignores unknown keys, rejects every bad value with ErrorCode_InvalidJSON (the message
 * names the key and the component's JSON-pointer path, e.g. `Text component at "/children/2": "size": ...`), and
 * rejects trees nested deeper than TEXT_COMPONENT_MAX_DEPTH levels.
 */


/**
 * @brief Serializes a component tree into a JSON value tree.
 *
 * The produced structures are borrowed from @p pool; release the root with JSONObjectPool_ReturnValue when done.
 * On failure everything built so far is returned to @p pool and @p outValue is a null value.
 * @param component The root of the tree to serialize (it may also be a subtree); must not be NULL.
 * @param pool The JSON object pool that owns the produced tree; must not be NULL.
 * @param outValue [out] Receives the root value (a compound, array or string); must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise a pool or JSON error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentJSON_Serialize(const TextComponent* component, JSONObjectPool* pool, JSONObjectValue* outValue);

/**
 * @brief Deserializes a JSON value tree into a new component tree.
 *
 * Accepts the shapes in references/text_component_structure.md: a component object, an array (an empty component
 * whose children are the entries) or a bare string (a default string component). The components are built with
 * @p factory and own copies of every string, so @p value may be released right after the call.
 * @param value The JSON value to deserialize; must not be NULL.
 * @param factory The factory that produces the components; must not be NULL.
 * @param outComponent [out] Receives the root, owned by the caller (see TextComponentFactory.h), or NULL on failure;
 *        must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise ErrorCode_InvalidJSON, for every
 *          failure: a value that does not match the format, a tree deeper than TEXT_COMPONENT_MAX_DEPTH levels, or a
 *          component the factory could not build (the message names the key and path, then the cause). On failure
 *          nothing is left allocated.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentJSON_Deserialize(const JSONObjectValue* value, TextComponentFactory* factory,
    TextComponent** outComponent);
