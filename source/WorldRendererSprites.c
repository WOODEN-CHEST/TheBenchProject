// The sprites: the unit quad every sprite draws with, the lit sprite shader (lights, shadows, haze, night vision; cutout,
// blended and additive alpha), each frame's sprite matrices (facing the camera, and facing each shadow light), and the
// lit draws of the scene and crisp passes (the sprites the camera's visibility list holds): the cutout sprites, then the
// decals, then the blended and additive sprites back to front. The G-buffer and shadow passes draw sprites through this
// file's quad and matrices with their own shaders.

#include <math.h>
#include "WorldRendererInternal.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"


// Macros.
/** Asset name of the lit sprite shader (vertex + fragment). */
#define SPRITE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sprite")
/** Initial capacity (in sprites) of the per-frame lists; they grow with the world. */
#define INITIAL_SPRITE_CAPACITY 32
/** The quad: two triangles, unindexed (raylib's DrawMesh draws indexed only while the CPU index array is kept, and the
 *  CPU arrays are freed after the upload). */
#define QUAD_VERTEX_COUNT 6


// Fields.
/* The quad's two triangles (x, y in [0, 1]: the image from left to right and bottom to top), counter-clockwise seen
 * from +Z (its front; sprites draw with culling off anyway). */
static const float QuadCorners[QUAD_VERTEX_COUNT][2] =
{
    { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f },
    { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f },
};
/* World up: the lights' up for billboards in their shadow passes, so a billboard's shadow stands upright. */
static const Vector3 WorldUp = { 0.0f, 1.0f, 0.0f };


// Static functions.
/* Frees the CPU arrays the quad was built from; the GPU keeps its copy. The pointers are cleared, so UnloadMesh later
 * frees only what raylib allocated itself (the buffer ids) and the GPU objects. */
static void FreeQuadArrays(Mesh* mesh)
{
    Memory_Free(mesh->vertices);
    Memory_Free(mesh->texcoords);
    Memory_Free(mesh->normals);
    mesh->vertices = NULL;
    mesh->texcoords = NULL;
    mesh->normals = NULL;
}

/* Builds and uploads the unit quad: x, y in [0, 1] and z 0, the normal +Z, the UVs (x, 1 - y) so v is 0 at the image's
 * top row (as raylib stores textures). false when the GPU did not take it (then nothing stays allocated). */
static bool BuildQuad(Mesh* outMesh)
{
    Mesh Quad = { 0 };
    Quad.vertexCount = QUAD_VERTEX_COUNT;
    Quad.triangleCount = QUAD_VERTEX_COUNT / 3;
    Quad.vertices = Memory_Allocate(QUAD_VERTEX_COUNT * 3U * sizeof(float));
    Quad.texcoords = Memory_Allocate(QUAD_VERTEX_COUNT * 2U * sizeof(float));
    Quad.normals = Memory_Allocate(QUAD_VERTEX_COUNT * 3U * sizeof(float));
    for (size_t Corner = 0; Corner < QUAD_VERTEX_COUNT; Corner++)
    {
        Quad.vertices[Corner * 3U + 0U] = QuadCorners[Corner][0];
        Quad.vertices[Corner * 3U + 1U] = QuadCorners[Corner][1];
        Quad.vertices[Corner * 3U + 2U] = 0.0f;
        Quad.texcoords[Corner * 2U + 0U] = QuadCorners[Corner][0];
        Quad.texcoords[Corner * 2U + 1U] = 1.0f - QuadCorners[Corner][1];
        Quad.normals[Corner * 3U + 0U] = 0.0f;
        Quad.normals[Corner * 3U + 1U] = 0.0f;
        Quad.normals[Corner * 3U + 2U] = 1.0f;
    }

    UploadMesh(&Quad, false);
    FreeQuadArrays(&Quad);
    if (Quad.vaoId == 0U)
    {
        UnloadMesh(Quad);
        return false;
    }
    *outMesh = Quad;
    return true;
}

/* Loads the lit sprite shader and caches its locations: the frame lighting through the shared preparation, then the
 * sprite's own uniforms. */
static void LoadLitShader(WorldRenderer* self)
{
    self->_sprites._shader = WorldRendererCommon_LoadShader(self, SPRITE_SHADER_ASSET_NAME);
    if (self->_sprites._shader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(self->_sprites._shader);
    WorldRendererLighting_PrepareLitShader(self, RayShader, SPRITE_SHADER_ASSET_NAME, &self->_sprites._lit);
    self->_sprites._locUvRect = GetShaderLocation(RayShader, "uvRect");
    self->_sprites._locShadowModel = GetShaderLocation(RayShader, "matShadowModel");
    self->_sprites._locPointShadowModel = GetShaderLocation(RayShader, "matPointShadowModel");
    self->_sprites._locDecalOffset = GetShaderLocation(RayShader, "decalOffset");
    self->_sprites._locAlphaMode = GetShaderLocation(RayShader, "alphaMode");
    self->_sprites._locAlphaCutoff = GetShaderLocation(RayShader, "alphaCutoff");
    self->_sprites._locIsLit = GetShaderLocation(RayShader, "isLit");
    self->_sprites._locEmission = GetShaderLocation(RayShader, "emission");
    self->_sprites._locLightWrap = GetShaderLocation(RayShader, "lightWrap");
    self->_sprites._locIsCard = GetShaderLocation(RayShader, "isCard");
    self->_sprites._locUseScreenAo = GetShaderLocation(RayShader, "useScreenAo");
    WorldRendererCommon_PrepareDither(RayShader, &self->_sprites._dither);
}

/* The cube-shadow light of the frame, or NULL while the cube shadow does not run. */
static const WorldRenderLight* GetPointShadowLight(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    return (frame->IsPointShadowActive && (frame->ShadowLightIndex < LightCount)) ? &Lights[frame->ShadowLightIndex]
        : NULL;
}

/* Whether a sprite is drawn in the sorted transparent part of the lit draws: blended or additive, and not a decal
 * (decals keep their order and are drawn before it). */
static bool IsSortedTransparent(const WorldRenderSprite* sprite)
{
    return !sprite->IsDecal && (sprite->AlphaMode != WorldSpriteAlphaMode_Cutout);
}

/* The blending of a sprite's alpha mode: cutout writes opaque texels (alpha 1) with the default blend, blended sprites
 * write premultiplied colour over what is behind, additive ones add their light without touching the scene's alpha. */
static int GetBlendMode(WorldSpriteAlphaMode alphaMode)
{
    switch (alphaMode)
    {
        case WorldSpriteAlphaMode_Cutout:   return BLEND_ALPHA;
        case WorldSpriteAlphaMode_Blend:    return BLEND_ALPHA_PREMULTIPLY;
        case WorldSpriteAlphaMode_Additive: return BLEND_ADD_COLORS;
    }
    return BLEND_ALPHA; // only for a corrupted mode: drawn like a cutout sprite
}

/* How a sprite shows its fades. A cutout sprite dithers by its combined fade (its draw distance, the object's opacity),
 * like a model. A blended or additive sprite is transparent already: its draw distance fades its alpha (the object's
 * opacity is in its alpha already) and it does not dither. */
static void GetFadedDraw(const WorldRenderSprite* sprite, WorldVisibilityFade fade, float* outDitherFade,
    Color* outTint)
{
    *outTint = sprite->Tint;
    if (sprite->AlphaMode == WorldSpriteAlphaMode_Cutout)
    {
        *outDitherFade = fade.Combined;
        return;
    }
    *outDitherFade = 0.0f;
    float Alpha = (float)sprite->Tint.a * (1.0f - fminf(fmaxf(fade.Distance, 0.0f), 1.0f));
    outTint->a = (unsigned char)lroundf(Alpha);
}

/* Uploads one sprite's settings, its fade and its forward point lights to the lit shader and draws it facing the camera.
 * With sprite lighting switched off (the A/B toggle) every sprite draws unlit at its full colours. */
static void DrawLitSprite(WorldRenderer* self, const WorldRenderSprite* sprite, const WorldRendererSpriteFrame* matrices,
    WorldVisibilityFade fade, bool applyScreenAo)
{
    Shader RayShader = GameShader_GetRaylibShader(self->_sprites._shader);
    bool IsLit = sprite->IsLit && self->_isSpriteLightingEnabled;
    float Emission = sprite->IsLit ? 1.0f : sprite->Emission;
    int AlphaMode = (int)sprite->AlphaMode;
    int IsLitValue = IsLit ? 1 : 0;
    // Wrap lighting only for billboards (round things seen from the front); cards are lit as cards.
    bool IsCard = sprite->Shape.Orientation == WorldSpriteOrientation_Fixed;
    float LightWrap = IsCard ? 0.0f : sprite->LightWrap;
    // Interior lighting reads a card's sky by its facing and a billboard's as a thing standing there.
    int IsCardValue = IsCard ? 1 : 0;
    float DecalOffset = sprite->IsDecal ? sprite->DecalOffset : 0.0f;
    // The AO at the pixel is the sprite's own for a cutout sprite (when the G-buffer drew it: its shader is optional) and
    // its surface's for a decal; behind a blended or additive sprite it belongs to something else.
    bool IsInAo = sprite->IsDecal
        || (WorldRenderSprite_IsOpaqueSurface(sprite) && (self->_gbuffer._spriteShader != NULL));
    int UseScreenAo = (applyScreenAo && IsInAo) ? 1 : 0;
    float DitherFade = 0.0f;
    Color Tint = sprite->Tint;
    GetFadedDraw(sprite, fade, &DitherFade, &Tint);

    SetShaderValue(RayShader, self->_sprites._locUvRect, sprite->UvRect, SHADER_UNIFORM_VEC4);
    SetShaderValueMatrix(RayShader, self->_sprites._locShadowModel, matrices->CascadeTransform);
    SetShaderValueMatrix(RayShader, self->_sprites._locPointShadowModel, matrices->PointTransform);
    SetShaderValue(RayShader, self->_sprites._locDecalOffset, &DecalOffset, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sprites._locAlphaMode, &AlphaMode, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, self->_sprites._locAlphaCutoff, &sprite->AlphaCutoff, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sprites._locIsLit, &IsLitValue, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, self->_sprites._locEmission, &Emission, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sprites._locLightWrap, &LightWrap, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_sprites._locIsCard, &IsCardValue, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, self->_sprites._locUseScreenAo, &UseScreenAo, SHADER_UNIFORM_INT);
    WorldRendererCommon_SetDither(RayShader, &self->_sprites._dither, DitherFade, 0.0f, false);
    // Lit or unlit, a sprite is seen through the haze at its own multiplier, and through the zones' air.
    WorldRendererLighting_SetDrawPlace(self, RayShader, &self->_sprites._lit, sprite->Shape.Position,
        sprite->BoundsRadius, sprite->Haze);
    if (IsLit)
    {
        WorldRendererLighting_UploadObjectLights(self, RayShader, &self->_sprites._lit, sprite->Shape.Position,
            sprite->BoundsRadius);
    }
    WorldRendererSprites_DrawQuad(self, RayShader, sprite, Tint, matrices->CameraTransform);
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererSprites_Construct(WorldRenderer* self)
{
    GenericBuffer_AllocateVariable(&self->_sprites._frames, INITIAL_SPRITE_CAPACITY, sizeof(WorldRendererSpriteFrame));
    GenericBuffer_AllocateVariable(&self->_sprites._transparentOrder, INITIAL_SPRITE_CAPACITY, sizeof(SpriteDrawOrder));
}

void WorldRendererSprites_Deconstruct(WorldRenderer* self)
{
    if (self->_sprites._hasQuad)
    {
        UnloadMesh(self->_sprites._quad);
        self->_sprites._hasQuad = false;
    }
    Memory_Free(self->_sprites._frames._data);
    Memory_Free(self->_sprites._transparentOrder._data);
    self->_sprites._frames._data = NULL;
    self->_sprites._transparentOrder._data = NULL;
}

void WorldRendererSprites_LoadShaders(WorldRenderer* self)
{
    LoadLitShader(self);
    if ((self->_sprites._shader == NULL) || self->_sprites._hasQuad)
    {
        return;
    }
    self->_sprites._hasQuad = BuildQuad(&self->_sprites._quad);
    if (!self->_sprites._hasQuad)
    {
        WorldRendererCommon_Warn(self,
            (const unsigned char*)u8"WorldRenderer: the GPU did not take the sprite quad; sprites are not drawn.");
    }
}

bool WorldRendererSprites_IsAvailable(const WorldRenderer* self)
{
    return self->_sprites._hasQuad && (self->_sprites._shader != NULL);
}

void WorldRendererSprites_PrepareFrame(WorldRenderer* self, const WorldRenderFrame* frame)
{
    GenericBuffer_Clear(&self->_sprites._frames);
    GenericBuffer_Clear(&self->_sprites._transparentOrder);
    size_t SpriteCount = 0;
    const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(&self->_cache, &SpriteCount);
    if (!WorldRendererSprites_IsAvailable(self))
    {
        return;
    }

    // In render space: the camera at the origin, each sprite's shape and each light relative to it. The camera's own up
    // and forward are the view matrix's rows: a billboard leans its top toward the camera's up.
    Vector3 CameraPosition = { 0.0f, 0.0f, 0.0f };
    Vector3 CameraUp = { frame->View.m1, frame->View.m5, frame->View.m9 };
    Vector3 CameraForward = { -frame->View.m2, -frame->View.m6, -frame->View.m10 };
    SpriteViewer CameraViewer = SpriteViewer_FromPoint(CameraPosition, CameraUp);
    SpriteViewer CascadeViewer = SpriteViewer_FromDirection(frame->ShadowDirection, WorldUp);
    const WorldRenderLight* PointLight = GetPointShadowLight(self, frame);
    SpriteViewer PointViewer = SpriteViewer_FromPoint((PointLight != NULL) ? PointLight->RenderPosition
        : CameraPosition, WorldUp);

    for (size_t Index = 0; Index < SpriteCount; Index++)
    {
        const WorldRenderSprite* Sprite = &Sprites[Index];
        WorldRendererSpriteFrame Entry;
        if (Sprite->Shape.Orientation == WorldSpriteOrientation_Fixed)
        {
            Entry.CameraTransform = Sprite->FixedTransform;
            Entry.CascadeTransform = Sprite->FixedTransform;
            Entry.PointTransform = Sprite->FixedTransform;
        }
        else
        {
            Entry.CameraTransform = SpriteTransform_GetMatrix(&Sprite->Shape, &CameraViewer);
            Entry.CascadeTransform = frame->IsCascadeShadowActive
                ? SpriteTransform_GetMatrix(&Sprite->Shape, &CascadeViewer) : Entry.CameraTransform;
            Entry.PointTransform = (PointLight != NULL)
                ? SpriteTransform_GetMatrix(&Sprite->Shape, &PointViewer) : Entry.CameraTransform;
        }
        Vector3 Center = SpriteTransform_GetCenter(Entry.CameraTransform);
        Entry.Depth = Vector3DotProduct(Vector3Subtract(Center, CameraPosition), CameraForward);
        if (!GenericBuffer_AddLast(&self->_sprites._frames, &Entry))
        {
            // Cannot happen short of address-space exhaustion; drawing no sprite this frame beats drawing them wrong.
            GenericBuffer_Clear(&self->_sprites._frames);
            return;
        }
    }

    // The transparent draw order holds only what the camera's passes draw (the visibility's camera list, ascending, so
    // equal depths keep the world's order).
    size_t VisibleCount = 0;
    const size_t* Visible = WorldVisibility_GetSprites(&self->_visibility, &self->_cache, WorldVisibilityPass_Camera,
        &VisibleCount);
    const void* FrameData = self->_sprites._frames._data;
    const WorldRendererSpriteFrame* Frames = FrameData;
    for (size_t Position = 0; Position < VisibleCount; Position++)
    {
        size_t Index = Visible[Position];
        SpriteDrawOrder Order = { .Index = Index, .Depth = Frames[Index].Depth };
        if (IsSortedTransparent(&Sprites[Index]) && !GenericBuffer_AddLast(&self->_sprites._transparentOrder, &Order))
        {
            GenericBuffer_Clear(&self->_sprites._frames);
            GenericBuffer_Clear(&self->_sprites._transparentOrder);
            return;
        }
    }
    SpriteTransform_SortBackToFront(&self->_sprites._transparentOrder);
}

const WorldRendererSpriteFrame* WorldRendererSprites_GetFrame(const WorldRenderer* self, size_t index)
{
    if (index >= self->_sprites._frames._count)
    {
        return NULL;
    }
    const void* FrameData = self->_sprites._frames._data;
    const WorldRendererSpriteFrame* Frames = FrameData;
    return &Frames[index];
}

void WorldRendererSprites_DrawQuad(WorldRenderer* self, Shader shader, const WorldRenderSprite* sprite, Color tint,
    Matrix transform)
{
    // A material built on the stack: the frame's texture on the albedo map (texture0), the tint as colDiffuse. Nothing
    // is written to the animation's asset.
    MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
    Memory_Zero(Maps, sizeof(Maps));
    Maps[MATERIAL_MAP_ALBEDO].texture = sprite->Frame._texture;
    Maps[MATERIAL_MAP_ALBEDO].color = tint;
    Material QuadMaterial = { .shader = shader, .maps = Maps };
    DrawMesh(self->_sprites._quad, QuadMaterial, transform);
}

void WorldRendererSprites_DrawLit(WorldRenderer* self, WorldRendererPixelFilter filter, bool applyScreenAo)
{
    size_t SpriteCount = 0;
    const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(&self->_cache, &SpriteCount);
    if (!WorldRendererSprites_IsAvailable(self) || (self->_sprites._frames._count != SpriteCount))
    {
        return;
    }
    size_t VisibleCount = 0;
    const size_t* Visible = WorldVisibility_GetSprites(&self->_visibility, &self->_cache, WorldVisibilityPass_Camera,
        &VisibleCount);

    // Draw what the batch holds (the debug grid) under the state it was queued with; sprites are two-sided.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();

    // The cutout sprites: opaque where drawn, writing depth, with the models.
    for (size_t Position = 0; Position < VisibleCount; Position++)
    {
        size_t Index = Visible[Position];
        const WorldRenderSprite* Sprite = &Sprites[Index];
        if (WorldRenderSprite_IsOpaqueSurface(Sprite)
            && WorldRendererGeometry_PassesFilter(Sprite->OmitsPixelation, filter))
        {
            DrawLitSprite(self, Sprite, WorldRendererSprites_GetFrame(self, Index),
                WorldVisibility_GetSpriteFade(&self->_visibility, Index), applyScreenAo);
        }
    }

    // The decals, over the opaque world (depth tested, not written; lifted off their surface), in world order.
    rlDisableDepthMask();
    for (size_t Position = 0; Position < VisibleCount; Position++)
    {
        size_t Index = Visible[Position];
        const WorldRenderSprite* Sprite = &Sprites[Index];
        if (Sprite->IsDecal && WorldRendererGeometry_PassesFilter(Sprite->OmitsPixelation, filter))
        {
            BeginBlendMode(GetBlendMode(Sprite->AlphaMode));
            DrawLitSprite(self, Sprite, WorldRendererSprites_GetFrame(self, Index),
                WorldVisibility_GetSpriteFade(&self->_visibility, Index), applyScreenAo);
        }
    }

    // The blended and additive sprites, farthest first, so each blends over everything behind it, the others included.
    // TODO: decals and blended or additive sprites write no depth (as designed: sorting, not depth, orders them), so the
    // passes after the scene that read the scene depth see what lies behind them: the bloom prefilter's sky mask (a glow
    // seen against the sky does not bloom, the same glow over geometry does), the sun shafts (smoke in front of the sun
    // does not block them), the crisp composite (it draws crisp objects over smoke in front of them) and the outlines
    // (a model's outline shows through smoke). A coverage mask these draws write (stencil, or a flag the post passes
    // read) would let those passes see them.
    const void* OrderData = self->_sprites._transparentOrder._data;
    const SpriteDrawOrder* Order = OrderData;
    for (size_t Position = 0; Position < self->_sprites._transparentOrder._count; Position++)
    {
        size_t Index = Order[Position].Index;
        const WorldRenderSprite* Sprite = &Sprites[Index];
        if (WorldRendererGeometry_PassesFilter(Sprite->OmitsPixelation, filter))
        {
            BeginBlendMode(GetBlendMode(Sprite->AlphaMode));
            DrawLitSprite(self, Sprite, WorldRendererSprites_GetFrame(self, Index),
                WorldVisibility_GetSpriteFade(&self->_visibility, Index), applyScreenAo);
        }
    }

    EndBlendMode();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}
