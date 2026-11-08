#include "TestSuites.h"
#include "UITextEdit.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include <string.h>


// Macros.
/* Seconds between two keystrokes of quick typing in these tests: well inside a group's pause. */
#define QUICK 0.05


// Static functions.
static bool Make(TestRun* run, UITextEdit* edit, const char* text, const UITextEditLimits* limits)
{
    UITextEditLimits Default = UITextEditLimits_Create();
    UITextEditUndoOptions Undo = UITextEditUndoOptions_Create();
    if (!TEST_CHECK_SUCCESS(run, UITextEdit_Construct(edit, (limits != NULL) ? limits : &Default, &Undo)))
    {
        return false;
    }
    return TEST_CHECK_SUCCESS(run, UITextEdit_SetText(edit, (const unsigned char*)text, strlen(text), NULL));
}

static bool IsText(const UITextEdit* edit, const char* expected)
{
    size_t Length = strlen(expected);
    return (UITextEdit_GetLength(edit) == Length) && (memcmp(UITextEdit_GetText(edit), expected, Length) == 0)
        && (UITextEdit_GetText(edit)[Length] == 0U);
}

static bool IsSelection(const UITextEdit* edit, size_t anchor, size_t caret)
{
    return (UITextEdit_GetAnchor(edit) == anchor) && (UITextEdit_GetCaret(edit) == caret);
}

/* Types each byte of an ASCII text as a codepoint, @p step seconds apart from *time on. */
static void TypeText(TestRun* run, UITextEdit* edit, const char* text, double* time, double step)
{
    for (const char* Cursor = text; *Cursor != 0; Cursor++)
    {
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(edit, (uint32_t)(unsigned char)*Cursor, false, *time, NULL));
        *time += step;
    }
}

static Error Paste(UITextEdit* edit, const char* text, double time, UITextEditChange* outChange)
{
    return UITextEdit_ReplaceSelection(edit, (const unsigned char*)text, strlen(text), UITextEditKind_Whole, time, outChange);
}

static bool AcceptsDigits(uint32_t codepoint, const UserData* userData)
{
    (void)userData;
    return (codepoint >= (uint32_t)'0') && (codepoint <= (uint32_t)'9');
}

static bool DoesNotStartWithZero(const unsigned char* text, size_t length, const UserData* userData)
{
    const bool* IsRefusingAll = UserData_GetPointer(userData);
    return ((IsRefusingAll == NULL) || !*IsRefusingAll) && ((length == 0U) || (text[0] != (unsigned char)'0'));
}

static void CountChange(const UITextEditChange* change, const UserData* userData)
{
    size_t* Count = UserData_GetPointer(userData);
    *Count += change->HasChanged ? 1U : 0U;
}


// Tests: what comes in.
static void TextComesInThroughTheLimits(TestRun* run)
{
    UITextEdit Edit;
    if (Make(run, &Edit, "a\r\nb\rc\x01" "d\x7F" "e\xC2\x85" "f\tg", NULL))
    {
        // "\r\n" and '\r' become '\n', C0 controls, DEL and C1 controls are dropped, a tab stays.
        TEST_CHECK(run, IsText(&Edit, "a\nb\ncdef\tg"));
        TEST_CHECK(run, (UITextEdit_GetCharacterCount(&Edit) == 10U) && (UITextEdit_GetLineCount(&Edit) == 3U));
        TEST_CHECK(run, IsSelection(&Edit, 10U, 10U));
        // Bytes that are no valid UTF-8 become U+FFFD, one per byte: a lone continuation, a lead byte with no continuation, a byte that
        // can never start a codepoint.
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"a\x80" "b\xC3" "c\xFF", 6U, NULL));
        TEST_CHECK(run, IsText(&Edit, "a\xEF\xBF\xBD" "b\xEF\xBF\xBD" "c\xEF\xBF\xBD"));
        TEST_CHECK(run, UITextEdit_GetCharacterCount(&Edit) == 6U);
    }
    UITextEdit_Deconstruct(&Edit);
}

static void LineBreaksObeyTheLimits(TestRun* run)
{
    UITextEdit Edit;
    UITextEditLimits Limits = UITextEditLimits_Create();
    Limits.AllowsNewlines = false;
    if (Make(run, &Edit, "a\nb\r\nc", &Limits))
    {
        // A one-line text turns every line break into a space.
        TEST_CHECK(run, IsText(&Edit, "a b c"));
    }
    UITextEdit_Deconstruct(&Edit);
    Limits = UITextEditLimits_Create();
    Limits.HasMaxLines = true;
    Limits.MaxLines = 2U;
    if (Make(run, &Edit, "a\nb\nc\nd", &Limits))
    {
        // The breaks past the last line allowed become spaces.
        TEST_CHECK(run, IsText(&Edit, "a\nb c d"));
        // A typed break with no line left is refused whole (Enter at the limit), never typed as a space.
        UITextEditChange Change;
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'\n', false, 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "a\nb c d") && !Change.HasChanged && Change.IsRefused);
        // Over a selection holding a break, a typed break fits: the line it frees.
        UITextEdit_Select(&Edit, 0U, 3U);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'\n', false, 1.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "\n c d") && Change.HasChanged && !Change.IsRefused);
    }
    UITextEdit_Deconstruct(&Edit);
    // A text that allows no break refuses a typed one too.
    Limits = UITextEditLimits_Create();
    Limits.AllowsNewlines = false;
    if (Make(run, &Edit, "one", &Limits))
    {
        UITextEditChange Change;
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'\n', false, 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "one") && !Change.HasChanged && Change.IsRefused);
    }
    UITextEdit_Deconstruct(&Edit);
}

static void BudgetsCutAtACodepoint(TestRun* run)
{
    UITextEdit Edit;
    UITextEditLimits Limits = UITextEditLimits_Create();
    Limits.MaxBytes = 4U;
    if (Make(run, &Edit, "", &Limits))
    {
        // "a", then U+0101 (two bytes), then another: the second would end past the limit, so it and what follows are cut.
        UITextEditChange Change;
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"a\xC4\x81\xC4\x81" "b", 6U, &Change));
        TEST_CHECK(run, IsText(&Edit, "a\xC4\x81") && Change.IsRefused && Change.HasChanged);
        // One byte left: a two-byte codepoint does not fit and is refused whole.
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, 0x101U, false, 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "a\xC4\x81") && !Change.HasChanged && Change.IsRefused);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'z', false, 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "a\xC4\x81z") && Change.HasChanged && !Change.IsRefused);
    }
    UITextEdit_Deconstruct(&Edit);
    Limits = UITextEditLimits_Create();
    Limits.HasMaxCharacters = true;
    Limits.MaxCharacters = 3U;
    if (Make(run, &Edit, "\xC4\x81" "bcd", &Limits))
    {
        TEST_CHECK(run, IsText(&Edit, "\xC4\x81" "bc") && (UITextEdit_GetCharacterCount(&Edit) == 3U));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void LimitsJudgeTheResult(TestRun* run)
{
    UITextEdit Edit;
    UITextEditLimits Limits = UITextEditLimits_Create();
    Limits.MaxBytes = 3U;
    Limits.HasMaxCharacters = true;
    Limits.MaxCharacters = 3U;
    if (Make(run, &Edit, "abc", &Limits))
    {
        UITextEditChange Change;
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'x', false, 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "abc") && !Change.HasChanged && Change.IsRefused);
        // Typing over a selection in a full text works: what it replaces does not count.
        UITextEdit_Select(&Edit, 1U, 2U);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'x', false, 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "axc") && Change.HasChanged && !Change.IsRefused && IsSelection(&Edit, 2U, 2U));
        // Overwriting is no growth...
        UITextEdit_SetCaret(&Edit, 0U, false);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'z', true, 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "zxc") && Change.HasChanged);
        // ...but a longer codepoint over a short one is, by bytes.
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, 0x101U, true, 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "zxc") && Change.IsRefused && !Change.HasChanged);
        // A paste over the selection keeps what fits; one where nothing fits, or only control characters come in, changes nothing.
        UITextEdit_Select(&Edit, 0U, 1U);
        TEST_CHECK_SUCCESS(run, Paste(&Edit, "\x01\x02", 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "zxc") && !Change.HasChanged && IsSelection(&Edit, 0U, 1U));
        TEST_CHECK_SUCCESS(run, Paste(&Edit, "\xC4\x81\xC4\x81", 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "zxc") && !Change.HasChanged && Change.IsRefused);
        TEST_CHECK_SUCCESS(run, Paste(&Edit, "123", 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "1xc") && Change.IsRefused && Change.HasChanged);
    }
    UITextEdit_Deconstruct(&Edit);
}

static void ForbiddenCodepointsAndTheFilterDropWhatTheyRefuse(TestRun* run)
{
    UITextEdit Edit;
    UITextEditLimits Limits = UITextEditLimits_Create();
    Limits.ForbiddenCodepoints[0] = (uint32_t)'7';
    Limits.ForbiddenCount = 1U;
    Limits.Filter = AcceptsDigits;
    if (Make(run, &Edit, "", &Limits))
    {
        UITextEditChange Change;
        TEST_CHECK_SUCCESS(run, Paste(&Edit, "1 2a37", 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "123") && Change.IsRefused && Change.HasChanged);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'q', false, 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "123") && Change.IsRefused && !Change.HasChanged);
    }
    UITextEdit_Deconstruct(&Edit);
    Limits.ForbiddenCount = UI_TEXT_EDIT_MAX_FORBIDDEN + 1U;
    TEST_CHECK(run, !UITextEditLimits_IsValid(&Limits));
    Limits.ForbiddenCount = 1U;
    Limits.ForbiddenCodepoints[0] = 0xD800U;
    TEST_CHECK(run, !UITextEditLimits_IsValid(&Limits));
}

static void TheValidatorJudgesTheWholeResult(TestRun* run)
{
    UITextEdit Edit;
    bool IsRefusingAll = false;
    UITextEditLimits Limits = UITextEditLimits_Create();
    Limits.Validator = DoesNotStartWithZero;
    Limits.FilterData = UserData_FromPointer(&IsRefusingAll);
    if (Make(run, &Edit, "", &Limits))
    {
        UITextEditChange Change;
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'0', false, 0.0, &Change));
        TEST_CHECK(run, IsText(&Edit, "") && Change.IsRefused);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'1', false, 0.0, &Change));
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'0', false, QUICK, &Change));
        TEST_CHECK(run, IsText(&Edit, "10"));
        // Undo and redo restore what was there: no limit refuses them.
        IsRefusingAll = true;
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, &Change));
        TEST_CHECK(run, IsText(&Edit, "") && Change.HasChanged);
        TEST_CHECK_SUCCESS(run, UITextEdit_Redo(&Edit, &Change));
        TEST_CHECK(run, IsText(&Edit, "10") && Change.HasChanged);
        // Text from code is judged as well: refused, the text stays.
        IsRefusingAll = false;
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"07", 2U, &Change));
        TEST_CHECK(run, IsText(&Edit, "10") && !Change.HasChanged && Change.IsRefused);
    }
    UITextEdit_Deconstruct(&Edit);
}

/* A filter that tries to change the text it judges: what it gets back. */
typedef struct MeddlingStruct
{
    UITextEdit* Edit;
    ErrorCode SetCode;
    ErrorCode UndoCode;
} Meddling;

static bool Meddling_Filter(uint32_t codepoint, const UserData* userData)
{
    (void)codepoint;
    Meddling* Self = UserData_GetPointer(userData);
    Error Result = UITextEdit_SetText(Self->Edit, (const unsigned char*)"meddled", 7U, NULL);
    Self->SetCode = Result.Code;
    Error_Deconstruct(&Result);
    Result = UITextEdit_Undo(Self->Edit, NULL);
    Self->UndoCode = Result.Code;
    Error_Deconstruct(&Result);
    return true;
}

static void ACallbackCannotChangeItsText(TestRun* run)
{
    UITextEdit Edit;
    Meddling Self = { .Edit = &Edit, .SetCode = ErrorCode_Success, .UndoCode = ErrorCode_Success };
    UITextEditLimits Limits = UITextEditLimits_Create();
    Limits.Filter = Meddling_Filter;
    Limits.FilterData = UserData_FromPointer(&Self);
    if (Make(run, &Edit, "", &Limits))
    {
        double Time = 0.0;
        TypeText(run, &Edit, "ab", &Time, QUICK);
        // Refused while the filter runs; the edit it judged goes in whole, and undoes as a step.
        TEST_CHECK(run, IsText(&Edit, "ab") && (Self.SetCode == ErrorCode_InvalidOperation) && (Self.UndoCode == ErrorCode_InvalidOperation));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, ""));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void HostilePastesStayBounded(TestRun* run)
{
    UITextEdit Edit;
    UITextEditLimits Limits = UITextEditLimits_Create();
    Limits.MaxBytes = 64U;
    Limits.AllowsNewlines = false;
    if (Make(run, &Edit, "", &Limits))
    {
        // A megabyte of text keeps what fits.
        size_t HugeLength = 1024U * 1024U;
        unsigned char* Huge = Memory_Allocate(HugeLength);
        Memory_Set(Huge, (unsigned char)'x', HugeLength);
        UITextEditChange Change;
        TEST_CHECK_SUCCESS(run, UITextEdit_ReplaceSelection(&Edit, Huge, HugeLength, UITextEditKind_Whole, 0.0, &Change));
        TEST_CHECK(run, (UITextEdit_GetLength(&Edit) == 64U) && Change.IsRefused);
        // A megabyte of control characters writes nothing and reads only a few bytes per byte of room.
        Memory_Set(Huge, 0x01U, HugeLength);
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, Huge, HugeLength, &Change));
        TEST_CHECK(run, IsText(&Edit, "") && Change.IsRefused);
        // Newlines and invalid UTF-8 in a one-line paste.
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"line one\r\nline\xFE two", 19U, &Change));
        TEST_CHECK(run, IsText(&Edit, "line one line\xEF\xBF\xBD two") && !Change.IsRefused);
        Memory_Free(Huge);
    }
    UITextEdit_Deconstruct(&Edit);
}

static void LimitsAndOptionsAreChecked(TestRun* run)
{
    UITextEditLimits Limits = UITextEditLimits_Create();
    UITextEditUndoOptions Undo = UITextEditUndoOptions_Create();
    TEST_CHECK(run, UITextEditLimits_IsValid(&Limits) && UITextEditUndoOptions_IsValid(&Undo));
    Limits.MaxBytes = UI_TEXT_EDIT_MAX_BYTES + 1U;
    TEST_CHECK(run, !UITextEditLimits_IsValid(&Limits));
    Limits = UITextEditLimits_Create();
    Limits.HasMaxLines = true;
    Limits.MaxLines = 0U;
    TEST_CHECK(run, !UITextEditLimits_IsValid(&Limits));
    Undo.GroupCharacters = 0U;
    TEST_CHECK(run, !UITextEditUndoOptions_IsValid(&Undo));
    Undo = UITextEditUndoOptions_Create();
    Undo.PauseSeconds = -1.0;
    TEST_CHECK(run, !UITextEditUndoOptions_IsValid(&Undo));
    Undo = UITextEditUndoOptions_Create();
    Undo.BudgetBytes = UI_TEXT_EDIT_MAX_UNDO_BUDGET + 1U;
    TEST_CHECK(run, !UITextEditUndoOptions_IsValid(&Undo));
    UITextEdit Edit;
    TEST_CHECK_ERROR(run, UITextEdit_Construct(&Edit, &Limits, &Undo), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UITextEdit_Construct(NULL, &Limits, &Undo), ErrorCode_IllegalArgument);
    UITextEditLimits Good = UITextEditLimits_Create();
    UITextEditUndoOptions GoodUndo = UITextEditUndoOptions_Create();
    if (TEST_CHECK_SUCCESS(run, UITextEdit_Construct(&Edit, &Good, &GoodUndo)))
    {
        TEST_CHECK_ERROR(run, UITextEdit_Replace(&Edit, 2U, 1U, NULL, 0U, UITextEditKind_Whole, 0.0, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UITextEdit_Replace(&Edit, 0U, 0U, NULL, 3U, UITextEditKind_Whole, 0.0, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UITextEdit_Replace(&Edit, 0U, 0U, NULL, 0U, (UITextEditKind)99, 0.0, NULL), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UITextEdit_TypeCodepoint(&Edit, 0x110000U, false, 0.0, NULL), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UITextEdit_SetLimits(&Edit, &Limits, NULL), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UITextEdit_SetUndoOptions(&Edit, &Undo), ErrorCode_ArgumentOutOfRange);
    }
    UITextEdit_Deconstruct(&Edit);
}


// Tests: editing.
static void TypingReplacesTheSelection(TestRun* run)
{
    UITextEdit Edit;
    if (Make(run, &Edit, "hello", NULL))
    {
        UITextEdit_Select(&Edit, 4U, 1U);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'a', false, 0.0, NULL));
        TEST_CHECK(run, IsText(&Edit, "hao") && IsSelection(&Edit, 2U, 2U));
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, 0x416U, false, QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "ha\xD0\x96o") && IsSelection(&Edit, 4U, 4U));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void OverwritingNeverTakesALineBreak(TestRun* run)
{
    UITextEdit Edit;
    if (Make(run, &Edit, "ab\ncd", NULL))
    {
        UITextEdit_SetCaret(&Edit, 1U, false);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'x', true, 0.0, NULL));
        TEST_CHECK(run, IsText(&Edit, "ax\ncd") && IsSelection(&Edit, 2U, 2U));
        // Before the line break it goes in.
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'y', true, QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "axy\ncd"));
        // At the end it goes in too, and over a selection it replaces the selection.
        UITextEdit_SetCaret(&Edit, UITextEdit_GetLength(&Edit), false);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'!', true, QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "axy\ncd!"));
        UITextEdit_Select(&Edit, 0U, 4U);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'Q', true, QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "Qcd!"));
        // A multi-byte character is replaced whole.
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"\xC4\x81" "b", 3U, NULL));
        UITextEdit_SetCaret(&Edit, 0U, false);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'a', true, 0.0, NULL));
        TEST_CHECK(run, IsText(&Edit, "ab"));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void DeletingByCharacterSelectionAndWord(TestRun* run)
{
    UITextEdit Edit;
    if (Make(run, &Edit, "one two\xC4\x81", NULL))
    {
        TEST_CHECK_SUCCESS(run, UITextEdit_DeleteBackward(&Edit, false, false, 0.0, NULL));
        TEST_CHECK(run, IsText(&Edit, "one two"));
        TEST_CHECK_SUCCESS(run, UITextEdit_DeleteBackward(&Edit, true, false, 1.0, NULL));
        TEST_CHECK(run, IsText(&Edit, "one ") && IsSelection(&Edit, 4U, 4U));
        UITextEdit_SetCaret(&Edit, 0U, false);
        TEST_CHECK_SUCCESS(run, UITextEdit_DeleteBackward(&Edit, false, false, 2.0, NULL));
        TEST_CHECK(run, IsText(&Edit, "one "));
        TEST_CHECK_SUCCESS(run, UITextEdit_DeleteForward(&Edit, true, false, 3.0, NULL));
        TEST_CHECK(run, IsText(&Edit, " "));
        TEST_CHECK_SUCCESS(run, UITextEdit_DeleteForward(&Edit, false, false, 4.0, NULL));
        TEST_CHECK(run, IsText(&Edit, ""));
        TEST_CHECK_SUCCESS(run, UITextEdit_DeleteForward(&Edit, false, false, 5.0, NULL));
        TEST_CHECK(run, IsText(&Edit, ""));
        // A selection goes whole, whichever way.
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"abcdef", 6U, NULL));
        UITextEdit_Select(&Edit, 4U, 1U);
        TEST_CHECK_SUCCESS(run, UITextEdit_DeleteForward(&Edit, true, false, 6.0, NULL));
        TEST_CHECK(run, IsText(&Edit, "aef") && IsSelection(&Edit, 1U, 1U));
        // A masked text's word is the whole text.
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"pass word", 9U, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_DeleteBackward(&Edit, true, true, 7.0, NULL));
        TEST_CHECK(run, IsText(&Edit, ""));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void LeftAndRightCollapseASelection(TestRun* run)
{
    UITextEdit Edit;
    if (Make(run, &Edit, "abcd efg", NULL))
    {
        UITextEdit_Select(&Edit, 1U, 3U);
        UITextEdit_MoveLeft(&Edit, false, false, false);
        TEST_CHECK(run, IsSelection(&Edit, 1U, 1U));
        UITextEdit_Select(&Edit, 3U, 1U);
        UITextEdit_MoveRight(&Edit, false, false, false);
        TEST_CHECK(run, IsSelection(&Edit, 3U, 3U));
        // Shift extends both ways from the caret.
        UITextEdit_MoveRight(&Edit, false, true, false);
        UITextEdit_MoveRight(&Edit, false, true, false);
        TEST_CHECK(run, IsSelection(&Edit, 3U, 5U));
        UITextEdit_MoveLeft(&Edit, false, true, false);
        UITextEdit_MoveLeft(&Edit, false, true, false);
        UITextEdit_MoveLeft(&Edit, false, true, false);
        TEST_CHECK(run, IsSelection(&Edit, 3U, 2U));
        // Ctrl moves by words, from the selection's end without Shift.
        UITextEdit_Select(&Edit, 1U, 2U);
        UITextEdit_MoveRight(&Edit, true, false, false);
        TEST_CHECK(run, IsSelection(&Edit, 4U, 4U));
        UITextEdit_MoveRight(&Edit, true, true, false);
        TEST_CHECK(run, IsSelection(&Edit, 4U, 8U));
        UITextEdit_MoveLeft(&Edit, true, false, true);
        TEST_CHECK(run, IsSelection(&Edit, 0U, 0U));
        UITextEdit_MoveLeft(&Edit, false, false, false);
        TEST_CHECK(run, IsSelection(&Edit, 0U, 0U));
        UITextEdit_SelectAll(&Edit);
        TEST_CHECK(run, IsSelection(&Edit, 0U, 8U));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void WordsStopAtSymbolsSpacesAndLineBreaks(TestRun* run)
{
    // f0 o1 o2 _3 b4 a5 r6, two spaces, b9 a10 z11 .12 q13 u14 x15 \n16 !17 !18 x19.
    const unsigned char* Text = (const unsigned char*)"foo_bar  baz.qux\n!!x";
    size_t Length = 20U;
    TEST_CHECK(run, UITextEdit_FindWordEnd(Text, Length, 0U, false) == 7U);
    TEST_CHECK(run, UITextEdit_FindWordEnd(Text, Length, 7U, false) == 12U);
    TEST_CHECK(run, UITextEdit_FindWordEnd(Text, Length, 12U, false) == 13U);
    TEST_CHECK(run, UITextEdit_FindWordEnd(Text, Length, 13U, false) == 16U);
    TEST_CHECK(run, UITextEdit_FindWordEnd(Text, Length, 16U, false) == 19U);
    TEST_CHECK(run, UITextEdit_FindWordEnd(Text, Length, 19U, false) == 20U);
    TEST_CHECK(run, UITextEdit_FindWordEnd(Text, Length, 20U, false) == 20U);
    TEST_CHECK(run, UITextEdit_FindWordStart(Text, Length, 20U, false) == 19U);
    TEST_CHECK(run, UITextEdit_FindWordStart(Text, Length, 19U, false) == 17U);
    TEST_CHECK(run, UITextEdit_FindWordStart(Text, Length, 17U, false) == 13U);
    TEST_CHECK(run, UITextEdit_FindWordStart(Text, Length, 13U, false) == 12U);
    TEST_CHECK(run, UITextEdit_FindWordStart(Text, Length, 12U, false) == 9U);
    TEST_CHECK(run, UITextEdit_FindWordStart(Text, Length, 9U, false) == 0U);
    TEST_CHECK(run, UITextEdit_FindWordStart(Text, Length, 0U, false) == 0U);
    TEST_CHECK(run, (UITextEdit_FindWordStart(Text, Length, 9U, true) == 0U) && (UITextEdit_FindWordEnd(Text, Length, 1U, true) == Length));

    size_t Start = 99U;
    size_t End = 99U;
    UITextEdit_GetWordAt(Text, Length, 5U, false, &Start, &End);
    TEST_CHECK(run, (Start == 0U) && (End == 7U));
    UITextEdit_GetWordAt(Text, Length, 7U, false, &Start, &End);
    TEST_CHECK(run, (Start == 7U) && (End == 9U));
    UITextEdit_GetWordAt(Text, Length, 12U, false, &Start, &End);
    TEST_CHECK(run, (Start == 12U) && (End == 13U));
    // At a line break and at the end, the run before it.
    UITextEdit_GetWordAt(Text, Length, 16U, false, &Start, &End);
    TEST_CHECK(run, (Start == 13U) && (End == 16U));
    UITextEdit_GetWordAt(Text, Length, 20U, false, &Start, &End);
    TEST_CHECK(run, (Start == 19U) && (End == 20U));
    UITextEdit_GetWordAt(Text, Length, 3U, true, &Start, &End);
    TEST_CHECK(run, (Start == 0U) && (End == Length));
    // An empty line has no word.
    UITextEdit_GetWordAt((const unsigned char*)"a\n\nb", 4U, 2U, false, &Start, &End);
    TEST_CHECK(run, (Start == 2U) && (End == 2U));

    // Outside ASCII: letters are words, the middle dot is a symbol, the no-break space a space.
    TEST_CHECK(run, UITextEdit_GetCharacterClass(0x101U) == UITextEditCharacterClass_Word);
    TEST_CHECK(run, UITextEdit_GetCharacterClass(0x416U) == UITextEditCharacterClass_Word);
    TEST_CHECK(run, UITextEdit_GetCharacterClass(0xB7U) == UITextEditCharacterClass_Symbol);
    TEST_CHECK(run, UITextEdit_GetCharacterClass(0x2014U) == UITextEditCharacterClass_Symbol);
    TEST_CHECK(run, UITextEdit_GetCharacterClass(0xA0U) == UITextEditCharacterClass_Space);
    TEST_CHECK(run, UITextEdit_GetCharacterClass((uint32_t)'\t') == UITextEditCharacterClass_Space);
    const unsigned char* Latvian = (const unsigned char*)"\xC4\x81" "b\xC2\xB7" "c";
    TEST_CHECK(run, UITextEdit_FindWordEnd(Latvian, 6U, 0U, false) == 3U);
}

static void LinesSpanTheirLineBreaks(TestRun* run)
{
    const unsigned char* Text = (const unsigned char*)"ab\ncd\n";
    size_t Start = 99U;
    size_t End = 99U;
    UITextEdit_GetLineAt(Text, 6U, 1U, &Start, &End);
    TEST_CHECK(run, (Start == 0U) && (End == 2U));
    UITextEdit_GetLineAt(Text, 6U, 2U, &Start, &End);
    TEST_CHECK(run, (Start == 0U) && (End == 2U));
    UITextEdit_GetLineAt(Text, 6U, 4U, &Start, &End);
    TEST_CHECK(run, (Start == 3U) && (End == 5U));
    UITextEdit_GetLineAt(Text, 6U, 6U, &Start, &End);
    TEST_CHECK(run, (Start == 6U) && (End == 6U));
    UITextEdit_GetLineAt(Text, 6U, 60U, &Start, &End);
    TEST_CHECK(run, (Start == 6U) && (End == 6U));
}

static void OffsetsStayOnCodepointBoundaries(TestRun* run)
{
    // "a", U+20AC (three bytes), "b".
    const unsigned char* Text = (const unsigned char*)"a\xE2\x82\xAC" "b";
    TEST_CHECK(run, UITextEdit_SnapToBoundary(Text, 5U, 2U) == 1U);
    TEST_CHECK(run, UITextEdit_SnapToBoundary(Text, 5U, 3U) == 1U);
    TEST_CHECK(run, UITextEdit_SnapToBoundary(Text, 5U, 9U) == 5U);
    TEST_CHECK(run, UITextEdit_GetNextBoundary(Text, 5U, 1U) == 4U);
    TEST_CHECK(run, UITextEdit_GetPreviousBoundary(Text, 5U, 4U) == 1U);
    TEST_CHECK(run, UITextEdit_GetPreviousBoundary(Text, 5U, 0U) == 0U);
    TEST_CHECK(run, UITextEdit_GetNextBoundary(Text, 5U, 5U) == 5U);
    UITextEdit Edit;
    if (Make(run, &Edit, "a\xE2\x82\xAC" "b", NULL))
    {
        UITextEdit_SetCaret(&Edit, 3U, false);
        TEST_CHECK(run, IsSelection(&Edit, 1U, 1U));
        UITextEdit_Select(&Edit, 2U, 99U);
        TEST_CHECK(run, IsSelection(&Edit, 1U, 5U));
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 2U, 3U, (const unsigned char*)"x", 1U, UITextEditKind_Whole, 0.0, NULL));
        TEST_CHECK(run, IsText(&Edit, "ax\xE2\x82\xAC" "b"));
    }
    UITextEdit_Deconstruct(&Edit);
}


// Tests: undo.
static void TypingGroupsIntoOneStep(TestRun* run)
{
    UITextEdit Edit;
    double Time = 0.0;
    if (Make(run, &Edit, "", NULL))
    {
        TypeText(run, &Edit, "hello", &Time, QUICK);
        TEST_CHECK(run, UITextEdit_GetStepCount(&Edit) == 1U);
        UITextEditChange Change;
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, &Change));
        TEST_CHECK(run, IsText(&Edit, "") && IsSelection(&Edit, 0U, 0U) && Change.HasChanged && (Change.RemovedLength == 5U));
        TEST_CHECK_SUCCESS(run, UITextEdit_Redo(&Edit, &Change));
        TEST_CHECK(run, IsText(&Edit, "hello") && IsSelection(&Edit, 5U, 5U));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void APauseEndsAGroupAtItsEdge(TestRun* run)
{
    UITextEdit Edit;
    if (Make(run, &Edit, "", NULL))
    {
        // A pause of exactly the limit still joins; a little more starts a new step.
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'a', false, 10.0, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'b', false, 11.0, NULL));
        TEST_CHECK(run, UITextEdit_GetStepCount(&Edit) == 1U);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'c', false, 12.001, NULL));
        TEST_CHECK(run, UITextEdit_GetStepCount(&Edit) == 2U);
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "ab"));
        // The pause counts from the last keystroke: steady typing a little slower than the pause's half stays one step.
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"", 0U, NULL));
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'s', false, 15.0 + (0.8 * (double)Index), NULL));
        }
        TEST_CHECK(run, IsText(&Edit, "ssss") && (UITextEdit_GetStepCount(&Edit) == 1U));
        // A shorter pause of the text's own.
        UITextEditUndoOptions Options = UITextEditUndoOptions_Create();
        Options.PauseSeconds = 0.2;
        TEST_CHECK_SUCCESS(run, UITextEdit_SetUndoOptions(&Edit, &Options));
        TEST_CHECK(run, UITextEdit_GetStepCount(&Edit) == 0U);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'d', false, 20.0, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'e', false, 20.3, NULL));
        TEST_CHECK(run, UITextEdit_GetStepCount(&Edit) == 2U);
    }
    UITextEdit_Deconstruct(&Edit);
}

static void AGroupHoldsItsCountOfCharacters(TestRun* run)
{
    UITextEdit Edit;
    double Time = 0.0;
    if (Make(run, &Edit, "", NULL))
    {
        TypeText(run, &Edit, "abcdefghijklmnopqrstuvwxy", &Time, QUICK);
        TEST_CHECK(run, UITextEdit_GetStepCount(&Edit) == 2U);
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "abcdefghijklmnopqrst"));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, ""));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void ACaretMoveEndsAGroup(TestRun* run)
{
    UITextEdit Edit;
    double Time = 0.0;
    if (Make(run, &Edit, "", NULL))
    {
        TypeText(run, &Edit, "ab", &Time, QUICK);
        UITextEdit_SetCaret(&Edit, 2U, false);
        TypeText(run, &Edit, "cd", &Time, QUICK);
        TEST_CHECK(run, UITextEdit_GetStepCount(&Edit) == 2U);
        UITextEdit_CloseUndoGroup(&Edit);
        TypeText(run, &Edit, "e", &Time, QUICK);
        TEST_CHECK(run, UITextEdit_GetStepCount(&Edit) == 3U);
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "ab"));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void AChangeOfKindEndsAGroupAndBackspacesGroup(TestRun* run)
{
    UITextEdit Edit;
    double Time = 0.0;
    if (Make(run, &Edit, "", NULL))
    {
        TypeText(run, &Edit, "abcdef", &Time, QUICK);
        for (int32_t Index = 0; Index < 3; Index++)
        {
            TEST_CHECK_SUCCESS(run, UITextEdit_DeleteBackward(&Edit, false, false, Time, NULL));
            Time += QUICK;
        }
        TEST_CHECK(run, IsText(&Edit, "abc") && (UITextEdit_GetStepCount(&Edit) == 2U));
        TypeText(run, &Edit, "x", &Time, QUICK);
        TEST_CHECK(run, UITextEdit_GetStepCount(&Edit) == 3U);
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "abcdef") && IsSelection(&Edit, 6U, 6U));
        // Deleting forward groups the same way, and undoing puts the caret back before it.
        UITextEdit_SetCaret(&Edit, 1U, false);
        for (int32_t Index = 0; Index < 3; Index++)
        {
            TEST_CHECK_SUCCESS(run, UITextEdit_DeleteForward(&Edit, false, false, Time, NULL));
            Time += QUICK;
        }
        TEST_CHECK(run, IsText(&Edit, "aef"));
        size_t Steps = UITextEdit_GetStepCount(&Edit);
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "abcdef") && IsSelection(&Edit, 1U, 1U) && (Steps == 2U));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void TypingOverASelectionStartsAGroup(TestRun* run)
{
    UITextEdit Edit;
    double Time = 0.0;
    if (Make(run, &Edit, "hello", NULL))
    {
        TypeText(run, &Edit, "!", &Time, QUICK);
        UITextEdit_Select(&Edit, 1U, 4U);
        TypeText(run, &Edit, "ab", &Time, QUICK);
        TEST_CHECK(run, IsText(&Edit, "habo!") && (UITextEdit_GetStepCount(&Edit) == 2U));
        // Undo puts the selection back as it was.
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "hello!") && IsSelection(&Edit, 1U, 4U));
        // Overwriting groups the same way.
        UITextEdit_SetCaret(&Edit, 0U, false);
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'J', true, Time, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_TypeCodepoint(&Edit, (uint32_t)'E', true, Time + QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "JEllo!"));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "hello!") && IsSelection(&Edit, 0U, 0U));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void OnlyContiguousEditsOfAKindJoin(TestRun* run)
{
    UITextEdit Edit;
    if (Make(run, &Edit, "", NULL))
    {
        // Typing that removes something, or that lands somewhere else, starts a step; typing on at the end joins.
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 0U, 0U, (const unsigned char*)"abc", 3U, UITextEditKind_Typing, 0.0, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 3U, 3U, (const unsigned char*)"d", 1U, UITextEditKind_Typing, QUICK, NULL));
        TEST_CHECK(run, UITextEdit_GetStepCount(&Edit) == 1U);
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 3U, 4U, (const unsigned char*)"e", 1U, UITextEditKind_Typing, 2.0 * QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "abce") && (UITextEdit_GetStepCount(&Edit) == 2U));
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 0U, 0U, (const unsigned char*)"x", 1U, UITextEditKind_Typing, 3.0 * QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "xabce") && (UITextEdit_GetStepCount(&Edit) == 3U));
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 1U, 2U, (const unsigned char*)"y", 1U, UITextEditKind_Typing, 4.0 * QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "xybce") && (UITextEdit_GetStepCount(&Edit) == 4U));
        // Deleting backward joins only the deletion just before it, forward only the one at the same place.
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"abcdefgh", 8U, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 6U, 7U, NULL, 0U, UITextEditKind_DeletingBackward, 0.0, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 5U, 6U, NULL, 0U, UITextEditKind_DeletingBackward, QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "abcdeh") && (UITextEdit_GetStepCount(&Edit) == 1U));
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 1U, 2U, NULL, 0U, UITextEditKind_DeletingBackward, 2.0 * QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "acdeh") && (UITextEdit_GetStepCount(&Edit) == 2U));
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 1U, 2U, NULL, 0U, UITextEditKind_DeletingForward, 3.0 * QUICK, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 1U, 2U, NULL, 0U, UITextEditKind_DeletingForward, 4.0 * QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "aeh") && (UITextEdit_GetStepCount(&Edit) == 3U));
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 2U, 3U, NULL, 0U, UITextEditKind_DeletingForward, 5.0 * QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "ae") && (UITextEdit_GetStepCount(&Edit) == 4U));
        // Each step undoes alone.
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "aeh"));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "acdeh"));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "abcdeh"));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "abcdefgh"));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void PastesCutsAndWordsAreStepsOfTheirOwn(TestRun* run)
{
    UITextEdit Edit;
    double Time = 0.0;
    if (Make(run, &Edit, "", NULL))
    {
        TypeText(run, &Edit, "ab", &Time, QUICK);
        TEST_CHECK_SUCCESS(run, Paste(&Edit, " cd", Time, NULL));
        TypeText(run, &Edit, "e", &Time, QUICK);
        TEST_CHECK(run, IsText(&Edit, "ab cde") && (UITextEdit_GetStepCount(&Edit) == 3U));
        TEST_CHECK_SUCCESS(run, UITextEdit_DeleteBackward(&Edit, true, false, Time, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_DeleteBackward(&Edit, false, false, Time + QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "ab") && (UITextEdit_GetStepCount(&Edit) == 5U));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "ab "));
        // A cut is the selection deleted as a step of its own.
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"abcd", 4U, NULL));
        UITextEdit_Select(&Edit, 1U, 3U);
        TEST_CHECK_SUCCESS(run, UITextEdit_ReplaceSelection(&Edit, NULL, 0U, UITextEditKind_Whole, Time, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_DeleteBackward(&Edit, false, false, Time + QUICK, NULL));
        TEST_CHECK(run, IsText(&Edit, "d") && (UITextEdit_GetStepCount(&Edit) == 2U));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void AnEditClearsWhatCouldBeRedone(TestRun* run)
{
    UITextEdit Edit;
    double Time = 0.0;
    if (Make(run, &Edit, "", NULL))
    {
        TypeText(run, &Edit, "ab", &Time, QUICK);
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, UITextEdit_CanRedo(&Edit));
        TypeText(run, &Edit, "x", &Time, QUICK);
        TEST_CHECK(run, !UITextEdit_CanRedo(&Edit) && (UITextEdit_GetStepCount(&Edit) == 1U));
        // After an undo the next edit never joins the step before.
        TypeText(run, &Edit, "y", &Time, QUICK);
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_Redo(&Edit, NULL));
        TypeText(run, &Edit, "z", &Time, QUICK);
        TEST_CHECK(run, IsText(&Edit, "xyz") && (UITextEdit_GetStepCount(&Edit) == 2U));
        // With nothing left, undo and redo are refused and change nothing.
        UITextEditChange Change;
        TEST_CHECK_SUCCESS(run, UITextEdit_Redo(&Edit, &Change));
        TEST_CHECK(run, !Change.HasChanged && Change.IsRefused);
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, &Change));
        TEST_CHECK(run, IsText(&Edit, "") && !Change.HasChanged && Change.IsRefused);
    }
    UITextEdit_Deconstruct(&Edit);
}

static void TheStepCapDropsTheOldest(TestRun* run)
{
    UITextEdit Edit;
    if (Make(run, &Edit, "", NULL))
    {
        for (size_t Index = 0U; Index < UI_TEXT_EDIT_MAX_UNDO_STEPS + 5U; Index++)
        {
            TEST_CHECK_SUCCESS(run, Paste(&Edit, "x", (double)Index, NULL));
        }
        TEST_CHECK(run, UITextEdit_GetStepCount(&Edit) == UI_TEXT_EDIT_MAX_UNDO_STEPS);
        while (UITextEdit_CanUndo(&Edit))
        {
            TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        }
        // The five oldest steps are gone: their text cannot be taken back.
        TEST_CHECK(run, IsText(&Edit, "xxxxx"));
    }
    UITextEdit_Deconstruct(&Edit);
}

static void TheBudgetDropsTheOldest(TestRun* run)
{
    UITextEdit Edit;
    UITextEditLimits Limits = UITextEditLimits_Create();
    UITextEditUndoOptions Undo = UITextEditUndoOptions_Create();
    Undo.BudgetBytes = 1000U;
    if (TEST_CHECK_SUCCESS(run, UITextEdit_Construct(&Edit, &Limits, &Undo)))
    {
        char Chunk[101];
        Memory_Set(Chunk, 'y', 100U);
        Chunk[100] = 0;
        for (size_t Index = 0U; Index < 20U; Index++)
        {
            TEST_CHECK_SUCCESS(run, Paste(&Edit, Chunk, (double)Index, NULL));
            TEST_CHECK(run, UITextEdit_GetUndoMemory(&Edit) <= Undo.BudgetBytes);
        }
        size_t Kept = UITextEdit_GetStepCount(&Edit);
        TEST_CHECK(run, (Kept > 0U) && (Kept < 20U));
        // A step larger than the whole budget empties the list; the edit itself still happens.
        char Big[1201];
        Memory_Set(Big, 'z', 1200U);
        Big[1200] = 0;
        UITextEditChange Change;
        TEST_CHECK_SUCCESS(run, Paste(&Edit, Big, 30.0, &Change));
        TEST_CHECK(run, Change.HasChanged && (UITextEdit_GetStepCount(&Edit) == 0U) && !UITextEdit_CanUndo(&Edit));
        TEST_CHECK(run, UITextEdit_GetLength(&Edit) == 3200U);
        // The room that step took is let go.
        TEST_CHECK(run, Edit._undoBytes._capacity <= Undo.BudgetBytes);
        // Each step undoes its own bytes once older ones went: chunks of different letters, undone one by one.
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"", 0U, NULL));
        for (size_t Index = 0U; Index < 12U; Index++)
        {
            Memory_Set(Chunk, (int8_t)('a' + (int)Index), 100U);
            TEST_CHECK_SUCCESS(run, Paste(&Edit, Chunk, 100.0 + (double)Index, NULL));
        }
        size_t Steps = UITextEdit_GetStepCount(&Edit);
        TEST_CHECK(run, (Steps > 0U) && (Steps < 12U));
        for (size_t Undone = 1U; Undone <= Steps; Undone++)
        {
            TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
            size_t Left = 12U - Undone;
            const unsigned char* Text = UITextEdit_GetText(&Edit);
            bool IsRight = UITextEdit_GetLength(&Edit) == (Left * 100U);
            for (size_t Offset = 0U; IsRight && (Offset < (Left * 100U)); Offset++)
            {
                IsRight = Text[Offset] == (unsigned char)('a' + (int)(Offset / 100U));
            }
            TEST_CHECK(run, IsRight);
        }
        // And redo puts back each step's own bytes.
        while (UITextEdit_CanRedo(&Edit))
        {
            TEST_CHECK_SUCCESS(run, UITextEdit_Redo(&Edit, NULL));
        }
        const unsigned char* Text = UITextEdit_GetText(&Edit);
        bool IsWhole = UITextEdit_GetLength(&Edit) == 1200U;
        for (size_t Offset = 0U; IsWhole && (Offset < 1200U); Offset++)
        {
            IsWhole = Text[Offset] == (unsigned char)('a' + (int)(Offset / 100U));
        }
        TEST_CHECK(run, IsWhole);
    }
    UITextEdit_Deconstruct(&Edit);
}

static void CodeAndNewLimitsEmptyTheList(TestRun* run)
{
    UITextEdit Edit;
    double Time = 0.0;
    if (Make(run, &Edit, "", NULL))
    {
        TypeText(run, &Edit, "abc", &Time, QUICK);
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"new", 3U, NULL));
        TEST_CHECK(run, !UITextEdit_CanUndo(&Edit) && IsSelection(&Edit, 3U, 3U));
        // An edit from code is an undoable step of its own.
        TEST_CHECK_SUCCESS(run, UITextEdit_Replace(&Edit, 0U, 0U, (const unsigned char*)"re", 2U, UITextEditKind_Whole, Time, NULL));
        TEST_CHECK(run, IsText(&Edit, "renew") && (UITextEdit_GetStepCount(&Edit) == 1U));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "new"));
        // New limits cut the text to fit and empty the list.
        TEST_CHECK_SUCCESS(run, UITextEdit_Redo(&Edit, NULL));
        UITextEditLimits Limits = UITextEditLimits_Create();
        Limits.MaxBytes = 3U;
        UITextEditChange Change;
        TEST_CHECK_SUCCESS(run, UITextEdit_SetLimits(&Edit, &Limits, &Change));
        TEST_CHECK(run, IsText(&Edit, "ren") && Change.HasChanged && Change.IsRefused && !UITextEdit_CanUndo(&Edit));
        TEST_CHECK(run, UITextEdit_GetCaret(&Edit) <= 3U);
        // Limits the text already fits change nothing but the list.
        TypeText(run, &Edit, "", &Time, QUICK);
        TEST_CHECK_SUCCESS(run, UITextEdit_SetLimits(&Edit, &Limits, &Change));
        TEST_CHECK(run, IsText(&Edit, "ren") && !Change.HasChanged);
    }
    UITextEdit_Deconstruct(&Edit);
}

static void AMarkRevertsByUndoing(TestRun* run)
{
    UITextEdit Edit;
    double Time = 0.0;
    if (Make(run, &Edit, "", NULL))
    {
        TypeText(run, &Edit, "ab", &Time, QUICK);
        UITextEdit_SetMark(&Edit);
        TypeText(run, &Edit, "cd", &Time, QUICK);
        TEST_CHECK_SUCCESS(run, Paste(&Edit, "!", Time, NULL));
        size_t Changes = 0U;
        UserData Counter = UserData_FromPointer(&Changes);
        bool HasChanged = false;
        TEST_CHECK_SUCCESS(run, UITextEdit_RevertToMark(&Edit, (const unsigned char*)"ab", 2U, CountChange, &Counter, &HasChanged));
        TEST_CHECK(run, IsText(&Edit, "ab") && HasChanged && (Changes == 2U));
        // The steps undone stay redoable, and the steps before the mark undoable.
        TEST_CHECK(run, UITextEdit_CanRedo(&Edit) && UITextEdit_CanUndo(&Edit));
        TEST_CHECK_SUCCESS(run, UITextEdit_Redo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, "abcd"));
        // Reached by redoing: undo past the mark, then revert.
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TEST_CHECK(run, IsText(&Edit, ""));
        TEST_CHECK_SUCCESS(run, UITextEdit_RevertToMark(&Edit, (const unsigned char*)"ab", 2U, NULL, NULL, &HasChanged));
        TEST_CHECK(run, IsText(&Edit, "ab") && HasChanged);
        // Nothing to revert.
        TEST_CHECK_SUCCESS(run, UITextEdit_RevertToMark(&Edit, (const unsigned char*)"ab", 2U, NULL, NULL, &HasChanged));
        TEST_CHECK(run, IsText(&Edit, "ab") && !HasChanged);
    }
    UITextEdit_Deconstruct(&Edit);
}

static void ALostMarkFallsBackToTheText(TestRun* run)
{
    UITextEdit Edit;
    double Time = 0.0;
    if (Make(run, &Edit, "start", NULL))
    {
        UITextEdit_SetMark(&Edit);
        TEST_CHECK(run, UITextEdit_HasMark(&Edit));
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"by code", 7U, NULL));
        TEST_CHECK(run, !UITextEdit_HasMark(&Edit));
        TypeText(run, &Edit, "x", &Time, QUICK);
        bool HasChanged = false;
        TEST_CHECK_SUCCESS(run, UITextEdit_RevertToMark(&Edit, (const unsigned char*)"start", 5U, NULL, NULL, &HasChanged));
        TEST_CHECK(run, IsText(&Edit, "start") && HasChanged && !UITextEdit_CanUndo(&Edit));
        // The steps the mark needs dropped by the step cap.
        UITextEdit_SetMark(&Edit);
        for (size_t Index = 0U; Index < UI_TEXT_EDIT_MAX_UNDO_STEPS + 1U; Index++)
        {
            TEST_CHECK_SUCCESS(run, Paste(&Edit, "x", (double)Index, NULL));
        }
        TEST_CHECK(run, !UITextEdit_HasMark(&Edit));
        // An edit that drops redoable steps the mark stood among.
        TEST_CHECK_SUCCESS(run, UITextEdit_SetText(&Edit, (const unsigned char*)"", 0U, NULL));
        TypeText(run, &Edit, "ab", &Time, QUICK);
        UITextEdit_SetMark(&Edit);
        TEST_CHECK_SUCCESS(run, UITextEdit_Undo(&Edit, NULL));
        TypeText(run, &Edit, "q", &Time, QUICK);
        TEST_CHECK(run, !UITextEdit_HasMark(&Edit));
    }
    UITextEdit_Deconstruct(&Edit);
}


// Public functions.
void UITextEditTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "text_comes_in_through_the_limits", TextComesInThroughTheLimits },
        { "line_breaks_obey_the_limits", LineBreaksObeyTheLimits },
        { "budgets_cut_at_a_codepoint", BudgetsCutAtACodepoint },
        { "limits_judge_the_result", LimitsJudgeTheResult },
        { "forbidden_codepoints_and_the_filter_drop_what_they_refuse", ForbiddenCodepointsAndTheFilterDropWhatTheyRefuse },
        { "the_validator_judges_the_whole_result", TheValidatorJudgesTheWholeResult },
        { "a_callback_cannot_change_its_text", ACallbackCannotChangeItsText },
        { "hostile_pastes_stay_bounded", HostilePastesStayBounded },
        { "limits_and_options_are_checked", LimitsAndOptionsAreChecked },
        { "typing_replaces_the_selection", TypingReplacesTheSelection },
        { "overwriting_never_takes_a_line_break", OverwritingNeverTakesALineBreak },
        { "deleting_by_character_selection_and_word", DeletingByCharacterSelectionAndWord },
        { "left_and_right_collapse_a_selection", LeftAndRightCollapseASelection },
        { "words_stop_at_symbols_spaces_and_line_breaks", WordsStopAtSymbolsSpacesAndLineBreaks },
        { "lines_span_their_line_breaks", LinesSpanTheirLineBreaks },
        { "offsets_stay_on_codepoint_boundaries", OffsetsStayOnCodepointBoundaries },
        { "typing_groups_into_one_step", TypingGroupsIntoOneStep },
        { "a_pause_ends_a_group_at_its_edge", APauseEndsAGroupAtItsEdge },
        { "a_group_holds_its_count_of_characters", AGroupHoldsItsCountOfCharacters },
        { "a_caret_move_ends_a_group", ACaretMoveEndsAGroup },
        { "a_change_of_kind_ends_a_group_and_backspaces_group", AChangeOfKindEndsAGroupAndBackspacesGroup },
        { "typing_over_a_selection_starts_a_group", TypingOverASelectionStartsAGroup },
        { "only_contiguous_edits_of_a_kind_join", OnlyContiguousEditsOfAKindJoin },
        { "pastes_cuts_and_words_are_steps_of_their_own", PastesCutsAndWordsAreStepsOfTheirOwn },
        { "an_edit_clears_what_could_be_redone", AnEditClearsWhatCouldBeRedone },
        { "the_step_cap_drops_the_oldest", TheStepCapDropsTheOldest },
        { "the_budget_drops_the_oldest", TheBudgetDropsTheOldest },
        { "code_and_new_limits_empty_the_list", CodeAndNewLimitsEmptyTheList },
        { "a_mark_reverts_by_undoing", AMarkRevertsByUndoing },
        { "a_lost_mark_falls_back_to_the_text", ALostMarkFallsBackToTheText },
    };
    TestRun_RunSuite(run, "UITextEdit", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
