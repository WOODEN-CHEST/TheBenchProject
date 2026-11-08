#include "UITextBox.h"
#include "GameInput.h"
#include "UIContextMenu.h"
#include "UIKit.h"
#include "UIMenu.h"
#include "UIPopup.h"
#include "UIScreen.h"
#include "UITextBoxInternal.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** The longest shortcut text a context menu item shows, its NUL included. */
#define SHORTCUT_CAPACITY ((size_t)48)
/** How far inside the view's far edges a place out of it counts, in UI pixels: the last line or column that shows, not the next one. */
#define VIEW_EDGE_INSET 0.5f
/** The scratch a box keeps after a paste beyond twice its text, in bytes. */
#define SCRATCH_KEPT_BYTES ((size_t)4096)


// Types.
/* The items of a box's context menu. */
typedef enum TextBoxMenuItemEnum
{
    TextBoxMenuItem_Undo = 1,
    TextBoxMenuItem_Redo,
    TextBoxMenuItem_Cut,
    TextBoxMenuItem_Copy,
    TextBoxMenuItem_Paste,
    TextBoxMenuItem_Delete,
    TextBoxMenuItem_SelectAll
} TextBoxMenuItem;


// Static functions: small helpers.
static bool HasModifier(uint8_t modifiers, uint8_t flag)
{
    return (modifiers & flag) != 0U;
}

/* Whether a key's press types text (its codepoint comes as typed text): a letter, a digit, punctuation, space, the keypad's characters. */
static bool IsTextKey(GameKey key)
{
    if (((key >= GameKey_A) && (key <= GameKey_Z)) || ((key >= GameKey_0) && (key <= GameKey_9))
        || ((key >= GameKey_Keypad0) && (key <= GameKey_Keypad9)))
    {
        return true;
    }
    switch (key)
    {
        case GameKey_Space:
        case GameKey_Apostrophe:
        case GameKey_Comma:
        case GameKey_Minus:
        case GameKey_Period:
        case GameKey_Slash:
        case GameKey_Semicolon:
        case GameKey_Equal:
        case GameKey_LeftBracket:
        case GameKey_Backslash:
        case GameKey_RightBracket:
        case GameKey_Grave:
        case GameKey_KeypadDecimal:
        case GameKey_KeypadDivide:
        case GameKey_KeypadMultiply:
        case GameKey_KeypadSubtract:
        case GameKey_KeypadAdd:
        case GameKey_KeypadEqual:
            return true;
        default:
            return false; // Every other key types nothing: this is a test of a key, not a switch over every value.
    }
}

/* Whether the modifiers held leave a text key typing text: none of Ctrl, Alt and Super; Ctrl with Alt (what AltGr is on Windows, and how
 * it is typed without one); or the right Alt alone (AltGr as GLFW reports it, on X11 and on Windows). The left Alt alone is a shortcut. */
static bool AreModifiersTyping(const Widget* widget, uint8_t modifiers)
{
    bool IsControl = HasModifier(modifiers, GAME_KEY_MODIFIER_CONTROL);
    bool IsAlt = HasModifier(modifiers, GAME_KEY_MODIFIER_ALT);
    if (HasModifier(modifiers, GAME_KEY_MODIFIER_SUPER))
    {
        return false;
    }
    if (IsControl == IsAlt)
    {
        return true;
    }
    const GameInput* Input = UIScreen_GetInput(Widget_GetScreen(widget));
    bool IsRightAlt = (Input != NULL) && ((GameInput_GetRawKeyState(Input, GameKey_RightAlt) & GAME_INPUT_KEY_STATE_DOWN) != 0U);
    bool IsLeftAlt = (Input != NULL) && ((GameInput_GetRawKeyState(Input, GameKey_LeftAlt) & GAME_INPUT_KEY_STATE_DOWN) != 0U);
    return IsAlt && !IsControl && IsRightAlt && !IsLeftAlt;
}

/* Whether a press types text: a text key with modifiers that type. */
static bool IsTypingPress(const Widget* widget, const WidgetKeyboardInputArgs* args)
{
    return IsTextKey(args->Key) && AreModifiersTyping(widget, args->Modifiers);
}

/* Whether the box holds one line: no line break can go in. Enter then leaves, and a key entering it selects everything. */
static bool IsOneLine(const UITextBoxCapability* self)
{
    const UITextEditLimits* Limits = UITextEdit_GetLimits(&self->Edit);
    return !Limits->AllowsNewlines || (Limits->HasMaxLines && (Limits->MaxLines == 1U));
}

/* Whether the box shows one line: one that holds one line and does not wrap. Up, Down and the page keys are its owner's then. */
static bool ShowsOneLine(const UITextBoxCapability* self)
{
    return IsOneLine(self) && !self->_isWrapped;
}

/* The box's size in its own UI pixels, as layout put it. */
static Vector2 GetBoxSize(Widget* widget)
{
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    return (Vector2){ .x = Pixels.width / Scale, .y = Pixels.height / Scale };
}

/* One line's height in UI pixels, from the look; the theme's default without it. */
static float GetLineHeight(const UITextBoxCapability* self, const Widget* widget)
{
    float Height = (self->_geometry != NULL) ? self->_geometry->GetLineHeight(widget) : UI_THEME_DEFAULT_TEXT_SIZE;
    return (Height > 0.0f) ? Height : UI_THEME_DEFAULT_TEXT_SIZE;
}

/* The caret moved by the keyboard or the mouse: it is to be shown, the blink restarts, and the undo group has ended. */
static void SetCaret(UITextBoxCapability* self, size_t offset, bool isExtending)
{
    UITextEdit_SetCaret(&self->Edit, offset, isExtending);
    UITextBoxInternal_OnCaretMoved(self);
}

static void Select(UITextBoxCapability* self, size_t anchor, size_t caret)
{
    UITextEdit_Select(&self->Edit, anchor, caret);
    UITextBoxInternal_OnCaretMoved(self);
}

/* Whether an edit now is a whole change (Changed, then Committed): one made while the box is not being edited. */
static bool IsWholeChange(const Widget* widget)
{
    return !Widget_IsEntered(widget);
}


// Static functions: the pointer.
/* The byte offset under a place in the widget's box (normalized), through the look's layout; the text's end without one. A place out of
 * the view counts as the nearest place inside it, so a drag past an edge selects up to what shows there, and the edge's scrolling takes it
 * on from there. */
static size_t HitAt(Widget* widget, UITextBoxCapability* self, Vector2 widgetPosition)
{
    const TextLayout* Layout = NULL;
    float UnitsToUI = 0.0f;
    if (!UITextBoxInternal_GetLayout(self, widget, &Layout, &UnitsToUI))
    {
        return UITextEdit_GetLength(&self->Edit);
    }
    Vector2 BoxSize = GetBoxSize(widget);
    Rectangle View = self->_viewBox;
    float X = Math_ClampFloat(widgetPosition.x * BoxSize.x, View.x, Math_MaxFloat(View.x, (View.x + View.width) - VIEW_EDGE_INSET));
    float Y = Math_ClampFloat(widgetPosition.y * BoxSize.y, View.y, Math_MaxFloat(View.y, (View.y + View.height) - VIEW_EDGE_INSET));
    Vector2 Content = { .x = (X - View.x) + self->Scrollable._offset.x, .y = (Y - View.y) + self->Scrollable._offset.y };
    size_t Offset = 0U;
    TextLayout_HitTest(Layout, (Vector2){ .x = Content.x / UnitsToUI, .y = Content.y / UnitsToUI }, &Offset);
    return UITextBoxInternal_ToBoxOffset(self, Offset);
}

/* The unit a drag selects by around an offset: the word or the line it lies in. */
static void GetUnitAt(const UITextBoxCapability* self, size_t offset, size_t* outStart, size_t* outEnd)
{
    const unsigned char* Text = UITextEdit_GetText(&self->Edit);
    size_t Length = UITextEdit_GetLength(&self->Edit);
    if (self->_dragUnit == (uint32_t)UITextBoxDragUnit_Word)
    {
        UITextEdit_GetWordAt(Text, Length, offset, self->_isMasked, outStart, outEnd);
        return;
    }
    if (IsOneLine(self))
    {
        *outStart = 0U;
        *outEnd = Length;
        return;
    }
    UITextEdit_GetLineAt(Text, Length, offset, outStart, outEnd);
}

/* A drag reaching an offset: the caret goes there, or by whole words or lines from what the double or triple click took. */
static void ExtendDragTo(UITextBoxCapability* self, size_t offset)
{
    if (self->_dragUnit == (uint32_t)UITextBoxDragUnit_Character)
    {
        SetCaret(self, offset, true);
        return;
    }
    size_t Start = 0U;
    size_t End = 0U;
    GetUnitAt(self, offset, &Start, &End);
    if (offset < self->_dragStart)
    {
        Select(self, self->_dragEnd, Start);
    }
    else
    {
        Select(self, self->_dragStart, (End > self->_dragEnd) ? End : self->_dragEnd);
    }
}

static void BeginDrag(Widget* widget, UITextBoxCapability* self, const WidgetMouseInputArgs* args)
{
    size_t Offset = HitAt(widget, self, args->WidgetPosition);
    self->_hasColumn = false;
    self->_isDragging = true;
    self->_dragPointer = args->WidgetPosition;
    if (args->ClickCount <= 1U)
    {
        self->_dragUnit = (uint32_t)UITextBoxDragUnit_Character;
        SetCaret(self, Offset, HasModifier(args->Modifiers, GAME_KEY_MODIFIER_SHIFT));
        return;
    }
    self->_dragUnit = (args->ClickCount == 2U) ? (uint32_t)UITextBoxDragUnit_Word : (uint32_t)UITextBoxDragUnit_Line;
    GetUnitAt(self, Offset, &self->_dragStart, &self->_dragEnd);
    Select(self, self->_dragStart, self->_dragEnd);
}


// Static functions: the clipboard.
/* Puts a range of the text on the clipboard; a tree's object characters (its sprites) are left out. */
static Error CopyRange(Widget* widget, UITextBoxCapability* self, size_t start, size_t end)
{
    const GameInput* Input = UIScreen_GetInput(Widget_GetScreen(widget));
    if ((Input == NULL) || (end <= start))
    {
        return Error_CreateSuccess();
    }
    GenericBuffer_Clear(&self->_scratch); // Its own scratch.
    const unsigned char* Text = UITextEdit_GetText(&self->Edit);
    for (size_t Offset = start; Offset < end;)
    {
        bool IsObject = ((Offset + 2U) < end) && (Text[Offset] == 0xEFU) && (Text[Offset + 1U] == 0xBFU) && (Text[Offset + 2U] == 0xBCU);
        if (IsObject && (self->_component != NULL))
        {
            Offset += 3U;
            continue;
        }
        if (!GenericBuffer_AppendByte(&self->_scratch, Text[Offset]))
        {
            return Error_Construct2(ErrorCode_BufferTooLarge, "UITextBox: the copy could not grow.");
        }
        Offset++;
    }
    if (!GenericBuffer_AppendByte(&self->_scratch, 0U))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UITextBox: the copy could not grow.");
    }
    return GameInput_SetClipboardText(Input, self->_scratch._data);
}

/* Copies the selection, or the whole text: refused (the cue) from a masked box. */
static Error Copy(Widget* widget, UITextBoxCapability* self, bool isWhole)
{
    if (self->_isMasked)
    {
        return UIElement_PostCue(widget, UIElementCue_Refused);
    }
    size_t Start = 0U;
    size_t End = UITextEdit_GetLength(&self->Edit);
    if (!isWhole)
    {
        UITextEdit_GetSelection(&self->Edit, &Start, &End);
    }
    return CopyRange(widget, self, Start, End);
}

/* What a paste leaves the scratch holding beyond what the text needs is let go: the clipboard can be far larger than the box takes. */
static void TrimScratch(UITextBoxCapability* self)
{
    size_t Needed = UITextEdit_GetLength(&self->Edit) + 1U;
    if (self->_scratch._capacity > ((2U * Needed) + SCRATCH_KEPT_BYTES))
    {
        Memory_Free(self->_scratch._data);
        GenericBuffer_AllocateVariable(&self->_scratch, 0U, sizeof(unsigned char));
    }
}

/* Pastes the clipboard over the selection, or over the whole text; a step of its own. */
static Error Paste(Widget* widget, UITextBoxCapability* self, bool isWhole)
{
    const GameInput* Input = UIScreen_GetInput(Widget_GetScreen(widget));
    if (!self->_isEditable)
    {
        return UIElement_PostCue(widget, UIElementCue_Refused);
    }
    if (Input == NULL)
    {
        return Error_CreateSuccess();
    }
    GenericBuffer_Clear(&self->_scratch); // Its own scratch.
    Error Result = GameInput_GetClipboardText(Input, UITextEdit_GetMostIncomingBytes(&self->Edit), &self->_scratch);
    if ((Result.Code != ErrorCode_Success) || (self->_scratch._count == 0U))
    {
        return Result;
    }
    size_t Start = 0U;
    size_t End = UITextEdit_GetLength(&self->Edit);
    if (!isWhole)
    {
        UITextEdit_GetSelection(&self->Edit, &Start, &End);
    }
    UITextEditChange Change;
    Result = UITextEdit_Replace(&self->Edit, Start, End, self->_scratch._data, self->_scratch._count, UITextEditKind_Whole, self->_time, &Change);
    TrimScratch(self);
    return (Result.Code == ErrorCode_Success) ? UITextBoxInternal_PostEdit(widget, self, &Change, IsWholeChange(widget)) : Result;
}

/* Copies the selection and deletes it, a step of its own; refused from a masked or a read-only box. */
static Error Cut(Widget* widget, UITextBoxCapability* self)
{
    if (self->_isMasked || !self->_isEditable)
    {
        return UIElement_PostCue(widget, UIElementCue_Refused);
    }
    if (!UITextEdit_HasSelection(&self->Edit))
    {
        return Error_CreateSuccess();
    }
    Error Result = Copy(widget, self, false);
    UITextEditChange Change;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITextEdit_ReplaceSelection(&self->Edit, NULL, 0U, UITextEditKind_Whole, self->_time, &Change);
    }
    return (Result.Code == ErrorCode_Success) ? UITextBoxInternal_PostEdit(widget, self, &Change, IsWholeChange(widget)) : Result;
}

static Error DeleteSelection(Widget* widget, UITextBoxCapability* self)
{
    if (!self->_isEditable || !UITextEdit_HasSelection(&self->Edit))
    {
        return Error_CreateSuccess();
    }
    UITextEditChange Change;
    Error Result = UITextEdit_ReplaceSelection(&self->Edit, NULL, 0U, UITextEditKind_Whole, self->_time, &Change);
    return (Result.Code == ErrorCode_Success) ? UITextBoxInternal_PostEdit(widget, self, &Change, IsWholeChange(widget)) : Result;
}

/* Undo or redo: the box's own, with the Refused cue when nothing is left. */
static Error UndoOrRedo(Widget* widget, UITextBoxCapability* self, bool isRedo)
{
    if (!self->_isEditable)
    {
        return UIElement_PostCue(widget, UIElementCue_Refused);
    }
    UITextEditChange Change;
    Error Result = isRedo ? UITextEdit_Redo(&self->Edit, &Change) : UITextEdit_Undo(&self->Edit, &Change);
    self->_hasColumn = false;
    return (Result.Code == ErrorCode_Success) ? UITextBoxInternal_PostEdit(widget, self, &Change, IsWholeChange(widget)) : Result;
}

static void SelectAll(UITextBoxCapability* self)
{
    UITextEdit_SelectAll(&self->Edit);
    self->_hasColumn = false;
    UITextBoxInternal_OnCaretMoved(self);
}

/* Back to the default: a whole change and a step of its own. */
static Error ResetToDefault(Widget* widget, UITextBoxCapability* self)
{
    const unsigned char* Default = UITextContent_Get(&self->_default);
    UITextEditChange Change;
    Error Result = UITextEdit_Replace(&self->Edit, 0U, UITextEdit_GetLength(&self->Edit), Default, StringUTF8_GetByteLength(Default),
        UITextEditKind_Whole, self->_time, &Change);
    return (Result.Code == ErrorCode_Success) ? UITextBoxInternal_PostEdit(widget, self, &Change, IsWholeChange(widget)) : Result;
}


// Static functions: the keyboard.
/* Up and Down by lines, keeping the column across short lines; past the first line to the text's start, past the last to its end. */
static void MoveByLines(Widget* widget, UITextBoxCapability* self, int32_t lines, bool isExtending)
{
    const TextLayout* Layout = NULL;
    float UnitsToUI = 0.0f;
    if (!UITextBoxInternal_GetLayout(self, widget, &Layout, &UnitsToUI) || (TextLayout_GetLineCount(Layout) == 0U))
    {
        return;
    }
    size_t Caret = UITextBoxCapability_GetLayoutCaret(self);
    if (!self->_hasColumn)
    {
        Rectangle Box = { 0 };
        TextLayout_GetCaretBox(Layout, Caret, &Box);
        self->_column = Box.x * UnitsToUI;
        self->_hasColumn = true;
    }
    size_t LineCount = TextLayout_GetLineCount(Layout);
    size_t Line = TextLayout_GetLineAtOffset(Layout, Caret);
    size_t Target = 0U;
    if ((lines < 0) && (Line == 0U))
    {
        UITextEdit_SetCaret(&self->Edit, 0U, isExtending);
        UITextBoxInternal_OnCaretMoved(self);
        return;
    }
    if ((lines > 0) && ((Line + 1U) >= LineCount))
    {
        UITextEdit_SetCaret(&self->Edit, UITextEdit_GetLength(&self->Edit), isExtending);
        UITextBoxInternal_OnCaretMoved(self);
        return;
    }
    size_t Distance = (size_t)((lines < 0) ? -lines : lines);
    Target = (lines < 0) ? ((Line > Distance) ? (Line - Distance) : 0U) : (((Line + Distance) < LineCount) ? (Line + Distance) : (LineCount - 1U));
    size_t Offset = TextLayout_GetOffsetOnLine(Layout, Target, self->_column / UnitsToUI);
    // The column stays for the next Up or Down.
    UITextEdit_SetCaret(&self->Edit, UITextBoxInternal_ToBoxOffset(self, Offset), isExtending);
    UITextBoxInternal_OnCaretMoved(self);
}

/* Home and End: the line as shown (a wrapped line's piece), through the look's layout; without it the line between its breaks. */
static void MoveToLineEdge(Widget* widget, UITextBoxCapability* self, bool isEnd, bool isExtending)
{
    const TextLayout* Layout = NULL;
    float UnitsToUI = 0.0f;
    size_t Start = 0U;
    size_t End = UITextEdit_GetLength(&self->Edit);
    if (UITextBoxInternal_GetLayout(self, widget, &Layout, &UnitsToUI) && (TextLayout_GetLineCount(Layout) > 0U))
    {
        size_t Line = TextLayout_GetLineAtOffset(Layout, UITextBoxCapability_GetLayoutCaret(self));
        size_t LayoutStart = 0U;
        size_t LayoutEnd = 0U;
        TextLayout_GetLineOffsets(Layout, Line, &LayoutStart, &LayoutEnd);
        Start = UITextBoxInternal_ToBoxOffset(self, LayoutStart);
        End = UITextBoxInternal_ToBoxOffset(self, LayoutEnd);
    }
    else
    {
        UITextEdit_GetLineAt(UITextEdit_GetText(&self->Edit), UITextEdit_GetLength(&self->Edit), UITextEdit_GetCaret(&self->Edit), &Start, &End);
    }
    self->_hasColumn = false;
    SetCaret(self, isEnd ? End : Start, isExtending);
}

/* How many lines a page is: the view's height in lines, at least one. */
static int32_t GetPageLines(const UITextBoxCapability* self, const Widget* widget)
{
    float Lines = Math_FloorFloat(self->_viewBox.height / GetLineHeight(self, widget));
    return (Lines >= 1.0f) ? (int32_t)Math_MinFloat(Lines, 100000.0f) : 1;
}

/* A read-only box's keys scroll it: the arrows a line, the page keys a page, Ctrl with Home and End to the ends. */
static bool ScrollByKey(Widget* widget, UITextBoxCapability* self, GameKey key, uint8_t modifiers)
{
    float Line = GetLineHeight(self, widget);
    float Page = Math_MaxFloat(Line, self->_viewBox.height);
    Vector2 Range = UIScrollableCapability_GetRange(&self->Scrollable);
    bool IsControl = HasModifier(modifiers, GAME_KEY_MODIFIER_CONTROL);
    Vector2 Delta = { .x = 0.0f, .y = 0.0f };
    switch (key)
    {
        case GameKey_Up:
            Delta.y = -Line;
            break;
        case GameKey_Down:
            Delta.y = Line;
            break;
        case GameKey_Left:
            Delta.x = -Line;
            break;
        case GameKey_Right:
            Delta.x = Line;
            break;
        case GameKey_PageUp:
            Delta.y = -Page;
            break;
        case GameKey_PageDown:
            Delta.y = Page;
            break;
        case GameKey_Home:
            Delta = IsControl ? (Vector2){ .x = -Range.x, .y = -Range.y } : (Vector2){ .x = -Range.x, .y = 0.0f };
            break;
        case GameKey_End:
            Delta = IsControl ? Range : (Vector2){ .x = Range.x, .y = 0.0f };
            break;
        default:
            return false; // Only these keys scroll.
    }
    UIScrollableCapability_ScrollBy(&self->Scrollable, Delta);
    return true;
}

/* Types a codepoint, in overwrite mode or not; an edit of a visit (or a whole change when not entered). */
static Error Type(Widget* widget, UITextBoxCapability* self, uint32_t codepoint)
{
    if (!self->_isEditable)
    {
        return UIElement_PostCue(widget, UIElementCue_Refused);
    }
    UITextEditChange Change;
    Error Result = UITextEdit_TypeCodepoint(&self->Edit, codepoint, self->_isOverwriting, self->_time, &Change);
    self->_hasColumn = false;
    return (Result.Code == ErrorCode_Success) ? UITextBoxInternal_PostEdit(widget, self, &Change, IsWholeChange(widget)) : Result;
}

static Error Delete(Widget* widget, UITextBoxCapability* self, bool isForward, bool isByWord)
{
    if (!self->_isEditable)
    {
        return UIElement_PostCue(widget, UIElementCue_Refused);
    }
    UITextEditChange Change;
    Error Result = isForward ? UITextEdit_DeleteForward(&self->Edit, isByWord, self->_isMasked, self->_time, &Change)
        : UITextEdit_DeleteBackward(&self->Edit, isByWord, self->_isMasked, self->_time, &Change);
    self->_hasColumn = false;
    return (Result.Code == ErrorCode_Success) ? UITextBoxInternal_PostEdit(widget, self, &Change, IsWholeChange(widget)) : Result;
}

/* The actions a box takes while it is entered: its undo, redo, clipboard and select-all (rebindable). True when one was the key's. */
static Error TryAction(Widget* widget, UITextBoxCapability* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    const UIScreen* Screen = Widget_GetScreen(widget);
    bool IsRepeat = args->Type == UIKeyboardInputType_KeyRepeat;
    GameKey Key = args->Key;
    Error Result = Error_CreateSuccess();
    *outIsHandled = true;
    if (UIElement_IsActionPress(Screen, GameAction_UIRedo, Key))
    {
        Result = UndoOrRedo(widget, self, true);
    }
    else if (UIElement_IsActionPress(Screen, GameAction_UIUndo, Key))
    {
        Result = UndoOrRedo(widget, self, false);
    }
    else if (UIElement_IsActionPress(Screen, GameAction_UICut, Key))
    {
        Result = IsRepeat ? Error_CreateSuccess() : Cut(widget, self);
    }
    else if (UIElement_IsActionPress(Screen, GameAction_UICopy, Key))
    {
        Result = IsRepeat ? Error_CreateSuccess() : Copy(widget, self, false);
    }
    else if (UIElement_IsActionPress(Screen, GameAction_UIPaste, Key))
    {
        Result = Paste(widget, self, false);
    }
    else if (UIElement_IsActionPress(Screen, GameAction_UISelectAll, Key))
    {
        if (!IsRepeat)
        {
            SelectAll(self);
        }
    }
    else
    {
        *outIsHandled = false;
    }
    return Result;
}

/* The keys that move the caret or edit, by their raw keys. *outIsHandled stays false for the keys the box leaves to its owner. */
static Error TryEditingKey(Widget* widget, UITextBoxCapability* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    uint8_t Modifiers = args->Modifiers;
    bool IsControl = HasModifier(Modifiers, GAME_KEY_MODIFIER_CONTROL);
    bool IsShift = HasModifier(Modifiers, GAME_KEY_MODIFIER_SHIFT);
    bool IsAlt = HasModifier(Modifiers, GAME_KEY_MODIFIER_ALT);
    bool IsRepeat = args->Type == UIKeyboardInputType_KeyRepeat;
    bool IsOneLineBox = ShowsOneLine(self);
    *outIsHandled = true;
    switch (args->Key)
    {
        case GameKey_Left:
            UITextEdit_MoveLeft(&self->Edit, IsControl, IsShift, self->_isMasked);
            self->_hasColumn = false;
            UITextBoxInternal_OnCaretMoved(self);
            return Error_CreateSuccess();
        case GameKey_Right:
            UITextEdit_MoveRight(&self->Edit, IsControl, IsShift, self->_isMasked);
            self->_hasColumn = false;
            UITextBoxInternal_OnCaretMoved(self);
            return Error_CreateSuccess();
        case GameKey_Up:
        case GameKey_Down:
            *outIsHandled = !IsOneLineBox;
            if (!IsOneLineBox)
            {
                MoveByLines(widget, self, (args->Key == GameKey_Up) ? -1 : 1, IsShift);
            }
            return Error_CreateSuccess();
        case GameKey_PageUp:
        case GameKey_PageDown:
        {
            *outIsHandled = !IsOneLineBox;
            if (IsOneLineBox)
            {
                return Error_CreateSuccess();
            }
            int32_t Lines = GetPageLines(self, widget);
            float Distance = (float)Lines * GetLineHeight(self, widget);
            UIScrollableCapability_ScrollBy(&self->Scrollable, (Vector2){ .x = 0.0f, .y = (args->Key == GameKey_PageUp) ? -Distance : Distance });
            MoveByLines(widget, self, (args->Key == GameKey_PageUp) ? -Lines : Lines, IsShift);
            return Error_CreateSuccess();
        }
        case GameKey_Home:
        case GameKey_End:
            if (IsControl)
            {
                self->_hasColumn = false;
                SetCaret(self, (args->Key == GameKey_End) ? UITextEdit_GetLength(&self->Edit) : 0U, IsShift);
                return Error_CreateSuccess();
            }
            MoveToLineEdge(widget, self, args->Key == GameKey_End, IsShift);
            return Error_CreateSuccess();
        case GameKey_Backspace:
            return Delete(widget, self, false, IsControl);
        case GameKey_Delete:
            return Delete(widget, self, true, IsControl);
        case GameKey_Insert:
            if (!IsRepeat && (Modifiers == GAME_KEY_MODIFIER_NONE) && self->_isEditable)
            {
                self->_isOverwriting = self->_allowsOverwrite && !self->_isOverwriting;
                UITextBoxInternal_OnCaretMoved(self);
            }
            return Error_CreateSuccess();
        case GameKey_Enter:
        case GameKey_KeypadEnter:
            // A new line on a box that holds several (refused at the line limit); Ctrl+Enter, and Enter where no line break can go, is left:
            // the screen keeps and leaves on it.
            *outIsHandled = !IsOneLine(self) && !IsControl && !IsAlt;
            return *outIsHandled ? Type(widget, self, (uint32_t)'\n') : Error_CreateSuccess();
        case GameKey_Tab:
            *outIsHandled = self->_acceptsTab && !IsShift && !IsControl && !IsAlt;
            return *outIsHandled ? Type(widget, self, (uint32_t)'\t') : Error_CreateSuccess();
        default:
            break; // Every other key is left to the box's owner (the presses that type text were claimed before).
    }
    *outIsHandled = false;
    return Error_CreateSuccess();
}

/* What an entered box does with a key: the presses that type text first (so a letter bound to an action types the letter, and AltGr's
 * characters type rather than select all or paste; the text comes as typed text), then its actions, then its editing keys (a read-only
 * box's scroll it). */
static Error OnEnteredKey(Widget* widget, UITextBoxCapability* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    if (IsTypingPress(widget, args))
    {
        *outIsHandled = true;
        return Error_CreateSuccess();
    }
    Error Result = TryAction(widget, self, args, outIsHandled);
    if ((Result.Code != ErrorCode_Success) || *outIsHandled)
    {
        return Result;
    }
    if (!self->_isEditable)
    {
        *outIsHandled = ScrollByKey(widget, self, args->Key, args->Modifiers);
        return Error_CreateSuccess();
    }
    return TryEditingKey(widget, self, args, outIsHandled);
}

/* What a box that is not entered does with the short list (focused, or under the pointer): copy and paste the whole text, reset. */
static Error OnShortListKey(Widget* widget, UITextBoxCapability* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    const UIScreen* Screen = Widget_GetScreen(widget);
    if (args->Type != UIKeyboardInputType_KeyPress)
    {
        return Error_CreateSuccess();
    }
    if (UIElement_IsActionPress(Screen, GameAction_UIReset, args->Key))
    {
        *outIsHandled = self->_hasDefault && self->_isEditable;
        return *outIsHandled ? ResetToDefault(widget, self) : Error_CreateSuccess();
    }
    if (UIElement_IsActionPress(Screen, GameAction_UICopy, args->Key))
    {
        *outIsHandled = self->_isSelectable;
        return *outIsHandled ? Copy(widget, self, true) : Error_CreateSuccess();
    }
    if (UIElement_IsActionPress(Screen, GameAction_UIPaste, args->Key))
    {
        *outIsHandled = self->_isEditable;
        return *outIsHandled ? Paste(widget, self, true) : Error_CreateSuccess();
    }
    return args->IsHoverDelivery ? Error_CreateSuccess() : UIElement_OnContextKey(widget, args, outIsHandled);
}


// Static functions: the context menu.
/* "Ctrl+Z": an action's first binding as a shortcut text. */
static void WriteShortcut(const UIScreen* screen, GameActionId action, unsigned char* outText)
{
    outText[0] = 0U;
    const GameInput* Input = UIScreen_GetInput(screen);
    GameInputBinding Binding;
    if ((Input == NULL) || !GameInput_TryGetBinding(Input, action, 0U, &Binding))
    {
        return;
    }
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, outText, SHORTCUT_CAPACITY, sizeof(unsigned char), 0U);
    static const struct { uint8_t Flag; const char* Name; } Modifiers[] =
    {
        { GAME_KEY_MODIFIER_CONTROL, "Ctrl+" }, { GAME_KEY_MODIFIER_ALT, "Alt+" }, { GAME_KEY_MODIFIER_SHIFT, "Shift+" }, { GAME_KEY_MODIFIER_SUPER, "Super+" }
    };
    for (size_t Index = 0U; Index < (sizeof(Modifiers) / sizeof(Modifiers[0])); Index++)
    {
        if (HasModifier(Binding.Modifiers, Modifiers[Index].Flag))
        {
            GenericBuffer_AppendString(&Text, (const unsigned char*)Modifiers[Index].Name);
        }
    }
    const unsigned char* Name = GameKey_GetDisplayName(Binding.Key);
    if (Name != NULL)
    {
        GenericBuffer_AppendString(&Text, Name);
    }
    if (!GenericBuffer_NullTerminate(&Text))
    {
        outText[0] = 0U;
    }
}

static uint64_t GetBoxId(const UserData* userData)
{
    uint64_t Id = WIDGET_ID_INVALID;
    Memory_Copy(userData->Bytes, &Id, sizeof(Id));
    return Id;
}

static Error OnMenuItem(UIScreen* screen, const UIMenuActivation* activation, const UserData* userData)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, GetBoxId(userData), &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result); // The box went away while its menu was open: nothing to do.
        return Error_CreateSuccess();
    }
    switch ((TextBoxMenuItem)activation->ItemId)
    {
        case TextBoxMenuItem_Undo:
            return UndoOrRedo(Target, Box, false);
        case TextBoxMenuItem_Redo:
            return UndoOrRedo(Target, Box, true);
        case TextBoxMenuItem_Cut:
            return Cut(Target, Box);
        case TextBoxMenuItem_Copy:
            return Copy(Target, Box, false);
        case TextBoxMenuItem_Paste:
            return Paste(Target, Box, false);
        case TextBoxMenuItem_Delete:
            return DeleteSelection(Target, Box);
        case TextBoxMenuItem_SelectAll:
            SelectAll(Box);
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

static UIMenuItem MakeItem(TextBoxMenuItem id, const char* label, bool isEnabled, const unsigned char* shortcut)
{
    UIMenuItem Item = UIMenuItem_Create((uint32_t)id, (const unsigned char*)label);
    Item.IsEnabled = isEnabled;
    Item.Shortcut = ((shortcut != NULL) && (shortcut[0] != 0U)) ? shortcut : NULL;
    return Item;
}

static Error ProvideMenu(UIScreen* screen, const UIContextMenuRequest* request, const UserData* userData)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, GetBoxId(userData), &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    bool HasSelection = UITextEdit_HasSelection(&Box->Edit);
    bool IsEditable = Box->_isEditable && Box->_isEnabled;
    unsigned char Shortcuts[6][SHORTCUT_CAPACITY];
    WriteShortcut(screen, GameAction_UIUndo, Shortcuts[0]);
    WriteShortcut(screen, GameAction_UIRedo, Shortcuts[1]);
    WriteShortcut(screen, GameAction_UICut, Shortcuts[2]);
    WriteShortcut(screen, GameAction_UICopy, Shortcuts[3]);
    WriteShortcut(screen, GameAction_UIPaste, Shortcuts[4]);
    WriteShortcut(screen, GameAction_UISelectAll, Shortcuts[5]);
    UIMenuItem Items[] =
    {
        MakeItem(TextBoxMenuItem_Undo, "Undo", IsEditable && UITextEdit_CanUndo(&Box->Edit), Shortcuts[0]),
        MakeItem(TextBoxMenuItem_Redo, "Redo", IsEditable && UITextEdit_CanRedo(&Box->Edit), Shortcuts[1]),
        UIMenuItem_Separator(),
        MakeItem(TextBoxMenuItem_Cut, "Cut", IsEditable && !Box->_isMasked && HasSelection, Shortcuts[2]),
        MakeItem(TextBoxMenuItem_Copy, "Copy", Box->_isSelectable && !Box->_isMasked && HasSelection, Shortcuts[3]),
        MakeItem(TextBoxMenuItem_Paste, "Paste", IsEditable, Shortcuts[4]),
        MakeItem(TextBoxMenuItem_Delete, "Delete", IsEditable && HasSelection, NULL),
        UIMenuItem_Separator(),
        MakeItem(TextBoxMenuItem_SelectAll, "Select All", Box->_isSelectable && (UITextEdit_GetLength(&Box->Edit) > 0U), Shortcuts[5]),
    };
    // Opened as UIMenu_OpenAt opens a context menu, keeping its id. It takes the focus as it opens, so the box is left: a visit goes on
    // through its own menu (UITextBoxInternal_OnLeave), which is what _isOpeningMenu tells the leave.
    UIMenuOptions Options = UIMenuOptions_Create();
    Options.AnchorBox = (Rectangle){ .x = request->ScreenPosition.x, .y = request->ScreenPosition.y, .width = 0.0f, .height = 0.0f };
    Options.OwnerId = request->TargetWidgetId;
    Options.FocusesFirstItem = request->IsFromKey;
    Options.Handler = OnMenuItem;
    Options.Data = *userData;
    uint64_t BoxId = GetBoxId(userData);
    uint64_t MenuId = WIDGET_ID_INVALID;
    Box->_isOpeningMenu = true;
    Result = UIMenu_Open(screen, Items, sizeof(Items) / sizeof(Items[0]), &Options, &MenuId);
    // The open ran the box's leave hook: the box is found again by its id.
    Error Found = UITextBoxInternal_Resolve(screen, BoxId, &Target, &Box);
    if (Found.Code == ErrorCode_Success)
    {
        Box->_isOpeningMenu = false;
        Box->_menuId = Box->_isVisitSuspended ? MenuId : WIDGET_ID_INVALID;
    }
    Error_Deconstruct(&Found);
    return Result;
}


// Internal functions (declared in UITextBoxInternal.h).
Error UITextBoxInternal_OnMouseInput(void* widget, const WidgetMouseInputArgs* args)
{
    UITextBoxCapability* Self = UITextBoxInternal_GetCapability(widget);
    Error Result = UIElement_PostMouseCues(widget, args, Self->_isEnabled);
    if ((Result.Code != ErrorCode_Success) || !Self->_isEnabled)
    {
        return Result;
    }
    Result = UIElement_PostMouseEvents(widget, args);
    if ((Result.Code != ErrorCode_Success) || !Self->_isSelectable)
    {
        return Result;
    }
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
            if (args->Button == GameKey_MouseLeft)
            {
                BeginDrag(widget, Self, args);
            }
            else if (args->Button == GameKey_MouseRight)
            {
                // A right press outside the selection moves the caret there first, so the menu acts where it was opened.
                size_t Offset = HitAt(widget, Self, args->WidgetPosition);
                size_t Start = 0U;
                size_t End = 0U;
                UITextEdit_GetSelection(&Self->Edit, &Start, &End);
                if (!UITextEdit_HasSelection(&Self->Edit) || (Offset < Start) || (Offset > End))
                {
                    Self->_hasColumn = false;
                    SetCaret(Self, Offset, false);
                }
            }
            return Error_CreateSuccess();
        case UIMouseInputType_Move:
            if ((args->Button == GameKey_MouseLeft) && Self->_isDragging)
            {
                Self->_dragPointer = args->WidgetPosition;
                ExtendDragTo(Self, HitAt(widget, Self, args->WidgetPosition));
            }
            return Error_CreateSuccess();
        case UIMouseInputType_ButtonRelease:
            if (args->Button == GameKey_MouseLeft)
            {
                Self->_isDragging = false;
            }
            return Error_CreateSuccess();
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

Error UITextBoxInternal_OnPressLost(void* widget, GameKey button)
{
    UITextBoxCapability* Self = UITextBoxInternal_GetCapability(widget);
    if (button == GameKey_MouseLeft)
    {
        Self->_isDragging = false;
    }
    return Error_CreateSuccess();
}

/* The wheel scrolls it while it can still scroll that way, and goes on up the tree at the end. Shift turns a plain wheel across. */
Error UITextBoxInternal_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    UITextBoxCapability* Self = UITextBoxInternal_GetCapability(widget);
    bool IsSideways = HasModifier(args->Modifiers, GAME_KEY_MODIFIER_SHIFT) || ((args->ScrollDelta.x != 0.0f) && (args->ScrollDelta.y == 0.0f));
    float Notches = IsSideways ? ((args->ScrollDelta.y != 0.0f) ? args->ScrollDelta.y : args->ScrollDelta.x) : args->ScrollDelta.y;
    UIScrollAxis Axis = IsSideways ? UIScrollAxis_Horizontal : UIScrollAxis_Vertical;
    float Distance = -Notches * Self->Scrollable._wheelStep;
    if (!UIScrollableCapability_CanScroll(&Self->Scrollable, Axis, Distance))
    {
        return Error_CreateSuccess();
    }
    Vector2 Delta = (Axis == UIScrollAxis_Horizontal) ? (Vector2){ .x = Distance, .y = 0.0f } : (Vector2){ .x = 0.0f, .y = Distance };
    UIScrollableCapability_ScrollBy(&Self->Scrollable, Delta);
    *outIsHandled = true;
    return Error_CreateSuccess();
}

Error UITextBoxInternal_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    UITextBoxCapability* Self = UITextBoxInternal_GetCapability(widget);
    if (!Self->_isEnabled)
    {
        return Error_CreateSuccess();
    }
    if (args->Type == UIKeyboardInputType_Text)
    {
        // The screen gives typed text only to an entered box (and a box not entered only the short list's presses); a read-only box takes
        // it and types nothing.
        *outIsHandled = true;
        return Self->_isEditable ? Type(widget, Self, args->Codepoint) : Error_CreateSuccess();
    }
    if (args->Type == UIKeyboardInputType_KeyRelease)
    {
        return Error_CreateSuccess();
    }
    if (!Widget_IsEntered(widget) || args->IsHoverDelivery)
    {
        return OnShortListKey(widget, Self, args, outIsHandled);
    }
    return OnEnteredKey(widget, Self, args, outIsHandled);
}

Error UITextBoxInternal_OnEnter(void* widget, UIEnterCause cause)
{
    UITextBoxCapability* Self = UITextBoxInternal_GetCapability(widget);
    if (Self->_isVisitSuspended)
    {
        // Back from its own menu: the same visit goes on (its text at entering, its mark, its undo list, overwrite mode).
        Self->_isVisitSuspended = false;
        Self->_menuId = WIDGET_ID_INVALID;
        UITextBoxInternal_OnCaretMoved(Self);
        return Error_CreateSuccess();
    }
    GenericBuffer_Clear(&Self->_enteredText);
    // AppendRangeBytes only reads its source; the API is just not declared const.
    if (!GenericBuffer_AppendRangeBytes(&Self->_enteredText, (unsigned char*)UITextEdit_GetText(&Self->Edit), UITextEdit_GetLength(&Self->Edit)))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UITextBox: the text at entering could not be kept.");
    }
    if (Self->_undoScope == UITextBoxUndoScope_Visit)
    {
        UITextEdit_ClearUndo(&Self->Edit);
    }
    UITextEdit_SetMark(&Self->Edit);
    Self->_isVisiting = true;
    Self->_isVisitSuspended = false;
    Self->_menuId = WIDGET_ID_INVALID;
    Self->_isOverwriting = false;
    Self->_hasColumn = false;
    if ((cause == UIEnterCause_Key) && Self->_isEditable)
    {
        if (IsOneLine(Self))
        {
            UITextEdit_SelectAll(&Self->Edit);
        }
        else
        {
            UITextEdit_SetCaret(&Self->Edit, UITextEdit_GetLength(&Self->Edit), false);
        }
    }
    UITextBoxInternal_OnCaretMoved(Self);
    return Error_CreateSuccess();
}

/* A visit ends: Committed when the text changed, or with a cancel (Escape) the text at entering put back, Cancelled when it had changed. */
static Error EndVisit(Widget* widget, UITextBoxCapability* self, UILeaveReason reason)
{
    if (!self->_isVisiting)
    {
        return Error_CreateSuccess();
    }
    self->_isVisiting = false;
    self->_isVisitSuspended = false;
    self->_isOverwriting = false;
    self->_menuId = WIDGET_ID_INVALID;
    UIScreen* Screen = Widget_GetScreen(widget);
    bool IsChanged = UITextBoxInternal_HasTextChangedSinceEntering(self);
    if ((reason == UILeaveReason_Cancel) && self->_escapeReverts && self->_isEditable)
    {
        // Put back even when the text is as it was: the undo list goes back to where the visit found it.
        bool HasChanged = false;
        Error Result = UITextBoxInternal_RevertToEntered(self, &HasChanged);
        if ((Result.Code != ErrorCode_Success) || !IsChanged)
        {
            return Result;
        }
        UIPropertyValue Length = UIPropertyValue_FromDouble((double)UITextEdit_GetLength(&self->Edit));
        return UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Cancelled, &Length);
    }
    if (!IsChanged)
    {
        return Error_CreateSuccess();
    }
    UIPropertyValue Length = UIPropertyValue_FromDouble((double)UITextEdit_GetLength(&self->Edit));
    return UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Committed, &Length);
}

Error UITextBoxInternal_OnLeave(void* widget, UILeaveReason reason)
{
    UITextBoxCapability* Self = UITextBoxInternal_GetCapability(widget);
    Self->_isDragging = false;
    UITextEdit_CloseUndoGroup(&Self->Edit);
    UITextBoxInternal_OnCaretMoved(Self);
    if (Self->_isVisiting && Self->_isOpeningMenu)
    {
        // Its own context menu takes the focus: the visit goes on through it, and ends only if the focus does not come back
        // (UITextBoxInternal_UpdateVisit).
        Self->_isVisitSuspended = true;
        return Error_CreateSuccess();
    }
    return EndVisit(widget, Self, reason);
}

Error UITextBoxInternal_UpdateVisit(void* widget)
{
    UITextBoxCapability* Self = UITextBoxInternal_GetCapability(widget);
    if (!Self->_isVisitSuspended || Widget_IsEntered(widget))
    {
        return Error_CreateSuccess();
    }
    if ((Self->_menuId != WIDGET_ID_INVALID) && UIScreen_IsPopupOpen(Widget_GetScreen(widget), Self->_menuId))
    {
        return Error_CreateSuccess();
    }
    // The menu closed and the focus went elsewhere: the visit ends here, kept.
    return EndVisit(widget, Self, UILeaveReason_Keep);
}

Error UITextBoxInternal_UpdateDrag(void* widget, double passedSeconds)
{
    UITextBoxCapability* Self = UITextBoxInternal_GetCapability(widget);
    if (!Self->_isDragging)
    {
        return Error_CreateSuccess();
    }
    // A button that is up ends the drag, in case its release could not reach the box.
    const GameInput* Input = UIScreen_GetInput(Widget_GetScreen(widget));
    if ((Input == NULL) || ((GameInput_GetRawKeyState(Input, GameKey_MouseLeft) & GAME_INPUT_KEY_STATE_DOWN) == 0U))
    {
        Self->_isDragging = false;
        return Error_CreateSuccess();
    }
    // Past an edge the text scrolls toward the pointer, faster further out, and the selection follows.
    Vector2 BoxSize = GetBoxSize(widget);
    Vector2 Pointer = { .x = Self->_dragPointer.x * BoxSize.x, .y = Self->_dragPointer.y * BoxSize.y };
    Rectangle View = Self->_viewBox;
    float Out[UIScrollAxis_Count] =
    {
        (Pointer.x < View.x) ? (Pointer.x - View.x) : ((Pointer.x > (View.x + View.width)) ? (Pointer.x - (View.x + View.width)) : 0.0f),
        (Pointer.y < View.y) ? (Pointer.y - View.y) : ((Pointer.y > (View.y + View.height)) ? (Pointer.y - (View.y + View.height)) : 0.0f)
    };
    Vector2 Delta = { .x = 0.0f, .y = 0.0f };
    for (int32_t Axis = 0; Axis < (int32_t)UIScrollAxis_Count; Axis++)
    {
        float Speed = Math_ClampFloat(Math_AbsFloat(Out[Axis]) * UI_TEXT_BOX_EDGE_SCROLL_RATE, UI_TEXT_BOX_MIN_EDGE_SCROLL_SPEED,
            UI_TEXT_BOX_MAX_EDGE_SCROLL_SPEED);
        float Step = (Out[Axis] == 0.0f) ? 0.0f : (((Out[Axis] < 0.0f) ? -Speed : Speed) * (float)passedSeconds);
        if (Axis == (int32_t)UIScrollAxis_Horizontal)
        {
            Delta.x = Step;
        }
        else
        {
            Delta.y = Step;
        }
    }
    Vector2 Moved = UIScrollableCapability_ScrollBy(&Self->Scrollable, Delta);
    if ((Moved.x != 0.0f) || (Moved.y != 0.0f))
    {
        ExtendDragTo(Self, HitAt(widget, Self, Self->_dragPointer));
    }
    return Error_CreateSuccess();
}


// Public functions.
Error UITextBox_AttachContextMenu(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UITextBoxCapability* Box = NULL;
    Error Result = UITextBoxInternal_Resolve(screen, widgetId, &Target, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UserData Data = UserData_CreateEmpty();
    Memory_Copy(&widgetId, Data.Bytes, sizeof(widgetId));
    return UIContextMenu_SetProvider(screen, widgetId, ProvideMenu, &Data);
}
