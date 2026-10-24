// The shadow cascades of the sky's shadow light (the sun by day, the moon on bright nights) and the point-light cube
// shadow, cast by models and by cutout sprites (turned toward the light). Each pass draws the casters the frame's
// visibility lists for it (its box or its reach), each dithered by its fade, so a fading object's shadow thins with it.
// Everything is in render space: the cascades' boxes sit around the camera with their centres snapped to their texel
// grids (WorldRenderSpace_StepCascade), so their edges stand still while the camera moves; the cube sits at its light's
// render-space position.

#include <math.h>
#include "WorldRendererInternal.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"


// Macros.
/** Asset name of the depth-only shader used for the shadow cascades. */
#define DEPTH_SHADER_ASSET_NAME ((const unsigned char*)u8"world_depth")
/** Asset name of the point-light omnidirectional (cube) shadow depth shader (stores packed linear distance). */
#define CUBE_DEPTH_SHADER_ASSET_NAME ((const unsigned char*)u8"world_cube_depth")
/** Asset names of the two depth shaders' skinned twins (T29): the same fragment stages over vertex stages that skin. */
#define DEPTH_SKINNED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_depth_skinned")
#define CUBE_DEPTH_SKINNED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_cube_depth_skinned")
/** Asset names of the cutout sprites' alpha-tested variants of the two depth shaders. */
#define SPRITE_DEPTH_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sprite_depth")
#define SPRITE_CUBE_DEPTH_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sprite_cube_depth")
/** The shadow light (the sun or the moon) casts TWO cascaded orthographic shadow maps, both around the camera (their
 *  centres snapped to their texels): a NEAR cascade with a small extent (sharp shadows for close detail; 1.6 cm texels)
 *  and a FAR cascade with a large extent (coarse shadows for big / distant casters; 7.8 cm texels). Per-object
 *  WorldShadowTier routes which cascade(s) each object casts into. World-space width/height of each cascade's
 *  frustum: */
#define SHADOW_NEAR_ORTHO_SIZE 32.0f
#define SHADOW_FAR_ORTHO_SIZE 160.0f
/** Distance the shadow light camera is placed from its focus, along the shadow light's direction. */
#define SHADOW_DISTANCE 120.0f
/** Near plane of the shadow orthographic frustum (shared by both cascades). */
#define SHADOW_NEAR 1.0f
/** Far plane of the shadow orthographic frustum (brackets the scene around the focus; shared by both cascades). */
#define SHADOW_FAR (SHADOW_DISTANCE * 2.0f)
/** Resolution (per cube face) of the point-light shadow cube map, in texels. */
#define POINT_SHADOW_CUBE_SIZE 1024
/** Near plane of the point-light shadow cube's 90-degree face projections. */
#define POINT_SHADOW_NEAR 0.05


// Static functions.
/* Creates a depth-only framebuffer whose depth attachment is a samplable texture, for a shadow cascade (the
 * depth texture lives in .depth). Returns a zero target (id 0) if the framebuffer could not be made complete. */
static RenderTexture2D LoadShadowMap(int width, int height)
{
    RenderTexture2D Target = { 0 };
    Target.id = rlLoadFramebuffer();
    if (Target.id == 0)
    {
        return Target;
    }

    rlEnableFramebuffer(Target.id);
    // No colour attachment (depth-only). BeginTextureMode reads .texture size for the viewport, so set it.
    Target.texture.width = width;
    Target.texture.height = height;
    Target.depth.id = rlLoadTextureDepth(width, height, false); // false => a samplable depth TEXTURE
    Target.depth.width = width;
    Target.depth.height = height;
    Target.depth.format = SCENE_DEPTH_FORMAT;
    Target.depth.mipmaps = 1;
    rlFramebufferAttach(Target.id, Target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    bool Complete = (Target.depth.id != 0) && rlFramebufferComplete(Target.id);
    rlDisableFramebuffer();

    if (!Complete)
    {
        rlUnloadFramebuffer(Target.id); // also deletes the attached depth texture
        Target = (RenderTexture2D){ 0 };
    }
    return Target;
}

/* Creates the point-light omnidirectional shadow resources: an RGBA8 colour CUBE (rlgl cannot make empty float
 * cube maps, so linear distance is PACKED into RGBA8), a depth renderbuffer, and a framebuffer with the depth +
 * face 0 attached (the colour face is re-pointed per face at render time). Sets _hasCube on success; on any
 * failure releases whatever it made and leaves it false. Needs a live GL context.
 * The renderbuffer is attached as soon as it exists, because rlUnloadFramebuffer is the only rlgl call that
 * deletes one (rlUnloadTexture would delete an unrelated texture that has the same numeric id). */
static void LoadPointShadowCube(WorldRenderer* self)
{
    unsigned int Cube = rlLoadTextureCubemap(NULL, POINT_SHADOW_CUBE_SIZE, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    if (Cube == 0)
    {
        return;
    }
    // Packed distance must be read with NEAREST (linear filtering would blend the packed bytes into garbage).
    rlCubemapParameters(Cube, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlCubemapParameters(Cube, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);

    unsigned int Framebuffer = rlLoadFramebuffer();
    if (Framebuffer == 0)
    {
        rlUnloadTexture(Cube);
        return;
    }

    unsigned int DepthRenderbuffer = rlLoadTextureDepth(POINT_SHADOW_CUBE_SIZE, POINT_SHADOW_CUBE_SIZE, true);
    if (DepthRenderbuffer != 0)
    {
        rlFramebufferAttach(Framebuffer, DepthRenderbuffer, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
    }
    rlFramebufferAttach(Framebuffer, Cube, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_CUBEMAP_POSITIVE_X, 0);
    if ((DepthRenderbuffer == 0) || !rlFramebufferComplete(Framebuffer))
    {
        rlUnloadFramebuffer(Framebuffer); // also deletes the attached depth renderbuffer
        rlUnloadTexture(Cube);
        return;
    }

    self->_pointShadow._cube = Cube;
    self->_pointShadow._depthRenderbuffer = DepthRenderbuffer;
    self->_pointShadow._framebuffer = Framebuffer;
    self->_pointShadow._hasCube = true;
}

/* Draws the depth of a shadow pass's model casters (its visibility list) through @p shaders, each dithered by its fade
 * (into the skinned shader too, through @p skinnedDither, while the caster has a pose). A caster mid LOD cross-fade casts
 * the LOD it heads to, solid (no cross-fade dither), so its shadow changes once per change and never shimmers. Ignores
 * the pixelation filter (every pixelation state casts). The sprites are DrawSpriteCasters'. */
static void DrawShadowCasters(WorldRenderer* self, const WorldRendererMeshShaders* shaders,
    const WorldRendererDitherLocations* dither, const WorldRendererDitherLocations* skinnedDither,
    WorldVisibilityPass pass)
{
    size_t ModelCount = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    size_t CasterCount = 0;
    const size_t* Casters = WorldVisibility_GetModels(&self->_visibility, &self->_cache, pass, &CasterCount);
    for (size_t Position = 0; Position < CasterCount; Position++)
    {
        size_t Index = Casters[Position];
        const WorldRenderModel* Entry = &Models[Index];
        float Fade = WorldVisibility_GetModelFade(&self->_visibility, Index).Combined;
        WorldRendererCommon_SetDither(*shaders->Rigid, dither, Fade, 0.0f, false);
        if ((shaders->Skinned != NULL) && (Entry->Pose != NULL))
        {
            WorldRendererCommon_SetDither(*shaders->Skinned, skinnedDither, Fade, 0.0f, false);
        }
        WorldRendererGeometry_DrawModel(self, Entry, WorldLodState_GetShadowLod(&Entry->Lod), shaders, false);
    }
}

/* Draws a shadow pass's sprite casters (its visibility list: cutout sprites that are not decals, with casts_shadows)
 * through an alpha-tested sprite depth shader, each dithered by its fade and with its matrix for this light (a billboard
 * faces it: @p isCascade picks the cascades' light, else the cube shadow's), so the shadow has the sprite's shape and
 * does not turn with the camera. Two-sided: culling is off for them. No-op without sprites. */
static void DrawSpriteCasters(WorldRenderer* self, Shader shader, const WorldRendererDitherLocations* dither,
    int locUvRect, int locAlphaCutoff, WorldVisibilityPass pass, bool isCascade)
{
    if (!WorldRendererSprites_IsAvailable(self))
    {
        return;
    }
    size_t SpriteCount = 0;
    const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(&self->_cache, &SpriteCount);
    size_t CasterCount = 0;
    const size_t* Casters = WorldVisibility_GetSprites(&self->_visibility, &self->_cache, pass, &CasterCount);
    rlDisableBackfaceCulling();
    for (size_t Position = 0; Position < CasterCount; Position++)
    {
        size_t Index = Casters[Position];
        const WorldRenderSprite* Entry = &Sprites[Index];
        const WorldRendererSpriteFrame* Matrices = WorldRendererSprites_GetFrame(self, Index);
        if (Matrices == NULL)
        {
            continue;
        }
        SetShaderValue(shader, locUvRect, Entry->UvRect, SHADER_UNIFORM_VEC4);
        SetShaderValue(shader, locAlphaCutoff, &Entry->AlphaCutoff, SHADER_UNIFORM_FLOAT);
        WorldRendererCommon_SetDither(shader, dither, WorldVisibility_GetSpriteFade(&self->_visibility, Index).Combined,
            0.0f, false);
        WorldRendererSprites_DrawQuad(self, shader, Entry, WHITE,
            isCascade ? Matrices->CascadeTransform : Matrices->PointTransform);
    }
    rlEnableBackfaceCulling();
}

/* One cascade's light matrices and box for the frame, in render space: an orthographic box of the given world-space
 * extent around the camera, looking along the frame's shadow light, its centre moved from its last place (@p grid,
 * updated here) to the camera by whole texels (WorldRenderSpace_StepCascade). */
static WorldRendererCascadeFrame GetCascadeFrame(const WorldRenderFrame* frame, float orthoSize,
    WorldRenderCascadeGrid* grid)
{
    WorldRenderCascadeShape Shape =
    {
        .Size = orthoSize,
        .LightDistance = SHADOW_DISTANCE,
        .NearPlane = SHADOW_NEAR,
        .FarPlane = SHADOW_FAR,
        .MapSize = SHADOW_MAP_SIZE
    };
    WorldRenderCascade Snapped = WorldRenderSpace_StepCascade(grid, frame->Origin, frame->ShadowDirection, &Shape);
    WorldRendererCascadeFrame Cascade;
    Cascade.View = Snapped.View;
    Cascade.Projection = Snapped.Projection;
    Cascade.ViewProjection = Snapped.ViewProjection;
    Cascade.Box = WorldVisibility_ExtractFrustum(Cascade.ViewProjection);
    return Cascade;
}

/* Renders one shadow cascade: the depth of its casters (the visibility list of @p pass) from the frame's shadow light
 * into @p map, with the cascade's light matrices. Opens/closes its own texture + 3D passes. */
static void RenderShadowCascade(WorldRenderer* self, RenderTexture2D map, const WorldRendererCascadeFrame* cascade,
    WorldVisibilityPass pass)
{
    Shader DepthShader = GameShader_GetRaylibShader(self->_cascades._depthShader);
    bool HasSkinned = (self->_cascades._skinnedDepthShader != NULL);
    Shader SkinnedShader = HasSkinned ? GameShader_GetRaylibShader(self->_cascades._skinnedDepthShader) : DepthShader;
    WorldRendererMeshShaders Shaders = { .Rigid = &DepthShader, .Skinned = HasSkinned ? &SkinnedShader : NULL,
        .Palette = &self->_cascades._skinnedPalette };

    BeginTextureMode(map);
    ClearBackground(WHITE); // clears depth to the far plane (colour is irrelevant on the depth-only target)
    rlEnableDepthTest();
    rlSetMatrixProjection(cascade->Projection);
    rlSetMatrixModelview(cascade->View);   // DrawMesh reads these to build each model's light-space MVP
    // Render BACK faces into the shadow map (cull front). The lit front faces then sit well in front of the
    // stored depth, which removes most self-shadowing acne without a large (peter-panning) depth bias.
    rlSetCullFace(RL_CULL_FACE_FRONT);
    DrawShadowCasters(self, &Shaders, &self->_cascades._dither, &self->_cascades._skinnedDither, pass);
    // The terrain's chunks (B01) the same way: a hill's far side, facing away from the light, shades what lies behind it.
    WorldRendererTerrain_DrawCasters(self, DepthShader, &self->_cascades._dither, pass);
    rlSetCullFace(RL_CULL_FACE_BACK);  // restore the default culling for the scene pass
    // Cutout sprites, facing the light, alpha-tested.
    if (self->_cascades._spriteDepthShader != NULL)
    {
        DrawSpriteCasters(self, GameShader_GetRaylibShader(self->_cascades._spriteDepthShader),
            &self->_cascades._spriteDither, self->_cascades._spriteLocUvRect, self->_cascades._spriteLocAlphaCutoff, pass,
            true);
    }
    rlDisableDepthTest();
    EndTextureMode();
}


/* Creates the near cascade map and, when it exists, the optional far one (without it the renderer falls back to
 * the near cascade only). Each failure is logged; this runs once, at shader load. */
static void CreateCascadeMaps(WorldRenderer* self)
{
    self->_cascades._nearMap = LoadShadowMap(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    self->_cascades._hasNearMap = (self->_cascades._nearMap.id != 0);
    if (!self->_cascades._hasNearMap)
    {
        WorldRendererCommon_Warn(self,
            (const unsigned char*)u8"WorldRenderer: could not create the shadow map; sun and moon shadows are off.");
        return;
    }
    // Manual PCF compares raw depth texels, so nearest (point) sampling is required; linear filtering would
    // interpolate depths and corrupt the comparison.
    SetTextureFilter(self->_cascades._nearMap.depth, TEXTURE_FILTER_POINT);

    self->_cascades._farMap = LoadShadowMap(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    self->_cascades._hasFarMap = (self->_cascades._farMap.id != 0);
    if (self->_cascades._hasFarMap)
    {
        SetTextureFilter(self->_cascades._farMap.depth, TEXTURE_FILTER_POINT);
    }
    else
    {
        WorldRendererCommon_Warn(self, (const unsigned char*)u8"WorldRenderer: could not create the far shadow "
            u8"cascade; distant objects cast no sun or moon shadow.");
    }
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererShadows_LoadShaders(WorldRenderer* self)
{
    // Depth shader + cascade maps for the sun's and the moon's shadows (optional; models draw unshadowed without them).
    // The shadow maps' dither sits on their texel grid (the default pixel scale of 1).
    self->_cascades._depthShader = WorldRendererCommon_LoadShader(self, DEPTH_SHADER_ASSET_NAME);
    if (self->_cascades._depthShader != NULL)
    {
        WorldRendererCommon_PrepareDither(GameShader_GetRaylibShader(self->_cascades._depthShader),
            &self->_cascades._dither);
    }
    if ((self->_cascades._depthShader != NULL) && !self->_cascades._hasNearMap)
    {
        CreateCascadeMaps(self);
    }
    // Its skinned twin (T29; optional: without it skinned meshes cast the shadow of their bind pose).
    self->_cascades._skinnedDepthShader = (self->_cascades._depthShader != NULL)
        ? WorldRendererCommon_LoadShader(self, DEPTH_SKINNED_SHADER_ASSET_NAME) : NULL;
    self->_cascades._skinnedPalette = (WorldRendererPalette){ ._location = -1 };
    if (self->_cascades._skinnedDepthShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_cascades._skinnedDepthShader);
        WorldRendererCommon_PrepareDither(RayShader, &self->_cascades._skinnedDither);
        WorldRendererCommon_PreparePalette(RayShader, &self->_cascades._skinnedPalette);
    }

    // The cutout sprites' alpha-tested depth shader for the cascades (optional: without it sprites cast no sun or moon
    // shadow).
    self->_cascades._spriteDepthShader = WorldRendererCommon_LoadShader(self, SPRITE_DEPTH_SHADER_ASSET_NAME);
    if (self->_cascades._spriteDepthShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_cascades._spriteDepthShader);
        self->_cascades._spriteLocUvRect = GetShaderLocation(RayShader, "uvRect");
        self->_cascades._spriteLocAlphaCutoff = GetShaderLocation(RayShader, "alphaCutoff");
        WorldRendererCommon_PrepareDither(RayShader, &self->_cascades._spriteDither);
    }

    // The cutout sprites' alpha-tested cube-depth shader (optional: without it sprites cast no point-light shadow).
    self->_pointShadow._spriteShader = WorldRendererCommon_LoadShader(self, SPRITE_CUBE_DEPTH_SHADER_ASSET_NAME);
    if (self->_pointShadow._spriteShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_pointShadow._spriteShader);
        self->_pointShadow._spriteLocUvRect = GetShaderLocation(RayShader, "uvRect");
        self->_pointShadow._spriteLocAlphaCutoff = GetShaderLocation(RayShader, "alphaCutoff");
        self->_pointShadow._spriteLocLightPos = GetShaderLocation(RayShader, "lightPosition");
        self->_pointShadow._spriteLocLightFar = GetShaderLocation(RayShader, "lightFar");
        WorldRendererCommon_PrepareDither(RayShader, &self->_pointShadow._spriteDither);
    }

    // Cube-depth shader + point-light shadow cube (optional; point lights cast no shadows without them).
    self->_pointShadow._shader = WorldRendererCommon_LoadShader(self, CUBE_DEPTH_SHADER_ASSET_NAME);
    if (self->_pointShadow._shader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_pointShadow._shader);
        self->_pointShadow._locLightPos = GetShaderLocation(RayShader, "lightPosition");
        self->_pointShadow._locLightFar = GetShaderLocation(RayShader, "lightFar");
        WorldRendererCommon_PrepareDither(RayShader, &self->_pointShadow._dither);
        // Its skinned twin (T29; optional: without it skinned meshes cast the shadow of their bind pose).
        self->_pointShadow._skinnedShader = WorldRendererCommon_LoadShader(self, CUBE_DEPTH_SKINNED_SHADER_ASSET_NAME);
        self->_pointShadow._skinnedPalette = (WorldRendererPalette){ ._location = -1 };
        if (self->_pointShadow._skinnedShader != NULL)
        {
            Shader SkinnedShader = GameShader_GetRaylibShader(self->_pointShadow._skinnedShader);
            self->_pointShadow._skinnedLocLightPos = GetShaderLocation(SkinnedShader, "lightPosition");
            self->_pointShadow._skinnedLocLightFar = GetShaderLocation(SkinnedShader, "lightFar");
            WorldRendererCommon_PrepareDither(SkinnedShader, &self->_pointShadow._skinnedDither);
            WorldRendererCommon_PreparePalette(SkinnedShader, &self->_pointShadow._skinnedPalette);
        }
        if (!self->_pointShadow._hasCube)
        {
            LoadPointShadowCube(self);
            if (!self->_pointShadow._hasCube)
            {
                WorldRendererCommon_Warn(self, (const unsigned char*)u8"WorldRenderer: could not create the "
                    u8"point-light shadow cube; point lights cast no shadows.");
            }
        }
    }
}

void WorldRendererShadows_Unload(WorldRenderer* self)
{
    if (self->_cascades._hasNearMap)
    {
        // Frees the framebuffer and its depth-texture attachment (Raylib queries + deletes attachments).
        rlUnloadFramebuffer(self->_cascades._nearMap.id);
        self->_cascades._hasNearMap = false;
    }
    if (self->_cascades._hasFarMap)
    {
        rlUnloadFramebuffer(self->_cascades._farMap.id);
        self->_cascades._hasFarMap = false;
    }
    if (self->_pointShadow._hasCube)
    {
        rlUnloadFramebuffer(self->_pointShadow._framebuffer); // also deletes its depth renderbuffer
        rlUnloadTexture(self->_pointShadow._cube);
        self->_pointShadow._hasCube = false;
    }
}

size_t WorldRendererShadows_SelectPointLight(const WorldRenderer* self)
{
    size_t Best = SIZE_MAX;
    float BestDistance = 0.0f;
    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    for (size_t Index = 0; Index < LightCount; Index++)
    {
        const WorldRenderLight* Light = &Lights[Index];
        if (!Light->CastsShadows || (Light->Intensity <= 0.0f) || (Light->Range <= 0.0f))
        {
            continue;
        }
        float Distance = Vector3Length(Light->RenderPosition); // the camera sits at the origin of render space
        if ((Best == SIZE_MAX) || (Distance < BestDistance))
        {
            Best = Index;
            BestDistance = Distance;
        }
    }
    return Best;
}

void WorldRendererShadows_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame)
{
    if (frame->IsCascadeShadowActive)
    {
        frame->NearCascade = GetCascadeFrame(frame, SHADOW_NEAR_ORTHO_SIZE, &self->_cascades._nearGrid);
        frame->FarCascade = GetCascadeFrame(frame, SHADOW_FAR_ORTHO_SIZE, &self->_cascades._farGrid);
    }

    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    if (frame->IsPointShadowActive && (frame->ShadowLightIndex < LightCount))
    {
        // The light's reach, kept clear of the faces' near plane: the packed distances are normalized by it.
        float Far = Lights[frame->ShadowLightIndex].Range;
        if (Far < (float)(POINT_SHADOW_NEAR * 2.0))
        {
            Far = (float)(POINT_SHADOW_NEAR * 2.0);
        }
        frame->PointShadowPosition = Lights[frame->ShadowLightIndex].RenderPosition;
        frame->PointShadowFar = Far;
    }
}

void WorldRendererShadows_RenderCascades(WorldRenderer* self, const WorldRenderFrame* frame)
{
    RenderShadowCascade(self, self->_cascades._nearMap, &frame->NearCascade, WorldVisibilityPass_NearCascade);
    if (self->_cascades._hasFarMap)
    {
        RenderShadowCascade(self, self->_cascades._farMap, &frame->FarCascade, WorldVisibilityPass_FarCascade);
    }
}

/* Each face is a 90-degree perspective from the light storing the packed linear distance. The colour attachment
 * is re-pointed to each face in turn; BeginTextureMode re-binds the framebuffer and sets the viewport. */
void WorldRendererShadows_RenderPointLight(WorldRenderer* self, const WorldRenderFrame* frame)
{
    size_t LightCount = 0;
    WorldRenderCache_GetLights(&self->_cache, &LightCount);
    if (frame->ShadowLightIndex >= LightCount)
    {
        return;
    }
    Vector3 LightPos = frame->PointShadowPosition;
    float Far = frame->PointShadowFar;

    Shader CubeShader = GameShader_GetRaylibShader(self->_pointShadow._shader);
    float LightPosArr[3] = { LightPos.x, LightPos.y, LightPos.z };
    SetShaderValue(CubeShader, self->_pointShadow._locLightPos, LightPosArr, SHADER_UNIFORM_VEC3);
    SetShaderValue(CubeShader, self->_pointShadow._locLightFar, &Far, SHADER_UNIFORM_FLOAT);
    bool HasSkinned = (self->_pointShadow._skinnedShader != NULL);
    Shader SkinnedShader = HasSkinned ? GameShader_GetRaylibShader(self->_pointShadow._skinnedShader) : CubeShader;
    if (HasSkinned)
    {
        SetShaderValue(SkinnedShader, self->_pointShadow._skinnedLocLightPos, LightPosArr, SHADER_UNIFORM_VEC3);
        SetShaderValue(SkinnedShader, self->_pointShadow._skinnedLocLightFar, &Far, SHADER_UNIFORM_FLOAT);
    }
    WorldRendererMeshShaders Shaders = { .Rigid = &CubeShader, .Skinned = HasSkinned ? &SkinnedShader : NULL,
        .Palette = &self->_pointShadow._skinnedPalette };
    bool HasSpriteShader = (self->_pointShadow._spriteShader != NULL);
    Shader SpriteCubeShader = HasSpriteShader ? GameShader_GetRaylibShader(self->_pointShadow._spriteShader)
        : CubeShader;
    if (HasSpriteShader)
    {
        SetShaderValue(SpriteCubeShader, self->_pointShadow._spriteLocLightPos, LightPosArr, SHADER_UNIFORM_VEC3);
        SetShaderValue(SpriteCubeShader, self->_pointShadow._spriteLocLightFar, &Far, SHADER_UNIFORM_FLOAT);
    }

    Matrix CubeProj = MatrixPerspective(90.0 * DEG2RAD, 1.0, POINT_SHADOW_NEAR, (double)Far);

    // Standard OpenGL cube-map face directions + up vectors, so the rendered faces line up with samplerCube(dir).
    const Vector3 Targets[6] = { { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } };
    const Vector3 Ups[6] = { { 0.0f, -1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, -1.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } };
    const int FaceAttach[6] = {
        RL_ATTACHMENT_CUBEMAP_POSITIVE_X, RL_ATTACHMENT_CUBEMAP_NEGATIVE_X,
        RL_ATTACHMENT_CUBEMAP_POSITIVE_Y, RL_ATTACHMENT_CUBEMAP_NEGATIVE_Y,
        RL_ATTACHMENT_CUBEMAP_POSITIVE_Z, RL_ATTACHMENT_CUBEMAP_NEGATIVE_Z };

    RenderTexture2D CubeTarget = { 0 };
    CubeTarget.id = self->_pointShadow._framebuffer;
    CubeTarget.texture.id = self->_pointShadow._cube;
    CubeTarget.texture.width = POINT_SHADOW_CUBE_SIZE;
    CubeTarget.texture.height = POINT_SHADOW_CUBE_SIZE;

    for (int Face = 0; Face < 6; Face++)
    {
        rlFramebufferAttach(self->_pointShadow._framebuffer, self->_pointShadow._cube, RL_ATTACHMENT_COLOR_CHANNEL0,
            FaceAttach[Face], 0);
        Matrix FaceView = MatrixLookAt(LightPos, Vector3Add(LightPos, Targets[Face]), Ups[Face]);

        BeginTextureMode(CubeTarget);
        // The packed distance must land as written: raylib blends by default (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), and the
        // packed value's alpha byte is a digit of the distance, not a coverage (0.5 up close, exactly 0 from half the
        // reach on), so blending would mix every texel with the cleared "far" and lose the casters beyond half the reach.
        // BeginTextureMode has flushed the batch, so this changes the state of the draws below only.
        rlDisableColorBlend();
        ClearBackground(WHITE); // packed white = far distance (no occluder); clears depth to the far plane too
        rlEnableDepthTest();
        rlSetMatrixProjection(CubeProj);
        rlSetMatrixModelview(FaceView);
        // Default (back-face) culling + the PBR shader's slope-scaled bias handles acne; front-face culling
        // here caused peter-panning + seam artifacts, so it is intentionally not used. The casters are those within the
        // light's reach (the visibility's cube list).
        DrawShadowCasters(self, &Shaders, &self->_pointShadow._dither, &self->_pointShadow._skinnedDither,
            WorldVisibilityPass_PointShadow);
        WorldRendererTerrain_DrawCasters(self, CubeShader, &self->_pointShadow._dither, WorldVisibilityPass_PointShadow);
        if (HasSpriteShader)
        {
            DrawSpriteCasters(self, SpriteCubeShader, &self->_pointShadow._spriteDither,
                self->_pointShadow._spriteLocUvRect, self->_pointShadow._spriteLocAlphaCutoff,
                WorldVisibilityPass_PointShadow, false);
        }
        rlDisableDepthTest();
        rlEnableColorBlend(); // restore the default blending for the passes that follow
        EndTextureMode();
    }
}
