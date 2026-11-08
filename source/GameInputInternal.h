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

/** Puts right what the device layer says of the cursor, against the last tick's sample: raylib sets its position to (0, 0) each time the cursor
 *  leaves the window, and with a press held past the edge it leaves again at the release. That is no place, so the last one stands, until it
 *  says another. That may be long after the cursor is back: GLFW reports no movement when the cursor comes back to the very place it left from. On the
 *  window with no reset behind it (0, 0) is a real corner. The movement counts from the place that stands, but a captured cursor's stays the device's. */
void GameInputInternal_KeepCursorPlace(GameInput* self, GameInputSample* sample);

/** Copies a NUL-terminated text the system lent (the clipboard's) into @p outText: at most @p maxBytes of it, cut before a codepoint that
 *  would not fit whole, reading no further than that. Appends the bytes, no terminator. */
Error GameInputInternal_CopyLentText(const unsigned char* text, size_t maxBytes, GenericBuffer* outText);
