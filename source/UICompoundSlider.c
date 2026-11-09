#include "UICompoundSlider.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UILabel.h"
#include "UIPropertyBox.h"
#include "UIScreen.h"
#include "wr/WRMath.h"


// Static functions: small helpers.
static float ToLength(float value)
{
    return ((value > 0.0f) && !Math_IsInfinityFloat(value)) ? value : 0.0f;
}

static bool IsOrientationValid(UISliderOrientation orientation)
{
    return (orientation == UISliderOrientation_Horizontal) || (orientation == UISliderOrientation_Vertical);
}

static UICompoundSliderCapability* GetCapability(void* widget)
{
    return &((UICompoundSliderLookBase*)widget)->CompoundSlider;
}

static Error ResolveCompound(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UICompoundSliderCapability** outSelf)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UICompoundSlider: the widget is gone, or its screen has no kit.");
    }
    UICompoundSliderCapability* Self = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_CompoundSlider));
    if (Self == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UICompoundSlider: the widget is not a compound slider.");
    }
    *outWidget = Target;
    *outSelf = Self;
    return Error_CreateSuccess();
}

/* A part, when it is alive and still the compound's. */
static Widget* FindPart(Widget* widget, uint64_t partId)
{
    Widget* Part = NULL;
    if (!UIScreen_TryGetWidgetById(Widget_GetScreen(widget), partId, &Part) || (Widget_GetParent(Part) != widget))
    {
        return NULL;
    }
    return Part;
}

/* Hides or shows a part. */
static Error ShowPart(Widget* widget, uint64_t partId, bool isShown)
{
    Widget* Part = FindPart(widget, partId);
    return ((Part != NULL) && (Widget_IsRendered(Part) != isShown)) ? Widget_SetIsRendered(Part, isShown) : Error_CreateSuccess();
}

/* Puts the compound's value, settings (when they changed) and mixed state into one part, quietly: the part's own hook hears it as code
 * (it never tells the compound: that is UIValue's way, which a push does not take). */
static Error PushToPart(UICompoundSliderCapability* self, Widget* widget, uint64_t partId, bool isPushingSettings)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    Widget* Part = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(Screen, partId, &Part, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result); // A part taken away hears nothing.
        return Error_CreateSuccess();
    }
    if (isPushingSettings)
    {
        Result = UIValueCapability_SetSettings(Value, UIValueCapability_GetSettings(&self->Value));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    UIValueCapability_SetValueQuietly(Value, UIValueCapability_GetValue(&self->Value));
    UIValueCapability_SetStatesQuietly(Value, UIValueCapability_IsEnabled(Value), UIValueCapability_IsMixed(&self->Value));
    if (!isPushingSettings)
    {
        UIValueCapability_RebaseChange(Value); // As code's value: a drag or a scrub of the part goes on from here.
    }
    return ((Value->_hooks != NULL) && (Value->_hooks->OnChangedByCode != NULL)) ? Value->_hooks->OnChangedByCode(Part) : Error_CreateSuccess();
}

/* Puts the compound's value into its parts but the one that moved (whose change in progress must go on as it is). The widget may be gone
 * after it: callers find it again. */
static Error PushToParts(Widget* widget, uint64_t exceptPartId)
{
    UICompoundSliderCapability* Self = GetCapability(widget);
    uint64_t Id = Widget_GetId(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t Parts[2] = { Self->_sliderId, Self->_boxId };
    bool IsPushingSettings = Self->_pushedSettingsRevision != Self->Value._settingsRevision;
    uint64_t Revision = Self->Value._settingsRevision;
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < 2U) && (Result.Code == ErrorCode_Success); Index++)
    {
        if (Parts[Index] == exceptPartId)
        {
            continue;
        }
        Result = PushToPart(Self, widget, Parts[Index], IsPushingSettings);
        if (!UIScreen_TryGetWidgetById(Screen, Id, &widget))
        {
            return Result;
        }
        Self = GetCapability(widget);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Self->_pushedSettingsRevision = Revision;
    }
    return Result;
}

/* The width the slider and the box take across, or their heights upright, from what shows. */
static void GetParts(Widget* widget, Widget** outName, Widget** outSlider, Widget** outBox)
{
    const UICompoundSliderCapability* Self = GetCapability(widget);
    *outName = FindPart(widget, Self->_nameId);
    *outSlider = FindPart(widget, Self->_sliderId);
    *outBox = FindPart(widget, Self->_boxId);
    *outName = ((*outName != NULL) && Widget_IsRendered(*outName)) ? *outName : NULL;
    *outSlider = ((*outSlider != NULL) && Widget_IsRendered(*outSlider)) ? *outSlider : NULL;
    *outBox = ((*outBox != NULL) && Widget_IsRendered(*outBox)) ? *outBox : NULL;
}

static Error MeasurePart(Widget* part, const UIMeasureContext* context, float width, Vector2* outSize)
{
    *outSize = (Vector2){ .x = 0.0f, .y = 0.0f };
    return (part != NULL) ? Widget_Measure(part, context, width, outSize) : Error_CreateSuccess();
}


// Static functions: the kind's behaviour, for the kit's composed widget types.
static void* ResolveCapability(void* widget)
{
    return GetCapability(widget);
}

static void* ResolveValue(void* widget)
{
    return &GetCapability(widget)->Value;
}

/* Code changed the compound's value or a setting: both parts follow. */
static Error Value_OnChangedByCode(Widget* widget)
{
    return PushToParts(widget, WIDGET_ID_INVALID);
}

/* Enabled or disabled: both parts are, by their own hooks (a drag taken back, the focus given up). */
static Error Value_OnEnabledChanged(Widget* widget)
{
    UICompoundSliderCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    bool IsEnabled = UIValueCapability_IsEnabled(&Self->Value);
    uint64_t Parts[2] = { Self->_sliderId, Self->_boxId };
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        Error Result = UIValue_SetIsEnabled(Screen, Parts[Index], IsEnabled);
        if (Result.Code == ErrorCode_InvalidOperation)
        {
            Error_Deconstruct(&Result); // A part taken away hears nothing.
            continue;
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* A part moved (by the pointer, the keys or code by its id): the compound's value follows it, and so does the other part; the part that
 * moved too, only when the compound's rules made its value another (its change in progress would start again from there). */
static Error Value_OnPartChanged(Widget* widget, uint64_t partId)
{
    UICompoundSliderCapability* Self = GetCapability(widget);
    Widget* Part = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(Widget_GetScreen(widget), partId, &Part, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    double PartValue = UIValueCapability_GetValue(Value);
    UIValueCapability_SetValueQuietly(&Self->Value, PartValue);
    UIValueCapability_SetStatesQuietly(&Self->Value, UIValueCapability_IsEnabled(&Self->Value), UIValueCapability_IsMixed(Value));
    return PushToParts(widget, (UIValueCapability_GetValue(&Self->Value) == PartValue) ? partId : WIDGET_ID_INVALID);
}

static const UIValueHooks CompoundValueHooks =
{
    .OnChangedByCode = Value_OnChangedByCode,
    .OnEnabledChanged = Value_OnEnabledChanged,
    .OnPartChanged = Value_OnPartChanged,
    .GetUnit = NULL
};

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UICompoundSlider: the arguments are required.");
    }
    const UICompoundSliderOptions* Options = ((const UICompoundSliderArgs*)args)->Options;
    if (!IsOrientationValid(Options->Orientation))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UICompoundSlider: the orientation is none of UISliderOrientation.");
    }
    UICompoundSliderCapability* Self = GetCapability(widget);
    Error Result = UIValueCapability_Construct(&Self->Value, &Options->Value, &CompoundValueHooks, true);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        UIValueCapability_Deconstruct(&Self->Value);
        return Result;
    }
    Self->_orientation = Options->Orientation;
    Self->_nameId = WIDGET_ID_INVALID;
    Self->_sliderId = WIDGET_ID_INVALID;
    Self->_boxId = WIDGET_ID_INVALID;
    Self->_showsName = Options->ShowsName;
    Self->_showsSlider = Options->ShowsSlider;
    Self->_showsBox = Options->ShowsBox;
    Self->_pushedSettingsRevision = Self->Value._settingsRevision;
    Self->_spacing = 0.0f;
    Self->_boxWidth = 0.0f;
    return Error_CreateSuccess();
}

static Error Behaviour_Deconstruct(void* widget)
{
    UIValueCapability_Deconstruct(&GetCapability(widget)->Value);
    return Error_CreateSuccess();
}

static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    return UICompoundSlider_Arrange(widget, context);
}


// Fields.
static const UIKindCapability CompoundCapabilities[] =
{
    { .Capability = UIElementCapability_CompoundSlider, .Resolve = ResolveCapability },
    { .Capability = UIElementCapability_Value, .Resolve = ResolveValue }
};

static const UIKindBehaviour CompoundBehaviour =
{
    .Kind = UIElementKind_CompoundSlider,
    .Capabilities = CompoundCapabilities,
    .CapabilityCount = sizeof(CompoundCapabilities) / sizeof(CompoundCapabilities[0]),
    .BaseSize = sizeof(UICompoundSliderLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Deconstruct = Behaviour_Deconstruct,
    .Layout = Behaviour_Layout
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UICompoundSlider_GetKindBehaviour(void)
{
    return &CompoundBehaviour;
}


// Public functions: helpers.
UICompoundSliderOptions UICompoundSliderOptions_Create(void)
{
    return (UICompoundSliderOptions)
    {
        .Element = UIElementOptions_Create(),
        .Value = UIValueOptions_Create(),
        .Orientation = UISliderOrientation_Horizontal,
        .Name = NULL,
        .ShowsName = true,
        .ShowsSlider = true,
        .ShowsBox = true,
        .Precision = 3,
        .Unit = NULL
    };
}


// Public functions: calling code.
Error UICompoundSlider_GetPartIds(UIScreen* screen, uint64_t widgetId, uint64_t* outNameId, uint64_t* outSliderId, uint64_t* outBoxId)
{
    Widget* Target = NULL;
    UICompoundSliderCapability* Self = NULL;
    Error Result = ResolveCompound(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outNameId != NULL)
    {
        *outNameId = Self->_nameId;
    }
    if (outSliderId != NULL)
    {
        *outSliderId = Self->_sliderId;
    }
    if (outBoxId != NULL)
    {
        *outBoxId = Self->_boxId;
    }
    return Error_CreateSuccess();
}

Error UICompoundSlider_SetShows(UIScreen* screen, uint64_t widgetId, bool showsName, bool showsSlider, bool showsBox)
{
    Widget* Target = NULL;
    UICompoundSliderCapability* Self = NULL;
    Error Result = ResolveCompound(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_showsName = showsName;
    Self->_showsSlider = showsSlider;
    Self->_showsBox = showsBox;
    uint64_t Parts[3] = { Self->_nameId, Self->_sliderId, Self->_boxId };
    bool Shows[3] = { showsName, showsSlider, showsBox };
    for (size_t Index = 0U; (Index < 3U) && (Result.Code == ErrorCode_Success); Index++)
    {
        if (!UIScreen_TryGetWidgetById(screen, widgetId, &Target))
        {
            break;
        }
        Result = ShowPart(Target, Parts[Index], Shows[Index]);
    }
    return Result;
}

Error UICompoundSlider_SetOrientation(UIScreen* screen, uint64_t widgetId, UISliderOrientation orientation)
{
    Widget* Target = NULL;
    UICompoundSliderCapability* Self = NULL;
    Error Result = ResolveCompound(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsOrientationValid(orientation))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UICompoundSlider_SetOrientation: the orientation is none of UISliderOrientation.");
    }
    Self->_orientation = orientation;
    Widget_InvalidateMeasure(Target);
    Result = UISlider_SetOrientation(screen, Self->_sliderId, orientation);
    if (Result.Code == ErrorCode_InvalidOperation)
    {
        Error_Deconstruct(&Result); // The slider was taken away: the compound turns without it.
        return Error_CreateSuccess();
    }
    return Result;
}

Error UICompoundSlider_SetName(UIScreen* screen, uint64_t widgetId, const unsigned char* name)
{
    Widget* Target = NULL;
    UICompoundSliderCapability* Self = NULL;
    Error Result = ResolveCompound(screen, widgetId, &Target, &Self);
    return (Result.Code == ErrorCode_Success) ? UILabel_SetText(screen, Self->_nameId, (name != NULL) ? name : (const unsigned char*)u8"") : Result;
}


// Public functions: behaviour helpers.
void UICompoundSliderCapability_SetMetrics(UICompoundSliderCapability* self, Widget* widget, float spacing, float boxWidth)
{
    float Spacing = ToLength(spacing);
    float BoxWidth = ToLength(boxWidth);
    if ((self->_spacing != Spacing) || (self->_boxWidth != BoxWidth))
    {
        self->_spacing = Spacing;
        self->_boxWidth = BoxWidth;
        Widget_InvalidateMeasure(widget);
    }
}

Error UICompoundSlider_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    const UICompoundSliderCapability* Self = GetCapability(widget);
    Widget* Name = NULL;
    Widget* Slider = NULL;
    Widget* Box = NULL;
    GetParts(widget, &Name, &Slider, &Box);
    Vector2 Sizes[3] = { { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } };
    Widget* Parts[3] = { Name, Slider, Box };
    size_t Shown = 0U;
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Error Result = MeasurePart(Parts[Index], context, availableWidth, &Sizes[Index]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Shown += (Parts[Index] != NULL) ? 1U : 0U;
    }
    float Gaps = (Shown > 1U) ? (Self->_spacing * (float)(Shown - 1U)) : 0.0f;
    if (Self->_orientation == UISliderOrientation_Vertical)
    {
        *outSize = (Vector2){ .x = Math_MaxFloat(Sizes[0].x, Math_MaxFloat(Sizes[1].x, Sizes[2].x)), .y = Sizes[0].y + Sizes[1].y + Sizes[2].y + Gaps };
        return Error_CreateSuccess();
    }
    float BoxWidth = (Box != NULL) ? Math_MaxFloat(Self->_boxWidth, 0.0f) : 0.0f;
    *outSize = (Vector2){ .x = Sizes[0].x + Sizes[1].x + BoxWidth + Gaps, .y = Math_MaxFloat(Sizes[0].y, Math_MaxFloat(Sizes[1].y, Sizes[2].y)) };
    return Error_CreateSuccess();
}

Error UICompoundSlider_Arrange(Widget* widget, const UILayoutContext* context)
{
    const UICompoundSliderCapability* Self = GetCapability(widget);
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    Vector2 Area = { .x = Pixels.width / Scale, .y = Pixels.height / Scale };
    Widget* Name = NULL;
    Widget* Slider = NULL;
    Widget* Box = NULL;
    GetParts(widget, &Name, &Slider, &Box);
    Vector2 NameSize = { 0.0f, 0.0f };
    Vector2 BoxSize = { 0.0f, 0.0f };
    Error Result = MeasurePart(Name, context->Sizing, UI_MEASURE_UNBOUNDED_WIDTH, &NameSize);
    if (Result.Code == ErrorCode_Success)
    {
        Result = MeasurePart(Box, context->Sizing, Area.x, &BoxSize);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIPlacement Placements[3];
    if (Self->_orientation == UISliderOrientation_Vertical)
    {
        float Top = (Name != NULL) ? (NameSize.y + Self->_spacing) : 0.0f;
        float Bottom = (Box != NULL) ? (BoxSize.y + Self->_spacing) : 0.0f;
        Placements[0] = UIPlacement_FromTopLeft(0.0f, 0.0f, Area.x, NameSize.y);
        Placements[1] = UIPlacement_FromTopLeft(0.0f, Top, Area.x, Math_MaxFloat(0.0f, Area.y - Top - Bottom));
        Placements[2] = UIPlacement_FromTopLeft(0.0f, Math_MaxFloat(Top, Area.y - BoxSize.y), Area.x, BoxSize.y);
    }
    else
    {
        float Left = (Name != NULL) ? (NameSize.x + Self->_spacing) : 0.0f;
        float BoxWidth = (Box != NULL) ? ((Slider != NULL) ? Self->_boxWidth : Math_MaxFloat(0.0f, Area.x - Left)) : 0.0f;
        float Right = (Box != NULL) ? (BoxWidth + ((Slider != NULL) ? Self->_spacing : 0.0f)) : 0.0f;
        Placements[0] = UIPlacement_FromTopLeft(0.0f, Math_MaxFloat(0.0f, (Area.y - NameSize.y) * 0.5f), NameSize.x, Math_MinFloat(NameSize.y, Area.y));
        Placements[1] = UIPlacement_FromTopLeft(Left, 0.0f, Math_MaxFloat(0.0f, Area.x - Left - Right), Area.y);
        Placements[2] = UIPlacement_FromTopLeft(Math_MaxFloat(Left, Area.x - BoxWidth), 0.0f, BoxWidth, Area.y);
    }
    Widget* Parts[3] = { Name, Slider, Box };
    for (size_t Index = 0U; (Index < 3U) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = (Parts[Index] != NULL) ? Widget_SetPlacement(Parts[Index], &Placements[Index]) : Error_CreateSuccess();
    }
    return Result;
}

Error UICompoundSlider_BindParts(UIScreen* screen, uint64_t widgetId, uint64_t nameId, uint64_t sliderId, uint64_t boxId)
{
    Widget* Target = NULL;
    UICompoundSliderCapability* Self = NULL;
    Error Result = ResolveCompound(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t Parts[2] = { sliderId, boxId };
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        Widget* Part = NULL;
        UIValueCapability* Value = NULL;
        Result = UIValue_Resolve(screen, Parts[Index], &Part, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        UIValueCapability_SetEventTarget(Value, widgetId, false, 0);
    }
    Self->_nameId = nameId;
    Self->_sliderId = sliderId;
    Self->_boxId = boxId;
    return UICompoundSlider_SetShows(screen, widgetId, Self->_showsName, Self->_showsSlider, Self->_showsBox);
}
