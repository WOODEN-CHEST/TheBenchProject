#pragma once
#include <stdbool.h>
#include "Prefab.h"

/*
 * Private header (source/) of the prefab module and the system that makes its copies (WorldPrefabs): how one value reaches one row,
 * the same when a file is checked and when a copy is applied.
 */


// Functions.
/* Whether a target or an override may write a row: not a ReadOnly one, not a data map, not an object's name (what addresses it). */
bool PrefabInternal_IsRowWritable(const PropertyTable* table, const PropertyDescriptor* row);

/* Writes a value to a row of an instance: through the row's own reader, or a number into one or every axis of a vector row, leaving
 * the rest. On failure the instance is unchanged. */
Error PrefabInternal_ApplyRowValue(const PropertyTable* table, void* instance, const PropertyDescriptor* row, PrefabAxis axis,
    const DataValue* value, JSONObjectPool* pool);
