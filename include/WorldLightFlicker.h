#pragma once
#include <stdint.h>


// Macros.
/** @brief The fastest a flicker may run: its speed, times its kind's own tempo (as a light's flicker_speed allows). */
#define WORLD_LIGHT_FLICKER_MAX_SPEED 100.0f


/**
 * @file WorldLightFlicker.h
 * @brief A light's flicker as pure maths: how bright a candle, a buzzing fluorescent tube or a broken lamp is at a moment.
 *
 * A flicker is a function of the time, the light's own seed (its object's id, so two candles never flicker together)
 * and its settings, and of nothing else: it keeps no state, the same inputs always give the same value, and every run of
 * the game shows the same flicker. The renderer evaluates it once per light per frame at its lights' clock of real seconds
 * (WorldRenderer_GetLightSeconds) and multiplies the light's intensity by it; game code (a candle's sound, a sprite's
 * glow) gets the same value from the same call at that clock.
 *
 * The value always lies in [1 - amount, 1]: amount 0 never flickers, amount 1 reaches darkness. The speed scales the
 * time: the value at speed 2 and t seconds is the value at speed 1 and 2t seconds.
 */


// Types.
/** @brief How a light flickers. Values are saved: never renumber. */
typedef enum WorldLightFlickerEnum
{
    /** @brief A steady light (the value is always 1). */
    WorldLightFlicker_None = 0,
    /** @brief A flame: a soft, smooth, random wavering, now slow, now quicker, never a sudden jump. */
    WorldLightFlicker_Candle = 1,
    /** @brief A fluorescent tube: a fast, faint shimmer, and now and then (about every ten seconds at speed 1) a dip of a
     *  few quick blinks to its darkest. */
    WorldLightFlicker_Fluorescent = 2,
    /** @brief A broken lamp: on for a while, then long dark stretches of a second or more, stuttering on and off where
     *  it fails and comes back. */
    WorldLightFlicker_Broken = 3
} WorldLightFlicker;


// Functions.
/**
 * @brief Returns how bright a flickering light is at a moment, as a multiplier of its intensity.
 *
 * Pure and deterministic: no state, the same inputs give the same value on every call and every run. Cheap (a few
 * hashes); the renderer calls it once per light per frame.
 * @param flicker The kind of flicker; an unknown value counts as WorldLightFlicker_None.
 * @param amount How deep it goes, in [0, 1] (clamped; not finite counts as 0).
 * @param speed Its tempo: the time runs this many times as fast (1 the kind's own; values below 0 or not finite count
 *        as 0, which holds the flicker still at its value for time 0; above WORLD_LIGHT_FLICKER_MAX_SPEED as that).
 * @param seconds The time, in seconds (any clock; not finite, or past what a double holds once sped up, counts as 0).
 *        The renderer passes its lights' clock (WorldRenderer_GetLightSeconds).
 * @param seed The light's own seed (its object's id), so lights flicker apart.
 * @returns The multiplier, in [1 - amount, 1]; exactly 1 for WorldLightFlicker_None or amount 0.
 */
float WorldLightFlicker_GetFactor(WorldLightFlicker flicker, float amount, float speed, double seconds, uint64_t seed);
