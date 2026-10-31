#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "GameInput.h"
#include "InputActions.h"
#include "Logger.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"
#include <stdio.h>
#include <string.h>


// Macros.
/** Initial capacity (bytes) of path and text buffers; they grow as needed. */
#define ACTIONS_TEST_BUFFER_CAPACITY ((size_t)256)


// Types.
/** A definition that must be refused: the JSON, the error code and a piece of its message. */
typedef struct BrokenDefinitionStruct
{
    const char* Json;
    ErrorCode Code;
    const char* MessagePart;
} BrokenDefinition;

/* An asset manager, an input with its own key registry, and a logger in a work directory. */
typedef struct ActionsFixtureStruct
{
    TestAssetFixture Assets;
    KeyRegistry Keys;
    GameInput Input;
    Logger Log;
    GenericBuffer Directory;
    bool AreAssetsReady;
    bool HasLogger;
    bool IsReady;
} ActionsFixture;


// Static functions.
static bool Fixture_Construct(TestRun* run, ActionsFixture* self, const char* logDirectory)
{
    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->Directory, ACTIONS_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&self->Keys));
    self->AreAssetsReady = TestAssetFixture_Construct(run, &self->Assets);
    self->HasLogger = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, logDirectory, &self->Directory))
        && TEST_CHECK_SUCCESS(run, Logger_Construct(&self->Log, self->Directory._data));
    self->IsReady = self->AreAssetsReady && self->HasLogger
        && TEST_CHECK_SUCCESS(run, GameInput_Construct(&self->Input, &self->Keys, NULL, &self->Log));
    return self->IsReady;
}

static void Fixture_CloseLog(TestRun* run, ActionsFixture* self)
{
    if (self->HasLogger)
    {
        TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&self->Log));
        self->HasLogger = false;
    }
}

static void Fixture_Deconstruct(TestRun* run, ActionsFixture* self)
{
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&self->Input));
    Fixture_CloseLog(run, self);
    if (self->AreAssetsReady)
    {
        TestAssetFixture_Deconstruct(run, &self->Assets);
    }
    KeyRegistry_Deconstruct(&self->Keys);
    Memory_Free(self->Directory._data);
}

/* Counts the occurrences of @p needle in the fixture's closed log. */
static size_t CountInLog(TestRun* run, const ActionsFixture* self, const char* needle)
{
    size_t Count = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, ACTIONS_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, ACTIONS_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
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

/* Builds a definition from JSON with the real type; returns its error (the definition is destroyed). */
static Error BuildDefinition(TestAssetFixture* fixture, const char* json)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, fixture->Types.InputActions, &Bytes,
        (const unsigned char*)u8"test input actions", &Definition);
    Memory_Free(Bytes._data);
    if (Definition != NULL)
    {
        Definition->VTable->Destroy(Definition);
    }
    return Result;
}

static bool AreTextsEqual(const unsigned char* text, const char* expected)
{
    return (text != NULL) && (strcmp((const char*)text, expected) == 0);
}

static bool HasDisplayName(const GameInput* input, GameActionId id, const char* name)
{
    GameActionInfo Info;
    return GameInput_TryGetActionInfo(input, id, &Info) && AreTextsEqual(Info.DisplayName, name);
}


// Tests.
/* Every part of a definition is read: the category, its actions in file order, the overrides with what they change. */
static void TheReaderTakesAFullDefinition(TestRun* run)
{
    static const char* const Json =
        "{\"name\": \"mymod:journal\", \"display_name\": \"Journal\", \"description\": \"Reading and writing.\", "
        "\"order\": 500, \"actions\": {"
        "  \"open_journal\": {\"display_name\": \"Open Journal\", \"default_bindings\": [\"j\"], \"keeps_a_binding\": true},"
        "  \"next_page\": {\"display_name\": \"Next Page\", \"description\": \"Turns a page.\", "
        "    \"default_bindings\": [\"page_down\", \"shift+mouse_side\"], \"kind\": \"button\"}},"
        "\"overrides\": {\"use\": {\"display_name\": \"Inspect\", \"default_bindings\": []}, "
        "  \"other:thing\": {\"description\": \"\"}},"
        "\"category_overrides\": {\"tbp:movement\": {\"order\": -3}}}";
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    InputActions* Loaded = NULL;
    if (TestAssetFixture_Construct(run, &Fixture) && TestAssets_AddInputActions(run, &Fixture, Json)
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadInputActions(Fixture.Manager, (const unsigned char*)u8"mymod:journal",
        User, &Loaded)))
    {
        TEST_CHECK(run, AreTextsEqual(Loaded->Name, "mymod:journal") && AreTextsEqual(Loaded->DisplayName, "Journal")
            && AreTextsEqual(Loaded->Description, "Reading and writing.") && (Loaded->Order == 500));
        if (TEST_CHECK(run, Loaded->ActionCount == 2U))
        {
            const InputActionsAction* Open = &Loaded->Actions[0];
            const InputActionsAction* Next = &Loaded->Actions[1];
            TEST_CHECK(run, AreTextsEqual(Open->Key, "mymod:open_journal") && AreTextsEqual(Open->Description, "")
                && Open->KeepsABinding && (Open->DefaultBindingCount == 1U) && (Open->DefaultBindings[0].Key == GameKey_J));
            TEST_CHECK(run, AreTextsEqual(Next->Key, "mymod:next_page") && !Next->KeepsABinding
                && (Next->Kind == GameActionKind_Button) && (Next->DefaultBindingCount == 2U)
                && (Next->DefaultBindings[1].Key == GameKey_MouseSide)
                && (Next->DefaultBindings[1].Modifiers == GAME_KEY_MODIFIER_SHIFT));
        }
        if (TEST_CHECK(run, (Loaded->OverrideCount == 2U) && (Loaded->CategoryOverrideCount == 1U)))
        {
            const InputActionsOverride* Use = &Loaded->Overrides[0];
            TEST_CHECK(run, AreTextsEqual(Use->Key, "tbp:use") && Use->OverridesDisplayName
                && AreTextsEqual(Use->DisplayName, "Inspect") && !Use->OverridesDescription
                && Use->OverridesDefaultBindings && (Use->DefaultBindingCount == 0U));
            const InputActionsOverride* Thing = &Loaded->Overrides[1];
            TEST_CHECK(run, AreTextsEqual(Thing->Key, "other:thing") && Thing->OverridesDescription
                && !Thing->OverridesDisplayName && !Thing->OverridesDefaultBindings);
            const InputActionsCategoryOverride* Movement = &Loaded->CategoryOverrides[0];
            TEST_CHECK(run, AreTextsEqual(Movement->Key, "tbp:movement") && Movement->OverridesOrder
                && (Movement->Order == -3) && !Movement->OverridesDisplayName);
        }

        // A copy owns its own strings.
        InputActions* Copy = InputActions_Clone(Loaded);
        TEST_CHECK(run, AreTextsEqual(Copy->Actions[1].Key, "mymod:next_page") && (Copy->Actions[1].Key != Loaded->Actions[1].Key)
            && AreTextsEqual(Copy->Overrides[0].DisplayName, "Inspect") && (Copy->CategoryOverrides[0].Order == -3));
        InputActions_Destroy(Copy);
    }

    // The smallest definition: a category alone, in the default namespace when written short.
    InputActions* Small = NULL;
    if (TestAssets_AddInputActions(run, &Fixture, "{\"name\": \"plain\", \"display_name\": \"Plain\"}")
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadInputActions(Fixture.Manager, (const unsigned char*)u8"tbp:plain", User,
        &Small)))
    {
        TEST_CHECK(run, AreTextsEqual(Small->Name, "tbp:plain") && (Small->Order == INPUT_ACTIONS_DEFAULT_ORDER)
            && (Small->ActionCount == 0U) && (Small->OverrideCount == 0U) && AreTextsEqual(Small->Description, ""));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* Every rule is checked when the definition is read, with the path to the bad value in the message. */
static void TheReaderRefusesEveryBrokenRule(TestRun* run)
{
    static const BrokenDefinition Cases[] =
    {
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"colour\": 1}", ErrorCode_InvalidAssetDefinition, "colour" },
        { "{\"name\": \"m:a\"}", ErrorCode_InvalidAssetDefinition, "\"display_name\" is required" },
        { "{\"name\": \"m:a\", \"display_name\": \"\"}", ErrorCode_InvalidAssetDefinition, "\"display_name\" is empty" },
        { "{\"name\": \"m:a\", \"display_name\": 5}", ErrorCode_InvalidJSON, "must be a string" },
        { "{\"name\": \"m:a\", \"display_name\": \"Line\\nbreak\"}", ErrorCode_InvalidAssetDefinition,
            "control character" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"description\": \"Nul\\u0000here\"}",
            ErrorCode_InvalidAssetDefinition, "NUL" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"order\": 3000000000}", ErrorCode_InvalidAssetDefinition,
            "out of range" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"order\": \"first\"}", ErrorCode_InvalidJSON, "its root" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": []}", ErrorCode_InvalidJSON, "expected an object" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"b\": {}}}", ErrorCode_InvalidAssetDefinition,
            "actions.b: \"display_name\" is required" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"b\": {\"display_name\": \"B\", \"key\": \"x\"}}}",
            ErrorCode_InvalidAssetDefinition, "actions.b" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"Big\": {\"display_name\": \"B\"}}}",
            ErrorCode_InvalidAssetDefinition, "not a key path" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"b:c\": {\"display_name\": \"B\"}}}",
            ErrorCode_InvalidAssetDefinition, "not a key path" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"\": {\"display_name\": \"B\"}}}",
            ErrorCode_InvalidAssetDefinition, "not a key path" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"b\": {\"display_name\": \"B\", "
            "\"default_bindings\": \"x\"}}}", ErrorCode_InvalidJSON, "must be a list" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"b\": {\"display_name\": \"B\", "
            "\"default_bindings\": [\"a\", \"b\", \"c\", \"d\", \"e\"]}}}", ErrorCode_InvalidAssetDefinition,
            "at most 4" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"b\": {\"display_name\": \"B\", "
            "\"default_bindings\": [\"shiftt+a\"]}}}", ErrorCode_InvalidAssetDefinition, "default binding 0" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"b\": {\"display_name\": \"B\", "
            "\"default_bindings\": [7]}}}", ErrorCode_InvalidAssetDefinition, "default binding 0" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"b\": {\"display_name\": \"B\", "
            "\"keeps_a_binding\": true}}}", ErrorCode_InvalidAssetDefinition, "needs a default binding" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"b\": {\"display_name\": \"B\", "
            "\"keeps_a_binding\": 3}}}", ErrorCode_InvalidJSON, "actions.b" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"b\": {\"display_name\": \"B\", "
            "\"kind\": \"axis\"}}}", ErrorCode_InvalidAssetDefinition, "\"kind\" must be \"button\"" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"overrides\": {\"Use\": {\"display_name\": \"B\"}}}",
            ErrorCode_InvalidAssetDefinition, "not an action key" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"overrides\": {\"tbp:use\": {}}}",
            ErrorCode_InvalidAssetDefinition, "changes nothing" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"overrides\": {\"tbp:use\": {\"keeps_a_binding\": true}}}",
            ErrorCode_InvalidAssetDefinition, "keeps_a_binding" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"overrides\": {\"use\": {\"display_name\": \"B\"}, "
            "\"tbp:use\": {\"description\": \"C\"}}}", ErrorCode_InvalidAssetDefinition, "overridden twice" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"overrides\": {\"tbp:use\": {\"display_name\": \"\"}}}",
            ErrorCode_InvalidAssetDefinition, "overrides.tbp:use" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"category_overrides\": {\"tbp:movement\": {}}}",
            ErrorCode_InvalidAssetDefinition, "changes nothing" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"category_overrides\": {\"tbp:movement\": {\"order\": 1.5}}}",
            ErrorCode_InvalidJSON, "category_overrides.tbp:movement" },
        { "{\"name\": \"m:a\", \"display_name\": \"A\", \"category_overrides\": {\"tbp:movement\": "
            "{\"default_bindings\": []}}}", ErrorCode_InvalidAssetDefinition, "default_bindings" },
    };
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        TestAssetFixture_Deconstruct(run, &Fixture);
        return;
    }
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        const char* const Parts[] = { "\"m:a\"", Cases[Index].MessagePart, NULL };
        if (!TestText_CheckErrorMessage(run, BuildDefinition(&Fixture, Cases[Index].Json), Cases[Index].Code, Parts))
        {
            printf("    (case %u: %s)\n", (unsigned)Index, Cases[Index].Json);
        }
    }

    // Keys too long, and too many entries.
    char Json[16384];
    char LongName[NAMESPACED_KEY_MAX_LENGTH + 1U];
    memset(LongName, 'a', NAMESPACED_KEY_MAX_LENGTH - 1U);
    LongName[NAMESPACED_KEY_MAX_LENGTH - 1U] = '\0'; // "m:" and the name: one byte too many.
    snprintf(Json, sizeof(Json), "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"%s\": "
        "{\"display_name\": \"B\"}}}", LongName);
    const char* const TooLong[] = { "at most 128 are allowed", NULL };
    TestText_CheckErrorMessage(run, BuildDefinition(&Fixture, Json), ErrorCode_InvalidAssetDefinition, TooLong);
    LongName[NAMESPACED_KEY_MAX_LENGTH - 2U] = '\0'; // Exactly the most.
    snprintf(Json, sizeof(Json), "{\"name\": \"m:a\", \"display_name\": \"A\", \"actions\": {\"%s\": "
        "{\"display_name\": \"B\"}}}", LongName);
    TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, Json));
    size_t Length = (size_t)snprintf(Json, sizeof(Json), "{\"name\": \"m:a\", \"display_name\": \"A\", \"overrides\": {");
    for (size_t Index = 0U; Index <= INPUT_ACTIONS_MAX_OVERRIDES; Index++)
    {
        Length += (size_t)snprintf(Json + Length, sizeof(Json) - Length, "%s\"x:a%u\": {\"display_name\": \"B\"}",
            (Index == 0U) ? "" : ", ", (unsigned)Index);
    }
    snprintf(Json + Length, sizeof(Json) - Length, "}}");
    const char* const TooMany[] = { "257 entries; at most 256", NULL };
    TestText_CheckErrorMessage(run, BuildDefinition(&Fixture, Json), ErrorCode_InvalidAssetDefinition, TooMany);
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* Every definition registers under its own key as the source, all or nothing; overrides apply after every
 * definition's actions exist, all or nothing per definition, and never over another source's override. */
static void DefinitionsRegisterAllOrNothing(TestRun* run)
{
    static const char* const Definitions[] =
    {
        // Overrides an action a definition later in byte order declares, and the engine's "use".
        "{\"name\": \"cmod:renames\", \"display_name\": \"Renames\", "
        "\"overrides\": {\"zmod:late_action\": {\"display_name\": \"Renamed\"}, \"tbp:use\": {\"display_name\": \"Inspect\"}},"
        "\"category_overrides\": {\"tbp:general\": {\"order\": 1}}}",
        "{\"name\": \"amod:tools\", \"display_name\": \"Tools\", \"actions\": {"
        "  \"hammer\": {\"display_name\": \"Hammer\", \"default_bindings\": [\"f\"]},"
        "  \"saw\": {\"display_name\": \"Saw\"}}}",
        // Its "jump" is the engine's "tbp:jump": nothing of it registers.
        "{\"name\": \"tbp:extra\", \"display_name\": \"Extra\", \"actions\": {"
        "  \"extra_action\": {\"display_name\": \"Extra\"}, \"jump\": {\"display_name\": \"Jump Again\"}}}",
        // "tbp:use" is changed by cmod already: its overrides are refused, its action stays.
        "{\"name\": \"dmod:second\", \"display_name\": \"Second\", \"actions\": {\"dance\": {\"display_name\": \"Dance\"}},"
        "\"overrides\": {\"amod:hammer\": {\"display_name\": \"Mallet\"}, \"tbp:use\": {\"display_name\": \"Look\"}}}",
        // Overrides an action nobody registered.
        "{\"name\": \"emod:missing\", \"display_name\": \"Missing\", \"actions\": {\"sing\": {\"display_name\": \"Sing\"}},"
        "\"overrides\": {\"nomod:ghost\": {\"display_name\": \"Boo\"}}}",
        "{\"name\": \"zmod:late\", \"display_name\": \"Late\", \"actions\": {\"late_action\": {\"display_name\": \"Late\"}}}",
    };
    ActionsFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, "input_actions_registration"))
    {
        Fixture_Deconstruct(run, &Fixture);
        return;
    }
    bool AreAdded = true;
    for (size_t Index = 0U; Index < (sizeof(Definitions) / sizeof(Definitions[0])); Index++)
    {
        AreAdded = TestAssets_AddInputActions(run, &Fixture.Assets, Definitions[Index]) && AreAdded;
    }
    GameInput* Input = &Fixture.Input;
    if (AreAdded && TEST_CHECK_SUCCESS(run, InputActions_RegisterDefinitions(Fixture.Assets.Manager, Input, &Fixture.Log)))
    {
        GameActionId Hammer = UINT32_MAX;
        GameActionId Late = UINT32_MAX;
        GameActionId Id = UINT32_MAX;
        uint32_t ToolsSource = NAMESPACED_KEY_ID_NONE;
        TEST_CHECK(run, GameInput_FindActionText(Input, (const unsigned char*)u8"amod:hammer", &Hammer)
            && KeyRegistry_TryFind(&Fixture.Keys, (const unsigned char*)u8"amod:tools", &ToolsSource)
            && (GameInput_GetActionSource(Input, Hammer) == ToolsSource));
        GameActionInfo Info;
        GameActionCategoryInfo CategoryInfo;
        GameActionCategoryId Tools = UINT32_MAX;
        TEST_CHECK(run, GameInput_TryGetActionInfo(Input, Hammer, &Info) && (Info.DefaultBindings[0].Key == GameKey_F)
            && GameInput_FindCategoryText(Input, (const unsigned char*)u8"amod:tools", &Tools) && (Info.Category == Tools));
        TEST_CHECK(run, GameInput_TryGetCategoryInfo(Input, Tools, &CategoryInfo)
            && (CategoryInfo.Order == INPUT_ACTIONS_DEFAULT_ORDER) && AreTextsEqual(CategoryInfo.DisplayName, "Tools"));
        TEST_CHECK(run, GameInput_FindActionText(Input, (const unsigned char*)u8"amod:saw", &Id));

        // All or nothing: the clashing definition registered neither its category nor its other action.
        TEST_CHECK(run, !GameInput_FindActionText(Input, (const unsigned char*)u8"tbp:extra_action", &Id)
            && !GameInput_FindCategoryText(Input, (const unsigned char*)u8"tbp:extra", &Tools)
            && HasDisplayName(Input, GameAction_Jump, "Jump"));

        // Overrides of an action registered later in byte order apply; a second override of one action is refused,
        // and takes its definition's other overrides with it; an unknown target refuses its definition's overrides.
        TEST_CHECK(run, GameInput_FindActionText(Input, (const unsigned char*)u8"zmod:late_action", &Late)
            && HasDisplayName(Input, Late, "Renamed") && HasDisplayName(Input, GameAction_Use, "Inspect"));
        TEST_CHECK(run, HasDisplayName(Input, Hammer, "Hammer"));
        TEST_CHECK(run, GameInput_FindActionText(Input, (const unsigned char*)u8"dmod:dance", &Id)
            && GameInput_FindActionText(Input, (const unsigned char*)u8"emod:sing", &Id));
        TEST_CHECK(run, GameInput_TryGetCategoryInfo(Input, GameActionCategory_General, &CategoryInfo)
            && (CategoryInfo.Order == 1));

        // One definition withdraws with one call.
        uint32_t Renames = NAMESPACED_KEY_ID_NONE;
        TEST_CHECK(run, KeyRegistry_TryFind(&Fixture.Keys, (const unsigned char*)u8"cmod:renames", &Renames)
            && (GameInput_WithdrawSource(Input, Renames) == 4U)); // Two overrides, one category override, its category.
        TEST_CHECK(run, HasDisplayName(Input, GameAction_Use, "Use") && HasDisplayName(Input, Late, "Late"));

        Fixture_CloseLog(run, &Fixture);
        TEST_CHECK(run, CountInLog(run, &Fixture, "Input actions \"tbp:extra\" registered nothing") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "tbp:jump\" is registered already (by the engine)") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "\"dmod:second\": its overrides were not applied") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "\"tbp:use\" is changed by cmod:renames already") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "the action \"nomod:ghost\" it changes is not registered") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "Input actions: 5 of 6 definitions registered, with 5 actions.") == 1U);
    }
    Fixture_Deconstruct(run, &Fixture);
}

/* The registration helpers refuse what they cannot do and leave nothing behind. */
static void RegisteringOneDefinitionIsAllOrNothing(TestRun* run)
{
    ActionsFixture Fixture;
    if (!Fixture_Construct(run, &Fixture, "input_actions_one"))
    {
        Fixture_Deconstruct(run, &Fixture);
        return;
    }
    GameInput* Input = &Fixture.Input;
    AssetUserID User = ASSET_USER_ID_INVALID;
    InputActions* Tools = NULL;
    if (TestAssets_AddInputActions(run, &Fixture.Assets, "{\"name\": \"amod:tools\", \"display_name\": \"Tools\", "
        "\"actions\": {\"hammer\": {\"display_name\": \"Hammer\"}, \"saw\": {\"display_name\": \"Saw\"}}}")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Assets.Manager, &User))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadInputActions(Fixture.Assets.Manager,
        (const unsigned char*)u8"amod:tools", User, &Tools)))
    {
        // Another source holds one of its keys: nothing registers.
        uint32_t Other = NAMESPACED_KEY_ID_NONE;
        uint32_t SawKey = NAMESPACED_KEY_ID_NONE;
        TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Fixture.Keys, (const unsigned char*)u8"other:pack", &Other));
        TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Fixture.Keys, (const unsigned char*)u8"amod:saw", &SawKey));
        GameActionInfo Saw = { .DisplayName = (const unsigned char*)u8"Saw", .Description = (const unsigned char*)u8"",
            .Category = GameActionCategory_General, .Kind = GameActionKind_Button };
        TEST_CHECK_SUCCESS(run, GameInput_RegisterAction(Input, SawKey, &Saw, Other, NULL));
        uint32_t Count = GameInput_GetActionCount(Input);
        const char* const Parts[] = { "amod:saw", "registered already (by other:pack)", NULL };
        TestText_CheckErrorMessage(run, InputActions_RegisterCategoryAndActions(Tools, Input),
            ErrorCode_InvalidOperation, Parts);
        GameActionId Id = UINT32_MAX;
        GameActionCategoryId Category = UINT32_MAX;
        TEST_CHECK(run, (GameInput_GetActionCount(Input) == Count)
            && !GameInput_FindActionText(Input, (const unsigned char*)u8"amod:hammer", &Id)
            && !GameInput_FindCategoryText(Input, (const unsigned char*)u8"amod:tools", &Category));

        // Once the other source is gone, it registers; a second time it is refused.
        GameInput_WithdrawSource(Input, Other);
        TEST_CHECK_SUCCESS(run, InputActions_RegisterCategoryAndActions(Tools, Input));
        TEST_CHECK(run, GameInput_FindActionText(Input, (const unsigned char*)u8"amod:hammer", &Id)
            && GameInput_FindActionText(Input, (const unsigned char*)u8"amod:saw", &Id));
        TEST_CHECK_ERROR(run, InputActions_RegisterCategoryAndActions(Tools, Input), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, InputActions_ApplyOverrides(Tools, Input)); // It has none.

        // Room for one action only: the first of two registers, the second cannot, and the first goes again.
        InputActions* Pair = NULL;
        bool IsFull = true;
        for (uint32_t Index = GameInput_GetActionCount(Input); IsFull && (Index < (GAME_INPUT_MAX_ACTIONS - 1U)); Index++)
        {
            char Key[64];
            snprintf(Key, sizeof(Key), "fill:action_%u", (unsigned)Index);
            uint32_t KeyId = NAMESPACED_KEY_ID_NONE;
            IsFull = TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Fixture.Keys, (const unsigned char*)Key, &KeyId))
                && TEST_CHECK_SUCCESS(run, GameInput_RegisterAction(Input, KeyId, &Saw, Other, NULL));
        }
        if (IsFull && TestAssets_AddInputActions(run, &Fixture.Assets, "{\"name\": \"bmod:pair\", \"display_name\": "
            "\"Pair\", \"actions\": {\"first\": {\"display_name\": \"First\"}, \"second\": {\"display_name\": "
            "\"Second\"}}}") && TEST_CHECK_SUCCESS(run, AssetManager_LoadInputActions(Fixture.Assets.Manager,
            (const unsigned char*)u8"bmod:pair", User, &Pair)))
        {
            const char* const Full[] = { "is full", "bmod:second", NULL };
            TestText_CheckErrorMessage(run, InputActions_RegisterCategoryAndActions(Pair, Input), ErrorCode_InvalidOperation,
                Full);
            TEST_CHECK(run, !GameInput_FindActionText(Input, (const unsigned char*)u8"bmod:first", &Id)
                && !GameInput_FindCategoryText(Input, (const unsigned char*)u8"bmod:pair", &Category));
        }
    }
    TEST_CHECK_ERROR(run, InputActions_RegisterDefinitions(NULL, Input, NULL), ErrorCode_IllegalArgument);
    Fixture_Deconstruct(run, &Fixture);
}

/* The base game's own definitions (compile/out/asset/input_actions) read without a failure and register: the test
 * frame's wave is an action from data. */
static void TheShippedTestFrameActionsRegister(TestRun* run)
{
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, ACTIONS_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    AssetManager* Manager = NULL;
    StandardAssetTypes Types;
    JSONObjectPool* Pool = NULL;
    size_t FailedCount = 1U;
    KeyRegistry Keys;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys));
    GameInput Input;
    bool IsInputReady = TEST_CHECK_SUCCESS(run, GameInput_Construct(&Input, &Keys, NULL, NULL));
    if (IsInputReady && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/asset", &Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(Manager, &Types, &Pool))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Manager, Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Manager, &FailedCount))
        && TEST_CHECK(run, FailedCount == 0U)
        && TEST_CHECK_SUCCESS(run, InputActions_RegisterDefinitions(Manager, &Input, NULL)))
    {
        GameActionId WaveAction = UINT32_MAX;
        GameActionInfo Info;
        GameActionCategoryInfo Category;
        uint32_t Source = NAMESPACED_KEY_ID_NONE;
        TEST_CHECK(run, GameInput_FindActionText(&Input, (const unsigned char*)u8"tbp:test_wave", &WaveAction)
            && GameInput_TryGetActionInfo(&Input, WaveAction, &Info) && (Info.DefaultBindingCount == 1U)
            && (Info.DefaultBindings[0].Key == GameKey_0) && (Info.DefaultBindings[0].Modifiers == GAME_KEY_MODIFIER_NONE)
            && GameInput_TryGetCategoryInfo(&Input, Info.Category, &Category) && (Category.Order == 900)
            && AreTextsEqual(GameInput_GetCategoryKeyText(&Input, Info.Category), "tbp:test_frame"));
        TEST_CHECK(run, KeyRegistry_TryFind(&Keys, (const unsigned char*)u8"tbp:test_frame", &Source)
            && (GameInput_GetActionSource(&Input, WaveAction) == Source));
    }
    if (Manager != NULL)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_Deconstruct(Manager));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Input));
    KeyRegistry_Deconstruct(&Keys);
    Memory_Free(Root._data);
}


// Public functions.
void InputActionsTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_reader_takes_a_full_definition", TheReaderTakesAFullDefinition },
        { "the_reader_refuses_every_broken_rule", TheReaderRefusesEveryBrokenRule },
        { "definitions_register_all_or_nothing", DefinitionsRegisterAllOrNothing },
        { "registering_one_definition_is_all_or_nothing", RegisteringOneDefinitionIsAllOrNothing },
        { "the_shipped_test_frame_actions_register", TheShippedTestFrameActionsRegister },
    };
    TestRun_RunSuite(run, "InputActions", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
