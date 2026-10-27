// The sky's visibility map (interior lighting): a top-down depth map around the camera of everything that keeps the sky
// out (models and instance cells that occlude the sky, the terrain), which the lit shaders read at a ring of taps around
// each surface (shaders/common/interior.glsl; the layout and the C mirror are WorldInterior.h's). Its centre is snapped
// to whole texels in double, so it stands still in the world while the camera moves. It draws with the shadow
// cascades' depth shaders.

#include "WorldRendererInternal.h"
#include "raylib/rlgl.h"


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererSkyVisibility_Load(WorldRenderer* self)
{
    WorldRendererSkyVisibilityState* State = &self->_skyVisibility;
    if (State->_hasMap || (self->_cascades._depthShader == NULL))
    {
        return; // without the depth shader there is nothing to draw it with (its failure was logged)
    }
    State->_map = WorldRendererCommon_CreateDepthTarget(WORLD_INTERIOR_SKY_MAP_RESOLUTION,
        WORLD_INTERIOR_SKY_MAP_RESOLUTION);
    State->_hasMap = State->_map.id != 0;
    if (!State->_hasMap)
    {
        WorldRendererCommon_WarnOnce(self, &State->_isFailureReported, (const unsigned char*)u8"WorldRenderer: could "
            u8"not create the sky's visibility map; roofs keep no sky light out.");
    }
}

void WorldRendererSkyVisibility_Unload(WorldRenderer* self)
{
    WorldRendererSkyVisibilityState* State = &self->_skyVisibility;
    if (State->_hasMap)
    {
        rlUnloadFramebuffer(State->_map.id); // also deletes its depth texture
        State->_hasMap = false;
    }
}

bool WorldRendererSkyVisibility_IsAvailable(const WorldRenderer* self)
{
    return self->_skyVisibility._hasMap && (self->_cascades._depthShader != NULL);
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
}

void WorldRendererSkyVisibility_Render(WorldRenderer* self, const WorldRenderFrame* frame)
{
    Shader DepthShader = GameShader_GetRaylibShader(self->_cascades._depthShader);
    bool HasSkinned = (self->_cascades._skinnedDepthShader != NULL);
    Shader SkinnedShader = HasSkinned ? GameShader_GetRaylibShader(self->_cascades._skinnedDepthShader) : DepthShader;
    WorldRendererMeshShaders Shaders = { .Rigid = &DepthShader, .Skinned = HasSkinned ? &SkinnedShader : NULL,
        .Palette = &self->_cascades._skinnedPalette };

    BeginTextureMode(self->_skyVisibility._map);
    ClearBackground(WHITE); // depth cleared to the bottom of the map's box: nothing there
    rlEnableDepthTest();
    rlSetMatrixProjection(frame->SkyMap.Projection);
    rlSetMatrixModelview(frame->SkyMap.View);
    // Both faces: whichever way a surface faces, the topmost one at a texel is what keeps the sky out (a roof modelled
    // as one plane facing down, a terrain's overhang seen from below).
    rlDisableBackfaceCulling();
    WorldRendererShadows_DrawModelCasters(self, &Shaders, &self->_cascades._dither, &self->_cascades._skinnedDither,
        WorldVisibilityPass_SkyVisibility);
    WorldRendererInstances_DrawCasters(self, frame, DepthShader, &self->_cascades._dither,
        WorldVisibilityPass_SkyVisibility);
    WorldRendererTerrain_DrawCasters(self, DepthShader, &self->_cascades._dither, WorldVisibilityPass_SkyVisibility);
    rlEnableBackfaceCulling();
    rlDisableDepthTest();
    EndTextureMode();
}

void WorldRendererSkyVisibility_BindMap(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (!frame->IsSkyVisibilityActive)
    {
        return;
    }
    rlActiveTextureSlot(WorldRendererTextureUnit_SkyVisibility);
    rlEnableTexture(self->_skyVisibility._map.depth.id);
    rlActiveTextureSlot(0);
}
