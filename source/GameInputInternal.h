#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "GameInput.h"

/*
 * GameInput's internals for tests.
 */


/** Tests only: how many key-index entries the most-specific-binding check has looked at since the program started
 *  (main thread only, like the input), so a test can show a query looks only at the bindings on its own key. */
uint64_t GameInputInternal_GetMatchVisitCount(void);

/** One tick of watching the window for the monitor's sake. Returns whether it is time to ask for the monitor's size again: at once the first
 *  time, soon after the window moved or changed size, and every so often otherwise. */
bool GameInputInternal_TickMonitorWatch(GameInput* self, Vector2 windowPosition, Vector2 windowSize);

/** Keeps the answer, with where the window was when it was asked; the next tick's sample carries the size. */
void GameInputInternal_StoreMonitorSize(GameInput* self, Vector2 monitorSize, Vector2 windowPosition, Vector2 windowSize);
