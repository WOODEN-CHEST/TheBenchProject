// A CRT screen's look as pure maths (include/CrtScreen.h): the curvature, the power animation's timing, shape and turning
// around, the scanlines' levels, the detail's fade, the flicker and a mask tile's mean. The C twin of
// shaders/common/crt.glsl.

#include <math.h>
#include <stddef.h>
#include "CrtScreen.h"


// Macros.
/** A full turn, in radians. */
#define TWO_PI 6.283185307179586
/** The flicker's wobble: three sines at unrelated frequencies (Hz), their weights (summing to 1) and phases. */
#define FLICKER_FREQUENCY_A 7.3
#define FLICKER_FREQUENCY_B 12.7
#define FLICKER_FREQUENCY_C 0.9
#define FLICKER_WEIGHT_A 0.55
#define FLICKER_WEIGHT_B 0.3
#define FLICKER_WEIGHT_C 0.15
#define FLICKER_PHASE_B 1.3
#define FLICKER_PHASE_C 0.4
/** How a seed shifts a screen's wobble: the seed mixed (SplitMix64's finalizer, so neighbouring ids land far apart),
 *  its remainder by this prime, in thousandths of a second. */
#define FLICKER_SEED_PERIOD 1000003U
#define FLICKER_SEED_STEP 0.001
#define SPLITMIX_MULTIPLIER_A 0xBF58476D1CE4E5B9ULL
#define SPLITMIX_MULTIPLIER_B 0x94D049BB133111EBULL
/** A mask tile's pixels: 4 bytes each, of which the first 3 are colour; a byte's largest value; the shader's sRGB decode. */
#define MASK_PIXEL_BYTES 4U
#define MASK_COLOR_CHANNELS 3U
#define MASK_BYTE_MAX 255.0
#define MASK_SRGB_GAMMA 2.2


// Fields.
/* The procedural masks' names, as definitions write them, indexed by CrtMaskKind (none for a texture). */
static const unsigned char* const MaskKindNames[CrtMaskKind_Count] =
{
    [CrtMaskKind_ApertureGrille] = u8"aperture_grille",
    [CrtMaskKind_ShadowMask] = u8"shadow_mask",
    [CrtMaskKind_None] = u8"none",
    [CrtMaskKind_Texture] = NULL
};


// Static functions.
static float Clamp01(float value)
{
    return (value > 0.0f) ? ((value < 1.0f) ? value : 1.0f) : 0.0f; // NaN gives 0
}

/* GLSL's smoothstep. */
static float Smoothstep(float edge0, float edge1, float value)
{
    float T = Clamp01((value - edge0) / (edge1 - edge0));
    return T * T * (3.0f - (2.0f * T));
}

/* Turning on, at progress @p progress: the line appears and brightens, opens into the picture, which settles. */
static CrtPowerShape GetPowerOnShape(float progress)
{
    CrtPowerShape Shape = { .OpenX = 1.0f, .OpenY = 1.0f, .Brightness = 1.0f, .Whiteness = 0.0f, .Light = 0.0f };
    if (progress < CRT_SCREEN_POWER_ON_LINE_END)
    {
        Shape.OpenY = CRT_SCREEN_POWER_LINE;
        Shape.Brightness = CRT_SCREEN_POWER_MAX_BRIGHTNESS * Smoothstep(0.0f, CRT_SCREEN_POWER_ON_LINE_END, progress);
        return Shape;
    }
    if (progress < CRT_SCREEN_POWER_ON_OPEN_END)
    {
        float Step = (progress - CRT_SCREEN_POWER_ON_LINE_END)
            / (CRT_SCREEN_POWER_ON_OPEN_END - CRT_SCREEN_POWER_ON_LINE_END);
        float Eased = 1.0f - ((1.0f - Step) * (1.0f - Step)); // fast at first, slowing as it fills the glass
        Shape.OpenY = CRT_SCREEN_POWER_LINE + ((1.0f - CRT_SCREEN_POWER_LINE) * Eased);
        Shape.Brightness = fminf((1.0f + (CRT_SCREEN_POWER_OVERSHOOT * Step)) / Shape.OpenY,
            CRT_SCREEN_POWER_MAX_BRIGHTNESS);
        return Shape;
    }
    Shape.Brightness = 1.0f + (CRT_SCREEN_POWER_OVERSHOOT
        * (1.0f - Smoothstep(CRT_SCREEN_POWER_ON_OPEN_END, 1.0f, progress)));
    return Shape;
}

/* Turning off, at progress @p progress: the picture collapses into the line, the line into the dot, the dot fades. */
static CrtPowerShape GetPowerOffShape(float progress, float aspect)
{
    CrtPowerShape Shape = { .OpenX = 1.0f, .OpenY = 1.0f, .Brightness = 1.0f, .Whiteness = 0.0f, .Light = 0.0f };
    float Dot = CRT_SCREEN_POWER_LINE / aspect;
    if (progress < CRT_SCREEN_POWER_OFF_LINE_END)
    {
        float Step = progress / CRT_SCREEN_POWER_OFF_LINE_END;
        Shape.OpenY = 1.0f - ((1.0f - CRT_SCREEN_POWER_LINE) * Step * Step); // slow at first, collapsing faster
        Shape.Brightness = fminf(1.0f / Shape.OpenY, CRT_SCREEN_POWER_MAX_BRIGHTNESS);
        return Shape;
    }
    Shape.OpenY = CRT_SCREEN_POWER_LINE;
    if (progress < CRT_SCREEN_POWER_OFF_DOT_END)
    {
        float Step = (progress - CRT_SCREEN_POWER_OFF_LINE_END)
            / (CRT_SCREEN_POWER_OFF_DOT_END - CRT_SCREEN_POWER_OFF_LINE_END);
        Shape.OpenX = 1.0f - ((1.0f - Dot) * Step * Step);
        Shape.Brightness = CRT_SCREEN_POWER_MAX_BRIGHTNESS;
        return Shape;
    }
    Shape.OpenX = Dot;
    Shape.Brightness = CRT_SCREEN_POWER_MAX_BRIGHTNESS
        * (1.0f - Smoothstep(CRT_SCREEN_POWER_OFF_DOT_END, 1.0f, progress));
    return Shape;
}


// Public functions.
CrtScreenSettings CrtScreenSettings_CreateDefault(void)
{
    return (CrtScreenSettings)
    {
        .Curvature = CRT_SCREEN_DEFAULT_CURVATURE, .AreScanlinesEnabled = true,
        .ScanlineStrength = CRT_SCREEN_DEFAULT_SCANLINE_STRENGTH, .Mask = CrtMaskKind_ApertureGrille,
        .MaskStrength = CRT_SCREEN_DEFAULT_MASK_STRENGTH, .Glow = CRT_SCREEN_DEFAULT_GLOW,
        .Reflection = CRT_SCREEN_DEFAULT_REFLECTION, .Flicker = CRT_SCREEN_DEFAULT_FLICKER,
        .Vignette = CRT_SCREEN_DEFAULT_VIGNETTE, .PowerOnSeconds = CRT_SCREEN_DEFAULT_POWER_ON_SECONDS,
        .PowerOffSeconds = CRT_SCREEN_DEFAULT_POWER_OFF_SECONDS
    };
}

const unsigned char* CrtMaskKind_GetName(CrtMaskKind kind)
{
    return (((int32_t)kind >= 0) && ((int32_t)kind < (int32_t)CrtMaskKind_Count)) ? MaskKindNames[kind] : NULL;
}

Vector2 CrtScreen_Distort(Vector2 uv, float curvature, float aspect)
{
    float X = (uv.x * 2.0f) - 1.0f;
    float Y = (uv.y * 2.0f) - 1.0f;
    float BentX = X * (1.0f + (curvature * Y * Y));
    float BentY = Y * (1.0f + (curvature * aspect * X * X));
    return (Vector2){ (BentX * 0.5f) + 0.5f, (BentY * 0.5f) + 0.5f };
}

float CrtScreen_AdvancePower(float transition, bool isPowered, float powerOnSeconds, float powerOffSeconds,
    float seconds)
{
    float Now = Clamp01(transition);
    float Duration = isPowered ? powerOnSeconds : powerOffSeconds;
    if (!(Duration > 0.0f))
    {
        return isPowered ? 1.0f : 0.0f;
    }
    if (!(seconds > 0.0f))
    {
        return Now;
    }
    float Step = seconds / Duration;
    return isPowered ? fminf(Now + Step, 1.0f) : fmaxf(Now - Step, 0.0f);
}

CrtPowerShape CrtScreen_GetPowerShape(bool isPowered, float transition, float aspect)
{
    float Progress = Clamp01(transition);
    float Aspect = (isfinite(aspect) && (aspect > 0.0f)) ? aspect : 1.0f;
    CrtPowerShape Shape = isPowered ? GetPowerOnShape(Progress) : GetPowerOffShape(1.0f - Progress, Aspect);
    Shape.Whiteness = 1.0f - Smoothstep(CRT_SCREEN_POWER_LINE, CRT_SCREEN_POWER_WHITE_HEIGHT, Shape.OpenY);
    Shape.Light = Shape.Brightness * Shape.OpenX * Shape.OpenY;
    return Shape;
}

CrtScanlineLevels CrtScreen_GetScanlineLevels(float rowsPerPixel)
{
    // The level whose lines just reach the least span, as a fraction: lines of 2^level rows span exactly
    // CRT_SCREEN_SCANLINE_MIN_PIXELS there. The level drawn is the next whole one up (its lines span more), and over its
    // range's last stretch the level after it blends in, all of it where the drawn level would turn too fine.
    float Rows = (isfinite(rowsPerPixel) && (rowsPerPixel > 0.0f)) ? rowsPerPixel : 0.0f;
    float Level = log2f(fmaxf(Rows * CRT_SCREEN_SCANLINE_MIN_PIXELS, 1e-6f));
    float Drawn = fmaxf(ceilf(Level), 0.0f);
    return (CrtScanlineLevels)
    {
        .RowsPerLine = exp2f(Drawn),
        .CoarseWeight = Smoothstep(Drawn - CRT_SCREEN_SCANLINE_BLEND, Drawn, Level)
    };
}

float CrtScreen_GetDetailFade(float pictureHeightPixels)
{
    return Smoothstep(CRT_SCREEN_DETAIL_FADE_START, CRT_SCREEN_DETAIL_FADE_END, pictureHeightPixels);
}

float CrtScreen_GetFlicker(float flicker, double seconds, uint64_t seed)
{
    uint64_t Mixed = (seed ^ (seed >> 30U)) * SPLITMIX_MULTIPLIER_A;
    Mixed = (Mixed ^ (Mixed >> 27U)) * SPLITMIX_MULTIPLIER_B;
    Mixed ^= Mixed >> 31U;
    double Moment = seconds + ((double)(Mixed % FLICKER_SEED_PERIOD) * FLICKER_SEED_STEP);
    double Wobble = (FLICKER_WEIGHT_A * sin(TWO_PI * FLICKER_FREQUENCY_A * Moment))
        + (FLICKER_WEIGHT_B * sin((TWO_PI * FLICKER_FREQUENCY_B * Moment) + FLICKER_PHASE_B))
        + (FLICKER_WEIGHT_C * sin((TWO_PI * FLICKER_FREQUENCY_C * Moment) + FLICKER_PHASE_C));
    return 1.0f + (flicker * (float)Wobble);
}

float CrtScreen_GetSwitchedTransition(bool isPowered, float transition)
{
    float Progress = Clamp01(transition);
    if ((Progress <= 0.0f) || (Progress >= 1.0f))
    {
        return Progress; // at rest: dark, or the whole picture
    }
    if (!isPowered)
    {
        // It was turning on. A whole picture (settling) collapses from the start; the opening's height LINE + (1 - LINE)
        // (1 - (1 - Step)^2) is the collapse's 1 - (1 - LINE) S^2 at S = 1 - Step, so a picture still opening collapses
        // from its height, and a line still brightening (Step 0) is the collapse's end, the line.
        if (Progress >= CRT_SCREEN_POWER_ON_OPEN_END)
        {
            return 1.0f;
        }
        float Step = fmaxf(Progress - CRT_SCREEN_POWER_ON_LINE_END, 0.0f)
            / (CRT_SCREEN_POWER_ON_OPEN_END - CRT_SCREEN_POWER_ON_LINE_END);
        return 1.0f - (CRT_SCREEN_POWER_OFF_LINE_END * (1.0f - Step));
    }
    // It was turning off, at the power-off animation's progress 1 - Progress.
    float OffProgress = 1.0f - Progress;
    if (OffProgress < CRT_SCREEN_POWER_OFF_LINE_END)
    {
        float Collapse = OffProgress / CRT_SCREEN_POWER_OFF_LINE_END;
        return CRT_SCREEN_POWER_ON_LINE_END
            + ((CRT_SCREEN_POWER_ON_OPEN_END - CRT_SCREEN_POWER_ON_LINE_END) * (1.0f - Collapse));
    }
    if (OffProgress < CRT_SCREEN_POWER_OFF_DOT_END)
    {
        return CRT_SCREEN_POWER_ON_LINE_END; // a line shrinking to its dot: the whole line, about to open
    }
    return 0.0f; // a fading dot: the tube warms up again
}

Vector3 CrtScreen_GetMaskTileMean(const unsigned char* rgbaPixels, size_t pixelCount, bool isSrgb)
{
    if ((rgbaPixels == NULL) || (pixelCount == 0U))
    {
        return (Vector3){ 1.0f, 1.0f, 1.0f };
    }
    double Sums[MASK_COLOR_CHANNELS] = { 0.0, 0.0, 0.0 };
    for (size_t Pixel = 0U; Pixel < pixelCount; Pixel++)
    {
        for (size_t Channel = 0U; Channel < MASK_COLOR_CHANNELS; Channel++)
        {
            double Value = (double)rgbaPixels[(Pixel * MASK_PIXEL_BYTES) + Channel] / MASK_BYTE_MAX;
            Sums[Channel] += isSrgb ? pow(Value, MASK_SRGB_GAMMA) : Value;
        }
    }
    return (Vector3)
    {
        fmaxf((float)(Sums[0] / (double)pixelCount), CRT_SCREEN_MIN_MASK_TILE_MEAN),
        fmaxf((float)(Sums[1] / (double)pixelCount), CRT_SCREEN_MIN_MASK_TILE_MEAN),
        fmaxf((float)(Sums[2] / (double)pixelCount), CRT_SCREEN_MIN_MASK_TILE_MEAN)
    };
}
