#include <inttypes.h>
#include <math.h>
#include "WorldRenderCache.h"
#include "WorldObject.h"
#include "Renderer.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "wr/WRHash.h"


// Macros.
/** Initial capacity (in entries) of each list; they grow with the world. */
#define INITIAL_LIST_CAPACITY 32


// Static functions.
/* Largest axis scale factor of a transform (longest of its three basis-vector columns), for conservatively
 * scaling a local bounding-sphere radius into world space under non-uniform scale + rotation. */
static float MatrixMaxScale(Matrix m)
{
    float ScaleX = Vector3Length((Vector3){ m.m0, m.m1, m.m2 });
    float ScaleY = Vector3Length((Vector3){ m.m4, m.m5, m.m6 });
    float ScaleZ = Vector3Length((Vector3){ m.m8, m.m9, m.m10 });
    return fmaxf(ScaleX, fmaxf(ScaleY, ScaleZ));
}

/* Identity of a model name for re-matching entries: its hash, or 0 for no name. */
static uint64_t HashModelName(const unsigned char* name)
{
    return (name != NULL) ? Hash_String(name) : 0;
}

/* Logs (once, when it happens) that an object's model could not be resolved, and releases the error. */
static void ReportModelFailure(Logger* logger, WorldModelObject* object, const unsigned char* assetName, Error* error)
{
    if (logger != NULL)
    {
        WorldObject* Base = WorldModelObject_AsObject(object);
        const unsigned char* ObjectName = WorldObject_GetName(Base);
        Error LogResult = Logger_LogWarningFormatted(logger,
            (const unsigned char*)u8"WorldRenderer: model object %" PRIu64 u8" (\"%s\") draws nothing: model \"%s\" "
            u8"could not be loaded (%s).",
            WorldObject_GetId(Base),
            (ObjectName != NULL) ? (const char*)ObjectName : "",
            (const char*)assetName,
            (error->Message != NULL) ? (const char*)error->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

/* Resolves an entry's model for the object's current asset revision. An object without an asset name draws
 * nothing (not an error); a load failure is logged here, once, and not retried until the revision changes. */
static void ResolveModel(WorldRenderModel* entry, uint32_t revision, AssetManager* assets, AssetUserID modelUser,
    Logger* logger)
{
    const unsigned char* AssetName = WorldModelObject_GetModelAssetName(entry->Object);
    entry->AssetRevision = revision;
    entry->IsResolved = true;
    entry->NameHash = HashModelName(AssetName);
    entry->Asset = NULL;
    if (AssetName == NULL)
    {
        return;
    }

    GameModel* Asset = NULL;
    Error LoadResult = AssetManager_LoadModel(assets, AssetName, modelUser, &Asset);
    if (LoadResult.Code != ErrorCode_Success)
    {
        ReportModelFailure(logger, entry->Object, AssetName, &LoadResult);
        return;
    }

    BoundingBox Bounds = GameModel_GetLocalBounds(Asset);
    Vector3 Half = Vector3Scale(Vector3Subtract(Bounds.max, Bounds.min), 0.5f);
    entry->LocalCenter = Vector3Scale(Vector3Add(Bounds.min, Bounds.max), 0.5f);
    entry->LocalRadius = Vector3Length(Half);
    entry->Asset = Asset;
}

/* Brings one model entry up to date for this frame: re-resolves its asset if needed, then recomputes its tint,
 * final transform (the import transform inside the object's scale -> Euler XYZ -> translation, matching raylib's
 * DrawModelEx) and world bounding sphere. */
static void UpdateModel(WorldRenderModel* entry, AssetManager* assets, AssetUserID modelUser, Logger* logger)
{
    uint32_t Revision = WorldModelObject_GetAssetRevision(entry->Object);
    if (!entry->IsResolved || (entry->AssetRevision != Revision))
    {
        ResolveModel(entry, Revision, assets, modelUser, logger);
    }

    WorldObject* Base = WorldModelObject_AsObject(entry->Object);
    entry->Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Base));
    if (entry->Asset == NULL)
    {
        return;
    }

    Vector3 Position = WorldObject_GetPosition(Base);
    Vector3 Rotation = WorldObject_GetRotation(Base);
    Vector3 Scale = WorldObject_GetScale(Base);
    Matrix WorldTransform = MatrixMultiply(MatrixMultiply(MatrixScale(Scale.x, Scale.y, Scale.z),
        MatrixRotateXYZ(Rotation)), MatrixTranslate(Position.x, Position.y, Position.z));

    Model RayModel = GameModel_GetRaylibModel(entry->Asset);
    entry->Transform = MatrixMultiply(RayModel.transform, WorldTransform);
    entry->Center = Vector3Transform(entry->LocalCenter, entry->Transform);
    entry->Radius = entry->LocalRadius * MatrixMaxScale(entry->Transform);
}

/* Re-matches the model entries to the world's current model objects, in world order: an object keeps its entry
 * (and resolved asset) when one with its id exists and was resolved for the model name the object has now;
 * otherwise it gets a new, unresolved one (an id can be reused: removed, then added again with another model).
 * Entries of removed objects are dropped. The world only appends objects and removes them in place, so one
 * forward scan through the previous entries finds every survivor; an object found out of order just gets a new
 * entry (and is resolved again, which is cheap while its model is still held). */
static Error RematchModels(WorldRenderCache* self, World* world)
{
    size_t PreviousCount = self->_models._count;
    size_t Cursor = 0;
    GenericBuffer_Clear(&self->_rematch);

    size_t ObjectCount = World_GetObjectCount(world);
    for (size_t Index = 0; Index < ObjectCount; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            return GetResult;
        }
        if (WorldObject_GetType(Object) != WorldObjectType_Model)
        {
            continue;
        }

        uint64_t Id = WorldObject_GetId(Object);
        WorldRenderModel Entry = { .ObjectId = Id, .IsResolved = false, .Asset = NULL };
        for (size_t Search = Cursor; Search < PreviousCount; Search++)
        {
            const WorldRenderModel* Previous = GenericBuffer_GetPointerToElement(&self->_models, Search);
            if (Previous->ObjectId == Id)
            {
                uint64_t NameHash = HashModelName(WorldModelObject_GetModelAssetName((WorldModelObject*)Object));
                if (!Previous->IsResolved || (Previous->NameHash == NameHash))
                {
                    Entry = *Previous;
                }
                Cursor = Search + 1;
                break;
            }
        }
        Entry.Object = (WorldModelObject*)Object;
        if (!GenericBuffer_AddLast(&self->_rematch, &Entry))
        {
            return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the model list.");
        }
    }

    GenericBuffer_Clear(&self->_models);
    if ((self->_rematch._count > 0)
        && !GenericBuffer_AddLastRange(&self->_models, self->_rematch._data, self->_rematch._count))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the model list.");
    }
    return Error_CreateSuccess();
}

/* Lists a sprite when its playback instance has a frame to draw (the world binds the animation; a sprite without
 * one, or whose animation failed to load, is simply not drawn). */
static Error AddSprite(WorldRenderCache* self, WorldSpriteObject* sprite)
{
    SpriteAnimationInstance* Instance = WorldSpriteObject_GetAnimationInstance(sprite);
    if (!SpriteAnimationInstance_HasSource(Instance) || (SpriteAnimationInstance_GetFrameCount(Instance) == 0))
    {
        return Error_CreateSuccess();
    }

    WorldRenderSprite Entry;
    Error FrameResult = SpriteAnimationInstance_GetCurrentFrame(Instance, &Entry.Frame);
    if (FrameResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&FrameResult);
        return Error_CreateSuccess();
    }

    WorldObject* Base = WorldSpriteObject_AsObject(sprite);
    Vector3 Scale = WorldObject_GetScale(Base);
    Entry.Object = sprite;
    Entry.Position = WorldObject_GetPosition(Base);
    Entry.Size = (Vector2){ Scale.x, Scale.y };
    Entry.Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Base));
    if (!GenericBuffer_AddLast(&self->_sprites, &Entry))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the sprite list.");
    }
    if (sprite->OmitPixelation)
    {
        self->_hasCrispObjects = true;
    }
    return Error_CreateSuccess();
}

/* Lists a point light with the values the passes read. */
static Error AddLight(WorldRenderCache* self, WorldLight* light)
{
    WorldRenderLight Entry =
    {
        .Light = light,
        .Position = WorldObject_GetPosition(WorldLight_AsObject(light)),
        .Range = WorldLight_GetSize(light),
        .Intensity = WorldLight_GetIntensity(light),
        .Color = light->Color,
        .CastsShadows = light->CastsShadows
    };
    if (!GenericBuffer_AddLast(&self->_lights, &Entry))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the light list.");
    }
    return Error_CreateSuccess();
}

/* Walks the world once: brings every model entry up to date and rebuilds the sprite and light lists. The n-th
 * model object must own the n-th entry; when an entry belongs to another id or object (or the counts differ) the
 * walk stops with *outAreEntriesAligned false so the caller re-matches. */
static Error BuildLists(WorldRenderCache* self, World* world, AssetManager* assets, AssetUserID modelUser,
    Logger* logger, bool* outAreEntriesAligned)
{
    *outAreEntriesAligned = false;
    GenericBuffer_Clear(&self->_sprites);
    GenericBuffer_Clear(&self->_lights);
    self->_hasCrispObjects = false;

    size_t ModelIndex = 0;
    size_t ObjectCount = World_GetObjectCount(world);
    for (size_t Index = 0; Index < ObjectCount; Index++)
    {
        WorldObject* Object = NULL;
        Error Result = World_GetObjectByIndex(world, Index, &Object);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        switch (WorldObject_GetType(Object))
        {
            case WorldObjectType_Model:
            {
                if (ModelIndex >= self->_models._count)
                {
                    return Error_CreateSuccess();
                }
                WorldRenderModel* Entry = GenericBuffer_GetPointerToElement(&self->_models, ModelIndex);
                if ((Entry->ObjectId != WorldObject_GetId(Object)) || (Entry->Object != (WorldModelObject*)Object))
                {
                    return Error_CreateSuccess();
                }
                ModelIndex++;
                UpdateModel(Entry, assets, modelUser, logger);
                if ((Entry->Asset != NULL) && Entry->Object->OmitPixelation)
                {
                    self->_hasCrispObjects = true;
                }
                break;
            }

            case WorldObjectType_Sprite:
                Result = AddSprite(self, (WorldSpriteObject*)Object);
                break;

            case WorldObjectType_Light:
                Result = AddLight(self, (WorldLight*)Object);
                break;
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    *outAreEntriesAligned = (ModelIndex == self->_models._count);
    return Error_CreateSuccess();
}


// Internal functions (declared in WorldRenderCache.h).
void WorldRenderCache_Construct(WorldRenderCache* self)
{
    GenericBuffer_AllocateVariable(&self->_models, INITIAL_LIST_CAPACITY, sizeof(WorldRenderModel));
    GenericBuffer_AllocateVariable(&self->_rematch, INITIAL_LIST_CAPACITY, sizeof(WorldRenderModel));
    GenericBuffer_AllocateVariable(&self->_sprites, INITIAL_LIST_CAPACITY, sizeof(WorldRenderSprite));
    GenericBuffer_AllocateVariable(&self->_lights, INITIAL_LIST_CAPACITY, sizeof(WorldRenderLight));
    self->_world = NULL;
    self->_objectSetVersion = 0;
    self->_isMatched = false;
    self->_hasCrispObjects = false;
}

void WorldRenderCache_Deconstruct(WorldRenderCache* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->_models._data);
    Memory_Free(self->_rematch._data);
    Memory_Free(self->_sprites._data);
    Memory_Free(self->_lights._data);
}

void WorldRenderCache_Reset(WorldRenderCache* self)
{
    GenericBuffer_Clear(&self->_models);
    GenericBuffer_Clear(&self->_sprites);
    GenericBuffer_Clear(&self->_lights);
    self->_world = NULL;
    self->_isMatched = false;
    self->_hasCrispObjects = false;
}

Error WorldRenderCache_Sync(WorldRenderCache* self, World* world, AssetManager* assets, AssetUserID modelUser,
    Logger* logger)
{
    if ((self == NULL) || (world == NULL) || (assets == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldRenderCache_Sync: self, world and assets must not be NULL.");
    }

    // Entries of another world say nothing about this one (its ids and revisions start over), so start fresh.
    if (self->_isMatched && (self->_world != world))
    {
        WorldRenderCache_Reset(self);
    }

    uint64_t Version = World_GetObjectSetVersion(world);
    bool IsMatchCurrent = self->_isMatched && (self->_objectSetVersion == Version);
    // A fresh match always lines up with the world, so a second attempt is the most that can be needed: the first
    // only fails when the entries do not follow the world after all (a world rebuilt at the same address).
    for (int Attempt = 0; Attempt < 2; Attempt++)
    {
        if (!IsMatchCurrent)
        {
            Error RematchResult = RematchModels(self, world);
            if (RematchResult.Code != ErrorCode_Success)
            {
                WorldRenderCache_Reset(self);
                return RematchResult;
            }
            self->_world = world;
            self->_objectSetVersion = Version;
            self->_isMatched = true;
        }

        bool AreEntriesAligned = false;
        Error BuildResult = BuildLists(self, world, assets, modelUser, logger, &AreEntriesAligned);
        if (BuildResult.Code != ErrorCode_Success)
        {
            WorldRenderCache_Reset(self);
            return BuildResult;
        }
        if (AreEntriesAligned)
        {
            return Error_CreateSuccess();
        }
        IsMatchCurrent = false;
    }

    WorldRenderCache_Reset(self);
    return Error_Construct2(ErrorCode_InvalidState,
        "WorldRenderCache_Sync: the model entries do not follow the world.");
}
