// Post-processing passes: outlines, bloom, sun shafts and the tonemap.

#include <math.h>
#include "WorldRendererInternal.h"
#include "raylib/raymath.h"


// Macros.
/** Asset name of the tonemap post-pass shader (maps the linear-HDR scene to displayable sRGB). */
#define TONEMAP_SHADER_ASSET_NAME ((const unsigned char*)u8"world_tonemap")
/** Asset names of the two bloom shaders: a bright-pass/downsample prefilter and a separable Gaussian blur. */
#define BLOOM_PREFILTER_SHADER_ASSET_NAME ((const unsigned char*)u8"world_bloom_prefilter")
#define BLOOM_BLUR_SHADER_ASSET_NAME ((const unsigned char*)u8"world_bloom_blur")
/** Asset name of the screen-space sun-shafts (god rays) shader. */
#define SUNSHAFT_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sunshafts")
/** Asset name of the outline post-process shader (depth/normal-edge silhouettes and creases). */
#define POSTFX_SHADER_ASSET_NAME ((const unsigned char*)u8"world_postfx")
/** Built-in outline strength (multiplied by nothing config-side per spec: outlines are code-configurable).
 *  0..1; scales how strongly the depth/normal edge detection recolours silhouettes and creases. */
#define OUTLINE_BASE_STRENGTH 1.0f
/** Divisor on the scene resolution for the bloom buffers (2 = half res). Bloom is a soft glow, so a lower-res
 *  buffer is cheaper and looks the same; the blur taps rely on BILINEAR filtering of these buffers. */
#define BLOOM_RESOLUTION_DIVISOR 2
/** Number of horizontal+vertical blur iterations; more = a wider, softer glow (at more passes). */
#define BLOOM_BLUR_ITERATIONS 2
/** Linear-HDR brightness (max channel) above which a pixel starts to bloom, and the soft-knee width [0,1]. */
#define BLOOM_THRESHOLD 1.0f
#define BLOOM_SOFT_KNEE 0.5f
/** Divisor on the scene resolution for the sun-shaft buffer (2 = half res). Shafts are soft, so a lower-res
 *  radial blur is cheaper and looks the same; the buffer is BILINEAR for a smooth upsample in the tonemap. */
#define SUNSHAFT_RESOLUTION_DIVISOR 2
/** Minimum sun elevation (sunDir.y) for shafts: below this the sun is too low/set and shafts are skipped, so
 *  they do not fire at night and stay gentle right at the horizon. */
#define SUNSHAFT_MIN_ELEVATION 0.05f
/** How far outside the [0,1] screen box the sun may sit and still cast shafts (rays from a just-off-screen sun
 *  still enter the frame); beyond this margin the pass is skipped. The shaft strength fades smoothly to 0 as
 *  the sun crosses this margin (rather than a hard cutoff) so turning the camera away dims the shafts gradually
 *  instead of making them pop off. */
#define SUNSHAFT_SCREEN_MARGIN 0.35f
/** Elevation band (above SUNSHAFT_MIN_ELEVATION) over which the shafts fade in, so they also ease off gently at
 *  the horizon instead of cutting out abruptly. */
#define SUNSHAFT_ELEVATION_FADE_BAND 0.15f


// Static functions.
/* Loads the shaders (tonemap, bloom prefilter + blur, sun shafts) the HDR composite needs, caching their uniform
 * locations. Each is optional: a missing one only switches its own effect off. */
static void LoadCompositeShaders(WorldRenderer* self)
{
    self->_tonemap._shader = WorldRendererCommon_LoadShader(self, TONEMAP_SHADER_ASSET_NAME);
    if (self->_tonemap._shader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_tonemap._shader);
        self->_tonemap._locExposure = GetShaderLocation(RayShader, "exposure");
        self->_tonemap._locBloomTexture = GetShaderLocation(RayShader, "bloomTexture");
        self->_tonemap._locBloomStrength = GetShaderLocation(RayShader, "bloomStrength");
        self->_tonemap._locSunshaftTexture = GetShaderLocation(RayShader, "sunshaftTexture");
        self->_tonemap._locSunshaftStrength = GetShaderLocation(RayShader, "sunshaftStrength");
    }

    self->_bloom._prefilterShader = WorldRendererCommon_LoadShader(self, BLOOM_PREFILTER_SHADER_ASSET_NAME);
    if (self->_bloom._prefilterShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_bloom._prefilterShader);
        self->_bloom._prefilterLocResolution = GetShaderLocation(RayShader, "resolution");
        self->_bloom._prefilterLocThreshold = GetShaderLocation(RayShader, "threshold");
        self->_bloom._prefilterLocSoftKnee = GetShaderLocation(RayShader, "softKnee");
        self->_bloom._prefilterLocDepthTexture = GetShaderLocation(RayShader, "depthTexture");
        self->_bloom._prefilterLocDepthMask = GetShaderLocation(RayShader, "depthMask");
    }
    self->_bloom._blurShader = WorldRendererCommon_LoadShader(self, BLOOM_BLUR_SHADER_ASSET_NAME);
    if (self->_bloom._blurShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_bloom._blurShader);
        self->_bloom._blurLocResolution = GetShaderLocation(RayShader, "resolution");
        self->_bloom._blurLocDirection = GetShaderLocation(RayShader, "direction");
    }

    self->_sunshaft._shader = WorldRendererCommon_LoadShader(self, SUNSHAFT_SHADER_ASSET_NAME);
    if (self->_sunshaft._shader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_sunshaft._shader);
        self->_sunshaft._locResolution = GetShaderLocation(RayShader, "resolution");
        self->_sunshaft._locDepthTexture = GetShaderLocation(RayShader, "depthTexture");
        self->_sunshaft._locSunScreenPos = GetShaderLocation(RayShader, "sunScreenPos");
    }
}

/* Creates the two bloom ping-pong buffers (colour-only HDR, reduced resolution). BILINEAR filtering is required:
 * the blur's fractional-texel taps and the tonemap's bloom upsample both rely on it (the smooth glow is
 * intentionally NOT the pixelated look). */
static void CreateBloomTargets(WorldRenderer* self, int sceneWidth, int sceneHeight)
{
    int BloomWidth = sceneWidth / BLOOM_RESOLUTION_DIVISOR;
    int BloomHeight = sceneHeight / BLOOM_RESOLUTION_DIVISOR;
    if (BloomWidth < 1) { BloomWidth = 1; }
    if (BloomHeight < 1) { BloomHeight = 1; }
    self->_bloom._targetA = WorldRendererCommon_CreateColorTarget(BloomWidth, BloomHeight);
    self->_bloom._targetB = WorldRendererCommon_CreateColorTarget(BloomWidth, BloomHeight);
    self->_bloom._hasTargets = (self->_bloom._targetA.id != 0) && (self->_bloom._targetB.id != 0);
    if (!self->_bloom._hasTargets)
    {
        // Partial creation: release whichever succeeded so nothing leaks and the pass cleanly skips.
        if (self->_bloom._targetA.id != 0) { UnloadRenderTexture(self->_bloom._targetA); }
        if (self->_bloom._targetB.id != 0) { UnloadRenderTexture(self->_bloom._targetB); }
        self->_bloom._targetA = (RenderTexture2D){ 0 };
        self->_bloom._targetB = (RenderTexture2D){ 0 };
        WorldRendererCommon_WarnOnce(self, &self->_bloom._isTargetFailureReported,
            (const unsigned char*)u8"WorldRenderer: could not create the bloom buffers; bloom is off.");
        return;
    }

    self->_bloom._width = BloomWidth;
    self->_bloom._height = BloomHeight;
    SetTextureFilter(self->_bloom._targetA.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(self->_bloom._targetB.texture, TEXTURE_FILTER_BILINEAR);
    // CLAMP wrap so edge taps (blur + the tonemap's bilinear upsample) don't wrap a bright glow to the opposite
    // screen edge (the emissive-coloured edge lines). raylib RTs default to REPEAT.
    SetTextureWrap(self->_bloom._targetA.texture, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(self->_bloom._targetB.texture, TEXTURE_WRAP_CLAMP);
}

/* Creates the sun-shaft buffer (colour-only HDR, reduced resolution, BILINEAR). */
static void CreateSunshaftTarget(WorldRenderer* self, int sceneWidth, int sceneHeight)
{
    int ShaftWidth = sceneWidth / SUNSHAFT_RESOLUTION_DIVISOR;
    int ShaftHeight = sceneHeight / SUNSHAFT_RESOLUTION_DIVISOR;
    if (ShaftWidth < 1) { ShaftWidth = 1; }
    if (ShaftHeight < 1) { ShaftHeight = 1; }
    self->_sunshaft._target = WorldRendererCommon_CreateColorTarget(ShaftWidth, ShaftHeight);
    self->_sunshaft._hasTarget = (self->_sunshaft._target.id != 0);
    if (!self->_sunshaft._hasTarget)
    {
        WorldRendererCommon_WarnOnce(self, &self->_sunshaft._isTargetFailureReported,
            (const unsigned char*)u8"WorldRenderer: could not create the sun-shaft buffer; sun shafts are off.");
        return;
    }
    self->_sunshaft._width = ShaftWidth;
    self->_sunshaft._height = ShaftHeight;
    SetTextureFilter(self->_sunshaft._target.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(self->_sunshaft._target.texture, TEXTURE_WRAP_CLAMP); // no opposite-edge wrap on upsample
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererPost_LoadShaders(WorldRenderer* self)
{
    LoadCompositeShaders(self);

    // Outline shader. Optional: without it the scene goes straight to the tonemap. It also needs the
    // samplable-depth scene target and the G-buffer (checked at use time).
    self->_outline._shader = WorldRendererCommon_LoadShader(self, POSTFX_SHADER_ASSET_NAME);
    if (self->_outline._shader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_outline._shader);
        self->_outline._locResolution = GetShaderLocation(RayShader, "resolution");
        self->_outline._locInvProjection = GetShaderLocation(RayShader, "invProjection");
        self->_outline._locProjection = GetShaderLocation(RayShader, "projection");
        self->_outline._locDepthTexture = GetShaderLocation(RayShader, "depthTexture");
        self->_outline._locNormalTexture = GetShaderLocation(RayShader, "normalTexture");
        self->_outline._locSunDirectionView = GetShaderLocation(RayShader, "sunDirectionView");
        self->_outline._locStrength = GetShaderLocation(RayShader, "outlineStrength");
        self->_outline._locFogDensity = GetShaderLocation(RayShader, "fogDensity");
    }
}

void WorldRendererPost_CreateTargets(WorldRenderer* self, int width, int height)
{
    // Outline target (colour-only HDR, same size as the scene). Only when the outline shader is present and the
    // scene depth is samplable (the outline pass reconstructs view positions from it).
    if ((self->_outline._shader != NULL) && self->_scene._isDepthSamplable)
    {
        self->_outline._target = WorldRendererCommon_CreateColorTarget(width, height);
        self->_outline._hasTarget = (self->_outline._target.id != 0);
        if (self->_outline._hasTarget)
        {
            SetTextureFilter(self->_outline._target.texture, TEXTURE_FILTER_POINT);
        }
        else
        {
            WorldRendererCommon_WarnOnce(self, &self->_outline._isTargetFailureReported,
                (const unsigned char*)u8"WorldRenderer: could not create the outline target; outlines are off.");
        }
    }

    if ((self->_bloom._prefilterShader != NULL) && (self->_bloom._blurShader != NULL))
    {
        CreateBloomTargets(self, width, height);
    }
    if (self->_sunshaft._shader != NULL)
    {
        CreateSunshaftTarget(self, width, height);
    }
}

void WorldRendererPost_ReleaseTargets(WorldRenderer* self)
{
    WorldRendererCommon_UnloadTarget(&self->_outline._target, &self->_outline._hasTarget);
    if (self->_bloom._hasTargets)
    {
        UnloadRenderTexture(self->_bloom._targetA);
        UnloadRenderTexture(self->_bloom._targetB);
        self->_bloom._hasTargets = false;
    }
    WorldRendererCommon_UnloadTarget(&self->_sunshaft._target, &self->_sunshaft._hasTarget);
}

/* Reads the scene colour and the G-buffer (its normals, flags and depth), using the scene's projection to turn depth
 * back into view positions. Outputs linear HDR into the outline target; the tonemap pass maps it afterwards. */
void WorldRendererPost_RenderOutlines(WorldRenderer* self, const WorldRenderFrame* frame)
{
    Shader RayShader = GameShader_GetRaylibShader(self->_outline._shader);

    float Resolution[2] = { (float)frame->SceneWidth, (float)frame->SceneHeight };
    SetShaderValue(RayShader, self->_outline._locResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValueMatrix(RayShader, self->_outline._locProjection, frame->Projection);
    SetShaderValueMatrix(RayShader, self->_outline._locInvProjection, frame->InvProjection);

    // Sun direction in VIEW space for the sun-aware crease colouring. The G-buffer stores view-space normals,
    // so turning the sun's direction by the same view makes the shader's sun test a plain dot product. In render space
    // the view is a pure rotation (no translation to drop).
    // TODO: on moonlit nights the moon is the key light, so the creases could follow the frame's shadow light; that
    // needs a smooth hand-over (the switch would flip which side of a crease darkens), so they stay on the sun for now.
    Vector3 ViewSun = Vector3Normalize(Vector3Transform(frame->SunDirection, frame->View));
    float SunDirView[3] = { ViewSun.x, ViewSun.y, ViewSun.z };
    SetShaderValue(RayShader, self->_outline._locSunDirectionView, SunDirView, SHADER_UNIFORM_VEC3);

    // Outlines are code-configurable (not JSON) per spec: a single renderer toggle + a built-in strength.
    float OutlineStrength = self->_outline._isEnabled ? OUTLINE_BASE_STRENGTH : 0.0f;
    SetShaderValue(RayShader, self->_outline._locStrength, &OutlineStrength, SHADER_UNIFORM_FLOAT);

    // The fog's density, so a line fades with the fog as the surface under it does (T34).
    SetShaderValue(RayShader, self->_outline._locFogDensity, &frame->FogDensity, SHADER_UNIFORM_FLOAT);

    BeginTextureMode(self->_outline._target);
    ClearBackground(BLACK);
    BeginShaderMode(RayShader);
    // Bind the scene depth + normal G-buffer as extra sampler2Ds. This MUST use SetShaderValueTexture (not
    // manual rlActiveTextureSlot/rlEnableTexture): DrawTexturePro draws through Raylib's 2D batch, and the
    // batch only binds textures it tracks in its own activeTextureId[] table (which SetShaderValueTexture
    // populates) plus texture0. A manual glActiveTexture bind is NOT in that table, so the batch would leave
    // these samplers on unit 0 = the scene colour texture, making the shader read colour as depth+normal (the
    // sky-noise/ring bug). Call these AFTER BeginShaderMode (its batch flush clears the table), before the draw.
    // The depth is the G-buffer's own, which matches its normals and flags: the G-buffer draws a fading object solid
    // (until half its fade) where the scene's depth has the dither's holes, and those holes would read as silhouette
    // steps all over the object. Without a samplable G-buffer depth the scene's is the fallback (fading objects then get
    // noisy outlines until half their fade).
    Texture2D Depth = self->_gbuffer._isDepthSamplable ? self->_gbuffer._target.depth : self->_scene._target.depth;
    SetShaderValueTexture(RayShader, self->_outline._locDepthTexture, Depth);
    SetShaderValueTexture(RayShader, self->_outline._locNormalTexture, self->_gbuffer._target.texture);

    // The shader samples by gl_FragCoord (not fragTexCoord), so this straight full-screen copy needs no flip.
    // DrawTexturePro binds the scene colour to unit 0 (texture0); the batch binds depth/normal to units 1/2 and
    // resets the table after drawing, so nothing lingers on those units into the next pass.
    Rectangle Source = { 0.0f, 0.0f, (float)self->_scene._target.texture.width,
        (float)self->_scene._target.texture.height };
    Rectangle Destination = { 0.0f, 0.0f, (float)frame->SceneWidth, (float)frame->SceneHeight };
    DrawTexturePro(self->_scene._target.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
}

/* Each sub-pass opens/closes its own texture + shader modes. The bloom shaders sample by gl_FragCoord/resolution,
 * so a positive-height full copy keeps every bloom buffer in the scene's orientation (the tonemap then samples
 * the bloom with the same fragTexCoord it uses for the scene, so the glow lines up). */
void WorldRendererPost_RenderBloom(WorldRenderer* self, RenderTexture2D source)
{
    float BloomResolution[2] = { (float)self->_bloom._width, (float)self->_bloom._height };
    Rectangle FullDest = { 0.0f, 0.0f, (float)self->_bloom._width, (float)self->_bloom._height };
    Rectangle BloomSourceRect = { 0.0f, 0.0f, (float)self->_bloom._width, (float)self->_bloom._height };

    // Bright-pass + downsample the source into bloom buffer A. The sky (sun disc + stars) is excluded from bloom
    // by masking against the scene depth (sky wrote no depth → sits at the far plane), so only objects glow —
    // but only when the scene depth is a samplable texture; otherwise everything blooms (graceful fallback).
    Shader Prefilter = GameShader_GetRaylibShader(self->_bloom._prefilterShader);
    float Threshold = BLOOM_THRESHOLD;
    float SoftKnee = BLOOM_SOFT_KNEE;
    float DepthMask = self->_scene._isDepthSamplable ? 1.0f : 0.0f;
    Rectangle SourceRect = { 0.0f, 0.0f, (float)source.texture.width, (float)source.texture.height };
    BeginTextureMode(self->_bloom._targetA);
    ClearBackground(BLANK);
    SetShaderValue(Prefilter, self->_bloom._prefilterLocResolution, BloomResolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(Prefilter, self->_bloom._prefilterLocThreshold, &Threshold, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Prefilter, self->_bloom._prefilterLocSoftKnee, &SoftKnee, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Prefilter, self->_bloom._prefilterLocDepthMask, &DepthMask, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(Prefilter);
    // Bind the scene depth (unit 1) via SetShaderValueTexture AFTER BeginShaderMode so it enters Raylib's 2D
    // batch texture table (a manual slot bind would leave the sampler on unit 0 = the scene colour). Only when
    // it will actually be sampled (depthMask on), so a disabled mask never reads an unbound sampler.
    if (self->_scene._isDepthSamplable)
    {
        SetShaderValueTexture(Prefilter, self->_bloom._prefilterLocDepthTexture, self->_scene._target.depth);
    }
    DrawTexturePro(source.texture, SourceRect, FullDest, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();

    // Separable Gaussian blur, ping-ponging A<->B: horizontal (A->B) then vertical (B->A) per iteration.
    Shader Blur = GameShader_GetRaylibShader(self->_bloom._blurShader);
    for (int Iteration = 0; Iteration < BLOOM_BLUR_ITERATIONS; Iteration++)
    {
        float DirectionH[2] = { 1.0f, 0.0f };
        BeginTextureMode(self->_bloom._targetB);
        ClearBackground(BLANK);
        SetShaderValue(Blur, self->_bloom._blurLocResolution, BloomResolution, SHADER_UNIFORM_VEC2);
        SetShaderValue(Blur, self->_bloom._blurLocDirection, DirectionH, SHADER_UNIFORM_VEC2);
        BeginShaderMode(Blur);
        DrawTexturePro(self->_bloom._targetA.texture, BloomSourceRect, FullDest, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
        EndShaderMode();
        EndTextureMode();

        float DirectionV[2] = { 0.0f, 1.0f };
        BeginTextureMode(self->_bloom._targetA);
        ClearBackground(BLANK);
        SetShaderValue(Blur, self->_bloom._blurLocResolution, BloomResolution, SHADER_UNIFORM_VEC2);
        SetShaderValue(Blur, self->_bloom._blurLocDirection, DirectionV, SHADER_UNIFORM_VEC2);
        BeginShaderMode(Blur);
        DrawTexturePro(self->_bloom._targetB.texture, BloomSourceRect, FullDest, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
        EndShaderMode();
        EndTextureMode();
    }
}

/* Uses the scene's view + projection (so the UV lines up with the shaft buffer, which samples by
 * gl_FragCoord/resolution → y up, matching the NDC->UV here). */
bool WorldRendererPost_ComputeSunScreenUv(const WorldRenderFrame* frame, Vector2* outUv, float* outFade)
{
    Vector3 SunDirection = frame->SunDirection;
    if (SunDirection.y < SUNSHAFT_MIN_ELEVATION)
    {
        return false; // sun set / too low
    }

    // The sun is a point at infinity: its direction projected with w = 0 (render space -> clip, the inverse of the sky's
    // invViewProj), so the camera's position plays no part.
    const Matrix* ViewProj = &frame->ViewProjection;
    float ClipX = ViewProj->m0*SunDirection.x + ViewProj->m4*SunDirection.y + ViewProj->m8*SunDirection.z;
    float ClipY = ViewProj->m1*SunDirection.x + ViewProj->m5*SunDirection.y + ViewProj->m9*SunDirection.z;
    float ClipW = ViewProj->m3*SunDirection.x + ViewProj->m7*SunDirection.y + ViewProj->m11*SunDirection.z;
    if (ClipW <= 1e-4f)
    {
        return false; // sun behind the camera
    }

    float UvX = (ClipX / ClipW)*0.5f + 0.5f;
    float UvY = (ClipY / ClipW)*0.5f + 0.5f;

    // How far outside the [0,1] box the sun sits (0 while on-screen). Fade linearly to 0 across the margin so
    // the shafts dim smoothly as the camera turns away, instead of the pass cutting out at the box edge.
    float OutsideX = fmaxf(fmaxf(-UvX, UvX - 1.0f), 0.0f);
    float OutsideY = fmaxf(fmaxf(-UvY, UvY - 1.0f), 0.0f);
    float Outside = fmaxf(OutsideX, OutsideY);
    float MarginFade = 1.0f - (Outside / SUNSHAFT_SCREEN_MARGIN);
    // Ease the shafts in over a small band above the minimum elevation so they also fade gently at the horizon.
    float ElevationFade = (SunDirection.y - SUNSHAFT_MIN_ELEVATION) / SUNSHAFT_ELEVATION_FADE_BAND;
    if (ElevationFade > 1.0f) { ElevationFade = 1.0f; }

    float Fade = MarginFade * ElevationFade;
    if (Fade <= 0.0f)
    {
        return false; // sun too far off-screen / too low for its shafts to reach the frame
    }

    outUv->x = UvX;
    outUv->y = UvY;
    *outFade = Fade;
    return true;
}

/* Accumulates the unoccluded sky's radiance (in its own sky colour) along rays toward the sun into the sun-shaft
 * buffer; the tonemap adds it in HDR. Reads @p source's colour and the scene depth (for occlusion). */
void WorldRendererPost_RenderSunshafts(WorldRenderer* self, const WorldRenderFrame* frame, RenderTexture2D source)
{
    Shader Shaft = GameShader_GetRaylibShader(self->_sunshaft._shader);
    float Resolution[2] = { (float)self->_sunshaft._width, (float)self->_sunshaft._height };
    float SunPos[2] = { frame->SunScreenUv.x, frame->SunScreenUv.y };
    Rectangle SourceRect = { 0.0f, 0.0f, (float)source.texture.width, (float)source.texture.height };
    Rectangle FullDest = { 0.0f, 0.0f, (float)self->_sunshaft._width, (float)self->_sunshaft._height };

    BeginTextureMode(self->_sunshaft._target);
    ClearBackground(BLANK);
    SetShaderValue(Shaft, self->_sunshaft._locResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(Shaft, self->_sunshaft._locSunScreenPos, SunPos, SHADER_UNIFORM_VEC2);
    BeginShaderMode(Shaft);
    // Bind the scene depth (unit 1) via SetShaderValueTexture AFTER BeginShaderMode (batch texture table), like
    // the outline/bloom passes; the source colour is bound to unit 0 by DrawTexturePro.
    SetShaderValueTexture(Shaft, self->_sunshaft._locDepthTexture, self->_scene._target.depth);
    DrawTexturePro(source.texture, SourceRect, FullDest, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
}

/* A negative source height applies the vertical flip Raylib render textures need (the same convention the frame
 * manager uses when compositing), and point filtering on the scene texture makes the upscale produce hard square
 * pixels. Without the tonemap shader the raw (untonemapped) HDR is blitted so the scene still shows. */
void WorldRendererPost_Tonemap(WorldRenderer* self, const WorldRenderFrame* frame, RenderTexture2D source,
    RenderTexture2D target)
{
    BeginTextureMode(target);
    ClearBackground(BLACK);
    Rectangle Source = { .x = 0.0f, .y = 0.0f, .width = (float)source.texture.width,
        .height = -(float)source.texture.height };
    Rectangle Destination = { .x = 0.0f, .y = 0.0f, .width = (float)frame->WindowWidth,
        .height = (float)frame->WindowHeight };
    if (self->_tonemap._shader != NULL)
    {
        Shader TonemapRay = GameShader_GetRaylibShader(self->_tonemap._shader);
        SetShaderValue(TonemapRay, self->_tonemap._locExposure, &frame->Exposure, SHADER_UNIFORM_FLOAT);
        // Bloom + sun-shaft strengths (0 when inactive → the shader skips sampling that texture, so it need not
        // bind). Both are added in linear HDR before the tonemap.
        float BloomStrength = frame->IsBloomActive ? frame->BloomStrength : 0.0f;
        SetShaderValue(TonemapRay, self->_tonemap._locBloomStrength, &BloomStrength, SHADER_UNIFORM_FLOAT);
        float SunshaftStrength = frame->AreSunshaftsActive ? frame->SunshaftStrength : 0.0f;
        SetShaderValue(TonemapRay, self->_tonemap._locSunshaftStrength, &SunshaftStrength, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(TonemapRay);
        // Bind the extra samplers (bloom unit 1, sun shafts unit 2) AFTER BeginShaderMode, via
        // SetShaderValueTexture so they enter Raylib's 2D batch texture table (the manual-slot idiom would leave
        // them on unit 0 = scene).
        if (frame->IsBloomActive)
        {
            SetShaderValueTexture(TonemapRay, self->_tonemap._locBloomTexture, self->_bloom._targetA.texture);
        }
        if (frame->AreSunshaftsActive)
        {
            SetShaderValueTexture(TonemapRay, self->_tonemap._locSunshaftTexture, self->_sunshaft._target.texture);
        }
    }
    DrawTexturePro(source.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    if (self->_tonemap._shader != NULL)
    {
        EndShaderMode();
    }
    EndTextureMode();
}
