#pragma once
#include <stdint.h>

/**
 * @brief A two-dimensional vector of signed 32-bit integer components.
 *
 * A plain value type holding an X and Y component, used for 2D integer coordinates, sizes, or
 * offsets. Copying is a plain value copy; it owns no resources.
 */
typedef struct Int32VectorStruct
{
    int32_t X;
    int32_t Y;
} Int32Vector;
