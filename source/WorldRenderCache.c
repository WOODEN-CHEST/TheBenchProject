#include <inttypes.h>
#include <math.h>
#include "WorldRenderCache.h"
#include "WorldObject.h"
#include "WorldTransform.h"
#include "GameMath.h"
#include "Renderer.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "wr/WRHash.h"


// Macros.
/** Initial capacity (in entries) of each list; they grow with the world. */
#define INITIAL_LIST_CAPACITY 32


// Static functions.
/* Identity of a name for re-matching entries and noticing changes: its hash, or 0 for no name. */
static uint64_t HashName(const unsigned char* name)
{
    return (name != NULL) ? Hash_String(name) : 0;
}

/* Whether the renderer draws anything of the object this frame: it and its ancestors are enabled and visible. */
static bool IsObjectShown(const WorldObject* object)
{
    return WorldObject_IsEffectivelyEnabled(object) && WorldObject_IsEffectivelyVisible(object);
}

/* The object's name for a log line ("" without one). */
static const char* GetOwnerName(const WorldModelComponent* model)
{
    const unsigned char* Name = WorldObject_GetName(WorldComponent_GetOwner(&model->Base));
    return (Name != NULL) ? (const char*)Name : "";
}

/* Logs (once, when it happens) that an object's world model could not be resolved, and releases the error. */
static void ReportModelFailure(Logger* logger, const WorldModelComponent* model, const unsigned char* assetName,
    Error* error)
{
    if (logger != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(logger,
            (const unsigned char*)u8"WorldRenderer: object %" PRIu64 u8" (\"%s\") draws nothing: world model \"%s\" "
            u8"could not be loaded (%s).",
            WorldObject_GetId(WorldComponent_GetOwner(&model->Base)), GetOwnerName(model), (const char*)assetName,
            (error->Message != NULL) ? (const char*)error->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

/* Logs (once, when the variant is resolved) that an object asks for a variant its world model does not have. */
static void ReportVariantFailure(Logger* logger, const WorldModelComponent* model)
{
    if (logger == NULL)
    {
        return;
    }
    const unsigned char* Variant = WorldModelComponent_GetVariant(model);
    Error LogResult = Logger_LogWarningFormatted(logger,
        (const unsigned char*)u8"WorldRenderer: object %" PRIu64 u8" (\"%s\") draws world model \"%s\" with its own "
        u8"materials: it has no variant \"%s\".",
        WorldObject_GetId(WorldComponent_GetOwner(&model->Base)), GetOwnerName(model),
        (const char*)WorldModelComponent_GetWorldModel(model), (Variant != NULL) ? (const char*)Variant : "");
    Error_Deconstruct(&LogResult);
}

/* Resolves an entry's variant for the component's current uses_variant and variant name: the variant's index, or the
 * model's own materials (an unknown name is logged here, once, and not looked up again until it changes). */
static void ResolveVariant(WorldRenderModel* entry, Logger* logger)
{
    entry->UsesVariant = WorldModelComponent_UsesVariant(entry->Component);
    entry->VariantNameHash = HashName(WorldModelComponent_GetVariant(entry->Component));
    entry->HasVariant = false;
    entry->VariantIndex = 0U;
    if (!entry->UsesVariant || (entry->Asset == NULL))
    {
        return;
    }
    size_t Index = 0U;
    if (WorldModel_TryFindVariant(entry->Asset, WorldModelComponent_GetVariant(entry->Component), &Index))
    {
        entry->HasVariant = true;
        entry->VariantIndex = Index;
        return;
    }
    ReportVariantFailure(logger, entry->Component);
}

/* Whether a component asks for another variant than the one an entry resolved. */
static bool IsVariantChanged(const WorldRenderModel* entry, const WorldModelComponent* component)
{
    return (WorldModelComponent_UsesVariant(component) != entry->UsesVariant)
        || (HashName(WorldModelComponent_GetVariant(component)) != entry->VariantNameHash);
}

/* Logs (once, when it starts) that an object's own LOD distances do not fit its world model, whose distances it uses. */
static void ReportLodListRefused(Logger* logger, const WorldRenderModel* entry)
{
    if (logger == NULL)
    {
        return;
    }
    size_t OwnCount = 0U;
    WorldModelComponent_GetLodDistances(entry->Component, &OwnCount);
    Error LogResult = Logger_LogWarningFormatted(logger,
        (const unsigned char*)u8"WorldRenderer: object %" PRIu64 u8" (\"%s\") uses its world model \"%s\"'s LOD "
        u8"distances: its own lod_distances holds %" PRIu64 u8", the world model has %" PRIu64 u8" LODs.",
        entry->ObjectId, GetOwnerName(entry->Component), (const char*)WorldModelComponent_GetWorldModel(entry->Component),
        (uint64_t)OwnCount, (uint64_t)WorldModel_GetLodCount(entry->Asset));
    Error_Deconstruct(&LogResult);
}

/* Resolves an entry's world model for the component's current name, then its variant. A component without a name draws
 * nothing (not an error); a load failure is logged here, once, and not retried until the name changes. The LOD state
 * starts afresh: another world model's LODs say nothing about this one's. */
static void ResolveModel(WorldRenderModel* entry, AssetManager* assets, AssetUserID modelUser, Logger* logger)
{
    const unsigned char* AssetName = WorldModelComponent_GetWorldModel(entry->Component);
    entry->IsResolved = true;
    entry->NameHash = HashName(AssetName);
    entry->Asset = NULL;
    entry->Lod = WorldLodState_Create();
    entry->IsLodListRefused = false;
    if (AssetName != NULL)
    {
        WorldModel* Asset = NULL;
        Error LoadResult = AssetManager_LoadWorldModel(assets, AssetName, modelUser, &Asset);
        if (LoadResult.Code == ErrorCode_Success)
        {
            entry->Asset = Asset;
            entry->LocalCenter = WorldModel_GetBoundsCenter(Asset);
            entry->LocalRadius = WorldModel_GetBoundsRadius(Asset);
            entry->BoundsTransform = WorldModel_GetBoundsTransform(Asset);
        }
        else
        {
            ReportModelFailure(logger, entry->Component, AssetName, &LoadResult);
        }
    }
    ResolveVariant(entry, logger);
}

/* Brings one model entry up to date for this frame: re-resolves its world model if its name changed (or its variant if
 * that changed), then takes the settings in effect (WorldModelComponent_ResolveSettings) and recomputes whether it is
 * drawn, its tint, its world matrix (scale -> Euler XYZ -> translation, as raylib's DrawModelEx composes them, times its
 * parents'; each LOD is drawn with its import transform inside it) and its world bounding sphere (the world model's,
 * through its own transform composed with the world matrix: conservative also when the hierarchy shears it, and exactly
 * LOD 0's sphere when the world model has no bounds of its own), both in double, and both in render space for
 * @p origin. */
static void UpdateModel(WorldRenderModel* entry, Vector3Double origin, AssetManager* assets, AssetUserID modelUser,
    Logger* logger)
{
    const WorldModelComponent* ModelComponent = entry->Component;
    uint32_t Revision = WorldComponent_GetRevision(&ModelComponent->Base);
    bool IsChanged = entry->IsResolved && (entry->Revision != Revision);
    bool IsRenamed = IsChanged && (HashName(WorldModelComponent_GetWorldModel(ModelComponent)) != entry->NameHash);
    if (!entry->IsResolved || IsRenamed)
    {
        ResolveModel(entry, assets, modelUser, logger);
    }
    else if (IsChanged && IsVariantChanged(entry, ModelComponent))
    {
        ResolveVariant(entry, logger);
    }
    entry->Revision = Revision;

    WorldModelSettings Defaults = (entry->Asset != NULL) ? *WorldModel_GetSettings(entry->Asset)
        : WorldModelSettings_CreateDefault();
    WorldModelSettings Settings = WorldModelComponent_ResolveSettings(ModelComponent, &Defaults);
    entry->HasOutline = Settings.HasOutline;
    entry->OmitsPixelation = Settings.OmitsPixelation;
    entry->ShadowTier = Settings.ShadowTier;
    entry->CastsShadows = Settings.CastsShadows;
    entry->DrawDistance = Settings.DrawDistance;
    entry->IsDrawDistanceFixed = Settings.IsDrawDistanceFixed;

    const WorldObject* Owner = WorldComponent_GetOwner(&ModelComponent->Base);
    entry->Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Owner));
    entry->IsDrawn = (entry->Asset != NULL) && IsObjectShown(Owner);
    if (entry->Asset == NULL)
    {
        return;
    }

    // TODO: T33 gives the world double positions; these widen the world's float matrix until then.
    entry->WorldMatrix = GameMath_FromMatrix(WorldObject_GetWorldMatrix(Owner));
    Matrix4Double BoundsTransform = GameMath_FromMatrix(entry->BoundsTransform);
    Matrix4Double BoundsToWorld = Matrix4Double_Multiply(&BoundsTransform, &entry->WorldMatrix);
    entry->Center = Matrix4Double_TransformPoint(&BoundsToWorld, GameMath_FromVector3(entry->LocalCenter));
    // The stretch reads only the matrix's axes, which a float holds as well as anywhere.
    entry->Radius = entry->LocalRadius * WorldTransform_GetMaxScale(GameMath_ToMatrix(&BoundsToWorld));
    entry->RenderMatrix = GameMath_ToMatrixRelative(&entry->WorldMatrix, origin);
    entry->RenderCenter = GameMath_ToVector3Relative(entry->Center, origin);
}

/* Re-matches the model entries to the world's current model components, in order: a component keeps its entry (and
 * resolved asset) when one with its owner's id exists and was resolved for the world model name and the variant the
 * component asks for now; otherwise it gets a new, unresolved one (an id can be reused: removed, then created again
 * with another model or variant). The revision cannot tell that: revisions of two components are not comparable, and a
 * component read from a file or copied often starts at the same one.
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
        WorldRenderModel Entry = { .ObjectId = Id, .IsResolved = false, .Asset = NULL, .Lod = WorldLodState_Create() };
        for (size_t Search = Cursor; Search < PreviousCount; Search++)
        {
            const WorldRenderModel* Previous = GenericBuffer_GetPointerToElement(&self->_models, Search);
            if (Previous->ObjectId == Id)
            {
                uint64_t NameHash = HashName(WorldModelComponent_GetWorldModel(ModelComponent));
                if (!Previous->IsResolved
                    || ((Previous->NameHash == NameHash) && !IsVariantChanged(Previous, ModelComponent)))
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

/* The frame's area in its texture, normalized to [0, 1] (the shader's uvRect); all zero for a texture without a size. */
static void GetUvRect(const SpriteAnimationFrame* frame, float outRect[4])
{
    float Width = (float)frame->_texture.width;
    float Height = (float)frame->_texture.height;
    if ((Width <= 0.0f) || (Height <= 0.0f))
    {
        Memory_Zero(outRect, 4U * sizeof(float));
        return;
    }
    outRect[0] = frame->_areaInTexture.x / Width;
    outRect[1] = frame->_areaInTexture.y / Height;
    outRect[2] = frame->_areaInTexture.width / Width;
    outRect[3] = frame->_areaInTexture.height / Height;
}

/* The sprite's quad before a viewer turns it, in render space: the object's world position relative to @p origin (its
 * world position goes to @p outPosition, in double) and its rotation axes, the world size from the frame's pixels, the
 * pixels per unit and the object's world scale, the pivot and the flips. */
static SpriteShape GetShape(const WorldSpriteSettings* settings, const WorldObject* owner, Rectangle frameArea,
    Vector3Double origin, Vector3Double* outPosition)
{
    Vector3 Position;
    Vector3 Rotation;
    Vector3 Scale;
    WorldObject_GetWorldTransform(owner, &Position, &Rotation, &Scale);
    // TODO: T33 gives the world double positions; this widens the world's float position until then.
    *outPosition = GameMath_FromVector3(Position);
    Matrix Axes = MatrixRotateXYZ(Rotation);
    return (SpriteShape)
    {
        .Orientation = settings->Orientation,
        .Position = GameMath_ToVector3Relative(*outPosition, origin),
        .AxisX = { Axes.m0, Axes.m1, Axes.m2 },
        .AxisY = { Axes.m4, Axes.m5, Axes.m6 },
        .AxisZ = { Axes.m8, Axes.m9, Axes.m10 },
        .Size = SpriteTransform_GetWorldSize(frameArea, settings->PixelsPerUnit, Scale),
        .Pivot = settings->Pivot,
        .IsFlippedX = settings->IsFlippedX,
        .IsFlippedY = settings->IsFlippedY
    };
}

/* Lists a sprite when its object is shown and its playback instance has a frame to draw (the world binds its world
 * sprite; a sprite without one, or whose world sprite failed to load, is simply not drawn), with its quad in render space
 * for @p origin, its frame's place in the texture and the settings in effect the passes read. */
static Error AddSprite(WorldRenderCache* self, WorldSpriteComponent* sprite, Vector3Double origin)
{
    const WorldObject* Owner = WorldComponent_GetOwner(&sprite->Base);
    const WorldSprite* Asset = WorldSpriteComponent_GetBoundWorldSprite(sprite);
    SpriteAnimationInstance* Instance = WorldSpriteComponent_GetAnimationInstance(sprite);
    if (!IsObjectShown(Owner) || (Asset == NULL) || !SpriteAnimationInstance_HasSource(Instance)
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

    WorldSpriteSettings Settings = WorldSpriteComponent_ResolveSettings(sprite, WorldSprite_GetSettings(Asset));
    Entry.Component = sprite;
    GetUvRect(&Entry.Frame, Entry.UvRect);
    Entry.Shape = GetShape(&Settings, Owner, Entry.Frame._areaInTexture, origin, &Entry.Position);
    // A fixed quad does not depend on its viewer.
    SpriteViewer AnyViewer = SpriteViewer_FromPoint(Entry.Shape.Position, (Vector3){ 0.0f, 1.0f, 0.0f });
    Entry.FixedTransform = (Entry.Shape.Orientation == WorldSpriteOrientation_Fixed)
        ? SpriteTransform_GetMatrix(&Entry.Shape, &AnyViewer) : MatrixIdentity();
    Entry.BoundsRadius = SpriteTransform_GetBoundingRadius(Entry.Shape.Size, Entry.Shape.Pivot);
    Entry.Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Owner));
    Entry.AlphaMode = Settings.AlphaMode;
    Entry.AlphaCutoff = Settings.AlphaCutoff;
    Entry.IsLit = Settings.IsLit;
    Entry.Emission = Settings.Emission;
    Entry.LightWrap = Settings.LightWrap;
    Entry.IsDecal = WorldSpriteSettings_IsDrawnAsDecal(&Settings);
    Entry.DecalOffset = Settings.DecalOffset;
    Entry.CastsShadows = Settings.Render.CastsShadows;
    Entry.HasOutline = Settings.Render.HasOutline;
    Entry.OmitsPixelation = Settings.Render.OmitsPixelation && (Entry.AlphaMode == WorldSpriteAlphaMode_Cutout)
        && !Entry.IsDecal;
    Entry.DrawDistance = Settings.DrawDistance;
    Entry.IsDrawDistanceFixed = Settings.IsDrawDistanceFixed;
    if (!GenericBuffer_AddLast(&self->_sprites, &Entry))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the sprite list.");
    }
    return Error_CreateSuccess();
}

/* Lists a point light of a shown object with the values the passes read (it sits at the object's world position, also
 * kept in render space for @p origin). */
static Error AddLight(WorldRenderCache* self, const WorldLightComponent* light, Vector3Double origin)
{
    const WorldObject* Owner = WorldComponent_GetOwner(&light->Base);
    if (!IsObjectShown(Owner))
    {
        return Error_CreateSuccess();
    }

    // TODO: T33 gives the world double positions; this widens the world's float position until then.
    Vector3Double Position = GameMath_FromVector3(WorldObject_GetWorldPosition(Owner));
    WorldRenderLight Entry =
    {
        .Component = light,
        .Position = Position,
        .RenderPosition = GameMath_ToVector3Relative(Position, origin),
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

/* Brings every model entry up to date (in render space for @p origin). The n-th model component must own the n-th
 * entry; when an entry belongs to another component or id (or the counts differ) it stops with *outAreEntriesAligned
 * false so the caller re-matches. */
static void UpdateModels(WorldRenderCache* self, World* world, Vector3Double origin, AssetManager* assets,
    AssetUserID modelUser, Logger* logger, bool* outAreEntriesAligned)
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
        UpdateModel(Entry, origin, assets, modelUser, logger);
    }
    *outAreEntriesAligned = true;
}

/* One drawn entry's LOD this frame: its LOD settings in effect (a placement's own list that does not fit its world model
 * is warned about once, when that starts), the LOD for the camera's zoomed distance to its sphere's centre, chosen from
 * the LOD it heads to, then the cross-fade moved on. */
static void UpdateLod(WorldRenderModel* entry, const WorldRenderLodView* view, float zoomFactor, Logger* logger)
{
    WorldModelLodSettings Settings;
    bool IsAsAsked = WorldModelComponent_ResolveLodSettings(entry->Component, entry->Asset, &Settings);
    if (!IsAsAsked && !entry->IsLodListRefused)
    {
        ReportLodListRefused(logger, entry);
    }
    entry->IsLodListRefused = !IsAsAsked;

    // A state from a world model with more LODs (cannot happen while the cache resolves again on a rename) starts afresh.
    if ((entry->Lod.CurrentLod >= Settings.LodCount) || (entry->Lod.TargetLod >= Settings.LodCount))
    {
        entry->Lod = WorldLodState_Create();
    }
    float Distance = Vector3Distance(view->CameraPosition, entry->RenderCenter) * zoomFactor;
    size_t Lod = WorldLod_Select(&Settings, Distance, view->DistanceScale, entry->Lod.IsStarted,
        WorldLodState_GetDestinationLod(&entry->Lod));
    WorldLodState_Update(&entry->Lod, Lod, view->DeltaSeconds, Settings.FadeSeconds);
}

/* Rebuilds this frame's sprite and light lists from the world's sprite and light components, in render space for
 * @p origin. */
static Error BuildSpritesAndLights(WorldRenderCache* self, World* world, Vector3Double origin)
{
    size_t SpriteCount = 0;
    WorldComponent* const* Sprites = World_GetComponents(world, WorldComponentType_Sprite, &SpriteCount);
    for (size_t Index = 0; Index < SpriteCount; Index++)
    {
        Error Result = AddSprite(self, WorldSpriteComponent_FromComponent(Sprites[Index]), origin);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    size_t LightCount = 0;
    WorldComponent* const* Lights = World_GetComponents(world, WorldComponentType_Light, &LightCount);
    for (size_t Index = 0; Index < LightCount; Index++)
    {
        Error Result = AddLight(self, WorldLightComponent_FromComponent(Lights[Index]), origin);
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
}

Error WorldRenderCache_Sync(WorldRenderCache* self, World* world, Vector3Double origin, AssetManager* assets,
    AssetUserID modelUser, Logger* logger)
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
        UpdateModels(self, world, origin, assets, modelUser, logger, &AreEntriesAligned);
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
    Error BuildResult = BuildSpritesAndLights(self, world, origin);
    if (BuildResult.Code != ErrorCode_Success)
    {
        WorldRenderCache_Reset(self);
        return BuildResult;
    }
    return Error_CreateSuccess();
}

void WorldRenderCache_UpdateLods(WorldRenderCache* self, const WorldRenderLodView* view, Logger* logger)
{
    float ZoomFactor = (isfinite(view->ZoomFactor) && (view->ZoomFactor > 0.0f)) ? view->ZoomFactor : 1.0f;
    for (size_t Index = 0; Index < self->_models._count; Index++)
    {
        WorldRenderModel* Entry = GenericBuffer_GetPointerToElement(&self->_models, Index);
        if (!Entry->IsDrawn)
        {
            // When it is drawn again it appears at its LOD at once, not fading from the one it had.
            Entry->Lod = WorldLodState_Create();
            continue;
        }
        UpdateLod(Entry, view, ZoomFactor, logger);
    }
}

Matrix WorldRenderModel_GetLodTransform(const WorldRenderModel* model, size_t lod)
{
    GameModel* LodModel = WorldRenderModel_GetLodModel(model, lod);
    return (LodModel != NULL) ? MatrixMultiply(GameModel_GetRaylibModel(LodModel).transform, model->RenderMatrix)
        : model->RenderMatrix;
}
