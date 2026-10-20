#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "WorldModel.h"

/*
 * Private header (source/): an object's levels of detail as pure maths, tested headless (tests/WorldLodTests.c).
 *
 * SELECTION. A placement's switch distances in effect (WorldModelLodSettings, from
 * WorldModelComponent_ResolveLodSettings: its world model's max distances or its own list, times its multiplier), times
 * the player's lod_distance_scale, decide its LOD by the camera's distance to its bounding sphere's centre times the
 * zoom factor (WorldVisibility_GetZoomFactor, so zooming in picks finer LODs): LOD i while the distance is below switch
 * distance i, the last LOD past the last boundary (it keeps drawing until the draw distance, where the draw-distance
 * fade takes it). From the LOD an object shows, the world model's hysteresis h moves it to a coarser LOD only once the
 * distance reaches a switch distance x (1 + h), and back only below x (1 - h), so an object standing at a boundary does
 * not flicker. A forced LOD is taken as it is, without hysteresis.
 *
 * THE CROSS-FADE. WorldLodState is one object's state across frames: the LOD a fade started from, the LOD it started
 * toward, and the second one's share of the pixels (0..1, moved by each frame's real seconds over the world model's
 * fade time; a fade time of 0 swaps at once). While it fades, both LODs are drawn with complementary dither masks
 * (shaders/common/dither.glsl: the LOD it started from keeps the pixels whose threshold is above the share, the other
 * one the rest), so every pixel is drawn by exactly one of them and the background never shows; the draw-distance and
 * opacity fade removes its pixels from both alike. Changes during a fade never pop the image back: when the selection
 * returns to the LOD the fade comes from, the fade runs back (the share falls) and every pixel keeps its LOD at the
 * turn; a third LOD keeps the LOD with the larger share and fades from it to the new one (the other LOD's pixels, at
 * most half, switch at once), so there are never three LODs at once. The G-buffer draws the LOD with the larger share
 * solid, the shadow passes the LOD the object heads to solid (no dither from the cross-fade), so a shadow changes once
 * per change and never shimmers.
 */


// Macros.
/** @brief The most LODs drawn at once for one object: the outgoing and the incoming LOD of a cross-fade. */
#define WORLD_LOD_MAX_DRAWS 2U
/** @brief From this progress on, the incoming LOD holds the larger share of a cross-fade (the G-buffer draws it). */
#define WORLD_LOD_SOLID_SHARE 0.5f


// Types.
/**
 * @brief One object's LOD state across frames: what the renderer's cache keeps per model entry.
 *
 * A plain value type: start with WorldLodState_Create, then WorldLodState_Update once per frame with the LOD selected.
 */
typedef struct WorldLodStateStruct
{
    /** @brief Whether the state holds a LOD yet: false until the first update, which takes the selected LOD at once
     *  (without a fade). */
    bool IsStarted;
    /** @brief The LOD drawn while no fade runs; during a fade, the LOD it started from, which keeps the pixels whose
     *  dither threshold lies above Progress (the outgoing mask). */
    size_t CurrentLod;
    /** @brief During a fade, the LOD it started toward, which keeps the pixels whose dither threshold is at or below
     *  Progress (the incoming mask); equal to CurrentLod while no fade runs. */
    size_t TargetLod;
    /** @brief TargetLod's share of the pixels, in [0, 1]: it rises while the fade runs to TargetLod and falls while it
     *  returns; 1 while no fade runs. */
    float Progress;
    /** @brief Whether the fade returns to CurrentLod (the selection went back to it during the fade): Progress falls,
     *  and the fade ends at CurrentLod when it reaches 0. False while no fade runs. */
    bool IsReturning;
} WorldLodState;

/** @brief One LOD a pass draws for an object, with its part of the cross-fade's dither. A plain value type. */
typedef struct WorldLodDrawStruct
{
    /** @brief The LOD. */
    size_t Lod;
    /** @brief The cross-fade's progress: the share of the pixels the incoming LOD takes (0 for a LOD drawn alone, which
     *  then keeps every pixel). */
    float Progress;
    /** @brief Whether this is the incoming LOD: it keeps the pixels whose dither threshold is at or below Progress; the
     *  outgoing LOD keeps the others. */
    bool IsIncoming;
} WorldLodDraw;


// Functions.
/**
 * @brief The LOD an object shows at a distance: chosen afresh, or from the LOD it shows now with the hysteresis.
 *
 * A forced LOD (settings->IsLodForced) is returned as it is (clamped to the last LOD). Otherwise switch distance i is
 * settings->SwitchDistances[i] x @p distanceScale; afresh, LOD i is the first whose switch distance lies beyond the
 * distance (the last LOD past them all); from @p currentLod the object moves to a coarser LOD only past a boundary's
 * switch distance x (1 + h) and back to a finer one only below x (1 - h), h the settings' hysteresis. A scale of 0
 * gives the last LOD at any distance (also for switch distances that overflowed to infinity).
 * @param settings The placement's LOD settings; must not be NULL. Its LodCount is taken as at most
 *        WORLD_MODEL_MAX_LODS.
 * @param distance The camera's distance to the object's bounding sphere's centre times the zoom factor, in metres. A
 *        NaN keeps the current LOD (afresh: the last).
 * @param distanceScale The player's lod_distance_scale; anything not finite or below 0 counts as 1.
 * @param hasCurrentLod Whether @p currentLod is the LOD the object shows or heads to now
 *        (WorldLodState_GetDestinationLod); false chooses afresh.
 * @param currentLod That LOD; one past the last LOD also chooses afresh.
 * @returns The LOD, below the settings' LOD count (0 for a count of 0 or 1).
 */
size_t WorldLod_Select(const WorldModelLodSettings* settings, float distance, float distanceScale, bool hasCurrentLod,
    size_t currentLod);

/**
 * @brief A state that holds no LOD yet: its first update takes the selected LOD at once. Until then it reads as LOD 0,
 *        not fading.
 * @returns The state.
 */
WorldLodState WorldLodState_Create(void);

/**
 * @brief Brings a state to this frame: follows a change of the selected LOD, then moves the fade on.
 *
 * The first update of a state that is not started takes @p selectedLod at once. While no fade runs, another LOD starts
 * a fade from the LOD drawn. During a fade, the LOD it started from turns it around (it returns, every pixel keeping
 * its LOD), the LOD it heads to changes nothing, and a third LOD starts a new fade from the LOD with the larger share
 * (WorldLodState_GetSolidLod) to it. A fade moves by @p deltaSeconds / @p fadeSeconds per update, beginning with the
 * update that starts it, and ends at the LOD it heads to (Progress 1 to TargetLod, 0 back to CurrentLod); a fade time
 * that is 0 (or not finite) gets there at once.
 * @param self The state; must not be NULL.
 * @param selectedLod The LOD selected this frame (WorldLod_Select).
 * @param deltaSeconds Real seconds since the previous update; 0, negative or not finite holds the fade.
 * @param fadeSeconds How long a cross-fade takes (the world model's lod_fade_seconds).
 */
void WorldLodState_Update(WorldLodState* self, size_t selectedLod, float deltaSeconds, float fadeSeconds);

/**
 * @brief Reports whether a cross-fade runs: two LODs are drawn.
 * @param self The state; must not be NULL.
 * @returns true while CurrentLod and TargetLod differ.
 */
static inline bool WorldLodState_IsFading(const WorldLodState* self)
{
    return self->CurrentLod != self->TargetLod;
}

/**
 * @brief The LOD an object heads to: the one a running fade ends at (TargetLod, or CurrentLod while it returns), else
 *        the one drawn. The selection's hysteresis starts from it, the shadow passes draw it and the status counts by
 *        it.
 * @param self The state; must not be NULL.
 * @returns The LOD.
 */
static inline size_t WorldLodState_GetDestinationLod(const WorldLodState* self)
{
    return self->IsReturning ? self->CurrentLod : self->TargetLod;
}

/**
 * @brief The LODs the camera's passes draw for an object, with their parts of the cross-fade's dither: one LOD (keeping
 *        every pixel) while no fade runs; while one runs, the outgoing LOD and the incoming one, whose masks are
 *        complementary.
 * @param self The state; must not be NULL.
 * @param outDraws [out] Receives the draws, the outgoing LOD first. Must hold WORLD_LOD_MAX_DRAWS.
 * @returns How many draws were written: 1 or 2.
 */
size_t WorldLodState_GetDraws(const WorldLodState* self, WorldLodDraw outDraws[WORLD_LOD_MAX_DRAWS]);

/**
 * @brief The LOD drawn solid where a pass draws one LOD for the camera (the G-buffer, the unlit fallback): the one with
 *        the larger share of a cross-fade (the incoming one from WORLD_LOD_SOLID_SHARE on).
 * @param self The state; must not be NULL.
 * @returns The LOD.
 */
size_t WorldLodState_GetSolidLod(const WorldLodState* self);

/**
 * @brief The LOD the shadow passes draw: the one the object heads to, solid, so a shadow changes once per change and
 *        never shimmers.
 * @param self The state; must not be NULL.
 * @returns The LOD.
 */
static inline size_t WorldLodState_GetShadowLod(const WorldLodState* self)
{
    return WorldLodState_GetDestinationLod(self);
}
