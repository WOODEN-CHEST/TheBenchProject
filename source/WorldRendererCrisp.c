// The crisp overlay: OmitPixelation objects rendered at window resolution and composited over the
// pixelated frame.

#include "WorldRendererInternal.h"


// Macros.
/** Asset name of the crisp-composite shader: composites the full-res, un-pixelated (OmitPixelation) objects
 *  over the pixelated frame, depth-tested against the scene so they are occluded correctly. */
#define CRISP_COMPOSITE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_crisp_composite")


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererCrisp_LoadShader(WorldRenderer* self)
{
    // Optional: without it (or a samplable-depth crisp target) OmitPixelation objects just render pixelated.
    self->_crisp._compositeShader = WorldRendererCommon_LoadShader(self, CRISP_COMPOSITE_SHADER_ASSET_NAME);
    if (self->_crisp._compositeShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_crisp._compositeShader);
        self->_crisp._locCrispDepth = GetShaderLocation(RayShader, "crispDepth");
        self->_crisp._locSceneDepth = GetShaderLocation(RayShader, "sceneDepth");
        self->_crisp._locExposure = GetShaderLocation(RayShader, "exposure");
    }
}

/* At WINDOW resolution (not the low pixelation resolution — that is the whole point): an HDR colour + SAMPLABLE
 * depth texture (the composite depth-tests the crisp objects against the scene). BILINEAR so the crisp surface
 * (e.g. a readable screen) is smoothly sampled, not pixelated. */
void WorldRendererCrisp_CreateTargets(WorldRenderer* self, int windowWidth, int windowHeight)
{
    if (self->_crisp._compositeShader == NULL)
    {
        return;
    }
    self->_crisp._target = WorldRendererCommon_TryCreateHdrTarget(windowWidth, windowHeight, true);
    self->_crisp._hasTarget = (self->_crisp._target.id != 0);
    if (!self->_crisp._hasTarget)
    {
        WorldRendererCommon_WarnOnce(self, &self->_crisp._isTargetFailureReported, (const unsigned char*)
            u8"WorldRenderer: could not create the crisp overlay target; OmitPixelation objects render pixelated.");
        return;
    }
    self->_crisp._width = windowWidth;
    self->_crisp._height = windowHeight;
    SetTextureFilter(self->_crisp._target.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(self->_crisp._target.depth, TEXTURE_FILTER_POINT);
}

void WorldRendererCrisp_ReleaseTargets(WorldRenderer* self)
{
    WorldRendererCommon_UnloadTarget(&self->_crisp._target, &self->_crisp._hasTarget);
}

/* Draws the OmitPixelation models and sprites with the scene camera and PBR lighting into the full-res HDR crisp
 * target, so they can be composited un-pixelated over the pixelated frame. Opens/closes its own passes. */
void WorldRendererCrisp_Render(WorldRenderer* self, const WorldRenderFrame* frame)
{
    BeginTextureMode(self->_crisp._target);
    ClearBackground(BLANK); // clears colour to (0,0,0,0) and depth to the far plane: "no crisp object here"
    // Re-upload the lighting (sun/ambient/fog/shadow + re-bind the shadow map) — the crisp pass runs after
    // the bloom/shaft passes, which may have changed GL texture-slot / shader state. applyScreenAo=false: this
    // is a full-res pass, so the low-res AO map (sampled by gl_FragCoord) would mis-align; crisp objects get no AO.
    // For the same reason the dither's pixel scale is window / scene: a fading crisp object dithers in scene pixels,
    // on the same grid as everything else.
    Vector2 DitherPixelScale =
    {
        (frame->SceneWidth > 0) ? ((float)frame->WindowWidth / (float)frame->SceneWidth) : 1.0f,
        (frame->SceneHeight > 0) ? ((float)frame->WindowHeight / (float)frame->SceneHeight) : 1.0f
    };
    WorldRendererLighting_UploadFrame(self, frame, false, DitherPixelScale);
    BeginMode3D(frame->RenderCamera);
    bool IsLit = (self->_pbr._shader != NULL);
    WorldRendererGeometry_DrawModels(self, IsLit ? &self->_pbr._passShader : NULL, IsLit,
        WorldRendererPixelFilter_CrispOnly);
    // Crisp sprites (OmitPixelation on cutout sprites) render full-res here too, so they stay sharp like crisp models.
    WorldRendererSprites_DrawLit(self, WorldRendererPixelFilter_CrispOnly, false);
    EndMode3D();
    EndTextureMode();
}

/* Draws the crisp HDR colour through the crisp-composite shader, which tonemaps it (matching the main tonemap,
 * via the frame's exposure) and keeps only pixels where a crisp object is the frontmost surface (depth-tested
 * against the low-res scene depth). Must run into @p target AFTER the pixelated scene has been blitted there. The
 * negative source height applies the same vertical flip the tonemap blit uses, so the crisp overlay aligns with
 * the pixelated frame already in the target. */
void WorldRendererCrisp_Composite(WorldRenderer* self, const WorldRenderFrame* frame, RenderTexture2D target)
{
    Shader Composite = GameShader_GetRaylibShader(self->_crisp._compositeShader);
    BeginTextureMode(target);
    SetShaderValue(Composite, self->_crisp._locExposure, &frame->Exposure, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(Composite);
    // Crisp colour is bound to unit 0 by DrawTexturePro; the two depth textures go to units 1/2 via
    // SetShaderValueTexture (after BeginShaderMode) so they enter Raylib's 2D batch texture table.
    SetShaderValueTexture(Composite, self->_crisp._locCrispDepth, self->_crisp._target.depth);
    SetShaderValueTexture(Composite, self->_crisp._locSceneDepth, self->_scene._target.depth);
    Rectangle Source =
    {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)self->_crisp._target.texture.width,
        .height = -(float)self->_crisp._target.texture.height
    };
    Rectangle Destination = { .x = 0.0f, .y = 0.0f, .width = (float)frame->WindowWidth,
        .height = (float)frame->WindowHeight };
    DrawTexturePro(self->_crisp._target.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
}
