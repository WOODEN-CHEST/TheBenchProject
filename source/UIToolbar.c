#include "UIToolbar.h"
#include "UIButton.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIScreen.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Static functions: helpers.
static UIToolbarCapability* GetCapability(void* widget)
{
    return &((UIToolbarLookBase*)widget)->Toolbar;
}

static Error ResolveBar(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIToolbarCapability** outBar)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIToolbar: the widget is gone, or its screen has no kit.");
    }
    UIToolbarCapability* Bar = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Toolbar));
    if (Bar == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIToolbar: the widget is not a tool bar.");
    }
    *outWidget = Target;
    *outBar = Bar;
    return Error_CreateSuccess();
}

static UIToolbarEntry* GetEntryAt(UIToolbarCapability* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_entries, index);
}

/* The item of an id, separators aside; NULL for none. */
static UIToolbarEntry* FindItem(UIToolbarCapability* self, int32_t itemId)
{
    for (size_t Index = 0U; Index < self->_entries._count; Index++)
    {
        UIToolbarEntry* Entry = GetEntryAt(self, Index);
        if ((Entry->Kind != UIToolbarItemKind_Separator) && (Entry->Id == itemId))
        {
            return Entry;
        }
    }
    return NULL;
}

static bool HasState(UIToolbarItemKind kind)
{
    return (kind == UIToolbarItemKind_Toggle) || (kind == UIToolbarItemKind_Choice);
}

static float CleanLength(float value)
{
    return (Math_IsNaNFloat(value) || Math_IsInfinityFloat(value) || !(value > 0.0f)) ? 0.0f : value;
}

static UserData MakeIdData(uint64_t id)
{
    UserData Data = UserData_CreateEmpty();
    Memory_Copy(&id, Data.Bytes, sizeof(id));
    return Data;
}

static uint64_t ReadIdData(const UserData* data)
{
    uint64_t Id = 0U;
    Memory_Copy(data->Bytes, &Id, sizeof(Id));
    return Id;
}

/* Turns an item on or off; taking a choice lets go of the others of its group. */
static Error SetToggled(UIScreen* screen, UIToolbarCapability* self, const UIToolbarEntry* entry, bool isToggled)
{
    Error Result = UIButton_SetIsToggled(screen, entry->ButtonId, isToggled);
    if ((Result.Code != ErrorCode_Success) || (entry->Kind != UIToolbarItemKind_Choice) || !isToggled)
    {
        return Result;
    }
    for (size_t Index = 0U; Index < self->_entries._count; Index++)
    {
        const UIToolbarEntry* Other = GetEntryAt(self, Index);
        if ((Other != entry) && (Other->Kind == UIToolbarItemKind_Choice) && (Other->Group == entry->Group))
        {
            Error Released = UIButton_SetIsToggled(screen, Other->ButtonId, false);
            Error_Deconstruct(&Released); // A button calling code took away holds no state to keep.
        }
    }
    return Error_CreateSuccess();
}

/* A button of the bar was clicked: the bar posts what its item does. */
static Error OnButtonEvent(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    if (event->WidgetEventId != (int32_t)UIElementEvent_Activated)
    {
        return Error_CreateSuccess();
    }
    Widget* Target = NULL;
    UIToolbarCapability* Self = NULL;
    uint64_t BarId = ReadIdData(userData);
    Error Result = ResolveBar(screen, BarId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess(); // Gone with its buttons.
    }
    UIToolbarEntry* Entry = NULL;
    for (size_t Index = 0U; (Index < Self->_entries._count) && (Entry == NULL); Index++)
    {
        Entry = (GetEntryAt(Self, Index)->ButtonId == event->WidgetId) ? GetEntryAt(Self, Index) : NULL;
    }
    if (Entry == NULL)
    {
        return Error_CreateSuccess();
    }
    if (Entry->Kind == UIToolbarItemKind_Button)
    {
        return UIScreen_PostWidgetIndexedEvent(screen, BarId, UIElementEvent_Activated, NULL, Entry->Id);
    }
    bool IsToggled = false;
    Result = UIButton_IsToggled(screen, Entry->ButtonId, &IsToggled);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    if ((Entry->Kind == UIToolbarItemKind_Choice) && IsToggled)
    {
        return Error_CreateSuccess(); // Already taken: nothing changes.
    }
    int32_t ItemId = Entry->Id;
    Result = SetToggled(screen, Self, Entry, !IsToggled);
    UIPropertyValue Value = UIPropertyValue_FromBool(!IsToggled);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_PostWidgetIndexedEvent(screen, BarId, UIElementEvent_Changed, &Value, ItemId);
    }
    return (Result.Code == ErrorCode_Success) ? UIScreen_PostWidgetIndexedEvent(screen, BarId, UIElementEvent_Committed, &Value, ItemId) : Result;
}

/* A button's size along the bar and across it: the metrics' size or its own measure, whichever is more. */
static Error MeasureButton(const UIScreen* screen, const UIToolbarEntry* entry, const UIMeasureContext* context, const UIToolbarMetrics* metrics,
    bool isVertical, float* outAlong, float* outAcross)
{
    Widget* Button = NULL;
    *outAlong = 0.0f;
    *outAcross = 0.0f;
    if (entry->Kind == UIToolbarItemKind_Separator)
    {
        *outAlong = CleanLength(metrics->SeparatorSpacing);
        return Error_CreateSuccess();
    }
    if (!UIScreen_TryGetWidgetById((UIScreen*)screen, entry->ButtonId, &Button))
    {
        return Error_CreateSuccess(); // Taken away by calling code: no room.
    }
    Vector2 Size = { .x = 0.0f, .y = 0.0f };
    Error Result = Widget_Measure(Button, context, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float Along = isVertical ? Size.y : Size.x;
    float Across = isVertical ? Size.x : Size.y;
    *outAlong = Math_MaxFloat(CleanLength(isVertical ? metrics->ButtonSize.y : metrics->ButtonSize.x), CleanLength(Along));
    *outAcross = Math_MaxFloat(CleanLength(isVertical ? metrics->ButtonSize.x : metrics->ButtonSize.y), CleanLength(Across));
    return Error_CreateSuccess();
}

/* Whether a spacing goes before an item: between two buttons side by side, never by a separator. */
static bool IsSpacedFrom(const UIToolbarEntry* previous, const UIToolbarEntry* entry)
{
    return (previous != NULL) && (previous->Kind != UIToolbarItemKind_Separator) && (entry->Kind != UIToolbarItemKind_Separator);
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
        return Error_Construct2(ErrorCode_IllegalArgument, "UIToolbar: the arguments are required.");
    }
    const UIToolbarOptions* Options = ((const UIToolbarArgs*)args)->Options;
    if (!UIToolbarOptions_IsValid(Options))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIToolbar: at most UI_TOOLBAR_MAX_ITEMS items of known kinds with valid icons and texts (an icon or a text each) and different ids, at most one choice of a group taken, and a row or a column are required.");
    }
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIToolbarCapability* Self = GetCapability(widget);
    Self->_direction = Options->Direction;
    GenericBuffer_AllocateVariable(&Self->_entries, Math_MaxSizeT(Options->ItemCount, 1U), sizeof(UIToolbarEntry));
    for (size_t Index = 0U; Index < Options->ItemCount; Index++)
    {
        const UIToolbarItem* Item = &Options->Items[Index];
        UIToolbarEntry Entry = { .Kind = Item->Kind, .Id = (Item->Kind == UIToolbarItemKind_Separator) ? 0 : Item->Id, .Group = Item->Group,
            .ButtonId = WIDGET_ID_INVALID, .Start = 0.0f, .Length = 0.0f };
        if (!GenericBuffer_AddLast(&Self->_entries, &Entry))
        {
            Memory_Free(Self->_entries._data);
            return Error_Construct2(ErrorCode_BufferTooLarge, "UIToolbar: its items do not fit in memory.");
        }
    }
    // Cut to its box: a bar shorter than its items cuts the last ones off, neither drawn nor hit past its end.
    Result = Widget_SetAreSubWidgetsCut(widget, true);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Self->_entries._data); // A failed construction leaves nothing to release.
    }
    return Result;
}

static Error Behaviour_Deconstruct(void* widget)
{
    Memory_Free(GetCapability(widget)->_entries._data);
    return Error_CreateSuccess();
}

static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    UIToolbarMetrics Metrics = UIToolbarMetrics_CreateDefault();
    return UIToolbar_Arrange(widget, context, &Metrics);
}


// Fields.
static const UIKindCapability ToolbarCapabilities[] = { { .Capability = UIElementCapability_Toolbar, .Resolve = ResolveCapability } };

static const UIKindBehaviour ToolbarBehaviour =
{
    .Kind = UIElementKind_Toolbar,
    .Capabilities = ToolbarCapabilities,
    .CapabilityCount = sizeof(ToolbarCapabilities) / sizeof(ToolbarCapabilities[0]),
    .BaseSize = sizeof(UIToolbarLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Deconstruct = Behaviour_Deconstruct,
    .Layout = Behaviour_Layout
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIToolbar_GetKindBehaviour(void)
{
    return &ToolbarBehaviour;
}


// Public functions: helpers.
bool UIToolbarOptions_IsValid(const UIToolbarOptions* options)
{
    if (((options->Items == NULL) && (options->ItemCount > 0U)) || (options->ItemCount > UI_TOOLBAR_MAX_ITEMS)
        || ((options->Direction != UIStackDirection_Row) && (options->Direction != UIStackDirection_Column))
        || ((options->ButtonVariant != NULL) && !StringUTF8_IsEncodingValid(options->ButtonVariant)))
    {
        return false;
    }
    for (size_t Index = 0U; Index < options->ItemCount; Index++)
    {
        const UIToolbarItem* Item = &options->Items[Index];
        if (((int32_t)Item->Kind < 0) || ((int32_t)Item->Kind >= (int32_t)UIToolbarItemKind_Count))
        {
            return false;
        }
        if (Item->Kind == UIToolbarItemKind_Separator)
        {
            continue;
        }
        bool HasText = (Item->Text != NULL) && (Item->Text[0] != 0U);
        if (!UIElementIcon_IsValid(&Item->Icon) || ((Item->Text != NULL) && !StringUTF8_IsEncodingValid(Item->Text))
            || ((Item->Name != NULL) && !StringUTF8_IsEncodingValid(Item->Name)) || ((Item->Icon.Kind == UIElementIconKind_None) && !HasText))
        {
            return false; // Each button shows something: an icon, a text or both.
        }
        for (size_t Other = 0U; Other < Index; Other++)
        {
            const UIToolbarItem* Earlier = &options->Items[Other];
            bool IsSameId = (Earlier->Kind != UIToolbarItemKind_Separator) && (Earlier->Id == Item->Id);
            bool IsSecondTaken = (Item->Kind == UIToolbarItemKind_Choice) && Item->IsToggled && (Earlier->Kind == UIToolbarItemKind_Choice)
                && Earlier->IsToggled && (Earlier->Group == Item->Group);
            if (IsSameId || IsSecondTaken)
            {
                return false;
            }
        }
    }
    return true;
}

Error UIToolbar_Arrange(Widget* widget, const UILayoutContext* context, const UIToolbarMetrics* metrics)
{
    UIToolbarCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    bool IsVertical = UIToolbarCapability_IsVertical(Self);
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Screen, widget);
    float BoxAcross = (IsVertical ? Pixels.width : Pixels.height) / Scale;
    float PaddingAlong = CleanLength(IsVertical ? metrics->Padding.y : metrics->Padding.x);
    float PaddingAcross = CleanLength(IsVertical ? metrics->Padding.x : metrics->Padding.y);
    // Each item's length along the bar, and the widest button across it.
    float Across = CleanLength(IsVertical ? metrics->ButtonSize.x : metrics->ButtonSize.y);
    for (size_t Index = 0U; Index < Self->_entries._count; Index++)
    {
        UIToolbarEntry* Entry = GetEntryAt(Self, Index);
        float ButtonAcross = 0.0f;
        Error Result = MeasureButton(Screen, Entry, context->Sizing, metrics, IsVertical, &Entry->Length, &ButtonAcross);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Across = Math_MaxFloat(Across, ButtonAcross);
    }
    // In their order from the padding, centred across the bar.
    float AcrossStart = Math_MaxFloat(PaddingAcross, (BoxAcross - Across) * 0.5f);
    float Cursor = PaddingAlong;
    const UIToolbarEntry* Previous = NULL;
    for (size_t Index = 0U; Index < Self->_entries._count; Index++)
    {
        UIToolbarEntry* Entry = GetEntryAt(Self, Index);
        Cursor += IsSpacedFrom(Previous, Entry) ? CleanLength(metrics->Spacing) : 0.0f;
        Entry->Start = Cursor;
        Cursor += Entry->Length;
        Previous = Entry;
        Widget* Button = NULL;
        if ((Entry->Kind == UIToolbarItemKind_Separator) || !UIScreen_TryGetWidgetById(Screen, Entry->ButtonId, &Button))
        {
            continue;
        }
        UIPlacement Placement = IsVertical ? UIPlacement_FromTopLeft(AcrossStart, Entry->Start, Across, Entry->Length)
            : UIPlacement_FromTopLeft(Entry->Start, AcrossStart, Entry->Length, Across);
        Error Result = Widget_SetPlacement(Button, &Placement);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

Error UIToolbar_Measure(Widget* widget, const UIMeasureContext* context, const UIToolbarMetrics* metrics, Vector2* outSize)
{
    UIToolbarCapability* Self = GetCapability(widget);
    const UIScreen* Screen = Widget_GetScreen(widget);
    bool IsVertical = UIToolbarCapability_IsVertical(Self);
    float Along = 0.0f;
    float Across = CleanLength(IsVertical ? metrics->ButtonSize.x : metrics->ButtonSize.y);
    const UIToolbarEntry* Previous = NULL;
    for (size_t Index = 0U; Index < Self->_entries._count; Index++)
    {
        const UIToolbarEntry* Entry = GetEntryAt(Self, Index);
        float Length = 0.0f;
        float ButtonAcross = 0.0f;
        Error Result = MeasureButton(Screen, Entry, context, metrics, IsVertical, &Length, &ButtonAcross);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Along += Length + (IsSpacedFrom(Previous, Entry) ? CleanLength(metrics->Spacing) : 0.0f);
        Across = Math_MaxFloat(Across, ButtonAcross);
        Previous = Entry;
    }
    float PaddingAlong = CleanLength(IsVertical ? metrics->Padding.y : metrics->Padding.x);
    float PaddingAcross = CleanLength(IsVertical ? metrics->Padding.x : metrics->Padding.y);
    Along += 2.0f * PaddingAlong;
    Across += 2.0f * PaddingAcross;
    *outSize = IsVertical ? (Vector2){ .x = Across, .y = Along } : (Vector2){ .x = Along, .y = Across };
    return Error_CreateSuccess();
}

Error UIToolbar_AttachButton(UIScreen* screen, uint64_t widgetId, size_t index, uint64_t buttonId)
{
    Widget* Target = NULL;
    UIToolbarCapability* Self = NULL;
    Error Result = ResolveBar(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((index >= Self->_entries._count) || (GetEntryAt(Self, index)->Kind == UIToolbarItemKind_Separator))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIToolbar_AttachButton: no item, or a separator, at that index.");
    }
    GetEntryAt(Self, index)->ButtonId = buttonId;
    UserData Data = MakeIdData(widgetId);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    return UIScreen_Subscribe(screen, buttonId, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnButtonEvent, &Data, &Subscription);
}


// Public functions: calling code.
Error UIToolbar_SetIsToggled(UIScreen* screen, uint64_t widgetId, int32_t itemId, bool isToggled)
{
    Widget* Target = NULL;
    UIToolbarCapability* Self = NULL;
    Error Result = ResolveBar(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIToolbarEntry* Entry = FindItem(Self, itemId);
    if ((Entry == NULL) || !HasState(Entry->Kind))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIToolbar_SetIsToggled: the bar has no toggle or choice of that id.");
    }
    return SetToggled(screen, Self, Entry, isToggled);
}

Error UIToolbar_IsToggled(UIScreen* screen, uint64_t widgetId, int32_t itemId, bool* outIsToggled)
{
    Widget* Target = NULL;
    UIToolbarCapability* Self = NULL;
    Error Result = ResolveBar(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIToolbarEntry* Entry = FindItem(Self, itemId);
    if ((Entry == NULL) || !HasState(Entry->Kind))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIToolbar_IsToggled: the bar has no toggle or choice of that id.");
    }
    return UIButton_IsToggled(screen, Entry->ButtonId, outIsToggled);
}

Error UIToolbar_SetIsEnabled(UIScreen* screen, uint64_t widgetId, int32_t itemId, bool isEnabled)
{
    Widget* Target = NULL;
    UIToolbarCapability* Self = NULL;
    Error Result = ResolveBar(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIToolbarEntry* Entry = FindItem(Self, itemId);
    if (Entry == NULL)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIToolbar_SetIsEnabled: the bar has no item of that id.");
    }
    return UIButton_SetIsEnabled(screen, Entry->ButtonId, isEnabled);
}

Error UIToolbar_GetButtonId(UIScreen* screen, uint64_t widgetId, int32_t itemId, uint64_t* outButtonId)
{
    Widget* Target = NULL;
    UIToolbarCapability* Self = NULL;
    Error Result = ResolveBar(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIToolbarEntry* Entry = FindItem(Self, itemId);
    if (Entry == NULL)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIToolbar_GetButtonId: the bar has no item of that id.");
    }
    *outButtonId = Entry->ButtonId;
    return Error_CreateSuccess();
}

Error UIToolbar_SetDirection(UIScreen* screen, uint64_t widgetId, UIStackDirection direction)
{
    Widget* Target = NULL;
    UIToolbarCapability* Self = NULL;
    Error Result = ResolveBar(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((direction != UIStackDirection_Row) && (direction != UIStackDirection_Column))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIToolbar_SetDirection: a row or a column is required.");
    }
    if (Self->_direction != direction)
    {
        Self->_direction = direction;
        Widget_InvalidateMeasure(Target);
    }
    return Error_CreateSuccess();
}
