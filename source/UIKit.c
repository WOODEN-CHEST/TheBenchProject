#include "UIKit.h"
#include "LabelWidget.h"
#include "Logger.h"
#include "UIKitInternal.h"
#include "UIWorkspaceInternal.h"
#include "UIMenuInternal.h"
#include "UIScreen.h"
#include "UIStandardLooks.h"
#include "UITooltip.h"
#include "UIWidgetFactory.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Types.
/* One registered look: a kind's widget type by name. A kind's standard type the kit does not compose (the label's, the tooltip's)
 * has no hooks. */
typedef struct UIKitLookStruct
{
    UIElementKind Kind;
    unsigned char Name[NAMESPACED_KEY_BUFFER_SIZE];
    uint64_t TypeId;
    /* The look's hooks and the kind's behaviour; both NULL for a type the kit does not compose. */
    const UILookVTable* Look;
    const UIKindBehaviour* Behaviour;
} UIKitLook;


// Static functions: the tables.
static bool IsKindValid(UIElementKind kind)
{
    return ((int32_t)kind >= 0) && ((int32_t)kind < (int32_t)UIElementKind_Count);
}

/* The behaviour of a kind whose looks the kit composes; NULL for the others. */
static const UIKindBehaviour* GetBehaviour(UIElementKind kind)
{
    switch (kind)
    {
        case UIElementKind_Panel:
            return UIPanel_GetKindBehaviour();
        case UIElementKind_Button:
            return UIButton_GetKindBehaviour();
        case UIElementKind_Toggle:
            return UIToggle_GetKindBehaviour();
        case UIElementKind_Slider:
            return UISlider_GetKindBehaviour();
        case UIElementKind_Scrollbar:
            return UIScrollbar_GetKindBehaviour();
        case UIElementKind_ScrollArea:
            return UIScrollArea_GetKindBehaviour();
        case UIElementKind_Dropdown:
            return UIDropdown_GetKindBehaviour();
        case UIElementKind_MenuBar:
            return UIMenuBar_GetKindBehaviour();
        case UIElementKind_StatusBar:
            return UIStatusBar_GetKindBehaviour();
        // TODO: the label's, the tooltip's, the menu's, the backdrop's and the drag's looks are not replaceable: the label is LabelWidget
        // itself, and the tooltip's box, a menu's list and items, a modal popup's backdrop and what shows a drag are plain types of the kit's
        // (the tooltip's, the backdrop's and the drag's named to the factory). Each needs a behaviour of its own split out first.
        case UIElementKind_TextBox:
            return UITextBox_GetKindBehaviour();
        case UIElementKind_PropertyBox:
            return UIPropertyBox_GetKindBehaviour();
        case UIElementKind_Labelled:
            return UILabelled_GetKindBehaviour();
        case UIElementKind_Form:
            return UIForm_GetKindBehaviour();
        case UIElementKind_CompoundSlider:
            return UICompoundSlider_GetKindBehaviour();
        case UIElementKind_VectorRow:
            return UIVectorRow_GetKindBehaviour();
        case UIElementKind_List:
            return UIList_GetKindBehaviour();
        case UIElementKind_Tree:
            return UITree_GetKindBehaviour();
        case UIElementKind_Dialog:
            return UIDialog_GetKindBehaviour();
        case UIElementKind_ColorPicker:
            return UIColorPicker_GetKindBehaviour();
        case UIElementKind_Toolbar:
            return UIToolbar_GetKindBehaviour();
        case UIElementKind_Window:
            return UIWindow_GetKindBehaviour();
        case UIElementKind_Workspace:
            return UIWorkspace_GetKindBehaviour();
        case UIElementKind_Label:
        case UIElementKind_Tooltip:
        case UIElementKind_Menu:
        case UIElementKind_Backdrop:
        case UIElementKind_Drag:
        case UIElementKind_Count:
            return NULL;
    }
    return NULL;
}

static const UIKitLook* GetLookAt(const UIKit* self, size_t index)
{
    return GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_looks, index);
}

/* The full form of a look's name, or false for one that is no valid key. */
static bool TryMakeFullName(const unsigned char* name, unsigned char* outName)
{
    GenericBuffer Name;
    GenericBuffer_CreateConstant(&Name, outName, NAMESPACED_KEY_BUFFER_SIZE, sizeof(unsigned char), 0U);
    Error Result = NamespacedKey_Parse(name, StringUTF8_GetByteLength(name), NULL, &Name);
    bool IsValid = Result.Code == ErrorCode_Success;
    Error_Deconstruct(&Result);
    return IsValid;
}

static bool IsSameName(const unsigned char* a, const unsigned char* b)
{
    bool IsSame = false;
    Error Compare = StringUTF8_EqualsExact(a, b, &IsSame);
    Error_Deconstruct(&Compare);
    return IsSame;
}

/* The look of a kind by its full name, or NULL. */
static const UIKitLook* FindLookByName(const UIKit* self, UIElementKind kind, const unsigned char* fullName)
{
    for (size_t Index = 0; Index < self->_looks._count; Index++)
    {
        const UIKitLook* Entry = GetLookAt(self, Index);
        if ((Entry->Kind == kind) && IsSameName(Entry->Name, fullName))
        {
            return Entry;
        }
    }
    return NULL;
}

/* Records a look under its widget type. False, with nothing recorded, when a table could not grow. */
static bool AddLook(UIKit* self, UIKitLook* look)
{
    size_t TypeIndex = (size_t)(look->TypeId - 1U);
    uint32_t None = 0U;
    while (self->_lookByType._count <= TypeIndex)
    {
        if (!GenericBuffer_AddLast(&self->_lookByType, &None))
        {
            return false;
        }
    }
    if (!GenericBuffer_AddLast(&self->_looks, look))
    {
        return false;
    }
    uint32_t Index = (uint32_t)self->_looks._count;
    GenericBuffer_Replace(&self->_lookByType, &Index, TypeIndex);
    return true;
}

/* The look a widget of the kit was made with: through its screen's factory's kit and its type. NULL when the kit is gone or the
 * type is no look of it. */
static const UIKitLook* FindLookOfWidget(const Widget* widget)
{
    const UIKit* Kit = UIWidgetFactory_GetKit(UIScreen_GetFactory(Widget_GetScreen(widget)));
    uint64_t TypeId = Widget_GetTypeId(widget);
    if ((Kit == NULL) || (TypeId == WIDGET_TYPE_ID_INVALID) || (TypeId > Kit->_lookByType._count))
    {
        return NULL;
    }
    uint32_t Index = *(const uint32_t*)GenericBuffer_GetPointerToElement((GenericBuffer*)&Kit->_lookByType, (size_t)(TypeId - 1U));
    return (Index != 0U) ? GetLookAt(Kit, Index - 1U) : NULL;
}

static Error KitGone(void)
{
    return Error_Construct2(ErrorCode_InvalidOperation, "UIKit: an element's kit is gone (screens go before their kit).");
}


// Static functions: the composed widget type. Every slot finds the widget's look, then calls the kind's hook and the look's. A slot
// that calls more than one copies the two vtables out first: a hook may register a look, which can move the kit's table.
static Error Composed_Construct(void* widget, void* args)
{
    const UIKitLook* Entry = FindLookOfWidget(widget);
    if ((Entry == NULL) || (Entry->Behaviour == NULL))
    {
        return KitGone();
    }
    const UIKindBehaviour* Behaviour = Entry->Behaviour;
    const UILookVTable* Look = Entry->Look;
    Error Result = (Behaviour->Construct != NULL) ? Behaviour->Construct(widget, args) : Error_CreateSuccess();
    if ((Result.Code != ErrorCode_Success) || (Look->Construct == NULL))
    {
        return Result;
    }
    Result = Look->Construct(widget, args);
    if ((Result.Code != ErrorCode_Success) && (Behaviour->Deconstruct != NULL))
    {
        Error Cleanup = Behaviour->Deconstruct(widget);
        Error_Deconstruct(&Cleanup);
    }
    return Result;
}

static Error Composed_OnDeconstruct(void* self)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if (Entry == NULL)
    {
        return KitGone();
    }
    const UIKindBehaviour* Behaviour = Entry->Behaviour;
    const UILookVTable* Look = Entry->Look;
    Error First = (Look->Deconstruct != NULL) ? Look->Deconstruct(self) : Error_CreateSuccess();
    Error Second = (Behaviour->Deconstruct != NULL) ? Behaviour->Deconstruct(self) : Error_CreateSuccess();
    if (First.Code == ErrorCode_Success)
    {
        return Second;
    }
    Error_Deconstruct(&Second);
    return First;
}

static Error Composed_Update(void* self, ProgramTime time)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if (Entry == NULL)
    {
        return KitGone();
    }
    const UILookVTable* Look = Entry->Look;
    Error Result = (Entry->Behaviour->Update != NULL) ? Entry->Behaviour->Update(self, time) : Error_CreateSuccess();
    if ((Result.Code != ErrorCode_Success) || (Look->Update == NULL))
    {
        return Result;
    }
    return Look->Update(self, time);
}

static Error Composed_Layout(void* self, const UILayoutContext* context)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if (Entry == NULL)
    {
        return KitGone();
    }
    if (Entry->Look->Layout != NULL)
    {
        return Entry->Look->Layout(self, context);
    }
    return (Entry->Behaviour->Layout != NULL) ? Entry->Behaviour->Layout(self, context) : Error_CreateSuccess();
}

static Error Composed_Render(void* self, UIRenderContext* context)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    return (Entry != NULL) ? Entry->Look->Render(self, context) : KitGone();
}

static Error Composed_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    return (Entry != NULL) ? Entry->Look->Measure(self, context, availableWidth, outSize) : KitGone();
}

static bool Composed_HitTest(void* self, const WidgetHitTestArgs* args)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if ((Entry == NULL) || (Entry->Look->HitTest == NULL))
    {
        return args->IsInsideBox;
    }
    return Entry->Look->HitTest(self, args);
}

static Error Composed_OnHoverStart(void* self, const WidgetHoverArgs* args)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if (Entry == NULL)
    {
        return KitGone();
    }
    return (Entry->Behaviour->OnHoverStart != NULL) ? Entry->Behaviour->OnHoverStart(self, args) : Error_CreateSuccess();
}

static Error Composed_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if (Entry == NULL)
    {
        return KitGone();
    }
    return (Entry->Behaviour->OnMouseInput != NULL) ? Entry->Behaviour->OnMouseInput(self, args) : Error_CreateSuccess();
}

static Error Composed_OnPressLost(void* self, GameKey button)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if (Entry == NULL)
    {
        return KitGone();
    }
    return (Entry->Behaviour->OnPressLost != NULL) ? Entry->Behaviour->OnPressLost(self, button) : Error_CreateSuccess();
}

static Error Composed_OnScrollInput(void* self, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if (Entry == NULL)
    {
        return KitGone();
    }
    return (Entry->Behaviour->OnScrollInput != NULL) ? Entry->Behaviour->OnScrollInput(self, args, outIsHandled) : Error_CreateSuccess();
}

static Error Composed_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if (Entry == NULL)
    {
        return KitGone();
    }
    return (Entry->Behaviour->OnKeyboardInput != NULL) ? Entry->Behaviour->OnKeyboardInput(self, args, outIsHandled)
        : Error_CreateSuccess();
}

static Error Composed_OnEnter(void* self, UIEnterCause cause)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if (Entry == NULL)
    {
        return KitGone();
    }
    return (Entry->Behaviour->OnEnter != NULL) ? Entry->Behaviour->OnEnter(self, cause) : Error_CreateSuccess();
}

static Error Composed_OnLeave(void* self, UILeaveReason reason)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if (Entry == NULL)
    {
        return KitGone();
    }
    return (Entry->Behaviour->OnLeave != NULL) ? Entry->Behaviour->OnLeave(self, reason) : Error_CreateSuccess();
}

static Error Composed_OnDescendantFocus(void* self, uint64_t widgetId)
{
    const UIKitLook* Entry = FindLookOfWidget(self);
    if (Entry == NULL)
    {
        return KitGone();
    }
    return (Entry->Behaviour->OnDescendantFocus != NULL) ? Entry->Behaviour->OnDescendantFocus(self, widgetId) : Error_CreateSuccess();
}

/* The look capability of a composed widget: its look's UIElementLook. Declared only for looks that have GetThemeLook. */
static void* Composed_ResolveThemeLook(void* widget)
{
    const UIKitLook* Entry = FindLookOfWidget(widget);
    return ((Entry != NULL) && (Entry->Look->GetThemeLook != NULL)) ? Entry->Look->GetThemeLook(widget) : NULL;
}


// Fields.
/* The one vtable of every composed widget type: each slot dispatches through the widget's look. */
static const WidgetVTable ComposedVTable =
{
    .OnDeconstruct = Composed_OnDeconstruct,
    .Update = Composed_Update,
    .Layout = Composed_Layout,
    .Render = Composed_Render,
    .OnHoverStart = Composed_OnHoverStart,
    .OnMouseInput = Composed_OnMouseInput,
    .OnPressLost = Composed_OnPressLost,
    .OnScrollInput = Composed_OnScrollInput,
    .OnKeyboardInput = Composed_OnKeyboardInput,
    .OnEnter = Composed_OnEnter,
    .OnLeave = Composed_OnLeave,
    .Measure = Composed_Measure,
    .HitTest = Composed_HitTest,
    .OnDescendantFocus = Composed_OnDescendantFocus
};

/* The names of the kinds' widget types, for the factory's messages. */
static const unsigned char* const KindTypeNames[UIElementKind_Count] =
{
    [UIElementKind_Panel] = u8"Panel",
    [UIElementKind_Label] = u8"UILabel",
    [UIElementKind_Button] = u8"Button",
    [UIElementKind_Toggle] = u8"Toggle",
    [UIElementKind_Slider] = u8"Slider",
    [UIElementKind_Tooltip] = u8"Tooltip",
    [UIElementKind_Scrollbar] = u8"Scrollbar",
    [UIElementKind_ScrollArea] = u8"ScrollArea",
    [UIElementKind_Dropdown] = u8"Dropdown",
    [UIElementKind_Menu] = u8"Menu",
    [UIElementKind_MenuBar] = u8"MenuBar",
    [UIElementKind_StatusBar] = u8"StatusBar",
    [UIElementKind_Backdrop] = u8"Backdrop",
    [UIElementKind_TextBox] = u8"TextBox",
    [UIElementKind_PropertyBox] = u8"PropertyBox",
    [UIElementKind_Labelled] = u8"Labelled",
    [UIElementKind_Form] = u8"Form",
    [UIElementKind_CompoundSlider] = u8"CompoundSlider",
    [UIElementKind_VectorRow] = u8"VectorRow",
    [UIElementKind_List] = u8"List",
    [UIElementKind_Tree] = u8"Tree",
    [UIElementKind_Dialog] = u8"Dialog",
    [UIElementKind_ColorPicker] = u8"ColorPicker",
    [UIElementKind_Toolbar] = u8"Toolbar",
    [UIElementKind_Drag] = u8"DragGhost",
    [UIElementKind_Window] = u8"Window",
    [UIElementKind_Workspace] = u8"Workspace",
};


// Static functions: construction.
/* Records a kind's standard type that the kit does not compose under the standard look's name. */
static Error AddStandardType(UIKit* self, UIElementKind kind, uint64_t typeId)
{
    UIKitLook Entry = { .Kind = kind, .TypeId = typeId, .Look = NULL, .Behaviour = NULL };
    Memory_Copy(UI_KIT_STANDARD_LOOK, Entry.Name, StringUTF8_GetByteLength(UI_KIT_STANDARD_LOOK) + 1U);
    if (!AddLook(self, &Entry))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIKit: the table of looks could not grow.");
    }
    return Error_CreateSuccess();
}

static Error RegisterEverything(UIKit* self)
{
    Error Result = Error_CreateSuccess();
    for (int32_t Index = 0; (Index < (int32_t)UIElementCapability_Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = UIWidgetFactory_RegisterCapability(self->_factory, &self->_capabilities[Index]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StackWidget_RegisterType(self->_factory, &self->_stackType);
    }
    uint64_t LabelType = WIDGET_TYPE_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = LabelWidget_RegisterElementType(self->_factory, self->_capabilities[UIElementCapability_Look],
            self->_capabilities[UIElementCapability_Label], &LabelType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddStandardType(self, UIElementKind_Label, LabelType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIStandardLooks_Register(self);
    }
    uint64_t TooltipType = WIDGET_TYPE_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIStandardLooks_RegisterTooltip(self, self->_factory, &TooltipType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddStandardType(self, UIElementKind_Tooltip, TooltipType);
    }
    uint64_t BackdropType = WIDGET_TYPE_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIStandardLooks_RegisterBackdrop(self, self->_factory, &BackdropType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddStandardType(self, UIElementKind_Backdrop, BackdropType);
    }
    uint64_t DragGhostType = WIDGET_TYPE_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIStandardLooks_RegisterDragGhost(self, self->_factory, &DragGhostType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddStandardType(self, UIElementKind_Drag, DragGhostType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIDropdown_RegisterTypes(self, self->_factory);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIMenu_RegisterTypes(self, self->_factory);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIMenuBar_RegisterTypes(self, self->_factory);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIList_RegisterTypes(self, self->_factory);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIColorPicker_RegisterTypes(self, self->_factory);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_RegisterTypes(self, self->_factory);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_RegisterTypes(self, self->_factory);
    }
    return Result;
}

/* The widget type an element is made with: the options' look, else the theme's for its kind, else the standard one. A name no
 * look answers is logged, once per name. */
static uint64_t ResolveLookType(UIKit* self, const UIScreen* screen, UIElementKind kind, const unsigned char* requested)
{
    const unsigned char* Name = (requested != NULL) ? requested : UITheme_GetLookName(UIScreen_GetTheme(screen), kind);
    uint64_t TypeId = WIDGET_TYPE_ID_INVALID;
    if ((Name == NULL) || UIKit_TryGetLookType(self, kind, Name, &TypeId))
    {
        return (Name == NULL) ? UIKit_GetElementType(self, kind) : TypeId;
    }
    unsigned char FullName[NAMESPACED_KEY_BUFFER_SIZE];
    bool IsValid = TryMakeFullName(Name, FullName);
    bool IsReported = false;
    for (size_t Index = 0; IsValid && (Index < self->_reportedLookCount); Index++)
    {
        IsReported = IsReported || IsSameName(self->_reportedLooks[Index], FullName);
    }
    if (IsValid && !IsReported && (self->_reportedLookCount < UI_KIT_MAX_REPORTED_LOOKS))
    {
        Memory_Copy(FullName, self->_reportedLooks[self->_reportedLookCount], sizeof(FullName));
        self->_reportedLookCount++;
        if (self->_logger != NULL)
        {
            Error LogResult = Logger_LogWarningFormatted(self->_logger,
                (const unsigned char*)u8"UI kit: no look \"%s\" for the element \"%s\": it gets the standard look.",
                (const char*)FullName, (const char*)UIElementKind_GetName(kind));
            Error_Deconstruct(&LogResult);
        }
    }
    return UIKit_GetElementType(self, kind);
}

/* The shared body of every UIKit_Create*: the type's constructor takes the arguments and applies the options, then the
 * element is initialized and attached, resolved by id again after each call that can run hooks. */
static Error CreateElement(UIKit* self, UIScreen* screen, uint64_t parentId, uint64_t typeId, void* args, uint64_t* outId)
{
    if ((self == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_Create: self, screen, the options and outId must not be NULL.");
    }
    *outId = WIDGET_ID_INVALID;
    Widget* Parent = NULL;
    Widget* Created = NULL;
    Error Result = UIWidgetFactory_CreateWidget(self->_factory, screen, typeId, args, &Created);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t Id = Widget_GetId(Created);
    Result = Widget_InitializeWidget(Created);
    if (Result.Code == ErrorCode_Success)
    {
        bool IsParentGone = (parentId != WIDGET_ID_INVALID) && !UIScreen_TryGetWidgetById(screen, parentId, &Parent);
        if (IsParentGone || !UIScreen_TryGetWidgetById(screen, Id, &Created))
        {
            Result = Error_Construct2(ErrorCode_InvalidOperation, "UIKit_Create: the parent is gone, or a hook took the element away.");
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = (parentId != WIDGET_ID_INVALID) ? Widget_AddSubWidget(Parent, Created) : UIScreen_AddWidget(screen, Created);
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (UIScreen_TryGetWidgetById(screen, Id, &Created))
        {
            Error Cleanup = Widget_Destroy(Created);
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }
    *outId = Id;
    return Error_CreateSuccess();
}

static void ReleaseTables(UIKit* self)
{
    Memory_Free(self->_looks._data);
    Memory_Free(self->_lookByType._data);
    Memory_Zero(&self->_looks, sizeof(self->_looks));
    Memory_Zero(&self->_lookByType, sizeof(self->_lookByType));
}


// Public functions.
Error UIKit_Construct(UIKit* self, UIWidgetFactory* factory, TextComponentFactory* textFactory, AssetManager* assets, Logger* logger)
{
    if ((self == NULL) || (factory == NULL) || (textFactory == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_Construct: self, factory and textFactory must not be NULL.");
    }
    if (UIWidgetFactory_GetKit(factory) != NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIKit_Construct: the factory has a kit already: one kit per factory.");
    }
    Memory_Zero(self, sizeof(*self));
    self->_factory = factory;
    self->_textFactory = textFactory;
    self->_assets = assets;
    self->_logger = logger;
    GenericBuffer_AllocateVariable(&self->_looks, 8U, sizeof(UIKitLook));
    GenericBuffer_AllocateVariable(&self->_lookByType, 16U, sizeof(uint32_t));
    Error Result = RegisterEverything(self);
    if (Result.Code != ErrorCode_Success)
    {
        ReleaseTables(self);
        return Result;
    }
    UIWidgetFactory_SetKit(factory, self);
    UIWidgetFactory_SetTooltipType(factory, UIKit_GetElementType(self, UIElementKind_Tooltip));
    UIWidgetFactory_SetBackdropType(factory, UIKit_GetElementType(self, UIElementKind_Backdrop));
    UIWidgetFactory_SetDragGhostType(factory, UIKit_GetElementType(self, UIElementKind_Drag));
    return Error_CreateSuccess();
}

Error UIKit_Deconstruct(UIKit* self)
{
    if ((self == NULL) || (self->_factory == NULL))
    {
        return Error_CreateSuccess();
    }
    // A widget of a look's type finds its hooks through the kit's tables: none may outlive them.
    for (size_t Index = 0; Index < self->_looks._count; Index++)
    {
        if (UIWidgetFactory_GetLiveWidgetCount(self->_factory, GetLookAt(self, Index)->TypeId) != 0U)
        {
            return Error_Construct2(ErrorCode_InvalidOperation,
                "UIKit_Deconstruct: widgets of the kit's looks are alive; deconstruct their screens first. The kit is left as it was.");
        }
    }
    UIWidgetFactory_SetKit(self->_factory, NULL);
    UIWidgetFactory_SetTooltipType(self->_factory, WIDGET_TYPE_ID_INVALID);
    UIWidgetFactory_SetBackdropType(self->_factory, WIDGET_TYPE_ID_INVALID);
    UIWidgetFactory_SetDragGhostType(self->_factory, WIDGET_TYPE_ID_INVALID);
    self->_factory = NULL;
    ReleaseTables(self);
    return Error_CreateSuccess();
}

UIKit* UIKit_FromScreen(const UIScreen* screen)
{
    return UIWidgetFactory_GetKit(UIScreen_GetFactory(screen));
}

uint64_t UIKit_GetCapabilityId(const UIKit* self, UIElementCapability capability)
{
    bool IsValid = ((int32_t)capability >= 0) && ((int32_t)capability < (int32_t)UIElementCapability_Count);
    return IsValid ? self->_capabilities[capability] : WIDGET_CAPABILITY_ID_INVALID;
}

uint64_t UIKit_GetElementType(const UIKit* self, UIElementKind kind)
{
    if (!IsKindValid(kind))
    {
        return WIDGET_TYPE_ID_INVALID;
    }
    const UIKitLook* Standard = FindLookByName(self, kind, UI_KIT_STANDARD_LOOK);
    return (Standard != NULL) ? Standard->TypeId : WIDGET_TYPE_ID_INVALID;
}

Error UIKit_RegisterLook(UIKit* self, UIElementKind kind, const unsigned char* lookName, size_t structSize, const UILookVTable* look,
    uint64_t* outTypeId)
{
    if ((self == NULL) || (lookName == NULL) || (look == NULL) || (look->Render == NULL) || (look->Measure == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "UIKit_RegisterLook: self, the name and the look must not be NULL, and a look needs Render and Measure.");
    }
    if (outTypeId != NULL)
    {
        *outTypeId = WIDGET_TYPE_ID_INVALID;
    }
    const UIKindBehaviour* Behaviour = IsKindValid(kind) ? GetBehaviour(kind) : NULL;
    if (Behaviour == NULL)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIKit_RegisterLook: the kind is unknown, or its look is not replaceable (the label, the tooltip).");
    }
    if (structSize < Behaviour->BaseSize)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIKit_RegisterLook: the look's struct must start with its kind's look base.");
    }
    UIKitLook Entry = { .Kind = kind, .TypeId = WIDGET_TYPE_ID_INVALID, .Look = look, .Behaviour = Behaviour };
    if (!TryMakeFullName(lookName, Entry.Name))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIKit_RegisterLook: the look's name is not a valid namespaced key.");
    }
    if (FindLookByName(self, kind, Entry.Name) != NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIKit_RegisterLook: the kind has a look by that name already.");
    }

    WidgetCapabilityEntry Capabilities[UI_KIND_MAX_CAPABILITIES + 1U];
    size_t CapabilityCount = 0U;
    for (size_t Index = 0U; (Index < Behaviour->CapabilityCount) && (Index < UI_KIND_MAX_CAPABILITIES); Index++)
    {
        Capabilities[CapabilityCount++] = (WidgetCapabilityEntry){ .CapabilityId = self->_capabilities[Behaviour->Capabilities[Index].Capability],
            .Resolver = Behaviour->Capabilities[Index].Resolve };
    }
    if (look->GetThemeLook != NULL)
    {
        Capabilities[CapabilityCount++] = (WidgetCapabilityEntry){ .CapabilityId = self->_capabilities[UIElementCapability_Look],
            .Resolver = Composed_ResolveThemeLook };
    }
    WidgetTypeDescriptor Descriptor =
    {
        .Name = KindTypeNames[kind],
        .StructSize = structSize,
        .VTable = &ComposedVTable,
        .Constructor = Composed_Construct,
        .Capabilities = Capabilities,
        .CapabilityCount = CapabilityCount,
        .Properties = NULL,
        .PropertyCount = 0U,
        .IsFocusable = Behaviour->IsFocusable,
        .NeedsEntering = Behaviour->NeedsEntering,
        .EntersItself = Behaviour->EntersItself
    };
    Error Result = UIWidgetFactory_RegisterType(self->_factory, &Descriptor, &Entry.TypeId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!AddLook(self, &Entry))
    {
        Error Cleanup = UIWidgetFactory_UnregisterType(self->_factory, Entry.TypeId);
        Error_Deconstruct(&Cleanup);
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIKit_RegisterLook: the table of looks could not grow.");
    }
    if (outTypeId != NULL)
    {
        *outTypeId = Entry.TypeId;
    }
    return Error_CreateSuccess();
}

bool UIKit_TryGetLookType(const UIKit* self, UIElementKind kind, const unsigned char* lookName, uint64_t* outTypeId)
{
    unsigned char FullName[NAMESPACED_KEY_BUFFER_SIZE];
    if ((self == NULL) || (lookName == NULL) || (outTypeId == NULL) || !IsKindValid(kind) || !TryMakeFullName(lookName, FullName))
    {
        return false;
    }
    const UIKitLook* Entry = FindLookByName(self, kind, FullName);
    if (Entry == NULL)
    {
        return false;
    }
    *outTypeId = Entry->TypeId;
    return true;
}

Error UIKit_CreateStack(UIKit* self, UIScreen* screen, uint64_t parentId, const UIStackOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateStack: self and options must not be NULL.");
    }
    return CreateElement(self, screen, parentId, self->_stackType, (void*)options, outId);
}

Error UIKit_CreatePanel(UIKit* self, UIScreen* screen, uint64_t parentId, const UIPanelOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreatePanel: self, screen and options must not be NULL.");
    }
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Panel, options->Element.Look);
    return CreateElement(self, screen, parentId, TypeId, (void*)options, outId);
}

Error UIKit_CreateLabel(UIKit* self, UIScreen* screen, uint64_t parentId, const UILabelOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateLabel: self, screen and options must not be NULL.");
    }
    LabelWidgetArgs Arguments =
    {
        .ComponentFactory = self->_textFactory,
        .Assets = self->_assets,
        .Logger = self->_logger,
        .Text = NULL,
        .ElementOptions = options
    };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Label, options->Element.Look);
    return CreateElement(self, screen, parentId, TypeId, &Arguments, outId);
}

Error UIKit_CreateButton(UIKit* self, UIScreen* screen, uint64_t parentId, const UIButtonOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateButton: self, screen and options must not be NULL.");
    }
    UIButtonArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Button, options->Element.Look);
    return CreateElement(self, screen, parentId, TypeId, &Arguments, outId);
}

Error UIKit_CreateToggle(UIKit* self, UIScreen* screen, uint64_t parentId, const UIToggleOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateToggle: self, screen and options must not be NULL.");
    }
    UIToggleArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Toggle, options->Element.Look);
    return CreateElement(self, screen, parentId, TypeId, &Arguments, outId);
}

static void DestroyPartlyMade(UIScreen* screen, uint64_t id);

Error UIKit_CreateSlider(UIKit* self, UIScreen* screen, uint64_t parentId, const UISliderOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateSlider: self, screen and options must not be NULL.");
    }
    UISliderArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Slider, options->Element.Look);
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, outId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIValue_AttachContextMenu(screen, *outId);
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, *outId);
        *outId = WIDGET_ID_INVALID;
    }
    return Result;
}

Error UIKit_CreateDropdown(UIKit* self, UIScreen* screen, uint64_t parentId, const UIDropdownOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateDropdown: self, screen and options must not be NULL.");
    }
    UIDropdownArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Dropdown, options->Element.Look);
    return CreateElement(self, screen, parentId, TypeId, &Arguments, outId);
}

Error UIKit_CreateMenuBar(UIKit* self, UIScreen* screen, uint64_t parentId, const UIMenuBarOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateMenuBar: self, screen and options must not be NULL.");
    }
    UIMenuBarArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_MenuBar, options->Element.Look);
    return CreateElement(self, screen, parentId, TypeId, &Arguments, outId);
}

Error UIKit_CreateStatusBar(UIKit* self, UIScreen* screen, uint64_t parentId, const UIStatusBarOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateStatusBar: self, screen, options and outId must not be NULL.");
    }
    UIStatusBarArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_StatusBar, options->Element.Look);
    uint64_t BarId = WIDGET_ID_INVALID;
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, &BarId);
    if (Result.Code != ErrorCode_Success)
    {
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    uint64_t Sections[UIStatusBarSection_Count] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID, WIDGET_ID_INVALID };
    for (int32_t Section = 0; (Section < (int32_t)UIStatusBarSection_Count) && (Result.Code == ErrorCode_Success); Section++)
    {
        UIStackOptions Row = UIStackOptions_Create(UIStackDirection_Row);
        Row.Element.HasPlacement = true;
        Row.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 0.0f, 0.0f);
        Result = UIKit_CreateStack(self, screen, BarId, &Row, &Sections[Section]);
    }
    Widget* Bar = NULL;
    if ((Result.Code != ErrorCode_Success) || !UIScreen_TryGetWidgetById(screen, BarId, &Bar))
    {
        DestroyPartlyMade(screen, BarId);
        *outId = WIDGET_ID_INVALID;
        return (Result.Code != ErrorCode_Success) ? Result
            : Error_Construct2(ErrorCode_InvalidOperation, "UIKit_CreateStatusBar: a hook took the bar away.");
    }
    // Every status bar look's struct starts with UIStatusBarLookBase (UIKit_RegisterLook checks its size).
    UIStatusBar_SetSections(&((UIStatusBarLookBase*)(void*)Bar)->StatusBar, Sections);
    *outId = BarId;
    return Error_CreateSuccess();
}

Error UIKit_CreateScrollbar(UIKit* self, UIScreen* screen, uint64_t parentId, const UIScrollbarOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateScrollbar: self, screen and options must not be NULL.");
    }
    UIScrollbarArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Scrollbar, options->Element.Look);
    return CreateElement(self, screen, parentId, TypeId, &Arguments, outId);
}

/* Destroys an element made in parts whose making failed half way, with its subtree. */
static void DestroyPartlyMade(UIScreen* screen, uint64_t id)
{
    Widget* Made = NULL;
    if (UIScreen_TryGetWidgetById(screen, id, &Made))
    {
        Error Cleanup = Widget_Destroy(Made);
        Error_Deconstruct(&Cleanup);
    }
}

/* Makes one of a scroll area's bars: placed by the area, on top of its content. */
static Error CreateAreaBar(UIKit* self, UIScreen* screen, uint64_t areaId, UIScrollAxis axis, uint64_t* outBarId)
{
    UIScrollbarOptions Bar = UIScrollbarOptions_Create();
    Bar.Axis = axis;
    Bar.TargetId = areaId;
    Bar.Element.HasPlacement = true;
    Bar.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 0.0f, 0.0f);
    Error Result = UIKit_CreateScrollbar(self, screen, areaId, &Bar, outBarId);
    Widget* Made = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, *outBarId, &Made))
    {
        Result = Widget_SetZLayer(Made, 1.0f);
    }
    return Result;
}

Error UIKit_CreateScrollArea(UIKit* self, UIScreen* screen, uint64_t parentId, const UIScrollAreaOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateScrollArea: self, screen, options and outId must not be NULL.");
    }
    UIScrollAreaArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_ScrollArea, options->Element.Look);
    uint64_t AreaId = WIDGET_ID_INVALID;
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, &AreaId);
    if (Result.Code != ErrorCode_Success)
    {
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    uint64_t BarIds[UIScrollAxis_Count] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID };
    Result = CreateAreaBar(self, screen, AreaId, UIScrollAxis_Horizontal, &BarIds[UIScrollAxis_Horizontal]);
    if (Result.Code == ErrorCode_Success)
    {
        Result = CreateAreaBar(self, screen, AreaId, UIScrollAxis_Vertical, &BarIds[UIScrollAxis_Vertical]);
    }
    Widget* Area = NULL;
    UIScrollableCapability* Scrollable = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, AreaId, &Area))
    {
        Scrollable = Widget_GetCapability(Area, self->_capabilities[UIElementCapability_Scrollable]);
    }
    if ((Result.Code != ErrorCode_Success) || (Scrollable == NULL))
    {
        DestroyPartlyMade(screen, AreaId);
        *outId = WIDGET_ID_INVALID;
        return (Result.Code != ErrorCode_Success) ? Result
            : Error_Construct2(ErrorCode_InvalidOperation, "UIKit_CreateScrollArea: a hook took the area away.");
    }
    // Every scroll area look's struct starts with UIScrollAreaLookBase (UIKit_RegisterLook checks its size).
    UIScrollArea_SetBars(&((UIScrollAreaLookBase*)(void*)Area)->Area, BarIds[UIScrollAxis_Horizontal], BarIds[UIScrollAxis_Vertical]);
    *outId = AreaId;
    return Error_CreateSuccess();
}

Error UIKit_CreateTextBox(UIKit* self, UIScreen* screen, uint64_t parentId, const UITextBoxOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateTextBox: self, screen, options and outId must not be NULL.");
    }
    UITextBoxArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_TextBox, options->Element.Look);
    uint64_t BoxId = WIDGET_ID_INVALID;
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, &BoxId);
    if (Result.Code != ErrorCode_Success)
    {
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    uint64_t BarIds[UIScrollAxis_Count] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID };
    Result = CreateAreaBar(self, screen, BoxId, UIScrollAxis_Horizontal, &BarIds[UIScrollAxis_Horizontal]);
    if (Result.Code == ErrorCode_Success)
    {
        Result = CreateAreaBar(self, screen, BoxId, UIScrollAxis_Vertical, &BarIds[UIScrollAxis_Vertical]);
    }
    Widget* Box = NULL;
    if ((Result.Code == ErrorCode_Success) && !UIScreen_TryGetWidgetById(screen, BoxId, &Box))
    {
        Result = Error_Construct2(ErrorCode_InvalidOperation, "UIKit_CreateTextBox: a hook took the box away.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        // Every text box look's struct starts with UITextBoxLookBase (UIKit_RegisterLook checks its size).
        UITextBox_SetBars(&((UITextBoxLookBase*)(void*)Box)->TextBox, BarIds[UIScrollAxis_Horizontal], BarIds[UIScrollAxis_Vertical]);
        Result = UITextBox_AttachContextMenu(screen, BoxId);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, BoxId);
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    *outId = BoxId;
    return Error_CreateSuccess();
}

Error UIKit_CreatePropertyBox(UIKit* self, UIScreen* screen, uint64_t parentId, const UIPropertyBoxOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreatePropertyBox: self, screen, options and outId must not be NULL.");
    }
    UIPropertyBoxArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_PropertyBox, options->Element.Look);
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, outId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIPropertyBox_AttachContextMenu(screen, *outId);
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, *outId);
        *outId = WIDGET_ID_INVALID;
    }
    return Result;
}

/* Makes one of a compound's parts placed by it: no stack hint, its box from the compound's layout. */
static UIElementOptions MakePartElement(void)
{
    UIElementOptions Element = UIElementOptions_Create();
    Element.HasPlacement = true;
    Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 0.0f, 0.0f);
    return Element;
}

Error UIKit_CreateLabelled(UIKit* self, UIScreen* screen, uint64_t parentId, const UILabelledOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateLabelled: self, screen, options and outId must not be NULL.");
    }
    UILabelledArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Labelled, options->Element.Look);
    uint64_t LabelledId = WIDGET_ID_INVALID;
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, &LabelledId);
    if (Result.Code != ErrorCode_Success)
    {
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    UILabelOptions Label = UILabelOptions_Create((options->Text != NULL) ? options->Text : (const unsigned char*)u8"");
    Label.Element = MakePartElement();
    uint64_t LabelId = WIDGET_ID_INVALID;
    Result = UIKit_CreateLabel(self, screen, LabelledId, &Label, &LabelId);
    Widget* Labelled = NULL;
    if ((Result.Code == ErrorCode_Success) && !UIScreen_TryGetWidgetById(screen, LabelledId, &Labelled))
    {
        Result = Error_Construct2(ErrorCode_InvalidOperation, "UIKit_CreateLabelled: a hook took the element away.");
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, LabelledId);
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    // Every labelled look's struct starts with UILabelledLookBase (UIKit_RegisterLook checks its size).
    UILabelled_SetLabel(&((UILabelledLookBase*)(void*)Labelled)->Labelled, LabelId);
    *outId = LabelledId;
    return Error_CreateSuccess();
}

Error UIKit_CreateForm(UIKit* self, UIScreen* screen, uint64_t parentId, const UIFormOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateForm: self, screen and options must not be NULL.");
    }
    UIFormArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Form, options->Element.Look);
    return CreateElement(self, screen, parentId, TypeId, &Arguments, outId);
}

Error UIKit_CreateCompoundSlider(UIKit* self, UIScreen* screen, uint64_t parentId, const UICompoundSliderOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateCompoundSlider: self, screen, options and outId must not be NULL.");
    }
    UICompoundSliderArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_CompoundSlider, options->Element.Look);
    uint64_t CompoundId = WIDGET_ID_INVALID;
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, &CompoundId);
    if (Result.Code != ErrorCode_Success)
    {
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    uint64_t NameId = WIDGET_ID_INVALID;
    uint64_t SliderId = WIDGET_ID_INVALID;
    uint64_t BoxId = WIDGET_ID_INVALID;
    UILabelOptions Name = UILabelOptions_Create((options->Name != NULL) ? options->Name : (const unsigned char*)u8"");
    Name.Element = MakePartElement();
    Result = UIKit_CreateLabel(self, screen, CompoundId, &Name, &NameId);
    if (Result.Code == ErrorCode_Success)
    {
        UISliderOptions Slider = UISliderOptions_Create();
        Slider.Element = MakePartElement();
        Slider.Value = options->Value;
        Slider.Orientation = options->Orientation;
        Result = UIKit_CreateSlider(self, screen, CompoundId, &Slider, &SliderId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UIPropertyBoxOptions Box = UIPropertyBoxOptions_Create();
        Box.Element = MakePartElement();
        Box.Value = options->Value;
        Box.Precision = options->Precision;
        Box.Unit = options->Unit;
        Box.ShowsFill = false;
        Result = UIKit_CreatePropertyBox(self, screen, CompoundId, &Box, &BoxId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UICompoundSlider_BindParts(screen, CompoundId, NameId, SliderId, BoxId);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, CompoundId);
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    *outId = CompoundId;
    return Error_CreateSuccess();
}

Error UIKit_CreateVectorRow(UIKit* self, UIScreen* screen, uint64_t parentId, const UIVectorRowOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateVectorRow: self, screen, options and outId must not be NULL.");
    }
    UIVectorRowArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_VectorRow, options->Element.Look);
    uint64_t RowId = WIDGET_ID_INVALID;
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, &RowId);
    if (Result.Code != ErrorCode_Success)
    {
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    uint64_t FieldIds[UI_VECTOR_ROW_MAX_COUNT] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID, WIDGET_ID_INVALID, WIDGET_ID_INVALID };
    for (size_t Axis = 0U; (Axis < options->Count) && (Axis < UI_VECTOR_ROW_MAX_COUNT) && (Result.Code == ErrorCode_Success); Axis++)
    {
        UIPropertyBoxOptions Field = UIPropertyBoxOptions_Create();
        Field.Element = MakePartElement();
        Field.Value = options->Value;
        Field.Value.Value = options->Values[Axis];
        Field.Precision = options->Precision;
        Field.Unit = options->Unit;
        Field.ShowsFill = false;
        Result = UIKit_CreatePropertyBox(self, screen, RowId, &Field, &FieldIds[Axis]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIVectorRow_BindFields(screen, RowId, FieldIds);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, RowId);
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    *outId = RowId;
    return Error_CreateSuccess();
}

/* Makes one of the kit's own widgets as a child of an element: a part its look does not stand for (the box that cuts a list's rows). */
static Error CreateOwnPart(UIKit* self, UIScreen* screen, uint64_t parentId, uint64_t typeId, void* args, uint64_t* outId)
{
    *outId = WIDGET_ID_INVALID;
    Widget* Made = NULL;
    Error Result = UIWidgetFactory_CreateWidget(self->_factory, screen, typeId, args, &Made);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t Id = Widget_GetId(Made);
    Result = Widget_InitializeWidget(Made);
    Widget* Parent = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, parentId, &Parent) && UIScreen_TryGetWidgetById(screen, Id, &Made))
    {
        Result = Widget_AddSubWidget(Parent, Made);
    }
    else if (Result.Code == ErrorCode_Success)
    {
        Result = Error_Construct2(ErrorCode_InvalidOperation, "UIKit: an element's part lost its parent while it was made.");
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, Id);
        return Result;
    }
    *outId = Id;
    return Error_CreateSuccess();
}

Error UIKit_CreateList(UIKit* self, UIScreen* screen, uint64_t parentId, const UIListOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateList: self, screen, options and outId must not be NULL.");
    }
    UIListArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_List, options->Element.Look);
    uint64_t ListId = WIDGET_ID_INVALID;
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, &ListId);
    if (Result.Code != ErrorCode_Success)
    {
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    uint64_t RowsBoxId = WIDGET_ID_INVALID;
    uint64_t BarId = WIDGET_ID_INVALID;
    Result = CreateOwnPart(self, screen, ListId, self->_clipType, NULL, &RowsBoxId);
    if (Result.Code == ErrorCode_Success)
    {
        Result = CreateAreaBar(self, screen, ListId, UIScrollAxis_Vertical, &BarId);
    }
    Widget* List = NULL;
    if ((Result.Code == ErrorCode_Success) && !UIScreen_TryGetWidgetById(screen, ListId, &List))
    {
        Result = Error_Construct2(ErrorCode_InvalidOperation, "UIKit_CreateList: a hook took the list away.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIList_SetParts(screen, ListId, RowsBoxId, BarId);
    }
    if ((Result.Code == ErrorCode_Success) && options->IsDragSource)
    {
        Result = UIList_SetDragSource(screen, ListId, true, options->DragKind);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, ListId);
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    *outId = ListId;
    return Error_CreateSuccess();
}

Error UIKit_CreateTree(UIKit* self, UIScreen* screen, uint64_t parentId, const UITreeOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateTree: self, screen, options and outId must not be NULL.");
    }
    UITreeArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Tree, options->Element.Look);
    uint64_t TreeId = WIDGET_ID_INVALID;
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, &TreeId);
    if (Result.Code != ErrorCode_Success)
    {
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    uint64_t RowsBoxId = WIDGET_ID_INVALID;
    uint64_t BarId = WIDGET_ID_INVALID;
    Result = CreateOwnPart(self, screen, TreeId, self->_clipType, NULL, &RowsBoxId);
    if (Result.Code == ErrorCode_Success)
    {
        Result = CreateAreaBar(self, screen, TreeId, UIScrollAxis_Vertical, &BarId);
    }
    Widget* Tree = NULL;
    if ((Result.Code == ErrorCode_Success) && !UIScreen_TryGetWidgetById(screen, TreeId, &Tree))
    {
        Result = Error_Construct2(ErrorCode_InvalidOperation, "UIKit_CreateTree: a hook took the tree away.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        // Every tree look's struct starts with UITreeLookBase (UIKit_RegisterLook checks its size).
        UITree_SetParts(&((UITreeLookBase*)(void*)Tree)->Tree, RowsBoxId, BarId);
        if (options->IsDragSource)
        {
            Result = UITree_SetDragSource(screen, TreeId, true, options->DragKind);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, TreeId);
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    *outId = TreeId;
    return Error_CreateSuccess();
}

Error UIKit_CreateDialog(UIKit* self, UIScreen* screen, const UIDialogOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateDialog: self, screen, options and outId must not be NULL.");
    }
    *outId = WIDGET_ID_INVALID;
    // Made shut: neither a root nor a child, until it opens as a popup.
    UIDialogArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Dialog, options->Element.Look);
    Widget* Made = NULL;
    Error Result = UIWidgetFactory_CreateWidget(self->_factory, screen, TypeId, &Arguments, &Made);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t DialogId = Widget_GetId(Made);
    Result = Widget_InitializeWidget(Made);
    UILabelOptions Title = UILabelOptions_Create((options->Title != NULL) ? options->Title : (const unsigned char*)u8"");
    Title.Element = MakePartElement();
    Title.Element.Variant = u8"heading";
    uint64_t TitleId = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_CreateLabel(self, screen, DialogId, &Title, &TitleId);
    }
    UILabelOptions Message = UILabelOptions_Create((options->Message != NULL) ? options->Message : (const unsigned char*)u8"");
    Message.Element = MakePartElement();
    Message.IsWrapped = true;
    uint64_t MessageId = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_CreateLabel(self, screen, DialogId, &Message, &MessageId);
    }
    const UIThemeStyle* Style = UITheme_FindStyle(UIScreen_GetTheme(screen), UIElementKind_Dialog, options->Element.Variant);
    UIStackOptions Content = UIStackOptions_Create(UIStackDirection_Column);
    Content.Element = MakePartElement();
    Content.Layout.Spacing = UIThemeStyle_GetNumber(Style, UIElementKind_Dialog, UIDialogPart_Spacing);
    uint64_t ContentId = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_CreateStack(self, screen, DialogId, &Content, &ContentId);
    }
    uint64_t ButtonIds[UI_DIALOG_MAX_BUTTONS];
    size_t ButtonCount = 0U;
    for (; (ButtonCount < options->ButtonCount) && (ButtonCount < UI_DIALOG_MAX_BUTTONS) && (Result.Code == ErrorCode_Success); ButtonCount++)
    {
        UIButtonOptions Button = UIButtonOptions_Create(options->Buttons[ButtonCount].Text);
        Button.Element = MakePartElement();
        Result = UIKit_CreateButton(self, screen, DialogId, &Button, &ButtonIds[ButtonCount]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIDialog_SetParts(screen, DialogId, TitleId, MessageId, ContentId, ButtonIds, options->Buttons, ButtonCount);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, DialogId);
        return Result;
    }
    *outId = DialogId;
    return Error_CreateSuccess();
}

Error UIKit_CreateColorPicker(UIKit* self, UIScreen* screen, uint64_t parentId, const UIColorPickerOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateColorPicker: self, screen, options and outId must not be NULL.");
    }
    UIColorPickerArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_ColorPicker, options->Element.Look);
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, outId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIColorPicker_Attach(screen, *outId);
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, *outId);
        *outId = WIDGET_ID_INVALID;
    }
    return Result;
}

Error UIKit_CreateToolbar(UIKit* self, UIScreen* screen, uint64_t parentId, const UIToolbarOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateToolbar: self, screen, options and outId must not be NULL.");
    }
    UIToolbarArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Toolbar, options->Element.Look);
    uint64_t BarId = WIDGET_ID_INVALID;
    Error Result = CreateElement(self, screen, parentId, TypeId, &Arguments, &BarId);
    for (size_t Index = 0U; (Index < options->ItemCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        const UIToolbarItem* Item = &options->Items[Index];
        if (Item->Kind == UIToolbarItemKind_Separator)
        {
            continue;
        }
        UIButtonOptions Button = UIButtonOptions_Create(Item->Text);
        Button.Element = MakePartElement();
        Button.Element.Variant = options->ButtonVariant;
        Button.Icon = Item->Icon;
        Button.IsEnabled = Item->IsEnabled;
        Button.IsToggled = (Item->Kind != UIToolbarItemKind_Button) && Item->IsToggled;
        uint64_t ButtonId = WIDGET_ID_INVALID;
        Result = UIKit_CreateButton(self, screen, BarId, &Button, &ButtonId);
        if ((Result.Code == ErrorCode_Success) && (Item->Name != NULL) && (Item->Name[0] != 0U))
        {
            Result = UITooltip_Set(screen, ButtonId, Item->Name);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = UIToolbar_AttachButton(screen, BarId, Index, ButtonId);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, BarId);
        *outId = WIDGET_ID_INVALID;
        return Result;
    }
    *outId = BarId;
    return Error_CreateSuccess();
}

/* Makes one of a window's caption buttons in its title bar: a button of the kit of its variant, a placeholder letter for its icon, never
 * taking the focus (a press on it acts on nothing inside the window, as one on the title bar). */
static Error CreateCaptionButton(UIKit* self, UIScreen* screen, uint64_t titleBarId, const unsigned char* variant, uint32_t letter,
    uint64_t* outId)
{
    UIButtonOptions Button = UIButtonOptions_Create(NULL);
    Button.Element = MakePartElement();
    Button.Element.Variant = variant;
    Button.Icon = UIElementIcon_FromLetter(letter);
    Error Result = UIKit_CreateButton(self, screen, titleBarId, &Button, outId);
    Widget* Made = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, *outId, &Made))
    {
        Result = Widget_SetIsFocusable(Made, false);
    }
    return Result;
}

/* Makes a window's content: a scroll area by its fits, and in it the column calling code fills, padded and spaced as the theme says. */
static Error CreateWindowContent(UIKit* self, UIScreen* screen, uint64_t windowId, const UIWindowOptions* options, uint64_t* outAreaId,
    uint64_t* outContentId)
{
    UIScrollAreaOptions Area = UIScrollAreaOptions_Create();
    Area.Element = MakePartElement();
    Area.Element.Variant = u8"window"; // A theme draws the window's content background under it.
    Area.Horizontal = (options->FitAcross == UIWindowFit_Fit) ? UIScrollMode_Disabled : UIScrollMode_Auto;
    Area.Vertical = (options->FitDown == UIWindowFit_Fit) ? UIScrollMode_Disabled : UIScrollMode_Auto;
    Error Result = UIKit_CreateScrollArea(self, screen, windowId, &Area, outAreaId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIThemeStyle* Style = UITheme_FindStyle(UIScreen_GetTheme(screen), UIElementKind_Window, options->Element.Variant);
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Window, UIWindowPart_ContentPadding);
    UIStackOptions Column = UIStackOptions_Create(UIStackDirection_Column);
    Column.Layout.Padding = (UIInsets){ .Left = Padding.x, .Top = Padding.y, .Right = Padding.x, .Bottom = Padding.y };
    Column.Layout.Spacing = UIThemeStyle_GetNumber(Style, UIElementKind_Window, UIWindowPart_ContentSpacing);
    return UIKit_CreateStack(self, screen, *outAreaId, &Column, outContentId);
}

Error UIKit_CreateWindow(UIKit* self, UIScreen* screen, const UIWindowOptions* options, uint64_t* outId, uint64_t* outContentId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateWindow: self, screen, options and outId must not be NULL.");
    }
    *outId = WIDGET_ID_INVALID;
    if (outContentId != NULL)
    {
        *outContentId = WIDGET_ID_INVALID;
    }
    UIWindowArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Window, options->Element.Look);
    uint64_t WindowId = WIDGET_ID_INVALID;
    Error Result = CreateElement(self, screen, WIDGET_ID_INVALID, TypeId, &Arguments, &WindowId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Widget* Window = NULL;
    if (UIScreen_TryGetWidgetById(screen, WindowId, &Window))
    {
        Result = UIScreen_SetRootLayer(screen, Window, UIScreenLayer_Floating);
    }
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, WindowId, &Window))
    {
        Result = UIScreen_BringToTop(screen, Window); // Over the floating windows raised before it.
    }
    // The frame first, under everything made after it: a press on what shows of it (the border) acts on nothing inside.
    UIWindowParts Parts = { .FrameId = WIDGET_ID_INVALID, .TitleBarId = WIDGET_ID_INVALID, .AreaId = WIDGET_ID_INVALID, .ContentId = WIDGET_ID_INVALID };
    for (int32_t Button = 0; Button < (int32_t)UIWindowButton_Count; Button++)
    {
        Parts.ButtonIds[Button] = WIDGET_ID_INVALID;
    }
    for (int32_t Edge = 0; Edge < (int32_t)UIWindowEdge_Count; Edge++)
    {
        Parts.EdgeIds[Edge] = WIDGET_ID_INVALID;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = CreateOwnPart(self, screen, WindowId, self->_windowFrameType, &WindowId, &Parts.FrameId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = CreateOwnPart(self, screen, WindowId, self->_windowTitleType, &WindowId, &Parts.TitleBarId);
    }
    static const uint32_t Letters[UIWindowButton_Count] = { UI_WINDOW_MINIMIZE_LETTER, UI_WINDOW_MAXIMIZE_LETTER, UI_WINDOW_CLOSE_LETTER };
    for (int32_t Button = 0; (Button < (int32_t)UIWindowButton_Count) && (Result.Code == ErrorCode_Success); Button++)
    {
        const unsigned char* Variant = (Button == (int32_t)UIWindowButton_Close) ? u8"window_close" : u8"window_button";
        Result = CreateCaptionButton(self, screen, Parts.TitleBarId, Variant, Letters[Button], &Parts.ButtonIds[Button]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = CreateWindowContent(self, screen, WindowId, options, &Parts.AreaId, &Parts.ContentId);
    }
    for (int32_t Edge = 0; (Edge < (int32_t)UIWindowEdge_Count) && (Result.Code == ErrorCode_Success); Edge++)
    {
        UIWindowEdgeArgs EdgeArguments = { .WindowId = WindowId, .Edge = (UIWindowEdge)Edge };
        Result = CreateOwnPart(self, screen, WindowId, self->_windowEdgeType, &EdgeArguments, &Parts.EdgeIds[Edge]);
        Widget* Made = NULL;
        if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, Parts.EdgeIds[Edge], &Made))
        {
            Result = Widget_SetZLayer(Made, 2.0f); // Over the content and the title bar: an edge is caught first.
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_SetParts(screen, WindowId, &Parts);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, WindowId);
        return Result;
    }
    *outId = WindowId;
    if (outContentId != NULL)
    {
        *outContentId = Parts.ContentId;
    }
    return Error_CreateSuccess();
}

Error UIKit_CreateWorkspace(UIKit* self, UIScreen* screen, const UIWorkspaceOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateWorkspace: self, screen, options and outId must not be NULL.");
    }
    *outId = WIDGET_ID_INVALID;
    UIWorkspaceArgs Arguments = { .Kit = self, .Options = options };
    uint64_t TypeId = ResolveLookType(self, screen, UIElementKind_Workspace, options->Element.Look);
    uint64_t WorkspaceId = WIDGET_ID_INVALID;
    Error Result = CreateElement(self, screen, WIDGET_ID_INVALID, TypeId, &Arguments, &WorkspaceId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Widget* Workspace = NULL;
    if (UIScreen_TryGetWidgetById(screen, WorkspaceId, &Workspace))
    {
        Result = UIScreen_SetRootLayer(screen, Workspace, UIScreenLayer_Background);
    }
    uint64_t MenuBarId = WIDGET_ID_INVALID;
    uint64_t StatusBarId = WIDGET_ID_INVALID;
    uint64_t ZonesId = WIDGET_ID_INVALID;
    if ((Result.Code == ErrorCode_Success) && options->HasMenuBar)
    {
        UIMenuBarOptions MenuBar = options->MenuBar;
        MenuBar.Element.HasPlacement = true; // Placed by the workspace.
        MenuBar.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 0.0f, 0.0f);
        Result = UIKit_CreateMenuBar(self, screen, WorkspaceId, &MenuBar, &MenuBarId);
    }
    if ((Result.Code == ErrorCode_Success) && options->HasStatusBar)
    {
        UIStatusBarOptions StatusBar = options->StatusBar;
        StatusBar.Element.HasPlacement = true;
        StatusBar.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 0.0f, 0.0f);
        Result = UIKit_CreateStatusBar(self, screen, WorkspaceId, &StatusBar, &StatusBarId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_InternalCreateZones(screen, WorkspaceId, &ZonesId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_InternalSetParts(screen, WorkspaceId, MenuBarId, StatusBarId, ZonesId);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPartlyMade(screen, ZonesId);
        DestroyPartlyMade(screen, WorkspaceId);
        return Result;
    }
    *outId = WorkspaceId;
    return Error_CreateSuccess();
}
