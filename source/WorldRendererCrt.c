// The CRT surfaces' programs: a CRT's glass draws in the camera's lit passes (the scene and the crisp overlay) with
// world_crt, which shades it the PBR program's way (its material, the frame's light, a screen's light, the haze) and adds
// what a CRT shows: its picture through the tube's curvature with scanlines, a mask, a glow, a vignette, a flicker and the
// power animation, behind glossy glass that mirrors the sky (shaders/crt/fragment.frag, shaders/common/crt.glsl; the C
// twin is CrtScreen.h). Not a pass: the geometry's lit draws use it for the CRT's material slot.

#include "WorldRendererInternal.h"
#include "CrtScreen.h"
#include "GameTexture.h"
#include "raylib/rlgl.h"


// Macros.
/** Asset names of the CRT programs: the rigid one and its skinned twin (the same fragment stage over the PBR's skinned
 *  vertex stage). */
#define CRT_SHADER_ASSET_NAME ((const unsigned char*)u8"world_crt")
#define CRT_SKINNED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_crt_skinned")


// Static functions.
/* Caches a CRT program's own uniforms and points its mask tile's sampler at its unit (once). */
static void PrepareCrtLocations(const WorldRendererPbrProgram* program, WorldRendererCrtLocations* outLocations)
{
    *outLocations = (WorldRendererCrtLocations)
    {
        ._locShape = -1, ._locLook = -1, ._locPower = -1, ._locMaskKind = -1, ._locMaskTile = -1, ._locMaskMean = -1,
        ._locMaskIsSrgb = -1, ._locReflection = -1, ._locWindowPixelScale = -1
    };
    if (program->_shader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(program->_shader);
    outLocations->_locShape = GetShaderLocation(RayShader, "crtShape");
    outLocations->_locLook = GetShaderLocation(RayShader, "crtLook");
    outLocations->_locPower = GetShaderLocation(RayShader, "crtPower");
    outLocations->_locMaskKind = GetShaderLocation(RayShader, "crtMaskKind");
    outLocations->_locMaskTile = GetShaderLocation(RayShader, "crtMaskTile");
    outLocations->_locMaskMean = GetShaderLocation(RayShader, "crtMaskMean");
    outLocations->_locMaskIsSrgb = GetShaderLocation(RayShader, "crtMaskIsSrgb");
    outLocations->_locReflection = GetShaderLocation(RayShader, "crtReflection");
    outLocations->_locWindowPixelScale = GetShaderLocation(RayShader, "crtWindowPixelScale");
    int Unit = (int)WorldRendererTextureUnit_CrtMask;
    SetShaderValue(RayShader, outLocations->_locMaskTile, &Unit, SHADER_UNIFORM_INT);
}

/* The mask a CRT shows: the renderer's override while one is set, else its own; a texture mask without a tile, none. */
static CrtMaskKind GetMask(const WorldRenderer* self, const WorldModelSurface* surface)
{
    CrtMaskKind Mask = self->_crt._isMaskOverridden ? self->_crt._maskOverride : WorldModelSurface_GetCrt(surface)->Mask;
    return ((Mask == CrtMaskKind_Texture) && (WorldModelSurface_GetMaskTexture(surface) == NULL)) ? CrtMaskKind_None
        : Mask;
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererCrt_LoadShaders(WorldRenderer* self)
{
    WorldRendererLighting_LoadPbrProgram(self, &self->_crt._rigid, CRT_SHADER_ASSET_NAME);
    if (self->_crt._rigid._shader != NULL)
    {
        WorldRendererLighting_LoadPbrProgram(self, &self->_crt._skinned, CRT_SKINNED_SHADER_ASSET_NAME);
    }
    PrepareCrtLocations(&self->_crt._rigid, &self->_crt._rigidCrt);
    PrepareCrtLocations(&self->_crt._skinned, &self->_crt._skinnedCrt);
}

bool WorldRendererCrt_IsCrtEntry(const WorldRenderer* self, const WorldRenderModel* model)
{
    return (self->_crt._rigid._shader != NULL) && (model->Surface != NULL)
        && (WorldModelSurface_GetKind(model->Surface) == WorldModelSurfaceKind_Crt);
}

bool WorldRendererCrt_IsCrtMesh(const WorldRenderer* self, const WorldRenderModel* model, int materialIndex)
{
    return WorldRendererCrt_IsCrtEntry(self, model) && (materialIndex == WorldModelSurface_GetMaterialSlot(model->Surface));
}

void WorldRendererCrt_UploadPass(WorldRenderer* self, const WorldRenderFrame* frame, float windowPixelScale)
{
    self->_crt._timeSeconds = frame->TimeSeconds;
    const WorldRendererPbrProgram* Programs[] = { &self->_crt._rigid, &self->_crt._skinned };
    const WorldRendererCrtLocations* Locations[] = { &self->_crt._rigidCrt, &self->_crt._skinnedCrt };
    for (size_t Index = 0U; Index < (sizeof(Programs) / sizeof(Programs[0])); Index++)
    {
        if (Programs[Index]->_shader != NULL)
        {
            SetShaderValue(GameShader_GetRaylibShader(Programs[Index]->_shader), Locations[Index]->_locWindowPixelScale,
                &windowPixelScale, SHADER_UNIFORM_FLOAT);
        }
    }
}

CrtPowerShape WorldRendererCrt_GetPowerShape(const WorldRenderModel* model, float timeSeconds)
{
    const WorldModelSurface* Surface = model->Surface;
    int32_t Height = WorldModelSurface_GetHeight(Surface);
    float Aspect = (Height > 0) ? ((float)WorldModelSurface_GetWidth(Surface) / (float)Height) : 1.0f;
    CrtPowerShape Shape = CrtScreen_GetPowerShape(model->IsSurfacePowered, model->SurfacePower, Aspect);
    float Flicker = CrtScreen_GetFlicker(WorldModelSurface_GetCrt(Surface)->Flicker, (double)timeSeconds,
        model->ObjectId);
    Shape.Brightness *= Flicker;
    Shape.Light *= Flicker;
    return Shape;
}

void WorldRendererCrt_UploadDraw(const WorldRenderer* self, const WorldRendererPbrProgram* program,
    const WorldRendererCrtLocations* locations, const WorldRenderModel* model)
{
    Shader RayShader = GameShader_GetRaylibShader(program->_shader);
    const WorldModelSurface* Surface = model->Surface;
    const CrtScreenSettings* Crt = WorldModelSurface_GetCrt(Surface);
    float Width = (float)WorldModelSurface_GetWidth(Surface);
    float Height = (float)WorldModelSurface_GetHeight(Surface);
    float Shape[4] = { Crt->Curvature, Width / Height, Width, Height };
    float Look[4] =
    {
        Crt->AreScanlinesEnabled ? Crt->ScanlineStrength : 0.0f, Crt->MaskStrength, Crt->Glow, Crt->Vignette
    };
    CrtPowerShape Power = WorldRendererCrt_GetPowerShape(model, self->_crt._timeSeconds);
    float PowerValues[4] = { Power.OpenX, Power.OpenY, Power.Brightness, Power.Whiteness };
    CrtMaskKind Mask = GetMask(self, Surface);
    int MaskKind = (int)Mask;
    SetShaderValue(RayShader, locations->_locShape, Shape, SHADER_UNIFORM_VEC4);
    SetShaderValue(RayShader, locations->_locLook, Look, SHADER_UNIFORM_VEC4);
    SetShaderValue(RayShader, locations->_locPower, PowerValues, SHADER_UNIFORM_VEC4);
    SetShaderValue(RayShader, locations->_locMaskKind, &MaskKind, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, locations->_locReflection, &Crt->Reflection, SHADER_UNIFORM_FLOAT);
    if (Mask != CrtMaskKind_Texture)
    {
        return;
    }
    const GameTexture* Tile = WorldModelSurface_GetMaskTexture(Surface);
    int IsSrgb = GameTexture_IsSrgb(Tile) ? 1 : 0;
    SetShaderValue(RayShader, locations->_locMaskMean, WorldModelSurface_GetMaskMean(Surface), SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, locations->_locMaskIsSrgb, &IsSrgb, SHADER_UNIFORM_INT);
    rlActiveTextureSlot(WorldRendererTextureUnit_CrtMask);
    rlEnableTexture(GameTexture_GetRaylibTexture(Tile).id);
    rlActiveTextureSlot(0);
}
