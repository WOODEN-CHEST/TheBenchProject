// Drawing of world model objects and light gizmos for the renderer's passes, from the render cache's lists. Meshes draw
// with materials built on the stack, so no pass writes into a shared model asset. Sprites have their own file
// (WorldRendererSprites.c).

#include "WorldRendererInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** Radius of the debug wireframe gizmo drawn at each light's position. */
#define LIGHT_GIZMO_RADIUS 0.2f


// Static functions.
/* Per-channel 0..255 colour multiply (a*b/255), matching how raylib's DrawModel folds the object tint into a
 * material's albedo colour. */
static Color MultiplyColor(Color a, Color b)
{
    Color Result;
    Result.r = (unsigned char)(((int)a.r * (int)b.r) / 255);
    Result.g = (unsigned char)(((int)a.g * (int)b.g) / 255);
    Result.b = (unsigned char)(((int)a.b * (int)b.b) / 255);
    Result.a = (unsigned char)(((int)a.a * (int)b.a) / 255);
    return Result;
}

/* Draws one mesh of a model entry with a material built on the stack. The asset material's maps are copied, so the
 * variant (its maps and factors for this material slot), the tint and the pass shader land on the copy and nothing is
 * written to the shared assets (raylib's DrawModel instead writes the tint into the asset's maps for the duration of
 * the draw). The lit pass uploads the copy's PBR factors and map flags before the draw. A NULL @p passShader keeps the
 * material's own shader. DrawMesh still binds the maps' textures, through the shader's location table. */
static void DrawMeshWithCopiedMaterial(WorldRenderer* self, const WorldRenderModel* model, Model rayModel,
    int meshIndex, const Shader* passShader, bool isLit)
{
    int MaterialIndex = rayModel.meshMaterial[meshIndex];
    const Material* AssetMaterial = &rayModel.materials[MaterialIndex];
    MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
    Memory_Copy(AssetMaterial->maps, Maps, sizeof(Maps));
    if (model->HasVariant)
    {
        WorldModel_ApplyVariant(model->Asset, model->VariantIndex, MaterialIndex, Maps);
    }

    Material DrawMaterial = *AssetMaterial;
    DrawMaterial.maps = Maps;
    if (isLit)
    {
        WorldRendererLighting_UploadMaterial(self, &DrawMaterial);
    }
    Maps[MATERIAL_MAP_ALBEDO].color = MultiplyColor(Maps[MATERIAL_MAP_ALBEDO].color, model->Tint);
    if (passShader != NULL)
    {
        DrawMaterial.shader = *passShader;
    }
    DrawMesh(rayModel.meshes[meshIndex], DrawMaterial, model->Transform);
}


// Internal functions (declared in WorldRendererInternal.h).
bool WorldRendererGeometry_PassesFilter(bool omitPixelation, WorldRendererPixelFilter filter)
{
    switch (filter)
    {
        case WorldRendererPixelFilter_PixelatedOnly: return !omitPixelation;
        case WorldRendererPixelFilter_CrispOnly:     return omitPixelation;
        case WorldRendererPixelFilter_All:           return true;
    }
    // Only reachable with a corrupted filter value; drawing the object is the safe choice.
    return true;
}

void WorldRendererGeometry_DrawModel(WorldRenderer* self, const WorldRenderModel* model, const Shader* passShader,
    bool isLit)
{
    // Lit (PBR) pass: reach-cull this object's point lights and upload them before its meshes. The depth and
    // normal passes do not light, so they skip this.
    if (isLit)
    {
        WorldRendererLighting_UploadObjectLights(self, GameShader_GetRaylibShader(self->_pbr._shader), &self->_pbr._lit,
            model->Center, model->Radius);
    }

    // Mesh by mesh, so each material's PBR factors and map flags can be uploaded before its draw (raylib's
    // DrawModel cannot do that). The transform is the LOD's import transform inside the object's world
    // transform, matching raylib's DrawModelEx convention.
    Model RayModel = GameModel_GetRaylibModel(model->Model);
    for (int MeshIndex = 0; MeshIndex < RayModel.meshCount; MeshIndex++)
    {
        DrawMeshWithCopiedMaterial(self, model, RayModel, MeshIndex, passShader, isLit);
    }
}

void WorldRendererGeometry_DrawModels(WorldRenderer* self, const Shader* passShader, bool isLit,
    WorldRendererPixelFilter filter)
{
    size_t ModelCount = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    size_t VisibleCount = 0;
    const size_t* Visible = WorldVisibility_GetModels(&self->_visibility, &self->_cache, WorldVisibilityPass_Camera,
        &VisibleCount);
    for (size_t Position = 0; Position < VisibleCount; Position++)
    {
        size_t Index = Visible[Position];
        const WorldRenderModel* Entry = &Models[Index];
        if (!WorldRendererGeometry_PassesFilter(Entry->OmitsPixelation, filter))
        {
            continue;
        }
        // The lit pass dithers the model by its fade (its draw distance, its opacity); its meshes share it. Without the
        // PBR shader the materials' own shaders draw it solid.
        if (isLit)
        {
            WorldRendererCommon_SetDither(self->_pbr._passShader, &self->_pbr._dither,
                WorldVisibility_GetModelFade(&self->_visibility, Index).Combined, false);
        }
        WorldRendererGeometry_DrawModel(self, Entry, passShader, isLit);
    }
}

void WorldRendererGeometry_DrawLightGizmos(WorldRenderer* self)
{
    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    for (size_t Index = 0; Index < LightCount; Index++)
    {
        DrawSphereWires(Lights[Index].Position, LIGHT_GIZMO_RADIUS, 6, 6, Lights[Index].Color);
    }
}
