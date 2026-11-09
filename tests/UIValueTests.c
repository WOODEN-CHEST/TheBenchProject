#include "TestSuites.h"
#include "TestUI.h"
#include "UIKit.h"
#include "wr/WRMemory.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
#define VALUE_THEME \
    "{\"name\": \"values\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"slider\": {\"min_size\": [64, 24], \"thumb_size\": [20, 20], \"track_thickness\": 4, \"track\": {\"kind\": \"flat\"}, \"fill\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"button\": {\"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#ffffff\"}}," \
    "\"menu\": {\"background\": {\"kind\": \"flat\"}, \"item\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#ffffff\"}, \"shortcut\": {\"normal\": \"#ffffff\"}, \"separator\": {\"kind\": \"flat\"}}}}"


// Types.
/* A clipboard of the tests' own. */
typedef struct ValueClipboardStruct
{
    char Text[512];
    /** How many times it was read. */
    size_t Reads;
} ValueClipboard;


// Static functions: helpers.
static bool IsNear(double value, double expected, double tolerance)
{
    return fabs(value - expected) <= tolerance;
}

static Error Clipboard_GetText(void* self, GenericBuffer* outText, size_t maxBytes)
{
    ValueClipboard* Clipboard = self;
    Clipboard->Reads++;
    size_t Length = strlen(Clipboard->Text);
    Length = (Length < maxBytes) ? Length : maxBytes;
    if ((Length > 0U) && !GenericBuffer_AppendRangeBytes(outText, (unsigned char*)Clipboard->Text, Length))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "test clipboard");
    }
    return Error_CreateSuccess();
}

static Error Clipboard_SetText(void* self, const unsigned char* text)
{
    ValueClipboard* Clipboard = self;
    snprintf(Clipboard->Text, sizeof(Clipboard->Text), "%s", (const char*)text);
    return Error_CreateSuccess();
}

static void Clipboard_Destroy(void* self)
{
    (void)self;
}

static const IClipboardVTable ClipboardVTable = { .GetText = Clipboard_GetText, .SetText = Clipboard_SetText, .Destroy = Clipboard_Destroy };

static bool Construct(TestRun* run, TestUIFixture* fixture, ValueClipboard* clipboard)
{
    if (!TestUI_Construct(run, fixture) || !TestUI_SetTheme(run, fixture, VALUE_THEME))
    {
        return false;
    }
    Memory_Zero(clipboard, sizeof(*clipboard));
    GameInput_SetClipboard(&fixture->Input, (IClipboard){ .VTable = &ClipboardVTable, .Self = clipboard });
    return true;
}

static uint64_t AddSlider(TestRun* run, TestUIFixture* fixture, const UIValueOptions* value)
{
    UISliderOptions Options = UISliderOptions_Create();
    Options.Value = *value;
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(100.0f, 100.0f, 220.0f, 24.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    TestUI_Tick(run, fixture);
    return Id;
}

static double Get(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    double Value = NAN;
    TEST_CHECK_SUCCESS(run, UIValue_Get(&fixture->Screen, id, &Value));
    return Value;
}

static bool IsValid(const UIValueSettings* settings)
{
    return UIValueSettings_IsValid(settings, false, NULL);
}

/* Writes a value's shortest text and reads it back. */
static bool RoundTrips(double value, bool isSinglePrecision)
{
    unsigned char Bytes[128];
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, Bytes, sizeof(Bytes), sizeof(unsigned char), 0U);
    Error Result = UIValue_WriteShortestText(value, isSinglePrecision, &Text);
    bool IsWritten = Result.Code == ErrorCode_Success;
    Error_Deconstruct(&Result);
    double Read = NAN;
    return IsWritten && UIValue_ParseText(Bytes, Text._count, NULL, isSinglePrecision, &Read) && (memcmp(&Read, &value, sizeof(double)) == 0);
}

static bool IsFixed(double value, int32_t precision, const char* expected)
{
    unsigned char Bytes[512];
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, Bytes, sizeof(Bytes), sizeof(unsigned char), 0U);
    Error Result = UIValue_WriteFixedText(value, precision, &Text);
    bool IsWritten = Result.Code == ErrorCode_Success;
    Error_Deconstruct(&Result);
    return IsWritten && (strcmp((const char*)Bytes, expected) == 0);
}

static bool Parses(const char* text, const unsigned char* unit, double expected)
{
    double Value = NAN;
    return UIValue_ParseText((const unsigned char*)text, strlen(text), unit, false, &Value) && (Value == expected);
}

static bool Refuses(const char* text, const unsigned char* unit)
{
    double Value = 12345.0;
    return !UIValue_ParseText((const unsigned char*)text, strlen(text), unit, false, &Value) && (Value == 12345.0);
}

/* A custom conversion that answers what the test says: a square's curve, a NaN, out of range, backwards, or one that expects what it is
 * promised (a share of 0.5 for a value outside its range, and a value just above the lower end). */
static double Custom_ToValue(double share, double minimum, double maximum, const UserData* userData)
{
    int32_t Mode = 0;
    Memory_Copy(userData->Bytes, &Mode, sizeof(Mode));
    switch (Mode)
    {
        case 1:
            return NAN;
        case 2:
            return maximum * 10.0;
        case 3:
            return maximum - (share * (maximum - minimum));
        case 4:
            return minimum + 1.0;
        default:
            return minimum + ((maximum - minimum) * share * share);
    }
}

static double Custom_ToShare(double value, double minimum, double maximum, const UserData* userData)
{
    int32_t Mode = 0;
    Memory_Copy(userData->Bytes, &Mode, sizeof(Mode));
    switch (Mode)
    {
        case 1:
            return NAN;
        case 2:
            return -4.0;
        case 3:
            return (maximum - value) / (maximum - minimum);
        case 4:
            return ((value < minimum) || (value > maximum)) ? 0.5 : ((value - minimum) / (maximum - minimum));
        default:
            return sqrt((value - minimum) / (maximum - minimum));
    }
}

static UIValueSettings CustomSettings(int32_t mode)
{
    UIValueSettings Settings = UIValueSettings_CreateRange(0.0, 100.0);
    Settings.Conversion = UIValueConversion_Custom;
    Settings.Custom.ToValue = Custom_ToValue;
    Settings.Custom.ToShare = Custom_ToShare;
    Settings.Custom.Data = UserData_CreateEmpty();
    Memory_Copy(&mode, Settings.Custom.Data.Bytes, sizeof(mode));
    return Settings;
}


// Tests: the rules.
static void EveryRuleIsCheckedAndOneThatBreaksIsRefused(TestRun* run)
{
    UIValueSettings Good = UIValueSettings_CreateRange(0.0, 10.0);
    TEST_CHECK(run, IsValid(&Good));
    UIValueSettings S = Good;
    S.Minimum = INFINITY;
    TEST_CHECK(run, !IsValid(&S));
    S = Good;
    S.Maximum = NAN;
    TEST_CHECK(run, !IsValid(&S));
    S = Good;
    S.Maximum = 0.0;
    TEST_CHECK(run, !IsValid(&S));
    S = UIValueSettings_CreateRange(-DBL_MAX, DBL_MAX); // Its span is infinite.
    TEST_CHECK(run, !IsValid(&S));
    S = UIValueSettings_Create(); // No limits at all is a valid value.
    TEST_CHECK(run, IsValid(&S));
    S.HasMinimum = true; // One side alone too.
    S.Minimum = 3.0;
    TEST_CHECK(run, IsValid(&S));
    const char* Reason = NULL;
    TEST_CHECK(run, !UIValueSettings_IsValid(&S, true, &Reason) && (Reason != NULL)); // A slider needs both ends.

    // The soft range: inside the hard limits, rising, finite.
    S = Good;
    S.HasSoftRange = true;
    S.SoftMinimum = 2.0;
    S.SoftMaximum = 8.0;
    TEST_CHECK(run, IsValid(&S));
    S.SoftMinimum = -1.0;
    TEST_CHECK(run, !IsValid(&S));
    S.SoftMinimum = 8.0;
    TEST_CHECK(run, !IsValid(&S));
    S.SoftMinimum = 2.0;
    S.SoftMaximum = INFINITY;
    TEST_CHECK(run, !IsValid(&S));

    // The step: finite, above 0, whole for a whole number; never with a list.
    S = Good;
    S.HasStep = true;
    S.Step = 0.0;
    TEST_CHECK(run, !IsValid(&S));
    S.Step = -1.0;
    TEST_CHECK(run, !IsValid(&S));
    S.Step = NAN;
    TEST_CHECK(run, !IsValid(&S));
    S.Step = INFINITY;
    TEST_CHECK(run, !IsValid(&S));
    S.Step = 0.5;
    TEST_CHECK(run, IsValid(&S));
    S.IsInteger = true;
    TEST_CHECK(run, !IsValid(&S));
    S.Step = 2.0;
    TEST_CHECK(run, IsValid(&S));
    double List[] = { 1.0, 2.0, 4.0 };
    S.ValueList = List;
    S.ValueCount = 3U;
    TEST_CHECK(run, !IsValid(&S)); // A step or a list, not both.

    // A whole number needs a whole number inside its limits.
    S = UIValueSettings_CreateRange(0.2, 0.8);
    S.IsInteger = true;
    TEST_CHECK(run, !IsValid(&S));
    S = Good;
    S.IsInteger = true;
    S.HasSoftRange = true;
    S.SoftMinimum = 2.2;
    S.SoftMaximum = 2.8;
    TEST_CHECK(run, !IsValid(&S));

    // The fine factor and the default.
    S = Good;
    S.FineFactor = 0.0;
    TEST_CHECK(run, !IsValid(&S));
    S.FineFactor = 1.5;
    TEST_CHECK(run, !IsValid(&S));
    S.FineFactor = NAN;
    TEST_CHECK(run, !IsValid(&S));
    S.FineFactor = 1.0;
    TEST_CHECK(run, IsValid(&S));
    S.HasDefault = true;
    S.Default = INFINITY;
    TEST_CHECK(run, !IsValid(&S));
    S.HasDefault = false; // With no default its number means nothing.
    TEST_CHECK(run, IsValid(&S));
}

static void AValueListMustRiseStayInsideAndKeepItsLimit(TestRun* run)
{
    UIValueSettings S = UIValueSettings_CreateRange(0.0, 100.0);
    double Rising[] = { 1.0, 5.0, 50.0 };
    S.ValueList = Rising;
    S.ValueCount = 3U;
    TEST_CHECK(run, IsValid(&S));
    double Unsorted[] = { 5.0, 1.0, 50.0 };
    S.ValueList = Unsorted;
    TEST_CHECK(run, !IsValid(&S));
    double Twice[] = { 1.0, 5.0, 5.0 };
    S.ValueList = Twice;
    TEST_CHECK(run, !IsValid(&S));
    double Outside[] = { 1.0, 5.0, 500.0 };
    S.ValueList = Outside;
    TEST_CHECK(run, !IsValid(&S));
    double NotFinite[] = { 1.0, NAN, 50.0 };
    S.ValueList = NotFinite;
    TEST_CHECK(run, !IsValid(&S));
    S.ValueList = NULL;
    TEST_CHECK(run, !IsValid(&S));
    S.ValueList = Rising;
    S.ValueCount = UI_VALUE_MAX_LIST_COUNT + 1U;
    TEST_CHECK(run, !IsValid(&S));
    S.ValueCount = 3U;
    S.IsInteger = true;
    double Halves[] = { 1.0, 2.5, 4.0 };
    S.ValueList = Halves;
    TEST_CHECK(run, !IsValid(&S));
    S.ValueList = Rising;
    TEST_CHECK(run, IsValid(&S));
    S.HasSoftRange = true; // A list lists what can be chosen: no soft range.
    S.SoftMinimum = 1.0;
    S.SoftMaximum = 5.0;
    TEST_CHECK(run, !IsValid(&S));

    // The most a list holds is taken; a huge rising list is fine.
    static double Huge[UI_VALUE_MAX_LIST_COUNT];
    for (size_t Index = 0U; Index < UI_VALUE_MAX_LIST_COUNT; Index++)
    {
        Huge[Index] = (double)Index;
    }
    UIValueSettings Big = UIValueSettings_Create();
    Big.ValueList = Huge;
    Big.ValueCount = UI_VALUE_MAX_LIST_COUNT;
    TEST_CHECK(run, IsValid(&Big));
    TEST_CHECK(run, UIValueSettings_Normalize(&Big, 1234.4) == 1234.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&Big, 1234.5) == 1235.0); // Halfway, the upper one.
}

static void AConversionNeedsWhatItConvertsWith(TestRun* run)
{
    UIValueSettings S = UIValueSettings_CreateRange(0.0, 100.0);
    S.Conversion = UIValueConversion_Log;
    TEST_CHECK(run, !IsValid(&S)); // Log needs a minimum above 0.
    S.Minimum = 1.0;
    TEST_CHECK(run, IsValid(&S));
    S.HasMaximum = false;
    TEST_CHECK(run, !IsValid(&S)); // ... and a range.
    S = UIValueSettings_CreateRange(0.0, 100.0);
    S.Conversion = UIValueConversion_Power;
    S.Exponent = 0.0;
    TEST_CHECK(run, !IsValid(&S));
    S.Exponent = UI_VALUE_MAX_EXPONENT * 2.0;
    TEST_CHECK(run, !IsValid(&S));
    S.Exponent = NAN;
    TEST_CHECK(run, !IsValid(&S));
    S.Exponent = 3.0;
    TEST_CHECK(run, IsValid(&S));
    S.Conversion = UIValueConversion_Even;
    TEST_CHECK(run, !IsValid(&S)); // Even needs a list of two at least.
    double List[] = { 1.0, 2.0 };
    S.ValueList = List;
    S.ValueCount = 2U;
    TEST_CHECK(run, IsValid(&S));
    S = CustomSettings(0);
    TEST_CHECK(run, IsValid(&S));
    S.Custom.ToShare = NULL;
    TEST_CHECK(run, !IsValid(&S));
    S = UIValueSettings_CreateRange(0.0, 1.0);
    S.Conversion = (UIValueConversion)99;
    TEST_CHECK(run, !IsValid(&S));
}


// Tests: the arithmetic.
static void TheDragRangeIsTheSoftOneElseTheListElseTheLimits(TestRun* run)
{
    double Minimum = -1.0;
    double Maximum = -1.0;
    UIValueSettings S = UIValueSettings_Create();
    TEST_CHECK(run, !UIValueSettings_TryGetDragRange(&S, &Minimum, &Maximum) && (Minimum == -1.0));
    S = UIValueSettings_CreateRange(0.0, 100.0);
    TEST_CHECK(run, UIValueSettings_TryGetDragRange(&S, &Minimum, &Maximum) && (Minimum == 0.0) && (Maximum == 100.0));
    double List[] = { 10.0, 20.0, 40.0 };
    S.ValueList = List;
    S.ValueCount = 3U;
    TEST_CHECK(run, UIValueSettings_TryGetDragRange(&S, &Minimum, &Maximum) && (Minimum == 10.0) && (Maximum == 40.0));
    S.ValueCount = 0U;
    S.HasSoftRange = true;
    S.SoftMinimum = 5.0;
    S.SoftMaximum = 15.0;
    TEST_CHECK(run, UIValueSettings_TryGetDragRange(&S, &Minimum, &Maximum) && (Minimum == 5.0) && (Maximum == 15.0));

    // Around a value typed past the soft range, the range widens to take it in, within the hard limits.
    TEST_CHECK(run, UIValueSettings_GetDragRangeAround(&S, 50.0, &Minimum, &Maximum) && (Minimum == 5.0) && (Maximum == 50.0));
    TEST_CHECK(run, UIValueSettings_GetDragRangeAround(&S, -50.0, &Minimum, &Maximum) && (Minimum == 0.0) && (Maximum == 15.0));
    TEST_CHECK(run, UIValueSettings_GetDragRangeAround(&S, NAN, &Minimum, &Maximum) && (Minimum == 5.0) && (Maximum == 15.0));
    S.HasSoftRange = false; // Only a soft range widens.
    TEST_CHECK(run, UIValueSettings_GetDragRangeAround(&S, 500.0, &Minimum, &Maximum) && (Maximum == 100.0));
}

static void NormalizingClampsThenSnaps(TestRun* run)
{
    UIValueSettings S = UIValueSettings_CreateRange(0.0, 10.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&S, 5.5) == 5.5);
    TEST_CHECK(run, UIValueSettings_Normalize(&S, NAN) == 0.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&S, INFINITY) == 10.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&S, -INFINITY) == 0.0);
    S.HasStep = true;
    S.Step = 3.0;
    TEST_CHECK(run, UIValueSettings_Normalize(&S, 4.4) == 3.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&S, 9.7) == 10.0); // The upper end counts as a step.
    TEST_CHECK(run, UIValueSettings_Normalize(&S, 8.9) == 9.0);
    S.Step = 1e-320; // Too small to count with: no step.
    TEST_CHECK(run, UIValueSettings_Normalize(&S, 4.4) == 4.4);

    // No limits: steps count from 0, a NaN is 0, an infinity the largest finite double.
    UIValueSettings Free = UIValueSettings_Create();
    TEST_CHECK(run, UIValueSettings_Normalize(&Free, NAN) == 0.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&Free, INFINITY) == DBL_MAX);
    TEST_CHECK(run, UIValueSettings_Normalize(&Free, -INFINITY) == -DBL_MAX);
    Free.HasStep = true;
    Free.Step = 0.25;
    TEST_CHECK(run, UIValueSettings_Normalize(&Free, -1.3) == -1.25);
    TEST_CHECK(run, UIValueSettings_Normalize(&Free, 1e300) == 1e300); // Far out, a step too small to count with.

    // Whole numbers: rounded, inside whole limits.
    UIValueSettings Whole = UIValueSettings_CreateRange(0.5, 10.5);
    Whole.IsInteger = true;
    TEST_CHECK(run, UIValueSettings_Normalize(&Whole, 3.5) == 4.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&Whole, 0.6) == 1.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&Whole, 10.4) == 10.0);
    Whole.HasStep = true;
    Whole.Step = 5.0;
    TEST_CHECK(run, UIValueSettings_Normalize(&Whole, 3.0) == 1.0); // From the first whole number inside: 1, 6, then the end.
    TEST_CHECK(run, UIValueSettings_Normalize(&Whole, 4.0) == 6.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&Whole, 9.0) == 10.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&Whole, 2e9) == 10.0);

    // A list: the nearest value, the upper one halfway.
    UIValueSettings Listed = UIValueSettings_CreateRange(0.0, 100.0);
    double List[] = { 1.0, 5.0, 50.0 };
    Listed.ValueList = List;
    Listed.ValueCount = 3U;
    TEST_CHECK(run, UIValueSettings_Normalize(&Listed, 2.0) == 1.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&Listed, 3.0) == 5.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&Listed, 99.0) == 50.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&Listed, -5.0) == 1.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&Listed, NAN) == 1.0);
}

static void ConversionsMapAShareAndBack(TestRun* run)
{
    UIValueSettings Linear = UIValueSettings_CreateRange(0.0, 100.0);
    TEST_CHECK(run, UIValueSettings_FromShare(&Linear, 0.0, 100.0, 0.25) == 25.0);
    TEST_CHECK(run, UIValueSettings_FromShare(&Linear, 0.0, 100.0, 1.0) == 100.0);
    TEST_CHECK(run, UIValueSettings_FromShare(&Linear, 0.0, 100.0, NAN) == 0.0);
    TEST_CHECK(run, UIValueSettings_FromShare(&Linear, 0.0, 100.0, 7.0) == 100.0);
    TEST_CHECK(run, UIValueSettings_ToShare(&Linear, 0.0, 100.0, 25.0) == 0.25);
    TEST_CHECK(run, UIValueSettings_ToShare(&Linear, 0.0, 100.0, NAN) == 0.0);

    UIValueSettings Log = UIValueSettings_CreateRange(1.0, 100.0);
    Log.Conversion = UIValueConversion_Log;
    TEST_CHECK(run, IsNear(UIValueSettings_FromShare(&Log, 1.0, 100.0, 0.5), 10.0, 1e-9)); // Each half multiplies by ten.
    TEST_CHECK(run, UIValueSettings_FromShare(&Log, 1.0, 100.0, 1.0) == 100.0); // The ends exact.
    TEST_CHECK(run, UIValueSettings_FromShare(&Log, 1.0, 100.0, 0.0) == 1.0);
    TEST_CHECK(run, IsNear(UIValueSettings_ToShare(&Log, 1.0, 100.0, 10.0), 0.5, 1e-12));
    UIValueSettings Wide = UIValueSettings_CreateRange(1e-300, 1e300); // No overflow across a range of any width.
    Wide.Conversion = UIValueConversion_Log;
    TEST_CHECK(run, IsValid(&Wide) && IsNear(UIValueSettings_FromShare(&Wide, 1e-300, 1e300, 0.5), 1.0, 1e-6));

    UIValueSettings Power = UIValueSettings_CreateRange(0.0, 100.0);
    Power.Conversion = UIValueConversion_Power;
    Power.Exponent = 2.0;
    TEST_CHECK(run, IsNear(UIValueSettings_FromShare(&Power, 0.0, 100.0, 0.5), 25.0, 1e-9));
    TEST_CHECK(run, IsNear(UIValueSettings_ToShare(&Power, 0.0, 100.0, 25.0), 0.5, 1e-12));

    UIValueSettings Even = UIValueSettings_CreateRange(0.0, 1000.0);
    double List[] = { 1.0, 2.0, 10.0, 1000.0 };
    Even.ValueList = List;
    Even.ValueCount = 4U;
    Even.Conversion = UIValueConversion_Even;
    TEST_CHECK(run, UIValueSettings_FromShare(&Even, 1.0, 1000.0, 0.34) == 2.0); // A third of the track each, however far apart.
    TEST_CHECK(run, UIValueSettings_FromShare(&Even, 1.0, 1000.0, 0.66) == 10.0);
    TEST_CHECK(run, UIValueSettings_FromShare(&Even, 1.0, 1000.0, 1.0) == 1000.0);
    TEST_CHECK(run, IsNear(UIValueSettings_ToShare(&Even, 1.0, 1000.0, 10.0), 2.0 / 3.0, 1e-12));

    // Round trips within rounding, for every conversion, over the whole track.
    const UIValueSettings* All[] = { &Linear, &Log, &Power };
    double Lows[] = { 0.0, 1.0, 0.0 };
    double Highs[] = { 100.0, 100.0, 100.0 };
    for (size_t Kind = 0U; Kind < 3U; Kind++)
    {
        bool IsRoundTripping = true;
        for (int32_t Step = 0; Step <= 1000; Step++)
        {
            double Share = (double)Step / 1000.0;
            double Value = UIValueSettings_FromShare(All[Kind], Lows[Kind], Highs[Kind], Share);
            double Back = UIValueSettings_ToShare(All[Kind], Lows[Kind], Highs[Kind], Value);
            IsRoundTripping = IsRoundTripping && IsNear(Back, Share, 1e-9) && IsNear(UIValueSettings_FromShare(All[Kind], Lows[Kind], Highs[Kind], Back), Value,
                1e-9 * Highs[Kind]);
        }
        TEST_CHECK(run, IsRoundTripping);
    }
}

static void ACustomConversionIsCheckedEveryCall(TestRun* run)
{
    UIValueSettings Square = CustomSettings(0);
    TEST_CHECK(run, IsNear(UIValueSettings_FromShare(&Square, 0.0, 100.0, 0.5), 25.0, 1e-9));
    TEST_CHECK(run, IsNear(UIValueSettings_ToShare(&Square, 0.0, 100.0, 25.0), 0.5, 1e-12));
    UIValueSettings Broken = CustomSettings(1); // A NaN: the lower end, a share of 0.
    TEST_CHECK(run, UIValueSettings_FromShare(&Broken, 0.0, 100.0, 0.5) == 0.0);
    TEST_CHECK(run, UIValueSettings_ToShare(&Broken, 0.0, 100.0, 50.0) == 0.0);
    UIValueSettings Outside = CustomSettings(2); // Out of range: clamped, never stored.
    TEST_CHECK(run, UIValueSettings_FromShare(&Outside, 0.0, 100.0, 0.5) == 100.0);
    TEST_CHECK(run, UIValueSettings_ToShare(&Outside, 0.0, 100.0, 50.0) == 0.0);
    UIValueSettings Backwards = CustomSettings(3); // Running backwards still stays in the range.
    TEST_CHECK(run, UIValueSettings_FromShare(&Backwards, 0.0, 100.0, 0.25) == 75.0);
    TEST_CHECK(run, UIValueSettings_ToShare(&Backwards, 0.0, 100.0, 75.0) == 0.25);
    TEST_CHECK(run, UIValueSettings_Step(&Backwards, 75.0, 1.0, false) == 74.0); // Up the track is down the value: a hundredth.
}

static void AStepGoesByTheListTheStepOrAHundredthOfTheTrack(TestRun* run)
{
    // A list: from value to value; from between two, the next one that way.
    UIValueSettings Listed = UIValueSettings_CreateRange(0.0, 100.0);
    double List[] = { 1.0, 5.0, 50.0 };
    Listed.ValueList = List;
    Listed.ValueCount = 3U;
    TEST_CHECK(run, UIValueSettings_Step(&Listed, 5.0, 1.0, false) == 50.0);
    TEST_CHECK(run, UIValueSettings_Step(&Listed, 5.0, -1.0, false) == 1.0);
    TEST_CHECK(run, UIValueSettings_Step(&Listed, 3.0, 1.0, false) == 5.0);
    TEST_CHECK(run, UIValueSettings_Step(&Listed, 3.0, -1.0, false) == 1.0);
    TEST_CHECK(run, UIValueSettings_Step(&Listed, 50.0, 4.0, false) == 50.0);
    TEST_CHECK(run, UIValueSettings_Step(&Listed, 1.0, 0.4, false) == 1.0); // Less than half a step moves nothing.
    TEST_CHECK(run, UIValueSettings_Step(&Listed, 1.0, 1.0, true) == 5.0); // A fine step is still one.
    TEST_CHECK(run, UIValueSettings_Step(&Listed, 1.0, INFINITY, false) == 50.0);

    // A step: whole steps; a fine step on a stepped value still moves one step.
    UIValueSettings Stepped = UIValueSettings_CreateRange(0.0, 10.0);
    Stepped.HasStep = true;
    Stepped.Step = 2.0;
    TEST_CHECK(run, UIValueSettings_Step(&Stepped, 4.0, 1.0, false) == 6.0);
    TEST_CHECK(run, UIValueSettings_Step(&Stepped, 4.0, 1.0, true) == 6.0);
    TEST_CHECK(run, UIValueSettings_Step(&Stepped, 9.0, 1.0, false) == 10.0);
    TEST_CHECK(run, UIValueSettings_Step(&Stepped, 10.0, -1.0, false) == 8.0);
    TEST_CHECK(run, UIValueSettings_Step(&Stepped, 4.0, NAN, false) == 4.0);
    TEST_CHECK(run, UIValueSettings_Step(&Stepped, 4.0, -INFINITY, false) == 0.0);

    // Whole numbers with no step: a step of 1.
    UIValueSettings Whole = UIValueSettings_CreateRange(0.0, 10.0);
    Whole.IsInteger = true;
    TEST_CHECK(run, UIValueSettings_Step(&Whole, 4.0, 1.0, true) == 5.0);

    // No step: a hundredth of the track, through the conversion; Shift a fine one.
    UIValueSettings Linear = UIValueSettings_CreateRange(0.0, 10.0);
    TEST_CHECK(run, IsNear(UIValueSettings_Step(&Linear, 4.0, 1.0, false), 4.1, 1e-12));
    TEST_CHECK(run, IsNear(UIValueSettings_Step(&Linear, 4.0, 1.0, true), 4.01, 1e-12));
    TEST_CHECK(run, UIValueSettings_Step(&Linear, 9.99, 1.0, false) == 10.0);
    UIValueSettings Log = UIValueSettings_CreateRange(1.0, 100.0);
    Log.Conversion = UIValueConversion_Log;
    double Up = UIValueSettings_Step(&Log, 10.0, 1.0, false);
    double Down = UIValueSettings_Step(&Log, 10.0, -1.0, false);
    TEST_CHECK(run, IsNear(Up / 10.0, 10.0 / Down, 1e-9) && (Up > 10.0)); // By the same factor either way.

    // No range and no step: by the value's magnitude.
    UIValueSettings Free = UIValueSettings_Create();
    TEST_CHECK(run, IsNear(UIValueSettings_Step(&Free, 250.0, 1.0, false), 251.0, 1e-9));
    TEST_CHECK(run, IsNear(UIValueSettings_Step(&Free, 0.5, 1.0, false), 0.51, 1e-12));
    TEST_CHECK(run, UIValueSettings_Step(&Free, 1.0, INFINITY, false) == DBL_MAX);

    // The soft range holds a step, widened to take in a value typed past it.
    UIValueSettings Soft = UIValueSettings_CreateRange(0.0, 100.0);
    Soft.HasSoftRange = true;
    Soft.SoftMinimum = 0.0;
    Soft.SoftMaximum = 10.0;
    TEST_CHECK(run, UIValueSettings_Step(&Soft, 10.0, 1.0, false) == 10.0);
    double FromPast = UIValueSettings_Step(&Soft, 50.0, -1.0, false);
    TEST_CHECK(run, (FromPast < 50.0) && (FromPast > 40.0)); // From where it is, not from the soft end.
}

static void AScrubsRateAndACoarseSnapFollowTheValue(TestRun* run)
{
    UIValueSettings Free = UIValueSettings_Create();
    TEST_CHECK(run, IsNear(UIValueSettings_GetScrubRate(&Free, 0.3), 0.01, 1e-15));
    TEST_CHECK(run, IsNear(UIValueSettings_GetScrubRate(&Free, 250.0), 1.0, 1e-12));
    TEST_CHECK(run, IsNear(UIValueSettings_GetScrubRate(&Free, 1000.0), 10.0, 1e-9)); // An exact power of ten is its own magnitude.
    TEST_CHECK(run, IsNear(UIValueSettings_GetScrubRate(&Free, -1000.0), 10.0, 1e-9));
    TEST_CHECK(run, IsNear(UIValueSettings_GetScrubRate(&Free, NAN), 0.01, 1e-15));
    UIValueSettings Stepped = Free;
    Stepped.HasStep = true;
    Stepped.Step = 0.5;
    TEST_CHECK(run, UIValueSettings_GetScrubRate(&Stepped, 1e9) == 0.5 / UI_VALUE_SCRUB_PIXELS_PER_STEP);
    UIValueSettings Whole = Free;
    Whole.IsInteger = true;
    TEST_CHECK(run, UIValueSettings_GetScrubRate(&Whole, 1e9) == 1.0 / UI_VALUE_SCRUB_PIXELS_PER_STEP);

    UIValueSettings Linear = UIValueSettings_CreateRange(0.0, 10.0);
    TEST_CHECK(run, UIValueSettings_SnapCoarse(&Linear, 0.0, 10.0, true, 3.7, 0.0) == 4.0); // A tenth of the range's power of ten.
    UIValueSettings Unit = UIValueSettings_CreateRange(0.0, 1.0);
    TEST_CHECK(run, IsNear(UIValueSettings_SnapCoarse(&Unit, 0.0, 1.0, true, 0.37, 0.0), 0.4, 1e-12));
    TEST_CHECK(run, UIValueSettings_SnapCoarse(&Free, 0.0, 0.0, false, 263.0, 250.0) == 260.0); // A tenth of the start's magnitude.
    UIValueSettings Log = UIValueSettings_CreateRange(1.0, 100.0);
    Log.Conversion = UIValueConversion_Log;
    TEST_CHECK(run, IsNear(UIValueSettings_SnapCoarse(&Log, 1.0, 100.0, true, 9.0, 0.0), 10.0, 1e-9)); // The share's tenths.
    UIValueSettings Coarse = UIValueSettings_CreateRange(0.0, 100.0);
    Coarse.HasStep = true;
    Coarse.Step = 3.0;
    double Snapped = UIValueSettings_SnapCoarse(&Coarse, 0.0, 100.0, true, 50.0, 0.0);
    TEST_CHECK(run, fmod(Snapped, 3.0) == 0.0); // Never off the step.
}

static void TheSliderPrimitivesKeepTheirPromises(TestRun* run)
{
    TEST_CHECK(run, UIValue_GetUnclampedPointerFraction(-90.0, 220.0, 20.0) == -0.5);
    TEST_CHECK(run, UIValue_GetUnclampedPointerFraction(310.0, 220.0, 20.0) == 1.5);
    TEST_CHECK(run, UIValue_GetUnclampedPointerFraction(10.0, 20.0, 20.0) == 0.0);
    TEST_CHECK(run, UIValue_GetUnclampedPointerFraction(NAN, 220.0, 20.0) == 0.0);
    TEST_CHECK(run, UIValue_GetUnclampedPointerFraction(10.0, INFINITY, 20.0) == 0.0);
    TEST_CHECK(run, UIValue_GetPointerFraction(310.0, 220.0, 20.0) == 1.0);
}


// Tests: text.
static void ShowingAValueNeverChangesIt(TestRun* run)
{
    double Values[] = { 0.0, -0.0, 1.0, 0.1, 1.0 / 3.0, 1e7 + 0.1, 5e6 + 0.123456789, -123456.789, 1e300, 1e-300, 5e-324,
        1.7976931348623157e308, 123456789012345678.0, 0.30000000000000004 };
    bool IsRoundTripping = true;
    for (size_t Index = 0U; Index < (sizeof(Values) / sizeof(Values[0])); Index++)
    {
        IsRoundTripping = IsRoundTripping && RoundTrips(Values[Index], false);
    }
    TEST_CHECK(run, IsRoundTripping);
    // Far doubles in steps of a hundredth near ten million, and a sweep of magnitudes.
    for (int32_t Step = 0; Step < 1000; Step++)
    {
        IsRoundTripping = IsRoundTripping && RoundTrips(1e7 + ((double)Step * 0.01), false) && RoundTrips(ldexp(1.0 + (double)Step / 1000.0, Step - 500), false);
    }
    TEST_CHECK(run, IsRoundTripping);

    // A float's value: a float's shortest text, read back as the same float.
    unsigned char Bytes[64];
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, Bytes, sizeof(Bytes), sizeof(unsigned char), 0U);
    TEST_CHECK_SUCCESS(run, UIValue_WriteShortestText((double)0.1f, true, &Text));
    TEST_CHECK(run, strcmp((const char*)Bytes, "0.1") == 0);
    TEST_CHECK(run, RoundTrips((double)0.1f, true) && RoundTrips((double)3.4028234663852886e38f, true));
    // Not a float: its own text shows, and read back as a float it is another value (a property box keeps text left as it was untouched).
    TEST_CHECK(run, !RoundTrips(0.1, true));
}

static void AShownValueHasItsDigitsAndNoNegativeZero(TestRun* run)
{
    TEST_CHECK(run, IsFixed(1.5, 3, "1.500"));
    TEST_CHECK(run, IsFixed(1.5, 0, "2"));
    TEST_CHECK(run, IsFixed(-0.0, 3, "0.000"));
    TEST_CHECK(run, IsFixed(-0.0004, 3, "0.000"));
    TEST_CHECK(run, IsFixed(-0.0006, 3, "-0.001"));
    TEST_CHECK(run, IsFixed(2.0, -5, "2"));
    TEST_CHECK(run, IsFixed(0.1, 99, "0.100000000000000"));
    TEST_CHECK(run, IsFixed(1e7 + 0.1, 2, "10000000.10"));
}

static void TypedTextReadsAsANumberOrNothing(TestRun* run)
{
    TEST_CHECK(run, Parses("1.5", NULL, 1.5));
    TEST_CHECK(run, Parses("  -2.25  ", NULL, -2.25));
    TEST_CHECK(run, Parses("+3", NULL, 3.0));
    TEST_CHECK(run, Parses("1e-3", NULL, 0.001));
    TEST_CHECK(run, Parses("5 m", u8" m", 5.0));
    TEST_CHECK(run, Parses("5m", u8" m", 5.0));
    TEST_CHECK(run, Parses(" 7.5   m ", u8"m", 7.5));
    TEST_CHECK(run, Parses((const char*)u8"90°", u8"°", 90.0));
    TEST_CHECK(run, Refuses("m", u8"m"));
    TEST_CHECK(run, Refuses("", NULL));
    TEST_CHECK(run, Refuses("   ", NULL));
    TEST_CHECK(run, Refuses("-", NULL));
    TEST_CHECK(run, Refuses("nan", NULL));
    TEST_CHECK(run, Refuses("infinity", NULL));
    TEST_CHECK(run, Refuses("-infinity", NULL));
    TEST_CHECK(run, Refuses("1e400", NULL));
    TEST_CHECK(run, Refuses("1,5", NULL));
    TEST_CHECK(run, Refuses("2*3", NULL));
    TEST_CHECK(run, Refuses("5 kg", u8"m"));
    TEST_CHECK(run, Refuses("\xff\xfe", NULL));
    char Long[UI_VALUE_MAX_TEXT_BYTES + 8U];
    memset(Long, '1', sizeof(Long) - 1U);
    Long[sizeof(Long) - 1U] = 0;
    TEST_CHECK(run, Refuses(Long, NULL));
    // A NUL inside ends the text.
    double Value = 0.0;
    TEST_CHECK(run, UIValue_ParseText((const unsigned char*)"4\0junk", 6U, NULL, false, &Value) && (Value == 4.0));
    TEST_CHECK(run, !UIValue_ParseText(NULL, 0U, NULL, false, &Value));
    // Read as a float: the nearest float, widened; past a float's range nothing.
    TEST_CHECK(run, UIValue_ParseText((const unsigned char*)"0.1", 3U, NULL, true, &Value) && (Value == (double)0.1f));
    TEST_CHECK(run, !UIValue_ParseText((const unsigned char*)"1e39", 4U, NULL, true, &Value));
}


// Tests: by widget id.
static void TheFunctionsWorkByIdAndRefuseWhatBreaksARule(TestRun* run)
{
    TestUIFixture Fixture;
    ValueClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        uint64_t Id = AddSlider(run, &Fixture, &Options);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 7.5));
        TEST_CHECK(run, Get(run, &Fixture, Id) == 7.5);
        TEST_CHECK_ERROR(run, UIValue_Set(&Fixture.Screen, Id, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 99.0));
        TEST_CHECK(run, Get(run, &Fixture, Id) == 10.0);

        // A slider needs both ends; a refused setting changes nothing.
        TEST_CHECK_ERROR(run, UIValue_SetRange(&Fixture.Screen, Id, true, 0.0, false, 0.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIValue_SetRange(&Fixture.Screen, Id, true, 5.0, true, 5.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 10.0);
        TEST_CHECK_SUCCESS(run, UIValue_SetRange(&Fixture.Screen, Id, true, 0.0, true, 4.0));
        TEST_CHECK(run, Get(run, &Fixture, Id) == 4.0);

        TEST_CHECK_SUCCESS(run, UIValue_SetStep(&Fixture.Screen, Id, true, 1.5));
        TEST_CHECK(run, Get(run, &Fixture, Id) == 4.0); // The end counts as a step.
        double List[] = { 0.5, 1.0, 2.0 };
        TEST_CHECK_ERROR(run, UIValue_SetValueList(&Fixture.Screen, Id, List, 3U), ErrorCode_ArgumentOutOfRange); // Not with a step.
        TEST_CHECK_SUCCESS(run, UIValue_SetStep(&Fixture.Screen, Id, false, 0.0));
        TEST_CHECK_SUCCESS(run, UIValue_SetValueList(&Fixture.Screen, Id, List, 3U));
        TEST_CHECK(run, Get(run, &Fixture, Id) == 2.0);
        List[2] = 99.0; // The list was copied.
        UIValueSettings Settings;
        TEST_CHECK_SUCCESS(run, UIValue_GetSettings(&Fixture.Screen, Id, &Settings));
        TEST_CHECK(run, (Settings.ValueCount == 3U) && (Settings.ValueList[2] == 2.0) && (Settings.ValueList != List));
        TEST_CHECK_SUCCESS(run, UIValue_SetConversion(&Fixture.Screen, Id, UIValueConversion_Even, 0.0));
        TEST_CHECK_SUCCESS(run, UIValue_SetValueList(&Fixture.Screen, Id, NULL, 0U)); // An even conversion goes linear with its list.
        TEST_CHECK_SUCCESS(run, UIValue_GetSettings(&Fixture.Screen, Id, &Settings));
        TEST_CHECK(run, (Settings.Conversion == UIValueConversion_Linear) && (Settings.ValueCount == 0U));
        TEST_CHECK_ERROR(run, UIValue_SetValueList(&Fixture.Screen, Id, NULL, 2U), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIValue_SetConversion(&Fixture.Screen, Id, UIValueConversion_Custom, 0.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIValue_SetConversion(&Fixture.Screen, Id, UIValueConversion_Log, 0.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIValue_SetCustomConversion(&Fixture.Screen, Id, Custom_ToValue, Custom_ToShare, NULL));
        TEST_CHECK_ERROR(run, UIValue_SetCustomConversion(&Fixture.Screen, Id, NULL, Custom_ToShare, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_SUCCESS(run, UIValue_SetConversion(&Fixture.Screen, Id, UIValueConversion_Power, 3.0));
        TEST_CHECK_SUCCESS(run, UIValue_GetSettings(&Fixture.Screen, Id, &Settings));
        TEST_CHECK(run, (Settings.Conversion == UIValueConversion_Power) && (Settings.Exponent == 3.0));

        TEST_CHECK_SUCCESS(run, UIValue_SetIsInteger(&Fixture.Screen, Id, true));
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 2.4));
        TEST_CHECK(run, Get(run, &Fixture, Id) == 2.0);
        TEST_CHECK_SUCCESS(run, UIValue_SetIsInteger(&Fixture.Screen, Id, false));
        TEST_CHECK_ERROR(run, UIValue_ResetToDefault(&Fixture.Screen, Id), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, UIValue_SetDefault(&Fixture.Screen, Id, true, 3.25));
        TEST_CHECK_SUCCESS(run, UIValue_ResetToDefault(&Fixture.Screen, Id));
        TEST_CHECK(run, Get(run, &Fixture, Id) == 3.25);
        TEST_CHECK_ERROR(run, UIValue_SetFineFactor(&Fixture.Screen, Id, 0.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIValue_SetFineFactor(&Fixture.Screen, Id, 0.5));
        TEST_CHECK_SUCCESS(run, UIValue_SetSoftRange(&Fixture.Screen, Id, true, 1.0, 3.0));
        TEST_CHECK_ERROR(run, UIValue_SetSoftRange(&Fixture.Screen, Id, true, -1.0, 3.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIValue_Step(&Fixture.Screen, Id, 1.0, false));
        TEST_CHECK(run, Get(run, &Fixture, Id) == 3.25); // Past the soft range, the range widens to the value and no further.
        TEST_CHECK_SUCCESS(run, UIValue_GetSettings(&Fixture.Screen, Id, &Settings));
        double Expected = UIValueSettings_Step(&Settings, 3.25, -1.0, false);
        TEST_CHECK_SUCCESS(run, UIValue_Step(&Fixture.Screen, Id, -1.0, false));
        TEST_CHECK(run, (Get(run, &Fixture, Id) == Expected) && (Expected < 3.25) && (Expected > 3.0)); // From where it is, by the track.

        bool IsMixed = true;
        TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, Id, &IsMixed));
        TEST_CHECK(run, !IsMixed);
        TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, true));
        TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, Id, &IsMixed));
        TEST_CHECK(run, IsMixed);

        // A widget that holds no value, and a stale id.
        UIButtonOptions Button = UIButtonOptions_Create(u8"b");
        uint64_t ButtonId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Button, &ButtonId));
        double Value = 0.0;
        TEST_CHECK_ERROR(run, UIValue_Get(&Fixture.Screen, ButtonId, &Value), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIValue_Set(&Fixture.Screen, 99U, 1.0), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIValue_SetMixed(&Fixture.Screen, 99U, true), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIValue_SetIsEnabled(&Fixture.Screen, 99U, true), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AChangeIsCommittedOrTakenBackWithItsMixedState(TestRun* run)
{
    TestUIFixture Fixture;
    ValueClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        Options.IsMixed = true;
        uint64_t Id = AddSlider(run, &Fixture, &Options);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            UIValueCapability* Value = NULL;
            TEST_CHECK_SUCCESS(run, UIValue_Resolve(&Fixture.Screen, Id, &Base, &Value));
            UIValueCapability_BeginChange(Value);
            bool HasMoved = false;
            TEST_CHECK_SUCCESS(run, UIValueCapability_MoveTo(Value, Base, 6.0, &HasMoved));
            TEST_CHECK(run, HasMoved && !UIValueCapability_IsMixed(Value)); // Edited, it is mixed no more.
            TEST_CHECK_SUCCESS(run, UIValueCapability_ApplyWholeChange(Value, Base, 9.0, &HasMoved)); // Refused during a change.
            TEST_CHECK(run, !HasMoved && (UIValueCapability_GetValue(Value) == 6.0));
            TEST_CHECK_SUCCESS(run, UIValueCapability_CancelChange(Value, Base));
            TEST_CHECK(run, (UIValueCapability_GetValue(Value) == 5.0) && UIValueCapability_IsMixed(Value)); // Both back.
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 2U) && (Events.Events[0].WidgetEventId == (int32_t)UIElementEvent_Changed)
                && (Events.Events[1].WidgetEventId == (int32_t)UIElementEvent_Cancelled) && (Events.Events[1].Value.Data.DoubleValue == 5.0));

            // Several values made one is a change though the number ends where it began: committed. Not mixed, the same commits nothing.
            UIValueCapability_BeginChange(Value);
            TEST_CHECK_SUCCESS(run, UIValueCapability_MoveTo(Value, Base, 7.0, NULL));
            TEST_CHECK_SUCCESS(run, UIValueCapability_MoveTo(Value, Base, 5.0, NULL));
            TEST_CHECK_SUCCESS(run, UIValueCapability_EndChange(Value, Base));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 5U) && (Events.Events[4].WidgetEventId == (int32_t)UIElementEvent_Committed)
                && (Events.Events[4].Value.Data.DoubleValue == 5.0) && !UIValueCapability_IsMixed(Value));
            UIValueCapability_BeginChange(Value);
            TEST_CHECK_SUCCESS(run, UIValueCapability_MoveTo(Value, Base, 7.0, NULL));
            TEST_CHECK_SUCCESS(run, UIValueCapability_MoveTo(Value, Base, 5.0, NULL));
            TEST_CHECK_SUCCESS(run, UIValueCapability_EndChange(Value, Base));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 7U) && (Events.Events[6].WidgetEventId == (int32_t)UIElementEvent_Changed));

            // A whole change posts both at once; to the same number, nothing (unless mixed, below).
            TEST_CHECK_SUCCESS(run, UIValueCapability_ApplyWholeChange(Value, Base, 5.0, &HasMoved));
            TEST_CHECK(run, !HasMoved);
            TEST_CHECK_SUCCESS(run, UIValueCapability_ApplyWholeChange(Value, Base, INFINITY, &HasMoved));
            TEST_CHECK(run, !HasMoved);
            TEST_CHECK_SUCCESS(run, UIValueCapability_ApplyWholeChange(Value, Base, 8.0, &HasMoved));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, HasMoved && (Events.Count == 9U) && (Events.Events[7].WidgetEventId == (int32_t)UIElementEvent_Changed)
                && (Events.Events[8].WidgetEventId == (int32_t)UIElementEvent_Committed) && (Events.Events[8].Value.Data.DoubleValue == 8.0));
            TEST_CHECK_SUCCESS(run, UIValueCapability_EndChange(Value, Base)); // Nothing to end.
            TEST_CHECK_SUCCESS(run, UIValueCapability_CancelChange(Value, Base));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 9U);

            // Mixed, a whole change to its own number and a move to it are changes: Changed (and Committed), and one value from then on.
            TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, true));
            TEST_CHECK_SUCCESS(run, UIValueCapability_ApplyWholeChange(Value, Base, 8.0, &HasMoved));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, HasMoved && !UIValueCapability_IsMixed(Value) && (Events.Count == 11U)
                && (Events.Events[10].WidgetEventId == (int32_t)UIElementEvent_Committed) && (Events.Events[10].Value.Data.DoubleValue == 8.0));
            TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, true));
            UIValueCapability_BeginChange(Value);
            TEST_CHECK_SUCCESS(run, UIValueCapability_MoveTo(Value, Base, 8.0, &HasMoved));
            TEST_CHECK(run, HasMoved && !UIValueCapability_IsMixed(Value));
            // Taken back with the number where it began, the mixed state comes back, and Cancelled says so.
            TEST_CHECK_SUCCESS(run, UIValueCapability_CancelChange(Value, Base));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, UIValueCapability_IsMixed(Value) && (Events.Count == 13U)
                && (Events.Events[12].WidgetEventId == (int32_t)UIElementEvent_Cancelled));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Code's value during a change is where the change now starts: a release after it commits only what came after, a cancel goes back to it;
 * a setting changed by code keeps the start (inside the new settings). */
static void CodesValueDuringAChangeIsItsNewStart(TestRun* run)
{
    TestUIFixture Fixture;
    ValueClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        uint64_t Id = AddSlider(run, &Fixture, &Options);
        Widget* Base = NULL;
        UIValueCapability* Value = NULL;
        if (TEST_CHECK_SUCCESS(run, UIValue_Resolve(&Fixture.Screen, Id, &Base, &Value)))
        {
            // A change that moved nothing yet: code sets 8, the end commits nothing.
            UIValueCapability_BeginChange(Value);
            TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 8.0));
            TEST_CHECK_SUCCESS(run, UIValue_Resolve(&Fixture.Screen, Id, &Base, &Value));
            TEST_CHECK_SUCCESS(run, UIValueCapability_EndChange(Value, Base));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 0U);

            // A cancel after code's value goes back to it, not to where the change began.
            UIValueCapability_BeginChange(Value);
            TEST_CHECK_SUCCESS(run, UIValueCapability_MoveTo(Value, Base, 2.0, NULL));
            TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 6.0));
            TEST_CHECK_SUCCESS(run, UIValue_Resolve(&Fixture.Screen, Id, &Base, &Value));
            TEST_CHECK_SUCCESS(run, UIValueCapability_MoveTo(Value, Base, 3.0, NULL));
            TEST_CHECK_SUCCESS(run, UIValueCapability_CancelChange(Value, Base));
            TEST_CHECK(run, UIValueCapability_GetValue(Value) == 6.0);
            // Code's mixed state too, and a reset and a step by code alike.
            UIValueCapability_BeginChange(Value);
            TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, true));
            TEST_CHECK_SUCCESS(run, UIValue_Resolve(&Fixture.Screen, Id, &Base, &Value));
            TEST_CHECK_SUCCESS(run, UIValueCapability_CancelChange(Value, Base));
            TEST_CHECK(run, UIValueCapability_IsMixed(Value) && (UIValueCapability_GetValue(Value) == 6.0));
            TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, false));
            TEST_CHECK_SUCCESS(run, UIValue_SetDefault(&Fixture.Screen, Id, true, 1.0));
            UIValueCapability_BeginChange(Value);
            TEST_CHECK_SUCCESS(run, UIValue_ResetToDefault(&Fixture.Screen, Id));
            TEST_CHECK_SUCCESS(run, UIValue_Resolve(&Fixture.Screen, Id, &Base, &Value));
            TEST_CHECK_SUCCESS(run, UIValueCapability_CancelChange(Value, Base));
            TEST_CHECK(run, UIValueCapability_GetValue(Value) == 1.0);
            UIValueCapability_BeginChange(Value);
            TEST_CHECK_SUCCESS(run, UIValue_Step(&Fixture.Screen, Id, 100.0, false));
            TEST_CHECK_SUCCESS(run, UIValue_Resolve(&Fixture.Screen, Id, &Base, &Value));
            TEST_CHECK_SUCCESS(run, UIValueCapability_EndChange(Value, Base));
            TEST_CHECK(run, UIValueCapability_GetValue(Value) == 10.0);
            TestUI_Tick(run, &Fixture);
            // The two moves' Changed and the cancel of the move after code's value (back to 6); nothing for code's own changes.
            TEST_CHECK(run, (Events.Count == 3U) && (Events.Events[2].WidgetEventId == (int32_t)UIElementEvent_Cancelled)
                && (Events.Events[2].Value.Data.DoubleValue == 6.0));

            // A setting changed by code keeps the start, inside the new limits: the cancel goes back there.
            UIValueCapability_BeginChange(Value);
            TEST_CHECK_SUCCESS(run, UIValueCapability_MoveTo(Value, Base, 4.0, NULL));
            TEST_CHECK_SUCCESS(run, UIValue_SetRange(&Fixture.Screen, Id, true, 0.0, true, 5.0));
            TEST_CHECK_SUCCESS(run, UIValue_Resolve(&Fixture.Screen, Id, &Base, &Value));
            TEST_CHECK_SUCCESS(run, UIValueCapability_CancelChange(Value, Base));
            TEST_CHECK(run, UIValueCapability_GetValue(Value) == 5.0);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void CopyAndPasteGoThroughTheClipboardAsText(TestRun* run)
{
    TestUIFixture Fixture;
    ValueClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 100.0, 12.5);
        uint64_t Id = AddSlider(run, &Fixture, &Options);
        // Hovered, Ctrl+C copies its shortest text and Ctrl+V reads a number back as a whole change.
        TestUI_MovePointer(&Fixture, 150.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        Fixture.Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
        TestUI_Tap(run, &Fixture, GameKey_C);
        TEST_CHECK(run, strcmp(Clipboard.Text, "12.5") == 0);
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "  42.25 ");
        TestUI_Tap(run, &Fixture, GameKey_V);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 42.25);
        TEST_CHECK(run, (Events.Count >= 2U) && (Events.Events[Events.Count - 1U].WidgetEventId == (int32_t)UIElementEvent_Committed));
        // Hostile text: no number, a NaN, too long, past the limits (clamped).
        size_t Count = Events.Count;
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "drop table");
        TestUI_Tap(run, &Fixture, GameKey_V);
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "nan");
        TestUI_Tap(run, &Fixture, GameKey_V);
        memset(Clipboard.Text, '7', sizeof(Clipboard.Text) - 1U);
        Clipboard.Text[sizeof(Clipboard.Text) - 1U] = 0;
        TestUI_Tap(run, &Fixture, GameKey_V);
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 42.25) && (Events.Count == Count));
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "1e9");
        TestUI_Tap(run, &Fixture, GameKey_V);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 100.0);
        Fixture.Sample.KeyStates[GameKey_LeftControl] = 0U;
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A soft range's ends lie on the steps (or whole numbers) or on a hard limit, else a value would land between two steps and move at the next
 * unrelated setting; a step too small to count with at the limits' size is refused, as it would move nothing there. */
static void SoftEndsLieOnTheStepsAndAStepMustCount(TestRun* run)
{
    UIValueSettings S = UIValueSettings_CreateRange(0.0, 10.0);
    S.HasStep = true;
    S.Step = 1.0;
    S.HasSoftRange = true;
    S.SoftMinimum = 0.5;
    S.SoftMaximum = 9.5;
    const char* Reason = NULL;
    TEST_CHECK(run, !UIValueSettings_IsValid(&S, false, &Reason) && (Reason != NULL));
    S.SoftMinimum = 1.0;
    TEST_CHECK(run, !IsValid(&S));
    S.SoftMaximum = 9.0;
    TEST_CHECK(run, IsValid(&S));
    S.Step = 0.1; // 0.3 is three steps of 0.1, within a double's noise.
    S.SoftMinimum = 0.3;
    TEST_CHECK(run, IsValid(&S));
    S = UIValueSettings_CreateRange(0.0, 10.5); // An end on a hard limit that is no step counts.
    S.HasStep = true;
    S.Step = 1.0;
    S.HasSoftRange = true;
    S.SoftMinimum = 2.0;
    S.SoftMaximum = 10.5;
    TEST_CHECK(run, IsValid(&S));
    S = UIValueSettings_CreateRange(0.0, 10.0);
    S.IsInteger = true;
    S.HasSoftRange = true;
    S.SoftMinimum = 0.5;
    S.SoftMaximum = 9.0;
    TEST_CHECK(run, !IsValid(&S));
    S.SoftMinimum = 1.0;
    TEST_CHECK(run, IsValid(&S));
    S = UIValueSettings_Create(); // With no lower limit, the steps count from 0.
    S.HasStep = true;
    S.Step = 0.25;
    S.HasSoftRange = true;
    S.SoftMinimum = -1.25;
    S.SoftMaximum = 0.75;
    TEST_CHECK(run, IsValid(&S));
    S.SoftMaximum = 0.8;
    TEST_CHECK(run, !IsValid(&S));

    // The step against the largest limit's size: 2^-50 of it counts, half that does not; a denormal step never does.
    S = UIValueSettings_CreateRange(0.0, 1e6);
    S.HasStep = true;
    S.Step = 1e-10;
    TEST_CHECK(run, !IsValid(&S));
    S.Step = 1e6 * 8.8817841970012523e-16;
    TEST_CHECK(run, IsValid(&S));
    S.Step *= 0.5;
    TEST_CHECK(run, !IsValid(&S));
    S = UIValueSettings_CreateRange(-1e6, 0.0);
    S.HasStep = true;
    S.Step = 1e-10;
    TEST_CHECK(run, !IsValid(&S));
    S = UIValueSettings_Create();
    S.HasStep = true;
    S.Step = 5e-324;
    TEST_CHECK(run, !IsValid(&S));
    S.Step = DBL_MIN;
    TEST_CHECK(run, IsValid(&S));
    S.HasSoftRange = true; // A soft range's size counts too.
    S.SoftMinimum = 0.0;
    S.SoftMaximum = 1.0;
    TEST_CHECK(run, !IsValid(&S));
}

/* A huge step never sends a far value to the other limit: the step past the largest double becomes the one before it. */
static void AHugeStepSnapsToTheNearestStepThatIsANumber(TestRun* run)
{
    UIValueSettings S = UIValueSettings_Create();
    S.HasMaximum = true;
    S.Maximum = 10.0;
    S.HasStep = true;
    S.Step = 1e308;
    TEST_CHECK(run, IsValid(&S));
    TEST_CHECK(run, UIValueSettings_Normalize(&S, -1.7e308) == -1e308);
    TEST_CHECK(run, UIValueSettings_Normalize(&S, -INFINITY) == -1e308);
    TEST_CHECK(run, UIValueSettings_Normalize(&S, -0.6e308) == -1e308);
    TEST_CHECK(run, UIValueSettings_Normalize(&S, 3.0) == 0.0);
    TEST_CHECK(run, UIValueSettings_Normalize(&S, 8.0) == 10.0);
    S = UIValueSettings_Create();
    S.HasMinimum = true;
    S.Minimum = -10.0;
    S.HasStep = true;
    S.Step = 1e308;
    TEST_CHECK(run, UIValueSettings_Normalize(&S, 1.7e308) == (-10.0 + 1e308));
    TEST_CHECK(run, UIValueSettings_Normalize(&S, INFINITY) == (-10.0 + 1e308));
}


// Tests: the arithmetic's, the text's and the capability's corners.
/* The coarse snap: decimal increments of the span (a power of ten exactly too, whose logarithm may come out a hair below), a whole number
 * of steps on a stepped value, and never past the range's end. */
static void TheCoarseSnapsIncrementsFollowTheSpan(TestRun* run)
{
    UIValueSettings S = UIValueSettings_CreateRange(0.0, 100.0);
    TEST_CHECK(run, UIValueSettings_SnapCoarse(&S, 0.0, 100.0, true, 43.0, 43.0) == 40.0);
    TEST_CHECK(run, UIValueSettings_SnapCoarse(&S, 0.0, 10.0, true, 4.37, 4.37) == 4.0); // A span of 10 snaps to ones, of 9.9 to tenths.
    TEST_CHECK(run, IsNear(UIValueSettings_SnapCoarse(&S, 0.0, 9.9, true, 4.37, 4.37), 4.4, 1e-12));
    bool IsTenths = true;
    for (double Span = 1e3; Span <= 1e15; Span *= 1e3)
    {
        S = UIValueSettings_CreateRange(0.0, Span);
        IsTenths = IsTenths && IsNear(UIValueSettings_SnapCoarse(&S, 0.0, Span, true, 0.437 * Span, 0.437 * Span), 0.4 * Span, 1e-9 * Span);
    }
    TEST_CHECK(run, IsTenths);
    S = UIValueSettings_CreateRange(0.0, 100.0);
    S.HasStep = true;
    S.Step = 2.0; // Tens are five steps: 43 snaps to 40, not to the nearest step (44).
    TEST_CHECK(run, UIValueSettings_SnapCoarse(&S, 0.0, 100.0, true, 43.0, 43.0) == 40.0);
    S.Step = 30.0; // A step longer than the increment: the step itself.
    TEST_CHECK(run, UIValueSettings_SnapCoarse(&S, 0.0, 100.0, true, 43.0, 43.0) == 30.0);
    S = UIValueSettings_CreateRange(0.0, 99.9); // Ones: 99.9 rounds to 100, past the end, and comes back to it.
    TEST_CHECK(run, UIValueSettings_SnapCoarse(&S, 0.0, 99.9, true, 99.9, 99.9) == 99.9);
}

/* Steps, whole numbers and limits at their corners. */
static void StepsAndWholeNumbersKeepToTheirLimits(TestRun* run)
{
    UIValueSettings S = UIValueSettings_Create(); // No lower limit: the steps count from 0.
    S.HasStep = true;
    S.Step = 1.0;
    TEST_CHECK(run, (UIValueSettings_Normalize(&S, 2.3) == 2.0) && (UIValueSettings_Normalize(&S, -2.7) == -3.0));
    S = UIValueSettings_CreateRange(0.2, 5.8); // Whole numbers inside limits that are not whole.
    S.IsInteger = true;
    TEST_CHECK(run, (UIValueSettings_Normalize(&S, 0.2) == 1.0) && (UIValueSettings_Normalize(&S, 5.8) == 5.0));
    TEST_CHECK(run, UIValueSettings_FromShare(&S, 2.3, 2.7, 0.5) == 2.7); // A range with no whole number in it keeps its ends.
    S = UIValueSettings_CreateRange(0.0, 10.0);
    S.IsInteger = true;
    TEST_CHECK(run, (UIValueSettings_FromShare(&S, 0.0, 10.0, 0.47) == 5.0) && (UIValueSettings_FromShare(&S, 0.0, 10.0, 0.44) == 4.0));
    S = UIValueSettings_CreateRange(-10.0, 10.0); // A NaN is the lower limit, below 0 too.
    TEST_CHECK(run, UIValueSettings_Normalize(&S, NAN) == -10.0);
    S.Conversion = UIValueConversion_Power; // A step of 0 keeps a value exactly, where the conversion does not round-trip so.
    S.Exponent = 3.0;
    S.Minimum = 0.0;
    bool IsKept = true;
    for (double Value = 0.1; Value < 10.0; Value += 0.37)
    {
        IsKept = IsKept && (UIValueSettings_Step(&S, Value, 0.0, false) == Value) && (UIValueSettings_Step(&S, Value, -0.0, true) == Value);
    }
    TEST_CHECK(run, IsKept);
    S = UIValueSettings_CreateRange(0.0, 100.0); // The share of a reversed or empty range is its minimum, whatever the conversion says.
    UIValueSettings Custom = CustomSettings(4);
    TEST_CHECK(run, (UIValueSettings_FromShare(&Custom, 10.0, 0.0, 0.5) == 10.0) && (UIValueSettings_FromShare(&S, 7.0, 7.0, 0.5) == 7.0));
    TEST_CHECK(run, UIValueSettings_ToShare(&Custom, 0.0, 100.0, 150.0) == 1.0); // The custom function sees a value inside its range.
    TEST_CHECK(run, UIValueSettings_ToShare(&Custom, 0.0, 100.0, NAN) == 0.0);
}

/* What the rules count: an even conversion's two values, a list's length, a soft range's finite ends and span with no hard limits. */
static void TheRulesCountListsAndSoftEnds(TestRun* run)
{
    static const double One[] = { 4.0 };
    UIValueSettings S = UIValueSettings_CreateRange(0.0, 10.0);
    S.ValueList = One;
    S.ValueCount = 1U;
    S.Conversion = UIValueConversion_Even;
    TEST_CHECK(run, !IsValid(&S));
    S.Conversion = UIValueConversion_Linear;
    TEST_CHECK(run, IsValid(&S));
    static double Many[UI_VALUE_MAX_LIST_COUNT + 1U];
    for (size_t Index = 0U; Index <= UI_VALUE_MAX_LIST_COUNT; Index++)
    {
        Many[Index] = (double)Index;
    }
    S = UIValueSettings_Create();
    S.ValueList = Many;
    S.ValueCount = UI_VALUE_MAX_LIST_COUNT + 1U;
    TEST_CHECK(run, !IsValid(&S));
    S.ValueCount = UI_VALUE_MAX_LIST_COUNT;
    TEST_CHECK(run, IsValid(&S));
    S = UIValueSettings_Create();
    S.HasSoftRange = true;
    S.SoftMinimum = -INFINITY;
    S.SoftMaximum = 5.0;
    TEST_CHECK(run, !IsValid(&S));
    S.SoftMinimum = -DBL_MAX;
    S.SoftMaximum = DBL_MAX;
    TEST_CHECK(run, !IsValid(&S));
    S.SoftMinimum = -1.0;
    S.SoftMaximum = 1.0;
    TEST_CHECK(run, IsValid(&S));
}

/* Text at its corners: a float piece holding a double that is no float writes the double; a buffer whose NUL comes first is read up to it. */
static void TextAtItsCorners(TestRun* run)
{
    unsigned char Bytes[64];
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, Bytes, sizeof(Bytes), sizeof(unsigned char), 0U);
    TEST_CHECK_SUCCESS(run, UIValue_WriteShortestText(0.123456789, true, &Text));
    TEST_CHECK(run, strcmp((const char*)Bytes, "0.123456789") == 0);
    GenericBuffer_Clear(&Text);
    TEST_CHECK_SUCCESS(run, UIValue_WriteShortestText((double)0.1f, true, &Text));
    TEST_CHECK(run, strcmp((const char*)Bytes, "0.1") == 0);
    unsigned char Long[200];
    memset(Long, 'x', sizeof(Long));
    Long[0] = (unsigned char)'4';
    Long[1] = (unsigned char)'2';
    Long[2] = 0U;
    double Value = 0.0;
    TEST_CHECK(run, UIValue_ParseText(Long, sizeof(Long), NULL, false, &Value) && (Value == 42.0));
}

/* The capability at its corners: a cancel with nothing changed posts nothing; a quiet value is normalized; a custom conversion comes back
 * only through UIValue_SetCustomConversion; UIValue_Step takes a fine step; an event target gone means the piece's own id, with no index. */
static void TheCapabilityAtItsCorners(TestRun* run)
{
    TestUIFixture Fixture;
    ValueClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 100.0, 50.0);
        uint64_t Id = AddSlider(run, &Fixture, &Options);
        Widget* Base = NULL;
        UIValueCapability* Value = NULL;
        if (TEST_CHECK_SUCCESS(run, UIValue_Resolve(&Fixture.Screen, Id, &Base, &Value)))
        {
            UIValueCapability_BeginChange(Value);
            TEST_CHECK_SUCCESS(run, UIValueCapability_CancelChange(Value, Base));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 0U);
            TEST_CHECK_SUCCESS(run, UIValue_SetStep(&Fixture.Screen, Id, true, 1.0));
            UIValueCapability_SetValueQuietly(Value, 7.3);
            TEST_CHECK(run, UIValueCapability_GetValue(Value) == 7.0);
            TEST_CHECK_SUCCESS(run, UIValue_SetStep(&Fixture.Screen, Id, false, 0.0));
            TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 50.0));
            TEST_CHECK_SUCCESS(run, UIValue_Step(&Fixture.Screen, Id, 1.0, true));
            TEST_CHECK(run, IsNear(Get(run, &Fixture, Id), 50.1, 1e-9));
            UIValueSettings Custom = CustomSettings(0);
            TEST_CHECK_SUCCESS(run, UIValue_SetCustomConversion(&Fixture.Screen, Id, Custom.Custom.ToValue, Custom.Custom.ToShare, &Custom.Custom.Data));
            TEST_CHECK_SUCCESS(run, UIValue_SetConversion(&Fixture.Screen, Id, UIValueConversion_Linear, 0.0));
            TEST_CHECK_ERROR(run, UIValue_SetConversion(&Fixture.Screen, Id, UIValueConversion_Custom, 0.0), ErrorCode_ArgumentOutOfRange);
            // A target that is gone (or never was): its own id, with no index.
            UIValueCapability_SetEventTarget(Value, 987654U, true, 2);
            TEST_CHECK_SUCCESS(run, UIValue_Resolve(&Fixture.Screen, Id, &Base, &Value));
            TEST_CHECK_SUCCESS(run, UIValueCapability_ApplyWholeChange(Value, Base, 30.0, NULL));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 2U) && (Events.Events[1].WidgetId == Id) && !Events.Events[1].HasIndex);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}


/* A right click on the slider (at 100, 100, 220 x 24) opens its value's menu: Reset, a line, Copy, Paste. */
static bool OpenValueMenu(TestRun* run, TestUIFixture* fixture)
{
    TestUI_MovePointer(fixture, 150.0f, 112.0f);
    TestUI_Press(fixture, GameKey_MouseRight);
    TestUI_Tick(run, fixture);
    TestUI_Release(fixture, GameKey_MouseRight);
    TestUI_Ticks(run, fixture, 3U);
    Widget* Menu = NULL;
    return TEST_CHECK(run, (UIScreen_GetPopupCount(&fixture->Screen) == 1U)
        && UIScreen_TryGetWidgetById(&fixture->Screen, UIScreen_GetPopupAt(&fixture->Screen, 0U), &Menu) && (Widget_GetSubWidgetCount(Menu) == 4U));
}

static void ClickValueMenuItem(TestRun* run, TestUIFixture* fixture, size_t index)
{
    Widget* Menu = NULL;
    if (UIScreen_TryGetWidgetById(&fixture->Screen, UIScreen_GetPopupAt(&fixture->Screen, 0U), &Menu) && (index < Widget_GetSubWidgetCount(Menu)))
    {
        Rectangle Item = TestUI_GetPixelBox(run, fixture, Widget_GetId(Widget_GetSubWidgetAt(Menu, index)));
        TestUI_Click(run, fixture, Item.x + 4.0f, Item.y + (Item.height * 0.5f));
        TestUI_Tick(run, fixture);
    }
}

/* The value's menu offers only what can be done (Reset without a default, Reset and Paste while disabled: a click on them does nothing and
 * the menu stays), an item chosen after its element was disabled meanwhile does nothing, and a paste during a drag reads no clipboard. */
static void TheValuesMenuOffersOnlyWhatCanBeDone(TestRun* run)
{
    TestUIFixture Fixture;
    ValueClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        uint64_t Id = AddSlider(run, &Fixture, &Options);
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "7");
        if (OpenValueMenu(run, &Fixture))
        {
            ClickValueMenuItem(run, &Fixture, 0U); // Reset with no default.
            TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 1U);
            TestUI_Tap(run, &Fixture, GameKey_Escape);
        }
        TEST_CHECK_SUCCESS(run, UIValue_SetDefault(&Fixture.Screen, Id, true, 1.0));
        TEST_CHECK_SUCCESS(run, UIValue_SetIsEnabled(&Fixture.Screen, Id, false));
        if (OpenValueMenu(run, &Fixture))
        {
            ClickValueMenuItem(run, &Fixture, 3U); // Paste, disabled.
            ClickValueMenuItem(run, &Fixture, 0U); // Reset, disabled.
            TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.Screen) == 1U) && (Get(run, &Fixture, Id) == 5.0));
            TestUI_Tap(run, &Fixture, GameKey_Escape);
        }
        for (size_t Item = 0U; Item < 4U; Item += 3U) // Reset, then Paste, each chosen after a disable.
        {
            TEST_CHECK_SUCCESS(run, UIValue_SetIsEnabled(&Fixture.Screen, Id, true));
            if (OpenValueMenu(run, &Fixture))
            {
                TEST_CHECK_SUCCESS(run, UIValue_SetIsEnabled(&Fixture.Screen, Id, false));
                ClickValueMenuItem(run, &Fixture, Item);
                TEST_CHECK(run, Get(run, &Fixture, Id) == 5.0);
            }
        }
        // A paste during a drag reads nothing: the value cannot take it.
        TEST_CHECK_SUCCESS(run, UIValue_SetIsEnabled(&Fixture.Screen, Id, true));
        TestUI_Tick(run, &Fixture);
        size_t Reads = Clipboard.Reads;
        TestUI_MovePointer(&Fixture, 210.0f, 112.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        Fixture.Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
        TestUI_Tap(run, &Fixture, GameKey_V);
        Fixture.Sample.KeyStates[GameKey_LeftControl] = 0U;
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Clipboard.Reads == Reads) && (Get(run, &Fixture, Id) == 5.0));
    }
    TestUI_Deconstruct(run, &Fixture);
}


void UIValueTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "every_rule_is_checked_and_one_that_breaks_is_refused", EveryRuleIsCheckedAndOneThatBreaksIsRefused },
        { "a_value_list_must_rise_stay_inside_and_keep_its_limit", AValueListMustRiseStayInsideAndKeepItsLimit },
        { "a_conversion_needs_what_it_converts_with", AConversionNeedsWhatItConvertsWith },
        { "the_drag_range_is_the_soft_one_else_the_list_else_the_limits", TheDragRangeIsTheSoftOneElseTheListElseTheLimits },
        { "normalizing_clamps_then_snaps", NormalizingClampsThenSnaps },
        { "conversions_map_a_share_and_back", ConversionsMapAShareAndBack },
        { "a_custom_conversion_is_checked_every_call", ACustomConversionIsCheckedEveryCall },
        { "a_step_goes_by_the_list_the_step_or_a_hundredth_of_the_track", AStepGoesByTheListTheStepOrAHundredthOfTheTrack },
        { "a_scrubs_rate_and_a_coarse_snap_follow_the_value", AScrubsRateAndACoarseSnapFollowTheValue },
        { "the_slider_primitives_keep_their_promises", TheSliderPrimitivesKeepTheirPromises },
        { "showing_a_value_never_changes_it", ShowingAValueNeverChangesIt },
        { "a_shown_value_has_its_digits_and_no_negative_zero", AShownValueHasItsDigitsAndNoNegativeZero },
        { "typed_text_reads_as_a_number_or_nothing", TypedTextReadsAsANumberOrNothing },
        { "the_functions_work_by_id_and_refuse_what_breaks_a_rule", TheFunctionsWorkByIdAndRefuseWhatBreaksARule },
        { "a_change_is_committed_or_taken_back_with_its_mixed_state", AChangeIsCommittedOrTakenBackWithItsMixedState },
        { "codes_value_during_a_change_is_its_new_start", CodesValueDuringAChangeIsItsNewStart },
        { "soft_ends_lie_on_the_steps_and_a_step_must_count", SoftEndsLieOnTheStepsAndAStepMustCount },
        { "a_huge_step_snaps_to_the_nearest_step_that_is_a_number", AHugeStepSnapsToTheNearestStepThatIsANumber },
        { "the_coarse_snaps_increments_follow_the_span", TheCoarseSnapsIncrementsFollowTheSpan },
        { "steps_and_whole_numbers_keep_to_their_limits", StepsAndWholeNumbersKeepToTheirLimits },
        { "the_rules_count_lists_and_soft_ends", TheRulesCountListsAndSoftEnds },
        { "text_at_its_corners", TextAtItsCorners },
        { "the_capability_at_its_corners", TheCapabilityAtItsCorners },
        { "the_values_menu_offers_only_what_can_be_done", TheValuesMenuOffersOnlyWhatCanBeDone },
        { "copy_and_paste_go_through_the_clipboard_as_text", CopyAndPasteGoThroughTheClipboardAsText },
    };
    TestRun_RunSuite(run, "UIValue", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
