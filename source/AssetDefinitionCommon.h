#pragma once
#include "AssetManager.h"
#include "GameModel.h"
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

/**
 * @brief Parsed "texture_properties": the scaling filter, the wrap mode and whether the texture has mipmaps.
 *
 * Every field defaults to what a texture got before the keys existed, so a definition without them loads unchanged.
 * Not every loader applies every field: sprite sheets, sprite animation frames and fonts apply only Filter (and their
 * definitions reject trilinear filtering); the texture type and model material maps apply all three, through
 * TextureProperties_Apply.
 */
typedef struct TexturePropertiesStruct
{
    /** @brief A raylib TEXTURE_FILTER_* value: TEXTURE_FILTER_POINT, TEXTURE_FILTER_BILINEAR (the default) or
     *         TEXTURE_FILTER_TRILINEAR (only with AreMipmapsEnabled). */
    int Filter;
    /** @brief A raylib TEXTURE_WRAP_* value; defaults to TEXTURE_WRAP_REPEAT, raylib's own default. */
    int Wrap;
    /** @brief Whether the texture gets a mipmap chain: generated at load unless its file brings mip levels (a dds).
     *         Defaults to false. */
    bool AreMipmapsEnabled;
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
/** @brief Animation libraries (read with cgltf, AnimationLibrary.h): gltf, glb. */
extern const AssetExtensionFilter ASSET_EXTENSIONS_ANIMATION_LIBRARY;
/** @brief A terrain's model (imported with cgltf, TerrainMesh.h): gltf, glb. */
extern const AssetExtensionFilter ASSET_EXTENSIONS_TERRAIN_MODEL;
/** @brief A terrain's binary form (TerrainMesh_ReadGHDF): ghdf. */
extern const AssetExtensionFilter ASSET_EXTENSIONS_TERRAIN_DATA;


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
 * @brief Reads the required "name" property of a definition root into an owned copy of its full form ("desk" is read
 *        as "tbp:desk", AssetManager_ParseAssetName).
 * @param root The root compound.
 * @param sourceDescription Origin for error messages; may be NULL.
 * @param outName [out] Receives the owned full name. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if absent, not a string or not a valid asset name (the message
 *          names the problem).
 */
Error AssetJSON_ReadName(JSONCompound* root, const unsigned char* sourceDescription, unsigned char** outName);

/**
 * @brief Replaces an owned asset name as written with an owned copy of its full form ("desk" becomes "tbp:desk").
 *
 * For the names a definition reads (a world model's LOD models, a world sprite's animation, ...): the loaders keep and
 * show full names. The old string is freed with Memory_Free when it is replaced.
 * @param name [in, out] The owned name, allocated with Memory_Allocate; must not be NULL and must point to a string.
 *        Left as it is when invalid.
 * @returns NULL on success; otherwise what is wrong with the name (a static description for the caller's message,
 *          AssetManager_DescribeAssetNameProblem).
 */
const char* AssetJSON_MakeFullAssetName(unsigned char** name);

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
 * @brief Reads an optional texture_properties compound into @p outProps.
 *
 * Its keys are all optional: "filtering" ("point", "bilinear" (default) or "trilinear"), "wrap" ("repeat" (default),
 * "clamp", "mirror_repeat" or "mirror_clamp") and "mipmaps" (a boolean, default false). Trilinear filtering blends
 * mip levels, so it needs "mipmaps": true, and a caller whose textures never have mipmaps rejects it outright. Every
 * key is validated whatever the caller applies (see TextureProperties).
 * @param compound The compound to read from.
 * @param key The texture-properties key.
 * @param areMipmapsSupported Whether the caller can give its textures mipmaps (the texture type and model material
 *        maps can; sprite sheets, sprite animation frames and fonts cannot).
 * @param outProps [out] Receives the parsed properties, or the defaults when the key is absent. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition if present but not a compound, for a filtering or wrap value
 *          that is not a known string, or for trilinear filtering without mipmaps; ErrorCode_InvalidJSON (from
 *          GameJSON) for a "mipmaps" value that is not a boolean.
 */
Error AssetJSON_ReadTextureProperties(JSONCompound* compound, const unsigned char* key, bool areMipmapsSupported,
    TextureProperties* outProps);

/**
 * @brief Reads an optional texture_properties compound like AssetJSON_ReadTextureProperties, but every key the compound
 *        leaves out (or the whole compound, when absent) takes its value from @p defaults.
 *
 * One rule ties the defaults together: when the compound leaves "filtering" out, sets "mipmaps" to false and the
 * default filtering is trilinear (which needs mipmaps), the filtering is bilinear. A compound that names "trilinear"
 * without mipmaps is still rejected.
 * @param compound The compound to read from.
 * @param key The texture-properties key.
 * @param areMipmapsSupported Whether the caller can give its textures mipmaps (see AssetJSON_ReadTextureProperties).
 * @param defaults The values of the keys left out; must not be NULL, and valid (trilinear only with mipmaps).
 * @param outProps [out] Receives the properties. Must not be NULL; may be @p defaults.
 * @param outIsPresent [out] Whether the key was there; may be NULL.
 * @returns As AssetJSON_ReadTextureProperties.
 */
Error AssetJSON_ReadTexturePropertiesWithDefaults(JSONCompound* compound, const unsigned char* key,
    bool areMipmapsSupported, const TextureProperties* defaults, TextureProperties* outProps, bool* outIsPresent);

/**
 * @brief Applies texture properties to a texture that was just loaded: generates its mipmaps when they are enabled
 *        and the file brought none, then sets its filter and wrap mode.
 *
 * Needs the GL context (main thread). A "mirror_clamp" wrap needs the GL_EXT_texture_mirror_clamp extension;
 * without it raylib keeps the previous wrap mode (repeat) and logs a warning.
 * @param self The parsed properties; must not be NULL.
 * @param texture [in,out] The texture; its mipmap count is updated when mipmaps are generated. Must not be NULL.
 * @param description What the texture is, for messages (e.g. its location); must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetData when mipmaps are enabled but cannot be generated: a compressed texture
 *          (dds) without its own mip levels, or a GPU that refuses (the texture is left as it was).
 */
Error TextureProperties_Apply(const TextureProperties* self, Texture2D* texture, const unsigned char* description);

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
 * @brief Converts a JSON value to a finite float: a number, an integer or a numeric string (GameJSON_ValueToDouble's
 *        forms) whose value lies within the float range.
 * @param value The JSON value; must not be NULL.
 * @param outValue [out] Receives the value. Must not be NULL. Unchanged on failure.
 * @returns Success; ErrorCode_InvalidJSON for a value that is not a number form, not finite, or beyond the float range.
 */
Error AssetJSON_ValueToFloat(const JSONObjectValue* value, float* outValue);

/**
 * @brief Reads an optional float key (see AssetJSON_ValueToFloat).
 * @param compound The compound.
 * @param key The key.
 * @param defaultValue Value used when the key is absent.
 * @param outValue [out] Receives the value. Must not be NULL.
 * @param outFound [out] Receives whether the key was present. May be NULL.
 * @returns Success; ErrorCode_InvalidJSON if present but not a finite number within the float range (the message names
 *          the key).
 */
Error AssetJSON_ReadOptionalFloat(JSONCompound* compound, const unsigned char* key, float defaultValue,
    float* outValue, bool* outFound);

/**
 * @brief Copies text from a definition into a buffer for an error message, cut to fit, with every byte that is not
 *        printable ASCII shown as '?' (a hostile file cannot put control characters or escape sequences into the log).
 * @param text The text, NUL-terminated; NULL copies as "".
 * @param buffer [out] Receives the NUL-terminated copy. Must not be NULL.
 * @param capacity The buffer's size in bytes, at least 1.
 */
void AssetJSON_CopyPrintable(const unsigned char* text, char* buffer, size_t capacity);

/**
 * @brief Rejects a compound that holds a key outside @p knownKeys, for definitions that refuse unknown keys (a
 *        misspelt key would otherwise be ignored without a word).
 * @param compound The compound to check.
 * @param knownKeys The keys the compound may hold; must not be NULL when @p knownKeyCount is above 0.
 * @param knownKeyCount The number of known keys.
 * @param where What the compound is, for the message, e.g. "world model \"desk\", lods[1]"; must not be NULL.
 * @returns Success when every key is known; ErrorCode_InvalidAssetDefinition naming the first unknown key (made
 *          printable, see AssetJSON_CopyPrintable), where it is and the keys allowed there; ErrorCode_BufferTooSmall if
 *          the keys could not be enumerated.
 */
Error AssetJSON_RejectUnknownKeys(JSONCompound* compound, const unsigned char* const* knownKeys, size_t knownKeyCount,
    const char* where);

/**
 * @brief Reads a material override's required "slot": the index of the material it changes in the model, a whole
 *        number from 0 to INT32_MAX.
 *
 * Shared by the model type's "material_overrides" and world model variants.
 * @param compound The override's compound.
 * @param outSlot [out] Receives the slot. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition when the slot is absent, not a whole number or out of range.
 */
Error AssetJSON_ReadMaterialSlot(JSONCompound* compound, int32_t* outSlot);

/**
 * @brief Reads the factor keys of a material override, each optional: "tint" and "emissive_color" (colours, any
 *        AssetJSON_ParseColor form), "metallic" and "roughness" (in [0, 1]) and "emissive_intensity" (>= 0).
 *
 * The one parser the model type's "material_overrides" and world model variants share. A key that is absent leaves its
 * factor's flag false.
 * @param compound The override's compound.
 * @param outFactors [out] Receives the factors. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON for a malformed value (the message names the key);
 *          ErrorCode_InvalidAssetDefinition for a number outside its range.
 */
Error AssetJSON_ReadMaterialFactors(JSONCompound* compound, GameMaterialFactors* outFactors);

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
