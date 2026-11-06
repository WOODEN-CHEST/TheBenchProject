#include "TestSuites.h"
#include "Config.h"
#include "UIScalePolicy.h"
#include "UIScreen.h"
#include <math.h>


// Tests.
// Fixed answers its own value, whatever the window, the monitor or the player's scale.
static void FixedIsTheGivenValue(TestRun* run)
{
    UIScalePolicy Policy = UIScalePolicy_Fixed(1.5f);
    TEST_CHECK(run, UIScalePolicy_GetScale(&Policy, 1.0f, 720.0f, 1080.0f) == 1.5f);
    TEST_CHECK(run, UIScalePolicy_GetScale(&Policy, 3.0f, 2160.0f, 4320.0f) == 1.5f);
    TEST_CHECK(run, UIScalePolicy_GetScale(&Policy, 1.0f, 0.0f, NAN) == 1.5f);
}

// The monitor's height against 1080 in quarters, never below 1, whatever the window's height.
static void ConstantPixelsFollowsTheMonitor(TestRun* run)
{
    static const struct
    {
        float MonitorHeight;
        float Expected;
    } Cases[] =
    {
        { 720.0f, 1.0f },   // A small monitor gets 1080p's pixels.
        { 768.0f, 1.0f },
        { 1080.0f, 1.0f },
        { 1200.0f, 1.0f },
        { 1440.0f, 1.25f }, // 1.33 in quarters.
        { 1600.0f, 1.5f },
        { 2160.0f, 2.0f },
        { 2880.0f, 2.75f },
        { 4320.0f, 4.0f },
    };
    UIScalePolicy Policy = UIScalePolicy_ConstantPixels();
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        TEST_CHECK(run, UIScalePolicy_GetScale(&Policy, 1.0f, 500.0f, Cases[Index].MonitorHeight) == Cases[Index].Expected);
        // The window's height means nothing here.
        TEST_CHECK(run, UIScalePolicy_GetScale(&Policy, 1.0f, 3000.0f, Cases[Index].MonitorHeight) == Cases[Index].Expected);
    }
}

// The window's height against 1080, with no rounding: the UI scales with the window.
static void FollowHeightFollowsTheWindow(TestRun* run)
{
    static const struct
    {
        float WindowHeight;
        float Expected;
    } Cases[] =
    {
        { 720.0f, 720.0f / 1080.0f },
        { 1080.0f, 1.0f },
        { 1440.0f, 1440.0f / 1080.0f },
        { 2160.0f, 2.0f },
        { 4320.0f, 4.0f },
    };
    UIScalePolicy Policy = UIScalePolicy_FollowHeight();
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        TEST_CHECK(run, UIScalePolicy_GetScale(&Policy, 1.0f, Cases[Index].WindowHeight, 2160.0f) == Cases[Index].Expected);
        // The monitor means nothing here.
        TEST_CHECK(run, UIScalePolicy_GetScale(&Policy, 1.0f, Cases[Index].WindowHeight, 720.0f) == Cases[Index].Expected);
    }
}

// The player's scale multiplies what the two policies with one answer, after the monitor's floor and its quarters.
static void ThePlayersScaleGoesOnTop(TestRun* run)
{
    UIScalePolicy Constant = UIScalePolicy_ConstantPixels();
    TEST_CHECK(run, UIScalePolicy_GetScale(&Constant, 2.0f, 720.0f, 720.0f) == 2.0f);
    TEST_CHECK(run, UIScalePolicy_GetScale(&Constant, 1.5f, 720.0f, 1440.0f) == 1.875f);
    TEST_CHECK(run, UIScalePolicy_GetScale(&Constant, 0.75f, 720.0f, 2160.0f) == 1.5f);
    UIScalePolicy Follow = UIScalePolicy_FollowHeight();
    TEST_CHECK(run, UIScalePolicy_GetScale(&Follow, 2.0f, 1080.0f, 720.0f) == 2.0f);
    TEST_CHECK(run, UIScalePolicy_GetScale(&Follow, 0.75f, 2160.0f, 720.0f) == 1.5f);
}

// Nothing answers below the player's lowest scale or past the most a screen takes.
static void AnswersStayWithinTheLimits(TestRun* run)
{
    UIScalePolicy Follow = UIScalePolicy_FollowHeight();
    TEST_CHECK(run, UIScalePolicy_GetScale(&Follow, 1.0f, 360.0f, 1080.0f) == CONFIG_MIN_UI_SCALE); // A third, kept to the floor.
    TEST_CHECK(run, UIScalePolicy_GetScale(&Follow, 1.0f, 540.0f, 1080.0f) == CONFIG_MIN_UI_SCALE); // Exactly the floor.
    TEST_CHECK(run, UIScalePolicy_GetScale(&Follow, 1.0f, 541.0f, 1080.0f) > CONFIG_MIN_UI_SCALE);
    TEST_CHECK(run, UIScalePolicy_GetScale(&Follow, 2.0f, 360.0f, 1080.0f) == (2.0f * 360.0f / 1080.0f));
    TEST_CHECK(run, UIScalePolicy_GetScale(&Follow, 4.0f, 8640.0f, 1080.0f) == UI_SCALE_POLICY_MAX_SCALE);
    UIScalePolicy Constant = UIScalePolicy_ConstantPixels();
    TEST_CHECK(run, UIScalePolicy_GetScale(&Constant, 4.0f, 720.0f, 4320.0f) == UI_SCALE_POLICY_MAX_SCALE); // 8K at 4 is 16.
    TEST_CHECK(run, UIScalePolicy_GetScale(&Constant, 4.0f, 720.0f, 1e30f) == UI_SCALE_POLICY_MAX_SCALE);
    TEST_CHECK(run, UIScalePolicy_GetScale(&Constant, 0.5f, 720.0f, 1080.0f) == CONFIG_MIN_UI_SCALE);
    UIScalePolicy Small = UIScalePolicy_Fixed(0.1f);
    TEST_CHECK(run, UIScalePolicy_GetScale(&Small, 1.0f, 720.0f, 1080.0f) == CONFIG_MIN_UI_SCALE);
    UIScalePolicy Large = UIScalePolicy_Fixed(100.0f);
    TEST_CHECK(run, UIScalePolicy_GetScale(&Large, 1.0f, 720.0f, 1080.0f) == UI_SCALE_POLICY_MAX_SCALE);
    // What a screen is told must be a scale it takes.
    TEST_CHECK(run, (CONFIG_MIN_UI_SCALE >= UI_SCREEN_MIN_SCALE) && (UI_SCALE_POLICY_MAX_SCALE <= UI_SCREEN_MAX_SCALE));
}

// A height that is no number above 0 gives the player's scale alone, and a player's scale that is none counts as 1.
static void BrokenNumbersGiveThePlayersScaleAlone(TestRun* run)
{
    const float BrokenHeights[] = { NAN, 0.0f, -0.0f, -1080.0f, INFINITY, -INFINITY };
    UIScalePolicy Constant = UIScalePolicy_ConstantPixels();
    UIScalePolicy Follow = UIScalePolicy_FollowHeight();
    for (size_t Index = 0U; Index < (sizeof(BrokenHeights) / sizeof(BrokenHeights[0])); Index++)
    {
        TEST_CHECK(run, UIScalePolicy_GetScale(&Constant, 1.5f, 1080.0f, BrokenHeights[Index]) == 1.5f);
        TEST_CHECK(run, UIScalePolicy_GetScale(&Follow, 1.5f, BrokenHeights[Index], 1080.0f) == 1.5f);
        UIScalePolicy BrokenFixed = UIScalePolicy_Fixed(BrokenHeights[Index]);
        TEST_CHECK(run, UIScalePolicy_GetScale(&BrokenFixed, 1.5f, 1080.0f, 1080.0f) == 1.5f);
    }
    const float BrokenScales[] = { NAN, 0.0f, -2.0f, INFINITY };
    for (size_t Index = 0U; Index < (sizeof(BrokenScales) / sizeof(BrokenScales[0])); Index++)
    {
        TEST_CHECK(run, UIScalePolicy_GetScale(&Constant, BrokenScales[Index], 1080.0f, 2160.0f) == 2.0f);
        TEST_CHECK(run, UIScalePolicy_GetScale(&Follow, BrokenScales[Index], 2160.0f, 1080.0f) == 2.0f);
    }
    TEST_CHECK(run, UIScalePolicy_GetScale(NULL, 1.5f, 2160.0f, 2160.0f) == 1.5f);
    TEST_CHECK(run, UIScalePolicy_GetScale(NULL, NAN, 2160.0f, 2160.0f) == 1.0f);
    // A player's scale out of the player's range is still kept to the limits, with nothing else to go by.
    TEST_CHECK(run, UIScalePolicy_GetScale(&Constant, 0.1f, 1080.0f, NAN) == CONFIG_MIN_UI_SCALE);
    TEST_CHECK(run, UIScalePolicy_GetScale(&Follow, 100.0f, NAN, 1080.0f) == UI_SCALE_POLICY_MAX_SCALE);
}


// Public functions.
void UIScalePolicyTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "fixed_is_the_given_value", FixedIsTheGivenValue },
        { "constant_pixels_follows_the_monitor", ConstantPixelsFollowsTheMonitor },
        { "follow_height_follows_the_window", FollowHeightFollowsTheWindow },
        { "the_players_scale_goes_on_top", ThePlayersScaleGoesOnTop },
        { "answers_stay_within_the_limits", AnswersStayWithinTheLimits },
        { "broken_numbers_give_the_players_scale_alone", BrokenNumbersGiveThePlayersScaleAlone },
    };
    TestRun_RunSuite(run, "UIScalePolicy", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
