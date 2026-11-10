#include "TestSuites.h"
#include "NamespacedKey.h"
#include "UILayouts.h"
#include "UIWindowTypes.h"
#include "wr/WRFileSystem.h"
#include "wr/WRMemory.h"
#include "wr/WRPath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef __linux__
#include <sys/stat.h>
#endif


// Macros.
/** Room for paths and documents. */
#define PATH_CAPACITY ((size_t)512)
/** How many mutations of a valid document the hostile files' case reads. */
#define MUTATION_COUNT 4000U


// Types.
/* Keys, the window types a layout names (A, B and C open once, Notes many times) and the layouts. */
typedef struct LayoutsFixtureStruct
{
    KeyRegistry Keys;
    UIWindowTypes Types;
    UILayouts Layouts;
    uint32_t KeyIds[4];
    bool IsReady;
} LayoutsFixture;


// Static functions: the fixture.
static Error FillNothing(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)screen;
    (void)windowId;
    (void)contentId;
    (void)userData;
    return Error_CreateSuccess();
}

static bool Construct(TestRun* run, LayoutsFixture* fixture, const unsigned char* path)
{
    Memory_Zero(fixture, sizeof(*fixture));
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&fixture->Keys)))
    {
        return false;
    }
    if (!TEST_CHECK_SUCCESS(run, UIWindowTypes_Construct(&fixture->Types, &fixture->Keys, NULL)))
    {
        KeyRegistry_Deconstruct(&fixture->Keys);
        return false;
    }
    static const char* const Keys[4] = { "tbp:test_a", "tbp:test_b", "tbp:test_c", "tbp:test_notes" };
    for (size_t Type = 0U; Type < 4U; Type++)
    {
        UIWindowTypeDefinition Definition = UIWindowTypeDefinition_Create(u8"Type", FillNothing);
        Definition.IsSingle = Type < 3U;
        uint32_t Index = 0U;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Register(&fixture->Types, (const unsigned char*)Keys[Type], &Definition, NAMESPACED_KEY_ID_NONE, &Index));
        fixture->KeyIds[Type] = UIWindowTypes_GetKeyId(&fixture->Types, Index);
    }
    fixture->IsReady = TEST_CHECK_SUCCESS(run, UILayouts_Construct(&fixture->Layouts, (path != NULL) ? path : u8"unused.json", &fixture->Types,
        &fixture->Keys, NULL));
    return fixture->IsReady;
}

static void Deconstruct(LayoutsFixture* fixture)
{
    if (fixture->IsReady)
    {
        UILayouts_Deconstruct(&fixture->Layouts);
    }
    UIWindowTypes_Deconstruct(&fixture->Types);
    KeyRegistry_Deconstruct(&fixture->Keys);
}

static Rectangle Box(float x, float y, float width, float height)
{
    return (Rectangle){ .x = x, .y = y, .width = width, .height = height };
}

/* A layout of every kind of thing: A and B tabs of one area (B selected), C down beside them, Notes floating minimized, another Notes
 * floating maximized, the places of A and C. */
static void BuildRichLayout(TestRun* run, const LayoutsFixture* fixture, const unsigned char* name, UILayout* outLayout)
{
    UILayout_Construct(outLayout);
    TEST_CHECK_SUCCESS(run, UILayout_SetName(outLayout, name));
    TEST_CHECK_SUCCESS(run, UILayout_AddWindow(outLayout, fixture->KeyIds[0], Box(10.0f, 20.0f, 300.0f, 200.0f), false, false, NULL));
    TEST_CHECK_SUCCESS(run, UILayout_AddWindow(outLayout, fixture->KeyIds[1], Box(-40.5f, 0.25f, 250.0f, 180.0f), false, false, NULL));
    TEST_CHECK_SUCCESS(run, UILayout_AddWindow(outLayout, fixture->KeyIds[2], Box(100.0f, 100.0f, 400.0f, 300.0f), false, false, NULL));
    TEST_CHECK_SUCCESS(run, UILayout_AddWindow(outLayout, fixture->KeyIds[3], Box(600.0f, 50.0f, 200.0f, 120.0f), true, false, NULL));
    TEST_CHECK_SUCCESS(run, UILayout_AddWindow(outLayout, fixture->KeyIds[3], Box(1e6f, -1e6f, 1e3f, 2e3f), false, true, NULL));
    int32_t Root = UI_DOCK_NODE_NONE;
    int32_t Top = UI_DOCK_NODE_NONE;
    int32_t Bottom = UI_DOCK_NODE_NONE;
    TEST_CHECK_SUCCESS(run, UIDockTree_BuildNode(&outLayout->Tree, UI_DOCK_NODE_NONE, 0, UIDockNodeKind_Split, UIDockSplit_Down, 0.3125f, &Root));
    TEST_CHECK_SUCCESS(run, UIDockTree_BuildNode(&outLayout->Tree, Root, 0, UIDockNodeKind_Leaf, UIDockSplit_Across, 0.0f, &Top));
    TEST_CHECK_SUCCESS(run, UIDockTree_BuildNode(&outLayout->Tree, Root, 1, UIDockNodeKind_Leaf, UIDockSplit_Across, 0.0f, &Bottom));
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&outLayout->Tree, Top, 1U, 9U));
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&outLayout->Tree, Top, 2U, 9U));
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&outLayout->Tree, Bottom, 3U, 9U));
    TEST_CHECK_SUCCESS(run, UILayout_SetPlace(outLayout, fixture->KeyIds[0], Box(5.0f, 6.0f, 70.0f, 80.0f)));
    TEST_CHECK_SUCCESS(run, UILayout_SetPlace(outLayout, fixture->KeyIds[2], Box(15.0f, 16.0f, 170.0f, 180.0f)));
    TEST_CHECK(run, UILayout_IsValid(outLayout));
}

static Error ReadText(LayoutsFixture* fixture, const char* text)
{
    return UILayouts_ReadJSON(&fixture->Layouts, (const unsigned char*)text, strlen(text));
}

/* A document around one layout's text. */
static Error ReadOneLayout(LayoutsFixture* fixture, const char* layout)
{
    char Text[8192];
    snprintf(Text, sizeof(Text), "{\"version\": 1, \"layouts\": [%s]}", layout);
    return ReadText(fixture, Text);
}

static uint64_t NextRandom(uint64_t* state)
{
    *state = (*state * 6364136223846793005ULL) + 1442695040888963407ULL;
    return *state >> 33U;
}


// Static functions: test cases.
static void NamesAreCheckedAndMadeUp(TestRun* run)
{
    LayoutsFixture Fixture;
    if (Construct(run, &Fixture, NULL))
    {
        TEST_CHECK(run, UILayout_IsNameValid(u8"Terrain") && UILayout_IsNameValid(u8"Ēdītor ✓"));
        TEST_CHECK(run, !UILayout_IsNameValid(NULL) && !UILayout_IsNameValid(u8"") && !UILayout_IsNameValid(u8"a\tb"));
        TEST_CHECK(run, !UILayout_IsNameValid((const unsigned char*)"\xff\xfe"));
        TEST_CHECK(run, UILayout_IsNameValid(u8"0123456789012345678901234567890123456789012345678901234567890123"));
        TEST_CHECK(run, !UILayout_IsNameValid(u8"01234567890123456789012345678901234567890123456789012345678901234"));
        UILayout* Layout = Memory_Allocate(sizeof(UILayout));
        UILayout_Construct(Layout);
        TEST_CHECK(run, !UILayout_IsValid(Layout)); // No name yet.
        TEST_CHECK_ERROR(run, UILayout_SetName(Layout, u8""), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UILayout_SetName(Layout, u8"Layout"));
        TEST_CHECK_SUCCESS(run, UILayouts_Add(&Fixture.Layouts, Layout, NULL));
        TEST_CHECK_ERROR(run, UILayouts_Add(&Fixture.Layouts, Layout, NULL), ErrorCode_ArgumentOutOfRange);
        unsigned char Name[UI_LAYOUT_NAME_CAPACITY];
        UILayouts_MakeName(&Fixture.Layouts, u8"Layout", Name);
        TEST_CHECK(run, strcmp((const char*)Name, "Layout 2") == 0);
        TEST_CHECK_SUCCESS(run, UILayout_SetName(Layout, Name));
        size_t Index = 0U;
        TEST_CHECK_SUCCESS(run, UILayouts_Add(&Fixture.Layouts, Layout, &Index));
        TEST_CHECK(run, Index == 1U);
        UILayouts_MakeName(&Fixture.Layouts, u8"Layout", Name);
        TEST_CHECK(run, strcmp((const char*)Name, "Layout 3") == 0);
        // A base too long for a number keeps its start.
        UILayouts_MakeName(&Fixture.Layouts, u8"0123456789012345678901234567890123456789012345678901234567890123", Name);
        TEST_CHECK(run, UILayout_IsNameValid(Name));
        TEST_CHECK_ERROR(run, UILayouts_Rename(&Fixture.Layouts, 1U, u8"Layout"), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UILayouts_Rename(&Fixture.Layouts, 1U, u8"Layout 2")); // Its own name.
        TEST_CHECK_SUCCESS(run, UILayouts_Rename(&Fixture.Layouts, 1U, u8"Paths"));
        TEST_CHECK(run, UILayouts_FindByName(&Fixture.Layouts, u8"Paths", &Index) && (Index == 1U));
        TEST_CHECK_ERROR(run, UILayouts_Rename(&Fixture.Layouts, 5U, u8"Other"), ErrorCode_ArgumentOutOfRange);
        // The active index follows its layout.
        TEST_CHECK_SUCCESS(run, UILayouts_SetActiveIndex(&Fixture.Layouts, 1U));
        TEST_CHECK_SUCCESS(run, UILayouts_Remove(&Fixture.Layouts, 0U));
        TEST_CHECK(run, (UILayouts_GetCount(&Fixture.Layouts) == 1U) && (UILayouts_GetActiveIndex(&Fixture.Layouts) == 0U));
        TEST_CHECK_ERROR(run, UILayouts_SetActiveIndex(&Fixture.Layouts, 3U), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UILayouts_Remove(&Fixture.Layouts, 3U), ErrorCode_ArgumentOutOfRange);
        // As many as there may be.
        for (size_t Count = 1U; Count < UI_LAYOUTS_MAX_COUNT; Count++)
        {
            UILayouts_MakeName(&Fixture.Layouts, u8"Layout", Name);
            TEST_CHECK_SUCCESS(run, UILayout_SetName(Layout, Name));
            TEST_CHECK_SUCCESS(run, UILayouts_Add(&Fixture.Layouts, Layout, NULL));
        }
        UILayouts_MakeName(&Fixture.Layouts, u8"Layout", Name);
        TEST_CHECK_SUCCESS(run, UILayout_SetName(Layout, Name));
        TEST_CHECK_ERROR(run, UILayouts_Add(&Fixture.Layouts, Layout, NULL), ErrorCode_InvalidOperation);
        Memory_Free(Layout);
    }
    Deconstruct(&Fixture);
}

static void ALayoutIsCheckedWhole(TestRun* run)
{
    LayoutsFixture Fixture;
    if (Construct(run, &Fixture, NULL))
    {
        UILayout* Layout = Memory_Allocate(sizeof(UILayout));
        UILayout* Broken = Memory_Allocate(sizeof(UILayout));
        BuildRichLayout(run, &Fixture, u8"Rich", Layout);
        TEST_CHECK_ERROR(run, UILayout_AddWindow(Layout, Fixture.KeyIds[0], Box(0.0f, 0.0f, 0.0f, 10.0f), false, false, NULL), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UILayout_AddWindow(Layout, Fixture.KeyIds[0], Box(NAN, 0.0f, 10.0f, 10.0f), false, false, NULL), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UILayout_AddWindow(Layout, Fixture.KeyIds[0], Box(2e9f, 0.0f, 10.0f, 10.0f), false, false, NULL), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UILayout_SetPlace(Layout, Fixture.KeyIds[0], Box(0.0f, 0.0f, -1.0f, 10.0f)), ErrorCode_ArgumentOutOfRange);
        // One place per type: a second replaces the first.
        TEST_CHECK_SUCCESS(run, UILayout_SetPlace(Layout, Fixture.KeyIds[0], Box(1.0f, 2.0f, 3.0f, 4.0f)));
        TEST_CHECK(run, (Layout->PlaceCount == 2U) && (Layout->Places[0].Box.width == 3.0f));
        TEST_CHECK(run, UILayout_IsWindowDocked(Layout, 1U) && !UILayout_IsWindowDocked(Layout, 3U) && !UILayout_IsWindowDocked(Layout, 99U));
        *Broken = *Layout;
        Broken->Windows[0].IsMinimized = true; // Docked and minimized.
        TEST_CHECK(run, !UILayout_IsValid(Broken));
        *Broken = *Layout;
        Broken->Windows[0].IsMaximized = true; // Two maximized.
        TEST_CHECK(run, !UILayout_IsValid(Broken));
        *Broken = *Layout;
        Broken->WindowCount = 2U; // A tab names a window it does not have.
        TEST_CHECK(run, !UILayout_IsValid(Broken));
        *Broken = *Layout;
        TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Broken->Tree, 3U)); // An empty area.
        TEST_CHECK(run, !UILayout_IsValid(Broken));
        *Broken = *Layout;
        Broken->Places[1].TypeKeyId = Broken->Places[0].TypeKeyId;
        TEST_CHECK(run, !UILayout_IsValid(Broken));
        // Equal by meaning: node indices may differ, a place's order too.
        *Broken = *Layout;
        UILayoutPlace First = Broken->Places[0];
        Broken->Places[0] = Broken->Places[1];
        Broken->Places[1] = First;
        TEST_CHECK(run, UILayout_Equals(Layout, Broken));
        Broken->Windows[1].Box.x += 1.0f;
        TEST_CHECK(run, !UILayout_Equals(Layout, Broken));
        *Broken = *Layout;
        TEST_CHECK_SUCCESS(run, UIDockTree_SelectTab(&Broken->Tree, 1U));
        TEST_CHECK(run, !UILayout_Equals(Layout, Broken));
        Memory_Free(Broken);
        Memory_Free(Layout);
    }
    Deconstruct(&Fixture);
}

static void LayoutsRoundTripThroughTheirDocument(TestRun* run)
{
    LayoutsFixture Fixture;
    LayoutsFixture Other;
    if (Construct(run, &Fixture, NULL) && Construct(run, &Other, NULL))
    {
        UILayout* Layout = Memory_Allocate(sizeof(UILayout));
        BuildRichLayout(run, &Fixture, u8"Rich", Layout);
        TEST_CHECK_SUCCESS(run, UILayouts_Add(&Fixture.Layouts, Layout, NULL));
        BuildRichLayout(run, &Fixture, u8"Ēmpty \"quoted\"", Layout);
        UIDockTree_Construct(&Layout->Tree);
        Layout->PlaceCount = 0U;
        Layout->Windows[0].IsMinimized = true;
        TEST_CHECK_SUCCESS(run, UILayouts_Add(&Fixture.Layouts, Layout, NULL));
        TEST_CHECK_SUCCESS(run, UILayouts_SetActiveIndex(&Fixture.Layouts, 1U));
        GenericBuffer Document;
        GenericBuffer_AllocateVariable(&Document, 1024U, sizeof(unsigned char));
        TEST_CHECK_SUCCESS(run, UILayouts_WriteJSON(&Fixture.Layouts, &Document));
        TEST_CHECK_SUCCESS(run, UILayouts_ReadJSON(&Other.Layouts, Document._data, Document._count));
        TEST_CHECK(run, UILayouts_GetCount(&Other.Layouts) == 2U);
        TEST_CHECK(run, UILayouts_GetActiveIndex(&Other.Layouts) == 1U);
        for (size_t Index = 0U; Index < 2U; Index++)
        {
            const UILayout* Written = UILayouts_Get(&Fixture.Layouts, Index);
            const UILayout* Read = UILayouts_Get(&Other.Layouts, Index);
            TEST_CHECK(run, (Written != NULL) && (Read != NULL) && UILayout_Equals(Written, Read));
        }
        // What it writes reads back the same again: a stable document.
        GenericBuffer Again;
        GenericBuffer_AllocateVariable(&Again, 1024U, sizeof(unsigned char));
        TEST_CHECK_SUCCESS(run, UILayouts_WriteJSON(&Other.Layouts, &Again));
        TEST_CHECK(run, (Again._count == Document._count) && Memory_IsEqual(Again._data, Document._data, Document._count));
        Memory_Free(Again._data);
        Memory_Free(Document._data);
        Memory_Free(Layout);
    }
    Deconstruct(&Other);
    Deconstruct(&Fixture);
}

static void UnknownTypesAndSecondSinglesAreDropped(TestRun* run)
{
    LayoutsFixture Fixture;
    if (Construct(run, &Fixture, NULL))
    {
        // An unknown type tabbed with A, another alone in an area, a second A floating, a place of an unknown type, a key it does not know.
        TEST_CHECK_SUCCESS(run, ReadOneLayout(&Fixture, "{\"name\": \"L\", \"extra\": 5, \"windows\": ["
            "{\"type\": \"tbp:test_a\", \"box\": [0, 0, 100, 100]},"
            "{\"type\": \"mymod:gone\", \"box\": [0, 0, 100, 100]},"
            "{\"type\": \"mymod:also_gone\", \"box\": [0, 0, 100, 100]},"
            "{\"type\": \"test_a\", \"box\": [5, 5, 100, 100]},"
            "{\"type\": \"tbp:test_notes\", \"box\": [0, 0, 100, 100]},"
            "{\"type\": \"tbp:test_notes\", \"box\": [9, 9, 100, 100]},"
            "{\"type\": \"not a key at all!\", \"box\": [0, 0, 100, 100]}],"
            "\"dock\": {\"split\": \"across\", \"ratio\": 0.5, \"first\": {\"tabs\": [0, 1], \"selected\": 1},"
            "\"second\": {\"tabs\": [2]}},"
            "\"places\": [{\"type\": \"mymod:gone\", \"box\": [1, 1, 10, 10]}, {\"type\": \"tbp:test_b\", \"box\": [2, 2, 20, 20]}]}"));
        const UILayout* Layout = UILayouts_Get(&Fixture.Layouts, 0U);
        TEST_CHECK(run, Layout != NULL);
        if (Layout != NULL)
        {
            TEST_CHECK(run, Layout->WindowCount == 3U); // A, then the two notes: several of a type that opens many times stay.
            TEST_CHECK(run, (Layout->Windows[0].TypeKeyId == Fixture.KeyIds[0]) && (Layout->Windows[1].TypeKeyId == Fixture.KeyIds[3]));
            TEST_CHECK(run, (Layout->Windows[2].Box.x == 9.0f));
            // The area of the unknown one alone went; A's area is the whole dock, A selected (its selected tab went).
            TEST_CHECK(run, UIDockTree_GetLeafCount(&Layout->Tree) == 1U);
            TEST_CHECK(run, UIDockTree_GetSelected(&Layout->Tree, UIDockTree_GetRoot(&Layout->Tree)) == 1U);
            TEST_CHECK(run, (Layout->PlaceCount == 1U) && (Layout->Places[0].TypeKeyId == Fixture.KeyIds[1]));
            TEST_CHECK(run, UILayout_IsValid(Layout));
        }
    }
    Deconstruct(&Fixture);
}

static void BadDocumentsChangeNothing(TestRun* run)
{
    static const char* const Documents[] =
    {
        "",
        "not json",
        "[]",
        "{\"layouts\": []}",
        "{\"version\": 2, \"layouts\": []}",
        "{\"version\": \"1\", \"layouts\": []}",
        "{\"version\": 1}",
        "{\"version\": 1, \"layouts\": {}}",
        "{\"version\": 1, \"layouts\": [], \"active\": 5}",
        "{\"version\": 1, \"layouts\": [5]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"A\"}, {\"name\": \"A\"}]}",
        "{\"version\": 1, \"layouts\": [{}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"\"}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"a\\tb\"}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": 7}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": {}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [5]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"box\": [0, 0, 1, 1]}]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": 5, \"box\": [0, 0, 1, 1]}]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\"}]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1]}]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 0, 1]}]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1e300, 1]}]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [2e9, 0, 1, 1]}]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [\"0\", 0, 1, 1]}]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1], \"minimized\": 1}]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1], \"maximized\": true},"
            "{\"type\": \"tbp:test_b\", \"box\": [0, 0, 1, 1], \"maximized\": true}]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1], \"minimized\": true}],"
            "\"dock\": {\"tabs\": [0]}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]}], \"dock\": {}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]}], \"dock\": {\"tabs\": []}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]}], \"dock\": {\"tabs\": [1]}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]}], \"dock\": {\"tabs\": [-1]}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]}], \"dock\": {\"tabs\": [0, 0]}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]}], \"dock\": {\"tabs\": [0.5]}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]}],"
            "\"dock\": {\"tabs\": [0], \"selected\": 3}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]},"
            "{\"type\": \"tbp:test_b\", \"box\": [0, 0, 1, 1]}], \"dock\": {\"split\": \"sideways\", \"ratio\": 0.5, \"first\": {\"tabs\": [0]},"
            "\"second\": {\"tabs\": [1]}}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]},"
            "{\"type\": \"tbp:test_b\", \"box\": [0, 0, 1, 1]}], \"dock\": {\"split\": \"across\", \"ratio\": 1.5, \"first\": {\"tabs\": [0]},"
            "\"second\": {\"tabs\": [1]}}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]},"
            "{\"type\": \"tbp:test_b\", \"box\": [0, 0, 1, 1]}], \"dock\": {\"split\": \"across\", \"first\": {\"tabs\": [0]},"
            "\"second\": {\"tabs\": [1]}}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]}],"
            "\"dock\": {\"split\": \"across\", \"ratio\": 0.5, \"first\": {\"tabs\": [0]}}}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"places\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, 1, 1]},"
            "{\"type\": \"tbp:test_a\", \"box\": [0, 0, 2, 2]}]}]}",
        "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"places\": [{\"type\": \"tbp:test_a\", \"box\": [0, 0, -1, 1]}]}]}",
    };
    LayoutsFixture Fixture;
    if (Construct(run, &Fixture, NULL))
    {
        UILayout* Kept = Memory_Allocate(sizeof(UILayout));
        BuildRichLayout(run, &Fixture, u8"Kept", Kept);
        TEST_CHECK_SUCCESS(run, UILayouts_Add(&Fixture.Layouts, Kept, NULL));
        for (size_t Index = 0U; Index < (sizeof(Documents) / sizeof(Documents[0])); Index++)
        {
            Error Result = ReadText(&Fixture, Documents[Index]);
            bool IsRefused = Result.Code == ErrorCode_InvalidJSON;
            Error_Deconstruct(&Result);
            if (!IsRefused)
            {
                printf("        not refused: %s\n", Documents[Index]);
            }
            TEST_CHECK(run, IsRefused);
            TEST_CHECK(run, (UILayouts_GetCount(&Fixture.Layouts) == 1U) && UILayout_Equals(UILayouts_Get(&Fixture.Layouts, 0U), Kept));
        }
        // More layouts than the layouts hold.
        char* Many = Memory_Allocate(4096U);
        size_t Length = (size_t)snprintf(Many, 4096U, "{\"version\": 1, \"layouts\": [");
        for (size_t Layout = 0U; Layout <= UI_LAYOUTS_MAX_COUNT; Layout++)
        {
            Length += (size_t)snprintf(Many + Length, 4096U - Length, "%s{\"name\": \"Layout %zu\"}", (Layout > 0U) ? "," : "", Layout + 1U);
        }
        snprintf(Many + Length, 4096U - Length, "]}");
        Error Result = ReadText(&Fixture, Many);
        TEST_CHECK(run, Result.Code == ErrorCode_InvalidJSON);
        Error_Deconstruct(&Result);
        TEST_CHECK(run, (UILayouts_GetCount(&Fixture.Layouts) == 1U) && UILayout_Equals(UILayouts_Get(&Fixture.Layouts, 0U), Kept));
        // One window shows maximized, another minimized from maximized keeps that it was: fine.
        TEST_CHECK_SUCCESS(run, ReadText(&Fixture, "{\"version\": 1, \"layouts\": [{\"name\": \"L\", \"windows\": ["
            "{\"type\": \"tbp:test_notes\", \"box\": [0, 0, 1, 1], \"maximized\": true, \"minimized\": true},"
            "{\"type\": \"tbp:test_notes\", \"box\": [0, 0, 1, 1], \"maximized\": true}]}]}"));
        TEST_CHECK(run, (UILayouts_GetCount(&Fixture.Layouts) == 1U) && (UILayouts_Get(&Fixture.Layouts, 0U)->WindowCount == 2U));
        // As many as they hold is fine.
        Length = (size_t)snprintf(Many, 4096U, "{\"version\": 1, \"layouts\": [");
        for (size_t Layout = 0U; Layout < UI_LAYOUTS_MAX_COUNT; Layout++)
        {
            Length += (size_t)snprintf(Many + Length, 4096U - Length, "%s{\"name\": \"Layout %zu\"}", (Layout > 0U) ? "," : "", Layout + 1U);
        }
        snprintf(Many + Length, 4096U - Length, "]}");
        TEST_CHECK_SUCCESS(run, ReadText(&Fixture, Many));
        TEST_CHECK(run, UILayouts_GetCount(&Fixture.Layouts) == UI_LAYOUTS_MAX_COUNT);
        Memory_Free(Many);
        Memory_Free(Kept);
    }
    Deconstruct(&Fixture);
}

static void DocksDeeperOrLargerThanATreeAreRefused(TestRun* run)
{
    LayoutsFixture Fixture;
    if (Construct(run, &Fixture, NULL))
    {
        // A chain of splits deeper than a tree goes, each with a window's area beside it.
        char* Text = Memory_Allocate(65536U);
        size_t Length = (size_t)snprintf(Text, 65536U, "{\"version\": 1, \"layouts\": [{\"name\": \"Deep\", \"windows\": [");
        for (int32_t Window = 0; Window < 20; Window++)
        {
            Length += (size_t)snprintf(Text + Length, 65536U - Length, "%s{\"type\": \"tbp:test_notes\", \"box\": [0, 0, 10, 10]}", (Window > 0) ? "," : "");
        }
        Length += (size_t)snprintf(Text + Length, 65536U - Length, "], \"dock\": ");
        for (int32_t Level = 0; Level < 18; Level++)
        {
            Length += (size_t)snprintf(Text + Length, 65536U - Length, "{\"split\": \"down\", \"ratio\": 0.5, \"first\": {\"tabs\": [%d]}, \"second\": ", (int)Level);
        }
        Length += (size_t)snprintf(Text + Length, 65536U - Length, "{\"tabs\": [19]}");
        for (int32_t Level = 0; Level < 18; Level++)
        {
            Length += (size_t)snprintf(Text + Length, 65536U - Length, "}");
        }
        snprintf(Text + Length, 65536U - Length, "}]}");
        TEST_CHECK_ERROR(run, ReadText(&Fixture, Text), ErrorCode_InvalidJSON);
        // At the deepest a tree goes, it reads.
        Length = (size_t)snprintf(Text, 65536U, "{\"version\": 1, \"layouts\": [{\"name\": \"Deep\", \"windows\": [");
        for (int32_t Window = 0; Window < 20; Window++)
        {
            Length += (size_t)snprintf(Text + Length, 65536U - Length, "%s{\"type\": \"tbp:test_notes\", \"box\": [0, 0, 10, 10]}", (Window > 0) ? "," : "");
        }
        Length += (size_t)snprintf(Text + Length, 65536U - Length, "], \"dock\": ");
        for (int32_t Level = 0; Level < UI_DOCK_TREE_MAX_DEPTH; Level++)
        {
            Length += (size_t)snprintf(Text + Length, 65536U - Length, "{\"split\": \"down\", \"ratio\": 0.5, \"first\": {\"tabs\": [%d]}, \"second\": ", (int)Level);
        }
        Length += (size_t)snprintf(Text + Length, 65536U - Length, "{\"tabs\": [19]}");
        for (int32_t Level = 0; Level < UI_DOCK_TREE_MAX_DEPTH; Level++)
        {
            Length += (size_t)snprintf(Text + Length, 65536U - Length, "}");
        }
        snprintf(Text + Length, 65536U - Length, "}]}");
        TEST_CHECK_SUCCESS(run, ReadText(&Fixture, Text));
        const UILayout* Layout = UILayouts_Get(&Fixture.Layouts, 0U);
        TEST_CHECK(run, (Layout != NULL) && (UIDockTree_GetLeafCount(&Layout->Tree) == (size_t)UI_DOCK_TREE_MAX_DEPTH + 1U));
        // More windows than a layout holds.
        Length = (size_t)snprintf(Text, 65536U, "{\"version\": 1, \"layouts\": [{\"name\": \"Many\", \"windows\": [");
        for (size_t Window = 0U; Window <= UI_LAYOUT_MAX_WINDOWS; Window++)
        {
            Length += (size_t)snprintf(Text + Length, 65536U - Length, "%s{\"type\": \"tbp:test_notes\", \"box\": [0, 0, 10, 10]}", (Window > 0U) ? "," : "");
        }
        snprintf(Text + Length, 65536U - Length, "]}]}");
        TEST_CHECK_ERROR(run, ReadText(&Fixture, Text), ErrorCode_InvalidJSON);
        // A nesting of plain arrays deeper than any reader allows.
        Length = (size_t)snprintf(Text, 65536U, "{\"version\": 1, \"layouts\": [{\"name\": \"N\", \"dock\": {\"tabs\": ");
        for (int32_t Level = 0; Level < 2000; Level++)
        {
            Text[Length++] = '[';
        }
        for (int32_t Level = 0; Level < 2000; Level++)
        {
            Text[Length++] = ']';
        }
        snprintf(Text + Length, 65536U - Length, "}}]}");
        TEST_CHECK_ERROR(run, ReadText(&Fixture, Text), ErrorCode_InvalidJSON);
        Memory_Free(Text);
    }
    Deconstruct(&Fixture);
}

/* Thousands of a valid document's mutations (bytes changed, dropped, doubled, cut, tokens put in): never a crash or a leak; each read
 * either refuses and changes nothing, or leaves only whole layouts within the limits. */
static void HostileDocumentsNeverBreakIt(TestRun* run)
{
    static const char* const Tokens[] = { "{", "}", "[", "]", ",", ":", "\"", "0", "-1", "1e308", "null", "true", "\"tbp:test_a\"", "\"split\"",
        "\"tabs\"", "\"selected\"", "\"ratio\"", "999999999999", "\"\\u0000\"", "\"down\"", "{\"tabs\": [0]}", "[0, 0, 1, 1]" };
    LayoutsFixture Fixture;
    LayoutsFixture Source;
    if (Construct(run, &Fixture, NULL) && Construct(run, &Source, NULL))
    {
        UILayout* Layout = Memory_Allocate(sizeof(UILayout));
        BuildRichLayout(run, &Source, u8"One", Layout);
        TEST_CHECK_SUCCESS(run, UILayouts_Add(&Source.Layouts, Layout, NULL));
        BuildRichLayout(run, &Source, u8"Two", Layout);
        TEST_CHECK_SUCCESS(run, UILayouts_Add(&Source.Layouts, Layout, NULL));
        GenericBuffer Valid;
        GenericBuffer_AllocateVariable(&Valid, 1024U, sizeof(unsigned char));
        TEST_CHECK_SUCCESS(run, UILayouts_WriteJSON(&Source.Layouts, &Valid));
        GenericBuffer Mutated;
        GenericBuffer_AllocateVariable(&Mutated, Valid._count * 2U, sizeof(unsigned char));
        uint64_t State = 2026U;
        size_t Refused = 0U;
        size_t Read = 0U;
        size_t Broken = 0U;
        for (uint32_t Round = 0U; Round < MUTATION_COUNT; Round++)
        {
            GenericBuffer_Clear(&Mutated);
            GenericBuffer_AddLastRange(&Mutated, Valid._data, Valid._count);
            uint64_t Changes = 1U + (NextRandom(&State) % 4U);
            for (uint64_t Change = 0U; (Change < Changes) && (Mutated._count > 0U); Change++)
            {
                size_t At = (size_t)(NextRandom(&State) % Mutated._count);
                unsigned char* Bytes = Mutated._data;
                switch (NextRandom(&State) % 5U)
                {
                    case 0U:
                        Bytes[At] = (unsigned char)(NextRandom(&State) & 0xFFU);
                        break;
                    case 1U:
                        GenericBuffer_RemoveAt(&Mutated, At);
                        break;
                    case 2U:
                    {
                        unsigned char Copy = Bytes[At];
                        GenericBuffer_Insert(&Mutated, &Copy, At);
                        break;
                    }
                    case 3U:
                        GenericBuffer_SetCount(&Mutated, At);
                        break;
                    default:
                    {
                        const char* Token = Tokens[NextRandom(&State) % (sizeof(Tokens) / sizeof(Tokens[0]))];
                        GenericBuffer_InsertRange(&Mutated, (void*)Token, strlen(Token), At);
                        break;
                    }
                }
            }
            size_t CountBefore = UILayouts_GetCount(&Fixture.Layouts);
            Error Result = UILayouts_ReadJSON(&Fixture.Layouts, Mutated._data, Mutated._count);
            if (Result.Code != ErrorCode_Success)
            {
                Refused++;
                Broken += ((Result.Code != ErrorCode_InvalidJSON) || (UILayouts_GetCount(&Fixture.Layouts) != CountBefore)) ? 1U : 0U;
                Error_Deconstruct(&Result);
                continue;
            }
            Read++;
            Broken += (UILayouts_GetCount(&Fixture.Layouts) > UI_LAYOUTS_MAX_COUNT) ? 1U : 0U;
            for (size_t Index = 0U; Index < UILayouts_GetCount(&Fixture.Layouts); Index++)
            {
                Broken += UILayout_IsValid(UILayouts_Get(&Fixture.Layouts, Index)) ? 0U : 1U;
            }
            Broken += (UILayouts_GetActiveIndex(&Fixture.Layouts) < UILayouts_GetCount(&Fixture.Layouts))
                || (UILayouts_GetCount(&Fixture.Layouts) == 0U) ? 0U : 1U;
        }
        TEST_CHECK(run, Broken == 0U);
        TEST_CHECK(run, (Refused > 0U) && (Read > 0U)); // Both ways were walked.
        Memory_Free(Mutated._data);
        Memory_Free(Valid._data);
        Memory_Free(Layout);
    }
    Deconstruct(&Source);
    Deconstruct(&Fixture);
}

/* Whether a file is there. */
static bool IsFileThere(const unsigned char* path)
{
    FileSystemEntryInfo Info;
    Error Result = FileSystem_GetEntryInfo(path, &Info);
    bool IsThere = Result.Code == ErrorCode_Success;
    if (IsThere)
    {
        FileSystemEntryInfo_Deconstruct(&Info);
    }
    Error_Deconstruct(&Result);
    return IsThere;
}

/* Takes away a file an earlier run left, so a case starts from an empty folder. */
static void RemoveIfThere(TestRun* run, const unsigned char* path)
{
    FileSystemEntryInfo Info;
    Error Result = FileSystem_GetEntryInfo(path, &Info);
    if (Result.Code == ErrorCode_Success)
    {
        FileSystemEntryInfo_Deconstruct(&Info);
        TEST_CHECK_SUCCESS(run, FileSystem_DeleteEntry(path));
    }
    Error_Deconstruct(&Result);
}

static void TheFileIsReadWrittenAndPutAsideWhenBad(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, PATH_CAPACITY, sizeof(unsigned char));
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, PATH_CAPACITY, sizeof(unsigned char));
    GenericBuffer BadPath;
    GenericBuffer_AllocateVariable(&BadPath, PATH_CAPACITY, sizeof(unsigned char));
    LayoutsFixture Fixture;
    LayoutsFixture Other;
    bool HasPaths = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "layouts_files", &Directory))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, u8"editor_layouts.json", &Path))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, u8"editor_layouts.json.bad", &BadPath));
    if (HasPaths)
    {
        RemoveIfThere(run, Path._data);
        RemoveIfThere(run, BadPath._data);
    }
    if (HasPaths && Construct(run, &Fixture, Path._data) && Construct(run, &Other, Path._data))
    {
        // No file: nothing changes.
        UILayoutsLoadResult Loaded = UILayoutsLoadResult_Loaded;
        TEST_CHECK_SUCCESS(run, UILayouts_Load(&Other.Layouts, &Loaded));
        TEST_CHECK(run, (Loaded == UILayoutsLoadResult_Missing) && (UILayouts_GetCount(&Other.Layouts) == 0U));
        UILayout* Layout = Memory_Allocate(sizeof(UILayout));
        BuildRichLayout(run, &Fixture, u8"Saved", Layout);
        TEST_CHECK_SUCCESS(run, UILayouts_Add(&Fixture.Layouts, Layout, NULL));
        UILayouts_NoteChange(&Fixture.Layouts, 3.0);
        TEST_CHECK(run, UILayouts_HasChange(&Fixture.Layouts));
        TEST_CHECK_SUCCESS(run, UILayouts_Save(&Fixture.Layouts));
        TEST_CHECK(run, !UILayouts_HasChange(&Fixture.Layouts));
        size_t Files = 0U;
        TEST_CHECK_SUCCESS(run, Test_CountFiles(Directory._data, &Files));
        TEST_CHECK(run, Files == 1U); // No temporary left beside it.
        TEST_CHECK_SUCCESS(run, UILayouts_Load(&Other.Layouts, &Loaded));
        TEST_CHECK(run, (Loaded == UILayoutsLoadResult_Loaded) && (UILayouts_GetCount(&Other.Layouts) == 1U));
        TEST_CHECK(run, UILayout_Equals(UILayouts_Get(&Other.Layouts, 0U), Layout));
        // A byte order mark (a hand edit) reads.
        GenericBuffer Document;
        GenericBuffer_AllocateVariable(&Document, 1024U, sizeof(unsigned char));
        unsigned char Mark[3] = { 0xEFU, 0xBBU, 0xBFU };
        GenericBuffer_AddLastRange(&Document, Mark, 3U);
        TEST_CHECK_SUCCESS(run, UILayouts_WriteJSON(&Fixture.Layouts, &Document));
        TEST_CHECK_SUCCESS(run, FileSystem_WriteAllBytes(Path._data, Document._data, Document._count));
        TEST_CHECK_SUCCESS(run, UILayouts_Load(&Other.Layouts, &Loaded));
        TEST_CHECK(run, Loaded == UILayoutsLoadResult_Loaded);
        // A bad file: put aside, no layouts.
        TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Directory._data, "editor_layouts.json", "{\"version\": 1, \"layouts\": [{\"name\": "));
        TEST_CHECK_SUCCESS(run, UILayouts_Load(&Other.Layouts, &Loaded));
        TEST_CHECK(run, (Loaded == UILayoutsLoadResult_Bad) && (UILayouts_GetCount(&Other.Layouts) == 0U));
        FileSystemEntryInfo Info;
        Error Missing = FileSystem_GetEntryInfo(Path._data, &Info);
        TEST_CHECK(run, Missing.Code != ErrorCode_Success);
        Error_Deconstruct(&Missing);
        GenericBuffer Aside;
        GenericBuffer_AllocateVariable(&Aside, 64U, sizeof(unsigned char));
        TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(BadPath._data, &Aside));
        TEST_CHECK(run, (Aside._count > 10U) && (memcmp(Aside._data, "{\"version\"", 10U) == 0));
        Memory_Free(Aside._data);
        // A second bad file replaces the first one put aside; a file too long is a bad one.
        char* Long = Memory_Allocate(UI_LAYOUTS_MAX_FILE_BYTES + 16U);
        Memory_Set(Long, ' ', UI_LAYOUTS_MAX_FILE_BYTES + 15U);
        Long[0] = '{';
        Long[UI_LAYOUTS_MAX_FILE_BYTES + 14U] = '}';
        Long[UI_LAYOUTS_MAX_FILE_BYTES + 15U] = 0;
        TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Directory._data, "editor_layouts.json", Long));
        Memory_Free(Long);
        TEST_CHECK_SUCCESS(run, UILayouts_Load(&Other.Layouts, &Loaded));
        TEST_CHECK(run, Loaded == UILayoutsLoadResult_Bad);
        // The next save writes the file again.
        TEST_CHECK_SUCCESS(run, UILayouts_Save(&Fixture.Layouts));
        TEST_CHECK_SUCCESS(run, UILayouts_Load(&Other.Layouts, &Loaded));
        TEST_CHECK(run, Loaded == UILayoutsLoadResult_Loaded);
        Memory_Free(Document._data);
        Memory_Free(Layout);
    }
    Deconstruct(&Other);
    Deconstruct(&Fixture);
    Memory_Free(BadPath._data);
    Memory_Free(Path._data);
    Memory_Free(Directory._data);
}

/* A file that is there but cannot be read is no bad file: it is left as it is, not put aside, and nothing is saved over it until a load
 * reads it. */
static void AFileThatCannotBeReadIsLeftAsItIs(TestRun* run)
{
#ifdef __linux__
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, PATH_CAPACITY, sizeof(unsigned char));
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, PATH_CAPACITY, sizeof(unsigned char));
    GenericBuffer BadPath;
    GenericBuffer_AllocateVariable(&BadPath, PATH_CAPACITY, sizeof(unsigned char));
    LayoutsFixture Fixture;
    bool HasPaths = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "layouts_unreadable", &Directory))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, u8"editor_layouts.json", &Path))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, u8"editor_layouts.json.bad", &BadPath));
    if (HasPaths)
    {
        RemoveIfThere(run, Path._data);
        RemoveIfThere(run, BadPath._data);
    }
    if (HasPaths && Construct(run, &Fixture, Path._data))
    {
        UILayout* Layout = Memory_Allocate(sizeof(UILayout));
        BuildRichLayout(run, &Fixture, u8"Kept", Layout);
        TEST_CHECK_SUCCESS(run, UILayouts_Add(&Fixture.Layouts, Layout, NULL));
        TEST_CHECK_SUCCESS(run, UILayouts_Save(&Fixture.Layouts));
        GenericBuffer Before;
        GenericBuffer_AllocateVariable(&Before, 1024U, sizeof(unsigned char));
        TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Before));
        bool IsLocked = chmod((const char*)Path._data, 0) == 0;
        TEST_CHECK(run, IsLocked);
        UILayoutsLoadResult Loaded = UILayoutsLoadResult_Loaded;
        TEST_CHECK_SUCCESS(run, UILayouts_Load(&Fixture.Layouts, &Loaded));
        // Run as root, the file could still be read: then there is nothing to try.
        if (Loaded == UILayoutsLoadResult_Unreadable)
        {
            TEST_CHECK(run, UILayouts_GetCount(&Fixture.Layouts) == 0U);
            TEST_CHECK(run, IsFileThere(Path._data) && !IsFileThere(BadPath._data));
            TEST_CHECK_SUCCESS(run, UILayouts_Add(&Fixture.Layouts, Layout, NULL));
            UILayouts_NoteChange(&Fixture.Layouts, 1.0);
            TEST_CHECK_ERROR(run, UILayouts_Save(&Fixture.Layouts), ErrorCode_InvalidOperation);
            TEST_CHECK(run, !UILayouts_HasChange(&Fixture.Layouts)); // Not tried again until the next change.
            TEST_CHECK_ERROR(run, UILayouts_Save(&Fixture.Layouts), ErrorCode_InvalidOperation); // Quiet the second time.
        }
        TEST_CHECK(run, (chmod((const char*)Path._data, 0644) == 0) || !IsLocked);
        GenericBuffer After;
        GenericBuffer_AllocateVariable(&After, 1024U, sizeof(unsigned char));
        TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &After));
        TEST_CHECK(run, (After._count == Before._count) && Memory_IsEqual(After._data, Before._data, Before._count));
        // Readable again: it loads, and saving works.
        TEST_CHECK_SUCCESS(run, UILayouts_Load(&Fixture.Layouts, &Loaded));
        TEST_CHECK(run, (Loaded == UILayoutsLoadResult_Loaded) && (UILayouts_GetCount(&Fixture.Layouts) == 1U));
        TEST_CHECK_SUCCESS(run, UILayouts_Save(&Fixture.Layouts));
        Memory_Free(After._data);
        Memory_Free(Before._data);
        Memory_Free(Layout);
    }
    Deconstruct(&Fixture);
    Memory_Free(BadPath._data);
    Memory_Free(Path._data);
    Memory_Free(Directory._data);
#else
    (void)run;
#endif
}

static void AFolderThatCannotBeWrittenKeepsItsFile(TestRun* run)
{
#ifdef __linux__
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, PATH_CAPACITY, sizeof(unsigned char));
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, PATH_CAPACITY, sizeof(unsigned char));
    LayoutsFixture Fixture;
    bool HasPaths = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "layouts_read_only", &Directory))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, u8"editor_layouts.json", &Path));
    if (HasPaths && Construct(run, &Fixture, Path._data))
    {
        UILayout* Layout = Memory_Allocate(sizeof(UILayout));
        BuildRichLayout(run, &Fixture, u8"First", Layout);
        TEST_CHECK_SUCCESS(run, UILayouts_Add(&Fixture.Layouts, Layout, NULL));
        TEST_CHECK_SUCCESS(run, UILayouts_Save(&Fixture.Layouts));
        GenericBuffer Before;
        GenericBuffer_AllocateVariable(&Before, 1024U, sizeof(unsigned char));
        TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Before));
        TEST_CHECK_SUCCESS(run, UILayouts_Rename(&Fixture.Layouts, 0U, u8"Renamed"));
        bool IsLocked = chmod((const char*)Directory._data, 0555) == 0;
        TEST_CHECK(run, IsLocked);
        // Run as root, the folder could still be written: then there is nothing to try.
        Error Result = UILayouts_Save(&Fixture.Layouts);
        bool IsRefused = Result.Code != ErrorCode_Success;
        Error_Deconstruct(&Result);
        Result = UILayouts_Save(&Fixture.Layouts); // The second failure is quiet, and as harmless.
        Error_Deconstruct(&Result);
        TEST_CHECK(run, (chmod((const char*)Directory._data, 0755) == 0) || !IsLocked);
        if (IsRefused)
        {
            GenericBuffer After;
            GenericBuffer_AllocateVariable(&After, 1024U, sizeof(unsigned char));
            TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &After));
            TEST_CHECK(run, (After._count == Before._count) && Memory_IsEqual(After._data, Before._data, Before._count));
            size_t Files = 0U;
            TEST_CHECK_SUCCESS(run, Test_CountFiles(Directory._data, &Files));
            TEST_CHECK(run, Files == 1U);
            Memory_Free(After._data);
        }
        TEST_CHECK_SUCCESS(run, UILayouts_Save(&Fixture.Layouts));
        Memory_Free(Before._data);
        Memory_Free(Layout);
    }
    Deconstruct(&Fixture);
    Memory_Free(Path._data);
    Memory_Free(Directory._data);
#else
    (void)run;
#endif
}

static void SavingWaitsForTheChangesToSettle(TestRun* run)
{
    LayoutsFixture Fixture;
    if (Construct(run, &Fixture, NULL))
    {
        TEST_CHECK(run, !UILayouts_IsSaveDue(&Fixture.Layouts, 100.0));
        UILayouts_NoteChange(&Fixture.Layouts, 10.0);
        TEST_CHECK(run, !UILayouts_IsSaveDue(&Fixture.Layouts, 10.5));
        TEST_CHECK(run, UILayouts_IsSaveDue(&Fixture.Layouts, 10.0 + UI_LAYOUTS_SETTLE_SECONDS));
        UILayouts_NoteChange(&Fixture.Layouts, 10.8);
        TEST_CHECK(run, !UILayouts_IsSaveDue(&Fixture.Layouts, 11.0 + UI_LAYOUTS_SETTLE_SECONDS - 0.5));
        TEST_CHECK(run, UILayouts_IsSaveDue(&Fixture.Layouts, 10.8 + UI_LAYOUTS_SETTLE_SECONDS));
    }
    Deconstruct(&Fixture);
}


// Fields.
static const TestCase Cases[] =
{
    { "names_are_checked_and_made_up", NamesAreCheckedAndMadeUp },
    { "a_layout_is_checked_whole", ALayoutIsCheckedWhole },
    { "layouts_round_trip_through_their_document", LayoutsRoundTripThroughTheirDocument },
    { "unknown_types_and_second_singles_are_dropped", UnknownTypesAndSecondSinglesAreDropped },
    { "bad_documents_change_nothing", BadDocumentsChangeNothing },
    { "docks_deeper_or_larger_than_a_tree_are_refused", DocksDeeperOrLargerThanATreeAreRefused },
    { "hostile_documents_never_break_it", HostileDocumentsNeverBreakIt },
    { "the_file_is_read_written_and_put_aside_when_bad", TheFileIsReadWrittenAndPutAsideWhenBad },
    { "a_folder_that_cannot_be_written_keeps_its_file", AFolderThatCannotBeWrittenKeepsItsFile },
    { "a_file_that_cannot_be_read_is_left_as_it_is", AFileThatCannotBeReadIsLeftAsItIs },
    { "saving_waits_for_the_changes_to_settle", SavingWaitsForTheChangesToSettle },
};


// Public functions.
void UILayoutsTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UILayouts", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
