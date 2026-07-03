#pragma once

/**
 * @brief Marks a parameter or variable as deliberately unused to suppress compiler warnings.
 *
 * Expands to a cast of x to void, which references x without using its value, silencing
 * unused-parameter / unused-variable diagnostics. The expression has no effect at run time.
 * @param x The parameter or variable to mark as intentionally unused.
 * @note x is evaluated (as (void)x), so it must be a usable expression; in practice it is given a
 *       plain parameter or variable name, not a side-effecting expression.
 */
#define UNUSED(x) ((void)x)
