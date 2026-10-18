#pragma once
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "wr/WRGHDF.h"
#include "wr/WRError.h"


/**
 * @file TextComponentGHDF.h
 * @brief GHDF (compound-tree) serialization and deserialization of text components.
 *
 * Converts between a text component tree and a GHDF compound tree (GHDFCompound / GHDFArray / values), NOT a binary
 * document: use GHDF_Write / GHDF_Read to go to and from bytes. This is the game's compact binary
 * storage form; the field layout (integer entry ids, explicit type wire ids) is internal and not user-facing, but it
 * comes from the same codec as the JSON form, so both accept exactly the same fields, defaults and ranges.
 *
 * Serializing writes only the fields that differ from their defaults. Deserializing ignores unknown entry ids, rejects
 * every bad value with ErrorCode_Deserialize (the message names the field and the component's path), and rejects
 * trees nested deeper than TEXT_COMPONENT_MAX_DEPTH levels.
 */


/**
 * @brief Serializes a component tree into a GHDF compound tree.
 *
 * The produced structures are borrowed from @p pool; release the root with GHDFObjectPool_ReturnCompound(pool, root,
 * true) before the pool is deconstructed. Do not leave it to the pool's teardown: WRGHDF's GHDFObjectPool_Deconstruct
 * uses freed memory when a borrowed compound still holds arrays or strings (a known WRFramework bug). On failure
 * everything built so far is returned to @p pool and @p outCompound is NULL.
 * @param component The root of the tree to serialize (it may also be a subtree); must not be NULL.
 * @param pool The GHDF object pool that owns the produced tree; must not be NULL.
 * @param outCompound [out] Receives the root compound; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise a pool or GHDF error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentGHDF_Serialize(const TextComponent* component, GHDFObjectPool* pool, GHDFCompound** outCompound);

/**
 * @brief Deserializes a GHDF compound tree into a new component tree.
 *
 * The components are built with @p factory and own copies of every string, so @p compound may be released right
 * after the call.
 * @param compound The GHDF compound to deserialize; must not be NULL.
 * @param factory The factory that produces the components; must not be NULL.
 * @param outComponent [out] Receives the root, owned by the caller (see TextComponentFactory.h), or NULL on failure;
 *        must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise ErrorCode_Deserialize, for every
 *          failure: a value that does not match the format (wrong entry type, out of range, unknown type id, invalid
 *          UTF-8, missing required field), a tree deeper than TEXT_COMPONENT_MAX_DEPTH levels, or a component the
 *          factory could not build (the message names the field and path, then the cause). On failure nothing is left
 *          allocated.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentGHDF_Deserialize(GHDFCompound* compound, TextComponentFactory* factory, TextComponent** outComponent);
