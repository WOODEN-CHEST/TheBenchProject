// What the world renderer draws this frame: frustum planes and sphere tests, draw distances, fades, the dither's C
// mirror, and the per-pass lists built from the render cache (see WorldVisibility.h).

#include <limits.h>
#include <math.h>
#include "WorldVisibility.h"
#include "raylib/raymath.h"


// Macros.
/** Initial capacity (in entries) of each list; they grow with the world. */
#define INITIAL_LIST_CAPACITY 32
/** Degrees to radians, in double precision (raymath's PI is a float). */
#define DEGREES_TO_RADIANS (3.14159265358979323846 / 180.0)


// Fields.
/* The 4x4 Bayer matrix, row by row from the target's bottom row (gl_FragCoord's origin): shaders/common/dither.glsl's
 * DITHER_BAYER, entry for entry (tests/ShaderSourceTests.c compares the two). */
static const uint8_t DitherBayer[WORLD_VISIBILITY_DITHER_SIZE][WORLD_VISIBILITY_DITHER_SIZE] =
{
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};


// Static functions.
/* A plane a*x + b*y + c*z + d = 0, normalized so that (a, b, c) is unit. A degenerate one (a zero normal, from a
 * degenerate matrix) is left with a zero normal and offset: every point lies on it, so it culls nothing. */
static WorldPlane MakePlane(float a, float b, float c, float d)
{
    float Length = sqrtf(a*a + b*b + c*c);
    if (!(Length > 0.0f))
    {
        return (WorldPlane){ .Normal = { 0.0f, 0.0f, 0.0f }, .Distance = 0.0f };
    }
    return (WorldPlane){ .Normal = { a / Length, b / Length, c / Length }, .Distance = d / Length };
}

/* The draw distance scale to use: anything not finite or below 0 counts as 1. */
static float SanitizeScale(float scale)
{
    return (isfinite(scale) && (scale >= 0.0f)) ? scale : 1.0f;
}

/* The view's zoom factor to use: anything not finite or not above 0 counts as 1. */
static float GetZoomFactor(const WorldVisibilityView* view)
{
    return (isfinite(view->ZoomFactor) && (view->ZoomFactor > 0.0f)) ? view->ZoomFactor : 1.0f;
}

/* Whether a model casts into the given shadow cascade, per its WorldShadowTier: Both casts into either; Near casts only
 * into the near cascade; Far casts only into the far cascade. */
static bool ModelCastsIntoCascade(WorldShadowTier shadowTier, bool isNearCascade)
{
    switch (shadowTier)
    {
        case WorldShadowTier_Near: return isNearCascade;
        case WorldShadowTier_Far:  return !isNearCascade;
        case WorldShadowTier_Both: return true;
    }
    // Only reachable with a corrupted tier value; casting into both cascades is the safe choice.
    return true;
}

/* Empties every list and the counts. */
static void Clear(WorldVisibility* self)
{
    GenericBuffer_Clear(&self->_modelFades);
    GenericBuffer_Clear(&self->_spriteFades);
    for (int Pass = 0; Pass < (int)WorldVisibilityPass_Count; Pass++)
    {
        GenericBuffer_Clear(&self->_models[Pass]);
        GenericBuffer_Clear(&self->_sprites[Pass]);
    }
    self->_counts = (WorldVisibilityCounts){ 0 };
    self->_hasCrispObjects = false;
}

/* Appends a cache index to a list. */
static bool AddIndex(GenericBuffer* list, size_t index)
{
    size_t Entry = index;
    return GenericBuffer_AddLast(list, &Entry);
}

/* An entry's fades: its draw distance fades it by the camera's distance to its bounding sphere (times @p zoomFactor),
 * its tint's alpha (the object's opacity) by 1 - opacity, and the draw takes the larger. */
static WorldVisibilityFade GetFade(const WorldVisibilityView* view, Vector3 center, float radius, float drawDistance,
    float zoomFactor, Color tint)
{
    float Distance = WorldVisibility_GetEntryDistance(view->CameraPosition, center, radius) * zoomFactor;
    float DistanceFade = WorldVisibility_GetDistanceFade(Distance, drawDistance);
    float OpacityFade = WorldVisibility_GetOpacityFade((float)tint.a / (float)UCHAR_MAX);
    return (WorldVisibilityFade){ .Distance = DistanceFade, .Combined = WorldVisibility_CombineFades(DistanceFade,
        OpacityFade) };
}

/* Whether a sphere is inside a view volume, or culling is off. */
static bool IsInVolume(const WorldVisibilityView* view, const WorldFrustum* volume, Vector3 center, float radius)
{
    return !view->IsCullingEnabled || WorldVisibility_IsSphereInFrustum(volume, center, radius);
}

/* Adds a shadow caster that is drawn this frame to the shadow passes that run and that it reaches: a cascade when it
 * casts into that cascade and lies in its box, the cube shadow when it touches the light's reach sphere (anything further
 * cannot shadow what the light lights: the light falls to 0 at its reach). */
static bool AddCaster(GenericBuffer lists[WorldVisibilityPass_Count], const WorldVisibilityView* view, Vector3 center,
    float radius, bool castsIntoNear, bool castsIntoFar, size_t index)
{
    if (view->IsNearCascadeActive && castsIntoNear && IsInVolume(view, &view->NearCascade, center, radius)
        && !AddIndex(&lists[WorldVisibilityPass_NearCascade], index))
    {
        return false;
    }
    if (view->IsFarCascadeActive && castsIntoFar && IsInVolume(view, &view->FarCascade, center, radius)
        && !AddIndex(&lists[WorldVisibilityPass_FarCascade], index))
    {
        return false;
    }
    if (view->IsPointShadowActive
        && (Vector3Distance(center, view->PointShadowPosition) < (radius + view->PointShadowReach))
        && !AddIndex(&lists[WorldVisibilityPass_PointShadow], index))
    {
        return false;
    }
    return true;
}

/* Adds an entry that is not faded out to the camera's list when it is in view, and counts it. @p outIsInView (may be NULL)
 * says whether it joined the list. */
static bool AddToCamera(WorldVisibility* self, GenericBuffer* cameraList, const WorldVisibilityView* view,
    Vector3 center, float radius, float fade, bool omitsPixelation, size_t index, size_t* drawnCount, bool* outIsInView)
{
    if (!IsInVolume(view, &view->CameraFrustum, center, radius))
    {
        self->_counts.OutsideViewCount++;
        return true;
    }
    if (!AddIndex(cameraList, index))
    {
        return false;
    }
    if (outIsInView != NULL)
    {
        *outIsInView = true;
    }
    (*drawnCount)++;
    if (fade > 0.0f)
    {
        self->_counts.FadingCount++;
    }
    if (omitsPixelation)
    {
        self->_hasCrispObjects = true;
    }
    return true;
}

/* One model entry: its fades (none for an entry the passes do not draw at all), then the camera's list and the shadow
 * passes' lists it joins. */
static bool AddModel(WorldVisibility* self, const WorldVisibilityView* view, const WorldRenderModel* model, size_t index)
{
    WorldVisibilityFade Fade = { .Distance = 1.0f, .Combined = 1.0f };
    if (model->IsDrawn)
    {
        // Its own draw distance (the world model's or the placement's, times its multiplier), scaled by the quality
        // setting unless it is fixed, against the zoomed distance (G17).
        float DrawDistance = WorldVisibility_ScaleDrawDistance(model->DrawDistance, model->IsDrawDistanceFixed,
            view->DrawDistanceScale);
        Fade = GetFade(view, model->RenderCenter, model->Radius, DrawDistance, GetZoomFactor(view), model->Tint);
    }
    if (!GenericBuffer_AddLast(&self->_modelFades, &Fade))
    {
        return false;
    }
    if (!model->IsDrawn)
    {
        return true;
    }
    if (!(Fade.Combined < 1.0f))
    {
        self->_counts.FadedOutCount++;
        return true;
    }

    bool IsInView = false;
    if (!AddToCamera(self, &self->_models[WorldVisibilityPass_Camera], view, model->RenderCenter, model->Radius,
        Fade.Combined, model->OmitsPixelation, index, &self->_counts.DrawnModelCount, &IsInView))
    {
        return false;
    }
    if (IsInView)
    {
        // By the LOD it shows or heads to (the cache's LOD update ran before this build).
        size_t Lod = WorldLodState_GetDestinationLod(&model->Lod);
        self->_counts.DrawnModelsByLod[(Lod < WORLD_MODEL_MAX_LODS) ? Lod : (WORLD_MODEL_MAX_LODS - 1U)]++;
        self->_counts.LodFadingCount += WorldLodState_IsFading(&model->Lod) ? 1U : 0U;
    }
    return !model->CastsShadows || AddCaster(self->_models, view, model->RenderCenter, model->Radius,
        ModelCastsIntoCascade(model->ShadowTier, true), ModelCastsIntoCascade(model->ShadowTier, false), index);
}

/* One sprite entry (the cache lists only sprites that have a frame to draw on shown objects). Its sphere is the one
 * around its pivot that holds its quad in every orientation, so it serves the camera and the lights alike. */
static bool AddSprite(WorldVisibility* self, const WorldVisibilityView* view, const WorldRenderSprite* sprite,
    size_t index)
{
    // Its own draw distance (the world sprite's, its kind's or the placement's, times its multiplier), with the models'
    // rules: scaled by the quality setting unless it is fixed, against the zoomed distance (G17).
    float DrawDistance = WorldVisibility_ScaleDrawDistance(sprite->DrawDistance, sprite->IsDrawDistanceFixed,
        view->DrawDistanceScale);
    WorldVisibilityFade Fade = GetFade(view, sprite->Shape.Position, sprite->BoundsRadius, DrawDistance,
        GetZoomFactor(view), sprite->Tint);
    if (!GenericBuffer_AddLast(&self->_spriteFades, &Fade))
    {
        return false;
    }
    if (!(Fade.Combined < 1.0f))
    {
        self->_counts.FadedOutCount++;
        return true;
    }

    if (!AddToCamera(self, &self->_sprites[WorldVisibilityPass_Camera], view, sprite->Shape.Position,
        sprite->BoundsRadius, Fade.Combined, sprite->OmitsPixelation, index, &self->_counts.DrawnSpriteCount, NULL))
    {
        return false;
    }
    // Sprites cast into both cascades.
    return !WorldRenderSprite_CastsShadows(sprite)
        || AddCaster(self->_sprites, view, sprite->Shape.Position, sprite->BoundsRadius, true, true, index);
}

/* Whether a pass value names a list. */
static bool IsPass(WorldVisibilityPass pass)
{
    return (unsigned int)pass < (unsigned int)WorldVisibilityPass_Count;
}


// Internal functions (declared in WorldVisibility.h).
WorldFrustum WorldVisibility_ExtractFrustum(Matrix viewProjection)
{
    // raymath stores a matrix by columns (m0..m3 the first), so the clip coordinates' rows are (m0, m4, m8, m12),
    // (m1, m5, m9, m13), (m2, m6, m10, m14) and (m3, m7, m11, m15). A point is inside where -w <= x, y, z <= w, i.e.
    // where w + x, w - x, w + y, w - y, w + z and w - z are all >= 0: each of those is a plane.
    const Matrix* M = &viewProjection;
    WorldFrustum Frustum;
    Frustum.Planes[WorldFrustumPlane_Left] = MakePlane(M->m3 + M->m0, M->m7 + M->m4, M->m11 + M->m8, M->m15 + M->m12);
    Frustum.Planes[WorldFrustumPlane_Right] = MakePlane(M->m3 - M->m0, M->m7 - M->m4, M->m11 - M->m8, M->m15 - M->m12);
    Frustum.Planes[WorldFrustumPlane_Bottom] = MakePlane(M->m3 + M->m1, M->m7 + M->m5, M->m11 + M->m9,
        M->m15 + M->m13);
    Frustum.Planes[WorldFrustumPlane_Top] = MakePlane(M->m3 - M->m1, M->m7 - M->m5, M->m11 - M->m9, M->m15 - M->m13);
    Frustum.Planes[WorldFrustumPlane_Near] = MakePlane(M->m3 + M->m2, M->m7 + M->m6, M->m11 + M->m10,
        M->m15 + M->m14);
    Frustum.Planes[WorldFrustumPlane_Far] = MakePlane(M->m3 - M->m2, M->m7 - M->m6, M->m11 - M->m10, M->m15 - M->m14);
    return Frustum;
}

bool WorldVisibility_IsSphereInFrustum(const WorldFrustum* frustum, Vector3 center, float radius)
{
    for (int Index = 0; Index < (int)WorldFrustumPlane_Count; Index++)
    {
        const WorldPlane* Plane = &frustum->Planes[Index];
        float SignedDistance = Plane->Normal.x*center.x + Plane->Normal.y*center.y + Plane->Normal.z*center.z
            + Plane->Distance;
        if (!(SignedDistance >= -radius)) // wholly behind this plane (a NaN counts as outside)
        {
            return false;
        }
    }
    return true;
}

float WorldVisibility_ScaleDrawDistance(float drawDistance, bool isFixed, float scale)
{
    return isFixed ? drawDistance : (drawDistance * SanitizeScale(scale));
}

float WorldVisibility_GetZoomFactor(float fovYDegrees)
{
    if (!(fovYDegrees > 0.0f) || !(fovYDegrees < 180.0f))
    {
        return 1.0f; // also a NaN
    }
    // In double, from the same expression for both angles, so the reference field of view gives exactly 1.
    double HalfAngle = (double)fovYDegrees * 0.5 * DEGREES_TO_RADIANS;
    double ReferenceHalfAngle = WORLD_VISIBILITY_REFERENCE_FOV_Y * 0.5 * DEGREES_TO_RADIANS;
    return (float)(tan(HalfAngle) / tan(ReferenceHalfAngle));
}

float WorldVisibility_GetFadeBand(float drawDistance)
{
    if (!(drawDistance > 0.0f))
    {
        return 0.0f;
    }
    float Band = fmaxf(drawDistance * WORLD_VISIBILITY_FADE_BAND_SHARE, WORLD_VISIBILITY_MIN_FADE_BAND);
    return fminf(Band, drawDistance);
}

float WorldVisibility_GetDistanceFade(float distance, float drawDistance)
{
    if (!(drawDistance > 0.0f) || !(distance < drawDistance))
    {
        return 1.0f; // nothing is drawn within it, the entry is beyond it, or a NaN
    }
    if (isinf(drawDistance))
    {
        return 0.0f;
    }
    float Band = WorldVisibility_GetFadeBand(drawDistance);
    float Start = drawDistance - Band;
    if (distance <= Start)
    {
        return 0.0f;
    }
    return fminf((distance - Start) / Band, 1.0f);
}

float WorldVisibility_GetEntryDistance(Vector3 cameraPosition, Vector3 center, float radius)
{
    float Distance = Vector3Distance(cameraPosition, center) - radius;
    return (Distance < 0.0f) ? 0.0f : Distance; // a NaN stays a NaN
}

float WorldVisibility_GetOpacityFade(float opacity)
{
    float Opacity = fminf(fmaxf(opacity, 0.0f), 1.0f); // fmaxf drops a NaN: transparent
    return 1.0f - Opacity;
}

float WorldVisibility_CombineFades(float distanceFade, float opacityFade)
{
    return fmaxf(distanceFade, opacityFade);
}

uint32_t WorldVisibility_GetDitherCell(float fragCoord, float pixelScale)
{
    float Cell = floorf(fragCoord / fmaxf(pixelScale, 1.0f));
    return (Cell > 0.0f) ? (uint32_t)Cell : 0U;
}

float WorldVisibility_GetDitherThreshold(uint32_t x, uint32_t y)
{
    float Value = (float)DitherBayer[y % WORLD_VISIBILITY_DITHER_SIZE][x % WORLD_VISIBILITY_DITHER_SIZE];
    return (Value + 0.5f) / (float)WORLD_VISIBILITY_DITHER_LEVELS;
}

bool WorldVisibility_IsDitherDiscarded(float fade, float lodProgress, bool isIncomingLod, uint32_t x, uint32_t y)
{
    float Threshold = WorldVisibility_GetDitherThreshold(x, y);
    bool IsKeptByFade = Threshold > fade;
    bool IsKeptByLod = isIncomingLod ? (Threshold <= lodProgress) : (Threshold > lodProgress);
    return !(IsKeptByFade && IsKeptByLod);
}

void WorldVisibility_Construct(WorldVisibility* self)
{
    GenericBuffer_AllocateVariable(&self->_modelFades, INITIAL_LIST_CAPACITY, sizeof(WorldVisibilityFade));
    GenericBuffer_AllocateVariable(&self->_spriteFades, INITIAL_LIST_CAPACITY, sizeof(WorldVisibilityFade));
    for (int Pass = 0; Pass < (int)WorldVisibilityPass_Count; Pass++)
    {
        GenericBuffer_AllocateVariable(&self->_models[Pass], INITIAL_LIST_CAPACITY, sizeof(size_t));
        GenericBuffer_AllocateVariable(&self->_sprites[Pass], INITIAL_LIST_CAPACITY, sizeof(size_t));
    }
    self->_counts = (WorldVisibilityCounts){ 0 };
    self->_hasCrispObjects = false;
}

void WorldVisibility_Deconstruct(WorldVisibility* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->_modelFades._data);
    Memory_Free(self->_spriteFades._data);
    for (int Pass = 0; Pass < (int)WorldVisibilityPass_Count; Pass++)
    {
        Memory_Free(self->_models[Pass]._data);
        Memory_Free(self->_sprites[Pass]._data);
    }
}

Error WorldVisibility_Build(WorldVisibility* self, const WorldRenderCache* cache, const WorldVisibilityView* view)
{
    if ((self == NULL) || (cache == NULL) || (view == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldVisibility_Build: self, cache and view must not be NULL.");
    }
    Clear(self);

    size_t ModelCount = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(cache, &ModelCount);
    for (size_t Index = 0; Index < ModelCount; Index++)
    {
        if (!AddModel(self, view, &Models[Index], Index))
        {
            Clear(self);
            return Error_Construct2(ErrorCode_InvalidOperation, "WorldVisibility: could not grow a model list.");
        }
    }

    size_t SpriteCount = 0;
    const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(cache, &SpriteCount);
    for (size_t Index = 0; Index < SpriteCount; Index++)
    {
        if (!AddSprite(self, view, &Sprites[Index], Index))
        {
            Clear(self);
            return Error_Construct2(ErrorCode_InvalidOperation, "WorldVisibility: could not grow a sprite list.");
        }
    }
    return Error_CreateSuccess();
}

const size_t* WorldVisibility_GetModels(const WorldVisibility* self, const WorldRenderCache* cache,
    WorldVisibilityPass pass, size_t* outCount)
{
    size_t ModelCount = 0;
    WorldRenderCache_GetModels(cache, &ModelCount);
    *outCount = 0;
    if (!IsPass(pass) || (ModelCount != self->_modelFades._count) || (self->_models[pass]._count == 0))
    {
        return NULL;
    }
    *outCount = self->_models[pass]._count;
    const void* Entries = self->_models[pass]._data;
    return Entries;
}

const size_t* WorldVisibility_GetSprites(const WorldVisibility* self, const WorldRenderCache* cache,
    WorldVisibilityPass pass, size_t* outCount)
{
    size_t SpriteCount = 0;
    WorldRenderCache_GetSprites(cache, &SpriteCount);
    *outCount = 0;
    if (!IsPass(pass) || (SpriteCount != self->_spriteFades._count) || (self->_sprites[pass]._count == 0))
    {
        return NULL;
    }
    *outCount = self->_sprites[pass]._count;
    const void* Entries = self->_sprites[pass]._data;
    return Entries;
}

WorldVisibilityFade WorldVisibility_GetModelFade(const WorldVisibility* self, size_t index)
{
    if (index >= self->_modelFades._count)
    {
        return (WorldVisibilityFade){ .Distance = 1.0f, .Combined = 1.0f };
    }
    const void* FadeData = self->_modelFades._data;
    const WorldVisibilityFade* Fades = FadeData;
    return Fades[index];
}

WorldVisibilityFade WorldVisibility_GetSpriteFade(const WorldVisibility* self, size_t index)
{
    if (index >= self->_spriteFades._count)
    {
        return (WorldVisibilityFade){ .Distance = 1.0f, .Combined = 1.0f };
    }
    const void* FadeData = self->_spriteFades._data;
    const WorldVisibilityFade* Fades = FadeData;
    return Fades[index];
}
