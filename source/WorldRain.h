#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector3 is raylib's: the wind, the rain's and the bolt's directions.
#include "raylib/raylib.h"
#include "WorldEnvironment.h"
#include "WorldRenderSpace.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): the rain and the lightning's bolts as the world renderer draws them, pure maths tested
 * headless (tests/WorldRainTests.c). The rain pass (WorldRendererRain.c) builds one WorldRainFrame a frame from the
 * environment's weather values and its rain settings, and feeds it to shaders/common/rain.glsl,
 * shaders/common/puddles.glsl and shaders/rain/, whose formulas this file mirrors (keep them in step;
 * tests/ShaderSourceTests.c pins the shared constants).
 *
 * THE RAIN'S TOP. Rain falls from the clouds' base (the atmosphere's ground plus CloudBase, WorldRainFrame.TopY): above
 * it nothing of the rain shows (no streak, no splash, no wetness or puddle on a surface there), fading out over the last
 * WORLD_RAIN_TOP_FADE metres below it (WorldRain_GetTopOpenness), so a camera above the clouds sees none around it and a
 * mountain above them stays dry. The streaks, the splashes and the rain map are not drawn at all while everything they
 * reach lies above the top (WorldRain_GetFrame takes the camera's height).
 *
 * THE RAIN. The weather's precipitation (0 none .. 1 a downpour) is a rain rate of WORLD_RAIN_DOWNPOUR_RATE x p^3
 * millimetres an hour (a light rain's 0.3 is 2.7 mm/h, the rain preset's 0.7 34 mm/h, a downpour 100 mm/h). Drops fall at
 * the environment's RainFallSpeed in three classes (3/4, 1 and 5/4 of it: small, middling and large drops) and drift with
 * the wind at its speed, gusts in, so the rain slants with the wind. Light in rain is lost at about 1.08 R^0.67 decibels a
 * kilometre (the optical links' measurements: 0.248 R^0.67 per kilometre), which the rain adds to the ground haze as a
 * layer from the ground to the clouds' base (WorldRain_GetMistExtinction): a light rain's visibility is about 8 km, the
 * rain preset's 1.5 km, a downpour's 700 m.
 *
 * THE STREAKS. WORLD_RAIN_STREAK_COUNT streaks fill a box WORLD_RAIN_BOX_SIZE metres on a side around the camera, each a
 * one-pixel-wide quad from a drop's place back along its motion over the eye's exposure. A streak has a fixed place in a
 * lattice that repeats every box in the world (its seed), moved by the rain's drift; the camera sees the copy of it in the
 * box around itself: render-space position = mod(seed x box - anchor + box / 2, box) - box / 2. The ANCHOR is the frame's
 * origin less the drift (the wind's sideways, the fall's downward), wrapped in double by the box (sideways and up) and the
 * fall by WORLD_RAIN_FALL_WRAP_BOXES boxes, so every speed class's fall (that times 3/4, 1 or 5/4) wraps by whole boxes:
 * the streaks never jump, and stay exact far from the world's origin (agents/WorldRuntime.md, "Render space"). The drift
 * is the renderer's own state (WorldRainDrift, in double), advanced by each frame's real seconds: drops fall in real time,
 * as the wind's gusts blow.
 *
 * THE RAIN MAP (WorldRain_GetMapShape). A depth map along the rain's mean fall (the wind without its gusts), around the
 * camera, of what keeps the sky out: like a shadow map for the rain. A point behind its first surface, seen from where the
 * rain comes from, is covered: no streak falls there, no splash lands there, and the surface stays dry; beyond its sides,
 * or farther down the rain than its depth reaches, a point is open (WorldRain_GetMapEdgeOpenness). Its box is a
 * shadow level's (WorldRenderSpace_StepCascade, with the direction toward the rain as its light), its centre snapped to
 * its texels in double, so its edges stand still while the camera moves. Toward its edge everything fades to open: beyond
 * it, the rain falls everywhere and every surface is wet. The map is KEPT between its draws (WorldRainMapKeep): drawn at
 * most every WORLD_RAIN_MAP_PERIOD frames, and at once when the camera has moved WORLD_RAIN_MAP_MOVE metres from the box
 * it was drawn with or the rain's mean fall has turned past WORLD_RAIN_MAP_TURN_COSINE; between draws the frames read it
 * through that box (WorldRenderSpace_GetCascadeAt: a point of the world keeps its texel however the camera moves), so
 * only what moves under it lags, by at most WORLD_RAIN_MAP_PERIOD - 1 frames. Seen at a slant, trees' cut-out canopies
 * make it cost several times what it costs straight down.
 *
 * THE SPLASHES. A grid of WORLD_RAIN_SPLASH_GRID x WORLD_RAIN_SPLASH_GRID cells WORLD_RAIN_SPLASH_CELL metres wide around
 * the camera, level, its cells fixed in the world (each one's place in it hashed from its whole coordinates, wrapped in
 * double by WORLD_RAIN_SPLASH_HASH_PERIOD cells). Each cell splashes now and then at a random place in it: where a drop
 * falling through there lands, the first surface the rain map finds on the way down its line along the map's own fall (a
 * bisection, WorldRain_FindLanding), so splashes sit on roofs, awnings, the street and the windward side of walls, never
 * under cover and never in the air of a wall's lee. A splash plays the world's splash sprite animation once, at the
 * animation's fps (its life is its frames over its fps, WorldRain_GetSplashLife; WorldRain_GetSplashFrame picks the frame
 * an age shows), each frame drawn with its own texture and area, so a creator's animation may hold any number of frames
 * of any size from any source. Every cell runs its cycles (WorldRainFrame.SplashPeriod long) by a count the renderer
 * keeps (WorldRainDrift.SplashCycles), as the ripples do theirs: a count advanced by each frame's rate, never a clock
 * times a rate (which would race or run backwards while the rain changes).
 *
 * PUDDLES. A surface facing up holds water where the puddle mask (a tileable texture spread evenly over 0 .. 1, read twice:
 * the second read turned by cosine 4/5 and sine 3/5 and 5/4 as fine, so the mix repeats only every WORLD_RAIN_PATTERN_TILES
 * tiles) passes a threshold: the more wetness, the more of it (none below WORLD_RAIN_PUDDLE_WETNESS of wetness, up to
 * WORLD_RAIN_PUDDLE_MAX_COVERAGE of a surface whose puddles is 1), times the surface's own puddles (asphalt 1, grass 0) and
 * the world's PuddleAmount. The threshold for a coverage is the inverse of the mix's distribution (the mean of two even
 * reads): WorldRain_GetPuddleThreshold. The patterns get the origin wrapped by their period in double, as every
 * world-anchored pattern.
 *
 * THE BOLT. While a lightning flash lasts, its channel stands between the ground and the clouds' base toward the strike,
 * drawn as one of the frames of the world's bolt sprite animation (each frame one bolt, picked from the strike's direction
 * and distance, and mirrored or not; the frame's own shape sets the bolt's width), and it lights every surface from its
 * middle, unshadowed (its light WORLD_RAIN_BOLT_LIGHT_SHARE times the flash's on a level surface, from the side it stands
 * on).
 */


// Macros.
/** @brief The streaks' box around the camera, metres on every side. */
#define WORLD_RAIN_BOX_SIZE 30.0
/** @brief How many streaks the mesh holds (a share of them is drawn: WorldRainFrame.StreakShare). */
#define WORLD_RAIN_STREAK_COUNT 10000U
/** @brief The three speed classes of the drops, in quarters of the fall speed: 3/4, 4/4 and 5/4. */
#define WORLD_RAIN_SPEED_CLASS_COUNT 3U
#define WORLD_RAIN_SLOWEST_QUARTERS 3U
/** @brief Where the fall's drift wraps, in boxes: 4 of them, so every speed class's fall wraps by whole boxes. */
#define WORLD_RAIN_FALL_WRAP_BOXES 4.0
/** @brief How far inside the box's faces the streaks fade out, metres (they wrap there unseen). */
#define WORLD_RAIN_BOX_FADE 3.0f
/** @brief The eye's exposure a streak spans at RainStreakLength 1, seconds: a drop at 7.5 m/s draws 25 cm. */
#define WORLD_RAIN_EXPOSURE (1.0f / 30.0f)
/** @brief A downpour's rain rate, millimetres an hour (precipitation 1). */
#define WORLD_RAIN_DOWNPOUR_RATE 100.0f
/** @brief Light lost in rain, per metre per (millimetre an hour)^WORLD_RAIN_EXTINCTION_POWER: 0.248 per kilometre. */
#define WORLD_RAIN_EXTINCTION_SCALE 0.248e-3f
#define WORLD_RAIN_EXTINCTION_POWER 0.67f
/** @brief The thinnest the rain's mist may be, metres (the clouds' base above the ground haze's base, at least this). */
#define WORLD_RAIN_MIN_MIST_THICKNESS 50.0f
/** @brief The rain map: its side, metres; its texels on a side; how far its eye stands from its centre along the rain, and
 *  its depth from the eye, metres. */
#define WORLD_RAIN_MAP_SIZE 96.0f
#define WORLD_RAIN_MAP_TEXELS 1024
#define WORLD_RAIN_MAP_EYE_DISTANCE 500.0f
#define WORLD_RAIN_MAP_DEPTH 1000.0f
/** @brief The rain map's lookups: their depth bias toward the rain and their move off a surface along its normal, in the
 *  map's texels; the share of its half size where everything starts to fade to open. */
#define WORLD_RAIN_MAP_DEPTH_BIAS_TEXELS 1.0f
#define WORLD_RAIN_MAP_NORMAL_OFFSET_TEXELS 1.5f
#define WORLD_RAIN_MAP_EDGE_FADE_START 0.85f
/** @brief Keeping the rain map: it is drawn at least every this many frames, and at once when the camera has moved this
 *  far from the box it was drawn with (metres) or the rain's mean fall has turned more than the angle whose cosine this is
 *  (one degree). */
#define WORLD_RAIN_MAP_PERIOD 4U
#define WORLD_RAIN_MAP_MOVE 4.0
#define WORLD_RAIN_MAP_TURN_COSINE 0.99984769f
/** @brief The splashes: the grid's cells on a side, a cell's width (metres), where their hash wraps (cells), how high above
 *  and below the camera a landing is sought (metres) and in how many halvings. */
#define WORLD_RAIN_SPLASH_GRID 20
#define WORLD_RAIN_SPLASH_CELL 1.0
#define WORLD_RAIN_SPLASH_HASH_PERIOD 4096.0
#define WORLD_RAIN_SPLASH_REACH 12.0f
#define WORLD_RAIN_SPLASH_STEPS 12
/** @brief The most frames of the splash animation a splash plays (each frame is a draw), and a cell's mean time between
 *  splashes in a downpour, seconds (longer in lighter rain: WorldRainFrame.SplashPeriod). */
#define WORLD_RAIN_SPLASH_MAX_FRAMES 64U
#define WORLD_RAIN_SPLASH_PERIOD 0.6f
/** @brief Where the splashes' and the ripples' cycle counts wrap: a whole number of cycles, the shaders taking a cycle's
 *  number modulo the same, so the wrap changes nothing. */
#define WORLD_RAIN_CYCLE_WRAP 4096.0
/** @brief The puddle mask's tile at PuddleScale 1, metres, and how many tiles its twice-read mix repeats over. */
#define WORLD_RAIN_PUDDLE_TILE 16.0
#define WORLD_RAIN_PATTERN_TILES 4.0
/** @brief The ripples' cells (one ring at a time in each), metres, and where their pattern wraps (a whole number of cells
 *  and of puddle tiles at PuddleScale 1). */
#define WORLD_RAIN_RIPPLE_CELL 0.25
#define WORLD_RAIN_RIPPLE_PERIOD 64.0
/** @brief Puddles: none below this wetness, and the most of a surface they cover (its puddles 1, soaked). */
#define WORLD_RAIN_PUDDLE_WETNESS 0.25f
#define WORLD_RAIN_PUDDLE_MAX_COVERAGE 0.35f
/** @brief How far below the rain's top (the clouds' base) the rain starts to fade out, metres. */
#define WORLD_RAIN_TOP_FADE 30.0f
/** @brief A flash below this draws no bolt. */
#define WORLD_RAIN_BOLT_MIN_FLASH 0.01f
/** @brief The bolt's channel's light on a surface facing it, as a share of the flash's on a level surface. */
#define WORLD_RAIN_BOLT_LIGHT_SHARE 1.0f
/** @brief The bolt's radiance on screen at a flash of 1 and LightningBoltBrightness 1, for an exposure of
 *  WORLD_RAIN_BOLT_SCREEN_EXPOSURE (it is divided by the eye's exposure, as the sun's disc is, so it stays white). */
#define WORLD_RAIN_BOLT_RADIANCE 40.0f
#define WORLD_RAIN_BOLT_SCREEN_EXPOSURE 1.08f


// Types.
/** @brief How far the rain has drifted, metres, and how many cycles its splashes and ripples have run, in double, each
 *  wrapped where its patterns repeat. The renderer's own state: zero it to start, advance it every frame with
 *  WorldRain_AdvanceDrift. */
typedef struct WorldRainDriftStruct
{
    /** @brief The wind's sideways drift (x and z; y unused). */
    Vector3Double Wind;
    /** @brief The fall: how far the middle speed class has fallen. */
    double Fall;
    /** @brief The splashes' cycles (each WorldRainFrame.SplashPeriod long) and the ripples' (WorldRainFrame.RippleRate a
     *  second), wrapped by WORLD_RAIN_CYCLE_WRAP. */
    double SplashCycles;
    double RippleCycles;
} WorldRainDrift;

/** @brief The rain map kept between its draws (WorldRain_IsMapDue, WorldRain_KeepMap): the box it was drawn with. A plain
 *  value type; zeroed, it holds no map. */
typedef struct WorldRainMapKeepStruct
{
    /** @brief Whether a map was drawn that later frames may read. */
    bool HasMap;
    /** @brief Its box's centre in the world, in double (the drawing frame's WorldRenderCascade.Anchor). */
    Vector3Double Center;
    /** @brief The unit direction toward the rain it was drawn along. */
    Vector3 TowardRain;
    /** @brief The frames since it was drawn (0 on the frame that drew it). */
    uint32_t FramesSince;
} WorldRainMapKeep;

/** @brief The lightning's bolt in one frame, in render space (WorldRain_GetBolt). A plain value. */
typedef struct WorldRainBoltStruct
{
    /** @brief Whether it is drawn and lights the world (a flash at least WORLD_RAIN_BOLT_MIN_FLASH, a brightness above 0). */
    bool IsVisible;
    /** @brief Its foot on the ground and its top at the clouds' base, render space (its width is its height times the
     *  shown frame's width over its height). */
    Vector3 Foot;
    Vector3 Top;
    /** @brief Which of the bolt animation's frames it shows (below the frame count given; 0 without frames), and whether
     *  it is mirrored. */
    uint32_t Variant;
    bool IsMirrored;
    /** @brief Its radiance before the eye's exposure: WORLD_RAIN_BOLT_RADIANCE x the flash x the brightness (the pass
     *  divides it by the exposure). */
    float Radiance;
    /** @brief The unit direction from the camera toward its middle, and its light on a surface facing it (linear). */
    Vector3 Direction;
    float Irradiance[3];
} WorldRainBolt;

/** @brief The frame's rain as the passes and the shaders take it. A plain value; WorldRain_GetFrame fills it. */
typedef struct WorldRainFrameStruct
{
    /** @brief The weather's precipitation (0 .. 1) and its rain rate, millimetres an hour. */
    float Precipitation;
    float RainRate;
    /** @brief Whether streaks fall (a share of them above 0, and their box around the camera reaching below the rain's top)
     *  and whether splashes land (theirs above 0, and their search reaching below the top). */
    bool AreStreaksVisible;
    bool AreSplashesVisible;
    /** @brief The share of the streaks drawn (precipitation x RainStreakDensity, at most 1) and of the splash cells that
     *  splash in a cycle (precipitation x RainSplashDensity, at most 1). */
    float StreakShare;
    float SplashShare;
    /** @brief A cell's time between splashes, seconds. */
    float SplashPeriod;
    /** @brief The middle speed class's fall, metres per second (RainFallSpeed). */
    float FallSpeed;
    /** @brief The wind now, gust in (level, metres per second): the drops drift with it; and the mean wind without the
     *  gust, which the rain map looks along. */
    Vector3 Wind;
    Vector3 MeanWind;
    /** @brief The unit direction toward where the rain comes from: up and upwind against the mean fall (the rain map's
     *  light). */
    Vector3 TowardRain;
    /** @brief The exposure a streak spans, seconds (WORLD_RAIN_EXPOSURE x RainStreakLength), its brightness multiplier
     *  and the rain's colour (linear). */
    float StreakExposure;
    float StreakBrightness;
    float Color[3];
    /** @brief The rain's top: the clouds' base, world Y (the atmosphere's ground plus CloudBase); nothing of the rain shows
     *  above it (WorldRain_GetTopOpenness). */
    double TopY;
    /** @brief The weather's wetness (0 .. 1), and whether the rain map is drawn: it rains or surfaces are wet, and its box
     *  reaches below the rain's top. */
    float Wetness;
    bool IsMapNeeded;
    /** @brief The puddles: the most of a puddles-1 surface under water now (the wetness's and PuddleAmount's), and the
     *  puddle mask's tile, metres (WORLD_RAIN_PUDDLE_TILE x PuddleScale). */
    float PuddleCoverage;
    float PuddleTile;
    /** @brief How strongly the rain rings the puddles (the precipitation), and how many rings a ripple cell starts a
     *  second. */
    float RippleStrength;
    float RippleRate;
} WorldRainFrame;


// Functions.
/**
 * @brief The rain rate of a precipitation: WORLD_RAIN_DOWNPOUR_RATE x p^3 millimetres an hour.
 * @param precipitation The weather's precipitation; clamped to [0, 1], a non-finite one counts as 0.
 * @returns The rain rate (>= 0).
 */
float WorldRain_GetRainRate(float precipitation);

/**
 * @brief The light rain takes per metre at a precipitation (the optical links' 0.248 R^0.67 per kilometre).
 * @param precipitation The weather's precipitation; clamped to [0, 1], a non-finite one counts as 0.
 * @returns The extinction, per metre (0 without rain; a downpour's about 0.0054: 720 m of visibility).
 */
float WorldRain_GetMistExtinction(float precipitation);

/**
 * @brief How quickly the rain's mist thins with height: from the ground haze's base to the clouds' base, at least
 *        WORLD_RAIN_MIN_MIST_THICKNESS.
 * @param cloudBaseY The clouds' base, world Y (the atmosphere's ground plus CloudBase).
 * @param mistBaseY The ground haze's base, world Y.
 * @returns The thickness, metres; WORLD_RAIN_MIN_MIST_THICKNESS for anything not finite.
 */
float WorldRain_GetMistThickness(float cloudBaseY, float mistBaseY);

/**
 * @brief Fills the frame's rain from the environment (the weather's precipitation, wetness and wind; the rain settings;
 *        the clouds' base as the rain's top) and the camera's height (the streaks, the splashes and the rain map only
 *        while what they reach lies below the top). Non-finite values count as none; settings are clamped to their rules.
 * @param environment The environment; must not be NULL.
 * @param originY The frame's origin's Y (the camera's height), world; a non-finite one counts as below the top.
 * @param outFrame [out] The frame; must not be NULL.
 */
void WorldRain_GetFrame(const WorldEnvironment* environment, double originY, WorldRainFrame* outFrame);

/**
 * @brief How much of the rain shows at a height, by the rain's top: 1 up to WORLD_RAIN_TOP_FADE metres below it, falling
 *        smoothly to 0 at it and above (the C mirror of common/rain's RainTopOpenness). Both heights in one frame of
 *        reference (render space: the top less the camera's height).
 * @param height The point's height.
 * @param top The rain's top.
 * @returns The share, in [0, 1]; 1 for anything not finite.
 */
float WorldRain_GetTopOpenness(float height, float top);

/**
 * @brief A splash's life: its animation's frames (at most WORLD_RAIN_SPLASH_MAX_FRAMES) over its fps.
 * @param frameCount The splash animation's frames.
 * @param fps The animation's fps.
 * @returns The life, seconds; 0 (no splashes) without frames or with an fps not above 0.
 */
float WorldRain_GetSplashLife(size_t frameCount, double fps);

/**
 * @brief The frame of the splash animation a splash shows at an age (the C mirror of shaders/rain/splashes_vertex.vert's
 *        pick): the frames spread evenly over its life.
 * @param age The splash's age, seconds (below its life while it shows).
 * @param life Its life (WorldRain_GetSplashLife).
 * @param frameCount The frames it plays (at most WORLD_RAIN_SPLASH_MAX_FRAMES).
 * @returns The frame, below @p frameCount; 0 without frames, life or a finite age.
 */
uint32_t WorldRain_GetSplashFrame(float age, float life, uint32_t frameCount);

/**
 * @brief Advances the rain's drift by real seconds: the wind's sideways drift by the wind now, the fall by the fall speed,
 *        the splashes' cycles by the seconds over the frame's SplashPeriod and the ripples' by its RippleRate. A
 *        non-finite or negative @p seconds holds it.
 * @param drift The drift; must not be NULL.
 * @param frame The frame's rain; must not be NULL.
 * @param seconds The frame's real seconds.
 */
void WorldRain_AdvanceDrift(WorldRainDrift* drift, const WorldRainFrame* frame, double seconds);

/**
 * @brief The streaks' anchor for a frame's origin: x and z the origin less the wind's drift and y the origin, each wrapped
 *        in double by the box into [0, WORLD_RAIN_BOX_SIZE); and the fall wrapped by WORLD_RAIN_FALL_WRAP_BOXES boxes.
 * @param drift The drift; must not be NULL.
 * @param origin The frame's origin.
 * @param outAnchor [out] The anchor (x, y, z); must not be NULL.
 * @param outFall [out] The wrapped fall; must not be NULL.
 */
void WorldRain_GetStreakAnchor(const WorldRainDrift* drift, Vector3Double origin, float outAnchor[3], float* outFall);

/**
 * @brief A streak's place in render space: its seed's copy in the box around the camera (the C mirror of
 *        shaders/rain/streaks_vertex.vert's RainStreakPosition).
 * @param seed The streak's seed, each in [0, 1).
 * @param speedClass Its speed class, below WORLD_RAIN_SPEED_CLASS_COUNT.
 * @param anchor The anchor (WorldRain_GetStreakAnchor).
 * @param fall The wrapped fall (WorldRain_GetStreakAnchor).
 * @returns Its place, each coordinate in [-WORLD_RAIN_BOX_SIZE / 2, WORLD_RAIN_BOX_SIZE / 2].
 */
Vector3 WorldRain_GetStreakPosition(Vector3 seed, uint32_t speedClass, const float anchor[3], float fall);

/**
 * @brief A speed class's share of the fall speed: (3 + class) / 4.
 * @param speedClass The class (clamped to the last).
 * @returns 0.75, 1 or 1.25.
 */
float WorldRain_GetSpeedFactor(uint32_t speedClass);

/**
 * @brief How much a streak shows by where it is in the box: 1 inside, fading to 0 over WORLD_RAIN_BOX_FADE metres to the
 *        box's faces (where it wraps).
 * @param position The streak's place (WorldRain_GetStreakPosition).
 * @returns The fade, in [0, 1].
 */
float WorldRain_GetBoxFade(Vector3 position);

/**
 * @brief The rain map's box: WORLD_RAIN_MAP_SIZE metres wide, WORLD_RAIN_MAP_TEXELS texels, its eye
 *        WORLD_RAIN_MAP_EYE_DISTANCE metres up the rain from its centre, its depth WORLD_RAIN_MAP_DEPTH.
 * @returns The shape, for WorldRenderSpace_StepCascade.
 */
WorldRenderCascadeShape WorldRain_GetMapShape(void);

/**
 * @brief Whether this frame draws the rain map again, counting the frame: when no map is kept, every
 *        WORLD_RAIN_MAP_PERIOD frames, when the camera stands WORLD_RAIN_MAP_MOVE metres or more from the kept box's centre,
 *        or when the rain's mean fall has turned past WORLD_RAIN_MAP_TURN_COSINE (a non-finite place or direction counts
 *        as moved). Otherwise the frame reads the kept map through its box (WorldRenderSpace_GetCascadeAt with the
 *        keep's Center and TowardRain).
 * @param keep The kept map; must not be NULL. Its FramesSince counts this frame.
 * @param origin The frame's origin (the camera's world position).
 * @param towardRain The frame's unit direction toward the rain (WorldRainFrame.TowardRain).
 * @returns true when the frame draws the map (then call WorldRain_KeepMap with the box it draws).
 */
bool WorldRain_IsMapDue(WorldRainMapKeep* keep, Vector3Double origin, Vector3 towardRain);

/**
 * @brief Keeps the box a frame draws the rain map with, for the frames after it.
 * @param keep The kept map; must not be NULL.
 * @param center The box's centre in the world (its WorldRenderCascade.Anchor).
 * @param towardRain The unit direction toward the rain it is drawn along.
 */
void WorldRain_KeepMap(WorldRainMapKeep* keep, Vector3Double center, Vector3 towardRain);

/**
 * @brief The rain map's lookups' offsets: the depth bias (in the map's depth, 0 .. 1 over WORLD_RAIN_MAP_DEPTH) and the
 *        move off a surface along its normal (metres).
 * @param outDepthBias [out] The depth bias; must not be NULL.
 * @param outNormalOffset [out] The normal offset; must not be NULL.
 */
void WorldRain_GetMapOffsets(float* outDepthBias, float* outNormalOffset);

/**
 * @brief How open a point of the rain map is by its place on it alone: 0 inside WORLD_RAIN_MAP_EDGE_FADE_START of its half
 *        size, rising to 1 at its sides and beyond; 1 at its far plane and beyond, farther down the rain than its depth
 *        reaches (the map's float depth does not clamp the comparison: without this everything there would read as
 *        covered); 1 for anything not finite (the C mirror of common/rain's RainOpennessOfClip). Its map coordinates are
 *        the point through the map's view-projection, in clip units (-1 .. 1 across and deep).
 * @param clipX The point's clip x.
 * @param clipY The point's clip y.
 * @param clipZ The point's clip z (1 at the far plane).
 * @returns The share of the open that its place gives, in [0, 1].
 */
float WorldRain_GetMapEdgeOpenness(float clipX, float clipY, float clipZ);

/**
 * @brief The horizontal step per metre up along a fall: a drop that falls along it passes over a point this far upwind
 *        for each metre above it (the splashes' line, along the rain map's own fall).
 * @param towardRain The unit direction toward where the rain comes from (its y above 0).
 * @returns The step (x, z); (0, 0) for a direction not finite or not up.
 */
Vector2 WorldRain_GetSlant(Vector3 towardRain);

/** @brief Whether a point is open to the rain (a caller's lookup: the map, or a test's field). @p context is the caller's. */
typedef bool (*WorldRainOpenFunction)(const void* context, Vector3 point);

/**
 * @brief Where a drop falling down a line along the rain lands: the bisection the splashes run (the C mirror of
 *        shaders/rain/splashes_vertex.vert's RainLanding). The line passes through render-space (x, 0, z) along the rain
 *        map's own fall: its point at height h is (x + slant.x h, h, z + slant.y h). Along the map's axis the change from
 *        open to covered is the first surface itself; along a vertical line in a slanting rain it would be the edge of a
 *        wall's rain shadow, in the air. Searched between the heights @p top (which must be open) and @p bottom (which
 *        must be covered), WORLD_RAIN_SPLASH_STEPS halvings.
 * @param isOpen The lookup; must not be NULL.
 * @param context The lookup's context.
 * @param x The line's x at height 0, render space.
 * @param z The line's z at height 0, render space.
 * @param slant The line's step per metre up (WorldRain_GetSlant of the direction the map was drawn along).
 * @param top The top of the search, render-space Y.
 * @param bottom The bottom of the search, render-space Y.
 * @param outLanding [out] The landing (the line's point at the middle of the last interval); must not be NULL.
 * @returns false when there is nothing to land on (the top covered, or the bottom open).
 */
bool WorldRain_FindLanding(WorldRainOpenFunction isOpen, const void* context, float x, float z, Vector2 slant, float top,
    float bottom, Vector3* outLanding);

/**
 * @brief The splash grid for a frame's origin: the hash coordinates (whole, wrapped by WORLD_RAIN_SPLASH_HASH_PERIOD) of the
 *        cell the grid starts at (WORLD_RAIN_SPLASH_GRID / 2 cells below the camera's cell on x and z), and that cell's
 *        corner in render space (x and z).
 * @param origin The frame's origin.
 * @param outCell [out] The first cell's hash coordinates (x, z), each in [0, WORLD_RAIN_SPLASH_HASH_PERIOD); must not be
 *        NULL.
 * @param outCorner [out] Its corner, render space (x, z); must not be NULL.
 */
void WorldRain_GetSplashGrid(Vector3Double origin, float outCell[2], float outCorner[2]);

/**
 * @brief The rain's hash of three whole numbers into 0 .. 2^32 - 1 (the C mirror of common/rain's rainHash: the splashes'
 *        places and phases, the ripples').
 * @param x The first.
 * @param y The second.
 * @param z The third.
 * @returns The hash.
 */
uint32_t WorldRain_Hash(uint32_t x, uint32_t y, uint32_t z);

/**
 * @brief An origin wrapped by a pattern's period, x and z (the puddle mask's, the ripples'), in double.
 * @param origin The frame's origin.
 * @param period The period, metres (above 0).
 * @param outWrapped [out] fmod of each, in [0, period); must not be NULL.
 */
void WorldRain_GetPatternOrigin(Vector3Double origin, double period, float outWrapped[2]);

/**
 * @brief The most of a surface with puddles 1 under water: none below WORLD_RAIN_PUDDLE_WETNESS of wetness, rising to
 *        WORLD_RAIN_PUDDLE_MAX_COVERAGE soaked, times the world's PuddleAmount (at most 1).
 * @param wetness The wetness, 0 .. 1.
 * @param amount The world's PuddleAmount (>= 0).
 * @returns The coverage, in [0, 1].
 */
float WorldRain_GetPuddleCoverage(float wetness, float amount);

/**
 * @brief The puddle mask's threshold for a coverage: the mix of its two reads (their mean) passes it on that share of a
 *        surface, for evenly spread reads (the inverse of the mean's triangular distribution; the C mirror of common/puddles'
 *        RainPuddleThreshold).
 * @param coverage The coverage, 0 .. 1 (0: 1, nothing; 1: 0, everything).
 * @returns The threshold, in [0, 1].
 */
float WorldRain_GetPuddleThreshold(float coverage);

/**
 * @brief The lightning's bolt for a frame: while the flash lasts, its channel toward the strike from the ground (the
 *        atmosphere's ground, HorizonGroundHeight) to the clouds' base, in render space, the frame of the bolt animation
 *        it shows, its radiance and its light.
 * @param environment The environment (the lightning's values, the clouds' base, the ground, the bolt's brightness); must
 *        not be NULL.
 * @param flashIrradiance The flash's light on a level surface (WorldWeatherLight.FlashIrradiance).
 * @param originY The frame's origin's Y (the camera's height), world.
 * @param frameCount The bolt animation's frames (each one bolt); 0 without an animation (the bolt still lights).
 * @param outBolt [out] The bolt; must not be NULL.
 */
void WorldRain_GetBolt(const WorldEnvironment* environment, const float flashIrradiance[3], double originY,
    uint32_t frameCount, WorldRainBolt* outBolt);
