// The sky's occupancy volume (interior lighting): everything that keeps the sky out (models and instance cells that
// occlude the sky, the terrain) drawn from above onto the map's grid around the camera, into an integer texture whose
// bits are height slices, and an OR pyramid of it; the ambient pre-pass and the lit shaders march through it around each
// surface (shaders/common/interior.glsl; the layout and the C mirror are WorldInterior.h's). It is drawn twice with no
// depth test and both faces: with XOR, toggling the slices whose centre lies below each fragment (a slice is solid where
// an odd number of surfaces lie above it: inside walls under a roof, not in the air under it), then with OR, setting the
// slice each model's fragment passes through (a slab thinner than a slice is not lost; the terrain's sheet needs none).
// Parity takes a single closed thing whole,
// but where two of them overlap (walls meeting at a corner) their parities cancel: so the models are drawn in layers,
// things whose boxes overlap in different ones where they can, each layer's parity in a scratch volume and then ORed into
// the volume (the terrain's sheet straight into it, the instance cells as a layer of their own). Its centre is snapped to
// whole texels and its window to whole slices in double, so it stands still in the world while the camera moves.

#include "WorldRendererInternal.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"


// Macros.
/** Asset names of the volume's program and its skinned twin (the instanced one is loaded with the instances' twins). */
#define SLICES_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sky_slices")
#define SLICES_SKINNED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sky_slices_skinned")
/** Asset name of the pyramid's program: a level from the one below. */
#define REDUCE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sky_reduce")
/** The volume's drawings: the slices below each fragment toggled, then the slice it passes through set. */
#define SLICE_MODE_PARITY 0
#define SLICE_MODE_PRESENCE 1
/** The layers the models are drawn in, each its own parity: things whose boxes overlap go to different layers where they
 *  can (the instance cells take one more of their own, the terrain draws straight into the volume). */
#define MODEL_LAYER_COUNT 4
/** How deep two boxes must overlap on every axis to count as overlapping, in metres: faces that only touch share a
 *  boundary, which parity takes whole. */
#define LAYER_OVERLAP_TOLERANCE 0.02f
/** The level shader's steps: a level of the pyramid from the one below, a layer copied texel for texel. */
#define REDUCE_STEP 2
#define COPY_STEP 1
/** How many texels a layer's footprint on the map grows by on every side (rasterization at its edges). */
#define FOOTPRINT_MARGIN 2


// Static functions.
/* Caches a program's slice uniforms. */
static void PrepareSliceLocations(Shader shader, WorldRendererSliceLocations* outLocations)
{
    outLocations->_locEyeHeight = GetShaderLocation(shader, "mapEyeHeight");
    outLocations->_locDepth = GetShaderLocation(shader, "mapDepth");
    outLocations->_locBottom = GetShaderLocation(shader, "volumeBottom");
    outLocations->_locMode = GetShaderLocation(shader, "sliceMode");
}

/* Sets a program's slice uniforms for a drawing: the map's eye (half its depth above the camera) and depth, the frame's
 * window, the mode. */
static void SetSliceUniforms(Shader shader, const WorldRendererSliceLocations* locations, const WorldRenderFrame* frame,
    int mode)
{
    float EyeHeight = (float)(WORLD_INTERIOR_SKY_MAP_DEPTH * 0.5);
    float Depth = (float)WORLD_INTERIOR_SKY_MAP_DEPTH;
    SetShaderValue(shader, locations->_locEyeHeight, &EyeHeight, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locDepth, &Depth, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locBottom, &frame->SkyVolumeBottom, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locMode, &mode, SHADER_UNIFORM_INT);
}

/* A level of the volume as a render texture raylib can begin (its framebuffer, its side). */
static RenderTexture2D GetLevelTarget(const WorldRendererSkyVisibilityState* state, int level)
{
    int Side = WORLD_INTERIOR_SKY_MAP_RESOLUTION >> level;
    RenderTexture2D Target = { 0 };
    Target.id = state->_framebuffers[level];
    Target.texture.id = state->_volume;
    Target.texture.width = Side;
    Target.texture.height = Side;
    Target.texture.mipmaps = 1;
    Target.texture.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    return Target;
}

/* A model's box in render space: LOD 0's mesh bounds through its import transform and the object's matrix, or the cube
 * around its bounding sphere where that is all there is (the world model's own bounds, a posed placement). */
static BoundingBox GetModelBox(const WorldRenderModel* model)
{
    if (WorldModel_HasOwnBounds(model->Asset) || (model->Pose != NULL))
    {
        Vector3 Half = { model->Radius, model->Radius, model->Radius };
        return (BoundingBox){ Vector3Subtract(model->RenderCenter, Half), Vector3Add(model->RenderCenter, Half) };
    }
    BoundingBox Local = GameModel_GetLocalBounds(WorldModel_GetLodModel(model->Asset, 0U));
    Matrix ToRender = MatrixMultiply(model->BoundsTransform, model->RenderMatrix);
    BoundingBox Box = { { INFINITY, INFINITY, INFINITY }, { -INFINITY, -INFINITY, -INFINITY } };
    for (int Corner = 0; Corner < 8; Corner++)
    {
        Vector3 Point = { (Corner & 1) ? Local.max.x : Local.min.x, (Corner & 2) ? Local.max.y : Local.min.y,
            (Corner & 4) ? Local.max.z : Local.min.z };
        Point = Vector3Transform(Point, ToRender);
        Box.min = Vector3Min(Box.min, Point);
        Box.max = Vector3Max(Box.max, Point);
    }
    return Box;
}

/* Whether two boxes overlap deeper than LAYER_OVERLAP_TOLERANCE on every axis. */
static bool DoBoxesOverlap(const BoundingBox* a, const BoundingBox* b)
{
    return (a->min.x < (b->max.x - LAYER_OVERLAP_TOLERANCE)) && (b->min.x < (a->max.x - LAYER_OVERLAP_TOLERANCE))
        && (a->min.y < (b->max.y - LAYER_OVERLAP_TOLERANCE)) && (b->min.y < (a->max.y - LAYER_OVERLAP_TOLERANCE))
        && (a->min.z < (b->max.z - LAYER_OVERLAP_TOLERANCE)) && (b->min.z < (a->max.z - LAYER_OVERLAP_TOLERANCE));
}

/* Gives each model the volume draws a layer, in its visibility list's order: the one where the fewest models already
 * placed overlap its box (none, wherever a layer is free of them), the lowest on a tie. Fills the state's buffers and
 * counts each layer's models. */
static void AssignModelLayers(WorldRenderer* self, size_t* outCounts)
{
    WorldRendererSkyVisibilityState* State = &self->_skyVisibility;
    size_t ModelCount = 0U;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    size_t VisibleCount = 0U;
    const size_t* Visible = WorldVisibility_GetModels(&self->_visibility, &self->_cache, WorldVisibilityPass_SkyVisibility,
        &VisibleCount);
    GenericBuffer_Clear(&State->_modelLayers);
    GenericBuffer_Clear(&State->_modelBoxes);
    for (int Layer = 0; Layer < MODEL_LAYER_COUNT; Layer++)
    {
        outCounts[Layer] = 0U;
    }
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        BoundingBox Box = GetModelBox(&Models[Visible[Position]]);
        const BoundingBox* Boxes = (const BoundingBox*)State->_modelBoxes._data;
        const uint8_t* Layers = (const uint8_t*)State->_modelLayers._data;
        size_t Overlaps[MODEL_LAYER_COUNT] = { 0U };
        for (size_t Earlier = 0U; Earlier < Position; Earlier++)
        {
            Overlaps[Layers[Earlier]] += DoBoxesOverlap(&Box, &Boxes[Earlier]) ? 1U : 0U;
        }
        uint8_t Chosen = 0U;
        for (uint8_t Layer = 1U; Layer < MODEL_LAYER_COUNT; Layer++)
        {
            Chosen = (Overlaps[Layer] < Overlaps[Chosen]) ? Layer : Chosen;
        }
        if (!GenericBuffer_AddLast(&State->_modelBoxes, &Box) || !GenericBuffer_AddLast(&State->_modelLayers, &Chosen))
        {
            Chosen = 0U; // (the buffers abort the process rather than fail to grow)
        }
        outCounts[Chosen]++;
    }
}

/* Grows a rectangle of the map's texels (in float, from its bottom left) by where a box in render space lands on the map. */
static void AddToFootprint(const WorldRenderFrame* frame, Vector3 min, Vector3 max, Vector2* ioLow, Vector2* ioHigh)
{
    float Resolution = (float)WORLD_INTERIOR_SKY_MAP_RESOLUTION;
    for (int Corner = 0; Corner < 4; Corner++)
    {
        Vector3 Point = { (Corner & 1) ? max.x : min.x, 0.0f, (Corner & 2) ? max.z : min.z };
        Vector3 Clip = Vector3Transform(Point, frame->SkyMap.ViewProjection);
        Vector2 Texel = { ((Clip.x * 0.5f) + 0.5f) * Resolution, ((Clip.y * 0.5f) + 0.5f) * Resolution };
        *ioLow = Vector2Min(*ioLow, Texel);
        *ioHigh = Vector2Max(*ioHigh, Texel);
    }
}

/* The rectangle of the map's texels a layer's models (MODEL_LAYER_COUNT: the instance cells) cover, grown by
 * FOOTPRINT_MARGIN and clamped to the map, as a scissor box (from its bottom left); false when it covers none. */
static bool GetLayerFootprint(WorldRenderer* self, const WorldRenderFrame* frame, int layer, int* outBox)
{
    const WorldRendererSkyVisibilityState* State = &self->_skyVisibility;
    Vector2 Low = { INFINITY, INFINITY };
    Vector2 High = { -INFINITY, -INFINITY };
    if (layer < MODEL_LAYER_COUNT)
    {
        const BoundingBox* Boxes = (const BoundingBox*)State->_modelBoxes._data;
        const uint8_t* Layers = (const uint8_t*)State->_modelLayers._data;
        for (size_t Position = 0U; Position < State->_modelLayers._count; Position++)
        {
            if (Layers[Position] == layer)
            {
                AddToFootprint(frame, Boxes[Position].min, Boxes[Position].max, &Low, &High);
            }
        }
    }
    else
    {
        size_t EntryCount = 0U;
        const WorldRenderInstanceCell* Entries = WorldRenderCache_GetInstanceCells(&self->_cache, &EntryCount);
        size_t CellCount = 0U;
        const size_t* Cells = WorldVisibility_GetInstanceCells(&self->_visibility, &self->_cache,
            WorldVisibilityPass_SkyVisibility, &CellCount);
        for (size_t Position = 0U; Position < CellCount; Position++)
        {
            const WorldRenderInstanceCell* Cell = &Entries[Cells[Position]];
            Vector3 Half = { Cell->Radius, Cell->Radius, Cell->Radius };
            AddToFootprint(frame, Vector3Subtract(Cell->RenderCenter, Half), Vector3Add(Cell->RenderCenter, Half), &Low,
                &High);
        }
    }
    int Side = WORLD_INTERIOR_SKY_MAP_RESOLUTION;
    // Clamped in float first: a box far off the map (or a broken one) never overflows an int.
    float Left = fmaxf(floorf(Low.x) - (float)FOOTPRINT_MARGIN, 0.0f);
    float Bottom = fmaxf(floorf(Low.y) - (float)FOOTPRINT_MARGIN, 0.0f);
    float Right = fminf(ceilf(High.x) + (float)FOOTPRINT_MARGIN, (float)Side);
    float Top = fminf(ceilf(High.y) + (float)FOOTPRINT_MARGIN, (float)Side);
    if (!(Left < Right) || !(Bottom < Top))
    {
        return false;
    }
    outBox[0] = (int)Left;
    outBox[1] = (int)Bottom;
    outBox[2] = (int)Right - (int)Left;
    outBox[3] = (int)Top - (int)Bottom;
    return true;
}

/* Draws the volume's models of one layer (MODEL_LAYER_COUNT: every layer), each dithered by its fade. */
static void DrawModelLayer(WorldRenderer* self, const WorldRendererMeshShaders* shaders, int layer)
{
    const WorldRendererSkyVisibilityState* State = &self->_skyVisibility;
    size_t ModelCount = 0U;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    size_t VisibleCount = 0U;
    const size_t* Visible = WorldVisibility_GetModels(&self->_visibility, &self->_cache, WorldVisibilityPass_SkyVisibility,
        &VisibleCount);
    const uint8_t* Layers = (const uint8_t*)State->_modelLayers._data;
    for (size_t Position = 0U; Position < VisibleCount; Position++)
    {
        if ((layer < MODEL_LAYER_COUNT) && ((Position >= State->_modelLayers._count) || (Layers[Position] != layer)))
        {
            continue;
        }
        size_t Index = Visible[Position];
        const WorldRenderModel* Entry = &Models[Index];
        float Fade = WorldVisibility_GetModelFade(&self->_visibility, Index).Combined;
        WorldRendererCommon_SetDither(*shaders->Rigid, &State->_dither, Fade, 0.0f, false);
        if ((shaders->Skinned != NULL) && (Entry->Pose != NULL))
        {
            WorldRendererCommon_SetDither(*shaders->Skinned, &State->_skinnedDither, Fade, 0.0f, false);
        }
        WorldRendererGeometry_DrawModel(self, Entry, WorldLodState_GetShadowLod(&Entry->Lod), shaders, false);
    }
}

/* Opens a draw into a target of the volume with the map's view, every fragment kept (no depth test, both faces). */
static void BeginVolumeDraw(RenderTexture2D target, const WorldRenderFrame* frame)
{
    BeginTextureMode(target);
    rlSetMatrixProjection(frame->SkyMap.Projection);
    rlSetMatrixModelview(frame->SkyMap.View);
    rlDisableDepthTest();
    rlDisableBackfaceCulling();
}

static void EndVolumeDraw(void)
{
    rlEnableBackfaceCulling();
    EndTextureMode();
}

/* Draws a whole level-sized texture into a level of the volume through the level shader, by a step (REDUCE_STEP: a level
 * from the one below; COPY_STEP: a layer, texel for texel). */
static void DrawLevel(const WorldRendererSkyVisibilityState* state, RenderTexture2D target, Texture2D source, int step)
{
    Shader ReduceShader = GameShader_GetRaylibShader(state->_reduceShader);
    SetShaderValue(ReduceShader, state->_reduceLocStep, &step, SHADER_UNIFORM_INT);
    BeginShaderMode(ReduceShader);
    DrawTexturePro(source, (Rectangle){ 0.0f, 0.0f, (float)source.width, (float)source.height },
        (Rectangle){ 0.0f, 0.0f, (float)target.texture.width, (float)target.texture.height }, (Vector2){ 0.0f, 0.0f },
        0.0f, WHITE);
    EndShaderMode();
}

/* Frees the volume and its framebuffers (rlUnloadFramebuffer deletes only a depth attachment, which they have none of). */
static void ReleaseVolume(WorldRendererSkyVisibilityState* state)
{
    if (state->_scratchFramebuffer != 0U)
    {
        rlUnloadFramebuffer(state->_scratchFramebuffer);
        state->_scratchFramebuffer = 0U;
    }
    GLSliceMap_DeleteTexture(&state->_api, state->_scratch);
    state->_scratch = 0U;
    for (int Level = 0; Level < WORLD_INTERIOR_SKY_LEVEL_COUNT; Level++)
    {
        if (state->_framebuffers[Level] != 0U)
        {
            rlUnloadFramebuffer(state->_framebuffers[Level]);
            state->_framebuffers[Level] = 0U;
        }
    }
    GLSliceMap_DeleteTexture(&state->_api, state->_volume);
    state->_volume = 0U;
    state->_hasVolume = false;
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererSkyVisibility_Load(WorldRenderer* self)
{
    WorldRendererSkyVisibilityState* State = &self->_skyVisibility;
    if (State->_hasVolume)
    {
        return;
    }
    if (!GLSliceMap_LoadApi(&State->_api))
    {
        if (self->_logger != NULL)
        {
            Error LogResult = Logger_LogWarningFormatted(self->_logger, (const unsigned char*)u8"WorldRenderer: the sky's "
                u8"occupancy volume needs %s, which the GL lacks; roofs keep no sky light out.",
                (State->_api._missing != NULL) ? State->_api._missing : "a GL function");
            Error_Deconstruct(&LogResult);
        }
        return;
    }
    // Without its program or the pyramid's there is nothing to draw it with (the failure was logged).
    State->_shader = WorldRendererCommon_LoadShader(self, SLICES_SHADER_ASSET_NAME);
    State->_reduceShader = (State->_shader != NULL) ? WorldRendererCommon_LoadShader(self, REDUCE_SHADER_ASSET_NAME)
        : NULL;
    if ((State->_shader == NULL) || (State->_reduceShader == NULL))
    {
        return;
    }
    State->_reduceLocStep = GetShaderLocation(GameShader_GetRaylibShader(State->_reduceShader), "sourceStep");
    Shader RayShader = GameShader_GetRaylibShader(State->_shader);
    PrepareSliceLocations(RayShader, &State->_locations);
    WorldRendererCommon_PrepareDither(RayShader, &State->_dither);
    // Its skinned twin. Optional: without it skinned meshes draw their bind pose into the volume.
    State->_skinnedShader = WorldRendererCommon_LoadShader(self, SLICES_SKINNED_SHADER_ASSET_NAME);
    State->_skinnedPalette = (WorldRendererPalette){ ._location = -1 };
    if (State->_skinnedShader != NULL)
    {
        Shader SkinnedShader = GameShader_GetRaylibShader(State->_skinnedShader);
        PrepareSliceLocations(SkinnedShader, &State->_skinnedLocations);
        WorldRendererCommon_PrepareDither(SkinnedShader, &State->_skinnedDither);
        WorldRendererCommon_PreparePalette(SkinnedShader, &State->_skinnedPalette);
    }

    Error CreateResult = GLSliceMap_CreateTexture(&State->_api, WORLD_INTERIOR_SKY_MAP_RESOLUTION,
        WORLD_INTERIOR_SKY_LEVEL_COUNT, &State->_volume);
    bool IsComplete = CreateResult.Code == ErrorCode_Success;
    Error_Deconstruct(&CreateResult);
    if (IsComplete)
    {
        Error ScratchResult = GLSliceMap_CreateTexture(&State->_api, WORLD_INTERIOR_SKY_MAP_RESOLUTION, 1,
            &State->_scratch);
        IsComplete = ScratchResult.Code == ErrorCode_Success;
        Error_Deconstruct(&ScratchResult);
    }
    if (IsComplete)
    {
        State->_scratchFramebuffer = rlLoadFramebuffer();
        IsComplete = State->_scratchFramebuffer != 0U;
        if (IsComplete)
        {
            rlFramebufferAttach(State->_scratchFramebuffer, State->_scratch, RL_ATTACHMENT_COLOR_CHANNEL0,
                RL_ATTACHMENT_TEXTURE2D, 0);
            IsComplete = rlFramebufferComplete(State->_scratchFramebuffer);
        }
    }
    for (int Level = 0; IsComplete && (Level < WORLD_INTERIOR_SKY_LEVEL_COUNT); Level++)
    {
        State->_framebuffers[Level] = rlLoadFramebuffer();
        IsComplete = State->_framebuffers[Level] != 0U;
        if (IsComplete)
        {
            rlFramebufferAttach(State->_framebuffers[Level], State->_volume, RL_ATTACHMENT_COLOR_CHANNEL0,
                RL_ATTACHMENT_TEXTURE2D, Level);
            IsComplete = rlFramebufferComplete(State->_framebuffers[Level]);
        }
    }
    rlDisableFramebuffer();
    if (!IsComplete)
    {
        ReleaseVolume(State);
        WorldRendererCommon_WarnOnce(self, &State->_isFailureReported, (const unsigned char*)u8"WorldRenderer: could "
            u8"not create the sky's occupancy volume; roofs keep no sky light out.");
        return;
    }
    State->_hasVolume = true;
}

void WorldRendererSkyVisibility_Unload(WorldRenderer* self)
{
    if (self->_skyVisibility._api._isAvailable)
    {
        ReleaseVolume(&self->_skyVisibility);
    }
}

bool WorldRendererSkyVisibility_IsAvailable(const WorldRenderer* self)
{
    const WorldRendererSkyVisibilityState* State = &self->_skyVisibility;
    return State->_hasVolume && (State->_shader != NULL) && (State->_reduceShader != NULL);
}

void WorldRendererSkyVisibility_PrepareFrame(const WorldRenderer* self, WorldRenderFrame* frame)
{
    frame->IsSkyVisibilityActive = frame->IsInteriorActive && WorldRendererSkyVisibility_IsAvailable(self);
    if (!frame->IsSkyVisibilityActive)
    {
        return;
    }
    frame->SkyMap = WorldInterior_GetSkyMap(frame->Origin);
    frame->SkyMapBox = WorldVisibility_ExtractFrustum(frame->SkyMap.ViewProjection);
    frame->SkyVolumeBottom = WorldInterior_GetSkyWindowBottom(frame->Origin);
}

void WorldRendererSkyVisibility_Render(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererSkyVisibilityState* State = &self->_skyVisibility;
    Shader SliceShader = GameShader_GetRaylibShader(State->_shader);
    bool HasSkinned = (State->_skinnedShader != NULL);
    Shader SkinnedShader = HasSkinned ? GameShader_GetRaylibShader(State->_skinnedShader) : SliceShader;
    WorldRendererMeshShaders Shaders = { .Rigid = &SliceShader, .Skinned = HasSkinned ? &SkinnedShader : NULL,
        .Palette = &State->_skinnedPalette };
    GameShader* InstancedTwin = self->_instances._skySliceShader;
    RenderTexture2D Volume = GetLevelTarget(State, 0);
    RenderTexture2D Scratch = Volume;
    Scratch.id = State->_scratchFramebuffer;
    Scratch.texture.id = State->_scratch;
    size_t LayerCounts[MODEL_LAYER_COUNT];
    AssignModelLayers(self, LayerCounts);
    size_t CellCount = 0U;
    WorldVisibility_GetInstanceCells(&self->_visibility, &self->_cache, WorldVisibilityPass_SkyVisibility, &CellCount);

    for (int Mode = SLICE_MODE_PARITY; Mode <= SLICE_MODE_PRESENCE; Mode++)
    {
        SetSliceUniforms(SliceShader, &State->_locations, frame, Mode);
        if (HasSkinned)
        {
            SetSliceUniforms(SkinnedShader, &State->_skinnedLocations, frame, Mode);
        }
        if (InstancedTwin != NULL)
        {
            SetSliceUniforms(GameShader_GetRaylibShader(InstancedTwin), &self->_instances._skySliceLocations, frame,
                Mode);
        }
        if (Mode == SLICE_MODE_PARITY)
        {
            // The terrain's sheet: its parity straight into the cleared volume.
            BeginVolumeDraw(Volume, frame);
            GLSliceMap_Clear(&State->_api);
            GLSliceMap_BeginLogicOp(&State->_api, GLSliceMapOp_Xor);
            WorldRendererTerrain_DrawCasters(self, SliceShader, &State->_dither, WorldVisibilityPass_SkyVisibility);
            GLSliceMap_EndLogicOp(&State->_api);
            EndVolumeDraw();
            // Each layer of models, then the instance cells: its parity in the scratch, then ORed into the volume; the
            // scratch cleared and copied only where the layer's boxes land on the map.
            for (int Layer = 0; Layer <= MODEL_LAYER_COUNT; Layer++)
            {
                int Footprint[4] = { 0, 0, 0, 0 };
                if (((Layer < MODEL_LAYER_COUNT) && (LayerCounts[Layer] == 0U))
                    || ((Layer == MODEL_LAYER_COUNT) && (CellCount == 0U))
                    || !GetLayerFootprint(self, frame, Layer, Footprint))
                {
                    continue;
                }
                rlDrawRenderBatchActive();
                rlEnableScissorTest();
                rlScissor(Footprint[0], Footprint[1], Footprint[2], Footprint[3]);
                BeginVolumeDraw(Scratch, frame);
                GLSliceMap_Clear(&State->_api);
                GLSliceMap_BeginLogicOp(&State->_api, GLSliceMapOp_Xor);
                if (Layer < MODEL_LAYER_COUNT)
                {
                    DrawModelLayer(self, &Shaders, Layer);
                }
                else
                {
                    WorldRendererInstances_DrawCasters(self, frame, SliceShader, &State->_dither,
                        WorldVisibilityPass_SkyVisibility);
                }
                GLSliceMap_EndLogicOp(&State->_api);
                EndVolumeDraw();
                BeginTextureMode(Volume);
                GLSliceMap_BeginLogicOp(&State->_api, GLSliceMapOp_Or);
                DrawLevel(State, Volume, Scratch.texture, COPY_STEP);
                GLSliceMap_EndLogicOp(&State->_api);
                EndTextureMode();
                rlDisableScissorTest();
            }
        }
        else
        {
            // Every model's own slices, ORed straight into the volume (the terrain's sheet, whose parity already holds it,
            // needs none: the bands start above a surface's own slice, and the ground at an upright surface's foot is
            // what it stands over).
            BeginVolumeDraw(Volume, frame);
            GLSliceMap_BeginLogicOp(&State->_api, GLSliceMapOp_Or);
            DrawModelLayer(self, &Shaders, MODEL_LAYER_COUNT);
            WorldRendererInstances_DrawCasters(self, frame, SliceShader, &State->_dither,
                WorldVisibilityPass_SkyVisibility);
            GLSliceMap_EndLogicOp(&State->_api);
            EndVolumeDraw();
        }
    }

    // The pyramid: each level the union of 2 x 2 texels of the one below, read alone while the next is drawn.
    for (int Level = 1; Level < WORLD_INTERIOR_SKY_LEVEL_COUNT; Level++)
    {
        GLSliceMap_ReadLevel(&State->_api, State->_volume, Level - 1);
        RenderTexture2D Target = GetLevelTarget(State, Level);
        BeginTextureMode(Target);
        DrawLevel(State, Target, GetLevelTarget(State, Level - 1).texture, REDUCE_STEP);
        EndTextureMode();
    }
    GLSliceMap_ReadAllLevels(&State->_api, State->_volume, WORLD_INTERIOR_SKY_LEVEL_COUNT);
}

void WorldRendererSkyVisibility_BindMap(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (!frame->IsSkyVisibilityActive)
    {
        return;
    }
    rlActiveTextureSlot(WorldRendererTextureUnit_SkyVisibility);
    rlEnableTexture(self->_skyVisibility._volume);
    rlActiveTextureSlot(0);
}

Texture2D WorldRendererSkyVisibility_GetVolumeTexture(const WorldRenderer* self)
{
    return GetLevelTarget(&self->_skyVisibility, 0).texture;
}
