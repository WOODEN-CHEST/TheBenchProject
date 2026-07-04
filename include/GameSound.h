#pragma once
#include "raylib/raylib.h"

/**
 * @file GameSound.h
 * @brief Thin wrapper around a fully-decoded Raylib Sound loaded by the asset manager.
 *
 * A GameSound holds a fully in-memory, ready-to-play Raylib Sound (not a streamed Music). It is the
 * static-state sound asset produced by the asset manager's sound type: it carries no per-play state and
 * may be shared and played from anywhere. The asset manager owns the wrapped Sound and unloads it when
 * the asset is unloaded; do not call UnloadSound on the handle returned by GameSound_GetRaylibSound.
 * Playing streamed music is out of scope for this wrapper.
 */


/**
 * @brief A loaded, playable sound backed by a Raylib Sound.
 *
 * Created and owned by the asset manager. The Raylib Sound is stored by value and released by whoever
 * loaded it (the asset manager) when the asset unloads.
 */
typedef struct GameSoundStruct
{
    /** @brief The wrapped Raylib sound; owned by the asset manager, not this wrapper. */
    Sound _raySound;
} GameSound;


/**
 * @brief Returns the underlying Raylib Sound for playback.
 *
 * The returned Sound is borrowed and remains valid until the asset is unloaded; do not unload it.
 * @param self The game sound to read; must not be NULL.
 * @returns The wrapped Raylib Sound by value.
 */
static inline Sound GameSound_GetRaylibSound(GameSound* self)
{
    return self->_raySound;
}
