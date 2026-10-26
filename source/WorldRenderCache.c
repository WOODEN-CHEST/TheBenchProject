#include <inttypes.h>
#include <math.h>
#include "WorldRenderCache.h"
#include "WorldObject.h"
#include "WorldTransform.h"
#include "GameSkeleton.h"
#include "SkeletalAnimation.h"
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

/* An object's name for a log line ("" without one). */
static const char* GetObjectName(const WorldObject* object)
{
    const unsigned char* Name = WorldObject_GetName(object);
    return (Name != NULL) ? (const char*)Name : "";
}

/* Logs (once, when it happens) that an object's world model could not be resolved, and releases the error. @p consequence
 * says what the object then lacks ("draws nothing", "draws no instances"). */
static void ReportWorldModelFailure(Logger* logger, const WorldObject* owner, const char* consequence,
    const unsigned char* assetName, Error* error)
{
    if (logger != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(logger,
            (const unsigned char*)u8"WorldRenderer: object %" PRIu64 u8" (\"%s\") %s: world model \"%s\" could not be "
            u8"loaded (%s).", WorldObject_GetId(owner), GetObjectName(owner), consequence, (const char*)assetName,
            (error->Message != NULL) ? (const char*)error->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

/* Logs (once, when the variant is resolved) that an object asks for a variant its world model does not have. @p what
 * names what it draws ("world model", "the instances of world model"). */
static void ReportVariantFailure(Logger* logger, const WorldObject* owner, const char* what,
    const unsigned char* worldModel, const unsigned char* variant)
{
    if (logger == NULL)
    {
        return;
    }
    Error LogResult = Logger_LogWarningFormatted(logger,
        (const unsigned char*)u8"WorldRenderer: object %" PRIu64 u8" (\"%s\") draws %s \"%s\" with its own "
        u8"materials: it has no variant \"%s\".", WorldObject_GetId(owner), GetObjectName(owner), what,
        (const char*)worldModel, (variant != NULL) ? (const char*)variant : "");
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
    ReportVariantFailure(logger, WorldComponent_GetOwner(&entry->Component->Base), "world model",
        WorldModelComponent_GetWorldModel(entry->Component), WorldModelComponent_GetVariant(entry->Component));
}

/* Whether a component asks for another variant than the one an entry resolved. */
static bool IsVariantChanged(const WorldRenderModel* entry, const WorldModelComponent* component)
{
    return (WorldModelComponent_UsesVariant(component) != entry->UsesVariant)
        || (HashName(WorldModelComponent_GetVariant(component)) != entry->VariantNameHash);
}

/* Logs (once, when it starts) that an object's own LOD distances do not fit its world model, whose distances it uses. */
static void ReportLodListRefused(Logger* logger, const WorldObject* owner, const unsigned char* worldModelName,
    size_t ownCount, const WorldModel* worldModel)
{
    if (logger == NULL)
    {
        return;
    }
    Error LogResult = Logger_LogWarningFormatted(logger,
        (const unsigned char*)u8"WorldRenderer: object %" PRIu64 u8" (\"%s\") uses its world model \"%s\"'s LOD "
        u8"distances: its own lod_distances holds %" PRIu64 u8", the world model has %" PRIu64 u8" LODs.",
        WorldObject_GetId(owner), GetObjectName(owner), (const char*)worldModelName, (uint64_t)ownCount,
        (uint64_t)WorldModel_GetLodCount(worldModel));
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
            ReportWorldModelFailure(logger, WorldComponent_GetOwner(&entry->Component->Base), "draws nothing", AssetName,
                &LoadResult);
        }
    }
    ResolveVariant(entry, logger);
}

/* The pose of the object's animator when it was made for @p asset (T29), or NULL. */
static const WorldAnimatorPose* GetPose(const WorldObject* owner, const WorldModel* asset)
{
    const WorldAnimatorComponent* Animator = WorldAnimatorComponent_FromComponent(WorldObject_GetComponent(owner,
        WorldComponentType_Animator));
    const WorldAnimatorPose* Pose = (Animator != NULL) ? WorldAnimatorComponent_GetPose(Animator) : NULL;
    return ((Pose != NULL) && (Pose->_worldModel == asset)) ? Pose : NULL;
}

/* Brings one model entry up to date for this frame: re-resolves its world model if its name changed (or its variant if
 * that changed), then takes the settings in effect (WorldModelComponent_ResolveSettings) and recomputes whether it is
 * drawn, its tint, its world matrix (scale -> Euler XYZ -> translation, as raylib's DrawModelEx composes them, times its
 * parents'; each LOD is drawn with its import transform inside it), its pose (T29) and its world bounding sphere (the
 * world model's, or its pose's box's when it has a pose and the world model no bounds of its own, through its own
 * transform composed with the world matrix: conservative also when the hierarchy shears it, and exactly LOD 0's sphere
 * when the world model has no bounds of its own and the object no pose), both in double, and both in render space for
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
    entry->Haze = Settings.Haze;
    entry->DrawDistance = Settings.DrawDistance;
    entry->IsDrawDistanceFixed = Settings.IsDrawDistanceFixed;

    const WorldObject* Owner = WorldComponent_GetOwner(&ModelComponent->Base);
    entry->Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Owner));
    entry->IsDrawn = (entry->Asset != NULL) && IsObjectShown(Owner);
    entry->Pose = NULL;
    entry->CameraPose = NULL;
    if (entry->Asset == NULL)
    {
        return;
    }

    entry->Pose = GetPose(Owner, entry->Asset);
    entry->CameraPose = entry->Pose;
    Vector3 LocalCenter = entry->LocalCenter;
    float LocalRadius = entry->LocalRadius;
    Matrix LocalTransform = entry->BoundsTransform;
    if ((entry->Pose != NULL) && entry->Pose->_hasBounds && !WorldModel_HasOwnBounds(entry->Asset))
    {
        // The pose's box, in the skeleton's mesh space: the skeleton LOD's import transform takes it to the object's.
        BoundingBox Box = entry->Pose->_bounds;
        LocalCenter = Vector3Scale(Vector3Add(Box.min, Box.max), 0.5f);
        LocalRadius = Vector3Length(Vector3Scale(Vector3Subtract(Box.max, Box.min), 0.5f));
        LocalTransform = GameModel_GetRaylibModel(WorldModel_GetLodModel(entry->Asset,
            WorldModel_GetSkeletonLod(entry->Asset))).transform;
    }

    entry->WorldMatrix = WorldObject_GetWorldMatrix(Owner);
    Matrix4Double BoundsTransform = GameMath_FromMatrix(LocalTransform);
    Matrix4Double BoundsToWorld = Matrix4Double_Multiply(&BoundsTransform, &entry->WorldMatrix);
    entry->Center = Matrix4Double_TransformPoint(&BoundsToWorld, GameMath_FromVector3(LocalCenter));
    entry->Radius = (float)((double)LocalRadius * WorldTransform_GetMaxScale(&BoundsToWorld));
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
    Vector3 Rotation;
    Vector3 Scale;
    WorldObject_GetWorldTransform(owner, outPosition, &Rotation, &Scale);
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
    Entry.Haze = Settings.Render.Haze;
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

    Vector3Double Position = WorldObject_GetWorldPosition(Owner);
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
        size_t OwnCount = 0U;
        WorldModelComponent_GetLodDistances(entry->Component, &OwnCount);
        ReportLodListRefused(logger, WorldComponent_GetOwner(&entry->Component->Base),
            WorldModelComponent_GetWorldModel(entry->Component), OwnCount, entry->Asset);
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


/* Logs (once, when it happens) that an object's terrain could not be resolved, and releases the error. */
static void ReportTerrainFailure(Logger* logger, const WorldTerrainComponent* terrain, const unsigned char* assetName,
    Error* error)
{
    if (logger != NULL)
    {
        const WorldObject* Owner = WorldComponent_GetOwner(&terrain->Base);
        const unsigned char* Name = WorldObject_GetName(Owner);
        Error LogResult = Logger_LogWarningFormatted(logger,
            (const unsigned char*)u8"WorldRenderer: object %" PRIu64 u8" (\"%s\") draws no terrain: terrain \"%s\" could "
            u8"not be loaded (%s).", WorldObject_GetId(Owner), (Name != NULL) ? (const char*)Name : "",
            (const char*)assetName, (error->Message != NULL) ? (const char*)error->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

/* Resolves a terrain entry for the component's current name. A component without a name draws nothing (not an error); a
 * load failure is logged here, once, and not retried until the name changes. */
static void ResolveTerrain(WorldRenderTerrain* entry, AssetManager* assets, AssetUserID modelUser, Logger* logger)
{
    const unsigned char* AssetName = WorldTerrainComponent_GetTerrain(entry->Component);
    entry->IsResolved = true;
    entry->NameHash = HashName(AssetName);
    entry->Asset = NULL;
    if (AssetName == NULL)
    {
        return;
    }
    TerrainAsset* Asset = NULL;
    Error LoadResult = AssetManager_LoadTerrain(assets, AssetName, modelUser, &Asset);
    if (LoadResult.Code == ErrorCode_Success)
    {
        entry->Asset = Asset;
        return;
    }
    ReportTerrainFailure(logger, entry->Component, AssetName, &LoadResult);
}

/* Re-matches the terrain entries to the world's current terrain components, in order (as RematchModels): a component
 * keeps its entry (and resolved terrain) when one with its owner's id and its serial exists; otherwise it gets a new,
 * unresolved one. Entries of removed components are dropped. */
static Error RematchTerrains(WorldRenderCache* self, World* world)
{
    size_t PreviousCount = self->_terrains._count;
    size_t Cursor = 0;
    GenericBuffer_Clear(&self->_terrainRematch);
    size_t TerrainCount = 0;
    WorldComponent* const* Terrains = World_GetComponents(world, WorldComponentType_Terrain, &TerrainCount);
    for (size_t Index = 0; Index < TerrainCount; Index++)
    {
        uint64_t Id = WorldObject_GetId(WorldComponent_GetOwner(Terrains[Index]));
        uint64_t Serial = WorldComponent_GetSerial(Terrains[Index]);
        WorldRenderTerrain Entry = { .ObjectId = Id, .Serial = Serial, .IsResolved = false, .Asset = NULL };
        for (size_t Search = Cursor; Search < PreviousCount; Search++)
        {
            const WorldRenderTerrain* Previous = GenericBuffer_GetPointerToElement(&self->_terrains, Search);
            if (Previous->ObjectId == Id)
            {
                if (Previous->Serial == Serial)
                {
                    Entry = *Previous;
                }
                Cursor = Search + 1;
                break;
            }
        }
        Entry.Component = WorldTerrainComponent_FromComponent(Terrains[Index]);
        if (!GenericBuffer_AddLast(&self->_terrainRematch, &Entry))
        {
            return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the terrain list.");
        }
    }
    GenericBuffer_Clear(&self->_terrains);
    if ((self->_terrainRematch._count > 0)
        && !GenericBuffer_AddLastRange(&self->_terrains, self->_terrainRematch._data, self->_terrainRematch._count))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the terrain list.");
    }
    return Error_CreateSuccess();
}

/* Lists the chunks of a resolved terrain whose object is shown, with the settings in effect and each chunk's placement:
 * the object's world matrix times the translation to the chunk's centre (in double, and in render space for @p origin)
 * and the chunk's sphere through it. Chunks of holes only are left out. */
static Error AddTerrainChunks(WorldRenderCache* self, const WorldRenderTerrain* entry, Vector3Double origin)
{
    const WorldObject* Owner = WorldComponent_GetOwner(&entry->Component->Base);
    if ((entry->Asset == NULL) || !IsObjectShown(Owner))
    {
        return Error_CreateSuccess();
    }
    TerrainSettings Settings = WorldTerrainComponent_ResolveSettings(entry->Component,
        TerrainAsset_GetSettings(entry->Asset));
    const TerrainMesh* Ground = TerrainAsset_GetMesh(entry->Asset);
    Matrix4Double ObjectWorld = WorldObject_GetWorldMatrix(Owner);
    double MaxScale = WorldTransform_GetMaxScale(&ObjectWorld);
    Color Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Owner));
    for (uint32_t Chunk = 0U; Chunk < TerrainMesh_GetChunkCount(Ground); Chunk++)
    {
        const TerrainChunk* Source = TerrainMesh_GetChunk(Ground, Chunk);
        if (TerrainChunk_GetSurfaceTriangleCount(Source) == 0U)
        {
            continue;
        }
        Matrix4Double ToCenter = Matrix4Double_FromTranslation(TerrainChunk_GetCenter(Source));
        Vector3 SphereCenter;
        float SphereRadius = 0.0f;
        TerrainChunk_GetSphere(Source, &SphereCenter, &SphereRadius);
        WorldRenderTerrainChunk Entry =
        {
            .ObjectId = entry->ObjectId, .Serial = entry->Serial, .Asset = entry->Asset, .ChunkIndex = Chunk,
            .TriangleCount = TerrainChunk_GetSurfaceTriangleCount(Source), .HasOutline = Settings.Render.HasOutline,
            .ShadowTier = Settings.Render.ShadowTier, .CastsShadows = Settings.Render.CastsShadows,
            .Haze = Settings.Render.Haze, .DrawDistance = Settings.Render.DrawDistance,
            .IsDrawDistanceFixed = Settings.Render.IsDrawDistanceFixed,
            .WorldMatrix = Matrix4Double_Multiply(&ToCenter, &ObjectWorld), .Radius = (float)((double)SphereRadius
                * MaxScale), .Tint = Tint
        };
        Entry.Center = Matrix4Double_TransformPoint(&Entry.WorldMatrix, GameMath_FromVector3(SphereCenter));
        Entry.RenderMatrix = GameMath_ToMatrixRelative(&Entry.WorldMatrix, origin);
        Entry.RenderCenter = GameMath_ToVector3Relative(Entry.Center, origin);
        if (!GenericBuffer_AddLast(&self->_terrainChunks, &Entry))
        {
            return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the terrain chunk "
                "list.");
        }
    }
    return Error_CreateSuccess();
}

/* Brings every terrain entry up to date (resolving it again when its name changed) and lists this frame's chunks, in
 * render space for @p origin. The n-th terrain component must own the n-th entry; when one does not (or the counts
 * differ) it stops with *outAreEntriesAligned false so the caller re-matches. */
static Error UpdateTerrains(WorldRenderCache* self, World* world, Vector3Double origin, AssetManager* assets,
    AssetUserID modelUser, Logger* logger, bool* outAreEntriesAligned)
{
    *outAreEntriesAligned = false;
    GenericBuffer_Clear(&self->_terrainChunks);
    size_t TerrainCount = 0;
    WorldComponent* const* Terrains = World_GetComponents(world, WorldComponentType_Terrain, &TerrainCount);
    if (TerrainCount != self->_terrains._count)
    {
        return Error_CreateSuccess();
    }
    for (size_t Index = 0; Index < TerrainCount; Index++)
    {
        WorldRenderTerrain* Entry = GenericBuffer_GetPointerToElement(&self->_terrains, Index);
        if ((Entry->Component != WorldTerrainComponent_FromComponent(Terrains[Index]))
            || (Entry->Serial != WorldComponent_GetSerial(Terrains[Index])))
        {
            GenericBuffer_Clear(&self->_terrainChunks);
            return Error_CreateSuccess();
        }
        uint32_t Revision = WorldComponent_GetRevision(Terrains[Index]);
        bool IsRenamed = Entry->IsResolved && (Entry->Revision != Revision)
            && (HashName(WorldTerrainComponent_GetTerrain(Entry->Component)) != Entry->NameHash);
        if (!Entry->IsResolved || IsRenamed)
        {
            ResolveTerrain(Entry, assets, modelUser, logger);
        }
        Entry->Revision = Revision;
        Error Result = AddTerrainChunks(self, Entry, origin);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    *outAreEntriesAligned = true;
    return Error_CreateSuccess();
}

/* Releases an instances entry's cells. */
static void FreeInstanceCells(WorldRenderInstances* entry)
{
    if (entry->Cells != NULL)
    {
        WorldInstanceCells_Deconstruct(entry->Cells);
        Memory_Free(entry->Cells);
        entry->Cells = NULL;
    }
    entry->AreCellsBuilt = false;
}

/* Releases the cells of every entry in an instances list (before the list is cleared). */
static void FreeInstanceList(GenericBuffer* list)
{
    for (size_t Index = 0U; Index < list->_count; Index++)
    {
        FreeInstanceCells(GenericBuffer_GetPointerToElement(list, Index));
    }
}

/* Resolves an instances entry's variant for the component's current uses_variant and variant name, as ResolveVariant
 * does for a model (an unknown name logged once). */
static void ResolveInstancesVariant(WorldRenderInstances* entry, Logger* logger)
{
    const WorldModelInstancesComponent* Component = entry->Component;
    entry->UsesVariant = WorldModelInstancesComponent_UsesVariant(Component);
    entry->VariantNameHash = HashName(WorldModelInstancesComponent_GetVariant(Component));
    entry->HasVariant = false;
    entry->VariantIndex = 0U;
    if (!entry->UsesVariant || (entry->Asset == NULL))
    {
        return;
    }
    size_t Index = 0U;
    if (WorldModel_TryFindVariant(entry->Asset, WorldModelInstancesComponent_GetVariant(Component), &Index))
    {
        entry->HasVariant = true;
        entry->VariantIndex = Index;
        return;
    }
    ReportVariantFailure(logger, WorldComponent_GetOwner(&Component->Base), "the instances of world model",
        WorldModelInstancesComponent_GetWorldModel(Component), WorldModelInstancesComponent_GetVariant(Component));
}

/* The world model's bounding sphere in a copy's space: its centre through the bounds transform, its radius times that
 * transform's stretch (in double, as a model's sphere is). */
static WorldInstanceBounds GetInstanceBounds(const WorldModel* asset)
{
    Matrix4Double Bounds = GameMath_FromMatrix(WorldModel_GetBoundsTransform(asset));
    Vector3Double Center = Matrix4Double_TransformPoint(&Bounds, GameMath_FromVector3(WorldModel_GetBoundsCenter(asset)));
    return (WorldInstanceBounds)
    {
        .Center = GameMath_ToVector3(Center),
        .Radius = (float)((double)WorldModel_GetBoundsRadius(asset) * WorldTransform_GetMaxScale(&Bounds))
    };
}

/* Resolves an instances entry's world model for the component's current name, then its variant; the cells must be built
 * again (the bounds may differ). A component without a name draws nothing; a load failure is logged here, once. */
static void ResolveInstances(WorldRenderInstances* entry, AssetManager* assets, AssetUserID modelUser, Logger* logger)
{
    const unsigned char* AssetName = WorldModelInstancesComponent_GetWorldModel(entry->Component);
    entry->IsResolved = true;
    entry->NameHash = HashName(AssetName);
    entry->Asset = NULL;
    entry->AreCellsBuilt = false;
    entry->IsLodListRefused = false;
    if (AssetName != NULL)
    {
        WorldModel* Asset = NULL;
        Error LoadResult = AssetManager_LoadWorldModel(assets, AssetName, modelUser, &Asset);
        if (LoadResult.Code == ErrorCode_Success)
        {
            entry->Asset = Asset;
            entry->Bounds = GetInstanceBounds(Asset);
        }
        else
        {
            ReportWorldModelFailure(logger, WorldComponent_GetOwner(&entry->Component->Base), "draws no instances",
                AssetName, &LoadResult);
        }
    }
    ResolveInstancesVariant(entry, logger);
}

/* Builds an entry's cells from its component's copies (allocating the cells on the first build), with a new build id. */
static Error BuildInstanceCells(WorldRenderCache* self, WorldRenderInstances* entry)
{
    if (entry->Cells == NULL)
    {
        entry->Cells = Memory_Allocate(sizeof(WorldInstanceCells));
        WorldInstanceCells_Construct(entry->Cells);
    }
    size_t Count = 0U;
    const PropertyInstanceEntry* Copies = WorldModelInstancesComponent_GetInstances(entry->Component, &Count);
    Error Result = WorldInstanceCells_Build(entry->Cells, Copies, Count, entry->ObjectId, &entry->Bounds);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    entry->AreCellsBuilt = true;
    entry->CellsRevision = WorldModelInstancesComponent_GetInstancesRevision(entry->Component);
    entry->CellsBuildId = ++self->_nextCellsBuildId;
    return Error_CreateSuccess();
}

/* Re-matches the instances entries to the world's current model instances components, in order (as RematchTerrains): a
 * component keeps its entry (its world model and its cells) when one with its owner's id and its serial exists;
 * otherwise it gets a new, unresolved one. The cells of entries no component keeps are released. */
static Error RematchInstances(WorldRenderCache* self, World* world)
{
    size_t PreviousCount = self->_instances._count;
    size_t Cursor = 0;
    GenericBuffer_Clear(&self->_instancesRematch);
    size_t ComponentCount = 0;
    WorldComponent* const* Components = World_GetComponents(world, WorldComponentType_ModelInstances, &ComponentCount);
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0; (Index < ComponentCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        uint64_t Id = WorldObject_GetId(WorldComponent_GetOwner(Components[Index]));
        uint64_t Serial = WorldComponent_GetSerial(Components[Index]);
        WorldRenderInstances Entry = { .ObjectId = Id, .Serial = Serial, .IsResolved = false, .Asset = NULL,
            .Cells = NULL, .AreCellsBuilt = false };
        for (size_t Search = Cursor; Search < PreviousCount; Search++)
        {
            WorldRenderInstances* Previous = GenericBuffer_GetPointerToElement(&self->_instances, Search);
            if (Previous->ObjectId == Id)
            {
                if (Previous->Serial == Serial)
                {
                    // The entry moves with its cells: the old list no longer owns them.
                    Entry = *Previous;
                    Previous->Cells = NULL;
                }
                Cursor = Search + 1;
                break;
            }
        }
        Entry.Component = WorldModelInstancesComponent_FromComponent(Components[Index]);
        if (!GenericBuffer_AddLast(&self->_instancesRematch, &Entry))
        {
            FreeInstanceCells(&Entry);
            Result = Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the instances list.");
        }
    }
    FreeInstanceList(&self->_instances);
    GenericBuffer_Clear(&self->_instances);
    if ((Result.Code == ErrorCode_Success) && (self->_instancesRematch._count > 0)
        && !GenericBuffer_AddLastRange(&self->_instances, self->_instancesRematch._data, self->_instancesRematch._count))
    {
        Result = Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the instances list.");
    }
    if (Result.Code != ErrorCode_Success)
    {
        FreeInstanceList(&self->_instancesRematch);
        GenericBuffer_Clear(&self->_instances);
    }
    GenericBuffer_Clear(&self->_instancesRematch);
    return Result;
}

/* Lists the cells of a drawn instances entry for this frame: each cell's world matrix (the object's world matrix times
 * the translation to its centre) and its sphere through it, in double and in render space for @p origin. */
static Error AddInstanceCells(WorldRenderCache* self, const WorldRenderInstances* entry, size_t entryIndex,
    Vector3Double origin)
{
    size_t CellCount = 0U;
    const WorldInstanceCell* Cells = WorldInstanceCells_GetCells(entry->Cells, &CellCount);
    for (size_t Index = 0U; Index < CellCount; Index++)
    {
        const WorldInstanceCell* Source = &Cells[Index];
        Matrix4Double ToCenter = Matrix4Double_FromTranslation(Source->Center);
        WorldRenderInstanceCell Cell =
        {
            .Entry = entryIndex, .Cell = Index, .FirstRecord = Source->FirstRecord, .RecordCount = Source->RecordCount,
            .HasOutline = entry->HasOutline, .ShadowTier = entry->ShadowTier, .CastsShadows = entry->CastsShadows,
            .DrawDistance = entry->DrawDistance, .IsDrawDistanceFixed = entry->IsDrawDistanceFixed, .Tint = entry->Tint,
            .WorldMatrix = Matrix4Double_Multiply(&ToCenter, &entry->WorldMatrix),
            .Center = Matrix4Double_TransformPoint(&entry->WorldMatrix, Source->SphereCenter),
            .Radius = (float)(Source->SphereRadius * (double)entry->WorldStretch),
            .LodMask = 0U
        };
        Cell.RenderMatrix = GameMath_ToMatrixRelative(&Cell.WorldMatrix, origin);
        Cell.RenderCenter = GameMath_ToVector3Relative(Cell.Center, origin);
        if (!GenericBuffer_AddLast(&self->_instanceCells, &Cell))
        {
            return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderCache: could not grow the instance cell list.");
        }
    }
    return Error_CreateSuccess();
}

/* Brings one instances entry up to date for this frame: its world model and variant when they changed, its cells when
 * its copies or its world model did, then its settings in effect, its tint, its world matrix and its cells. */
static Error UpdateInstancesEntry(WorldRenderCache* self, WorldRenderInstances* entry, size_t entryIndex,
    Vector3Double origin, AssetManager* assets, AssetUserID modelUser, Logger* logger)
{
    const WorldModelInstancesComponent* Component = entry->Component;
    uint32_t Revision = WorldComponent_GetRevision(&Component->Base);
    bool IsChanged = entry->IsResolved && (entry->Revision != Revision);
    bool IsRenamed = IsChanged && (HashName(WorldModelInstancesComponent_GetWorldModel(Component)) != entry->NameHash);
    bool IsVariantChanged = IsChanged && ((WorldModelInstancesComponent_UsesVariant(Component) != entry->UsesVariant)
        || (HashName(WorldModelInstancesComponent_GetVariant(Component)) != entry->VariantNameHash));
    if (!entry->IsResolved || IsRenamed)
    {
        ResolveInstances(entry, assets, modelUser, logger);
    }
    else if (IsVariantChanged)
    {
        ResolveInstancesVariant(entry, logger);
    }
    entry->Revision = Revision;

    WorldModelSettings Defaults = (entry->Asset != NULL) ? *WorldModel_GetSettings(entry->Asset)
        : WorldModelSettings_CreateDefault();
    WorldModelSettings Settings = WorldModelInstancesComponent_ResolveSettings(Component, &Defaults);
    entry->HasOutline = Settings.HasOutline;
    entry->ShadowTier = Settings.ShadowTier;
    entry->CastsShadows = Settings.CastsShadows;
    entry->Haze = Settings.Haze;
    entry->DrawDistance = Settings.DrawDistance;
    entry->IsDrawDistanceFixed = Settings.IsDrawDistanceFixed;
    const WorldObject* Owner = WorldComponent_GetOwner(&Component->Base);
    entry->Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Owner));
    entry->IsDrawn = (entry->Asset != NULL) && IsObjectShown(Owner);
    if (!entry->IsDrawn)
    {
        return Error_CreateSuccess();
    }
    if (!entry->AreCellsBuilt || (entry->CellsRevision != WorldModelInstancesComponent_GetInstancesRevision(Component)))
    {
        Error BuildResult = BuildInstanceCells(self, entry);
        if (BuildResult.Code != ErrorCode_Success)
        {
            return BuildResult;
        }
    }
    entry->WorldMatrix = WorldObject_GetWorldMatrix(Owner);
    entry->WorldStretch = (float)WorldTransform_GetMaxScale(&entry->WorldMatrix);
    return AddInstanceCells(self, entry, entryIndex, origin);
}

/* Brings every instances entry up to date and lists this frame's cells, in render space for @p origin. The n-th model
 * instances component must own the n-th entry; when one does not (or the counts differ) it stops with
 * *outAreEntriesAligned false so the caller re-matches. */
static Error UpdateInstances(WorldRenderCache* self, World* world, Vector3Double origin, AssetManager* assets,
    AssetUserID modelUser, Logger* logger, bool* outAreEntriesAligned)
{
    *outAreEntriesAligned = false;
    GenericBuffer_Clear(&self->_instanceCells);
    size_t ComponentCount = 0;
    WorldComponent* const* Components = World_GetComponents(world, WorldComponentType_ModelInstances, &ComponentCount);
    if (ComponentCount != self->_instances._count)
    {
        return Error_CreateSuccess();
    }
    for (size_t Index = 0; Index < ComponentCount; Index++)
    {
        WorldRenderInstances* Entry = GenericBuffer_GetPointerToElement(&self->_instances, Index);
        if ((Entry->Component != WorldModelInstancesComponent_FromComponent(Components[Index]))
            || (Entry->Serial != WorldComponent_GetSerial(Components[Index])))
        {
            GenericBuffer_Clear(&self->_instanceCells);
            return Error_CreateSuccess();
        }
        Error Result = UpdateInstancesEntry(self, Entry, Index, origin, assets, modelUser, logger);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    *outAreEntriesAligned = true;
    return Error_CreateSuccess();
}

/* One drawn instances entry's LOD bands this frame, from its LOD settings in effect (a placement's own list that does not
 * fit its world model is warned about once, when that starts). */
static void UpdateInstanceBands(WorldRenderInstances* entry, const WorldRenderLodView* view, Logger* logger)
{
    bool IsAsAsked = WorldModelInstancesComponent_ResolveLodSettings(entry->Component, entry->Asset, &entry->LodSettings);
    if (!IsAsAsked && !entry->IsLodListRefused)
    {
        size_t OwnCount = 0U;
        WorldModelInstancesComponent_GetLodDistances(entry->Component, &OwnCount);
        ReportLodListRefused(logger, WorldComponent_GetOwner(&entry->Component->Base),
            WorldModelInstancesComponent_GetWorldModel(entry->Component), OwnCount, entry->Asset);
    }
    entry->IsLodListRefused = !IsAsAsked;
    WorldInstancing_GetLodBands(&entry->LodSettings, view->DistanceScale, &entry->Bands);
}

/* The LODs a cell's copies can show this frame: its entry's bands over the camera's zoomed distances to the cell's
 * sphere's nearest and farthest points (each copy's sphere centre lies inside it). */
static void UpdateInstanceCellLods(WorldRenderInstanceCell* cell, const WorldRenderInstances* entry,
    const WorldRenderLodView* view, float zoomFactor)
{
    float Distance = Vector3Distance(view->CameraPosition, cell->RenderCenter);
    cell->LodMask = WorldInstancing_GetCellLodMask(&entry->Bands, fmaxf(Distance - cell->Radius, 0.0f) * zoomFactor,
        (Distance + cell->Radius) * zoomFactor);
}

// Internal functions (declared in WorldRenderCache.h).
/* The first-person object's camera pose (T30): its pose with every bone its world model's first_person block hides, and
 * each one's subtree, skinned to its hidden ancestor's joint (every vertex it moves lands on one point: nothing to draw),
 * in the cache's own copy of the palette. */
static void UpdateFirstPerson(WorldRenderCache* self)
{
    if (self->_firstPersonObjectId == 0U)
    {
        return;
    }
    WorldRenderModel* Entries = (WorldRenderModel*)(void*)self->_models._data;
    for (size_t Index = 0U; Index < self->_models._count; Index++)
    {
        WorldRenderModel* Entry = &Entries[Index];
        const uint32_t* HiddenBones = NULL;
        uint32_t HiddenCount = 0U;
        if ((Entry->Pose == NULL) || (Entry->ObjectId != self->_firstPersonObjectId)
            || !WorldModel_GetFirstPerson(Entry->Asset, NULL, &HiddenBones, &HiddenCount) || (HiddenCount == 0U))
        {
            continue;
        }
        const WorldAnimatorPose* Pose = Entry->Pose;
        uint32_t BoneCount = Pose->_boneCount;
        if (BoneCount > self->_firstPersonPaletteBones)
        {
            Memory_Free(self->_firstPersonPalette);
            self->_firstPersonPalette = Memory_Allocate((size_t)BoneCount * SKELETAL_PALETTE_FLOATS_PER_BONE * sizeof(float));
            self->_firstPersonPaletteBones = BoneCount;
        }
        float* Palette = self->_firstPersonPalette;
        Memory_Copy(Pose->_palette, Palette, (size_t)BoneCount * SKELETAL_PALETTE_FLOATS_PER_BONE * sizeof(float));
        // Each bone's hidden ancestor (itself when it is listed), parents first; no hidden one: UINT32_MAX.
        uint32_t HiddenRoot[GAME_SKELETON_MAX_BONES];
        const GameSkeleton* Skeleton = Pose->_skeleton;
        const uint16_t* Order = GameSkeleton_GetOrder(Skeleton);
        for (uint32_t Bone = 0U; Bone < BoneCount; Bone++)
        {
            HiddenRoot[Bone] = UINT32_MAX;
        }
        for (uint32_t Listed = 0U; Listed < HiddenCount; Listed++)
        {
            if (HiddenBones[Listed] < BoneCount)
            {
                HiddenRoot[HiddenBones[Listed]] = HiddenBones[Listed];
            }
        }
        for (uint32_t Step = 0U; Step < BoneCount; Step++)
        {
            uint32_t Bone = Order[Step];
            int32_t Parent = GameSkeleton_GetBone(Skeleton, Bone)->_parent;
            if ((HiddenRoot[Bone] == UINT32_MAX) && (Parent >= 0))
            {
                HiddenRoot[Bone] = HiddenRoot[Parent];
            }
            if (HiddenRoot[Bone] == UINT32_MAX)
            {
                continue;
            }
            // No linear part: the vertices go to the hidden bone's joint, in mesh space.
            const Matrix* Joint = &Pose->_boneMatrices[HiddenRoot[Bone]];
            float* Rows = Palette + ((size_t)Bone * SKELETAL_PALETTE_FLOATS_PER_BONE);
            Memory_Zero(Rows, SKELETAL_PALETTE_FLOATS_PER_BONE * sizeof(float));
            Rows[3] = Joint->m12;
            Rows[7] = Joint->m13;
            Rows[11] = Joint->m14;
        }
        self->_firstPersonPose = *Pose;
        self->_firstPersonPose._palette = Palette;
        Entry->CameraPose = &self->_firstPersonPose;
        return;
    }
}

void WorldRenderCache_Construct(WorldRenderCache* self)
{
    GenericBuffer_AllocateVariable(&self->_models, INITIAL_LIST_CAPACITY, sizeof(WorldRenderModel));
    GenericBuffer_AllocateVariable(&self->_rematch, INITIAL_LIST_CAPACITY, sizeof(WorldRenderModel));
    GenericBuffer_AllocateVariable(&self->_sprites, INITIAL_LIST_CAPACITY, sizeof(WorldRenderSprite));
    GenericBuffer_AllocateVariable(&self->_lights, INITIAL_LIST_CAPACITY, sizeof(WorldRenderLight));
    GenericBuffer_AllocateVariable(&self->_terrains, INITIAL_LIST_CAPACITY, sizeof(WorldRenderTerrain));
    GenericBuffer_AllocateVariable(&self->_terrainRematch, INITIAL_LIST_CAPACITY, sizeof(WorldRenderTerrain));
    GenericBuffer_AllocateVariable(&self->_terrainChunks, INITIAL_LIST_CAPACITY, sizeof(WorldRenderTerrainChunk));
    GenericBuffer_AllocateVariable(&self->_instances, INITIAL_LIST_CAPACITY, sizeof(WorldRenderInstances));
    GenericBuffer_AllocateVariable(&self->_instancesRematch, INITIAL_LIST_CAPACITY, sizeof(WorldRenderInstances));
    GenericBuffer_AllocateVariable(&self->_instanceCells, INITIAL_LIST_CAPACITY, sizeof(WorldRenderInstanceCell));
    self->_nextCellsBuildId = 0U;
    self->_world = NULL;
    self->_componentSetVersion = 0;
    self->_isMatched = false;
    self->_firstPersonObjectId = 0U;
    Memory_Zero(&self->_firstPersonPose, sizeof(self->_firstPersonPose));
    self->_firstPersonPalette = NULL;
    self->_firstPersonPaletteBones = 0U;
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
    Memory_Free(self->_terrains._data);
    Memory_Free(self->_terrainRematch._data);
    Memory_Free(self->_terrainChunks._data);
    FreeInstanceList(&self->_instances);
    Memory_Free(self->_instances._data);
    Memory_Free(self->_instancesRematch._data);
    Memory_Free(self->_instanceCells._data);
    Memory_Free(self->_firstPersonPalette);
}

void WorldRenderCache_Reset(WorldRenderCache* self)
{
    GenericBuffer_Clear(&self->_models);
    GenericBuffer_Clear(&self->_sprites);
    GenericBuffer_Clear(&self->_lights);
    GenericBuffer_Clear(&self->_terrains);
    GenericBuffer_Clear(&self->_terrainChunks);
    FreeInstanceList(&self->_instances);
    GenericBuffer_Clear(&self->_instances);
    GenericBuffer_Clear(&self->_instanceCells);
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
    bool IsTerrainMatchCurrent = IsMatchCurrent;
    bool IsInstancesMatchCurrent = IsMatchCurrent;
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
    UpdateFirstPerson(self);

    // The terrains follow the world's terrain components the same way (B01).
    bool AreTerrainsAligned = false;
    for (int Attempt = 0; (Attempt < 2) && !AreTerrainsAligned; Attempt++)
    {
        Error Result = IsTerrainMatchCurrent ? Error_CreateSuccess() : RematchTerrains(self, world);
        if (Result.Code == ErrorCode_Success)
        {
            Result = UpdateTerrains(self, world, origin, assets, modelUser, logger, &AreTerrainsAligned);
        }
        if (Result.Code != ErrorCode_Success)
        {
            WorldRenderCache_Reset(self);
            return Result;
        }
        IsTerrainMatchCurrent = false;
    }
    if (!AreTerrainsAligned)
    {
        WorldRenderCache_Reset(self);
        return Error_Construct2(ErrorCode_InvalidState,
            "WorldRenderCache_Sync: the terrain entries do not follow the world.");
    }

    // The model instances follow the world's model instances components the same way (B03).
    bool AreInstancesAligned = false;
    for (int Attempt = 0; (Attempt < 2) && !AreInstancesAligned; Attempt++)
    {
        Error Result = IsInstancesMatchCurrent ? Error_CreateSuccess() : RematchInstances(self, world);
        if (Result.Code == ErrorCode_Success)
        {
            Result = UpdateInstances(self, world, origin, assets, modelUser, logger, &AreInstancesAligned);
        }
        if (Result.Code != ErrorCode_Success)
        {
            WorldRenderCache_Reset(self);
            return Result;
        }
        IsInstancesMatchCurrent = false;
    }
    if (!AreInstancesAligned)
    {
        WorldRenderCache_Reset(self);
        return Error_Construct2(ErrorCode_InvalidState,
            "WorldRenderCache_Sync: the model instances entries do not follow the world.");
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
    for (size_t Index = 0; Index < self->_instances._count; Index++)
    {
        WorldRenderInstances* Entry = GenericBuffer_GetPointerToElement(&self->_instances, Index);
        if (Entry->IsDrawn)
        {
            UpdateInstanceBands(Entry, view, logger);
        }
    }
    // Only drawn entries list cells, so every cell's entry has its bands now.
    for (size_t Index = 0; Index < self->_instanceCells._count; Index++)
    {
        WorldRenderInstanceCell* Cell = GenericBuffer_GetPointerToElement(&self->_instanceCells, Index);
        UpdateInstanceCellLods(Cell, GenericBuffer_GetPointerToElement(&self->_instances, Cell->Entry), view, ZoomFactor);
    }
}

Matrix WorldRenderModel_GetLodTransform(const WorldRenderModel* model, size_t lod)
{
    GameModel* LodModel = WorldRenderModel_GetLodModel(model, lod);
    return (LodModel != NULL) ? MatrixMultiply(GameModel_GetRaylibModel(LodModel).transform, model->RenderMatrix)
        : model->RenderMatrix;
}
