// The surfaces' pictures: what the code that owns a screen's content (a UI drawn into a render target) hands the
// renderer for one render (WorldRenderer_SetSurfaceTexture), and putting it into the per-draw copy of the surface's
// material, never into an asset: a screen's (and a CRT's) picture as its emissive map, a paper's as its albedo map. And
// the screens' light: every shown screen that glows lights what is in front of it in its picture's colour, one screen
// per lit draw (the nearest that reaches it), through common/lighting's ScreenLight.

#include <math.h>
#include <stdint.h>
#include "WorldRendererInternal.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "wr/WRString.h"


// Macros.
/** An index that names no picture. */
#define NO_BINDING SIZE_MAX
/** A screen light's picture is read at the mipmap level where it spans about this many texels a side (2^this), four by
 *  four taps there giving its mean colour. */
#define SCREEN_LIGHT_TAP_LEVELS 2.0f


// Static functions.
/* The index of the picture handed over for an object's surface, or NO_BINDING. */
static size_t FindBindingIndex(const WorldRenderer* self, uint64_t objectId, const unsigned char* surfaceName)
{
    const WorldRendererSurfaceBinding* Bindings =
        (const WorldRendererSurfaceBinding*)(const void*)self->_surfaces._bindings._data;
    size_t Length = StringUTF8_GetByteLength(surfaceName);
    for (size_t Index = 0U; Index < self->_surfaces._bindings._count; Index++)
    {
        const WorldRendererSurfaceBinding* Binding = &Bindings[Index];
        if ((Binding->ObjectId == objectId) && (StringUTF8_GetByteLength(Binding->SurfaceName) == Length)
            && Memory_IsEqual(Binding->SurfaceName, surfaceName, Length))
        {
            return Index;
        }
    }
    return NO_BINDING;
}

/* The picture handed over for an object's surface, or NULL. */
static const WorldRendererSurfaceBinding* FindBinding(const WorldRenderer* self, uint64_t objectId,
    const unsigned char* surfaceName)
{
    size_t Index = FindBindingIndex(self, objectId, surfaceName);
    return (Index != NO_BINDING)
        ? &((const WorldRendererSurfaceBinding*)(const void*)self->_surfaces._bindings._data)[Index] : NULL;
}


// Internal functions (declared in WorldRendererInternal.h).
Error WorldRendererSurfaces_SetTexture(WorldRenderer* self, uint64_t objectId, const unsigned char* surfaceName,
    Texture2D texture, bool isBottomUp)
{
    if ((objectId == 0U) || (surfaceName == NULL) || (texture.id == 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldRenderer_SetSurfaceTexture: the object id must not be "
            "0, the surface's name must not be NULL and the texture must be one (its id not 0).");
    }
    size_t Length = StringUTF8_GetByteLength(surfaceName);
    if (Length > WORLD_MODEL_SURFACE_NAME_MAX_LENGTH)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "WorldRenderer_SetSurfaceTexture: the surface's name is "
            "longer than a surface's can be.");
    }
    size_t Existing = FindBindingIndex(self, objectId, surfaceName);
    if (Existing != NO_BINDING)
    {
        WorldRendererSurfaceBinding* Binding = GenericBuffer_GetPointerToElement(&self->_surfaces._bindings, Existing);
        Binding->Texture = texture;
        Binding->IsBottomUp = isBottomUp;
        return Error_CreateSuccess();
    }
    WorldRendererSurfaceBinding Binding = { .ObjectId = objectId, .Texture = texture, .IsBottomUp = isBottomUp };
    Memory_Copy(surfaceName, Binding.SurfaceName, Length);
    Binding.SurfaceName[Length] = 0U;
    if (!GenericBuffer_AddLast(&self->_surfaces._bindings, &Binding))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "WorldRenderer_SetSurfaceTexture: could not grow the list of "
            "pictures.");
    }
    return Error_CreateSuccess();
}

void WorldRendererSurfaces_ForgetTextures(WorldRenderer* self)
{
    GenericBuffer_Clear(&self->_surfaces._bindings);
}

WorldRendererMapOrientation WorldRendererSurfaces_Apply(const WorldRenderer* self, const WorldRenderModel* model,
    Material* material)
{
    WorldRendererMapOrientation Orientation = { .IsAlbedoBottomUp = false, .IsEmissiveBottomUp = false };
    const WorldRendererSurfaceBinding* Binding = FindBinding(self, model->ObjectId,
        WorldModelSurface_GetName(model->Surface));
    MaterialMap* Maps = material->maps;
    switch (WorldModelSurface_GetKind(model->Surface))
    {
        case WorldModelSurfaceKind_Screen:
            if ((Binding != NULL) && model->IsSurfacePowered)
            {
                Maps[MATERIAL_MAP_EMISSION].texture = Binding->Texture;
                Maps[MATERIAL_MAP_EMISSION].color = WHITE;
                Maps[MATERIAL_MAP_EMISSION].value = model->SurfaceEmission;
                Orientation.IsEmissiveBottomUp = Binding->IsBottomUp;
                return Orientation;
            }
            // Off, or nothing to show: no glow at all (not even the material's own); its own surface shows.
            Maps[MATERIAL_MAP_EMISSION].texture.id = 0U;
            Maps[MATERIAL_MAP_EMISSION].color = BLACK;
            Maps[MATERIAL_MAP_EMISSION].value = 0.0f;
            return Orientation;
        case WorldModelSurfaceKind_Crt:
            // While it is on or still turning off; the CRT program plays its power animation on the picture.
            if ((Binding != NULL) && (model->IsSurfacePowered || (model->SurfacePower > 0.0f)))
            {
                Maps[MATERIAL_MAP_EMISSION].texture = Binding->Texture;
                Maps[MATERIAL_MAP_EMISSION].color = WHITE;
                Maps[MATERIAL_MAP_EMISSION].value = model->SurfaceEmission;
                Orientation.IsEmissiveBottomUp = Binding->IsBottomUp;
                return Orientation;
            }
            Maps[MATERIAL_MAP_EMISSION].texture.id = 0U;
            Maps[MATERIAL_MAP_EMISSION].color = BLACK;
            Maps[MATERIAL_MAP_EMISSION].value = 0.0f;
            return Orientation;
        case WorldModelSurfaceKind_Paper:
            if (Binding != NULL)
            {
                Maps[MATERIAL_MAP_ALBEDO].texture = Binding->Texture;
                Orientation.IsAlbedoBottomUp = Binding->IsBottomUp;
            }
            return Orientation;
        case WorldModelSurfaceKind_Count:
            break;
    }
    // Only reachable with a corrupted kind: the material as it is.
    return Orientation;
}

void WorldRendererSurfaces_Count(const WorldRenderer* self, uint32_t* outSurfaceCount, uint32_t* outPictureCount,
    uint32_t* outCrtCount)
{
    uint32_t Surfaces = 0U;
    uint32_t Pictures = 0U;
    uint32_t Crts = 0U;
    size_t ModelCount = 0U;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    for (size_t Index = 0U; Index < ModelCount; Index++)
    {
        const WorldRenderModel* Entry = &Models[Index];
        if (!Entry->IsDrawn || (Entry->Surface == NULL))
        {
            continue;
        }
        Surfaces++;
        if (FindBinding(self, Entry->ObjectId, WorldModelSurface_GetName(Entry->Surface)) != NULL)
        {
            Pictures++;
        }
        if (WorldModelSurface_GetKind(Entry->Surface) == WorldModelSurfaceKind_Crt)
        {
            Crts++;
        }
    }
    *outSurfaceCount = Surfaces;
    *outPictureCount = Pictures;
    *outCrtCount = Crts;
}

void WorldRendererSurfaces_PrepareLitShader(Shader shader, WorldRendererScreenLightLocations* outLocations)
{
    outLocations->_locActive = GetShaderLocation(shader, "screenLightActive");
    outLocations->_locPosition = GetShaderLocation(shader, "screenLightPosition");
    outLocations->_locNormal = GetShaderLocation(shader, "screenLightNormal");
    outLocations->_locShape = GetShaderLocation(shader, "screenLightShape");
    outLocations->_locGlow = GetShaderLocation(shader, "screenLightGlow");
    outLocations->_locPicture = GetShaderLocation(shader, "screenLightPicture");
    int Unit = (int)WorldRendererTextureUnit_ScreenLight;
    SetShaderValue(shader, outLocations->_locPicture, &Unit, SHADER_UNIFORM_INT);
}

/* The glow a shown entry's screen gives off around it (0: none): its emission, its light intensity and its power. */
static float GetScreenGlow(const WorldRenderModel* entry, float timeSeconds, float* outWhiteness)
{
    *outWhiteness = 0.0f;
    float Glow = entry->SurfaceEmission * WorldModelSurface_GetLightIntensity(entry->Surface);
    if (WorldModelSurface_GetKind(entry->Surface) != WorldModelSurfaceKind_Crt)
    {
        return entry->IsSurfacePowered ? Glow : 0.0f;
    }
    CrtPowerShape Power = WorldRendererCrt_GetPowerShape(entry, timeSeconds);
    *outWhiteness = Power.Whiteness;
    return Glow * Power.Light;
}

/* The mipmap level whose four by four taps give a picture's mean colour: where it spans about four texels a side (0
 * without mipmaps: the taps then read the picture itself). */
static float GetMeanLevel(Texture2D picture)
{
    int Side = (picture.width > picture.height) ? picture.width : picture.height;
    if ((picture.mipmaps <= 1) || (Side <= 1))
    {
        return 0.0f;
    }
    return fmaxf(floorf(log2f((float)Side)) - SCREEN_LIGHT_TAP_LEVELS, 0.0f);
}

void WorldRendererSurfaces_PrepareFrame(WorldRenderer* self, const WorldRenderFrame* frame)
{
    GenericBuffer_Clear(&self->_surfaces._lights);
    size_t ModelCount = 0U;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    for (size_t Index = 0U; Index < ModelCount; Index++)
    {
        const WorldRenderModel* Entry = &Models[Index];
        if (!Entry->IsDrawn || (Entry->Surface == NULL) || !WorldModelSurface_IsGlowing(Entry->Surface))
        {
            continue;
        }
        const WorldRendererSurfaceBinding* Binding = FindBinding(self, Entry->ObjectId,
            WorldModelSurface_GetName(Entry->Surface));
        float Whiteness = 0.0f;
        float Glow = (Binding != NULL) ? GetScreenGlow(Entry, frame->TimeSeconds, &Whiteness) : 0.0f;
        if (!(Glow > 0.0f))
        {
            continue;
        }
        // The glass in render space: its centre through the object's matrix, its two axes as directions (their cross the
        // area, also under an uneven scale), its front through the inverse transpose (a mirror's too).
        Matrix Turn = Entry->RenderMatrix;
        Turn.m12 = 0.0f;
        Turn.m13 = 0.0f;
        Turn.m14 = 0.0f;
        Vector3 AxisU = Vector3Transform(WorldModelSurface_GetAxisU(Entry->Surface), Turn);
        Vector3 AxisV = Vector3Transform(WorldModelSurface_GetAxisV(Entry->Surface), Turn);
        Vector3 Normal = Vector3Normalize(Vector3Transform(WorldModelSurface_GetNormal(Entry->Surface),
            MatrixTranspose(MatrixInvert(Turn))));
        WorldRendererScreenLight Light =
        {
            .ObjectId = Entry->ObjectId,
            .Position = Vector3Transform(WorldModelSurface_GetCenter(Entry->Surface), Entry->RenderMatrix),
            .Normal = Normal, .Area = Vector3Length(Vector3CrossProduct(AxisU, AxisV)),
            .Range = WorldModelSurface_GetLightRange(Entry->Surface), .Glow = Glow, .Whiteness = Whiteness,
            .Picture = Binding->Texture, .Level = GetMeanLevel(Binding->Texture)
        };
        if (!(Light.Area > 0.0f) || !isfinite(Light.Area) || !(Vector3Length(Normal) > 0.5f))
        {
            continue; // a glass flattened to nothing (a zero scale) gives no light
        }
        GenericBuffer_AddLast(&self->_surfaces._lights, &Light);
    }
}

const WorldRendererScreenLight* WorldRendererSurfaces_FindScreenLight(const WorldRenderer* self, Vector3 center,
    float radius)
{
    const WorldRendererScreenLight* Lights = (const WorldRendererScreenLight*)(const void*)self->_surfaces._lights._data;
    const WorldRendererScreenLight* Nearest = NULL;
    float NearestDistance = 0.0f;
    for (size_t Index = 0U; Index < self->_surfaces._lights._count; Index++)
    {
        const WorldRendererScreenLight* Light = &Lights[Index];
        Vector3 ToDraw = Vector3Subtract(center, Light->Position);
        float Distance = Vector3Length(ToDraw);
        if (((Distance - radius) >= Light->Range) || (Vector3DotProduct(ToDraw, Light->Normal) <= -radius))
        {
            continue; // out of its reach, or wholly behind its glass
        }
        if ((Nearest == NULL) || (Distance < NearestDistance))
        {
            Nearest = Light;
            NearestDistance = Distance;
        }
    }
    return Nearest;
}

void WorldRendererSurfaces_UploadScreenLight(const WorldRenderer* self, Shader shader,
    const WorldRendererScreenLightLocations* locations, Vector3 center, float radius)
{
    if (locations->_locActive < 0)
    {
        return;
    }
    const WorldRendererScreenLight* Nearest = WorldRendererSurfaces_FindScreenLight(self, center, radius);
    int Active = (Nearest != NULL) ? 1 : 0;
    SetShaderValue(shader, locations->_locActive, &Active, SHADER_UNIFORM_INT);
    if (Nearest == NULL)
    {
        return;
    }
    float Shape[4] = { Nearest->Area, Nearest->Range, Nearest->Level, Nearest->Whiteness };
    SetShaderValue(shader, locations->_locPosition, &Nearest->Position, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locNormal, &Nearest->Normal, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locShape, Shape, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, locations->_locGlow, &Nearest->Glow, SHADER_UNIFORM_FLOAT);
    rlActiveTextureSlot(WorldRendererTextureUnit_ScreenLight);
    rlEnableTexture(Nearest->Picture.id);
    rlActiveTextureSlot(0);
}
