#include "UITextBox.h"
#include "NamespacedKey.h"
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIScreen.h"
#include "UIScrollbar.h"
#include "UITextBoxInternal.h"
#include "UIWidgetFactory.h"
#include "wr/WRChar.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** How many times a layout decides the bars again: each bar that shows narrows the view, which can call for the other. */
#define MAX_BAR_PASSES 3
/** How far, in UI pixels, the text may pass the view and still fit: float noise does not show a bar. */
#define FIT_TOLERANCE 0.01f
/** Room after a line's end for the caret, in UI pixels: a caret at the end of the longest line stays in view. */
#define CARET_ROOM 2.0f
/** The default number of lines a several-line box asks to be tall. */
#define DEFAULT_MULTILINE_LINES ((size_t)4)
/** U+FFFC OBJECT REPLACEMENT CHARACTER: a sprite in a tree's flow text, as the layout counts it. */
static const unsigned char ObjectCharacter[] = { 0xEFU, 0xBFU, 0xBCU };
#define OBJECT_CHARACTER_BYTES ((size_t)3)


// Types.
/* A style range as the box keeps it: its bytes, the style without its borrowed font name, and a copy of that name. */
typedef struct UITextBoxStyleRecordStruct
{
    size_t Start;
    size_t End;
    TextStyle Style;
    bool HasFontName;
    unsigned char FontName[NAMESPACED_KEY_BUFFER_SIZE];
} UITextBoxStyleRecord;


// Static functions: small helpers.
static UITextBoxStyleRecord* GetRecord(UITextBoxCapability* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_styleRanges, index);
}

static Error GrowthError(void)
{
    return Error_Construct2(ErrorCode_BufferTooLarge, "UITextBox: a buffer could not grow.");
}

static UIScrollMode GetMode(const UITextBoxCapability* self, UIScrollAxis axis)
{
    return self->Scrollable._modes[axis];
}

static bool IsUndoScopeValid(UITextBoxUndoScope scope)
{
    return (scope == UITextBoxUndoScope_Box) || (scope == UITextBoxUndoScope_Visit);
}


// Static functions: style ranges.
/* Moves a range with an edit that replaced [start, start + removed) with @p inserted bytes. Text typed at a range's end stays out of it,
 * text typed inside it (or over a selection that starts in it) joins it. False when nothing is left of it. */
static bool AdjustRecord(UITextBoxStyleRecord* record, size_t start, size_t removed, size_t inserted)
{
    size_t RemovedEnd = start + removed;
    if (record->End <= start)
    {
        return true;
    }
    if (record->Start >= RemovedEnd)
    {
        record->Start = (record->Start - removed) + inserted;
        record->End = (record->End - removed) + inserted;
        return true;
    }
    size_t InsertedEnd = start + inserted;
    record->End = (record->End > RemovedEnd) ? ((record->End - removed) + inserted) : InsertedEnd;
    record->Start = (record->Start <= start) ? record->Start : InsertedEnd;
    return record->End > record->Start;
}

/* Keeps the ranges a pass kept, in order, in one sweep (removing them one by one would cost the square of their count). */
static void KeepRecord(UITextBoxCapability* self, size_t index, size_t* kept)
{
    if (*kept != index)
    {
        Memory_Copy(GetRecord(self, index), GetRecord(self, *kept), sizeof(UITextBoxStyleRecord));
    }
    (*kept)++;
}

static void AdjustRanges(UITextBoxCapability* self, size_t start, size_t removed, size_t inserted)
{
    size_t Kept = 0U;
    for (size_t Index = 0U; Index < self->_styleRanges._count; Index++)
    {
        if (AdjustRecord(GetRecord(self, Index), start, removed, inserted))
        {
            KeepRecord(self, Index, &Kept);
        }
    }
    GenericBuffer_RemoveRange(&self->_styleRanges, Kept, self->_styleRanges._count);
}

/* Keeps every range inside the text and on codepoint boundaries, dropping the empty ones: after text set by code. */
static void ClampRanges(UITextBoxCapability* self)
{
    const unsigned char* Text = UITextEdit_GetText(&self->Edit);
    size_t Length = UITextEdit_GetLength(&self->Edit);
    size_t Kept = 0U;
    for (size_t Index = 0U; Index < self->_styleRanges._count; Index++)
    {
        UITextBoxStyleRecord* Record = GetRecord(self, Index);
        Record->Start = UITextEdit_SnapToBoundary(Text, Length, Record->Start);
        Record->End = UITextEdit_SnapToBoundary(Text, Length, Record->End);
        if (Record->End > Record->Start)
        {
            KeepRecord(self, Index, &Kept);
        }
    }
    GenericBuffer_RemoveRange(&self->_styleRanges, Kept, self->_styleRanges._count);
}

/* The text was set whole (by code, by a revert that could not step back, by a tree): the ranges keep their places, clamped to it; the
 * document is built again; Up and Down measure their column again. */
static void OnTextSetWhole(UITextBoxCapability* self)
{
    ClampRanges(self);
    self->_contentRevision++;
    self->_hasColumn = false;
    UITextBoxInternal_OnCaretMoved(self);
}

/* A style's font name in full (written without a namespace, tbp's): false for a name that is no valid asset name. A style without one
 * gives true and no name. */
static bool TryCopyFontName(const TextStyle* style, unsigned char* outFullName, bool* outHasFontName)
{
    const unsigned char* FontName = NULL;
    bool HasFontName = TextStyle_GetFontName(style, &FontName) && (FontName != NULL);
    if (outHasFontName != NULL)
    {
        *outHasFontName = HasFontName;
    }
    outFullName[0] = 0U;
    if (!HasFontName)
    {
        return true;
    }
    GenericBuffer Name;
    GenericBuffer_CreateConstant(&Name, outFullName, NAMESPACED_KEY_BUFFER_SIZE, sizeof(unsigned char), 0U);
    Error Result = NamespacedKey_Parse(FontName, StringUTF8_GetByteLength(FontName), NULL, &Name);
    bool IsValid = Result.Code == ErrorCode_Success;
    Error_Deconstruct(&Result);
    if (outHasFontName != NULL)
    {
        *outHasFontName = IsValid;
    }
    return IsValid;
}

/* Observes the changes a revert makes, so the ranges move with each. */
static void ObserveRevert(const UITextEditChange* change, const UserData* userData)
{
    UITextBoxCapability* Self = UserData_GetPointer(userData);
    UITextBoxInternal_OnTextChanged(Self, change);
}


// Static functions: what a masked text's offsets are.
static size_t GetMaskBytes(const UITextBoxCapability* self)
{
    size_t Count = CharUTF8_GetByteCountCodepoint((self->_builtMask <= (uint32_t)INT32_MAX) ? (CodePoint)self->_builtMask : CODEPOINT_NONE);
    return (Count > 0U) ? Count : 1U;
}


// Static functions: a component tree.
static bool IsDefaultColor(RenderColor color)
{
    RenderColor White = RenderColor_White();
    return (color.Tint.r == White.Tint.r) && (color.Tint.g == White.Tint.g) && (color.Tint.b == White.Tint.b)
        && (color.Brightness == White.Brightness) && (color.Opacity == White.Opacity);
}

/* Whether a string component has only the defaults: what an editable box flattening a tree keeps of it. */
static bool IsPlainString(const StringComponent* string)
{
    return (StringComponent_GetFontName(string) == NULL) && IsDefaultColor(StringComponent_GetColor(string))
        && (StringComponent_GetSize(string) == STRING_COMPONENT_DEFAULT_SIZE)
        && (StringComponent_IsShadowActive(string) == STRING_COMPONENT_DEFAULT_IS_SHADOW_ACTIVE)
        && (StringComponent_GetShadowColorType(string) == TextShadowColorType_Default)
        && (StringComponent_GetShadowOffsetType(string) == TextShadowOffsetType_Default) && !StringComponent_IsUnderlined(string)
        && !StringComponent_IsStrikethrough(string) && (StringComponent_GetSpacing(string) == STRING_COMPONENT_DEFAULT_SPACING);
}

/* Appends a tree's flow text as the layout reads it: a string's text, a translate node's fallback (else its key), a sprite as U+FFFC; its
 * own content first, then its children. *outIsPlain tells whether the tree had only plain strings. A tree is at most
 * TEXT_COMPONENT_MAX_DEPTH deep, and the walk keeps to that. */
static Error AppendFlowText(const TextComponent* component, size_t depth, bool isWithSprites, GenericBuffer* outText, bool* outIsPlain)
{
    if (depth > (size_t)TEXT_COMPONENT_MAX_DEPTH)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UITextBox: a component tree deeper than its limit.");
    }
    const unsigned char* Text = NULL;
    switch (TextComponent_GetType(component))
    {
        case TextComponentType_String:
        {
            const StringComponent* String = TextComponent_AsConstStringComponent(component);
            Text = StringComponent_GetText(String);
            *outIsPlain = *outIsPlain && IsPlainString(String);
            break;
        }
        case TextComponentType_Translate:
        {
            // TODO: once translations exist, a translate node's flow text is its translation first, as the layout takes it (TextDocument's
            // translation hook): this walk has no document, so it must read the text from the look's layout, or the offsets part ways.
            const TranslateComponent* Translate = TextComponent_AsConstTranslateComponent(component);
            const unsigned char* Fallback = TranslateComponent_GetFallback(Translate);
            Text = (Fallback[0] != 0U) ? Fallback : TranslateComponent_GetKey(Translate);
            *outIsPlain = false;
            break;
        }
        case TextComponentType_Sprite:
            *outIsPlain = false;
            // AppendRangeBytes only reads its source; the API is just not declared const.
            if (isWithSprites && !GenericBuffer_AppendRangeBytes(outText, (unsigned char*)ObjectCharacter, OBJECT_CHARACTER_BYTES))
            {
                return GrowthError();
            }
            break;
        case TextComponentType_Empty:
            break;
    }
    if ((Text != NULL) && (Text[0] != 0U) && !GenericBuffer_AppendRangeBytes(outText, (unsigned char*)Text, StringUTF8_GetByteLength(Text)))
    {
        return GrowthError();
    }
    for (size_t Index = 0U; Index < TextComponent_GetSubComponentCount(component); Index++)
    {
        Error Result = AppendFlowText(TextComponent_GetSubComponentAt(component, Index), depth + 1U, isWithSprites, outText, outIsPlain);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* A tree's flow text into the box's scratch: with its sprites as object characters (what a read-only box shows), or without them (what an
 * editable box keeps). */
static Error WriteFlowText(UITextBoxCapability* self, const TextComponent* root, bool isWithSprites, bool* outIsPlain)
{
    bool IsPlain = true;
    GenericBuffer_Clear(&self->_scratch);
    Error Result = AppendFlowText(root, 1U, isWithSprites, &self->_scratch, &IsPlain);
    if (outIsPlain != NULL)
    {
        *outIsPlain = IsPlain;
    }
    return Result;
}

static void ReleaseComponent(UITextBoxCapability* self)
{
    if (self->_component != NULL)
    {
        Error Result = TextComponentFactory_ReturnTree(self->_factory, self->_component);
        Error_Deconstruct(&Result);
        self->_component = NULL;
    }
}


// Static functions: building the document.
/* A string component of a part of the text, in the plain colour, with the ranges over it (@p over: their indices, in the order they apply
 * in) applied, added under the root. */
static Error AddSegment(UITextBoxCapability* self, TextComponentFactory* factory, TextComponent* root, size_t start, size_t end,
    RenderColor textColor, const size_t* over, size_t overCount)
{
    GenericBuffer_Clear(&self->_scratch); // Its own scratch.
    // AppendRangeBytes only reads its source; the API is just not declared const.
    bool IsCopied = GenericBuffer_AppendRangeBytes(&self->_scratch, (unsigned char*)UITextEdit_GetText(&self->Edit) + start, end - start)
        && GenericBuffer_AppendByte(&self->_scratch, 0U);
    if (!IsCopied)
    {
        return GrowthError();
    }
    StringComponent* String = NULL;
    Error Result = TextComponentFactory_CreateString(factory, self->_scratch._data, &String);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = StringComponent_SetSize(String, 1.0f);
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetColor(String, textColor);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetIsShadowActive(String, false);
    }
    for (size_t Index = 0U; (Index < overCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        const UITextBoxStyleRecord* Record = GetRecord(self, over[Index]);
        TextStyle Style = Record->Style;
        if (Record->HasFontName)
        {
            TextStyle_SetFontName(&Style, Record->FontName);
        }
        Result = TextStyle_ApplyToComponent(&Style, StringComponent_AsComponent(String));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = TextComponent_AddSubComponent(root, StringComponent_AsComponent(String));
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = TextComponentFactory_ReturnTree(factory, StringComponent_AsComponent(String));
        Error_Deconstruct(&Cleanup);
    }
    return Result;
}

static ComparisonResult CompareOffsets(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b, const UserData* userData)
{
    (void)buffer;
    (void)userData;
    size_t A = *(const size_t*)a._element;
    size_t B = *(const size_t*)b._element;
    return (A < B) ? ComparisonResult_ALessThanB : ((A > B) ? ComparisonResult_AGreaterThanB : ComparisonResult_AEqualsB);
}

/* A range's start and index, ordered by the start, then by the index (the order ranges apply in). */
typedef struct RangeStartStruct
{
    size_t Start;
    size_t Index;
} RangeStart;

static ComparisonResult CompareRangeStarts(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    const RangeStart* A = a._element;
    const RangeStart* B = b._element;
    if (A->Start != B->Start)
    {
        return (A->Start < B->Start) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
    }
    return (A->Index < B->Index) ? ComparisonResult_ALessThanB : ((A->Index > B->Index) ? ComparisonResult_AGreaterThanB : ComparisonResult_AEqualsB);
}

/* The pieces of the text between the ranges' ends, each with the ranges over it, swept from the start: the cuts and the ranges' starts are
 * sorted once, and the ranges over a piece are kept in the order they apply in. What a keystroke costs grows with the text and with the
 * ranges over each piece: disjoint ranges cost each once, fully nested ones the square of their count. */
static Error AddRangedPieces(UITextBoxCapability* self, TextComponentFactory* factory, TextComponent* root, RenderColor textColor,
    GenericBuffer* cuts, GenericBuffer* starts, GenericBuffer* over)
{
    size_t Length = UITextEdit_GetLength(&self->Edit);
    size_t Count = self->_styleRanges._count;
    size_t Ends[2] = { 0U, Length };
    bool IsAdded = GenericBuffer_AddLast(cuts, &Ends[0]) && GenericBuffer_AddLast(cuts, &Ends[1]);
    for (size_t Index = 0U; (Index < Count) && IsAdded; Index++)
    {
        const UITextBoxStyleRecord* Record = GetRecord(self, Index);
        size_t RangeEnds[2] = { Record->Start, Record->End };
        RangeStart Start = { .Start = Record->Start, .Index = Index };
        IsAdded = GenericBuffer_AddLast(cuts, &RangeEnds[0]) && GenericBuffer_AddLast(cuts, &RangeEnds[1])
            && GenericBuffer_AddLast(starts, &Start);
    }
    if (!IsAdded || !GenericBuffer_SortAscendingAllocating(cuts, CompareOffsets, NULL)
        || !GenericBuffer_SortAscendingAllocating(starts, CompareRangeStarts, NULL))
    {
        return GrowthError();
    }
    const size_t* Cuts = (const size_t*)cuts->_data;
    const RangeStart* Starts = (const RangeStart*)starts->_data;
    size_t* Over = (size_t*)over->_data;
    size_t OverCount = 0U;
    size_t NextStart = 0U;
    for (size_t Cut = 0U; ((Cut + 1U) < cuts->_count) && (Cuts[Cut] < Length); Cut++)
    {
        size_t PieceStart = Cuts[Cut];
        size_t PieceEnd = Cuts[Cut + 1U];
        if (PieceEnd <= PieceStart)
        {
            continue;
        }
        // The ranges that ended leave; the ones that start here come in at their place in the order.
        size_t Kept = 0U;
        for (size_t Index = 0U; Index < OverCount; Index++)
        {
            if (GetRecord(self, Over[Index])->End > PieceStart)
            {
                Over[Kept] = Over[Index];
                Kept++;
            }
        }
        OverCount = Kept;
        for (; (NextStart < Count) && (Starts[NextStart].Start <= PieceStart); NextStart++)
        {
            size_t Coming = Starts[NextStart].Index;
            if (GetRecord(self, Coming)->End <= PieceStart)
            {
                continue;
            }
            size_t Place = OverCount;
            while ((Place > 0U) && (Over[Place - 1U] > Coming))
            {
                Over[Place] = Over[Place - 1U];
                Place--;
            }
            Over[Place] = Coming;
            OverCount++;
        }
        Error Result = AddSegment(self, factory, root, PieceStart, PieceEnd, textColor, Over, OverCount);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* The text cut at every range's ends, one string component per piece. */
static Error AddStyledText(UITextBoxCapability* self, TextComponentFactory* factory, TextComponent* root, RenderColor textColor)
{
    size_t Length = UITextEdit_GetLength(&self->Edit);
    size_t Count = self->_styleRanges._count;
    if ((Count == 0U) || (Length == 0U))
    {
        // An empty text still gets its piece: the layout takes a line's height from it, which the caret is as tall as.
        return AddSegment(self, factory, root, 0U, Length, textColor, NULL, 0U);
    }
    // The ranges over a piece are written straight into Over's room: never more than all of them.
    GenericBuffer Cuts;
    GenericBuffer Starts;
    GenericBuffer Over;
    GenericBuffer_AllocateVariable(&Cuts, (2U * Count) + 2U, sizeof(size_t));
    GenericBuffer_AllocateVariable(&Starts, Count, sizeof(RangeStart));
    GenericBuffer_AllocateVariable(&Over, Count, sizeof(size_t));
    Error Result = AddRangedPieces(self, factory, root, textColor, &Cuts, &Starts, &Over);
    Memory_Free(Cuts._data);
    Memory_Free(Starts._data);
    Memory_Free(Over._data);
    return Result;
}

/* A masked text: one mask character per character, line breaks kept, in the plain colour. */
static Error AddMaskedText(UITextBoxCapability* self, TextComponentFactory* factory, TextComponent* root, uint32_t mask, RenderColor textColor)
{
    unsigned char Mask[4];
    size_t MaskBytes = CharUTF8_WriteCodePoint(Mask, (mask <= (uint32_t)INT32_MAX) ? (CodePoint)mask : CODEPOINT_NONE);
    if (MaskBytes == 0U)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITextBox: the mask is no codepoint.");
    }
    GenericBuffer_Clear(&self->_scratch); // Its own scratch.
    const unsigned char* Text = UITextEdit_GetText(&self->Edit);
    size_t Length = UITextEdit_GetLength(&self->Edit);
    for (size_t Offset = 0U; Offset < Length; Offset = UITextEdit_GetNextBoundary(Text, Length, Offset))
    {
        bool IsBreak = Text[Offset] == (unsigned char)'\n';
        bool IsAdded = IsBreak ? GenericBuffer_AppendByte(&self->_scratch, (unsigned char)'\n')
            : GenericBuffer_AppendRangeBytes(&self->_scratch, Mask, MaskBytes);
        if (!IsAdded)
        {
            return GrowthError();
        }
    }
    if (!GenericBuffer_AppendByte(&self->_scratch, 0U))
    {
        return GrowthError();
    }
    StringComponent* String = NULL;
    Error Result = TextComponentFactory_CreateString(factory, self->_scratch._data, &String);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = StringComponent_SetSize(String, 1.0f);
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetColor(String, textColor);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetIsShadowActive(String, false);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = TextComponent_AddSubComponent(root, StringComponent_AsComponent(String));
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = TextComponentFactory_ReturnTree(factory, StringComponent_AsComponent(String));
        Error_Deconstruct(&Cleanup);
    }
    return Result;
}


// Static functions: layout.
/* The look's insets, or none. */
static UIInsets GetInsets(const UITextBoxCapability* self, const Widget* widget)
{
    if (self->_geometry == NULL)
    {
        return UIInsets_All(0.0f);
    }
    return self->_geometry->GetTextInsets(widget);
}

/* How far in from the box's edges the bars stand: the look's border; none without the look's answer. */
static UIInsets GetBarInsets(const UITextBoxCapability* self, const Widget* widget)
{
    if ((self->_geometry == NULL) || (self->_geometry->GetBarInsets == NULL))
    {
        return UIInsets_All(0.0f);
    }
    return self->_geometry->GetBarInsets(widget);
}

/* The room a bar takes from the text's area, which ends at the text's inset: its thickness, and the part of the border it stands inside of
 * that goes deeper than the text's inset. */
static void GetBarRoom(const UITextBoxCapability* self, const Widget* widget, const float thickness[UIScrollAxis_Count], float outRoom[UIScrollAxis_Count])
{
    UIInsets Insets = GetInsets(self, widget);
    UIInsets BarInsets = GetBarInsets(self, widget);
    outRoom[UIScrollAxis_Horizontal] = thickness[UIScrollAxis_Horizontal] + Math_MaxFloat(0.0f, BarInsets.Bottom - Insets.Bottom);
    outRoom[UIScrollAxis_Vertical] = thickness[UIScrollAxis_Vertical] + Math_MaxFloat(0.0f, BarInsets.Right - Insets.Right);
}

/* The text's size at a view's width, in UI pixels, the caret's room counted; zero without the look's answers. */
static Error MeasureTextSize(UITextBoxCapability* self, Widget* widget, float viewWidth, Vector2* outSize, float* outWrapWidth)
{
    *outSize = (Vector2){ .x = 0.0f, .y = 0.0f };
    *outWrapWidth = Math_MaxFloat(0.0f, viewWidth - CARET_ROOM);
    if (self->_geometry == NULL)
    {
        return Error_CreateSuccess();
    }
    const TextLayout* Layout = NULL;
    float UnitsToUI = 0.0f;
    Error Result = self->_geometry->LayOut(widget, self->_isWrapped, *outWrapWidth, &Layout, &UnitsToUI);
    if ((Result.Code != ErrorCode_Success) || (Layout == NULL))
    {
        return Result;
    }
    // As wide as a caret can stand (a line's trailing spaces count), so the view can always scroll to it.
    Vector2 Size = TextLayout_GetSize(Layout);
    float Width = Math_MaxFloat(Size.x, TextLayout_GetCaretExtent(Layout));
    float LineHeight = self->_geometry->GetLineHeight(widget);
    *outSize = (Vector2){ .x = (Width * UnitsToUI) + CARET_ROOM, .y = Math_MaxFloat(Size.y * UnitsToUI, LineHeight) };
    return Error_CreateSuccess();
}

/* Decides which bars show and how large the text and the view are, starting from what the last layout decided. @p room is what each bar
 * takes from the area while it shows (GetBarRoom). */
static Error DecideBars(UITextBoxCapability* self, Widget* widget, Vector2 area, const float room[UIScrollAxis_Count], Vector2* outContent,
    Vector2* outView)
{
    bool IsShown[UIScrollAxis_Count];
    for (int32_t Axis = 0; Axis < (int32_t)UIScrollAxis_Count; Axis++)
    {
        UIScrollMode Mode = GetMode(self, (UIScrollAxis)Axis);
        IsShown[Axis] = (Mode == UIScrollMode_Always) || ((Mode == UIScrollMode_Auto) && self->_isBarShown[Axis]);
    }
    for (int32_t Pass = 0; Pass < MAX_BAR_PASSES; Pass++)
    {
        Vector2 View =
        {
            .x = Math_MaxFloat(0.0f, area.x - (IsShown[UIScrollAxis_Vertical] ? room[UIScrollAxis_Vertical] : 0.0f)),
            .y = Math_MaxFloat(0.0f, area.y - (IsShown[UIScrollAxis_Horizontal] ? room[UIScrollAxis_Horizontal] : 0.0f))
        };
        Vector2 Text = { .x = 0.0f, .y = 0.0f };
        Error Result = MeasureTextSize(self, widget, View.x, &Text, &self->_wrapWidth);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        *outView = View;
        *outContent = (Vector2)
        {
            // Wrapped text is as wide as the view, but for a word longer than a line: then it scrolls across too, by the mode.
            .x = (GetMode(self, UIScrollAxis_Horizontal) == UIScrollMode_Disabled) ? View.x : Math_MaxFloat(Text.x, View.x),
            .y = (GetMode(self, UIScrollAxis_Vertical) == UIScrollMode_Disabled) ? View.y : Math_MaxFloat(Text.y, View.y)
        };
        bool Needs[UIScrollAxis_Count];
        for (int32_t Axis = 0; Axis < (int32_t)UIScrollAxis_Count; Axis++)
        {
            UIScrollMode Mode = GetMode(self, (UIScrollAxis)Axis);
            float Content = (Axis == (int32_t)UIScrollAxis_Horizontal) ? outContent->x : outContent->y;
            float Length = (Axis == (int32_t)UIScrollAxis_Horizontal) ? View.x : View.y;
            Needs[Axis] = (Mode == UIScrollMode_Always) || ((Mode == UIScrollMode_Auto) && (Content > (Length + FIT_TOLERANCE)));
        }
        if ((Needs[UIScrollAxis_Horizontal] == IsShown[UIScrollAxis_Horizontal]) && (Needs[UIScrollAxis_Vertical] == IsShown[UIScrollAxis_Vertical]))
        {
            break;
        }
        IsShown[UIScrollAxis_Horizontal] = Needs[UIScrollAxis_Horizontal];
        IsShown[UIScrollAxis_Vertical] = Needs[UIScrollAxis_Vertical];
    }
    self->_isBarShown[UIScrollAxis_Horizontal] = IsShown[UIScrollAxis_Horizontal];
    self->_isBarShown[UIScrollAxis_Vertical] = IsShown[UIScrollAxis_Vertical];
    return Error_CreateSuccess();
}

/* Scrolls as little as it takes to show the caret, now that the layout is current. */
static void RevealCaret(UITextBoxCapability* self, Widget* widget)
{
    const TextLayout* Layout = NULL;
    float UnitsToUI = 0.0f;
    if (!UITextBoxInternal_GetLayout(self, widget, &Layout, &UnitsToUI))
    {
        return;
    }
    Rectangle Caret = { 0 };
    if (!TextLayout_GetCaretBox(Layout, UITextBoxInternal_ToLayoutOffset(self, UITextEdit_GetCaret(&self->Edit)), &Caret))
    {
        return;
    }
    Rectangle Box = { .x = Caret.x * UnitsToUI, .y = Caret.y * UnitsToUI, .width = CARET_ROOM, .height = Caret.height * UnitsToUI };
    UIScrollableCapability_EnsureVisible(&self->Scrollable, Box);
}


// Static functions: the kind's behaviour, for the kit's composed widget types.
static void* ResolveCapability(void* widget)
{
    return UITextBoxInternal_GetCapability(widget);
}

static void* ResolveScrollable(void* widget)
{
    return &UITextBoxInternal_GetCapability(widget)->Scrollable;
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextBox: the arguments are required.");
    }
    return UITextBoxCapability_Construct(UITextBoxInternal_GetCapability(widget), widget, args);
}

static Error Behaviour_Deconstruct(void* widget)
{
    return UITextBoxCapability_Deconstruct(UITextBoxInternal_GetCapability(widget));
}

Error UITextBoxInternal_Update(void* widget, ProgramTime time)
{
    UITextBoxCapability* Self = UITextBoxInternal_GetCapability(widget);
    Self->_time = time.TotalTime;
    Error Result = UITextBoxInternal_UpdateVisit(widget);
    return (Result.Code == ErrorCode_Success) ? UITextBoxInternal_UpdateDrag(widget, time.PassedTime) : Result;
}

static Error Behaviour_OnHoverStart(void* widget, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(widget, UIElementCue_Hover);
}

Error UITextBoxInternal_Layout(void* widget, const UILayoutContext* context)
{
    UITextBoxCapability* Self = UITextBoxInternal_GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Screen, widget);
    Vector2 BoxSize = { .x = Pixels.width / Scale, .y = Pixels.height / Scale };
    UIInsets Insets = GetInsets(Self, widget);
    float Thickness[UIScrollAxis_Count] = { 0.0f, 0.0f };
    Error Result = UIScrollbar_MeasureThickness(Screen, Self->_barIds[UIScrollAxis_Horizontal], UIScrollAxis_Horizontal, context->Sizing,
        &Thickness[UIScrollAxis_Horizontal]);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScrollbar_MeasureThickness(Screen, Self->_barIds[UIScrollAxis_Vertical], UIScrollAxis_Vertical, context->Sizing,
            &Thickness[UIScrollAxis_Vertical]);
    }
    Vector2 Area = { .x = Math_MaxFloat(0.0f, BoxSize.x - Insets.Left - Insets.Right), .y = Math_MaxFloat(0.0f, BoxSize.y - Insets.Top - Insets.Bottom) };
    Vector2 Content = { .x = 0.0f, .y = 0.0f };
    Vector2 View = { .x = 0.0f, .y = 0.0f };
    float Room[UIScrollAxis_Count] = { 0.0f, 0.0f };
    GetBarRoom(Self, widget, Thickness, Room);
    if (Result.Code == ErrorCode_Success)
    {
        Result = DecideBars(Self, widget, Area, Room, &Content, &View);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIScrollableCapability_SetSizes(&Self->Scrollable, Content, View);
    Self->_viewBox = (Rectangle){ .x = Insets.Left, .y = Insets.Top, .width = View.x, .height = View.y };
    Self->Scrollable._viewWidgetId = Widget_GetId(widget);
    if (Self->_isRevealPending)
    {
        RevealCaret(Self, widget);
        Self->_isRevealPending = false;
    }
    Self->Scrollable._placedOffset = Self->Scrollable._offset;
    bool IsHorizontalShown = Self->_isBarShown[UIScrollAxis_Horizontal];
    bool IsVerticalShown = Self->_isBarShown[UIScrollAxis_Vertical];
    UIInsets BarInsets = GetBarInsets(Self, widget);
    Result = UIScrollbar_PlaceAlongEdge(Screen, Self->_barIds[UIScrollAxis_Vertical], UIScrollAxis_Vertical, IsVerticalShown,
        Thickness[UIScrollAxis_Vertical], IsHorizontalShown ? Thickness[UIScrollAxis_Horizontal] : 0.0f, BarInsets, Scale);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIScrollbar_PlaceAlongEdge(Screen, Self->_barIds[UIScrollAxis_Horizontal], UIScrollAxis_Horizontal, IsHorizontalShown,
        Thickness[UIScrollAxis_Horizontal], IsVerticalShown ? Thickness[UIScrollAxis_Vertical] : 0.0f, BarInsets, Scale);
}


// Fields.
static const UIKindCapability TextBoxCapabilities[] =
{
    { .Capability = UIElementCapability_TextBox, .Resolve = ResolveCapability },
    { .Capability = UIElementCapability_Scrollable, .Resolve = ResolveScrollable }
};

static const UIKindBehaviour TextBoxBehaviour =
{
    .Kind = UIElementKind_TextBox,
    .Capabilities = TextBoxCapabilities,
    .CapabilityCount = sizeof(TextBoxCapabilities) / sizeof(TextBoxCapabilities[0]),
    .BaseSize = sizeof(UITextBoxLookBase),
    .IsFocusable = true,
    .NeedsEntering = true,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Deconstruct = Behaviour_Deconstruct,
    .Update = UITextBoxInternal_Update,
    .Layout = UITextBoxInternal_Layout,
    .OnHoverStart = Behaviour_OnHoverStart,
    .OnMouseInput = UITextBoxInternal_OnMouseInput,
    .OnPressLost = UITextBoxInternal_OnPressLost,
    .OnScrollInput = UITextBoxInternal_OnScrollInput,
    .OnKeyboardInput = UITextBoxInternal_OnKeyboardInput,
    .OnEnter = UITextBoxInternal_OnEnter,
    .OnLeave = UITextBoxInternal_OnLeave
};


// Internal functions (declared in UIKitInternal.h and UITextBoxInternal.h).
const UIKindBehaviour* UITextBox_GetKindBehaviour(void)
{
    return &TextBoxBehaviour;
}

UITextBoxCapability* UITextBoxInternal_GetCapability(void* widget)
{
    return &((UITextBoxLookBase*)widget)->TextBox;
}

Error UITextBoxInternal_Resolve(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UITextBoxCapability** outBox)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UITextBox: the widget is gone, or its screen has no kit.");
    }
    UITextBoxCapability* Box = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_TextBox));
    if (Box == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UITextBox: the widget is not a text box.");
    }
    *outWidget = Target;
    *outBox = Box;
    return Error_CreateSuccess();
}

void UITextBoxInternal_OnTextChanged(UITextBoxCapability* self, const UITextEditChange* change)
{
    if (change->HasChanged)
    {
        AdjustRanges(self, change->Start, change->RemovedLength, change->InsertedLength);
        self->_contentRevision++;
        self->_hasColumn = false; // Up and Down measure their column again from where the caret now is.
    }
    UITextBoxInternal_OnCaretMoved(self);
}

void UITextBoxInternal_OnCaretMoved(UITextBoxCapability* self)
{
    self->_caretRevision++;
    self->_isRevealPending = true;
}

bool UITextBoxInternal_GetLayout(UITextBoxCapability* self, Widget* widget, const TextLayout** outLayout, float* outUnitsToUI)
{
    *outLayout = NULL;
    *outUnitsToUI = 0.0f;
    if (self->_geometry == NULL)
    {
        return false;
    }
    Error Result = self->_geometry->LayOut(widget, self->_isWrapped, self->_wrapWidth, outLayout, outUnitsToUI);
    bool IsLaidOut = (Result.Code == ErrorCode_Success) && (*outLayout != NULL) && TextLayout_HasOffsets(*outLayout) && (*outUnitsToUI > 0.0f);
    Error_Deconstruct(&Result); // A look that cannot lay its text out now places no caret by it; it reports its own failures.
    return IsLaidOut;
}

size_t UITextBoxInternal_ToLayoutOffset(const UITextBoxCapability* self, size_t boxOffset)
{
    if (!self->_isMasked)
    {
        return boxOffset;
    }
    const unsigned char* Text = UITextEdit_GetText(&self->Edit);
    size_t Length = UITextEdit_GetLength(&self->Edit);
    size_t MaskBytes = GetMaskBytes(self);
    size_t Layout = 0U;
    for (size_t Offset = 0U; (Offset < boxOffset) && (Offset < Length); Offset = UITextEdit_GetNextBoundary(Text, Length, Offset))
    {
        Layout += (Text[Offset] == (unsigned char)'\n') ? 1U : MaskBytes;
    }
    return Layout;
}

size_t UITextBoxInternal_ToBoxOffset(const UITextBoxCapability* self, size_t layoutOffset)
{
    if (!self->_isMasked)
    {
        return UITextEdit_SnapToBoundary(UITextEdit_GetText(&self->Edit), UITextEdit_GetLength(&self->Edit), layoutOffset);
    }
    const unsigned char* Text = UITextEdit_GetText(&self->Edit);
    size_t Length = UITextEdit_GetLength(&self->Edit);
    size_t MaskBytes = GetMaskBytes(self);
    size_t Layout = 0U;
    size_t Offset = 0U;
    while (Offset < Length)
    {
        size_t Step = (Text[Offset] == (unsigned char)'\n') ? 1U : MaskBytes;
        if ((Layout + Step) > layoutOffset)
        {
            break;
        }
        Layout += Step;
        Offset = UITextEdit_GetNextBoundary(Text, Length, Offset);
    }
    return Offset;
}

Error UITextBoxInternal_PostEdit(Widget* widget, UITextBoxCapability* self, const UITextEditChange* change, bool isWhole)
{
    UITextBoxInternal_OnTextChanged(self, change);
    Error Result = change->IsRefused ? UIElement_PostCue(widget, UIElementCue_Refused) : Error_CreateSuccess();
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (self->_host != NULL)
    {
        return self->_host->OnEdited(widget, change, isWhole);
    }
    if (!change->HasChanged)
    {
        return Result;
    }
    UIScreen* Screen = Widget_GetScreen(widget);
    UIPropertyValue Length = UIPropertyValue_FromDouble((double)UITextEdit_GetLength(&self->Edit));
    Result = UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Changed, &Length);
    if ((Result.Code == ErrorCode_Success) && isWhole)
    {
        Result = UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Committed, &Length);
    }
    return Result;
}

Error UITextBoxInternal_SetText(UITextBoxCapability* self, const unsigned char* text, size_t length)
{
    ReleaseComponent(self);
    UITextEditChange Change;
    Error Result = UITextEdit_SetText(&self->Edit, text, length, &Change);
    OnTextSetWhole(self);
    return Result;
}

bool UITextBoxInternal_HasTextChangedSinceEntering(const UITextBoxCapability* self)
{
    size_t Length = UITextEdit_GetLength(&self->Edit);
    return (Length != self->_enteredText._count)
        || ((Length > 0U) && !Memory_IsEqual(UITextEdit_GetText(&self->Edit), self->_enteredText._data, Length));
}

Error UITextBoxInternal_RevertToEntered(UITextBoxCapability* self, bool* outHasChanged)
{
    if (UITextEdit_HasMark(&self->Edit))
    {
        // Back step by step: the ranges move with each step as they did with the edits.
        UserData Data = UserData_FromPointer(self);
        return UITextEdit_RevertToMark(&self->Edit, self->_enteredText._data, self->_enteredText._count, ObserveRevert, &Data, outHasChanged);
    }
    // The steps back are gone (undone past, dropped, or the text or the limits set by code): the text is set back whole, and the ranges
    // keep their places in it, as for text from code.
    UITextEditChange Change;
    Error Result = UITextEdit_SetText(&self->Edit, self->_enteredText._data, self->_enteredText._count, &Change);
    OnTextSetWhole(self);
    if (outHasChanged != NULL)
    {
        *outHasChanged = (Result.Code == ErrorCode_Success) && Change.HasChanged;
    }
    return Result;
}


// Public functions: helpers.
UITextBoxOptions UITextBoxOptions_Create(void)
{
    UITextBoxOptions Options;
    Memory_Zero(&Options, sizeof(Options));
    Options.Element = UIElementOptions_Create();
    Options.Text = NULL;
    Options.Placeholder = NULL;
    Options.Limits = UITextEditLimits_Create();
    Options.Limits.AllowsNewlines = false;
    Options.Undo = UITextEditUndoOptions_Create();
    Options.UndoScope = UITextBoxUndoScope_Box;
    Options.IsEditable = true;
    Options.IsSelectable = true;
    Options.IsEnabled = true;
    Options.AcceptsTab = false;
    Options.AllowsOverwrite = true;
    Options.IsMasked = false;
    Options.EscapeReverts = true;
    Options.IsWrapped = false;
    Options.HasDefault = false;
    Options.Default = NULL;
    Options.Horizontal = UIScrollMode_Hidden;
    Options.Vertical = UIScrollMode_Disabled;
    Options.VisibleLines = 1U;
    return Options;
}

UITextBoxOptions UITextBoxOptions_CreateMultiline(void)
{
    UITextBoxOptions Options = UITextBoxOptions_Create();
    Options.Limits.AllowsNewlines = true;
    Options.EscapeReverts = false;
    Options.IsWrapped = true;
    Options.Horizontal = UIScrollMode_Hidden; // Only a word longer than a line takes it across.
    Options.Vertical = UIScrollMode_Auto;
    Options.VisibleLines = DEFAULT_MULTILINE_LINES;
    return Options;
}


// Public functions: behaviour helpers.
Error UITextBoxCapability_Construct(UITextBoxCapability* self, Widget* widget, const UITextBoxArgs* args)
{
    const UITextBoxOptions* Options = args->Options;
    bool IsValid = UITextEditLimits_IsValid(&Options->Limits) && UITextEditUndoOptions_IsValid(&Options->Undo)
        && IsUndoScopeValid(Options->UndoScope) && UIScrollMode_IsValid(Options->Horizontal) && UIScrollMode_IsValid(Options->Vertical)
        && (Options->VisibleLines >= 1U) && (Options->VisibleLines <= UI_TEXT_BOX_MAX_VISIBLE_LINES);
    if (!IsValid)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITextBox: the limits, the undo options, the modes or the line count are not valid.");
    }
    const unsigned char* Text = (Options->Text != NULL) ? Options->Text : (const unsigned char*)u8"";
    if (!StringUTF8_IsEncodingValid(Text))
    {
        return Error_Construct2(ErrorCode_InvalidTextEncoding, "UITextBox: the text is not valid UTF-8.");
    }
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Memory_Zero(self, sizeof(*self));
    Result = UITextEdit_Construct(&self->Edit, &Options->Limits, &Options->Undo);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UITextContent_Construct(&self->_default, Options->HasDefault ? Options->Default : NULL);
    if (Result.Code != ErrorCode_Success)
    {
        UITextEdit_Deconstruct(&self->Edit);
        return Result;
    }
    Result = UITextContent_Construct(&self->_placeholder, Options->Placeholder);
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&self->_default);
        UITextEdit_Deconstruct(&self->Edit);
        return Result;
    }
    Result = UITextEdit_SetText(&self->Edit, Text, StringUTF8_GetByteLength(Text), NULL);
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&self->_placeholder);
        UITextContent_Deconstruct(&self->_default);
        UITextEdit_Deconstruct(&self->Edit);
        return Result;
    }
    UIScrollableCapability_Construct(&self->Scrollable, Options->Horizontal, Options->Vertical, UI_SCROLLABLE_DEFAULT_WHEEL_STEP);
    GenericBuffer_AllocateVariable(&self->_enteredText, 0U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&self->_styleRanges, 0U, sizeof(UITextBoxStyleRecord));
    GenericBuffer_AllocateVariable(&self->_scratch, 0U, sizeof(unsigned char));
    self->_isEditable = Options->IsEditable;
    self->_isSelectable = Options->IsSelectable || Options->IsEditable;
    self->_isEnabled = Options->IsEnabled;
    self->_acceptsTab = Options->AcceptsTab;
    self->_allowsOverwrite = Options->AllowsOverwrite;
    self->_isMasked = Options->IsMasked;
    self->_escapeReverts = Options->EscapeReverts;
    self->_isWrapped = Options->IsWrapped;
    self->_hasDefault = Options->HasDefault;
    self->_undoScope = Options->UndoScope;
    self->_visibleLines = Options->VisibleLines;
    self->_factory = UIKit_GetTextFactory(args->Kit);
    self->_contentRevision = 1U;
    self->_caretRevision = 1U;
    self->_builtMask = (uint32_t)'*';
    self->_dragUnit = (uint32_t)UITextBoxDragUnit_Character;
    for (int32_t Axis = 0; Axis < (int32_t)UIScrollAxis_Count; Axis++)
    {
        self->_barIds[Axis] = WIDGET_ID_INVALID;
    }
    // The widget is not alive yet: nothing below can fail or run a hook.
    Error Flags = Widget_SetIsFocusable(widget, self->_isEnabled && self->_isSelectable);
    Error_Deconstruct(&Flags);
    Widget_SetCursorShape(widget, self->_isSelectable ? UICursorShape_IBeam : UICursorShape_Arrow);
    return Error_CreateSuccess();
}

Error UITextBoxCapability_Deconstruct(UITextBoxCapability* self)
{
    ReleaseComponent(self);
    UITextContent_Deconstruct(&self->_placeholder);
    UITextContent_Deconstruct(&self->_default);
    UITextEdit_Deconstruct(&self->Edit);
    Memory_Free(self->_enteredText._data);
    Memory_Free(self->_styleRanges._data);
    Memory_Free(self->_scratch._data);
    Memory_Zero(&self->_enteredText, sizeof(self->_enteredText));
    Memory_Zero(&self->_styleRanges, sizeof(self->_styleRanges));
    Memory_Zero(&self->_scratch, sizeof(self->_scratch));
    return Error_CreateSuccess();
}

void UITextBoxCapability_SetGeometry(UITextBoxCapability* self, const UITextBoxGeometry* geometry)
{
    self->_geometry = geometry;
}

void UITextBoxCapability_SetHost(UITextBoxCapability* self, const UITextBoxHost* host)
{
    self->_host = host;
}

UIElementState UITextBoxCapability_GetState(const UITextBoxCapability* self, const Widget* widget)
{
    return UIElementState_Choose(self->_isEnabled, self->_isDragging, self->_isInvalid, Widget_IsEntered(widget), false, Widget_IsHovered(widget),
        Widget_IsFocused(widget));
}

Error UITextBoxCapability_BuildDocument(UITextBoxCapability* self, TextComponentFactory* factory, uint32_t mask, RenderColor textColor,
    TextComponent** outRoot)
{
    *outRoot = NULL;
    if ((self->_component != NULL) && !self->_isMasked)
    {
        return TextComponentFactory_CloneTree(factory, self->_component, outRoot);
    }
    EmptyComponent* Root = NULL;
    Error Result = TextComponentFactory_CreateEmpty(factory, &Root);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (self->_isMasked)
    {
        Result = AddMaskedText(self, factory, EmptyComponent_AsComponent(Root), mask, textColor);
        self->_builtMask = (Result.Code == ErrorCode_Success) ? mask : self->_builtMask;
    }
    else
    {
        Result = AddStyledText(self, factory, EmptyComponent_AsComponent(Root), textColor);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = TextComponentFactory_ReturnTree(factory, EmptyComponent_AsComponent(Root));
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    *outRoot = EmptyComponent_AsComponent(Root);
    return Error_CreateSuccess();
}

bool UITextBoxCapability_IsShowingPlaceholder(const UITextBoxCapability* self)
{
    return (UITextEdit_GetLength(&self->Edit) == 0U) && (UITextContent_Get(&self->_placeholder)[0] != 0U);
}

bool UITextBoxCapability_IsCaretShown(const UITextBoxCapability* self, const Widget* widget)
{
    return self->_isEditable && self->_isEnabled && Widget_IsEntered(widget);
}

size_t UITextBoxCapability_GetLayoutCaret(const UITextBoxCapability* self)
{
    return UITextBoxInternal_ToLayoutOffset(self, UITextEdit_GetCaret(&self->Edit));
}

void UITextBoxCapability_GetLayoutSelection(const UITextBoxCapability* self, size_t* outStart, size_t* outEnd)
{
    size_t Start = 0U;
    size_t End = 0U;
    UITextEdit_GetSelection(&self->Edit, &Start, &End);
    *outStart = UITextBoxInternal_ToLayoutOffset(self, Start);
    *outEnd = UITextBoxInternal_ToLayoutOffset(self, End);
}

Error UITextBox_MeasurePreferred(UITextBoxCapability* self, Widget* widget, const UIMeasureContext* context, Vector2 minimum, Vector2* outSize)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    float Thickness[UIScrollAxis_Count] = { 0.0f, 0.0f };
    Error Result = UIScrollbar_MeasureThickness(Screen, self->_barIds[UIScrollAxis_Horizontal], UIScrollAxis_Horizontal, context,
        &Thickness[UIScrollAxis_Horizontal]);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScrollbar_MeasureThickness(Screen, self->_barIds[UIScrollAxis_Vertical], UIScrollAxis_Vertical, context,
            &Thickness[UIScrollAxis_Vertical]);
    }
    UIInsets Insets = GetInsets(self, widget);
    float Room[UIScrollAxis_Count] = { 0.0f, 0.0f };
    GetBarRoom(self, widget, Thickness, Room);
    float LineHeight = (self->_geometry != NULL) ? self->_geometry->GetLineHeight(widget) : 0.0f;
    float Height = ((float)self->_visibleLines * LineHeight) + Insets.Top + Insets.Bottom;
    Height += (GetMode(self, UIScrollAxis_Horizontal) == UIScrollMode_Always) ? Room[UIScrollAxis_Horizontal] : 0.0f;
    float Width = Insets.Left + Insets.Right + ((GetMode(self, UIScrollAxis_Vertical) == UIScrollMode_Always) ? Room[UIScrollAxis_Vertical] : 0.0f);
    *outSize = (Vector2){ .x = Math_MaxFloat(minimum.x, Width), .y = Math_MaxFloat(minimum.y, Height) };
    return Result;
}

void UITextBox_SetBars(UITextBoxCapability* self, uint64_t horizontalBarId, uint64_t verticalBarId)
{
    self->_barIds[UIScrollAxis_Horizontal] = horizontalBarId;
    self->_barIds[UIScrollAxis_Vertical] = verticalBarId;
}


// Public functions: calling code.
Error UITextBox_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const unsigned char* Text = (text != NULL) ? text : (const unsigned char*)u8"";
    if (!StringUTF8_IsEncodingValid(Text))
    {
        return Error_Construct2(ErrorCode_InvalidTextEncoding, "UITextBox_SetText: the text is not valid UTF-8.");
    }
    return UITextBoxInternal_SetText(Box, Text, StringUTF8_GetByteLength(Text));
}

Error UITextBox_GetText(UIScreen* screen, uint64_t widgetId, const unsigned char** outText, size_t* outLength)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outText = UITextEdit_GetText(&Box->Edit);
    if (outLength != NULL)
    {
        *outLength = UITextEdit_GetLength(&Box->Edit);
    }
    return Error_CreateSuccess();
}

Error UITextBox_ReplaceRange(UIScreen* screen, uint64_t widgetId, size_t start, size_t end, const unsigned char* text)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (end < start)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextBox_ReplaceRange: the end must not come before the start.");
    }
    if (Box->_component != NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UITextBox_ReplaceRange: the box shows a component tree; it changes by a new tree or a text (UITextBox_SetComponent, _SetText).");
    }
    size_t Length = (text != NULL) ? StringUTF8_GetByteLength(text) : 0U;
    UITextEditChange Change;
    Result = UITextEdit_Replace(&Box->Edit, start, end, text, Length, UITextEditKind_Whole, Box->_time, &Change);
    if (Result.Code == ErrorCode_Success)
    {
        UITextBoxInternal_OnTextChanged(Box, &Change);
    }
    return Result;
}

Error UITextBox_GetSelection(UIScreen* screen, uint64_t widgetId, size_t* outAnchor, size_t* outCaret)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outAnchor != NULL)
    {
        *outAnchor = UITextEdit_GetAnchor(&Box->Edit);
    }
    if (outCaret != NULL)
    {
        *outCaret = UITextEdit_GetCaret(&Box->Edit);
    }
    return Error_CreateSuccess();
}

Error UITextBox_SetSelection(UIScreen* screen, uint64_t widgetId, size_t anchor, size_t caret)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UITextEdit_Select(&Box->Edit, anchor, caret);
    Box->_hasColumn = false;
    UITextBoxInternal_OnCaretMoved(Box);
    return Error_CreateSuccess();
}

Error UITextBox_SetLimits(UIScreen* screen, uint64_t widgetId, const UITextEditLimits* limits)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UITextBoxInternal_SetLimits(Box, limits);
}

Error UITextBoxInternal_SetLimits(UITextBoxCapability* self, const UITextEditLimits* limits)
{
    UITextEditChange Change;
    Error Result = UITextEdit_SetLimits(&self->Edit, limits, &Change);
    if ((Result.Code == ErrorCode_Success) && (self->_component != NULL))
    {
        // A box showing a tree keeps its text whole, the layout's offsets: the limits hold for a text set later.
        Result = WriteFlowText(self, self->_component, true, NULL);
        if (Result.Code == ErrorCode_Success)
        {
            Result = UITextEdit_SetTextVerbatim(&self->Edit, self->_scratch._data, self->_scratch._count);
        }
    }
    if ((Result.Code == ErrorCode_Success) && Change.HasChanged)
    {
        OnTextSetWhole(self);
    }
    return Result;
}

Error UITextBox_SetUndoOptions(UIScreen* screen, uint64_t widgetId, const UITextEditUndoOptions* options, UITextBoxUndoScope scope)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsUndoScopeValid(scope))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITextBox_SetUndoOptions: unknown scope.");
    }
    Result = UITextEdit_SetUndoOptions(&Box->Edit, options);
    if (Result.Code == ErrorCode_Success)
    {
        Box->_undoScope = scope;
    }
    return Result;
}

Error UITextBox_SetPlaceholder(UIScreen* screen, uint64_t widgetId, const unsigned char* placeholder)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UITextContent_Set(&Box->_placeholder, (placeholder != NULL) ? placeholder : (const unsigned char*)u8"", NULL);
}

Error UITextBox_SetDefault(UIScreen* screen, uint64_t widgetId, bool hasDefault, const unsigned char* text)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UITextContent_Set(&Box->_default, (hasDefault && (text != NULL)) ? text : (const unsigned char*)u8"", NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Box->_hasDefault = hasDefault;
    }
    return Result;
}

/* A box takes focus while it is enabled and its text can be selected; the pointer shows an I-beam over a text that can. */
static Error RefreshFocusability(Widget* widget, const UITextBoxCapability* box)
{
    Widget_SetCursorShape(widget, box->_isSelectable ? UICursorShape_IBeam : UICursorShape_Arrow);
    return Widget_SetIsFocusable(widget, box->_isEnabled && box->_isSelectable);
}

Error UITextBox_SetIsEditable(UIScreen* screen, uint64_t widgetId, bool isEditable)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Box->_isEditable = isEditable;
    Box->_isSelectable = Box->_isSelectable || isEditable;
    Box->_isOverwriting = Box->_isOverwriting && isEditable;
    if (isEditable && (Box->_component != NULL))
    {
        // An editable box keeps no tree: its text without its sprites becomes the text, through the limits, as UITextBox_SetComponent
        // does for an editable box.
        Result = WriteFlowText(Box, Box->_component, false, NULL);
        ReleaseComponent(Box);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Result = UITextEdit_SetText(&Box->Edit, Box->_scratch._data, Box->_scratch._count, NULL);
        OnTextSetWhole(Box);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return RefreshFocusability(Target, Box);
}

Error UITextBox_SetIsSelectable(UIScreen* screen, uint64_t widgetId, bool isSelectable)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Box->_isSelectable = isSelectable || Box->_isEditable;
    return RefreshFocusability(Target, Box);
}

Error UITextBox_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Box->_isEnabled = isEnabled;
    Box->_isDragging = Box->_isDragging && isEnabled;
    return RefreshFocusability(Target, Box);
}

Error UITextBox_SetIsInvalid(UIScreen* screen, uint64_t widgetId, bool isInvalid)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code == ErrorCode_Success)
    {
        Box->_isInvalid = isInvalid;
    }
    return Result;
}

Error UITextBox_SetIsMasked(UIScreen* screen, uint64_t widgetId, bool isMasked)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if ((Result.Code == ErrorCode_Success) && (Box->_isMasked != isMasked))
    {
        Box->_isMasked = isMasked;
        Box->_contentRevision++;
        UITextBoxInternal_OnCaretMoved(Box);
    }
    return Result;
}

Error UITextBox_SetKeys(UIScreen* screen, uint64_t widgetId, bool acceptsTab, bool allowsOverwrite, bool escapeReverts)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code == ErrorCode_Success)
    {
        Box->_acceptsTab = acceptsTab;
        Box->_allowsOverwrite = allowsOverwrite;
        Box->_isOverwriting = Box->_isOverwriting && allowsOverwrite;
        Box->_escapeReverts = escapeReverts;
    }
    return Result;
}

Error UITextBox_SetIsWrapped(UIScreen* screen, uint64_t widgetId, bool isWrapped)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if ((Result.Code == ErrorCode_Success) && (Box->_isWrapped != isWrapped))
    {
        Box->_isWrapped = isWrapped;
        UITextBoxInternal_OnCaretMoved(Box);
    }
    return Result;
}

/* Whether every range's style is one a string component takes (a finite size, a spacing within its bounds, ...): each is applied to a
 * scratch component as the document's build applies it, so a style that would fail every build is refused when it is set. */
static Error CheckStyles(UITextBoxCapability* box, const UITextStyleRange* ranges, size_t count)
{
    StringComponent* Scratch = NULL;
    Error Result = TextComponentFactory_CreateString(box->_factory, (const unsigned char*)u8"", &Scratch);
    for (size_t Index = 0U; (Index < count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = TextStyle_ApplyToComponent(&ranges[Index].Style, StringComponent_AsComponent(Scratch));
    }
    if (Scratch != NULL)
    {
        Error Cleanup = TextComponentFactory_ReturnTree(box->_factory, StringComponent_AsComponent(Scratch));
        Error_Deconstruct(&Cleanup);
    }
    return Result;
}

// TODO: formatting the user chooses and that is stored is not built (a paper editor's colours, fonts and sizes on a selection, images
// inline, saved and reloaded as text components): style ranges come from code only, and the user types plain text.
Error UITextBox_SetStyleRanges(UIScreen* screen, uint64_t widgetId, const UITextStyleRange* ranges, size_t count)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((ranges == NULL) && (count > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextBox_SetStyleRanges: ranges must not be NULL with a count.");
    }
    if (count > UI_TEXT_BOX_MAX_STYLE_RANGES)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITextBox_SetStyleRanges: more ranges than UI_TEXT_BOX_MAX_STYLE_RANGES.");
    }
    // Checked whole first, so a refusal changes nothing.
    for (size_t Index = 0U; Index < count; Index++)
    {
        unsigned char FullName[NAMESPACED_KEY_BUFFER_SIZE];
        if (ranges[Index].End <= ranges[Index].Start)
        {
            return Error_Construct2(ErrorCode_IllegalArgument, "UITextBox_SetStyleRanges: a range's end must lie above its start.");
        }
        if (!TryCopyFontName(&ranges[Index].Style, FullName, NULL))
        {
            return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITextBox_SetStyleRanges: a font name is no valid asset name.");
        }
    }
    Result = CheckStyles(Box, ranges, count);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GenericBuffer_Clear(&Box->_styleRanges);
    if (!GenericBuffer_EnsureTotalCapacity(&Box->_styleRanges, count))
    {
        return GrowthError();
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        UITextBoxStyleRecord Record;
        Memory_Zero(&Record, sizeof(Record));
        Record.Start = ranges[Index].Start;
        Record.End = ranges[Index].End;
        Record.Style = ranges[Index].Style;
        TryCopyFontName(&ranges[Index].Style, Record.FontName, &Record.HasFontName);
        TextStyle_ClearFontName(&Record.Style);
        GenericBuffer_AddLast(&Box->_styleRanges, &Record);
    }
    ClampRanges(Box);
    Box->_contentRevision++;
    return Error_CreateSuccess();
}

Error UITextBox_GetStyleRange(UIScreen* screen, uint64_t widgetId, size_t index, UITextStyleRange* outRange, size_t* outCount)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outCount != NULL)
    {
        *outCount = Box->_styleRanges._count;
    }
    if (index < Box->_styleRanges._count)
    {
        const UITextBoxStyleRecord* Record = GetRecord(Box, index);
        outRange->Start = Record->Start;
        outRange->End = Record->End;
        outRange->Style = Record->Style;
        if (Record->HasFontName)
        {
            TextStyle_SetFontName(&outRange->Style, Record->FontName);
        }
    }
    return Error_CreateSuccess();
}

Error UITextBox_SetComponent(UIScreen* screen, uint64_t widgetId, const TextComponent* root, bool* outIsKeptWhole)
{
    if (outIsKeptWhole != NULL)
    {
        *outIsKeptWhole = false;
    }
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (root == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextBox_SetComponent: root must not be NULL.");
    }
    bool IsPlain = true;
    Result = WriteFlowText(Box, root, !Box->_isEditable, &IsPlain);
    TextComponent* Copy = NULL;
    if ((Result.Code == ErrorCode_Success) && !Box->_isEditable)
    {
        Result = TextComponentFactory_CloneTree(Box->_factory, root, &Copy);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // A read-only box's text is the tree's flow text as it is; an editable box's goes through its limits.
    ReleaseComponent(Box);
    GenericBuffer_Clear(&Box->_styleRanges);
    UITextEditChange Change = { 0 };
    if (Copy != NULL)
    {
        // Its offsets must be the layout's: the flow text as it is, no rule of what comes in applied.
        Result = UITextEdit_SetTextVerbatim(&Box->Edit, Box->_scratch._data, Box->_scratch._count);
        if (Result.Code == ErrorCode_Success)
        {
            Box->_component = Copy;
        }
        else
        {
            Error Cleanup = TextComponentFactory_ReturnTree(Box->_factory, Copy);
            Error_Deconstruct(&Cleanup);
            Copy = NULL;
        }
    }
    else
    {
        Result = UITextEdit_SetText(&Box->Edit, Box->_scratch._data, Box->_scratch._count, &Change);
    }
    Box->_contentRevision++;
    UITextBoxInternal_OnCaretMoved(Box);
    if (outIsKeptWhole != NULL)
    {
        *outIsKeptWhole = (Result.Code == ErrorCode_Success) && !Change.IsRefused && ((Copy != NULL) || IsPlain);
    }
    return Result;
}
