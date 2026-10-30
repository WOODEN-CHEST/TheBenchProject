#pragma once
#include "AssetManager.h"

/*
 * Private header (source/): declarations of the standard asset-type definition constructors.
 *
 * Each is an AssetDefinitionConstructor (see AssetManager.h) that parses a JSON definition blob (using the
 * shared JSONObjectPool carried in userData) into a concrete definition for its type. They are wired up by
 * AssetManager_CreateStandardAssetTypes. Implemented in their respective modules
 * (SpriteSheetDefinition.c, SoundDefinition.c, etc.); this header lets AssetManager.c reference them
 * without exposing them as public API.
 *
 * The standard type names and directory names used at registration.
 */

/** @brief Standard sprite sheet type name / directory. */
#define ASSET_TYPE_NAME_SPRITE_SHEET      ((const unsigned char*)u8"sprite_sheet")
#define ASSET_TYPE_DIRECTORY_SPRITE_SHEET ((const unsigned char*)u8"sprite_sheets")
/** @brief Standard sprite animation type name / directory. */
#define ASSET_TYPE_NAME_SPRITE_ANIMATION      ((const unsigned char*)u8"sprite_animation")
#define ASSET_TYPE_DIRECTORY_SPRITE_ANIMATION ((const unsigned char*)u8"sprite_animations")
/** @brief Standard sound type name / directory. */
#define ASSET_TYPE_NAME_SOUND      ((const unsigned char*)u8"sound")
#define ASSET_TYPE_DIRECTORY_SOUND ((const unsigned char*)u8"sounds")
/** @brief Standard font type name / directory. */
#define ASSET_TYPE_NAME_FONT      ((const unsigned char*)u8"font")
#define ASSET_TYPE_DIRECTORY_FONT ((const unsigned char*)u8"fonts")
/** @brief Standard shader type name / directory. */
#define ASSET_TYPE_NAME_SHADER      ((const unsigned char*)u8"shader")
#define ASSET_TYPE_DIRECTORY_SHADER ((const unsigned char*)u8"shaders")
/** @brief Standard model type name / directory. */
#define ASSET_TYPE_NAME_MODEL      ((const unsigned char*)u8"model")
#define ASSET_TYPE_DIRECTORY_MODEL ((const unsigned char*)u8"models")
/** @brief Standard texture type name / directory. */
#define ASSET_TYPE_NAME_TEXTURE      ((const unsigned char*)u8"texture")
#define ASSET_TYPE_DIRECTORY_TEXTURE ((const unsigned char*)u8"textures")
/** @brief Standard world model type name / directory (WorldModel.h: LOD models, draw distance, render defaults and
 *  variants, built on model and texture assets). */
#define ASSET_TYPE_NAME_WORLD_MODEL      ((const unsigned char*)u8"world_model")
#define ASSET_TYPE_DIRECTORY_WORLD_MODEL ((const unsigned char*)u8"world_models")
/** @brief Standard world sprite type name / directory (WorldSprite.h: how a sprite animation stands, is lit and is seen
 *  in the world, built on a sprite animation asset). */
#define ASSET_TYPE_NAME_WORLD_SPRITE      ((const unsigned char*)u8"world_sprite")
#define ASSET_TYPE_DIRECTORY_WORLD_SPRITE ((const unsigned char*)u8"world_sprites")
/** @brief Standard character type name / directory (CharacterAsset.h: the defaults of a kind of character, its body and how
 *  it moves). */
#define ASSET_TYPE_NAME_CHARACTER      ((const unsigned char*)u8"character")
#define ASSET_TYPE_DIRECTORY_CHARACTER ((const unsigned char*)u8"characters")
/** @brief Standard animation library type name / directory (AnimationLibrary.h: the skeletal animation clips of a glTF
 *  file, which world models name for their placements to play). */
#define ASSET_TYPE_NAME_ANIMATION_LIBRARY      ((const unsigned char*)u8"animation_library")
#define ASSET_TYPE_DIRECTORY_ANIMATION_LIBRARY ((const unsigned char*)u8"animation_libraries")
/** @brief Standard animation graph type name / directory (AnimationGraph.h: which clip an animated object plays when,
 *  its layers, transitions and special actions, which world models name next to their animation libraries). */
#define ASSET_TYPE_NAME_ANIMATION_GRAPH      ((const unsigned char*)u8"animation_graph")
#define ASSET_TYPE_DIRECTORY_ANIMATION_GRAPH ((const unsigned char*)u8"animation_graphs")
/** @brief Standard terrain type name / directory (TerrainAsset.h: a terrain's free-form mesh, its palette and density
 *  channels, and the defaults of its placements). */
#define ASSET_TYPE_NAME_TERRAIN      ((const unsigned char*)u8"terrain")
#define ASSET_TYPE_DIRECTORY_TERRAIN ((const unsigned char*)u8"terrains")
/** @brief Standard terrain material type name / directory (TerrainMaterial.h: one material of a terrain's palette, its
 *  maps' pixels and settings). */
#define ASSET_TYPE_NAME_TERRAIN_MATERIAL      ((const unsigned char*)u8"terrain_material")
#define ASSET_TYPE_DIRECTORY_TERRAIN_MATERIAL ((const unsigned char*)u8"terrain_materials")
/** @brief Standard climate type name / directory (Climate.h: how a world's weather behaves: its presets, their seasons,
 *  links and rates). */
#define ASSET_TYPE_NAME_CLIMATE      ((const unsigned char*)u8"climate")
#define ASSET_TYPE_DIRECTORY_CLIMATE ((const unsigned char*)u8"climates")

/** @brief Definition-file extension (without the dot) for every standard type: their definitions are JSON.
 *  AssetManager_ReadDefinitions uses it to skip non-definition resource files sharing the type directory. */
#define ASSET_TYPE_DEFINITION_EXTENSION ((const unsigned char*)u8"json")


/** @brief Builds a sprite sheet definition from a JSON blob. See AssetDefinitionConstructor. */
Error SpriteSheetDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/** @brief Builds a sprite animation definition from a JSON blob. See AssetDefinitionConstructor. */
Error SpriteAnimationDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/** @brief Builds a sound definition from a JSON blob. See AssetDefinitionConstructor. */
Error SoundDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/** @brief Builds a font definition from a JSON blob. See AssetDefinitionConstructor. */
Error FontDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/** @brief Builds a shader definition from a JSON blob. See AssetDefinitionConstructor. */
Error ShaderDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/** @brief Builds a model definition from a JSON blob. See AssetDefinitionConstructor. */
Error ModelDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/** @brief Builds a texture definition from a JSON blob. See AssetDefinitionConstructor. */
Error TextureDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/** @brief Builds a world model definition from a JSON blob (WorldModelDefinition.c). See AssetDefinitionConstructor. */
Error WorldModelDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/** @brief Builds a world sprite definition from a JSON blob (WorldSpriteDefinition.c). See AssetDefinitionConstructor. */
Error WorldSpriteDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/** @brief Builds a character definition from a JSON blob (CharacterDefinition.c). See AssetDefinitionConstructor. */
Error CharacterDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/** @brief Builds an animation library definition from a JSON blob (AnimationLibraryDefinition.c). See
 *  AssetDefinitionConstructor. */
Error AnimationLibraryDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);
/** @brief Builds an animation graph definition from a JSON blob (AnimationGraphDefinition.c). See
 *  AssetDefinitionConstructor. */
Error AnimationGraphDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);
/** @brief Builds a terrain definition from a JSON blob (TerrainDefinition.c). See AssetDefinitionConstructor. */
Error TerrainDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);
/** @brief Builds a climate definition from a JSON blob (ClimateDefinition.c). See AssetDefinitionConstructor. */
Error ClimateDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);
/** @brief Builds a terrain material definition from a JSON blob (TerrainMaterialDefinition.c). See
 *  AssetDefinitionConstructor. */
Error TerrainMaterialDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition);

/**
 * @brief Decodes the image file of a texture definition into 8-bit RGBA pixels on the CPU (TextureDefinition.c), the
 *        body of AssetManager_LoadTexturePixels.
 * @param definition A texture definition (the manager's record); one not built by the texture type is refused.
 * @param manager The manager that resolves its file.
 * @param maxSide The largest width or height to read (at least 1; the texture type's limit applies above it).
 * @param outPixels [out] Receives the pixels (the caller frees them with TexturePixels_Deconstruct); zeroed on failure.
 * @returns Success; ErrorCode_InvalidOperation for a definition that is not the texture type's; the file's errors (not
 *          found, ambiguous, not decodable: ErrorCode_InvalidAssetData, also for a DDS file, which is never decoded, a
 *          size outside 1 to the limit, checked before the conversion, or a compressed image).
 */
Error TextureDefinition_LoadPixels(const AssetDefinition* definition, AssetManager* manager, int32_t maxSide,
    TexturePixels* outPixels);
