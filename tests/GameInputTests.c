#include "TestSuites.h"
#include "TestText.h"
#include "GameInput.h"
#include "GameInputInternal.h"
#include "GameKey.h"
#include "Logger.h"
#include "Registry.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"
#include <stdio.h>
#include <string.h>


// Macros.
/** Initial capacity (bytes) of path and text buffers; they grow as needed. */
#define INPUT_TEST_BUFFER_CAPACITY ((size_t)256)
/** How many actions the cost test registers on keys other than W. */
#define INPUT_TEST_FILLER_ACTIONS ((uint32_t)4000)


// Types.
/* An input with its own key registry, and a logger in a work directory when a test reads what was logged. */
typedef struct InputFixtureStruct
{
    KeyRegistry Keys;
    GameInput Input;
    Logger Log;
    GenericBuffer Directory;
    bool HasLogger;
    bool IsReady;
} InputFixture;


// Static functions: the fixture.
/* Builds the fixture; with @p logDirectory, a logger in that work directory too. */
static bool Fixture_Construct(TestRun* run, InputFixture* self, const char* logDirectory)
{
    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->Directory, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&self->Keys));
    if (logDirectory != NULL)
    {
        self->HasLogger = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, logDirectory, &self->Directory))
            && TEST_CHECK_SUCCESS(run, Logger_Construct(&self->Log, self->Directory._data));
    }
    self->IsReady = ((logDirectory == NULL) || self->HasLogger)
        && TEST_CHECK_SUCCESS(run, GameInput_Construct(&self->Input, &self->Keys, NULL,
        self->HasLogger ? &self->Log : NULL));
    return self->IsReady;
}

/* Closes the logger, so the log can be read. */
static void Fixture_CloseLog(TestRun* run, InputFixture* self)
{
    if (self->HasLogger)
    {
        TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&self->Log));
        self->HasLogger = false;
    }
}

static void Fixture_Deconstruct(TestRun* run, InputFixture* self)
{
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&self->Input));
    Fixture_CloseLog(run, self);
    KeyRegistry_Deconstruct(&self->Keys);
    Memory_Free(self->Directory._data);
}

/* Counts the occurrences of @p needle in the fixture's closed log. */
static size_t CountInLog(TestRun* run, const InputFixture* self, const char* needle)
{
    size_t Count = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(self->Directory._data, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        for (const char* Found = strstr((const char*)Text._data, needle); Found != NULL; Found = strstr(Found + 1, needle))
        {
            Count++;
        }
    }
    Memory_Free(Text._data);
    Memory_Free(Path._data);
    return Count;
}

static uint32_t Intern(TestRun* run, InputFixture* self, const char* key)
{
    uint32_t Id = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&self->Keys, (const unsigned char*)key, &Id));
    return Id;
}

static GameActionCategoryId AddCategory(TestRun* run, InputFixture* self, const char* key, uint32_t source)
{
    GameActionCategoryInfo Info = { .DisplayName = (const unsigned char*)u8"Tools",
        .Description = (const unsigned char*)u8"", .Order = 500 };
    GameActionCategoryId Id = 0U;
    TEST_CHECK_SUCCESS(run, GameInput_RegisterCategory(&self->Input, Intern(run, self, key), &Info, source, &Id));
    return Id;
}

/* An action's description with one default binding (none when @p key is GameKey_Unknown). */
static GameActionInfo MakeAction(GameActionCategoryId category, GameKey key, uint8_t modifiers)
{
    GameActionInfo Info = { .DisplayName = (const unsigned char*)u8"Hammer",
        .Description = (const unsigned char*)u8"Hits things.", .Category = category, .Kind = GameActionKind_Button };
    if (key != GameKey_Unknown)
    {
        Info.DefaultBindings[0] = (GameInputBinding){ .Key = key, .Modifiers = modifiers };
        Info.DefaultBindingCount = 1U;
    }
    return Info;
}

static GameActionId AddAction(TestRun* run, InputFixture* self, const char* key, GameActionCategoryId category,
    GameKey defaultKey, uint8_t modifiers, uint32_t source)
{
    GameActionInfo Info = MakeAction(category, defaultKey, modifiers);
    GameActionId Id = 0U;
    TEST_CHECK_SUCCESS(run, GameInput_RegisterAction(&self->Input, Intern(run, self, key), &Info, source, &Id));
    return Id;
}

/* A sample with nothing going on. */
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

/* Reads a whole text file into @p outText (NUL-terminated). */
static bool ReadFile(TestRun* run, const GenericBuffer* path, GenericBuffer* outText)
{
    GenericBuffer_Clear(outText);
    return TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(path->_data, outText))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(outText));
}

static bool HasBinding(const GameInput* input, GameActionId action, size_t index, GameKey key, uint8_t modifiers)
{
    GameInputBinding Binding;
    return GameInput_TryGetBinding(input, action, index, &Binding) && (Binding.Key == key) && (Binding.Modifiers == modifiers);
}

/* How many key-index entries of @p action are on @p key. */
static size_t CountOnKey(const GameInput* input, GameKey key, GameActionId action)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < GameInput_GetKeyBindingCount(input, key); Index++)
    {
        GameActionId Id = 0U;
        GameInputBinding Binding;
        if (GameInput_TryGetKeyBinding(input, key, Index, &Id, &Binding) && (Id == action))
        {
            Count++;
        }
    }
    return Count;
}

static bool HasDisplayName(const GameInput* input, GameActionId id, const char* name)
{
    GameActionInfo Info;
    return GameInput_TryGetActionInfo(input, id, &Info) && (strcmp((const char*)Info.DisplayName, name) == 0);
}


// Tests: keys, consumption and bindings.
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
}

// A consumed key reads as not pressed while it is held and on the tick of its release; a new press starts fresh.
static void ConsumedKeysStayConsumedWhileHeld(TestRun* run)
{
    InputFixture Fixture;
    if (Fixture_Construct(run, &Fixture, NULL))
    {
        GameInput* Input = &Fixture.Input;
        GameInputSample Sample = MakeSample();
        Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        Tick(Input, &Sample);
        TEST_CHECK(run, GameInput_IsKeyPressed(Input, GameKey_W) && GameInput_IsActionPressed(Input, GameAction_MoveForward));
        GameInput_ConsumeKey(Input, GameKey_W);
        TEST_CHECK(run, !GameInput_IsKeyPressed(Input, GameKey_W) && !GameInput_IsKeyDown(Input, GameKey_W));
        TEST_CHECK(run, !GameInput_IsActionDown(Input, GameAction_MoveForward));
        TEST_CHECK(run, (GameInput_GetRawKeyState(Input, GameKey_W) & GAME_INPUT_KEY_STATE_PRESSED) != 0);

        Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN;
        Tick(Input, &Sample);
        TEST_CHECK(run, !GameInput_IsKeyDown(Input, GameKey_W) && GameInput_IsKeyConsumed(Input, GameKey_W));

        Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_RELEASED;
        Tick(Input, &Sample);
        TEST_CHECK(run, !GameInput_IsKeyReleased(Input, GameKey_W));
        TEST_CHECK(run, (GameInput_GetRawKeyState(Input, GameKey_W) & GAME_INPUT_KEY_STATE_RELEASED) != 0);

        Sample.KeyStates[GameKey_W] = 0;
        Tick(Input, &Sample);
        TEST_CHECK(run, !GameInput_IsKeyConsumed(Input, GameKey_W));

        // A press right after a consumed one (the release was missed) is unconsumed.
        Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        Tick(Input, &Sample);
        GameInput_ConsumeKey(Input, GameKey_W);
        Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        Tick(Input, &Sample);
        TEST_CHECK(run, GameInput_IsKeyPressed(Input, GameKey_W));
    }
    Fixture_Deconstruct(run, &Fixture);
}

static void WheelTextAndPointerAreConsumedForOneTick(TestRun* run)
{
    InputFixture Fixture;
    if (Fixture_Construct(run, &Fixture, NULL))
    {
        GameInput* Input = &Fixture.Input;
        GameInputSample Sample = MakeSample();
        Sample.WheelDelta = (Vector2){ .x = 0.0f, .y = 2.0f };
        Sample.TextCodepoints[0] = 0x0161U; // š
        Sample.TextCodepointCount = 1U;
        Tick(Input, &Sample);

        TEST_CHECK(run, (GameInput_GetWheelDelta(Input).y == 2.0f) && (GameInput_GetTextCodepointCount(Input) == 1U));
        TEST_CHECK(run, GameInput_GetTextCodepoint(Input, 0U) == 0x0161U);
        GameInput_ConsumeWheel(Input);
        GameInput_ConsumeText(Input);
        GameInput_ConsumePointer(Input);
        TEST_CHECK(run, (GameInput_GetWheelDelta(Input).y == 0.0f) && (GameInput_GetTextCodepointCount(Input) == 0U));
        TEST_CHECK(run, (GameInput_GetTextCodepoint(Input, 0U) == 0U) && GameInput_IsPointerConsumed(Input));

        Tick(Input, &Sample);
        TEST_CHECK(run, (GameInput_GetWheelDelta(Input).y == 2.0f) && (GameInput_GetTextCodepointCount(Input) == 1U));
        TEST_CHECK(run, !GameInput_IsPointerConsumed(Input));

        // A count past the maximum is clamped.
        Sample.TextCodepointCount = GAME_INPUT_MAX_TEXT_CODEPOINTS + 5U;
        Tick(Input, &Sample);
        TEST_CHECK(run, GameInput_GetTextCodepointCount(Input) == GAME_INPUT_MAX_TEXT_CODEPOINTS);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A binding's modifiers must be held; other held modifiers are ignored unless a more specific binding on the same key
// asks for them.
static void ActionsFollowTheirBindingsAndModifiers(TestRun* run)
{
    InputFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        Fixture_Deconstruct(run, &Fixture);
        return;
    }
    GameInput* Input = &Fixture.Input;
    GameInputSample Sample = MakeSample();

    Sample.KeyStates[GameKey_Tab] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    Tick(Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressed(Input, GameAction_UINext));
    TEST_CHECK(run, !GameInput_IsActionPressed(Input, GameAction_UIPrevious));

    Sample.KeyStates[GameKey_LeftShift] = GAME_INPUT_KEY_STATE_DOWN;
    Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(Input, &Sample);
    TEST_CHECK(run, GameInput_GetHeldModifiers(Input) == GAME_KEY_MODIFIER_SHIFT);
    // Shift+Tab is only ui_previous's chord (more specific than ui_next's plain Tab); W still walks while Shift is held.
    TEST_CHECK(run, GameInput_IsActionPressed(Input, GameAction_UIPrevious) && !GameInput_IsActionPressed(Input, GameAction_UINext));
    TEST_CHECK(run, GameInput_IsActionDown(Input, GameAction_MoveForward));
    GameInput_ConsumeAction(Input, GameAction_UIPrevious);
    TEST_CHECK(run, !GameInput_IsActionPressed(Input, GameAction_UIPrevious) && GameInput_IsKeyConsumed(Input, GameKey_Tab));
    TEST_CHECK(run, GameInput_IsActionDown(Input, GameAction_Sprint)); // Shift itself is not consumed by the chord.

    // Repeats count for "pressed or repeated", not for "pressed".
    Sample.KeyStates[GameKey_LeftShift] = 0;
    Sample.KeyStates[GameKey_W] = 0;
    Sample.KeyStates[GameKey_Down] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_REPEATED;
    Tick(Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressedOrRepeated(Input, GameAction_UIDown) && !GameInput_IsActionPressed(Input, GameAction_UIDown));

    // Two keys can drive one action, and rebinding replaces them; the player set them now.
    GameInputBinding Bindings[] = { { .Key = GameKey_Up, .Modifiers = GAME_KEY_MODIFIER_NONE }, { .Key = GameKey_I, .Modifiers = GAME_KEY_MODIFIER_NONE } };
    TEST_CHECK(run, !GameInput_IsSetByPlayer(Input, GameAction_MoveForward));
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_MoveForward, Bindings, 2U));
    TEST_CHECK(run, GameInput_IsSetByPlayer(Input, GameAction_MoveForward));
    Sample.KeyStates[GameKey_Down] = 0;
    Sample.KeyStates[GameKey_I] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionDown(Input, GameAction_MoveForward) && (GameInput_GetBindingCount(Input, GameAction_MoveForward) == 2U));
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_MoveForward, NULL, 0U));
    TEST_CHECK(run, !GameInput_IsActionDown(Input, GameAction_MoveForward) && (GameInput_GetBindingCount(Input, GameAction_MoveForward) == 0U));

    GameInputBinding Bad[] = { { .Key = GameKey_Count, .Modifiers = GAME_KEY_MODIFIER_NONE } };
    TEST_CHECK_ERROR(run, GameInput_SetBindings(Input, GameAction_Sprint, Bad, 1U), ErrorCode_IllegalArgument);
    GameInputBinding BadModifier[] = { { .Key = GameKey_A, .Modifiers = 0x10U } };
    TEST_CHECK_ERROR(run, GameInput_SetBindings(Input, GameAction_Sprint, BadModifier, 1U), ErrorCode_IllegalArgument);
    GameInputBinding TooMany[GAME_INPUT_MAX_BINDINGS_PER_ACTION + 1U];
    for (size_t Index = 0; Index < (GAME_INPUT_MAX_BINDINGS_PER_ACTION + 1U); Index++)
    {
        TooMany[Index] = (GameInputBinding){ .Key = GameKey_A, .Modifiers = GAME_KEY_MODIFIER_NONE };
    }
    TEST_CHECK_ERROR(run, GameInput_SetBindings(Input, GameAction_Sprint, TooMany, GAME_INPUT_MAX_BINDINGS_PER_ACTION + 1U),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameInput_SetBindings(Input, GameAction_Count + 7U, Bindings, 1U), ErrorCode_InvalidOperation);
    TEST_CHECK(run, HasBinding(Input, GameAction_Sprint, 0U, GameKey_LeftShift, GAME_KEY_MODIFIER_NONE));
    TEST_CHECK(run, !GameInput_IsSetByPlayer(Input, GameAction_Sprint));

    // One action back to its defaults, then all.
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_Jump, Bindings, 1U));
    TEST_CHECK_SUCCESS(run, GameInput_ResetActionBindings(Input, GameAction_MoveForward));
    TEST_CHECK(run, HasBinding(Input, GameAction_MoveForward, 0U, GameKey_W, GAME_KEY_MODIFIER_NONE)
        && !GameInput_IsSetByPlayer(Input, GameAction_MoveForward) && GameInput_IsSetByPlayer(Input, GameAction_Jump));
    TEST_CHECK_ERROR(run, GameInput_ResetActionBindings(Input, GameAction_Count + 7U), ErrorCode_InvalidOperation);
    GameInput_ResetBindings(Input);
    TEST_CHECK(run, HasBinding(Input, GameAction_Jump, 0U, GameKey_Space, GAME_KEY_MODIFIER_NONE)
        && !GameInput_IsSetByPlayer(Input, GameAction_Jump));
    Fixture_Deconstruct(run, &Fixture);
}

// Navigation must not assume ui_previous holds the Shift chord: with Tab and Shift+Tab swapped between actions,
// Shift+Tab is still only the chord's action; a binding asking for modifiers that are not all held shadows nothing.
// GameInput_GetActionKeys reports exactly what GameInput_ConsumeAction consumes.
static void TheMostSpecificBindingWins(TestRun* run)
{
    InputFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        Fixture_Deconstruct(run, &Fixture);
        return;
    }
    GameInput* Input = &Fixture.Input;
    GameInputBinding ShiftTab = { .Key = GameKey_Tab, .Modifiers = GAME_KEY_MODIFIER_SHIFT };
    GameInputBinding PlainTab = { .Key = GameKey_Tab, .Modifiers = GAME_KEY_MODIFIER_NONE };
    GameInputBinding ControlShiftTab = { .Key = GameKey_Tab, .Modifiers = GAME_KEY_MODIFIER_CONTROL | GAME_KEY_MODIFIER_SHIFT };
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_UINext, &ShiftTab, 1U));
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_UIPrevious, &PlainTab, 1U));
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_Pause, &ControlShiftTab, 1U));
    GameInputSample Sample = MakeSample();

    Sample.KeyStates[GameKey_Tab] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    Tick(Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressed(Input, GameAction_UIPrevious) && !GameInput_IsActionPressed(Input, GameAction_UINext));

    Sample.KeyStates[GameKey_RightShift] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressed(Input, GameAction_UINext) && !GameInput_IsActionPressed(Input, GameAction_UIPrevious));
    TEST_CHECK(run, !GameInput_IsActionPressed(Input, GameAction_Pause)); // Control is not held.
    GameKey Keys[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    TEST_CHECK(run, (GameInput_GetActionKeys(Input, GameAction_UINext, Keys) == 1U) && (Keys[0] == GameKey_Tab));
    TEST_CHECK(run, GameInput_GetActionKeys(Input, GameAction_UIPrevious, Keys) == 0U);
    GameInput_ConsumeAction(Input, GameAction_UIPrevious); // It matches no key, so it consumes nothing.
    TEST_CHECK(run, !GameInput_IsKeyConsumed(Input, GameKey_Tab));
    GameInput_ConsumeAction(Input, GameAction_UINext);
    TEST_CHECK(run, GameInput_IsKeyConsumed(Input, GameKey_Tab) && !GameInput_IsActionPressed(Input, GameAction_UINext));
    TEST_CHECK(run, GameInput_GetActionKeys(Input, GameAction_UINext, Keys) == 1U); // Consumed keys are still reported.

    Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressed(Input, GameAction_Pause) && !GameInput_IsActionPressed(Input, GameAction_UINext));
    TEST_CHECK(run, GameInput_GetActionKeys(Input, GameAction_Count + 7U, Keys) == 0U);
    Fixture_Deconstruct(run, &Fixture);
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


// Tests: the registries.
/* The engine's actions and categories are registered first, so each enum value is its id; both registries are in the
 * root while the input lives. */
static void TheEngineActionsGetTheirEnumValues(TestRun* run)
{
    static const struct
    {
        GameAction Action;
        const char* Key;
        GameActionCategory Category;
    } Expected[] =
    {
        { GameAction_UIUp, "tbp:ui_up", GameActionCategory_Interface },
        { GameAction_UIDown, "tbp:ui_down", GameActionCategory_Interface },
        { GameAction_UILeft, "tbp:ui_left", GameActionCategory_Interface },
        { GameAction_UIRight, "tbp:ui_right", GameActionCategory_Interface },
        { GameAction_UINext, "tbp:ui_next", GameActionCategory_Interface },
        { GameAction_UIPrevious, "tbp:ui_previous", GameActionCategory_Interface },
        { GameAction_UIAccept, "tbp:ui_accept", GameActionCategory_Interface },
        { GameAction_UIBack, "tbp:ui_back", GameActionCategory_Interface },
        { GameAction_MoveForward, "tbp:move_forward", GameActionCategory_Movement },
        { GameAction_MoveBack, "tbp:move_back", GameActionCategory_Movement },
        { GameAction_MoveLeft, "tbp:move_left", GameActionCategory_Movement },
        { GameAction_MoveRight, "tbp:move_right", GameActionCategory_Movement },
        { GameAction_MoveUp, "tbp:move_up", GameActionCategory_Movement },
        { GameAction_MoveDown, "tbp:move_down", GameActionCategory_Movement },
        { GameAction_Sprint, "tbp:sprint", GameActionCategory_Movement },
        { GameAction_Jump, "tbp:jump", GameActionCategory_Character },
        { GameAction_Crouch, "tbp:crouch", GameActionCategory_Character },
        { GameAction_Use, "tbp:use", GameActionCategory_Character },
        { GameAction_Pause, "tbp:pause", GameActionCategory_General },
    };
    static const struct
    {
        GameActionCategory Category;
        const char* Key;
        int32_t Order;
    } ExpectedCategories[] =
    {
        { GameActionCategory_Movement, "tbp:movement", 100 },
        { GameActionCategory_Character, "tbp:character", 200 },
        { GameActionCategory_General, "tbp:general", 300 },
        { GameActionCategory_Interface, "tbp:interface", 400 },
    };
    TEST_CHECK(run, (sizeof(Expected) / sizeof(Expected[0])) == (size_t)GameAction_Count);

    KeyRegistry Keys;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys));
    RegistryRoot Root;
    TEST_CHECK_SUCCESS(run, RegistryRoot_Construct(&Root, &Keys));
    GameInput Input;
    if (TEST_CHECK_SUCCESS(run, GameInput_Construct(&Input, &Keys, &Root, NULL)))
    {
        TEST_CHECK(run, (GameInput_GetActionCount(&Input) == (uint32_t)GameAction_Count)
            && (GameInput_GetCategoryCount(&Input) == (uint32_t)GameActionCategory_Count));
        for (size_t Index = 0U; Index < (sizeof(Expected) / sizeof(Expected[0])); Index++)
        {
            GameActionId Found = UINT32_MAX;
            GameActionInfo Info;
            TEST_CHECK(run, GameInput_FindActionText(&Input, (const unsigned char*)Expected[Index].Key, &Found)
                && (Found == (GameActionId)Expected[Index].Action));
            TEST_CHECK(run, strcmp((const char*)GameInput_GetActionKeyText(&Input, Expected[Index].Action),
                Expected[Index].Key) == 0);
            if (TEST_CHECK(run, GameInput_TryGetActionInfo(&Input, Expected[Index].Action, &Info)))
            {
                TEST_CHECK(run, (Info.Category == (GameActionCategoryId)Expected[Index].Category)
                    && (Info.DisplayName[0] != 0) && (Info.Description[0] != 0) && (Info.Kind == GameActionKind_Button)
                    && (Info.DefaultBindingCount >= 1U));
                // Only pause always keeps a binding.
                TEST_CHECK(run, Info.KeepsABinding == (Expected[Index].Action == GameAction_Pause));
            }
            TEST_CHECK(run, (GameInput_GetActionSource(&Input, Expected[Index].Action) == NAMESPACED_KEY_ID_NONE)
                && GameInput_IsActionActive(&Input, Expected[Index].Action)
                && !GameInput_IsSetByPlayer(&Input, Expected[Index].Action));
        }
        for (size_t Index = 0U; Index < (sizeof(ExpectedCategories) / sizeof(ExpectedCategories[0])); Index++)
        {
            GameActionCategoryId Found = UINT32_MAX;
            GameActionCategoryInfo Info;
            TEST_CHECK(run, GameInput_FindCategoryText(&Input, (const unsigned char*)ExpectedCategories[Index].Key, &Found)
                && (Found == (GameActionCategoryId)ExpectedCategories[Index].Category));
            TEST_CHECK(run, GameInput_TryGetCategoryInfo(&Input, ExpectedCategories[Index].Category, &Info)
                && (Info.Order == ExpectedCategories[Index].Order) && (Info.DisplayName[0] != 0));
        }

        // The actions by prefix, in byte order of their keys.
        GenericBuffer Ids;
        GenericBuffer_AllocateVariable(&Ids, 8U, sizeof(GameActionId));
        if (TEST_CHECK_SUCCESS(run, GameInput_FindActionsByPrefix(&Input, (const unsigned char*)u8"tbp:move_", &Ids))
            && TEST_CHECK(run, Ids._count == 6U))
        {
            const GameActionId* Found = (const GameActionId*)Ids._data;
            TEST_CHECK(run, (Found[0] == GameAction_MoveBack) && (Found[1] == GameAction_MoveDown)
                && (Found[5] == GameAction_MoveUp));
        }
        Memory_Free(Ids._data);

        // The root lists both registries; a second input cannot add its own under the same keys, and leaves nothing.
        Registry* Actions = RegistryRoot_FindText(&Root, GAME_INPUT_ACTION_REGISTRY_KEY);
        Registry* Categories = RegistryRoot_FindText(&Root, GAME_INPUT_CATEGORY_REGISTRY_KEY);
        TEST_CHECK(run, (Actions != NULL) && (Registry_GetActiveCount(Actions) == (uint32_t)GameAction_Count)
            && (Categories != NULL) && (Registry_GetActiveCount(Categories) == (uint32_t)GameActionCategory_Count));
        GameInput Second;
        TEST_CHECK_ERROR(run, GameInput_Construct(&Second, &Keys, &Root, NULL), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Second));
        TEST_CHECK(run, RegistryRoot_FindText(&Root, GAME_INPUT_ACTION_REGISTRY_KEY) == Actions);
        TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Input));
        TEST_CHECK(run, (RegistryRoot_FindText(&Root, GAME_INPUT_ACTION_REGISTRY_KEY) == NULL)
            && (RegistryRoot_FindText(&Root, GAME_INPUT_CATEGORY_REGISTRY_KEY) == NULL));
    }
    TEST_CHECK_ERROR(run, GameInput_Construct(&Input, NULL, NULL, NULL), ErrorCode_IllegalArgument);
    RegistryRoot_Deconstruct(&Root);
    KeyRegistry_Deconstruct(&Keys);
}

/* Every rule of a registration, and a refused one changes nothing. */
static void RegistrationRefusesBadEntries(TestRun* run)
{
    InputFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        Fixture_Deconstruct(run, &Fixture);
        return;
    }
    GameInput* Input = &Fixture.Input;
    uint32_t Pack = Intern(run, &Fixture, "mymod:pack");
    GameActionCategoryId Tools = AddCategory(run, &Fixture, "mymod:tools", Pack);
    uint32_t HammerKey = Intern(run, &Fixture, "mymod:hammer");
    GameActionInfo Good = MakeAction(Tools, GameKey_F, GAME_KEY_MODIFIER_NONE);

    // An active key refuses, naming both sources.
    static const char* const BothSources[] = { "tbp:jump", "the engine", "mymod:pack", NULL };
    TestText_CheckErrorMessage(run, GameInput_RegisterAction(Input, Intern(run, &Fixture, "tbp:jump"), &Good, Pack, NULL),
        ErrorCode_InvalidOperation, BothSources);
    GameActionCategoryInfo Category = { .DisplayName = (const unsigned char*)u8"Again",
        .Description = (const unsigned char*)u8"", .Order = 1 };
    TEST_CHECK_ERROR(run, GameInput_RegisterCategory(Input, Intern(run, &Fixture, "tbp:movement"), &Category, Pack, NULL),
        ErrorCode_InvalidOperation);

    // The category must be registered now.
    GameActionInfo Info = MakeAction(9999U, GameKey_F, GAME_KEY_MODIFIER_NONE);
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, HammerKey, &Info, Pack, NULL), ErrorCode_InvalidOperation);
    uint32_t Gone = Intern(run, &Fixture, "gone:pack");
    GameActionCategoryId GoneCategory = AddCategory(run, &Fixture, "gone:tools", Gone);
    TEST_CHECK(run, GameInput_WithdrawSource(Input, Gone) == 1U);
    Info = MakeAction(GoneCategory, GameKey_F, GAME_KEY_MODIFIER_NONE);
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, HammerKey, &Info, Pack, NULL), ErrorCode_InvalidOperation);

    // Names and descriptions.
    char Long[GAME_INPUT_MAX_DESCRIPTION_BYTES + 2U];
    memset(Long, 'a', sizeof(Long) - 1U);
    Long[sizeof(Long) - 1U] = '\0';
    char LongName[GAME_INPUT_MAX_DISPLAY_NAME_BYTES + 2U];
    memset(LongName, 'a', sizeof(LongName) - 1U);
    LongName[sizeof(LongName) - 1U] = '\0';
    const char* BadNames[] = { "", LongName, "Two\nLines", "\xff\xfe", "Tab\there" };
    for (size_t Index = 0U; Index < (sizeof(BadNames) / sizeof(BadNames[0])); Index++)
    {
        Info = Good;
        Info.DisplayName = (const unsigned char*)BadNames[Index];
        TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, HammerKey, &Info, Pack, NULL), ErrorCode_IllegalArgument);
        Category.DisplayName = (const unsigned char*)BadNames[Index];
        TEST_CHECK_ERROR(run, GameInput_RegisterCategory(Input, Intern(run, &Fixture, "mymod:other"), &Category, Pack,
            NULL), ErrorCode_IllegalArgument);
    }
    Info = Good;
    Info.DisplayName = NULL;
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, HammerKey, &Info, Pack, NULL), ErrorCode_IllegalArgument);
    Info = Good;
    Info.Description = (const unsigned char*)Long;
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, HammerKey, &Info, Pack, NULL), ErrorCode_IllegalArgument);
    Info.Description = NULL;
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, HammerKey, &Info, Pack, NULL), ErrorCode_IllegalArgument);
    Long[GAME_INPUT_MAX_DESCRIPTION_BYTES] = '\0'; // Exactly the most: allowed.
    LongName[GAME_INPUT_MAX_DISPLAY_NAME_BYTES] = '\0';
    Info = Good;
    Info.Description = (const unsigned char*)Long;
    Info.DisplayName = (const unsigned char*)LongName;
    GameActionId Hammer = UINT32_MAX;
    TEST_CHECK_SUCCESS(run, GameInput_RegisterAction(Input, HammerKey, &Info, Pack, &Hammer));
    TEST_CHECK(run, (Hammer == (GameActionId)GameAction_Count) && HasDisplayName(Input, Hammer, LongName));

    // Bindings, kind, keeps-a-binding, keys and sources.
    uint32_t SawKey = Intern(run, &Fixture, "mymod:saw");
    Info = Good;
    Info.DefaultBindings[0].Key = GameKey_Count;
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, SawKey, &Info, Pack, NULL), ErrorCode_IllegalArgument);
    Info = Good;
    Info.DefaultBindingCount = GAME_INPUT_MAX_BINDINGS_PER_ACTION + 1U;
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, SawKey, &Info, Pack, NULL), ErrorCode_ArgumentOutOfRange);
    Info = Good;
    Info.Kind = GameActionKind_Count;
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, SawKey, &Info, Pack, NULL), ErrorCode_IllegalArgument);
    Info = MakeAction(Tools, GameKey_Unknown, GAME_KEY_MODIFIER_NONE);
    Info.KeepsABinding = true;
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, SawKey, &Info, Pack, NULL), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, 123456U, &Good, Pack, NULL), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, SawKey, &Good, 123456U, NULL), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, SawKey, NULL, Pack, NULL), ErrorCode_IllegalArgument);
    TEST_CHECK(run, (GameInput_GetActionCount(Input) == ((uint32_t)GameAction_Count + 1U))
        && !GameInput_FindActionText(Input, (const unsigned char*)u8"mymod:saw", &Hammer));

    // The maxima: the categories, then the actions.
    Category.DisplayName = (const unsigned char*)u8"Filler";
    bool AreAllRegistered = true;
    for (uint32_t Index = GameInput_GetCategoryCount(Input); Index < GAME_INPUT_MAX_CATEGORIES; Index++)
    {
        char Key[64];
        snprintf(Key, sizeof(Key), "fill:category_%u", (unsigned)Index);
        Error Result = GameInput_RegisterCategory(Input, Intern(run, &Fixture, Key), &Category, Pack, NULL);
        AreAllRegistered = AreAllRegistered && (Result.Code == ErrorCode_Success);
        Error_Deconstruct(&Result);
    }
    TEST_CHECK(run, AreAllRegistered);
    TEST_CHECK_ERROR(run, GameInput_RegisterCategory(Input, Intern(run, &Fixture, "fill:one_more"), &Category, Pack, NULL),
        ErrorCode_InvalidOperation);
    Info = MakeAction(Tools, GameKey_Unknown, GAME_KEY_MODIFIER_NONE);
    for (uint32_t Index = GameInput_GetActionCount(Input); Index < GAME_INPUT_MAX_ACTIONS; Index++)
    {
        char Key[64];
        snprintf(Key, sizeof(Key), "fill:action_%u", (unsigned)Index);
        Error Result = GameInput_RegisterAction(Input, Intern(run, &Fixture, Key), &Info, Pack, NULL);
        AreAllRegistered = AreAllRegistered && (Result.Code == ErrorCode_Success);
        Error_Deconstruct(&Result);
    }
    TEST_CHECK(run, AreAllRegistered && (GameInput_GetActionCount(Input) == GAME_INPUT_MAX_ACTIONS));
    TEST_CHECK_ERROR(run, GameInput_RegisterAction(Input, Intern(run, &Fixture, "fill:one_more"), &Info, Pack, NULL),
        ErrorCode_InvalidOperation);
    Fixture_Deconstruct(run, &Fixture);
}

/* A pack's action reads like the engine's: pressed by its key, the most specific binding on a key wins across sources,
 * and consumption is per physical key. */
static void PackActionsReadLikeTheEngines(TestRun* run)
{
    InputFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        Fixture_Deconstruct(run, &Fixture);
        return;
    }
    GameInput* Input = &Fixture.Input;
    uint32_t Pack = Intern(run, &Fixture, "mymod:pack");
    GameActionCategoryId Tools = AddCategory(run, &Fixture, "mymod:tools", Pack);
    GameActionId Hammer = AddAction(run, &Fixture, "mymod:hammer", Tools, GameKey_F, GAME_KEY_MODIFIER_NONE, Pack);
    GameActionId Switch = AddAction(run, &Fixture, "mymod:switch", Tools, GameKey_Tab, GAME_KEY_MODIFIER_CONTROL, Pack);
    GameActionId Found = UINT32_MAX;
    TEST_CHECK(run, GameInput_FindAction(Input, Intern(run, &Fixture, "mymod:hammer"), &Found) && (Found == Hammer));
    TEST_CHECK(run, GameInput_GetActionSource(Input, Hammer) == Pack);
    GameInputSample Sample = MakeSample();

    Sample.KeyStates[GameKey_F] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    Tick(Input, &Sample);
    GameKey Keys[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    TEST_CHECK(run, GameInput_IsActionPressed(Input, Hammer) && GameInput_IsActionDown(Input, Hammer));
    TEST_CHECK(run, (GameInput_GetActionKeys(Input, Hammer, Keys) == 1U) && (Keys[0] == GameKey_F));
    GameInput_ConsumeAction(Input, Hammer);
    TEST_CHECK(run, GameInput_IsKeyConsumed(Input, GameKey_F) && !GameInput_IsActionPressed(Input, Hammer));

    // Control+Tab is only the pack's chord, not the engine's plain Tab; plain Tab is only the engine's.
    Sample.KeyStates[GameKey_F] = 0;
    Sample.KeyStates[GameKey_Tab] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressed(Input, Switch) && !GameInput_IsActionPressed(Input, GameAction_UINext));
    GameInput_ConsumeAction(Input, Switch);
    Sample.KeyStates[GameKey_LeftControl] = 0;
    Sample.KeyStates[GameKey_Tab] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(Input, &Sample);
    TEST_CHECK(run, !GameInput_IsActionDown(Input, GameAction_UINext)); // The pack consumed the physical key.
    Sample.KeyStates[GameKey_Tab] = 0;
    Tick(Input, &Sample);
    Sample.KeyStates[GameKey_Tab] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    Tick(Input, &Sample);
    TEST_CHECK(run, GameInput_IsActionPressed(Input, GameAction_UINext) && !GameInput_IsActionPressed(Input, Switch));

    // Unknown ids read as not pressed.
    TEST_CHECK(run, !GameInput_IsActionDown(Input, 99999U) && !GameInput_IsActionActive(Input, 99999U)
        && (GameInput_GetActionKeyText(Input, 99999U) == NULL) && (GameInput_GetBindingCount(Input, 99999U) == 0U));
    Fixture_Deconstruct(run, &Fixture);
}

/* The key index is right after every kind of change, and holds only active actions' bindings. */
static void TheKeyIndexFollowsEveryChange(TestRun* run)
{
    InputFixture Fixture;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    if (!Fixture_Construct(run, &Fixture, NULL)
        || !WriteControlsFile(run, "key_index", "{ \"bindings\": { \"jump\": [\"j\"] } }", &Path))
    {
        Fixture_Deconstruct(run, &Fixture);
        Memory_Free(Path._data);
        return;
    }
    GameInput* Input = &Fixture.Input;
    TEST_CHECK(run, (GameInput_GetKeyBindingCount(Input, GameKey_W) == 1U)
        && (CountOnKey(Input, GameKey_W, GameAction_MoveForward) == 1U));
    TEST_CHECK(run, (GameInput_GetKeyBindingCount(Input, GameKey_Space) == 2U)
        && (CountOnKey(Input, GameKey_Space, GameAction_Jump) == 1U) && (CountOnKey(Input, GameKey_Space, GameAction_MoveUp) == 1U));
    TEST_CHECK(run, (GameInput_GetKeyBindingCount(Input, GameKey_Unknown) == 0U)
        && (GameInput_GetKeyBindingCount(Input, GameKey_Count) == 0U));

    // Set, reset one, reset all.
    GameInputBinding Bindings[] = { { .Key = GameKey_Up, .Modifiers = GAME_KEY_MODIFIER_NONE }, { .Key = GameKey_I, .Modifiers = GAME_KEY_MODIFIER_SHIFT } };
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_MoveForward, Bindings, 2U));
    TEST_CHECK(run, (GameInput_GetKeyBindingCount(Input, GameKey_W) == 0U)
        && (CountOnKey(Input, GameKey_I, GameAction_MoveForward) == 1U) && (GameInput_GetKeyBindingCount(Input, GameKey_Up) == 2U));
    GameActionId Id = UINT32_MAX;
    GameInputBinding Binding;
    TEST_CHECK(run, GameInput_TryGetKeyBinding(Input, GameKey_I, 0U, &Id, &Binding) && (Id == GameAction_MoveForward)
        && (Binding.Key == GameKey_I) && (Binding.Modifiers == GAME_KEY_MODIFIER_SHIFT));
    TEST_CHECK(run, !GameInput_TryGetKeyBinding(Input, GameKey_I, 1U, &Id, &Binding));
    TEST_CHECK_SUCCESS(run, GameInput_ResetActionBindings(Input, GameAction_MoveForward));
    TEST_CHECK(run, (CountOnKey(Input, GameKey_W, GameAction_MoveForward) == 1U) && (GameInput_GetKeyBindingCount(Input, GameKey_I) == 0U));
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_Sprint, Bindings, 1U));
    GameInput_ResetBindings(Input);
    TEST_CHECK(run, (GameInput_GetKeyBindingCount(Input, GameKey_Up) == 1U)
        && (CountOnKey(Input, GameKey_LeftShift, GameAction_Sprint) == 1U));

    // A file, a registration, an override, a withdrawal.
    TEST_CHECK_SUCCESS(run, GameInput_LoadBindingsFromFile(Input, Path._data));
    TEST_CHECK(run, (CountOnKey(Input, GameKey_J, GameAction_Jump) == 1U) && (GameInput_GetKeyBindingCount(Input, GameKey_Space) == 1U));
    uint32_t Pack = Intern(run, &Fixture, "mymod:pack");
    GameActionCategoryId Tools = AddCategory(run, &Fixture, "mymod:tools", Pack);
    GameActionId Hammer = AddAction(run, &Fixture, "mymod:hammer", Tools, GameKey_K, GAME_KEY_MODIFIER_NONE, Pack);
    TEST_CHECK(run, CountOnKey(Input, GameKey_K, Hammer) == 1U);
    GameActionOverride Override = { .OverridesDefaultBindings = true,
        .DefaultBindings = { { .Key = GameKey_L, .Modifiers = GAME_KEY_MODIFIER_NONE } }, .DefaultBindingCount = 1U };
    TEST_CHECK_SUCCESS(run, GameInput_OverrideAction(Input, Hammer, &Override, Pack));
    TEST_CHECK(run, (GameInput_GetKeyBindingCount(Input, GameKey_K) == 0U) && (CountOnKey(Input, GameKey_L, Hammer) == 1U));
    TEST_CHECK(run, GameInput_WithdrawSource(Input, Pack) == 3U); // The override, the action and the category.
    TEST_CHECK(run, (GameInput_GetKeyBindingCount(Input, GameKey_L) == 0U) && (GameInput_GetKeyBindingCount(Input, GameKey_K) == 0U));
    Fixture_Deconstruct(run, &Fixture);
    Memory_Free(Path._data);
}

/* With thousands of actions on other keys, a query looks only at the bindings on its own key. */
static void AQueryLooksOnlyAtItsKeysBindings(TestRun* run)
{
    InputFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        Fixture_Deconstruct(run, &Fixture);
        return;
    }
    GameInput* Input = &Fixture.Input;
    uint32_t Pack = Intern(run, &Fixture, "mymod:pack");
    GameActionCategoryId Fillers = AddCategory(run, &Fixture, "mymod:fillers", Pack);
    static const GameKey FillerKeys[] = { GameKey_F1, GameKey_F2, GameKey_F3, GameKey_F4, GameKey_F5, GameKey_F6 };
    GameActionId First = UINT32_MAX;
    for (uint32_t Index = 0U; Index < INPUT_TEST_FILLER_ACTIONS; Index++)
    {
        char Key[64];
        snprintf(Key, sizeof(Key), "mymod:filler_%u", (unsigned)Index);
        GameActionId Id = AddAction(run, &Fixture, Key, Fillers, FillerKeys[Index % 6U], GAME_KEY_MODIFIER_NONE, Pack);
        First = (Index == 0U) ? Id : First;
    }
    GameInputSample Sample = MakeSample();
    Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(Input, &Sample);
    uint64_t Before = GameInputInternal_GetMatchVisitCount();
    TEST_CHECK(run, GameInput_IsActionDown(Input, GameAction_MoveForward));
    uint64_t Visits = GameInputInternal_GetMatchVisitCount() - Before;
    TEST_CHECK(run, (Visits == 1U) && (GameInput_GetKeyBindingCount(Input, GameKey_W) == 1U));

    // A key many bindings share is looked at in full.
    Sample.KeyStates[GameKey_F1] = GAME_INPUT_KEY_STATE_DOWN;
    Tick(Input, &Sample);
    Before = GameInputInternal_GetMatchVisitCount();
    TEST_CHECK(run, GameInput_IsActionDown(Input, First));
    Visits = GameInputInternal_GetMatchVisitCount() - Before;
    TEST_CHECK(run, (Visits == GameInput_GetKeyBindingCount(Input, GameKey_F1))
        && (Visits == ((INPUT_TEST_FILLER_ACTIONS + 5U) / 6U)));
    Fixture_Deconstruct(run, &Fixture);
}


// Tests: overrides and withdrawal.
static void OverridesChangeNamesDefaultsAndOrder(TestRun* run)
{
    InputFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, NULL))
    {
        Fixture_Deconstruct(run, &Fixture);
        return;
    }
    GameInput* Input = &Fixture.Input;
    uint32_t Story = Intern(run, &Fixture, "story:pack");
    uint32_t Other = Intern(run, &Fixture, "other:pack");
    GameActionOverride Inspect = { .OverridesDisplayName = true, .DisplayName = (const unsigned char*)u8"Inspect",
        .OverridesDefaultBindings = true, .DefaultBindings = { { .Key = GameKey_F, .Modifiers = GAME_KEY_MODIFIER_NONE } },
        .DefaultBindingCount = 1U };
    TEST_CHECK_SUCCESS(run, GameInput_OverrideAction(Input, GameAction_Use, &Inspect, Story));
    GameActionInfo Info;
    TEST_CHECK(run, GameInput_TryGetActionInfo(Input, GameAction_Use, &Info)
        && (strcmp((const char*)Info.DisplayName, "Inspect") == 0) && (strcmp((const char*)Info.Description, "Uses what the character looks at.") == 0)
        && (Info.DefaultBindingCount == 1U) && (Info.DefaultBindings[0].Key == GameKey_F));
    TEST_CHECK(run, HasBinding(Input, GameAction_Use, 0U, GameKey_F, GAME_KEY_MODIFIER_NONE)
        && !GameInput_IsSetByPlayer(Input, GameAction_Use) && (GameInput_GetKeyBindingCount(Input, GameKey_E) == 0U));
    uint32_t Holder = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK(run, GameInput_TryGetActionOverrideSource(Input, GameAction_Use, &Holder) && (Holder == Story));

    // Another source is refused; the holder replaces its own.
    static const char* const BothSources[] = { "story:pack", "other:pack", "tbp:use", NULL };
    TestText_CheckErrorMessage(run, GameInput_OverrideAction(Input, GameAction_Use, &Inspect, Other),
        ErrorCode_InvalidOperation, BothSources);
    GameActionOverride Examine = { .OverridesDescription = true, .Description = (const unsigned char*)u8"Looks closer." };
    TEST_CHECK_SUCCESS(run, GameInput_OverrideAction(Input, GameAction_Use, &Examine, Story));
    TEST_CHECK(run, GameInput_TryGetActionInfo(Input, GameAction_Use, &Info) && (strcmp((const char*)Info.DisplayName, "Use") == 0)
        && (strcmp((const char*)Info.Description, "Looks closer.") == 0) && HasBinding(Input, GameAction_Use, 0U, GameKey_E, 0U));

    // Bindings the player set win over changed defaults.
    GameInputBinding PlayerKey = { .Key = GameKey_G, .Modifiers = GAME_KEY_MODIFIER_NONE };
    TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_Jump, &PlayerKey, 1U));
    GameActionOverride Leap = { .OverridesDefaultBindings = true,
        .DefaultBindings = { { .Key = GameKey_H, .Modifiers = GAME_KEY_MODIFIER_NONE } }, .DefaultBindingCount = 1U };
    TEST_CHECK_SUCCESS(run, GameInput_OverrideAction(Input, GameAction_Jump, &Leap, Other));
    TEST_CHECK(run, HasBinding(Input, GameAction_Jump, 0U, GameKey_G, GAME_KEY_MODIFIER_NONE));
    TEST_CHECK_SUCCESS(run, GameInput_ResetActionBindings(Input, GameAction_Jump));
    TEST_CHECK(run, HasBinding(Input, GameAction_Jump, 0U, GameKey_H, GAME_KEY_MODIFIER_NONE));

    // Rules: something must change, the target must be active, an action that keeps a binding keeps a default.
    GameActionOverride Nothing;
    Memory_Zero(&Nothing, sizeof(Nothing));
    TEST_CHECK_ERROR(run, GameInput_OverrideAction(Input, GameAction_Sprint, &Nothing, Story), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, GameInput_OverrideAction(Input, 99999U, &Examine, Story), ErrorCode_InvalidOperation);
    GameActionOverride NoKeys = { .OverridesDefaultBindings = true, .DefaultBindingCount = 0U };
    TEST_CHECK_ERROR(run, GameInput_OverrideAction(Input, GameAction_Pause, &NoKeys, Story), ErrorCode_InvalidOperation);
    TEST_CHECK_SUCCESS(run, GameInput_OverrideAction(Input, GameAction_Sprint, &NoKeys, Story));
    TEST_CHECK(run, GameInput_GetBindingCount(Input, GameAction_Sprint) == 0U); // Others may lose their defaults.
    GameActionOverride BadName = { .OverridesDisplayName = true, .DisplayName = (const unsigned char*)u8"" };
    TEST_CHECK_ERROR(run, GameInput_OverrideAction(Input, GameAction_Crouch, &BadName, Story), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, GameInput_OverrideAction(Input, GameAction_Crouch, &Examine, 123456U), ErrorCode_ArgumentOutOfRange);

    // Categories.
    GameActionCategoryOverride First = { .OverridesOrder = true, .Order = -5, .OverridesDisplayName = true,
        .DisplayName = (const unsigned char*)u8"Walking" };
    TEST_CHECK_SUCCESS(run, GameInput_OverrideCategory(Input, GameActionCategory_Movement, &First, Story));
    GameActionCategoryInfo CategoryInfo;
    TEST_CHECK(run, GameInput_TryGetCategoryInfo(Input, GameActionCategory_Movement, &CategoryInfo)
        && (CategoryInfo.Order == -5) && (strcmp((const char*)CategoryInfo.DisplayName, "Walking") == 0));
    TEST_CHECK_ERROR(run, GameInput_OverrideCategory(Input, GameActionCategory_Movement, &First, Other),
        ErrorCode_InvalidOperation);
    GameActionCategoryOverride NoChange;
    Memory_Zero(&NoChange, sizeof(NoChange));
    TEST_CHECK_ERROR(run, GameInput_OverrideCategory(Input, GameActionCategory_General, &NoChange, Story),
        ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, GameInput_OverrideCategory(Input, 999U, &First, Story), ErrorCode_InvalidOperation);

    // Withdrawing a source restores what was there before it.
    TEST_CHECK(run, GameInput_WithdrawSource(Input, Story) == 3U); // Use, sprint, movement.
    TEST_CHECK(run, GameInput_TryGetActionInfo(Input, GameAction_Use, &Info) && (strcmp((const char*)Info.Description, "Uses what the character looks at.") == 0));
    TEST_CHECK(run, HasBinding(Input, GameAction_Sprint, 0U, GameKey_LeftShift, GAME_KEY_MODIFIER_NONE)
        && !GameInput_TryGetActionOverrideSource(Input, GameAction_Use, &Holder));
    TEST_CHECK(run, GameInput_TryGetCategoryInfo(Input, GameActionCategory_Movement, &CategoryInfo) && (CategoryInfo.Order == 100));
    TEST_CHECK(run, GameInput_WithdrawSource(Input, Other) == 1U);
    TEST_CHECK(run, HasBinding(Input, GameAction_Jump, 0U, GameKey_Space, GAME_KEY_MODIFIER_NONE));

    // An override that empties an action's defaults never leaves it without a binding once it revives keeping one.
    uint32_t Pack = Intern(run, &Fixture, "mymod:pack");
    GameActionCategoryId Tools = AddCategory(run, &Fixture, "mymod:tools", Pack);
    GameActionId Hammer = AddAction(run, &Fixture, "mymod:hammer", Tools, GameKey_F, GAME_KEY_MODIFIER_NONE, Pack);
    TEST_CHECK_SUCCESS(run, GameInput_OverrideAction(Input, Hammer, &NoKeys, Story));
    TEST_CHECK(run, GameInput_GetBindingCount(Input, Hammer) == 0U);
    GameInput_WithdrawSource(Input, Pack);
    Tools = AddCategory(run, &Fixture, "mymod:tools", Pack);
    GameActionInfo Keeping = { .DisplayName = (const unsigned char*)u8"Hammer", .Description = (const unsigned char*)u8"",
        .Category = Tools, .DefaultBindings = { { .Key = GameKey_G, .Modifiers = GAME_KEY_MODIFIER_NONE } },
        .DefaultBindingCount = 1U, .Kind = GameActionKind_Button, .KeepsABinding = true };
    TEST_CHECK_SUCCESS(run, GameInput_RegisterAction(Input, Intern(run, &Fixture, "mymod:hammer"), &Keeping, Pack, NULL));
    TEST_CHECK(run, HasBinding(Input, Hammer, 0U, GameKey_G, GAME_KEY_MODIFIER_NONE)
        && GameInput_TryGetActionInfo(Input, Hammer, &Info) && (Info.DefaultBindingCount == 1U));
    Fixture_Deconstruct(run, &Fixture);
}

/* A withdrawn action reads not pressed and leaves the key index, but keeps the player's bindings (saved, and back when
 * it revives with the same id); one the player never set takes its new registration's defaults. */
static void WithdrawnActionsKeepThePlayersBindings(TestRun* run)
{
    InputFixture Fixture;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    if (Fixture_Construct(run, &Fixture, NULL) && WriteControlsFile(run, "withdrawn", NULL, &Path))
    {
        GameInput* Input = &Fixture.Input;
        uint32_t Pack = Intern(run, &Fixture, "mymod:pack");
        GameActionCategoryId Tools = AddCategory(run, &Fixture, "mymod:tools", Pack);
        GameActionId Hammer = AddAction(run, &Fixture, "mymod:hammer", Tools, GameKey_F, GAME_KEY_MODIFIER_NONE, Pack);
        GameActionId Saw = AddAction(run, &Fixture, "mymod:saw", Tools, GameKey_K, GAME_KEY_MODIFIER_NONE, Pack);
        GameActionId Drill = AddAction(run, &Fixture, "mymod:drill", Tools, GameKey_M, GAME_KEY_MODIFIER_NONE, Pack);
        GameInputBinding PlayerKey = { .Key = GameKey_G, .Modifiers = GAME_KEY_MODIFIER_NONE };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, Hammer, &PlayerKey, 1U));

        TEST_CHECK(run, GameInput_WithdrawSource(Input, Pack) == 4U);
        GameInputSample Sample = MakeSample();
        Sample.KeyStates[GameKey_G] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        Tick(Input, &Sample);
        TEST_CHECK(run, !GameInput_IsActionActive(Input, Hammer) && !GameInput_IsActionPressed(Input, Hammer));
        TEST_CHECK(run, (GameInput_GetKeyBindingCount(Input, GameKey_G) == 0U) && (GameInput_GetBindingCount(Input, Hammer) == 1U));
        TEST_CHECK_ERROR(run, GameInput_SetBindings(Input, Hammer, &PlayerKey, 1U), ErrorCode_InvalidOperation);
        GameActionInfo Info;
        TEST_CHECK(run, !GameInput_TryGetActionInfo(Input, Hammer, &Info)
            && (strcmp((const char*)GameInput_GetActionKeyText(Input, Hammer), "mymod:hammer") == 0));
        TEST_CHECK_SUCCESS(run, GameInput_SaveBindingsToFile(Input, Path._data));
        if (ReadFile(run, &Path, &Text))
        {
            TEST_CHECK(run, (strstr((const char*)Text._data, "\"mymod:hammer\"") != NULL)
                && (strstr((const char*)Text._data, "\"g\"") != NULL) && (strstr((const char*)Text._data, "saw") == NULL)
                && (strstr((const char*)Text._data, "drill") == NULL));
        }
        // A file's entry for a withdrawn action is the player's binding for when it revives.
        GenericBuffer_Clear(&Path);
        TEST_CHECK(run, WriteControlsFile(run, "withdrawn_load", "{ \"bindings\": { \"mymod:saw\": [\"x\"] } }", &Path)
            && TEST_CHECK_SUCCESS(run, GameInput_LoadBindingsFromFile(Input, Path._data)));
        TEST_CHECK(run, (GameInput_GetKeyBindingCount(Input, GameKey_X) == 0U) && GameInput_IsSetByPlayer(Input, Saw));

        // Revived: the same ids; the player's bindings come back, the others' new defaults apply.
        Tools = AddCategory(run, &Fixture, "mymod:tools", Pack);
        TEST_CHECK(run, AddAction(run, &Fixture, "mymod:saw", Tools, GameKey_N, GAME_KEY_MODIFIER_NONE, Pack) == Saw);
        TEST_CHECK(run, AddAction(run, &Fixture, "mymod:hammer", Tools, GameKey_H, GAME_KEY_MODIFIER_NONE, Pack) == Hammer);
        TEST_CHECK(run, AddAction(run, &Fixture, "mymod:drill", Tools, GameKey_O, GAME_KEY_MODIFIER_NONE, Pack) == Drill);
        TEST_CHECK(run, HasBinding(Input, Hammer, 0U, GameKey_G, GAME_KEY_MODIFIER_NONE) && GameInput_IsSetByPlayer(Input, Hammer));
        TEST_CHECK(run, HasBinding(Input, Saw, 0U, GameKey_X, GAME_KEY_MODIFIER_NONE) && GameInput_IsSetByPlayer(Input, Saw));
        TEST_CHECK(run, HasBinding(Input, Drill, 0U, GameKey_O, GAME_KEY_MODIFIER_NONE) && !GameInput_IsSetByPlayer(Input, Drill));
        Tick(Input, &Sample);
        TEST_CHECK(run, GameInput_IsActionPressed(Input, Hammer));

        // A category withdrawn under another source's action leaves the action registered.
        uint32_t Other = Intern(run, &Fixture, "other:pack");
        GameActionId Chisel = AddAction(run, &Fixture, "other:chisel", Tools, GameKey_C, GAME_KEY_MODIFIER_NONE, Other);
        TEST_CHECK(run, GameInput_WithdrawSource(Input, Pack) == 4U);
        TEST_CHECK(run, GameInput_IsActionActive(Input, Chisel) && !GameInput_IsCategoryActive(Input, Tools));
    }
    Fixture_Deconstruct(run, &Fixture);
    Memory_Free(Text._data);
    Memory_Free(Path._data);
}

/* An action that keeps a binding is never left with none: an empty list restores its defaults, logged once. */
static void ActionsThatKeepABindingRestoreTheirDefaults(TestRun* run)
{
    InputFixture Fixture;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    if (Fixture_Construct(run, &Fixture, "keeps_a_binding_log")
        && WriteControlsFile(run, "keeps_a_binding", "{ \"bindings\": { \"pause\": [], \"sprint\": [] } }", &Path))
    {
        GameInput* Input = &Fixture.Input;
        GameInputBinding PlayerKey = { .Key = GameKey_P, .Modifiers = GAME_KEY_MODIFIER_NONE };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_Pause, &PlayerKey, 1U));
        TEST_CHECK(run, HasBinding(Input, GameAction_Pause, 0U, GameKey_P, 0U) && GameInput_IsSetByPlayer(Input, GameAction_Pause));
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_Pause, NULL, 0U));
        TEST_CHECK(run, HasBinding(Input, GameAction_Pause, 0U, GameKey_Escape, 0U)
            && (GameInput_GetBindingCount(Input, GameAction_Pause) == 1U) && !GameInput_IsSetByPlayer(Input, GameAction_Pause));

        // The file's empty list too; other actions can be unbound.
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_Pause, &PlayerKey, 1U));
        TEST_CHECK_SUCCESS(run, GameInput_LoadBindingsFromFile(Input, Path._data));
        TEST_CHECK(run, HasBinding(Input, GameAction_Pause, 0U, GameKey_Escape, 0U) && !GameInput_IsSetByPlayer(Input, GameAction_Pause));
        TEST_CHECK(run, (GameInput_GetBindingCount(Input, GameAction_Sprint) == 0U) && GameInput_IsSetByPlayer(Input, GameAction_Sprint));

        // A pack's action may keep a binding too.
        uint32_t Pack = Intern(run, &Fixture, "mymod:pack");
        GameActionCategoryId Tools = AddCategory(run, &Fixture, "mymod:tools", Pack);
        GameActionInfo Info = MakeAction(Tools, GameKey_J, GAME_KEY_MODIFIER_NONE);
        Info.KeepsABinding = true;
        GameActionId Journal = UINT32_MAX;
        TEST_CHECK_SUCCESS(run, GameInput_RegisterAction(Input, Intern(run, &Fixture, "mymod:journal"), &Info, Pack, &Journal));
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, Journal, NULL, 0U));
        TEST_CHECK(run, HasBinding(Input, Journal, 0U, GameKey_J, 0U));
        Fixture_CloseLog(run, &Fixture);
        TEST_CHECK(run, CountInLog(run, &Fixture, "\"tbp:pause\" always keeps a binding") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "leaves \"tbp:pause\" without a binding") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "\"mymod:journal\" always keeps a binding") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "sprint") == 0U);
    }
    Fixture_Deconstruct(run, &Fixture);
    Memory_Free(Path._data);
}

/* A pack's default on a key another action uses is logged, naming both; the engine's own shared defaults are not. */
static void DefaultConflictsOfPacksAreLogged(TestRun* run)
{
    InputFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "conflicts"))
    {
        uint32_t Pack = Intern(run, &Fixture, "mymod:pack");
        GameActionCategoryId Tools = AddCategory(run, &Fixture, "mymod:tools", Pack);
        AddAction(run, &Fixture, "mymod:hammer", Tools, GameKey_E, GAME_KEY_MODIFIER_NONE, Pack); // tbp:use's key.
        AddAction(run, &Fixture, "mymod:saw", Tools, GameKey_E, GAME_KEY_MODIFIER_SHIFT, Pack); // Another chord.
        GameActionId Chisel = AddAction(run, &Fixture, "mymod:chisel", Tools, GameKey_K, GAME_KEY_MODIFIER_NONE, Pack);
        GameActionOverride OnSpace = { .OverridesDefaultBindings = true,
            .DefaultBindings = { { .Key = GameKey_Space, .Modifiers = GAME_KEY_MODIFIER_NONE } }, .DefaultBindingCount = 1U };
        TEST_CHECK_SUCCESS(run, GameInput_OverrideAction(&Fixture.Input, Chisel, &OnSpace, Pack));
        Fixture_CloseLog(run, &Fixture);
        TEST_CHECK(run, CountInLog(run, &Fixture, "\"mymod:hammer\"'s default binding \"e\" is also bound to \"tbp:use\"") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "\"mymod:saw\"'s default") == 0U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "\"mymod:chisel\"'s default binding \"space\" is also bound to") == 2U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "tbp:jump\"'s default") == 0U);
    }
    Fixture_Deconstruct(run, &Fixture);
}

/* The listing names categories in menu order, actions by key, with bindings, sources and overrides. */
static void TheListingShowsEveryAction(TestRun* run)
{
    InputFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "listing"))
    {
        GameInput* Input = &Fixture.Input;
        uint32_t Pack = Intern(run, &Fixture, "mymod:pack");
        uint32_t Other = Intern(run, &Fixture, "other:pack");
        GameActionCategoryId Tools = AddCategory(run, &Fixture, "mymod:tools", Pack);
        AddAction(run, &Fixture, "mymod:hammer", Tools, GameKey_F, GAME_KEY_MODIFIER_NONE, Pack);
        AddAction(run, &Fixture, "other:chisel", Tools, GameKey_Unknown, GAME_KEY_MODIFIER_NONE, Other);
        GameActionOverride Inspect = { .OverridesDisplayName = true, .DisplayName = (const unsigned char*)u8"Inspect" };
        TEST_CHECK_SUCCESS(run, GameInput_OverrideAction(Input, GameAction_Use, &Inspect, Pack));
        GameInputBinding Bindings[] = { { .Key = GameKey_Up, .Modifiers = GAME_KEY_MODIFIER_NONE }, { .Key = GameKey_W, .Modifiers = GAME_KEY_MODIFIER_CONTROL } };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_MoveForward, Bindings, 2U));
        GameInput_LogActions(Input);
        GameInput_WithdrawSource(Input, Pack);
        GameInput_LogActions(Input);
        Fixture_CloseLog(run, &Fixture);
        TEST_CHECK(run, CountInLog(run, &Fixture, "21 actions in 5 categories:") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "Movement (tbp:movement, order 100):") == 2U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "Tools (mymod:tools, order 500, from mymod:pack):") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "tbp:move_forward \"Move Forward\": up, control+w (set by the player)") == 2U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "tbp:use \"Inspect\": e, changed by mymod:pack") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "mymod:hammer \"Hammer\": f, from mymod:pack") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "other:chisel \"Hammer\": unbound, from other:pack") == 2U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "Without a category:") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "20 actions in 4 categories:") == 1U);
    }
    Fixture_Deconstruct(run, &Fixture);
}


// Tests: the controls file.
/* Saving writes only what the player set, by full key in byte order; loading sets those bindings as the player's and
 * keeps the rest; an old file without namespaces loads into the engine's actions. */
static void ControlsFilesRoundTrip(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    InputFixture Fixture;
    if (Fixture_Construct(run, &Fixture, NULL) && WriteControlsFile(run, "round_trip", NULL, &Path))
    {
        GameInput* Input = &Fixture.Input;
        GameInputBinding Custom[] = { { .Key = GameKey_Up, .Modifiers = GAME_KEY_MODIFIER_NONE }, { .Key = GameKey_E, .Modifiers = GAME_KEY_MODIFIER_CONTROL } };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_MoveForward, Custom, 2U));
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(Input, GameAction_Jump, NULL, 0U));
        TEST_CHECK_SUCCESS(run, GameInput_SaveBindingsToFile(Input, Path._data));
        if (ReadFile(run, &Path, &Text))
        {
            const char* Jump = strstr((const char*)Text._data, "\"tbp:jump\"");
            const char* Forward = strstr((const char*)Text._data, "\"tbp:move_forward\"");
            TEST_CHECK(run, (Jump != NULL) && (Forward != NULL) && (Jump < Forward));
            TEST_CHECK(run, (strstr((const char*)Text._data, "sprint") == NULL) && (strstr((const char*)Text._data, "pause") == NULL));
        }

        GameInput_ResetBindings(Input);
        TEST_CHECK_SUCCESS(run, GameInput_LoadBindingsFromFile(Input, Path._data));
        TEST_CHECK(run, (GameInput_GetBindingCount(Input, GameAction_MoveForward) == 2U)
            && HasBinding(Input, GameAction_MoveForward, 0U, GameKey_Up, GAME_KEY_MODIFIER_NONE)
            && HasBinding(Input, GameAction_MoveForward, 1U, GameKey_E, GAME_KEY_MODIFIER_CONTROL));
        TEST_CHECK(run, (GameInput_GetBindingCount(Input, GameAction_Jump) == 0U) && GameInput_IsSetByPlayer(Input, GameAction_Jump));
        TEST_CHECK(run, HasBinding(Input, GameAction_UIPrevious, 0U, GameKey_Tab, GAME_KEY_MODIFIER_SHIFT)
            && !GameInput_IsSetByPlayer(Input, GameAction_UIPrevious));
    }
    GenericBuffer_Clear(&Path);
    // Files written before keys had namespaces load into the engine's actions; one may mix both spellings.
    if (Fixture.IsReady && WriteControlsFile(run, "old_keys", "{ \"bindings\": { \"sprint\": [\"right_shift\", \"mouse_right\"], \"tbp:crouch\": [\"c\"] } }", &Path))
    {
        GameInput* Input = &Fixture.Input;
        GameInput_ResetBindings(Input);
        TEST_CHECK_SUCCESS(run, GameInput_LoadBindingsFromFile(Input, Path._data));
        TEST_CHECK(run, HasBinding(Input, GameAction_Sprint, 0U, GameKey_RightShift, GAME_KEY_MODIFIER_NONE)
            && HasBinding(Input, GameAction_Sprint, 1U, GameKey_MouseRight, GAME_KEY_MODIFIER_NONE));
        TEST_CHECK(run, HasBinding(Input, GameAction_Crouch, 0U, GameKey_C, GAME_KEY_MODIFIER_NONE));
        TEST_CHECK(run, HasBinding(Input, GameAction_MoveForward, 0U, GameKey_W, GAME_KEY_MODIFIER_NONE));
        // Saved again, the keys are in full.
        TEST_CHECK_SUCCESS(run, GameInput_SaveBindingsToFile(Input, Path._data));
        if (ReadFile(run, &Path, &Text))
        {
            TEST_CHECK(run, (strstr((const char*)Text._data, "\"tbp:sprint\"") != NULL)
                && (strstr((const char*)Text._data, "\"sprint\"") == NULL));
        }
    }
    GenericBuffer_Clear(&Path);
    // A byte order mark, which some editors write, is skipped.
    if (Fixture.IsReady && WriteControlsFile(run, "byte_order_mark", "\xEF\xBB\xBF{ \"bindings\": { \"sprint\": [\"right_control\"] } }", &Path))
    {
        GameInput_ResetBindings(&Fixture.Input);
        TEST_CHECK_SUCCESS(run, GameInput_LoadBindingsFromFile(&Fixture.Input, Path._data));
        TEST_CHECK(run, HasBinding(&Fixture.Input, GameAction_Sprint, 0U, GameKey_RightControl, GAME_KEY_MODIFIER_NONE));
    }
    Error MissingResult = GameInput_LoadBindingsFromFile(&Fixture.Input, (const unsigned char*)u8"/nonexistent/dir/controls.json");
    TEST_CHECK(run, (MissingResult.Code == ErrorCode_FileNotFound) || (MissingResult.Code == ErrorCode_DirectoryNotFound));
    Error_Deconstruct(&MissingResult);
    Fixture_Deconstruct(run, &Fixture);
    Memory_Free(Text._data);
    Memory_Free(Path._data);
}

/* Entries for keys no action has are kept, saved, and taken by the action that registers under the key. */
static void EntriesForUnknownActionsAreKept(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    InputFixture Fixture;
    if (Fixture_Construct(run, &Fixture, NULL)
        && WriteControlsFile(run, "kept", "{ \"bindings\": { \"zmod:open_journal\": [\"j\", \"shift+k\"], "
        "\"amod:dance\": [] } }", &Path))
    {
        GameInput* Input = &Fixture.Input;
        TEST_CHECK_SUCCESS(run, GameInput_LoadBindingsFromFile(Input, Path._data));
        TEST_CHECK_SUCCESS(run, GameInput_SaveBindingsToFile(Input, Path._data));
        if (ReadFile(run, &Path, &Text))
        {
            const char* Dance = strstr((const char*)Text._data, "\"amod:dance\"");
            const char* Journal = strstr((const char*)Text._data, "\"zmod:open_journal\"");
            TEST_CHECK(run, (Dance != NULL) && (Journal != NULL) && (Dance < Journal)
                && (strstr((const char*)Text._data, "\"shift+k\"") != NULL));
        }

        // The action takes the kept bindings as the player's, and they are written once.
        uint32_t Pack = Intern(run, &Fixture, "zmod:pack");
        GameActionCategoryId Tools = AddCategory(run, &Fixture, "zmod:journal", Pack);
        GameActionId Journal = AddAction(run, &Fixture, "zmod:open_journal", Tools, GameKey_O, GAME_KEY_MODIFIER_NONE, Pack);
        TEST_CHECK(run, (GameInput_GetBindingCount(Input, Journal) == 2U) && HasBinding(Input, Journal, 0U, GameKey_J, 0U)
            && HasBinding(Input, Journal, 1U, GameKey_K, GAME_KEY_MODIFIER_SHIFT) && GameInput_IsSetByPlayer(Input, Journal));
        TEST_CHECK_SUCCESS(run, GameInput_SaveBindingsToFile(Input, Path._data));
        if (ReadFile(run, &Path, &Text))
        {
            const char* First = strstr((const char*)Text._data, "\"zmod:open_journal\"");
            TEST_CHECK(run, (First != NULL) && (strstr(First + 1, "\"zmod:open_journal\"") == NULL));
        }

        // Resetting every binding forgets the kept entries.
        GameInput_ResetBindings(Input);
        TEST_CHECK_SUCCESS(run, GameInput_SaveBindingsToFile(Input, Path._data));
        if (ReadFile(run, &Path, &Text))
        {
            TEST_CHECK(run, (strstr((const char*)Text._data, "amod") == NULL) && (strstr((const char*)Text._data, "zmod") == NULL));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
    Memory_Free(Text._data);
    Memory_Free(Path._data);
}

// A file with any bad entry changes nothing, and the error names the entry.
static void BadControlsFilesChangeNothing(TestRun* run)
{
    static const struct
    {
        const char* Name;
        const char* Text;
        const char* Named;
    } Cases[] =
    {
        { "bad_key", "{ \"bindings\": { \"move_back\": [\"down\"], \"sprint\": [\"shiftt\"] } }", "tbp:sprint" },
        { "not_a_list", "{ \"bindings\": { \"move_back\": [\"down\"], \"sprint\": \"left_shift\" } }", "tbp:sprint" },
        { "not_a_string", "{ \"bindings\": { \"move_back\": [\"down\"], \"sprint\": [5] } }", "tbp:sprint" },
        { "too_many", "{ \"bindings\": { \"move_back\": [\"down\"], \"sprint\": [\"a\", \"b\", \"c\", \"d\", \"e\"] } }", "tbp:sprint" },
        { "empty_binding", "{ \"bindings\": { \"move_back\": [\"down\"], \"sprint\": [\"\"] } }", "tbp:sprint" },
        { "bad_action_key", "{ \"bindings\": { \"move_back\": [\"down\"], \"Sprint!\": [\"a\"] } }", "Sprint!" },
        { "key_twice", "{ \"bindings\": { \"move_back\": [\"down\"], \"sprint\": [\"a\"], \"tbp:sprint\": [\"b\"] } }",
            "tbp:sprint\" is listed twice" },
        { "not_an_object", "{ \"bindings\": [] }", NULL },
        { "not_json", "{ \"bindings\": ", NULL },
    };
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, INPUT_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    for (size_t Index = 0; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        InputFixture Fixture;
        GenericBuffer_Clear(&Path);
        if (Fixture_Construct(run, &Fixture, NULL) && WriteControlsFile(run, Cases[Index].Name, Cases[Index].Text, &Path))
        {
            Error Result = GameInput_LoadBindingsFromFile(&Fixture.Input, Path._data);
            bool IsNamed = (Cases[Index].Named == NULL)
                || ((Result.Message != NULL) && (strstr((const char*)Result.Message, Cases[Index].Named) != NULL));
            TEST_CHECK(run, IsNamed);
            TEST_CHECK_ERROR(run, Result, ErrorCode_InvalidJSON);
            TEST_CHECK(run, HasBinding(&Fixture.Input, GameAction_MoveBack, 0U, GameKey_S, GAME_KEY_MODIFIER_NONE)
                && !GameInput_IsSetByPlayer(&Fixture.Input, GameAction_MoveBack));
            TEST_CHECK(run, HasBinding(&Fixture.Input, GameAction_Sprint, 0U, GameKey_LeftShift, GAME_KEY_MODIFIER_NONE));
        }
        Fixture_Deconstruct(run, &Fixture);
    }

    // More entries than allowed.
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"{ \"bindings\": { ");
    for (size_t Index = 0U; Index <= GAME_INPUT_MAX_CONTROLS_ENTRIES; Index++)
    {
        char Entry[64];
        snprintf(Entry, sizeof(Entry), "%s\"mod:a%u\": []", (Index == 0U) ? "" : ", ", (unsigned)Index);
        GenericBuffer_AppendString(&Text, (const unsigned char*)Entry);
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8" } }");
    GenericBuffer_NullTerminate(&Text);
    InputFixture Fixture;
    GenericBuffer_Clear(&Path);
    if (Fixture_Construct(run, &Fixture, NULL) && WriteControlsFile(run, "too_many_entries", (const char*)Text._data, &Path))
    {
        TEST_CHECK_ERROR(run, GameInput_LoadBindingsFromFile(&Fixture.Input, Path._data), ErrorCode_InvalidJSON);
    }
    Fixture_Deconstruct(run, &Fixture);
    Memory_Free(Text._data);
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
        { "the_engine_actions_get_their_enum_values", TheEngineActionsGetTheirEnumValues },
        { "registration_refuses_bad_entries", RegistrationRefusesBadEntries },
        { "pack_actions_read_like_the_engines", PackActionsReadLikeTheEngines },
        { "the_key_index_follows_every_change", TheKeyIndexFollowsEveryChange },
        { "a_query_looks_only_at_its_keys_bindings", AQueryLooksOnlyAtItsKeysBindings },
        { "overrides_change_names_defaults_and_order", OverridesChangeNamesDefaultsAndOrder },
        { "withdrawn_actions_keep_the_players_bindings", WithdrawnActionsKeepThePlayersBindings },
        { "actions_that_keep_a_binding_restore_their_defaults", ActionsThatKeepABindingRestoreTheirDefaults },
        { "default_conflicts_of_packs_are_logged", DefaultConflictsOfPacksAreLogged },
        { "the_listing_shows_every_action", TheListingShowsEveryAction },
        { "controls_files_round_trip", ControlsFilesRoundTrip },
        { "entries_for_unknown_actions_are_kept", EntriesForUnknownActionsAreKept },
        { "bad_controls_files_change_nothing", BadControlsFilesChangeNothing },
    };
    TestRun_RunSuite(run, "GameInput", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
