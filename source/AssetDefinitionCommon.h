#pragma once
#include "AssetManager.h"
#include "wr/WRJSON.h"
#include "raylib/raylib.h"

/*
 * Private header (source/): shared JSON-parsing helpers for the standard asset-definition constructors.
 *
 * These operate on the WRJSON tree produced by a shared JSONObjectPool and implement the common value
 * shapes documented in references/asset_structure.md (locations, names, numbers that may arrive as
 * strings, texture properties, integer vectors, colors). Owned strings the helpers produce are
 * heap-allocated and released with Memory_Free (or the type's Deconstruct).
 */


/** @brief An asset location whose Value string is owned by the definition (unlike the borrowing AssetLocation). */
typedef struct OwnedAssetLocationStruct
{
    /** @brief File or reference. */
    AssetLocationType Type;
    /** @brief Owned, NUL-terminated UTF-8 path (file) or reference name. */
    unsigned char* Value;
} OwnedAssetLocation;

/** @brief Parsed texture properties (currently just the scaling filter). */
typedef struct TexturePropertiesStruct
{
    /** @brief A Raylib TEXTURE_FILTER_* value; defaults to TEXTURE_FILTER_BILINEAR. */
    int Filter;
} TextureProperties;


/**
 * @brief Deserializes raw bytes into a JSON tree and verifies the root is a compound.
 *
 * @param pool Shared JSON object pool that will own the produced tree.
 * @param rawData The raw definition bytes (UTF-8 JSON).
 * @param sourceDescription Origin string for error messages; may be NULL.
 * @param outRootValue [out] Receives the root value; the caller must return it to @p pool with
 *        JSONObjectPool_ReturnValue once finished (on success or failure paths). Set to a None value on
 *        failure so returning it is a safe no-op.
 * @param outRoot [out] Receives the root compound (borrowed from @p outRootValue). Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if the JSON is malformed or the root is not a compound.
 */
Error AssetJSON_DeserializeRoot(JSONObjectPool* pool, const GenericBuffer* rawData,
    const unsigned char* sourceDescription, JSONObjectValue* outRootValue, JSONCompound** outRoot);

/**
 * @brief Copies a JSON string value's bytes into an owned, NUL-terminated string.
 * @param value The value; must be of type JSONValueType_String.
 * @param outString [out] Receives the owned copy. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if @p value is not a string.
 */
Error AssetJSON_ValueToOwnedString(const JSONObjectValue* value, unsigned char** outString);

/**
 * @brief Reads the required "name" property of a definition root into an owned string.
 * @param root The root compound.
 * @param sourceDescription Origin for error messages; may be NULL.
 * @param outName [out] Receives the owned name. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if absent or not a string.
 */
Error AssetJSON_ReadName(JSONCompound* root, const unsigned char* sourceDescription, unsigned char** outName);

/**
 * @brief Parses an asset location from a JSON value (a bare string is a file path; a compound is
 *        {"type": "file"|"reference", "value": string}).
 * @param value The JSON value to parse.
 * @param sourceDescription Origin for error messages; may be NULL.
 * @param outLocation [out] Receives the owned location. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition on a malformed location.
 */
Error AssetJSON_ParseLocation(const JSONObjectValue* value, const unsigned char* sourceDescription,
    OwnedAssetLocation* outLocation);

/**
 * @brief Reads a required location-valued key from a compound.
 * @param compound The compound to read from.
 * @param key The key.
 * @param sourceDescription Origin for error messages; may be NULL.
 * @param outLocation [out] Receives the owned location. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if absent/malformed.
 */
Error AssetJSON_ReadLocation(JSONCompound* compound, const unsigned char* key,
    const unsigned char* sourceDescription, OwnedAssetLocation* outLocation);

/**
 * @brief Reads an optional string key into an owned copy.
 * @param compound The compound.
 * @param key The key.
 * @param outString [out] Receives the owned string when found, else NULL. Must not be NULL.
 * @param outFound [out] Receives whether the key was present. May be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if present but not a string.
 */
Error AssetJSON_ReadOptionalOwnedString(JSONCompound* compound, const unsigned char* key,
    unsigned char** outString, bool* outFound);

/**
 * @brief Reads an optional texture_properties compound into @p outProps (filter defaults to bilinear).
 * @param compound The compound to read from.
 * @param key The texture-properties key.
 * @param outProps [out] Receives the parsed properties. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if present but malformed.
 */
Error AssetJSON_ReadTextureProperties(JSONCompound* compound, const unsigned char* key, TextureProperties* outProps);

/**
 * @brief Reads an optional integer that may be a JSON number or a whitespace-trimmed string (0x/0b bases).
 * @param compound The compound.
 * @param key The key.
 * @param defaultValue Value used when the key is absent.
 * @param outValue [out] Receives the value. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if present but not an integer/number/parsable string.
 */
Error AssetJSON_ReadOptionalInteger(JSONCompound* compound, const unsigned char* key,
    int64_t defaultValue, int64_t* outValue);

/**
 * @brief Reads an optional boolean that may be a JSON boolean or the string "true"/"false".
 * @param compound The compound.
 * @param key The key.
 * @param defaultValue Value used when the key is absent.
 * @param outValue [out] Receives the value. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if present but not a boolean/parsable string.
 */
Error AssetJSON_ReadOptionalBoolean(JSONCompound* compound, const unsigned char* key,
    bool defaultValue, bool* outValue);

/**
 * @brief Reads an optional integer vector (compound {x,y} or array [x,y]).
 * @param compound The compound.
 * @param key The key.
 * @param defaultX Default x when absent.
 * @param defaultY Default y when absent.
 * @param outX [out] Receives x. Must not be NULL.
 * @param outY [out] Receives y. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if present but malformed.
 */
Error AssetJSON_ReadOptionalVectorInt(JSONCompound* compound, const unsigned char* key,
    int64_t defaultX, int64_t defaultY, int64_t* outX, int64_t* outY);

/**
 * @brief Parses a color from a JSON value (hex string "#rrggbb[aa]", array [r,g,b,(a)], or compound
 *        {r,g,b,(a)}). Missing alpha is opaque (255).
 * @param value The JSON value.
 * @param outColor [out] Receives the color. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition on a malformed color.
 */
Error AssetJSON_ParseColor(const JSONObjectValue* value, Color* outColor);

/**
 * @brief Frees an owned location's string and zeroes it.
 * @param self The location to free; may be NULL.
 */
void OwnedAssetLocation_Deconstruct(OwnedAssetLocation* self);

/**
 * @brief Builds a borrowing AssetLocation view over an owned location (valid while the owner lives).
 * @param self The owned location; must not be NULL.
 * @returns An AssetLocation whose Value borrows @p self->Value.
 */
static inline AssetLocation OwnedAssetLocation_View(const OwnedAssetLocation* self)
{
    AssetLocation Location;
    Location.Type = self->Type;
    Location.Value = self->Value;
    return Location;
}
