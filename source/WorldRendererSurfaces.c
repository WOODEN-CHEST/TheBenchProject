// The surfaces' pictures: what the code that owns a screen's content (a UI drawn into a render target) hands the
// renderer for one render (WorldRenderer_SetSurfaceTexture), and putting it into the per-draw copy of the surface's
// material, never into an asset: a screen's picture as its emissive map, a paper's as its albedo map.

#include <stdint.h>
#include "WorldRendererInternal.h"
#include "wr/WRString.h"


// Macros.
/** An index that names no picture. */
#define NO_BINDING SIZE_MAX


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

void WorldRendererSurfaces_Count(const WorldRenderer* self, uint32_t* outSurfaceCount, uint32_t* outPictureCount)
{
    uint32_t Surfaces = 0U;
    uint32_t Pictures = 0U;
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
    }
    *outSurfaceCount = Surfaces;
    *outPictureCount = Pictures;
}
