#include <math.h>
#include "WorldRenderer.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldModelObject.h"
#include "GameCamera.h"
#include "AssetManager.h"
#include "GameModel.h"
#include "Logger.h"
#include "Renderer.h"
#include "raylib/raylib.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** Number of grid lines to each side of the origin for the debug reference grid. */
#define DEBUG_GRID_SLICES 40
/** World-space spacing between debug grid lines. */
#define DEBUG_GRID_SPACING 1.0f
/** Pixel count on the LONGER window axis for the pixelation pass; the shorter axis is derived to keep
 *  pixels square. For a 16:9 window this yields the game's 640x360 pixel grid. */
#define PIXELATION_LONG_AXIS 640


// Types.
struct WorldRendererStruct
{
    /** Asset manager used to resolve object assets; borrowed. */
    AssetManager* _assetManager;
    /** Logger for diagnostics; borrowed, may be NULL. */
    Logger* _logger;
    /** The renderer's asset user id; every asset it loads is held under this. */
    AssetUserID _assetUser;
    /** Whether the debug reference grid is drawn. */
    bool _drawDebugGrid;
    /** Whether the final pixelation pass is applied. */
    bool _pixelationEnabled;
    /** Internal scene target the 3D world renders into (pixel resolution when pixelating). */
    RenderTexture2D _sceneTarget;
    /** Whether _sceneTarget has been created. */
    bool _hasSceneTarget;
    /** Current width of _sceneTarget, in pixels. */
    int _sceneWidth;
    /** Current height of _sceneTarget, in pixels. */
    int _sceneHeight;
};


// Static functions.
/* Placeholder sky color used until the physically based sky pass is implemented. */
static Color GetSkyColor(void)
{
    return (Color){ .r = 132, .g = 173, .b = 220, .a = 255 };
}

/* Computes the scene-target size for a given window size. With pixelation, the longer window axis is
 * pinned to PIXELATION_LONG_AXIS and the shorter axis derived proportionally, so the upscale to the window
 * is a near-uniform (square-pixel) magnification. Without pixelation the scene target matches the window. */
static void ComputeSceneSize(bool pixelation, int windowWidth, int windowHeight, int* outWidth, int* outHeight)
{
    int SafeWidth = (windowWidth < 1) ? 1 : windowWidth;
    int SafeHeight = (windowHeight < 1) ? 1 : windowHeight;

    if (!pixelation)
    {
        *outWidth = SafeWidth;
        *outHeight = SafeHeight;
        return;
    }

    if (SafeWidth >= SafeHeight)
    {
        *outWidth = PIXELATION_LONG_AXIS;
        *outHeight = (int)lroundf((float)PIXELATION_LONG_AXIS * (float)SafeHeight / (float)SafeWidth);
    }
    else
    {
        *outHeight = PIXELATION_LONG_AXIS;
        *outWidth = (int)lroundf((float)PIXELATION_LONG_AXIS * (float)SafeWidth / (float)SafeHeight);
    }

    if (*outWidth < 1) { *outWidth = 1; }
    if (*outHeight < 1) { *outHeight = 1; }
}

/* Ensures the scene target exists and matches the desired size for the given window size / pixelation. */
static void EnsureSceneTarget(WorldRenderer* self, int windowWidth, int windowHeight)
{
    int DesiredWidth = 0;
    int DesiredHeight = 0;
    ComputeSceneSize(self->_pixelationEnabled, windowWidth, windowHeight, &DesiredWidth, &DesiredHeight);

    if (self->_hasSceneTarget && (self->_sceneWidth == DesiredWidth) && (self->_sceneHeight == DesiredHeight))
    {
        return;
    }

    if (self->_hasSceneTarget)
    {
        UnloadRenderTexture(self->_sceneTarget);
        self->_hasSceneTarget = false;
    }

    self->_sceneTarget = LoadRenderTexture(DesiredWidth, DesiredHeight);
    // Point filtering makes the upscale to the window produce hard, square pixels.
    SetTextureFilter(self->_sceneTarget.texture, TEXTURE_FILTER_POINT);
    self->_hasSceneTarget = true;
    self->_sceneWidth = DesiredWidth;
    self->_sceneHeight = DesiredHeight;
}

/* Logs an asset-resolution failure (if a logger is present) and releases the error. */
static void ReportAssetFailure(WorldRenderer* self, const unsigned char* assetName, Error* error)
{
    if (self->_logger != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(self->_logger,
            (const unsigned char*)u8"WorldRenderer: could not load model asset \"%s\": %s",
            (const char*)assetName,
            (error->Message != NULL) ? (const char*)error->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

/* Draws a single model object. Missing/failed assets are skipped silently (PrepareWorld reports them). */
static void DrawModelObject(WorldRenderer* self, WorldModelObject* modelObject)
{
    const unsigned char* AssetName = WorldModelObject_GetModelAssetName(modelObject);
    if (AssetName == NULL)
    {
        return;
    }

    GameModel* ModelAsset = NULL;
    Error LoadResult = AssetManager_LoadModel(self->_assetManager, AssetName, self->_assetUser, &ModelAsset);
    if (LoadResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&LoadResult);
        return;
    }

    WorldObject* Base = WorldModelObject_AsObject(modelObject);
    Vector3 Position = WorldObject_GetPosition(Base);
    Vector3 Rotation = WorldObject_GetRotation(Base);
    Vector3 Scale = WorldObject_GetScale(Base);

    Matrix World = MatrixMultiply(MatrixMultiply(MatrixScale(Scale.x, Scale.y, Scale.z),
        MatrixRotateXYZ(Rotation)), MatrixTranslate(Position.x, Position.y, Position.z));

    // Compose over the model's baked import transform (import inner, world outer), matching raylib's own
    // DrawModelEx convention, then draw with a neutral extra transform so only our matrix and tint apply.
    Model RayModel = GameModel_GetRaylibModel(ModelAsset);
    RayModel.transform = MatrixMultiply(RayModel.transform, World);
    Color Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Base));
    DrawModel(RayModel, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Tint);
}

/* Draws the world's 3D content into the currently active render target with the given camera. */
static void DrawScene(WorldRenderer* self, World* world, const GameCamera* camera)
{
    ClearBackground(GetSkyColor());

    BeginMode3D(GameCamera_ToRaylibCamera(camera));

    if (self->_drawDebugGrid)
    {
        DrawGrid(DEBUG_GRID_SLICES, DEBUG_GRID_SPACING);
    }

    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&GetResult);
            continue;
        }

        switch (WorldObject_GetType(Object))
        {
            case WorldObjectType_Model:
                DrawModelObject(self, (WorldModelObject*)Object);
                break;

            case WorldObjectType_Sprite:
                // TODO: draw sprite objects (textured, world-placed planes) in a later step.
                break;

            case WorldObjectType_Light:
                // TODO: feed lights into the (future) lighting pass; lights draw no geometry.
                break;
        }
    }

    EndMode3D();
}


// Public functions.
Error WorldRenderer_Create(AssetManager* assetManager, Logger* logger, WorldRenderer** outRenderer)
{
    if ((assetManager == NULL) || (outRenderer == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldRenderer_Create: assetManager and outRenderer must not be NULL.");
    }
    *outRenderer = NULL;

    AssetUserID User = ASSET_USER_ID_INVALID;
    Error UserResult = AssetManager_GetNewUserID(assetManager, &User);
    if (UserResult.Code != ErrorCode_Success)
    {
        return UserResult;
    }

    WorldRenderer* Renderer = Memory_Allocate(sizeof(WorldRenderer));
    Memory_Zero(Renderer, sizeof(*Renderer));
    Renderer->_assetManager = assetManager;
    Renderer->_logger = logger;
    Renderer->_assetUser = User;
    Renderer->_drawDebugGrid = true;
    Renderer->_pixelationEnabled = true;
    Renderer->_hasSceneTarget = false;

    *outRenderer = Renderer;
    return Error_CreateSuccess();
}

Error WorldRenderer_Deconstruct(WorldRenderer* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    if (self->_hasSceneTarget)
    {
        UnloadRenderTexture(self->_sceneTarget);
        self->_hasSceneTarget = false;
    }

    Error Result = AssetManager_ReleaseAllAssetsForUser(self->_assetManager, self->_assetUser);

    Error RetireResult = AssetManager_RetireUser(self->_assetManager, self->_assetUser);
    if (Result.Code == ErrorCode_Success)
    {
        Result = RetireResult;
    }
    else
    {
        Error_Deconstruct(&RetireResult);
    }

    Memory_Free(self);
    return Result;
}

Error WorldRenderer_PrepareWorld(WorldRenderer* self, World* world)
{
    if ((self == NULL) || (world == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldRenderer_PrepareWorld: self and world must not be NULL.");
    }

    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&GetResult);
            continue;
        }

        if (WorldObject_GetType(Object) != WorldObjectType_Model)
        {
            continue;
        }

        WorldModelObject* ModelObject = (WorldModelObject*)Object;
        const unsigned char* AssetName = WorldModelObject_GetModelAssetName(ModelObject);
        if (AssetName == NULL)
        {
            continue;
        }

        GameModel* ModelAsset = NULL;
        Error LoadResult = AssetManager_LoadModel(self->_assetManager, AssetName, self->_assetUser, &ModelAsset);
        if (LoadResult.Code != ErrorCode_Success)
        {
            ReportAssetFailure(self, AssetName, &LoadResult);
        }
    }

    return Error_CreateSuccess();
}

Error WorldRenderer_RenderToTarget(WorldRenderer* self, World* world, const GameCamera* camera, RenderTexture2D target)
{
    if ((self == NULL) || (world == NULL) || (camera == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldRenderer_RenderToTarget: self, world and camera must not be NULL.");
    }

    int WindowWidth = target.texture.width;
    int WindowHeight = target.texture.height;
    EnsureSceneTarget(self, WindowWidth, WindowHeight);

    // Pass 1: draw the 3D world into the scene target (pixel resolution when pixelating).
    BeginTextureMode(self->_sceneTarget);
    DrawScene(self, world, camera);
    EndTextureMode();

    // Pass 2: blit the scene target into the frame target. A negative source height applies the vertical
    // flip Raylib render textures need (the same convention the frame manager uses when compositing), and
    // point filtering on the scene texture makes the upscale produce hard square pixels.
    BeginTextureMode(target);
    ClearBackground(BLACK);
    Rectangle Source =
    {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)self->_sceneTarget.texture.width,
        .height = -(float)self->_sceneTarget.texture.height
    };
    Rectangle Destination = { .x = 0.0f, .y = 0.0f, .width = (float)WindowWidth, .height = (float)WindowHeight };
    DrawTexturePro(self->_sceneTarget.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndTextureMode();

    return Error_CreateSuccess();
}

void WorldRenderer_SetPixelationEnabled(WorldRenderer* self, bool enabled)
{
    self->_pixelationEnabled = enabled;
}

bool WorldRenderer_IsPixelationEnabled(const WorldRenderer* self)
{
    return self->_pixelationEnabled;
}

void WorldRenderer_SetDebugGridEnabled(WorldRenderer* self, bool enabled)
{
    self->_drawDebugGrid = enabled;
}

bool WorldRenderer_IsDebugGridEnabled(const WorldRenderer* self)
{
    return self->_drawDebugGrid;
}
