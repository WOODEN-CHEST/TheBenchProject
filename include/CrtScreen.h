#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector2 is raylib's (the picture's coordinates, as the surfaces and the UI take them); this include is unavoidable
// here.
#include "raylib/raylib.h"


/**
 * @file CrtScreen.h
 * @brief A CRT screen's look as pure maths: the settings a world model's "crt" surface takes, the curvature that maps a
 *        point of the glass to the point of the picture shown there, the power animation's timing, shape and turning
 *        around, the scanlines' choice of lines, the flicker and a mask tile's mean. The C twin of
 *        shaders/common/crt.glsl.
 *
 * THE CURVATURE (CrtScreen_Distort). A CRT's picture bulges: its edges bow outward and its corners round off inside the
 * glass. The surface's texture coordinates cover the whole glass (0..1, (0, 0) its top-left); the picture under a point
 * of the glass is CrtScreen_Distort of that point, and outside 0..1 the glass is dark (the tube's border). The shader and
 * pointing use this one function, forward only, so a click lands on exactly what the shader shows under the pointer.
 *
 * THE POWER ANIMATION. A screen component's power_transition (WorldScreenComponent.h) goes from 0 (dark) to 1 (its
 * picture) over the CRT's power-on time while it is powered, and back to 0 over its power-off time while it is not
 * (CrtScreen_AdvancePower, once a tick by the screens system). CrtScreen_GetPowerShape turns the two into what the glass
 * shows: turning on, a bright horizontal line that opens into the picture, which settles after a short overshoot;
 * turning off, the picture collapses into a line, the line into a dot, and the dot fades. The squeezed beam is brighter
 * (up to CRT_SCREEN_POWER_MAX_BRIGHTNESS) and whiter. Switched half way, the transition turns around to the point of the
 * other animation that carries on what is shown (CrtScreen_GetSwitchedTransition): a picture still opening collapses
 * from its height, a collapsing one opens again from its height; a line still brightening goes out as the line, a line
 * already shrinking to its dot comes back as the whole line, and a fading dot starts the tube warming up again.
 *
 * THE SCANLINES. Lines across the picture, every 1, 2, 4 ... of its rows: the fewest rows that keep a line at least
 * CRT_SCREEN_SCANLINE_MIN_PIXELS of the pass's pixels high (the window's in the crisp overlay, the pixelated scene's
 * larger ones there), so a screen seen small never shows moiré (CrtScreen_GetScanlineLevels). Between two such counts the
 * coarser lines blend in, so they never jump while the camera moves. The lines and the mask (patterns of window pixels)
 * fade out while the whole picture is shorter than CRT_SCREEN_DETAIL_FADE_END window pixels (CrtScreen_GetDetailFade):
 * from across a room a CRT just glows. Both keep the picture's mean light: a mask tile is divided by its mean colour
 * (CrtScreen_GetMaskTileMean).
 */


// Macros.
/** @brief The curvature a crt surface has when its definition does not say: its picture's corners come in by about this
 *  share of the picture's half width. */
#define CRT_SCREEN_DEFAULT_CURVATURE 0.08f
/** @brief The largest curvature a crt surface may have. */
#define CRT_SCREEN_MAX_CURVATURE 0.5f
/** @brief How deep the scanlines are when the definition does not say: the lines' light swings by this share around the
 *  picture's (0 none, 1 dark gaps between lines twice as bright). */
#define CRT_SCREEN_DEFAULT_SCANLINE_STRENGTH 0.35f
/** @brief How strong the mask is when the definition does not say (0 none, 1 each window pixel shows only its own
 *  phosphors' colours). */
#define CRT_SCREEN_DEFAULT_MASK_STRENGTH 0.3f
/** @brief The share of the picture's light its glow spreads around it when the definition does not say. */
#define CRT_SCREEN_DEFAULT_GLOW 0.35f
/** @brief A crt's glass's reflectance facing it, as a share of a bare glass's, when the definition does not say (an
 *  anti-glare coating's; toward grazing angles every glass mirrors more, whatever its coating). */
#define CRT_SCREEN_DEFAULT_REFLECTION 0.6f
/** @brief How much the picture's brightness wobbles when the definition does not say (0 steady). */
#define CRT_SCREEN_DEFAULT_FLICKER 0.01f
/** @brief The largest flicker a crt surface may have. */
#define CRT_SCREEN_MAX_FLICKER 0.5f
/** @brief How much the picture darkens toward its edges when the definition does not say (0 not at all, 1 black at its
 *  corners). */
#define CRT_SCREEN_DEFAULT_VIGNETTE 0.25f
/** @brief How long turning on and turning off take when the definition does not say, in seconds. */
#define CRT_SCREEN_DEFAULT_POWER_ON_SECONDS 0.8f
#define CRT_SCREEN_DEFAULT_POWER_OFF_SECONDS 0.6f
/** @brief The longest a crt surface's power-on or power-off may take, in seconds. */
#define CRT_SCREEN_MAX_POWER_SECONDS 10.0f
/** @brief The largest side a mask tile's image may have, in pixels. */
#define CRT_SCREEN_MAX_MASK_TILE_SIDE 64
/** @brief The line the picture squeezes into while turning on or off: its half height as a share of the picture's half
 *  height (the dot's is the same, so the dot is round). */
#define CRT_SCREEN_POWER_LINE 0.012f
/** @brief The most the squeezed beam brightens the line and the dot (its light falls on less of the glass). */
#define CRT_SCREEN_POWER_MAX_BRIGHTNESS 5.0f
/** @brief How far the picture overshoots its brightness just after it has opened, settled by the end of the power-on. */
#define CRT_SCREEN_POWER_OVERSHOOT 0.25f
/** @brief Turning on: the share of the power-on time by which the line has appeared, and by which it has opened into
 *  the picture. */
#define CRT_SCREEN_POWER_ON_LINE_END 0.2f
#define CRT_SCREEN_POWER_ON_OPEN_END 0.6f
/** @brief Turning off: the share of the power-off time by which the picture has collapsed into the line, and by which
 *  the line has collapsed into the dot (the dot fades over the rest). */
#define CRT_SCREEN_POWER_OFF_LINE_END 0.25f
#define CRT_SCREEN_POWER_OFF_DOT_END 0.45f
/** @brief How open the squeezed picture is (its half height) when it has taken back its colours: below it, the beam's
 *  overlapping lines show white. */
#define CRT_SCREEN_POWER_WHITE_HEIGHT 0.3f
/** @brief The least a scanline spans, in the pass's pixels: below it lines would beat against the pixels (moiré). */
#define CRT_SCREEN_SCANLINE_MIN_PIXELS 2.5f
/** @brief The last share of a level's range over which the next, coarser level of lines blends in (in log2 steps). */
#define CRT_SCREEN_SCANLINE_BLEND 0.35f
/** @brief Where the scanlines and the mask fade out: while the whole picture spans fewer window pixels than the end,
 *  down to none at the start. */
#define CRT_SCREEN_DETAIL_FADE_START 64.0f
#define CRT_SCREEN_DETAIL_FADE_END 160.0f
/** @brief The procedural masks' period across, in window pixels (a red, a green and a blue column). */
#define CRT_SCREEN_MASK_PERIOD 3.0f
/** @brief The least a mask tile's mean colour is taken as, per channel (a black channel would divide by 0). */
#define CRT_SCREEN_MIN_MASK_TILE_MEAN 1e-3f


// Types.
/** @brief What a CRT's mask is: the pattern of phosphors over its picture, in window pixels. */
typedef enum CrtMaskKindEnum
{
    /** @brief Red, green and blue columns (a Trinitron's). */
    CrtMaskKind_ApertureGrille = 0,
    /** @brief Red, green and blue triads in staggered rows (a television's shadow mask). */
    CrtMaskKind_ShadowMask = 1,
    /** @brief No mask. */
    CrtMaskKind_None = 2,
    /** @brief A tile from a texture, repeated over the window's pixels (the surface names it). */
    CrtMaskKind_Texture = 3,
    /** @brief The number of kinds (not a kind). */
    CrtMaskKind_Count = 4
} CrtMaskKind;

/**
 * @brief A CRT surface's look: the "crt" block of a world model's surface (references/asset_structure.md). A plain value
 *        type; CrtScreenSettings_CreateDefault gives every default.
 */
typedef struct CrtScreenSettingsStruct
{
    /** @brief How far the picture's corners come in, about this share of its half width (0 flat, at most
     *  CRT_SCREEN_MAX_CURVATURE). */
    float Curvature;
    /** @brief Whether it shows scanlines, and how deep they are (0..1). */
    bool AreScanlinesEnabled;
    float ScanlineStrength;
    /** @brief Its mask, and how strong (0..1). A texture mask's tile is the surface's (WorldModelSurface_GetMaskTexture). */
    CrtMaskKind Mask;
    float MaskStrength;
    /** @brief The share of the picture's light spread into a soft glow around it (0..1). */
    float Glow;
    /** @brief Its glass's reflectance facing it as a share of a bare glass's (0..1): the lights' highlights and what it
     *  mirrors. Toward grazing angles the glass mirrors more whatever this share (the Fresnel), as coated glass does. */
    float Reflection;
    /** @brief How much its brightness wobbles (0..CRT_SCREEN_MAX_FLICKER). */
    float Flicker;
    /** @brief How much its picture darkens toward the edges (0..1). */
    float Vignette;
    /** @brief How long turning on and turning off take, in seconds (0 at once, at most CRT_SCREEN_MAX_POWER_SECONDS). */
    float PowerOnSeconds;
    float PowerOffSeconds;
} CrtScreenSettings;

/**
 * @brief What a CRT's glass shows of its power animation at one moment (CrtScreen_GetPowerShape). A plain value type.
 *
 * In the picture's centred coordinates (-1..1 across, both ways) only the part within OpenX and OpenY glows, and the
 * whole picture is squeezed into it.
 */
typedef struct CrtPowerShapeStruct
{
    /** @brief The open part's half width and half height, as shares of the picture's (0..1]. */
    float OpenX;
    float OpenY;
    /** @brief The glow's brightness, times the picture's (0 dark, 1 the picture as it is, up to
     *  CRT_SCREEN_POWER_MAX_BRIGHTNESS while squeezed). */
    float Brightness;
    /** @brief How white the squeezed beam is (0 the picture's colours, 1 white). */
    float Whiteness;
    /** @brief The light the glass gives off, as a share of the whole picture's: Brightness times the open area. */
    float Light;
} CrtPowerShape;

/**
 * @brief Which lines a CRT draws at a distance (CrtScreen_GetScanlineLevels). A plain value type.
 */
typedef struct CrtScanlineLevelsStruct
{
    /** @brief How many of the picture's rows one line spans (1, 2, 4, ...). */
    float RowsPerLine;
    /** @brief How much the next, coarser lines (twice as many rows each) are blended in (0..1). */
    float CoarseWeight;
} CrtScanlineLevels;


// Functions.
/**
 * @brief Returns the settings of a crt surface whose definition gives none: every CRT_SCREEN_DEFAULT_*, scanlines on and
 *        the aperture grille.
 * @returns The defaults.
 */
CrtScreenSettings CrtScreenSettings_CreateDefault(void);

/**
 * @brief Returns a mask kind's name as definitions write it ("aperture_grille", "shadow_mask", "none"; a texture mask is
 *        named by its texture instead).
 * @param kind The kind.
 * @returns The name (static); NULL for CrtMaskKind_Texture and for a value that is not a kind.
 */
const unsigned char* CrtMaskKind_GetName(CrtMaskKind kind);

/**
 * @brief Maps a point of a CRT's glass to the point of its picture shown there (see the file comment, "THE CURVATURE").
 *
 * In centred coordinates (x, y from -1 to 1), x becomes x (1 + curvature y^2) and y becomes y (1 + curvature aspect
 * x^2): the centre and the middles of the edges stay, the picture's corners come in by a little less than the curvature
 * times the half width, about as far across as down (a round corner on any aspect). The identity at curvature 0 (to a
 * float's rounding).
 * @param uv The point of the glass: its texture coordinates ((0, 0) the top-left, (1, 1) the bottom-right).
 * @param curvature The surface's curvature (CrtScreenSettings.Curvature).
 * @param aspect The picture's width over its height.
 * @returns The point of the picture, in the same convention; outside 0..1 on either axis the glass shows no picture.
 */
Vector2 CrtScreen_Distort(Vector2 uv, float curvature, float aspect);

/**
 * @brief Advances a screen's power transition by one step of time toward its power state.
 * @param transition The transition now (0 dark, 1 the picture); clamped to 0..1, NaN taken as 0.
 * @param isPowered Whether the screen is on: it rises to 1, else falls to 0.
 * @param powerOnSeconds How long rising from 0 to 1 takes (0 or less: at once).
 * @param powerOffSeconds How long falling from 1 to 0 takes (0 or less: at once).
 * @param seconds The step, in seconds (0, negative or NaN: no change, beyond the clamp).
 * @returns The new transition, 0..1.
 */
float CrtScreen_AdvancePower(float transition, bool isPowered, float powerOnSeconds, float powerOffSeconds,
    float seconds);

/**
 * @brief What a CRT's glass shows of its power animation (see the file comment, "THE POWER ANIMATION").
 * @param isPowered Whether the screen is on: the power-on animation at progress @p transition; otherwise the power-off
 *        animation at progress 1 - @p transition.
 * @param transition The power transition, 0..1 (clamped; NaN taken as 0).
 * @param aspect The picture's width over its height (the dot's half width is the line's half height across it); 0 or
 *        less, or not finite, taken as 1.
 * @returns The shape: fully open at brightness 1 when powered at 1; brightness 0 when off at 0.
 */
CrtPowerShape CrtScreen_GetPowerShape(bool isPowered, float transition, float aspect);

/**
 * @brief Where a screen's power transition goes when its power state switches: the point of the new state's animation
 *        that carries on what the glass showed (see the file comment, "THE POWER ANIMATION").
 *
 * Switched off while turning on: a picture still opening goes to the collapse at the same open height, a whole picture
 * (settling, or settled at 1) to the collapse's start, a line still brightening to the line before it shrinks. Switched
 * on while turning off: a collapsing picture goes to the opening at the same open height (a whole one, at 1, stays
 * whole), a line shrinking to its dot to the whole line about to open, a fading dot to the start (0). The ends are kept:
 * 0 and 1 map to themselves, so a screen at rest (any kind) never moves.
 * @param isPowered The new power state: the animation the transition runs from now on.
 * @param transition The transition at the switch, of the other animation (0..1; clamped, NaN taken as 0).
 * @returns The transition for @p isPowered, 0..1.
 */
float CrtScreen_GetSwitchedTransition(bool isPowered, float transition);

/**
 * @brief Chooses a CRT's lines for how many of its picture's rows fall on one of the pass's pixels (see the file comment,
 *        "THE SCANLINES").
 *
 * The rows per line are the fewest powers of two whose lines span at least CRT_SCREEN_SCANLINE_MIN_PIXELS of the pass's
 * pixels; over the last CRT_SCREEN_SCANLINE_BLEND of the range where a level is chosen the next, coarser level blends
 * in, all of it by the level's end, so the choice is continuous as the distance changes.
 * @param rowsPerPixel How many of the picture's rows one of the pass's pixels spans (0 or less, or not finite: lines of
 *        one row).
 * @returns The lines.
 */
CrtScanlineLevels CrtScreen_GetScanlineLevels(float rowsPerPixel);

/**
 * @brief How much of the scanlines and the mask a CRT shows for the size its picture is seen at.
 * @param pictureHeightPixels How many window pixels the picture spans from its top to its bottom.
 * @returns 0 below CRT_SCREEN_DETAIL_FADE_START, 1 from CRT_SCREEN_DETAIL_FADE_END, smooth between.
 */
float CrtScreen_GetDetailFade(float pictureHeightPixels);

/**
 * @brief A CRT's brightness factor for its flicker at a moment: 1 plus the flicker times a wobble between -1 and 1 of a
 *        few slow, unrelated sines, shifted per screen by @p seed so two screens never wobble together.
 * @param flicker The surface's flicker (CrtScreenSettings.Flicker).
 * @param seconds The moment, in seconds (the renderer's clock).
 * @param seed Anything that tells the screen apart (its object's id).
 * @returns The factor, within 1 +- @p flicker.
 */
float CrtScreen_GetFlicker(float flicker, double seconds, uint64_t seed);

/**
 * @brief The mean linear colour of a mask tile's pixels: what the shader divides the tile by, so a mask keeps the
 *        picture's mean light (see the file comment, "THE SCANLINES").
 * @param rgbaPixels The tile's pixels, 4 bytes each (red, green, blue, alpha; alpha is not read).
 * @param pixelCount How many pixels.
 * @param isSrgb Whether the bytes are sRGB (decoded with the shader's power of 2.2 before averaging) or linear.
 * @returns The mean per channel (x red, y green, z blue), each at least CRT_SCREEN_MIN_MASK_TILE_MEAN; (1, 1, 1) for
 *          no pixels or NULL.
 */
Vector3 CrtScreen_GetMaskTileMean(const unsigned char* rgbaPixels, size_t pixelCount, bool isSrgb);
