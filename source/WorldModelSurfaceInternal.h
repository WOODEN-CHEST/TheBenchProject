#pragma once
#include "AssetManager.h"
#include "GameModel.h"
#include "WorldModel.h"
#include "wr/WRError.h"
#include "wr/WRJSON.h"

/*
 * Private header (source/): a world model definition's "surfaces" section (references/asset_structure.md, "World
 * models", "surfaces"), parsed while the definition is read and built while the world model loads. Used by the world
 * model type (source/WorldModelDefinition.c).
 *
 * PARSING checks every rule it can without the models (names, slots, resolutions, kinds, emissions, the light, a CRT's
 * look, the limits) and refuses unknown keys. LOADING finds each surface's meshes in LOD 0 (the meshes of its material
 * slot, which must have texture coordinates), builds their triangles into a collision mesh in the object's own space
 * (after LOD 0's import transform) and copies each kept triangle's three texture coordinates beside it, into one block
 * the asset owns; takes the glass's frame for its light from them; and loads a CRT's mask tile (a texture) as the world
 * model's dependency, with the tile's mean colour read from its image file.
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

/* Whether a surface of the section shows its picture on a material slot (false for a NULL section). */
bool WorldModelSurfaceDefinition_HasSlot(const WorldModelSurfaceDefinition* self, int32_t materialSlot);

/* Builds the surfaces of @p model from LOD 0 (@p lod0) into one block (*outBlock, owned by the world model: release it
 * with WorldModelSurfaces_DestroyBlock) and points the model's fields into it; a CRT's mask tile loads from @p manager
 * under @p dependencyUser. @p self may be NULL (no surfaces). On failure nothing is kept (the manager releases the tiles
 * that loaded): InvalidAssetData naming the world model and the surface for a slot no mesh of LOD 0 uses, a slot's mesh
 * without texture coordinates or with coordinates that are not finite, or triangles that make no mesh; the tile's own
 * errors (AssetManager_LoadTexturePixels: a tile above CRT_SCREEN_MAX_MASK_TILE_SIDE pixels a side, or not read from an
 * image file) naming the world model, the surface and the tile. */
Error WorldModelSurfaceDefinition_Load(const WorldModelSurfaceDefinition* self, const unsigned char* worldModelName,
    AssetManager* manager, AssetUserID dependencyUser, GameModel* lod0, WorldModel* model, void** outBlock);

/* Releases a loaded surfaces block: each surface's mesh, then the block. NULL does nothing. */
void WorldModelSurfaces_DestroyBlock(void* block);
