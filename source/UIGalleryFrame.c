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
#include "GameKey.h"
#include "StackWidget.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "UITooltip.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "UIWindowHost.h"
#include "raylib/raylib.h"
#include "wr/WRCompile.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"


// Macros.
/** Debug name used in log and crash messages. */
#define FRAME_DEBUG_NAME ((const unsigned char*)u8"UIGallery")
/** The themes the T key goes through. */
#define THEME_COUNT ((size_t)3)
/** The scales the + and - keys go through. */
#define SCALE_COUNT ((size_t)6)
/** How many elements are named in the log. */
#define MAX_NAMED_ELEMENTS ((size_t)80)
/** Room for a status line. */
#define STATUS_CAPACITY ((size_t)160)
/** The background behind everything, opaque: the gallery has no world under it. */
#define BACKGROUND_COLOR ((Color){ 18, 19, 22, 255 })
/** Seconds between two logged Changed events: a drag posts one per tick at most, and the log is not per tick. */
#define CHANGED_LOG_PERIOD_SECONDS 0.25
/** The test row's box that asks for a hidden-cursor drag: how far the pointer goes before it does. */
#define SCRUB_START_PIXELS 3.0f
/** The keyboard box's width, in UI pixels. */
#define KEYBOARD_BOX_WIDTH 320.0f
/** How many pages the gallery has (N steps through them). */
#define PAGE_COUNT ((size_t)2)
/** How long the turning panel takes for a whole turn, and how far the slide-in starts, in UI pixels, and how long it takes. */
#define TURN_SECONDS 20.0
#define SLIDE_DISTANCE 360.0f
#define SLIDE_SECONDS 0.45
/** The diamond look's size, in its own UI pixels. */
#define DIAMOND_SIZE 28.0f
#define PI_F 3.14159265f


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
    /** The UI's host: the window's size, the scale policy and the player's scale. */
    UIWindowHost _host;
    /** Whether the UI follows the window's height (the P key's comparison) instead of keeping its pixels. */
    bool _isFollowingHeight;
    /** Which of the four above are constructed (for teardown after a failed creation). */
    bool _isTextFactoryConstructed;
    bool _isWidgetFactoryConstructed;
    bool _isKitConstructed;
    bool _isScreenConstructed;

    size_t _themeIndex;
    size_t _scaleIndex;
    /** The pages (roots), which N shows one at a time, and the one shown. */
    uint64_t _pageIds[PAGE_COUNT];
    size_t _pageIndex;
    /** The turning panel and the panel that slides in. */
    uint64_t _turningId;
    uint64_t _slideId;
    bool _isEventLogEnabled;
    uint64_t _wheelStackType;
    uint64_t _scrubBoxType;
    /** Each page's status line, all kept saying the same. */
    uint64_t _statusIds[PAGE_COUNT];
    /** The button that slides the slide-in panel in again. */
    uint64_t _slideButtonId;
    double _time;
    double _lastChangedLogTime;
    NamedElement _names[MAX_NAMED_ELEMENTS];
    size_t _nameCount;
    /** The elements given a hover text of their own; the rest named get one saying what they are. */
    uint64_t _describedIds[MAX_NAMED_ELEMENTS];
    size_t _describedCount;
} UIGalleryFrame;

/* A stack that takes the wheel, as a scroll area does: its children's wheel comes up to it. */
typedef struct WheelStackWidgetStruct
{
    Widget Base;
    UIStackLayout Layout;
} WheelStackWidget;

/* A box to drag: it asks for a hidden-cursor drag once the pointer has gone a few pixels, and shows how far the raw motion went.
 * The property box's scrub stands on the same request. */
typedef struct ScrubBoxWidgetStruct
{
    Widget Base;
    UIGalleryFrame* Frame;
    /* The left button holds a press on it, how far it moved before asking, and the raw motion since. */
    bool IsHeld;
    float MovedPixels;
    float ScrubbedPixels;
} ScrubBoxWidget;

/* What building the page carries along; the first failure stops the rest. */
typedef struct GalleryBuildStruct
{
    UIGalleryFrame* Frame;
    Error Result;
} GalleryBuild;


// Fields.
static const unsigned char* const ThemeNames[THEME_COUNT] = { u8"tbp:test_flat", u8"tbp:test_nine_slice", u8"tbp:test_light" };
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

/* A label that wraps to the width it is given. */
static void AddWrappedLabel(GalleryBuild* build, uint64_t parent, const unsigned char* text, const unsigned char* variant)
{
    if (!HasFailed(build))
    {
        UILabelOptions Options = UILabelOptions_Create(text);
        Options.IsWrapped = true;
        Options.Element.Hint.Align = UIStackAlign_Stretch;
        Options.Element.Variant = variant;
        uint64_t Id = WIDGET_ID_INVALID;
        build->Result = UIKit_CreateLabel(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
    }
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
        Options.HasDefault = true;
        Options.Default = value;
        uint64_t Id = WIDGET_ID_INVALID;
        build->Result = UIKit_CreateSlider(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
        Remember(build, Id, name);
    }
}

/* Puts a text in every page's status line. */
static Error SetStatus(UIGalleryFrame* frame, const char* text)
{
    Error First = Error_CreateSuccess();
    for (size_t Index = 0U; Index < PAGE_COUNT; Index++)
    {
        if (frame->_statusIds[Index] == WIDGET_ID_INVALID)
        {
            continue;
        }
        Error Result = UILabel_SetText(&frame->_screen, frame->_statusIds[Index], (const unsigned char*)text);
        if (First.Code == ErrorCode_Success)
        {
            First = Result;
        }
        else
        {
            Error_Deconstruct(&Result);
        }
    }
    return First;
}

/* Sets a status line and logs it. */
static void ReportScrub(UIGalleryFrame* frame, const char* what, float pixels)
{
    char Status[STATUS_CAPACITY];
    snprintf(Status, sizeof(Status), "the scrub box %s: %.0f pixels", what, (double)pixels);
    Error Result = SetStatus(frame, Status);
    Error_Deconstruct(&Result);
    if ((frame->_services->Logger != NULL) && frame->_isEventLogEnabled)
    {
        Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger, (const unsigned char*)u8"UIGallery: %s", Status);
        Error_Deconstruct(&LogResult);
    }
}

static Error WheelStack_Layout(void* self, const UILayoutContext* context)
{
    WheelStackWidget* Stack = self;
    return UIStackLayout_Arrange(&Stack->Layout, &Stack->Base, context);
}

static Error WheelStack_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    WheelStackWidget* Stack = self;
    return UIStackLayout_Measure(&Stack->Layout, &Stack->Base, context, availableWidth, outSize);
}

/* It takes every wheel that reaches it, and reports it as an element does. */
static Error WheelStack_OnScrollInput(void* self, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    WheelStackWidget* Stack = self;
    *outIsHandled = true;
    UIEventMouse Mouse = { .Button = GameKey_Unknown, .Modifiers = args->Modifiers, .ScreenPosition = args->ScreenPosition,
        .ScrollDelta = args->ScrollDelta, .IsOverWidget = true };
    return UIScreen_PostWidgetMouseEvent(Widget_GetScreen(&Stack->Base), Widget_GetId(&Stack->Base), UIElementEvent_Scrolled, &Mouse);
}

static Error WheelStack_Construct(void* widget, void* args)
{
    WheelStackWidget* Stack = widget;
    Stack->Layout = *(const UIStackLayout*)args;
    return Error_CreateSuccess();
}

static const WidgetVTable WheelStackVTable =
{
    .Layout = WheelStack_Layout,
    .Measure = WheelStack_Measure,
    .OnScrollInput = WheelStack_OnScrollInput
};

static Error ScrubBox_Construct(void* widget, void* args)
{
    ScrubBoxWidget* Box = widget;
    Box->Frame = args;
    return Error_CreateSuccess();
}

static Error ScrubBox_Render(void* self, UIRenderContext* context)
{
    const ScrubBoxWidget* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    UIRenderContext_RenderPixelRectangle(context, Pixels, Box->IsHeld ? (Color){ 96, 120, 170, 255 } : (Color){ 70, 74, 84, 255 });
    return Error_CreateSuccess();
}

static Error ScrubBox_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    ScrubBoxWidget* Box = self;
    if (args->Button != GameKey_MouseLeft)
    {
        return Error_CreateSuccess();
    }
    UIScreen* Screen = Widget_GetScreen(&Box->Base);
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
            Box->IsHeld = true;
            Box->MovedPixels = 0.0f;
            Box->ScrubbedPixels = 0.0f;
            return Error_CreateSuccess();
        case UIMouseInputType_Move:
            if (UIScreen_IsPointerCaptured(Screen))
            {
                Box->ScrubbedPixels += args->DeltaPixels.x;
                return Error_CreateSuccess();
            }
            Box->MovedPixels += args->DeltaPixels.x;
            return ((Box->MovedPixels >= SCRUB_START_PIXELS) || (Box->MovedPixels <= -SCRUB_START_PIXELS))
                ? UIScreen_CapturePointer(Screen, Widget_GetId(&Box->Base)) : Error_CreateSuccess();
        case UIMouseInputType_ButtonRelease:
            Box->IsHeld = false;
            ReportScrub(Box->Frame, "went", Box->ScrubbedPixels);
            return Error_CreateSuccess();
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

static Error ScrubBox_OnPressLost(void* self, GameKey button)
{
    ScrubBoxWidget* Box = self;
    Box->IsHeld = Box->IsHeld && (button != GameKey_MouseLeft);
    return Error_CreateSuccess();
}

static const WidgetVTable ScrubBoxVTable =
{
    .Render = ScrubBox_Render,
    .OnMouseInput = ScrubBox_OnMouseInput,
    .OnPressLost = ScrubBox_OnPressLost
};

/* A widget of the gallery's own, made whole and added. */
static uint64_t AddOwnWidget(GalleryBuild* build, uint64_t parent, uint64_t type, void* args, UIStackHint hint)
{
    uint64_t Id = WIDGET_ID_INVALID;
    if (HasFailed(build))
    {
        return Id;
    }
    UIScreen* Screen = &build->Frame->_screen;
    Widget* Created = NULL;
    Widget* Parent = NULL;
    build->Result = UIWidgetFactory_CreateWidget(&build->Frame->_widgetFactory, Screen, type, args, &Created);
    if (HasFailed(build))
    {
        return Id;
    }
    Id = Widget_GetId(Created);
    build->Result = Widget_SetStackHint(Created, &hint);
    if (!HasFailed(build))
    {
        build->Result = Widget_InitializeWidget(Created);
    }
    if (!HasFailed(build) && UIScreen_TryGetWidgetById(Screen, parent, &Parent))
    {
        build->Result = Widget_AddSubWidget(Parent, Created);
    }
    return Id;
}

/* A button for one click mode. */
static uint64_t AddClickModeButton(GalleryBuild* build, uint64_t parent, const unsigned char* text, const char* name, UIClickMode mode)
{
    uint64_t Id = WIDGET_ID_INVALID;
    if (!HasFailed(build))
    {
        UIButtonOptions Options = UIButtonOptions_Create(text);
        Options.ClickMode = mode;
        Options.ActivationButtons = UI_BUTTON_BIT(GameKey_MouseLeft) | UI_BUTTON_BIT(GameKey_MouseMiddle);
        build->Result = UIKit_CreateButton(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
        Remember(build, Id, name);
    }
    return Id;
}

/* The routing's own row: a button per click mode, the wheel going up the tree, and a box per pointer shape. */
static void BuildRouting(GalleryBuild* build, uint64_t panel)
{
    AddLabel(build, panel, u8"Click modes: press and drag off, press elsewhere and release on it", u8"heading");
    uint64_t Modes = AddRow(build, panel, 6.0f);
    AddClickModeButton(build, Modes, u8"On press", "the on press button", UIClickMode_OnPress);
    AddClickModeButton(build, Modes, u8"On release", "the on release button", UIClickMode_OnRelease);
    AddClickModeButton(build, Modes, u8"Press and release", "the press and release button", UIClickMode_OnPressAndRelease);

    AddLabel(build, panel, u8"The wheel: this row takes it, the last button takes its own", u8"heading");
    UIStackLayout WheelLayout = { .Direction = UIStackDirection_Row, .Padding = UIInsets_All(0.0f), .Spacing = 6.0f };
    uint64_t Wheel = AddOwnWidget(build, panel, build->Frame->_wheelStackType, &WheelLayout, UIStackHint_Content());
    Remember(build, Wheel, "the wheel stack");
    AddButton(build, Wheel, u8"In the stack", "a button in the stack", true, false, NULL, UIStackHint_Content());
    AddButton(build, Wheel, u8"In it too", "a second button in the stack", true, false, NULL, UIStackHint_Content());
    if (!HasFailed(build))
    {
        UIButtonOptions Options = UIButtonOptions_Create(u8"Takes the wheel");
        Options.TakesWheel = true;
        uint64_t Id = WIDGET_ID_INVALID;
        build->Result = UIKit_CreateButton(&build->Frame->_kit, &build->Frame->_screen, Wheel, &Options, &Id);
        Remember(build, Id, "the button taking the wheel");
    }

    AddLabel(build, panel, u8"Pointer shapes, and a box to drag with the cursor hidden", u8"heading");
    uint64_t Shapes = AddRow(build, panel, 4.0f);
    const UICursorShape ShapeList[] = { UICursorShape_IBeam, UICursorShape_ResizeHorizontal, UICursorShape_ResizeVertical, UICursorShape_ResizeDiagonalDown,
        UICursorShape_ResizeDiagonalUp, UICursorShape_Move, UICursorShape_Hand, UICursorShape_NotAllowed };
    const unsigned char* const ShapeNames[] = { u8"I", u8"< >", u8"^ v", u8"\\", u8"/", u8"+", u8"Hand", u8"No" };
    for (size_t Index = 0U; (Index < (sizeof(ShapeList) / sizeof(ShapeList[0]))) && !HasFailed(build); Index++)
    {
        uint64_t Id = AddButton(build, Shapes, ShapeNames[Index], NULL, true, false, NULL, UIStackHint_Content());
        Widget* Base = NULL;
        if (!HasFailed(build) && UIScreen_TryGetWidgetById(&build->Frame->_screen, Id, &Base))
        {
            build->Result = Widget_SetCursorShape(Base, ShapeList[Index]);
        }
    }
    uint64_t Scrub = AddOwnWidget(build, Shapes, build->Frame->_scrubBoxType, build->Frame, UIStackHint_Fill(1.0f));
    Widget* ScrubBase = NULL;
    if (!HasFailed(build) && UIScreen_TryGetWidgetById(&build->Frame->_screen, Scrub, &ScrubBase))
    {
        build->Result = Widget_SetCursorShape(ScrubBase, UICursorShape_ResizeHorizontal);
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

    AddLabel(build, Panel, u8"Sliders: free, in steps of 10, disabled. Backspace over one: back to its default", u8"heading");
    AddSlider(build, Panel, "the free slider", 35.0, false, 0.0, true);
    AddSlider(build, Panel, "the stepped slider", 60.0, true, 10.0, true);
    AddSlider(build, Panel, "the disabled slider", 80.0, false, 0.0, false);

    BuildRouting(build, Panel);

    AddLabel(build, Panel, u8"Tab moves, the arrows too; Enter or Space clicks; the wheel moves a slider once it is entered, or with Ctrl.", u8"hint");
}

/* The keyboard box: a button, two sliders and a toggle, to try Tab, the arrows and Enter on. The box is focusable and holds focusable
 * elements, so it is a group: navigation reaches it as one, Enter goes in and Escape comes back out, and inside it Tab and the arrows
 * stay among its elements (the page's reading order would mix them with the other panel's). A slider is an edit element: moving onto
 * it changes nothing, and Enter or a click enters it. */
static void BuildKeyboard(GalleryBuild* build, uint64_t panel)
{
    AddLabel(build, panel, u8"Keyboard: a box to go into with Enter; the arrows move past its sliders without changing them", u8"heading");
    uint64_t Box = WIDGET_ID_INVALID;
    if (!HasFailed(build))
    {
        UIPanelOptions Options = UIPanelOptions_Create();
        Options.HasSpacing = true;
        Options.Spacing = 6.0f;
        Options.Element.Variant = u8"inset";
        Options.Element.Hint.Align = UIStackAlign_Start;
        Options.Element.Hint.HasCrossSize = true;
        Options.Element.Hint.CrossSize = KEYBOARD_BOX_WIDTH;
        build->Result = UIKit_CreatePanel(&build->Frame->_kit, &build->Frame->_screen, panel, &Options, &Box);
        Remember(build, Box, "the keyboard box");
    }
    Widget* BoxBase = NULL;
    if (!HasFailed(build) && UIScreen_TryGetWidgetById(&build->Frame->_screen, Box, &BoxBase))
    {
        build->Result = Widget_SetIsFocusable(BoxBase, true);
    }
    AddButton(build, Box, u8"A button: Enter presses it at once", "the keyboard box's button", true, false, NULL, UIStackHint_Content());
    AddSlider(build, Box, "the keyboard box's first slider", 30.0, false, 0.0, true);
    AddSlider(build, Box, "the keyboard box's second slider", 70.0, true, 10.0, true);
    AddToggle(build, Box, u8"A toggle", "the keyboard box's toggle", UIToggleState_Off, true);
    AddWrappedLabel(build, panel, u8"Enter or a click enters a slider: its thumb takes its entered colour, and all four arrows are its own, each press at "
        u8"once. Escape or Enter leaves it; Tab leaves it and enters the next slider, and only focuses a button. Escape again leaves the box.", u8"hint");
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
    AddWrappedLabel(build, Panel, LongText, u8"hint");

    BuildKeyboard(build, Panel);
}

static void BuildLooksPage(GalleryBuild* build);
static void DescribeEverything(GalleryBuild* build);

/* The first page: a heading, two panels side by side, and a status line; then the second page, and hover text on everything. */
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
    AddLabel(&Build, Page, u8"T the theme, + and - the scale, P the scale policy, L the event log. Escape steps out, and quits once nothing is focused.", u8"hint");
    uint64_t Panels = AddRow(&Build, Page, 8.0f);
    if (!HasFailed(&Build))
    {
        UIStackHint Fill = UIStackHint_Fill(1.0f);
        Build.Result = StackWidget_SetChildHint(&frame->_screen, Panels, &Fill);
    }
    BuildElements(&Build, Panels);
    BuildLayouts(&Build, Panels);
    frame->_statusIds[0] = AddLabel(&Build, Page, u8"Nothing yet.", u8"hint");
    frame->_pageIds[0] = Page;
    BuildLooksPage(&Build);
    DescribeEverything(&Build);
    return Build.Result;
}


// Static functions: the second page.
/* A look of the button with Render and Measure alone: a diamond in the theme's button colours for the state the button is in, with no
 * fade (it has no Update) and no text. It does not keep a UIElementLook, so it does not declare the look capability. */
typedef struct DiamondLookStruct
{
    UIButtonLookBase Base;
} DiamondLook;

static Error Diamond_Render(void* self, UIRenderContext* context)
{
    DiamondLook* Look = self;
    Rectangle Box = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Box))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = UITheme_FindStyle(UIScreen_GetTheme(Widget_GetScreen(&Look->Base.Base)), UIElementKind_Button, NULL);
    const UIBoxStyle* Background = UIThemeStyle_GetBox(Style, UIButtonPart_Background);
    const UIStateColors* Text = UIThemeStyle_GetColors(Style, UIButtonPart_Text);
    UIElementState State = UIButtonCapability_GetState(&Look->Base.Button, &Look->Base.Base);
    Color Fill = (Background != NULL) ? UIStateColors_Resolve(&Background->Colors, State, GRAY) : GRAY;
    Color Mark = (Text != NULL) ? UIStateColors_Resolve(Text, State, WHITE) : WHITE;
    float Side = Math_MinFloat(Box.width, Box.height) * 0.70710678f; // A square turned an eighth fits the box.
    Vector2 Middle = { .x = 0.5f, .y = 0.5f };
    Error Result = UIRenderContext_RenderRectangle(context, Middle, (Vector2){ .x = Side / Box.width, .y = Side / Box.height }, Middle, PI_F * 0.25f,
        UIRenderColor_FromColor(Fill));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float Inner = Side * 0.35f;
    return UIRenderContext_RenderRectangle(context, Middle, (Vector2){ .x = Inner / Box.width, .y = Inner / Box.height }, Middle, PI_F * 0.25f,
        UIRenderColor_FromColor(Mark));
}

static Error Diamond_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    UNUSED(self);
    UNUSED(context);
    UNUSED(availableWidth);
    *outSize = (Vector2){ .x = DIAMOND_SIZE, .y = DIAMOND_SIZE };
    return Error_CreateSuccess();
}

static const UILookVTable DiamondLookVTable = { .Render = Diamond_Render, .Measure = Diamond_Measure };

/* Gives an element its hover text; nothing for an id that is not there. */
static void Describe(GalleryBuild* build, uint64_t id, const unsigned char* text)
{
    UIGalleryFrame* Frame = build->Frame;
    if (!HasFailed(build) && (id != WIDGET_ID_INVALID))
    {
        build->Result = UITooltip_Set(&Frame->_screen, id, text);
        if (Frame->_describedCount < MAX_NAMED_ELEMENTS)
        {
            Frame->_describedIds[Frame->_describedCount] = id;
            Frame->_describedCount++;
        }
    }
}

/* Hover text on everything named that has none of its own yet: what it is. */
static void DescribeEverything(GalleryBuild* build)
{
    UIGalleryFrame* Frame = build->Frame;
    for (size_t Index = 0U; (Index < Frame->_nameCount) && !HasFailed(build); Index++)
    {
        bool HasOwn = false;
        for (size_t Described = 0U; Described < Frame->_describedCount; Described++)
        {
            HasOwn = HasOwn || (Frame->_describedIds[Described] == Frame->_names[Index].Id);
        }
        if (!HasOwn)
        {
            char Text[STATUS_CAPACITY];
            snprintf(Text, sizeof(Text), "This is %s.", Frame->_names[Index].Name);
            Describe(build, Frame->_names[Index].Id, (const unsigned char*)Text);
        }
    }
}

/* A button of the scale rows: a fixed width and height in its row, or sized by its content, at a scale of its own. */
static uint64_t AddScaledButton(GalleryBuild* build, uint64_t parent, const unsigned char* text, const char* name, bool isFixed, float width,
    float scale)
{
    uint64_t Id = WIDGET_ID_INVALID;
    if (!HasFailed(build))
    {
        UIButtonOptions Options = UIButtonOptions_Create(text);
        Options.Element.Scale = scale;
        Options.Element.Hint = isFixed ? UIStackHint_Fixed(width) : UIStackHint_Content();
        Options.Element.Hint.Align = UIStackAlign_Start;
        Options.Element.Hint.HasCrossSize = isFixed;
        Options.Element.Hint.CrossSize = 24.0f;
        build->Result = UIKit_CreateButton(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
        Remember(build, Id, name);
    }
    return Id;
}

/* The scale rows: the element design's table of size against scale. */
static void BuildScaleRows(GalleryBuild* build, uint64_t panel)
{
    AddLabel(build, panel, u8"Size and scale: fixed 100 x 24, fixed 400 x 24, fixed at scale 2, by content at scale 2, fixed in a box of scale 2", u8"heading");
    uint64_t Row = AddRow(build, panel, 8.0f);
    Describe(build, AddScaledButton(build, Row, u8"Button", "the 100 by 24 button", true, 100.0f, 1.0f), u8"Fixed 100 x 24 at scale 1.");
    Describe(build, AddScaledButton(build, Row, u8"Long button", "the long button", true, 400.0f, 1.0f),
        u8"A long button: fixed 400 x 24. Its size changes, its text and corners do not.");
    Describe(build, AddScaledButton(build, Row, u8"Big, fixed", "the big fixed button", true, 100.0f, 2.0f),
        u8"Scale 2 in a fixed 100 x 24 box: everything inside is twice as large, the box is not, and its text is clipped.");
    Describe(build, AddScaledButton(build, Row, u8"By content", "the big button by content", false, 0.0f, 2.0f),
        u8"Scale 2, sized by its content: the box grows with what it measures.");
    uint64_t Box = WIDGET_ID_INVALID;
    if (!HasFailed(build))
    {
        UIPanelOptions Options = UIPanelOptions_Create();
        Options.Direction = UIStackDirection_Row; // Its child's fixed 100 is a width.
        Options.Element.Variant = u8"inset";
        Options.Element.Scale = 2.0f;
        Options.Element.Hint.Align = UIStackAlign_Start;
        build->Result = UIKit_CreatePanel(&build->Frame->_kit, &build->Frame->_screen, Row, &Options, &Box);
        Remember(build, Box, "the box of scale 2");
    }
    Describe(build, AddScaledButton(build, Box, u8"In a box", "the button in the box of scale 2", true, 100.0f, 1.0f),
        u8"Fixed 100 x 24 at scale 1, in a box of scale 2: its size counts in its parent's scale, so it is 200 x 48 on the screen.");
}

/* Starts the turning panel's turn: a whole one, over and over. */
static Error StartTurning(UIGalleryFrame* frame)
{
    UIKeyframe Turn[2] =
    {
        { .Time = 0.0, .Interpolation = UIInterpolation_Linear, .CustomEasing = NULL, .Value = { .FloatValue = 0.0f } },
        { .Time = TURN_SECONDS, .Interpolation = UIInterpolation_Linear, .CustomEasing = NULL, .Value = { .FloatValue = 2.0f * PI_F } }
    };
    UIAnimationOptions Options = UIAnimationOptions_CreateDefault();
    Options.IsLooping = true;
    uint64_t Animation = 0U;
    return UIScreen_StartAnimation(&frame->_screen, frame->_turningId, WidgetBaseProperty_Rotation, UIPropertyType_Float, Turn, 2U, Options, &Animation);
}

/* Slides the slide-in panel in from the left by its visual offset: layout never moves it. */
static Error StartSlideIn(UIGalleryFrame* frame)
{
    UIKeyframe Slide[2] =
    {
        { .Time = 0.0, .Interpolation = UIInterpolation_ExpOutPow3, .CustomEasing = NULL, .Value = { .Vector2Value = { -SLIDE_DISTANCE, 0.0f } } },
        { .Time = SLIDE_SECONDS, .Interpolation = UIInterpolation_Linear, .CustomEasing = NULL, .Value = { .Vector2Value = { 0.0f, 0.0f } } }
    };
    uint64_t Animation = 0U;
    return UIScreen_StartAnimation(&frame->_screen, frame->_slideId, WidgetBaseProperty_VisualOffset, UIPropertyType_Vector2, Slide, 2U,
        UIAnimationOptions_CreateDefault(), &Animation);
}

/* A fixed box of a row for a turning or sliding panel. */
static uint64_t AddMovingPanel(GalleryBuild* build, uint64_t parent, const char* name)
{
    uint64_t Id = WIDGET_ID_INVALID;
    if (!HasFailed(build))
    {
        UIPanelOptions Options = UIPanelOptions_Create();
        Options.Element.Variant = u8"inset";
        Options.HasSpacing = true;
        Options.Spacing = 8.0f;
        Options.Element.Hint = UIStackHint_Fixed(260.0f);
        Options.Element.Hint.Align = UIStackAlign_Start;
        Options.Element.Hint.HasCrossSize = true;
        Options.Element.Hint.CrossSize = 110.0f;
        build->Result = UIKit_CreatePanel(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
        Remember(build, Id, name);
    }
    return Id;
}

/* The turning panel and the one that slides in. */
static void BuildMovingRows(GalleryBuild* build, uint64_t panel)
{
    UIGalleryFrame* Frame = build->Frame;
    AddLabel(build, panel, u8"Any widget turns with what it holds and is clicked where it shows; a visual offset slides a panel in", u8"heading");
    uint64_t Row = WIDGET_ID_INVALID;
    if (!HasFailed(build))
    {
        // Room above and below for the turning panel's corners, which layout does not see.
        UIStackOptions Options = UIStackOptions_Create(UIStackDirection_Row);
        Options.Layout.Spacing = 80.0f;
        Options.Layout.Padding = (UIInsets){ .Left = 20.0f, .Top = 90.0f, .Right = 0.0f, .Bottom = 90.0f };
        build->Result = UIKit_CreateStack(&Frame->_kit, &Frame->_screen, panel, &Options, &Row);
    }
    Frame->_turningId = AddMovingPanel(build, Row, "the turning panel");
    Describe(build, Frame->_turningId, u8"It turns once in 20 seconds, its button and slider with it; layout and navigation keep its box.");
    Describe(build, AddButton(build, Frame->_turningId, u8"Click me while I turn", "the turning button", true, false, NULL, UIStackHint_Content()),
        u8"Hit where it shows: the pointer goes through the turn's inverse.");
    AddSlider(build, Frame->_turningId, "the turning slider", 40.0, false, 0.0, true);
    Frame->_slideId = AddMovingPanel(build, Row, "the sliding panel");
    Describe(build, Frame->_slideId, u8"It slides in by its visual offset, which layout never sees.");
    Frame->_slideButtonId = AddButton(build, Frame->_slideId, u8"Slide in again", "the slide-in button", true, false, NULL, UIStackHint_Content());
    Describe(build, Frame->_slideButtonId, u8"Slides this panel in again from the left.");
    if (!HasFailed(build))
    {
        build->Result = StartTurning(Frame);
    }
    if (!HasFailed(build))
    {
        build->Result = StartSlideIn(Frame);
    }
}

/* A button of the diamond look. */
static uint64_t AddDiamond(GalleryBuild* build, uint64_t parent, const char* name, bool isEnabled, bool isToggled)
{
    uint64_t Id = WIDGET_ID_INVALID;
    if (!HasFailed(build))
    {
        UIButtonOptions Options = UIButtonOptions_Create(NULL);
        Options.Element.Look = u8"tbp:gallery_diamond";
        Options.IsEnabled = isEnabled;
        Options.IsToggled = isToggled;
        build->Result = UIKit_CreateButton(&build->Frame->_kit, &build->Frame->_screen, parent, &Options, &Id);
        Remember(build, Id, name);
    }
    return Id;
}

/* The second page: scale, turns and offsets, a second look, icons. */
static void BuildLooksPage(GalleryBuild* build)
{
    UIGalleryFrame* Frame = build->Frame;
    if (HasFailed(build))
    {
        return;
    }
    UIStackOptions PageOptions = UIStackOptions_Create(UIStackDirection_Column);
    PageOptions.Layout.Padding = UIInsets_All(12.0f);
    PageOptions.Layout.Spacing = 8.0f;
    PageOptions.Element.HasPlacement = true;
    PageOptions.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
    uint64_t Page = WIDGET_ID_INVALID;
    build->Result = UIKit_CreateStack(&Frame->_kit, &Frame->_screen, WIDGET_ID_INVALID, &PageOptions, &Page);
    Frame->_pageIds[1] = Page;
    AddLabel(build, Page, u8"UI gallery: looks, scale and turns", u8"heading");
    AddLabel(build, Page, u8"N the other page, T the theme (the third is the first with another palette), + and - the scale. Hover anything for its text.", u8"hint");
    uint64_t Panel = AddPanel(build, Page, UIStackHint_Fill(1.0f), NULL);
    BuildScaleRows(build, Panel);
    BuildMovingRows(build, Panel);

    AddLabel(build, Panel, u8"A second look of the button: Render and Measure only, the kind does the rest", u8"heading");
    uint64_t Diamonds = AddRow(build, Panel, 10.0f);
    Describe(build, AddDiamond(build, Diamonds, "the diamond button", true, false),
        u8"Another look of the same button: a diamond in the theme's colours. It wrote Render and Measure; clicks, keys and events are the button's.");
    Describe(build, AddDiamond(build, Diamonds, "the toggled diamond", true, true), u8"Toggled: the look reads the state the button is in.");
    Describe(build, AddDiamond(build, Diamonds, "the disabled diamond", false, false), u8"Disabled: it swallows the click, as every button does.");

    AddLabel(build, Panel, u8"Icons: a placeholder letter before the text, until art exists", u8"heading");
    uint64_t Icons = AddRow(build, Panel, 6.0f);
    const char* const IconNames[] = { "the save button", "the open button", "the play button" };
    const unsigned char* const IconTexts[] = { u8"Save", u8"Open", u8"Play" };
    const uint32_t Letters[] = { (uint32_t)'S', (uint32_t)'O', (uint32_t)'P' };
    for (size_t Index = 0U; (Index < 3U) && !HasFailed(build); Index++)
    {
        UIButtonOptions Options = UIButtonOptions_Create(IconTexts[Index]);
        Options.Icon = UIElementIcon_FromLetter(Letters[Index]);
        uint64_t Id = WIDGET_ID_INVALID;
        build->Result = UIKit_CreateButton(&Frame->_kit, &Frame->_screen, Icons, &Options, &Id);
        Remember(build, Id, IconNames[Index]);
    }
    Frame->_statusIds[1] = AddLabel(build, Page, u8"Nothing yet.", u8"hint");
    Widget* PageBase = NULL;
    if (!HasFailed(build) && UIScreen_TryGetWidgetById(&Frame->_screen, Page, &PageBase))
    {
        build->Result = Widget_SetIsRendered(PageBase, false);
    }
}

/* Shows the next page and hides the one shown. */
static Error ShowNextPage(UIGalleryFrame* frame)
{
    Widget* Shown = NULL;
    Widget* Next = NULL;
    size_t NextIndex = (frame->_pageIndex + 1U) % PAGE_COUNT;
    if (!UIScreen_TryGetWidgetById(&frame->_screen, frame->_pageIds[frame->_pageIndex], &Shown)
        || !UIScreen_TryGetWidgetById(&frame->_screen, frame->_pageIds[NextIndex], &Next))
    {
        return Error_CreateSuccess();
    }
    Error Result = Widget_SetIsRendered(Shown, false);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_SetIsRendered(Next, true);
    }
    frame->_pageIndex = NextIndex;
    return Result;
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

/* What a mouse event says, in words. */
static void DescribeMouse(const UIEvent* event, char* outText, size_t capacity)
{
    const UIEventMouse* Mouse = &event->Mouse;
    uint8_t Modifiers = Mouse->Modifiers;
    if (Mouse->Button == GameKey_Unknown)
    {
        snprintf(outText, capacity, ": %.0f notches at %.2f, %.2f%s%s%s", (double)Mouse->ScrollDelta.y, (double)Mouse->ScreenPosition.x, (double)Mouse->ScreenPosition.y,
            ((Modifiers & GAME_KEY_MODIFIER_SHIFT) != 0U) ? ", Shift" : "", ((Modifiers & GAME_KEY_MODIFIER_CONTROL) != 0U) ? ", Ctrl" : "",
            ((Modifiers & GAME_KEY_MODIFIER_ALT) != 0U) ? ", Alt" : "");
        return;
    }
    snprintf(outText, capacity, ": %s at %.2f, %.2f%s%s%s%s", (const char*)GameKey_GetDisplayName(Mouse->Button), (double)Mouse->ScreenPosition.x,
        (double)Mouse->ScreenPosition.y, Mouse->IsOverWidget ? ", over it" : ", off it", ((Modifiers & GAME_KEY_MODIFIER_SHIFT) != 0U) ? ", Shift" : "",
        ((Modifiers & GAME_KEY_MODIFIER_CONTROL) != 0U) ? ", Ctrl" : "", ((Modifiers & GAME_KEY_MODIFIER_ALT) != 0U) ? ", Alt" : "");
}

/* Shows each event in the status line, and logs it (a drag's Changed events at most a few times a second). */
static Error OnElementEvent(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    UIGalleryFrame* Frame = UserData_GetPointer(userData);
    static const char* const EventNames[] = { "activated", "changed", "committed", "cancelled", "pressed", "released", "double clicked", "scrolled", "context requested" };
    bool IsKnown = (event->WidgetEventId >= 0) && ((size_t)event->WidgetEventId < (sizeof(EventNames) / sizeof(EventNames[0])));
    char Value[96] = "";
    if (event->HasValue && (event->Value.Type == UIPropertyType_Double))
    {
        snprintf(Value, sizeof(Value), ": %.3f", event->Value.Data.DoubleValue);
    }
    else if (event->HasValue && (event->Value.Type == UIPropertyType_Bool))
    {
        snprintf(Value, sizeof(Value), ": %s", event->Value.Data.BoolValue ? "on" : "off");
    }
    else if (event->HasMouse)
    {
        DescribeMouse(event, Value, sizeof(Value));
    }
    char Status[STATUS_CAPACITY];
    snprintf(Status, sizeof(Status), "%s %s%s", GetElementName(Frame, event->WidgetId), IsKnown ? EventNames[event->WidgetEventId] : "posted", Value);
    (void)screen;
    Error Result = SetStatus(Frame, Status);
    if (Result.Code == ErrorCode_Success)
    {
        bool IsSlideAgain = (event->WidgetId == Frame->_slideButtonId) && (event->WidgetEventId == (int32_t)UIElementEvent_Activated);
        Result = IsSlideAgain ? StartSlideIn(Frame) : Error_CreateSuccess();
    }
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

/* Shows and logs an element being entered or left. */
static Error OnEnteredChanged(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    UIGalleryFrame* Frame = UserData_GetPointer(userData);
    char Status[STATUS_CAPACITY];
    snprintf(Status, sizeof(Status), "%s %s", GetElementName(Frame, event->WidgetId), event->IsSet ? "entered: its keys are its own" : "left");
    (void)screen;
    Error Result = SetStatus(Frame, Status);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Frame->_isEventLogEnabled && (Frame->_services->Logger != NULL))
    {
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
        UIWindowHost_SetPlayerScale(&frame->_host, Scales[frame->_scaleIndex]);
    }
    if (GameInput_IsKeyPressed(input, GameKey_P))
    {
        frame->_isFollowingHeight = !frame->_isFollowingHeight;
        UIWindowHost_SetPolicy(&frame->_host, frame->_isFollowingHeight ? UIScalePolicy_FollowHeight() : UIScalePolicy_ConstantPixels());
        const char* Name = frame->_isFollowingHeight ? "the UI follows the window's height." : "the UI keeps its pixels: the window gives room.";
        LogLine(frame, "%s", Name);
        Error Result = SetStatus(frame, Name);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    if (GameInput_IsKeyPressed(input, GameKey_N))
    {
        Error Result = ShowNextPage(frame);
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
    LogLine(self, "%s", "every element in every state. T the theme, + and - the scale, P the scale policy, L the event log, Escape steps out and quits.");
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
    Error Result = UIWindowHost_Update(&Frame->_host, &Frame->_screen, time, Input);
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
    UIWindowHost_Deconstruct(&frame->_host);
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
    float PlayerScale = (Shared->Config != NULL) ? Shared->Config->UIScale : 1.0f;
    frame->_scaleIndex = FindScaleIndex(PlayerScale);
    UIWindowHost_Construct(&frame->_host, UIScalePolicy_ConstantPixels(), PlayerScale, GameFrameManager_GetTargetAspectRatio(Shared->FrameManager),
        GameFrameManager_GetTargetAreaPosition(Shared->FrameManager));
    Error Result = TextComponentFactory_Construct(&frame->_textFactory);
    frame->_isTextFactoryConstructed = (Result.Code == ErrorCode_Success);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWidgetFactory_Construct(&frame->_widgetFactory);
        frame->_isWidgetFactoryConstructed = (Result.Code == ErrorCode_Success);
    }
    if (Result.Code == ErrorCode_Success)
    {
        WidgetTypeDescriptor Descriptor = { .Name = u8"GalleryWheelStack", .StructSize = sizeof(WheelStackWidget), .VTable = &WheelStackVTable,
            .Constructor = WheelStack_Construct, .IsFocusable = false };
        Result = UIWidgetFactory_RegisterType(&frame->_widgetFactory, &Descriptor, &frame->_wheelStackType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        WidgetTypeDescriptor Descriptor = { .Name = u8"GalleryScrubBox", .StructSize = sizeof(ScrubBoxWidget), .VTable = &ScrubBoxVTable,
            .Constructor = ScrubBox_Construct, .IsFocusable = false };
        Result = UIWidgetFactory_RegisterType(&frame->_widgetFactory, &Descriptor, &frame->_scrubBoxType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_Construct(&frame->_kit, &frame->_widgetFactory, &frame->_textFactory, Shared->Assets, Shared->Logger);
        frame->_isKitConstructed = (Result.Code == ErrorCode_Success);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(&frame->_kit, UIElementKind_Button, u8"tbp:gallery_diamond", sizeof(DiamondLook), &DiamondLookVTable, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_Construct(&frame->_screen, &frame->_widgetFactory, Shared->Logger);
        frame->_isScreenConstructed = (Result.Code == ErrorCode_Success);
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
        UserData FrameData = UserData_FromPointer(frame);
        uint64_t Subscription = UI_SCREEN_ID_INVALID;
        Result = UIScreen_Subscribe(&frame->_screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_EnteredChanged), OnEnteredChanged, &FrameData,
            &Subscription);
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
