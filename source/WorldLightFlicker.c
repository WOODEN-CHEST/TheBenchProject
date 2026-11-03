#include "WorldLightFlicker.h"
#include <stdbool.h>
#include <math.h>


// Macros.
/** The time a flicker reads is wrapped at this many seconds (about 136 years at speed 1), so a hostile clock never takes
 *  the noise's lattice indices out of range. */
#define TIME_WRAP_SECONDS 4294967296.0
/** One over 2^53: a hash's top 53 bits as a double in [0, 1). */
#define HASH_TO_UNIT (1.0 / 9007199254740992.0)

/** The candle's three wavers: how many times a second each turns, and its share of the flicker's depth (the shares add up
 *  to 1). The slow one carries most of it, the quick ones the shimmer of the flame. */
#define CANDLE_SLOW_RATE 1.7
#define CANDLE_MID_RATE 4.3
#define CANDLE_QUICK_RATE 9.1
#define CANDLE_SLOW_SHARE 0.5
#define CANDLE_MID_SHARE 0.33
#define CANDLE_QUICK_SHARE 0.17

/** The fluorescent tube: its shimmer's rate and its share of the depth, and its dips: the time is cut into slots of this
 *  length, each holding a dip with this chance (one every ten seconds on average), which starts within its first
 *  stretch and is one to three blinks to the darkest, each dark for a time in the first range and followed by a gap in
 *  the second. The longest dip (start, three blinks and their gaps) stays within its slot. */
#define TUBE_SHIMMER_RATE 23.0
#define TUBE_SHIMMER_SHARE 0.12
#define TUBE_DIP_SLOT_SECONDS 0.8
#define TUBE_DIP_CHANCE 0.08
#define TUBE_DIP_LATEST_START 0.1
#define TUBE_DIP_MAX_BLINKS 3
#define TUBE_BLINK_MIN_SECONDS 0.04
#define TUBE_BLINK_SPREAD_SECONDS 0.08
#define TUBE_GAP_MIN_SECONDS 0.03
#define TUBE_GAP_SPREAD_SECONDS 0.06

/** The broken lamp: the time is cut into slots of this length, each holding a dark gap with this chance; a gap lasts
 *  from the shortest to the shortest plus the spread, and for a while before and after it the lamp stutters (dark
 *  wherever a fast noise is below a half), all within its slot. */
#define BROKEN_SLOT_SECONDS 4.0
#define BROKEN_GAP_CHANCE 0.75
#define BROKEN_GAP_MIN_SECONDS 0.8
#define BROKEN_GAP_SPREAD_SECONDS 1.7
#define BROKEN_STUTTER_SECONDS 0.35
#define BROKEN_STUTTER_RATE 12.0

/** Each stream of noise mixes its own salt into the light's seed, so its streams never move together. */
#define SALT_CANDLE_SLOW 0x6A09E667F3BCC908ULL
#define SALT_CANDLE_MID 0xBB67AE8584CAA73BULL
#define SALT_CANDLE_QUICK 0x3C6EF372FE94F82BULL
#define SALT_TUBE_SHIMMER 0xA54FF53A5F1D36F1ULL
#define SALT_TUBE_DIPS 0x510E527FADE682D1ULL
#define SALT_BROKEN_GAPS 0x9B05688C2B3E6C1FULL
#define SALT_BROKEN_STUTTER 0x1F83D9ABFB41BD6BULL


// Static functions.
/* SplitMix64's step: a well-mixed 64-bit hash of @p value. */
static uint64_t Mix(uint64_t value)
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

/* A hash's top 53 bits as a value in [0, 1). */
static double ToUnit(uint64_t hash)
{
    return (double)(hash >> 11) * HASH_TO_UNIT;
}

/* The key of one lattice point (or one slot) of a stream (the stream's key: the seed with its salt mixed in). */
static uint64_t PointKey(uint64_t stream, int64_t index)
{
    return Mix(stream ^ Mix((uint64_t)index));
}

/* A value in [0, 1) for one lattice point of a stream. */
static double Lattice(uint64_t stream, int64_t index)
{
    return ToUnit(PointKey(stream, index));
}

/* Smooth value noise in [0, 1] of a stream at @p x: the lattice values at the whole numbers around it, blended by a
 * smoothstep (no jump, no kink). */
static double Noise(uint64_t stream, double x)
{
    double Cell = floor(x);
    double Fraction = x - Cell;
    int64_t Index = (int64_t)Cell;
    double Low = Lattice(stream, Index);
    double High = Lattice(stream, Index + 1);
    double Blend = Fraction * Fraction * (3.0 - (2.0 * Fraction));
    return Low + ((High - Low) * Blend);
}

/* A stream's key: the light's seed with the stream's salt. */
static uint64_t Stream(uint64_t seed, uint64_t salt)
{
    return Mix(seed ^ salt);
}

/* How dark a candle is at @p time (0 its brightest .. 1 its darkest): its three wavers. */
static double GetCandleDarkness(uint64_t seed, double time)
{
    return (CANDLE_SLOW_SHARE * Noise(Stream(seed, SALT_CANDLE_SLOW), time * CANDLE_SLOW_RATE))
        + (CANDLE_MID_SHARE * Noise(Stream(seed, SALT_CANDLE_MID), time * CANDLE_MID_RATE))
        + (CANDLE_QUICK_SHARE * Noise(Stream(seed, SALT_CANDLE_QUICK), time * CANDLE_QUICK_RATE));
}

/* Whether a fluorescent tube is in one of its dips' blinks at @p time: its slot's hash says whether the slot holds a
 * dip, where it starts and how many blinks it has; each blink's and gap's length come from hashes of their own. */
static bool IsTubeBlinking(uint64_t seed, double time)
{
    double Slot = floor(time / TUBE_DIP_SLOT_SECONDS);
    uint64_t Key = PointKey(Stream(seed, SALT_TUBE_DIPS), (int64_t)Slot);
    if (ToUnit(Key) >= TUBE_DIP_CHANCE)
    {
        return false;
    }
    double Local = time - (Slot * TUBE_DIP_SLOT_SECONDS);
    double At = ToUnit(Mix(Key ^ 1U)) * TUBE_DIP_LATEST_START;
    int BlinkCount = 1 + (int)(Mix(Key ^ 2U) % (uint64_t)TUBE_DIP_MAX_BLINKS);
    for (int Blink = 0; Blink < BlinkCount; Blink++)
    {
        uint64_t BlinkKey = Mix(Key ^ (uint64_t)(16 + Blink));
        double Dark = TUBE_BLINK_MIN_SECONDS + (ToUnit(BlinkKey) * TUBE_BLINK_SPREAD_SECONDS);
        double Gap = TUBE_GAP_MIN_SECONDS + (ToUnit(Mix(BlinkKey)) * TUBE_GAP_SPREAD_SECONDS);
        if ((Local >= At) && (Local < (At + Dark)))
        {
            return true;
        }
        At += Dark + Gap;
    }
    return false;
}

/* How dark a fluorescent tube is at @p time: its faint shimmer, or its darkest during a dip's blink. */
static double GetTubeDarkness(uint64_t seed, double time)
{
    if (IsTubeBlinking(seed, time))
    {
        return 1.0;
    }
    return TUBE_SHIMMER_SHARE * Noise(Stream(seed, SALT_TUBE_SHIMMER), time * TUBE_SHIMMER_RATE);
}

/* How dark a broken lamp is at @p time: its darkest within its slot's gap, stuttering (its darkest wherever a fast noise
 * is below a half) just before and after it, else lit. The gap and its stutter lie inside the slot. */
static double GetBrokenDarkness(uint64_t seed, double time)
{
    double Slot = floor(time / BROKEN_SLOT_SECONDS);
    uint64_t Key = PointKey(Stream(seed, SALT_BROKEN_GAPS), (int64_t)Slot);
    if (ToUnit(Key) >= BROKEN_GAP_CHANCE)
    {
        return 0.0;
    }
    double Local = time - (Slot * BROKEN_SLOT_SECONDS);
    double Length = BROKEN_GAP_MIN_SECONDS + (ToUnit(Mix(Key ^ 1U)) * BROKEN_GAP_SPREAD_SECONDS);
    double Room = BROKEN_SLOT_SECONDS - Length - (2.0 * BROKEN_STUTTER_SECONDS);
    double Start = BROKEN_STUTTER_SECONDS + (ToUnit(Mix(Key ^ 2U)) * Room);
    double End = Start + Length;
    if ((Local >= Start) && (Local < End))
    {
        return 1.0;
    }
    bool IsStuttering = ((Local >= (Start - BROKEN_STUTTER_SECONDS)) && (Local < Start))
        || ((Local >= End) && (Local < (End + BROKEN_STUTTER_SECONDS)));
    if (!IsStuttering)
    {
        return 0.0;
    }
    return (Noise(Stream(seed, SALT_BROKEN_STUTTER), time * BROKEN_STUTTER_RATE) < 0.5) ? 1.0 : 0.0;
}


// Public functions.
float WorldLightFlicker_GetFactor(WorldLightFlicker flicker, float amount, float speed, double seconds, uint64_t seed)
{
    float Amount = isfinite(amount) ? fminf(fmaxf(amount, 0.0f), 1.0f) : 0.0f;
    if (Amount <= 0.0f)
    {
        return 1.0f;
    }
    double Speed = (isfinite(speed) && (speed > 0.0f)) ? (double)fminf(speed, WORLD_LIGHT_FLICKER_MAX_SPEED) : 0.0;
    // The time at the speed: one that is not finite (the clock's own, or a clock past what a double holds once sped up)
    // counts as 0.
    double Scaled = seconds * Speed;
    double Time = isfinite(Scaled) ? fmod(Scaled, TIME_WRAP_SECONDS) : 0.0;

    double Darkness = 0.0;
    switch (flicker)
    {
        case WorldLightFlicker_None:
            return 1.0f;
        case WorldLightFlicker_Candle:
            Darkness = GetCandleDarkness(seed, Time);
            break;
        case WorldLightFlicker_Fluorescent:
            Darkness = GetTubeDarkness(seed, Time);
            break;
        case WorldLightFlicker_Broken:
            Darkness = GetBrokenDarkness(seed, Time);
            break;
    }
    // An unknown kind leaves the darkness at 0: a steady light. Every kind's darkness is in [0, 1], so the product stays
    // within [0, Amount] and the factor within [1 - Amount, 1] (float rounding is monotonic).
    return 1.0f - (Amount * (float)Darkness);
}
