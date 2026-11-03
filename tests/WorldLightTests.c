#include <math.h>
#include <string.h>
#include "TestSuites.h"
#include "TestText.h"
#include "TestWorld.h"
#include "WorldLightComponent.h"
#include "WorldLightFlicker.h"
#include "WorldObject.h"


// Macros.
/** Degrees to radians, for readable angles. */
#define DEGREES(degrees) ((float)((degrees) * 3.14159265358979323846 / 180.0))
/** The flicker's samples: sixty a second (a frame each) over two minutes. */
#define FLICKER_SAMPLE_RATE 60.0
#define FLICKER_SAMPLE_COUNT 7200


// Types.
/* What a stretch of a flicker's samples shows. */
typedef struct FlickerStatsStruct
{
    float Minimum;
    float Maximum;
    double Mean;
    /** The largest change between two samples a frame apart. */
    float LargestStep;
    /** The share of samples at the darkest, and the longest stretch of them, in seconds. */
    double DarkShare;
    double LongestDarkSeconds;
    /** The share of samples within a fifth of the amount of full brightness. */
    double NearFullShare;
} FlickerStats;


// Static functions.
/* Creates an object with a light component in @p world; returns the light (world-owned), or NULL with a failed check. */
static WorldLightComponent* AddLight(TestRun* run, World* world, const char* name)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, (const unsigned char*)name, &Object))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(Object), WorldComponentType_Light,
            &Component)))
    {
        return NULL;
    }
    return WorldLightComponent_FromComponent(Component);
}

/* Whether a compound holds @p key (a failed lookup is recorded and counts as absent). */
static bool HasKey(TestRun* run, JSONCompound* compound, const unsigned char* key)
{
    JSONObjectValue Found;
    bool IsFound = false;
    return TEST_CHECK_SUCCESS(run, JSONCompound_GetOptional(compound, key, &Found, &IsFound)) && IsFound;
}

/* Samples a flicker from @p start for FLICKER_SAMPLE_COUNT frames. */
static FlickerStats MeasureFlicker(WorldLightFlicker flicker, float amount, float speed, double start, uint64_t seed)
{
    FlickerStats Stats = { .Minimum = 2.0f, .Maximum = -1.0f };
    float Darkest = 1.0f - amount;
    float Last = 0.0f;
    int DarkRun = 0;
    int LongestDarkRun = 0;
    int DarkCount = 0;
    int NearFullCount = 0;
    double Sum = 0.0;
    for (int Sample = 0; Sample < FLICKER_SAMPLE_COUNT; Sample++)
    {
        float Factor = WorldLightFlicker_GetFactor(flicker, amount, speed, start + ((double)Sample / FLICKER_SAMPLE_RATE),
            seed);
        Stats.Minimum = fminf(Stats.Minimum, Factor);
        Stats.Maximum = fmaxf(Stats.Maximum, Factor);
        Sum += (double)Factor;
        if (Sample > 0)
        {
            Stats.LargestStep = fmaxf(Stats.LargestStep, fabsf(Factor - Last));
        }
        Last = Factor;
        if (Factor <= Darkest)
        {
            DarkCount++;
            DarkRun++;
            LongestDarkRun = (DarkRun > LongestDarkRun) ? DarkRun : LongestDarkRun;
        }
        else
        {
            DarkRun = 0;
        }
        if (Factor >= (1.0f - (0.2f * amount)))
        {
            NearFullCount++;
        }
    }
    Stats.Mean = Sum / (double)FLICKER_SAMPLE_COUNT;
    Stats.DarkShare = (double)DarkCount / (double)FLICKER_SAMPLE_COUNT;
    Stats.LongestDarkSeconds = (double)LongestDarkRun / FLICKER_SAMPLE_RATE;
    Stats.NearFullShare = (double)NearFullCount / (double)FLICKER_SAMPLE_COUNT;
    return Stats;
}

/* The largest bend of a candle at full depth over its first ten seconds (the second difference of its values a tenth of
 * a millisecond apart): a smooth waver's shrinks with the step squared, a kink's only with the step. */
static float MeasureCandleBend(uint64_t seed)
{
    const double Step = 1e-4;
    float Bend = 0.0f;
    for (int Sample = 1; Sample < 100000; Sample++)
    {
        double Seconds = (double)Sample * Step;
        float Before = WorldLightFlicker_GetFactor(WorldLightFlicker_Candle, 1.0f, 1.0f, Seconds - Step, seed);
        float At = WorldLightFlicker_GetFactor(WorldLightFlicker_Candle, 1.0f, 1.0f, Seconds, seed);
        float After = WorldLightFlicker_GetFactor(WorldLightFlicker_Candle, 1.0f, 1.0f, Seconds + Step, seed);
        Bend = fmaxf(Bend, fabsf((Before - At) - (At - After)));
    }
    return Bend;
}

/* The correlation of two candles' values at full depth over FLICKER_SAMPLE_COUNT frames, seeded @p a and @p b. */
static double MeasureCandleCorrelation(uint64_t a, uint64_t b)
{
    double SumA = 0.0;
    double SumB = 0.0;
    double SumAA = 0.0;
    double SumBB = 0.0;
    double SumAB = 0.0;
    for (int Sample = 0; Sample < FLICKER_SAMPLE_COUNT; Sample++)
    {
        double Seconds = (double)Sample / FLICKER_SAMPLE_RATE;
        double ValueA = (double)WorldLightFlicker_GetFactor(WorldLightFlicker_Candle, 1.0f, 1.0f, Seconds, a);
        double ValueB = (double)WorldLightFlicker_GetFactor(WorldLightFlicker_Candle, 1.0f, 1.0f, Seconds, b);
        SumA += ValueA;
        SumB += ValueB;
        SumAA += ValueA * ValueA;
        SumBB += ValueB * ValueB;
        SumAB += ValueA * ValueB;
    }
    double Count = (double)FLICKER_SAMPLE_COUNT;
    double MeanA = SumA / Count;
    double MeanB = SumB / Count;
    double Spread = sqrt(((SumAA / Count) - (MeanA * MeanA)) * ((SumBB / Count) - (MeanB * MeanB)));
    return (Spread > 0.0) ? (((SumAB / Count) - (MeanA * MeanB)) / Spread) : 1.0;
}

/* A new light starts as a white point light of intensity 1 and range 1, on, with the documented cone and a flicker of
 * none (0.3 deep at speed 1), casting no shadow. */
static void NewLightHasItsDefaults(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldLightComponent* Light = AddLight(run, &TestWorld, "lamp");
    if (Light != NULL)
    {
        Color White = WorldLightComponent_GetColor(Light);
        TEST_CHECK(run, WorldLightComponent_IsOn(Light) && (WorldLightComponent_GetType(Light) == WorldLightType_Point)
            && (White.r == 255) && (White.g == 255) && (White.b == 255) && (White.a == 255));
        TEST_CHECK(run, (WorldLightComponent_GetIntensity(Light) == WORLD_LIGHT_COMPONENT_DEFAULT_INTENSITY)
            && (WorldLightComponent_GetRange(Light) == WORLD_LIGHT_COMPONENT_DEFAULT_RANGE)
            && !WorldLightComponent_CastsShadows(Light));
        TEST_CHECK(run, (WorldLightComponent_GetInnerAngle(Light) == DEGREES(25.0))
            && (WorldLightComponent_GetOuterAngle(Light) == DEGREES(35.0)));
        TEST_CHECK(run, (WorldLightComponent_GetFlicker(Light) == WorldLightFlicker_None)
            && (WorldLightComponent_GetFlickerAmount(Light) == 0.3f)
            && (WorldLightComponent_GetFlickerSpeed(Light) == 1.0f)
            && (WorldLightComponent_GetFlickerFactor(Light, 12.5) == 1.0f));
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

/* The cone's angles lie strictly between 0 and 90 degrees, the setter of the pair refuses an inner angle past the outer
 * (changing nothing), and the cone in effect takes the outer angle for an inner one past it (the table checks each
 * angle alone, so a file can set them in any order). */
static void ConeAnglesFollowTheirRules(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldLightComponent* Light = AddLight(run, &TestWorld, "spot");
    if (Light == NULL)
    {
        TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
        return;
    }

    uint32_t Revision = WorldComponent_GetRevision(&Light->Base);
    TEST_CHECK_SUCCESS(run, WorldLightComponent_SetCone(Light, DEGREES(10.0), DEGREES(60.0)));
    TEST_CHECK_SUCCESS(run, WorldLightComponent_SetCone(Light, DEGREES(30.0), DEGREES(30.0))); // a hard edge
    TEST_CHECK(run, (WorldLightComponent_GetInnerAngle(Light) == DEGREES(30.0))
        && (WorldLightComponent_GetOuterAngle(Light) == DEGREES(30.0))
        && (WorldComponent_GetRevision(&Light->Base) == (Revision + 2U)));

    Revision = WorldComponent_GetRevision(&Light->Base);
    const float BadAngles[] = { 0.0f, -0.1f, DEGREES(90.0), DEGREES(120.0), NAN, INFINITY };
    for (size_t Index = 0; Index < (sizeof(BadAngles) / sizeof(BadAngles[0])); Index++)
    {
        TEST_CHECK_ERROR(run, WorldLightComponent_SetCone(Light, BadAngles[Index], DEGREES(45.0)),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldLightComponent_SetCone(Light, DEGREES(5.0), BadAngles[Index]),
            ErrorCode_ArgumentOutOfRange);
    }
    TEST_CHECK_ERROR(run, WorldLightComponent_SetCone(Light, DEGREES(40.0), DEGREES(35.0)), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, (WorldLightComponent_GetInnerAngle(Light) == DEGREES(30.0))
        && (WorldLightComponent_GetOuterAngle(Light) == DEGREES(30.0))
        && (WorldComponent_GetRevision(&Light->Base) == Revision));
    TEST_CHECK_ERROR(run, WorldLightComponent_SetCone(NULL, DEGREES(1.0), DEGREES(2.0)), ErrorCode_IllegalArgument);

    // Through the table each angle is checked alone: an inner angle past the outer is kept, and the cone in effect has
    // a hard edge at the outer angle.
    const PropertyTable* Table = WorldLightComponent_GetPropertyTable();
    PropertyValue Wide = PropertyValue_FromAngle(DEGREES(50.0));
    TEST_CHECK_SUCCESS(run, Properties_Set(Table, Light, u8"inner_angle", &Wide));
    WorldLightCone Cone = WorldLightComponent_GetCone(Light);
    TEST_CHECK(run, (WorldLightComponent_GetInnerAngle(Light) == DEGREES(50.0)) && (Cone.InnerAngle == DEGREES(30.0))
        && (Cone.OuterAngle == DEGREES(30.0)));
    PropertyValue Right = PropertyValue_FromAngle(DEGREES(90.0));
    TEST_CHECK_ERROR(run, Properties_Set(Table, Light, u8"outer_angle", &Right), ErrorCode_ArgumentOutOfRange);
    PropertyValue Wider = PropertyValue_FromAngle(DEGREES(70.0));
    TEST_CHECK_SUCCESS(run, Properties_Set(Table, Light, u8"outer_angle", &Wider));
    Cone = WorldLightComponent_GetCone(Light);
    TEST_CHECK(run, (Cone.InnerAngle == DEGREES(50.0)) && (Cone.OuterAngle == DEGREES(70.0)));

    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

/* Every new setter advances the revision once and refuses what its row refuses, changing nothing; the type and the
 * flicker take only their enums' values. */
static void SettersAdvanceTheRevisionAndRefuseBadValues(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldLightComponent* Light = AddLight(run, &TestWorld, "lamp");
    if (Light == NULL)
    {
        TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
        return;
    }

    uint32_t Revision = WorldComponent_GetRevision(&Light->Base);
    TEST_CHECK_SUCCESS(run, WorldLightComponent_SetIsOn(Light, false));
    TEST_CHECK_SUCCESS(run, WorldLightComponent_SetType(Light, WorldLightType_Spot));
    TEST_CHECK_SUCCESS(run, WorldLightComponent_SetFlicker(Light, WorldLightFlicker_Broken));
    TEST_CHECK_SUCCESS(run, WorldLightComponent_SetFlickerAmount(Light, 1.0f));
    TEST_CHECK_SUCCESS(run, WorldLightComponent_SetFlickerSpeed(Light, 0.0f));
    TEST_CHECK_SUCCESS(run, WorldLightComponent_SetCastsShadows(Light, true));
    TEST_CHECK(run, (WorldComponent_GetRevision(&Light->Base) == (Revision + 6U)) && !WorldLightComponent_IsOn(Light)
        && (WorldLightComponent_GetType(Light) == WorldLightType_Spot)
        && (WorldLightComponent_GetFlicker(Light) == WorldLightFlicker_Broken)
        && (WorldLightComponent_GetFlickerAmount(Light) == 1.0f) && (WorldLightComponent_GetFlickerSpeed(Light) == 0.0f));

    Revision = WorldComponent_GetRevision(&Light->Base);
    TEST_CHECK_ERROR(run, WorldLightComponent_SetType(Light, (WorldLightType)2), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, WorldLightComponent_SetFlicker(Light, (WorldLightFlicker)4), ErrorCode_ArgumentOutOfRange);
    const float BadAmounts[] = { -0.01f, 1.01f, NAN };
    const float BadSpeeds[] = { -1.0f, WORLD_LIGHT_FLICKER_MAX_SPEED * 2.0f, INFINITY };
    for (size_t Index = 0; Index < 3U; Index++)
    {
        TEST_CHECK_ERROR(run, WorldLightComponent_SetFlickerAmount(Light, BadAmounts[Index]),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldLightComponent_SetFlickerSpeed(Light, BadSpeeds[Index]), ErrorCode_ArgumentOutOfRange);
    }
    TEST_CHECK(run, (WorldComponent_GetRevision(&Light->Base) == Revision)
        && (WorldLightComponent_GetType(Light) == WorldLightType_Spot)
        && (WorldLightComponent_GetFlickerAmount(Light) == 1.0f) && (WorldLightComponent_GetFlickerSpeed(Light) == 0.0f));
    TEST_CHECK_ERROR(run, WorldLightComponent_SetIsOn(NULL, true), ErrorCode_IllegalArgument);

    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

/* JSON names the type and the flicker, writes the angles in degrees, leaves the defaults out and reads it all back; a
 * bad value refuses the whole read. */
static void LightReadsAndWritesItsJson(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    JSONObjectPool* Pool = NULL;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldLightComponent* Light = AddLight(run, &TestWorld, "lamp");
    WorldLightComponent* Copy = AddLight(run, &TestWorld, "copy");
    if ((Light == NULL) || (Copy == NULL) || !TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
        return;
    }

    const PropertyTable* Table = WorldLightComponent_GetPropertyTable();
    JSONObjectValue Value;
    if (TestText_ParseJSON(run, Pool, "{\"type\": \"spot\", \"inner_angle\": 20, \"outer_angle\": 40, \"is_on\": false, "
        "\"flicker\": \"fluorescent\", \"flicker_amount\": 0.5, \"flicker_speed\": 2, \"intensity\": 3}", &Value))
    {
        TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Value.Value.Compound, Light, NULL));
        WorldLightCone Cone = WorldLightComponent_GetCone(Light);
        TEST_CHECK(run, (WorldLightComponent_GetType(Light) == WorldLightType_Spot)
            && (fabsf(Cone.InnerAngle - DEGREES(20.0)) < 1e-6f) && (fabsf(Cone.OuterAngle - DEGREES(40.0)) < 1e-6f)
            && !WorldLightComponent_IsOn(Light) && (WorldLightComponent_GetIntensity(Light) == 3.0f));
        TEST_CHECK(run, (WorldLightComponent_GetFlicker(Light) == WorldLightFlicker_Fluorescent)
            && (WorldLightComponent_GetFlickerAmount(Light) == 0.5f) && (WorldLightComponent_GetFlickerSpeed(Light) == 2.0f));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    const char* const BadTexts[] =
    {
        "{\"type\": \"area\"}", "{\"flicker\": \"strobe\"}", "{\"outer_angle\": 90}", "{\"inner_angle\": 0}",
        "{\"flicker_amount\": 2}"
    };
    for (size_t Index = 0; Index < (sizeof(BadTexts) / sizeof(BadTexts[0])); Index++)
    {
        if (TestText_ParseJSON(run, Pool, BadTexts[Index], &Value))
        {
            TEST_CHECK_ERROR(run, Properties_ReadJSON(Table, Value.Value.Compound, Light, NULL), ErrorCode_InvalidJSON);
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
    }
    TEST_CHECK(run, (WorldLightComponent_GetType(Light) == WorldLightType_Spot) && !WorldLightComponent_IsOn(Light));

    // Written and read back into another light, every setting survives; the defaults are left out.
    JSONCompound* Written = NULL;
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Written)))
    {
        TEST_CHECK_SUCCESS(run, Properties_WriteJSON(Table, Light, Pool, Written, NULL));
        TEST_CHECK(run, HasKey(run, Written, u8"type") && HasKey(run, Written, u8"inner_angle")
            && !HasKey(run, Written, u8"color") && !HasKey(run, Written, u8"casts_shadows"));
        TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Written, Copy, NULL));
        TEST_CHECK(run, Properties_Equals(Table, Light, Copy));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Written, true));
    }

    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

/* The flicker is a pure function: the same inputs give the same value, other seeds other values, the speed scales the
 * time exactly, every value lies in [1 - amount, 1], and none (or amount 0) is exactly 1. */
static void FlickerIsDeterministicAndBounded(TestRun* run)
{
    const WorldLightFlicker Kinds[] = { WorldLightFlicker_Candle, WorldLightFlicker_Fluorescent, WorldLightFlicker_Broken };
    const float Amounts[] = { 0.3f, 0.75f, 1.0f };
    for (size_t Kind = 0; Kind < 3U; Kind++)
    {
        for (size_t AmountIndex = 0; AmountIndex < 3U; AmountIndex++)
        {
            float Amount = Amounts[AmountIndex];
            FlickerStats Stats = MeasureFlicker(Kinds[Kind], Amount, 1.0f, 1000.0, 53U);
            TEST_CHECK(run, (Stats.Minimum >= (1.0f - Amount)) && (Stats.Maximum <= 1.0f));
            TEST_CHECK(run, Stats.Maximum > Stats.Minimum); // it does flicker
        }
        int Differing = 0;
        bool IsRepeatable = true;
        bool IsSpeedTime = true;
        for (int Sample = 0; Sample < 600; Sample++)
        {
            double Seconds = 7.0 + ((double)Sample / 30.0);
            float Value = WorldLightFlicker_GetFactor(Kinds[Kind], 0.8f, 1.0f, Seconds, 7U);
            IsRepeatable = IsRepeatable && (WorldLightFlicker_GetFactor(Kinds[Kind], 0.8f, 1.0f, Seconds, 7U) == Value);
            Differing += (WorldLightFlicker_GetFactor(Kinds[Kind], 0.8f, 1.0f, Seconds, 8U) != Value) ? 1 : 0;
            IsSpeedTime = IsSpeedTime
                && (WorldLightFlicker_GetFactor(Kinds[Kind], 0.8f, 2.0f, Seconds * 0.5, 7U) == Value);
        }
        TEST_CHECK(run, IsRepeatable && IsSpeedTime);
        TEST_CHECK(run, Differing > 100); // two lights do not flicker together
        // Speed 0 holds it still, at its value for time 0, and so do speeds below 0 or not finite.
        float Held = WorldLightFlicker_GetFactor(Kinds[Kind], 0.8f, 0.0f, 0.0, 7U);
        TEST_CHECK(run, (WorldLightFlicker_GetFactor(Kinds[Kind], 0.8f, 0.0f, 123.4, 7U) == Held)
            && (WorldLightFlicker_GetFactor(Kinds[Kind], 0.8f, NAN, 55.5, 7U) == Held)
            && (WorldLightFlicker_GetFactor(Kinds[Kind], 0.8f, -2.0f, 77.7, 7U) == Held));
        TEST_CHECK(run, WorldLightFlicker_GetFactor(Kinds[Kind], 0.0f, 1.0f, 3.0, 7U) == 1.0f);
        TEST_CHECK(run, WorldLightFlicker_GetFactor(Kinds[Kind], NAN, 1.0f, 3.0, 7U) == 1.0f);
        // An amount past 1 is 1 (darkness its floor); a clock that is not finite counts as time 0.
        float Clamped = WorldLightFlicker_GetFactor(Kinds[Kind], 5.0f, 1.0f, 3.0, 7U);
        TEST_CHECK(run, (Clamped >= 0.0f) && (Clamped == WorldLightFlicker_GetFactor(Kinds[Kind], 1.0f, 1.0f, 3.0, 7U)));
        TEST_CHECK(run, WorldLightFlicker_GetFactor(Kinds[Kind], 0.5f, 1.0f, INFINITY, 7U)
            == WorldLightFlicker_GetFactor(Kinds[Kind], 0.5f, 1.0f, 0.0, 7U));
        // So does a finite clock past what a double holds once sped up.
        TEST_CHECK(run, WorldLightFlicker_GetFactor(Kinds[Kind], 0.5f, 100.0f, 1.8e306, 7U)
            == WorldLightFlicker_GetFactor(Kinds[Kind], 0.5f, 1.0f, 0.0, 7U));
    }
    TEST_CHECK(run, (WorldLightFlicker_GetFactor(WorldLightFlicker_None, 1.0f, 1.0f, 3.0, 7U) == 1.0f)
        && (WorldLightFlicker_GetFactor((WorldLightFlicker)99, 1.0f, 1.0f, 3.0, 7U) == 1.0f));
}

/* Each kind has its own character: a candle wavers softly all the time, never jumps and never kinks, and two candles
 * waver apart; a fluorescent tube stays near its brightest and dips to its darkest only now and then, briefly; a broken
 * lamp spends long stretches dark. */
static void EachFlickerHasItsCharacter(TestRun* run)
{
    const uint64_t Seeds[] = { 1U, 53U, 9001U, 0xFFFFFFFFFFFFFFFFULL };
    for (size_t Index = 0; Index < (sizeof(Seeds) / sizeof(Seeds[0])); Index++)
    {
        FlickerStats Candle = MeasureFlicker(WorldLightFlicker_Candle, 1.0f, 1.0f, 0.0, Seeds[Index]);
        TEST_CHECK(run, (Candle.LargestStep < 0.12f) && ((Candle.Maximum - Candle.Minimum) > 0.5f)
            && (Candle.DarkShare == 0.0) && (Candle.Mean > 0.35) && (Candle.Mean < 0.65));
        TEST_CHECK(run, MeasureCandleBend(Seeds[Index]) < 2e-5f);
        TEST_CHECK(run, fabs(MeasureCandleCorrelation(Seeds[Index], Seeds[Index] + 1U)) < 0.3);

        FlickerStats Tube = MeasureFlicker(WorldLightFlicker_Fluorescent, 1.0f, 1.0f, 0.0, Seeds[Index]);
        TEST_CHECK(run, (Tube.NearFullShare > 0.9) && (Tube.DarkShare > 0.0) && (Tube.DarkShare < 0.05)
            && (Tube.LongestDarkSeconds <= 0.2) && (Tube.LargestStep > 0.9f));

        FlickerStats Broken = MeasureFlicker(WorldLightFlicker_Broken, 1.0f, 1.0f, 0.0, Seeds[Index]);
        TEST_CHECK(run, (Broken.LongestDarkSeconds >= 1.0) && (Broken.LongestDarkSeconds <= 3.5)
            && (Broken.DarkShare > 0.2) && (Broken.DarkShare < 0.6) && (Broken.Minimum == 0.0f));
    }
}

/* A light's own flicker is the flicker of its settings with its object's id as the seed. */
static void LightFlickersByItsObjectsId(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldLightComponent* Light = AddLight(run, &TestWorld, "candle");
    if ((Light != NULL) && TEST_CHECK_SUCCESS(run, WorldLightComponent_SetFlicker(Light, WorldLightFlicker_Candle))
        && TEST_CHECK_SUCCESS(run, WorldLightComponent_SetFlickerSpeed(Light, 1.5f)))
    {
        uint64_t Id = WorldObject_GetId(WorldComponent_GetOwner(&Light->Base));
        TEST_CHECK(run, WorldLightComponent_GetFlickerFactor(Light, 42.25)
            == WorldLightFlicker_GetFactor(WorldLightFlicker_Candle, 0.3f, 1.5f, 42.25, Id));
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}


// Public functions.
void WorldLightTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "new_light_has_its_defaults", NewLightHasItsDefaults },
        { "cone_angles_follow_their_rules", ConeAnglesFollowTheirRules },
        { "setters_advance_the_revision_and_refuse_bad_values", SettersAdvanceTheRevisionAndRefuseBadValues },
        { "light_reads_and_writes_its_json", LightReadsAndWritesItsJson },
        { "flicker_is_deterministic_and_bounded", FlickerIsDeterministicAndBounded },
        { "each_flicker_has_its_character", EachFlickerHasItsCharacter },
        { "light_flickers_by_its_objects_id", LightFlickersByItsObjectsId },
    };
    TestRun_RunSuite(run, "WorldLight", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
