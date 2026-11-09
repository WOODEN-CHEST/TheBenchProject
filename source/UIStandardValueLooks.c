#include "UICompoundSlider.h"
#include "UIKit.h"
#include "UILabelled.h"
#include "UIPropertyBox.h"
#include "UIScreen.h"
#include "UIStandardLooksInternal.h"
#include "UITypingView.h"
#include "UIVectorRow.h"
#include "wr/WRMath.h"


/*
 * The kit's standard look (tbp:test_box) for the value elements: the property box (a box, a fill bar by its conversion, its number and
 * unit, or while typing the text box's text, selection and caret), and the compounds, which draw nothing of their own but the row of
 * fields' axis tags. Every value comes from the screen's theme.
 */


// Macros.
/** The mask character a property box's typing view is built with: a property box is never masked, so it never shows. */
#define UNMASKED_CHARACTER ((uint32_t)'*')


// Types.
/* The standard property box. */
typedef struct PropertyBoxBoxStruct
{
    UIPropertyBoxLookBase Base;
    UIElementLook Look;
    UITypingView Typing;
    UIElementText Shown;
    UIElementText Unit;
} PropertyBoxBox;

/* The standard labelled element and form: their theme's metrics, nothing drawn. */
typedef struct LabelledBoxStruct
{
    UILabelledLookBase Base;
    UIElementLook Look;
} LabelledBox;

typedef struct FormBoxStruct
{
    UIFormLookBase Base;
    UIElementLook Look;
} FormBox;

/* The standard compound slider: its theme's metrics, nothing drawn (its parts draw themselves). */
typedef struct CompoundSliderBoxStruct
{
    UICompoundSliderLookBase Base;
    UIElementLook Look;
} CompoundSliderBox;

/* The standard row of fields: a coloured tag with its axis's letter before each field. */
typedef struct VectorRowBoxStruct
{
    UIVectorRowLookBase Base;
    UIElementLook Look;
    UIElementText Letters[UI_VECTOR_ROW_MAX_COUNT];
    UITextContent LetterTexts[UI_VECTOR_ROW_MAX_COUNT];
} VectorRowBox;


// Static functions: shared.
static float RoundToPixel(float value)
{
    return Math_FloorFloat(value + 0.5f);
}

static Error KeepFirst(Error first, Error later)
{
    if (first.Code == ErrorCode_Success)
    {
        return later;
    }
    Error_Deconstruct(&later);
    return first;
}


// Static functions: the property box.
static const UIThemeStyle* PropertyBoxBox_GetStyle(const PropertyBoxBox* box)
{
    return UIElementLook_GetStyle(&box->Look, Widget_GetScreen(&box->Base.Base));
}

static UIInsets PropertyBoxBox_GetTextInsets(const void* widget)
{
    const PropertyBoxBox* Box = widget;
    Vector2 Padding = UIThemeStyle_GetVector(PropertyBoxBox_GetStyle(Box), UIElementKind_PropertyBox, UIPropertyBoxPart_Padding);
    return (UIInsets){ .Left = Padding.x, .Top = Padding.y, .Right = Padding.x, .Bottom = Padding.y };
}

static float PropertyBoxBox_GetLineHeight(const void* widget)
{
    const PropertyBoxBox* Box = widget;
    return UITheme_GetTextSize(UIScreen_GetTheme(Widget_GetScreen(&Box->Base.Base)));
}

static UIInsets PropertyBoxBox_GetBarInsets(const void* widget)
{
    const PropertyBoxBox* Box = widget;
    return UIStandardLooks_GetBorderInsets(UIThemeStyle_GetBox(PropertyBoxBox_GetStyle(Box), UIPropertyBoxPart_Box));
}

/* The text's colour for the state the box is going to: the typing view's document holds it, so it changes only with the state. */
static Color PropertyBoxBox_GetTextColor(const PropertyBoxBox* box)
{
    const UIStateColors* Colors = UIThemeStyle_GetColors(PropertyBoxBox_GetStyle(box), UIPropertyBoxPart_Text);
    return (Colors != NULL) ? UIStateColors_Resolve(Colors, UIPropertyBoxCapability_GetState(&box->Base), WHITE) : WHITE;
}

static Error PropertyBoxBox_LayOut(void* widget, bool isWrapped, float wrapWidth, const TextLayout** outLayout, float* outUIPixelsPerUnit)
{
    PropertyBoxBox* Box = widget;
    *outLayout = NULL;
    *outUIPixelsPerUnit = 0.0f;
    Error Result = UITypingView_RefreshDocument(&Box->Typing, &Box->Base.Base, &Box->Base.TextBox, UNMASKED_CHARACTER, PropertyBoxBox_GetTextColor(Box));
    return (Result.Code == ErrorCode_Success) ? UITypingView_LayOut(&Box->Typing, &Box->Base.Base, isWrapped, wrapWidth, outLayout, outUIPixelsPerUnit)
        : Result;
}

static const UITextBoxGeometry PropertyBoxBoxGeometry =
{
    .GetTextInsets = PropertyBoxBox_GetTextInsets,
    .LayOut = PropertyBoxBox_LayOut,
    .GetLineHeight = PropertyBoxBox_GetLineHeight,
    .GetBarInsets = PropertyBoxBox_GetBarInsets
};

static const UITypingParts PropertyBoxBoxParts =
{
    .Kind = UIElementKind_PropertyBox,
    .Selection = UIPropertyBoxPart_Selection,
    .InactiveSelection = -1,
    .Caret = UIPropertyBoxPart_Caret,
    .OverwriteCaret = -1,
    .CaretWidth = UIPropertyBoxPart_CaretWidth
};

static Error PropertyBoxBox_Construct(void* self, void* args)
{
    PropertyBoxBox* Box = self;
    const UIPropertyBoxArgs* Arguments = args;
    UIKit* Kit = Arguments->Kit;
    Error Result = UIElementLook_Construct(&Box->Look, UIElementKind_PropertyBox, Arguments->Options->Element.Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UITypingView_Construct(&Box->Typing, Kit);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementText_Construct(&Box->Shown, UIKit_GetTextFactory(Kit), UIKit_GetAssets(Kit), UIKit_GetLogger(Kit));
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementText_Construct(&Box->Unit, UIKit_GetTextFactory(Kit), UIKit_GetAssets(Kit), UIKit_GetLogger(Kit));
        if (Result.Code != ErrorCode_Success)
        {
            Error Cleanup = UIElementText_Deconstruct(&Box->Shown);
            Error_Deconstruct(&Cleanup);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = UITypingView_Deconstruct(&Box->Typing);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    UITextBoxCapability_SetGeometry(&Box->Base.TextBox, &PropertyBoxBoxGeometry);
    return Error_CreateSuccess();
}

static Error PropertyBoxBox_Deconstruct(void* self)
{
    PropertyBoxBox* Box = self;
    UITextBoxCapability_SetGeometry(&Box->Base.TextBox, NULL);
    Error First = UITypingView_Deconstruct(&Box->Typing);
    First = KeepFirst(First, UIElementText_Deconstruct(&Box->Shown));
    return KeepFirst(First, UIElementText_Deconstruct(&Box->Unit));
}

static Error PropertyBoxBox_Update(void* self, ProgramTime time)
{
    PropertyBoxBox* Box = self;
    Widget* Base = &Box->Base.Base;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(Base), UIPropertyBoxCapability_GetState(&Box->Base), time.PassedTime);
    Error Result = UITypingView_Update(&Box->Typing, &Box->Base.TextBox, time);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementText_Refresh(&Box->Shown, Base, UIPropertyBoxCapability_GetShownText(&Box->Base.PropertyBox));
    }
    return (Result.Code == ErrorCode_Success) ? UIElementText_Refresh(&Box->Unit, Base, UIPropertyBoxCapability_GetUnit(&Box->Base.PropertyBox)) : Result;
}

/* The shown text's width with its unit, in surface pixels at the widget's scale, and the gap between them. */
static float PropertyBoxBox_GetShownWidth(const PropertyBoxBox* box, const UIThemeStyle* style, float* outGap)
{
    const UIScreen* Screen = Widget_GetScreen(&box->Base.Base);
    float Width = UIElementText_GetPixelSize(&box->Shown, Screen).x;
    *outGap = 0.0f;
    if (UIPropertyBoxCapability_ShowsUnit(&box->Base.PropertyBox))
    {
        *outGap = RoundToPixel(UIThemeStyle_GetNumber(style, UIElementKind_PropertyBox, UIPropertyBoxPart_UnitSpacing) * Widget_GetEffectiveScale(&box->Base.Base));
        Width += *outGap + UIElementText_GetPixelSize(&box->Unit, Screen).x;
    }
    return Width;
}

static Error PropertyBoxBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    PropertyBoxBox* Box = self;
    const UIThemeStyle* Style = PropertyBoxBox_GetStyle(Box);
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    Vector2 Minimum = UIThemeStyle_GetVector(Style, UIElementKind_PropertyBox, UIPropertyBoxPart_MinSize);
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_PropertyBox, UIPropertyBoxPart_Padding);
    float Gap = 0.0f;
    float TextWidth = PropertyBoxBox_GetShownWidth(Box, Style, &Gap) / ((Scale > 0.0f) ? Scale : 1.0f);
    float Line = UITheme_GetTextSize(UIScreen_GetTheme(Widget_GetScreen(&Box->Base.Base)));
    outSize->x = Math_MaxFloat(Minimum.x, TextWidth + (2.0f * Padding.x));
    outSize->y = Math_MaxFloat(Minimum.y, Line + (2.0f * Padding.y));
    return Error_CreateSuccess();
}

/* The number and its unit, centred (a string at the start), clipped to the padding. */
static Error PropertyBoxBox_RenderShown(const PropertyBoxBox* box, UIRenderContext* context, const UIThemeStyle* style, Rectangle pixels)
{
    const UIPropertyBoxCapability* PropertyBox = &box->Base.PropertyBox;
    const UIScreen* Screen = Widget_GetScreen(&box->Base.Base);
    float Scale = Widget_GetEffectiveScale(&box->Base.Base);
    Vector2 Padding = UIThemeStyle_GetVector(style, UIElementKind_PropertyBox, UIPropertyBoxPart_Padding);
    Rectangle Inner =
    {
        .x = pixels.x + RoundToPixel(Padding.x * Scale), .y = pixels.y,
        .width = Math_MaxFloat(0.0f, pixels.width - (2.0f * RoundToPixel(Padding.x * Scale))), .height = pixels.height
    };
    if (UIElement_IsBoxEmpty(Inner))
    {
        return Error_CreateSuccess();
    }
    float Gap = 0.0f;
    float Width = PropertyBoxBox_GetShownWidth(box, style, &Gap);
    Vector2 TextSize = UIElementText_GetPixelSize(&box->Shown, Screen);
    float X = UIPropertyBoxCapability_IsString(PropertyBox) ? Inner.x : (Inner.x + Math_MaxFloat(0.0f, RoundToPixel((Inner.width - Width) * 0.5f)));
    float Y = pixels.y + RoundToPixel((pixels.height - TextSize.y) * 0.5f);
    Color Text = UIElementLook_BlendColors(&box->Look, UIThemeStyle_GetColors(style, UIPropertyBoxPart_Text), WHITE);
    RenderContext* Device = UIRenderContext_GetRenderContext(context);
    RenderContext_PushClip(Device, Inner);
    Error Result = UIElementText_Draw(&box->Shown, context, X, Y, Text);
    if ((Result.Code == ErrorCode_Success) && UIPropertyBoxCapability_ShowsUnit(PropertyBox))
    {
        Color Unit = UIElementLook_BlendColors(&box->Look, UIThemeStyle_GetColors(style, UIPropertyBoxPart_Unit), Text);
        Result = UIElementText_Draw(&box->Unit, context, X + TextSize.x + Gap, Y, Unit);
    }
    RenderContext_PopClip(Device);
    return Result;
}

static Error PropertyBoxBox_Render(void* self, UIRenderContext* context)
{
    PropertyBoxBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = PropertyBoxBox_GetStyle(Box);
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    Error Result = UIElementLook_DrawBox(&Box->Look, Style, UIPropertyBoxPart_Box, context, Pixels, Scale);
    if ((Result.Code == ErrorCode_Success) && UIPropertyBoxCapability_ShowsFill(&Box->Base.PropertyBox))
    {
        // Inside the box's border, from its start as far as the value's share of its range.
        UIInsets Border = UIStandardLooks_GetBorderInsets(UIThemeStyle_GetBox(Style, UIPropertyBoxPart_Box));
        Rectangle Inner =
        {
            .x = Pixels.x + RoundToPixel(Border.Left * Scale), .y = Pixels.y + RoundToPixel(Border.Top * Scale),
            .width = Math_MaxFloat(0.0f, Pixels.width - RoundToPixel(Border.Left * Scale) - RoundToPixel(Border.Right * Scale)),
            .height = Math_MaxFloat(0.0f, Pixels.height - RoundToPixel(Border.Top * Scale) - RoundToPixel(Border.Bottom * Scale))
        };
        Inner.width = RoundToPixel(Inner.width * (float)UIPropertyBoxCapability_GetFillShare(&Box->Base.PropertyBox));
        if (!UIElement_IsBoxEmpty(Inner))
        {
            Result = UIElementLook_DrawBox(&Box->Look, Style, UIPropertyBoxPart_Fill, context, Inner, Scale);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (UIPropertyBoxCapability_IsTyping(&Box->Base))
    {
        const UITextBoxCapability* TextBox = &Box->Base.TextBox;
        return UITypingView_Render(&Box->Typing, &Box->Look, &Box->Base.Base, TextBox, context, Style, &PropertyBoxBoxParts,
            UITypingView_GetViewPixels(&Box->Base.Base, TextBox, Pixels), NULL, WHITE);
    }
    return PropertyBoxBox_RenderShown(Box, context, Style, Pixels);
}

static UIElementLook* PropertyBoxBox_GetThemeLook(void* self)
{
    return &((PropertyBoxBox*)self)->Look;
}


// Static functions: the labelled element and the form.
static Error LabelledBox_Construct(void* self, void* args)
{
    return UIElementLook_Construct(&((LabelledBox*)self)->Look, UIElementKind_Labelled, ((const UILabelledArgs*)args)->Options->Element.Variant);
}

static Error LabelledBox_Update(void* self, ProgramTime time)
{
    LabelledBox* Box = self;
    Widget* Base = &Box->Base.Base;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(Base), UIElementState_Normal, time.PassedTime);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(Base));
    UILabelledCapability_SetSpacing(&Box->Base.Labelled, Base, UIThemeStyle_GetNumber(Style, UIElementKind_Labelled, UILabelledPart_Spacing));
    return Error_CreateSuccess();
}

static Error LabelledBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    return UILabelled_Measure(&((LabelledBox*)self)->Base.Base, context, availableWidth, outSize);
}

static Error NothingToRender(void* self, UIRenderContext* context)
{
    (void)self;
    (void)context;
    return Error_CreateSuccess();
}

static UIElementLook* LabelledBox_GetThemeLook(void* self)
{
    return &((LabelledBox*)self)->Look;
}

static Error FormBox_Construct(void* self, void* args)
{
    return UIElementLook_Construct(&((FormBox*)self)->Look, UIElementKind_Form, ((const UIFormArgs*)args)->Options->Element.Variant);
}

static Error FormBox_Update(void* self, ProgramTime time)
{
    FormBox* Box = self;
    Widget* Base = &Box->Base.Base;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(Base), UIElementState_Normal, time.PassedTime);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(Base));
    UIFormCapability_SetMetrics(&Box->Base.Form, Base, UIThemeStyle_GetVector(Style, UIElementKind_Form, UIFormPart_Padding),
        UIThemeStyle_GetNumber(Style, UIElementKind_Form, UIFormPart_Spacing));
    return Error_CreateSuccess();
}

static Error FormBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    return UIForm_Measure(&((FormBox*)self)->Base.Base, context, availableWidth, outSize);
}

static UIElementLook* FormBox_GetThemeLook(void* self)
{
    return &((FormBox*)self)->Look;
}


// Static functions: the compound slider.
static Error CompoundSliderBox_Construct(void* self, void* args)
{
    return UIElementLook_Construct(&((CompoundSliderBox*)self)->Look, UIElementKind_CompoundSlider,
        ((const UICompoundSliderArgs*)args)->Options->Element.Variant);
}

static Error CompoundSliderBox_Update(void* self, ProgramTime time)
{
    CompoundSliderBox* Box = self;
    Widget* Base = &Box->Base.Base;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(Base), UIElementState_Normal, time.PassedTime);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(Base));
    UICompoundSliderCapability_SetMetrics(&Box->Base.CompoundSlider, Base, UIThemeStyle_GetNumber(Style, UIElementKind_CompoundSlider,
        UICompoundSliderPart_Spacing), UIThemeStyle_GetNumber(Style, UIElementKind_CompoundSlider, UICompoundSliderPart_BoxWidth));
    return Error_CreateSuccess();
}

static Error CompoundSliderBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    return UICompoundSlider_Measure(&((CompoundSliderBox*)self)->Base.Base, context, availableWidth, outSize);
}

static UIElementLook* CompoundSliderBox_GetThemeLook(void* self)
{
    return &((CompoundSliderBox*)self)->Look;
}


// Static functions: the row of fields.
static const unsigned char* const AxisLetters[UI_VECTOR_ROW_MAX_COUNT] = { u8"X", u8"Y", u8"Z", u8"W" };
static const int32_t AxisTagParts[UI_VECTOR_ROW_MAX_COUNT] = { UIVectorRowPart_XTag, UIVectorRowPart_YTag, UIVectorRowPart_ZTag, UIVectorRowPart_WTag };

static Error VectorRowBox_Construct(void* self, void* args)
{
    VectorRowBox* Box = self;
    UIKit* Kit = ((const UIVectorRowArgs*)args)->Kit;
    Error Result = UIElementLook_Construct(&Box->Look, UIElementKind_VectorRow, ((const UIVectorRowArgs*)args)->Options->Element.Variant);
    size_t Made = 0U;
    for (; (Made < UI_VECTOR_ROW_MAX_COUNT) && (Result.Code == ErrorCode_Success); Made++)
    {
        Result = UITextContent_Construct(&Box->LetterTexts[Made], AxisLetters[Made]);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        Result = UIElementText_Construct(&Box->Letters[Made], UIKit_GetTextFactory(Kit), UIKit_GetAssets(Kit), UIKit_GetLogger(Kit));
        if (Result.Code != ErrorCode_Success)
        {
            UITextContent_Deconstruct(&Box->LetterTexts[Made]);
            break;
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        while (Made-- > 0U)
        {
            Error Cleanup = UIElementText_Deconstruct(&Box->Letters[Made]);
            Error_Deconstruct(&Cleanup);
            UITextContent_Deconstruct(&Box->LetterTexts[Made]);
        }
    }
    return Result;
}

static Error VectorRowBox_Deconstruct(void* self)
{
    VectorRowBox* Box = self;
    Error First = Error_CreateSuccess();
    for (size_t Axis = 0U; Axis < UI_VECTOR_ROW_MAX_COUNT; Axis++)
    {
        First = KeepFirst(First, UIElementText_Deconstruct(&Box->Letters[Axis]));
        UITextContent_Deconstruct(&Box->LetterTexts[Axis]);
    }
    return First;
}

static Error VectorRowBox_Update(void* self, ProgramTime time)
{
    VectorRowBox* Box = self;
    Widget* Base = &Box->Base.Base;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(Base), UIElementState_Normal, time.PassedTime);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(Base));
    UIVectorRowCapability_SetMetrics(&Box->Base.VectorRow, Base, UIThemeStyle_GetNumber(Style, UIElementKind_VectorRow, UIVectorRowPart_Spacing),
        UIThemeStyle_GetNumber(Style, UIElementKind_VectorRow, UIVectorRowPart_TagWidth));
    Error Result = Error_CreateSuccess();
    for (size_t Axis = 0U; (Axis < UI_VECTOR_ROW_MAX_COUNT) && (Result.Code == ErrorCode_Success); Axis++)
    {
        Result = UIElementText_Refresh(&Box->Letters[Axis], Base, &Box->LetterTexts[Axis]);
    }
    return Result;
}

static Error VectorRowBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    return UIVectorRow_Measure(&((VectorRowBox*)self)->Base.Base, context, availableWidth, outSize);
}

static Error VectorRowBox_Render(void* self, UIRenderContext* context)
{
    VectorRowBox* Box = self;
    Widget* Base = &Box->Base.Base;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIScreen* Screen = Widget_GetScreen(Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    float Scale = Widget_GetEffectiveScale(Base);
    Color Letter = UIElementLook_BlendColors(&Box->Look, UIThemeStyle_GetColors(Style, UIVectorRowPart_TagText), WHITE);
    for (size_t Axis = 0U; Axis < Box->Base.VectorRow._count; Axis++)
    {
        Rectangle Tag = { 0 };
        if (!UIVectorRow_GetTagBox(Base, Axis, &Tag))
        {
            continue;
        }
        Rectangle TagPixels = { .x = Pixels.x + RoundToPixel(Tag.x * Scale), .y = Pixels.y + RoundToPixel(Tag.y * Scale),
            .width = RoundToPixel(Tag.width * Scale), .height = RoundToPixel(Tag.height * Scale) };
        if (UIElement_IsBoxEmpty(TagPixels))
        {
            continue;
        }
        Error Result = UIElementLook_DrawBox(&Box->Look, Style, AxisTagParts[Axis], context, TagPixels, Scale);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Vector2 Size = UIElementText_GetPixelSize(&Box->Letters[Axis], Screen);
        Result = UIElementText_Draw(&Box->Letters[Axis], context, TagPixels.x + RoundToPixel((TagPixels.width - Size.x) * 0.5f),
            TagPixels.y + RoundToPixel((TagPixels.height - Size.y) * 0.5f), Letter);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

static UIElementLook* VectorRowBox_GetThemeLook(void* self)
{
    return &((VectorRowBox*)self)->Look;
}


// Fields.
static const UILookVTable PropertyBoxBoxLook =
{
    .Construct = PropertyBoxBox_Construct,
    .Deconstruct = PropertyBoxBox_Deconstruct,
    .Update = PropertyBoxBox_Update,
    .Render = PropertyBoxBox_Render,
    .Measure = PropertyBoxBox_Measure,
    .GetThemeLook = PropertyBoxBox_GetThemeLook
};

static const UILookVTable LabelledBoxLook =
{
    .Construct = LabelledBox_Construct,
    .Update = LabelledBox_Update,
    .Render = NothingToRender,
    .Measure = LabelledBox_Measure,
    .GetThemeLook = LabelledBox_GetThemeLook
};

static const UILookVTable FormBoxLook =
{
    .Construct = FormBox_Construct,
    .Update = FormBox_Update,
    .Render = NothingToRender,
    .Measure = FormBox_Measure,
    .GetThemeLook = FormBox_GetThemeLook
};

static const UILookVTable CompoundSliderBoxLook =
{
    .Construct = CompoundSliderBox_Construct,
    .Update = CompoundSliderBox_Update,
    .Render = NothingToRender,
    .Measure = CompoundSliderBox_Measure,
    .GetThemeLook = CompoundSliderBox_GetThemeLook
};

static const UILookVTable VectorRowBoxLook =
{
    .Construct = VectorRowBox_Construct,
    .Deconstruct = VectorRowBox_Deconstruct,
    .Update = VectorRowBox_Update,
    .Render = VectorRowBox_Render,
    .Measure = VectorRowBox_Measure,
    .GetThemeLook = VectorRowBox_GetThemeLook
};


// Internal functions (declared in UIStandardLooksInternal.h).
Error UIStandardValueLooks_Register(UIKit* kit)
{
    Error Result = UIKit_RegisterLook(kit, UIElementKind_PropertyBox, UI_KIT_STANDARD_LOOK, sizeof(PropertyBoxBox), &PropertyBoxBoxLook, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Labelled, UI_KIT_STANDARD_LOOK, sizeof(LabelledBox), &LabelledBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Form, UI_KIT_STANDARD_LOOK, sizeof(FormBox), &FormBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_CompoundSlider, UI_KIT_STANDARD_LOOK, sizeof(CompoundSliderBox), &CompoundSliderBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_VectorRow, UI_KIT_STANDARD_LOOK, sizeof(VectorRowBox), &VectorRowBoxLook, NULL);
    }
    return Result;
}
