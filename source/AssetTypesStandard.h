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
