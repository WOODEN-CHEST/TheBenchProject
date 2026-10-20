#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRGHDF.h"


/**
 * @file GameGHDF.h
 * @brief Shared helpers for the game's GHDF values: integers written in the smallest type that holds them and read from
 *        any integer type whose value fits, and strings.
 *
 * The GHDF counterpart of GameJSON.h, so every GHDF writer and reader of the game treats integers the same way. Saves
 * and worlds sent over the network stay compact without the game choosing widths: a writer stores each integer in the
 * smallest GHDF type that holds it (200 as a UInt8, -300 as an Int16, a UInt64 only above UINT32_MAX), and a reader
 * takes an integer of any GHDF type, fixed width or encoded, as long as its value fits the range it needs. So data
 * written with wider types (older files, other tools) still reads. Strings are written with their terminator and read
 * back only without NUL characters inside. The property tables, the data values and the world files use these. Owns
 * nothing; no lifecycle.
 */


// Functions: types.
/**
 * @brief Returns the smallest signed GHDF type (Int8, Int16, Int32 or Int64) that holds every value in
 *        [@p minimum, @p maximum].
 * @param minimum The smallest value to hold.
 * @param maximum The largest value to hold; not below @p minimum.
 * @returns The type.
 */
GHDFValueType GameGHDF_GetSmallestSignedType(int64_t minimum, int64_t maximum);

/**
 * @brief Returns the smallest unsigned GHDF type (UInt8, UInt16, UInt32 or UInt64) that holds every value up to
 *        @p maximum.
 * @param maximum The largest value to hold.
 * @returns The type.
 */
GHDFValueType GameGHDF_GetSmallestUnsignedType(uint64_t maximum);

/**
 * @brief Reports whether a GHDF type is an integer type: any fixed-width integer, or the encoded integer.
 * @param type The type.
 * @returns true for UInt8, Int8, Int16, UInt16, Int32, UInt32, Int64, UInt64 and EncodedInteger.
 */
bool GameGHDF_IsIntegerType(GHDFValueType type);


// Functions: values.
/**
 * @brief Makes a GHDF value of a signed type holding @p value.
 * @param type Int8, Int16, Int32 or Int64, one that holds @p value (GameGHDF_GetSmallestSignedType); any other type
 *        gives an Int64.
 * @param value The value.
 * @returns The value, of @p type.
 */
GHDFObjectValue GameGHDF_CreateSigned(GHDFValueType type, int64_t value);

/**
 * @brief Makes a GHDF value of an unsigned type holding @p value.
 * @param type UInt8, UInt16, UInt32 or UInt64, one that holds @p value (GameGHDF_GetSmallestUnsignedType); any other
 *        type gives a UInt64.
 * @param value The value.
 * @returns The value, of @p type.
 */
GHDFObjectValue GameGHDF_CreateUnsigned(GHDFValueType type, uint64_t value);

/**
 * @brief Reads any GHDF integer as an int64.
 * @param value The value; must not be NULL.
 * @param outValue [out] Receives the value. Must not be NULL.
 * @returns true for an integer type; false for another type, or a UInt64 above INT64_MAX.
 */
bool GameGHDF_TryGetSigned(const GHDFObjectValue* value, int64_t* outValue);

/**
 * @brief Reads any GHDF integer as a uint64.
 * @param value The value; must not be NULL.
 * @param outValue [out] Receives the value. Must not be NULL.
 * @returns true for an integer type holding a value of at least 0; false for another type or a negative value.
 */
bool GameGHDF_TryGetUnsigned(const GHDFObjectValue* value, uint64_t* outValue);


// Functions: compound entries.
/**
 * @brief Sets a compound entry to a signed integer, in the smallest type that holds it.
 * @param compound The compound; must not be NULL.
 * @param id The entry id; not 0.
 * @param value The value.
 * @returns Success, or the error of GHDFCompound_SetValue (ErrorCode_IllegalArgument for a NULL compound or id 0).
 */
Error GameGHDF_PutSigned(GHDFCompound* compound, GHDFEntryID id, int64_t value);

/**
 * @brief Sets a compound entry to an unsigned integer, in the smallest type that holds it.
 * @param compound The compound; must not be NULL.
 * @param id The entry id; not 0.
 * @param value The value.
 * @returns Success, or the error of GHDFCompound_SetValue (ErrorCode_IllegalArgument for a NULL compound or id 0).
 */
Error GameGHDF_PutUnsigned(GHDFCompound* compound, GHDFEntryID id, uint64_t value);

/**
 * @brief Reads an optional compound entry as a signed integer in [@p minimum, @p maximum], from any integer type.
 * @param compound The compound; must not be NULL.
 * @param id The entry id.
 * @param minimum The smallest value accepted.
 * @param maximum The largest value accepted; not below @p minimum.
 * @param outValue [out] Receives the value when the entry is there; left unchanged otherwise. Must not be NULL.
 * @param outFound [out] Receives whether the entry is there. Must not be NULL.
 * @returns Success (also for an absent entry); ErrorCode_IllegalArgument for a NULL argument; ErrorCode_Deserialize
 *          when the entry is not an integer or its value is outside the range (the message gives the range; callers
 *          add which entry it was).
 */
Error GameGHDF_GetOptionalSigned(GHDFCompound* compound, GHDFEntryID id, int64_t minimum, int64_t maximum,
    int64_t* outValue, bool* outFound);

/**
 * @brief Reads an optional compound entry as an unsigned integer in [0, @p maximum], from any integer type.
 * @param compound The compound; must not be NULL.
 * @param id The entry id.
 * @param maximum The largest value accepted.
 * @param outValue [out] Receives the value when the entry is there; left unchanged otherwise. Must not be NULL.
 * @param outFound [out] Receives whether the entry is there. Must not be NULL.
 * @returns Success (also for an absent entry); ErrorCode_IllegalArgument for a NULL argument; ErrorCode_Deserialize
 *          when the entry is not an integer or its value is negative or above @p maximum (the message gives the
 *          range; callers add which entry it was).
 */
Error GameGHDF_GetOptionalUnsigned(GHDFCompound* compound, GHDFEntryID id, uint64_t maximum, uint64_t* outValue,
    bool* outFound);

/**
 * @brief Sets a compound entry to a string borrowed from @p pool (a copy of @p text with its terminator).
 * @param pool The pool owning @p compound; must not be NULL.
 * @param compound The compound; must not be NULL.
 * @param id The entry id; not 0.
 * @param text The NUL-terminated UTF-8 text; must not be NULL.
 * @returns Success; ErrorCode_EncodeError if the string could not be built; the error of GHDFCompound_SetValue (the
 *          string goes back to the pool then).
 */
Error GameGHDF_PutString(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, const unsigned char* text);

/**
 * @brief Reads an optional compound entry as a string without NUL characters inside.
 * @param compound The compound; must not be NULL.
 * @param id The entry id.
 * @param outText [out] Receives the NUL-terminated text when the entry is there (borrowed: owned by the compound, valid
 *        while it is); left unchanged otherwise. Must not be NULL.
 * @param outByteCount [out] Receives the text's length in bytes, without the terminator. Must not be NULL.
 * @param outFound [out] Receives whether the entry is there. Must not be NULL.
 * @returns Success (also for an absent entry); ErrorCode_IllegalArgument for a NULL argument; ErrorCode_Deserialize for
 *          an entry of another type or a string with a NUL character inside (callers add which entry it was).
 */
Error GameGHDF_GetOptionalString(GHDFCompound* compound, GHDFEntryID id, const unsigned char** outText,
    size_t* outByteCount, bool* outFound);
