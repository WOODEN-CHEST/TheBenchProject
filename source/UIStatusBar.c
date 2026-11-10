#include "UIStatusBar.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIScreen.h"
#include "UITooltip.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"
#include "wr/WRString.h"


// Static functions: helpers.
static UIStatusBarCapability* GetCapability(void* widget)
{
    return &((UIStatusBarLookBase*)widget)->StatusBar;
}

static Error ResolveBar(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIStatusBarCapability** outBar)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIStatusBar: the widget is gone, or its screen has no kit.");
    }
    UIStatusBarCapability* Bar = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_StatusBar));
    if (Bar == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIStatusBar: the widget is not a status bar.");
    }
    *outWidget = Target;
    *outBar = Bar;
    return Error_CreateSuccess();
}

/* What shows: the message, else the hover text where the pointer rests, else nothing. */
static Error RefreshShown(UIStatusBarCapability* self, const UIScreen* screen)
{
    const unsigned char* Text = u8"";
    UITooltipState Hover;
    if (self->_hasMessage)
    {
        Text = UITextContent_Get(&self->_message);
    }
    else if (self->_showsHoverText && UITooltip_GetCurrent(screen, &Hover))
    {
        Text = Hover.Text;
    }
    return UITextContent_Set(&self->_shown, Text, NULL);
}


// Static functions: the kind's behaviour, for the kit's composed widget types.
static void* ResolveCapability(void* widget)
{
    return GetCapability(widget);
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIStatusBar: the arguments are required.");
    }
    const UIStatusBarOptions* Options = ((const UIStatusBarArgs*)args)->Options;
    UIStatusBarCapability* Self = GetCapability(widget);
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITextContent_Construct(&Self->_message, NULL);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UITextContent_Construct(&Self->_shown, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&Self->_message);
        return Result;
    }
    for (int32_t Section = 0; Section < (int32_t)UIStatusBarSection_Count; Section++)
    {
        Self->_sectionIds[Section] = WIDGET_ID_INVALID;
    }
    Self->_hasMessage = false;
    Self->_isMessageTimed = false;
    Self->_messageSeconds = 0.0;
    Self->_showsHoverText = Options->ShowsHoverText;
    return Error_CreateSuccess();
}

static Error Behaviour_Deconstruct(void* widget)
{
    UIStatusBarCapability* Self = GetCapability(widget);
    UITextContent_Deconstruct(&Self->_message);
    UITextContent_Deconstruct(&Self->_shown);
    return Error_CreateSuccess();
}

/* A timed message counts down and goes; what shows follows. */
static Error Behaviour_Update(void* widget, ProgramTime time)
{
    UIStatusBarCapability* Self = GetCapability(widget);
    if (Self->_hasMessage && Self->_isMessageTimed)
    {
        Self->_messageSeconds -= time.PassedTime;
        if (Self->_messageSeconds <= 0.0)
        {
            Self->_hasMessage = false;
        }
    }
    return RefreshShown(Self, Widget_GetScreen(widget));
}

static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    return UIStatusBar_ArrangeSections(widget, context, (Vector2){ .x = 0.0f, .y = 0.0f }, 0.0f);
}


// Fields.
static const UIKindCapability StatusBarCapabilities[] = { { .Capability = UIElementCapability_StatusBar, .Resolve = ResolveCapability } };

static const UIKindBehaviour StatusBarBehaviour =
{
    .Kind = UIElementKind_StatusBar,
    .Capabilities = StatusBarCapabilities,
    .CapabilityCount = sizeof(StatusBarCapabilities) / sizeof(StatusBarCapabilities[0]),
    .BaseSize = sizeof(UIStatusBarLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Deconstruct = Behaviour_Deconstruct,
    .Update = Behaviour_Update,
    .Layout = Behaviour_Layout
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIStatusBar_GetKindBehaviour(void)
{
    return &StatusBarBehaviour;
}


// Public functions: calling code.
Error UIStatusBar_SetMessage(UIScreen* screen, uint64_t widgetId, const unsigned char* text, double seconds)
{
    if (text == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIStatusBar_SetMessage: the text must not be NULL.");
    }
    Widget* Target = NULL;
    UIStatusBarCapability* Bar = NULL;
    Error Result = ResolveBar(screen, widgetId, &Target, &Bar);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!(seconds >= 0.0) || Math_IsInfinityDouble(seconds))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIStatusBar_SetMessage: the time must be a finite number >= 0.");
    }
    Result = UITextContent_Set(&Bar->_message, text, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Bar->_hasMessage = true;
    Bar->_isMessageTimed = seconds > 0.0;
    Bar->_messageSeconds = seconds;
    return RefreshShown(Bar, screen);
}

Error UIStatusBar_ClearMessage(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIStatusBarCapability* Bar = NULL;
    Error Result = ResolveBar(screen, widgetId, &Target, &Bar);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Bar->_hasMessage = false;
    return RefreshShown(Bar, screen);
}

Error UIStatusBar_SetShowsHoverText(UIScreen* screen, uint64_t widgetId, bool showsHoverText)
{
    Widget* Target = NULL;
    UIStatusBarCapability* Bar = NULL;
    Error Result = ResolveBar(screen, widgetId, &Target, &Bar);
    if (Result.Code == ErrorCode_Success)
    {
        Bar->_showsHoverText = showsHoverText;
    }
    return Result;
}

Error UIStatusBar_GetSection(UIScreen* screen, uint64_t widgetId, UIStatusBarSection section, uint64_t* outStackId)
{
    Widget* Target = NULL;
    UIStatusBarCapability* Bar = NULL;
    Error Result = ResolveBar(screen, widgetId, &Target, &Bar);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (((int32_t)section < 0) || ((int32_t)section >= (int32_t)UIStatusBarSection_Count))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIStatusBar_GetSection: an unknown section.");
    }
    *outStackId = Bar->_sectionIds[section];
    return Error_CreateSuccess();
}


// Public functions: helpers.
Error UIStatusBar_ArrangeSections(Widget* widget, const UILayoutContext* context, Vector2 padding, float spacing)
{
    UIStatusBarCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Screen, widget);
    Vector2 Box = { .x = Pixels.width / Scale, .y = Pixels.height / Scale };
    float Height = Math_MaxFloat(0.0f, Box.y - (2.0f * padding.y));
    Widget* Sections[UIStatusBarSection_Count] = { NULL, NULL, NULL };
    float Widths[UIStatusBarSection_Count] = { 0.0f, 0.0f, 0.0f };
    for (int32_t Section = 0; Section < (int32_t)UIStatusBarSection_Count; Section++)
    {
        if (!UIScreen_TryGetWidgetById(Screen, Self->_sectionIds[Section], &Sections[Section]) || (Section == (int32_t)UIStatusBarSection_Centre))
        {
            continue;
        }
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Error Result = Widget_Measure(Sections[Section], context->Sizing, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Widths[Section] = Size.x;
    }
    float Left = padding.x;
    float Right = Box.x - padding.x - Widths[UIStatusBarSection_Right];
    float CentreStart = Left + Widths[UIStatusBarSection_Left] + ((Widths[UIStatusBarSection_Left] > 0.0f) ? spacing : 0.0f);
    float CentreEnd = Right - ((Widths[UIStatusBarSection_Right] > 0.0f) ? spacing : 0.0f);
    UIPlacement Placements[UIStatusBarSection_Count] =
    {
        [UIStatusBarSection_Left] = UIPlacement_FromTopLeft(Left, padding.y, Widths[UIStatusBarSection_Left], Height),
        [UIStatusBarSection_Centre] = UIPlacement_FromTopLeft(CentreStart, padding.y, Math_MaxFloat(0.0f, CentreEnd - CentreStart), Height),
        [UIStatusBarSection_Right] = UIPlacement_FromTopLeft(Math_MaxFloat(Right, CentreStart), padding.y, Widths[UIStatusBarSection_Right], Height),
    };
    for (int32_t Section = 0; Section < (int32_t)UIStatusBarSection_Count; Section++)
    {
        Error Result = (Sections[Section] != NULL) ? Widget_SetPlacement(Sections[Section], &Placements[Section]) : Error_CreateSuccess();
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

Error UIStatusBar_MeasureSections(Widget* widget, const UIMeasureContext* context, Vector2 padding, float spacing, Vector2* outSize)
{
    const UIStatusBarCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    float Width = 0.0f;
    float Height = 0.0f;
    size_t Shown = 0U;
    for (int32_t Section = 0; Section < (int32_t)UIStatusBarSection_Count; Section++)
    {
        Widget* Stack = NULL;
        if (!UIScreen_TryGetWidgetById(Screen, Self->_sectionIds[Section], &Stack))
        {
            continue;
        }
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Error Result = Widget_Measure(Stack, context, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Width += Size.x;
        Height = Math_MaxFloat(Height, Size.y);
        Shown += (Size.x > 0.0f) ? 1U : 0U;
    }
    float Gaps = (Shown > 1U) ? (spacing * (float)(Shown - 1U)) : 0.0f;
    *outSize = (Vector2){ .x = Width + Gaps + (2.0f * padding.x), .y = Height + (2.0f * padding.y) };
    return Error_CreateSuccess();
}

void UIStatusBar_GetTextSpan(const Widget* widget, float* outStart, float* outEnd)
{
    const UIStatusBarCapability* Self = &((const UIStatusBarLookBase*)(const void*)widget)->StatusBar;
    UIScreen* Screen = Widget_GetScreen(widget);
    *outStart = 0.0f;
    *outEnd = 1.0f;
    // The centre section stands between the others with the bar's spacing on either side of it: the text keeps the same gaps. (Calling code
    // that destroyed it leaves the text the whole bar.)
    Widget* Centre = NULL;
    if (UIScreen_TryGetWidgetById(Screen, Self->_sectionIds[UIStatusBarSection_Centre], &Centre))
    {
        *outStart = Widget_GetPosition(Centre).x;
        *outEnd = Widget_GetPosition(Centre).x + Widget_GetSize(Centre).x;
    }
}

void UIStatusBar_SetSections(UIStatusBarCapability* self, const uint64_t sectionIds[UIStatusBarSection_Count])
{
    for (int32_t Section = 0; Section < (int32_t)UIStatusBarSection_Count; Section++)
    {
        self->_sectionIds[Section] = sectionIds[Section];
    }
}
