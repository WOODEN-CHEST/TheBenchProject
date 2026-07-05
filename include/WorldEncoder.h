#pragma once
#include "wr/WRError.h"
#include "wr/WRGHDF.h"
#include "WorldDTO.h"


/**
 * @file WorldEncoder.h
 * @brief Encodes a WorldDTO into a GHDF compound (the in-memory object form of a save document).
 *
 * This module bridges the world persistence snapshot (WorldDTO) and the binary GHDF format. It builds a
 * GHDFCompound tree that mirrors the DTO: a root compound with the format version, the world name and id
 * counter, a nested environment compound, and an array of per-object compounds. Every field is keyed by a
 * stable numeric entry id (see the WorldEncoder*Field enums), which together define the save schema.
 *
 * It does NOT write bytes to disk. Turning the returned compound into a binary document/file is a separate
 * step (GHDF_Write), left for later. All compounds, arrays and strings in the result are borrowed from the
 * caller-supplied GHDFObjectPool; the caller owns the returned root and must release it back to the pool
 * (GHDFObjectPool_ReturnCompound) or simply deconstruct the pool.
 */


// Types.
/** @brief Current world save-schema version encoded into the root compound. */
#define WORLD_ENCODER_FORMAT_VERSION ((uint32_t)1)

/**
 * @brief Entry ids of the fields in the root world compound.
 */
typedef enum WorldEncoderRootFieldEnum
{
    /** @brief uint32 format version (WORLD_ENCODER_FORMAT_VERSION). */
    WorldEncoderRootField_FormatVersion = 1,
    /** @brief String world name (omitted when the world has no name). */
    WorldEncoderRootField_Name = 2,
    /** @brief uint64 saved id counter (the next object id). */
    WorldEncoderRootField_NextObjectId = 3,
    /** @brief Compound holding the environment settings (see WorldEncoderEnvironmentField). */
    WorldEncoderRootField_Environment = 4,
    /** @brief Array of compounds, one per object (see WorldEncoderObjectField). */
    WorldEncoderRootField_Objects = 5
} WorldEncoderRootField;

/**
 * @brief Entry ids of the fields in the environment compound.
 */
typedef enum WorldEncoderEnvironmentFieldEnum
{
    WorldEncoderEnvironmentField_TimeOfDay = 1,             /**< @brief float time of day [0;1]. */
    WorldEncoderEnvironmentField_IsDayNightCycleEnabled = 2,/**< @brief bool day-night cycle enabled. */
    WorldEncoderEnvironmentField_DayLengthSeconds = 3,      /**< @brief float day length in seconds. */
    WorldEncoderEnvironmentField_SkyTurbidity = 4,          /**< @brief float atmospheric turbidity. */
    WorldEncoderEnvironmentField_SkyGroundAlbedo = 5,       /**< @brief uint8[4] RGBA ground albedo. */
    WorldEncoderEnvironmentField_SkyTint = 6,               /**< @brief uint8[4] RGBA sky tint. */
    WorldEncoderEnvironmentField_SunColor = 7,              /**< @brief uint8[4] RGBA sun color. */
    WorldEncoderEnvironmentField_SunIntensity = 8,          /**< @brief float sun intensity. */
    WorldEncoderEnvironmentField_SunSizeMultiplier = 9,     /**< @brief float sun size multiplier. */
    WorldEncoderEnvironmentField_StarSeed = 10,             /**< @brief uint64 star seed. */
    WorldEncoderEnvironmentField_StarDensity = 11,          /**< @brief float star density. */
    WorldEncoderEnvironmentField_StarBrightness = 12,       /**< @brief float star brightness. */
    WorldEncoderEnvironmentField_AmbientSkylightColor = 13, /**< @brief uint8[4] RGBA ambient color. */
    WorldEncoderEnvironmentField_AmbientSkylightIntensity = 14, /**< @brief float ambient intensity. */
    WorldEncoderEnvironmentField_FogColor = 15,            /**< @brief uint8[4] RGBA fog color. */
    WorldEncoderEnvironmentField_FogStrength = 16,          /**< @brief float fog strength. */
    WorldEncoderEnvironmentField_IsBloomEnabled = 17,       /**< @brief bool bloom enabled. */
    WorldEncoderEnvironmentField_BloomStrength = 18,        /**< @brief float bloom strength. */
    WorldEncoderEnvironmentField_AreSunshaftsEnabled = 19,  /**< @brief bool sunshafts enabled. */
    WorldEncoderEnvironmentField_SunshaftStrength = 20,     /**< @brief float sunshaft strength. */
    WorldEncoderEnvironmentField_AreShadowsEnabled = 21,    /**< @brief bool shadows enabled. */
    WorldEncoderEnvironmentField_ShadowStrength = 22        /**< @brief float shadow strength. */
} WorldEncoderEnvironmentField;

/**
 * @brief Entry ids of the fields in a per-object compound.
 *
 * Ids 1-9 are common to every object. Ids 20-22 are the "visible object" fields shared by model and
 * sprite objects (the asset name and the two render toggles), disambiguated by the Type field. Ids 40+
 * are light-specific.
 */
typedef enum WorldEncoderObjectFieldEnum
{
    WorldEncoderObjectField_Type = 1,           /**< @brief uint8 WorldObjectType. */
    WorldEncoderObjectField_Id = 2,             /**< @brief uint64 object id. */
    WorldEncoderObjectField_Name = 3,           /**< @brief String object name (omitted when NULL). */
    WorldEncoderObjectField_Position = 4,       /**< @brief float[3] position (x,y,z). */
    WorldEncoderObjectField_Rotation = 5,       /**< @brief float[3] Euler rotation (x,y,z). */
    WorldEncoderObjectField_Scale = 6,          /**< @brief float[3] scale (x,y,z). */
    WorldEncoderObjectField_TintColor = 7,      /**< @brief uint8[4] RGBA tint color. */
    WorldEncoderObjectField_TintBrightness = 8, /**< @brief float tint brightness. */
    WorldEncoderObjectField_TintOpacity = 9,    /**< @brief float tint opacity. */

    WorldEncoderObjectField_AssetName = 20,     /**< @brief String model/sprite asset name (omitted when NULL). */
    WorldEncoderObjectField_HasOutline = 21,    /**< @brief bool outline toggle (model/sprite). */
    WorldEncoderObjectField_OmitPixelation = 22,/**< @brief bool pixelation-exempt toggle (model/sprite). */

    WorldEncoderObjectField_LightColor = 40,    /**< @brief uint8[4] RGBA light color. */
    WorldEncoderObjectField_LightIntensity = 41,/**< @brief float light intensity. */
    WorldEncoderObjectField_LightSize = 42,     /**< @brief float light size. */
    WorldEncoderObjectField_LightCastsShadows = 43 /**< @brief bool light casts-shadows toggle. */
} WorldEncoderObjectField;


// Functions.
/**
 * @brief Encodes a world DTO into a freshly borrowed GHDF root compound.
 *
 * Builds the full compound tree (root + environment compound + object array) using @p pool for every
 * compound, array and string. On success @p outCompound receives the root, which the caller OWNS: return
 * it with GHDFObjectPool_ReturnCompound(pool, root, true) or reclaim everything by deconstructing the
 * pool. On failure any partially built structures are returned to the pool and @p outCompound is set to
 * NULL.
 * @param dto The world snapshot to encode; must not be NULL.
 * @param pool The object pool to borrow compounds/arrays/strings from; must not be NULL.
 * @param outCompound [out] Receives the root compound on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if any argument is NULL; ErrorCode_EncodeError (or a
 *          propagated GHDF error) if the tree could not be built.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldEncoder_EncodeToCompound(const WorldDTO* dto, GHDFObjectPool* pool, GHDFCompound** outCompound);
