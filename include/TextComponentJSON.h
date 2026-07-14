#pragma once
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "wr/WRJSON.h"
#include "wr/WRBufferPool.h"
#include "wr/WRError.h"


/**
 * @file TextComponentJSON.h
 * @brief JSON (value-tree) serialization and deserialization of text components.
 *
 * Converts between a text component tree and a WRJSON value tree (JSONCompound / JSONArray / values), NOT
 * JSON text - use JSON_Serialize / JSON_Deserialize to go to and from bytes. The JSON shape is documented
 * in references/text_component_structure.md.
 *
 * All JSON structures are borrowed from a caller-provided JSONObjectPool; all components are produced by a
 * caller-provided TextComponentFactory. Deserialization copies every borrowed string (text, font and
 * animation names) into byte buffers borrowed from a caller-provided WRBufferPool so the components have
 * stable, caller-owned storage to reference; the caller keeps that pool alive while the components are used
 * and deconstructs it to free the strings. Deserialized components carry only names — their live GameFont
 * and SpriteAnimationInstance handles are bound separately (see TextComponentResolver).
 */


/**
 * @brief Serializes a component tree into a JSON value tree.
 *
 * Builds compounds/arrays/strings borrowed from @p pool per references/text_component_structure.md. The
 * returned value is owned by @p pool; release it with JSONObjectPool_ReturnValue when done. On failure any
 * partially built structure is returned to @p pool and @p outValue is set to a null value.
 * @param component The component tree to serialize; must not be NULL.
 * @param pool The JSON object pool that will own the produced tree; must not be NULL.
 * @param outValue [out] Receives the root JSON value (a compound); must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer argument is NULL; otherwise a pool/JSON error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentJSON_Serialize(const TextComponent* component, JSONObjectPool* pool, JSONObjectValue* outValue);

/**
 * @brief Deserializes a JSON value tree into a component tree.
 *
 * Accepts the shapes in references/text_component_structure.md: a component object, an array (an empty
 * component whose children are the entries), or a bare string (a default string component). Components are
 * built with @p factory. Every string the components reference is copied into a byte buffer borrowed from
 * @p stringBufferPool (one buffer per string), so it stays valid as long as that pool lives; the caller
 * owns @p stringBufferPool and frees the strings by deconstructing it once the components are no longer
 * used. Font and sprite handles are left unbound (names only). On failure any partial component tree is
 * returned to @p factory and @p outComponent is set to NULL.
 * @param value The JSON value to deserialize; must not be NULL.
 * @param factory The factory that produces the components; must not be NULL.
 * @param stringBufferPool A byte-buffer pool the components' strings are copied into; must not be NULL and
 *        must outlive the components.
 * @param outComponent [out] Receives the root component; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer argument is NULL; ErrorCode_InvalidJSON if the
 *          value does not match a component shape; ErrorCode_InvalidTextEncoding for invalid UTF-8 text;
 *          otherwise a factory/pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentJSON_Deserialize(const JSONObjectValue* value, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent);
