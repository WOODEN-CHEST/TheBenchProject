#pragma once
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "wr/WRGHDF.h"
#include "wr/WRBufferPool.h"
#include "wr/WRError.h"


/**
 * @file TextComponentGHDF.h
 * @brief GHDF (compound-tree) serialization and deserialization of text components.
 *
 * Converts between a text component tree and a GHDF compound tree (GHDFCompound / GHDFArray / values), NOT
 * a binary document — use GHDF_WriteDocument / GHDF_ReadDocument to go to and from bytes. This is the
 * game's compact binary storage form; the field layout is internal (integer entry IDs) and, unlike the
 * JSON form, is not user-facing.
 *
 * All GHDF structures are borrowed from a caller-provided GHDFObjectPool; components are produced by a
 * caller-provided TextComponentFactory. As with the JSON deserializer, every borrowed string is copied
 * into a byte buffer from a caller-provided WRBufferPool (kept alive by the caller and freed by
 * deconstructing it), and font/sprite handles are left unbound (names only; see TextComponentResolver).
 */


/**
 * @brief Serializes a component tree into a GHDF compound tree.
 *
 * Builds compounds/arrays/strings borrowed from @p pool. The returned compound is owned by @p pool;
 * release it with GHDFObjectPool_ReturnCompound (or by deconstructing the pool). On failure any partial
 * structure is returned to @p pool and @p outCompound is set to NULL.
 * @param component The component tree to serialize; must not be NULL.
 * @param pool The GHDF object pool that will own the produced tree; must not be NULL.
 * @param outCompound [out] Receives the root compound; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer argument is NULL; otherwise a pool/GHDF error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentGHDF_Serialize(const TextComponent* component, GHDFObjectPool* pool, GHDFCompound** outCompound);

/**
 * @brief Deserializes a GHDF compound tree into a component tree.
 *
 * Components are built with @p factory; every referenced string is copied into a byte buffer borrowed from
 * @p stringBufferPool (see the file comment). Font and sprite handles are left unbound. On failure any
 * partial component tree is returned to @p factory and @p outComponent is set to NULL.
 * @param compound The GHDF compound to deserialize; must not be NULL.
 * @param factory The factory that produces the components; must not be NULL.
 * @param stringBufferPool A byte-buffer pool the components' strings are copied into; must not be NULL and
 *        must outlive the components.
 * @param outComponent [out] Receives the root component; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer argument is NULL; ErrorCode_InvalidTextEncoding
 *          for invalid UTF-8 text; otherwise a factory/GHDF error (e.g. a missing required field).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentGHDF_Deserialize(GHDFCompound* compound, TextComponentFactory* factory,
    WRBufferPool* stringBufferPool, TextComponent** outComponent);
