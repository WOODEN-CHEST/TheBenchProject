#include "UITypingView.h"
#include "TextComponentFactory.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "wr/WRMath.h"


// Macros.
/** How long a caret shows, then hides, in seconds. */
#define CARET_BLINK_HALF_PERIOD 0.53
/** How far past a line's end a selection over its line break reaches, as a share of the line's height. */
#define SELECTED_BREAK_SHARE 0.3f
/** How wide a block caret is at a line's end, where it covers no character, as a share of the line's height. */
#define BLOCK_CARET_SHARE 0.5f


// Static functions.
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

static bool IsSameColor(Color a, Color b)
{
    return (a.r == b.r) && (a.g == b.g) && (a.b == b.b) && (a.a == b.a);
}

/* The selection's boxes, line by line, behind the text: from where a caret at its start stands to where one at its end does, a little past a
 * line's end where the selection goes on over its line break; lines out of the view are passed over. */
static Error RenderSelection(const UITypingView* self, const UIElementLook* look, const Widget* widget, const UITextBoxCapability* textBox,
    UIRenderContext* context, const UIThemeStyle* style, const UITypingParts* parts, Vector2 origin, float unitsToPixels, Rectangle view)
{
    size_t Start = 0U;
    size_t End = 0U;
    UITextBoxCapability_GetLayoutSelection(textBox, &Start, &End);
    if (End <= Start)
    {
        return Error_CreateSuccess();
    }
    int32_t Part = (Widget_IsEntered(widget) || (parts->InactiveSelection < 0)) ? parts->Selection : parts->InactiveSelection;
    size_t FirstLine = TextLayout_GetLineAtOffset(&self->Layout, Start);
    size_t LastLine = TextLayout_GetLineAtOffset(&self->Layout, End);
    // Only the lines in view: a selection over a long text costs what shows.
    if (unitsToPixels > 0.0f)
    {
        size_t TopLine = TextLayout_GetLineAtY(&self->Layout, (view.y - origin.y) / unitsToPixels);
        size_t BottomLine = TextLayout_GetLineAtY(&self->Layout, ((view.y + view.height) - origin.y) / unitsToPixels);
        FirstLine = (FirstLine > TopLine) ? FirstLine : TopLine;
        LastLine = (LastLine < BottomLine) ? LastLine : BottomLine;
    }
    float Scale = Widget_GetEffectiveScale(widget);
    for (size_t Line = FirstLine; Line <= LastLine; Line++)
    {
        Rectangle Range = { 0 };
        if (!TextLayout_GetRangeBoxOnLine(&self->Layout, Line, Start, End, &Range))
        {
            continue;
        }
        size_t LineStart = 0U;
        size_t LineEnd = 0U;
        TextLayout_GetLineOffsets(&self->Layout, Line, &LineStart, &LineEnd);
        float Extra = (End > LineEnd) ? (Range.height * SELECTED_BREAK_SHARE) : 0.0f;
        Rectangle Pixels =
        {
            .x = RoundToPixel(origin.x + (Range.x * unitsToPixels)),
            .y = RoundToPixel(origin.y + (Range.y * unitsToPixels)),
            .width = RoundToPixel((Range.width + Extra) * unitsToPixels),
            .height = RoundToPixel(Range.height * unitsToPixels)
        };
        bool IsInView = ((Pixels.y + Pixels.height) > view.y) && (Pixels.y < (view.y + view.height));
        if (!IsInView || UIElement_IsBoxEmpty(Pixels))
        {
            continue;
        }
        Error Result = UIElementLook_DrawBox(look, style, Part, context, Pixels, Scale);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* The caret while the box is being edited and the blink shows it: a line, or a block over the character overwriting would replace. */
static Error RenderCaret(const UITypingView* self, const UIElementLook* look, const Widget* widget, const UITextBoxCapability* textBox,
    UIRenderContext* context, const UIThemeStyle* style, const UITypingParts* parts, Vector2 origin, float unitsToPixels)
{
    bool IsBlinkShowing = Math_RemainderDouble(self->BlinkSeconds, 2.0 * CARET_BLINK_HALF_PERIOD) < CARET_BLINK_HALF_PERIOD;
    if (!UITextBoxCapability_IsCaretShown(textBox, widget) || !IsBlinkShowing)
    {
        return Error_CreateSuccess();
    }
    Rectangle Caret = { 0 };
    if (!TextLayout_GetCaretBox(&self->Layout, UITextBoxCapability_GetLayoutCaret(textBox), &Caret))
    {
        return Error_CreateSuccess();
    }
    float Scale = Widget_GetEffectiveScale(widget);
    bool IsBlock = UITextBoxCapability_IsOverwriting(textBox) && (parts->OverwriteCaret >= 0);
    float Width = IsBlock ? Math_MaxFloat(Caret.width, Caret.height * BLOCK_CARET_SHARE) * unitsToPixels
        : (UIThemeStyle_GetNumber(style, parts->Kind, parts->CaretWidth) * Scale);
    Rectangle Pixels =
    {
        .x = RoundToPixel(origin.x + (Caret.x * unitsToPixels)),
        .y = RoundToPixel(origin.y + (Caret.y * unitsToPixels)),
        .width = Math_MaxFloat(1.0f, RoundToPixel(Width)),
        .height = Math_MaxFloat(1.0f, RoundToPixel(Caret.height * unitsToPixels))
    };
    return UIElementLook_DrawBox(look, style, IsBlock ? parts->OverwriteCaret : parts->Caret, context, Pixels, Scale);
}


// Functions.
Error UITypingView_Construct(UITypingView* self, UIKit* kit)
{
    Error Result = TextLayout_Construct(&self->Layout);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = TextDocument_Construct(&self->Document, UIKit_GetTextFactory(kit), UIKit_GetAssets(kit), UIKit_GetLogger(kit));
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = TextLayout_Deconstruct(&self->Layout);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    self->HasBuilt = false;
    self->BuiltContentRevision = 0U;
    self->BuiltMask = 0U;
    self->BuiltColor = (Color){ 0 };
    self->HasAppliedTheme = false;
    self->AppliedThemeRevision = 0U;
    self->BlinkSeconds = 0.0;
    self->SeenCaretRevision = 0U;
    return Error_CreateSuccess();
}

Error UITypingView_Deconstruct(UITypingView* self)
{
    // The layout points into the document: it goes first.
    Error First = TextLayout_Deconstruct(&self->Layout);
    return KeepFirst(First, TextDocument_Deconstruct(&self->Document));
}

Error UITypingView_RefreshDocument(UITypingView* self, const Widget* widget, UITextBoxCapability* textBox, uint32_t mask, Color textColor)
{
    const UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t ThemeRevision = UIScreen_GetThemeRevision(Screen);
    if (!self->HasAppliedTheme || (self->AppliedThemeRevision != ThemeRevision))
    {
        Error Result = TextDocument_SetDefaultFontName(&self->Document, UITheme_GetFontName(UIScreen_GetTheme(Screen)));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        self->HasAppliedTheme = true;
        self->AppliedThemeRevision = ThemeRevision;
    }
    bool IsCurrent = self->HasBuilt && (self->BuiltContentRevision == UITextBoxCapability_GetContentRevision(textBox)) && (self->BuiltMask == mask)
        && IsSameColor(self->BuiltColor, textColor);
    if (IsCurrent)
    {
        return Error_CreateSuccess();
    }
    TextComponent* Root = NULL;
    Error Result = UITextBoxCapability_BuildDocument(textBox, textBox->_factory, mask, UIRenderColor_FromColor(textColor), &Root);
    if (Result.Code == ErrorCode_Success)
    {
        Result = TextDocument_SetRoot(&self->Document, Root);
        if (Result.Code != ErrorCode_Success)
        {
            Error Cleanup = TextComponentFactory_ReturnTree(textBox->_factory, Root);
            Error_Deconstruct(&Cleanup);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        self->HasBuilt = true;
        self->BuiltContentRevision = UITextBoxCapability_GetContentRevision(textBox);
        self->BuiltMask = mask;
        self->BuiltColor = textColor;
    }
    return Result;
}

Error UITypingView_LayOut(UITypingView* self, const Widget* widget, bool isWrapped, float wrapWidth, const TextLayout** outLayout,
    float* outUIPixelsPerUnit)
{
    const UIScreen* Screen = Widget_GetScreen(widget);
    float Scale = Widget_GetEffectiveScale(widget);
    TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
    Options.SizeMultiplier = Math_ClampFloat(UIScreen_PixelsToFitted(Screen, UITheme_GetTextSize(UIScreen_GetTheme(Screen)) * Scale), 0.0f,
        TEXT_LAYOUT_SIZE_MULTIPLIER_MAX);
    Options.TracksOffsets = true;
    if (isWrapped)
    {
        Options.HasMaxWidth = true;
        Options.MaxWidth = UIScreen_PixelsToFitted(Screen, Math_MaxFloat(0.0f, wrapWidth) * Scale);
        Options.IsWrapEnabled = true;
    }
    Error Result = TextLayout_Refresh(&self->Layout, &self->Document, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float FittedPerPixel = UIScreen_PixelsToFitted(Screen, 1.0f);
    *outLayout = &self->Layout;
    *outUIPixelsPerUnit = (FittedPerPixel > 0.0f) ? ((1.0f / FittedPerPixel) / Scale) : 0.0f;
    return Error_CreateSuccess();
}

Error UITypingView_Update(UITypingView* self, const UITextBoxCapability* textBox, ProgramTime time)
{
    // The caret shows at once after an edit or a move, then blinks.
    if (UITextBoxCapability_GetCaretRevision(textBox) != self->SeenCaretRevision)
    {
        self->SeenCaretRevision = UITextBoxCapability_GetCaretRevision(textBox);
        self->BlinkSeconds = 0.0;
    }
    else
    {
        self->BlinkSeconds += time.PassedTime;
    }
    return TextDocument_Update(&self->Document, time);
}

Rectangle UITypingView_GetViewPixels(const Widget* widget, const UITextBoxCapability* textBox, Rectangle pixels)
{
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle ViewBox = UITextBoxCapability_GetViewBox(textBox);
    return (Rectangle)
    {
        .x = pixels.x + RoundToPixel(ViewBox.x * Scale),
        .y = pixels.y + RoundToPixel(ViewBox.y * Scale),
        .width = RoundToPixel(ViewBox.width * Scale),
        .height = RoundToPixel(ViewBox.height * Scale)
    };
}

Error UITypingView_Render(const UITypingView* self, const UIElementLook* look, const Widget* widget, const UITextBoxCapability* textBox,
    UIRenderContext* context, const UIThemeStyle* style, const UITypingParts* parts, Rectangle view, const UIElementText* placeholder,
    Color placeholderColor)
{
    if (UIElement_IsBoxEmpty(view))
    {
        return Error_CreateSuccess();
    }
    const UIScreen* Screen = Widget_GetScreen(widget);
    float Scale = Widget_GetEffectiveScale(widget);
    float FittedPerPixel = UIScreen_PixelsToFitted(Screen, 1.0f);
    float UnitsToPixels = (FittedPerPixel > 0.0f) ? (1.0f / FittedPerPixel) : 0.0f;
    Vector2 Offset = UITextBoxCapability_GetScrollOffset(textBox);
    Vector2 Origin = { .x = RoundToPixel(view.x - (Offset.x * Scale)), .y = RoundToPixel(view.y - (Offset.y * Scale)) };
    RenderContext* Device = UIRenderContext_GetRenderContext(context);
    RenderContext_PushClip(Device, view);
    Error Result = RenderSelection(self, look, widget, textBox, context, style, parts, Origin, UnitsToPixels, view);
    if (Result.Code == ErrorCode_Success)
    {
        if ((placeholder != NULL) && UITextBoxCapability_IsShowingPlaceholder(textBox))
        {
            Result = UIElementText_Draw(placeholder, context, Origin.x, Origin.y, placeholderColor);
        }
        else
        {
            // Only the view's part of a long text is drawn: the rest costs nothing.
            TextLayoutDrawArguments Arguments = TextLayoutDrawArguments_Create(RenderVector2D_Window(Origin));
            Arguments.Tint = UIRenderContext_GetTint(context);
            Arguments.HasCullBox = UnitsToPixels > 0.0f;
            if (Arguments.HasCullBox)
            {
                Arguments.CullBox = (Rectangle){ .x = (view.x - Origin.x) / UnitsToPixels, .y = (view.y - Origin.y) / UnitsToPixels,
                    .width = view.width / UnitsToPixels, .height = view.height / UnitsToPixels };
            }
            Result = TextLayout_Draw(&self->Layout, Device, &Arguments);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = RenderCaret(self, look, widget, textBox, context, style, parts, Origin, UnitsToPixels);
    }
    RenderContext_PopClip(Device);
    return Result;
}
