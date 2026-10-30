#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WorldRenderSpace.h"
#include "raylib/raylib.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): the sun's and the moon's shadow levels as pure maths, tested headless
 * (tests/WorldShadowLevelsTests.c). The shadow passes render them (WorldRendererShadows.c) and common/lighting reads
 * them; WorldShadowLevels_GetLookup is the C mirror of that read's choice of level.
 *
 * THE LEVELS. Nested square boxes around the camera in the shadow light's view (a clipmap): level k is
 * WORLD_SHADOW_BASE_SIZE x Factor^k metres wide (32, 128, 512, 2048 and 8192 m with the defaults), its map MapSize
 * texels a side, so a texel is Size / MapSize wide (1.6 cm, 6.3 cm, 25 cm, 1 m and 4 m at 2048). Each is centred on the
 * camera and snapped to its own texel grid (WorldRenderSpace_StepCascade): turning the camera moves no level, and a
 * level's map stays right while the camera stands still. At a 640 x 360 pixel-art picture a scene pixel at distance d is
 * about 0.0032 d wide, so at each level's inner edge (where the level before it ends) a texel is about a scene pixel:
 * the shadow's quality falls with distance no faster than the picture's. The levels are boxes centred on the camera, not
 * slices of its view: a turn of the camera renders nothing again, and the half of a level behind the camera still holds
 * the casters that shade what it sees.
 *
 * THE REACH. Shadows end at the world's reach (its environment's shadow_reach), fading out over the reach's last
 * tenth (at least WORLD_SHADOW_MIN_REACH_FADE metres): the levels used are the first ones up to the one that holds the
 * reach (WorldShadowLevels_GetUsedCount), at most the player's level count. Every level's depth runs the reach both ways
 * from the camera along the light, so whatever lies within the reach is inside every level along the light; casters
 * nearer the light than the box (a tall tower toward a low sun) are drawn flattened onto its near plane (depth
 * clamping), so the box need not reach them.
 *
 * WHICH LEVEL A POINT READS (WorldShadowLevels_GetLookup). The smallest that holds it: the larger of the point's
 * distances from the camera along the light's right and up axes against the level's half size, less a margin of
 * WORLD_SHADOW_MARGIN_TEXELS of its texels for the filter, the lookup's offset off its surface and the centre's
 * snapping. Over the outer WORLD_SHADOW_BLEND_SHARE of that half size the shadow blends into the next level's (two reads
 * only there); the last level fades to no shadow over its band, so neither a level's edge nor the end of the shadows is a
 * line.
 *
 * WHAT A LEVEL DRAWS. A caster whose bounding sphere is narrower than WORLD_SHADOW_SIZE_CUT_TEXELS of a level's texels
 * stays out of it: its shadow could not show there (a cup casts into the first level only, a tree up to the 1 m level, a
 * house and the terrain into every one). A level past the first draws a caster at the LOD it would show at the level's
 * inner edge (WorldShadowLevels_GetLodDistance), unless it shows a coarser one already or casts with a LOD of its own.
 * And a level past the first draws only its ring: the light's view is orthographic, so a caster shades only what lies
 * under it there, and a caster deep enough inside the level before's box shades only points that read the level before
 * alone. It stays out when its sphere lies inside that box's sides by WorldShadowLevels_GetInnerInset: the band
 * (where both are read), the camera's movement and the light's turn while the level keeps its map, both levels'
 * snapping, and the texels a lookup in the level may read around its point.
 *
 * WHEN A LEVEL RENDERS (WorldShadowLevels_BeginFrame). The far levels render less often, staggered so that no two of
 * them render in the same frame while their periods are powers of two: a level of period P renders when the frame's
 * count is P / 2 - 1 past a multiple of P (the defaults: level 2 every 2nd frame, level 3 every 4th, level 4 every 8th).
 * A level also renders at once: on its first frame and after it was not used, when the settings its map was rendered for
 * change (the map's size, the levels' sizes, the reach), when the light has turned since by WORLD_SHADOW_TURN_TEXELS of
 * its texels at its edge (the angle is the same for every level: the texel grows with the box) or across the up vector's
 * switch (WorldRenderSpace_HasSameCascadeUp: every box turns about the light at once there), and when the camera has
 * moved by WORLD_SHADOW_MOVE_SHARE of its half size (the box would no longer hold what the level is read for). A level
 * that does not render keeps its map and the light frame it was rendered with (its centre in the world, in double, and
 * the light's direction): its matrices are built again from them relative to each frame's origin
 * (WorldRenderSpace_GetCascadeAt), so its lookups read its map as it was rendered. A moving caster's shadow in a level
 * that renders every P frames lags by up to P - 1 frames of its motion.
 */


// Macros.
/** @brief The most levels there may be. */
#define WORLD_SHADOW_MAX_LEVELS 8U
/** @brief The first level's width, in metres. */
#define WORLD_SHADOW_BASE_SIZE 32.0f
/** @brief The outer share of a level's usable half size over which its shadow blends into the next level's. */
#define WORLD_SHADOW_BLEND_SHARE 0.15f
/** @brief The texels at a level's edge no lookup reads: the filter's reach (one and a half texels for a grid of bilinear
 *  taps), the lookup's move off its surface (one and a half at grazing incidence) and the centre's snapping (half a
 *  texel), with half a texel to spare. */
#define WORLD_SHADOW_MARGIN_TEXELS 4.0f
/** @brief A caster narrower than this many texels of a level (its bounding sphere's diameter) stays out of it. */
#define WORLD_SHADOW_SIZE_CUT_TEXELS 2.0f
/** @brief The texels of a level around a lookup's point that its read may touch: the move off the surface (one and a half
 *  at grazing incidence) and the bilinear tap (one), with one and a half to spare. */
#define WORLD_SHADOW_INNER_READ_TEXELS 4.0f
/** @brief A level renders again once the light has turned this many of its texels at its edge since its last render. */
#define WORLD_SHADOW_TURN_TEXELS 0.5f
/** @brief A level renders again once the camera has moved this share of its half size since its last render. */
#define WORLD_SHADOW_MOVE_SHARE 0.02f
/** @brief The reach's fade: its last WORLD_SHADOW_REACH_FADE_SHARE, but at least WORLD_SHADOW_MIN_REACH_FADE metres (and at
 *  most the whole reach). */
#define WORLD_SHADOW_REACH_FADE_SHARE 0.1f
#define WORLD_SHADOW_MIN_REACH_FADE 3.0f
/** @brief The levels' depth runs the reach times this both ways from the camera along the light (a little more than the
 *  reach, so the farthest point the shadows reach is not on the box's face). */
#define WORLD_SHADOW_DEPTH_REACH_SCALE 1.05f
/** @brief The shortest depth half range, in metres, whatever the reach. */
#define WORLD_SHADOW_MIN_DEPTH_HALF_RANGE 16.0f
/** @brief How far the light's eye stands from a level's box centre toward the light, in metres: close, so the float view
 *  matrix holds no large translation (the box's near plane lies behind the eye, toward the light). */
#define WORLD_SHADOW_EYE_DISTANCE 1.0f
/** @brief The default settings (WorldShadowLevelsSettings_CreateDefault; the player's config and the world's reach
 *  default to the same): five levels, each four times as wide as the one before, 2048 texels, a 4 km reach. */
#define WORLD_SHADOW_DEFAULT_LEVEL_COUNT 5U
#define WORLD_SHADOW_DEFAULT_FACTOR 4.0f
#define WORLD_SHADOW_DEFAULT_MAP_SIZE 2048
#define WORLD_SHADOW_DEFAULT_REACH 4000.0f


// Types.
/**
 * @brief What the levels are made for: the player's quality settings and the world's reach. A plain value type the caller
 *        fills completely (WorldShadowLevelsSettings_Sanitize brings any values into range).
 */
typedef struct WorldShadowLevelsSettingsStruct
{
    /** @brief The most levels the player allows, 1 to WORLD_SHADOW_MAX_LEVELS. */
    uint32_t LevelCount;
    /** @brief Each level's width over the level before it, above 1 (4 by default). */
    float Factor;
    /** @brief Every level's map's texels along each side, above 0. */
    int MapSize;
    /** @brief How far the shadows reach from the camera, in metres (0: no shadow at all). */
    float Reach;
    /** @brief Each level renders every UpdatePeriods[k] frames, 1 every frame. */
    uint32_t UpdatePeriods[WORLD_SHADOW_MAX_LEVELS];
} WorldShadowLevelsSettings;

/**
 * @brief Where a point's shadow comes from (WorldShadowLevels_GetLookup): one level, or two over the band where the first
 *        blends into the next, times the reach's fade. A plain value type.
 */
typedef struct WorldShadowLookupStruct
{
    /** @brief Whether any level holds the point within the reach (false: no shadow there). */
    bool IsInside;
    /** @brief The level read, below the used count (valid while IsInside). */
    uint32_t Level;
    /** @brief Whether the point lies in Level's band with a next level to blend into (false at the last level, whose band
     *  fades to no shadow, and outside every band). */
    bool HasNext;
    /** @brief The share of the point's shadow the next level gives (or, at the last level, the share that fades to none),
     *  in [0, 1]: 0 outside the band, rising across it. */
    float NextWeight;
    /** @brief The share of the shadow the reach leaves, in [0, 1]: 1 within it, falling to 0 over its fade. */
    float ReachFade;
} WorldShadowLookup;

/** @brief One level's state from frame to frame: the light frame its map was rendered with. Zeroed, it has no map. */
typedef struct WorldShadowLevelStateStruct
{
    /** @brief Its texel grid: its centre in the world from its last render (WorldRenderSpace_StepCascade). */
    WorldRenderCascadeGrid Grid;
    /** @brief The unit direction toward the light its map was rendered from. */
    Vector3 TowardLight;
    /** @brief The frame's origin (the camera's position) when its map was rendered. */
    Vector3Double RenderOrigin;
    /** @brief Whether its map holds a render for the levels' current settings. */
    bool HasMap;
} WorldShadowLevelState;

/**
 * @brief The levels' states from frame to frame, for one camera (the renderer keeps one). Zero it (or call
 *        WorldShadowLevels_Construct) before its first frame; it owns no memory.
 */
typedef struct WorldShadowLevelsStruct
{
    /** @brief Each level's state. */
    WorldShadowLevelState _levels[WORLD_SHADOW_MAX_LEVELS];
    /** @brief The frames counted so far (the update schedule's clock). */
    uint64_t _frame;
    /** @brief The settings the maps were rendered for (valid while _hasSettings): a change renders every level again. */
    WorldShadowLevelsSettings _settings;
    bool _hasSettings;
} WorldShadowLevels;

/** @brief One level in a frame (WorldShadowLevels_BeginFrame). A plain value type. */
typedef struct WorldShadowLevelFrameStruct
{
    /** @brief Whether the level is read this frame (below the used count). */
    bool IsUsed;
    /** @brief Whether its map is rendered this frame (else it is read as it was rendered, with Cascade's matrices). */
    bool IsRendered;
    /** @brief Its width and its texel's, in metres. */
    float Size;
    float Texel;
    /** @brief Its shape: the box's size and depth range (the light's eye at the near plane). */
    WorldRenderCascadeShape Shape;
    /** @brief Its matrices relative to this frame's origin: the ones its map is rendered with this frame, or was rendered
     *  with (valid while IsUsed). */
    WorldRenderCascade Cascade;
    /** @brief The unit direction toward the light its map holds. */
    Vector3 TowardLight;
} WorldShadowLevelFrame;


// Functions.
/**
 * @brief The levels' default settings: the WORLD_SHADOW_DEFAULT_* values and the periods 1, 1, 2, 4, 8, 16, 32 and 64.
 * @returns The settings.
 */
WorldShadowLevelsSettings WorldShadowLevelsSettings_CreateDefault(void);

/**
 * @brief Brings settings into range: the level count into 1 to WORLD_SHADOW_MAX_LEVELS, a factor that is not finite or
 *        not above 1 to 4, a map size not above 0 to 2048, a reach that is not finite or below 0 to 0 (no shadows), a
 *        period of 0 to 1.
 * @param settings The settings; must not be NULL.
 * @returns The settings in range.
 */
WorldShadowLevelsSettings WorldShadowLevelsSettings_Sanitize(const WorldShadowLevelsSettings* settings);

/**
 * @brief A level's width: WORLD_SHADOW_BASE_SIZE x Factor^level, in metres.
 * @param settings The settings (in range); must not be NULL.
 * @param level The level.
 * @returns The width (+infinity where it overflows).
 */
float WorldShadowLevels_GetSize(const WorldShadowLevelsSettings* settings, uint32_t level);

/**
 * @brief A level's texel: its width over its map's size, in metres.
 * @param settings The settings (in range); must not be NULL.
 * @param level The level.
 * @returns The texel's width.
 */
float WorldShadowLevels_GetTexel(const WorldShadowLevelsSettings* settings, uint32_t level);

/**
 * @brief The levels used: the first ones up to the one whose half size holds the reach, at most the settings' level
 *        count (the reach then ends at the last level's band).
 * @param settings The settings (in range); must not be NULL.
 * @returns 1 to the settings' level count; 0 while the reach is 0 (no shadows).
 */
uint32_t WorldShadowLevels_GetUsedCount(const WorldShadowLevelsSettings* settings);

/**
 * @brief Where the reach's fade starts: the reach less its last WORLD_SHADOW_REACH_FADE_SHARE, at least
 *        WORLD_SHADOW_MIN_REACH_FADE metres (and at most the whole reach) before it. The shadows fade linearly from there to
 *        none at the reach.
 * @param settings The settings (in range); must not be NULL.
 * @returns The distance, in metres, at least 0.
 */
float WorldShadowLevels_GetReachFadeStart(const WorldShadowLevelsSettings* settings);

/**
 * @brief The levels' depth half range: how far their boxes run both ways from the camera along the light (the reach times
 *        WORLD_SHADOW_DEPTH_REACH_SCALE, at least WORLD_SHADOW_MIN_DEPTH_HALF_RANGE).
 * @param settings The settings (in range); must not be NULL.
 * @returns The half range, in metres.
 */
float WorldShadowLevels_GetDepthHalfRange(const WorldShadowLevelsSettings* settings);

/**
 * @brief A level's shape for WorldRenderSpace: its width, the light's eye WORLD_SHADOW_EYE_DISTANCE toward the light from
 *        the box's centre, its near and far planes the depth half range before and behind the centre (the near plane
 *        behind the eye), its map's size.
 * @param settings The settings (in range); must not be NULL.
 * @param level The level.
 * @returns The shape.
 */
WorldRenderCascadeShape WorldShadowLevels_GetShape(const WorldShadowLevelsSettings* settings, uint32_t level);

/**
 * @brief The share of a level's half size its lookups may read: 1 less WORLD_SHADOW_MARGIN_TEXELS texels, as a share
 *        (the same for every level: the margin grows with the texel). In light clip coordinates (a level's half size is
 *        1 there), a point is in the level while the larger of its |x| and |y| is at most this.
 * @param settings The settings (in range); must not be NULL.
 * @returns The share, in (0, 1).
 */
float WorldShadowLevels_GetEdge(const WorldShadowLevelsSettings* settings);

/**
 * @brief Where a point's shadow comes from: the smallest used level that holds it, its band's blend into the next
 *        level, and the reach's fade. The C mirror of common/lighting's choice (which measures the point in each level's
 *        own light clip coordinates: a kept level's box may lie off the camera by its move limit and turn by its turn
 *        limit).
 * @param settings The settings (in range); must not be NULL.
 * @param usedCount The levels used (WorldShadowLevels_GetUsedCount).
 * @param boxDistance The larger of the point's distances from the camera along the light's right and up axes, in metres.
 * @param distance The point's distance from the camera, in metres (the reach's fade).
 * @returns The lookup; IsInside false outside every used level, beyond the reach, or with no level used.
 */
WorldShadowLookup WorldShadowLevels_GetLookup(const WorldShadowLevelsSettings* settings, uint32_t usedCount,
    float boxDistance, float distance);

/**
 * @brief The smallest bounding sphere's radius a caster of a level may have: half WORLD_SHADOW_SIZE_CUT_TEXELS of its
 *        texels.
 * @param settings The settings (in range); must not be NULL.
 * @param level The level.
 * @returns The radius, in metres.
 */
float WorldShadowLevels_GetMinRadius(const WorldShadowLevelsSettings* settings, uint32_t level);

/**
 * @brief Whether a caster casts into a level by its size: its bounding sphere's diameter is at least
 *        WORLD_SHADOW_SIZE_CUT_TEXELS of the level's texels.
 * @param settings The settings (in range); must not be NULL.
 * @param level The level.
 * @param radius The caster's bounding sphere's radius, in metres (a NaN never casts).
 * @returns true when its shadow can show in the level.
 */
bool WorldShadowLevels_IsLargeEnough(const WorldShadowLevelsSettings* settings, uint32_t level, float radius);

/**
 * @brief The distance at which a level picks its casters' LODs: where it starts being read, the level before it's
 *        usable half size less its band (0 for the first level: it draws what the camera shows).
 * @param settings The settings (in range); must not be NULL.
 * @param level The level.
 * @returns The distance, in metres (before the zoom factor).
 */
float WorldShadowLevels_GetLodDistance(const WorldShadowLevelsSettings* settings, uint32_t level);

/**
 * @brief How far inside the level before's box sides a caster's sphere must lie for a level to leave it out (no lookup
 *        that reads the level could see its shadow there, whatever the light and the camera do before the level renders
 *        again): from the box's side to the band's start, the camera's movement while either level keeps its map
 *        (WORLD_SHADOW_MOVE_SHARE of the level's half size, and twice of the level before's), the two levels' snapping
 *        (two texels of the level before), WORLD_SHADOW_INNER_READ_TEXELS of the level's texels, and the light's turns
 *        while the level keeps its map: the level before's light moves up to three turn limits from its light when the
 *        level was drawn, carrying a left-out caster's place in its box over the caster's depth and, through the box's
 *        axes (WORLD_RENDER_SPACE_CASCADE_STRAIGHT_UP_Y), over its place in the box; the level's own light lies up to two
 *        limits from that, over the depth from the caster to what it shades (twice the reach). 0 for the first level: it
 *        draws everything.
 * @param settings The settings (in range); must not be NULL.
 * @param level The level.
 * @returns The inset, in metres.
 */
float WorldShadowLevels_GetInnerInset(const WorldShadowLevelsSettings* settings, uint32_t level);

/**
 * @brief The frame a level of the given update period renders at: its phase, P / 2 - 1 for a period P of 2 or more (0
 *        for 1), so periods that are powers of two never meet.
 * @param period The period, in frames (0 counts as 1).
 * @returns The phase, below the period.
 */
uint32_t WorldShadowLevels_GetPhase(uint32_t period);

/**
 * @brief Initializes the levels' states: no level has a map; the first frame renders every used level.
 * @param self The states; must not be NULL.
 */
void WorldShadowLevels_Construct(WorldShadowLevels* self);

/**
 * @brief Releases the states (they own nothing).
 * @param self The states, or NULL.
 */
void WorldShadowLevels_Deconstruct(WorldShadowLevels* self);

/**
 * @brief Forgets every level's map, so each renders at the next frame that uses it (after its map texture was made
 *        again, for one).
 * @param self The states; must not be NULL.
 */
void WorldShadowLevels_Invalidate(WorldShadowLevels* self);

/**
 * @brief Decides this frame's levels: which are used, which render (the schedule and the reasons to render at once), and
 *        each one's matrices relative to the frame's origin, and moves the schedule's clock on.
 *
 * A level that renders steps its texel grid toward the camera (WorldRenderSpace_StepCascade) and keeps the light, the
 * origin and the settings it is rendered with; one that does not keeps its last render's (WorldRenderSpace_GetCascadeAt).
 * The levels past the used count are not used, and forget their maps. Call once a frame while the shadows run; after
 * frames without them, or for another world, call WorldShadowLevels_Invalidate first (the kept maps hold what was there
 * then). Constant time; no allocation.
 * @param self The states; must not be NULL. Updated.
 * @param settings This frame's settings (in range); must not be NULL.
 * @param origin The frame's origin (the camera's world position).
 * @param towardLight The unit direction toward the shadow light.
 * @param outLevels [out] Receives each level's frame, WORLD_SHADOW_MAX_LEVELS of them. Must not be NULL.
 * @returns The levels used this frame (WorldShadowLevels_GetUsedCount).
 */
uint32_t WorldShadowLevels_BeginFrame(WorldShadowLevels* self, const WorldShadowLevelsSettings* settings,
    Vector3Double origin, Vector3 towardLight, WorldShadowLevelFrame outLevels[WORLD_SHADOW_MAX_LEVELS]);
