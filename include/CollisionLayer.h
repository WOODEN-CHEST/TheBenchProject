#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "PropertyDescriptor.h"

/**
 * @file CollisionLayer.h
 * @brief Collision layers and the masks of layers a collider collides with.
 *
 * Every collider (WorldColliderComponent.h) is on one layer and carries a mask of the layers it collides with. Two
 * colliders block each other only when each one's mask holds the other's layer (the rule is symmetric). Queries pass
 * their own mask of the layers they see. A mask is a 32-bit set, bit N for the layer of value N (COLLISION_LAYER_BIT).
 *
 * Each layer has a default mask (CollisionLayer_GetDefaultMask), which a world model or a placement may replace. The
 * layer values are saved (GHDF) and never renumbered; JSON names them ("prop").
 */


// Macros.
/** @brief The mask bit of a layer. */
#define COLLISION_LAYER_BIT(layer) ((uint32_t)1U << (uint32_t)(layer))
/** @brief A mask of every layer. */
#define COLLISION_LAYER_MASK_ALL ((uint32_t)((1U << (uint32_t)CollisionLayer_Count) - 1U))


// Types.
/**
 * @brief The collision layers. Saved by value: never renumber.
 */
typedef enum CollisionLayerEnum
{
    /** @brief Static architecture and terrain: buildings, floors, walls. */
    CollisionLayer_World = 0,
    /** @brief Things placed in the world: furniture, crates, cups (the default). */
    CollisionLayer_Prop = 1,
    /** @brief The player and NPCs. */
    CollisionLayer_Character = 2,
    /** @brief Cars (later). */
    CollisionLayer_Vehicle = 3,
    /** @brief Physics objects (rigid bodies, later). */
    CollisionLayer_Dynamic = 4,
    /** @brief Trigger volumes: zones, cutscene triggers. */
    CollisionLayer_Trigger = 5,
    /** @brief Things only the interaction ray looks for: it collides with nothing by default. */
    CollisionLayer_Interaction = 6,
    /** @brief The number of layers (not a layer). */
    CollisionLayer_Count = 7
} CollisionLayer;


// Fields.
/** @brief The layers' names ("world", "prop", "character", "vehicle", "dynamic", "trigger", "interaction"), as an enum
 *  table: the collider's layer row reads it, and its values are the layers' bit indices, so the mask rows (an EnumSet)
 *  read it too. */
extern const PropertyEnumTable COLLISION_LAYER_ENUM;


// Functions.
/**
 * @brief The layers a collider on @p layer collides with unless a world model or the placement says otherwise.
 *
 * world, prop, character, vehicle and dynamic collide with each other; trigger sees prop, character, vehicle and
 * dynamic; interaction collides with nothing (only queries that ask for it see it).
 * @param layer The layer.
 * @returns The mask; 0 for a value that is not a layer.
 */
uint32_t CollisionLayer_GetDefaultMask(CollisionLayer layer);

/**
 * @brief Returns a layer's name ("prop").
 * @param layer The layer.
 * @returns The name (static), or NULL for a value that is not a layer.
 */
const unsigned char* CollisionLayer_GetName(CollisionLayer layer);

/**
 * @brief Finds a layer by name.
 * @param name The name, NUL-terminated UTF-8; NULL finds nothing.
 * @param outLayer [out] Receives the layer when found. Must not be NULL.
 * @returns true when @p name is a layer's name.
 */
bool CollisionLayer_TryParse(const unsigned char* name, CollisionLayer* outLayer);

/**
 * @brief Whether two colliders block each other: each one's mask holds the other's layer.
 * @param layerA The first collider's layer.
 * @param maskA The first collider's mask.
 * @param layerB The second collider's layer.
 * @param maskB The second collider's mask.
 * @returns true when they collide.
 */
static inline bool CollisionLayer_DoCollide(CollisionLayer layerA, uint32_t maskA, CollisionLayer layerB,
    uint32_t maskB)
{
    return ((maskA & COLLISION_LAYER_BIT(layerB)) != 0U) && ((maskB & COLLISION_LAYER_BIT(layerA)) != 0U);
}
