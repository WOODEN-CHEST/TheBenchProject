#include <math.h>
#include "EyeAdaptation.h"
#include "NightVision.h"


// Macros.
/** The aspect the weights take for an unusable one. */
#define DEFAULT_ASPECT (16.0f / 9.0f)
/** The widest window: the meter's without an anchor. */
#define WIDEST_WINDOW 64.0f


// Static functions.
/* The centre weights along one side of the meter: a Gaussian of the blocks' centres' distances from the middle, in view
 * heights (@p extent: the side's length in them). The offsets are exact fractions, so mirrored blocks weigh exactly
 * alike. */
static void GetSideWeights(int count, float extent, float* outWeights)
{
    float Spread = 2.0f * EYE_ADAPTATION_CENTRE_SPREAD * EYE_ADAPTATION_CENTRE_SPREAD;
    for (int Index = 0; Index < count; Index++)
    {
        float Offset = ((float)(2 * Index + 1 - count) / (float)(2 * count)) * extent;
        outWeights[Index] = expf(-(Offset * Offset) / Spread);
    }
}


// Internal functions (declared in EyeAdaptation.h).
void EyeAdaptation_Construct(EyeAdaptation* self)
{
    self->_level = 0.0f;
    self->_target = 0.0f;
    self->_isStarted = false;
    self->_isMeasured = false;
}

float EyeAdaptation_Measure(const float* blocks, float aspect)
{
    float Aspect = (isfinite(aspect) && (aspect > 0.0f)) ? aspect : DEFAULT_ASPECT;
    float ColumnWeights[EYE_ADAPTATION_METER_WIDTH];
    float RowWeights[EYE_ADAPTATION_METER_HEIGHT];
    GetSideWeights(EYE_ADAPTATION_METER_WIDTH, Aspect, ColumnWeights);
    GetSideWeights(EYE_ADAPTATION_METER_HEIGHT, 1.0f, RowWeights);
    double Sum = 0.0;
    double WeightSum = 0.0;
    for (int Row = 0; Row < EYE_ADAPTATION_METER_HEIGHT; Row++)
    {
        for (int Column = 0; Column < EYE_ADAPTATION_METER_WIDTH; Column++)
        {
            float Value = blocks[Row * EYE_ADAPTATION_METER_WIDTH + Column];
            if (!isfinite(Value))
            {
                continue;
            }
            double Weight = (double)RowWeights[Row] * (double)ColumnWeights[Column];
            Sum += Weight * (double)Value;
            WeightSum += Weight;
        }
    }
    return (WeightSum > 0.0) ? (float)(Sum / WeightSum) : NAN;
}

float EyeAdaptation_GetAnchor(float sceneLuminance)
{
    if (!(sceneLuminance > 0.0f) || !isfinite(sceneLuminance))
    {
        return NAN;
    }
    return log2f(sceneLuminance * EYE_ADAPTATION_ANCHOR_SHARE);
}

EyeAdaptationWindow EyeAdaptation_GetWindow(float anchor, float nightLightLuminance)
{
    EyeAdaptationWindow Window = { .Low = -WIDEST_WINDOW, .SkyLow = -WIDEST_WINDOW, .High = WIDEST_WINDOW };
    if (isfinite(anchor))
    {
        Window.Low = anchor - EYE_ADAPTATION_WINDOW_BELOW;
        Window.High = anchor + EYE_ADAPTATION_WINDOW_ABOVE;
    }
    Window.SkyLow = Window.Low;
    if ((nightLightLuminance > 0.0f) && isfinite(nightLightLuminance))
    {
        Window.SkyLow = fminf(fmaxf(log2f(nightLightLuminance), Window.Low), Window.High);
    }
    return Window;
}

void EyeAdaptation_Update(EyeAdaptation* self, float anchor, float measurement, float deltaSeconds)
{
    if (isfinite(measurement))
    {
        self->_target = measurement;
        if (!self->_isMeasured)
        {
            // The first measurement sets the eye: no fade from the anchor as the game starts.
            self->_level = measurement;
            self->_isStarted = true;
            self->_isMeasured = true;
            return;
        }
    }
    else if (!self->_isMeasured)
    {
        if (isfinite(anchor))
        {
            self->_level = anchor;
            self->_isStarted = true;
        }
        return;
    }
    if (!(deltaSeconds > 0.0f))
    {
        return;
    }
    float Seconds = fminf(deltaSeconds, EYE_ADAPTATION_MAX_DELTA_SECONDS);
    float Step = self->_target - self->_level;
    float Rate = EYE_ADAPTATION_BASE_SPEED * (1.0f + EYE_ADAPTATION_STOP_GAIN * fabsf(Step));
    if (Step > 0.0f)
    {
        Rate *= EYE_ADAPTATION_BRIGHTEN_MULTIPLIER;
    }
    self->_level += Step * (1.0f - expf(-Seconds * Rate));
}

float EyeAdaptation_GetLevel(const EyeAdaptation* self)
{
    return self->_isStarted ? self->_level : NAN;
}

bool EyeAdaptation_IsMeasured(const EyeAdaptation* self)
{
    return self->_isMeasured;
}

double EyeAdaptation_GetRealScale(float dayLuminance, float nightLuminance)
{
    double Floor = NIGHT_VISION_NIGHT_CANDELAS / NIGHT_VISION_NIGHT_LUMINANCE;
    double Day = (dayLuminance > 0.0f) ? (double)dayLuminance : 0.0;
    double Night = (nightLuminance > 0.0f) ? (double)nightLuminance : 0.0;
    double Total = Day + Night;
    if (!(Total > 0.0))
    {
        return Floor;
    }
    // An infinite light gives infinity over infinity: NaN, which the floor takes too.
    double Scale = NightVision_GetRealFieldLuminance(dayLuminance, nightLuminance) / Total;
    return (Scale > Floor) ? Scale : Floor;
}

float EyeAdaptation_GetExposure(float level, double realScale)
{
    if (!isfinite(level))
    {
        return 1.0f;
    }
    double Floor = NIGHT_VISION_NIGHT_CANDELAS / NIGHT_VISION_NIGHT_LUMINANCE;
    double Scale = (isfinite(realScale) && (realScale > Floor)) ? realScale : Floor;
    // In stops: the screen level is KEY x (R / noon)^POWER for R = 2^level x Scale, the exposure that over 2^level.
    double Level = (double)level;
    double Exposure = log2((double)EYE_ADAPTATION_KEY)
        + (double)EYE_ADAPTATION_POWER * (Level + log2(Scale / NIGHT_VISION_NOON_CANDELAS)) - Level;
    double Minimum = log2((double)EYE_ADAPTATION_MIN_EXPOSURE);
    double Maximum = log2((double)EYE_ADAPTATION_MAX_EXPOSURE);
    Exposure = (Exposure < Minimum) ? Minimum : ((Exposure > Maximum) ? Maximum : Exposure);
    return (float)exp2(Exposure);
}
