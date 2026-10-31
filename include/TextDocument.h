#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// The document embeds its binding lists, instance pool and built-in font copy by value, and its API passes
// ProgramTime and asset user ids.
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "AssetManager.h"
#include "GameFont.h"
#include "SpriteAnimation.h"
#include "ProgramTime.h"
#include "wr/WRMemory.h"
#include "wr/WRObjectPool.h"
#include "wr/WRError.h"


/**
 * @file TextDocument.h
 * @brief The single owner of one rich text: its component tree, its asset user and its asset bindings.
 *
 * A TextDocument owns a text component tree and everything needed to show it:
 *  - its own AssetUserID, under which it loads the fonts and sprite animations the tree names;
 *  - the bindings from those names to the loaded assets, made the first time a name is resolved and kept until the
 *    document is destroyed (a name that fails to load is logged once and remembered as failed);
 *  - ONE playback instance per sprite-animation name: every sprite component naming the same animation shows the
 *    same frame. TextDocument_Update advances the instances once per tick.
 *
 * Text never holds fonts or animations itself (TextComponent.h), so destroying the document releases everything at
 * once and nothing can dangle. TextLayout (TextLayout.h) measures and arranges the tree through the document,
 * which is when names are bound: in update code, after the window exists.
 *
 * FONT FALLBACK. A string component without a font name uses the document's default font name; a document without
 * one (or whose default font fails to load) uses raylib's built-in font. A named font that fails to load falls back
 * the same way.
 *
 * TRANSLATIONS. Translate components show TextDocument_ResolveTranslation(key) when it returns text, else their
 * fallback, else their key. There are no string tables yet, so the fallback always shows.
 *
 * LIFETIME. Construct with TextDocument_Construct and release with TextDocument_Deconstruct. Never move or copy a
 * constructed document (layouts point at its bindings); embed it in a stable object instead. The factory and the
 * asset manager must outlive it. Main thread only.
 */


// Types.
/** @brief The game's logger; full type in Logger.h. Borrowed. */
typedef struct LoggerStruct Logger;

/**
 * @brief One rich text and its assets. All fields are internal; use the TextDocument_* functions.
 */
typedef struct TextDocumentStruct
{
    /** @brief The factory the tree comes from and is returned to; borrowed. */
    TextComponentFactory* _factory;
    /** @brief Loads the named assets; borrowed, may be NULL (then names never bind: built-in font, no sprites). */
    AssetManager* _assets;
    /** @brief Reports names that fail to load (once each); borrowed, may be NULL. */
    Logger* _logger;
    /** @brief The document's own asset user (ASSET_USER_ID_INVALID without an asset manager). */
    AssetUserID _assetUser;
    /** @brief The owned tree, or NULL for an empty document. */
    TextComponent* _root;
    /** @brief Owned font asset name used by components without one, or NULL for raylib's built-in font. */
    unsigned char* _defaultFontName;
    /** @brief Font bindings (private record type), one per resolved name. */
    GenericBuffer _fontBindings;
    /** @brief Sprite-animation bindings (private record type), one per resolved name. */
    GenericBuffer _animationBindings;
    /** @brief Storage of the playback instances (stable addresses; deconstructing the pool tears them down). */
    ObjectPool _instancePool;
    /** @brief raylib's built-in font (not an asset; raylib owns it until CloseWindow), wrapped again whenever raylib's
     *         font changes (it has no glyphs before InitWindow). */
    GameFont _builtInFont;
    /** @brief Changes whenever the root or the default font name changes. */
    uint64_t _revision;
} TextDocument;


// Lifecycle.
/**
 * @brief Initializes an empty document.
 * @param self The document to initialize; must not be NULL.
 * @param factory The factory the document's trees come from; borrowed, must not be NULL, must outlive the document.
 * @param assets The asset manager names are loaded through; borrowed, may be NULL (nothing is ever loaded: text
 *        uses raylib's built-in font and sprites draw nothing). Must outlive the document.
 * @param logger Reports names that fail to load, once each; borrowed, may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p factory is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextDocument_Construct(TextDocument* self, TextComponentFactory* factory, AssetManager* assets, Logger* logger);

/**
 * @brief Returns the tree to the factory, tears down the playback instances and releases every asset the document
 *        loaded (its asset user is retired). Layouts built from the document must not be drawn afterwards.
 *        Best-effort: the first error is returned. Safe on NULL.
 * @param self The document, or NULL.
 * @returns Success, or the first error met while tearing down.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextDocument_Deconstruct(TextDocument* self);


// The tree.
/**
 * @brief Returns the document's tree.
 * @param self The document; must not be NULL.
 * @returns The borrowed root (owned by the document; edit it in place), or NULL for an empty document.
 */
static inline TextComponent* TextDocument_GetRoot(const TextDocument* self)
{
    return self->_root;
}

/**
 * @brief Makes @p root the document's tree; the document takes ownership and returns the previous tree to the
 *        factory. Bindings are kept, so names used by both trees are not loaded again.
 *
 * While the document holds the root (TextComponent_IsHeld), the tree API refuses to add it to a parent, give it to
 * another document or return it to the factory; it can still be edited in place.
 * @param self The document; must not be NULL.
 * @param root A root (no parent) created by the document's factory, or NULL to empty the document.
 * @returns Success (the new root is owned by the document even when returning the old tree failed; that error is
 *          returned); ErrorCode_IllegalArgument if @p self is NULL or @p root comes from another factory;
 *          ErrorCode_InvalidOperation if @p root has a parent or is already another document's tree (it stays where
 *          it was then).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextDocument_SetRoot(TextDocument* self, TextComponent* root);

/**
 * @brief Hands the document's tree to the caller, leaving the document empty.
 * @param self The document; must not be NULL.
 * @param outRoot [out] Receives the root (now owned by the caller), or NULL if the document was empty; must not be
 *        NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL.
 */
Error TextDocument_TakeRoot(TextDocument* self, TextComponent** outRoot);

/**
 * @brief Returns the document's revision; it changes whenever the root or the default font name changes. (Edits
 *        inside the tree change the root's own revision, TextComponent_GetRevision.)
 * @param self The document; must not be NULL.
 * @returns The revision.
 */
static inline uint64_t TextDocument_GetRevision(const TextDocument* self)
{
    return self->_revision;
}


// Fonts.
/**
 * @brief Returns the font asset name used by components without one.
 * @param self The document; must not be NULL.
 * @returns The owned name, or NULL when such components use raylib's built-in font.
 */
static inline const unsigned char* TextDocument_GetDefaultFontName(const TextDocument* self)
{
    return self->_defaultFontName;
}

/**
 * @brief Sets the font asset name used by components without one (copied in full: "mono" is kept as "tbp:mono").
 * @param self The document; must not be NULL.
 * @param fontName A font asset name, or NULL for raylib's built-in font.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p fontName is not
 *          a valid asset name.
 */
Error TextDocument_SetDefaultFontName(TextDocument* self, const unsigned char* fontName);


// Binding (TextLayout calls these; they may load assets, so call them from update code, never from render code).
/**
 * @brief Returns the font that draws text with the given font name, loading and binding it on first use.
 *
 * Applies the fallback described in the file comment, so it always produces a font. A name that fails to load is
 * logged once (WARNING) and falls back from then on without loading again.
 * @param self The document; must not be NULL.
 * @param fontName A string component's font name, or NULL for the document's default font.
 * @param outFont [out] Receives the borrowed font (valid until the document is destroyed); must not be NULL. It may be
 *        an empty font (no glyphs) only when raylib's built-in font is used before InitWindow.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise an internal error (not for content
 *          problems).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextDocument_ResolveFont(TextDocument* self, const unsigned char* fontName, const GameFont** outFont);

/**
 * @brief Returns the document's playback instance of the named sprite animation, loading the animation and creating
 *        the instance (with the animation's default playback) on first use.
 *
 * A name that fails to load is logged once (WARNING) and resolves to NULL from then on. Subscribe to the instance's
 * events or change its playback through the returned pointer; it stays valid until the document is destroyed.
 * @param self The document; must not be NULL.
 * @param animationName A sprite-animation asset name, or NULL.
 * @param outInstance [out] Receives the instance, or NULL (no name, no asset manager, or the load failed); must not be
 *        NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outInstance is NULL; otherwise an internal error (not
 *          for content problems).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextDocument_ResolveAnimation(TextDocument* self, const unsigned char* animationName,
    SpriteAnimationInstance** outInstance);

/**
 * @brief Returns the translation of a key in the document's language.
 *
 * TODO: there are no string tables yet (the rest of translation comes later), so this always returns NULL and
 * translate components show their fallback.
 * @param self The document; must not be NULL.
 * @param key The translation key; must not be NULL.
 * @returns The borrowed translated text, or NULL when the key has no translation.
 */
const unsigned char* TextDocument_ResolveTranslation(const TextDocument* self, const unsigned char* key);


// Ticking.
/**
 * @brief Advances every bound sprite-animation instance by the tick's passed time (call once per update tick).
 *
 * Raises the instances' end/loop events; handlers may edit the document.
 * @param self The document; must not be NULL.
 * @param time The tick's time; PassedTime must be finite and non-negative.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; otherwise the first error of an instance update (a
 *          broken invariant such as a non-finite passed time).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextDocument_Update(TextDocument* self, ProgramTime time);
