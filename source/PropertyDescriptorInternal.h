#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "PropertyDescriptor.h"


/**
 * Private parts of the property descriptor module, shared by its three implementation files: PropertyDescriptor.c
 * (tables, instances, validation, access by key), PropertyDescriptorJSON.c and PropertyDescriptorGHDF.c. Not public
 * API. Field access goes through PropertyInternal_ReadField / WriteField, which copy bytes, so a scratch instance in a
 * plain byte buffer is accessed the same way as a real struct.
 */


// Macros.
/** Bytes of scratch storage on the stack for a read; bigger structs get a heap block. */
#define PROPERTY_SCRATCH_INLINE_SIZE ((size_t)1024)


// Types.
/**
 * A scratch instance for an all-or-nothing read: PropertyScratch_Begin fills it, the reader parses into it,
 * PropertyScratch_Commit validates it and moves it into the real instance, or PropertyScratch_Abort drops it. Lives on
 * the reader's stack; never copied.
 */
typedef struct PropertyScratchStruct
{
    /** Inline storage for structs up to PROPERTY_SCRATCH_INLINE_SIZE bytes, aligned for any member. */
    union
    {
        max_align_t Alignment;
        unsigned char Bytes[PROPERTY_SCRATCH_INLINE_SIZE];
    } _inline;
    /** The scratch instance: the inline bytes, or a heap block for bigger structs. */
    void* _instance;
    /** Whether _instance is a heap block (freed when the scratch ends). */
    bool _isHeap;
} PropertyScratch;


// Functions: fields.
/** Returns whether values of @p type are owned strings (String, AssetName). */
bool PropertyInternal_IsStringType(PropertyType type);

/** Returns whether fields of @p type own what they point to (the strings, DataMap and the lists): the instance frees it,
 *  copies clone it, reads hand it over. */
bool PropertyInternal_IsOwnedType(PropertyType type);

/** Allocates a float list of @p count values (1 to PROPERTY_FLOAT_LIST_MAX_COUNT), all 0, for a reader to fill in
 *  place before it hands the list to a field; the caller owns it (PropertyFloatList_Destroy). */
PropertyFloatList* PropertyInternal_AllocateFloatList(size_t count);

/** Allocates an instance list of @p count instances (1 to PROPERTY_INSTANCE_LIST_MAX_COUNT), each at its defaults
 *  (PropertyInstanceEntry_CreateDefault), for a reader to fill in place before it hands the list to a field; the caller
 *  owns it (PropertyInstanceList_Destroy). */
PropertyInstanceList* PropertyInternal_AllocateInstanceList(size_t count);

/** Returns whether @p descriptor has every flag in @p flags. */
static inline bool PropertyInternal_HasFlags(const PropertyDescriptor* descriptor, uint32_t flags)
{
    return (descriptor->Flags & flags) == flags;
}

/** Reads a field of @p instance (an enum widened to its int32 value; a string as the instance's pointer). */
PropertyData PropertyInternal_ReadField(const PropertyDescriptor* descriptor, const void* instance);

/** Writes a field of @p instance. A string field takes the pointer as it is: ownership is the caller's business. */
void PropertyInternal_WriteField(const PropertyDescriptor* descriptor, void* instance, const PropertyData* data);

/** Hands an owned value (a string, a map or a float list, or NULL) to an owned field, releasing the field's previous
 *  value. */
void PropertyInternal_AdoptOwned(const PropertyDescriptor* descriptor, void* instance, void* owned);

/** Reports whether two values of @p type are equal (floats by value, strings by bytes, a RenderColor without its tint's
 *  alpha). */
bool PropertyInternal_AreDataEqual(PropertyType type, const PropertyData* a, const PropertyData* b);

/** Reports whether a field of @p instance equals its default. */
bool PropertyInternal_IsDefault(const PropertyDescriptor* descriptor, const void* instance);

/** Finds an enum entry by value; NULL when the table has none. */
const PropertyEnumEntry* PropertyInternal_FindEnumByValue(const PropertyEnumTable* table, int32_t value);

/** Appends the entries' names to @p destination as one string: "\"both\", \"near\" or \"far\"". */
Error PropertyInternal_AppendEnumNames(const PropertyEnumTable* table, GenericBuffer* destination);

/** Finds an enum entry by the first @p byteCount bytes of @p name (not NUL-terminated); NULL when the table has none. */
const PropertyEnumEntry* PropertyInternal_FindEnumByName(const PropertyEnumTable* table, const unsigned char* name,
    size_t byteCount);


// Functions: errors.
/** Checks a value against the property's type rules and its descriptor's rules; ArgumentOutOfRange names the table, the
 *  key, the C member and the rule. */
Error PropertyInternal_CheckData(const PropertyTable* table, const PropertyDescriptor* descriptor, const PropertyData* data);

/** Makes an error of @p code reading "<table>.<key>: <inner message>" (or "<table>: ..." without a descriptor), and
 *  deconstructs @p inner. */
Error PropertyInternal_WrapError(ErrorCode code, const PropertyTable* table, const PropertyDescriptor* descriptor,
    Error* inner);

/** Gives @p inner's message another code (ArgumentOutOfRange from validation becomes InvalidJSON in a JSON read),
 *  deconstructing @p inner. A success stays a success. */
Error PropertyInternal_Recode(Error* inner, ErrorCode code);


// Functions: the read scratch.
/** Starts a read of @p instance: the scratch holds every persistent property at its default and a deep copy of every
 *  other described property. */
void PropertyScratch_Begin(PropertyScratch* self, const PropertyTable* table, const void* instance);

/** Validates the scratch, then moves every described property into @p instance (releasing the instance's old strings)
 *  and calls the table's AfterSet with a NULL descriptor. On a validation failure the scratch is aborted, @p instance
 *  is untouched and the error carries @p failureCode. The scratch is ended either way. */
Error PropertyScratch_Commit(PropertyScratch* self, const PropertyTable* table, void* instance, ErrorCode failureCode);

/** Ends the scratch without committing, releasing its strings. */
void PropertyScratch_Abort(PropertyScratch* self, const PropertyTable* table);
