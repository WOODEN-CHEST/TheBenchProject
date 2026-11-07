#include "TestUI.h"
#include <math.h>
#include "UIWidget.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"


// Macros.
/** The fixture's font: a glyph advances this many pixels at the font's base size of 10. */
#define TEST_UI_FONT_ADVANCE 5
/** How far apart two float positions may be and still be the same pixel. */
#define PIXEL_TOLERANCE 0.01f


// Static functions.
static Error RecordEvent(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    TestUIEvents* Events = UserData_GetPointer(userData);
    bool IsInputEvent = (event->WidgetEventId == (int32_t)UIElementEvent_Pressed) || (event->WidgetEventId == (int32_t)UIElementEvent_Released)
        || (event->WidgetEventId == (int32_t)UIElementEvent_DoubleClicked) || (event->WidgetEventId == (int32_t)UIElementEvent_Scrolled)
        || (event->WidgetEventId == (int32_t)UIElementEvent_ContextRequested);
    if (IsInputEvent && !Events->IsRecordingInputEvents)
    {
        return Error_CreateSuccess();
    }
    if (Events->Count < TEST_UI_MAX_RECORDED_EVENTS)
    {
        Events->Events[Events->Count] = *event;
        Events->Count++;
    }
    Widget* Target = NULL;
    if (Events->IsDestroyingTheWidget && UIScreen_TryGetWidgetById(screen, event->WidgetId, &Target))
    {
        return Widget_Destroy(Target);
    }
    return Error_CreateSuccess();
}


// Public functions.
bool TestUI_ConstructLogged(TestRun* run, TestUIFixture* self, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    bool IsBuilt = TestAssetFixture_Construct(run, &self->Assets)
        && TestAssets_AddFont(run, &self->Assets, "mono", TEST_UI_FONT_ADVANCE, NULL)
        && TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&self->Components))
        && TEST_CHECK_SUCCESS(run, UIWidgetFactory_Construct(&self->Widgets))
        && TEST_CHECK_SUCCESS(run, UIKit_Construct(&self->Kit, &self->Widgets, &self->Components, self->Assets.Manager, logger))
        && TEST_CHECK_SUCCESS(run, UIScreen_Construct(&self->Screen, &self->Widgets, logger))
        && TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&self->Keys))
        && TEST_CHECK_SUCCESS(run, GameInput_Construct(&self->Input, &self->Keys, NULL, NULL));
    self->IsReady = IsBuilt;
    if (!IsBuilt)
    {
        return false;
    }
    TestUI_SetSurface(run, self, TEST_UI_SURFACE_WIDTH, TEST_UI_SURFACE_HEIGHT);
    GameInputSample_Clear(&self->Sample);
    self->Sample.WindowSize = (Vector2){ .x = TEST_UI_SURFACE_WIDTH, .y = TEST_UI_SURFACE_HEIGHT };
    self->ScreenInput = (UIScreenInput)
    {
        .Input = &self->Input,
        .IsPointerPresent = false,
        .PointerPosition = (Vector2){ .x = 0.0f, .y = 0.0f },
        .HasKeyboardFocus = true
    };
    return true;
}

bool TestUI_Construct(TestRun* run, TestUIFixture* self)
{
    return TestUI_ConstructLogged(run, self, NULL);
}

void TestUI_Deconstruct(TestRun* run, TestUIFixture* self)
{
    if (!self->IsReady)
    {
        return;
    }
    TEST_CHECK_SUCCESS(run, UIScreen_Deconstruct(&self->Screen));
    TEST_CHECK(run, UIWidgetFactory_GetTotalLiveWidgetCount(&self->Widgets) == 0U);
    TEST_CHECK_SUCCESS(run, UIKit_Deconstruct(&self->Kit));
    TEST_CHECK_SUCCESS(run, UIWidgetFactory_Deconstruct(&self->Widgets));
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&self->Components) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&self->Components));
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&self->Input));
    KeyRegistry_Deconstruct(&self->Keys);
    TestAssetFixture_Deconstruct(run, &self->Assets);
}

bool TestUI_SetTheme(TestRun* run, TestUIFixture* self, const char* json)
{
    if (!TestAssets_AddUITheme(run, &self->Assets, json))
    {
        return false;
    }
    // The name is the one thing every test theme's text starts with.
    const char* NameStart = json;
    while ((*NameStart != 0) && (*NameStart != ':'))
    {
        NameStart++;
    }
    while ((*NameStart != 0) && (*NameStart != '"'))
    {
        NameStart++;
    }
    char Name[128];
    size_t Length = 0U;
    if (*NameStart == '"')
    {
        NameStart++;
        while ((NameStart[Length] != 0) && (NameStart[Length] != '"') && (Length < (sizeof(Name) - 1U)))
        {
            Name[Length] = NameStart[Length];
            Length++;
        }
    }
    Name[Length] = 0;
    return TEST_CHECK_SUCCESS(run, UIScreen_SetTheme(&self->Screen, self->Assets.Manager, (const unsigned char*)Name))
        && TEST_CHECK(run, UIScreen_GetTheme(&self->Screen) != NULL);
}

void TestUI_Tick(TestRun* run, TestUIFixture* self)
{
    GameInput_BeginTick(&self->Input, &self->Sample);
    self->Time += TEST_UI_TICK_SECONDS;
    TEST_CHECK_SUCCESS(run, UIScreen_Update(&self->Screen, ProgramTime_Create(self->Time, TEST_UI_TICK_SECONDS), &self->ScreenInput));
    for (int32_t Index = 0; Index < (int32_t)GameKey_Count; Index++)
    {
        uint8_t State = self->Sample.KeyStates[Index];
        self->Sample.KeyStates[Index] = ((State & GAME_INPUT_KEY_STATE_RELEASED) != 0) ? 0U : (uint8_t)(State & GAME_INPUT_KEY_STATE_DOWN);
    }
    self->Sample.WheelDelta = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->Sample.TextCodepointCount = 0U;
}

void TestUI_Ticks(TestRun* run, TestUIFixture* self, size_t count)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        TestUI_Tick(run, self);
    }
}

void TestUI_Press(TestUIFixture* self, GameKey key)
{
    self->Sample.KeyStates[key] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
}

void TestUI_Release(TestUIFixture* self, GameKey key)
{
    self->Sample.KeyStates[key] = GAME_INPUT_KEY_STATE_RELEASED;
}

void TestUI_Repeat(TestUIFixture* self, GameKey key)
{
    self->Sample.KeyStates[key] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_REPEATED;
}

void TestUI_Tap(TestRun* run, TestUIFixture* self, GameKey key)
{
    TestUI_Press(self, key);
    TestUI_Tick(run, self);
    TestUI_Release(self, key);
    TestUI_Tick(run, self);
}

void TestUI_MovePointer(TestUIFixture* self, float x, float y)
{
    UIViewport Viewport = UIScreen_GetViewport(&self->Screen);
    self->ScreenInput.IsPointerPresent = true;
    self->ScreenInput.PointerPosition = (Vector2){ .x = x / Viewport.SizePixels.x, .y = y / Viewport.SizePixels.y };
}

void TestUI_RemovePointer(TestUIFixture* self)
{
    self->ScreenInput.IsPointerPresent = false;
}

void TestUI_Click(TestRun* run, TestUIFixture* self, float x, float y)
{
    TestUI_MovePointer(self, x, y);
    TestUI_Tick(run, self);
    TestUI_Press(self, GameKey_MouseLeft);
    TestUI_Tick(run, self);
    TestUI_Release(self, GameKey_MouseLeft);
    TestUI_Tick(run, self);
}

void TestUI_Wheel(TestUIFixture* self, float notches)
{
    self->Sample.WheelDelta = (Vector2){ .x = 0.0f, .y = notches };
}

void TestUI_SetSurface(TestRun* run, TestUIFixture* self, float width, float height)
{
    UIViewport Viewport = UIViewport_Create((Vector2){ .x = width, .y = height }, width / height, RenderTargetPosition_Centered());
    TEST_CHECK_SUCCESS(run, UIScreen_SetViewport(&self->Screen, &Viewport));
}

Rectangle TestUI_GetPixelBox(TestRun* run, TestUIFixture* self, uint64_t widgetId)
{
    Widget* Target = NULL;
    if (!TEST_CHECK(run, UIScreen_TryGetWidgetById(&self->Screen, widgetId, &Target)))
    {
        return (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f };
    }
    Vector2 TopLeft = UIScreen_WidgetToScreen(&self->Screen, Target, (Vector2){ .x = 0.0f, .y = 0.0f });
    Vector2 BottomRight = UIScreen_WidgetToScreen(&self->Screen, Target, (Vector2){ .x = 1.0f, .y = 1.0f });
    UIViewport Viewport = UIScreen_GetViewport(&self->Screen);
    return (Rectangle)
    {
        .x = TopLeft.x * Viewport.SizePixels.x,
        .y = TopLeft.y * Viewport.SizePixels.y,
        .width = (BottomRight.x - TopLeft.x) * Viewport.SizePixels.x,
        .height = (BottomRight.y - TopLeft.y) * Viewport.SizePixels.y
    };
}

bool TestUI_RecordEvents(TestRun* run, TestUIFixture* self, TestUIEvents* events)
{
    Memory_Zero(events, sizeof(*events));
    UserData Data = UserData_FromPointer(events);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    return TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&self->Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_Widget),
        RecordEvent, &Data, &Subscription));
}

bool TestUI_IsSamePixel(float a, float b)
{
    return fabsf(a - b) <= PIXEL_TOLERANCE;
}

bool TestUI_IsBox(Rectangle box, float x, float y, float width, float height)
{
    return TestUI_IsSamePixel(box.x, x) && TestUI_IsSamePixel(box.y, y) && TestUI_IsSamePixel(box.width, width)
        && TestUI_IsSamePixel(box.height, height);
}
