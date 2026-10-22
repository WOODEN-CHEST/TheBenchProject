// Drawing of world model objects and light gizmos for the renderer's passes, from the render cache's lists. Meshes draw
// with materials built on the stack, so no pass writes into a shared model asset. A model is drawn one LOD at a time:
// the lit passes draw both LODs of a cross-fade with complementary dithers (WorldLod.h). Sprites have their own file
// (WorldRendererSprites.c).

#include "WorldRendererInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** Radius of the debug wireframe gizmo drawn at each light's position. */
#define LIGHT_GIZMO_RADIUS 0.2f
/** How many colours the LOD tinting has: one per LOD up to the last, which every coarser LOD shares. */
#define LOD_TINT_COLOR_COUNT 5U
/** How strongly a tinted LOD past LOD 0 glows in its colour (linear emissive intensity): faint by day, where the
 *  albedo's tint shows, and clear at night, where the eye's night vision greys reflected colour but not emitted light. */
#define LOD_TINT_GLOW_INTENSITY 0.05f


// Types.
/* How one LOD of a model is drawn: the colour multiplied into its albedo (the object's tint, times the LOD's debug colour
 * while the LOD tinting is on) and, for a tinted LOD past LOD 0, a glow in its debug colour. */
typedef struct LodLookStruct
{
    Color Tint;
    bool HasGlow;
    Color Glow;
} LodLook;


// Fields.
/* The LOD tinting's colours (WorldRenderer_SetLodTintingEnabled), multiplied into each LOD's albedo: LOD 0 white (its own
 * colours), 1 green, 2 yellow, 3 red, coarser magenta. */
static const Color LodTintColors[LOD_TINT_COLOR_COUNT] =
{
    { 255, 255, 255, 255 }, { 0, 255, 0, 255 }, { 255, 255, 0, 255 }, { 255, 0, 0, 255 }, { 255, 0, 255, 255 }
};


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

/* Draws one mesh of a model entry's LOD with a material built on the stack. The asset material's maps are copied, so the
 * variant (its maps and factors for this material slot), the tint, the LOD tinting's glow and the pass shader land on the
 * copy and nothing is written to the shared assets (raylib's DrawModel instead writes the tint into the asset's maps for
 * the duration of the draw). The lit pass uploads the copy's PBR factors and map flags before the draw. A NULL
 * @p passShader keeps the material's own shader. DrawMesh still binds the maps' textures, through the shader's location
 * table. */
static void DrawMeshWithCopiedMaterial(WorldRenderer* self, const WorldRenderModel* model, Model rayModel,
    int meshIndex, Matrix transform, const LodLook* look, const Shader* passShader, bool isLit)
{
    int MaterialIndex = rayModel.meshMaterial[meshIndex];
    const Material* AssetMaterial = &rayModel.materials[MaterialIndex];
    MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
    Memory_Copy(AssetMaterial->maps, Maps, sizeof(Maps));
    if (model->HasVariant)
    {
        WorldModel_ApplyVariant(model->Asset, model->VariantIndex, MaterialIndex, Maps);
    }
    if (look->HasGlow)
    {
        Maps[MATERIAL_MAP_EMISSION].texture.id = 0U; // the debug glow replaces the material's own emission
        Maps[MATERIAL_MAP_EMISSION].color = look->Glow;
        Maps[MATERIAL_MAP_EMISSION].value = LOD_TINT_GLOW_INTENSITY;
    }

    Material DrawMaterial = *AssetMaterial;
    DrawMaterial.maps = Maps;
    if (isLit)
    {
        WorldRendererLighting_UploadMaterial(self, &DrawMaterial);
    }
    Maps[MATERIAL_MAP_ALBEDO].color = MultiplyColor(Maps[MATERIAL_MAP_ALBEDO].color, look->Tint);
    if (passShader != NULL)
    {
        DrawMaterial.shader = *passShader;
    }
    DrawMesh(rayModel.meshes[meshIndex], DrawMaterial, transform);
}

/* How a LOD is drawn: with the object's tint, and while the LOD tinting is on, times the LOD's debug colour, in which a
 * LOD past LOD 0 also glows faintly. */
static LodLook GetLodLook(const WorldRenderer* self, const WorldRenderModel* model, size_t lod)
{
    if (!self->_isLodTintingEnabled)
    {
        return (LodLook){ .Tint = model->Tint, .HasGlow = false, .Glow = { 0, 0, 0, 255 } };
    }
    Color Debug = LodTintColors[(lod < LOD_TINT_COLOR_COUNT) ? lod : (LOD_TINT_COLOR_COUNT - 1U)];
    return (LodLook){ .Tint = MultiplyColor(model->Tint, Debug), .HasGlow = lod > 0U, .Glow = Debug };
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

void WorldRendererGeometry_DrawModel(WorldRenderer* self, const WorldRenderModel* model, size_t lod,
    const Shader* passShader, bool isLit)
{
    GameModel* LodModel = WorldRenderModel_GetLodModel(model, lod);
    if (LodModel == NULL)
    {
        return; // a LOD past the last: the cache's state keeps its LODs below the count
    }
    // Mesh by mesh, so each material's PBR factors and map flags can be uploaded before its draw (raylib's DrawModel
    // cannot do that). The transform is the LOD's import transform inside the object's render-space matrix, matching
    // raylib's DrawModelEx convention.
    Model RayModel = GameModel_GetRaylibModel(LodModel);
    Matrix LodTransform = WorldRenderModel_GetLodTransform(model, lod);
    LodLook Look = GetLodLook(self, model, lod);
    for (int MeshIndex = 0; MeshIndex < RayModel.meshCount; MeshIndex++)
    {
        DrawMeshWithCopiedMaterial(self, model, RayModel, MeshIndex, LodTransform, &Look, passShader, isLit);
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
        // Without the PBR shader the materials' own shaders draw the model solid: one LOD, the one with the larger share.
        if (!isLit)
        {
            WorldRendererGeometry_DrawModel(self, Entry, WorldLodState_GetSolidLod(&Entry->Lod), passShader, false);
            continue;
        }

        // The lit pass reach-culls the object's point lights and uploads them once for its LODs, then dithers each LOD
        // by the object's fade (its draw distance, its opacity) and by its part of a LOD cross-fade: the two LODs of a
        // cross-fade keep complementary pixels. A LOD's meshes share its dither.
        WorldRendererLighting_UploadObjectLights(self, GameShader_GetRaylibShader(self->_pbr._shader), &self->_pbr._lit,
            Entry->RenderCenter, Entry->Radius);
        float Fade = WorldVisibility_GetModelFade(&self->_visibility, Index).Combined;
        WorldLodDraw Draws[WORLD_LOD_MAX_DRAWS];
        size_t DrawCount = WorldLodState_GetDraws(&Entry->Lod, Draws);
        for (size_t Draw = 0; Draw < DrawCount; Draw++)
        {
            WorldRendererCommon_SetDither(self->_pbr._passShader, &self->_pbr._dither, Fade, Draws[Draw].Progress,
                Draws[Draw].IsIncoming);
            WorldRendererGeometry_DrawModel(self, Entry, Draws[Draw].Lod, passShader, true);
        }
    }
}

void WorldRendererGeometry_DrawLightGizmos(WorldRenderer* self)
{
    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    for (size_t Index = 0; Index < LightCount; Index++)
    {
        DrawSphereWires(Lights[Index].RenderPosition, LIGHT_GIZMO_RADIUS, 6, 6, Lights[Index].Color);
    }
}
