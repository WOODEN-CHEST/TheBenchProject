#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "AssetManager.h"
#include "GameModel.h"
#include "WorldModel.h"
#include "wr/WRError.h"
#include "wr/WRJSON.h"

/*
 * Private header (source/): a world model definition's "water" section (references/asset_structure.md, "World models",
 * "water"), parsed while the definition is read and built while the world model loads. Used by the world model type
 * (source/WorldModelDefinition.c).
 *
 * PARSING checks every rule it can without the models (the slots, unique; the water materials' names; the limit) and
 * refuses unknown keys. LOADING checks that LOD 0 has meshes of each slot and loads each water material as the world
 * model's dependency, into one block the asset owns (the water slots and their materials' names).
 */


// Types.
/** The parsed section; NULL when the definition has none. Owned by the world model definition. */
typedef struct WorldModelWaterDefinitionStruct WorldModelWaterDefinition;


// Functions.
/* Reads @p root's "water" key, if any (*outDefinition NULL without one or with an empty array). Errors name the world
 * model (@p printableName) and the place (water[1].material_slot): a broken rule is InvalidAssetDefinition. */
Error WorldModelWaterDefinition_Parse(const char* printableName, JSONCompound* root,
    WorldModelWaterDefinition** outDefinition);

/* Whether the section draws a material slot as water (false for a NULL section). */
bool WorldModelWaterDefinition_HasSlot(const WorldModelWaterDefinition* self, int32_t materialSlot);

/* How many water slots the section has (0 for a NULL section), and one's material slot (index below the count). */
uint32_t WorldModelWaterDefinition_GetCount(const WorldModelWaterDefinition* self);
int32_t WorldModelWaterDefinition_GetSlot(const WorldModelWaterDefinition* self, uint32_t index);

/* Releases a parsed section; NULL does nothing. */
void WorldModelWaterDefinition_Destroy(WorldModelWaterDefinition* self);

/* Builds the water slots of @p model into one block (*outBlock, owned by the world model: release it with Memory_Free)
 * and points the model's fields into it; each water material loads from @p manager under @p dependencyUser. @p self may
 * be NULL (no water). On failure nothing is kept (the manager releases the materials that loaded): InvalidAssetData
 * naming the world model and the slot for a slot no mesh of LOD 0 (@p lod0) uses; a water material's own errors naming
 * the world model, the slot and the material. */
Error WorldModelWaterDefinition_Load(const WorldModelWaterDefinition* self, const unsigned char* worldModelName,
    AssetManager* manager, AssetUserID dependencyUser, GameModel* lod0, WorldModel* model, void** outBlock);
