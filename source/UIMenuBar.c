#include "UIMenuBar.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIMenuInternal.h"
#include "UIPopup.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"


// Types.
/* What an entry is made with. */
typedef struct EntryArgsStruct
{
    UIKit* Kit;
    uint64_t BarId;
    const unsigned char* Label;
    const UIMenuItem* Items;
    size_t Count;
} EntryArgs;

/* One entry of a bar: its label and its menu's items. */
typedef struct MenuBarEntryStruct
{
    Widget Base;
    UIElementLook Look;
    UIElementText Label;
    UITextContent LabelContent;
    UIMenuData Data;
    UIPressable Press;
    uint64_t BarId;
} MenuBarEntry;


// Static functions: helpers.
static UIMenuBarCapability* GetCapability(void* widget)
{
    return &((UIMenuBarLookBase*)widget)->MenuBar;
}

static float RoundToPixel(float value)
{
    return Math_FloorFloat(value + 0.5f);
}

static Error ResolveBar(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIMenuBarCapability** outBar)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIMenuBar: the widget is gone, or its screen has no kit.");
    }
    UIMenuBarCapability* Bar = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_MenuBar));
    if (Bar == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIMenuBar: the widget is not a menu bar.");
    }
    *outWidget = Target;
    *outBar = Bar;
    return Error_CreateSuccess();
}

static MenuBarEntry* ResolveEntry(const UIScreen* screen, uint64_t id)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(screen, id, &Found) ? (MenuBarEntry*)(void*)Found : NULL;
}

/* Opens an entry's menu, closing another of the bar's first. */
static Error OpenEntryMenu(UIScreen* screen, uint64_t barId, uint64_t entryId, bool isFromKey)
{
    Widget* BarWidget = NULL;
    UIMenuBarCapability* Bar = NULL;
    Error Result = ResolveBar(screen, barId, &BarWidget, &Bar);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (UIScreen_IsPopupOpen(screen, Bar->_openMenuId))
    {
        uint64_t Open = Bar->_openMenuId;
        Bar->_openMenuId = WIDGET_ID_INVALID;
        Bar->_openEntryId = WIDGET_ID_INVALID;
        Result = UIScreen_ClosePopup(screen, Open);
        if ((Result.Code != ErrorCode_Success) || ((Result = ResolveBar(screen, barId, &BarWidget, &Bar)).Code != ErrorCode_Success))
        {
            return Result;
        }
    }
    MenuBarEntry* Entry = ResolveEntry(screen, entryId);
    if (Entry == NULL)
    {
        return Error_CreateSuccess();
    }
    UIMenuHandler Handler = Bar->_handler;
    UserData Data = Bar->_data;
    uint64_t MenuId = WIDGET_ID_INVALID;
    Result = UIMenu_InternalOpenForBar(screen, barId, entryId, Entry->Data.Items, Entry->Data.Count, Handler, &Data, isFromKey, &MenuId);
    if ((Result.Code == ErrorCode_Success) && (ResolveBar(screen, barId, &BarWidget, &Bar).Code == ErrorCode_Success))
    {
        Bar->_openMenuId = MenuId;
        Bar->_openEntryId = entryId;
    }
    return Result;
}

static Error CloseBarMenu(UIScreen* screen, UIMenuBarCapability* bar)
{
    uint64_t Open = bar->_openMenuId;
    bar->_openMenuId = WIDGET_ID_INVALID;
    bar->_openEntryId = WIDGET_ID_INVALID;
    return UIScreen_IsPopupOpen(screen, Open) ? UIScreen_ClosePopup(screen, Open) : Error_CreateSuccess();
}

/* Whether a bar's child is one of its entries. */
static bool IsEntry(const UIKit* kit, const Widget* child)
{
    return Widget_GetTypeId(child) == kit->_menuBarEntryType;
}


// Static functions: the bar's behaviour, for the kit's composed widget types.
static void* ResolveCapability(void* widget)
{
    return GetCapability(widget);
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIMenuBar: the arguments are required.");
    }
    const UIMenuBarOptions* Options = ((const UIMenuBarArgs*)args)->Options;
    UIMenuBarCapability* Self = GetCapability(widget);
    Self->_handler = Options->Handler;
    Self->_data = Options->Data;
    Self->_openMenuId = WIDGET_ID_INVALID;
    Self->_openEntryId = WIDGET_ID_INVALID;
    return UIElement_ApplyOptions(widget, &Options->Element);
}

static Error Behaviour_Update(void* widget, ProgramTime time)
{
    (void)time;
    UIMenuBarCapability* Self = GetCapability(widget);
    if ((Self->_openMenuId != WIDGET_ID_INVALID) && !UIScreen_IsPopupOpen(Widget_GetScreen(widget), Self->_openMenuId))
    {
        Self->_openMenuId = WIDGET_ID_INVALID; // Closed by the popup layer: a press outside, Escape, an item activated.
        Self->_openEntryId = WIDGET_ID_INVALID;
    }
    return Error_CreateSuccess();
}

static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    return UIMenuBar_ArrangeEntries(widget, context, (Vector2){ .x = 0.0f, .y = 0.0f });
}


// Static functions: the entries.
static Error Entry_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIMenuBar: an entry's arguments are required.");
    }
    const EntryArgs* Arguments = args;
    MenuBarEntry* Entry = widget;
    Error Result = UIMenu_InternalCopyItems(Arguments->Items, Arguments->Count, &Entry->Data);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementLook_Construct(&Entry->Look, UIElementKind_MenuBar, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITextContent_Construct(&Entry->LabelContent, Arguments->Label);
    }
    if (Result.Code != ErrorCode_Success)
    {
        UIMenu_InternalReleaseItems(&Entry->Data);
        return Result;
    }
    Result = UIElementText_Construct(&Entry->Label, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit), UIKit_GetLogger(Arguments->Kit));
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&Entry->LabelContent);
        UIMenu_InternalReleaseItems(&Entry->Data);
        return Result;
    }
    Entry->Press = UIPressable_Create();
    Entry->Press._clickMode = UIClickMode_OnPress;
    Entry->BarId = Arguments->BarId;
    return Error_CreateSuccess();
}

static Error Entry_OnDeconstruct(void* self)
{
    MenuBarEntry* Entry = self;
    UIMenu_InternalReleaseItems(&Entry->Data);
    UITextContent_Deconstruct(&Entry->LabelContent);
    return UIElementText_Deconstruct(&Entry->Label);
}

/* Whether the entry's menu is the bar's open one. */
static bool IsEntryOpen(const MenuBarEntry* entry)
{
    UIScreen* Screen = Widget_GetScreen(&entry->Base);
    Widget* BarWidget = NULL;
    UIMenuBarCapability* Bar = NULL;
    Error Result = ResolveBar(Screen, entry->BarId, &BarWidget, &Bar);
    bool IsOpen = (Result.Code == ErrorCode_Success) && (Bar->_openEntryId == Widget_GetId(&entry->Base)) && UIScreen_IsPopupOpen(Screen, Bar->_openMenuId);
    Error_Deconstruct(&Result);
    return IsOpen;
}

static Error Entry_Update(void* self, ProgramTime time)
{
    MenuBarEntry* Entry = self;
    UIScreen* Screen = Widget_GetScreen(&Entry->Base);
    UIPressable_Update(&Entry->Press, Screen);
    bool IsOpen = IsEntryOpen(Entry);
    UIElementLook_Update(&Entry->Look, Screen, UIElementState_Choose(true, UIPressable_IsPressed(&Entry->Press), false, false, IsOpen,
        Widget_IsHovered(&Entry->Base), Widget_IsFocused(&Entry->Base)), time.PassedTime);
    return UIElementText_Refresh(&Entry->Label, &Entry->Base, &Entry->LabelContent);
}

static Error Entry_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    MenuBarEntry* Entry = self;
    Error Result = UIElementText_Refresh(&Entry->Label, &Entry->Base, &Entry->LabelContent);
    const UIScreen* Screen = Widget_GetScreen(&Entry->Base);
    Vector2 Text = UIElementText_GetPixelSize(&Entry->Label, Screen);
    Vector2 Padding = UIThemeStyle_GetVector(UIElementLook_GetStyle(&Entry->Look, Screen), UIElementKind_MenuBar, UIMenuBarPart_EntryPadding);
    float Scale = Widget_GetEffectiveScale(&Entry->Base);
    outSize->x = (Text.x / Scale) + (2.0f * Padding.x);
    outSize->y = (Text.y / Scale) + (2.0f * Padding.y);
    return Result;
}

static Error Entry_Render(void* self, UIRenderContext* context)
{
    MenuBarEntry* Entry = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIScreen* Screen = Widget_GetScreen(&Entry->Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Entry->Look, Screen);
    Error Result = UIElementLook_DrawBox(&Entry->Look, Style, UIMenuBarPart_Entry, context, Pixels, Widget_GetEffectiveScale(&Entry->Base));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Vector2 Text = UIElementText_GetPixelSize(&Entry->Label, Screen);
    Color TextColor = UIElementLook_BlendColors(&Entry->Look, UIThemeStyle_GetColors(Style, UIMenuBarPart_Text), WHITE);
    return UIElementText_Draw(&Entry->Label, context, Pixels.x + RoundToPixel((Pixels.width - Text.x) * 0.5f),
        Pixels.y + RoundToPixel((Pixels.height - Text.y) * 0.5f), TextColor);
}

/* While another entry's menu is open, the pointer coming onto this one switches to its menu; resting there does nothing more, so the
 * keys may move on from it. */
static Error Entry_OnHoverStart(void* self, const WidgetHoverArgs* args)
{
    (void)args;
    MenuBarEntry* Entry = self;
    UIScreen* Screen = Widget_GetScreen(&Entry->Base);
    Error Result = UIElement_PostCue(&Entry->Base, UIElementCue_Hover);
    if ((Result.Code != ErrorCode_Success) || IsEntryOpen(Entry))
    {
        return Result;
    }
    Widget* BarWidget = NULL;
    UIMenuBarCapability* Bar = NULL;
    Result = ResolveBar(Screen, Entry->BarId, &BarWidget, &Bar);
    if ((Result.Code != ErrorCode_Success) || !UIScreen_IsPopupOpen(Screen, Bar->_openMenuId))
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    return OpenEntryMenu(Screen, Entry->BarId, Widget_GetId(&Entry->Base), false);
}

/* A left press opens the entry's menu at once, or closes it. */
static Error Entry_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    MenuBarEntry* Entry = self;
    UIScreen* Screen = Widget_GetScreen(&Entry->Base);
    Error Result = UIElement_PostMouseCues(&Entry->Base, args, true);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsActivated = false;
    UIPressable_OnMouseInput(&Entry->Press, &Entry->Base, args, true, &IsActivated);
    if (!IsActivated)
    {
        return Error_CreateSuccess();
    }
    if (IsEntryOpen(Entry))
    {
        Widget* BarWidget = NULL;
        UIMenuBarCapability* Bar = NULL;
        Result = ResolveBar(Screen, Entry->BarId, &BarWidget, &Bar);
        return (Result.Code == ErrorCode_Success) ? CloseBarMenu(Screen, Bar) : Result;
    }
    return OpenEntryMenu(Screen, Entry->BarId, Widget_GetId(&Entry->Base), false);
}

static Error Entry_OnPressLost(void* self, GameKey button)
{
    UIPressable_OnPressLost(&((MenuBarEntry*)self)->Press, button);
    return Error_CreateSuccess();
}

/* Enter or Space opens the entry's menu with its first item focused. */
static Error Entry_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    MenuBarEntry* Entry = self;
    bool IsActivated = false;
    UIPressable_OnKeyboardInput(&Entry->Press, &Entry->Base, args, true, outIsHandled, &IsActivated);
    return IsActivated ? OpenEntryMenu(Widget_GetScreen(&Entry->Base), Entry->BarId, Widget_GetId(&Entry->Base), true) : Error_CreateSuccess();
}


// Fields.
static const UIKindCapability MenuBarCapabilities[] = { { .Capability = UIElementCapability_MenuBar, .Resolve = ResolveCapability } };

static const UIKindBehaviour MenuBarBehaviour =
{
    .Kind = UIElementKind_MenuBar,
    .Capabilities = MenuBarCapabilities,
    .CapabilityCount = sizeof(MenuBarCapabilities) / sizeof(MenuBarCapabilities[0]),
    .BaseSize = sizeof(UIMenuBarLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Update = Behaviour_Update,
    .Layout = Behaviour_Layout
};

static const WidgetVTable EntryVTable =
{
    .OnDeconstruct = Entry_OnDeconstruct,
    .Update = Entry_Update,
    .Render = Entry_Render,
    .OnHoverStart = Entry_OnHoverStart,
    .OnMouseInput = Entry_OnMouseInput,
    .OnPressLost = Entry_OnPressLost,
    .OnKeyboardInput = Entry_OnKeyboardInput,
    .Measure = Entry_Measure
};


// Internal functions (declared in UIKitInternal.h and UIMenuInternal.h).
const UIKindBehaviour* UIMenuBar_GetKindBehaviour(void)
{
    return &MenuBarBehaviour;
}

Error UIMenuBar_InternalSwitch(UIScreen* screen, uint64_t barId, int32_t direction)
{
    Widget* BarWidget = NULL;
    UIMenuBarCapability* Bar = NULL;
    Error Result = ResolveBar(screen, barId, &BarWidget, &Bar);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIKit* Kit = UIKit_FromScreen(screen);
    uint64_t Entries[UI_MENU_MAX_ITEMS];
    size_t Count = 0U;
    size_t Open = GENERIC_BUFFER_INDEX_INVALID;
    for (size_t Index = 0U; (Index < Widget_GetSubWidgetCount(BarWidget)) && (Count < UI_MENU_MAX_ITEMS); Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(BarWidget, Index);
        if (IsEntry(Kit, Child))
        {
            Open = (Widget_GetId(Child) == Bar->_openEntryId) ? Count : Open;
            Entries[Count++] = Widget_GetId(Child);
        }
    }
    if ((Count == 0U) || (Open == GENERIC_BUFFER_INDEX_INVALID))
    {
        return Error_CreateSuccess();
    }
    size_t Next = (direction < 0) ? ((Open + Count - 1U) % Count) : ((Open + 1U) % Count);
    return OpenEntryMenu(screen, barId, Entries[Next], true);
}


// Public functions.
Error UIMenuBar_AddEntry(UIScreen* screen, uint64_t widgetId, const unsigned char* label, const UIMenuItem* items, size_t count, uint64_t* outEntryId)
{
    if (outEntryId != NULL)
    {
        *outEntryId = WIDGET_ID_INVALID;
    }
    if (label == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIMenuBar_AddEntry: the label must not be NULL.");
    }
    Widget* BarWidget = NULL;
    UIMenuBarCapability* Bar = NULL;
    Error Result = ResolveBar(screen, widgetId, &BarWidget, &Bar);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIKit* Kit = UIKit_FromScreen(screen);
    EntryArgs Arguments = { .Kit = Kit, .BarId = widgetId, .Label = label, .Items = items, .Count = count };
    Widget* Made = NULL;
    Result = UIWidgetFactory_CreateWidget(Kit->_factory, screen, Kit->_menuBarEntryType, &Arguments, &Made);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t EntryId = Widget_GetId(Made);
    Result = Widget_InitializeWidget(Made);
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, widgetId, &BarWidget) && UIScreen_TryGetWidgetById(screen, EntryId, &Made))
    {
        Result = Widget_AddSubWidget(BarWidget, Made);
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (UIScreen_TryGetWidgetById(screen, EntryId, &Made))
        {
            Error Cleanup = Widget_Destroy(Made);
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }
    if (outEntryId != NULL)
    {
        *outEntryId = EntryId;
    }
    return Error_CreateSuccess();
}

Error UIMenuBar_SetHandler(UIScreen* screen, uint64_t widgetId, UIMenuHandler handler, const UserData* userData)
{
    Widget* BarWidget = NULL;
    UIMenuBarCapability* Bar = NULL;
    Error Result = ResolveBar(screen, widgetId, &BarWidget, &Bar);
    if (Result.Code == ErrorCode_Success)
    {
        Bar->_handler = handler;
        Bar->_data = (userData != NULL) ? *userData : UserData_CreateEmpty();
    }
    return Result;
}

Error UIMenuBar_GetOpen(UIScreen* screen, uint64_t widgetId, uint64_t* outMenuId, uint64_t* outEntryId)
{
    Widget* BarWidget = NULL;
    UIMenuBarCapability* Bar = NULL;
    Error Result = ResolveBar(screen, widgetId, &BarWidget, &Bar);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsOpen = UIScreen_IsPopupOpen(screen, Bar->_openMenuId);
    *outMenuId = IsOpen ? Bar->_openMenuId : WIDGET_ID_INVALID;
    if (outEntryId != NULL)
    {
        *outEntryId = IsOpen ? Bar->_openEntryId : WIDGET_ID_INVALID;
    }
    return Error_CreateSuccess();
}

Error UIMenuBar_Close(UIScreen* screen, uint64_t widgetId)
{
    Widget* BarWidget = NULL;
    UIMenuBarCapability* Bar = NULL;
    Error Result = ResolveBar(screen, widgetId, &BarWidget, &Bar);
    return (Result.Code == ErrorCode_Success) ? CloseBarMenu(screen, Bar) : Result;
}

Error UIMenuBar_ArrangeEntries(Widget* widget, const UILayoutContext* context, Vector2 padding)
{
    UIKit* Kit = UIKit_FromScreen(Widget_GetScreen(widget));
    if (Kit == NULL)
    {
        return Error_CreateSuccess();
    }
    float Height = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget).height / Widget_GetEffectiveScale(widget);
    float X = padding.x;
    for (size_t Index = 0U; Index < Widget_GetSubWidgetCount(widget); Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        if (!IsEntry(Kit, Child))
        {
            continue;
        }
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Error Result = Widget_Measure(Child, context->Sizing, UI_MEASURE_UNBOUNDED_WIDTH, &Size); // Cached: an idle bar measures nothing.
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        UIPlacement Placement = UIPlacement_FromTopLeft(X, padding.y, Size.x, Math_MaxFloat(0.0f, Height - (2.0f * padding.y)));
        Result = Widget_SetPlacement(Child, &Placement);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        X += Size.x;
    }
    return Error_CreateSuccess();
}

Error UIMenuBar_MeasureEntries(Widget* widget, const UIMeasureContext* context, Vector2 padding, Vector2* outSize)
{
    UIKit* Kit = UIKit_FromScreen(Widget_GetScreen(widget));
    *outSize = (Vector2){ .x = 2.0f * padding.x, .y = 0.0f };
    for (size_t Index = 0U; (Kit != NULL) && (Index < Widget_GetSubWidgetCount(widget)); Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        if (!IsEntry(Kit, Child))
        {
            continue;
        }
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Error Result = Widget_Measure(Child, context, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        outSize->x += Size.x;
        outSize->y = Math_MaxFloat(outSize->y, Size.y);
    }
    outSize->y += 2.0f * padding.y;
    return Error_CreateSuccess();
}

Error UIMenuBar_RegisterTypes(UIKit* kit, UIWidgetFactory* factory)
{
    WidgetTypeDescriptor Entry = { .Name = u8"MenuBarEntry", .StructSize = sizeof(MenuBarEntry), .VTable = &EntryVTable, .Constructor = Entry_Construct,
        .IsFocusable = true };
    return UIWidgetFactory_RegisterType(factory, &Entry, &kit->_menuBarEntryType);
}
