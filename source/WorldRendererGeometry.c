// Drawing of world model objects and light gizmos for the renderer's passes, from the render cache's lists. Meshes draw
// with materials built on the stack, so no pass writes into a shared model asset. A model is drawn one LOD at a time:
// the lit passes draw both LODs of a cross-fade with complementary dithers (WorldLod.h). A posed object's skinned meshes
// draw with each pass's skinned shader and the pose's palette. Sprites have their own file (WorldRendererSprites.c).

#include "WorldRendererInternal.h"
#include "GameSkeleton.h"
#include "wr/WRMemory.h"


// Macros.
/** Radius of the debug wireframe gizmo drawn at each light's position. */
#define LIGHT_GIZMO_RADIUS 0.2f
/** How many colours the LOD tinting has: one per LOD up to the last, which every coarser LOD shares. */
#define LOD_TINT_COLOR_COUNT 5U
/** How strongly a tinted LOD past LOD 0 glows in its colour (linear emissive intensity): faint by day, where the
 *  albedo's tint shows, and clear at night, where the eye's night vision greys reflected colour but not emitted light. */
#define LOD_TINT_GLOW_INTENSITY 0.05f
/** Asset name of the unlit fallback's skinned shader. */
#define UNLIT_SKINNED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_unlit_skinned")


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
/* How a LOD is drawn: with the object's tint, and while the LOD tinting is on, times the LOD's debug colour, in which a
 * LOD past LOD 0 also glows faintly. */
static LodLook GetLodLook(const WorldRenderer* self, const WorldRenderModel* model, size_t lod)
{
    if (!self->_isLodTintingEnabled)
    {
        return (LodLook){ .Tint = model->Tint, .HasGlow = false, .Glow = { 0, 0, 0, 255 } };
    }
    Color Debug = LodTintColors[(lod < LOD_TINT_COLOR_COUNT) ? lod : (LOD_TINT_COLOR_COUNT - 1U)];
    return (LodLook){ .Tint = WorldRendererGeometry_MultiplyColor(model->Tint, Debug), .HasGlow = lod > 0U,
        .Glow = Debug };
}


/* Whether a LOD's skinned meshes can bend with the entry's pose in a pass: the entry has a pose, the LOD's model has a
 * skeleton with the pose's bones (every skinned LOD of a world model shares its bind poses, WorldModel.h), and the pass's
 * skinned shader holds the pose's palette (uploaded here when it does not yet). */
static bool CanSkin(const WorldRenderModel* model, const GameSkeleton* skeleton, const WorldRendererMeshShaders* shaders)
{
    // The camera's passes draw the first-person object without the bones it hides; a light's see all of it.
    const WorldAnimatorPose* Pose = shaders->IsCameraView ? model->CameraPose : model->Pose;
    return (Pose != NULL) && (skeleton != NULL) && (shaders->Skinned != NULL) && (shaders->Palette != NULL)
        && (GameSkeleton_GetBoneCount(skeleton) == Pose->_boneCount)
        && WorldRendererCommon_UploadPalette(*shaders->Skinned, shaders->Palette, Pose);
}


// Internal functions (declared in WorldRendererInternal.h).
Color WorldRendererGeometry_MultiplyColor(Color a, Color b)
{
    Color Result;
    Result.r = (unsigned char)(((int)a.r * (int)b.r) / 255);
    Result.g = (unsigned char)(((int)a.g * (int)b.g) / 255);
    Result.b = (unsigned char)(((int)a.b * (int)b.b) / 255);
    Result.a = (unsigned char)(((int)a.a * (int)b.a) / 255);
    return Result;
}

/* The asset material's maps are copied, so the variant (its maps and factors for this material slot), the tint, the LOD
 * tinting's glow and the pass shader land on the copy and nothing is written to the shared assets (raylib's DrawModel
 * instead writes the tint into the asset's maps for the duration of the draw). DrawMesh still binds the maps' textures,
 * through the shader's location table. */
Material WorldRendererGeometry_PrepareMaterial(const WorldRenderer* self, const WorldRenderModel* model, Model rayModel,
    int meshIndex, size_t lod, const Shader* passShader, const WorldRendererPbrProgram* litProgram,
    MaterialMap* outMaps)
{
    LodLook Look = GetLodLook(self, model, lod);
    int MaterialIndex = rayModel.meshMaterial[meshIndex];
    const Material* AssetMaterial = &rayModel.materials[MaterialIndex];
    Memory_Copy(AssetMaterial->maps, outMaps, WORLD_RENDERER_MATERIAL_MAP_COUNT * sizeof(MaterialMap));
    if (model->HasVariant)
    {
        WorldModel_ApplyVariant(model->Asset, model->VariantIndex, MaterialIndex, outMaps);
    }
    if (Look.HasGlow)
    {
        outMaps[MATERIAL_MAP_EMISSION].texture.id = 0U; // the debug glow replaces the material's own emission
        outMaps[MATERIAL_MAP_EMISSION].color = Look.Glow;
        outMaps[MATERIAL_MAP_EMISSION].value = LOD_TINT_GLOW_INTENSITY;
    }

    Material DrawMaterial = *AssetMaterial;
    DrawMaterial.maps = outMaps;
    if (litProgram != NULL)
    {
        WorldRendererLighting_UploadMaterial(litProgram, &DrawMaterial);
    }
    outMaps[MATERIAL_MAP_ALBEDO].color = WorldRendererGeometry_MultiplyColor(outMaps[MATERIAL_MAP_ALBEDO].color,
        Look.Tint);
    if (passShader != NULL)
    {
        DrawMaterial.shader = *passShader;
    }
    return DrawMaterial;
}

void WorldRendererGeometry_LoadShaders(WorldRenderer* self)
{
    self->_unlit._skinnedShader = WorldRendererCommon_LoadShader(self, UNLIT_SKINNED_SHADER_ASSET_NAME);
    self->_unlit._skinnedPalette = (WorldRendererPalette){ ._location = -1 };
    if (self->_unlit._skinnedShader != NULL)
    {
        WorldRendererCommon_PreparePalette(GameShader_GetRaylibShader(self->_unlit._skinnedShader),
            &self->_unlit._skinnedPalette);
    }
}

void WorldRendererGeometry_ResetPalettes(WorldRenderer* self)
{
    WorldRendererPalette* Palettes[] =
    {
        &self->_pbr._skinned._palette, &self->_unlit._skinnedPalette, &self->_gbuffer._skinnedPalette,
        &self->_cascades._skinnedPalette, &self->_pointShadow._skinnedPalette
    };
    for (size_t Index = 0U; Index < (sizeof(Palettes) / sizeof(Palettes[0])); Index++)
    {
        Palettes[Index]->_pose = NULL;
        Palettes[Index]->_revision = 0U;
    }
}

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
    const WorldRendererMeshShaders* shaders, bool isLit)
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
    const GameSkeleton* Skeleton = GameModel_GetSkeleton(LodModel);
    bool IsPosed = CanSkin(model, Skeleton, shaders);
    for (int MeshIndex = 0; MeshIndex < RayModel.meshCount; MeshIndex++)
    {
        bool IsSkinned = IsPosed && GameSkeleton_IsMeshSkinned(Skeleton, (uint32_t)MeshIndex);
        const WorldRendererPbrProgram* LitProgram = NULL;
        if (isLit)
        {
            LitProgram = IsSkinned ? &self->_pbr._skinned : &self->_pbr._rigid;
        }
        MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
        Material DrawMaterial = WorldRendererGeometry_PrepareMaterial(self, model, RayModel, MeshIndex, lod,
            IsSkinned ? shaders->Skinned : shaders->Rigid, LitProgram, Maps);
        DrawMesh(RayModel.meshes[MeshIndex], DrawMaterial, LodTransform);
    }
}

void WorldRendererGeometry_DrawModels(WorldRenderer* self, WorldRendererPixelFilter filter)
{
    // The lit pass draws with the PBR programs; without them the materials' own shaders draw (the unlit skinned one for
    // skinned meshes).
    bool IsLit = (self->_pbr._rigid._shader != NULL);
    bool HasLitSkinned = IsLit && (self->_pbr._skinned._shader != NULL);
    Shader UnlitSkinned = (self->_unlit._skinnedShader != NULL)
        ? GameShader_GetRaylibShader(self->_unlit._skinnedShader) : (Shader){ 0 };
    WorldRendererMeshShaders Shaders = IsLit
        ? (WorldRendererMeshShaders){ .Rigid = &self->_pbr._rigid._passShader,
            .Skinned = HasLitSkinned ? &self->_pbr._skinned._passShader : NULL, .Palette = &self->_pbr._skinned._palette,
            .IsCameraView = true }
        : (WorldRendererMeshShaders){ .Rigid = NULL, .Skinned = (self->_unlit._skinnedShader != NULL) ? &UnlitSkinned
            : NULL, .Palette = &self->_unlit._skinnedPalette, .IsCameraView = true };
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
        if (!IsLit)
        {
            WorldRendererGeometry_DrawModel(self, Entry, WorldLodState_GetSolidLod(&Entry->Lod), &Shaders, false);
            continue;
        }

        // The lit pass reach-culls the object's point lights and uploads them once for its LODs, then dithers each LOD
        // by the object's fade (its draw distance, its opacity) and by its part of a LOD cross-fade: the two LODs of a
        // cross-fade keep complementary pixels. A LOD's meshes share its dither. A posed object's skinned meshes draw
        // with the skinned program, which gets the same.
        bool IsPosed = HasLitSkinned && (Entry->CameraPose != NULL);
        WorldRendererLighting_UploadObjectLights(self, self->_pbr._rigid._passShader, &self->_pbr._rigid._lit,
            Entry->RenderCenter, Entry->Radius);
        WorldRendererLighting_SetDrawHaze(self->_pbr._rigid._passShader, &self->_pbr._rigid._lit, Entry->Haze);
        if (IsPosed)
        {
            WorldRendererLighting_UploadObjectLights(self, self->_pbr._skinned._passShader, &self->_pbr._skinned._lit,
                Entry->RenderCenter, Entry->Radius);
            WorldRendererLighting_SetDrawHaze(self->_pbr._skinned._passShader, &self->_pbr._skinned._lit, Entry->Haze);
        }
        float Fade = WorldVisibility_GetModelFade(&self->_visibility, Index).Combined;
        WorldLodDraw Draws[WORLD_LOD_MAX_DRAWS];
        size_t DrawCount = WorldLodState_GetDraws(&Entry->Lod, Draws);
        for (size_t Draw = 0; Draw < DrawCount; Draw++)
        {
            WorldRendererCommon_SetDither(self->_pbr._rigid._passShader, &self->_pbr._rigid._dither, Fade,
                Draws[Draw].Progress, Draws[Draw].IsIncoming);
            if (IsPosed)
            {
                WorldRendererCommon_SetDither(self->_pbr._skinned._passShader, &self->_pbr._skinned._dither, Fade,
                    Draws[Draw].Progress, Draws[Draw].IsIncoming);
            }
            WorldRendererGeometry_DrawModel(self, Entry, Draws[Draw].Lod, &Shaders, true);
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
