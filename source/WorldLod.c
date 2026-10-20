// An object's levels of detail: the LOD selection with hysteresis and the cross-fade state (see WorldLod.h).

#include <math.h>
#include "WorldLod.h"


// Static functions.
/* The distance scale to use: anything not finite or below 0 counts as 1. */
static float SanitizeScale(float scale)
{
    return (isfinite(scale) && (scale >= 0.0f)) ? scale : 1.0f;
}

/* The hysteresis to use: a share in [0, 1); anything else counts as none. */
static float SanitizeHysteresis(float hysteresis)
{
    return ((hysteresis >= 0.0f) && (hysteresis < 1.0f)) ? hysteresis : 0.0f;
}

/* A state that shows @p lod, with no fade running. */
static WorldLodState SettledAt(size_t lod)
{
    return (WorldLodState){ .IsStarted = true, .CurrentLod = lod, .TargetLod = lod, .Progress = 1.0f,
        .IsReturning = false };
}

/* The LOD for a distance without a LOD to start from: the first whose switch distance lies beyond it, the last past them
 * all (the last LOD's own switch distance never matters: it is drawn until the draw distance). */
static size_t SelectAfresh(const WorldModelLodSettings* settings, size_t lodCount, float distance, float scale)
{
    for (size_t Lod = 0U; (Lod + 1U) < lodCount; Lod++)
    {
        if (distance < (settings->SwitchDistances[Lod] * scale))
        {
            return Lod;
        }
    }
    return lodCount - 1U;
}


// Internal functions (declared in WorldLod.h).
size_t WorldLod_Select(const WorldModelLodSettings* settings, float distance, float distanceScale, bool hasCurrentLod,
    size_t currentLod)
{
    size_t Count = (settings->LodCount < WORLD_MODEL_MAX_LODS) ? settings->LodCount : WORLD_MODEL_MAX_LODS;
    if (Count <= 1U)
    {
        return 0U;
    }
    if (settings->IsLodForced)
    {
        return (settings->ForcedLod < Count) ? settings->ForcedLod : (Count - 1U);
    }
    float Scale = SanitizeScale(distanceScale);
    if (Scale == 0.0f)
    {
        return Count - 1U; // every switch distance is 0 (an infinite one times 0 would compare as NaN)
    }
    if (!hasCurrentLod || (currentLod >= Count))
    {
        return SelectAfresh(settings, Count, distance, Scale);
    }

    // Coarser past each boundary's far edge, finer below its near edge (a NaN distance passes neither).
    float Hysteresis = SanitizeHysteresis(settings->Hysteresis);
    size_t Lod = currentLod;
    while (((Lod + 1U) < Count) && (distance >= ((settings->SwitchDistances[Lod] * Scale) * (1.0f + Hysteresis))))
    {
        Lod++;
    }
    if (Lod != currentLod)
    {
        return Lod;
    }
    while ((Lod > 0U) && (distance < ((settings->SwitchDistances[Lod - 1U] * Scale) * (1.0f - Hysteresis))))
    {
        Lod--;
    }
    return Lod;
}

WorldLodState WorldLodState_Create(void)
{
    return (WorldLodState){ .IsStarted = false, .CurrentLod = 0U, .TargetLod = 0U, .Progress = 1.0f,
        .IsReturning = false };
}

void WorldLodState_Update(WorldLodState* self, size_t selectedLod, float deltaSeconds, float fadeSeconds)
{
    if (!self->IsStarted)
    {
        *self = SettledAt(selectedLod);
        return;
    }
    if (selectedLod != WorldLodState_GetDestinationLod(self))
    {
        if (!WorldLodState_IsFading(self))
        {
            *self = SettledAt(self->CurrentLod);
            self->TargetLod = selectedLod;
            self->Progress = 0.0f;
        }
        else if (selectedLod == (self->IsReturning ? self->TargetLod : self->CurrentLod))
        {
            // Back to the LOD the fade comes from: the fade turns around and every pixel keeps its LOD.
            self->IsReturning = !self->IsReturning;
        }
        else
        {
            // A third LOD: the LOD with the larger share stays (the G-buffer's) and the new fade starts from it; the
            // other one's pixels, at most half, switch at once. Never three LODs at once.
            size_t KeptLod = WorldLodState_GetSolidLod(self);
            *self = SettledAt(KeptLod);
            self->TargetLod = selectedLod;
            self->Progress = 0.0f;
        }
    }
    if (!WorldLodState_IsFading(self))
    {
        *self = SettledAt(self->CurrentLod);
        return;
    }

    float Step = (isfinite(deltaSeconds) && (deltaSeconds > 0.0f)) ? deltaSeconds : 0.0f;
    bool IsInstant = !(isfinite(fadeSeconds) && (fadeSeconds > 0.0f));
    if (self->IsReturning)
    {
        self->Progress = IsInstant ? 0.0f : (self->Progress - (Step / fadeSeconds));
        if (!(self->Progress > 0.0f))
        {
            *self = SettledAt(self->CurrentLod);
        }
        return;
    }
    self->Progress = IsInstant ? 1.0f : (self->Progress + (Step / fadeSeconds));
    if (!(self->Progress < 1.0f))
    {
        *self = SettledAt(self->TargetLod);
    }
}

size_t WorldLodState_GetDraws(const WorldLodState* self, WorldLodDraw outDraws[WORLD_LOD_MAX_DRAWS])
{
    if (!WorldLodState_IsFading(self))
    {
        outDraws[0] = (WorldLodDraw){ .Lod = self->CurrentLod, .Progress = 0.0f, .IsIncoming = false };
        return 1U;
    }
    outDraws[0] = (WorldLodDraw){ .Lod = self->CurrentLod, .Progress = self->Progress, .IsIncoming = false };
    outDraws[1] = (WorldLodDraw){ .Lod = self->TargetLod, .Progress = self->Progress, .IsIncoming = true };
    return 2U;
}

size_t WorldLodState_GetSolidLod(const WorldLodState* self)
{
    return (WorldLodState_IsFading(self) && (self->Progress >= WORLD_LOD_SOLID_SHARE)) ? self->TargetLod
        : self->CurrentLod;
}
