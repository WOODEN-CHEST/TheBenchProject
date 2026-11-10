#include "TestSuites.h"
#include "TestUI.h"
#include "Registry.h"
#include "UIKit.h"
#include "UIWindow.h"
#include "UIWindowTypes.h"
#include "wr/WRUserData.h"
#include <math.h>
#include <string.h>


// Macros.
/** The fixture's look: windows with small round numbers. */
#define TYPES_THEME \
    "{\"name\": \"window_types\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"label\": {\"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"background\": {\"kind\": \"flat\"}}," \
    "\"scroll_area\": {\"background\": {\"kind\": \"none\"}}," \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"window\": {\"title_height\": 20, \"title_padding\": 4, \"button_size\": [16, 14], \"button_spacing\": 2, \"border\": 2," \
    "\"min_size\": [100, 60], \"minimized_width\": 150, \"frame\": {\"kind\": \"flat\"}, \"title_bar\": {\"kind\": \"flat\"}," \
    "\"title_text\": {\"normal\": \"#ffffff\"}, \"content_background\": {\"kind\": \"flat\"}, \"resize_edge\": {\"kind\": \"flat\"}}}}"


// Types.
/* What the types' fill saw, and whether it fails. */
typedef struct FillProbeStruct
{
    size_t Calls;
    uint64_t WindowId;
    uint64_t ContentId;
    uint64_t AddedId;
    bool IsFailing;
} FillProbe;

/* The fixture: a UI fixture, a root registry and the window types over its key registry. */
typedef struct TypesFixtureStruct
{
    TestUIFixture UI;
    RegistryRoot Root;
    UIWindowTypes Types;
    bool HasRoot;
    bool HasTypes;
} TypesFixture;


// Static functions: helpers.
static bool Construct(TestRun* run, TypesFixture* fixture)
{
    fixture->HasRoot = false;
    fixture->HasTypes = false;
    if (!TestUI_Construct(run, &fixture->UI) || !TestUI_SetTheme(run, &fixture->UI, TYPES_THEME))
    {
        return false;
    }
    fixture->HasRoot = TEST_CHECK_SUCCESS(run, RegistryRoot_Construct(&fixture->Root, &fixture->UI.Keys));
    fixture->HasTypes = fixture->HasRoot && TEST_CHECK_SUCCESS(run, UIWindowTypes_Construct(&fixture->Types, &fixture->UI.Keys, &fixture->Root));
    return fixture->HasTypes;
}

static void Deconstruct(TestRun* run, TypesFixture* fixture)
{
    if (fixture->HasTypes)
    {
        UIWindowTypes_Deconstruct(&fixture->Types);
    }
    if (fixture->HasRoot)
    {
        RegistryRoot_Deconstruct(&fixture->Root);
    }
    TestUI_Deconstruct(run, &fixture->UI);
}

static Error Fill(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    FillProbe* Probe = UserData_GetPointer(userData);
    Probe->Calls++;
    Probe->WindowId = windowId;
    Probe->ContentId = contentId;
    if (Probe->IsFailing)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "The test's fill fails.");
    }
    UIButtonOptions Button = UIButtonOptions_Create(u8"inside");
    return UIKit_CreateButton(UIKit_FromScreen(screen), screen, contentId, &Button, &Probe->AddedId);
}

static UIWindowTypeDefinition MakeDefinition(const unsigned char* name, FillProbe* probe, bool isSingle)
{
    UIWindowTypeDefinition Definition = UIWindowTypeDefinition_Create(name, Fill);
    Definition.Data = UserData_FromPointer(probe);
    Definition.DefaultSize = (Vector2){ .x = 300.0f, .y = 200.0f };
    Definition.IsSingle = isSingle;
    Definition.Icon = UIElementIcon_FromLetter((uint32_t)'N');
    return Definition;
}

static uint32_t Register(TestRun* run, TypesFixture* fixture, const char* key, const UIWindowTypeDefinition* definition)
{
    uint32_t Index = REGISTRY_INDEX_NONE;
    TEST_CHECK_SUCCESS(run, UIWindowTypes_Register(&fixture->Types, (const unsigned char*)key, definition, NAMESPACED_KEY_ID_NONE, &Index));
    return Index;
}

static Rectangle GetShown(TestRun* run, TypesFixture* fixture, uint64_t windowId)
{
    Rectangle Shown = { 0.0f, 0.0f, 0.0f, 0.0f };
    TEST_CHECK_SUCCESS(run, UIWindow_GetShownBox(&fixture->UI.Screen, windowId, &Shown));
    return Shown;
}


// Tests.
static void TypesAreRegisteredAndFoundThroughTheRoot(TestRun* run)
{
    TypesFixture Fixture;
    if (Construct(run, &Fixture))
    {
        FillProbe Probe = { 0 };
        unsigned char Name[] = u8"Notes";
        UIWindowTypeDefinition Notes = MakeDefinition(Name, &Probe, false);
        uint32_t NotesIndex = Register(run, &Fixture, "tbp:test_notes", &Notes);
        Name[0] = (unsigned char)'X'; // The name was copied.
        UIWindowTypeDefinition Inspector = MakeDefinition(u8"Inspector", &Probe, true);
        uint32_t InspectorIndex = Register(run, &Fixture, "inspector", &Inspector);
        uint32_t Found = REGISTRY_INDEX_NONE;
        TEST_CHECK(run, UIWindowTypes_Find(&Fixture.Types, u8"tbp:inspector", &Found) && (Found == InspectorIndex));
        TEST_CHECK(run, UIWindowTypes_Find(&Fixture.Types, u8"test_notes", &Found) && (Found == NotesIndex));
        TEST_CHECK(run, !UIWindowTypes_Find(&Fixture.Types, u8"tbp:nothing", &Found));
        const UIWindowType* Type = UIWindowTypes_Get(&Fixture.Types, NotesIndex);
        TEST_CHECK(run, (Type != NULL) && (strcmp((const char*)Type->_displayName, "Notes") == 0) && !Type->_isSingle
            && (Type->_defaultSize.x == 300.0f) && (Type->_icon.Letter == (uint32_t)'N'));
        // Tools find it through the root, under its key.
        TEST_CHECK(run, RegistryRoot_FindText(&Fixture.Root, UI_WINDOW_TYPES_REGISTRY_KEY) == UIWindowTypes_AsRegistry(&Fixture.Types));
        UIWindowTypes Second;
        TEST_CHECK_ERROR(run, UIWindowTypes_Construct(&Second, &Fixture.UI.Keys, &Fixture.Root), ErrorCode_InvalidOperation);
        // A key taken is refused; so are definitions that cannot be made, and a key that is no key.
        TEST_CHECK_ERROR(run, UIWindowTypes_Register(&Fixture.Types, u8"tbp:test_notes", &Notes, NAMESPACED_KEY_ID_NONE, NULL),
            ErrorCode_InvalidOperation);
        UIWindowTypeDefinition Bad[8];
        for (size_t Index = 0U; Index < 8U; Index++)
        {
            Bad[Index] = MakeDefinition(u8"Bad", &Probe, false);
        }
        static const unsigned char BadText[] = { 0xC3U, 0x28U, 0U };
        unsigned char LongName[UI_WINDOW_TYPES_MAX_NAME_BYTES + 2U];
        memset(LongName, 'a', sizeof(LongName) - 1U);
        LongName[sizeof(LongName) - 1U] = 0U;
        Bad[0].Fill = NULL;
        Bad[1].DisplayName = u8"";
        Bad[2].DisplayName = LongName;
        Bad[3].DisplayName = BadText;
        Bad[4].DefaultSize.x = 0.0f;
        Bad[5].MinimumSize.y = NAN;
        Bad[6].FitAcross = (UIWindowFit)5;
        Bad[7].Icon = UIElementIcon_FromLetter(0U);
        for (size_t Index = 0U; Index < 8U; Index++)
        {
            TEST_CHECK_ERROR(run, UIWindowTypes_Register(&Fixture.Types, u8"tbp:bad", &Bad[Index], NAMESPACED_KEY_ID_NONE, NULL),
                ErrorCode_ArgumentOutOfRange);
        }
        TEST_CHECK_ERROR(run, UIWindowTypes_Register(&Fixture.Types, u8"Not A Key!", &Notes, NAMESPACED_KEY_ID_NONE, NULL),
            ErrorCode_ArgumentOutOfRange);
        // A name of the most bytes allowed is fine.
        unsigned char Longest[UI_WINDOW_TYPES_MAX_NAME_BYTES + 1U];
        memset(Longest, 'b', UI_WINDOW_TYPES_MAX_NAME_BYTES);
        Longest[UI_WINDOW_TYPES_MAX_NAME_BYTES] = 0U;
        UIWindowTypeDefinition AtLimit = MakeDefinition(Longest, &Probe, false);
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Register(&Fixture.Types, u8"tbp:longest", &AtLimit, NAMESPACED_KEY_ID_NONE, NULL));
        // Withdrawn, a type is not found nor listed; registered again, it keeps its index.
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Withdraw(&Fixture.Types, NotesIndex));
        TEST_CHECK(run, (UIWindowTypes_Get(&Fixture.Types, NotesIndex) == NULL) && !UIWindowTypes_Find(&Fixture.Types, u8"tbp:test_notes", &Found));
        TEST_CHECK(run, Register(run, &Fixture, "tbp:test_notes", &Notes) == NotesIndex);
        // A source's types go together.
        uint32_t Source = NAMESPACED_KEY_ID_NONE;
        TEST_CHECK_SUCCESS(run, KeyRegistry_Intern(&Fixture.UI.Keys, u8"tbp:test_pack", &Source));
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Register(&Fixture.Types, u8"tbp:pack_a", &Notes, Source, NULL));
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Register(&Fixture.Types, u8"tbp:pack_b", &Notes, Source, NULL));
        TEST_CHECK(run, UIWindowTypes_WithdrawSource(&Fixture.Types, Source) == 2U);
        TEST_CHECK(run, !UIWindowTypes_Find(&Fixture.Types, u8"tbp:pack_a", &Found) && UIWindowTypes_Find(&Fixture.Types, u8"tbp:test_notes", &Found));
    }
    Deconstruct(run, &Fixture);
}

static void OpeningMakesAWindowFilledByItsType(TestRun* run)
{
    TypesFixture Fixture;
    if (Construct(run, &Fixture))
    {
        FillProbe Probe = { 0 };
        UIWindowTypeDefinition Notes = MakeDefinition(u8"Notes", &Probe, false);
        Notes.FitDown = UIWindowFit_Fit;
        Notes.MinimumSize = (Vector2){ .x = 120.0f, .y = 0.0f };
        uint32_t Index = Register(run, &Fixture, "tbp:test_notes", &Notes);
        uint64_t First = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_notes", &First));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        uint64_t ContentId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindow_GetContentId(&Fixture.UI.Screen, First, &ContentId));
        TEST_CHECK(run, (Probe.Calls == 1U) && (Probe.WindowId == First) && (Probe.ContentId == ContentId)
            && UIScreen_IsWidgetAlive(&Fixture.UI.Screen, Probe.AddedId));
        const unsigned char* Title = NULL;
        TEST_CHECK_SUCCESS(run, UIWindow_GetTitle(&Fixture.UI.Screen, First, &Title));
        TEST_CHECK(run, (Title != NULL) && (strcmp((const char*)Title, "Notes") == 0));
        // Centred on the workspace at its default size, and given the focus.
        TEST_CHECK(run, TestUI_IsBox(GetShown(run, &Fixture, First), 350.0f, 150.0f, 300.0f, 200.0f));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == First);
        UIWindowTypes* Types = NULL;
        uint32_t TypeIndex = REGISTRY_INDEX_NONE;
        TEST_CHECK_SUCCESS(run, UIWindow_GetType(&Fixture.UI.Screen, First, &Types, &TypeIndex));
        TEST_CHECK(run, (Types == &Fixture.Types) && (TypeIndex == Index));
        // The type's fits, least size and icon are the window's.
        Widget* Window = NULL;
        const UIWindowCapability* Capability = UIScreen_TryGetWidgetById(&Fixture.UI.Screen, First, &Window)
            ? Widget_GetCapability(Window, UIKit_GetCapabilityId(&Fixture.UI.Kit, UIElementCapability_Window)) : NULL;
        TEST_CHECK(run, (Capability != NULL) && (Capability->_fitAcross == Notes.FitAcross) && (Capability->_fitDown == UIWindowFit_Fit)
            && (Capability->_minimumSize.x == 120.0f) && (Capability->_icon.Kind == UIElementIconKind_Letter) && (Capability->_icon.Letter == (uint32_t)'N'));
        // Another opens beside it, stepped down and to the right, and another past that.
        uint64_t Second = WIDGET_ID_INVALID;
        uint64_t Third = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_OpenAt(&Fixture.Types, &Fixture.UI.Screen, Index, &Second));
        TEST_CHECK_SUCCESS(run, UIWindowTypes_OpenAt(&Fixture.Types, &Fixture.UI.Screen, Index, &Third));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (Second != First) && (Third != Second) && (Probe.Calls == 3U));
        float Step = UI_WINDOW_TYPES_CASCADE_STEP;
        TEST_CHECK(run, TestUI_IsBox(GetShown(run, &Fixture, Second), 350.0f + Step, 150.0f + Step, 300.0f, 200.0f));
        TEST_CHECK(run, TestUI_IsBox(GetShown(run, &Fixture, Third), 350.0f + (2.0f * Step), 150.0f + (2.0f * Step), 300.0f, 200.0f));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Third);
    }
    Deconstruct(run, &Fixture);
}

/* A new window opens centred on the workspace; only a window at that very place, both ways, moves it on. */
static void ANewWindowOpensCentredOnTheWorkspace(TestRun* run)
{
    TypesFixture Fixture;
    if (Construct(run, &Fixture))
    {
        FillProbe Probe = { 0 };
        UIWindowTypeDefinition Notes = MakeDefinition(u8"Notes", &Probe, false);
        Register(run, &Fixture, "tbp:test_notes", &Notes);
        // The workspace runs 100 to 980 across: 300 wide, it opens at 390, 150.
        TEST_CHECK_SUCCESS(run, UIScreen_SetWorkspaceInsets(&Fixture.UI.Screen, (UIInsets){ .Left = 100.0f, .Top = 0.0f, .Right = 20.0f, .Bottom = 0.0f }));
        UIWindowOptions Options = UIWindowOptions_Create(u8"Above");
        Options.HasPosition = true;
        Options.Position = (Vector2){ .x = 390.0f, .y = 0.0f };
        uint64_t Above = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateWindow(&Fixture.UI.Kit, &Fixture.UI.Screen, &Options, &Above, NULL));
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_notes", &Id));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, TestUI_IsBox(GetShown(run, &Fixture, Id), 390.0f, 150.0f, 300.0f, 200.0f));
    }
    Deconstruct(run, &Fixture);
}

static Error FillAndClose(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)contentId;
    FillProbe* Probe = UserData_GetPointer(userData);
    Probe->Calls++;
    return UIWindow_CloseNow(screen, windowId);
}

/* A fill may close its own window: the open succeeds with no window. A window opened while another is maximized restores that one, so the
 * new one shows. */
static void OpeningCopesWithWhatTheFillAndTheScreenDo(TestRun* run)
{
    TypesFixture Fixture;
    if (Construct(run, &Fixture))
    {
        FillProbe Probe = { 0 };
        UIWindowTypeDefinition Closing = UIWindowTypeDefinition_Create(u8"Closing", FillAndClose);
        Closing.Data = UserData_FromPointer(&Probe);
        Register(run, &Fixture, "tbp:test_closing", &Closing);
        size_t Before = UIScreen_GetWidgetCount(&Fixture.UI.Screen);
        uint64_t Id = 1U;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_closing", &Id));
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, (Id == WIDGET_ID_INVALID) && (Probe.Calls == 1U) && (UIScreen_GetWidgetCount(&Fixture.UI.Screen) == Before));
        // Maximized first, then another opened over it: the first is restored.
        FillProbe NotesProbe = { 0 };
        UIWindowTypeDefinition Notes = MakeDefinition(u8"Notes", &NotesProbe, false);
        Register(run, &Fixture, "tbp:test_notes", &Notes);
        uint64_t First = WIDGET_ID_INVALID;
        uint64_t Second = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_notes", &First));
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.UI.Screen, First));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_notes", &Second));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        bool IsMaximized = true;
        TEST_CHECK_SUCCESS(run, UIWindow_GetState(&Fixture.UI.Screen, First, NULL, &IsMaximized));
        TEST_CHECK(run, !IsMaximized && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Second));
    }
    Deconstruct(run, &Fixture);
}

static void ASingleTypeOpensOnce(TestRun* run)
{
    TypesFixture Fixture;
    if (Construct(run, &Fixture))
    {
        FillProbe Probe = { 0 };
        UIWindowTypeDefinition Inspector = MakeDefinition(u8"Inspector", &Probe, true);
        UIWindowTypeDefinition Notes = MakeDefinition(u8"Notes", &Probe, false);
        Register(run, &Fixture, "tbp:test_inspector", &Inspector);
        Register(run, &Fixture, "tbp:test_notes", &Notes);
        uint64_t First = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_inspector", &First));
        uint64_t Over = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_notes", &Over));
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, Over, (Rectangle){ 350.0f, 150.0f, 300.0f, 200.0f }));
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.UI.Screen, First));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        uint64_t Again = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_inspector", &Again));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        // The same window, restored, in front of the other and focused; nothing new filled.
        bool IsMinimized = true;
        TEST_CHECK_SUCCESS(run, UIWindow_GetState(&Fixture.UI.Screen, First, &IsMinimized, NULL));
        TEST_CHECK(run, (Again == First) && !IsMinimized && (Probe.Calls == 2U));
        UIViewport Viewport = UIScreen_GetViewport(&Fixture.UI.Screen);
        Widget* Top = NULL;
        Vector2 Middle = { .x = 500.0f / Viewport.SizePixels.x, .y = 250.0f / Viewport.SizePixels.y };
        bool IsFirstOnTop = false;
        if (UIScreen_TryGetTopmostAt(&Fixture.UI.Screen, Middle, &Top))
        {
            for (Widget* Current = Top; Current != NULL; Current = Widget_GetParent(Current))
            {
                IsFirstOnTop = IsFirstOnTop || (Widget_GetId(Current) == First);
            }
        }
        TEST_CHECK(run, IsFirstOnTop);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == First);
        uint64_t Found = WIDGET_ID_INVALID;
        uint32_t InspectorIndex = REGISTRY_INDEX_NONE;
        TEST_CHECK(run, UIWindowTypes_Find(&Fixture.Types, u8"tbp:test_inspector", &InspectorIndex)
            && UIWindowTypes_FindOpen(&Fixture.Types, &Fixture.UI.Screen, InspectorIndex, &Found) && (Found == First));
        // Found wherever it is in the tree: under another widget too.
        UIStackOptions Holder = UIStackOptions_Create(UIStackDirection_Column);
        uint64_t HolderId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.UI.Kit, &Fixture.UI.Screen, WIDGET_ID_INVALID, &Holder, &HolderId));
        Widget* Window = NULL;
        Widget* HolderWidget = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, First, &Window)
            && UIScreen_TryGetWidgetById(&Fixture.UI.Screen, HolderId, &HolderWidget)))
        {
            TEST_CHECK_SUCCESS(run, Widget_MoveTo(Window, HolderWidget));
        }
        TEST_CHECK(run, UIWindowTypes_FindOpen(&Fixture.Types, &Fixture.UI.Screen, InspectorIndex, &Found) && (Found == First));
        // Another set of types (outside the root), its type at the same index: this one's window is not its.
        UIWindowTypes Other;
        if (TEST_CHECK_SUCCESS(run, UIWindowTypes_Construct(&Other, &Fixture.UI.Keys, NULL)))
        {
            uint32_t OtherIndex = REGISTRY_INDEX_NONE;
            TEST_CHECK_SUCCESS(run, UIWindowTypes_Register(&Other, u8"tbp:test_inspector", &Inspector, NAMESPACED_KEY_ID_NONE, &OtherIndex));
            TEST_CHECK(run, (OtherIndex == InspectorIndex) && !UIWindowTypes_FindOpen(&Other, &Fixture.UI.Screen, OtherIndex, &Found));
            UIWindowTypes_Deconstruct(&Other);
        }
        // Closed, it opens anew.
        TEST_CHECK_SUCCESS(run, UIWindow_CloseNow(&Fixture.UI.Screen, First));
        TEST_CHECK(run, !UIWindowTypes_FindOpen(&Fixture.Types, &Fixture.UI.Screen, InspectorIndex, &Found));
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_inspector", &Again));
        TEST_CHECK(run, (Again != First) && (Probe.Calls == 3U));
    }
    Deconstruct(run, &Fixture);
}

static void WhatCannotOpenOpensNothing(TestRun* run)
{
    TypesFixture Fixture;
    if (Construct(run, &Fixture))
    {
        FillProbe Probe = { 0 };
        UIWindowTypeDefinition Notes = MakeDefinition(u8"Notes", &Probe, false);
        uint32_t Index = Register(run, &Fixture, "tbp:test_notes", &Notes);
        size_t Before = UIScreen_GetWidgetCount(&Fixture.UI.Screen);
        uint64_t Id = 1U;
        TEST_CHECK_ERROR(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:nope", &Id), ErrorCode_InvalidOperation);
        TEST_CHECK(run, (Id == WIDGET_ID_INVALID) && (UIScreen_GetWidgetCount(&Fixture.UI.Screen) == Before));
        TEST_CHECK_ERROR(run, UIWindowTypes_OpenAt(&Fixture.Types, &Fixture.UI.Screen, Index + 5U, &Id), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIWindowTypes_Open(NULL, &Fixture.UI.Screen, u8"tbp:test_notes", &Id), ErrorCode_IllegalArgument);
        // A fill that fails leaves no window.
        Probe.IsFailing = true;
        TEST_CHECK_ERROR(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_notes", &Id), ErrorCode_InvalidOperation);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, (Id == WIDGET_ID_INVALID) && (Probe.Calls == 1U) && (UIScreen_GetWidgetCount(&Fixture.UI.Screen) == Before));
        // A withdrawn type does not open.
        Probe.IsFailing = false;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Withdraw(&Fixture.Types, Index));
        TEST_CHECK_ERROR(run, UIWindowTypes_OpenAt(&Fixture.Types, &Fixture.UI.Screen, Index, &Id), ErrorCode_InvalidOperation);
    }
    Deconstruct(run, &Fixture);
}

static void AClosedWindowsPlaceIsRemembered(TestRun* run)
{
    TypesFixture Fixture;
    if (Construct(run, &Fixture))
    {
        FillProbe Probe = { 0 };
        UIWindowTypeDefinition Notes = MakeDefinition(u8"Notes", &Probe, true);
        uint32_t Index = Register(run, &Fixture, "tbp:test_notes", &Notes);
        Rectangle Remembered;
        TEST_CHECK(run, !UIWindowTypes_GetRememberedBox(&Fixture.Types, Index, &Remembered));
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_notes", &Id));
        Rectangle Moved = { 40.0f, 60.0f, 320.0f, 240.0f };
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, Id, Moved));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        bool IsClosed = false;
        TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.UI.Screen, Id, &IsClosed));
        TEST_CHECK(run, IsClosed && UIWindowTypes_GetRememberedBox(&Fixture.Types, Index, &Remembered)
            && TestUI_IsBox(Remembered, Moved.x, Moved.y, Moved.width, Moved.height));
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Open(&Fixture.Types, &Fixture.UI.Screen, u8"tbp:test_notes", &Id));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, TestUI_IsBox(GetShown(run, &Fixture, Id), Moved.x, Moved.y, Moved.width, Moved.height));
        // A type that opens several: the next one at the remembered place steps aside from the one there.
        FillProbe ManyProbe = { 0 };
        UIWindowTypeDefinition Many = MakeDefinition(u8"Many", &ManyProbe, false);
        uint32_t ManyIndex = Register(run, &Fixture, "tbp:test_many", &Many);
        UIWindowTypes_RememberBox(&Fixture.Types, ManyIndex, Moved);
        uint64_t FirstMany = WIDGET_ID_INVALID;
        uint64_t SecondMany = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_OpenAt(&Fixture.Types, &Fixture.UI.Screen, ManyIndex, &FirstMany));
        TEST_CHECK_SUCCESS(run, UIWindowTypes_OpenAt(&Fixture.Types, &Fixture.UI.Screen, ManyIndex, &SecondMany));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        float Step = UI_WINDOW_TYPES_CASCADE_STEP;
        TEST_CHECK(run, TestUI_IsBox(GetShown(run, &Fixture, SecondMany), Moved.x + (2.0f * Step), Moved.y + (2.0f * Step), Moved.width, Moved.height)
            && TestUI_IsBox(GetShown(run, &Fixture, FirstMany), Moved.x + Step, Moved.y + Step, Moved.width, Moved.height));
        // A box that means nothing is not remembered.
        UIWindowTypes_RememberBox(&Fixture.Types, Index, (Rectangle){ NAN, 0.0f, 10.0f, 10.0f });
        TEST_CHECK(run, !UIWindowTypes_GetRememberedBox(&Fixture.Types, Index, &Remembered));
        UIWindowTypes_RememberBox(&Fixture.Types, Index, (Rectangle){ 0.0f, 5e9f, 10.0f, 10.0f }); // Further than a window can be.
        TEST_CHECK(run, !UIWindowTypes_GetRememberedBox(&Fixture.Types, Index, &Remembered));
        UIWindowTypes_RememberBox(&Fixture.Types, Index, (Rectangle){ -5e9f, 0.0f, 10.0f, 10.0f });
        TEST_CHECK(run, !UIWindowTypes_GetRememberedBox(&Fixture.Types, Index, &Remembered));
        UIWindowTypes_RememberBox(&Fixture.Types, Index + 100U, Moved);
    }
    Deconstruct(run, &Fixture);
}

static void AWindowMenuListsTheTypes(TestRun* run)
{
    TypesFixture Fixture;
    if (Construct(run, &Fixture))
    {
        FillProbe Probe = { 0 };
        UIWindowTypeDefinition First = MakeDefinition(u8"First", &Probe, false);
        UIWindowTypeDefinition Second = MakeDefinition(u8"Second", &Probe, false);
        UIWindowTypeDefinition Third = MakeDefinition(u8"Third", &Probe, false);
        Third.Icon = UIElementIcon_None();
        Register(run, &Fixture, "tbp:first", &First);
        uint32_t SecondIndex = Register(run, &Fixture, "tbp:second", &Second);
        uint32_t ThirdIndex = Register(run, &Fixture, "tbp:third", &Third);
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Withdraw(&Fixture.Types, SecondIndex));
        UIMenuItem Items[4];
        size_t Count = 0U;
        TEST_CHECK_SUCCESS(run, UIWindowTypes_WriteMenuItems(&Fixture.Types, 100U, Items, 4U, &Count));
        TEST_CHECK(run, (Count == 2U) && (Items[0].Id == 100U) && (strcmp((const char*)Items[0].Label, "First") == 0)
            && (Items[0].Icon.Kind == UIElementIconKind_Letter) && Items[0].IsEnabled);
        TEST_CHECK(run, (Items[1].Id == (100U + ThirdIndex)) && (strcmp((const char*)Items[1].Label, "Third") == 0)
            && (Items[1].Icon.Kind == UIElementIconKind_None));
        TEST_CHECK_ERROR(run, UIWindowTypes_WriteMenuItems(&Fixture.Types, 100U, Items, 1U, &Count), ErrorCode_BufferTooSmall);
        TEST_CHECK(run, Count == 1U);
        TEST_CHECK_ERROR(run, UIWindowTypes_WriteMenuItems(&Fixture.Types, UINT32_MAX - 1U, Items, 4U, &Count), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIWindowTypes_WriteMenuItems(&Fixture.Types, 0U, NULL, 4U, &Count), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIWindowTypes_WriteMenuItems(&Fixture.Types, 0U, NULL, 0U, &Count), ErrorCode_BufferTooSmall);
    }
    Deconstruct(run, &Fixture);
}


// Fields.
static const TestCase Cases[] =
{
    { "types_are_registered_and_found_through_the_root", TypesAreRegisteredAndFoundThroughTheRoot },
    { "opening_makes_a_window_filled_by_its_type", OpeningMakesAWindowFilledByItsType },
    { "a_new_window_opens_centred_on_the_workspace", ANewWindowOpensCentredOnTheWorkspace },
    { "opening_copes_with_what_the_fill_and_the_screen_do", OpeningCopesWithWhatTheFillAndTheScreenDo },
    { "a_single_type_opens_once", ASingleTypeOpensOnce },
    { "what_cannot_open_opens_nothing", WhatCannotOpenOpensNothing },
    { "a_closed_windows_place_is_remembered", AClosedWindowsPlaceIsRemembered },
    { "a_window_menu_lists_the_types", AWindowMenuListsTheTypes },
};


// Public functions.
void UIWindowTypesTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UIWindowTypes", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
