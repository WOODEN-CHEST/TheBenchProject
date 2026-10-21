#pragma once
#include "AssetManager.h"
#include "GameModel.h"
#include "WorldModel.h"
#include "wr/WRError.h"
#include "wr/WRJSON.h"

/*
 * Private header (source/): a world model definition's "collision" section (references/asset_structure.md, "World
 * models", "collision"), parsed while the definition is read and built while the world model loads. Used by the
 * world model type (source/WorldModelDefinition.c).
 *
 * PARSING checks every rule (the sets, their names and shapes, each shape's keys and sizes, the hull's points, the
 * placements' defaults) and refuses unknown keys, so a bad section skips the definition at startup like any other
 * error. LOADING builds the hulls and meshes (their models load as dependencies of the world model) and writes the sets
 * into one block the asset owns.
 */


// Types.
/** The parsed section; NULL when the definition has none. Owned by the world model definition. */
typedef struct WorldModelCollisionDefinitionStruct WorldModelCollisionDefinition;


// Functions.
/* Reads @p root's "collision" key, if any (*outDefinition NULL without one). Errors name the world model
 * (@p printableName) and the place; a broken rule is InvalidAssetDefinition, while a value the shared parsers refuse
 * keeps their code (InvalidJSON for a malformed vector) and so does a point hull that cannot be built
 * (InvalidOperation for flat points), as the world model's own parser wraps its inner errors. */
Error WorldModelCollisionDefinition_Parse(const char* printableName, JSONCompound* root,
    WorldModelCollisionDefinition** outDefinition);

/* Releases a parsed section; NULL does nothing. */
void WorldModelCollisionDefinition_Destroy(WorldModelCollisionDefinition* self);

/* Builds the loaded collision of @p self: loads the models its hulls and meshes name under @p dependencyUser, builds
 * them, and writes the sets into one block (*outBlock, owned: release it with WorldModelCollision_DestroyBlock). The
 * "bounds" fallback takes LOD 0 (@p lod0). On failure nothing is kept (the manager releases what loaded). */
Error WorldModelCollisionDefinition_Load(const WorldModelCollisionDefinition* self, const unsigned char* worldModelName,
    AssetManager* manager, AssetUserID dependencyUser, GameModel* lod0, void** outBlock,
    const WorldModelCollision** outCollision);

/* Releases a loaded collision block: its hulls, meshes and the block. NULL does nothing. */
void WorldModelCollision_DestroyBlock(void* block);
