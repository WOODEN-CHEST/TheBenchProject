#include "TestSuites.h"
#include "WorldLod.h"
#include "WorldVisibility.h"
#include <math.h>
#include <stdint.h>
#include <string.h>


// Macros.
/** Tolerance for the float comparisons. */
#define TEST_EPSILON 1e-5f
/** How many evenly spaced progress values the complementarity test tries (besides the pattern's own thresholds). */
#define TEST_PROGRESS_STEPS 1000U


// Static functions: helpers.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_EPSILON;
}

/* Three LODs switching at 10 and 25 m (the last one's 60 m is its default draw distance), with hysteresis @p hysteresis
 * and a fade of 0.4 s. */
static WorldModelLodSettings MakeSettings(float hysteresis)
{
    return (WorldModelLodSettings)
    {
        .LodCount = 3U, .SwitchDistances = { 10.0f, 25.0f, 60.0f }, .Hysteresis = hysteresis, .FadeSeconds = 0.4f,
        .IsLodForced = false, .ForcedLod = 0U
    };
}

/* A state fading from @p from to @p to at @p progress. */
static WorldLodState MakeFade(size_t from, size_t to, float progress)
{
    return (WorldLodState){ .IsStarted = true, .CurrentLod = from, .TargetLod = to, .Progress = progress,
        .IsReturning = false };
}

/* A state that faded from @p from toward @p to as far as @p progress and now runs back to @p from. */
static WorldLodState MakeReturn(size_t from, size_t to, float progress)
{
    WorldLodState State = MakeFade(from, to, progress);
    State.IsReturning = true;
    return State;
}

/* Which LOD draws each cell of the 4x4 pattern for a state (SIZE_MAX where none does), with no fade on the draws. */
static void GetCellLods(const WorldLodState* state, size_t outLods[WORLD_VISIBILITY_DITHER_LEVELS])
{
    WorldLodDraw Draws[WORLD_LOD_MAX_DRAWS];
    size_t DrawCount = WorldLodState_GetDraws(state, Draws);
    for (uint32_t Cell = 0U; Cell < WORLD_VISIBILITY_DITHER_LEVELS; Cell++)
    {
        outLods[Cell] = SIZE_MAX;
        for (size_t Draw = 0U; Draw < DrawCount; Draw++)
        {
            if (!WorldVisibility_IsDitherDiscarded(0.0f, Draws[Draw].Progress, Draws[Draw].IsIncoming,
                Cell % WORLD_VISIBILITY_DITHER_SIZE, Cell / WORLD_VISIBILITY_DITHER_SIZE))
            {
                outLods[Cell] = Draws[Draw].Lod;
            }
        }
    }
}

/* Walks @p count distances from LOD @p start, as frames would, and returns how often the LOD changed. */
static size_t CountChanges(const WorldModelLodSettings* settings, size_t start, const float* distances, size_t count,
    size_t* outLod)
{
    size_t Lod = start;
    size_t Changes = 0U;
    for (size_t Index = 0U; Index < count; Index++)
    {
        size_t Next = WorldLod_Select(settings, distances[Index], 1.0f, true, Lod);
        Changes += (Next != Lod) ? 1U : 0U;
        Lod = Next;
    }
    *outLod = Lod;
    return Changes;
}


// Tests: selection.
/* Chosen afresh, LOD i is drawn below switch distance i and the last LOD past the last boundary (the last LOD's own
 * switch distance is its draw distance and never matters); a NaN distance draws the last LOD. */
static void ADistancePicksTheLodBelowItsSwitchDistance(TestRun* run)
{
    WorldModelLodSettings Settings = MakeSettings(0.0f);
    const float Distances[] = { 0.0f, 9.99f, 10.0f, 24.9f, 25.0f, 59.0f, 61.0f, 5000.0f, INFINITY, NAN };
    const size_t Expected[] = { 0U, 0U, 1U, 1U, 2U, 2U, 2U, 2U, 2U, 2U };
    for (size_t Index = 0U; Index < (sizeof(Distances) / sizeof(Distances[0])); Index++)
    {
        TEST_CHECK(run, WorldLod_Select(&Settings, Distances[Index], 1.0f, false, 0U) == Expected[Index]);
    }
    // A current LOD past the count counts as none: chosen afresh.
    TEST_CHECK(run, WorldLod_Select(&Settings, 12.0f, 1.0f, true, 7U) == 1U);
    // One LOD (or none) is always LOD 0.
    Settings.LodCount = 1U;
    TEST_CHECK(run, WorldLod_Select(&Settings, 100.0f, 1.0f, false, 0U) == 0U);
    Settings.LodCount = 0U;
    TEST_CHECK(run, WorldLod_Select(&Settings, 100.0f, 1.0f, true, 0U) == 0U);
}

/* From the LOD it shows, an object moves coarser only at a switch distance x (1 + h) and back only below x (1 - h): a
 * camera wobbling around a boundary changes nothing, walking through the band changes the LOD once each way, a jump
 * crosses several boundaries at once, and a NaN keeps the LOD. */
static void HysteresisKeepsAnObjectAtABoundarySteady(TestRun* run)
{
    WorldModelLodSettings Settings = MakeSettings(0.1f); // the boundary at 10 m becomes 9 .. 11 m
    TEST_CHECK(run, WorldLod_Select(&Settings, 10.5f, 1.0f, true, 0U) == 0U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 11.0f, 1.0f, true, 0U) == 1U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 9.5f, 1.0f, true, 1U) == 1U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 8.99f, 1.0f, true, 1U) == 0U);

    // Standing at the boundary, the camera's small moves never flip it, from either side.
    const float Wobble[] = { 9.6f, 10.4f, 9.2f, 10.9f, 10.0f, 9.05f, 10.95f, 9.8f, 10.2f, 10.0f };
    size_t Lod = 0U;
    TEST_CHECK(run, CountChanges(&Settings, 0U, Wobble, sizeof(Wobble) / sizeof(Wobble[0]), &Lod) == 0U);
    TEST_CHECK(run, CountChanges(&Settings, 1U, Wobble, sizeof(Wobble) / sizeof(Wobble[0]), &Lod) == 0U);

    // Walking away through the band and back: one switch each way, at the band's far and near edges.
    const float Walk[] = { 8.0f, 9.0f, 10.0f, 10.9f, 11.0f, 11.5f, 10.0f, 9.1f, 9.0f, 8.9f, 8.0f };
    const size_t WalkLods[] = { 0U, 0U, 0U, 0U, 1U, 1U, 1U, 1U, 1U, 0U, 0U };
    Lod = 0U;
    bool IsWalkRight = true;
    for (size_t Index = 0U; Index < (sizeof(Walk) / sizeof(Walk[0])); Index++)
    {
        Lod = WorldLod_Select(&Settings, Walk[Index], 1.0f, true, Lod);
        IsWalkRight = IsWalkRight && (Lod == WalkLods[Index]);
    }
    TEST_CHECK(run, IsWalkRight);

    // A jump crosses every boundary it passes (each past its own band), and a NaN keeps the LOD.
    TEST_CHECK(run, WorldLod_Select(&Settings, 500.0f, 1.0f, true, 0U) == 2U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 26.0f, 1.0f, true, 0U) == 1U); // 26 < 25 x 1.1
    TEST_CHECK(run, WorldLod_Select(&Settings, 1.0f, 1.0f, true, 2U) == 0U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 23.0f, 1.0f, true, 2U) == 2U); // not below 25 x 0.9 = 22.5
    TEST_CHECK(run, WorldLod_Select(&Settings, 22.0f, 1.0f, true, 2U) == 1U); // below 22.5, not below 10 x 0.9
    TEST_CHECK(run, WorldLod_Select(&Settings, NAN, 1.0f, true, 1U) == 1U);
    // A hysteresis outside [0, 1) counts as none.
    Settings.Hysteresis = 1.5f;
    TEST_CHECK(run, WorldLod_Select(&Settings, 10.0f, 1.0f, true, 0U) == 1U);
    Settings.Hysteresis = NAN;
    TEST_CHECK(run, WorldLod_Select(&Settings, 9.99f, 1.0f, true, 1U) == 0U);
}

/* The player's lod_distance_scale multiplies every switch distance (above 1 keeps detail farther; 0 always draws the
 * coarsest LOD, also from a LOD shown and for switch distances that overflowed to infinity; anything not finite or
 * below 0 counts as 1); the zoom factor the caller multiplies into the distance picks finer LODs when zoomed in. */
static void TheScaleAndTheZoomMoveTheSwitches(TestRun* run)
{
    WorldModelLodSettings Settings = MakeSettings(0.0f);
    TEST_CHECK(run, WorldLod_Select(&Settings, 15.0f, 1.0f, false, 0U) == 1U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 15.0f, 2.0f, false, 0U) == 0U);  // switches at 20 and 50
    TEST_CHECK(run, WorldLod_Select(&Settings, 45.0f, 2.0f, false, 0U) == 1U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 6.0f, 0.5f, false, 0U) == 1U);   // switches at 5 and 12.5
    TEST_CHECK(run, WorldLod_Select(&Settings, 0.0f, 0.0f, false, 0U) == 2U);   // nothing is below 0
    TEST_CHECK(run, WorldLod_Select(&Settings, 15.0f, NAN, false, 0U) == 1U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 15.0f, -3.0f, false, 0U) == 1U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 15.0f, INFINITY, false, 0U) == 1U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 1.0f, 0.0f, true, 0U) == 2U);

    // Switch distances that overflowed to infinity (a huge multiplier) times a scale of 0 would compare as NaN.
    WorldModelLodSettings Overflowed = MakeSettings(0.1f);
    Overflowed.SwitchDistances[0] = INFINITY;
    Overflowed.SwitchDistances[1] = INFINITY;
    TEST_CHECK(run, WorldLod_Select(&Overflowed, 5.0f, 0.0f, true, 0U) == 2U);
    TEST_CHECK(run, WorldLod_Select(&Overflowed, 5.0f, 0.0f, false, 0U) == 2U);
    TEST_CHECK(run, WorldLod_Select(&Overflowed, 5.0f, 1.0f, true, 2U) == 0U); // at scale 1 they are just far away

    // Zoomed to 15 degrees: 40 m reads as about 9.1 m, so LOD 0; at the default 60 degrees exactly 40 m, LOD 2.
    float Zoom = WorldVisibility_GetZoomFactor(15.0f);
    TEST_CHECK(run, (Zoom > 0.2f) && (Zoom < 0.25f));
    TEST_CHECK(run, WorldLod_Select(&Settings, 40.0f * Zoom, 1.0f, false, 0U) == 0U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 40.0f * WorldVisibility_GetZoomFactor(60.0f), 1.0f, false, 0U) == 2U);
}

/* A forced LOD is drawn whatever the distance, without hysteresis, and clamped to the last LOD. */
static void AForcedLodIsTakenAsItIs(TestRun* run)
{
    WorldModelLodSettings Settings = MakeSettings(0.1f);
    Settings.IsLodForced = true;
    Settings.ForcedLod = 1U;
    TEST_CHECK(run, WorldLod_Select(&Settings, 0.0f, 1.0f, false, 0U) == 1U);
    TEST_CHECK(run, WorldLod_Select(&Settings, 500.0f, 1.0f, true, 2U) == 1U);
    TEST_CHECK(run, WorldLod_Select(&Settings, NAN, 0.0f, true, 0U) == 1U);
    Settings.ForcedLod = 9U;
    TEST_CHECK(run, WorldLod_Select(&Settings, 0.0f, 1.0f, false, 0U) == 2U);
    // Off again: chosen by distance.
    Settings.IsLodForced = false;
    TEST_CHECK(run, WorldLod_Select(&Settings, 0.0f, 1.0f, false, 0U) == 0U);
}


// Tests: the cross-fade.
/* A new state holds no LOD (it reads as LOD 0, not fading); its first update takes the selected LOD at once. */
static void TheFirstUpdateTakesTheLodAtOnce(TestRun* run)
{
    WorldLodState State = WorldLodState_Create();
    TEST_CHECK(run, !State.IsStarted && (State.CurrentLod == 0U) && !WorldLodState_IsFading(&State));
    WorldLodState_Update(&State, 2U, 0.016f, 0.4f);
    TEST_CHECK(run, State.IsStarted && (State.CurrentLod == 2U) && (State.TargetLod == 2U) && (State.Progress == 1.0f));
    WorldLodDraw Draws[WORLD_LOD_MAX_DRAWS];
    TEST_CHECK(run, (WorldLodState_GetDraws(&State, Draws) == 1U) && (Draws[0].Lod == 2U) && (Draws[0].Progress == 0.0f)
        && !Draws[0].IsIncoming);
}

/* A change starts a fade that advances by the frame's seconds over the fade time: both LODs are drawn (the outgoing first,
 * at the same progress), the G-buffer's LOD turns at half, the shadows draw the incoming LOD from the start, and at 1
 * the incoming LOD becomes the only one. */
static void AFadeAdvancesWithTime(TestRun* run)
{
    WorldLodState State = WorldLodState_Create();
    WorldLodState_Update(&State, 0U, 0.1f, 0.4f);
    WorldLodState_Update(&State, 1U, 0.1f, 0.4f); // starts and advances: 0.1 / 0.4
    TEST_CHECK(run, WorldLodState_IsFading(&State) && (State.CurrentLod == 0U) && (State.TargetLod == 1U)
        && IsNear(State.Progress, 0.25f));
    WorldLodDraw Draws[WORLD_LOD_MAX_DRAWS];
    TEST_CHECK(run, (WorldLodState_GetDraws(&State, Draws) == 2U)
        && (Draws[0].Lod == 0U) && !Draws[0].IsIncoming && IsNear(Draws[0].Progress, 0.25f)
        && (Draws[1].Lod == 1U) && Draws[1].IsIncoming && IsNear(Draws[1].Progress, 0.25f));
    TEST_CHECK(run, (WorldLodState_GetSolidLod(&State) == 0U) && (WorldLodState_GetShadowLod(&State) == 1U));

    WorldLodState_Update(&State, 1U, 0.1f, 0.4f);
    TEST_CHECK(run, IsNear(State.Progress, 0.5f) && (WorldLodState_GetSolidLod(&State) == 1U));
    WorldLodState_Update(&State, 1U, 0.1f, 0.4f);
    TEST_CHECK(run, WorldLodState_IsFading(&State) && IsNear(State.Progress, 0.75f));
    WorldLodState_Update(&State, 1U, 0.15f, 0.4f); // past the end: done
    TEST_CHECK(run, !WorldLodState_IsFading(&State) && (State.CurrentLod == 1U) && (State.Progress == 1.0f));
    TEST_CHECK(run, (WorldLodState_GetDraws(&State, Draws) == 1U) && (Draws[0].Lod == 1U));
    TEST_CHECK(run, (WorldLodState_GetSolidLod(&State) == 1U) && (WorldLodState_GetShadowLod(&State) == 1U));
}

/* Going back to the LOD a fade started from turns the fade around: at the turn every pixel keeps its LOD (a frame
 * without time changes none; a frame of time gives back only the cells the share passes on its way down), the shadows
 * and the hysteresis follow the LOD it heads back to, and it ends there at 0. Turning again runs it forward from where
 * it is. */
static void GoingBackDuringAFadeTurnsItAround(TestRun* run)
{
    size_t Before[WORLD_VISIBILITY_DITHER_LEVELS];
    size_t After[WORLD_VISIBILITY_DITHER_LEVELS];
    WorldLodState State = MakeFade(0U, 1U, 0.6f);
    GetCellLods(&State, Before);
    WorldLodState_Update(&State, 0U, 0.0f, 0.4f);
    GetCellLods(&State, After);
    TEST_CHECK(run, WorldLodState_IsFading(&State) && State.IsReturning && (State.CurrentLod == 0U)
        && (State.TargetLod == 1U) && (State.Progress == 0.6f) && (WorldLodState_GetDestinationLod(&State) == 0U)
        && (WorldLodState_GetShadowLod(&State) == 0U) && (WorldLodState_GetSolidLod(&State) == 1U));
    TEST_CHECK(run, memcmp(Before, After, sizeof(Before)) == 0);

    // 0.1 s of the 0.4 s fade runs the share back from 0.6 to 0.35: the cells with thresholds in (0.35, 0.6] go back.
    WorldLodState_Update(&State, 0U, 0.1f, 0.4f);
    GetCellLods(&State, After);
    TEST_CHECK(run, State.IsReturning && IsNear(State.Progress, 0.35f) && (WorldLodState_GetSolidLod(&State) == 0U));
    bool AreOnlyThePassedCellsBack = true;
    for (uint32_t Cell = 0U; Cell < WORLD_VISIBILITY_DITHER_LEVELS; Cell++)
    {
        float Threshold = WorldVisibility_GetDitherThreshold(Cell % WORLD_VISIBILITY_DITHER_SIZE,
            Cell / WORLD_VISIBILITY_DITHER_SIZE);
        size_t Expected = ((Threshold > 0.35f) && (Threshold <= 0.6f)) ? 0U : Before[Cell];
        AreOnlyThePassedCellsBack = AreOnlyThePassedCellsBack && (After[Cell] == Expected);
    }
    TEST_CHECK(run, AreOnlyThePassedCellsBack);

    // Turning once more runs it forward from there; turning back and staying ends it at LOD 0.
    WorldLodState_Update(&State, 1U, 0.1f, 0.4f);
    TEST_CHECK(run, !State.IsReturning && IsNear(State.Progress, 0.6f)
        && (WorldLodState_GetDestinationLod(&State) == 1U));
    WorldLodState_Update(&State, 0U, 0.1f, 0.4f);
    WorldLodState_Update(&State, 0U, 0.1f, 0.4f);
    TEST_CHECK(run, State.IsReturning && IsNear(State.Progress, 0.1f));
    WorldLodState_Update(&State, 0U, 0.1f, 0.4f);
    TEST_CHECK(run, !WorldLodState_IsFading(&State) && !State.IsReturning && (State.CurrentLod == 0U)
        && (State.TargetLod == 0U) && (State.Progress == 1.0f));
}

/* A third LOD during a fade keeps the LOD with the larger share (the G-buffer's) and fades from it to the new one, so
 * at most half the pixels switch at once and there are never three LODs; a fade running back is treated the same. */
static void AThirdLodDuringAFadeStartsFromTheLargerShare(TestRun* run)
{
    WorldLodState State = MakeFade(0U, 1U, 0.3f);
    WorldLodState_Update(&State, 2U, 0.1f, 0.4f);
    TEST_CHECK(run, (State.CurrentLod == 0U) && (State.TargetLod == 2U) && !State.IsReturning
        && IsNear(State.Progress, 0.25f) && (WorldLodState_GetShadowLod(&State) == 2U));
    State = MakeFade(0U, 1U, 0.7f);
    WorldLodState_Update(&State, 2U, 0.1f, 0.4f);
    TEST_CHECK(run, (State.CurrentLod == 1U) && (State.TargetLod == 2U) && IsNear(State.Progress, 0.25f));
    // At exactly half the incoming LOD, the G-buffer's from there on, stays.
    State = MakeFade(0U, 1U, 0.5f);
    WorldLodState_Update(&State, 2U, 0.0f, 0.4f);
    TEST_CHECK(run, (State.CurrentLod == 1U) && (State.TargetLod == 2U) && (State.Progress == 0.0f));
    // Running back to LOD 0 while LOD 1 still has the larger share: LOD 1 stays.
    State = MakeReturn(0U, 1U, 0.7f);
    WorldLodState_Update(&State, 2U, 0.1f, 0.4f);
    TEST_CHECK(run, (State.CurrentLod == 1U) && (State.TargetLod == 2U) && !State.IsReturning
        && (WorldLodState_GetDestinationLod(&State) == 2U));
    WorldLodDraw Draws[WORLD_LOD_MAX_DRAWS];
    TEST_CHECK(run, (WorldLodState_GetDraws(&State, Draws) == 2U) && (Draws[0].Lod == 1U) && (Draws[1].Lod == 2U));
}

/* A fade time of 0 swaps at once (in the frame of the change), as does one that is not finite; a fade running back gets
 * back at once. */
static void AFadeTimeOf0SwapsAtOnce(TestRun* run)
{
    const float FadeTimes[] = { 0.0f, -1.0f, NAN, INFINITY };
    for (size_t Index = 0U; Index < (sizeof(FadeTimes) / sizeof(FadeTimes[0])); Index++)
    {
        WorldLodState State = MakeFade(1U, 1U, 1.0f);
        WorldLodState_Update(&State, 2U, 0.016f, FadeTimes[Index]);
        TEST_CHECK(run, !WorldLodState_IsFading(&State) && (State.CurrentLod == 2U) && (State.Progress == 1.0f));
        State = MakeFade(0U, 1U, 0.5f);
        WorldLodState_Update(&State, 0U, 0.016f, FadeTimes[Index]);
        TEST_CHECK(run, !WorldLodState_IsFading(&State) && (State.CurrentLod == 0U) && !State.IsReturning
            && (State.Progress == 1.0f));
    }
}

/* A frame without time (0, negative, not finite) holds a fade where it is; it starts it at 0. */
static void AFrameWithoutTimeHoldsTheFade(TestRun* run)
{
    const float Deltas[] = { 0.0f, -0.5f, NAN, INFINITY };
    for (size_t Index = 0U; Index < (sizeof(Deltas) / sizeof(Deltas[0])); Index++)
    {
        WorldLodState State = MakeFade(0U, 1U, 0.3f);
        WorldLodState_Update(&State, 1U, Deltas[Index], 0.4f);
        TEST_CHECK(run, WorldLodState_IsFading(&State) && IsNear(State.Progress, 0.3f));
        State = MakeFade(0U, 0U, 1.0f);
        WorldLodState_Update(&State, 1U, Deltas[Index], 0.4f);
        TEST_CHECK(run, WorldLodState_IsFading(&State) && (State.Progress == 0.0f));
        State = MakeReturn(0U, 1U, 0.3f);
        WorldLodState_Update(&State, 0U, Deltas[Index], 0.4f);
        TEST_CHECK(run, State.IsReturning && IsNear(State.Progress, 0.3f));
    }
}


// Tests: the dither masks.
/* Whether a pixel of cell (x, y) is kept by any of a state's draws at @p fade, and by how many. */
static size_t CountKeeping(const WorldLodState* state, float fade, uint32_t x, uint32_t y)
{
    WorldLodDraw Draws[WORLD_LOD_MAX_DRAWS];
    size_t DrawCount = WorldLodState_GetDraws(state, Draws);
    size_t Keeping = 0U;
    for (size_t Draw = 0U; Draw < DrawCount; Draw++)
    {
        Keeping += WorldVisibility_IsDitherDiscarded(fade, Draws[Draw].Progress, Draws[Draw].IsIncoming, x, y) ? 0U : 1U;
    }
    return Keeping;
}

/* The two LODs of a cross-fade draw every pixel of every 4x4 cell exactly once, at every progress: evenly spaced ones,
 * the pattern's own thresholds and their float neighbours on both sides (where a rounded 1 - p would double-draw or leave
 * a hole); with the draw's fade on top they draw exactly the pixels the fade keeps, each once; and the incoming LOD's
 * share is the progress's (k of 16 pixels at k/16). */
static void TheTwoLodsOfACrossFadeDrawEveryPixelOnce(TestRun* run)
{
    float Progresses[TEST_PROGRESS_STEPS + 1U + (3U * WORLD_VISIBILITY_DITHER_LEVELS)];
    size_t ProgressCount = 0U;
    for (uint32_t Step = 0U; Step <= TEST_PROGRESS_STEPS; Step++)
    {
        Progresses[ProgressCount++] = (float)Step / (float)TEST_PROGRESS_STEPS;
    }
    for (uint32_t Level = 0U; Level < WORLD_VISIBILITY_DITHER_LEVELS; Level++)
    {
        float Threshold = ((float)Level + 0.5f) / (float)WORLD_VISIBILITY_DITHER_LEVELS;
        Progresses[ProgressCount++] = Threshold;
        Progresses[ProgressCount++] = nextafterf(Threshold, 0.0f);
        Progresses[ProgressCount++] = nextafterf(Threshold, 1.0f);
    }
    const float Fades[] = { 0.0f, 0.25f, 0.5f, 0.71f, 15.0f / 16.0f, 1.0f };

    bool IsEveryPixelOnce = true;
    bool IsTheFadeKept = true;
    for (size_t Index = 0U; Index < ProgressCount; Index++)
    {
        // A fade running forward and one running back draw alike at the same progress.
        const WorldLodState States[] = { MakeFade(0U, 1U, Progresses[Index]), MakeReturn(0U, 1U, Progresses[Index]) };
        for (size_t Kind = 0U; Kind < (sizeof(States) / sizeof(States[0])); Kind++)
        {
            for (uint32_t Cell = 0U; Cell < WORLD_VISIBILITY_DITHER_LEVELS; Cell++)
            {
                uint32_t X = Cell % WORLD_VISIBILITY_DITHER_SIZE;
                uint32_t Y = Cell / WORLD_VISIBILITY_DITHER_SIZE;
                IsEveryPixelOnce = IsEveryPixelOnce && (CountKeeping(&States[Kind], 0.0f, X, Y) == 1U);
                for (size_t Fade = 0U; Fade < (sizeof(Fades) / sizeof(Fades[0])); Fade++)
                {
                    size_t Expected = WorldVisibility_IsDitherDiscarded(Fades[Fade], 0.0f, false, X, Y) ? 0U : 1U;
                    IsTheFadeKept = IsTheFadeKept && (CountKeeping(&States[Kind], Fades[Fade], X, Y) == Expected);
                }
            }
        }
    }
    TEST_CHECK(run, IsEveryPixelOnce);
    TEST_CHECK(run, IsTheFadeKept);

    bool AreSharesExact = true;
    for (uint32_t Level = 0U; Level <= WORLD_VISIBILITY_DITHER_LEVELS; Level++)
    {
        WorldLodState State = MakeFade(2U, 3U, (float)Level / (float)WORLD_VISIBILITY_DITHER_LEVELS);
        WorldLodDraw Draws[WORLD_LOD_MAX_DRAWS];
        WorldLodState_GetDraws(&State, Draws);
        uint32_t Incoming = 0U;
        for (uint32_t Cell = 0U; Cell < WORLD_VISIBILITY_DITHER_LEVELS; Cell++)
        {
            Incoming += WorldVisibility_IsDitherDiscarded(0.0f, Draws[1].Progress, true, Cell % 4U, Cell / 4U) ? 0U : 1U;
        }
        AreSharesExact = AreSharesExact && (Incoming == Level);
    }
    TEST_CHECK(run, AreSharesExact);

    // A LOD drawn alone keeps every pixel the fade keeps.
    WorldLodState Alone = MakeFade(1U, 1U, 1.0f);
    bool IsAloneWhole = true;
    for (uint32_t Cell = 0U; Cell < WORLD_VISIBILITY_DITHER_LEVELS; Cell++)
    {
        IsAloneWhole = IsAloneWhole && (CountKeeping(&Alone, 0.0f, Cell % 4U, Cell / 4U) == 1U);
    }
    TEST_CHECK(run, IsAloneWhole);
}


// Public functions.
void WorldLodTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_distance_picks_the_lod_below_its_switch_distance", ADistancePicksTheLodBelowItsSwitchDistance },
        { "hysteresis_keeps_an_object_at_a_boundary_steady", HysteresisKeepsAnObjectAtABoundarySteady },
        { "the_scale_and_the_zoom_move_the_switches", TheScaleAndTheZoomMoveTheSwitches },
        { "a_forced_lod_is_taken_as_it_is", AForcedLodIsTakenAsItIs },
        { "the_first_update_takes_the_lod_at_once", TheFirstUpdateTakesTheLodAtOnce },
        { "a_fade_advances_with_time", AFadeAdvancesWithTime },
        { "going_back_during_a_fade_turns_it_around", GoingBackDuringAFadeTurnsItAround },
        { "a_third_lod_during_a_fade_starts_from_the_larger_share", AThirdLodDuringAFadeStartsFromTheLargerShare },
        { "a_fade_time_of_0_swaps_at_once", AFadeTimeOf0SwapsAtOnce },
        { "a_frame_without_time_holds_the_fade", AFrameWithoutTimeHoldsTheFade },
        { "the_two_lods_of_a_cross_fade_draw_every_pixel_once", TheTwoLodsOfACrossFadeDrawEveryPixelOnce },
    };
    TestRun_RunSuite(run, "WorldLod", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
