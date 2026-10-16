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


/*
 * Accepted resource-file extensions per loader: what the shipped raylib build can decode through the
 * function each loader calls. Pass them to AssetManager_OpenResource / AssetManager_AcquireResourcePath so
 * a File location only ever picks a file the loader can read (and an unsupported file gets a clear error).
 */
/** @brief Texture images (LoadImage/LoadTexture): png, bmp, gif, qoi, dds. Used by sprite sheet images,
 *         texture animation frames and model material maps. */
extern const AssetExtensionFilter ASSET_EXTENSIONS_TEXTURE;
/** @brief Models (LoadModel): obj, gltf, glb, iqm, vox, m3d. */
extern const AssetExtensionFilter ASSET_EXTENSIONS_MODEL;
/** @brief Fonts (LoadFontEx): ttf, otf. BMFont .fnt is only read by raylib's LoadFont, which the font
 *         loader does not use. */
extern const AssetExtensionFilter ASSET_EXTENSIONS_FONT;
/** @brief Sounds (LoadWave): wav, ogg, mp3, qoa. The xm/mod tracker formats are only read by raylib's
 *         music streams, which the sound loader does not use. */
extern const AssetExtensionFilter ASSET_EXTENSIONS_SOUND;
/** @brief Shader stage sources (read as text): vert, frag, vs, fs, glsl. */
extern const AssetExtensionFilter ASSET_EXTENSIONS_SHADER_STAGE;


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
 * @returns Success; ErrorCode_InvalidJSON (from JSON_Deserialize) if the JSON is malformed;
 *          ErrorCode_InvalidAssetDefinition if the root is not a compound.
 */
Error AssetJSON_DeserializeRoot(JSONObjectPool* pool, const GenericBuffer* rawData,
    const unsigned char* sourceDescription, JSONObjectValue* outRootValue, JSONCompound** outRoot);

/**
 * @brief Copies a JSON string value's bytes into an owned, NUL-terminated string.
 * @param value The value; must be of type JSONValueType_String.
 * @param outString [out] Receives the owned copy. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON (from GameJSON) if @p value is not a string.
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
 * @returns Success; ErrorCode_InvalidJSON (from GameJSON) if present but not an integer, a whole in-range
 *          real number or a parsable string.
 */
Error AssetJSON_ReadOptionalInteger(JSONCompound* compound, const unsigned char* key,
    int64_t defaultValue, int64_t* outValue);

/**
 * @brief Reads an optional boolean that may be a JSON boolean or the string "true"/"false".
 * @param compound The compound.
 * @param key The key.
 * @param defaultValue Value used when the key is absent.
 * @param outValue [out] Receives the value. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON (from GameJSON) if present but not a boolean/parsable string.
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
 * @returns Success; ErrorCode_InvalidJSON (from GameJSON) if present but malformed.
 */
Error AssetJSON_ReadOptionalVectorInt(JSONCompound* compound, const unsigned char* key,
    int64_t defaultX, int64_t defaultY, int64_t* outX, int64_t* outY);

/**
 * @brief Parses a color from a JSON value (hex string "#rrggbb[aa]", array [r,g,b,(a)], or compound
 *        {r,g,b,(a)}). Missing alpha is opaque (255).
 * @param value The JSON value.
 * @param outColor [out] Receives the color. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON (from GameJSON) on a malformed color.
 */
Error AssetJSON_ParseColor(const JSONObjectValue* value, Color* outColor);

/**
 * @brief Reads the optional "format" key (a file extension without the dot) and checks the loader accepts it.
 *
 * The comparison is case-insensitive (ASCII). For a File location the format narrows the file choice to
 * that extension (see AssetFormat_SelectFilter); for a Reference location it names the temp file's type.
 * @param root The definition root compound.
 * @param accepted The extensions the loader can decode; must not be NULL.
 * @param assetKindName What the definition describes, for messages (e.g. u8"model").
 * @param outFormat [out] Receives the owned format string, or NULL when the key is absent. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if "format" is present but not a string or not an
 *          accepted extension (the message lists the accepted ones).
 */
Error AssetJSON_ReadOptionalFormat(JSONCompound* root, const AssetExtensionFilter* accepted,
    const unsigned char* assetKindName, unsigned char** outFormat);

/**
 * @brief Checks at parse time that a location whose loader needs a real file can be loaded.
 *
 * A Reference location is materialized as a temp file, which needs an extension, so it requires a format.
 * File locations always pass (their file supplies the extension).
 * @param self The parsed location; must not be NULL.
 * @param format The definition's validated "format", or NULL when absent.
 * @param accepted The loader's accepted extensions (listed in the message); must not be NULL.
 * @param assetKindName What the definition describes, for messages (e.g. u8"model").
 * @returns Success; ErrorCode_InvalidAssetDefinition for a Reference location without a format.
 */
Error OwnedAssetLocation_ValidateReferenceFormat(const OwnedAssetLocation* self, const unsigned char* format,
    const AssetExtensionFilter* accepted, const unsigned char* assetKindName);

/**
 * @brief Checks at parse time that a location for a resource without a "format" key is a file path.
 *
 * Texture locations (sprite sheet images, texture animation frames, model material maps) have no way to
 * name a format, so a Reference location for them could never be loaded and is rejected here instead.
 * @param self The parsed location; must not be NULL.
 * @param resourceKindName What the location points at, for messages (e.g. u8"texture").
 * @returns Success; ErrorCode_InvalidAssetDefinition for a Reference location.
 */
Error OwnedAssetLocation_RequireFile(const OwnedAssetLocation* self, const unsigned char* resourceKindName);

/**
 * @brief Frees an owned location's string and zeroes it.
 * @param self The location to free; may be NULL.
 */
void OwnedAssetLocation_Deconstruct(OwnedAssetLocation* self);

/**
 * @brief Picks the extension filter for resolving a resource: only the definition's format when it has
 *        one, else every extension the loader accepts.
 * @param accepted The loader's accepted extensions; must not be NULL.
 * @param format Pointer to the definition's validated format string (or to NULL when it has none). The
 *        returned filter borrows this pointer, so the pointed-to variable must outlive the filter.
 * @returns The filter to pass to AssetManager_OpenResource / AssetManager_AcquireResourcePath.
 */
static inline AssetExtensionFilter AssetFormat_SelectFilter(const AssetExtensionFilter* accepted,
    const unsigned char* const* format)
{
    if (*format == NULL)
    {
        return *accepted;
    }
    AssetExtensionFilter Filter;
    Filter.Extensions = format;
    Filter.Count = 1U;
    return Filter;
}

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
