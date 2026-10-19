// Drawing of world model objects, sprite billboards and light gizmos for the renderer's passes, from the render
// cache's lists. Meshes draw with materials built on the stack, so no pass writes into a shared model asset.

#include "WorldRendererInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** Radius of the debug wireframe gizmo drawn at each light's position. */
#define LIGHT_GIZMO_RADIUS 0.2f
/** Asset name of the sprite shader: draws 2D sprite billboards, linearizing their sRGB art into the HDR scene. */
#define SPRITE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sprite")


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

/* Draws one mesh with a material built on the stack. The asset material's maps are copied, so the tint and the
 * pass shader land on the copy and nothing is written to the shared asset (raylib's DrawModel instead writes the
 * tint into the asset's maps for the duration of the draw). A NULL @p passShader keeps the material's own shader.
 * DrawMesh still binds the maps' textures, through the shader's location table. */
static void DrawMeshWithCopiedMaterial(Mesh mesh, const Material* assetMaterial, const Shader* passShader,
    Color tint, Matrix transform)
{
    MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
    Memory_Copy(assetMaterial->maps, Maps, sizeof(Maps));
    Maps[MATERIAL_MAP_ALBEDO].color = MultiplyColor(Maps[MATERIAL_MAP_ALBEDO].color, tint);

    Material DrawMaterial = *assetMaterial;
    DrawMaterial.maps = Maps;
    if (passShader != NULL)
    {
        DrawMaterial.shader = *passShader;
    }
    DrawMesh(mesh, DrawMaterial, transform);
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererGeometry_LoadShaders(WorldRenderer* self)
{
    // Sprite shader (linearizes sprite sRGB art into the HDR scene). Optional: without it sprites fall back to
    // Raylib's default (unlit) shader; it needs no cached uniform locations (texture0/colDiffuse are built-in).
    self->_sprites._shader = WorldRendererCommon_LoadShader(self, SPRITE_SHADER_ASSET_NAME);
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

void WorldRendererGeometry_DrawModel(WorldRenderer* self, const WorldRenderModel* model, const Shader* passShader,
    bool isLit)
{
    // Lit (PBR) pass: reach-cull this object's point lights and upload them before its meshes. The depth and
    // normal passes do not light, so they skip this.
    if (isLit)
    {
        WorldRendererLighting_UploadObjectLights(self, model->Center, model->Radius);
    }

    // Mesh by mesh, so each material's PBR factors and map flags can be uploaded before its draw (raylib's
    // DrawModel cannot do that). The transform is the asset's import transform inside the object's world
    // transform, matching raylib's DrawModelEx convention.
    Model RayModel = GameModel_GetRaylibModel(model->Asset);
    for (int MeshIndex = 0; MeshIndex < RayModel.meshCount; MeshIndex++)
    {
        const Material* AssetMaterial = &RayModel.materials[RayModel.meshMaterial[MeshIndex]];
        if (isLit)
        {
            WorldRendererLighting_UploadMaterial(self, AssetMaterial);
        }
        DrawMeshWithCopiedMaterial(RayModel.meshes[MeshIndex], AssetMaterial, passShader, model->Tint,
            model->Transform);
    }
}

void WorldRendererGeometry_DrawModels(WorldRenderer* self, const Shader* passShader, bool isLit,
    WorldRendererPixelFilter filter)
{
    size_t ModelCount = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    for (size_t Index = 0; Index < ModelCount; Index++)
    {
        const WorldRenderModel* Entry = &Models[Index];
        if (Entry->IsDrawn && WorldRendererGeometry_PassesFilter(Entry->OmitsPixelation, filter))
        {
            WorldRendererGeometry_DrawModel(self, Entry, passShader, isLit);
        }
    }
}

void WorldRendererGeometry_DrawSprites(WorldRenderer* self, const WorldRenderFrame* frame,
    WorldRendererPixelFilter filter)
{
    if (self->_sprites._shader != NULL)
    {
        BeginShaderMode(GameShader_GetRaylibShader(self->_sprites._shader));
    }

    // Each sprite draws its playback instance's CURRENT frame (advanced by game logic), sized by the object's
    // X/Y scale and tinted by its tint.
    size_t SpriteCount = 0;
    const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(&self->_cache, &SpriteCount);
    for (size_t Index = 0; Index < SpriteCount; Index++)
    {
        const WorldRenderSprite* Entry = &Sprites[Index];
        if (WorldRendererGeometry_PassesFilter(Entry->OmitsPixelation, filter))
        {
            DrawBillboardRec(frame->RayCamera, Entry->Frame._texture, Entry->Frame._areaInTexture, Entry->Position,
                Entry->Size, Entry->Tint);
        }
    }

    if (self->_sprites._shader != NULL)
    {
        EndShaderMode();
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
