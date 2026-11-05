#include "UILabel.h"
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "wr/WRMath.h"


// Macros.
/** The size of a kit label's string: the label's size multiplier is then its line height in normalized-fitted units. */
#define KIT_LABEL_COMPONENT_SIZE 1.0f


// Static functions.
static Error ResolveLabel(UIScreen* screen, uint64_t widgetId, UIKit** outKit, Widget** outWidget, UILabelCapability** outLabel)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UILabel: the widget is gone, or its screen has no kit.");
    }
    UILabelCapability* Label = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Label));
    if (Label == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UILabel: the widget is not a label.");
    }
    *outKit = Kit;
    *outWidget = Target;
    *outLabel = Label;
    return Error_CreateSuccess();
}


// Public functions.
Error UILabel_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text)
{
    UIKit* Kit = NULL;
    Widget* Target = NULL;
    UILabelCapability* Label = NULL;
    Error Result = ResolveLabel(screen, widgetId, &Kit, &Target, &Label);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (text == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UILabel_SetText: text must not be NULL.");
    }
    StringComponent* String = NULL;
    Result = TextComponentFactory_CreateString(UIKit_GetTextFactory(Kit), text, &String);
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetSize(String, KIT_LABEL_COMPONENT_SIZE);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = TextDocument_SetRoot(Label->_document, StringComponent_AsComponent(String));
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (String != NULL)
        {
            Error Cleanup = TextComponentFactory_ReturnTree(UIKit_GetTextFactory(Kit), StringComponent_AsComponent(String));
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }
    Widget_InvalidateMeasure(Target);
    return Error_CreateSuccess();
}

Error UILabel_SetTextSize(UIScreen* screen, uint64_t widgetId, bool hasTextSize, float textSize)
{
    UIKit* Kit = NULL;
    Widget* Target = NULL;
    UILabelCapability* Label = NULL;
    Error Result = ResolveLabel(screen, widgetId, &Kit, &Target, &Label);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (hasTextSize && (!(textSize > 0.0f) || Math_IsInfinityFloat(textSize)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILabel_SetTextSize: the size must be a finite number above 0.");
    }
    Label->_hasTextSize = hasTextSize;
    Label->_textSize = textSize;
    Widget_InvalidateMeasure(Target);
    return Error_CreateSuccess();
}
