#include "TestSuites.h"
#include "TestText.h"
#include "NamespacedKey.h"
#include <stdio.h>
#include <string.h>


// Macros.
/** How many generated keys the growth test interns: enough for several text blocks and map growths. */
#define KEY_TEST_MANY_COUNT ((size_t)5000)
/** Room for one generated key. */
#define KEY_TEST_TEXT_CAPACITY ((size_t)64)


// Static functions: helpers.
/* Parses @p text with @p defaultNamespace and checks the full key is @p expected. */
static void CheckParse(TestRun* run, const char* text, const char* defaultNamespace, const char* expected)
{
    unsigned char Storage[NAMESPACED_KEY_BUFFER_SIZE];
    GenericBuffer Key;
    GenericBuffer_CreateConstant(&Key, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    if (TEST_CHECK_SUCCESS(run, NamespacedKey_Parse((const unsigned char*)text, strlen(text),
        (const unsigned char*)defaultNamespace, &Key)))
    {
        TEST_CHECK(run, (Key._count == (strlen(expected) + 1U)) && (strcmp((const char*)Storage, expected) == 0));
    }
}

/* Checks that parsing @p text fails and that the message names the problem with @p fragment. */
static void CheckParseFails(TestRun* run, const char* text, const char* fragment)
{
    unsigned char Storage[NAMESPACED_KEY_BUFFER_SIZE];
    GenericBuffer Key;
    GenericBuffer_CreateConstant(&Key, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    const char* const Fragments[] = { fragment, NULL };
    TestText_CheckErrorMessage(run, NamespacedKey_Parse((const unsigned char*)text, strlen(text), NULL, &Key),
        ErrorCode_ArgumentOutOfRange, Fragments);
    TEST_CHECK(run, Key._count == 0U); // nothing appended on failure
}


// Static functions: keys.
static void ValidKeysUseTheKeyCharacters(TestRun* run)
{
    const char* const Valid[] = { "a:b", "a:b/c.d", "town:street_lamp", "my.pack-1:x/y/z", "game:0", "0:0" };
    for (size_t Index = 0; Index < (sizeof(Valid) / sizeof(Valid[0])); Index++)
    {
        TEST_CHECK(run, NamespacedKey_IsValid((const unsigned char*)Valid[Index]));
        TEST_CHECK_SUCCESS(run, NamespacedKey_Validate((const unsigned char*)Valid[Index]));
    }

    const char* const Invalid[] = { "", "ab", ":b", "a:", "Town:x", "town:X", "a b:c", "a:b c", "a/b:c", "a:b:c",
        "a:b\\c", "caf\xC3\xA9:x", "#town:x", " town:x" };
    for (size_t Index = 0; Index < (sizeof(Invalid) / sizeof(Invalid[0])); Index++)
    {
        TEST_CHECK(run, !NamespacedKey_IsValid((const unsigned char*)Invalid[Index]));
        TEST_CHECK_ERROR(run, NamespacedKey_Validate((const unsigned char*)Invalid[Index]),
            ErrorCode_ArgumentOutOfRange);
    }
    TEST_CHECK(run, !NamespacedKey_IsValid(NULL));
    TEST_CHECK_ERROR(run, NamespacedKey_Validate(NULL), ErrorCode_IllegalArgument);

    // The messages say what is wrong, and quote the key with anything unprintable as '?'.
    const char* const Uppercase[] = { "\"Town:x\"", "uppercase", NULL };
    TestText_CheckErrorMessage(run, NamespacedKey_Validate((const unsigned char*)u8"Town:x"),
        ErrorCode_ArgumentOutOfRange, Uppercase);
    const char* const NoColon[] = { "namespace:path", NULL };
    TestText_CheckErrorMessage(run, NamespacedKey_Validate((const unsigned char*)u8"lamp"), ErrorCode_ArgumentOutOfRange,
        NoColon);
    const char* const Unprintable[] = { "\"town:a?b\"", NULL };
    TestText_CheckErrorMessage(run, NamespacedKey_Validate((const unsigned char*)"town:a\x01" "b"),
        ErrorCode_ArgumentOutOfRange, Unprintable);
}

static void KeysAreAtMost128Bytes(TestRun* run)
{
    char Text[NAMESPACED_KEY_MAX_LENGTH + 2U];
    memset(Text, 'a', sizeof(Text));
    Text[1] = ':';
    Text[NAMESPACED_KEY_MAX_LENGTH] = '\0'; // exactly 128 bytes
    TEST_CHECK(run, NamespacedKey_IsValid((const unsigned char*)Text));
    Text[NAMESPACED_KEY_MAX_LENGTH] = 'a';
    Text[NAMESPACED_KEY_MAX_LENGTH + 1U] = '\0'; // 129 bytes
    TEST_CHECK(run, !NamespacedKey_IsValid((const unsigned char*)Text));
    const char* const Fragments[] = { "longer than 128 bytes", NULL };
    TestText_CheckErrorMessage(run, NamespacedKey_Validate((const unsigned char*)Text), ErrorCode_ArgumentOutOfRange,
        Fragments);

    // The default namespace counts too: a 124-byte path fits "game:" (5 bytes) only just.
    char Path[NAMESPACED_KEY_MAX_LENGTH];
    memset(Path, 'p', sizeof(Path));
    Path[123] = '\0';
    unsigned char Storage[NAMESPACED_KEY_BUFFER_SIZE];
    GenericBuffer Key;
    GenericBuffer_CreateConstant(&Key, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    TEST_CHECK_SUCCESS(run, NamespacedKey_Parse((const unsigned char*)Path, 123U, NULL, &Key));
    TEST_CHECK(run, Key._count == NAMESPACED_KEY_BUFFER_SIZE);
    GenericBuffer_CreateConstant(&Key, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    TEST_CHECK_ERROR(run, NamespacedKey_Parse((const unsigned char*)Path, 124U, NULL, &Key),
        ErrorCode_ArgumentOutOfRange);
}

static void TheDefaultNamespaceIsApplied(TestRun* run)
{
    CheckParse(run, "a:b", NULL, "a:b");
    CheckParse(run, "a:b/c.d", NULL, "a:b/c.d");
    CheckParse(run, "office", NULL, "game:office");
    CheckParse(run, "office", "town", "town:office");
    CheckParse(run, "town:office", "other", "town:office");

    CheckParseFails(run, "", "empty");
    CheckParseFails(run, ":office", "namespace before the colon is empty");
    CheckParseFails(run, "town:", "path after the colon is empty");
    CheckParseFails(run, "Office", "uppercase");
    CheckParseFails(run, "street lamp", "a path may only use");
    CheckParseFails(run, "a:b:c", "a path may only use");

    // A bad default namespace is the caller's mistake.
    unsigned char Storage[NAMESPACED_KEY_BUFFER_SIZE];
    GenericBuffer Key;
    GenericBuffer_CreateConstant(&Key, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    TEST_CHECK_ERROR(run, NamespacedKey_Parse((const unsigned char*)u8"x", 1U, (const unsigned char*)u8"Bad", &Key),
        ErrorCode_IllegalArgument);
}

static void ParseAppendsToOneGrowingString(TestRun* run)
{
    GenericBuffer Key;
    GenericBuffer_AllocateVariable(&Key, 0U, sizeof(unsigned char));
    TEST_CHECK(run, GenericBuffer_AppendString(&Key, (const unsigned char*)u8"keys: ")
        && GenericBuffer_NullTerminate(&Key));
    TEST_CHECK_SUCCESS(run, NamespacedKey_Parse((const unsigned char*)u8"lamp", 4U, NULL, &Key));
    TEST_CHECK(run, strcmp((const char*)Key._data, "keys: game:lamp") == 0);

    // Too small a buffer gets nothing.
    unsigned char Small[8];
    GenericBuffer Fixed;
    GenericBuffer_CreateConstant(&Fixed, Small, sizeof(Small), sizeof(unsigned char), 0U);
    TEST_CHECK_ERROR(run, NamespacedKey_Parse((const unsigned char*)u8"town:lamp", 9U, NULL, &Fixed),
        ErrorCode_BufferTooSmall);
    TEST_CHECK(run, Fixed._count == 0U);
    Memory_Free(Key._data);
}

static void NamespaceAndPathAccessors(TestRun* run)
{
    const unsigned char* Key = (const unsigned char*)u8"town:street/lamp";
    TEST_CHECK(run, NamespacedKey_GetNamespaceLength(Key) == 4U);
    TEST_CHECK(run, strcmp((const char*)NamespacedKey_GetPath(Key), "street/lamp") == 0);
    TEST_CHECK(run, (NamespacedKey_GetNamespaceLength((const unsigned char*)u8"lamp") == 0U)
        && (NamespacedKey_GetPath((const unsigned char*)u8"lamp") == NULL));
    TEST_CHECK(run, (NamespacedKey_GetNamespaceLength(NULL) == 0U) && (NamespacedKey_GetPath(NULL) == NULL));
    TEST_CHECK(run, NamespacedKey_IsNamespaceValid((const unsigned char*)u8"my.pack-1", 9U));
    TEST_CHECK(run, !NamespacedKey_IsNamespaceValid((const unsigned char*)u8"a/b", 3U));
    TEST_CHECK(run, !NamespacedKey_IsNamespaceValid((const unsigned char*)u8"", 0U));
}


// Static functions: the registry.
static void InterningGivesOneIdPerText(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }

    uint32_t Lamp = NAMESPACED_KEY_ID_NONE;
    uint32_t Desk = NAMESPACED_KEY_ID_NONE;
    uint32_t Again = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"town:lamp", &Lamp));
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"town:desk", &Desk));
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"town:lamp", &Again));
    TEST_CHECK(run, (Lamp == 1U) && (Desk == 2U) && (Again == Lamp) && (KeyRegistry_GetCount(&Keys) == 2U));
    TEST_CHECK(run, strcmp((const char*)KeyRegistry_GetText(&Keys, Desk), "town:desk") == 0);
    TEST_CHECK(run, (KeyRegistry_GetText(&Keys, NAMESPACED_KEY_ID_NONE) == NULL)
        && (KeyRegistry_GetText(&Keys, 3U) == NULL));

    // Text without a namespace gets the default one, so it meets the full form.
    uint32_t Office = NAMESPACED_KEY_ID_NONE;
    uint32_t FullOffice = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_SUCCESS(run, KeyRegistry_InternText(&Keys, (const unsigned char*)u8"office", 6U, NULL, &Office));
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"game:office", &FullOffice));
    TEST_CHECK(run, (Office == FullOffice) && (Office == 3U));

    // Finding does not intern.
    uint32_t Found = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK(run, KeyRegistry_TryFind(&Keys, (const unsigned char*)u8"town:desk", &Found) && (Found == Desk));
    TEST_CHECK(run, !KeyRegistry_TryFind(&Keys, (const unsigned char*)u8"town:chair", &Found));
    TEST_CHECK(run, !KeyRegistry_TryFind(&Keys, (const unsigned char*)u8"Town:Bad Key", &Found));
    TEST_CHECK(run, KeyRegistry_GetCount(&Keys) == 3U);

    // Invalid text is refused and interns nothing.
    uint32_t Bad = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_ERROR(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"office", &Bad), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, KeyRegistry_InternText(&Keys, (const unsigned char*)u8"Office", 6U, NULL, &Bad),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, (Bad == NAMESPACED_KEY_ID_NONE) && (KeyRegistry_GetCount(&Keys) == 3U));
    KeyRegistry_Deconstruct(&Keys);
}

static void ManyKeysKeepTheirIdsAndText(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }

    // The first key's text keeps its address while thousands more are interned (blocks never move).
    uint32_t First = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"test:first", &First));
    const unsigned char* FirstText = KeyRegistry_GetText(&Keys, First);
    char Text[KEY_TEST_TEXT_CAPACITY];
    bool AreAllInterned = true;
    for (size_t Index = 0; Index < KEY_TEST_MANY_COUNT; Index++)
    {
        snprintf(Text, sizeof(Text), "pack%zu:item/%zu", Index % 7U, Index);
        uint32_t Id = NAMESPACED_KEY_ID_NONE;
        Error Result = KeyRegistry_Intern(&Keys, (const unsigned char*)Text, &Id);
        AreAllInterned = AreAllInterned && (Result.Code == ErrorCode_Success) && (Id == (uint32_t)(Index + 2U));
        Error_Deconstruct(&Result);
    }
    TEST_CHECK(run, AreAllInterned && (KeyRegistry_GetCount(&Keys) == (KEY_TEST_MANY_COUNT + 1U)));
    TEST_CHECK(run, (KeyRegistry_GetText(&Keys, First) == FirstText) && (strcmp((const char*)FirstText, "test:first") == 0));

    bool AreAllFound = true;
    for (size_t Index = 0; Index < KEY_TEST_MANY_COUNT; Index++)
    {
        snprintf(Text, sizeof(Text), "pack%zu:item/%zu", Index % 7U, Index);
        uint32_t Id = NAMESPACED_KEY_ID_NONE;
        AreAllFound = AreAllFound && KeyRegistry_TryFind(&Keys, (const unsigned char*)Text, &Id)
            && (Id == (uint32_t)(Index + 2U)) && (strcmp((const char*)KeyRegistry_GetText(&Keys, Id), Text) == 0);
    }
    TEST_CHECK(run, AreAllFound);
    KeyRegistry_Deconstruct(&Keys);
}

static void PrefixSearchListsKeysInOrder(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    const char* const Texts[] = { "town:lamp", "game:office", "town:bench", "tools:hammer", "town:desk" };
    uint32_t Ids[5] = { 0U };
    for (size_t Index = 0; Index < 5U; Index++)
    {
        TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)Texts[Index], &Ids[Index]));
    }

    GenericBuffer Found;
    GenericBuffer_AllocateVariable(&Found, 0U, sizeof(uint32_t));
    uint32_t Marker = 99U; // already in the buffer: kept, and not sorted with the results
    GenericBuffer_AddLast(&Found, &Marker);
    TEST_CHECK_SUCCESS(run, KeyRegistry_FindByPrefix(&Keys, (const unsigned char*)u8"town:", &Found));
    const uint32_t* Results = (const uint32_t*)Found._data;
    TEST_CHECK(run, (Found._count == 4U) && (Results[0] == 99U) && (Results[1] == Ids[2]) && (Results[2] == Ids[4])
        && (Results[3] == Ids[0]));

    GenericBuffer_Clear(&Found);
    TEST_CHECK_SUCCESS(run, KeyRegistry_FindByPrefix(&Keys, (const unsigned char*)u8"to", &Found));
    Results = (const uint32_t*)Found._data;
    TEST_CHECK(run, (Found._count == 4U) && (Results[0] == Ids[3]) && (Results[1] == Ids[2]));

    GenericBuffer_Clear(&Found);
    TEST_CHECK_SUCCESS(run, KeyRegistry_FindByPrefix(&Keys, (const unsigned char*)u8"", &Found));
    TEST_CHECK(run, Found._count == 5U);
    GenericBuffer_Clear(&Found);
    TEST_CHECK_SUCCESS(run, KeyRegistry_FindByPrefix(&Keys, (const unsigned char*)u8"zzz", &Found));
    TEST_CHECK(run, Found._count == 0U);

    GenericBuffer Wrong;
    GenericBuffer_AllocateVariable(&Wrong, 0U, sizeof(uint64_t));
    TEST_CHECK_ERROR(run, KeyRegistry_FindByPrefix(&Keys, (const unsigned char*)u8"town", &Wrong),
        ErrorCode_IllegalArgument);
    Memory_Free(Wrong._data);
    Memory_Free(Found._data);
    KeyRegistry_Deconstruct(&Keys);
}


/* Interns the generated keys @p first to @p last - 1 ("pack<i % 7>:item/<i>"); false when one fails. */
static bool InternGenerated(KeyRegistry* keys, size_t first, size_t last)
{
    char Text[KEY_TEST_TEXT_CAPACITY];
    bool AreAllInterned = true;
    for (size_t Index = first; Index < last; Index++)
    {
        snprintf(Text, sizeof(Text), "pack%zu:item/%zu", Index % 7U, Index);
        uint32_t Id = NAMESPACED_KEY_ID_NONE;
        Error Result = KeyRegistry_Intern(keys, (const unsigned char*)Text, &Id);
        AreAllInterned = AreAllInterned && (Result.Code == ErrorCode_Success);
        Error_Deconstruct(&Result);
    }
    return AreAllInterned;
}

/* Whether the generated keys @p first to @p last - 1 are found with id <index> + @p idOffset (or all absent). */
static bool AreGeneratedFound(KeyRegistry* keys, size_t first, size_t last, size_t idOffset, bool isFound)
{
    char Text[KEY_TEST_TEXT_CAPACITY];
    for (size_t Index = first; Index < last; Index++)
    {
        snprintf(Text, sizeof(Text), "pack%zu:item/%zu", Index % 7U, Index);
        uint32_t Id = NAMESPACED_KEY_ID_NONE;
        bool IsFound = KeyRegistry_TryFind(keys, (const unsigned char*)Text, &Id);
        if ((IsFound != isFound) || (isFound && ((Id != (uint32_t)(Index + idOffset))
            || (strcmp((const char*)KeyRegistry_GetText(keys, Id), Text) != 0))))
        {
            return false;
        }
    }
    return true;
}

static void TruncatingForgetsTheNewestKeys(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    uint32_t Kept = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"test:kept", &Kept));
    const unsigned char* KeptText = KeyRegistry_GetText(&Keys, Kept);
    size_t FirstBlocks = Keys._blocks._count;

    // Thousands of keys over several text blocks; forgetting the newer half keeps the older half as it was.
    size_t Half = KEY_TEST_MANY_COUNT / 2U;
    TEST_CHECK(run, InternGenerated(&Keys, 0U, KEY_TEST_MANY_COUNT));
    size_t AllBlocks = Keys._blocks._count;
    TEST_CHECK(run, AllBlocks > (FirstBlocks + 1U));
    KeyRegistry_TruncateTo(&Keys, Half + 1U);
    TEST_CHECK(run, (KeyRegistry_GetCount(&Keys) == (Half + 1U)) && (Keys._blocks._count < AllBlocks));
    TEST_CHECK(run, AreGeneratedFound(&Keys, 0U, Half, 2U, true)
        && AreGeneratedFound(&Keys, Half, KEY_TEST_MANY_COUNT, 0U, false));
    TEST_CHECK(run, KeyRegistry_GetText(&Keys, (uint32_t)(Half + 2U)) == NULL);

    // The forgotten ids are handed out again, in order: interning the same keys again gives them their old ids.
    TEST_CHECK(run, InternGenerated(&Keys, Half, KEY_TEST_MANY_COUNT));
    TEST_CHECK(run, (Keys._blocks._count == AllBlocks) && AreGeneratedFound(&Keys, 0U, KEY_TEST_MANY_COUNT, 2U, true));

    // Down to the first key: its id, text and address stay, and the next key's text is stored right after it.
    KeyRegistry_TruncateTo(&Keys, 1U);
    uint32_t Found = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK(run, (KeyRegistry_GetCount(&Keys) == 1U) && (Keys._blocks._count == FirstBlocks));
    TEST_CHECK(run, KeyRegistry_TryFind(&Keys, (const unsigned char*)u8"test:kept", &Found) && (Found == Kept)
        && (KeyRegistry_GetText(&Keys, Kept) == KeptText)
        && AreGeneratedFound(&Keys, 0U, KEY_TEST_MANY_COUNT, 0U, false));
    uint32_t Next = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"test:next", &Next));
    TEST_CHECK(run, (Next == 2U) && (KeyRegistry_GetText(&Keys, Next) == (KeptText + strlen("test:kept") + 1U)));

    // At least the count does nothing, 0 forgets everything, and the registry works on afterwards.
    KeyRegistry_TruncateTo(&Keys, 5U);
    TEST_CHECK(run, KeyRegistry_GetCount(&Keys) == 2U);
    KeyRegistry_TruncateTo(&Keys, 0U);
    TEST_CHECK(run, (KeyRegistry_GetCount(&Keys) == 0U) && (Keys._blocks._count == 0U)
        && !KeyRegistry_TryFind(&Keys, (const unsigned char*)u8"test:kept", &Found));
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Keys, (const unsigned char*)u8"test:next", &Next));
    TEST_CHECK(run, (Next == 1U) && KeyRegistry_TryFind(&Keys, (const unsigned char*)u8"test:next", &Found)
        && (Found == 1U));
    KeyRegistry_TruncateTo(NULL, 0U);
    KeyRegistry_Deconstruct(&Keys);
}


// Public functions.
void NamespacedKeyTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "valid_keys_use_the_key_characters", ValidKeysUseTheKeyCharacters },
        { "keys_are_at_most_128_bytes", KeysAreAtMost128Bytes },
        { "the_default_namespace_is_applied", TheDefaultNamespaceIsApplied },
        { "parse_appends_to_one_growing_string", ParseAppendsToOneGrowingString },
        { "namespace_and_path_accessors", NamespaceAndPathAccessors },
        { "interning_gives_one_id_per_text", InterningGivesOneIdPerText },
        { "many_keys_keep_their_ids_and_text", ManyKeysKeepTheirIdsAndText },
        { "prefix_search_lists_keys_in_order", PrefixSearchListsKeysInOrder },
        { "truncating_forgets_the_newest_keys", TruncatingForgetsTheNewestKeys },
    };
    TestRun_RunSuite(run, "NamespacedKey", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
