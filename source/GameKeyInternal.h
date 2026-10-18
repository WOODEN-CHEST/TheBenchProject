#pragma once
#include <stdint.h>
#include "GameKey.h"


/*
 * Private to the input layer (GameKey.c and GameInput.c): the raylib code behind each GameKey. Nothing above the
 * input layer may use raylib key or mouse-button codes, which is why this is not in GameKey.h.
 */


/**
 * @brief Returns the raylib code of a key: a KeyboardKey value for a keyboard key, a MouseButton value for a mouse
 *        button (see GameKey_IsMouseButton).
 * @param key The key.
 * @returns The raylib code, or -1 for an invalid key.
 */
int32_t GameKey_GetRaylibCode(GameKey key);
