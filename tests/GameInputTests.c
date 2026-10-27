#include "TestSuites.h"
#include "GameInput.h"
#include "GameKey.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"
#include <string.h>


// Macros.
/** Initial capacity (bytes) of path and text buffers; they grow as needed. */
#define INPUT_TEST_BUFFER_CAPACITY ((size_t)256)


// Static functions.
/* A sample with the given keys in the given state and nothing else going on. */
static GameInputSample MakeSample(void)
{
    GameInputSample Sample;
    GameInputSample_Clear(&Sample);
    Sample.WindowSize = (Vector2){ .x = 1280.0f, .y = 720.0f };
    return Sample;
}

static void Tick(GameInput* input, GameInputSample* sample)
{
    GameInput_BeginTick(input, sample);
}

/* Writes @p text as "<work>/<testName>/controls.json" and appends that file's path to @p outPath. */
static bool WriteControlsFile(TestRun* run, const char* testName, const char* text, GenericBuffer* outPath)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    bool IsWritten = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, testName, &Directory))
        && ((text == NULL) || TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Directory._data, "controls.json", text)))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, (const unsigned char*)u8"controls.json", outPath));
    Memory_Free(Directory._data);
    return IsWritten;
}

static bool HasBinding(const GameInput* input, GameAction action, size_t index, GameKey key, uint8_t modifiers)
{
    GameInputBinding Binding;
    return GameInput_TryGetBinding(input, action, index, &Binding) && (Binding.Key == key) && (Binding.Modifiers == modifiers);
}


// Tests.
static void KeyIdsAreUniqueAndParseBack(TestRun* run)
{
    for (int32_t Index = (int32_t)GameKey_Unknown + 1; Index < (int32_t)GameKey_Count; Index++)
    {
        GameKey Key = (GameKey)Index;
        const unsigned char* Id = GameKey_GetIdString(Key);
        if (!TEST_CHECK(run, (Id != NULL) && (GameKey_GetDisplayName(Key) != NULL)))
        {
            continue;
        }
        GameKey Parsed = GameKey_Unknown;
        TEST_CHECK(run, GameKey_TryParseIdString(Id, &Parsed) && (Parsed == Key));
        TEST_CHECK(run, GameKey_IsMouseButton(Key) != GameKey_IsKeyboardKey(Key));
        for (int32_t Other = Index + 1; Other < (int32_t)GameKey_Count; Other++)
        {
            const unsigned char* OtherId = GameKey_GetIdString((GameKey)Other);
            TEST_CHECK(run, (OtherId == NULL) || (strcmp((const char*)Id, (const char*)OtherId) != 0));
        }
    }
    GameKey Unchanged = GameKey_A;
    TEST_CHECK(run, !GameKey_TryParseIdString((const unsigned char*)u8"no_such_key", &Unchanged) && (Unchanged == GameKey_A));
    TEST_CHECK(run, !GameKey_IsValid(GameKey_Unknown) && !GameKey_IsValid(GameKey_Count));
    TEST_CHECK(run, GameKey_GetIdString(GameKey_Unknown) == NULL);
    TEST_CHECK(run, GameKey_IsMouseButton(GameKey_MouseLeft) && !GameKey_IsMouseButton(GameKey_W));
    TEST_CHECK(run, (GameKey_GetModifierFlag(GameKey_RightShift) == GAME_KEY_MODIFIER_SHIFT)
        && (GameKey_GetModifierFlag(GameKey_LeftAlt) == GAME_KEY_MODIFIER_ALT)
        && (GameKey_GetModifierFlag(GameKey_A) == GAME_KEY_MODIFIER_NONE));

    for (int32_t Index = 0; Index < (int32_t)GameAction_Count; Index++)
    {
        GameAction Parsed = GameAction_Count;
        TEST_CHECK(run, GameAction_TryParseIdString(GameAction_GetIdString((GameAction)Index), &Parsed)
            && (Parsed == (GameAction)Index) && (GameAction_GetDisplayName((GameAction)Index) != NULL));
    }
}

// A consumed key reads as not pressed while it is held and on the tick of its release; a new press starts fresh.
static void ConsumedKeysStayConsumedWhileHeld(TestRun* run)
{
    GameInput Input;
    TEST_CHECK_SUCCESS(run, GameInput_Construct(&Input));
    GameInputSample Sample = MakeSample();

    Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    Tick(&Input, &Sample);
    TEST_CHECK(run, GameInput_IsKeyPressed(&Input, GameKey_W) && GameInput_IsActionPressed(&Input, GameAction_MoveForward));
    GameInput_ConsumeKey(&Input, GameKey_W);
    TEST_CHECK(run, !GameInput_IsKeyPressed(&Input, GameKey_W) && !GameInput_IsKeyDown(&Input, GameKey_W));
    TEST_CHECK(run, !GameInput_IsActionDown(&Input, GameAction_MoveForward));
    TEST_CHECK(run, (GameInput_GetRawKeyState(&Input, GameKey_W) & GAME_INPUT_KEY_STATE_PRESSED) != 0);

    Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(&Input, &Sample);
    TEST_CHECK(run, !GameInput_IsKeyDown(&Input, GameKey_W) && GameInput_IsKeyConsumed(&Input, GameKey_W));

    Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_RELEASED;
    Tick(&Input, &Sample);
    TEST_CHECK(run, !GameInput_IsKeyReleased(&Input, GameKey_W));
    TEST_CHECK(run, (GameInput_GetRawKeyState(&Input, GameKey_W) & GAME_INPUT_KEY_STATE_RELEASED) != 0);

    Sample.KeyStates[GameKey_W] = 0;
    Tick(&Input, &Sample);
    TEST_CHECK(run, !GameInput_IsKeyConsumed(&Input, GameKey_W));

    // A press right after a consumed one (the release was missed) is unconsumed.
    Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    Tick(&Input, &Sample);
    GameInput_ConsumeKey(&Input, GameKey_W);
    Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    Tick(&Input, &Sample);
    TEST_CHECK(run, GameInput_IsKeyPressed(&Input, GameKey_W));
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Input));
}

static void WheelTextAndPointerAreConsumedForOneTick(TestRun* run)
{
    GameInput Input;
    TEST_CHECK_SUCCESS(run, GameInput_Construct(&Input));
    GameInputSample Sample = MakeSample();
    Sample.WheelDelta = (Vector2){ .x = 0.0f, .y = 2.0f };
    Sample.TextCodepoints[0] = 0x0161U; // š
    Sample.TextCodepointCount = 1U;
    Tick(&Input, &Sample);

    TEST_CHECK(run, (GameInput_GetWheelDelta(&Input).y == 2.0f) && (GameInput_GetTextCodepointCount(&Input) == 1U));
    TEST_CHECK(run, GameInput_GetTextCodepoint(&Input, 0U) == 0x0161U);
    GameInput_ConsumeWheel(&Input);
    GameInput_ConsumeText(&Input);
    GameInput_ConsumePointer(&Input);
    TEST_CHECK(run, (GameInput_GetWheelDelta(&Input).y == 0.0f) && (GameInput_GetTextCodepointCount(&Input) == 0U));
    TEST_CHECK(run, (GameInput_GetTextCodepoint(&Input, 0U) == 0U) && GameInput_IsPointerConsumed(&Input));

    Tick(&Input, &Sample);
    TEST_CHECK(run, (GameInput_GetWheelDelta(&Input).y == 2.0f) && (GameInput_GetTextCodepointCount(&Input) == 1U));
    TEST_CHECK(run, !GameInput_IsPointerConsumed(&Input));

    // A count past the maximum is clamped.
    Sample.TextCodepointCount = GAME_INPUT_MAX_TEXT_CODEPOINTS + 5U;
    Tick(&Input, &Sample);
    TEST_CHECK(run, GameInput_GetTextCodepointCount(&Input) == GAME_INPUT_MAX_TEXT_CODEPOINTS);
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Input));
}

// A binding's modifiers must be held; other held modifiers are ignored unless a more specific binding on the same key
// asks for them.
static void ActionsFollowTheirBindingsAndModifiers(TestRun* run)
{
    GameInput Input;
    TEST_CHECK_SUCCESS(run, GameInput_Construct(&Input));
    GameInputSample Sample = MakeSample();

    Sample.KeyStates[GameKey_Tab] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    Tick(&Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressed(&Input, GameAction_UINext));
    TEST_CHECK(run, !GameInput_IsActionPressed(&Input, GameAction_UIPrevious));

    Sample.KeyStates[GameKey_LeftShift] = GAME_INPUT_KEY_STATE_DOWN;
    Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(&Input, &Sample);
    TEST_CHECK(run, GameInput_GetHeldModifiers(&Input) == GAME_KEY_MODIFIER_SHIFT);
    // Shift+Tab is only ui_previous's chord (more specific than ui_next's plain Tab); W still walks while Shift is held.
    TEST_CHECK(run, GameInput_IsActionPressed(&Input, GameAction_UIPrevious) && !GameInput_IsActionPressed(&Input, GameAction_UINext));
    TEST_CHECK(run, GameInput_IsActionDown(&Input, GameAction_MoveForward));
    GameInput_ConsumeAction(&Input, GameAction_UIPrevious);
    TEST_CHECK(run, !GameInput_IsActionPressed(&Input, GameAction_UIPrevious) && GameInput_IsKeyConsumed(&Input, GameKey_Tab));
    TEST_CHECK(run, GameInput_IsActionDown(&Input, GameAction_Sprint)); // Shift itself is not consumed by the chord.

    // Repeats count for "pressed or repeated", not for "pressed".
    Sample.KeyStates[GameKey_LeftShift] = 0;
    Sample.KeyStates[GameKey_W] = 0;
    Sample.KeyStates[GameKey_Down] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_REPEATED;
    Tick(&Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressedOrRepeated(&Input, GameAction_UIDown) && !GameInput_IsActionPressed(&Input, GameAction_UIDown));

    // Two keys can drive one action, and rebinding replaces them.
    GameInputBinding Bindings[] = { { .Key = GameKey_Up, .Modifiers = GAME_KEY_MODIFIER_NONE }, { .Key = GameKey_I, .Modifiers = GAME_KEY_MODIFIER_NONE } };
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Input, GameAction_MoveForward, Bindings, 2U));
    Sample.KeyStates[GameKey_Down] = 0;
    Sample.KeyStates[GameKey_I] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(&Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionDown(&Input, GameAction_MoveForward) && (GameInput_GetBindingCount(&Input, GameAction_MoveForward) == 2U));
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Input, GameAction_MoveForward, NULL, 0U));
    TEST_CHECK(run, !GameInput_IsActionDown(&Input, GameAction_MoveForward));

    GameInputBinding Bad[] = { { .Key = GameKey_Count, .Modifiers = GAME_KEY_MODIFIER_NONE } };
    TEST_CHECK_ERROR(run, GameInput_SetBindings(&Input, GameAction_Sprint, Bad, 1U), ErrorCode_IllegalArgument);
    GameInputBinding BadModifier[] = { { .Key = GameKey_A, .Modifiers = 0x10U } };
    TEST_CHECK_ERROR(run, GameInput_SetBindings(&Input, GameAction_Sprint, BadModifier, 1U), ErrorCode_IllegalArgument);
    GameInputBinding TooMany[GAME_INPUT_MAX_BINDINGS_PER_ACTION + 1U];
    for (size_t Index = 0; Index < (GAME_INPUT_MAX_BINDINGS_PER_ACTION + 1U); Index++)
    {
        TooMany[Index] = (GameInputBinding){ .Key = GameKey_A, .Modifiers = GAME_KEY_MODIFIER_NONE };
    }
    TEST_CHECK_ERROR(run, GameInput_SetBindings(&Input, GameAction_Sprint, TooMany, GAME_INPUT_MAX_BINDINGS_PER_ACTION + 1U),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, HasBinding(&Input, GameAction_Sprint, 0U, GameKey_LeftShift, GAME_KEY_MODIFIER_NONE));

    GameInput_ResetBindings(&Input);
    TEST_CHECK(run, HasBinding(&Input, GameAction_MoveForward, 0U, GameKey_W, GAME_KEY_MODIFIER_NONE));
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Input));
}

// Navigation must not assume ui_previous holds the Shift chord: with Tab and Shift+Tab swapped between actions,
// Shift+Tab is still only the chord's action; a binding asking for modifiers that are not all held shadows nothing.
// GameInput_GetActionKeys reports exactly what GameInput_ConsumeAction consumes.
static void TheMostSpecificBindingWins(TestRun* run)
{
    GameInput Input;
    TEST_CHECK_SUCCESS(run, GameInput_Construct(&Input));
    GameInputBinding ShiftTab = { .Key = GameKey_Tab, .Modifiers = GAME_KEY_MODIFIER_SHIFT };
    GameInputBinding PlainTab = { .Key = GameKey_Tab, .Modifiers = GAME_KEY_MODIFIER_NONE };
    GameInputBinding ControlShiftTab = { .Key = GameKey_Tab, .Modifiers = GAME_KEY_MODIFIER_CONTROL | GAME_KEY_MODIFIER_SHIFT };
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Input, GameAction_UINext, &ShiftTab, 1U));
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Input, GameAction_UIPrevious, &PlainTab, 1U));
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Input, GameAction_Pause, &ControlShiftTab, 1U));
    GameInputSample Sample = MakeSample();

    Sample.KeyStates[GameKey_Tab] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    Tick(&Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressed(&Input, GameAction_UIPrevious) && !GameInput_IsActionPressed(&Input, GameAction_UINext));

    Sample.KeyStates[GameKey_RightShift] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(&Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressed(&Input, GameAction_UINext) && !GameInput_IsActionPressed(&Input, GameAction_UIPrevious));
    TEST_CHECK(run, !GameInput_IsActionPressed(&Input, GameAction_Pause)); // Control is not held.
    GameKey Keys[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    TEST_CHECK(run, (GameInput_GetActionKeys(&Input, GameAction_UINext, Keys) == 1U) && (Keys[0] == GameKey_Tab));
    TEST_CHECK(run, GameInput_GetActionKeys(&Input, GameAction_UIPrevious, Keys) == 0U);
    GameInput_ConsumeAction(&Input, GameAction_UIPrevious); // It matches no key, so it consumes nothing.
    TEST_CHECK(run, !GameInput_IsKeyConsumed(&Input, GameKey_Tab));
    GameInput_ConsumeAction(&Input, GameAction_UINext);
    TEST_CHECK(run, GameInput_IsKeyConsumed(&Input, GameKey_Tab) && !GameInput_IsActionPressed(&Input, GameAction_UINext));
    TEST_CHECK(run, GameInput_GetActionKeys(&Input, GameAction_UINext, Keys) == 1U); // Consumed keys are still reported.

    Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(&Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressed(&Input, GameAction_Pause) && !GameInput_IsActionPressed(&Input, GameAction_UINext));
    TEST_CHECK(run, GameInput_GetActionKeys(&Input, GameAction_Count, Keys) == 0U);
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Input));
}

static void BindingTextRoundTrips(TestRun* run)
{
    GameInputBinding Binding = { .Key = GameKey_S, .Modifiers = GAME_KEY_MODIFIER_SHIFT | GAME_KEY_MODIFIER_CONTROL };
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, GameInputBinding_AppendText(&Binding, &Text)))
    {
        TEST_CHECK(run, strcmp((const char*)Text._data, "control+shift+s") == 0);
        GameInputBinding Parsed = { .Key = GameKey_Unknown, .Modifiers = 0U };
        TEST_CHECK(run, GameInputBinding_TryParse(Text._data, &Parsed) && (Parsed.Key == GameKey_S) && (Parsed.Modifiers == Binding.Modifiers));
    }
    GameInputBinding Parsed = { .Key = GameKey_Unknown, .Modifiers = 0U };
    TEST_CHECK(run, GameInputBinding_TryParse((const unsigned char*)u8"super+alt+keypad_add", &Parsed)
        && (Parsed.Key == GameKey_KeypadAdd) && (Parsed.Modifiers == (GAME_KEY_MODIFIER_SUPER | GAME_KEY_MODIFIER_ALT)));
    TEST_CHECK(run, !GameInputBinding_TryParse((const unsigned char*)u8"shift+shift+a", &Parsed));
    TEST_CHECK(run, !GameInputBinding_TryParse((const unsigned char*)u8"shift+", &Parsed));
    TEST_CHECK(run, !GameInputBinding_TryParse((const unsigned char*)u8"+a", &Parsed));
    TEST_CHECK(run, !GameInputBinding_TryParse((const unsigned char*)u8"hyper+a", &Parsed));
    TEST_CHECK(run, !GameInputBinding_TryParse((const unsigned char*)u8"", &Parsed));
    TEST_CHECK(run, !GameInputBinding_TryParse(NULL, &Parsed));
    Memory_Free(Text._data);
}

// Saving writes every action; loading replaces the actions a file lists and keeps the rest.
static void ControlsFilesRoundTrip(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GameInput Input;
    TEST_CHECK_SUCCESS(run, GameInput_Construct(&Input));
    if (WriteControlsFile(run, "round_trip", NULL, &Path))
    {
        GameInputBinding Custom[] = { { .Key = GameKey_Up, .Modifiers = GAME_KEY_MODIFIER_NONE }, { .Key = GameKey_E, .Modifiers = GAME_KEY_MODIFIER_CONTROL } };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Input, GameAction_MoveForward, Custom, 2U));
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Input, GameAction_Pause, NULL, 0U));
        TEST_CHECK_SUCCESS(run, GameInput_SaveBindingsToFile(&Input, Path._data));

        GameInput_ResetBindings(&Input);
        TEST_CHECK_SUCCESS(run, GameInput_LoadBindingsFromFile(&Input, Path._data));
        TEST_CHECK(run, (GameInput_GetBindingCount(&Input, GameAction_MoveForward) == 2U)
            && HasBinding(&Input, GameAction_MoveForward, 0U, GameKey_Up, GAME_KEY_MODIFIER_NONE)
            && HasBinding(&Input, GameAction_MoveForward, 1U, GameKey_E, GAME_KEY_MODIFIER_CONTROL));
        TEST_CHECK(run, GameInput_GetBindingCount(&Input, GameAction_Pause) == 0U);
        TEST_CHECK(run, HasBinding(&Input, GameAction_UIPrevious, 0U, GameKey_Tab, GAME_KEY_MODIFIER_SHIFT));
    }
    GenericBuffer_Clear(&Path);
    if (WriteControlsFile(run, "partial", "{ \"bindings\": { \"sprint\": [\"right_shift\", \"mouse_right\"], \"no_such_action\": [\"q\"] } }", &Path))
    {
        GameInput_ResetBindings(&Input);
        TEST_CHECK_SUCCESS(run, GameInput_LoadBindingsFromFile(&Input, Path._data));
        TEST_CHECK(run, HasBinding(&Input, GameAction_Sprint, 0U, GameKey_RightShift, GAME_KEY_MODIFIER_NONE)
            && HasBinding(&Input, GameAction_Sprint, 1U, GameKey_MouseRight, GAME_KEY_MODIFIER_NONE));
        TEST_CHECK(run, HasBinding(&Input, GameAction_MoveForward, 0U, GameKey_W, GAME_KEY_MODIFIER_NONE));
    }
    GenericBuffer_Clear(&Path);
    // A byte order mark, which some editors write, is skipped.
    if (WriteControlsFile(run, "byte_order_mark", "\xEF\xBB\xBF{ \"bindings\": { \"sprint\": [\"right_control\"] } }", &Path))
    {
        GameInput_ResetBindings(&Input);
        TEST_CHECK_SUCCESS(run, GameInput_LoadBindingsFromFile(&Input, Path._data));
        TEST_CHECK(run, HasBinding(&Input, GameAction_Sprint, 0U, GameKey_RightControl, GAME_KEY_MODIFIER_NONE));
    }
    Error MissingResult = GameInput_LoadBindingsFromFile(&Input, (const unsigned char*)u8"/nonexistent/dir/controls.json");
    TEST_CHECK(run, (MissingResult.Code == ErrorCode_FileNotFound) || (MissingResult.Code == ErrorCode_DirectoryNotFound));
    Error_Deconstruct(&MissingResult);
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Input));
    Memory_Free(Path._data);
}

// A file with any bad entry changes nothing, and the error names the action.
static void BadControlsFilesChangeNothing(TestRun* run)
{
    static const struct
    {
        const char* Name;
        const char* Text;
        bool IsActionNamed;
    } Cases[] =
    {
        { "bad_key", "{ \"bindings\": { \"move_back\": [\"down\"], \"sprint\": [\"shiftt\"] } }", true },
        { "not_a_list", "{ \"bindings\": { \"move_back\": [\"down\"], \"sprint\": \"left_shift\" } }", true },
        { "not_a_string", "{ \"bindings\": { \"move_back\": [\"down\"], \"sprint\": [5] } }", true },
        { "too_many", "{ \"bindings\": { \"move_back\": [\"down\"], \"sprint\": [\"a\", \"b\", \"c\", \"d\", \"e\"] } }", true },
        { "empty_binding", "{ \"bindings\": { \"move_back\": [\"down\"], \"sprint\": [\"\"] } }", true },
        { "not_an_object", "{ \"bindings\": [] }", false },
        { "not_json", "{ \"bindings\": ", false },
    };
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    for (size_t Index = 0; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        GameInput Input;
        TEST_CHECK_SUCCESS(run, GameInput_Construct(&Input));
        GenericBuffer_Clear(&Path);
        if (WriteControlsFile(run, Cases[Index].Name, Cases[Index].Text, &Path))
        {
            Error Result = GameInput_LoadBindingsFromFile(&Input, Path._data);
            bool IsNamed = !Cases[Index].IsActionNamed || ((Result.Message != NULL) && (strstr((const char*)Result.Message, "sprint") != NULL));
            TEST_CHECK(run, IsNamed);
            TEST_CHECK_ERROR(run, Result, ErrorCode_InvalidJSON);
            TEST_CHECK(run, HasBinding(&Input, GameAction_MoveBack, 0U, GameKey_S, GAME_KEY_MODIFIER_NONE));
            TEST_CHECK(run, HasBinding(&Input, GameAction_Sprint, 0U, GameKey_LeftShift, GAME_KEY_MODIFIER_NONE));
        }
        TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Input));
    }
    Memory_Free(Path._data);
}


// Public functions.
void GameInputTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "key_ids_are_unique_and_parse_back", KeyIdsAreUniqueAndParseBack },
        { "consumed_keys_stay_consumed_while_held", ConsumedKeysStayConsumedWhileHeld },
        { "wheel_text_and_pointer_are_consumed_for_one_tick", WheelTextAndPointerAreConsumedForOneTick },
        { "actions_follow_their_bindings_and_modifiers", ActionsFollowTheirBindingsAndModifiers },
        { "the_most_specific_binding_wins", TheMostSpecificBindingWins },
        { "binding_text_round_trips", BindingTextRoundTrips },
        { "controls_files_round_trip", ControlsFilesRoundTrip },
        { "bad_controls_files_change_nothing", BadControlsFilesChangeNothing },
    };
    TestRun_RunSuite(run, "GameInput", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
