#include <math.h>
#include "NightVision.h"


// Static functions.
/* The value, or 0 for a negative value or NaN. */
static double NonNegative(double value)
{
    return (value >= 0.0) ? value : 0.0;
}

/* The value clamped to [0, 1], NaN counting as 0. */
static double ClampUnit(double value)
{
    double Value = NonNegative(value);
    return (Value > 1.0) ? 1.0 : Value;
}


// Public functions.
NightVisionCalibration NightVision_GetCalibration(void)
{
    // real = noon candelas x (game / noon luminance)^g through both anchors, so g = ln(candela ratio) / ln(game ratio);
    // Hunt's half point and the full-colour ratio then fall at fixed game luminances.
    double Steepness = log(NIGHT_VISION_NOON_CANDELAS / NIGHT_VISION_NIGHT_CANDELAS)
        / log(NIGHT_VISION_NOON_LUMINANCE / NIGHT_VISION_NIGHT_LUMINANCE);
    double HalfLuminance = NIGHT_VISION_NOON_LUMINANCE
        * pow(NIGHT_VISION_HALF_CANDELAS / NIGHT_VISION_NOON_CANDELAS, 1.0 / Steepness);
    double FullColourLuminance = HalfLuminance * pow(NIGHT_VISION_FULL_COLOUR_RATIO, 1.0 / Steepness);
    return (NightVisionCalibration)
    {
        .HalfLuminance = (float)HalfLuminance,
        .Steepness = (float)Steepness,
        .FullColourLuminance = (float)FullColourLuminance
    };
}

double NightVision_GetRealLuminance(float luminance)
{
    if (!(luminance > 0.0f))
    {
        return 0.0;
    }
    NightVisionCalibration Calibration = NightVision_GetCalibration();
    return NIGHT_VISION_NOON_CANDELAS * pow((double)luminance / NIGHT_VISION_NOON_LUMINANCE, (double)Calibration.Steepness);
}

float NightVision_GetRodShare(float luminance, float fieldLuminance, float strength)
{
    NightVisionCalibration Calibration = NightVision_GetCalibration();
    double Level = fmax(NonNegative((double)luminance), NonNegative((double)fieldLuminance));
    double Ratio = pow(Level / (double)Calibration.HalfLuminance, (double)Calibration.Steepness);
    if (Ratio >= NIGHT_VISION_FULL_COLOUR_RATIO)
    {
        return 0.0f;
    }
    return (float)(ClampUnit((double)strength) / (1.0 + Ratio));
}

void NightVision_GetRodTint(float blueShift, float outTint[3])
{
    // The night blue at luminance 1 in CIE XYZ, into linear sRGB (IEC 61966-2-1's matrix), then scaled to a Rec. 709
    // luminance of exactly 1 (the shaders' weights; the matrix's own rounding leaves it 1.00005).
    double X = NIGHT_VISION_BLUE_X / NIGHT_VISION_BLUE_Y;
    double Z = (1.0 - NIGHT_VISION_BLUE_X - NIGHT_VISION_BLUE_Y) / NIGHT_VISION_BLUE_Y;
    double NightBlue[3] =
    {
        3.2404542 * X - 1.5371385 - 0.4985314 * Z,
        -0.9692660 * X + 1.8760108 + 0.0415560 * Z,
        0.0556434 * X - 0.2040259 + 1.0572252 * Z
    };
    double Luminance = 0.2126 * NightBlue[0] + 0.7152 * NightBlue[1] + 0.0722 * NightBlue[2];
    double Shift = ClampUnit((double)blueShift);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        outTint[Channel] = (float)((1.0 - Shift) + (Shift * NightBlue[Channel] / Luminance));
    }
}
