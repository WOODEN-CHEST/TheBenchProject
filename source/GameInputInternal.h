#pragma once
#include <stdint.h>

/*
 * GameInput's internals for tests.
 */


/** Tests only: how many key-index entries the most-specific-binding check has looked at since the program started
 *  (main thread only, like the input), so a test can show a query looks only at the bindings on its own key. */
uint64_t GameInputInternal_GetMatchVisitCount(void);
