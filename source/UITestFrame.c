#include "UITestFrame.h"
#include "GameFrame.h"
#include "GameFrameManager.h"
#include "GameInput.h"
#include "LabelWidget.h"
#include "Logger.h"
#include "Renderer.h"
#include "Services.h"
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "TextDocument.h"
#include "UIScreen.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "raylib/raylib.h"
#include "wr/WRCompile.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"


// Macros.
/** Debug name used in log and crash messages. */
#define FRAME_DEBUG_NAME ((const unsigned char*)u8"UITest")
/** The labels' font: covers Latvian and Cyrillic, like the world test's text sample. */
#define LABEL_FONT_NAME ((const unsigned char*)u8"noto_sans")
/** Where the hint sits (screen-relative) and how tall its text is (fitted units). */
#define HINT_POSITION_X 0.01f
#define HINT_POSITION_Y 0.955f
#define HINT_TEXT_SIZE 0.028f
/** Where the menu label sits (screen-relative) and how tall its text is (fitted units). */
#define MENU_POSITION_X 0.72f
#define MENU_POSITION_Y 0.06f
#define MENU_TEXT_SIZE 0.04f
/** The items' text size (fitted units), and their placement inside the menu label, in units of its box. */
#define ITEM_TEXT_SIZE 0.035f
#define ITEM_OFFSET_X 0.08f
#define ITEM_FIRST_OFFSET_Y 1.5f
#define ITEM_STEP_Y 1.25f
/** How many items the menu gets. */
#define ITEM_COUNT ((size_t)3)
/** The size pulse a label plays when it gains focus: its text size multiplier peaks at this, after this many seconds. */
#define FOCUS_PULSE_PEAK 1.15f
#define FOCUS_PULSE_PEAK_TIME 0.12
#define FOCUS_PULSE_END_TIME 0.25


// Types.
typedef struct UITestFrameStruct
{
    /** Abstract base (must be first). */
    GameFrame Base;
    /** Shared services; borrowed. */
    Services* _services;
    /** The labels' text components come from and go back to this factory; owned. */
    TextComponentFactory _textFactory;
    /** The widget factory (one per program in a real game; this test keeps its own); owned. */
    UIWidgetFactory _widgetFactory;
    /** The UI; owned. */
    UIScreen _screen;
    /** Which of the three above are constructed (for teardown after a failed creation). */
    bool _isTextFactoryConstructed;
    bool _isWidgetFactoryConstructed;
    bool _isScreenConstructed;
    /** The label widget type. */
    uint64_t _labelTypeId;
    /** The menu label's id (its items are its children); WIDGET_ID_INVALID when there is none. */
    uint64_t _menuId;
} UITestFrame;


// Fields.
static const unsigned char* const ItemTexts[ITEM_COUNT] = { u8"First item", u8"Second item", u8"Third item" };


// Static functions: building the UI.
/* Creates an initialized, detached label with one string of text. */
static Error CreateLabel(UITestFrame* frame, const unsigned char* text, float textSize, Vector2 position, bool isFocusable,
    LabelWidget** outLabel)
{
    *outLabel = NULL;
    StringComponent* String = NULL;
    Error Result = TextComponentFactory_CreateString(&frame->_textFactory, text, &String);
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetSize(String, textSize);
    }
    LabelWidget* Label = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = LabelWidget_Create(&frame->_screen, frame->_labelTypeId, &frame->_textFactory, frame->_services->Assets,
            frame->_services->Logger, StringComponent_AsComponent(String), &Label);
    }
    if ((Result.Code != ErrorCode_Success) && (String != NULL))
    {
        Error ReturnResult = TextComponentFactory_ReturnTree(&frame->_textFactory, StringComponent_AsComponent(String));
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Widget* Base = LabelWidget_AsWidget(Label);
    Result = TextDocument_SetDefaultFontName(LabelWidget_GetDocument(Label), LABEL_FONT_NAME);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_SetPosition(Base, position);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_SetIsFocusable(Base, isFocusable);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_InitializeWidget(Base);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error DestroyResult = Widget_Destroy(Base);
        Error_Deconstruct(&DestroyResult);
        return Result;
    }
    *outLabel = Label;
    return Error_CreateSuccess();
}

/* Builds the menu: a focusable label with three focusable item labels inside it. */
static Error BuildMenu(UITestFrame* frame)
{
    LabelWidget* Menu = NULL;
    Error Result = CreateLabel(frame, u8"Menu (Enter opens it)", MENU_TEXT_SIZE,
        (Vector2){ .x = MENU_POSITION_X, .y = MENU_POSITION_Y }, true, &Menu);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_AddWidget(&frame->_screen, LabelWidget_AsWidget(Menu));
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (Menu != NULL)
        {
            Error DestroyResult = Widget_Destroy(LabelWidget_AsWidget(Menu));
            Error_Deconstruct(&DestroyResult);
        }
        return Result;
    }
    frame->_menuId = Widget_GetId(LabelWidget_AsWidget(Menu));

    for (size_t Index = 0; Index < ITEM_COUNT; Index++)
    {
        LabelWidget* Item = NULL;
        Vector2 Position = { .x = ITEM_OFFSET_X, .y = ITEM_FIRST_OFFSET_Y + ((float)Index * ITEM_STEP_Y) };
        Result = CreateLabel(frame, ItemTexts[Index], ITEM_TEXT_SIZE, Position, true, &Item);
        if (Result.Code == ErrorCode_Success)
        {
            Result = Widget_AddSubWidget(LabelWidget_AsWidget(Menu), LabelWidget_AsWidget(Item));
            if (Result.Code != ErrorCode_Success)
            {
                Error DestroyResult = Widget_Destroy(LabelWidget_AsWidget(Item));
                Error_Deconstruct(&DestroyResult);
            }
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result; // The menu (and the items already in it) go when the screen is torn down.
        }
    }
    return Error_CreateSuccess();
}

/* Builds the hint: input-disabled, so the pointer goes through it. */
static Error BuildHint(UITestFrame* frame)
{
    LabelWidget* Hint = NULL;
    Error Result = CreateLabel(frame,
        u8"UI test: M frees the mouse. Tab or arrows move, Enter opens, Escape closes. Delete removes the focused item, R rebuilds.",
        HINT_TEXT_SIZE, (Vector2){ .x = HINT_POSITION_X, .y = HINT_POSITION_Y }, false, &Hint);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Widget* Base = LabelWidget_AsWidget(Hint);
    Result = Widget_SetIsInputEnabled(Base, false);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_AddWidget(&frame->_screen, Base);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error DestroyResult = Widget_Destroy(Base);
        Error_Deconstruct(&DestroyResult);
    }
    return Result;
}


// Static functions: reacting to the UI.
/* Hover and focus notifications (delivered at the end of UIScreen_Update): tints the label, and a label that gains
 * focus plays a short size pulse. */
static Error OnLabelStateChanged(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    UITestFrame* Frame = UserData_GetPointer(userData);
    Widget* Target = NULL;
    if (!UIScreen_TryGetWidgetById(screen, event->WidgetId, &Target) || (Widget_GetTypeId(Target) != Frame->_labelTypeId))
    {
        return Error_CreateSuccess();
    }

    RenderColor Tint = RenderColor_White();
    if (Widget_IsFocused(Target))
    {
        Tint.Tint = SKYBLUE;
    }
    else if (Widget_IsHovered(Target))
    {
        Tint.Tint = YELLOW;
    }
    Error Result = LabelWidget_SetTint((LabelWidget*)Target, Tint);
    if ((Result.Code != ErrorCode_Success) || (event->Kind != UIEventKind_FocusChanged) || !event->IsSet)
    {
        return Result;
    }

    UIKeyframe Pulse[] =
    {
        { .Time = 0.0, .Interpolation = UIInterpolation_SineInOut, .CustomEasing = NULL, .Value.FloatValue = 1.0f },
        { .Time = FOCUS_PULSE_PEAK_TIME, .Interpolation = UIInterpolation_SineInOut, .CustomEasing = NULL, .Value.FloatValue = FOCUS_PULSE_PEAK },
        { .Time = FOCUS_PULSE_END_TIME, .Interpolation = UIInterpolation_Linear, .CustomEasing = NULL, .Value.FloatValue = 1.0f },
    };
    uint64_t AnimationId = UI_SCREEN_ID_INVALID;
    return UIScreen_StartAnimation(screen, event->WidgetId, LabelProperty_Size, UIPropertyType_Float, Pulse,
        sizeof(Pulse) / sizeof(Pulse[0]), UIAnimationOptions_CreateDefault(), &AnimationId);
}

/* The test's own keys, from what the UI left unconsumed: Delete destroys the focused item, R rebuilds the menu. */
static Error HandleTestKeys(UITestFrame* frame, GameInput* input)
{
    if (GameInput_IsKeyPressed(input, GameKey_Delete))
    {
        Widget* Focused = NULL;
        bool HasFocusedItem = UIScreen_TryGetWidgetById(&frame->_screen, UIScreen_GetFocusedWidgetId(&frame->_screen), &Focused)
            && (Widget_GetParent(Focused) != NULL) && (Widget_GetId(Widget_GetParent(Focused)) == frame->_menuId);
        if (HasFocusedItem)
        {
            GameInput_ConsumeKey(input, GameKey_Delete);
            Error Result = Widget_Destroy(Focused); // Freed at the end of the next UIScreen_Update.
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
    }
    if (GameInput_IsKeyPressed(input, GameKey_R))
    {
        GameInput_ConsumeKey(input, GameKey_R);
        Widget* Menu = NULL;
        if (UIScreen_TryGetWidgetById(&frame->_screen, frame->_menuId, &Menu))
        {
            Error Result = Widget_Destroy(Menu);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
        frame->_menuId = WIDGET_ID_INVALID;
        return BuildMenu(frame);
    }
    return Error_CreateSuccess();
}


// Static functions: vtable behavior.
static Error UITestFrame_Start(void* self, ProgramTime time)
{
    UNUSED(time);
    UITestFrame* Frame = self;
    if (Frame->_services->Logger != NULL)
    {
        Error LogResult = Logger_LogInfo(Frame->_services->Logger,
            (const unsigned char*)u8"UITest: a menu at the top right. M (world test) frees the mouse to hover and click it; "
            u8"Tab or arrows navigate, Enter opens the menu, Escape closes it; Delete removes the focused item, R rebuilds.");
        Error_Deconstruct(&LogResult);
    }
    return Error_CreateSuccess();
}

static Error UITestFrame_End(void* self, ProgramTime time)
{
    UNUSED(self);
    UNUSED(time);
    return Error_CreateSuccess();
}

static Error UITestFrame_Update(void* self, ProgramTime time)
{
    UITestFrame* Frame = self;
    GameInput* Input = Frame->_services->Input;

    // The screen fills the window (the frame's render target has the window's size).
    if (Input != NULL)
    {
        Vector2 WindowSize = GameInput_GetWindowSize(Input);
        if ((WindowSize.x > 0.0f) && (WindowSize.y > 0.0f))
        {
            UIViewport Viewport = UIViewport_Create(WindowSize,
                GameFrameManager_GetTargetAspectRatio(Frame->_services->FrameManager),
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
    return HandleTestKeys(Frame, Input);
}

static Error UITestFrame_BeginLoad(void* self)
{
    UNUSED(self);
    return Error_CreateSuccess();
}

static Error UITestFrame_LoadStep(void* self)
{
    UNUSED(self);
    return Error_CreateSuccess();
}

static bool UITestFrame_IsLoaded(void* self)
{
    UNUSED(self);
    return true;
}

static Error UITestFrame_BeginUnload(void* self)
{
    UNUSED(self);
    return Error_CreateSuccess();
}

static Error UITestFrame_UnloadStep(void* self)
{
    UNUSED(self);
    return Error_CreateSuccess();
}

static bool UITestFrame_IsUnloaded(void* self)
{
    UNUSED(self);
    return true;
}

static Error UITestFrame_Render(void* self, const FrameRenderContext* context, RenderTexture2D target)
{
    UITestFrame* Frame = self;
    RenderContext Context;
    RenderContext_Create(&Context, &target, context->TargetAspectRatio, context->TargetAreaPosition);
    RenderContext_BeginRendering(&Context);
    ClearBackground(BLANK); // Transparent: the world shows through when the frames are composited.
    Error Result = UIScreen_Render(&Frame->_screen, &Context);
    RenderContext_EndRendering(&Context);
    RenderContext_Deconstruct(&Context);
    return Result;
}

/* Tears down what is constructed: the screen first (it destroys the labels, which return their text to the text
 * factory and their storage to the widget factory), then the factories. */
static void DestroyFrameContents(UITestFrame* frame)
{
    if (frame->_isScreenConstructed)
    {
        Error ScreenResult = UIScreen_Deconstruct(&frame->_screen);
        Error_Deconstruct(&ScreenResult);
    }
    if (frame->_isWidgetFactoryConstructed)
    {
        Error FactoryResult = UIWidgetFactory_Deconstruct(&frame->_widgetFactory);
        Error_Deconstruct(&FactoryResult);
    }
    if (frame->_isTextFactoryConstructed)
    {
        Error TextResult = TextComponentFactory_Deconstruct(&frame->_textFactory);
        Error_Deconstruct(&TextResult);
    }
}

static void UITestFrame_Destroy(void* self)
{
    UITestFrame* Frame = self;
    DestroyFrameContents(Frame);
    GameFrame_Deconstruct(&Frame->Base);
    Memory_Free(Frame);
}


// Fields.
static const GameFrameVTable UITestFrameVTable =
{
    .Start = UITestFrame_Start,
    .End = UITestFrame_End,
    .Update = UITestFrame_Update,
    .BeginLoad = UITestFrame_BeginLoad,
    .LoadStep = UITestFrame_LoadStep,
    .IsLoaded = UITestFrame_IsLoaded,
    .BeginUnload = UITestFrame_BeginUnload,
    .UnloadStep = UITestFrame_UnloadStep,
    .IsUnloaded = UITestFrame_IsUnloaded,
    .Render = UITestFrame_Render,
    .GetLoadProgress = NULL,
    .OnResize = NULL,
    .Destroy = UITestFrame_Destroy
};


// Static functions: construction.
static Error BuildFrameContents(UITestFrame* frame)
{
    Error Result = TextComponentFactory_Construct(&frame->_textFactory);
    frame->_isTextFactoryConstructed = (Result.Code == ErrorCode_Success);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWidgetFactory_Construct(&frame->_widgetFactory);
        frame->_isWidgetFactoryConstructed = (Result.Code == ErrorCode_Success);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = LabelWidget_RegisterType(&frame->_widgetFactory, &frame->_labelTypeId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_Construct(&frame->_screen, &frame->_widgetFactory, frame->_services->Logger);
        frame->_isScreenConstructed = (Result.Code == ErrorCode_Success);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UserData FrameData = UserData_FromPointer(frame);
        uint64_t SubscriptionId = UI_SCREEN_ID_INVALID;
        Result = UIScreen_Subscribe(&frame->_screen, WIDGET_ID_INVALID,
            UI_EVENT_KIND_MASK(UIEventKind_HoverChanged) | UI_EVENT_KIND_MASK(UIEventKind_FocusChanged),
            OnLabelStateChanged, &FrameData, &SubscriptionId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = BuildHint(frame);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = BuildMenu(frame);
    }
    return Result;
}


// Public functions.
Error UITestFrame_Create(Services* services, GameFrame** outFrame)
{
    if ((services == NULL) || (outFrame == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITestFrame_Create: services and outFrame must not be NULL.");
    }
    *outFrame = NULL;

    UITestFrame* Frame = Memory_Allocate(sizeof(UITestFrame));
    Memory_Zero(Frame, sizeof(*Frame));
    Frame->_services = services;
    Error Result = GameFrame_Construct(&Frame->Base, &UITestFrameVTable, FRAME_DEBUG_NAME);
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
