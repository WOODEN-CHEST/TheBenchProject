// The scene render target (size, HDR tiers, recreation on resize) and the scene colour pass.

#include <math.h>
#include "WorldRendererInternal.h"
#include "GameMath.h"
#include "Logger.h"
#include "raylib/rlgl.h"


// Macros.
/** Number of grid lines to each side of the origin for the debug reference grid. */
#define DEBUG_GRID_SLICES 40
/** World-space spacing between debug grid lines. */
#define DEBUG_GRID_SPACING 1.0f
/** Pixel count on the LONGER window axis for the pixelation pass; the shorter axis is derived to keep
 *  pixels square. For a 16:9 window this yields the game's 640x360 pixel grid. */
#define PIXELATION_LONG_AXIS 640


// Static functions.
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

/* Creates the scene render target, degrading gracefully so the post pass can never cost the HDR pipeline:
 * (1) HDR + samplable depth texture (full pipeline incl. postfx), (2) HDR + depth renderbuffer (postfx
 * disabled, sky/tonemap intact — the pre-postfx arrangement), (3) 8-bit LoadRenderTexture as the last
 * resort. An 8-bit scene buffer clamps/quantizes the linear-HDR sky badly, so it must never be entered just
 * because the depth-texture combination failed. Returns a zero target (id 0) only if even that fails. */
static RenderTexture2D CreateSceneTarget(int width, int height, bool* outIsHDR, bool* outDepthSamplable)
{
    *outIsHDR = false;
    *outDepthSamplable = false;

    RenderTexture2D Target = WorldRendererCommon_TryCreateHdrTarget(width, height, true);
    if (Target.id != 0)
    {
        *outIsHDR = true;
        *outDepthSamplable = true;
        return Target;
    }

    Target = WorldRendererCommon_TryCreateHdrTarget(width, height, false);
    if (Target.id != 0)
    {
        *outIsHDR = true;
        return Target;
    }

    return LoadRenderTexture(width, height);
}

/* Converts an opaque sRGB colour to the byte encoding of its LINEAR value, so clearing the linear-HDR scene
 * buffer to a CPU-authored (sRGB) sky colour leaves the buffer consistently linear for the tonemap pass. */
static Color LinearizeColorBytes(Color color)
{
    return (Color)
    {
        .r = (unsigned char)lroundf(powf((float)color.r / 255.0f, SRGB_GAMMA) * 255.0f),
        .g = (unsigned char)lroundf(powf((float)color.g / 255.0f, SRGB_GAMMA) * 255.0f),
        .b = (unsigned char)lroundf(powf((float)color.b / 255.0f, SRGB_GAMMA) * 255.0f),
        .a = color.a
    };
}

/* Draws the debug reference grid around the world's origin, which sits at minus the frame's origin in render space. */
static void DrawDebugGrid(const WorldRenderFrame* frame)
{
    Vector3 WorldOrigin = GameMath_ToVector3Relative(Vector3Double_Zero(), frame->Origin);
    rlPushMatrix();
    rlTranslatef(WorldOrigin.x, WorldOrigin.y, WorldOrigin.z);
    DrawGrid(DEBUG_GRID_SLICES, DEBUG_GRID_SPACING);
    rlPopMatrix();
}

/* Reports the scene-target tier once so it is easy to confirm which pipeline is active from the log. */
static void ReportSceneTier(WorldRenderer* self)
{
    if (self->_scene._isTierReported || (self->_logger == NULL))
    {
        return;
    }
    self->_scene._isTierReported = true;

    const unsigned char* Message;
    if (self->_scene._isHdr && self->_scene._isDepthSamplable)
    {
        Message = (const unsigned char*)u8"WorldRenderer: scene target is HDR (RGBA16F) with samplable depth (post "
            u8"effects available).";
    }
    else if (self->_scene._isHdr)
    {
        Message = (const unsigned char*)u8"WorldRenderer: scene target is HDR (RGBA16F); depth not samplable, so AO, "
            u8"outlines, sun shafts and the crisp overlay are unavailable.";
    }
    else
    {
        Message = (const unsigned char*)u8"WorldRenderer: HDR framebuffer unsupported; using an 8-bit scene target "
            u8"(AO, outlines, sun shafts and the crisp overlay are unavailable).";
    }
    Error LogResult = Logger_LogInfo(self->_logger, Message);
    Error_Deconstruct(&LogResult);
}


// Internal functions (declared in WorldRendererInternal.h).
Error WorldRendererScene_EnsureTargets(WorldRenderer* self, int windowWidth, int windowHeight)
{
    int SceneWidth = 0;
    int SceneHeight = 0;
    ComputeSceneSize(self->_pixelationEnabled, windowWidth, windowHeight, &SceneWidth, &SceneHeight);

    // The crisp overlay is WINDOW-resolution, so the targets also follow the raw window size (which can change
    // while the pixelated scene size stays put — same aspect, different window).
    int SafeWindowWidth = (windowWidth < 1) ? 1 : windowWidth;
    int SafeWindowHeight = (windowHeight < 1) ? 1 : windowHeight;
    if (self->_scene._hasTarget && (self->_scene._width == SceneWidth) && (self->_scene._height == SceneHeight)
        && (self->_scene._windowWidth == SafeWindowWidth) && (self->_scene._windowHeight == SafeWindowHeight))
    {
        return Error_CreateSuccess();
    }

    WorldRendererScene_ReleaseTargets(self);

    self->_scene._target = CreateSceneTarget(SceneWidth, SceneHeight, &self->_scene._isHdr,
        &self->_scene._isDepthSamplable);
    if (self->_scene._target.id == 0)
    {
        return Error_Construct4(ErrorCode_InvalidState,
            "WorldRenderer: could not create a %dx%d scene render target (not even 8-bit).", SceneWidth, SceneHeight);
    }
    // Point filtering makes the upscale to the window produce hard, square pixels. Clamped at its edges: raylib makes
    // colour textures repeat, so a pass reading the scene at an edge would take the opposite edge's pixels (the sun
    // shafts drew the town at the bottom of the screen as white and aqua streaks at its top).
    SetTextureFilter(self->_scene._target.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(self->_scene._target.texture, TEXTURE_WRAP_CLAMP);
    if (self->_scene._isDepthSamplable)
    {
        // The post passes compare raw depth texels, so nearest (point) sampling is required.
        SetTextureFilter(self->_scene._target.depth, TEXTURE_FILTER_POINT);
    }
    self->_scene._hasTarget = true;
    self->_scene._width = SceneWidth;
    self->_scene._height = SceneHeight;
    self->_scene._windowWidth = SafeWindowWidth;
    self->_scene._windowHeight = SafeWindowHeight;

    // The passes' own targets depend on the scene target's size and depth tier, so they come after it.
    WorldRendererGBuffer_CreateTargets(self, SceneWidth, SceneHeight);
    WorldRendererPost_CreateTargets(self, SceneWidth, SceneHeight);
    WorldRendererCrisp_CreateTargets(self, SafeWindowWidth, SafeWindowHeight);

    ReportSceneTier(self);
    return Error_CreateSuccess();
}

void WorldRendererScene_ReleaseTargets(WorldRenderer* self)
{
    WorldRendererCommon_UnloadTarget(&self->_scene._target, &self->_scene._hasTarget);
    self->_scene._isDepthSamplable = false;
    WorldRendererGBuffer_ReleaseTargets(self);
    WorldRendererPost_ReleaseTargets(self);
    WorldRendererCrisp_ReleaseTargets(self);
}

void WorldRendererScene_Render(WorldRenderer* self, const WorldRenderFrame* frame)
{
    BeginTextureMode(self->_scene._target);

    // Clear the depth buffer (and colour). When the atmospheric sky shader is present it overwrites every
    // pixel below, so the clear colour only matters as the fallback (the linearized CPU gradient) for when
    // the sky shader is unavailable. The scene buffer is linear HDR; the tonemap pass maps it to display.
    Color ClearColor = (self->_sky._shader != NULL)
        ? BLACK
        : LinearizeColorBytes(WorldEnvironment_ComputeSkyColor(frame->Environment));
    ClearBackground(ClearColor);

    // Atmospheric sky behind the geometry (full-screen linear-HDR pass; writes no depth), then the stars over it
    // (one pixel each, additive, writing no depth either).
    if (self->_sky._shader != NULL)
    {
        WorldRendererSky_Draw(self, frame);
    }
    if (frame->AreStarsActive)
    {
        WorldRendererStars_Draw(self, frame);
    }

    // Feed the sun/ambient/camera into the lit shaders (models, sprites) before drawing any lit geometry. The scene pass
    // applies the screen-space AO map (occludes the ambient term); it is the scene's own pixel grid, so the dither's
    // pixel scale is 1.
    WorldRendererLighting_UploadFrame(self, frame, true, (Vector2){ 1.0f, 1.0f });

    // Render space: the camera at the origin, every position relative to it (the render cache's).
    BeginMode3D(frame->RenderCamera);

    if (self->_drawDebugGrid)
    {
        DrawDebugGrid(frame);
    }

    // Model objects, shaded through the PBR shader (or each material's own default shader when it is
    // unavailable). Filtered by OmitPixelation: crisp objects are drawn by the crisp overlay pass instead.
    WorldRendererGeometry_DrawModels(self, frame->SceneFilter);

    // The model instances components' copies (B03; never crisp), lit like the models.
    WorldRendererInstances_DrawLit(self, frame, frame->SceneFilter);

    // The terrain's chunks (B01; never crisp), before the sprites: decals lie on it.
    WorldRendererTerrain_DrawLit(self);

    // Sprites, lit like the models: the cutout ones (opaque, writing depth), then the decals over the opaque world, then
    // the blended and additive ones back to front.
    WorldRendererSprites_DrawLit(self, frame->SceneFilter, true);

    // Light gizmos: a debug authoring aid so light placement is visible (lights are otherwise invisible).
    // Independent of the debug grid; a real game leaves this off so lights have no visible sphere.
    if (self->_lightGizmosEnabled)
    {
        WorldRendererGeometry_DrawLightGizmos(self);
    }

    // Collider wireframes: a debug aid (the collision world's read-only debug list), off unless a collision world is set.
    if (self->_collisionDebug != NULL)
    {
        WorldRendererCollision_Draw(self, frame);
    }

    EndMode3D();
    EndTextureMode();
}
