#pragma once
#include <stdbool.h>
#include "wr/WRError.h"
#include "WorldObject.h"


/**
 * @file WorldModelObject.h
 * @brief A 3D model object in a world: a WorldObject that references a model asset by name.
 *
 * A model object places a 3D model (resolved from the asset manager by name at render time; see the
 * asset manager) into the world with the base object's transform and tint. It adds two per-object
 * render toggles the world renderer honors: a 1-pixel outline (on by default) and whether the object
 * is exempt from the final pixelation pass.
 *
 * Create with WorldModelObject_Create (which heap-allocates it); ownership normally passes to a World.
 * Because the WorldObject base is the first member, a WorldModelObject* is also a valid WorldObject*.
 */


// Types.
/**
 * @brief A 3D model object: the base world object plus a referenced model asset and render toggles.
 *
 * Underscore-prefixed fields are read-only to outside code; use the accessors. The plain-named boolean
 * toggles are simple flags with no validation and may be set directly.
 */
typedef struct WorldModelObjectStruct
{
    /** @brief Abstract base (must be first). Carries id, name, transform and tint. */
    WorldObject Base;

    /** @brief Owned, NUL-terminated UTF-8 name of the model asset this object renders; may be NULL. */
    unsigned char* _modelAssetName;

    /** @brief When true (default), the renderer draws a 1-pixel outline around this object. */
    bool HasOutline;
    /** @brief When true, this object is excluded from the final pixelation pass (drawn at full resolution). */
    bool OmitPixelation;
} WorldModelObject;


// Functions.
/**
 * @brief Creates a heap-allocated 3D model object referencing a named model asset.
 *
 * Initializes the base (identity transform, opaque white tint, id 0), clones @p name and
 * @p modelAssetName into owned copies, and defaults HasOutline to true and OmitPixelation to false.
 * Release with the object's Destroy (via WorldObject_Destroy) or by handing it to a World.
 * @param name Borrowed NUL-terminated UTF-8 object name to clone, or NULL.
 * @param modelAssetName Borrowed NUL-terminated UTF-8 model asset name to clone, or NULL.
 * @param outObject [out] Receives the new object on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p outObject is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldModelObject_Create(const unsigned char* name, const unsigned char* modelAssetName,
    WorldModelObject** outObject);

/**
 * @brief Upcasts a model object to its WorldObject base.
 * @param self The model object; must not be NULL.
 * @returns The embedded base pointer.
 */
static inline WorldObject* WorldModelObject_AsObject(WorldModelObject* self)
{
    return &self->Base;
}

/**
 * @brief Returns the borrowed model asset name (may be NULL); valid until it is changed or freed.
 * @param self The model object; must not be NULL.
 * @returns The NUL-terminated UTF-8 model asset name, or NULL if unset.
 */
static inline const unsigned char* WorldModelObject_GetModelAssetName(const WorldModelObject* self)
{
    return self->_modelAssetName;
}

/**
 * @brief Replaces the referenced model asset name with an owned clone of @p modelAssetName.
 * @param self The model object; must not be NULL.
 * @param modelAssetName Borrowed NUL-terminated UTF-8 asset name to clone, or NULL to clear it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldModelObject_SetModelAssetName(WorldModelObject* self, const unsigned char* modelAssetName);
