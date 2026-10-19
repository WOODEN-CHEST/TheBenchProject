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

/* Whether the renderer draws anything of the object this frame. */
static bool IsObjectShown(const WorldObject* object)
{
    return WorldObject_IsEnabled(object) && WorldObject_IsVisible(object);
}

/* Logs (once, when it happens) that an object's model could not be resolved, and releases the error. */
static void ReportModelFailure(Logger* logger, const WorldModelComponent* model, const unsigned char* assetName,
    Error* error)
{
    if (logger != NULL)
    {
        const WorldObject* Owner = WorldComponent_GetOwner(&model->Base);
        const unsigned char* ObjectName = WorldObject_GetName(Owner);
        Error LogResult = Logger_LogWarningFormatted(logger,
            (const unsigned char*)u8"WorldRenderer: object %" PRIu64 u8" (\"%s\") draws nothing: model \"%s\" could "
            u8"not be loaded (%s).",
            WorldObject_GetId(Owner),
            (ObjectName != NULL) ? (const char*)ObjectName : "",
            (const char*)assetName,
            (error->Message != NULL) ? (const char*)error->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

/* Resolves an entry's model for the component's current name. A component without a model name draws nothing (not an
 * error); a load failure is logged here, once, and not retried until the name changes. */
static void ResolveModel(WorldRenderModel* entry, AssetManager* assets, AssetUserID modelUser, Logger* logger)
{
    const unsigned char* AssetName = WorldModelComponent_GetModel(entry->Component);
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
        ReportModelFailure(logger, entry->Component, AssetName, &LoadResult);
        return;
    }

    BoundingBox Bounds = GameModel_GetLocalBounds(Asset);
    Vector3 Half = Vector3Scale(Vector3Subtract(Bounds.max, Bounds.min), 0.5f);
    entry->LocalCenter = Vector3Scale(Vector3Add(Bounds.min, Bounds.max), 0.5f);
    entry->LocalRadius = Vector3Length(Half);
    entry->Asset = Asset;
}

/* Brings one model entry up to date for this frame: re-resolves its asset if its name changed, then copies the
 * component's settings and recomputes whether it is drawn, its tint, its final transform (the import transform inside
 * the object's scale -> Euler XYZ -> translation, matching raylib's DrawModelEx) and its world bounding sphere. */
static void UpdateModel(WorldRenderModel* entry, AssetManager* assets, AssetUserID modelUser, Logger* logger)
{
    const WorldModelComponent* ModelComponent = entry->Component;
    uint32_t Revision = WorldComponent_GetRevision(&ModelComponent->Base);
    if (!entry->IsResolved
        || ((entry->Revision != Revision)
            && (HashModelName(WorldModelComponent_GetModel(ModelComponent)) != entry->NameHash)))
    {
        ResolveModel(entry, assets, modelUser, logger);
    }
    entry->Revision = Revision;

    const WorldObject* Owner = WorldComponent_GetOwner(&ModelComponent->Base);
    entry->HasOutline = WorldModelComponent_HasOutline(ModelComponent);
    entry->OmitsPixelation = WorldModelComponent_OmitsPixelation(ModelComponent);
    entry->ShadowTier = WorldModelComponent_GetShadowTier(ModelComponent);
    entry->CastsShadows = WorldModelComponent_CastsShadows(ModelComponent);
    entry->Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Owner));
    entry->IsDrawn = (entry->Asset != NULL) && IsObjectShown(Owner);
    if (entry->Asset == NULL)
    {
        return;
    }

    Vector3 Position = WorldObject_GetPosition(Owner);
    Vector3 Rotation = WorldObject_GetRotation(Owner);
    Vector3 Scale = WorldObject_GetScale(Owner);
    Matrix WorldTransform = MatrixMultiply(MatrixMultiply(MatrixScale(Scale.x, Scale.y, Scale.z),
        MatrixRotateXYZ(Rotation)), MatrixTranslate(Position.x, Position.y, Position.z));

    Model RayModel = GameModel_GetRaylibModel(entry->Asset);
    entry->Transform = MatrixMultiply(RayModel.transform, WorldTransform);
    entry->Center = Vector3Transform(entry->LocalCenter, entry->Transform);
    entry->Radius = entry->LocalRadius * MatrixMaxScale(entry->Transform);
}

/* Re-matches the model entries to the world's current model components, in order: a component keeps its entry (and
 * resolved asset) when one with its owner's id exists and was resolved for the model name the component has now;
 * otherwise it gets a new, unresolved one (an id can be reused: removed, then created again with another model).
 * Entries of removed components are dropped. The world appends components and removes them in place, so one forward
 * scan through the previous entries finds every survivor; one found out of order just gets a new entry (and is
 * resolved again, which is cheap while its model is still held). */
static Error RematchModels(WorldRenderCache* self, World* world)
{
    size_t PreviousCount = self->_models._count;
    size_t Cursor = 0;
    GenericBuffer_Clear(&self->_rematch);

    size_t ModelCount = 0;
    WorldComponent* const* Models = World_GetComponents(world, WorldComponentType_Model, &ModelCount);
    for (size_t Index = 0; Index < ModelCount; Index++)
    {
        const WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(Models[Index]);
        uint64_t Id = WorldObject_GetId(WorldComponent_GetOwner(Models[Index]));
        WorldRenderModel Entry = { .ObjectId = Id, .IsResolved = false, .Asset = NULL };
        for (size_t Search = Cursor; Search < PreviousCount; Search++)
        {
            const WorldRenderModel* Previous = GenericBuffer_GetPointerToElement(&self->_models, Search);
            if (Previous->ObjectId == Id)
            {
                uint64_t NameHash = HashModelName(WorldModelComponent_GetModel(ModelComponent));
                if (!Previous->IsResolved || (Previous->NameHash == NameHash))
                {
                    Entry = *Previous;
                }
                Cursor = Search + 1;
                break;
            }
        }
        Entry.Component = ModelComponent;
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

/* Lists a sprite when its object is shown and its playback instance has a frame to draw (the world binds the
 * animation; a sprite without one, or whose animation failed to load, is simply not drawn). */
static Error AddSprite(WorldRenderCache* self, WorldSpriteComponent* sprite)
{
    const WorldObject* Owner = WorldComponent_GetOwner(&sprite->Base);
    SpriteAnimationInstance* Instance = WorldSpriteComponent_GetAnimationInstance(sprite);
    if (!IsObjectShown(Owner) || !SpriteAnimationInstance_HasSource(Instance)
        || (SpriteAnimationInstance_GetFrameCount(Instance) == 0))
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

    Vector3 Scale = WorldObject_GetScale(Owner);
    Entry.Component = sprite;
    Entry.Position = WorldObject_GetPosition(Owner);
    Entry.Size = (Vector2){ Scale.x, Scale.y };
    Entry.Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Owner));
    Entry.HasOutline = WorldSpriteComponent_HasOutline(sprite);
    Entry.OmitsPixelation = WorldSpriteComponent_OmitsPixelation(sprite);
    if (!GenericBuffer_AddLast(&self->_sprites, &Entry))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the sprite list.");
    }
    if (Entry.OmitsPixelation)
    {
        self->_hasCrispObjects = true;
    }
    return Error_CreateSuccess();
}

/* Lists a point light of a shown object with the values the passes read. */
static Error AddLight(WorldRenderCache* self, const WorldLightComponent* light)
{
    const WorldObject* Owner = WorldComponent_GetOwner(&light->Base);
    if (!IsObjectShown(Owner))
    {
        return Error_CreateSuccess();
    }

    WorldRenderLight Entry =
    {
        .Component = light,
        .Position = WorldObject_GetPosition(Owner),
        .Range = WorldLightComponent_GetRange(light),
        .Intensity = WorldLightComponent_GetIntensity(light),
        .Color = WorldLightComponent_GetColor(light),
        .CastsShadows = WorldLightComponent_CastsShadows(light)
    };
    if (!GenericBuffer_AddLast(&self->_lights, &Entry))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the light list.");
    }
    return Error_CreateSuccess();
}

/* Brings every model entry up to date. The n-th model component must own the n-th entry; when an entry belongs to
 * another component or id (or the counts differ) it stops with *outAreEntriesAligned false so the caller re-matches. */
static void UpdateModels(WorldRenderCache* self, World* world, AssetManager* assets, AssetUserID modelUser,
    Logger* logger, bool* outAreEntriesAligned)
{
    *outAreEntriesAligned = false;
    size_t ModelCount = 0;
    WorldComponent* const* Models = World_GetComponents(world, WorldComponentType_Model, &ModelCount);
    if (ModelCount != self->_models._count)
    {
        return;
    }

    for (size_t Index = 0; Index < ModelCount; Index++)
    {
        WorldRenderModel* Entry = GenericBuffer_GetPointerToElement(&self->_models, Index);
        const WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(Models[Index]);
        uint64_t Id = WorldObject_GetId(WorldComponent_GetOwner(Models[Index]));
        if ((Entry->Component != ModelComponent) || (Entry->ObjectId != Id))
        {
            return;
        }
        UpdateModel(Entry, assets, modelUser, logger);
        if (Entry->IsDrawn && Entry->OmitsPixelation)
        {
            self->_hasCrispObjects = true;
        }
    }
    *outAreEntriesAligned = true;
}

/* Rebuilds this frame's sprite and light lists from the world's sprite and light components. */
static Error BuildSpritesAndLights(WorldRenderCache* self, World* world)
{
    size_t SpriteCount = 0;
    WorldComponent* const* Sprites = World_GetComponents(world, WorldComponentType_Sprite, &SpriteCount);
    for (size_t Index = 0; Index < SpriteCount; Index++)
    {
        Error Result = AddSprite(self, WorldSpriteComponent_FromComponent(Sprites[Index]));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    size_t LightCount = 0;
    WorldComponent* const* Lights = World_GetComponents(world, WorldComponentType_Light, &LightCount);
    for (size_t Index = 0; Index < LightCount; Index++)
    {
        Error Result = AddLight(self, WorldLightComponent_FromComponent(Lights[Index]));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
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
    self->_componentSetVersion = 0;
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

    uint64_t Version = World_GetComponentSetVersion(world);
    bool IsMatchCurrent = self->_isMatched && (self->_componentSetVersion == Version);
    bool AreEntriesAligned = false;
    self->_hasCrispObjects = false;
    // A fresh match always lines up with the world, so a second attempt is the most that can be needed: the first
    // only fails when the entries do not follow the world after all (a world rebuilt at the same address).
    for (int Attempt = 0; (Attempt < 2) && !AreEntriesAligned; Attempt++)
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
            self->_componentSetVersion = Version;
            self->_isMatched = true;
        }
        self->_hasCrispObjects = false;
        UpdateModels(self, world, assets, modelUser, logger, &AreEntriesAligned);
        IsMatchCurrent = false;
    }
    if (!AreEntriesAligned)
    {
        WorldRenderCache_Reset(self);
        return Error_Construct2(ErrorCode_InvalidState,
            "WorldRenderCache_Sync: the model entries do not follow the world.");
    }

    GenericBuffer_Clear(&self->_sprites);
    GenericBuffer_Clear(&self->_lights);
    Error BuildResult = BuildSpritesAndLights(self, world);
    if (BuildResult.Code != ErrorCode_Success)
    {
        WorldRenderCache_Reset(self);
        return BuildResult;
    }
    return Error_CreateSuccess();
}
