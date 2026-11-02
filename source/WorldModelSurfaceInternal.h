#pragma once
#include "GameModel.h"
#include "WorldModel.h"
#include "wr/WRError.h"
#include "wr/WRJSON.h"

/*
 * Private header (source/): a world model definition's "surfaces" section (references/asset_structure.md, "World
 * models", "surfaces"), parsed while the definition is read and built while the world model loads. Used by the world
 * model type (source/WorldModelDefinition.c).
 *
 * PARSING checks every rule it can without the models (names, slots, resolutions, kinds, emissions, the limits) and
 * refuses unknown keys. LOADING finds each surface's meshes in LOD 0 (the meshes of its material slot, which must have
 * texture coordinates), builds their triangles into a collision mesh in the object's own space (after LOD 0's import
 * transform) and copies each kept triangle's three texture coordinates beside it, into one block the asset owns.
 */


// Types.
/** The parsed section; NULL when the definition has none. Owned by the world model definition. */
typedef struct WorldModelSurfaceDefinitionStruct WorldModelSurfaceDefinition;


// Functions.
/* Reads @p root's "surfaces" key, if any (*outDefinition NULL without one). Errors name the world model
 * (@p printableName) and the place (surfaces[1].resolution): a broken rule is InvalidAssetDefinition. */
Error WorldModelSurfaceDefinition_Parse(const char* printableName, JSONCompound* root,
    WorldModelSurfaceDefinition** outDefinition);

/* Releases a parsed section; NULL does nothing. */
void WorldModelSurfaceDefinition_Destroy(WorldModelSurfaceDefinition* self);

/* Builds the surfaces of @p model from LOD 0 (@p lod0) into one block (*outBlock, owned by the world model: release it
 * with WorldModelSurfaces_DestroyBlock) and points the model's fields into it. @p self may be NULL (no surfaces). On
 * failure nothing is kept: InvalidAssetData naming the world model and the surface for a slot no mesh of LOD 0 uses, a
 * slot's mesh without texture coordinates or with coordinates that are not finite, or triangles that make no mesh. */
Error WorldModelSurfaceDefinition_Load(const WorldModelSurfaceDefinition* self, const unsigned char* worldModelName,
    GameModel* lod0, WorldModel* model, void** outBlock);

/* Releases a loaded surfaces block: each surface's mesh, then the block. NULL does nothing. */
void WorldModelSurfaces_DestroyBlock(void* block);
