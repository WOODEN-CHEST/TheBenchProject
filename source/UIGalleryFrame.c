#include <stdio.h>
#include "UIGalleryFrame.h"
#include "Config.h"
#include "GameFrame.h"
#include "GameFrameManager.h"
#include "GameInput.h"
#include "Logger.h"
#include "Renderer.h"
#include "Services.h"
#include "TextComponentFactory.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "raylib/raylib.h"
#include "wr/WRCompile.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"


// Macros.
/** Debug name used in log and crash messages. */
#define FRAME_DEBUG_NAME ((const unsigned char*)u8"UIGallery")
/** The themes the T key goes through. */
#define THEME_COUNT ((size_t)2)
/** The scales the + and - keys go through. */
#define SCALE_COUNT ((size_t)6)
/** How many elements are named in the log. */
#define MAX_NAMED_ELEMENTS ((size_t)32)
/** Room for a status line. */
#define STATUS_CAPACITY ((size_t)160)
/** The background behind everything, opaque: the gallery has no world under it. */
#define BACKGROUND_COLOR ((Color){ 18, 19, 22, 255 })
/** Seconds between two logged Changed events: a drag posts one per tick at most, and the log is not per tick. */
#define CHANGED_LOG_PERIOD_SECONDS 0.25


// Types.
/* An element's name for the log. */
typedef struct NamedElementStruct
{
    uint64_t Id;
    const char* Name;
} NamedElement;

typedef struct UIGalleryFrameStruct
{
    /** Abstract base (must be first). */
    GameFrame Base;
    /** Shared services; borrowed. */
    Services* _services;
    TextComponentFactory _textFactory;
    UIWidgetFactory _widgetFactory;
    UIKit _kit;
    UIScreen _screen;
    /** Which of the four above are constructed (for teardown after a failed creation). */
    bool _isTextFactoryConstructed;
    bool _isWidgetFactoryConstructed;
    bool _isKitConstructed;
    bool _isScreenConstructed;

    size_t _themeIndex;
    size_t _scaleIndex;
    bool _isEventLogEnabled;
    uint64_t _statusId;
    double _time;
    double _lastChangedLogTime;
    NamedElement _names[MAX_NAMED_ELEMENTS];
    size_t _nameCount;
} UIGalleryFrame;

/* What building the page carries along; the first failure stops the rest. */
typedef struct GalleryBuildStruct
{
    UIGalleryFrame* Frame;
    Error Result;
} GalleryBuild;


// Fields.
static const unsigned char* const ThemeNames[THEME_COUNT] = { u8"tbp:test_flat", u8"tbp:test_nine_slice" };
static const float Scales[SCALE_COUNT] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f };
/* Text in every alphabet the font has, for looking at small sizes. */
static const unsigned char SampleText[] = u8"The quick brown fox jumps over the lazy dog 0123456789 āčēģīķļņšūž Ждём";
static const unsigned char LongText[] = u8"A label that wraps: this line is longer than the panel it stands in, so it breaks at a space and the stack makes room for the next line.";


// Static functions: building the page.
static void Remember(GalleryBuild* build, uint64_t id, const char* name)
{
    UIGalleryFrame* Frame = build->Frame;
    if ((name != NULL) && (Frame->_nameCount < MAX_NAMED_ELEMENTS))
    {
        Frame->_names[Frame->_nameCount] = (NamedElement){ .Id = id, .Name = name };
        Frame->_nameCount++;
    }
}

static bool HasFailed(const GalleryBuild* build)
{
    return build->Result.Code != ErrorCode_Success;
}

static uint64_t AddLabel(GalleryBuild* build, uint64_t parent, const unsigned char* text, const unsigned char* variant)
{
    uint64_t Id = WIDGET_ID_INVALID;
    if (!HasFailed(build))
    {
        UILabelOptions Options = UILabelOptions_Create(text);
        Options.Element.Variant = variant;
        Options.Element.Hint.Align = UIStackAlign_Start;
        build->Result = UIKit_CreateLabel(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
    }
    return Id;
}

static uint64_t AddRow(GalleryBuild* build, uint64_t parent, float spacing)
{
    uint64_t Id = WIDGET_ID_INVALID;
    if (!HasFailed(build))
    {
        UIStackOptions Options = UIStackOptions_Create(UIStackDirection_Row);
        Options.Layout.Spacing = spacing;
        build->Result = UIKit_CreateStack(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
    }
    return Id;
}

static uint64_t AddPanel(GalleryBuild* build, uint64_t parent, UIStackHint hint, const unsigned char* variant)
{
    uint64_t Id = WIDGET_ID_INVALID;
    if (!HasFailed(build))
    {
        UIPanelOptions Options = UIPanelOptions_Create();
        Options.Element.Hint = hint;
        Options.Element.Variant = variant;
        build->Result = UIKit_CreatePanel(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
    }
    return Id;
}

static uint64_t AddButton(GalleryBuild* build, uint64_t parent, const unsigned char* text, const char* name, bool isEnabled,
    bool isToggled, const unsigned char* variant, UIStackHint hint)
{
    uint64_t Id = WIDGET_ID_INVALID;
    if (!HasFailed(build))
    {
        UIButtonOptions Options = UIButtonOptions_Create(text);
        Options.IsEnabled = isEnabled;
        Options.IsToggled = isToggled;
        Options.Element.Variant = variant;
        Options.Element.Hint = hint;
        build->Result = UIKit_CreateButton(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
        Remember(build, Id, name);
    }
    return Id;
}

static void AddToggle(GalleryBuild* build, uint64_t parent, const unsigned char* text, const char* name, UIToggleState state, bool isEnabled)
{
    if (!HasFailed(build))
    {
        UIToggleOptions Options = UIToggleOptions_Create(text);
        Options.State = state;
        Options.IsEnabled = isEnabled;
        uint64_t Id = WIDGET_ID_INVALID;
        build->Result = UIKit_CreateToggle(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
        Remember(build, Id, name);
    }
}

static void AddSlider(GalleryBuild* build, uint64_t parent, const char* name, double value, bool hasStep, double step, bool isEnabled)
{
    if (!HasFailed(build))
    {
        UISliderOptions Options = UISliderOptions_Create();
        Options.Maximum = 100.0;
        Options.Value = value;
        Options.HasStep = hasStep;
        Options.Step = step;
        Options.IsEnabled = isEnabled;
        uint64_t Id = WIDGET_ID_INVALID;
        build->Result = UIKit_CreateSlider(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
        Remember(build, Id, name);
    }
}

/* The left panel: the elements, each in the states it can be put in. */
static void BuildElements(GalleryBuild* build, uint64_t page)
{
    uint64_t Panel = AddPanel(build, page, UIStackHint_Fill(1.0f), NULL);

    AddLabel(build, Panel, u8"Buttons", u8"heading");
    uint64_t Buttons = AddRow(build, Panel, 6.0f);
    AddButton(build, Buttons, u8"Normal", "the normal button", true, false, NULL, UIStackHint_Content());
    AddButton(build, Buttons, u8"Disabled", "the disabled button", false, false, NULL, UIStackHint_Content());
    AddButton(build, Buttons, u8"Toggled", "the toggled button", true, true, NULL, UIStackHint_Content());
    AddButton(build, Buttons, u8"Danger", "the danger button", true, false, u8"danger", UIStackHint_Content());
    uint64_t Wide = AddRow(build, Panel, 6.0f);
    AddButton(build, Wide, u8"A button filling what is left", "the filling button", true, false, NULL, UIStackHint_Fill(1.0f));
    AddButton(build, Wide, u8"Fixed 90", "the fixed button", true, false, NULL, UIStackHint_Fixed(90.0f));

    AddLabel(build, Panel, u8"Toggles", u8"heading");
    uint64_t Toggles = AddRow(build, Panel, 14.0f);
    AddToggle(build, Toggles, u8"Off", "the off toggle", UIToggleState_Off, true);
    AddToggle(build, Toggles, u8"On", "the on toggle", UIToggleState_On, true);
    AddToggle(build, Toggles, u8"Mixed", "the mixed toggle", UIToggleState_Mixed, true);
    AddToggle(build, Toggles, u8"Disabled", "the disabled toggle", UIToggleState_On, false);

    AddLabel(build, Panel, u8"Sliders: free, in steps of 10, disabled", u8"heading");
    AddSlider(build, Panel, "the free slider", 35.0, false, 0.0, true);
    AddSlider(build, Panel, "the stepped slider", 60.0, true, 10.0, true);
    AddSlider(build, Panel, "the disabled slider", 80.0, false, 0.0, false);

    AddLabel(build, Panel, u8"Tab moves, the arrows too, Enter or Space clicks; the wheel moves a focused slider.", u8"hint");
}

/* The right panel: how stacks size and align their children, and text at small sizes. */
static void BuildLayouts(GalleryBuild* build, uint64_t page)
{
    uint64_t Panel = AddPanel(build, page, UIStackHint_Fill(1.0f), NULL);

    AddLabel(build, Panel, u8"Along a row: fixed 70, content, fill 1, fill 2", u8"heading");
    uint64_t Sizes = AddRow(build, Panel, 4.0f);
    AddLabel(build, AddPanel(build, Sizes, UIStackHint_Fixed(70.0f), u8"inset"), u8"70", NULL);
    AddButton(build, Sizes, u8"Content", NULL, true, false, NULL, UIStackHint_Content());
    AddLabel(build, AddPanel(build, Sizes, UIStackHint_Fill(1.0f), u8"inset"), u8"1", NULL);
    AddLabel(build, AddPanel(build, Sizes, UIStackHint_Fill(2.0f), u8"inset"), u8"2", NULL);

    AddLabel(build, Panel, u8"Across a row: start, centre, end, stretch", u8"heading");
    UIStackHint RowHint = UIStackHint_Fixed(64.0f);
    uint64_t Across = AddPanel(build, Panel, RowHint, u8"bare");
    uint64_t Aligned = AddRow(build, Across, 4.0f);
    if (!HasFailed(build))
    {
        UIStackHint Fill = UIStackHint_Fill(1.0f);
        build->Result = StackWidget_SetChildHint(&build->Frame->_screen, Aligned, &Fill);
    }
    const UIStackAlign Aligns[] = { UIStackAlign_Start, UIStackAlign_Center, UIStackAlign_End, UIStackAlign_Stretch };
    const unsigned char* const AlignNames[] = { u8"Start", u8"Centre", u8"End", u8"Stretch" };
    for (size_t Index = 0U; Index < (sizeof(Aligns) / sizeof(Aligns[0])); Index++)
    {
        UIStackHint Hint = UIStackHint_Fill(1.0f);
        Hint.Align = Aligns[Index];
        AddButton(build, Aligned, AlignNames[Index], NULL, true, false, NULL, Hint);
    }

    AddLabel(build, Panel, u8"Text at 13, 14 and 16 UI pixels", u8"heading");
    const float TextSizes[] = { 13.0f, 14.0f, 16.0f };
    for (size_t Index = 0U; (Index < (sizeof(TextSizes) / sizeof(TextSizes[0]))) && !HasFailed(build); Index++)
    {
        UILabelOptions Options = UILabelOptions_Create(SampleText);
        Options.HasTextSize = true;
        Options.TextSize = TextSizes[Index];
        Options.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Id = WIDGET_ID_INVALID;
        build->Result = UIKit_CreateLabel(&build->Frame->_kit, &build->Frame->_screen, Panel, &Options, &Id);
    }
    if (!HasFailed(build))
    {
        UILabelOptions Options = UILabelOptions_Create(LongText);
        Options.IsWrapped = true;
        Options.Element.Hint.Align = UIStackAlign_Stretch;
        Options.Element.Variant = u8"hint";
        uint64_t Id = WIDGET_ID_INVALID;
        build->Result = UIKit_CreateLabel(&build->Frame->_kit, &build->Frame->_screen, Panel, &Options, &Id);
    }
}

/* The page: a heading, two panels side by side, and a status line. */
static Error BuildPage(UIGalleryFrame* frame)
{
    GalleryBuild Build = { .Frame = frame, .Result = Error_CreateSuccess() };
    UIStackOptions PageOptions = UIStackOptions_Create(UIStackDirection_Column);
    PageOptions.Layout.Padding = UIInsets_All(12.0f);
    PageOptions.Layout.Spacing = 8.0f;
    PageOptions.Element.HasPlacement = true;
    PageOptions.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
    uint64_t Page = WIDGET_ID_INVALID;
    Build.Result = UIKit_CreateStack(&frame->_kit, &frame->_screen, WIDGET_ID_INVALID, &PageOptions, &Page);

    AddLabel(&Build, Page, u8"UI gallery", u8"heading");
    AddLabel(&Build, Page, u8"T the theme, + and - the scale, L the event log, Escape quits.", u8"hint");
    uint64_t Panels = AddRow(&Build, Page, 8.0f);
    if (!HasFailed(&Build))
    {
        UIStackHint Fill = UIStackHint_Fill(1.0f);
        Build.Result = StackWidget_SetChildHint(&frame->_screen, Panels, &Fill);
    }
    BuildElements(&Build, Panels);
    BuildLayouts(&Build, Panels);
    frame->_statusId = AddLabel(&Build, Page, u8"Nothing yet.", u8"hint");
    return Build.Result;
}


// Static functions: reacting.
static const char* GetElementName(const UIGalleryFrame* frame, uint64_t id)
{
    for (size_t Index = 0U; Index < frame->_nameCount; Index++)
    {
        if (frame->_names[Index].Id == id)
        {
            return frame->_names[Index].Name;
        }
    }
    return "an element";
}

/* Shows each event in the status line, and logs it (a drag's Changed events at most a few times a second). */
static Error OnElementEvent(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    UIGalleryFrame* Frame = UserData_GetPointer(userData);
    static const char* const EventNames[] = { "clicked", "changed", "committed", "cancelled" };
    bool IsKnown = (event->WidgetEventId >= 0) && ((size_t)event->WidgetEventId < (sizeof(EventNames) / sizeof(EventNames[0])));
    char Value[32] = "";
    if (event->HasValue && (event->Value.Type == UIPropertyType_Double))
    {
        snprintf(Value, sizeof(Value), ": %.3f", event->Value.Data.DoubleValue);
    }
    else if (event->HasValue && (event->Value.Type == UIPropertyType_Bool))
    {
        snprintf(Value, sizeof(Value), ": %s", event->Value.Data.BoolValue ? "on" : "off");
    }
    char Status[STATUS_CAPACITY];
    snprintf(Status, sizeof(Status), "%s %s%s", GetElementName(Frame, event->WidgetId), IsKnown ? EventNames[event->WidgetEventId] : "posted", Value);
    Error Result = UILabel_SetText(screen, Frame->_statusId, (const unsigned char*)Status);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    bool IsChanged = event->WidgetEventId == (int32_t)UIElementEvent_Changed;
    bool IsDue = !IsChanged || ((Frame->_time - Frame->_lastChangedLogTime) >= CHANGED_LOG_PERIOD_SECONDS);
    if (Frame->_isEventLogEnabled && IsDue && (Frame->_services->Logger != NULL))
    {
        Frame->_lastChangedLogTime = IsChanged ? Frame->_time : Frame->_lastChangedLogTime;
        Error LogResult = Logger_LogInfoFormatted(Frame->_services->Logger, (const unsigned char*)u8"UIGallery: %s", Status);
        Error_Deconstruct(&LogResult);
    }
    return Error_CreateSuccess();
}

static void LogLine(const UIGalleryFrame* frame, const char* format, const char* text)
{
    if (frame->_services->Logger != NULL)
    {
        char Line[STATUS_CAPACITY];
        snprintf(Line, sizeof(Line), format, text);
        Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger, (const unsigned char*)u8"UIGallery: %s", Line);
        Error_Deconstruct(&LogResult);
    }
}

/* The frame's own keys, from what the UI left unconsumed. */
static Error HandleKeys(UIGalleryFrame* frame, GameInput* input)
{
    if (GameInput_IsKeyPressed(input, GameKey_T))
    {
        frame->_themeIndex = (frame->_themeIndex + 1U) % THEME_COUNT;
        Error Result = UIScreen_SetTheme(&frame->_screen, frame->_services->Assets, ThemeNames[frame->_themeIndex]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        LogLine(frame, "the theme is %s.", (const char*)ThemeNames[frame->_themeIndex]);
    }
    bool IsScaleUp = GameInput_IsKeyPressed(input, GameKey_Equal) || GameInput_IsKeyPressed(input, GameKey_KeypadAdd);
    bool IsScaleDown = GameInput_IsKeyPressed(input, GameKey_Minus) || GameInput_IsKeyPressed(input, GameKey_KeypadSubtract);
    if ((IsScaleUp && ((frame->_scaleIndex + 1U) < SCALE_COUNT)) || (IsScaleDown && (frame->_scaleIndex > 0U)))
    {
        frame->_scaleIndex = IsScaleUp ? (frame->_scaleIndex + 1U) : (frame->_scaleIndex - 1U);
        Error Result = UIScreen_SetScale(&frame->_screen, Scales[frame->_scaleIndex]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    if (GameInput_IsKeyPressed(input, GameKey_L))
    {
        frame->_isEventLogEnabled = !frame->_isEventLogEnabled;
        LogLine(frame, "the event log is %s.", frame->_isEventLogEnabled ? "on" : "off");
    }
    if (GameInput_IsActionPressed(input, GameAction_Pause))
    {
        GameFrameManager_RequestStop(frame->_services->FrameManager);
    }
    return Error_CreateSuccess();
}


// Static functions: vtable behavior.
static Error UIGalleryFrame_Start(void* self, ProgramTime time)
{
    UNUSED(time);
    LogLine(self, "%s", "every element in every state. T the theme, + and - the scale, L the event log, Escape quits.");
    return Error_CreateSuccess();
}

static Error UIGalleryFrame_End(void* self, ProgramTime time)
{
    UNUSED(self);
    UNUSED(time);
    return Error_CreateSuccess();
}

static Error UIGalleryFrame_Update(void* self, ProgramTime time)
{
    UIGalleryFrame* Frame = self;
    GameInput* Input = Frame->_services->Input;
    Frame->_time = time.TotalTime;
    if (Input != NULL)
    {
        Vector2 WindowSize = GameInput_GetWindowSize(Input);
        if ((WindowSize.x > 0.0f) && (WindowSize.y > 0.0f))
        {
            UIViewport Viewport = UIViewport_Create(WindowSize, GameFrameManager_GetTargetAspectRatio(Frame->_services->FrameManager),
                GameFrameManager_GetTargetAreaPosition(Frame->_services->FrameManager));
            Error ViewportResult = UIScreen_SetViewport(&Frame->_screen, &Viewport);
            if (ViewportResult.Code != ErrorCode_Success)
            {
                return ViewportResult;
            }
        }
    }
    UIScreenInput ScreenInput = UIScreenInput_CreateFromWindow(Input);
    Error Result = UIScreen_Update(&Frame->_screen, time, &ScreenInput);
    if ((Result.Code != ErrorCode_Success) || (Input == NULL))
    {
        return Result;
    }
    return HandleKeys(Frame, Input);
}

static Error UIGalleryFrame_NoStep(void* self)
{
    UNUSED(self);
    return Error_CreateSuccess();
}

static bool UIGalleryFrame_IsDone(void* self)
{
    UNUSED(self);
    return true;
}

static Error UIGalleryFrame_Render(void* self, const FrameRenderContext* context, RenderTexture2D target)
{
    UIGalleryFrame* Frame = self;
    RenderContext Context;
    RenderContext_Create(&Context, &target, context->TargetAspectRatio, context->TargetAreaPosition);
    RenderContext_BeginRendering(&Context);
    ClearBackground(BACKGROUND_COLOR);
    Error Result = UIScreen_Render(&Frame->_screen, &Context);
    RenderContext_EndRendering(&Context);
    RenderContext_Deconstruct(&Context);
    return Result;
}

/* Tears down what is constructed: the screen (it destroys the elements), the kit, then the factories. */
static void DestroyFrameContents(UIGalleryFrame* frame)
{
    if (frame->_isScreenConstructed)
    {
        Error Result = UIScreen_Deconstruct(&frame->_screen);
        Error_Deconstruct(&Result);
    }
    if (frame->_isKitConstructed)
    {
        Error Result = UIKit_Deconstruct(&frame->_kit);
        Error_Deconstruct(&Result);
    }
    if (frame->_isWidgetFactoryConstructed)
    {
        Error Result = UIWidgetFactory_Deconstruct(&frame->_widgetFactory);
        Error_Deconstruct(&Result);
    }
    if (frame->_isTextFactoryConstructed)
    {
        Error Result = TextComponentFactory_Deconstruct(&frame->_textFactory);
        Error_Deconstruct(&Result);
    }
}

static void UIGalleryFrame_Destroy(void* self)
{
    UIGalleryFrame* Frame = self;
    DestroyFrameContents(Frame);
    GameFrame_Deconstruct(&Frame->Base);
    Memory_Free(Frame);
}


// Fields.
static const GameFrameVTable UIGalleryFrameVTable =
{
    .Start = UIGalleryFrame_Start,
    .End = UIGalleryFrame_End,
    .Update = UIGalleryFrame_Update,
    .BeginLoad = UIGalleryFrame_NoStep,
    .LoadStep = UIGalleryFrame_NoStep,
    .IsLoaded = UIGalleryFrame_IsDone,
    .BeginUnload = UIGalleryFrame_NoStep,
    .UnloadStep = UIGalleryFrame_NoStep,
    .IsUnloaded = UIGalleryFrame_IsDone,
    .PrepareRender = NULL,
    .Render = UIGalleryFrame_Render,
    .GetLoadProgress = NULL,
    .OnResize = NULL,
    .Destroy = UIGalleryFrame_Destroy
};


// Static functions: construction.
/* The scale in the list nearest to the player's, which the keys step from. */
static size_t FindScaleIndex(float scale)
{
    size_t Best = 1U;
    for (size_t Index = 0U; Index < SCALE_COUNT; Index++)
    {
        float Distance = Scales[Index] - scale;
        float BestDistance = Scales[Best] - scale;
        if ((Distance * Distance) < (BestDistance * BestDistance))
        {
            Best = Index;
        }
    }
    return Best;
}

static Error BuildFrameContents(UIGalleryFrame* frame)
{
    Services* Shared = frame->_services;
    Error Result = TextComponentFactory_Construct(&frame->_textFactory);
    frame->_isTextFactoryConstructed = (Result.Code == ErrorCode_Success);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWidgetFactory_Construct(&frame->_widgetFactory);
        frame->_isWidgetFactoryConstructed = (Result.Code == ErrorCode_Success);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_Construct(&frame->_kit, &frame->_widgetFactory, &frame->_textFactory, Shared->Assets, Shared->Logger);
        frame->_isKitConstructed = (Result.Code == ErrorCode_Success);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_Construct(&frame->_screen, &frame->_widgetFactory, Shared->Logger);
        frame->_isScreenConstructed = (Result.Code == ErrorCode_Success);
    }
    if (Result.Code == ErrorCode_Success)
    {
        float Scale = (Shared->Config != NULL) ? Shared->Config->UIScale : 1.0f;
        frame->_scaleIndex = FindScaleIndex(Scale);
        Result = UIScreen_SetScale(&frame->_screen, Scale);
    }
    if ((Result.Code == ErrorCode_Success) && (Shared->Assets != NULL))
    {
        Result = UIScreen_SetTheme(&frame->_screen, Shared->Assets, ThemeNames[frame->_themeIndex]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UserData FrameData = UserData_FromPointer(frame);
        uint64_t Subscription = UI_SCREEN_ID_INVALID;
        Result = UIScreen_Subscribe(&frame->_screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnElementEvent, &FrameData, &Subscription);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = BuildPage(frame);
    }
    return Result;
}


// Public functions.
Error UIGalleryFrame_Create(Services* services, GameFrame** outFrame)
{
    if ((services == NULL) || (outFrame == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIGalleryFrame_Create: services and outFrame must not be NULL.");
    }
    *outFrame = NULL;

    UIGalleryFrame* Frame = Memory_Allocate(sizeof(UIGalleryFrame));
    Memory_Zero(Frame, sizeof(*Frame));
    Frame->_services = services;
    Frame->_isEventLogEnabled = true;
    Error Result = GameFrame_Construct(&Frame->Base, &UIGalleryFrameVTable, FRAME_DEBUG_NAME);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Frame);
        return Result;
    }
    Result = BuildFrameContents(Frame);
    if (Result.Code != ErrorCode_Success)
    {
        DestroyFrameContents(Frame);
        GameFrame_Deconstruct(&Frame->Base);
        Memory_Free(Frame);
        return Result;
    }

    *outFrame = &Frame->Base;
    return Error_CreateSuccess();
}
