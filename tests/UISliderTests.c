#include "TestSuites.h"
#include "TestUI.h"
#include "UIKit.h"
#include "UIPopup.h"
#include <math.h>


// Macros.
#define SLIDER_THEME \
    "{\"name\": \"sliders\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"slider\": {\"min_size\": [64, 24], \"thumb_size\": [20, 20], \"track_thickness\": 4, \"track\": {\"kind\": \"flat\"}, \"fill\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"menu\": {\"background\": {\"kind\": \"flat\"}, \"item\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#ffffff\"}, \"shortcut\": {\"normal\": \"#ffffff\"}, \"separator\": {\"kind\": \"flat\"}}}}"
/* An across slider at (100, 100), 220 x 24, its thumb 20 long: the thumb's centre travels from x 110 to x 310. */
#define ACROSS_X 100.0f
#define ACROSS_Y 100.0f
#define ACROSS_LENGTH 220.0f
#define TRACK_START 110.0f
#define TRAVEL 200.0f
/* An upright slider at (400, 100), 24 x 220: the thumb's centre travels from y 310 (the bottom) up to y 110. */
#define UPRIGHT_X 400.0f
#define UPRIGHT_Y 100.0f
#define UPRIGHT_BOTTOM 310.0f


// Static functions: helpers.
static bool IsNear(double value, double expected, double tolerance)
{
    return fabs(value - expected) <= tolerance;
}

/* Whether a value the pointer gave is the one expected: the pointer's place is a float, so its value carries a float's noise. */
static bool IsAbout(double value, double expected)
{
    return fabs(value - expected) <= (1e-5 * fmax(1.0, fabs(expected)));
}

static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, SLIDER_THEME);
}

static uint64_t AddSlider(TestRun* run, TestUIFixture* fixture, const UIValueOptions* value, UISliderOrientation orientation)
{
    UISliderOptions Options = UISliderOptions_Create();
    Options.Value = *value;
    Options.Orientation = orientation;
    Options.Element.HasPlacement = true;
    Options.Element.Placement = (orientation == UISliderOrientation_Vertical) ? UIPlacement_FromTopLeft(UPRIGHT_X, UPRIGHT_Y, 24.0f, ACROSS_LENGTH)
        : UIPlacement_FromTopLeft(ACROSS_X, ACROSS_Y, ACROSS_LENGTH, 24.0f);
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

/* The pointer's x for a share of an across slider's track. */
static float AcrossAt(double share)
{
    return TRACK_START + (float)(share * (double)TRAVEL);
}

/* The pointer's y for a share of an upright slider's track (0 at the bottom). */
static float UprightAt(double share)
{
    return UPRIGHT_BOTTOM - (float)(share * (double)TRAVEL);
}

static void SetShift(TestUIFixture* fixture, bool isDown)
{
    fixture->Sample.KeyStates[GameKey_LeftShift] = isDown ? GAME_INPUT_KEY_STATE_DOWN : 0U;
}

static void DragTo(TestRun* run, TestUIFixture* fixture, float x, float y)
{
    TestUI_MovePointer(fixture, x, y);
    TestUI_Tick(run, fixture);
}

static size_t CountEvents(const TestUIEvents* events, uint64_t id, UIElementEvent event)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < events->Count; Index++)
    {
        Count += ((events->Events[Index].WidgetId == id) && (events->Events[Index].WidgetEventId == (int32_t)event)) ? 1U : 0U;
    }
    return Count;
}


// Tests: the drag as arithmetic.
/*
 * Shift is a flag on the rate. While it is held the thumb moves by the pointer's motion times the fine factor, and at each change of Shift
 * the offset becomes the thumb less the pointer, so the value never jumps; a thumb held at an end makes the offset give way, and with the
 * pointer past that end of the box too, the thumb is under the pointer again. Blender's way would keep the press's offset through Shift's
 * changes, so letting Shift go would put the thumb back where the pointer and that offset say: a jump. This test states the rate rule.
 */
static void ShiftChangesOnlyTheRateAndTheValueNeverJumps(TestRun* run)
{
    // Every order of Shift's changes, at every few steps of a drag that goes back and forth: no move is ever a jump.
    for (int32_t Pattern = 0; Pattern < 16; Pattern++)
    {
        UISliderDrag Drag;
        UISliderDrag_Begin(&Drag, 0.5, 0.0, 0.5, 0.05, false);
        double Pointer = 0.5;
        double Previous = Drag.Share;
        bool IsSmooth = true;
        for (int32_t Step = 0; Step < 40; Step++)
        {
            bool IsFine = ((Pattern >> (Step / 10)) & 1) != 0;
            double Motion = (Step < 20) ? 0.01 : -0.012;
            Pointer += Motion;
            double Share = UISliderDrag_Move(&Drag, Pointer, IsFine, 0.1);
            double Allowed = fabs(Motion) * (IsFine ? 0.1 : 1.0) + 1e-12;
            IsSmooth = IsSmooth && (fabs(Share - Previous) <= Allowed);
            Previous = Share;
        }
        TEST_CHECK(run, IsSmooth);
    }

    // Held from the start, then let go: the thumb lags the pointer, and goes on from where it is at the full rate.
    UISliderDrag Drag;
    UISliderDrag_Begin(&Drag, 0.2, 0.0, 0.2, 0.05, true);
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 0.6, true, 0.1), 0.24, 1e-12));
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 0.6, false, 0.1), 0.24, 1e-12)); // Let go: no jump.
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 0.7, false, 0.1), 0.34, 1e-12)); // The full rate, the offset kept.
    TEST_CHECK(run, IsNear(Drag.Offset, -0.36, 1e-12));
}

static void PastAnEndTheThumbIsUnderThePointerAgain(TestRun* run)
{
    UISliderDrag Drag;
    UISliderDrag_Begin(&Drag, 0.5, 0.0, 0.5, 0.05, false);
    UISliderDrag_Move(&Drag, 0.9, true, 0.1); // The thumb lags: 0.54 under a pointer at 0.9.
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 0.95, false, 0.1), 0.59, 1e-12));
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 1.04, false, 0.1), 0.68, 1e-12)); // Past the share of 1, inside the box: the offset holds.
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 1.2, false, 0.1), 0.84, 1e-12)); // Past the box's end, the thumb short of it: no jump.
    TEST_CHECK(run, UISliderDrag_Move(&Drag, 1.5, false, 0.1) == 1.0); // The thumb at the end with the pointer past it ...
    TEST_CHECK(run, Drag.Offset == 0.0);
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 0.7, false, 0.1), 0.7, 1e-12)); // ... and under the pointer coming back.
    TEST_CHECK(run, UISliderDrag_Move(&Drag, -3.0, false, 0.1) == 0.0);
    TEST_CHECK(run, Drag.Offset == 0.0);
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 0.25, false, 0.1), 0.25, 1e-12));
    // With Shift held past an end, the thumb only stops there.
    TEST_CHECK(run, UISliderDrag_Move(&Drag, 9.0, true, 0.1) == 1.0);
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 8.9, true, 0.1), 0.99, 1e-12));
    // A pointer that is no number counts as its last place.
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, NAN, true, 0.1), 0.99, 1e-12));
    // Shift let go out there: no jump, it goes on from where it is; at the end, with the pointer past it, it is under the pointer again.
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 8.89, false, 0.1), 0.98, 1e-9));
    TEST_CHECK(run, UISliderDrag_Move(&Drag, 9.5, false, 0.1) == 1.0);
    TEST_CHECK(run, Drag.Offset == 0.0);
    // Held at an end by the press's offset with the pointer inside the box, the offset gives way: turning back moves the thumb at once.
    UISliderDrag_Begin(&Drag, 0.9, 0.04, 0.86, 0.05, false);
    TEST_CHECK(run, UISliderDrag_Move(&Drag, 0.99, false, 0.1) == 1.0);
    TEST_CHECK(run, IsNear(Drag.Offset, 0.01, 1e-12));
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 0.97, false, 0.1), 0.98, 1e-12));
    // The same at the low end.
    UISliderDrag_Begin(&Drag, 0.1, -0.04, 0.14, 0.05, false);
    TEST_CHECK(run, UISliderDrag_Move(&Drag, 0.01, false, 0.1) == 0.0);
    TEST_CHECK(run, IsNear(Drag.Offset, -0.01, 1e-12));
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 0.03, false, 0.1), 0.02, 1e-12));
}


// Tests: the slider.
static void AShiftDragOverASliderNeverJumps(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        uint64_t Id = AddSlider(run, &Fixture, &Options, UISliderOrientation_Horizontal);
        TestUI_MovePointer(&Fixture, AcrossAt(0.5), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 5.0); // On the thumb: held where grabbed.
        SetShift(&Fixture, true);
        DragTo(run, &Fixture, AcrossAt(0.9), 110.0f);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 5.4)); // A tenth of the rate.
        SetShift(&Fixture, false);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 5.4)); // Let go, still.
        DragTo(run, &Fixture, AcrossAt(0.8), 110.0f);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 4.4)); // The full rate from where it was.
        SetShift(&Fixture, true);
        DragTo(run, &Fixture, AcrossAt(0.7), 110.0f);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 4.3));
        SetShift(&Fixture, false);
        // Past the box's end the thumb is under the pointer again.
        DragTo(run, &Fixture, 900.0f, 110.0f);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 10.0);
        DragTo(run, &Fixture, AcrossAt(0.25), 110.0f);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 2.5));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, CountEvents(&Events, Id, UIElementEvent_Committed) == 1U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ASlowDragOverAStepStillMovesIt(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        Options.Settings.HasStep = true;
        Options.Settings.Step = 1.0;
        uint64_t Id = AddSlider(run, &Fixture, &Options, UISliderOrientation_Horizontal);
        TestUI_MovePointer(&Fixture, AcrossAt(0.5), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        SetShift(&Fixture, true);
        // Each move of 2 pixels at a tenth of the rate is a hundredth of a step: alone it snaps back, built up it moves.
        for (int32_t Move = 1; Move <= 30; Move++)
        {
            DragTo(run, &Fixture, AcrossAt(0.5) + (2.0f * (float)Move), 110.0f);
        }
        TEST_CHECK(run, Get(run, &Fixture, Id) == 5.0); // 60 pixels at a tenth: 0.3 of a step.
        for (int32_t Move = 31; Move <= 60; Move++)
        {
            DragTo(run, &Fixture, AcrossAt(0.5) + (2.0f * (float)Move), 110.0f);
        }
        TEST_CHECK(run, Get(run, &Fixture, Id) == 6.0); // 120 pixels: 0.6 of a step, the next one.
        SetShift(&Fixture, false);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AnUprightSliderRisesUpward(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 10.0, 0.0);
        Options.Settings.HasStep = true;
        Options.Settings.Step = 1.0;
        uint64_t Id = AddSlider(run, &Fixture, &Options, UISliderOrientation_Vertical);
        UISliderOrientation Orientation = UISliderOrientation_Horizontal;
        TEST_CHECK_SUCCESS(run, UISlider_GetOrientation(&Fixture.Screen, Id, &Orientation));
        TEST_CHECK(run, Orientation == UISliderOrientation_Vertical);
        // A press near the top is high; dragging down lowers it; past the bottom it is at its limit.
        TestUI_MovePointer(&Fixture, UPRIGHT_X + 12.0f, UprightAt(0.8));
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 8.0);
        DragTo(run, &Fixture, UPRIGHT_X + 12.0f, UprightAt(0.3));
        TEST_CHECK(run, Get(run, &Fixture, Id) == 3.0);
        DragTo(run, &Fixture, UPRIGHT_X + 12.0f, 490.0f);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 0.0);
        DragTo(run, &Fixture, UPRIGHT_X + 12.0f, -40.0f);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 10.0);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);

        // Entered, all four arrows: up and right higher, down and left lower, each press a whole change.
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 5.0));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        GameKey Keys[] = { GameKey_Up, GameKey_Right, GameKey_Down, GameKey_Left };
        double Expected[] = { 6.0, 7.0, 6.0, 5.0 };
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            size_t Committed = CountEvents(&Events, Id, UIElementEvent_Committed);
            TestUI_Tap(run, &Fixture, Keys[Index]);
            TEST_CHECK(run, (Get(run, &Fixture, Id) == Expected[Index]) && (CountEvents(&Events, Id, UIElementEvent_Committed) == Committed + 1U));
        }
        TEST_CHECK_SUCCESS(run, UISlider_SetOrientation(&Fixture.Screen, Id, UISliderOrientation_Horizontal));
        TEST_CHECK_ERROR(run, UISlider_SetOrientation(&Fixture.Screen, Id, (UISliderOrientation)7), ErrorCode_ArgumentOutOfRange);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ShiftWithAnArrowOrTheWheelIsAFineStep(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        uint64_t Id = AddSlider(run, &Fixture, &Options, UISliderOrientation_Horizontal);
        TestUI_Tap(run, &Fixture, GameKey_Right); // Not entered: the arrow is not its.
        TEST_CHECK(run, Get(run, &Fixture, Id) == 5.0);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 5.1));
        SetShift(&Fixture, true);
        TestUI_Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 5.11));
        TestUI_MovePointer(&Fixture, AcrossAt(0.9), 110.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Wheel(&Fixture, 1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 5.12));
        SetShift(&Fixture, false);
        // A stepped slider: a fine step is still a step.
        TEST_CHECK_SUCCESS(run, UIValue_SetStep(&Fixture.Screen, Id, true, 1.0));
        SetShift(&Fixture, true);
        TestUI_Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 6.0);
        SetShift(&Fixture, false);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AMiddleClickResetsAndAMiddleDragOffDoesNot(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 10.0, 7.0);
        Options.Settings.HasDefault = true;
        Options.Settings.Default = 2.0;
        uint64_t Id = AddSlider(run, &Fixture, &Options, UISliderOrientation_Horizontal);
        TestUI_MovePointer(&Fixture, AcrossAt(0.2), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 7.0); // At the press, nothing yet.
        TestUI_Release(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 2.0);
        TEST_CHECK(run, (CountEvents(&Events, Id, UIElementEvent_Changed) == 1U) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U));
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 7.0));
        TestUI_Press(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        DragTo(run, &Fixture, 800.0f, 400.0f);
        TestUI_Release(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 7.0); // Let go off it: nothing.
        // Without a default, a middle click does nothing.
        TEST_CHECK_SUCCESS(run, UIValue_SetDefault(&Fixture.Screen, Id, false, 0.0));
        TestUI_MovePointer(&Fixture, AcrossAt(0.2), 110.0f);
        TestUI_Click(run, &Fixture, AcrossAt(0.2), 110.0f);
        TEST_CHECK(run, Get(run, &Fixture, Id) != 7.0); // (a left click moves it: the track was pressed)
        double Before = Get(run, &Fixture, Id);
        TestUI_Press(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, Id) == Before);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EveryValueOfAListIsReachableEvenly(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        static const double List[] = { 1.0, 2.0, 5.0, 10.0, 100.0 };
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 100.0, 1.0);
        Options.Settings.ValueList = List;
        Options.Settings.ValueCount = 5U;
        Options.Settings.Conversion = UIValueConversion_Even;
        uint64_t Id = AddSlider(run, &Fixture, &Options, UISliderOrientation_Horizontal);
        TestUI_MovePointer(&Fixture, AcrossAt(0.0), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        bool IsEven = true;
        for (size_t Index = 0U; Index < 5U; Index++)
        {
            DragTo(run, &Fixture, AcrossAt((double)Index / 4.0), 110.0f);
            IsEven = IsEven && (Get(run, &Fixture, Id) == List[Index]);
        }
        TEST_CHECK(run, IsEven);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        // The arrows go from value to value.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tap(run, &Fixture, GameKey_Left);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 10.0);
        TestUI_Tap(run, &Fixture, GameKey_Home);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 1.0);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ALogSlidersMiddleIsTheGeometricMean(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(1.0, 10000.0, 1.0);
        Options.Settings.Conversion = UIValueConversion_Log;
        uint64_t Id = AddSlider(run, &Fixture, &Options, UISliderOrientation_Horizontal);
        TestUI_Click(run, &Fixture, AcrossAt(0.5), 110.0f);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 100.0));
        // A value set by code keeps its place: showing it never moves it.
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 3.7));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 3.7);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ARightClickOffersResetCopyAndPasteButNotDuringADrag(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIValueOptions Options = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        uint64_t Id = AddSlider(run, &Fixture, &Options, UISliderOrientation_Horizontal);
        TestUI_MovePointer(&Fixture, AcrossAt(0.5), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 1U);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 0U);
        // During a left drag a right click opens nothing: the menu would take the focus and end the drag.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 0U);
        DragTo(run, &Fixture, AcrossAt(0.7), 110.0f);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 7.0)); // Still dragging.
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

// Tests: what an independent review found.
/* The pointer along a slider's track at a share, @p past pixels beyond it (upright: up is higher, so past the top is a smaller y). */
static Vector2 AlongTrack(bool isUpright, double share, float past)
{
    return isUpright ? (Vector2){ .x = UPRIGHT_X + 12.0f, .y = UprightAt(share) - past } : (Vector2){ .x = AcrossAt(share) + past, .y = ACROSS_Y + 12.0f };
}

static void DragAlong(TestRun* run, TestUIFixture* fixture, bool isUpright, double share, float past)
{
    Vector2 Place = AlongTrack(isUpright, share, past);
    DragTo(run, fixture, Place.x, Place.y);
}

/* Shift let go with the pointer past an end: the value goes on from where it is, across and upright. */
static void ShiftLetGoPastAnEndNeverJumps(TestRun* run)
{
    for (int Orientation = 0; Orientation < 2; Orientation++)
    {
        TestUIFixture Fixture;
        if (Construct(run, &Fixture))
        {
            bool IsUpright = Orientation == 1;
            UIValueOptions Value = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
            uint64_t Id = AddSlider(run, &Fixture, &Value, IsUpright ? UISliderOrientation_Vertical : UISliderOrientation_Horizontal);
            Vector2 Thumb = AlongTrack(IsUpright, 0.5, 0.0f);
            TestUI_MovePointer(&Fixture, Thumb.x, Thumb.y);
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            DragAlong(run, &Fixture, IsUpright, 1.0, 590.0f); // Far past the high end.
            TEST_CHECK(run, Get(run, &Fixture, Id) == 10.0);
            SetShift(&Fixture, true);
            DragAlong(run, &Fixture, IsUpright, 1.0, 490.0f); // 100 pixels back, slowed: half a unit.
            TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 9.5));
            SetShift(&Fixture, false);
            DragAlong(run, &Fixture, IsUpright, 1.0, 489.0f); // Let go out there, a pixel on: no jump back to 10.
            TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 9.45));
            DragAlong(run, &Fixture, IsUpright, 1.0, 700.0f); // At the end again with the pointer past it ...
            TEST_CHECK(run, Get(run, &Fixture, Id) == 10.0);
            DragAlong(run, &Fixture, IsUpright, 0.25, 0.0f); // ... and under the pointer coming back.
            TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 2.5));
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
        }
        TestUI_Deconstruct(run, &Fixture);
    }
}

/* A value past its soft range sits at the end it passed; a drag goes on from the value, on the range around it, never jumping into the soft
 * range. */
static void ADragPastASoftRangeGoesOnFromTheValue(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIValueOptions Value = UIValueOptions_CreateRange(0.0, 1000.0, 5.0);
        Value.Settings.HasSoftRange = true;
        Value.Settings.SoftMinimum = 0.0;
        Value.Settings.SoftMaximum = 10.0;
        uint64_t Id = AddSlider(run, &Fixture, &Value, UISliderOrientation_Horizontal);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 500.0));
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK(run, UISliderCapability_GetShare(&((UISliderLookBase*)(void*)Base)->Slider) == 1.0);
        }
        TestUI_MovePointer(&Fixture, AcrossAt(1.0), ACROSS_Y + 12.0f); // On the thumb, at the end.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 500.0);
        DragTo(run, &Fixture, AcrossAt(1.0) - 2.0f, ACROSS_Y + 12.0f); // 2 pixels of 200 on [0, 500].
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 495.0));
        DragTo(run, &Fixture, AcrossAt(0.5), ACROSS_Y + 12.0f);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 250.0));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 250.0));
        // Code's value past the soft range during a drag: the drag goes on from it, on the range around it.
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 5.0));
        TestUI_MovePointer(&Fixture, AcrossAt(0.5), ACROSS_Y + 12.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 800.0));
        DragTo(run, &Fixture, AcrossAt(0.5) - 2.0f, ACROSS_Y + 12.0f);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 792.0));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A mixed slider at its default made one by a middle click: a change, committed. A press that moves nothing while code sets the value
 * commits nothing at its release: code's value is no part of the change. */
static void MixedMadeOneIsAChangeAndCodesValueIsNot(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIValueOptions Value = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        Value.IsMixed = true;
        Value.Settings.HasDefault = true;
        Value.Settings.Default = 5.0;
        uint64_t Id = AddSlider(run, &Fixture, &Value, UISliderOrientation_Horizontal);
        TestUI_MovePointer(&Fixture, AcrossAt(0.2), ACROSS_Y + 12.0f);
        TestUI_Press(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        bool IsMixed = true;
        TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, Id, &IsMixed));
        TEST_CHECK(run, !IsMixed && (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U));

        TestUI_MovePointer(&Fixture, AcrossAt(0.5), ACROSS_Y + 12.0f); // On the thumb: the press moves nothing.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 8.0));
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 8.0) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the drag's and the slider's corners.
static Vector2 MeasureSlider(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    Widget* Found = NULL;
    Vector2 Size = { .x = -1.0f, .y = -1.0f };
    if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, id, &Found)))
    {
        RenderContext Context;
        RenderContext_CreateMeasuring(&Context, (Vector2){ .x = TEST_UI_SURFACE_WIDTH, .y = TEST_UI_SURFACE_HEIGHT },
            TEST_UI_SURFACE_WIDTH / TEST_UI_SURFACE_HEIGHT, RenderTargetPosition_Centered());
        UIMeasureContext Sizing = { .Measure = &Context };
        TEST_CHECK_SUCCESS(run, Widget_Measure(Found, &Sizing, UI_MEASURE_UNBOUNDED_WIDTH, &Size));
    }
    return Size;
}

/* The fine factor is the value's own; the edge is the box's (half a thumb past the track's end), not the track's; code's value during a
 * drag is where the drag goes on from; Shift with the left arrow is fine too; a middle press during a drag, let go after it, resets
 * nothing. */
static void TheDragsCornersHold(TestRun* run)
{
    UISliderDrag Drag;
    UISliderDrag_Begin(&Drag, 0.2, 0.0, 0.2, 0.05, true);
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 0.6, true, 0.5), 0.4, 1e-12));
    TEST_CHECK(run, IsNear(UISliderDrag_Move(&Drag, 0.8, true, 2.0), 0.6, 1e-12)); // A factor out of (0, 1] counts as 1.
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIValueOptions Value = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        Value.Settings.HasDefault = true;
        Value.Settings.Default = 1.0;
        uint64_t Id = AddSlider(run, &Fixture, &Value, UISliderOrientation_Horizontal);
        float Y = ACROSS_Y + 12.0f;
        TestUI_MovePointer(&Fixture, AcrossAt(0.5) + 2.0f, Y); // On the thumb, a hundredth right of its middle.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        DragTo(run, &Fixture, AcrossAt(1.0) + 4.0f, Y); // Inside the box's end: the thumb held at the end, the offset gives way ...
        TEST_CHECK(run, Get(run, &Fixture, Id) == 10.0);
        DragTo(run, &Fixture, AcrossAt(0.5), Y); // ... so it is two hundredths short of the pointer coming back.
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 4.8));
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 2.0)); // Code's value: the drag goes on from it.
        DragTo(run, &Fixture, AcrossAt(0.5) + 2.0f, Y);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 2.1));
        TestUI_Press(&Fixture, GameKey_MouseMiddle); // A middle press during the drag ...
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseMiddle); // ... let go after it ended: no reset.
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 2.1));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        SetShift(&Fixture, true);
        TestUI_Tap(run, &Fixture, GameKey_Left);
        SetShift(&Fixture, false);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, Id), 2.09));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Made disabled, it takes no focus; turned upright, it is measured again with its sizes swapped. */
static void ItsFocusAndSizeFollowItsStateAndOrientation(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIValueOptions Value = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        Value.IsEnabled = false;
        uint64_t Id = AddSlider(run, &Fixture, &Value, UISliderOrientation_Horizontal);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK(run, !Widget_IsFocusable(Base));
        }
        Vector2 Across = MeasureSlider(run, &Fixture, Id);
        TEST_CHECK_SUCCESS(run, UISlider_SetOrientation(&Fixture.Screen, Id, UISliderOrientation_Vertical));
        Vector2 Upright = MeasureSlider(run, &Fixture, Id);
        TEST_CHECK(run, (Across.x == 64.0f) && (Across.y == 24.0f) && (Upright.x == 24.0f) && (Upright.y == 64.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}


void UISliderTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "shift_changes_only_the_rate_and_the_value_never_jumps", ShiftChangesOnlyTheRateAndTheValueNeverJumps },
        { "past_an_end_the_thumb_is_under_the_pointer_again", PastAnEndTheThumbIsUnderThePointerAgain },
        { "a_shift_drag_over_a_slider_never_jumps", AShiftDragOverASliderNeverJumps },
        { "a_slow_drag_over_a_step_still_moves_it", ASlowDragOverAStepStillMovesIt },
        { "an_upright_slider_rises_upward", AnUprightSliderRisesUpward },
        { "shift_with_an_arrow_or_the_wheel_is_a_fine_step", ShiftWithAnArrowOrTheWheelIsAFineStep },
        { "a_middle_click_resets_and_a_middle_drag_off_does_not", AMiddleClickResetsAndAMiddleDragOffDoesNot },
        { "every_value_of_a_list_is_reachable_evenly", EveryValueOfAListIsReachableEvenly },
        { "a_log_sliders_middle_is_the_geometric_mean", ALogSlidersMiddleIsTheGeometricMean },
        { "a_right_click_offers_reset_copy_and_paste_but_not_during_a_drag", ARightClickOffersResetCopyAndPasteButNotDuringADrag },
        { "shift_let_go_past_an_end_never_jumps", ShiftLetGoPastAnEndNeverJumps },
        { "a_drag_past_a_soft_range_goes_on_from_the_value", ADragPastASoftRangeGoesOnFromTheValue },
        { "mixed_made_one_is_a_change_and_codes_value_is_not", MixedMadeOneIsAChangeAndCodesValueIsNot },
        { "the_drags_corners_hold", TheDragsCornersHold },
        { "its_focus_and_size_follow_its_state_and_orientation", ItsFocusAndSizeFollowItsStateAndOrientation },
    };
    TestRun_RunSuite(run, "UISlider", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
