#include "UIKit.h"
#include "LabelWidget.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMemory.h"


// Fields.
/* The capability a widget type replacing a kind's must embed. */
static const UIElementCapability KindCapabilities[UIElementKind_Count] =
{
    [UIElementKind_Panel] = UIElementCapability_Look,
    [UIElementKind_Label] = UIElementCapability_Label,
    [UIElementKind_Button] = UIElementCapability_Button,
    [UIElementKind_Toggle] = UIElementCapability_Toggle,
    [UIElementKind_Slider] = UIElementCapability_Slider,
};


// Static functions.
static Error RegisterEverything(UIKit* self)
{
    Error Result = Error_CreateSuccess();
    for (int32_t Index = 0; (Index < (int32_t)UIElementCapability_Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = UIWidgetFactory_RegisterCapability(self->_factory, &self->_capabilities[Index]);
    }
    uint64_t Look = self->_capabilities[UIElementCapability_Look];
    if (Result.Code == ErrorCode_Success)
    {
        Result = StackWidget_RegisterType(self->_factory, &self->_stackType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIPanel_RegisterType(self->_factory, Look, &self->_types[UIElementKind_Panel]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = LabelWidget_RegisterElementType(self->_factory, Look, self->_capabilities[UIElementCapability_Label],
            &self->_types[UIElementKind_Label]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIButton_RegisterType(self->_factory, Look, self->_capabilities[UIElementCapability_Button],
            &self->_types[UIElementKind_Button]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIToggle_RegisterType(self->_factory, Look, self->_capabilities[UIElementCapability_Toggle],
            &self->_types[UIElementKind_Toggle]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UISlider_RegisterType(self->_factory, Look, self->_capabilities[UIElementCapability_Slider],
            &self->_types[UIElementKind_Slider]);
    }
    return Result;
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
    Error Result = RegisterEverything(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIWidgetFactory_SetKit(factory, self);
    return Error_CreateSuccess();
}

Error UIKit_Deconstruct(UIKit* self)
{
    if ((self != NULL) && (self->_factory != NULL))
    {
        UIWidgetFactory_SetKit(self->_factory, NULL);
        self->_factory = NULL;
    }
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
    bool IsValid = ((int32_t)kind >= 0) && ((int32_t)kind < (int32_t)UIElementKind_Count);
    return IsValid ? self->_types[kind] : WIDGET_TYPE_ID_INVALID;
}

Error UIKit_SetElementType(UIKit* self, UIElementKind kind, uint64_t typeId)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_SetElementType: self must not be NULL.");
    }
    if (((int32_t)kind < 0) || ((int32_t)kind >= (int32_t)UIElementKind_Count))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIKit_SetElementType: an unknown element kind.");
    }
    uint64_t Capability = self->_capabilities[KindCapabilities[kind]];
    if (!UIWidgetFactory_IsCapabilitySupported(self->_factory, typeId, Capability))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIKit_SetElementType: the type is not registered, or does not embed the kind's capability.");
    }
    self->_types[kind] = typeId;
    return Error_CreateSuccess();
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
    if ((self == NULL) || (options == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreatePanel: self and options must not be NULL.");
    }
    return CreateElement(self, screen, parentId, self->_types[UIElementKind_Panel], (void*)options, outId);
}

Error UIKit_CreateLabel(UIKit* self, UIScreen* screen, uint64_t parentId, const UILabelOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateLabel: self and options must not be NULL.");
    }
    LabelWidgetArgs Arguments =
    {
        .ComponentFactory = self->_textFactory,
        .Assets = self->_assets,
        .Logger = self->_logger,
        .Text = NULL,
        .ElementOptions = options
    };
    return CreateElement(self, screen, parentId, self->_types[UIElementKind_Label], &Arguments, outId);
}

Error UIKit_CreateButton(UIKit* self, UIScreen* screen, uint64_t parentId, const UIButtonOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateButton: self and options must not be NULL.");
    }
    UIButtonArgs Arguments = { .Kit = self, .Options = options };
    return CreateElement(self, screen, parentId, self->_types[UIElementKind_Button], &Arguments, outId);
}

Error UIKit_CreateToggle(UIKit* self, UIScreen* screen, uint64_t parentId, const UIToggleOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateToggle: self and options must not be NULL.");
    }
    UIToggleArgs Arguments = { .Kit = self, .Options = options };
    return CreateElement(self, screen, parentId, self->_types[UIElementKind_Toggle], &Arguments, outId);
}

Error UIKit_CreateSlider(UIKit* self, UIScreen* screen, uint64_t parentId, const UISliderOptions* options, uint64_t* outId)
{
    if ((self == NULL) || (options == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIKit_CreateSlider: self and options must not be NULL.");
    }
    UISliderArgs Arguments = { .Kit = self, .Options = options };
    return CreateElement(self, screen, parentId, self->_types[UIElementKind_Slider], &Arguments, outId);
}
