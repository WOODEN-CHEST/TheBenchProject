#include <inttypes.h>
#include "WorldScreens.h"
#include "Logger.h"
#include "World.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "WorldScreenComponent.h"
#include "raylib/raymath.h"
#include "wr/WRHash.h"
#include "wr/WRMemory.h"


// Macros.
/** Initial capacity of the entries and the occlusion ray's hits; they grow with the world. */
#define INITIAL_ENTRY_CAPACITY 8U
#define INITIAL_HIT_CAPACITY 8U


// Types.
/* One screen component and what it resolved to. */
typedef struct WorldScreenEntryStruct
{
    /** The component (borrowed: refreshed by every sync and used only during it, which also settles a new screen's
     *  power transition, since the world may remove it between syncs), its serial and the hash of the surface name it
     *  was resolved for (its other settings change nothing here). */
    WorldScreenComponent* Component;
    uint64_t Serial;
    uint64_t SurfaceNameHash;
    /** Its object's id. */
    uint64_t ObjectId;
    /** Whether it was resolved (a failure too: it is not retried until something changes). */
    bool IsResolved;
    /** The model component's serial (0: none) and the hash of the world model name it named (0: none). */
    uint64_t ModelSerial;
    uint64_t WorldModelNameHash;
    /** The world model (held under the screens' asset user), or NULL. */
    const WorldModel* Asset;
    /** Whether it shows on a surface of it, and which. */
    bool HasSurface;
    uint32_t SurfaceIndex;
} WorldScreenEntry;

struct WorldScreensStruct
{
    World* _world;
    AssetManager* _assets;
    AssetUserID _assetUser;
    Logger* _logger;
    /** The entries (WorldScreenEntry), in the order of the world's screen components after a sync. */
    GenericBuffer _entries;
    /** The entries of the last sync while the next one matches them (WorldScreenEntry). */
    GenericBuffer _previous;
    /** The occlusion ray's hits (WorldRayHit), reused. */
    GenericBuffer _hits;
};


// Static functions.
/* Identity of a name for noticing changes: its hash, or 0 for no name. */
static uint64_t HashName(const unsigned char* name)
{
    return (name != NULL) ? Hash_String(name) : 0U;
}

/* An object's name for a log line ("" without one). */
static const char* GetObjectName(const WorldObject* object)
{
    const unsigned char* Name = WorldObject_GetName(object);
    return (Name != NULL) ? (const char*)Name : "";
}

/* Logs (once, when it is resolved) why a screen shows nothing. */
static void Report(const WorldScreens* self, const WorldObject* owner, const char* reason, const unsigned char* what)
{
    if (self->_logger == NULL)
    {
        return;
    }
    Error LogResult = Logger_LogWarningFormatted(self->_logger, (const unsigned char*)u8"WorldScreens: object %" PRIu64
        u8" (\"%s\") cannot be pointed at: %s%s%s.", WorldObject_GetId(owner), GetObjectName(owner), reason,
        (what != NULL) ? " " : "", (what != NULL) ? (const char*)what : "");
    Error_Deconstruct(&LogResult);
}

/* The model component of a screen's object, or NULL. */
static const WorldModelComponent* GetModel(const WorldObject* owner)
{
    return WorldModelComponent_FromComponent(WorldObject_GetComponent(owner, WorldComponentType_Model));
}

/* Resolves an entry for its component as it is now: the world model its object's model component names, then the
 * surface the screen names in it. What fails is logged here, once. */
static void Resolve(WorldScreens* self, WorldScreenEntry* entry)
{
    const WorldObject* Owner = WorldComponent_GetOwner(&entry->Component->Base);
    const WorldModelComponent* ModelComponent = GetModel(Owner);
    const unsigned char* WorldModelName = (ModelComponent != NULL) ? WorldModelComponent_GetWorldModel(ModelComponent)
        : NULL;
    entry->IsResolved = true;
    entry->SurfaceNameHash = HashName(WorldScreenComponent_GetSurface(entry->Component));
    entry->ModelSerial = (ModelComponent != NULL) ? WorldComponent_GetSerial(&ModelComponent->Base) : 0U;
    entry->WorldModelNameHash = HashName(WorldModelName);
    entry->Asset = NULL;
    entry->HasSurface = false;
    entry->SurfaceIndex = 0U;
    if (WorldModelName == NULL)
    {
        Report(self, Owner, "it has no model component naming a world model", NULL);
        return;
    }
    if (self->_assets == NULL)
    {
        return;
    }
    WorldModel* Asset = NULL;
    Error LoadResult = AssetManager_LoadWorldModel(self->_assets, WorldModelName, self->_assetUser, &Asset);
    if (LoadResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&LoadResult);
        Report(self, Owner, "its world model did not load:", WorldModelName);
        return;
    }
    entry->Asset = Asset;
    const unsigned char* SurfaceName = WorldScreenComponent_GetSurface(entry->Component);
    entry->HasSurface = WorldModel_TryFindSurface(Asset, SurfaceName, &entry->SurfaceIndex);
    if (!entry->HasSurface)
    {
        Report(self, Owner, "its world model has no surface named", SurfaceName);
    }
}

/* Whether an entry must be resolved again: never resolved, or the surface its component names, its object's model
 * component or that component's world model changed since. */
static bool IsStale(const WorldScreenEntry* entry)
{
    if (!entry->IsResolved || (HashName(WorldScreenComponent_GetSurface(entry->Component)) != entry->SurfaceNameHash))
    {
        return true;
    }
    const WorldModelComponent* ModelComponent = GetModel(WorldComponent_GetOwner(&entry->Component->Base));
    uint64_t ModelSerial = (ModelComponent != NULL) ? WorldComponent_GetSerial(&ModelComponent->Base) : 0U;
    uint64_t NameHash = HashName((ModelComponent != NULL) ? WorldModelComponent_GetWorldModel(ModelComponent) : NULL);
    return (ModelSerial != entry->ModelSerial) || (NameHash != entry->WorldModelNameHash);
}

/* Matches the entries to the world's screen components, in order: a component keeps its entry when one with its serial
 * survives (one forward scan: the world keeps its components' order when it removes one), otherwise it gets a new,
 * unresolved one. */
static void Rematch(WorldScreens* self, WorldComponent* const* components, size_t count)
{
    bool IsSame = (self->_entries._count == count);
    const WorldScreenEntry* Entries = (const WorldScreenEntry*)(void*)self->_entries._data;
    for (size_t Index = 0U; IsSame && (Index < count); Index++)
    {
        IsSame = (Entries[Index].Serial == WorldComponent_GetSerial(components[Index]));
    }
    if (IsSame)
    {
        return;
    }
    GenericBuffer Swap = self->_previous;
    self->_previous = self->_entries;
    self->_entries = Swap;
    GenericBuffer_Clear(&self->_entries);
    GenericBuffer_EnsureTotalCapacity(&self->_entries, count);
    const WorldScreenEntry* Previous = (const WorldScreenEntry*)(void*)self->_previous._data;
    size_t Cursor = 0U;
    for (size_t Index = 0U; Index < count; Index++)
    {
        uint64_t Serial = WorldComponent_GetSerial(components[Index]);
        WorldScreenEntry Entry = { 0 };
        size_t Scan = Cursor;
        while ((Scan < self->_previous._count) && (Previous[Scan].Serial != Serial))
        {
            Scan++;
        }
        if (Scan < self->_previous._count)
        {
            Entry = Previous[Scan];
            Cursor = Scan + 1U;
        }
        Entry.Component = WorldScreenComponent_FromComponent(components[Index]);
        Entry.Serial = Serial;
        Entry.ObjectId = WorldObject_GetId(WorldComponent_GetOwner(components[Index]));
        GenericBuffer_AddLast(&self->_entries, &Entry);
    }
}

/* The object of an entry's screen as the world holds it now, or NULL when the object or that screen component went
 * away since the last sync (a query between syncs never reads a removed component). */
static WorldObject* FindOwner(const WorldScreens* self, const WorldScreenEntry* entry)
{
    WorldObject* Owner = NULL;
    if (!World_TryGetObject(self->_world, entry->ObjectId, &Owner))
    {
        return NULL;
    }
    const WorldComponent* Screen = WorldObject_GetComponent(Owner, WorldComponentType_Screen);
    return ((Screen != NULL) && (WorldComponent_GetSerial(Screen) == entry->Serial)) ? Owner : NULL;
}

/* The surface an entry's screen shows on, or NULL. */
static const WorldModelSurface* GetSurface(const WorldScreenEntry* entry)
{
    return entry->HasSurface ? WorldModel_GetSurface(entry->Asset, entry->SurfaceIndex) : NULL;
}

/* Moves a screen's power transition by @p seconds toward its power state: a CRT's over its power times, any other's at
 * once (a value that does not change is not written, so the component's revision stays). */
static void AdvancePower(WorldScreenComponent* screen, const WorldModelSurface* surface, float seconds)
{
    const CrtScreenSettings* Crt = ((surface != NULL) && (WorldModelSurface_GetKind(surface) == WorldModelSurfaceKind_Crt))
        ? WorldModelSurface_GetCrt(surface) : NULL;
    float Now = WorldScreenComponent_GetPowerTransition(screen);
    float Next = CrtScreen_AdvancePower(Now, WorldScreenComponent_IsPowered(screen),
        (Crt != NULL) ? Crt->PowerOnSeconds : 0.0f, (Crt != NULL) ? Crt->PowerOffSeconds : 0.0f, seconds);
    if (Next != Now)
    {
        Error Result = WorldScreenComponent_SetPowerTransition(screen, Next);
        Error_Deconstruct(&Result); // Next is within 0..1 by construction
    }
}

/* Whether anything but the screen's own object lies in front of a screen hit: a collider the ray meets before it. */
static bool IsHidden(WorldScreens* self, WorldCollision* collision, Vector3Double origin, Vector3 direction,
    float distance, const WorldCollisionFilter* filter, uint64_t screenObjectId)
{
    float Reach = distance - WORLD_SCREENS_OCCLUSION_TOLERANCE;
    if ((collision == NULL) || !(Reach > 0.0f))
    {
        return false;
    }
    GenericBuffer_Clear(&self->_hits);
    size_t Count = WorldCollision_RaycastAll(collision, origin, direction, Reach, filter, &self->_hits);
    const WorldRayHit* Hits = (const WorldRayHit*)(void*)self->_hits._data;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        if (Hits[Index].Shape.ObjectId != screenObjectId)
        {
            return true;
        }
    }
    return false;
}


// Public functions.
Error WorldScreens_Create(World* world, AssetManager* assets, Logger* logger, WorldScreens** outScreens)
{
    if ((world == NULL) || (outScreens == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldScreens_Create: world and outScreens must not be NULL.");
    }
    *outScreens = NULL;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (assets != NULL)
    {
        Error Result = AssetManager_GetNewUserID(assets, &User);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    WorldScreens* Self = Memory_Allocate(sizeof(WorldScreens));
    Memory_Zero(Self, sizeof(*Self));
    Self->_world = world;
    Self->_assets = assets;
    Self->_assetUser = User;
    Self->_logger = logger;
    GenericBuffer_AllocateVariable(&Self->_entries, INITIAL_ENTRY_CAPACITY, sizeof(WorldScreenEntry));
    GenericBuffer_AllocateVariable(&Self->_previous, INITIAL_ENTRY_CAPACITY, sizeof(WorldScreenEntry));
    GenericBuffer_AllocateVariable(&Self->_hits, INITIAL_HIT_CAPACITY, sizeof(WorldRayHit));
    *outScreens = Self;
    return Error_CreateSuccess();
}

void WorldScreens_Destroy(WorldScreens* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->_entries._data);
    Memory_Free(self->_previous._data);
    Memory_Free(self->_hits._data);
    if ((self->_assets != NULL) && (self->_assetUser != ASSET_USER_ID_INVALID))
    {
        Error Result = AssetManager_RetireUser(self->_assets, self->_assetUser);
        Error_Deconstruct(&Result);
    }
    Memory_Free(self);
}

void WorldScreens_Sync(WorldScreens* self)
{
    size_t Count = 0U;
    WorldComponent* const* Components = World_GetComponents(self->_world, WorldComponentType_Screen, &Count);
    Rematch(self, Components, (Components != NULL) ? Count : 0U);
    WorldScreenEntry* Entries = (WorldScreenEntry*)(void*)self->_entries._data;
    for (size_t Index = 0U; Index < self->_entries._count; Index++)
    {
        WorldScreenEntry* Entry = &Entries[Index];
        bool IsNew = !Entry->IsResolved;
        if (IsStale(Entry))
        {
            Resolve(self, Entry);
        }
        if (IsNew)
        {
            // Met for the first time: settled at its power state (nothing showed it turning).
            AdvancePower(Entry->Component, NULL, 0.0f);
        }
    }
}

void WorldScreens_Advance(WorldScreens* self, float seconds)
{
    const WorldScreenEntry* Entries = (const WorldScreenEntry*)(void*)self->_entries._data;
    for (size_t Index = 0U; Index < self->_entries._count; Index++)
    {
        const WorldScreenEntry* Entry = &Entries[Index];
        WorldObject* Owner = FindOwner(self, Entry);
        if (Owner != NULL)
        {
            AdvancePower(WorldScreenComponent_FromComponent(WorldObject_GetComponent(Owner, WorldComponentType_Screen)),
                GetSurface(Entry), seconds);
        }
    }
}

bool WorldScreens_TryGetScreen(const WorldScreens* self, uint64_t objectId, WorldScreenInfo* outInfo)
{
    const WorldScreenEntry* Entries = (const WorldScreenEntry*)(void*)self->_entries._data;
    for (size_t Index = 0U; Index < self->_entries._count; Index++)
    {
        const WorldScreenEntry* Entry = &Entries[Index];
        if ((Entry->ObjectId == objectId) && Entry->HasSurface && (FindOwner(self, Entry) != NULL))
        {
            *outInfo = (WorldScreenInfo)
            {
                .WorldModel = Entry->Asset, .Surface = WorldModel_GetSurface(Entry->Asset, Entry->SurfaceIndex),
                .SurfaceIndex = Entry->SurfaceIndex
            };
            return true;
        }
    }
    return false;
}

bool WorldScreens_Raycast(WorldScreens* self, Vector3Double origin, Vector3 direction, float maxDistance,
    WorldCollision* collision, const WorldCollisionFilter* filter, WorldScreenHit* outHit)
{
    const WorldScreenEntry* Entries = (const WorldScreenEntry*)(void*)self->_entries._data;
    const WorldScreenEntry* Nearest = NULL;
    WorldModelSurfaceHit NearestHit = { 0 };
    for (size_t Index = 0U; Index < self->_entries._count; Index++)
    {
        const WorldScreenEntry* Entry = &Entries[Index];
        const WorldObject* Owner = Entry->HasSurface ? FindOwner(self, Entry) : NULL;
        if ((Owner == NULL) || !WorldObject_IsEffectivelyEnabled(Owner) || !WorldObject_IsEffectivelyVisible(Owner))
        {
            continue;
        }
        Matrix4Double Placement = WorldObject_GetWorldMatrix(Owner);
        WorldModelSurfaceHit Hit;
        float Reach = (Nearest != NULL) ? NearestHit.Distance : maxDistance;
        if (WorldModelSurface_Raycast(WorldModel_GetSurface(Entry->Asset, Entry->SurfaceIndex), &Placement, origin,
            direction, Reach, &Hit) && ((Nearest == NULL) || (Hit.Distance < NearestHit.Distance)))
        {
            Nearest = Entry;
            NearestHit = Hit;
        }
    }
    if ((Nearest == NULL) || IsHidden(self, collision, origin, direction, NearestHit.Distance, filter, Nearest->ObjectId))
    {
        return false;
    }
    float Length = Vector3Length(direction);
    Vector3Double Unit = Vector3Double_Create((double)direction.x / (double)Length, (double)direction.y / (double)Length,
        (double)direction.z / (double)Length);
    *outHit = (WorldScreenHit)
    {
        .ObjectId = Nearest->ObjectId, .Surface = WorldModel_GetSurface(Nearest->Asset, Nearest->SurfaceIndex),
        .SurfaceIndex = Nearest->SurfaceIndex, .Uv = NearestHit.Uv, .Picture = NearestHit.Picture,
        .IsOnPicture = NearestHit.IsOnPicture,
        .Point = Vector3Double_AddScaled(origin, Unit, (double)NearestHit.Distance), .Distance = NearestHit.Distance
    };
    return true;
}
