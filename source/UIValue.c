#include "UIValue.h"
#include <float.h>
#include "UIKit.h"
#include "UIScreen.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Macros.
/** How far from a whole step, in steps, a value is still on it: float noise (0.1 x 3) does not make it lie between two. */
#define ON_STEP_TOLERANCE 1e-9
/** e, the base of the natural logarithm: a log conversion runs through logarithms, so a range of any width converts without an overflow. */
#define NATURAL_BASE 2.718281828459045
/** The smallest step against the size of the limits, 2^-50: four doubles' spacing, so each step moves a value there. */
#define STEP_RESOLUTION 8.8817841970012523e-16


// Static functions: small helpers.
static bool IsFinite(double value)
{
    return !Math_IsNaNDouble(value) && !Math_IsInfinityDouble(value);
}

/* Clamps into [0;1]; a NaN gives 0. */
static double ClampShare(double share)
{
    return (share > 0.0) ? ((share < 1.0) ? share : 1.0) : 0.0;
}

/* Clamps into [lower, upper]; a NaN gives lower. */
static double ClampTo(double value, double lower, double upper)
{
    return (value > lower) ? ((value < upper) ? value : upper) : lower;
}

static double GetLowerLimit(const UIValueSettings* settings)
{
    return settings->HasMinimum ? settings->Minimum : -DBL_MAX;
}

static double GetUpperLimit(const UIValueSettings* settings)
{
    return settings->HasMaximum ? settings->Maximum : DBL_MAX;
}

/* The power of ten at or below a value's size, at least 1: what a scrub with no range and a coarse snap count by. */
static double GetMagnitude(double value)
{
    double Size = IsFinite(value) ? Math_AbsDouble(value) : 0.0;
    if (!(Size >= 10.0))
    {
        return 1.0;
    }
    double Exponent = Math_FloorDouble(Math_LogDouble(Size, 10.0));
    double Magnitude = Math_PowDouble(10.0, Exponent);
    // log10 of an exact power of ten may come out a hair below it.
    if (IsFinite(Magnitude * 10.0) && ((Magnitude * 10.0) <= Size))
    {
        Magnitude *= 10.0;
    }
    return IsFinite(Magnitude) ? Magnitude : 1.0;
}

/* A tenth of the power of ten of a span: 0.1 for a span of 1 to 10, 1 for 10 to 100. */
static double GetDecimalIncrement(double span)
{
    if (!(span > 0.0) || !IsFinite(span))
    {
        return 1.0;
    }
    double Increment = Math_PowDouble(10.0, Math_FloorDouble(Math_LogDouble(span, 10.0)) - 1.0);
    if (IsFinite(Increment * 100.0) && ((Increment * 100.0) <= span))
    {
        Increment *= 10.0;
    }
    return ((Increment > 0.0) && IsFinite(Increment)) ? Increment : 1.0;
}


// Static functions: a value list.
/* The index of the first list value at or above a value; the count when none is. */
static size_t FindListPosition(const UIValueSettings* settings, double value)
{
    size_t Low = 0U;
    size_t High = settings->ValueCount;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (settings->ValueList[Middle] < value)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    return Low;
}

/* The index of the list value nearest a value; halfway between two, the upper one. */
static size_t FindNearestListIndex(const UIValueSettings* settings, double value)
{
    size_t Count = settings->ValueCount;
    if (Math_IsNaNDouble(value))
    {
        return 0U;
    }
    size_t Position = FindListPosition(settings, value);
    if (Position == 0U)
    {
        return 0U;
    }
    if (Position >= Count)
    {
        return Count - 1U;
    }
    double Below = value - settings->ValueList[Position - 1U];
    double Above = settings->ValueList[Position] - value;
    return (Below < Above) ? (Position - 1U) : Position;
}


// Static functions: steps.
/* A value snapped onto whole steps from an origin, then clamped; @p upper counts as a step when @p isUpperAStep, so an upper limit that is
 * no whole step stays reachable (a lower one is always a step: the origin, or a soft end the rules keep on the steps). A step too small to
 * count with is no step. */
static double SnapToStepFrom(double value, double origin, double lower, double upper, double step, bool isUpperAStep)
{
    double Value = ClampTo(value, lower, upper);
    double Steps = (Value - origin) / step;
    if (!(step > 0.0) || !IsFinite(step) || !IsFinite(Steps))
    {
        return Value;
    }
    double Count = Math_FloorDouble(Steps + 0.5);
    double Snapped = origin + (Count * step);
    if (!IsFinite(Snapped))
    {
        // The nearest step lies past the largest double: the one before it, towards the origin (between the origin and the value).
        Count += (Count < 0.0) ? 1.0 : -1.0;
        Snapped = origin + (Count * step);
    }
    if (isUpperAStep && ((Snapped >= upper) || ((upper - Value) < Math_AbsDouble(Value - Snapped))))
    {
        return upper;
    }
    return ClampTo(Snapped, lower, upper);
}

/* Moves a value by whole steps from an origin, within [lower, upper]: from between two steps, the next one that way; @p count rounded
 * to whole steps. */
static double StepFromOrigin(double value, double origin, double lower, double upper, double step, double count)
{
    double Value = ClampTo(value, lower, upper);
    double Steps = Math_FloorDouble(Math_AbsDouble(count) + 0.5);
    double Position = (Value - origin) / step;
    if ((Steps == 0.0) || !IsFinite(Position) || !(step > 0.0) || !IsFinite(step))
    {
        return Value;
    }
    double OnStep = Math_FloorDouble(Position + 0.5);
    bool IsOnStep = Math_AbsDouble(Position - OnStep) <= ON_STEP_TOLERANCE;
    double Target = 0.0;
    if (count > 0.0)
    {
        Target = (IsOnStep ? OnStep : Math_FloorDouble(Position)) + Steps;
    }
    else
    {
        Target = (IsOnStep ? OnStep : Math_CeilDouble(Position)) - Steps;
    }
    double Next = origin + (Target * step);
    return (Next <= lower) ? lower : ((Next >= upper) ? upper : Next);
}

/* Whether a value moves by whole steps (a step, or whole numbers), and the step and its origin. */
static bool TryGetStep(const UIValueSettings* settings, double* outStep, double* outOrigin)
{
    if (!settings->HasStep && !settings->IsInteger)
    {
        return false;
    }
    *outStep = settings->HasStep ? settings->Step : 1.0;
    if (!settings->HasMinimum)
    {
        *outOrigin = 0.0;
    }
    else
    {
        *outOrigin = settings->IsInteger ? Math_CeilDouble(settings->Minimum) : settings->Minimum;
    }
    return true;
}

/* The limits a whole-number value keeps to inside [lower, upper]. */
static void GetWholeLimits(const UIValueSettings* settings, double lower, double upper, double* outLower, double* outUpper)
{
    *outLower = settings->IsInteger ? Math_CeilDouble(lower) : lower;
    *outUpper = settings->IsInteger ? Math_FloorDouble(upper) : upper;
    if (*outLower > *outUpper)
    {
        // A range around a value can be too narrow for a whole number: its ends stand.
        *outLower = lower;
        *outUpper = upper;
    }
}

/* Whether a step can be counted with at the size of the limits: a normal double, at least STEP_RESOLUTION of the largest limit (a step
 * below a few doubles' spacing there moves nothing). With no limit, any normal step counts. */
static bool IsStepCountable(const UIValueSettings* settings)
{
    if (!(settings->Step >= DBL_MIN))
    {
        return false;
    }
    double Size = 0.0;
    Size = settings->HasMinimum ? Math_MaxDouble(Size, Math_AbsDouble(settings->Minimum)) : Size;
    Size = settings->HasMaximum ? Math_MaxDouble(Size, Math_AbsDouble(settings->Maximum)) : Size;
    Size = settings->HasSoftRange ? Math_MaxDouble(Size, Math_MaxDouble(Math_AbsDouble(settings->SoftMinimum), Math_AbsDouble(settings->SoftMaximum)))
        : Size;
    return settings->Step >= (Size * STEP_RESOLUTION);
}

/* Whether a value lies on the whole steps from their origin (within float noise) or on a hard limit; true for a value with no steps. */
static bool IsOnStepGrid(const UIValueSettings* settings, double value)
{
    double Step = 0.0;
    double Origin = 0.0;
    if (!TryGetStep(settings, &Step, &Origin) || (settings->HasMinimum && (value == settings->Minimum))
        || (settings->HasMaximum && (value == settings->Maximum)))
    {
        return true;
    }
    double Steps = (value - Origin) / Step;
    return IsFinite(Steps) && (Math_AbsDouble(Steps - Math_FloorDouble(Steps + 0.5)) <= ON_STEP_TOLERANCE);
}

/* A raw value snapped onto the list, the step or whole numbers, inside [lower, upper], whose ends count as steps. */
static double SnapInto(const UIValueSettings* settings, double lower, double upper, double value)
{
    double Value = ClampTo(value, lower, upper);
    if (settings->ValueCount > 0U)
    {
        return ClampTo(settings->ValueList[FindNearestListIndex(settings, Value)], lower, upper);
    }
    double WholeLower = lower;
    double WholeUpper = upper;
    GetWholeLimits(settings, lower, upper, &WholeLower, &WholeUpper);
    double Step = 0.0;
    double Origin = 0.0;
    if (settings->HasStep && TryGetStep(settings, &Step, &Origin))
    {
        return SnapToStepFrom(Value, Origin, WholeLower, WholeUpper, Step, true);
    }
    if (settings->IsInteger)
    {
        return ClampTo(Math_FloorDouble(Value + 0.5), WholeLower, WholeUpper);
    }
    return Value;
}


// Static functions: conversions.
/* A share of [lower, upper] through the conversion, the ends exact. A corrupted conversion keeps the linear value; a custom one's answer
 * is checked by SnapInto, the only caller's next step (a NaN is the lower end, the rest clamped). */
static double ConvertFromShare(const UIValueSettings* settings, double lower, double upper, double share)
{
    if (share <= 0.0)
    {
        return lower;
    }
    if (share >= 1.0)
    {
        return upper;
    }
    double Value = lower + (share * (upper - lower));
    switch (settings->Conversion)
    {
        case UIValueConversion_Linear:
        case UIValueConversion_Even:
            break;
        case UIValueConversion_Log:
            if ((lower > 0.0) && (upper > 0.0))
            {
                double LogLower = Math_LogNaturalDouble(lower);
                Value = Math_PowDouble(NATURAL_BASE, LogLower + (share * (Math_LogNaturalDouble(upper) - LogLower)));
            }
            break;
        case UIValueConversion_Power:
            Value = lower + ((upper - lower) * Math_PowDouble(share, settings->Exponent));
            break;
        case UIValueConversion_Custom:
            if (settings->Custom.ToValue != NULL)
            {
                Value = settings->Custom.ToValue(share, lower, upper, &settings->Custom.Data);
            }
            break;
    }
    return Value;
}

/* A value's share of [lower, upper] through the conversion, in [0;1]. */
static double ConvertToShare(const UIValueSettings* settings, double lower, double upper, double value)
{
    if (!(upper > lower))
    {
        return 0.0;
    }
    double Value = ClampTo(value, lower, upper);
    double Share = (Value - lower) / (upper - lower);
    switch (settings->Conversion)
    {
        case UIValueConversion_Linear:
        case UIValueConversion_Even:
            break;
        case UIValueConversion_Log:
            if ((lower > 0.0) && (upper > 0.0))
            {
                double LogLower = Math_LogNaturalDouble(lower);
                Share = (Math_LogNaturalDouble(Value) - LogLower) / (Math_LogNaturalDouble(upper) - LogLower);
            }
            break;
        case UIValueConversion_Power:
            Share = Math_PowDouble(Share, 1.0 / settings->Exponent);
            break;
        case UIValueConversion_Custom:
            if (settings->Custom.ToShare != NULL)
            {
                Share = settings->Custom.ToShare(Value, lower, upper, &settings->Custom.Data);
            }
            break;
    }
    return ClampShare(Share);
}


// Public functions: helpers.
UIValueSettings UIValueSettings_Create(void)
{
    UIValueSettings Settings;
    Memory_Zero(&Settings, sizeof(Settings));
    Settings.Conversion = UIValueConversion_Linear;
    Settings.Exponent = 2.0;
    Settings.Custom.Data = UserData_CreateEmpty();
    Settings.FineFactor = UI_VALUE_DEFAULT_FINE_FACTOR;
    return Settings;
}

UIValueSettings UIValueSettings_CreateRange(double minimum, double maximum)
{
    UIValueSettings Settings = UIValueSettings_Create();
    Settings.HasMinimum = true;
    Settings.Minimum = minimum;
    Settings.HasMaximum = true;
    Settings.Maximum = maximum;
    return Settings;
}

UIValueOptions UIValueOptions_Create(void)
{
    return UIValueOptions_CreateRange(0.0, 1.0, 0.0);
}

UIValueOptions UIValueOptions_CreateRange(double minimum, double maximum, double value)
{
    UIValueOptions Options;
    Memory_Zero(&Options, sizeof(Options));
    Options.Settings = UIValueSettings_CreateRange(minimum, maximum);
    Options.Value = value;
    Options.IsEnabled = true;
    Options.IsMixed = false;
    return Options;
}


// Public functions: the arithmetic.
bool UIValueSettings_IsValid(const UIValueSettings* settings, bool requiresRange, const char** outReason)
{
    const char* Reason = NULL;
    const UIValueSettings* S = settings;
    bool HasRange = S->HasMinimum && S->HasMaximum;
    if ((S->HasMinimum && !IsFinite(S->Minimum)) || (S->HasMaximum && !IsFinite(S->Maximum)))
    {
        Reason = "a limit is not a finite number";
    }
    else if (HasRange && (!(S->Minimum < S->Maximum) || !IsFinite(S->Maximum - S->Minimum)))
    {
        Reason = "the maximum must be above the minimum, with a finite span";
    }
    else if (S->HasSoftRange && (!IsFinite(S->SoftMinimum) || !IsFinite(S->SoftMaximum) || !(S->SoftMinimum < S->SoftMaximum)
        || !IsFinite(S->SoftMaximum - S->SoftMinimum)))
    {
        Reason = "the soft range's upper end must be above its lower end, both finite, with a finite span";
    }
    else if (S->HasSoftRange && ((S->HasMinimum && (S->SoftMinimum < S->Minimum)) || (S->HasMaximum && (S->SoftMaximum > S->Maximum))))
    {
        Reason = "the soft range must lie inside the hard limits";
    }
    else if (S->HasStep && (!IsFinite(S->Step) || !(S->Step > 0.0)))
    {
        Reason = "the step must be a finite number above 0";
    }
    else if (S->HasStep && S->IsInteger && (!(S->Step >= 1.0) || (Math_FloorDouble(S->Step) != S->Step)))
    {
        Reason = "a whole-number value's step must be a whole number";
    }
    else if (S->HasStep && !IsStepCountable(S))
    {
        Reason = "the step must be a normal double, and at least 2^-50 of the largest limit's size (so steps can be told apart there)";
    }
    else if (S->HasSoftRange && (!IsOnStepGrid(S, S->SoftMinimum) || !IsOnStepGrid(S, S->SoftMaximum)))
    {
        Reason = "the soft range's ends must lie on the steps (or whole numbers) or on a hard limit";
    }
    else if ((S->ValueCount > 0U) && S->HasStep)
    {
        Reason = "a value has a step or a value list, not both";
    }
    else if ((S->ValueCount > 0U) && S->HasSoftRange)
    {
        Reason = "a value list takes no soft range";
    }
    else if ((S->ValueCount > 0U) && ((S->ValueList == NULL) || (S->ValueCount > UI_VALUE_MAX_LIST_COUNT)))
    {
        Reason = "a value list must be given and hold at most UI_VALUE_MAX_LIST_COUNT values";
    }
    else if (S->IsInteger && HasRange && (Math_CeilDouble(S->Minimum) > Math_FloorDouble(S->Maximum)))
    {
        Reason = "the limits hold no whole number";
    }
    else if (!(S->FineFactor > 0.0) || !(S->FineFactor <= 1.0))
    {
        Reason = "the fine factor must lie within (0, 1]";
    }
    else if (S->HasDefault && !IsFinite(S->Default))
    {
        Reason = "the default must be a finite number";
    }
    for (size_t Index = 0U; (Reason == NULL) && (Index < S->ValueCount); Index++)
    {
        double Value = S->ValueList[Index];
        if (!IsFinite(Value))
        {
            Reason = "a value of the list is not a finite number";
        }
        else if ((Index > 0U) && !(Value > S->ValueList[Index - 1U]))
        {
            Reason = "a value list must rise strictly (no value twice)";
        }
        else if ((S->HasMinimum && (Value < S->Minimum)) || (S->HasMaximum && (Value > S->Maximum)))
        {
            Reason = "a value of the list lies outside the limits";
        }
        else if (S->IsInteger && (Math_FloorDouble(Value) != Value))
        {
            Reason = "a whole-number value's list must hold whole numbers";
        }
    }
    double Lower = 0.0;
    double Upper = 0.0;
    bool HasDragRange = (Reason == NULL) && UIValueSettings_TryGetDragRange(S, &Lower, &Upper);
    if (Reason == NULL)
    {
        bool IsConversionKnown = false;
        switch (S->Conversion)
        {
            case UIValueConversion_Linear:
                IsConversionKnown = true;
                break;
            case UIValueConversion_Log:
                IsConversionKnown = true;
                if (!S->HasMinimum || !(S->Minimum > 0.0) || !HasDragRange)
                {
                    Reason = "a log conversion needs a minimum above 0 and a range";
                }
                break;
            case UIValueConversion_Power:
                IsConversionKnown = true;
                if (!IsFinite(S->Exponent) || !(S->Exponent >= (1.0 / UI_VALUE_MAX_EXPONENT)) || !(S->Exponent <= UI_VALUE_MAX_EXPONENT)
                    || !HasDragRange)
                {
                    Reason = "a power conversion needs a range and an exponent within [1 / UI_VALUE_MAX_EXPONENT, UI_VALUE_MAX_EXPONENT]";
                }
                break;
            case UIValueConversion_Even:
                IsConversionKnown = true;
                if (S->ValueCount < 2U)
                {
                    Reason = "an even conversion needs a value list of at least two values";
                }
                break;
            case UIValueConversion_Custom:
                IsConversionKnown = true;
                if ((S->Custom.ToValue == NULL) || (S->Custom.ToShare == NULL) || !HasDragRange)
                {
                    Reason = "a custom conversion needs both functions and a range";
                }
                break;
        }
        if (!IsConversionKnown && (Reason == NULL))
        {
            Reason = "the conversion is not one of UIValueConversion";
        }
    }
    if ((Reason == NULL) && requiresRange && !HasDragRange)
    {
        Reason = "it needs both ends of a range";
    }
    if (outReason != NULL)
    {
        *outReason = Reason;
    }
    return Reason == NULL;
}

bool UIValueSettings_TryGetDragRange(const UIValueSettings* settings, double* outMinimum, double* outMaximum)
{
    if (settings->HasSoftRange)
    {
        *outMinimum = settings->SoftMinimum;
        *outMaximum = settings->SoftMaximum;
        return true;
    }
    if (settings->ValueCount >= 2U)
    {
        *outMinimum = settings->ValueList[0];
        *outMaximum = settings->ValueList[settings->ValueCount - 1U];
        return true;
    }
    if (settings->HasMinimum && settings->HasMaximum)
    {
        *outMinimum = settings->Minimum;
        *outMaximum = settings->Maximum;
        return true;
    }
    return false;
}

bool UIValueSettings_GetDragRangeAround(const UIValueSettings* settings, double value, double* outMinimum, double* outMaximum)
{
    if (!UIValueSettings_TryGetDragRange(settings, outMinimum, outMaximum))
    {
        return false;
    }
    if (settings->HasSoftRange && IsFinite(value))
    {
        double Value = ClampTo(value, GetLowerLimit(settings), GetUpperLimit(settings));
        *outMinimum = (Value < *outMinimum) ? Value : *outMinimum;
        *outMaximum = (Value > *outMaximum) ? Value : *outMaximum;
    }
    return true;
}

double UIValueSettings_Normalize(const UIValueSettings* settings, double value)
{
    double Lower = GetLowerLimit(settings);
    double Upper = GetUpperLimit(settings);
    double Value = value;
    if (Math_IsNaNDouble(Value))
    {
        Value = settings->HasMinimum ? settings->Minimum : ClampTo(0.0, Lower, Upper);
    }
    Value = ClampTo(Value, Lower, Upper);
    if (settings->ValueCount > 0U)
    {
        return settings->ValueList[FindNearestListIndex(settings, Value)];
    }
    double WholeLower = Lower;
    double WholeUpper = Upper;
    GetWholeLimits(settings, Lower, Upper, &WholeLower, &WholeUpper);
    double Step = 0.0;
    double Origin = 0.0;
    if (settings->HasStep && TryGetStep(settings, &Step, &Origin))
    {
        // The upper limit counts as a step; with no lower limit the steps count from 0 and nothing below is a limit.
        return SnapToStepFrom(Value, Origin, WholeLower, WholeUpper, Step, settings->HasMaximum);
    }
    if (settings->IsInteger)
    {
        return ClampTo(Math_FloorDouble(Value + 0.5), WholeLower, WholeUpper);
    }
    return Value;
}

double UIValueSettings_FromShare(const UIValueSettings* settings, double minimum, double maximum, double share)
{
    double Share = ClampShare(share);
    if ((settings->Conversion == UIValueConversion_Even) && (settings->ValueCount >= 2U))
    {
        double Last = (double)(settings->ValueCount - 1U);
        size_t Index = (size_t)Math_FloorDouble((Share * Last) + 0.5);
        Index = (Index < settings->ValueCount) ? Index : (settings->ValueCount - 1U);
        return settings->ValueList[Index];
    }
    if (!(maximum > minimum))
    {
        return SnapInto(settings, minimum, minimum, minimum);
    }
    return SnapInto(settings, minimum, maximum, ConvertFromShare(settings, minimum, maximum, Share));
}

double UIValueSettings_ToShare(const UIValueSettings* settings, double minimum, double maximum, double value)
{
    // A NaN is the lower end: the nearest list index and the clamp both read it so.
    if ((settings->Conversion == UIValueConversion_Even) && (settings->ValueCount >= 2U))
    {
        return (double)FindNearestListIndex(settings, value) / (double)(settings->ValueCount - 1U);
    }
    return ConvertToShare(settings, minimum, maximum, value);
}

double UIValueSettings_Step(const UIValueSettings* settings, double value, double count, bool isFine)
{
    double Lower = GetLowerLimit(settings);
    double Upper = GetUpperLimit(settings);
    double Value = Math_IsNaNDouble(value) ? (settings->HasMinimum ? settings->Minimum : ClampTo(0.0, Lower, Upper)) : ClampTo(value, Lower, Upper);
    if (Math_IsNaNDouble(count) || (count == 0.0))
    {
        return Value;
    }
    double RangeLower = 0.0;
    double RangeUpper = 0.0;
    bool HasRange = UIValueSettings_GetDragRangeAround(settings, Value, &RangeLower, &RangeUpper);
    if (HasRange)
    {
        Lower = RangeLower;
        Upper = RangeUpper;
    }
    if (settings->ValueCount > 0U)
    {
        double Steps = Math_FloorDouble(Math_AbsDouble(count) + 0.5);
        if (Steps == 0.0)
        {
            return Value;
        }
        size_t Count = settings->ValueCount;
        size_t Position = FindListPosition(settings, Value);
        bool IsOnValue = (Position < Count) && (settings->ValueList[Position] == Value);
        double From = (double)Position;
        if (count > 0.0)
        {
            From = IsOnValue ? (double)Position : ((double)Position - 1.0);
        }
        double Target = (count > 0.0) ? (From + Steps) : (From - Steps);
        Target = ClampTo(Target, 0.0, (double)(Count - 1U));
        return settings->ValueList[(size_t)Target];
    }
    double Step = 0.0;
    double Origin = 0.0;
    if (TryGetStep(settings, &Step, &Origin))
    {
        // A fine step on a stepped value is still a whole step: smaller would snap back to where it was.
        double WholeLower = Lower;
        double WholeUpper = Upper;
        GetWholeLimits(settings, Lower, Upper, &WholeLower, &WholeUpper);
        return StepFromOrigin(Value, Origin, WholeLower, WholeUpper, Step, count);
    }
    double Factor = isFine ? settings->FineFactor : 1.0;
    if (HasRange)
    {
        double Share = UIValueSettings_ToShare(settings, Lower, Upper, Value);
        if (Math_IsInfinityDouble(count))
        {
            return (count > 0.0) ? Upper : Lower;
        }
        return UIValueSettings_FromShare(settings, Lower, Upper, Share + (count * UI_VALUE_KEY_STEP_SHARE * Factor));
    }
    // No range, step or whole numbers: a hundredth of the value's magnitude.
    double Next = Value + (count * GetMagnitude(Value) * UI_VALUE_KEY_STEP_SHARE * Factor);
    if (!IsFinite(Next))
    {
        return (count > 0.0) ? Upper : Lower;
    }
    return ClampTo(Next, Lower, Upper);
}

double UIValueSettings_GetScrubRate(const UIValueSettings* settings, double value)
{
    if (settings->HasStep && IsFinite(settings->Step) && (settings->Step > 0.0))
    {
        return settings->Step / UI_VALUE_SCRUB_PIXELS_PER_STEP;
    }
    if (settings->IsInteger)
    {
        return 1.0 / UI_VALUE_SCRUB_PIXELS_PER_STEP;
    }
    return GetMagnitude(value) / UI_VALUE_SCRUB_PIXELS_PER_MAGNITUDE;
}

double UIValueSettings_SnapCoarse(const UIValueSettings* settings, double minimum, double maximum, bool hasRange, double value,
    double magnitudeValue)
{
    double Lower = hasRange ? minimum : GetLowerLimit(settings);
    double Upper = hasRange ? maximum : GetUpperLimit(settings);
    double Value = Math_IsNaNDouble(value) ? Lower : ClampTo(value, Lower, Upper);
    if (settings->ValueCount > 0U)
    {
        return hasRange ? SnapInto(settings, Lower, Upper, Value) : UIValueSettings_Normalize(settings, Value);
    }
    if (hasRange && (settings->Conversion != UIValueConversion_Linear))
    {
        double Share = UIValueSettings_ToShare(settings, Lower, Upper, Value);
        double Snapped = Math_FloorDouble((Share / UI_VALUE_COARSE_SHARE) + 0.5) * UI_VALUE_COARSE_SHARE;
        return UIValueSettings_FromShare(settings, Lower, Upper, Snapped);
    }
    double Increment = hasRange ? GetDecimalIncrement(Upper - Lower) : (GetMagnitude(magnitudeValue) / 10.0);
    double Step = 0.0;
    double Origin = 0.0;
    if (TryGetStep(settings, &Step, &Origin))
    {
        double Multiple = Math_FloorDouble((Increment / Step) + 0.5);
        Increment = Step * ((Multiple >= 1.0) ? Multiple : 1.0);
    }
    else
    {
        Origin = 0.0;
    }
    double Units = (Value - Origin) / Increment;
    double Snapped = IsFinite(Units) ? (Origin + (Math_FloorDouble(Units + 0.5) * Increment)) : Value;
    return hasRange ? SnapInto(settings, Lower, Upper, Snapped) : UIValueSettings_Normalize(settings, Snapped);
}

double UIValue_ClampAndSnapToStep(double value, double minimum, double maximum, bool hasStep, double step)
{
    if (Math_IsNaNDouble(value) || (value <= minimum))
    {
        return minimum;
    }
    if (value >= maximum)
    {
        return maximum;
    }
    bool IsStepUsable = hasStep && IsFinite(step) && (step > 0.0);
    double Steps = IsStepUsable ? ((value - minimum) / step) : 0.0;
    if (!IsStepUsable || !IsFinite(Steps))
    {
        return value;
    }
    double Snapped = minimum + (Math_FloorDouble(Steps + 0.5) * step);
    if ((Snapped >= maximum) || ((maximum - value) < Math_AbsDouble(value - Snapped)))
    {
        return maximum; // Past the last whole step the end counts as one.
    }
    return Snapped;
}

double UIValue_GetSteppedFrom(double value, double minimum, double maximum, bool hasStep, double step, double count)
{
    double Value = UIValue_ClampAndSnapToStep(value, minimum, maximum, false, 0.0);
    double Count = Math_IsNaNDouble(count) ? 0.0 : count;
    if (!hasStep || !IsFinite(step) || !(step > 0.0))
    {
        return UIValue_ClampAndSnapToStep(Value + (Count * (maximum - minimum) * UI_VALUE_KEY_STEP_SHARE), minimum, maximum, false, 0.0);
    }
    return StepFromOrigin(Value, minimum, minimum, maximum, step, Count);
}

double UIValue_GetFraction(double value, double minimum, double maximum)
{
    if (!(maximum > minimum))
    {
        return 0.0;
    }
    return ClampShare((value - minimum) / (maximum - minimum));
}

double UIValue_GetPointerFraction(double pointer, double boxLength, double thumbLength)
{
    return ClampShare(UIValue_GetUnclampedPointerFraction(pointer, boxLength, thumbLength));
}

double UIValue_GetUnclampedPointerFraction(double pointer, double boxLength, double thumbLength)
{
    double Travel = boxLength - thumbLength;
    if (!(Travel > 0.0) || !IsFinite(Travel) || !IsFinite(pointer) || !IsFinite(thumbLength))
    {
        return 0.0;
    }
    double Fraction = (pointer - (thumbLength * 0.5)) / Travel;
    return IsFinite(Fraction) ? Fraction : 0.0;
}


// Static functions: the capability.
/* Copies a list into owned storage; NULL for none. */
static double* CopyList(const double* values, size_t count)
{
    if (count == 0U)
    {
        return NULL;
    }
    double* Copy = Memory_Allocate(count * sizeof(double));
    Memory_Copy(values, Copy, count * sizeof(double));
    return Copy;
}

/* Whether a change in progress has changed anything since it began: the number, or several values made one (or one made several). */
static bool HasChangedSinceStart(const UIValueCapability* self)
{
    return (self->_value != self->_changeStartValue) || (self->_isMixed != self->_changeStartIsMixed);
}

/* The widget its value events are posted under: the event target while it is alive, else its own. */
static uint64_t GetEventWidgetId(const UIValueCapability* self, Widget* widget)
{
    if (self->_eventTargetId != WIDGET_ID_INVALID)
    {
        Widget* Target = NULL;
        if (UIScreen_TryGetWidgetById(Widget_GetScreen(widget), self->_eventTargetId, &Target))
        {
            return self->_eventTargetId;
        }
    }
    return Widget_GetId(widget);
}

static Error PostValue(const UIValueCapability* self, Widget* widget, UIElementEvent event)
{
    UIPropertyValue Value = UIPropertyValue_FromDouble(self->_value);
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t Id = GetEventWidgetId(self, widget);
    if (self->_hasEventIndex && (Id != Widget_GetId(widget)))
    {
        return UIScreen_PostWidgetIndexedEvent(Screen, Id, (int32_t)event, &Value, self->_eventIndex);
    }
    return UIScreen_PostWidgetEvent(Screen, Id, (int32_t)event, &Value);
}

/* Tells the event target that this part moved, so a compound keeps its other parts in step. */
static Error NotifyTarget(const UIValueCapability* self, Widget* widget)
{
    if (self->_eventTargetId == WIDGET_ID_INVALID)
    {
        return Error_CreateSuccess();
    }
    uint64_t PartId = Widget_GetId(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    Widget* Target = NULL;
    UIValueCapability* TargetValue = NULL;
    Error Result = UIValue_Resolve(Screen, self->_eventTargetId, &Target, &TargetValue);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result); // A target that holds no value piece (a row of fields) hears nothing.
        return Error_CreateSuccess();
    }
    if ((TargetValue->_hooks == NULL) || (TargetValue->_hooks->OnPartChanged == NULL))
    {
        return Error_CreateSuccess();
    }
    return TargetValue->_hooks->OnPartChanged(Target, PartId);
}

/* The element hears what code changed, then a target it posts for. Code's value (not its settings, which keep a change's start inside
 * them) is where a change in progress now starts. The widget is found again by id after the hook. */
static Error HearChangeByCode(UIScreen* screen, uint64_t widgetId, bool isValueChange)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    if (isValueChange)
    {
        UIValueCapability_RebaseChange(Value);
    }
    if ((Value->_hooks != NULL) && (Value->_hooks->OnChangedByCode != NULL))
    {
        Result = Value->_hooks->OnChangedByCode(Target);
        if ((Result.Code != ErrorCode_Success) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
        {
            return Result;
        }
        Value = NULL;
        Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Error_CreateSuccess();
        }
    }
    return NotifyTarget(Value, Target);
}

/* The widget whose settings a widget's are: a compound's part's are its compound's (the parts follow it), any other's its own. */
static uint64_t GetSettingsOwnerId(UIScreen* screen, uint64_t widgetId, const UIValueCapability* value)
{
    if (value->_eventTargetId == WIDGET_ID_INVALID)
    {
        return widgetId;
    }
    Widget* Target = NULL;
    UIValueCapability* TargetValue = NULL;
    Error Result = UIValue_Resolve(screen, value->_eventTargetId, &Target, &TargetValue);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result); // A target that holds no value piece (a row of fields): the field's settings are its own.
        return widgetId;
    }
    return value->_eventTargetId;
}

/* Replaces a widget's settings with @p settings (its own, changed by a setter), refusing what breaks a rule; the element hears it. A
 * compound's part passes them to its compound, which gives them to both parts. */
static Error ChangeSettings(UIScreen* screen, uint64_t widgetId, const UIValueSettings* settings)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t OwnerId = GetSettingsOwnerId(screen, widgetId, Value);
    if (OwnerId != widgetId)
    {
        Result = UIValue_Resolve(screen, OwnerId, &Target, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Result = UIValueCapability_SetSettings(Value, settings);
    return (Result.Code == ErrorCode_Success) ? HearChangeByCode(screen, OwnerId, false) : Result;
}


// Public functions: the capability.
Error UIValueCapability_Construct(UIValueCapability* self, const UIValueOptions* options, const UIValueHooks* hooks, bool requiresRange)
{
    const char* Reason = NULL;
    if (!UIValueSettings_IsValid(&options->Settings, requiresRange, &Reason))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "UIValue: %s.", Reason);
    }
    if (!IsFinite(options->Value))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIValue: the value must be a finite number.");
    }
    Memory_Zero(self, sizeof(*self));
    self->_list = CopyList(options->Settings.ValueList, options->Settings.ValueCount);
    self->_settings = options->Settings;
    self->_settings.ValueList = self->_list;
    self->_value = UIValueSettings_Normalize(&self->_settings, options->Value);
    self->_isEnabled = options->IsEnabled;
    self->_isMixed = options->IsMixed;
    self->_requiresRange = requiresRange;
    self->_hooks = hooks;
    self->_eventTargetId = WIDGET_ID_INVALID;
    self->_changeStartValue = self->_value;
    self->_settingsRevision = 1U;
    return Error_CreateSuccess();
}

void UIValueCapability_Deconstruct(UIValueCapability* self)
{
    Memory_Free(self->_list);
    self->_list = NULL;
    self->_settings.ValueList = NULL;
    self->_settings.ValueCount = 0U;
}

Error UIValueCapability_SetSettings(UIValueCapability* self, const UIValueSettings* settings)
{
    const char* Reason = NULL;
    if (!UIValueSettings_IsValid(settings, self->_requiresRange, &Reason))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "UIValue: %s.", Reason);
    }
    // The list is copied before the old one goes: the caller's may be the piece's own.
    double* List = CopyList(settings->ValueList, settings->ValueCount);
    Memory_Free(self->_list);
    self->_list = List;
    self->_settings = *settings;
    self->_settings.ValueList = List;
    self->_value = UIValueSettings_Normalize(&self->_settings, self->_value);
    self->_changeStartValue = UIValueSettings_Normalize(&self->_settings, self->_changeStartValue);
    self->_settingsRevision++;
    return Error_CreateSuccess();
}

void UIValueCapability_SetEventTarget(UIValueCapability* self, uint64_t targetId, bool hasIndex, int32_t index)
{
    self->_eventTargetId = targetId;
    self->_hasEventIndex = hasIndex;
    self->_eventIndex = hasIndex ? index : 0;
}

void UIValueCapability_BeginChange(UIValueCapability* self)
{
    self->_isChanging = true;
    self->_changeStartValue = self->_value;
    self->_changeStartIsMixed = self->_isMixed;
}

void UIValueCapability_RebaseChange(UIValueCapability* self)
{
    if (self->_isChanging)
    {
        self->_changeStartValue = self->_value;
        self->_changeStartIsMixed = self->_isMixed;
    }
}

Error UIValueCapability_MoveTo(UIValueCapability* self, Widget* widget, double value, bool* outHasMoved)
{
    if (outHasMoved != NULL)
    {
        *outHasMoved = false;
    }
    // Several values made one is a change too, though the number is the one it held.
    if (!IsFinite(value) || ((value == self->_value) && !self->_isMixed))
    {
        return Error_CreateSuccess();
    }
    self->_value = value;
    self->_isMixed = false;
    if (outHasMoved != NULL)
    {
        *outHasMoved = true;
    }
    Error Result = PostValue(self, widget, UIElementEvent_Changed);
    return (Result.Code == ErrorCode_Success) ? NotifyTarget(self, widget) : Result;
}

Error UIValueCapability_EndChange(UIValueCapability* self, Widget* widget)
{
    if (!self->_isChanging)
    {
        return Error_CreateSuccess();
    }
    self->_isChanging = false;
    return HasChangedSinceStart(self) ? PostValue(self, widget, UIElementEvent_Committed) : Error_CreateSuccess();
}

Error UIValueCapability_CancelChange(UIValueCapability* self, Widget* widget)
{
    if (!self->_isChanging)
    {
        return Error_CreateSuccess();
    }
    self->_isChanging = false;
    bool HasMoved = HasChangedSinceStart(self);
    self->_value = self->_changeStartValue;
    self->_isMixed = self->_changeStartIsMixed;
    if (!HasMoved)
    {
        return Error_CreateSuccess();
    }
    Error Result = PostValue(self, widget, UIElementEvent_Cancelled);
    return (Result.Code == ErrorCode_Success) ? NotifyTarget(self, widget) : Result;
}

Error UIValueCapability_ApplyWholeChange(UIValueCapability* self, Widget* widget, double value, bool* outHasMoved)
{
    if (outHasMoved != NULL)
    {
        *outHasMoved = false;
    }
    if (self->_isChanging || !IsFinite(value) || ((value == self->_value) && !self->_isMixed))
    {
        return Error_CreateSuccess();
    }
    self->_value = value;
    self->_isMixed = false;
    if (outHasMoved != NULL)
    {
        *outHasMoved = true;
    }
    Error Result = PostValue(self, widget, UIElementEvent_Changed);
    if (Result.Code == ErrorCode_Success)
    {
        Result = PostValue(self, widget, UIElementEvent_Committed);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElement_PostCue(widget, UIElementCue_ValueStep);
    }
    return (Result.Code == ErrorCode_Success) ? NotifyTarget(self, widget) : Result;
}

double UIValueCapability_GetShare(const UIValueCapability* self)
{
    double Lower = 0.0;
    double Upper = 0.0;
    if (!UIValueSettings_TryGetDragRange(&self->_settings, &Lower, &Upper))
    {
        return 0.0;
    }
    return UIValueSettings_ToShare(&self->_settings, Lower, Upper, self->_value);
}

bool UIValueCapability_HasDragRange(const UIValueCapability* self)
{
    double Lower = 0.0;
    double Upper = 0.0;
    return UIValueSettings_TryGetDragRange(&self->_settings, &Lower, &Upper);
}

void UIValueCapability_SetValueQuietly(UIValueCapability* self, double value)
{
    if (IsFinite(value))
    {
        self->_value = UIValueSettings_Normalize(&self->_settings, value);
    }
}

void UIValueCapability_SetStatesQuietly(UIValueCapability* self, bool isEnabled, bool isMixed)
{
    self->_isEnabled = isEnabled;
    self->_isMixed = isMixed;
}

Error UIValue_Resolve(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIValueCapability** outValue)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIValue: the widget is gone, or its screen has no kit.");
    }
    UIValueCapability* Value = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Value));
    if (Value == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIValue: the widget holds no value.");
    }
    *outWidget = Target;
    *outValue = Value;
    return Error_CreateSuccess();
}


// Public functions: calling code.
Error UIValue_Set(UIScreen* screen, uint64_t widgetId, double value)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsFinite(value))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIValue_Set: the value must be a finite number.");
    }
    Value->_value = UIValueSettings_Normalize(&Value->_settings, value);
    return HearChangeByCode(screen, widgetId, true);
}

Error UIValue_Get(UIScreen* screen, uint64_t widgetId, double* outValue)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code == ErrorCode_Success)
    {
        *outValue = Value->_value;
    }
    return Result;
}

Error UIValue_GetSettings(UIScreen* screen, uint64_t widgetId, UIValueSettings* outSettings)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code == ErrorCode_Success)
    {
        *outSettings = Value->_settings;
    }
    return Result;
}

Error UIValue_SetRange(UIScreen* screen, uint64_t widgetId, bool hasMinimum, double minimum, bool hasMaximum, double maximum)
{
    UIValueSettings Settings;
    Error Result = UIValue_GetSettings(screen, widgetId, &Settings);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Settings.HasMinimum = hasMinimum;
    Settings.Minimum = hasMinimum ? minimum : 0.0;
    Settings.HasMaximum = hasMaximum;
    Settings.Maximum = hasMaximum ? maximum : 0.0;
    return ChangeSettings(screen, widgetId, &Settings);
}

Error UIValue_SetSoftRange(UIScreen* screen, uint64_t widgetId, bool hasSoftRange, double minimum, double maximum)
{
    UIValueSettings Settings;
    Error Result = UIValue_GetSettings(screen, widgetId, &Settings);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Settings.HasSoftRange = hasSoftRange;
    Settings.SoftMinimum = hasSoftRange ? minimum : 0.0;
    Settings.SoftMaximum = hasSoftRange ? maximum : 0.0;
    return ChangeSettings(screen, widgetId, &Settings);
}

Error UIValue_SetStep(UIScreen* screen, uint64_t widgetId, bool hasStep, double step)
{
    UIValueSettings Settings;
    Error Result = UIValue_GetSettings(screen, widgetId, &Settings);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Settings.HasStep = hasStep;
    Settings.Step = hasStep ? step : 0.0;
    return ChangeSettings(screen, widgetId, &Settings);
}

Error UIValue_SetIsInteger(UIScreen* screen, uint64_t widgetId, bool isInteger)
{
    UIValueSettings Settings;
    Error Result = UIValue_GetSettings(screen, widgetId, &Settings);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Settings.IsInteger = isInteger;
    return ChangeSettings(screen, widgetId, &Settings);
}

Error UIValue_SetValueList(UIScreen* screen, uint64_t widgetId, const double* values, size_t count)
{
    if ((values == NULL) && (count > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIValue_SetValueList: the values must not be NULL with a count.");
    }
    UIValueSettings Settings;
    Error Result = UIValue_GetSettings(screen, widgetId, &Settings);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Settings.ValueList = (count > 0U) ? values : NULL;
    Settings.ValueCount = count;
    if ((count == 0U) && (Settings.Conversion == UIValueConversion_Even))
    {
        Settings.Conversion = UIValueConversion_Linear;
    }
    return ChangeSettings(screen, widgetId, &Settings);
}

Error UIValue_SetConversion(UIScreen* screen, uint64_t widgetId, UIValueConversion conversion, double exponent)
{
    if (conversion == UIValueConversion_Custom)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIValue_SetConversion: a custom conversion goes through UIValue_SetCustomConversion.");
    }
    UIValueSettings Settings;
    Error Result = UIValue_GetSettings(screen, widgetId, &Settings);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Settings.Conversion = conversion;
    if (conversion == UIValueConversion_Power)
    {
        Settings.Exponent = exponent;
    }
    return ChangeSettings(screen, widgetId, &Settings);
}

Error UIValue_SetCustomConversion(UIScreen* screen, uint64_t widgetId, UIValueToValue toValue, UIValueToShare toShare,
    const UserData* userData)
{
    if ((toValue == NULL) || (toShare == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIValue_SetCustomConversion: both functions are required.");
    }
    UIValueSettings Settings;
    Error Result = UIValue_GetSettings(screen, widgetId, &Settings);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Settings.Conversion = UIValueConversion_Custom;
    Settings.Custom.ToValue = toValue;
    Settings.Custom.ToShare = toShare;
    Settings.Custom.Data = (userData != NULL) ? *userData : UserData_CreateEmpty();
    return ChangeSettings(screen, widgetId, &Settings);
}

Error UIValue_SetDefault(UIScreen* screen, uint64_t widgetId, bool hasDefault, double value)
{
    UIValueSettings Settings;
    Error Result = UIValue_GetSettings(screen, widgetId, &Settings);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Settings.HasDefault = hasDefault;
    Settings.Default = hasDefault ? value : 0.0;
    return ChangeSettings(screen, widgetId, &Settings);
}

Error UIValue_ResetToDefault(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Value->_settings.HasDefault)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIValue_ResetToDefault: the value has no default.");
    }
    Value->_value = UIValueSettings_Normalize(&Value->_settings, Value->_settings.Default);
    return HearChangeByCode(screen, widgetId, true);
}

Error UIValue_SetFineFactor(UIScreen* screen, uint64_t widgetId, double fineFactor)
{
    UIValueSettings Settings;
    Error Result = UIValue_GetSettings(screen, widgetId, &Settings);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Settings.FineFactor = fineFactor;
    return ChangeSettings(screen, widgetId, &Settings);
}

Error UIValue_SetMixed(UIScreen* screen, uint64_t widgetId, bool isMixed)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Value->_isMixed = isMixed;
    return HearChangeByCode(screen, widgetId, true);
}

Error UIValue_IsMixed(UIScreen* screen, uint64_t widgetId, bool* outIsMixed)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code == ErrorCode_Success)
    {
        *outIsMixed = Value->_isMixed;
    }
    return Result;
}

Error UIValue_Step(UIScreen* screen, uint64_t widgetId, double count, bool isFine)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Value->_value = UIValueSettings_Step(&Value->_settings, Value->_value, count, isFine);
    return HearChangeByCode(screen, widgetId, true);
}

Error UIValue_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Value->_isEnabled = isEnabled;
    if ((Value->_hooks != NULL) && (Value->_hooks->OnEnabledChanged != NULL))
    {
        return Value->_hooks->OnEnabledChanged(Target);
    }
    return Error_CreateSuccess();
}
