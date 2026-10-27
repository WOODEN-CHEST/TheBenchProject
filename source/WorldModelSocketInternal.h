#pragma once
#include "GameSkeleton.h"
#include "WorldModel.h"
#include "wr/WRError.h"
#include "wr/WRJSON.h"

/*
 * Private header (source/): a world model definition's "sockets" and "first_person" sections
 * (references/asset_structure.md, "World models"), parsed while the definition is read and built while the world model
 * loads. Used by the world model type (source/WorldModelDefinition.c).
 *
 * PARSING checks every rule it can without the models (names, keys, vectors, counts, the eyes naming a socket) and
 * refuses unknown keys. LOADING resolves the bone names against the world model's skeleton (a bone socket or a hidden
 * bone its skeleton lacks fails the load, as one on a world model without a skeleton does) and writes the sockets,
 * their names and the hidden bones into one block the asset owns.
 */


// Types.
/** The parsed sections; NULL when the definition has neither. Owned by the world model definition. */
typedef struct WorldModelSocketDefinitionStruct WorldModelSocketDefinition;


// Functions.
/* Reads @p root's "sockets" and "first_person" keys, if any (*outDefinition NULL with neither). Errors name the world
 * model (@p printableName) and the place: a broken rule is InvalidAssetDefinition; a vector the shared parser refuses
 * keeps its code (InvalidJSON). */
Error WorldModelSocketDefinition_Parse(const char* printableName, JSONCompound* root,
    WorldModelSocketDefinition** outDefinition);

/* Releases parsed sections; NULL does nothing. */
void WorldModelSocketDefinition_Destroy(WorldModelSocketDefinition* self);

/* Builds the sockets and the first-person block of @p model (whose LODs and skeleton are loaded) into one block
 * (*outBlock, owned by the world model: Memory_Free it), and points the model's fields into it. @p self may be NULL (no
 * sockets). On failure nothing is kept: InvalidAssetData naming the world model for a bone its skeleton lacks, or a
 * bone socket or a first_person block on a world model without a skeleton. */
Error WorldModelSocketDefinition_Load(const WorldModelSocketDefinition* self, const unsigned char* worldModelName,
    WorldModel* model, void** outBlock);
