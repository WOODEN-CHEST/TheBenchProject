#pragma once
#include <stdbool.h>

/*
 * Private header (source/): the eye's adaptation to the picture (T37, decision G42). Pure maths and the eye's state, no
 * GL: the renderer meters each frame's brightness on the GPU (shaders/luminance/fragment.frag, read back by GLReadback.h
 * a frame or two later) and this turns the measurements into the exposure the tonemap multiplies the scene by.
 *
 * THE ANCHOR. The sky's light at the camera (WorldSkyLight_GetSceneLuminance: a white surface's luminance under it) sets
 * the level a view in the open has (EyeAdaptation_GetAnchor, EYE_ADAPTATION_ANCHOR_SHARE of it). The eye starts there
 * before its first measurement, and the meter's window is set around it.
 *
 * THE METER. The scene is measured in EYE_ADAPTATION_METER_WIDTH x EYE_ADAPTATION_METER_HEIGHT blocks. Each block holds
 * the mean log2 luminance of EYE_ADAPTATION_METER_SAMPLES x EYE_ADAPTATION_METER_SAMPLES of its pixels, every pixel's
 * held within the window around the anchor (EyeAdaptation_GetWindow): the sun's disc, a lamp's bulb or a black corner
 * count no further than its edges, so none of them swings the eye, and the eye stays within the window: down to a dark
 * interior's level, up to a lamp-lit square's at night. The sky counts as at least the night's own light: the game lights
 * surfaces at night with the natural night sky's glow (airglow, zodiacal light, starlight: WorldEnvironment's night
 * light) but does not draw it, and a sky that gives a white surface that light has that luminance. Without it a dark
 * site's black sky, half the view, would drag the eye stops below the light the ground is seen in (its ground would look
 * brighter than a light-polluted night's). EyeAdaptation_Measure weighs the blocks toward the middle of the view, where
 * the player looks: the view's log-average luminance (a geometric mean), as a camera meters. A window around the eye's
 * own level instead would chase a black sky down without end.
 *
 * THE EYE. Its level eases toward the measurements in stops (EyeAdaptation_Update): slowly, faster for a large step,
 * five times faster when the view brightens (light adaptation is quick, dark adaptation slow). The first measurement
 * sets it. Lamps count only by what they light on screen.
 *
 * THE CURVE: PARTIAL ADAPTATION. The eye does not bring every view to one brightness: a sunlit day looks brighter than a
 * dull one, dusk dimmer than the day, the night dimmer still. The level is read as a real luminance R (cd/m^2) on night
 * vision's scale (NightVision.h: the daylight linear, the night's raised lights through its power law), at the mix of the
 * two in the sky's light at the camera (EyeAdaptation_GetRealScale), and the screen shows it at EYE_ADAPTATION_KEY x
 * (R / NIGHT_VISION_NOON_CANDELAS)^EYE_ADAPTATION_POWER: each stop more real light shows POWER stops brighter. The
 * exposure is that over the level (EyeAdaptation_GetExposure). Through dusk the game's light falls far less than the
 * real light (the night's lights are raised for play), so the exposure first rises, then, as the twilight's real light
 * fades, falls back to the night's: the picture darkens steadily from the noon to the night.
 */


// Macros.
/** @brief The meter's blocks across and down the view (a 16:9 grid; any aspect works: the weights follow the view's). */
#define EYE_ADAPTATION_METER_WIDTH 64
#define EYE_ADAPTATION_METER_HEIGHT 36
#define EYE_ADAPTATION_METER_BLOCK_COUNT (EYE_ADAPTATION_METER_WIDTH * EYE_ADAPTATION_METER_HEIGHT)
/** @brief The pixels a block samples along each side (a regular grid over it, nearest pixels). */
#define EYE_ADAPTATION_METER_SAMPLES 8
/** @brief The share of the sky's scene luminance at the camera a view in the open has as its log-average: the anchor.
 *  Measured on the test world's open fields, by day and by night (T37's status block). */
#define EYE_ADAPTATION_ANCHOR_SHARE 0.11f
/** @brief The meter's window around the anchor, in stops: a pixel counts as at least WINDOW_BELOW under it (how far the
 *  eye opens up in a dark place) and at most WINDOW_ABOVE over it (how far a bright view closes it: further would grey a
 *  white wall or a lamp-lit square). */
#define EYE_ADAPTATION_WINDOW_BELOW 6.0f
#define EYE_ADAPTATION_WINDOW_ABOVE 2.0f
/** @brief The centre weighting: a Gaussian over the view whose standard deviation is this share of the view's height
 *  (at 16:9 the corner blocks count about a sixtieth of the middle's, those in the middle of the top and bottom edges
 *  about two fifths, in the middle of the left and right edges about a twentieth). */
#define EYE_ADAPTATION_CENTRE_SPREAD 0.35f
/** @brief ===== EXPOSURE KNOB =====
 *  The screen level (linear, before the tonemap) of a view whose log-average is a white surface's under a clear noon
 *  (R = NIGHT_VISION_NOON_CANDELAS). Higher brightens every scene alike. Tuned on rendered frames so the noon keeps its
 *  look from before T37 (T37's status block has the measurements). */
#define EYE_ADAPTATION_KEY 0.0873f
/** @brief ===== DAY AND NIGHT CONTRAST KNOB =====
 *  How much of a change in the real light the screen keeps, in stops per stop: 0 shows every scene alike (the eye fully
 *  adapted), 1 not at all. Higher darkens the dusk and the night against the day (and brightens a sunny day against a
 *  dull one). Tuned so the night keeps its look from before T37. */
#define EYE_ADAPTATION_POWER 0.0596f
/** @brief The exposure's range: a guard for skies far outside the tuned ones (a sun hundreds of times too bright, a
 *  world without light). The meter's window already keeps the eye within a few stops of the sky light's anchor: a clear
 *  noon's exposures lie between about 0.5 and 4, the night's between about 10 and 150. */
#define EYE_ADAPTATION_MIN_EXPOSURE (1.0f / 64.0f)
#define EYE_ADAPTATION_MAX_EXPOSURE 512.0f
/** @brief The adaptation's speed: the base rate (1/s), the extra rate per stop of the step (so a large change adapts
 *  faster than a gentle drift), the multiplier while brightening (light adaptation is quicker than dark adaptation), and
 *  the most of one frame's time that counts (one long stall cannot jump the eye). */
#define EYE_ADAPTATION_BASE_SPEED 0.6f
#define EYE_ADAPTATION_STOP_GAIN 0.25f
#define EYE_ADAPTATION_BRIGHTEN_MULTIPLIER 5.0f
#define EYE_ADAPTATION_MAX_DELTA_SECONDS 0.1f


// Types.
/**
 * @brief The eye's state: the level it is adapted to and the measurement it eases toward. A plain value: construct it
 *        with EyeAdaptation_Construct; nothing to release.
 */
typedef struct EyeAdaptationStruct
{
    /** @brief log2 of the luminance the eye is adapted to (the game's linear light units); valid while _isStarted. */
    float _level;
    /** @brief log2 of the last measurement, which it eases toward; valid while _isMeasured. */
    float _target;
    /** @brief Whether the level has been set (by the anchor or a measurement), and whether a measurement has. */
    bool _isStarted;
    bool _isMeasured;
} EyeAdaptation;

/**
 * @brief The meter's window, in log2 luminance: the lowest a surface's pixel counts as, the lowest the sky's counts as (the
 *        night's own light, within the window), and the highest any pixel counts as. A plain value
 *        (EyeAdaptation_GetWindow).
 */
typedef struct EyeAdaptationWindowStruct
{
    float Low;
    float SkyLow;
    float High;
} EyeAdaptationWindow;


// Functions.
/**
 * @brief Makes an eye that has not started: it starts at the first anchor or measurement it is given.
 * @param self The eye (overwritten); must not be NULL.
 */
void EyeAdaptation_Construct(EyeAdaptation* self);

/**
 * @brief Returns the anchor: the level a view in the open has under the sky's light at the camera,
 *        EYE_ADAPTATION_ANCHOR_SHARE of its scene luminance.
 * @param sceneLuminance The scene luminance (WorldSkyLight_GetSceneLuminance, the game's units).
 * @returns Its log2; NaN for a luminance that is not positive or not finite.
 */
float EyeAdaptation_GetAnchor(float sceneLuminance);

/**
 * @brief Returns the meter's window around an anchor: EYE_ADAPTATION_WINDOW_BELOW stops under it to
 *        EYE_ADAPTATION_WINDOW_ABOVE over it, the sky's floor at the night's own light within that.
 * @param anchor The anchor (EyeAdaptation_GetAnchor); NaN or infinite (no sky light) gives the widest window, -64 to 64.
 * @param nightLightLuminance The luminance the night's own light gives a white surface (the frame's sky light's Night, in
 *        the game's units): the natural night sky's glow, which is not drawn; one that is not positive and finite gives
 *        the sky no floor of its own.
 * @returns The window.
 */
EyeAdaptationWindow EyeAdaptation_GetWindow(float anchor, float nightLightLuminance);

/**
 * @brief Returns the view's log-average luminance from the meter's blocks: their mean weighted toward the middle of the
 *        view (EYE_ADAPTATION_CENTRE_SPREAD).
 * @param blocks EYE_ADAPTATION_METER_BLOCK_COUNT log2 luminances, row by row (from the top or the bottom alike: the
 *        weights are symmetric); must not be NULL. A block that is not finite is left out.
 * @param aspect The view's width over its height (the weights' circle); a value that is not positive and finite counts as
 *        16:9.
 * @returns log2 of the luminance; NaN when no block is finite.
 */
float EyeAdaptation_Measure(const float* blocks, float aspect);

/**
 * @brief Moves the eye on by a frame. Until the first measurement the level is the anchor's; the first measurement sets
 *        it; after that it eases toward the last measurement (a frame without one keeps easing toward it), at
 *        EYE_ADAPTATION_BASE_SPEED x (1 + EYE_ADAPTATION_STOP_GAIN x the step in stops), EYE_ADAPTATION_BRIGHTEN_MULTIPLIER
 *        times that while brightening: the share 1 - e^(-rate x seconds) of the step a frame.
 * @param self The eye; must not be NULL.
 * @param anchor The anchor's log2 (EyeAdaptation_GetAnchor); NaN or infinite counts as none (the level stays).
 * @param measurement A measurement's log2 ready this frame (EyeAdaptation_Measure), or NaN for none (infinite: none).
 * @param deltaSeconds The frame's time; at most EYE_ADAPTATION_MAX_DELTA_SECONDS counts, and a time that is not positive
 *        (or NaN) moves nothing (a measurement still becomes the target).
 */
void EyeAdaptation_Update(EyeAdaptation* self, float anchor, float measurement, float deltaSeconds);

/**
 * @brief Returns the eye's level.
 * @param self The eye; must not be NULL.
 * @returns log2 of the luminance it is adapted to; NaN until it has started.
 */
float EyeAdaptation_GetLevel(const EyeAdaptation* self);

/**
 * @brief Returns whether a measurement has set the eye (false while it follows the anchor).
 * @param self The eye; must not be NULL.
 * @returns Whether it has been measured.
 */
bool EyeAdaptation_IsMeasured(const EyeAdaptation* self);

/**
 * @brief Returns how many cd/m^2 one of the game's light units stands for in the sky's light at the camera: the real
 *        luminance of that light (NightVision_GetRealFieldLuminance of its daylight's and night lights' shares) over its
 *        luminance in the game. At least the moonless night's ratio (NIGHT_VISION_NIGHT_CANDELAS over
 *        NIGHT_VISION_NIGHT_LUMINANCE), which a darker night and a world without the sky's light (lamps alone) are read
 *        at, so they look like dark nights rather than black.
 * @param dayLuminance The daylight's share of the scene luminance (WorldSkyLight_GetDayLuminance); a value that is not
 *        positive counts as none.
 * @param nightLuminance The night lights' share (the scene luminance less the daylight's); likewise.
 * @returns The scale, in cd/m^2 per unit.
 */
double EyeAdaptation_GetRealScale(float dayLuminance, float nightLuminance);

/**
 * @brief Returns the exposure for a level: EYE_ADAPTATION_KEY x (R / NIGHT_VISION_NOON_CANDELAS)^EYE_ADAPTATION_POWER
 *        over the level's luminance, where R is the level's luminance times @p realScale; within
 *        [EYE_ADAPTATION_MIN_EXPOSURE, EYE_ADAPTATION_MAX_EXPOSURE].
 * @param level The eye's level (log2); NaN (an eye not started) gives 1.
 * @param realScale EyeAdaptation_GetRealScale's; a value that is not positive and finite counts as its floor.
 * @returns The exposure.
 */
float EyeAdaptation_GetExposure(float level, double realScale);
