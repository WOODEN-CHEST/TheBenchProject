// The shadow levels of the sky's shadow light (the sun by day, the moon on bright nights; WorldShadowLevels.h) and the
// point-light cube shadow, cast by models, the copies of model instances, terrain chunks and cutout sprites (turned
// toward the light). Each pass draws the casters the frame's visibility lists for it (a level's box and its size cut,
// or the cube's reach), each dithered by its shadow fade, so a fading object's shadow thins with it. Everything is in
// render space: the levels' boxes sit around the camera with their centres snapped to their texel grids
// (WorldRenderSpace_StepCascade), so their edges stand still while the camera moves; the cube sits at its light's
// render-space position.

#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"


// Macros.
/** Asset name of the depth-only shader the shadow levels draw with. */
#define DEPTH_SHADER_ASSET_NAME ((const unsigned char*)u8"world_depth")
/** Asset name of the point-light omnidirectional (cube) shadow depth shader (stores packed linear distance). */
#define CUBE_DEPTH_SHADER_ASSET_NAME ((const unsigned char*)u8"world_cube_depth")
/** Asset names of the two depth shaders' skinned twins: the same fragment stages over vertex stages that skin. */
#define DEPTH_SKINNED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_depth_skinned")
#define CUBE_DEPTH_SKINNED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_cube_depth_skinned")
/** Asset names of the cutout sprites' alpha-tested variants of the two depth shaders. */
#define SPRITE_DEPTH_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sprite_depth")
#define SPRITE_CUBE_DEPTH_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sprite_cube_depth")
/** Resolution (per cube face) of the point-light shadow cube map, in texels. */
#define POINT_SHADOW_CUBE_SIZE 1024
/** Near plane of the point-light shadow cube's 90-degree face projections. */
#define POINT_SHADOW_NEAR 0.05
/** The shadow levels' depth bias and normal offset, in texels of each level: half a texel of depth (the maps hold the
 *  casters' faces turned away from the light, so a lit surface never meets itself there), and the lookup moved off the
 *  surface along its normal by a texel and a half at grazing incidence (no surface shadows itself through the map's
 *  texels at a slant). In the first level that is 0.8 cm and 2.3 cm: a shadow reaches the foot of what casts it, and a
 *  roof's shadow the top of the wall under it. */
#define SHADOW_DEPTH_BIAS_TEXELS 0.5f
#define SHADOW_NORMAL_OFFSET_TEXELS 1.5f
/** The smallest depth bias, in the maps' depth (0 to 1 over the depth range): two of a 32-bit float's steps near 1
 *  (2^-23), so a bias of half a texel never falls below what the depth can hold (8192-texel maps with a reach past
 *  15 km would). */
#define SHADOW_MIN_DEPTH_BIAS 1.1920929e-7f
/** Room for a warning that quotes an error. */
#define WARNING_CAPACITY 512


// Static functions.
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

/* The LOD a model casts with in a shadow pass: its own shadow LOD while it has one (clamped to its last LOD);
 * otherwise the one it shows or heads to, solid, and in a level past the first at least the one it would show at the
 * level's inner edge (@p level NULL: the cube shadow, which draws what the camera shows). */
static size_t GetCasterLod(const WorldRenderer* self, const WorldRenderModel* entry, const WorldRendererShadowLevelFrame* level,
    float zoomFactor)
{
    size_t LodCount = entry->LodSettings.LodCount;
    size_t LastLod = (LodCount > 0U) ? (LodCount - 1U) : 0U;
    if (entry->HasShadowLod)
    {
        return (entry->ShadowLod < LastLod) ? (size_t)entry->ShadowLod : LastLod;
    }
    size_t Lod = WorldLodState_GetShadowLod(&entry->Lod);
    if ((level == NULL) || !(level->LodDistance > 0.0f))
    {
        return Lod;
    }
    size_t LevelLod = WorldLod_Select(&entry->LodSettings, level->LodDistance * zoomFactor,
        WorldRendererCommon_ConfigLodDistanceScale(self), false, 0U);
    return (LevelLod > Lod) ? LevelLod : Lod;
}

/* The model casters' depth (WorldRendererShadows_DrawModelCasters, declared in WorldRendererInternal.h): the sprites are
 * DrawSpriteCasters'. */
void WorldRendererShadows_DrawModelCasters(WorldRenderer* self, const WorldRendererMeshShaders* shaders,
    const WorldRendererDitherLocations* dither, const WorldRendererDitherLocations* skinnedDither,
    WorldVisibilityPass pass, const WorldRendererShadowLevelFrame* level, float zoomFactor, int faceCulled)
{
    size_t ModelCount = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    size_t CasterCount = 0;
    const size_t* Casters = WorldVisibility_GetModels(&self->_visibility, &self->_cache, pass, &CasterCount);
    WorldRendererDitherRun Run = { 0 };
    WorldRendererDitherRun SkinnedRun = { 0 };
    for (size_t Position = 0; Position < CasterCount; Position++)
    {
        size_t Index = Casters[Position];
        const WorldRenderModel* Entry = &Models[Index];
        float Fade = WorldVisibility_GetModelFade(&self->_visibility, Index).Shadow;
        WorldRendererCommon_SetRunDither(*shaders->Rigid, dither, &Run, Fade);
        if ((shaders->Skinned != NULL) && (Entry->Pose != NULL))
        {
            WorldRendererCommon_SetRunDither(*shaders->Skinned, skinnedDither, &SkinnedRun, Fade);
        }
        // A two-sided caster draws both faces (an open mesh: a one-plane roof, a sign, leaf cards).
        if (Entry->HasTwoSidedShadows)
        {
            rlDisableBackfaceCulling();
        }
        WorldRendererGeometry_DrawModelDepth(Entry, GetCasterLod(self, Entry, level, zoomFactor), shaders);
        if (Entry->HasTwoSidedShadows)
        {
            rlEnableBackfaceCulling();
            rlSetCullFace(faceCulled);
        }
    }
    WorldRendererCommon_EndDepthMeshes();
}

/* Draws a shadow pass's sprite casters (its visibility list: cutout sprites that are not decals, with casts_shadows)
 * through an alpha-tested sprite depth shader, each dithered by its shadow fade and with its matrix for this light (a
 * billboard faces it: @p isLevel picks the levels' light, else the cube shadow's), so the shadow has the sprite's shape
 * and does not turn with the camera. Two-sided: culling is off for them. No-op without sprites. */
static void DrawSpriteCasters(WorldRenderer* self, Shader shader, const WorldRendererDitherLocations* dither,
    int locUvRect, int locAlphaCutoff, WorldVisibilityPass pass, bool isLevel)
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
        WorldRendererCommon_SetDither(shader, dither, WorldVisibility_GetSpriteFade(&self->_visibility, Index).Shadow,
            0.0f, false);
        WorldRendererSprites_DrawQuad(self, shader, Entry, WHITE,
            isLevel ? Matrices->ShadowTransform : Matrices->PointTransform);
    }
    rlEnableBackfaceCulling();
}

/* A level's frame from the levels' state: its matrices as its map holds them, its box (its sides and far plane from
 * them: what lies nearer the light than the box is drawn onto its near plane), its texel, its LOD distance and its
 * lookups' offsets. */
static WorldRendererShadowLevelFrame GetLevelFrame(const WorldShadowLevelsSettings* settings, uint32_t level,
    const WorldShadowLevelFrame* levelFrame)
{
    WorldRendererShadowLevelFrame Frame = { 0 };
    Frame.IsRendered = levelFrame->IsRendered;
    Frame.View = levelFrame->Cascade.View;
    Frame.Projection = levelFrame->Cascade.Projection;
    Frame.ViewProjection = levelFrame->Cascade.ViewProjection;
    Frame.Box = WorldVisibility_ExtractFrustum(Frame.ViewProjection);
    Frame.Box.Planes[WorldFrustumPlane_Near] = (WorldPlane){ .Normal = { 0.0f, 0.0f, 0.0f }, .Distance = 0.0f };
    Frame.Texel = levelFrame->Texel;
    Frame.LodDistance = WorldShadowLevels_GetLodDistance(settings, level);
    float DepthRange = levelFrame->Shape.FarPlane - levelFrame->Shape.NearPlane;
    Frame.DepthBias = fmaxf(SHADOW_DEPTH_BIAS_TEXELS * Frame.Texel / DepthRange, SHADOW_MIN_DEPTH_BIAS);
    Frame.NormalOffset = SHADOW_NORMAL_OFFSET_TEXELS * Frame.Texel;
    return Frame;
}

/* Makes the levels' maps for the frame's settings when there are none, or of another size or count: one layer per level
 * the reach uses (so a short reach takes no memory for the levels it leaves out), the old ones released, every level's
 * map forgotten (each renders before it is read). A failure is logged once per size and count, and leaves the levels
 * without maps. Returns whether there are maps. */
static bool EnsureMaps(WorldRenderer* self, const WorldShadowLevelsSettings* settings)
{
    WorldRendererShadowLevelState* State = &self->_shadowLevels;
    int Size = settings->MapSize;
    uint32_t Layers = WorldShadowLevels_GetUsedCount(settings);
    if (Layers == 0U)
    {
        return false; // no reach: the frame runs no levels
    }
    if (State->_hasMaps && (State->_maps._size == Size) && ((uint32_t)State->_maps._layerCount == Layers))
    {
        return true;
    }
    if ((State->_failedMapSize == Size) && (State->_failedLayerCount == Layers))
    {
        return false;
    }
    if (State->_hasMaps)
    {
        GLShadowArray_Destroy(&State->_api, &State->_maps);
        State->_hasMaps = false;
    }
    Error Result = GLShadowArray_Create(&State->_api, Size, (int)Layers, &State->_maps);
    if (Result.Code != ErrorCode_Success)
    {
        char Message[WARNING_CAPACITY];
        snprintf(Message, sizeof(Message), "WorldRenderer: could not make %u shadow maps of %d texels; the sun and the "
            "moon cast no shadows (%s).", (unsigned int)Layers, Size,
            (Result.Message != NULL) ? (const char*)Result.Message : "no reason given");
        WorldRendererCommon_Warn(self, (const unsigned char*)Message);
        Error_Deconstruct(&Result);
        State->_failedMapSize = Size;
        State->_failedLayerCount = Layers;
        return false;
    }
    State->_failedMapSize = 0;
    State->_failedLayerCount = 0U;
    State->_hasMaps = true;
    WorldShadowLevels_Invalidate(&State->_levels);
    return true;
}

/* Renders one level into its layer of the maps: the depth of its casters (the visibility list of its pass) from the
 * frame's shadow light, with its matrices and depth clamping. Opens and closes its own texture and 3D passes. */
static void RenderLevel(WorldRenderer* self, const WorldRenderFrame* frame, uint32_t level)
{
    WorldRendererShadowLevelState* State = &self->_shadowLevels;
    const WorldRendererShadowLevelFrame* Level = &frame->ShadowLevels[level];
    WorldVisibilityPass Pass = WorldVisibility_GetShadowLevelPass(level);
    Shader DepthShader = GameShader_GetRaylibShader(State->_depthShader);
    bool HasSkinned = (State->_skinnedDepthShader != NULL);
    Shader SkinnedShader = HasSkinned ? GameShader_GetRaylibShader(State->_skinnedDepthShader) : DepthShader;
    WorldRendererMeshShaders Shaders = { .Rigid = &DepthShader, .Skinned = HasSkinned ? &SkinnedShader : NULL,
        .Palette = &State->_skinnedPalette };

    GLShadowArray_AttachLayer(&State->_api, &State->_maps, (int)level);
    RenderTexture2D Target = { 0 };
    Target.id = GLShadowArray_GetFramebuffer(&State->_maps);
    Target.texture.width = State->_maps._size;
    Target.texture.height = State->_maps._size;
    BeginTextureMode(Target);
    ClearBackground(WHITE); // clears depth to the far plane (the framebuffer has no colour)
    rlEnableDepthTest();
    rlSetMatrixProjection(Level->Projection);
    rlSetMatrixModelview(Level->View);   // DrawMesh reads these to build each model's light-space MVP
    GLShadowArray_BeginDepthClamp(&State->_api);
    // Render BACK faces into the maps (cull front). The lit front faces then sit well in front of the stored depth,
    // which removes most self-shadowing acne without a large (peter-panning) depth bias.
    rlSetCullFace(RL_CULL_FACE_FRONT);
    WorldRendererShadows_DrawModelCasters(self, &Shaders, &State->_dither, &State->_skinnedDither, Pass, Level,
        frame->ZoomFactor, RL_CULL_FACE_FRONT);
    // The model instances components' copies, each at its level's LOD, dithered by its own shadow fade.
    WorldRendererInstances_DrawCasters(self, frame, DepthShader, &State->_dither, Pass, Level);
    // The terrain's chunks the same way: a hill's far side, facing away from the light, shades what lies behind it.
    WorldRendererTerrain_DrawCasters(self, DepthShader, &State->_dither, Pass);
    rlSetCullFace(RL_CULL_FACE_BACK);  // restore the default culling for the scene pass
    // Cutout sprites, facing the light, alpha-tested.
    if (State->_spriteDepthShader != NULL)
    {
        DrawSpriteCasters(self, GameShader_GetRaylibShader(State->_spriteDepthShader), &State->_spriteDither,
            State->_spriteLocUvRect, State->_spriteLocAlphaCutoff, Pass, true);
    }
    GLShadowArray_EndDepthClamp(&State->_api);
    rlDisableDepthTest();
    EndTextureMode();
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererShadows_LoadShaders(WorldRenderer* self)
{
    WorldRendererShadowLevelState* State = &self->_shadowLevels;
    // The depth shader the levels draw with (optional; models draw unshadowed without it), and the levels' GL functions
    // (their maps are made at the first frame that runs them, for the config's size). The maps' dither sits on their
    // texel grid (the default pixel scale of 1).
    State->_depthShader = WorldRendererCommon_LoadShader(self, DEPTH_SHADER_ASSET_NAME);
    if (State->_depthShader != NULL)
    {
        WorldRendererCommon_PrepareDither(GameShader_GetRaylibShader(State->_depthShader), &State->_dither);
        if (!GLShadowArray_LoadApi(&State->_api))
        {
            char Message[WARNING_CAPACITY];
            snprintf(Message, sizeof(Message), "WorldRenderer: the GPU lacks %s; the sun and the moon cast no shadows.",
                (State->_api._missing != NULL) ? State->_api._missing : "a function the shadow maps need");
            WorldRendererCommon_Warn(self, (const unsigned char*)Message);
        }
    }
    // Its skinned twin (optional: without it skinned meshes cast the shadow of their bind pose).
    State->_skinnedDepthShader = (State->_depthShader != NULL)
        ? WorldRendererCommon_LoadShader(self, DEPTH_SKINNED_SHADER_ASSET_NAME) : NULL;
    State->_skinnedPalette = (WorldRendererPalette){ ._location = -1 };
    if (State->_skinnedDepthShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(State->_skinnedDepthShader);
        WorldRendererCommon_PrepareDither(RayShader, &State->_skinnedDither);
        WorldRendererCommon_PreparePalette(RayShader, &State->_skinnedPalette);
    }

    // The cutout sprites' alpha-tested depth shader for the levels (optional: without it sprites cast no sun or moon
    // shadow).
    State->_spriteDepthShader = WorldRendererCommon_LoadShader(self, SPRITE_DEPTH_SHADER_ASSET_NAME);
    if (State->_spriteDepthShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(State->_spriteDepthShader);
        State->_spriteLocUvRect = GetShaderLocation(RayShader, "uvRect");
        State->_spriteLocAlphaCutoff = GetShaderLocation(RayShader, "alphaCutoff");
        WorldRendererCommon_PrepareDither(RayShader, &State->_spriteDither);
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
        // Its skinned twin (optional: without it skinned meshes cast the shadow of their bind pose).
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
    WorldRendererShadowLevelState* State = &self->_shadowLevels;
    if (State->_hasMaps)
    {
        GLShadowArray_Destroy(&State->_api, &State->_maps);
        State->_hasMaps = false;
        WorldShadowLevels_Invalidate(&State->_levels);
    }
    if (self->_pointShadow._hasCube)
    {
        rlUnloadFramebuffer(self->_pointShadow._framebuffer); // also deletes its depth renderbuffer
        rlUnloadTexture(self->_pointShadow._cube);
        self->_pointShadow._hasCube = false;
    }
}

WorldShadowLevelsSettings WorldRendererShadows_GetSettings(const WorldRenderer* self, const WorldEnvironment* environment)
{
    WorldShadowLevelsSettings Settings = WorldShadowLevelsSettings_CreateDefault();
    const GameConfig* Config = self->_config;
    if (Config != NULL)
    {
        Settings.LevelCount = (Config->ShadowLevelCount > 0) ? (uint32_t)Config->ShadowLevelCount : 0U;
        Settings.Factor = Config->ShadowLevelFactor;
        Settings.MapSize = Config->ShadowMapSize;
        for (uint32_t Level = 0U; Level < WORLD_SHADOW_MAX_LEVELS; Level++)
        {
            int32_t Period = Config->ShadowUpdatePeriods[Level];
            Settings.UpdatePeriods[Level] = (Period > 0) ? (uint32_t)Period : 1U;
        }
    }
    Settings.Reach = environment->ShadowReach;
    return WorldShadowLevelsSettings_Sanitize(&Settings);
}

bool WorldRendererShadows_AreLevelsAvailable(const WorldRenderer* self)
{
    return (self->_shadowLevels._depthShader != NULL) && self->_shadowLevels._api._isAvailable;
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
    WorldRendererShadowLevelState* State = &self->_shadowLevels;
    if (frame->AreShadowLevelsActive)
    {
        // Kept maps of another world, or of before frames without the levels, hold what was there then.
        if (!State->_ranLastFrame || (State->_world != frame->World))
        {
            WorldShadowLevels_Invalidate(&State->_levels);
        }
        State->_world = frame->World;
        if (EnsureMaps(self, &frame->ShadowLevelSettings))
        {
            WorldShadowLevelFrame Levels[WORLD_SHADOW_MAX_LEVELS];
            frame->ShadowLevelCount = WorldShadowLevels_BeginFrame(&self->_shadowLevels._levels,
                &frame->ShadowLevelSettings, frame->Origin, frame->ShadowDirection, Levels);
            for (uint32_t Level = 0U; Level < frame->ShadowLevelCount; Level++)
            {
                frame->ShadowLevels[Level] = GetLevelFrame(&frame->ShadowLevelSettings, Level, &Levels[Level]);
            }
        }
        frame->AreShadowLevelsActive = self->_shadowLevels._hasMaps && (frame->ShadowLevelCount > 0U);
    }
    State->_ranLastFrame = frame->AreShadowLevelsActive;

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

void WorldRendererShadows_RenderLevels(WorldRenderer* self, const WorldRenderFrame* frame)
{
    for (uint32_t Level = 0U; Level < frame->ShadowLevelCount; Level++)
    {
        if (frame->ShadowLevels[Level].IsRendered)
        {
            RenderLevel(self, frame, Level);
        }
    }
}

void WorldRendererShadows_BindLevels(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (frame->AreShadowLevelsActive)
    {
        GLShadowArray_Bind(&self->_shadowLevels._api, &self->_shadowLevels._maps, WorldRendererTextureUnit_ShadowLevels);
    }
}

void WorldRendererShadows_UpdateStatus(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererStatus* Status = &self->_status;
    Status->ShadowLevelCount = frame->AreShadowLevelsActive ? frame->ShadowLevelCount : 0U;
    Status->ShadowLevelRenderedMask = 0U;
    Status->ShadowReach = frame->ShadowLevelSettings.Reach;
    Status->ShadowMapSize = frame->ShadowLevelSettings.MapSize;
    Status->IsShadowLevelViewEnabled = self->_shadowLevels._isLevelViewEnabled;
    for (uint32_t Level = 0U; Level < WORLD_RENDERER_STATUS_MAX_SHADOW_LEVELS; Level++)
    {
        Status->ShadowLevelSizes[Level] = 0.0f;
        Status->ShadowLevelCasterCounts[Level] = 0U;
        if (Level >= Status->ShadowLevelCount)
        {
            continue;
        }
        Status->ShadowLevelSizes[Level] = WorldShadowLevels_GetSize(&frame->ShadowLevelSettings, Level);
        if (!frame->ShadowLevels[Level].IsRendered)
        {
            continue;
        }
        Status->ShadowLevelRenderedMask |= 1U << Level;
        WorldVisibilityPass Pass = WorldVisibility_GetShadowLevelPass(Level);
        size_t Count = 0U;
        WorldVisibility_GetModels(&self->_visibility, &self->_cache, Pass, &Count);
        Status->ShadowLevelCasterCounts[Level] += Count;
        WorldVisibility_GetInstanceCells(&self->_visibility, &self->_cache, Pass, &Count);
        Status->ShadowLevelCasterCounts[Level] += Count;
        WorldVisibility_GetTerrainChunks(&self->_visibility, &self->_cache, Pass, &Count);
        Status->ShadowLevelCasterCounts[Level] += Count;
        WorldVisibility_GetSprites(&self->_visibility, &self->_cache, Pass, &Count);
        Status->ShadowLevelCasterCounts[Level] += Count;
    }
}

void WorldRendererShadows_LogStatus(const WorldRenderer* self)
{
    if ((self->_logger == NULL) || !self->_isStatusReported)
    {
        return;
    }
    const WorldRendererStatus* Status = &self->_status;
    char Levels[WARNING_CAPACITY];
    size_t Length = 0U;
    Levels[0] = '\0';
    for (uint32_t Level = 0U; (Level < Status->ShadowLevelCount) && (Length < sizeof(Levels)); Level++)
    {
        int Written = snprintf(Levels + Length, sizeof(Levels) - Length, "%s%.0f m (%s, %llu casters)",
            (Level > 0U) ? ", " : "", (double)Status->ShadowLevelSizes[Level],
            ((Status->ShadowLevelRenderedMask >> Level) & 1U) ? "rendered" : "kept",
            (unsigned long long)Status->ShadowLevelCasterCounts[Level]);
        if (Written < 0)
        {
            break;
        }
        Length += (size_t)Written;
    }
    Error LogResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: shadow levels: %u used (%s), %d texels a side, reach %.0f m%s.",
        (unsigned int)Status->ShadowLevelCount, (Status->ShadowLevelCount > 0U) ? Levels : "the levels did not run",
        Status->ShadowMapSize, (double)Status->ShadowReach, Status->IsShadowLevelViewEnabled ? "; level view on" : "");
    Error_Deconstruct(&LogResult);
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
        // light's reach (the visibility's cube list), the LOD they show (or their own shadow LOD).
        WorldRendererShadows_DrawModelCasters(self, &Shaders, &self->_pointShadow._dither,
            &self->_pointShadow._skinnedDither, WorldVisibilityPass_PointShadow, NULL, frame->ZoomFactor,
            RL_CULL_FACE_BACK);
        WorldRendererInstances_DrawCasters(self, frame, CubeShader, &self->_pointShadow._dither,
            WorldVisibilityPass_PointShadow, NULL);
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
