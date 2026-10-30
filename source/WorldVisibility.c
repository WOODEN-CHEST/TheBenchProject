// What the world renderer draws this frame: frustum planes and sphere tests, draw distances, fades, the dither's C
// mirror, and the per-pass lists built from the render cache (see WorldVisibility.h).

#include <limits.h>
#include <float.h>
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

/* Empties every list and the counts. */
static void Clear(WorldVisibility* self)
{
    GenericBuffer_Clear(&self->_modelFades);
    GenericBuffer_Clear(&self->_spriteFades);
    GenericBuffer_Clear(&self->_terrainChunkFades);
    GenericBuffer_Clear(&self->_instanceCellFades);
    for (int Pass = 0; Pass < (int)WorldVisibilityPass_Count; Pass++)
    {
        GenericBuffer_Clear(&self->_models[Pass]);
        GenericBuffer_Clear(&self->_sprites[Pass]);
        GenericBuffer_Clear(&self->_terrainChunks[Pass]);
        GenericBuffer_Clear(&self->_instanceCells[Pass]);
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
 * its tint's alpha (the object's opacity) by 1 - opacity, and the draw takes the larger; the shadow passes take the
 * larger of that and its shadow distance's fade (the same distance against @p shadowDistance). */
static WorldVisibilityFade GetFade(const WorldVisibilityView* view, Vector3 center, float radius, float drawDistance,
    float shadowDistance, float zoomFactor, Color tint)
{
    float Distance = WorldVisibility_GetEntryDistance(view->CameraPosition, center, radius) * zoomFactor;
    float DistanceFade = WorldVisibility_GetDistanceFade(Distance, drawDistance);
    float OpacityFade = WorldVisibility_GetOpacityFade((float)tint.a / (float)UCHAR_MAX);
    float Combined = WorldVisibility_CombineFades(DistanceFade, OpacityFade);
    float ShadowFade = WorldVisibility_CombineFades(Combined, WorldVisibility_GetDistanceFade(Distance, shadowDistance));
    return (WorldVisibilityFade){ .Distance = DistanceFade, .Combined = Combined, .Shadow = ShadowFade };
}

/* The shadow distance scale to use: anything not finite or below 0 counts as 1. */
static float GetShadowDistanceScale(const WorldVisibilityView* view)
{
    return SanitizeScale(view->ShadowDistanceScale);
}

/* Whether a sphere is inside a view volume, or culling is off. */
static bool IsInVolume(const WorldVisibilityView* view, const WorldFrustum* volume, Vector3 center, float radius)
{
    return !view->IsCullingEnabled || WorldVisibility_IsSphereInFrustum(volume, center, radius);
}

/* Whether a sphere lies wholly inside a shadow level's four sides (never with culling off): the level after it then
 * leaves it out. A NaN counts as outside. */
static bool IsInsideSides(const WorldVisibilityView* view, const WorldFrustum* box, Vector3 center, float radius)
{
    static const WorldFrustumPlane Sides[] =
    {
        WorldFrustumPlane_Left, WorldFrustumPlane_Right, WorldFrustumPlane_Bottom, WorldFrustumPlane_Top
    };
    if (!view->IsCullingEnabled)
    {
        return false;
    }
    for (size_t Index = 0U; Index < (sizeof(Sides) / sizeof(Sides[0])); Index++)
    {
        const WorldPlane* Plane = &box->Planes[Sides[Index]];
        float SignedDistance = Plane->Normal.x*center.x + Plane->Normal.y*center.y + Plane->Normal.z*center.z
            + Plane->Distance;
        if (!(SignedDistance >= radius))
        {
            return false;
        }
    }
    return true;
}

/* Adds a shadow caster that is drawn this frame, and casts (its shadow fade below 1), to the shadow passes that run and
 * that it reaches: each shadow level that renders, when the caster's sphere is large enough for it, reaches within the
 * shadow reach, lies in its box and, past the first level, is not deep inside the level before's box (its ring); with
 * culling off, the box and the ring take everything (the size cut and the reach stay). The cube shadow takes it when it
 * touches the light's reach sphere (anything further cannot shadow what the light lights: the light falls to 0 at its
 * reach). */
static bool AddCaster(GenericBuffer lists[WorldVisibilityPass_Count], const WorldVisibilityView* view, Vector3 center,
    float radius, float shadowFade, size_t index)
{
    if (!(shadowFade < 1.0f))
    {
        return true;
    }
    bool IsWithinReach = WorldVisibility_GetEntryDistance(view->CameraPosition, center, radius) <= view->ShadowReach;
    uint32_t LevelCount = (view->ShadowLevelCount < WORLD_VISIBILITY_MAX_SHADOW_LEVELS) ? view->ShadowLevelCount
        : WORLD_VISIBILITY_MAX_SHADOW_LEVELS;
    for (uint32_t Level = 0U; IsWithinReach && (Level < LevelCount); Level++)
    {
        const WorldVisibilityShadowLevel* ShadowLevel = &view->ShadowLevels[Level];
        if (ShadowLevel->IsRendered && (radius >= ShadowLevel->MinRadius)
            && IsInVolume(view, &ShadowLevel->Box, center, radius)
            && !((Level > 0U) && IsInsideSides(view, &view->ShadowLevels[Level - 1U].Box, center,
                radius + ShadowLevel->InnerInset))
            && !AddIndex(&lists[WorldVisibility_GetShadowLevelPass(Level)], index))
        {
            return false;
        }
    }
    if (view->IsPointShadowActive
        && (Vector3Distance(center, view->PointShadowPosition) < (radius + view->PointShadowReach))
        && !AddIndex(&lists[WorldVisibilityPass_PointShadow], index))
    {
        return false;
    }
    return true;
}

/* Adds an entry that keeps the sky out (drawn this frame, not faded out) to the sky's occupancy volume's list when the
 * volume is drawn and the entry lies in its map's box. */
static bool AddSkyOccluder(GenericBuffer* list, const WorldVisibilityView* view, Vector3 center, float radius,
    size_t index)
{
    return !view->IsSkyVisibilityActive || !IsInVolume(view, &view->SkyVisibilityBox, center, radius)
        || AddIndex(list, index);
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

/* One model entry: its fades (none for an entry the passes do not draw at all), then the camera's list (unless it is
 * only a shadow) and the shadow passes' lists it joins. */
static bool AddModel(WorldVisibility* self, const WorldVisibilityView* view, const WorldRenderModel* model, size_t index)
{
    WorldVisibilityFade Fade = { .Distance = 1.0f, .Combined = 1.0f, .Shadow = 1.0f };
    if (model->IsDrawn)
    {
        // Its own draw distance (the world model's or the placement's, times its multiplier), scaled by the quality
        // setting unless it is fixed, against the zoomed distance; its shadow distance the same way.
        float DrawDistance = WorldVisibility_ScaleDrawDistance(model->DrawDistance, model->IsDrawDistanceFixed,
            view->DrawDistanceScale);
        float ShadowDistance = WorldVisibility_ScaleShadowDistance(DrawDistance, model->HasShadowDistance,
            model->ShadowDistance, model->IsDrawDistanceFixed, model->IsShadowDistanceFixed, view->DrawDistanceScale,
            GetShadowDistanceScale(view));
        Fade = GetFade(view, model->RenderCenter, model->Radius, DrawDistance, ShadowDistance, GetZoomFactor(view),
            model->Tint);
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
    if (!model->IsShadowOnly && !AddToCamera(self, &self->_models[WorldVisibilityPass_Camera], view, model->RenderCenter,
        model->Radius, Fade.Combined, model->OmitsPixelation, index, &self->_counts.DrawnModelCount, &IsInView))
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
    if (model->OccludesSky && !AddSkyOccluder(&self->_models[WorldVisibilityPass_SkyVisibility], view,
        model->RenderCenter, model->Radius, index))
    {
        return false;
    }
    return !model->CastsShadows || AddCaster(self->_models, view, model->RenderCenter, model->Radius, Fade.Shadow, index);
}

/* One sprite entry (the cache lists only sprites that have a frame to draw on shown objects). Its sphere is the one
 * around its pivot that holds its quad in every orientation, so it serves the camera and the lights alike. */
static bool AddSprite(WorldVisibility* self, const WorldVisibilityView* view, const WorldRenderSprite* sprite,
    size_t index)
{
    // Its own draw distance (the world sprite's, its kind's or the placement's, times its multiplier), with the models'
    // rules: scaled by the quality setting unless it is fixed, against the zoomed distance; its shadow distance too.
    float DrawDistance = WorldVisibility_ScaleDrawDistance(sprite->DrawDistance, sprite->IsDrawDistanceFixed,
        view->DrawDistanceScale);
    float ShadowDistance = WorldVisibility_ScaleShadowDistance(DrawDistance, sprite->HasShadowDistance,
        sprite->ShadowDistance, sprite->IsDrawDistanceFixed, sprite->IsShadowDistanceFixed, view->DrawDistanceScale,
        GetShadowDistanceScale(view));
    WorldVisibilityFade Fade = GetFade(view, sprite->Shape.Position, sprite->BoundsRadius, DrawDistance, ShadowDistance,
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

    if (!sprite->IsShadowOnly && !AddToCamera(self, &self->_sprites[WorldVisibilityPass_Camera], view,
        sprite->Shape.Position, sprite->BoundsRadius, Fade.Combined, sprite->OmitsPixelation, index,
        &self->_counts.DrawnSpriteCount, NULL))
    {
        return false;
    }
    return !WorldRenderSprite_CastsShadows(sprite)
        || AddCaster(self->_sprites, view, sprite->Shape.Position, sprite->BoundsRadius, Fade.Shadow, index);
}

/* One terrain chunk entry (the cache lists only chunks of shown terrains): its fades, then the camera's list when it
 * is in view (counted with its triangles), and the shadow passes' lists, as a model's. */
static bool AddTerrainChunk(WorldVisibility* self, const WorldVisibilityView* view, const WorldRenderTerrainChunk* chunk,
    size_t index)
{
    float DrawDistance = WorldVisibility_ScaleDrawDistance(chunk->DrawDistance, chunk->IsDrawDistanceFixed,
        view->DrawDistanceScale);
    float ShadowDistance = WorldVisibility_ScaleShadowDistance(DrawDistance, chunk->HasShadowDistance,
        chunk->ShadowDistance, chunk->IsDrawDistanceFixed, chunk->IsShadowDistanceFixed, view->DrawDistanceScale,
        GetShadowDistanceScale(view));
    WorldVisibilityFade Fade = GetFade(view, chunk->RenderCenter, chunk->Radius, DrawDistance, ShadowDistance,
        GetZoomFactor(view), chunk->Tint);
    if (!GenericBuffer_AddLast(&self->_terrainChunkFades, &Fade))
    {
        return false;
    }
    if (!(Fade.Combined < 1.0f))
    {
        return true;
    }
    if (IsInVolume(view, &view->CameraFrustum, chunk->RenderCenter, chunk->Radius))
    {
        if (!AddIndex(&self->_terrainChunks[WorldVisibilityPass_Camera], index))
        {
            return false;
        }
        self->_counts.DrawnTerrainChunkCount++;
        self->_counts.DrawnTerrainTriangleCount += chunk->TriangleCount;
    }
    // The ground keeps the sky out wherever it overhangs (a cave, a cliff), whether it casts shadows or not.
    if (!AddSkyOccluder(&self->_terrainChunks[WorldVisibilityPass_SkyVisibility], view, chunk->RenderCenter,
        chunk->Radius, index))
    {
        return false;
    }
    return !chunk->CastsShadows || AddCaster(self->_terrainChunks, view, chunk->RenderCenter, chunk->Radius, Fade.Shadow,
        index);
}

/* One instance cell entry (the cache lists only cells of drawn components): its fades by its sphere around every
 * copy, then the camera's list when it is in view (counted with its copies) unless its copies are only shadows, and the
 * shadow passes' lists, as a model's (each copy fades and is cut by its size in the vertex stage on its own). */
static bool AddInstanceCell(WorldVisibility* self, const WorldVisibilityView* view, const WorldRenderInstanceCell* cell,
    size_t index)
{
    float DrawDistance = WorldVisibility_ScaleDrawDistance(cell->DrawDistance, cell->IsDrawDistanceFixed,
        view->DrawDistanceScale);
    float ShadowDistance = WorldVisibility_ScaleShadowDistance(DrawDistance, cell->HasShadowDistance,
        cell->ShadowDistance, cell->IsDrawDistanceFixed, cell->IsShadowDistanceFixed, view->DrawDistanceScale,
        GetShadowDistanceScale(view));
    WorldVisibilityFade Fade = GetFade(view, cell->RenderCenter, cell->Radius, DrawDistance, ShadowDistance,
        GetZoomFactor(view), cell->Tint);
    if (!GenericBuffer_AddLast(&self->_instanceCellFades, &Fade))
    {
        return false;
    }
    if (!(Fade.Combined < 1.0f))
    {
        self->_counts.InstanceCellsFadedOutCount++;
        return true;
    }
    // Copies that are only shadows cast and keep the sky out, but the camera's passes do not draw them.
    bool IsInView = !cell->IsShadowOnly && IsInVolume(view, &view->CameraFrustum, cell->RenderCenter, cell->Radius);
    if (IsInView)
    {
        if (!AddIndex(&self->_instanceCells[WorldVisibilityPass_Camera], index))
        {
            return false;
        }
        self->_counts.DrawnInstanceCellCount++;
        self->_counts.DrawnInstanceCount += cell->RecordCount;
    }
    else if (!cell->IsShadowOnly)
    {
        self->_counts.InstanceCellsOutsideViewCount++;
    }
    if (cell->OccludesSky && !AddSkyOccluder(&self->_instanceCells[WorldVisibilityPass_SkyVisibility], view,
        cell->RenderCenter, cell->Radius, index))
    {
        return false;
    }
    return !cell->CastsShadows || AddCaster(self->_instanceCells, view, cell->RenderCenter, cell->Radius, Fade.Shadow,
        index);
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

WorldFrustum WorldVisibility_ExtractReversedFrustum(Matrix viewProjection)
{
    // The sides do not depend on depth's convention. Inside along z where 0 <= z <= w: the near plane is w - z >= 0, and
    // z >= 0 holds at every distance in front (z is the near plane's constant there).
    const Matrix* M = &viewProjection;
    WorldFrustum Frustum = WorldVisibility_ExtractFrustum(viewProjection);
    Frustum.Planes[WorldFrustumPlane_Near] = MakePlane(M->m3 - M->m2, M->m7 - M->m6, M->m11 - M->m10, M->m15 - M->m14);
    Frustum.Planes[WorldFrustumPlane_Far] = (WorldPlane){ .Normal = { 0.0f, 0.0f, 0.0f }, .Distance = FLT_MAX };
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

float WorldVisibility_ScaleShadowDistance(float drawDistance, bool hasShadowDistance, float shadowDistance,
    bool isDrawDistanceFixed, bool isShadowDistanceFixed, float drawDistanceScale, float shadowDistanceScale)
{
    float Distance = hasShadowDistance ? shadowDistance
        : WorldVisibility_ScaleDrawDistance(shadowDistance, isDrawDistanceFixed, drawDistanceScale);
    Distance = WorldVisibility_ScaleDrawDistance(Distance, isShadowDistanceFixed, shadowDistanceScale);
    return (Distance < drawDistance) ? Distance : drawDistance; // a NaN takes the draw distance
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
    GenericBuffer_AllocateVariable(&self->_terrainChunkFades, INITIAL_LIST_CAPACITY, sizeof(WorldVisibilityFade));
    GenericBuffer_AllocateVariable(&self->_instanceCellFades, INITIAL_LIST_CAPACITY, sizeof(WorldVisibilityFade));
    for (int Pass = 0; Pass < (int)WorldVisibilityPass_Count; Pass++)
    {
        GenericBuffer_AllocateVariable(&self->_models[Pass], INITIAL_LIST_CAPACITY, sizeof(size_t));
        GenericBuffer_AllocateVariable(&self->_sprites[Pass], INITIAL_LIST_CAPACITY, sizeof(size_t));
        GenericBuffer_AllocateVariable(&self->_terrainChunks[Pass], INITIAL_LIST_CAPACITY, sizeof(size_t));
        GenericBuffer_AllocateVariable(&self->_instanceCells[Pass], INITIAL_LIST_CAPACITY, sizeof(size_t));
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
    Memory_Free(self->_terrainChunkFades._data);
    Memory_Free(self->_instanceCellFades._data);
    for (int Pass = 0; Pass < (int)WorldVisibilityPass_Count; Pass++)
    {
        Memory_Free(self->_models[Pass]._data);
        Memory_Free(self->_sprites[Pass]._data);
        Memory_Free(self->_terrainChunks[Pass]._data);
        Memory_Free(self->_instanceCells[Pass]._data);
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

    size_t ChunkCount = 0;
    const WorldRenderTerrainChunk* Chunks = WorldRenderCache_GetTerrainChunks(cache, &ChunkCount);
    for (size_t Index = 0; Index < ChunkCount; Index++)
    {
        if (!AddTerrainChunk(self, view, &Chunks[Index], Index))
        {
            Clear(self);
            return Error_Construct2(ErrorCode_InvalidOperation, "WorldVisibility: could not grow a terrain chunk "
                "list.");
        }
    }

    size_t CellCount = 0;
    const WorldRenderInstanceCell* Cells = WorldRenderCache_GetInstanceCells(cache, &CellCount);
    for (size_t Index = 0; Index < CellCount; Index++)
    {
        if (!AddInstanceCell(self, view, &Cells[Index], Index))
        {
            Clear(self);
            return Error_Construct2(ErrorCode_InvalidOperation, "WorldVisibility: could not grow an instance cell "
                "list.");
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

const size_t* WorldVisibility_GetTerrainChunks(const WorldVisibility* self, const WorldRenderCache* cache,
    WorldVisibilityPass pass, size_t* outCount)
{
    size_t ChunkCount = 0;
    WorldRenderCache_GetTerrainChunks(cache, &ChunkCount);
    *outCount = 0;
    if (!IsPass(pass) || (ChunkCount != self->_terrainChunkFades._count) || (self->_terrainChunks[pass]._count == 0))
    {
        return NULL;
    }
    *outCount = self->_terrainChunks[pass]._count;
    const void* Entries = self->_terrainChunks[pass]._data;
    return Entries;
}

const size_t* WorldVisibility_GetInstanceCells(const WorldVisibility* self, const WorldRenderCache* cache,
    WorldVisibilityPass pass, size_t* outCount)
{
    size_t CellCount = 0;
    WorldRenderCache_GetInstanceCells(cache, &CellCount);
    *outCount = 0;
    if (!IsPass(pass) || (CellCount != self->_instanceCellFades._count) || (self->_instanceCells[pass]._count == 0))
    {
        return NULL;
    }
    *outCount = self->_instanceCells[pass]._count;
    const void* Entries = self->_instanceCells[pass]._data;
    return Entries;
}

WorldVisibilityFade WorldVisibility_GetInstanceCellFade(const WorldVisibility* self, size_t index)
{
    if (index >= self->_instanceCellFades._count)
    {
        return (WorldVisibilityFade){ .Distance = 1.0f, .Combined = 1.0f, .Shadow = 1.0f };
    }
    const void* FadeData = self->_instanceCellFades._data;
    const WorldVisibilityFade* Fades = FadeData;
    return Fades[index];
}

WorldVisibilityFade WorldVisibility_GetModelFade(const WorldVisibility* self, size_t index)
{
    if (index >= self->_modelFades._count)
    {
        return (WorldVisibilityFade){ .Distance = 1.0f, .Combined = 1.0f, .Shadow = 1.0f };
    }
    const void* FadeData = self->_modelFades._data;
    const WorldVisibilityFade* Fades = FadeData;
    return Fades[index];
}

WorldVisibilityFade WorldVisibility_GetSpriteFade(const WorldVisibility* self, size_t index)
{
    if (index >= self->_spriteFades._count)
    {
        return (WorldVisibilityFade){ .Distance = 1.0f, .Combined = 1.0f, .Shadow = 1.0f };
    }
    const void* FadeData = self->_spriteFades._data;
    const WorldVisibilityFade* Fades = FadeData;
    return Fades[index];
}

WorldVisibilityFade WorldVisibility_GetTerrainChunkFade(const WorldVisibility* self, size_t index)
{
    if (index >= self->_terrainChunkFades._count)
    {
        return (WorldVisibilityFade){ .Distance = 1.0f, .Combined = 1.0f, .Shadow = 1.0f };
    }
    const void* FadeData = self->_terrainChunkFades._data;
    const WorldVisibilityFade* Fades = FadeData;
    return Fades[index];
}
