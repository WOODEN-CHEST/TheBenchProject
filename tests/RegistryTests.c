#include "TestSuites.h"
#include "TestText.h"
#include "Registry.h"
#include <stdio.h>
#include <string.h>


// Macros.
/** How many entries the address test registers: many blocks' worth. */
#define REGISTRY_TEST_MANY_COUNT ((uint32_t)1000)
/** Room for one generated key. */
#define REGISTRY_TEST_TEXT_CAPACITY ((size_t)64)


// Types.
/* The test element: a value and a heap string it owns. */
typedef struct TestEntryStruct
{
    uint32_t Value;
    unsigned char* OwnedText;
} TestEntry;

/* A key registry with a few interned keys, the fixture of most tests. */
typedef struct RegistryFixtureStruct
{
    KeyRegistry Keys;
    uint32_t RegistryKey;
    uint32_t PackA;
    uint32_t PackB;
} RegistryFixture;


// Static functions: helpers.
static uint32_t Intern(TestRun* run, KeyRegistry* keys, const char* key)
{
    uint32_t Id = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(keys, (const unsigned char*)key, &Id));
    return Id;
}

static bool ConstructFixture(TestRun* run, RegistryFixture* fixture)
{
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&fixture->Keys)))
    {
        return false;
    }
    fixture->RegistryKey = Intern(run, &fixture->Keys, "test:things");
    fixture->PackA = Intern(run, &fixture->Keys, "pack_a:pack");
    fixture->PackB = Intern(run, &fixture->Keys, "pack_b:pack");
    return true;
}

/* Counts every deconstruction in the size_t the user data points to, and frees the entry's text. */
static void DeconstructTestEntry(void* element, const UserData* userData)
{
    TestEntry* Entry = element;
    size_t* Count = UserData_GetPointer(userData);
    (*Count)++;
    Memory_Free(Entry->OwnedText);
    Entry->OwnedText = NULL;
}

/* A registry of TestEntry under the fixture's key; with @p deconstructionCount, elements are deconstructed and counted
 * there. */
static bool ConstructRegistry(TestRun* run, RegistryFixture* fixture, uint32_t maxEntryCount,
    size_t* deconstructionCount, Registry* outRegistry)
{
    RegistryConstructOptions Options = RegistryConstructOptions_CreateDefault(&fixture->Keys, fixture->RegistryKey,
        sizeof(TestEntry), maxEntryCount);
    if (deconstructionCount != NULL)
    {
        Options.DeconstructElement = DeconstructTestEntry;
        Options.DeconstructElementUserData = UserData_FromPointer(deconstructionCount);
    }
    return TEST_CHECK_SUCCESS(run, Registry_Construct(outRegistry, &Options));
}

/* An entry that owns a copy of @p text (NULL: owns nothing). */
static TestEntry MakeEntry(uint32_t value, const char* text)
{
    TestEntry Entry = { .Value = value, .OwnedText = NULL };
    if (text != NULL)
    {
        size_t Length = strlen(text);
        Entry.OwnedText = Memory_Allocate(Length + 1U);
        Memory_Copy(text, Entry.OwnedText, Length + 1U);
    }
    return Entry;
}

/* Registers @p value under the key @p key (interned first) for @p source; the index, or REGISTRY_INDEX_NONE. */
static uint32_t RegisterValue(TestRun* run, RegistryFixture* fixture, Registry* registry, const char* key,
    uint32_t value, uint32_t source)
{
    uint32_t KeyId = Intern(run, &fixture->Keys, key);
    TestEntry Entry = MakeEntry(value, NULL);
    uint32_t Index = REGISTRY_INDEX_NONE;
    TEST_CHECK_SUCCESS(run, Registry_Register(registry, KeyId, &Entry, source, &Index));
    return Index;
}

/* The value of the active entry @p index, or UINT32_MAX. */
static uint32_t GetValue(const Registry* registry, uint32_t index)
{
    const TestEntry* Entry = Registry_Get(registry, index);
    return (Entry != NULL) ? Entry->Value : UINT32_MAX;
}


// Static functions: the registry.
static void RegisteringFindsAndGetsEntries(TestRun* run)
{
    RegistryFixture Fixture;
    Registry Things;
    if (!ConstructFixture(run, &Fixture) || !ConstructRegistry(run, &Fixture, 16U, NULL, &Things))
    {
        KeyRegistry_Deconstruct(&Fixture.Keys);
        return;
    }
    TEST_CHECK(run, (Registry_GetCount(&Things) == 0U) && (Registry_GetActiveCount(&Things) == 0U)
        && (Registry_GetRegistryKey(&Things) == Fixture.RegistryKey) && (Registry_GetMaxEntryCount(&Things) == 16U));

    // Indices in registration order; each entry's key, source and element read back.
    uint32_t Lamp = RegisterValue(run, &Fixture, &Things, "town:lamp", 10U, NAMESPACED_KEY_ID_NONE);
    uint32_t Desk = RegisterValue(run, &Fixture, &Things, "town:desk", 20U, Fixture.PackA);
    uint32_t Bench = RegisterValue(run, &Fixture, &Things, "game:bench", 30U, Fixture.PackB);
    TEST_CHECK(run, (Lamp == 0U) && (Desk == 1U) && (Bench == 2U));
    TEST_CHECK(run, (Registry_GetCount(&Things) == 3U) && (Registry_GetActiveCount(&Things) == 3U));
    TEST_CHECK(run, (GetValue(&Things, Lamp) == 10U) && (GetValue(&Things, Desk) == 20U)
        && (GetValue(&Things, Bench) == 30U));
    TEST_CHECK(run, (strcmp((const char*)Registry_GetKeyText(&Things, Desk), "town:desk") == 0)
        && (Registry_GetKey(&Things, Desk) == Intern(run, &Fixture.Keys, "town:desk")));
    TEST_CHECK(run, (Registry_GetSource(&Things, Lamp) == NAMESPACED_KEY_ID_NONE)
        && (Registry_GetSource(&Things, Desk) == Fixture.PackA)
        && (Registry_GetSource(&Things, Bench) == Fixture.PackB));
    TEST_CHECK(run, Registry_IsActive(&Things, Lamp) && Registry_IsActive(&Things, Bench));

    // Finds by id and by full text; finding never interns, and keys the registry does not hold are not found.
    uint32_t Found = REGISTRY_INDEX_NONE;
    TEST_CHECK(run, Registry_Find(&Things, Intern(run, &Fixture.Keys, "game:bench"), &Found) && (Found == Bench));
    TEST_CHECK(run, Registry_FindText(&Things, (const unsigned char*)u8"town:lamp", &Found) && (Found == Lamp));
    size_t KeyCount = KeyRegistry_GetCount(&Fixture.Keys);
    TEST_CHECK(run, !Registry_FindText(&Things, (const unsigned char*)u8"town:chair", &Found));
    TEST_CHECK(run, !Registry_FindText(&Things, (const unsigned char*)u8"lamp", &Found)); // a full key only
    TEST_CHECK(run, !Registry_FindText(&Things, (const unsigned char*)u8"Town:Bad Key", &Found));
    TEST_CHECK(run, KeyRegistry_GetCount(&Fixture.Keys) == KeyCount);
    TEST_CHECK(run, !Registry_Find(&Things, Fixture.PackA, &Found)); // a key of the key registry, not of this registry

    // Indices the registry does not have read as nothing.
    TEST_CHECK(run, (Registry_Get(&Things, 3U) == NULL) && !Registry_IsActive(&Things, 3U)
        && (Registry_GetKey(&Things, 3U) == NAMESPACED_KEY_ID_NONE) && (Registry_GetKeyText(&Things, 3U) == NULL)
        && (Registry_GetSource(&Things, REGISTRY_INDEX_NONE) == NAMESPACED_KEY_ID_NONE));
    Registry_Deconstruct(&Things);
    KeyRegistry_Deconstruct(&Fixture.Keys);
}

static void PrefixSearchListsActiveEntriesInOrder(TestRun* run)
{
    RegistryFixture Fixture;
    Registry Things;
    if (!ConstructFixture(run, &Fixture) || !ConstructRegistry(run, &Fixture, 16U, NULL, &Things))
    {
        KeyRegistry_Deconstruct(&Fixture.Keys);
        return;
    }
    const char* const Texts[] = { "town:lamp", "game:office", "town:bench", "tools:hammer", "town:desk" };
    uint32_t Indices[5] = { 0U };
    for (uint32_t Index = 0U; Index < 5U; Index++)
    {
        Indices[Index] = RegisterValue(run, &Fixture, &Things, Texts[Index], Index, NAMESPACED_KEY_ID_NONE);
    }
    TEST_CHECK_SUCCESS(run, Registry_Withdraw(&Things, Indices[4])); // town:desk is left out from now on
    Intern(run, &Fixture.Keys, "town:chair"); // a key of the program, never registered here

    GenericBuffer Found;
    GenericBuffer_AllocateVariable(&Found, 0U, sizeof(uint32_t));
    uint32_t Marker = 99U; // already in the buffer: kept, and not sorted with the results
    GenericBuffer_AddLast(&Found, &Marker);
    TEST_CHECK_SUCCESS(run, Registry_FindByPrefix(&Things, (const unsigned char*)u8"town:", &Found));
    const uint32_t* Results = (const uint32_t*)Found._data;
    TEST_CHECK(run, (Found._count == 3U) && (Results[0] == 99U) && (Results[1] == Indices[2])
        && (Results[2] == Indices[0]));

    GenericBuffer_Clear(&Found);
    TEST_CHECK_SUCCESS(run, Registry_FindByPrefix(&Things, (const unsigned char*)u8"to", &Found));
    Results = (const uint32_t*)Found._data;
    TEST_CHECK(run, (Found._count == 3U) && (Results[0] == Indices[3]) && (Results[1] == Indices[2])
        && (Results[2] == Indices[0]));

    GenericBuffer_Clear(&Found);
    TEST_CHECK_SUCCESS(run, Registry_FindByPrefix(&Things, (const unsigned char*)u8"", &Found));
    TEST_CHECK(run, Found._count == 4U);
    GenericBuffer_Clear(&Found);
    TEST_CHECK_SUCCESS(run, Registry_FindByPrefix(&Things, (const unsigned char*)u8"zzz", &Found));
    TEST_CHECK(run, Found._count == 0U);

    // The withdrawn entry comes back with its key.
    TestEntry Desk = MakeEntry(44U, NULL);
    TEST_CHECK_SUCCESS(run, Registry_Register(&Things, Registry_GetKey(&Things, Indices[4]), &Desk,
        NAMESPACED_KEY_ID_NONE, NULL));
    GenericBuffer_Clear(&Found);
    TEST_CHECK_SUCCESS(run, Registry_FindByPrefix(&Things, (const unsigned char*)u8"town:d", &Found));
    TEST_CHECK(run, (Found._count == 1U) && (((const uint32_t*)Found._data)[0] == Indices[4]));

    GenericBuffer Wrong;
    GenericBuffer_AllocateVariable(&Wrong, 0U, sizeof(uint64_t));
    TEST_CHECK_ERROR(run, Registry_FindByPrefix(&Things, (const unsigned char*)u8"town", &Wrong),
        ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, Registry_FindByPrefix(&Things, NULL, &Found), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, Registry_FindByPrefix(NULL, (const unsigned char*)u8"town", &Found),
        ErrorCode_IllegalArgument);
    Memory_Free(Wrong._data);
    Memory_Free(Found._data);
    Registry_Deconstruct(&Things);
    KeyRegistry_Deconstruct(&Fixture.Keys);
}

static void AnActiveDuplicateIsRefused(TestRun* run)
{
    RegistryFixture Fixture;
    Registry Things;
    size_t Deconstructed = 0U;
    if (!ConstructFixture(run, &Fixture) || !ConstructRegistry(run, &Fixture, 16U, &Deconstructed, &Things))
    {
        KeyRegistry_Deconstruct(&Fixture.Keys);
        return;
    }
    uint32_t Jump = Intern(run, &Fixture.Keys, "tbp:jump");
    TestEntry First = MakeEntry(1U, "engine's");
    uint32_t Index = REGISTRY_INDEX_NONE;
    TEST_CHECK_SUCCESS(run, Registry_Register(&Things, Jump, &First, NAMESPACED_KEY_ID_NONE, &Index));
    uint64_t Revision = Registry_GetRevision(&Things);

    // Refused, naming the registry, the key and both sources; the caller still owns its element, nothing changed.
    TestEntry Second = MakeEntry(2U, "pack's");
    uint32_t Unchanged = 77U;
    const char* const Fragments[] = { "test:things", "tbp:jump", "the engine", "pack_a:pack", NULL };
    TestText_CheckErrorMessage(run, Registry_Register(&Things, Jump, &Second, Fixture.PackA, &Unchanged),
        ErrorCode_InvalidOperation, Fragments);
    TEST_CHECK(run, (Unchanged == 77U) && (Registry_GetCount(&Things) == 1U) && (Registry_GetActiveCount(&Things) == 1U)
        && (Registry_GetRevision(&Things) == Revision) && (Deconstructed == 0U));
    const TestEntry* Stored = Registry_Get(&Things, Index);
    TEST_CHECK(run, (Stored != NULL) && (Stored->Value == 1U)
        && (strcmp((const char*)Stored->OwnedText, "engine's") == 0)
        && (Registry_GetSource(&Things, Index) == NAMESPACED_KEY_ID_NONE));
    Memory_Free(Second.OwnedText);

    // The same source twice is refused too.
    TestEntry Third = MakeEntry(3U, "again");
    const char* const Again[] = { "by the engine", "so the engine cannot", NULL };
    TestText_CheckErrorMessage(run, Registry_Register(&Things, Jump, &Third, NAMESPACED_KEY_ID_NONE, NULL),
        ErrorCode_InvalidOperation, Again);
    Memory_Free(Third.OwnedText);

    Registry_Deconstruct(&Things);
    TEST_CHECK(run, Deconstructed == 1U);
    KeyRegistry_Deconstruct(&Fixture.Keys);
}

static void AWithdrawnKeyRevivesItsIndex(TestRun* run)
{
    RegistryFixture Fixture;
    Registry Things;
    if (!ConstructFixture(run, &Fixture) || !ConstructRegistry(run, &Fixture, 16U, NULL, &Things))
    {
        KeyRegistry_Deconstruct(&Fixture.Keys);
        return;
    }
    uint32_t A = RegisterValue(run, &Fixture, &Things, "test:a", 1U, NAMESPACED_KEY_ID_NONE);
    uint32_t B = RegisterValue(run, &Fixture, &Things, "test:b", 2U, Fixture.PackA);
    uint32_t C = RegisterValue(run, &Fixture, &Things, "test:c", 3U, NAMESPACED_KEY_ID_NONE);
    uint32_t BKey = Registry_GetKey(&Things, B);

    // Withdrawn: not found, not got, not active; its slot keeps its key and source, and the indices stay.
    TEST_CHECK_SUCCESS(run, Registry_Withdraw(&Things, B));
    uint32_t Found = REGISTRY_INDEX_NONE;
    TEST_CHECK(run, !Registry_Find(&Things, BKey, &Found)
        && !Registry_FindText(&Things, (const unsigned char*)u8"test:b", &Found));
    TEST_CHECK(run, (Registry_Get(&Things, B) == NULL) && !Registry_IsActive(&Things, B)
        && (Registry_GetKey(&Things, B) == BKey) && (Registry_GetSource(&Things, B) == Fixture.PackA));
    TEST_CHECK(run, (Registry_GetCount(&Things) == 3U) && (Registry_GetActiveCount(&Things) == 2U)
        && (GetValue(&Things, A) == 1U) && (GetValue(&Things, C) == 3U));
    // Its slot is still found by key, as active ones are; a key never registered has none.
    TEST_CHECK(run, Registry_FindSlot(&Things, BKey, &Found) && (Found == B)
        && Registry_FindSlot(&Things, Registry_GetKey(&Things, C), &Found) && (Found == C));
    uint32_t Unregistered = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Fixture.Keys, (const unsigned char*)u8"test:never", &Unregistered));
    Found = REGISTRY_INDEX_NONE;
    TEST_CHECK(run, !Registry_FindSlot(&Things, Unregistered, &Found) && (Found == REGISTRY_INDEX_NONE)
        && !Registry_FindSlot(NULL, BKey, &Found));

    // Withdrawing it again, or an index the registry does not have, fails.
    TEST_CHECK_ERROR(run, Registry_Withdraw(&Things, B), ErrorCode_InvalidOperation);
    TEST_CHECK_ERROR(run, Registry_Withdraw(&Things, 3U), ErrorCode_IndexOutOfBounds);
    TEST_CHECK_ERROR(run, Registry_Withdraw(NULL, 0U), ErrorCode_IllegalArgument);

    // Registering the key again revives the same index, with the new element and source.
    TestEntry Revived = MakeEntry(22U, NULL);
    uint32_t Index = REGISTRY_INDEX_NONE;
    TEST_CHECK_SUCCESS(run, Registry_Register(&Things, BKey, &Revived, Fixture.PackB, &Index));
    TEST_CHECK(run, (Index == B) && (GetValue(&Things, B) == 22U) && (Registry_GetSource(&Things, B) == Fixture.PackB)
        && (Registry_GetCount(&Things) == 3U) && (Registry_GetActiveCount(&Things) == 3U));
    TEST_CHECK(run, Registry_Find(&Things, BKey, &Found) && (Found == B));

    // A new key still takes the next index.
    TEST_CHECK(run, RegisterValue(run, &Fixture, &Things, "test:d", 4U, NAMESPACED_KEY_ID_NONE) == 3U);
    Registry_Deconstruct(&Things);
    KeyRegistry_Deconstruct(&Fixture.Keys);
}

static void WithdrawingASourceLeavesTheOthers(TestRun* run)
{
    RegistryFixture Fixture;
    Registry Things;
    if (!ConstructFixture(run, &Fixture) || !ConstructRegistry(run, &Fixture, 16U, NULL, &Things))
    {
        KeyRegistry_Deconstruct(&Fixture.Keys);
        return;
    }
    const uint32_t Sources[] = { NAMESPACED_KEY_ID_NONE, Fixture.PackA, Fixture.PackB, Fixture.PackA,
        NAMESPACED_KEY_ID_NONE, Fixture.PackA };
    const char* const Keys[] = { "test:a", "test:b", "test:c", "test:d", "test:e", "test:f" };
    for (uint32_t Index = 0U; Index < 6U; Index++)
    {
        TEST_CHECK(run, RegisterValue(run, &Fixture, &Things, Keys[Index], Index, Sources[Index]) == Index);
    }

    uint64_t Revision = Registry_GetRevision(&Things);
    TEST_CHECK(run, Registry_WithdrawSource(&Things, Fixture.PackA) == 3U);
    TEST_CHECK(run, (Registry_GetRevision(&Things) == (Revision + 3U)) && (Registry_GetCount(&Things) == 6U)
        && (Registry_GetActiveCount(&Things) == 3U));
    bool IsEachAsExpected = true;
    for (uint32_t Index = 0U; Index < 6U; Index++)
    {
        bool IsPackA = Sources[Index] == Fixture.PackA;
        IsEachAsExpected = IsEachAsExpected && (Registry_IsActive(&Things, Index) == !IsPackA)
            && (GetValue(&Things, Index) == (IsPackA ? UINT32_MAX : Index))
            && (strcmp((const char*)Registry_GetKeyText(&Things, Index), Keys[Index]) == 0);
    }
    TEST_CHECK(run, IsEachAsExpected);

    // Nothing left of the source: no change, no revision.
    Revision = Registry_GetRevision(&Things);
    TEST_CHECK(run, (Registry_WithdrawSource(&Things, Fixture.PackA) == 0U)
        && (Registry_GetRevision(&Things) == Revision));
    TEST_CHECK(run, Registry_WithdrawSource(NULL, Fixture.PackA) == 0U);

    // The engine's entries withdraw by the engine's source; the other pack's stays.
    TEST_CHECK(run, (Registry_WithdrawSource(&Things, NAMESPACED_KEY_ID_NONE) == 2U)
        && (Registry_GetActiveCount(&Things) == 1U) && Registry_IsActive(&Things, 2U));
    Registry_Deconstruct(&Things);
    KeyRegistry_Deconstruct(&Fixture.Keys);
}

static void ElementsAreDeconstructedOnce(TestRun* run)
{
    RegistryFixture Fixture;
    Registry Things;
    size_t Deconstructed = 0U;
    if (!ConstructFixture(run, &Fixture) || !ConstructRegistry(run, &Fixture, 16U, &Deconstructed, &Things))
    {
        KeyRegistry_Deconstruct(&Fixture.Keys);
        return;
    }
    char Text[REGISTRY_TEST_TEXT_CAPACITY];
    uint32_t KeyIds[5] = { 0U };
    for (uint32_t Index = 0U; Index < 5U; Index++)
    {
        snprintf(Text, sizeof(Text), "test:owner/%u", (unsigned int)Index);
        KeyIds[Index] = Intern(run, &Fixture.Keys, Text);
        TestEntry Entry = MakeEntry(Index, Text);
        TEST_CHECK_SUCCESS(run, Registry_Register(&Things, KeyIds[Index], &Entry, (Index < 2U) ? Fixture.PackA
            : NAMESPACED_KEY_ID_NONE, NULL));
    }

    // Withdrawal deconstructs at once: one entry by index, two by their source. The withdrawn element's bytes are
    // zeroed (its slot's storage stays the registry's), so a stale pointer to it reads no freed text.
    const TestEntry* Stale = Registry_Get(&Things, 4U);
    TEST_CHECK_SUCCESS(run, Registry_Withdraw(&Things, 4U));
    TEST_CHECK(run, (Deconstructed == 1U) && (Stale != NULL) && (Stale->Value == 0U) && (Stale->OwnedText == NULL));
    TEST_CHECK(run, (Registry_WithdrawSource(&Things, Fixture.PackA) == 2U) && (Deconstructed == 3U));

    // A revived entry owns its new element, which is deconstructed in its turn.
    TestEntry Revived = MakeEntry(40U, "revived");
    TEST_CHECK_SUCCESS(run, Registry_Register(&Things, KeyIds[4], &Revived, NAMESPACED_KEY_ID_NONE, NULL));
    TEST_CHECK(run, Deconstructed == 3U);

    // Teardown deconstructs the active ones (2, 3 and the revived 4), never a withdrawn one again: six elements were
    // registered, six deconstructions.
    Registry_Deconstruct(&Things);
    TEST_CHECK(run, Deconstructed == 6U);
    KeyRegistry_Deconstruct(&Fixture.Keys);
}

static void ElementsKeepTheirAddress(TestRun* run)
{
    RegistryFixture Fixture;
    Registry Things;
    if (!ConstructFixture(run, &Fixture) || !ConstructRegistry(run, &Fixture, REGISTRY_TEST_MANY_COUNT, NULL, &Things))
    {
        KeyRegistry_Deconstruct(&Fixture.Keys);
        return;
    }
    char Text[REGISTRY_TEST_TEXT_CAPACITY];
    const TestEntry* Watched[3] = { NULL, NULL, NULL };
    // The first entry, the last of the first block and the first of the next.
    const uint32_t WatchedIndices[3] = { 0U, 63U, 64U };
    bool AreAllRegistered = true;
    for (uint32_t Index = 0U; Index < REGISTRY_TEST_MANY_COUNT; Index++)
    {
        snprintf(Text, sizeof(Text), "pack%u:item/%u", (unsigned int)(Index % 7U), (unsigned int)Index);
        uint32_t KeyId = Intern(run, &Fixture.Keys, Text);
        TestEntry Entry = MakeEntry(Index * 3U, NULL);
        uint32_t Registered = REGISTRY_INDEX_NONE;
        Error Result = Registry_Register(&Things, KeyId, &Entry, NAMESPACED_KEY_ID_NONE, &Registered);
        AreAllRegistered = AreAllRegistered && (Result.Code == ErrorCode_Success) && (Registered == Index);
        Error_Deconstruct(&Result);
        for (size_t Slot = 0U; Slot < 3U; Slot++)
        {
            if (Index == WatchedIndices[Slot])
            {
                Watched[Slot] = Registry_Get(&Things, Index);
            }
        }
    }
    TEST_CHECK(run, AreAllRegistered && (Registry_GetCount(&Things) == REGISTRY_TEST_MANY_COUNT));
    for (size_t Slot = 0U; Slot < 3U; Slot++)
    {
        TEST_CHECK(run, (Watched[Slot] != NULL) && (Registry_Get(&Things, WatchedIndices[Slot]) == Watched[Slot])
            && (Watched[Slot]->Value == (WatchedIndices[Slot] * 3U)));
    }

    // Every key finds its own entry.
    bool AreAllFound = true;
    for (uint32_t Index = 0U; Index < REGISTRY_TEST_MANY_COUNT; Index++)
    {
        snprintf(Text, sizeof(Text), "pack%u:item/%u", (unsigned int)(Index % 7U), (unsigned int)Index);
        uint32_t Found = REGISTRY_INDEX_NONE;
        AreAllFound = AreAllFound && Registry_FindText(&Things, (const unsigned char*)Text, &Found) && (Found == Index)
            && (GetValue(&Things, Found) == (Index * 3U));
    }
    TEST_CHECK(run, AreAllFound);
    Registry_Deconstruct(&Things);
    KeyRegistry_Deconstruct(&Fixture.Keys);
}

static void TheMaximumEntryCountHolds(TestRun* run)
{
    RegistryFixture Fixture;
    Registry Things;
    if (!ConstructFixture(run, &Fixture) || !ConstructRegistry(run, &Fixture, 3U, NULL, &Things))
    {
        KeyRegistry_Deconstruct(&Fixture.Keys);
        return;
    }
    RegisterValue(run, &Fixture, &Things, "test:a", 1U, NAMESPACED_KEY_ID_NONE);
    uint32_t B = RegisterValue(run, &Fixture, &Things, "test:b", 2U, NAMESPACED_KEY_ID_NONE);
    RegisterValue(run, &Fixture, &Things, "test:c", 3U, NAMESPACED_KEY_ID_NONE);

    // A fourth key is refused and changes nothing.
    uint32_t D = Intern(run, &Fixture.Keys, "test:d");
    TestEntry Entry = MakeEntry(4U, NULL);
    uint64_t Revision = Registry_GetRevision(&Things);
    const char* const Fragments[] = { "test:things is full", "test:d", NULL };
    TestText_CheckErrorMessage(run, Registry_Register(&Things, D, &Entry, NAMESPACED_KEY_ID_NONE, NULL),
        ErrorCode_InvalidOperation, Fragments);
    TEST_CHECK(run, (Registry_GetCount(&Things) == 3U) && (Registry_GetRevision(&Things) == Revision));

    // Slots are never removed, so a withdrawn entry still counts; reviving it takes no new slot and is allowed.
    TEST_CHECK_SUCCESS(run, Registry_Withdraw(&Things, B));
    TEST_CHECK_ERROR(run, Registry_Register(&Things, D, &Entry, NAMESPACED_KEY_ID_NONE, NULL),
        ErrorCode_InvalidOperation);
    uint32_t Revived = REGISTRY_INDEX_NONE;
    TEST_CHECK_SUCCESS(run, Registry_Register(&Things, Registry_GetKey(&Things, B), &Entry, NAMESPACED_KEY_ID_NONE,
        &Revived));
    TEST_CHECK(run, (Revived == B) && (Registry_GetCount(&Things) == 3U) && (GetValue(&Things, B) == 4U));
    Registry_Deconstruct(&Things);
    KeyRegistry_Deconstruct(&Fixture.Keys);
}

static void BadArgumentsAreRefused(TestRun* run)
{
    RegistryFixture Fixture;
    if (!ConstructFixture(run, &Fixture))
    {
        KeyRegistry_Deconstruct(&Fixture.Keys);
        return;
    }

    // Construction: NULLs, a key id the key registry does not hold, element sizes and maxima out of range.
    Registry Things;
    RegistryConstructOptions Options = RegistryConstructOptions_CreateDefault(&Fixture.Keys, Fixture.RegistryKey,
        sizeof(TestEntry), 8U);
    TEST_CHECK_ERROR(run, Registry_Construct(NULL, &Options), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, Registry_Construct(&Things, NULL), ErrorCode_IllegalArgument);
    RegistryConstructOptions Bad = Options;
    Bad.Keys = NULL;
    TEST_CHECK_ERROR(run, Registry_Construct(&Things, &Bad), ErrorCode_IllegalArgument);
    Bad = Options;
    Bad.KeyId = NAMESPACED_KEY_ID_NONE;
    TEST_CHECK_ERROR(run, Registry_Construct(&Things, &Bad), ErrorCode_ArgumentOutOfRange);
    Bad.KeyId = (uint32_t)KeyRegistry_GetCount(&Fixture.Keys) + 1U;
    TEST_CHECK_ERROR(run, Registry_Construct(&Things, &Bad), ErrorCode_ArgumentOutOfRange);
    Bad = Options;
    Bad.ElementSize = 0U;
    TEST_CHECK_ERROR(run, Registry_Construct(&Things, &Bad), ErrorCode_ArgumentOutOfRange);
    Bad.ElementSize = SIZE_MAX; // a block would overflow
    TEST_CHECK_ERROR(run, Registry_Construct(&Things, &Bad), ErrorCode_ArgumentOutOfRange);
    Bad = Options;
    Bad.MaxEntryCount = 0U;
    TEST_CHECK_ERROR(run, Registry_Construct(&Things, &Bad), ErrorCode_ArgumentOutOfRange);
    Bad.MaxEntryCount = REGISTRY_MAX_ENTRY_COUNT + 1U;
    TEST_CHECK_ERROR(run, Registry_Construct(&Things, &Bad), ErrorCode_ArgumentOutOfRange);
    if (!TEST_CHECK_SUCCESS(run, Registry_Construct(&Things, &Options)))
    {
        KeyRegistry_Deconstruct(&Fixture.Keys);
        return;
    }

    // Registration: NULLs, key and source ids the key registry does not hold; nothing changes.
    TestEntry Entry = MakeEntry(1U, NULL);
    uint32_t Past = (uint32_t)KeyRegistry_GetCount(&Fixture.Keys) + 1U;
    TEST_CHECK_ERROR(run, Registry_Register(NULL, Fixture.PackA, &Entry, NAMESPACED_KEY_ID_NONE, NULL),
        ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, Registry_Register(&Things, Fixture.PackA, NULL, NAMESPACED_KEY_ID_NONE, NULL),
        ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, Registry_Register(&Things, NAMESPACED_KEY_ID_NONE, &Entry, NAMESPACED_KEY_ID_NONE, NULL),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, Registry_Register(&Things, Past, &Entry, NAMESPACED_KEY_ID_NONE, NULL),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, Registry_Register(&Things, Fixture.PackA, &Entry, Past, NULL), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, (Registry_GetCount(&Things) == 0U) && (Registry_GetRevision(&Things) == 0U));

    // Reads with NULLs find and get nothing.
    uint32_t Found = REGISTRY_INDEX_NONE;
    TEST_CHECK(run, !Registry_Find(NULL, Fixture.PackA, &Found) && !Registry_Find(&Things, Fixture.PackA, NULL));
    TEST_CHECK(run, !Registry_FindText(NULL, (const unsigned char*)u8"pack_a:pack", &Found)
        && !Registry_FindText(&Things, NULL, &Found)
        && !Registry_FindText(&Things, (const unsigned char*)u8"pack_a:pack", NULL));
    TEST_CHECK(run, (Registry_Get(NULL, 0U) == NULL) && !Registry_IsActive(NULL, 0U)
        && (Registry_GetKey(NULL, 0U) == NAMESPACED_KEY_ID_NONE) && (Registry_GetKeyText(NULL, 0U) == NULL));
    Registry_Deconstruct(&Things);
    Registry_Deconstruct(NULL);
    KeyRegistry_Deconstruct(&Fixture.Keys);
}

static void TheRevisionAdvancesOnChangesOnly(TestRun* run)
{
    RegistryFixture Fixture;
    Registry Things;
    if (!ConstructFixture(run, &Fixture) || !ConstructRegistry(run, &Fixture, 16U, NULL, &Things))
    {
        KeyRegistry_Deconstruct(&Fixture.Keys);
        return;
    }
    TEST_CHECK(run, Registry_GetRevision(&Things) == 0U);
    uint32_t A = RegisterValue(run, &Fixture, &Things, "test:a", 1U, Fixture.PackA);
    RegisterValue(run, &Fixture, &Things, "test:b", 2U, Fixture.PackA);
    TEST_CHECK(run, Registry_GetRevision(&Things) == 2U);

    // Reads leave it.
    uint32_t Found = REGISTRY_INDEX_NONE;
    GenericBuffer Indices;
    GenericBuffer_AllocateVariable(&Indices, 0U, sizeof(uint32_t));
    TEST_CHECK(run, Registry_Find(&Things, Registry_GetKey(&Things, A), &Found)
        && Registry_FindText(&Things, (const unsigned char*)u8"test:b", &Found) && (Registry_Get(&Things, A) != NULL)
        && Registry_IsActive(&Things, A) && (Registry_GetKeyText(&Things, A) != NULL));
    TEST_CHECK_SUCCESS(run, Registry_FindByPrefix(&Things, (const unsigned char*)u8"test:", &Indices));
    TEST_CHECK(run, Registry_GetRevision(&Things) == 2U);
    Memory_Free(Indices._data);

    // A refused registration leaves it; a withdrawal, a source's withdrawal (per entry) and a revival advance it.
    TestEntry Entry = MakeEntry(3U, NULL);
    TEST_CHECK_ERROR(run, Registry_Register(&Things, Registry_GetKey(&Things, A), &Entry, NAMESPACED_KEY_ID_NONE, NULL),
        ErrorCode_InvalidOperation);
    TEST_CHECK(run, Registry_GetRevision(&Things) == 2U);
    TEST_CHECK_SUCCESS(run, Registry_Withdraw(&Things, A));
    TEST_CHECK(run, Registry_GetRevision(&Things) == 3U);
    TEST_CHECK(run, (Registry_WithdrawSource(&Things, Fixture.PackA) == 1U) && (Registry_GetRevision(&Things) == 4U));
    TEST_CHECK_SUCCESS(run, Registry_Register(&Things, Registry_GetKey(&Things, A), &Entry, NAMESPACED_KEY_ID_NONE,
        NULL));
    TEST_CHECK(run, Registry_GetRevision(&Things) == 5U);
    Registry_Deconstruct(&Things);
    KeyRegistry_Deconstruct(&Fixture.Keys);
}


// Static functions: the root.
/* A registry of uint32_t under @p key. */
static bool ConstructNamedRegistry(TestRun* run, KeyRegistry* keys, const char* key, Registry* outRegistry)
{
    RegistryConstructOptions Options = RegistryConstructOptions_CreateDefault(keys, Intern(run, keys, key),
        sizeof(uint32_t), 64U);
    return TEST_CHECK_SUCCESS(run, Registry_Construct(outRegistry, &Options));
}

static void TheRootFindsRegistriesByKey(TestRun* run)
{
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    RegistryRoot Root;
    Registry Actions;
    Registry Categories;
    Registry Impostor; // a second registry under the actions' key
    if (!TEST_CHECK_SUCCESS(run, RegistryRoot_Construct(&Root, &Keys))
        || !ConstructNamedRegistry(run, &Keys, "tbp:input_action", &Actions)
        || !ConstructNamedRegistry(run, &Keys, "tbp:input_category", &Categories)
        || !ConstructNamedRegistry(run, &Keys, "tbp:input_action", &Impostor))
    {
        KeyRegistry_Deconstruct(&Keys);
        return;
    }
    const Registry* RootRegistry = RegistryRoot_AsRegistry(&Root);
    TEST_CHECK(run, strcmp((const char*)KeyRegistry_GetText(&Keys, Registry_GetRegistryKey(RootRegistry)),
        "tbp:registry") == 0);

    // Registries are found by key id, by text and by index; the root does not list itself.
    TEST_CHECK_SUCCESS(run, RegistryRoot_Add(&Root, &Actions));
    TEST_CHECK_SUCCESS(run, RegistryRoot_Add(&Root, &Categories));
    TEST_CHECK(run, (RegistryRoot_GetCount(&Root) == 2U) && (RegistryRoot_Get(&Root, 0U) == &Actions)
        && (RegistryRoot_Get(&Root, 1U) == &Categories) && (RegistryRoot_Get(&Root, 2U) == NULL));
    TEST_CHECK(run, (RegistryRoot_Find(&Root, Registry_GetRegistryKey(&Categories)) == &Categories)
        && (RegistryRoot_FindText(&Root, (const unsigned char*)u8"tbp:input_action") == &Actions)
        && (RegistryRoot_FindText(&Root, (const unsigned char*)u8"tbp:registry") == NULL)
        && (RegistryRoot_FindText(&Root, (const unsigned char*)u8"tbp:nothing") == NULL));

    // Prefix search over the registries' keys (autocomplete).
    GenericBuffer Found;
    GenericBuffer_AllocateVariable(&Found, 0U, sizeof(uint32_t));
    TEST_CHECK_SUCCESS(run, Registry_FindByPrefix(RootRegistry, (const unsigned char*)u8"tbp:input_", &Found));
    TEST_CHECK(run, (Found._count == 2U) && (RegistryRoot_Get(&Root, ((const uint32_t*)Found._data)[0]) == &Actions));
    Memory_Free(Found._data);

    // A second registry under a taken key, or one on another key registry, is refused.
    TEST_CHECK_ERROR(run, RegistryRoot_Add(&Root, &Impostor), ErrorCode_InvalidOperation);
    TEST_CHECK_ERROR(run, RegistryRoot_Withdraw(&Root, &Impostor), ErrorCode_InvalidOperation);
    KeyRegistry OtherKeys;
    Registry Stranger;
    if (TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&OtherKeys))
        && ConstructNamedRegistry(run, &OtherKeys, "tbp:stranger", &Stranger))
    {
        TEST_CHECK_ERROR(run, RegistryRoot_Add(&Root, &Stranger), ErrorCode_IllegalArgument);
        Registry_Deconstruct(&Stranger);
    }
    KeyRegistry_Deconstruct(&OtherKeys);

    // Withdrawn: no longer found; withdrawing it again fails; a new registry under its key takes its old index.
    TEST_CHECK_SUCCESS(run, RegistryRoot_Withdraw(&Root, &Actions));
    TEST_CHECK(run, (RegistryRoot_Find(&Root, Registry_GetRegistryKey(&Actions)) == NULL)
        && (RegistryRoot_Get(&Root, 0U) == NULL) && (RegistryRoot_GetCount(&Root) == 2U));
    TEST_CHECK_ERROR(run, RegistryRoot_Withdraw(&Root, &Actions), ErrorCode_InvalidOperation);
    TEST_CHECK_SUCCESS(run, RegistryRoot_Add(&Root, &Impostor));
    TEST_CHECK(run, (RegistryRoot_Get(&Root, 0U) == &Impostor)
        && (RegistryRoot_FindText(&Root, (const unsigned char*)u8"tbp:input_action") == &Impostor));

    // NULL arguments.
    TEST_CHECK_ERROR(run, RegistryRoot_Construct(NULL, &Keys), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, RegistryRoot_Add(&Root, NULL), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, RegistryRoot_Withdraw(NULL, &Categories), ErrorCode_IllegalArgument);
    TEST_CHECK(run, (RegistryRoot_Find(NULL, Registry_GetRegistryKey(&Categories)) == NULL)
        && (RegistryRoot_Get(NULL, 0U) == NULL) && (RegistryRoot_FindText(&Root, NULL) == NULL));

    TEST_CHECK_SUCCESS(run, RegistryRoot_Withdraw(&Root, &Impostor));
    TEST_CHECK_SUCCESS(run, RegistryRoot_Withdraw(&Root, &Categories));
    Registry_Deconstruct(&Impostor);
    Registry_Deconstruct(&Categories);
    Registry_Deconstruct(&Actions);
    RegistryRoot_Deconstruct(&Root);
    RegistryRoot_Deconstruct(NULL);
    KeyRegistry_Deconstruct(&Keys);
}


// Public functions.
void RegistryTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "registering_finds_and_gets_entries", RegisteringFindsAndGetsEntries },
        { "prefix_search_lists_active_entries_in_order", PrefixSearchListsActiveEntriesInOrder },
        { "an_active_duplicate_is_refused", AnActiveDuplicateIsRefused },
        { "a_withdrawn_key_revives_its_index", AWithdrawnKeyRevivesItsIndex },
        { "withdrawing_a_source_leaves_the_others", WithdrawingASourceLeavesTheOthers },
        { "elements_are_deconstructed_once", ElementsAreDeconstructedOnce },
        { "elements_keep_their_address", ElementsKeepTheirAddress },
        { "the_maximum_entry_count_holds", TheMaximumEntryCountHolds },
        { "bad_arguments_are_refused", BadArgumentsAreRefused },
        { "the_revision_advances_on_changes_only", TheRevisionAdvancesOnChangesOnly },
        { "the_root_finds_registries_by_key", TheRootFindsRegistriesByKey },
    };
    TestRun_RunSuite(run, "Registry", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
