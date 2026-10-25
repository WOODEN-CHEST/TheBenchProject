// The normal/flag G-buffer pass and the screen-space ambient-occlusion pre-pass.

#include "WorldRendererInternal.h"
#include "raylib/rlgl.h"


// Macros.
/** Asset name of the normal/mask G-buffer shader: writes a view-space normal (RGB) + surface/outline flag (A)
 *  that the outline pass edge-detects and the AO pass reads. */
#define NORMAL_SHADER_ASSET_NAME ((const unsigned char*)u8"world_normal")
/** Asset name of its skinned twin (T29): the same fragment stage over a vertex stage that skins. */
#define NORMAL_SKINNED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_normal_skinned")
/** Asset name of the sprite normal/mask shader: writes cutout sprites into the G-buffer with an alpha test at their
 *  cutoff (so the outline follows the sprite's shape, not its quad's rectangle) and their quad's view normal. */
#define NORMAL_SPRITE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_normal_sprite")
/** Asset name of the ambient-occlusion pre-pass shader: computes SSAO from the G-buffer's depth + flag into an
 *  AO-multiplier texture that the PBR scene pass applies to ONLY its ambient term. */
#define AO_SHADER_ASSET_NAME ((const unsigned char*)u8"world_ao")


// Static functions.
/* Creates the normal/flag G-buffer target: an 8-bit RGBA colour texture + a SAMPLABLE depth texture (so the AO
 * pre-pass can reconstruct view positions from it). Sets *outDepthSamplable true on success. Falls back to a
 * plain LoadRenderTexture (renderbuffer depth, not samplable → AO disabled, outlines still work) if the
 * samplable-depth framebuffer cannot be made complete. Returns a zero target only if even the fallback fails. */
static RenderTexture2D TryCreateNormalTarget(int width, int height, bool* outDepthSamplable)
{
    *outDepthSamplable = false;

    RenderTexture2D Target = { 0 };
    Target.id = rlLoadFramebuffer();
    if (Target.id != 0)
    {
        rlEnableFramebuffer(Target.id);

        Target.texture.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        Target.texture.width = width;
        Target.texture.height = height;
        Target.texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        Target.texture.mipmaps = 1;

        Target.depth.id = rlLoadTextureDepth(width, height, false); // false => a samplable depth TEXTURE
        Target.depth.width = width;
        Target.depth.height = height;
        Target.depth.format = SCENE_DEPTH_FORMAT;
        Target.depth.mipmaps = 1;

        rlFramebufferAttach(Target.id, Target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(Target.id, Target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);

        bool Complete = (Target.texture.id != 0) && rlFramebufferComplete(Target.id);
        rlDisableFramebuffer();
        if (Complete)
        {
            *outDepthSamplable = true;
            return Target;
        }
        UnloadRenderTexture(Target);
    }

    // Fallback: a plain render texture (renderbuffer depth). Outlines still work (they read the SCENE depth);
    // the AO pre-pass, which needs this target's depth samplable, stays off.
    return LoadRenderTexture(width, height);
}

/* Whether the G-buffer draws an entry with this fade: solid below half its fade, not at all from there on. It never
 * dithers: dither holes would break its outline into noise and speckle the AO, so those switch off once, at mid-fade. */
static bool IsSolidInGBuffer(float fade)
{
    return fade < WORLD_VISIBILITY_GBUFFER_FADE_LIMIT;
}

/* Draws the camera's models (passing @p filter, and solid enough: IsSolidInGBuffer) into the G-buffer through the
 * normal shader, setting the per-object outline flag uniform (1 when the object has outlines enabled, else 0) before
 * each draw. A model mid LOD cross-fade is drawn once, solid, as the LOD with the larger share (the same rule as the
 * fade's: no dither holes in the G-buffer). The normal shader writes RGB=view-space normal, A=surface/outline flag
 * (0.5 = surface, 1.0 = surface with outline); pixels the sky/grid leave untouched stay at the cleared 0 (not a
 * surface). */
static void DrawNormalBufferModels(WorldRenderer* self, WorldRendererPixelFilter filter)
{
    Shader NormalShader = GameShader_GetRaylibShader(self->_gbuffer._shader);
    bool HasSkinned = (self->_gbuffer._skinnedShader != NULL);
    Shader SkinnedShader = HasSkinned ? GameShader_GetRaylibShader(self->_gbuffer._skinnedShader) : NormalShader;
    WorldRendererMeshShaders Shaders = { .Rigid = &NormalShader, .Skinned = HasSkinned ? &SkinnedShader : NULL,
        .Palette = &self->_gbuffer._skinnedPalette, .IsCameraView = true };
    size_t ModelCount = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    size_t VisibleCount = 0;
    const size_t* Visible = WorldVisibility_GetModels(&self->_visibility, &self->_cache, WorldVisibilityPass_Camera,
        &VisibleCount);
    for (size_t Position = 0; Position < VisibleCount; Position++)
    {
        size_t Index = Visible[Position];
        const WorldRenderModel* Entry = &Models[Index];
        if (!WorldRendererGeometry_PassesFilter(Entry->OmitsPixelation, filter)
            || !IsSolidInGBuffer(WorldVisibility_GetModelFade(&self->_visibility, Index).Combined))
        {
            continue;
        }
        float OutlineFlag = Entry->HasOutline ? 1.0f : 0.0f;
        SetShaderValue(NormalShader, self->_gbuffer._locOutlineFlag, &OutlineFlag, SHADER_UNIFORM_FLOAT);
        if (HasSkinned && (Entry->CameraPose != NULL))
        {
            SetShaderValue(SkinnedShader, self->_gbuffer._skinnedLocOutlineFlag, &OutlineFlag, SHADER_UNIFORM_FLOAT);
        }
        WorldRendererGeometry_DrawModel(self, Entry, WorldLodState_GetSolidLod(&Entry->Lod), &Shaders, false);
    }
}

/* Draws the camera's cutout sprites that are not decals (passing @p filter, and solid enough: IsSolidInGBuffer) into the
 * G-buffer through the sprite normal shader, facing the camera as in the scene pass, so they get outlines and AO too.
 * The shader discards texels below each sprite's cutoff (the outline hugs the sprite's shape) and takes its outline
 * flag. Decals and blended or additive sprites stay out: they are not surfaces of their own. No-op without the shader or
 * without sprites. Must run inside the G-buffer pass's BeginMode3D (colour blending disabled by the caller). */
static void DrawNormalBufferSprites(WorldRenderer* self, WorldRendererPixelFilter filter)
{
    if ((self->_gbuffer._spriteShader == NULL) || !WorldRendererSprites_IsAvailable(self))
    {
        return;
    }
    Shader SpriteShader = GameShader_GetRaylibShader(self->_gbuffer._spriteShader);
    size_t SpriteCount = 0;
    const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(&self->_cache, &SpriteCount);
    size_t VisibleCount = 0;
    const size_t* Visible = WorldVisibility_GetSprites(&self->_visibility, &self->_cache, WorldVisibilityPass_Camera,
        &VisibleCount);
    rlDisableBackfaceCulling(); // two-sided quads, mirrored by flips
    for (size_t Position = 0; Position < VisibleCount; Position++)
    {
        size_t Index = Visible[Position];
        const WorldRenderSprite* Entry = &Sprites[Index];
        const WorldRendererSpriteFrame* Matrices = WorldRendererSprites_GetFrame(self, Index);
        if ((Matrices == NULL) || !WorldRenderSprite_IsOpaqueSurface(Entry)
            || !WorldRendererGeometry_PassesFilter(Entry->OmitsPixelation, filter)
            || !IsSolidInGBuffer(WorldVisibility_GetSpriteFade(&self->_visibility, Index).Combined))
        {
            continue;
        }
        float OutlineFlag = Entry->HasOutline ? 1.0f : 0.0f;
        SetShaderValue(SpriteShader, self->_gbuffer._spriteLocOutlineFlag, &OutlineFlag, SHADER_UNIFORM_FLOAT);
        SetShaderValue(SpriteShader, self->_gbuffer._spriteLocUvRect, Entry->UvRect, SHADER_UNIFORM_VEC4);
        SetShaderValue(SpriteShader, self->_gbuffer._spriteLocAlphaCutoff, &Entry->AlphaCutoff, SHADER_UNIFORM_FLOAT);
        // Tint WHITE: the G-buffer only reads the frame's alpha (for the cutout), not its colour.
        WorldRendererSprites_DrawQuad(self, SpriteShader, Entry, WHITE, Matrices->CameraTransform);
    }
    rlEnableBackfaceCulling();
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererGBuffer_LoadShaders(WorldRenderer* self)
{
    // Sprite normal/mask shader. Optional: without it sprites are simply not drawn into the G-buffer, so they
    // get no outlines and no AO of their own.
    self->_gbuffer._spriteShader = WorldRendererCommon_LoadShader(self, NORMAL_SPRITE_SHADER_ASSET_NAME);
    if (self->_gbuffer._spriteShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_gbuffer._spriteShader);
        self->_gbuffer._spriteLocOutlineFlag = GetShaderLocation(RayShader, "outlineFlag");
        self->_gbuffer._spriteLocUvRect = GetShaderLocation(RayShader, "uvRect");
        self->_gbuffer._spriteLocAlphaCutoff = GetShaderLocation(RayShader, "alphaCutoff");
        WorldRendererCommon_PrepareDither(RayShader, &self->_gbuffer._spriteDither); // stays solid (IsSolidInGBuffer)
    }

    // Model normal/mask shader: without it neither outlines nor AO can run (both read the G-buffer).
    self->_gbuffer._shader = WorldRendererCommon_LoadShader(self, NORMAL_SHADER_ASSET_NAME);
    if (self->_gbuffer._shader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_gbuffer._shader);
        self->_gbuffer._locOutlineFlag = GetShaderLocation(RayShader, "outlineFlag");
        WorldRendererCommon_PrepareDither(RayShader, &self->_gbuffer._dither); // stays solid (IsSolidInGBuffer)
    }

    // Its skinned twin (T29). Optional: without it skinned meshes draw their bind pose into the G-buffer.
    self->_gbuffer._skinnedShader = (self->_gbuffer._shader != NULL)
        ? WorldRendererCommon_LoadShader(self, NORMAL_SKINNED_SHADER_ASSET_NAME) : NULL;
    self->_gbuffer._skinnedPalette = (WorldRendererPalette){ ._location = -1 };
    if (self->_gbuffer._skinnedShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_gbuffer._skinnedShader);
        self->_gbuffer._skinnedLocOutlineFlag = GetShaderLocation(RayShader, "outlineFlag");
        WorldRendererCommon_PrepareDither(RayShader, &self->_gbuffer._skinnedDither); // stays solid like its twin
        WorldRendererCommon_PreparePalette(RayShader, &self->_gbuffer._skinnedPalette);
    }

    // AO pre-pass shader. Optional (without it, or without a samplable-depth G-buffer, AO is simply off).
    self->_ao._shader = WorldRendererCommon_LoadShader(self, AO_SHADER_ASSET_NAME);
    if (self->_ao._shader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_ao._shader);
        self->_ao._locResolution = GetShaderLocation(RayShader, "resolution");
        self->_ao._locInvProjection = GetShaderLocation(RayShader, "invProjection");
        self->_ao._locProjection = GetShaderLocation(RayShader, "projection");
        self->_ao._locDepthTexture = GetShaderLocation(RayShader, "depthTexture");
        self->_ao._locNormalTexture = GetShaderLocation(RayShader, "normalTexture");
        self->_ao._locStrength = GetShaderLocation(RayShader, "aoStrength");
    }
}

void WorldRendererGBuffer_CreateTargets(WorldRenderer* self, int width, int height)
{
    // The G-buffer is read by BOTH the outline pass AND the AO pre-pass. 8-bit RGBA colour (encoded normal +
    // flag, not HDR) + a SAMPLABLE depth texture so the AO pass can reconstruct view positions from it; its own
    // depth also lets the nearest model's normal win per pixel. Made when its shader is present, the scene has
    // samplable depth, and at least one consumer (outlines / AO) exists.
    if ((self->_gbuffer._shader != NULL) && self->_scene._isDepthSamplable
        && ((self->_outline._shader != NULL) || (self->_ao._shader != NULL)))
    {
        self->_gbuffer._target = TryCreateNormalTarget(width, height, &self->_gbuffer._isDepthSamplable);
        self->_gbuffer._hasTarget = (self->_gbuffer._target.id != 0);
        if (!self->_gbuffer._hasTarget)
        {
            WorldRendererCommon_WarnOnce(self, &self->_gbuffer._isTargetFailureReported, (const unsigned char*)
                u8"WorldRenderer: could not create the normal G-buffer; outlines and AO are off.");
        }
        else
        {
            SetTextureFilter(self->_gbuffer._target.texture, TEXTURE_FILTER_POINT);
            if (self->_gbuffer._isDepthSamplable)
            {
                SetTextureFilter(self->_gbuffer._target.depth, TEXTURE_FILTER_POINT);
            }
            else if (self->_ao._shader != NULL)
            {
                WorldRendererCommon_WarnOnce(self, &self->_ao._isTargetFailureReported, (const unsigned char*)
                    u8"WorldRenderer: the normal G-buffer has no samplable depth on this GPU; AO is off.");
            }
        }
    }

    // AO-multiplier target (8-bit, low-res, BILINEAR so the PBR ambient reads a smooth AO term). Only when the
    // AO shader is present and the G-buffer carries samplable depth for the AO pass to read.
    if ((self->_ao._shader != NULL) && self->_gbuffer._hasTarget && self->_gbuffer._isDepthSamplable)
    {
        self->_ao._target = LoadRenderTexture(width, height);
        self->_ao._hasTarget = (self->_ao._target.id != 0);
        if (self->_ao._hasTarget)
        {
            SetTextureFilter(self->_ao._target.texture, TEXTURE_FILTER_BILINEAR);
        }
        else
        {
            WorldRendererCommon_WarnOnce(self, &self->_ao._isTargetFailureReported,
                (const unsigned char*)u8"WorldRenderer: could not create the AO target; AO is off.");
        }
    }
}

void WorldRendererGBuffer_ReleaseTargets(WorldRenderer* self)
{
    WorldRendererCommon_UnloadTarget(&self->_gbuffer._target, &self->_gbuffer._hasTarget);
    self->_gbuffer._isDepthSamplable = false;
    WorldRendererCommon_UnloadTarget(&self->_ao._target, &self->_ao._hasTarget);
}

/* Uses its own depth (the nearest model's normal wins; the debug grid is intentionally not drawn here so it never
 * gets outlined). Colour blending is DISABLED so the encoded normal (RGB) and the flag (A, which is 0.5 for
 * non-outline surfaces) are written verbatim — with blending on, an alpha of 0.5 would blend the normal with the
 * cleared background and corrupt it. */
void WorldRendererGBuffer_RenderNormals(WorldRenderer* self, const WorldRenderFrame* frame)
{
    BeginTextureMode(self->_gbuffer._target);
    ClearBackground(BLANK); // RGBA (0,0,0,0): A=0 = "not a surface" everywhere the geometry does not draw
    rlDisableColorBlend();  // write RGB (normal) + A (flag) directly; no alpha blend
    BeginMode3D(frame->RenderCamera);
    DrawNormalBufferModels(self, frame->SceneFilter);
    // The model instances components' copies (B03; never crisp), each with its component's outline flag.
    if (frame->SceneFilter != WorldRendererPixelFilter_CrispOnly)
    {
        WorldRendererInstances_DrawNormals(self, frame, GameShader_GetRaylibShader(self->_gbuffer._shader),
            self->_gbuffer._locOutlineFlag);
    }
    // The terrain's chunks (B01), with their outline flags.
    WorldRendererTerrain_DrawNormals(self, GameShader_GetRaylibShader(self->_gbuffer._shader),
        self->_gbuffer._locOutlineFlag);
    // Cutout sprites into the same G-buffer (alpha-tested) so they get silhouette outlines and AO too.
    DrawNormalBufferSprites(self, frame->SceneFilter);
    EndMode3D();
    rlEnableColorBlend();   // restore the default blending for the following 2D passes
    EndTextureMode();
}

/* Runs BEFORE the scene colour pass: SSAO from the G-buffer's samplable depth + surface flag into the AO target
 * (an AO multiplier in R), which the PBR scene pass samples to darken ONLY its ambient term. Uses the scene's
 * projection to reconstruct view positions. Opens/closes its own passes. */
void WorldRendererGBuffer_RenderAo(WorldRenderer* self, const WorldRenderFrame* frame)
{
    Shader RayShader = GameShader_GetRaylibShader(self->_ao._shader);
    float Resolution[2] = { (float)frame->SceneWidth, (float)frame->SceneHeight };
    float Strength = frame->AoStrength;

    BeginTextureMode(self->_ao._target);
    ClearBackground(WHITE); // AO multiplier 1 (fully open) wherever nothing draws / no occlusion
    SetShaderValue(RayShader, self->_ao._locResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValueMatrix(RayShader, self->_ao._locProjection, frame->Projection);
    SetShaderValueMatrix(RayShader, self->_ao._locInvProjection, frame->InvProjection);
    SetShaderValue(RayShader, self->_ao._locStrength, &Strength, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(RayShader);
    // Bind the G-buffer's depth (unit 1) + colour/flag (unit 2) via SetShaderValueTexture AFTER BeginShaderMode
    // so they enter Raylib's 2D batch texture table (a manual slot bind would leave the samplers on unit 0).
    SetShaderValueTexture(RayShader, self->_ao._locDepthTexture, self->_gbuffer._target.depth);
    SetShaderValueTexture(RayShader, self->_ao._locNormalTexture, self->_gbuffer._target.texture);
    Rectangle Source = { 0.0f, 0.0f, (float)self->_gbuffer._target.texture.width,
        (float)self->_gbuffer._target.texture.height };
    Rectangle Destination = { 0.0f, 0.0f, (float)frame->SceneWidth, (float)frame->SceneHeight };
    DrawTexturePro(self->_gbuffer._target.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
}
