#pragma once
#include <stdbool.h>


/*
 * Private header (source/): the eye's dim-light vision as the renderer models it. Pure maths, no GL, no state. The sky
 * and PBR shaders apply it to reflected and scattered light (shaders/common/night_vision.glsl is the GLSL twin of
 * NightVision_GetRodShare; the tests check this one).
 *
 * THE EYE. In bright light the cones see colour. In dim light the rods take over: they see no colour, and a scene they
 * dominate looks grey, with the faint blue cast night scenes are seen and painted with. That is why a moonlit sky,
 * which is as orange near a low moon as a sunset sky (moonlight is sunlight, scattered by the same air), looks
 * grey-blue to the eye while a camera's long exposure shows the orange. The rods' share of vision follows Hunt's rod
 * sensitivity, as Krawczyk, Myszkowski and Seidel ("Perceptual Effects in Real-time Tone Mapping", 2005, eq. 4.7 and
 * 4.15 of Krawczyk's thesis) use it: sigma = 0.04 / (0.04 + L) for the luminance L in cd/m^2, half and half at
 * 0.04 cd/m^2 (a bright moonlit scene), nearly all cones from a few cd/m^2 up. The rods' percept is the pixel's
 * luminance in a grey shifted toward the night blue (CIE x, y) = (0.25, 0.25) of Jensen et al. ("A Physically-Based
 * Night Sky Model", 2001) by the world's blue shift (half way by default: the full night blue looks like a film's
 * day-for-night filter, and Krawczyk's own tint, (1.05, 0.97, 1.27), reads violet). The grey keeps the pixel's
 * luminance, so the night's tuned brightness does not change.
 *
 * THE GAME'S LIGHT. The game compresses light levels: its noon (a scene luminance of about 0.40 in its own units, T36's
 * light scale) stands for about 5000 cd/m^2, its moonless night (about 0.0054 with the default light pollution) for
 * about 0.001 cd/m^2: a range of about 10^2 in the game for 10^7 in the world. A game luminance Y is read as the real
 * luminance 5000 x (Y / 0.40)^g cd/m^2, the power g (about 3.6) set by those two anchors. Hunt's sensitivity then
 * becomes sigma = 1 / (1 + (Y / Yh)^g), half at Yh (about 0.015): the game's level of 0.04 cd/m^2.
 *
 * WHICH LUMINANCE. The larger of the pixel's own and the field luminance: the light the sky (the sun, the moon, the
 * sky's glow, light pollution) gives the whole scene. Since T36 the daylight in it (the sun's and the sky's light from
 * the sun) is on the real scale, linearly, while the night's lights (the moon, the moonless night's own light, the town's)
 * are raised for play and are read through the anchors' power law: NightVision_GetFieldLuminance adds their real
 * luminances, so the twilight keeps its colours until the sun is a few degrees down, as it does for the eye. By day the field is bright
 * and every pixel is seen by the cones, however dark (a shadow at noon is still tens of cd/m^2). At night each pixel
 * is seen by its own light: a lamp's bright pool keeps its colour, the dim sky beyond it greys. Lamps do not raise the
 * field (they do lower the exposure): the rods adapt locally, each patch of retina to its own part of the view, so a
 * lamp beside you darkens the sky in front of you but gives it no colour. Emitted light (the sun's and the moon's
 * discs, stars, emissive materials) is not passed through it: the game draws emitters far dimmer than they are, so
 * their pixels would understate them, and a real emitter that bright is seen in colour (the orange rising moon, a lit
 * screen at night).
 */


// Macros.
/** @brief The game's noon: the scene luminance (in the game's linear light units; what eye adaptation starts from:
 *  WorldSkyLight_GetSceneLuminance at a standing player's eyes) of a clear noon with the default environment
 *  (tests/WorldSkyLightTests.c checks it). */
#define NIGHT_VISION_NOON_LUMINANCE 0.40
/** @brief The real luminance a clear noon stands for, in cd/m^2 (a sunlit scene). */
#define NIGHT_VISION_NOON_CANDELAS 5000.0
/** @brief The game's moonless night: the scene luminance of a new-moon midnight with the default light pollution (the
 *  night's own light and the town's; tests/WorldSkyLightTests.c checks it). */
#define NIGHT_VISION_NIGHT_LUMINANCE 0.0054
/** @brief The real luminance a moonless night stands for, in cd/m^2 (starlight and a little skyglow). */
#define NIGHT_VISION_NIGHT_CANDELAS 0.001
/** @brief The anchors on the light scale before T36 (the renderer's A/B of the old light).
 *  TODO: remove once the owner approves T36's light. */
#define NIGHT_VISION_LEGACY_NOON_LUMINANCE 0.74
#define NIGHT_VISION_LEGACY_NIGHT_LUMINANCE 0.006
/** @brief Hunt's half point: the luminance in cd/m^2 at which rods and cones share vision equally. */
#define NIGHT_VISION_HALF_CANDELAS 0.04
/** @brief Above (Y / Yh)^g of this, the rods' share is below 1/1024 and the shaders leave the colour exactly as it is:
 *  by day nothing changes, bit for bit. */
#define NIGHT_VISION_FULL_COLOUR_RATIO 1023.0
/** @brief The night blue the rods' grey shifts toward at a blue shift of 1, as a CIE 1931 chromaticity: Jensen et
 *  al.'s (2001), the blue painters give night scenes. */
#define NIGHT_VISION_BLUE_X 0.25
#define NIGHT_VISION_BLUE_Y 0.25


// Types.
/**
 * @brief The night vision's calibration on the game's light scale, derived from the anchors above.
 *
 * A plain value from NightVision_GetCalibration; the renderer hands its fields to the shaders.
 */
typedef struct NightVisionCalibrationStruct
{
    /** @brief Yh: the game luminance at which rods and cones share vision equally (Hunt's 0.04 cd/m^2). */
    float HalfLuminance;
    /** @brief g: how steeply the game's light scale stretches into the real one (real ~ game^g). */
    float Steepness;
    /** @brief The game luminance from which the rods' share is below 1/1024 (NIGHT_VISION_FULL_COLOUR_RATIO): while
     *  the field luminance is at or above it, night vision changes nothing. */
    float FullColourLuminance;
} NightVisionCalibration;


// Functions.
/**
 * @brief Returns the calibration: Steepness from the noon and night anchors, HalfLuminance from Hunt's half point,
 *        FullColourLuminance from NIGHT_VISION_FULL_COLOUR_RATIO.
 * @returns The calibration (NightVision_GetCalibrationFor of NIGHT_VISION_NOON_LUMINANCE and
 *          NIGHT_VISION_NIGHT_LUMINANCE).
 */
NightVisionCalibration NightVision_GetCalibration(void);

/**
 * @brief Returns the calibration for a light scale whose clear noon and moonless night have these scene luminances (the
 *        candela values they stand for stay NIGHT_VISION_NOON_CANDELAS and NIGHT_VISION_NIGHT_CANDELAS): the renderer's
 *        A/B of the light before T36 takes it with NIGHT_VISION_LEGACY_NOON_LUMINANCE and _NIGHT_LUMINANCE.
 * @param noonLuminance The scale's noon (> nightLuminance > 0).
 * @param nightLuminance Its moonless night.
 * @returns The calibration.
 */
NightVisionCalibration NightVision_GetCalibrationFor(double noonLuminance, double nightLuminance);

/**
 * @brief Returns the real luminance a game luminance stands for: NIGHT_VISION_NOON_CANDELAS x (Y /
 *        NIGHT_VISION_NOON_LUMINANCE)^Steepness.
 * @param luminance The game luminance (linear light units); a value that is not positive gives 0.
 * @returns The luminance in cd/m^2.
 */
double NightVision_GetRealLuminance(float luminance);

/**
 * @brief Returns the field luminance the shaders' night vision compares with, on the game's light scale (T36), from the
 *        scene luminance split in two: the daylight's share (the sun's and the sky's light from the sun, on the real
 *        scale: NIGHT_VISION_NOON_LUMINANCE stands for NIGHT_VISION_NOON_CANDELAS, linearly) and the night lights' (the
 *        moon's, the moonless night's own light, the town's: raised for play, so NightVision_GetRealLuminance). Their real
 *        luminances add, and the sum comes back through the inverse of NightVision_GetRealLuminance.
 * @param dayLuminance The daylight's share; a value that is not positive gives none.
 * @param nightLuminance The night lights' share; a value that is not positive gives none.
 * @returns The field luminance (game units): the night lights' share by night, about the whole by day.
 */
float NightVision_GetFieldLuminance(float dayLuminance, float nightLuminance);

/**
 * @brief Returns the rods' share of vision for a pixel: 1 / (1 + (max(luminance, fieldLuminance) / Yh)^g), 0 (the
 *        colour stays exactly as it is) once that ratio reaches NIGHT_VISION_FULL_COLOUR_RATIO, times @p strength.
 *
 * The CPU twin of the shaders' nightVisionRodShare.
 * @param luminance The pixel's luminance (game units, linear); NaN and negative count as 0.
 * @param fieldLuminance The field luminance: the scene luminance the sky's light gives (game units); NaN and negative
 *        count as 0.
 * @param strength How strongly the night vision applies: 0 (none: colourful nights) to 1 (the eye); clamped.
 * @returns The rods' share in [0, 1]: the pixel's colour is blended this far toward its luminance in the rods' grey.
 */
float NightVision_GetRodShare(float luminance, float fieldLuminance, float strength);

/**
 * @brief Returns the rods' grey for a blue shift, as a linear sRGB (Rec. 709) colour of luminance 1: white at 0, the
 *        night blue (NIGHT_VISION_BLUE_X, NIGHT_VISION_BLUE_Y) at 1, mixed between.
 *
 * The shaders blend a dim pixel toward its luminance times this colour.
 * @param blueShift 0 (a neutral grey: the rods see no colour) to 1 (the night blue); clamped, NaN counts as 0.
 * @param outTint [out] Receives the colour (red, green, blue). Its luminance is 1, so it keeps a pixel's luminance.
 */
void NightVision_GetRodTint(float blueShift, float outTint[3]);
