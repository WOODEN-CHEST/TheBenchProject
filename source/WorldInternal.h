#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "AssetManager.h"
#include "Logger.h"
#include "PropertyDescriptor.h"
#include "World.h"
#include "WorldComponent.h"
#include "WorldObject.h"


/**
 * Private header (source/) of the world data modules: World, WorldObject, WorldComponent and the component types
 * (WorldModelComponent, WorldSpriteComponent, WorldLightComponent). Not public API. It holds what those modules share:
 * the object's construction (objects are made by the world only), the hook every component table calls after a
 * change, the world's assets for components that load some, small helpers, and a seam for tests.
 */


// Functions: objects (WorldObject.c).
/**
 * Constructs an object in place for @p world: the table's defaults, the world, the id and a clone of @p name (checked
 * against the name's rules). Pair with WorldObject_Deconstruct. On failure nothing is left to release.
 */
Error WorldObject_Construct(WorldObject* self, World* world, uint64_t id, const unsigned char* name);

/** Releases what the object owns (its name). Its components must have been destroyed first; not the memory itself. */
void WorldObject_Deconstruct(WorldObject* self);


// Functions: components (World.c).
/**
 * The AfterSet hook of every component table: advances the component's revision, then calls its type's OnPropertySet
 * hook with the world, when the component is attached. A component's own setters call it too, after writing.
 */
void WorldInternal_ComponentAfterSet(void* instance, const PropertyDescriptor* descriptor);

/**
 * Tests only: constructs a world (as World_Construct2) whose component types are @p types instead of the standard
 * registry, so tests can add types of their own (a transform driver before any real one exists). The standard types
 * come first, as their own entries (WorldComponent_GetStandardTypeInfo), then the test types; entry i must have Type i
 * and pass WorldComponentTypeInfo_Check; at most WORLD_MAX_COMPONENT_TYPES entries. The array of pointers is copied,
 * the entries are borrowed and must outlive the world.
 */
Error WorldInternal_ConstructWithTypes(World* self, AssetManager* assets, Logger* logger,
    const WorldComponentTypeInfo* const* types, size_t typeCount);


// Functions: the world's assets, for components that load some under the world's user.
/** The world's asset manager (borrowed), or NULL for a world without assets. */
static inline AssetManager* WorldInternal_GetAssets(const World* world)
{
    return world->_assets;
}

/** The asset user the world's components hold their assets under (ASSET_USER_ID_INVALID without assets). */
static inline AssetUserID WorldInternal_GetAssetUser(const World* world)
{
    return world->_assetUser;
}

/** The world's logger (borrowed), or NULL. */
static inline Logger* WorldInternal_GetLogger(const World* world)
{
    return world->_logger;
}


// Functions: helpers.
/** Returns true if @p value is a real, finite float (not NaN and not +/- infinity). */
static inline bool WorldInternal_IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

/**
 * Clones a NUL-terminated UTF-8 string into an owned buffer, or gives NULL for NULL. Memory_Allocate aborts on
 * failure, so this cannot fail. Free the result with Memory_Free.
 */
static inline unsigned char* WorldInternal_CloneString(const unsigned char* source)
{
    if (source == NULL)
    {
        return NULL;
    }

    size_t Length = 0;
    while (source[Length] != (unsigned char)u8'\0')
    {
        Length++;
    }
    unsigned char* Copy = Memory_Allocate(Length + 1);
    Memory_Copy(source, Copy, Length + 1);
    return Copy;
}

/** Byte equality of two NUL-terminated strings; two NULLs are equal, NULL and a string are not. */
static inline bool WorldInternal_AreStringsEqual(const unsigned char* a, const unsigned char* b)
{
    if ((a == NULL) || (b == NULL))
    {
        return a == b;
    }
    size_t Index = 0;
    while ((a[Index] == b[Index]) && (a[Index] != (unsigned char)u8'\0'))
    {
        Index++;
    }
    return a[Index] == b[Index];
}
