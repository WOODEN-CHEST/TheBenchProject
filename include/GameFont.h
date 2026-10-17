#pragma once
// raylib's Font is embedded by value, so the wrapper needs the full raylib type.
#include "raylib/raylib.h"


/**
 * @brief A loaded font asset: a thin wrapper around a raylib Font (glyph atlas texture, glyph metrics and
 *        rectangles).
 *
 * Obtained from AssetManager_LoadFont and owned by the asset manager: the pointer is borrowed and stays valid
 * only while the asset user that loaded it holds the font. Releasing the last hold unloads the font
 * (UnloadFont), which frees the glyph arrays and the atlas texture. Fonts are rasterized once at load time
 * (currently raylib's default 95 ASCII glyphs at 32 px; see agents/GameAssets.md).
 *
 * Do not copy the wrapper or its Font by value into anything that can outlive that asset user: the copy
 * shares the glyph pointers and the texture id, which dangle once the font is unloaded. (Text components do
 * this today; it is scheduled to change to name-based references, handoff/2026-10_code_review/FIX_PLAN.md Phase 4.)
 */
typedef struct GameFontStruct
{
    /** @brief The raylib font; owned by the asset manager, read-only for consumers. */
    Font _rayFont;
} GameFont;
