#pragma once
#include "TextComponent.h"
#include "AssetManager.h"
#include "wr/WRObjectPool.h"
#include "wr/WRError.h"


/**
 * @file TextComponentResolver.h
 * @brief Binds a text component tree's asset references to live asset handles.
 *
 * Deserialized components (see TextComponentJSON / TextComponentGHDF) carry only asset reference NAMES —
 * a string component's font name and a sprite component's animation name — not the live GameFont /
 * SpriteAnimationInstance needed to render. This module walks a tree and resolves those names through an
 * AssetManager: string components get their font loaded and set; sprite components get their animation
 * loaded and a playing instance created and set. Call it after parsing, whenever/wherever the components
 * need to become render-ready.
 *
 * Fonts and animations are loaded attributed to a caller-supplied AssetUserID, so the caller controls
 * their lifetime (release them with AssetManager_ReleaseAllAssetsForUser when done). Sprite animation
 * INSTANCES are created into a caller-provided ObjectPool; construct it with
 * TextComponentResolver_ConstructInstancePool and deconstruct it (once the components are no longer used)
 * to tear down every instance the resolver created. The components must not be used after that pool or the
 * asset user's assets are released.
 */


/**
 * @brief Initializes an ObjectPool suitable for the sprite-animation instances the resolver creates.
 *
 * The pool stores SpriteAnimationInstance values and is configured to deconstruct each instance when the
 * pool is deconstructed, so releasing every instance the resolver created is a single
 * ObjectPool_Deconstruct call.
 * @param instancePool [out] The pool to initialize; must not be NULL. Release with ObjectPool_Deconstruct.
 * @returns Success; ErrorCode_IllegalArgument if @p instancePool is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentResolver_ConstructInstancePool(ObjectPool* instancePool);

/**
 * @brief Resolves and binds the asset references throughout a component tree.
 *
 * Walks @p root and its whole subtree. For each string component with a font name whose font is not yet
 * bound, loads the font (attributed to @p user) and sets it. For each sprite component with an animation
 * name and no bound instance, loads the animation, creates a playing instance from @p instancePool, and
 * sets it. Components without a name, or already bound, are left as-is.
 *
 * Best-effort: a failed load/creation for one component does not stop the rest; the first error is
 * returned and every later error is released so none leak. Successfully-bound components stay bound.
 * @param root The component tree to resolve; must not be NULL.
 * @param assetManager The asset manager to load assets through; must not be NULL.
 * @param user The asset user id the loads are attributed to (keeps the assets alive).
 * @param instancePool A pool that owns the created sprite instances (see
 *        TextComponentResolver_ConstructInstancePool); must not be NULL.
 * @returns Success if everything bound; ErrorCode_IllegalArgument if @p root, @p assetManager or
 *          @p instancePool is NULL; otherwise the first non-success Error encountered while binding.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentResolver_ResolveTree(TextComponent* root, AssetManager* assetManager, AssetUserID user,
    ObjectPool* instancePool);
