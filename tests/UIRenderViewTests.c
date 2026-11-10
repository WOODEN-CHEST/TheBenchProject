#include "TestSuites.h"
#include "TestUI.h"
#include "Registry.h"
#include "Renderer.h"
#include "UIButton.h"
#include "UIDragDrop.h"
#include "UIKit.h"
#include "UIRenderView.h"
#include "UIScrollArea.h"
#include "UIScrollable.h"
#include "UISlider.h"
#include "UIWidgetFactory.h"
#include "UIWindow.h"
#include "UIWindowTypes.h"
#include "UIWorkspace.h"
#include "wr/WRUserData.h"
#include <math.h>


// Macros.
/** The fixture's look: a render view, windows, a workspace without bars, buttons and a slider, with small round numbers. */
#define VIEW_THEME \
    "{\"name\": \"views\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"background\": {\"kind\": \"flat\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"label\": {\"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"slider\": {\"min_size\": [64, 24], \"thumb_size\": [20, 20], \"track_thickness\": 4, \"track\": {\"kind\": \"flat\"}," \
    "\"fill\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"scroll_area\": {\"background\": {\"kind\": \"none\"}}," \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"window\": {\"title_height\": 20, \"title_padding\": 4, \"icon_spacing\": 4, \"button_size\": [16, 14], \"button_spacing\": 2, \"border\": 2," \
    "\"resize_margin\": 4, \"min_size\": [100, 60], \"minimized_width\": 150, \"content_padding\": [4, 4], \"content_spacing\": 2," \
    "\"frame\": {\"kind\": \"flat\"}, \"title_bar\": {\"kind\": \"flat\"}, \"title_text\": {\"normal\": \"#ffffff\"}, " \
    "\"content_background\": {\"kind\": \"flat\"}, \"resize_edge\": {\"kind\": \"flat\"}," \
    "\"variants\": {\"view\": {\"content_padding\": [0, 0], \"content_spacing\": 0}}}," \
    "\"workspace\": {\"tab_height\": 20, \"tab_padding\": 4, \"tab_spacing\": 2, \"icon_spacing\": 4, \"tab_max_width\": 150," \
    "\"splitter_thickness\": 4, \"guide_size\": 30, \"background\": {\"kind\": \"flat\"}, \"strip\": {\"kind\": \"flat\"}," \
    "\"tab\": {\"kind\": \"flat\"}, \"tab_text\": {\"normal\": \"#e0e0e0\"}, \"splitter\": {\"kind\": \"flat\"}, \"zone\": {\"kind\": \"flat\"}," \
    "\"drop_preview\": {\"kind\": \"flat\"}}," \
    "\"render_view\": {\"min_size\": [64, 48], \"background\": {\"kind\": \"flat\"}, " \
    "\"frame\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#00000000\"}, \"outline\": {\"thickness\": 1, \"colors\": {\"normal\": \"#00000000\"}}}}}}"
/** The view a root of the screen: its box on the fixture's 1000 x 500 surface. */
#define VIEW_X 100.0f
#define VIEW_Y 100.0f
#define VIEW_WIDTH 400.0f
#define VIEW_HEIGHT 300.0f
/** The window's numbers the tests reason with. */
#define TITLE_HEIGHT 20.0f
#define BORDER 2.0f
#define STRIP 20.0f
/** The workspace fixture's types. */
#define TYPE_VIEW 0U
#define TYPE_OTHER 1U
#define TYPE_COUNT 2U


// Types.
/* A screen with a view and a button above it. */
typedef struct ViewFixtureStruct
{
    TestUIFixture UI;
    uint64_t ViewId;
    uint64_t ButtonId;
} ViewFixture;

/* A workspace with a type whose window holds a view and a type whose window holds a button. */
typedef struct DockFixtureStruct
{
    TestUIFixture UI;
    RegistryRoot Root;
    UIWindowTypes Types;
    bool HasRoot;
    bool HasTypes;
    uint32_t TypeIndices[TYPE_COUNT];
    uint64_t WorkspaceId;
    uint64_t ViewId;
    uint64_t ButtonId;
} DockFixture;

/* What a drop target heard. */
typedef struct DropProbeStruct
{
    size_t Drops;
    uint64_t TargetId;
    uint64_t Value;
} DropProbe;


// Static functions: the fixtures.
static bool Construct(TestRun* run, ViewFixture* fixture)
{
    Memory_Zero(fixture, sizeof(*fixture));
    if (!TestUI_Construct(run, &fixture->UI) || !TestUI_SetTheme(run, &fixture->UI, VIEW_THEME))
    {
        return false;
    }
    UIButtonOptions Button = UIButtonOptions_Create(u8"Button");
    Button.Element.HasPlacement = true;
    Button.Element.Placement = UIPlacement_FromTopLeft(10.0f, 10.0f, 80.0f, 24.0f);
    UIRenderViewOptions View = UIRenderViewOptions_Create();
    View.Element.HasPlacement = true;
    View.Element.Placement = UIPlacement_FromTopLeft(VIEW_X, VIEW_Y, VIEW_WIDTH, VIEW_HEIGHT);
    bool IsMade = TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&fixture->UI.Kit, &fixture->UI.Screen, WIDGET_ID_INVALID, &Button, &fixture->ButtonId))
        && TEST_CHECK_SUCCESS(run, UIKit_CreateRenderView(&fixture->UI.Kit, &fixture->UI.Screen, WIDGET_ID_INVALID, &View, &fixture->ViewId));
    TestUI_Ticks(run, &fixture->UI, 2U);
    return IsMade;
}

static Error FillWithView(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    DockFixture* Fixture = UserData_GetPointer(userData);
    UIRenderViewOptions Options = UIRenderViewOptions_Create();
    Options.Element.Hint = UIStackHint_Fill(1.0f);
    Error Result = UIKit_CreateRenderView(UIKit_FromScreen(screen), screen, contentId, &Options, &Fixture->ViewId);
    return (Result.Code == ErrorCode_Success) ? UIWindow_SetDefaultFocus(screen, windowId, Fixture->ViewId, true) : Result;
}

static Error FillWithButton(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)windowId;
    DockFixture* Fixture = UserData_GetPointer(userData);
    UIButtonOptions Options = UIButtonOptions_Create(u8"Other");
    return UIKit_CreateButton(UIKit_FromScreen(screen), screen, contentId, &Options, &Fixture->ButtonId);
}

static bool ConstructDock(TestRun* run, DockFixture* fixture)
{
    Memory_Zero(fixture, sizeof(*fixture));
    if (!TestUI_Construct(run, &fixture->UI) || !TestUI_SetTheme(run, &fixture->UI, VIEW_THEME))
    {
        return false;
    }
    fixture->HasRoot = TEST_CHECK_SUCCESS(run, RegistryRoot_Construct(&fixture->Root, &fixture->UI.Keys));
    fixture->HasTypes = fixture->HasRoot && TEST_CHECK_SUCCESS(run, UIWindowTypes_Construct(&fixture->Types, &fixture->UI.Keys, &fixture->Root));
    if (!fixture->HasTypes)
    {
        return false;
    }
    UIWindowTypeDefinition View = UIWindowTypeDefinition_Create(u8"View", FillWithView);
    View.Data = UserData_FromPointer(fixture);
    View.FitAcross = UIWindowFit_Fit;
    View.FitDown = UIWindowFit_Fit;
    View.DefaultSize = (Vector2){ .x = 300.0f, .y = 200.0f };
    View.IsSingle = true;
    View.Variant = u8"view";
    UIWindowTypeDefinition Other = UIWindowTypeDefinition_Create(u8"Other", FillWithButton);
    Other.Data = UserData_FromPointer(fixture);
    Other.IsSingle = true;
    if (!TEST_CHECK_SUCCESS(run, UIWindowTypes_Register(&fixture->Types, u8"tbp:test_view", &View, NAMESPACED_KEY_ID_NONE, &fixture->TypeIndices[TYPE_VIEW]))
        || !TEST_CHECK_SUCCESS(run, UIWindowTypes_Register(&fixture->Types, u8"tbp:test_other", &Other, NAMESPACED_KEY_ID_NONE,
            &fixture->TypeIndices[TYPE_OTHER])))
    {
        return false;
    }
    UIWorkspaceOptions Options = UIWorkspaceOptions_Create();
    Options.HasMenuBar = false;
    Options.HasStatusBar = false;
    Options.Types = &fixture->Types;
    bool IsMade = TEST_CHECK_SUCCESS(run, UIKit_CreateWorkspace(&fixture->UI.Kit, &fixture->UI.Screen, &Options, &fixture->WorkspaceId));
    TestUI_Ticks(run, &fixture->UI, 2U);
    return IsMade;
}

static void DeconstructDock(TestRun* run, DockFixture* fixture)
{
    TestUI_Deconstruct(run, &fixture->UI);
    if (fixture->HasTypes)
    {
        UIWindowTypes_Deconstruct(&fixture->Types);
    }
    if (fixture->HasRoot)
    {
        RegistryRoot_Deconstruct(&fixture->Root);
    }
}

static uint64_t OpenType(TestRun* run, DockFixture* fixture, size_t type)
{
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIWorkspace_OpenType(&fixture->UI.Screen, fixture->WorkspaceId, fixture->TypeIndices[type], &Id));
    TestUI_Ticks(run, &fixture->UI, 2U);
    return Id;
}


// Static functions: helpers.
static UIRenderViewInput Take(TestRun* run, TestUIFixture* fixture, uint64_t viewId)
{
    UIRenderViewInput Input;
    TEST_CHECK_SUCCESS(run, UIRenderView_TakeInput(&fixture->Screen, viewId, &Input));
    return Input;
}

static const UIRenderViewButton* ButtonOf(const UIRenderViewInput* input, GameKey button)
{
    return &input->Buttons[(int32_t)button - (int32_t)GameKey_MouseLeft];
}

static bool IsNear(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static Int32Vector SizeOf(TestRun* run, TestUIFixture* fixture, uint64_t viewId)
{
    Int32Vector Size = { .X = -1, .Y = -1 };
    TEST_CHECK_SUCCESS(run, UIRenderView_GetPixelSize(&fixture->Screen, viewId, &Size));
    return Size;
}

static bool IsShown(TestRun* run, TestUIFixture* fixture, uint64_t viewId)
{
    bool Shown = false;
    TEST_CHECK_SUCCESS(run, UIRenderView_IsShown(&fixture->Screen, viewId, &Shown));
    return Shown;
}

static void PressAt(TestRun* run, TestUIFixture* fixture, GameKey button, float x, float y)
{
    TestUI_MovePointer(fixture, x, y);
    TestUI_Tick(run, fixture);
    TestUI_Press(fixture, button);
    TestUI_Tick(run, fixture);
}

static void ReleaseButton(TestRun* run, TestUIFixture* fixture, GameKey button)
{
    TestUI_Release(fixture, button);
    TestUI_Tick(run, fixture);
}

/* A key pressed for one tick, and what the tick left of it in the input: whether it was consumed. */
static bool TapIsConsumed(TestRun* run, TestUIFixture* fixture, GameKey key)
{
    TestUI_Press(fixture, key);
    TestUI_Tick(run, fixture);
    bool IsConsumed = GameInput_IsKeyConsumed(&fixture->Input, key);
    TestUI_Release(fixture, key);
    TestUI_Tick(run, fixture);
    return IsConsumed;
}

static Error ProvideDrag(UIScreen* screen, uint64_t sourceId, Vector2 pressScreenPosition, const UserData* userData, UIDragPayload* outPayload,
    bool* outIsDragged)
{
    (void)screen;
    (void)sourceId;
    (void)pressScreenPosition;
    (void)userData;
    outPayload->Kind = 7U;
    outPayload->Value = 42U;
    *outIsDragged = true;
    return UIDragPayload_SetText(outPayload, u8"a thing", 7U);
}

static bool AcceptDrag(UIScreen* screen, const UIDragQuery* query, const UserData* userData)
{
    (void)screen;
    (void)userData;
    return query->Payload->Kind == 7U;
}

static Error OnDrop(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    DropProbe* Probe = UserData_GetPointer(userData);
    if (event->HasDrag && (event->WidgetEventId == (int32_t)UIDragEvent_Dropped))
    {
        Probe->Drops++;
        Probe->TargetId = event->Drag.TargetId;
        Probe->Value = event->Drag.Value;
    }
    return Error_CreateSuccess();
}


// Tests: the record.
static void APressAndItsReleaseAreToldOnce(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TestUI_Press(&Fixture.UI, GameKey_LeftShift);
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, VIEW_X + 100.0f, VIEW_Y + 30.0f);
        UIRenderViewInput First = Take(run, &Fixture.UI, Fixture.ViewId);
        const UIRenderViewButton* Left = ButtonOf(&First, GameKey_MouseLeft);
        TEST_CHECK(run, Left->IsDown && Left->WasPressed && !Left->WasReleased && !Left->WasLost);
        TEST_CHECK(run, Left->ClickCount == 1U);
        TEST_CHECK(run, (Left->Modifiers & GAME_KEY_MODIFIER_SHIFT) != 0U);
        TEST_CHECK(run, IsNear(Left->PressPosition.x, 0.25f) && IsNear(Left->PressPosition.y, 0.1f));
        TEST_CHECK(run, !ButtonOf(&First, GameKey_MouseRight)->WasPressed);
        // Told once: the next take tells only that it is held.
        UIRenderViewInput Second = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, ButtonOf(&Second, GameKey_MouseLeft)->IsDown && !ButtonOf(&Second, GameKey_MouseLeft)->WasPressed);
        // The press's motion, summed until it is taken.
        TestUI_MovePointer(&Fixture.UI, VIEW_X + 130.0f, VIEW_Y + 40.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_MovePointer(&Fixture.UI, VIEW_X + 140.0f, VIEW_Y + 20.0f);
        TestUI_Tick(run, &Fixture.UI);
        UIRenderViewInput Moved = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, IsNear(ButtonOf(&Moved, GameKey_MouseLeft)->DeltaPixels.x, 40.0f) && IsNear(ButtonOf(&Moved, GameKey_MouseLeft)->DeltaPixels.y, -10.0f));
        TEST_CHECK(run, Moved.HasPointer && IsNear(Moved.Pointer.x, 0.35f) && IsNear(Moved.PointerPixels.y, 20.0f));
        UIRenderViewInput Still = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, IsNear(ButtonOf(&Still, GameKey_MouseLeft)->DeltaPixels.x, 0.0f) && IsNear(ButtonOf(&Still, GameKey_MouseLeft)->DeltaPixels.y, 0.0f));
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        UIRenderViewInput Released = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, !ButtonOf(&Released, GameKey_MouseLeft)->IsDown && ButtonOf(&Released, GameKey_MouseLeft)->WasReleased);
        UIRenderViewInput After = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, !ButtonOf(&After, GameKey_MouseLeft)->WasReleased);
        // A press and its release between two takes are both told.
        TestUI_Release(&Fixture.UI, GameKey_LeftShift);
        PressAt(run, &Fixture.UI, GameKey_MouseMiddle, VIEW_X + 10.0f, VIEW_Y + 10.0f);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseMiddle);
        UIRenderViewInput Both = Take(run, &Fixture.UI, Fixture.ViewId);
        const UIRenderViewButton* Middle = ButtonOf(&Both, GameKey_MouseMiddle);
        TEST_CHECK(run, Middle->WasPressed && Middle->WasReleased && !Middle->IsDown && (Middle->Modifiers == 0U));
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void ADoubleClickIsCounted(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);
        UIRenderViewInput Second = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, ButtonOf(&Second, GameKey_MouseLeft)->ClickCount == 2U);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void TheWheelReachesItFocusedOrNot(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TestUI_MovePointer(&Fixture.UI, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        TestUI_Tick(run, &Fixture.UI);
        (void)Take(run, &Fixture.UI, Fixture.ViewId);
        TestUI_Wheel(&Fixture.UI, 2.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, (GameInput_GetWheelDelta(&Fixture.UI.Input).y == 0.0f)); // The view took it: nothing below sees it.
        UIRenderViewInput Unfocused = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, !Unfocused.IsFocused && IsNear(Unfocused.Wheel.y, 2.0f));
        TEST_CHECK(run, IsNear(Take(run, &Fixture.UI, Fixture.ViewId).Wheel.y, 0.0f));
        // Two ticks' notches before a take are summed.
        TestUI_Wheel(&Fixture.UI, 1.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Wheel(&Fixture.UI, 2.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, IsNear(Take(run, &Fixture.UI, Fixture.ViewId).Wheel.y, 3.0f));
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        TestUI_Press(&Fixture.UI, GameKey_LeftControl);
        TestUI_Wheel(&Fixture.UI, -1.0f);
        TestUI_Tick(run, &Fixture.UI);
        UIRenderViewInput Focused = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, Focused.IsFocused && Focused.IsEntered && IsNear(Focused.Wheel.y, -1.0f));
        TEST_CHECK(run, (Focused.WheelModifiers & GAME_KEY_MODIFIER_CONTROL) != 0U);
        TestUI_Release(&Fixture.UI, GameKey_LeftControl);
        TestUI_Tick(run, &Fixture.UI);
        // Off the view the wheel is not its.
        TestUI_MovePointer(&Fixture.UI, 700.0f, 450.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Wheel(&Fixture.UI, 3.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, IsNear(Take(run, &Fixture.UI, Fixture.ViewId).Wheel.y, 0.0f));
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void HoverAndThePointersPlaceAreTold(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TestUI_MovePointer(&Fixture.UI, VIEW_X + 200.0f, VIEW_Y + 150.0f);
        TestUI_Tick(run, &Fixture.UI);
        UIRenderViewInput Over = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, Over.IsHovered && Over.HasPointer && Over.IsShown);
        TEST_CHECK(run, IsNear(Over.Pointer.x, 0.5f) && IsNear(Over.Pointer.y, 0.5f));
        TEST_CHECK(run, IsNear(Over.PointerPixels.x, 200.0f) && IsNear(Over.PointerPixels.y, 150.0f));
        TEST_CHECK(run, (Over.PixelSize.X == (int32_t)VIEW_WIDTH) && (Over.PixelSize.Y == (int32_t)VIEW_HEIGHT));
        TestUI_MovePointer(&Fixture.UI, 700.0f, 450.0f);
        TestUI_Tick(run, &Fixture.UI);
        UIRenderViewInput Off = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, !Off.IsHovered && !Off.HasPointer);
        // A press held past the box keeps telling where the pointer is.
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, VIEW_X + 200.0f, VIEW_Y + 150.0f);
        TestUI_MovePointer(&Fixture.UI, VIEW_X + VIEW_WIDTH + 100.0f, VIEW_Y + 150.0f);
        TestUI_Tick(run, &Fixture.UI);
        UIRenderViewInput Past = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, !Past.IsHovered && Past.HasPointer && IsNear(Past.Pointer.x, 1.25f));
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void ALostPressIsTold(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        PressAt(run, &Fixture.UI, GameKey_MouseRight, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        (void)Take(run, &Fixture.UI, Fixture.ViewId);
        Widget* View = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, Fixture.ViewId, &View)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(View, false));
        }
        TestUI_Tick(run, &Fixture.UI);
        UIRenderViewInput Lost = Take(run, &Fixture.UI, Fixture.ViewId);
        const UIRenderViewButton* Right = ButtonOf(&Lost, GameKey_MouseRight);
        TEST_CHECK(run, Right->WasLost && !Right->IsDown && !Right->WasReleased);
        TEST_CHECK(run, !Lost.IsShown && !Lost.HasPointer);
        UIRenderViewInput Again = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, !ButtonOf(&Again, GameKey_MouseRight)->WasLost);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseRight);
        UIRenderViewInput After = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, !ButtonOf(&After, GameKey_MouseRight)->WasReleased);
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void ItsPressesAskForAHiddenCursorDrag(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TEST_CHECK_SUCCESS(run, UIRenderView_CaptureOnPress(&Fixture.UI.Screen, Fixture.ViewId, UI_BUTTON_BIT(GameKey_MouseRight)));
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(&Fixture.UI.Screen) && !Take(run, &Fixture.UI, Fixture.ViewId).IsPointerCaptured);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        PressAt(run, &Fixture.UI, GameKey_MouseRight, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        TEST_CHECK(run, UIScreen_IsPointerCaptured(&Fixture.UI.Screen) && Take(run, &Fixture.UI, Fixture.ViewId).IsPointerCaptured);
        // Captured, the pointer stands still and the press hears the raw motion.
        Fixture.UI.ScreenInput.IsPointerRelative = true;
        Fixture.UI.ScreenInput.PointerDeltaPixels = (Vector2){ .x = -15.0f, .y = 4.0f };
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Tick(run, &Fixture.UI);
        Fixture.UI.ScreenInput.IsPointerRelative = false;
        Fixture.UI.ScreenInput.PointerDeltaPixels = (Vector2){ .x = 0.0f, .y = 0.0f };
        UIRenderViewInput Looked = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, IsNear(ButtonOf(&Looked, GameKey_MouseRight)->DeltaPixels.x, -30.0f)
            && IsNear(ButtonOf(&Looked, GameKey_MouseRight)->DeltaPixels.y, 8.0f));
        TEST_CHECK(run, IsNear(Looked.Pointer.x, 0.125f));
        ReleaseButton(run, &Fixture.UI, GameKey_MouseRight);
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(&Fixture.UI.Screen));
        // Taken back: the next right press drags as any.
        TEST_CHECK_SUCCESS(run, UIRenderView_CaptureOnPress(&Fixture.UI.Screen, Fixture.ViewId, 0U));
        PressAt(run, &Fixture.UI, GameKey_MouseRight, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(&Fixture.UI.Screen));
        ReleaseButton(run, &Fixture.UI, GameKey_MouseRight);
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void ItsHiddenCursorDragEndsWithItsButton(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TEST_CHECK_SUCCESS(run, UIRenderView_CaptureOnPress(&Fixture.UI.Screen, Fixture.ViewId, UI_BUTTON_BIT(GameKey_MouseRight)));
        PressAt(run, &Fixture.UI, GameKey_MouseRight, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        // The left button pressed during the drag lands on the view: the pointer stands still over it.
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);
        UIRenderViewInput Both = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, Both.IsPointerCaptured && ButtonOf(&Both, GameKey_MouseLeft)->IsDown && ButtonOf(&Both, GameKey_MouseRight)->IsDown);
        // Let go of the right button: the drag it asked for ends, and the left button's press goes on.
        ReleaseButton(run, &Fixture.UI, GameKey_MouseRight);
        UIRenderViewInput LeftOnly = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(&Fixture.UI.Screen) && !LeftOnly.IsPointerCaptured);
        TEST_CHECK(run, ButtonOf(&LeftOnly, GameKey_MouseLeft)->IsDown && ButtonOf(&LeftOnly, GameKey_MouseRight)->WasReleased);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        // The same when the right button's press is handed to another widget.
        PressAt(run, &Fixture.UI, GameKey_MouseRight, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, UIScreen_IsPointerCaptured(&Fixture.UI.Screen));
        TEST_CHECK_SUCCESS(run, UIScreen_TransferPress(&Fixture.UI.Screen, GameKey_MouseRight, Fixture.ButtonId));
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(&Fixture.UI.Screen));
        UIRenderViewInput Handed = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, ButtonOf(&Handed, GameKey_MouseRight)->WasLost && ButtonOf(&Handed, GameKey_MouseLeft)->IsDown);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseRight);
        // Two buttons that both ask for it: the drag lasts until the last of them comes up.
        TEST_CHECK_SUCCESS(run, UIRenderView_CaptureOnPress(&Fixture.UI.Screen, Fixture.ViewId,
            (uint8_t)(UI_BUTTON_BIT(GameKey_MouseRight) | UI_BUTTON_BIT(GameKey_MouseMiddle))));
        PressAt(run, &Fixture.UI, GameKey_MouseRight, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        TestUI_Press(&Fixture.UI, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture.UI);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseRight);
        TEST_CHECK(run, UIScreen_IsPointerCaptured(&Fixture.UI.Screen));
        ReleaseButton(run, &Fixture.UI, GameKey_MouseMiddle);
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(&Fixture.UI.Screen));
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void APressHandedToItIsHeld(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        // A press with Shift on the view first, so that what the handed press tells is its own.
        TestUI_Press(&Fixture.UI, GameKey_LeftShift);
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, VIEW_X + 10.0f, VIEW_Y + 10.0f);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        TestUI_Release(&Fixture.UI, GameKey_LeftShift);
        UIRenderViewInput Before = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, (ButtonOf(&Before, GameKey_MouseLeft)->Modifiers & GAME_KEY_MODIFIER_SHIFT) != 0U);
        // A press on the button handed to the view: held at once, though it never pressed on the view, begun where it began, and with no
        // modifiers (they were held over the button).
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, 30.0f, 20.0f);
        TEST_CHECK_SUCCESS(run, UIScreen_TransferPress(&Fixture.UI.Screen, GameKey_MouseLeft, Fixture.ViewId));
        UIRenderViewInput Handed = Take(run, &Fixture.UI, Fixture.ViewId);
        const UIRenderViewButton* Left = ButtonOf(&Handed, GameKey_MouseLeft);
        TEST_CHECK(run, Left->IsDown && !Left->WasPressed && (Left->ClickCount == 1U) && (Left->Modifiers == GAME_KEY_MODIFIER_NONE)
            && Handed.HasPointer);
        TEST_CHECK(run, IsNear(Left->PressPosition.x, (30.0f - VIEW_X) / VIEW_WIDTH) && IsNear(Left->PressPosition.y, (20.0f - VIEW_Y) / VIEW_HEIGHT));
        TestUI_MovePointer(&Fixture.UI, 60.0f, 30.0f);
        TestUI_Tick(run, &Fixture.UI);
        UIRenderViewInput Moved = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, ButtonOf(&Moved, GameKey_MouseLeft)->IsDown && IsNear(ButtonOf(&Moved, GameKey_MouseLeft)->DeltaPixels.x, 30.0f));
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        UIRenderViewInput Released = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, !ButtonOf(&Released, GameKey_MouseLeft)->IsDown && ButtonOf(&Released, GameKey_MouseLeft)->WasReleased);
        // Handed and let go between two takes: its release is told, with where it began.
        PressAt(run, &Fixture.UI, GameKey_MouseMiddle, 40.0f, 30.0f);
        TEST_CHECK_SUCCESS(run, UIScreen_TransferPress(&Fixture.UI.Screen, GameKey_MouseMiddle, Fixture.ViewId));
        ReleaseButton(run, &Fixture.UI, GameKey_MouseMiddle);
        UIRenderViewInput Quick = Take(run, &Fixture.UI, Fixture.ViewId);
        const UIRenderViewButton* Middle = ButtonOf(&Quick, GameKey_MouseMiddle);
        TEST_CHECK(run, !Middle->IsDown && !Middle->WasPressed && Middle->WasReleased);
        TEST_CHECK(run, IsNear(Middle->PressPosition.x, (40.0f - VIEW_X) / VIEW_WIDTH) && IsNear(Middle->PressPosition.y, (30.0f - VIEW_Y) / VIEW_HEIGHT));
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void AnotherWidgetsPressLetGoOverItIsNotItsOwn(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, 30.0f, 20.0f);
        TestUI_MovePointer(&Fixture.UI, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        TestUI_Tick(run, &Fixture.UI);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        UIRenderViewInput Over = Take(run, &Fixture.UI, Fixture.ViewId);
        const UIRenderViewButton* Left = ButtonOf(&Over, GameKey_MouseLeft);
        TEST_CHECK(run, Over.IsHovered && !Left->WasPressed && !Left->WasReleased && !Left->IsDown);
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void TheWheelStopsAtItInsideAScrollArea(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture) && TestUI_SetTheme(run, &Fixture, VIEW_THEME))
    {
        UIScrollAreaOptions Area = UIScrollAreaOptions_Create();
        Area.Element.HasPlacement = true;
        Area.Element.Placement = UIPlacement_FromTopLeft(100.0f, 100.0f, 300.0f, 200.0f);
        uint64_t AreaId = WIDGET_ID_INVALID;
        uint64_t ViewId = WIDGET_ID_INVALID;
        UIRenderViewOptions View = UIRenderViewOptions_Create();
        View.Element.Hint = UIStackHint_Fixed(600.0f);
        UIStackOptions Column = UIStackOptions_Create(UIStackDirection_Column);
        uint64_t ColumnId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateScrollArea(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Area, &AreaId));
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, AreaId, &Column, &ColumnId));
        TEST_CHECK_SUCCESS(run, UIKit_CreateRenderView(&Fixture.Kit, &Fixture.Screen, ColumnId, &View, &ViewId));
        TestUI_Ticks(run, &Fixture, 3U);
        Vector2 Range = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetRange(&Fixture.Screen, AreaId, &Range));
        TEST_CHECK(run, Range.y > 100.0f); // The view is taller than the area: the area could scroll.
        TestUI_MovePointer(&Fixture, 200.0f, 150.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Wheel(&Fixture, -2.0f);
        TestUI_Tick(run, &Fixture);
        Vector2 Offset = { .x = -1.0f, .y = -1.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, AreaId, &Offset));
        TEST_CHECK(run, IsNear(Offset.y, 0.0f) && IsNear(Take(run, &Fixture, ViewId).Wheel.y, -2.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ItShowsOnlyAttachedAndWithRoom(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TEST_CHECK(run, IsShown(run, &Fixture.UI, Fixture.ViewId));
        Widget* View = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, Fixture.ViewId, &View)))
        {
            UIPlacement Empty = UIPlacement_FromTopLeft(VIEW_X, VIEW_Y, 0.0f, VIEW_HEIGHT);
            TEST_CHECK_SUCCESS(run, Widget_SetPlacement(View, &Empty));
        }
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, !IsShown(run, &Fixture.UI, Fixture.ViewId) && !Take(run, &Fixture.UI, Fixture.ViewId).IsShown);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, Fixture.ViewId, &View)))
        {
            UIPlacement Room = UIPlacement_FromTopLeft(VIEW_X, VIEW_Y, VIEW_WIDTH, VIEW_HEIGHT);
            TEST_CHECK_SUCCESS(run, Widget_SetPlacement(View, &Room));
        }
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, IsShown(run, &Fixture.UI, Fixture.ViewId));
        // Taken off the screen (not destroyed), it shows nowhere.
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, Fixture.ViewId, &View)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(&Fixture.UI.Screen, View));
        }
        TEST_CHECK(run, !IsShown(run, &Fixture.UI, Fixture.ViewId));
        if (UIScreen_TryGetWidgetById(&Fixture.UI.Screen, Fixture.ViewId, &View))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&Fixture.UI.Screen, View)); // Back, so the fixture's teardown finds it.
        }
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}


// Tests: the keys.
static void APressEntersItAndItKeepsItsKeys(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, VIEW_X + 50.0f, VIEW_Y + 50.0f);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Fixture.ViewId);
        static const GameKey Keys[] = { GameKey_Tab, GameKey_Up, GameKey_Down, GameKey_Left, GameKey_Right, GameKey_Enter, GameKey_Escape,
            GameKey_W };
        for (size_t Index = 0U; Index < (sizeof(Keys) / sizeof(Keys[0])); Index++)
        {
            TEST_CHECK(run, !TapIsConsumed(run, &Fixture.UI, Keys[Index]));
            TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Fixture.ViewId)
                && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Fixture.ViewId));
        }
        UIRenderViewInput Kept = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, Kept.IsFocused && Kept.IsEntered);
        // A press elsewhere leaves it, and navigation takes Tab again.
        TestUI_Click(run, &Fixture.UI, 30.0f, 20.0f);
        TEST_CHECK(run, !Take(run, &Fixture.UI, Fixture.ViewId).IsEntered);
        TEST_CHECK(run, TapIsConsumed(run, &Fixture.UI, GameKey_Tab));
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void TabOntoItOnlyFocusesItAndEnterEntersIt(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TestUI_Click(run, &Fixture.UI, 30.0f, 20.0f);
        TestUI_Tap(run, &Fixture.UI, GameKey_Tab); // Navigation starts where the focus is.
        TestUI_Tap(run, &Fixture.UI, GameKey_Tab);
        UIRenderViewInput Tabbed = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, Tabbed.IsFocused && !Tabbed.IsEntered);
        // Focused but not entered, it leaves the arrows to navigation.
        TEST_CHECK(run, TapIsConsumed(run, &Fixture.UI, GameKey_Up));
        TestUI_Tap(run, &Fixture.UI, GameKey_Down);
        if (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) != Fixture.ViewId)
        {
            TestUI_Tap(run, &Fixture.UI, GameKey_Tab);
        }
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Fixture.ViewId);
        TestUI_Tap(run, &Fixture.UI, GameKey_Enter);
        UIRenderViewInput Entered = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, Entered.IsFocused && Entered.IsEntered);
        // No press reached the record: only the keys entered it.
        TEST_CHECK(run, !ButtonOf(&Entered, GameKey_MouseLeft)->WasPressed);
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void TabFromAnEnteredElementOnlyFocusesIt(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UISliderOptions Slider = UISliderOptions_Create();
        Slider.Element.HasPlacement = true;
        Slider.Element.Placement = UIPlacement_FromTopLeft(10.0f, 50.0f, 80.0f, 24.0f);
        uint64_t SliderId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.UI.Kit, &Fixture.UI.Screen, WIDGET_ID_INVALID, &Slider, &SliderId));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TestUI_Click(run, &Fixture.UI, 50.0f, 62.0f);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == SliderId);
        // Tab leaves the slider for the view, next in reading order: focused, not entered, so the keys can still leave it.
        TestUI_Tap(run, &Fixture.UI, GameKey_Tab);
        UIRenderViewInput Landed = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, Landed.IsFocused && !Landed.IsEntered);
        TEST_CHECK(run, TapIsConsumed(run, &Fixture.UI, GameKey_Tab));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) != Fixture.ViewId);
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}


// Tests: in windows.
static void ItsSizeFollowsASplitterAndAWindowEdge(TestRun* run)
{
    DockFixture Fixture;
    if (ConstructDock(run, &Fixture))
    {
        uint64_t ViewWindow = OpenType(run, &Fixture, TYPE_VIEW);
        uint64_t OtherWindow = OpenType(run, &Fixture, TYPE_OTHER);
        TEST_CHECK_SUCCESS(run, UIWorkspace_Dock(&Fixture.UI.Screen, Fixture.WorkspaceId, ViewWindow, WIDGET_ID_INVALID, UIDockZone_Center));
        TEST_CHECK_SUCCESS(run, UIWorkspace_Dock(&Fixture.UI.Screen, Fixture.WorkspaceId, OtherWindow, ViewWindow, UIDockZone_Right));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        Int32Vector Docked = SizeOf(run, &Fixture.UI, Fixture.ViewId);
        int32_t Root = UIDockTree_GetRoot(UIWorkspace_GetTree(&Fixture.UI.Screen, Fixture.WorkspaceId));
        uint64_t Splitter = UIWorkspace_GetSplitterId(&Fixture.UI.Screen, Fixture.WorkspaceId, Root);
        Rectangle SplitterBox = TestUI_GetPixelBox(run, &Fixture.UI, Splitter);
        float SplitterX = SplitterBox.x + (SplitterBox.width * 0.5f);
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, SplitterX, 250.0f);
        TestUI_MovePointer(&Fixture.UI, SplitterX + 50.0f, 250.0f);
        TestUI_Tick(run, &Fixture.UI);
        Int32Vector Dragging = SizeOf(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, (Dragging.X == (Docked.X + 50)) && (Dragging.Y == Docked.Y)); // In the tick the splitter moved.
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        // Floating: its frame's border and title bar, no padding; an edge dragged resizes it in that tick.
        TEST_CHECK_SUCCESS(run, UIWorkspace_Undock(&Fixture.UI.Screen, Fixture.WorkspaceId, ViewWindow));
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, ViewWindow, (Rectangle){ .x = 100.0f, .y = 100.0f, .width = 300.0f, .height = 200.0f }));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        Int32Vector Floating = SizeOf(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, (Floating.X == (int32_t)(300.0f - (2.0f * BORDER))) && (Floating.Y == (int32_t)(200.0f - TITLE_HEIGHT - BORDER)));
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, 100.0f + 300.0f - 1.0f, 200.0f);
        TestUI_MovePointer(&Fixture.UI, 100.0f + 300.0f - 1.0f + 40.0f, 200.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, SizeOf(run, &Fixture.UI, Fixture.ViewId).X == (Floating.X + 40));
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
    }
    DeconstructDock(run, &Fixture);
}

static void ItShowsOnlyWhileItsWindowShows(TestRun* run)
{
    DockFixture Fixture;
    if (ConstructDock(run, &Fixture))
    {
        uint64_t ViewWindow = OpenType(run, &Fixture, TYPE_VIEW);
        uint64_t OtherWindow = OpenType(run, &Fixture, TYPE_OTHER);
        TEST_CHECK(run, IsShown(run, &Fixture.UI, Fixture.ViewId));
        // A tab that is not selected.
        TEST_CHECK_SUCCESS(run, UIWorkspace_Dock(&Fixture.UI.Screen, Fixture.WorkspaceId, ViewWindow, WIDGET_ID_INVALID, UIDockZone_Center));
        TEST_CHECK_SUCCESS(run, UIWorkspace_Dock(&Fixture.UI.Screen, Fixture.WorkspaceId, OtherWindow, ViewWindow, UIDockZone_Center));
        TEST_CHECK_SUCCESS(run, UIWorkspace_SelectTab(&Fixture.UI.Screen, Fixture.WorkspaceId, OtherWindow));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, !IsShown(run, &Fixture.UI, Fixture.ViewId) && !Take(run, &Fixture.UI, Fixture.ViewId).IsShown);
        TEST_CHECK_SUCCESS(run, UIWorkspace_SelectTab(&Fixture.UI.Screen, Fixture.WorkspaceId, ViewWindow));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, IsShown(run, &Fixture.UI, Fixture.ViewId));
        // Minimized.
        TEST_CHECK_SUCCESS(run, UIWorkspace_Undock(&Fixture.UI.Screen, Fixture.WorkspaceId, ViewWindow));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, IsShown(run, &Fixture.UI, Fixture.ViewId));
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.UI.Screen, ViewWindow));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, !IsShown(run, &Fixture.UI, Fixture.ViewId));
        TEST_CHECK_SUCCESS(run, UIWindow_Restore(&Fixture.UI.Screen, ViewWindow));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, IsShown(run, &Fixture.UI, Fixture.ViewId));
        // Closed: it is gone.
        bool IsClosed = false;
        TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.UI.Screen, ViewWindow, &IsClosed));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        UIRenderViewInput Gone;
        TEST_CHECK(run, IsClosed);
        TEST_CHECK_ERROR(run, UIRenderView_TakeInput(&Fixture.UI.Screen, Fixture.ViewId, &Gone), ErrorCode_InvalidOperation);
        bool Shown = true;
        TEST_CHECK_ERROR(run, UIRenderView_IsShown(&Fixture.UI.Screen, Fixture.ViewId, &Shown), ErrorCode_InvalidOperation);
        TEST_CHECK(run, !Shown);
    }
    DeconstructDock(run, &Fixture);
}

static void APressOnTheTitleBarReachesNoRecordAndEntersIt(TestRun* run)
{
    DockFixture Fixture;
    if (ConstructDock(run, &Fixture))
    {
        uint64_t ViewWindow = OpenType(run, &Fixture, TYPE_VIEW);
        uint64_t OtherWindow = OpenType(run, &Fixture, TYPE_OTHER);
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, ViewWindow, (Rectangle){ .x = 100.0f, .y = 100.0f, .width = 300.0f, .height = 200.0f }));
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, OtherWindow, (Rectangle){ .x = 600.0f, .y = 100.0f, .width = 300.0f, .height = 200.0f }));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TestUI_Click(run, &Fixture.UI, 750.0f, 150.0f); // The other window has the focus.
        (void)Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, !Take(run, &Fixture.UI, Fixture.ViewId).IsFocused);
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, 200.0f, 110.0f);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        UIRenderViewInput Given = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, Given.IsFocused && Given.IsEntered);
        TEST_CHECK(run, !ButtonOf(&Given, GameKey_MouseLeft)->WasPressed && !ButtonOf(&Given, GameKey_MouseLeft)->WasReleased);
        // Focused, entered and left by code between two of the window's updates (as a layout is built): the window never saw it entered, and
        // its title bar gives it the keys as its default target asked.
        TestUI_Ticks(run, &Fixture.UI, (size_t)(UI_SCREEN_MULTI_CLICK_SECONDS / TEST_UI_TICK_SECONDS) + 2U); // No double click on the title bar.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, Fixture.ViewId));
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, NULL));
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID);
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, 200.0f, 110.0f);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Fixture.ViewId);
        // The first press on it when it has not the focus enters it and reaches the record.
        TestUI_Click(run, &Fixture.UI, 750.0f, 150.0f);
        TEST_CHECK(run, !Take(run, &Fixture.UI, Fixture.ViewId).IsFocused);
        PressAt(run, &Fixture.UI, GameKey_MouseRight, 250.0f, 200.0f);
        UIRenderViewInput First = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, First.IsEntered && ButtonOf(&First, GameKey_MouseRight)->WasPressed && ButtonOf(&First, GameKey_MouseRight)->IsDown);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseRight);
    }
    DeconstructDock(run, &Fixture);
}

static void ItTakesADrop(TestRun* run)
{
    ViewFixture Fixture;
    DropProbe Probe = { .Drops = 0U, .TargetId = WIDGET_ID_INVALID, .Value = 0U };
    if (Construct(run, &Fixture))
    {
        UserData Data = UserData_FromPointer(&Probe);
        uint64_t Subscription = UI_SCREEN_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIDragDrop_SetSource(&Fixture.UI.Screen, Fixture.ButtonId, ProvideDrag, NULL));
        TEST_CHECK_SUCCESS(run, UIDragDrop_SetTarget(&Fixture.UI.Screen, Fixture.ViewId, AcceptDrag, NULL));
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.UI.Screen, Fixture.ViewId, UI_EVENT_KIND_MASK(UIEventKind_Drag), OnDrop, &Data,
            &Subscription));
        PressAt(run, &Fixture.UI, GameKey_MouseLeft, 30.0f, 20.0f);
        TestUI_MovePointer(&Fixture.UI, 60.0f, 60.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_MovePointer(&Fixture.UI, VIEW_X + 100.0f, VIEW_Y + 100.0f);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        ReleaseButton(run, &Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, (Probe.Drops == 1U) && (Probe.TargetId == Fixture.ViewId) && (Probe.Value == 42U));
        // A drag is no press of the view's.
        UIRenderViewInput After = Take(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, !ButtonOf(&After, GameKey_MouseLeft)->WasPressed);
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}


// Tests: the rest.
static void ItsTextureIsSetAndCleared(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Widget* View = NULL;
        Texture2D Shown = { 0 };
        bool IsBottomUp = false;
        Texture2D Picture = { .id = 7U, .width = 400, .height = 300, .mipmaps = 1, .format = 7 };
        TEST_CHECK_SUCCESS(run, UIRenderView_SetTexture(&Fixture.UI.Screen, Fixture.ViewId, Picture, true));
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, Fixture.ViewId, &View)))
        {
            UIRenderViewCapability* Capability = Widget_GetCapability(View, UIKit_GetCapabilityId(&Fixture.UI.Kit, UIElementCapability_RenderView));
            TEST_CHECK(run, (Capability != NULL) && UIRenderViewCapability_GetTexture(Capability, &Shown, &IsBottomUp));
            TEST_CHECK(run, (Shown.id == 7U) && IsBottomUp);
            TEST_CHECK_SUCCESS(run, UIRenderView_ClearTexture(&Fixture.UI.Screen, Fixture.ViewId));
            TEST_CHECK(run, (Capability != NULL) && !UIRenderViewCapability_GetTexture(Capability, &Shown, &IsBottomUp));
        }
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void ItsLeastSizeIsTheThemes(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Widget* View = NULL;
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, Fixture.ViewId, &View)))
        {
            RenderContext Measure;
            RenderContext_CreateMeasuring(&Measure, (Vector2){ .x = 1000.0f, .y = 500.0f }, 2.0f, RenderTargetPosition_Centered());
            UIMeasureContext Context = { .Measure = &Measure };
            TEST_CHECK_SUCCESS(run, Widget_Measure(View, &Context, UI_MEASURE_UNBOUNDED_WIDTH, &Size));
        }
        TEST_CHECK(run, IsNear(Size.x, 64.0f) && IsNear(Size.y, 48.0f));
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void WhatIsNoViewIsRefused(TestRun* run)
{
    ViewFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIRenderViewInput Input;
        Int32Vector Size;
        bool Shown = false;
        Texture2D Picture = { 0 };
        TEST_CHECK_ERROR(run, UIRenderView_TakeInput(&Fixture.UI.Screen, Fixture.ButtonId, &Input), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIRenderView_GetPixelSize(&Fixture.UI.Screen, Fixture.ButtonId, &Size), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIRenderView_IsShown(&Fixture.UI.Screen, Fixture.ButtonId, &Shown), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIRenderView_SetTexture(&Fixture.UI.Screen, Fixture.ButtonId, Picture, false), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIRenderView_ClearTexture(&Fixture.UI.Screen, WIDGET_ID_INVALID), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIRenderView_CaptureOnPress(&Fixture.UI.Screen, Fixture.ButtonId, 0U), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIRenderView_CaptureOnPress(&Fixture.UI.Screen, Fixture.ViewId, (uint8_t)0x80U), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIRenderView_TakeInput(NULL, Fixture.ViewId, &Input), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIRenderView_TakeInput(&Fixture.UI.Screen, Fixture.ViewId, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIRenderView_GetPixelSize(&Fixture.UI.Screen, Fixture.ViewId, NULL), ErrorCode_IllegalArgument);
        UIRenderViewOptions Bad = UIRenderViewOptions_Create();
        Bad.CapturedButtons = (uint8_t)0x80U;
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIKit_CreateRenderView(&Fixture.UI.Kit, &Fixture.UI.Screen, WIDGET_ID_INVALID, &Bad, &Id), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Id == WIDGET_ID_INVALID);
        // A window type's variant is checked when it registers.
        RegistryRoot Root;
        UIWindowTypes Types;
        if (TEST_CHECK_SUCCESS(run, RegistryRoot_Construct(&Root, &Fixture.UI.Keys)))
        {
            if (TEST_CHECK_SUCCESS(run, UIWindowTypes_Construct(&Types, &Fixture.UI.Keys, &Root)))
            {
                UIWindowTypeDefinition Definition = UIWindowTypeDefinition_Create(u8"Long", FillWithButton);
                Definition.Variant = u8"a_variant_name_far_too_long_to_be_kept";
                TEST_CHECK_ERROR(run, UIWindowTypes_Register(&Types, u8"tbp:test_long", &Definition, NAMESPACED_KEY_ID_NONE, NULL),
                    ErrorCode_ArgumentOutOfRange);
                // Every name a window refuses as its variant: empty, capitals, a space, a namespace, past ASCII, a digit first.
                static const unsigned char* const Refused[] = { u8"", u8"View", u8"my view", u8"tbp:view", u8"\u00e9t\u00e9", u8"9view" };
                for (size_t Index = 0U; Index < (sizeof(Refused) / sizeof(Refused[0])); Index++)
                {
                    Definition.Variant = Refused[Index];
                    TEST_CHECK_ERROR(run, UIWindowTypes_Register(&Types, u8"tbp:test_long", &Definition, NAMESPACED_KEY_ID_NONE, NULL),
                        ErrorCode_ArgumentOutOfRange);
                }
                UIWindowTypes_Deconstruct(&Types);
            }
            RegistryRoot_Deconstruct(&Root);
        }
    }
    TestUI_Deconstruct(run, &Fixture.UI);
}

static void AWindowTypesVariantReachesItsWindows(TestRun* run)
{
    DockFixture Fixture;
    if (ConstructDock(run, &Fixture))
    {
        uint64_t ViewWindow = OpenType(run, &Fixture, TYPE_VIEW);
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, ViewWindow, (Rectangle){ .x = 100.0f, .y = 100.0f, .width = 300.0f, .height = 200.0f }));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        // The view variant has no padding: the view starts right at the border and under the title bar.
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture.UI, Fixture.ViewId);
        TEST_CHECK(run, TestUI_IsBox(Box, 100.0f + BORDER, 100.0f + TITLE_HEIGHT, 300.0f - (2.0f * BORDER), 200.0f - TITLE_HEIGHT - BORDER));
    }
    DeconstructDock(run, &Fixture);
}


// Fields.
static const TestCase Cases[] =
{
    { "a_press_and_its_release_are_told_once", APressAndItsReleaseAreToldOnce },
    { "a_double_click_is_counted", ADoubleClickIsCounted },
    { "the_wheel_reaches_it_focused_or_not", TheWheelReachesItFocusedOrNot },
    { "hover_and_the_pointers_place_are_told", HoverAndThePointersPlaceAreTold },
    { "a_lost_press_is_told", ALostPressIsTold },
    { "its_presses_ask_for_a_hidden_cursor_drag", ItsPressesAskForAHiddenCursorDrag },
    { "its_hidden_cursor_drag_ends_with_its_button", ItsHiddenCursorDragEndsWithItsButton },
    { "a_press_handed_to_it_is_held", APressHandedToItIsHeld },
    { "another_widgets_press_let_go_over_it_is_not_its_own", AnotherWidgetsPressLetGoOverItIsNotItsOwn },
    { "the_wheel_stops_at_it_inside_a_scroll_area", TheWheelStopsAtItInsideAScrollArea },
    { "it_shows_only_attached_and_with_room", ItShowsOnlyAttachedAndWithRoom },
    { "a_press_enters_it_and_it_keeps_its_keys", APressEntersItAndItKeepsItsKeys },
    { "tab_onto_it_only_focuses_it_and_enter_enters_it", TabOntoItOnlyFocusesItAndEnterEntersIt },
    { "tab_from_an_entered_element_only_focuses_it", TabFromAnEnteredElementOnlyFocusesIt },
    { "its_size_follows_a_splitter_and_a_window_edge", ItsSizeFollowsASplitterAndAWindowEdge },
    { "it_shows_only_while_its_window_shows", ItShowsOnlyWhileItsWindowShows },
    { "a_press_on_the_title_bar_reaches_no_record_and_enters_it", APressOnTheTitleBarReachesNoRecordAndEntersIt },
    { "it_takes_a_drop", ItTakesADrop },
    { "its_texture_is_set_and_cleared", ItsTextureIsSetAndCleared },
    { "its_least_size_is_the_themes", ItsLeastSizeIsTheThemes },
    { "what_is_no_view_is_refused", WhatIsNoViewIsRefused },
    { "a_window_types_variant_reaches_its_windows", AWindowTypesVariantReachesItsWindows },
};


// Public functions.
void UIRenderViewTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UIRenderView", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
