# GameGHDF

`include/GameGHDF.h`, `source/GameGHDF.c`. Tests: `tests/GameGHDFTests.c`. Added in T09.

The GHDF counterpart of [GameJSON.md](GameJSON.md): shared helpers for the game's GHDF values, so every writer and
reader treats them the same way. Owns nothing; no lifecycle.

## Integers: written small, read from any type

Saves and worlds sent over the network stay compact without the game choosing integer widths:

- **Writers** store each integer in the smallest GHDF type that holds it: `GameGHDF_PutUnsigned(compound, id, value)`
  (UInt8 up to 255, UInt16, UInt32, UInt64 only above `UINT32_MAX`) and `GameGHDF_PutSigned` (Int8 to Int64). The
  building blocks: `GameGHDF_GetSmallestUnsignedType(maximum)`, `GameGHDF_GetSmallestSignedType(minimum, maximum)` (for
  a whole list), `GameGHDF_CreateUnsigned(type, value)`, `GameGHDF_CreateSigned(type, value)`.
- **Readers** take an integer of any GHDF type, fixed width or encoded, as long as its value fits:
  `GameGHDF_GetOptionalUnsigned(compound, id, maximum, &value, &found)` and `GameGHDF_GetOptionalSigned(compound, id,
  minimum, maximum, &value, &found)`. An absent entry is no error (`found` false). An entry of another type, an array of
  integers (a GHDF value carries its element type, so the reader checks the entry's type too), a negative value for an
  unsigned read or a value out of range fails with `ErrorCode_Deserialize` ("The entry must be an integer from 0 to
  4294967295."); callers add which entry it was. Data written with wider types (older files, other tools) keeps
  reading. `GameGHDF_TryGetSigned`, `GameGHDF_TryGetUnsigned` and `GameGHDF_IsIntegerType` work on one value.

## Strings

`GameGHDF_PutString(pool, compound, id, text)` stores a copy (with its terminator) borrowed from the pool;
`GameGHDF_GetOptionalString(compound, id, &text, &length, &found)` returns the compound's own NUL-terminated text and
refuses a string with a NUL inside (it would read shorter than it is) or an entry of another type.

## Users

- The property tables ([PropertyDescriptors.md](PropertyDescriptors.md)): `Int32`, `UInt32`, `UInt64` and `ObjectId`
  properties are written in the smallest type and read from any integer type in their range (an object's `parent` of
  12 is one byte); strings go through the string helpers. `Enum` and `Date` stay encoded integers (as compact for their
  values).
- The data values ([ObjectData.md](ObjectData.md)): ints and object references, lists of them in the smallest type for
  the whole list.
- The world files ([WorldFile.md](WorldFile.md)): the format version, the next object id, object ids, the name.
