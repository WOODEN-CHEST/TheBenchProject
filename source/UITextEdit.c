#include "UITextEdit.h"
#include "wr/WRChar.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Macros.
/** The first codepoint that is no C0 control character, DEL, and the C1 controls: what never goes into a text (but '\n' and '\t'). */
#define FIRST_PRINTABLE ((uint32_t)0x20U)
#define DELETE_CODEPOINT ((uint32_t)0x7FU)
#define C1_FIRST ((uint32_t)0x80U)
#define C1_LAST ((uint32_t)0x9FU)
/** How many input bytes per byte of room left an edit reads at most: what keeps a hostile paste bounded (dropped control characters and
 *  "\r\n" pairs read more than they write; U+FFFD writes more). */
#define SCAN_BYTES_PER_ROOM ((size_t)4)
#define SCAN_SLACK_BYTES ((size_t)16)


// Types.
/* One undo step: the bytes it replaced at Start (removed, then inserted, kept in _undoBytes at BytesOffset) and the selection around it. */
typedef struct UITextEditStepStruct
{
    size_t Start;
    size_t RemovedLength;
    size_t InsertedLength;
    size_t BytesOffset;
    size_t CaretBefore;
    size_t AnchorBefore;
    size_t CaretAfter;
    size_t AnchorAfter;
    UITextEditKind Kind;
    size_t GroupCharacters;
    double Time;
} UITextEditStep;

/* What an edit may still add: bytes, codepoints and line breaks. */
typedef struct EditBudgetStruct
{
    size_t Bytes;
    bool HasCharacters;
    size_t Characters;
    bool HasNewlines;
    size_t Newlines;
} EditBudget;


// Static functions: UTF-8.
static bool IsContinuationByte(unsigned char byte)
{
    return (byte & 0xC0U) == 0x80U;
}

/* Decodes the codepoint at text[0..remaining). False for bytes that are no valid UTF-8: then one byte counts. */
static bool DecodeAt(const unsigned char* text, size_t remaining, uint32_t* outCodepoint, size_t* outByteCount)
{
    *outByteCount = 1U;
    *outCodepoint = UI_TEXT_EDIT_REPLACEMENT_CODEPOINT;
    if ((remaining == 0U) || !CharUTF8_IsCharBufferValid(text, remaining))
    {
        return false;
    }
    unsigned char Lead = text[0];
    if (Lead < 0x80U)
    {
        *outCodepoint = Lead;
        return true;
    }
    size_t Count = (Lead >= 0xF0U) ? 4U : ((Lead >= 0xE0U) ? 3U : 2U);
    uint32_t Value = (uint32_t)Lead & ((Count == 2U) ? 0x1FU : ((Count == 3U) ? 0x0FU : 0x07U));
    for (size_t Index = 1U; Index < Count; Index++)
    {
        Value = (Value << 6U) | ((uint32_t)text[Index] & 0x3FU);
    }
    *outCodepoint = Value;
    *outByteCount = Count;
    return true;
}

/* WRChar takes codepoints as ints: a value past INT32_MAX is none. */
static CodePoint ToCodePoint(uint32_t value)
{
    return (value <= (uint32_t)INT32_MAX) ? (CodePoint)value : CODEPOINT_NONE;
}

static bool IsControlCharacter(uint32_t codepoint)
{
    return (codepoint < FIRST_PRINTABLE) || (codepoint == DELETE_CODEPOINT) || ((codepoint >= C1_FIRST) && (codepoint <= C1_LAST));
}

/* Counts the codepoints and the line breaks of valid UTF-8. */
static void CountRange(const unsigned char* text, size_t length, size_t* outCharacters, size_t* outNewlines)
{
    size_t Characters = 0U;
    size_t Newlines = 0U;
    for (size_t Index = 0U; Index < length; Index++)
    {
        if (!IsContinuationByte(text[Index]))
        {
            Characters++;
        }
        if (text[Index] == (unsigned char)'\n')
        {
            Newlines++;
        }
    }
    *outCharacters = Characters;
    *outNewlines = Newlines;
}

static bool AreBytesEqual(const unsigned char* a, const unsigned char* b, size_t length)
{
    return (length == 0U) || Memory_IsEqual(a, b, length);
}


// Static functions: the text buffer (always valid UTF-8 with a counted NUL after it).
static unsigned char* GetTextBytes(UITextEdit* self)
{
    return (unsigned char*)self->_text._data;
}

static Error GrowthError(void)
{
    return Error_Construct2(ErrorCode_BufferTooLarge, "UITextEdit: a buffer could not grow.");
}

/* Replaces [start, start + removed) with @p inserted bytes, keeping the counts. @p bytes never lies in the text itself. */
static Error RawReplace(UITextEdit* self, size_t start, size_t removed, const unsigned char* bytes, size_t inserted)
{
    size_t RemovedCharacters = 0U;
    size_t RemovedNewlines = 0U;
    CountRange(GetTextBytes(self) + start, removed, &RemovedCharacters, &RemovedNewlines);
    if (!GenericBuffer_RemoveRange(&self->_text, start, start + removed))
    {
        return GrowthError();
    }
    // InsertRange only reads its source; the API is just not declared const.
    if ((inserted > 0U) && !GenericBuffer_InsertRange(&self->_text, (void*)bytes, inserted, start))
    {
        return GrowthError();
    }
    size_t InsertedCharacters = 0U;
    size_t InsertedNewlines = 0U;
    CountRange(bytes, inserted, &InsertedCharacters, &InsertedNewlines);
    self->_characterCount = (self->_characterCount - RemovedCharacters) + InsertedCharacters;
    self->_newlineCount = (self->_newlineCount - RemovedNewlines) + InsertedNewlines;
    return Error_CreateSuccess();
}


// Static functions: what comes in.
static bool IsForbidden(UITextEdit* self, uint32_t codepoint)
{
    const UITextEditLimits* Limits = &self->_limits;
    for (size_t Index = 0U; Index < Limits->ForbiddenCount; Index++)
    {
        if (Limits->ForbiddenCodepoints[Index] == codepoint)
        {
            return true;
        }
    }
    if (Limits->Filter == NULL)
    {
        return false;
    }
    self->_isInCallback = true;
    bool IsKept = Limits->Filter(codepoint, &Limits->FilterData);
    self->_isInCallback = false;
    return !IsKept;
}

/* What an edit attempted while the filter or the validator runs gets: they must not change the text they judge. */
static Error CallbackError(void)
{
    return Error_Construct2(ErrorCode_InvalidOperation, "UITextEdit: no edit while its filter or validator runs.");
}

/* The room an edit replacing [start, end) has under the limits: what the rest of the text leaves. */
static EditBudget GetBudget(const UITextEdit* self, size_t start, size_t end)
{
    const UITextEditLimits* Limits = &self->_limits;
    size_t RemovedCharacters = 0U;
    size_t RemovedNewlines = 0U;
    CountRange(UITextEdit_GetText(self) + start, end - start, &RemovedCharacters, &RemovedNewlines);
    size_t KeptBytes = UITextEdit_GetLength(self) - (end - start);
    size_t KeptCharacters = self->_characterCount - RemovedCharacters;
    size_t KeptNewlines = self->_newlineCount - RemovedNewlines;
    size_t AllowedNewlines = Limits->HasMaxLines ? (Limits->MaxLines - 1U) : 0U;
    return (EditBudget)
    {
        .Bytes = (Limits->MaxBytes > KeptBytes) ? (Limits->MaxBytes - KeptBytes) : 0U,
        .HasCharacters = Limits->HasMaxCharacters,
        .Characters = (Limits->MaxCharacters > KeptCharacters) ? (Limits->MaxCharacters - KeptCharacters) : 0U,
        .HasNewlines = Limits->HasMaxLines,
        .Newlines = (AllowedNewlines > KeptNewlines) ? (AllowedNewlines - KeptNewlines) : 0U
    };
}

/* Puts into _incoming what of the text may come in under the budget (see the file comment); *outIsRefused tells whether anything was
 * cut or dropped by a limit. Reads at most a few bytes per byte of room, so a hostile paste stays bounded. */
static Error FilterIncoming(UITextEdit* self, const unsigned char* text, size_t length, EditBudget budget, bool* outIsRefused)
{
    GenericBuffer_Clear(&self->_incoming); // Its own scratch: nothing of an earlier edit belongs in it.
    *outIsRefused = false;
    size_t ScanLimit = (budget.Bytes > ((SIZE_MAX - SCAN_SLACK_BYTES) / SCAN_BYTES_PER_ROOM)) ? SIZE_MAX
        : ((budget.Bytes * SCAN_BYTES_PER_ROOM) + SCAN_SLACK_BYTES);
    size_t Index = 0U;
    while (Index < length)
    {
        if (Index >= ScanLimit)
        {
            *outIsRefused = true;
            break;
        }
        uint32_t Codepoint = 0U;
        size_t ByteCount = 0U;
        DecodeAt(text + Index, length - Index, &Codepoint, &ByteCount);
        Index += ByteCount;
        if (Codepoint == (uint32_t)'\r')
        {
            Index += ((Index < length) && (text[Index] == (unsigned char)'\n')) ? 1U : 0U;
            Codepoint = (uint32_t)'\n';
        }
        if (IsControlCharacter(Codepoint) && (Codepoint != (uint32_t)'\n') && (Codepoint != (uint32_t)'\t'))
        {
            continue;
        }
        bool HasNoLineLeft = budget.HasNewlines && (budget.Newlines == 0U);
        if ((Codepoint == (uint32_t)'\n') && (!self->_limits.AllowsNewlines || HasNoLineLeft))
        {
            Codepoint = (uint32_t)' ';
        }
        if (IsForbidden(self, Codepoint))
        {
            *outIsRefused = true;
            continue;
        }
        size_t Encoded = CharUTF8_GetByteCountCodepoint(ToCodePoint(Codepoint));
        if ((Encoded > budget.Bytes) || (budget.HasCharacters && (budget.Characters == 0U)))
        {
            *outIsRefused = true;
            break;
        }
        unsigned char Bytes[4];
        CharUTF8_WriteCodePoint(Bytes, ToCodePoint(Codepoint));
        if (!GenericBuffer_AppendRangeBytes(&self->_incoming, Bytes, Encoded))
        {
            return GrowthError();
        }
        budget.Bytes -= Encoded;
        budget.Characters -= budget.HasCharacters ? 1U : 0U;
        budget.Newlines -= ((Codepoint == (uint32_t)'\n') && budget.HasNewlines) ? 1U : 0U;
    }
    return Error_CreateSuccess();
}

/* Whether the validator, when there is one, accepts the text the edit would leave. */
static Error IsAccepted(UITextEdit* self, size_t start, size_t end, bool* outIsAccepted)
{
    *outIsAccepted = true;
    if (self->_limits.Validator == NULL)
    {
        return Error_CreateSuccess();
    }
    GenericBuffer_Clear(&self->_candidate); // Its own scratch.
    const unsigned char* Text = UITextEdit_GetText(self);
    size_t Length = UITextEdit_GetLength(self);
    // AppendRangeBytes only reads its source; the API is just not declared const.
    bool IsBuilt = GenericBuffer_AppendRangeBytes(&self->_candidate, (unsigned char*)Text, start)
        && GenericBuffer_AppendRangeBytes(&self->_candidate, self->_incoming._data, self->_incoming._count)
        && GenericBuffer_AppendRangeBytes(&self->_candidate, (unsigned char*)Text + end, Length - end)
        && GenericBuffer_AppendByte(&self->_candidate, 0U);
    if (!IsBuilt)
    {
        return GrowthError();
    }
    self->_isInCallback = true;
    *outIsAccepted = self->_limits.Validator((const unsigned char*)self->_candidate._data, self->_candidate._count - 1U,
        &self->_limits.FilterData);
    self->_isInCallback = false;
    return Error_CreateSuccess();
}


// Static functions: the undo list.
static UITextEditStep* GetStep(UITextEdit* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_steps, index);
}

static const unsigned char* GetUndoBytes(const UITextEdit* self, size_t offset)
{
    return (const unsigned char*)self->_undoBytes._data + offset;
}

/* Drops the steps that could be redone, with their bytes; a mark among them can no longer be reached. */
static void DropRedoSteps(UITextEdit* self)
{
    if (self->_undoCount >= self->_steps._count)
    {
        return;
    }
    size_t BytesStart = GetStep(self, self->_undoCount)->BytesOffset;
    GenericBuffer_RemoveRange(&self->_undoBytes, BytesStart, self->_undoBytes._count);
    GenericBuffer_RemoveRange(&self->_steps, self->_undoCount, self->_steps._count);
    if (self->_hasMark && (self->_mark > self->_undoCount))
    {
        self->_hasMark = false;
    }
}

/* Drops the oldest step and its bytes; a mark before it can no longer be reached. */
static void DropOldestStep(UITextEdit* self)
{
    UITextEditStep* Oldest = GetStep(self, 0U);
    size_t Bytes = Oldest->RemovedLength + Oldest->InsertedLength;
    GenericBuffer_RemoveRange(&self->_undoBytes, 0U, Bytes);
    GenericBuffer_RemoveFirst(&self->_steps);
    for (size_t Index = 0U; Index < self->_steps._count; Index++)
    {
        GetStep(self, Index)->BytesOffset -= Bytes;
    }
    self->_undoCount -= (self->_undoCount > 0U) ? 1U : 0U;
    if (self->_hasMark)
    {
        if (self->_mark == 0U)
        {
            self->_hasMark = false;
        }
        else
        {
            self->_mark--;
        }
    }
}

/* Keeps the list within its step count and its budget, oldest first: a step larger than the whole budget goes too. */
static void EnforceUndoLimits(UITextEdit* self)
{
    while ((self->_steps._count > 0U)
        && ((self->_steps._count > UI_TEXT_EDIT_MAX_UNDO_STEPS) || (UITextEdit_GetUndoMemory(self) > self->_undoOptions.BudgetBytes)))
    {
        DropOldestStep(self);
    }
    if (self->_steps._count == 0U)
    {
        self->_isGroupOpen = false;
        // A step larger than the whole budget emptied the list: the room it took is let go.
        if (self->_undoBytes._capacity > self->_undoOptions.BudgetBytes)
        {
            Memory_Free(self->_undoBytes._data);
            GenericBuffer_AllocateVariable(&self->_undoBytes, 0U, sizeof(unsigned char));
        }
    }
}

/* Whether an edit of @p kind replacing [start, start + removed) with @p inserted bytes at @p time joins the last step. */
static bool CanJoinLastStep(UITextEdit* self, UITextEditKind kind, size_t start, size_t removed, size_t inserted, double time)
{
    if (!self->_isGroupOpen || (self->_undoCount == 0U) || (self->_undoCount != self->_steps._count))
    {
        return false;
    }
    const UITextEditStep* Last = GetStep(self, self->_undoCount - 1U);
    bool IsSoonEnough = Math_AbsDouble(time - Last->Time) <= self->_undoOptions.PauseSeconds;
    if ((Last->Kind != kind) || !IsSoonEnough || (Last->GroupCharacters >= self->_undoOptions.GroupCharacters))
    {
        return false;
    }
    switch (kind)
    {
        case UITextEditKind_Typing:
            return (removed == 0U) && (start == (Last->Start + Last->InsertedLength));
        case UITextEditKind_Overwriting:
            return start == (Last->Start + Last->InsertedLength);
        case UITextEditKind_DeletingBackward:
            return (inserted == 0U) && (Last->InsertedLength == 0U) && ((start + removed) == Last->Start);
        case UITextEditKind_DeletingForward:
            return (inserted == 0U) && (Last->InsertedLength == 0U) && (start == Last->Start);
        case UITextEditKind_Whole:
            return false; // A whole change never joins: a paste, a cut, a word deleted, an edit by code.
    }
    return false;
}

/* Records an edit about to be made (its removed bytes still in the text), joining the last step when the grouping rules say so. */
static Error RecordEdit(UITextEdit* self, UITextEditKind kind, size_t start, size_t removed, const unsigned char* inserted,
    size_t insertedLength, double time)
{
    DropRedoSteps(self);
    unsigned char* RemovedBytes = GetTextBytes(self) + start;
    size_t CaretAfter = start + insertedLength;
    if (CanJoinLastStep(self, kind, start, removed, insertedLength, time))
    {
        UITextEditStep* Last = GetStep(self, self->_undoCount - 1U);
        // Typing and overwriting: what they removed follows the step's removed bytes, and what they inserted its inserted ones.
        // Deleting backward: what it removed comes before the step's; forward: after it. The step's bytes are the store's last.
        size_t RemovedAt = (kind == UITextEditKind_DeletingBackward) ? Last->BytesOffset : (Last->BytesOffset + Last->RemovedLength);
        bool IsJoined = GenericBuffer_InsertRange(&self->_undoBytes, RemovedBytes, removed, RemovedAt)
            && ((insertedLength == 0U) || GenericBuffer_AppendRangeBytes(&self->_undoBytes, (unsigned char*)inserted, insertedLength));
        if (!IsJoined)
        {
            return GrowthError();
        }
        Last->Start = (kind == UITextEditKind_DeletingBackward) ? start : Last->Start;
        Last->RemovedLength += removed;
        Last->InsertedLength += insertedLength;
        Last->CaretAfter = CaretAfter;
        Last->AnchorAfter = CaretAfter;
        Last->GroupCharacters++;
        Last->Time = time;
        return Error_CreateSuccess();
    }

    UITextEditStep Step =
    {
        .Start = start,
        .RemovedLength = removed,
        .InsertedLength = insertedLength,
        .BytesOffset = self->_undoBytes._count,
        .CaretBefore = self->_caret,
        .AnchorBefore = self->_anchor,
        .CaretAfter = CaretAfter,
        .AnchorAfter = CaretAfter,
        .Kind = kind,
        .GroupCharacters = 1U,
        .Time = time
    };
    // AppendRangeBytes only reads its source; the API is just not declared const.
    bool IsAdded = GenericBuffer_AppendRangeBytes(&self->_undoBytes, RemovedBytes, removed)
        && ((insertedLength == 0U) || GenericBuffer_AppendRangeBytes(&self->_undoBytes, (unsigned char*)inserted, insertedLength))
        && GenericBuffer_AddLast(&self->_steps, &Step);
    if (!IsAdded)
    {
        return GrowthError();
    }
    self->_undoCount = self->_steps._count;
    self->_isGroupOpen = kind != UITextEditKind_Whole;
    return Error_CreateSuccess();
}

static void SetChange(UITextEditChange* outChange, bool hasChanged, bool isRefused, size_t start, size_t removed, size_t inserted)
{
    if (outChange != NULL)
    {
        *outChange = (UITextEditChange){ .HasChanged = hasChanged, .IsRefused = isRefused, .Start = start, .RemovedLength = removed,
            .InsertedLength = inserted };
    }
}


// Static functions: the caret.
static void SetCaretAndAnchor(UITextEdit* self, size_t caret, size_t anchor)
{
    const unsigned char* Text = UITextEdit_GetText(self);
    size_t Length = UITextEdit_GetLength(self);
    self->_caret = UITextEdit_SnapToBoundary(Text, Length, caret);
    self->_anchor = UITextEdit_SnapToBoundary(Text, Length, anchor);
}


// Public functions: helpers.
UITextEditLimits UITextEditLimits_Create(void)
{
    UITextEditLimits Limits;
    Memory_Zero(&Limits, sizeof(Limits));
    Limits.MaxBytes = UI_TEXT_EDIT_DEFAULT_MAX_BYTES;
    Limits.HasMaxCharacters = false;
    Limits.MaxCharacters = 0U;
    Limits.HasMaxLines = false;
    Limits.MaxLines = 1U;
    Limits.AllowsNewlines = true;
    Limits.ForbiddenCount = 0U;
    Limits.Filter = NULL;
    Limits.Validator = NULL;
    Limits.FilterData = UserData_CreateEmpty();
    return Limits;
}

bool UITextEditLimits_IsValid(const UITextEditLimits* limits)
{
    if ((limits->MaxBytes > UI_TEXT_EDIT_MAX_BYTES) || (limits->HasMaxLines && (limits->MaxLines == 0U))
        || (limits->ForbiddenCount > UI_TEXT_EDIT_MAX_FORBIDDEN))
    {
        return false;
    }
    for (size_t Index = 0U; Index < limits->ForbiddenCount; Index++)
    {
        if (!CharUTF8_IsCodePointValid(ToCodePoint(limits->ForbiddenCodepoints[Index])))
        {
            return false;
        }
    }
    return true;
}

UITextEditUndoOptions UITextEditUndoOptions_Create(void)
{
    return (UITextEditUndoOptions){ .BudgetBytes = UI_TEXT_EDIT_DEFAULT_UNDO_BUDGET, .PauseSeconds = UI_TEXT_EDIT_DEFAULT_UNDO_PAUSE_SECONDS,
        .GroupCharacters = UI_TEXT_EDIT_DEFAULT_UNDO_GROUP_CHARACTERS };
}

bool UITextEditUndoOptions_IsValid(const UITextEditUndoOptions* options)
{
    return (options->BudgetBytes >= 1U) && (options->BudgetBytes <= UI_TEXT_EDIT_MAX_UNDO_BUDGET) && (options->PauseSeconds >= 0.0)
        && (options->PauseSeconds <= UI_TEXT_EDIT_MAX_UNDO_PAUSE_SECONDS) && (options->GroupCharacters >= 1U);
}


// Public functions: lifecycle.
Error UITextEdit_Construct(UITextEdit* self, const UITextEditLimits* limits, const UITextEditUndoOptions* undoOptions)
{
    if ((self == NULL) || (limits == NULL) || (undoOptions == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextEdit_Construct: self, limits and undoOptions must not be NULL.");
    }
    if (!UITextEditLimits_IsValid(limits) || !UITextEditUndoOptions_IsValid(undoOptions))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITextEdit_Construct: the limits or the undo options are not valid.");
    }
    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->_text, 16U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&self->_steps, 0U, sizeof(UITextEditStep));
    GenericBuffer_AllocateVariable(&self->_undoBytes, 0U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&self->_incoming, 0U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&self->_candidate, 0U, sizeof(unsigned char));
    GenericBuffer_AppendByte(&self->_text, 0U);
    self->_limits = *limits;
    self->_undoOptions = *undoOptions;
    return Error_CreateSuccess();
}

void UITextEdit_Deconstruct(UITextEdit* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->_text._data);
    Memory_Free(self->_steps._data);
    Memory_Free(self->_undoBytes._data);
    Memory_Free(self->_incoming._data);
    Memory_Free(self->_candidate._data);
    Memory_Zero(self, sizeof(*self));
}


// Public functions: reading.
void UITextEdit_GetSelection(const UITextEdit* self, size_t* outStart, size_t* outEnd)
{
    *outStart = (self->_caret < self->_anchor) ? self->_caret : self->_anchor;
    *outEnd = (self->_caret < self->_anchor) ? self->_anchor : self->_caret;
}

size_t UITextEdit_GetMostIncomingBytes(const UITextEdit* self)
{
    size_t MaxBytes = self->_limits.MaxBytes;
    return (MaxBytes > ((SIZE_MAX - SCAN_SLACK_BYTES) / SCAN_BYTES_PER_ROOM)) ? SIZE_MAX : ((MaxBytes * SCAN_BYTES_PER_ROOM) + SCAN_SLACK_BYTES);
}

size_t UITextEdit_GetUndoMemory(const UITextEdit* self)
{
    return (self->_steps._count * sizeof(UITextEditStep)) + self->_undoBytes._count;
}


// Public functions: code.
Error UITextEdit_SetText(UITextEdit* self, const unsigned char* text, size_t length, UITextEditChange* outChange)
{
    SetChange(outChange, false, false, 0U, 0U, 0U);
    if (self->_isInCallback)
    {
        return CallbackError();
    }
    if ((text == NULL) && (length > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextEdit_SetText: text must not be NULL with a length.");
    }
    size_t OldLength = UITextEdit_GetLength(self);
    EditBudget Budget = GetBudget(self, 0U, OldLength);
    bool IsRefused = false;
    Error Result = FilterIncoming(self, text, length, Budget, &IsRefused);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsAcceptedResult = true;
    Result = IsAccepted(self, 0U, OldLength, &IsAcceptedResult);
    if ((Result.Code != ErrorCode_Success) || !IsAcceptedResult)
    {
        SetChange(outChange, false, true, 0U, 0U, 0U);
        return Result;
    }
    bool IsSame = (self->_incoming._count == OldLength) && AreBytesEqual(self->_incoming._data, UITextEdit_GetText(self), OldLength);
    if (!IsSame)
    {
        Result = RawReplace(self, 0U, OldLength, self->_incoming._data, self->_incoming._count);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    UITextEdit_ClearUndo(self);
    self->_caret = UITextEdit_GetLength(self);
    self->_anchor = self->_caret;
    SetChange(outChange, !IsSame, IsRefused, 0U, OldLength, UITextEdit_GetLength(self));
    return Error_CreateSuccess();
}

Error UITextEdit_SetTextVerbatim(UITextEdit* self, const unsigned char* text, size_t length)
{
    if (self->_isInCallback)
    {
        return CallbackError();
    }
    if ((text == NULL) && (length > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextEdit_SetTextVerbatim: text must not be NULL with a length.");
    }
    for (size_t Index = 0U; Index < length;)
    {
        uint32_t Codepoint = 0U;
        size_t Count = 0U;
        if (!DecodeAt(text + Index, length - Index, &Codepoint, &Count) || (Codepoint == 0U))
        {
            return Error_Construct2(ErrorCode_InvalidTextEncoding, "UITextEdit_SetTextVerbatim: the text is not valid UTF-8, or holds a NUL.");
        }
        Index += Count;
    }
    Error Result = RawReplace(self, 0U, UITextEdit_GetLength(self), text, length);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UITextEdit_ClearUndo(self);
    self->_caret = 0U;
    self->_anchor = 0U;
    return Error_CreateSuccess();
}

Error UITextEdit_SetLimits(UITextEdit* self, const UITextEditLimits* limits, UITextEditChange* outChange)
{
    SetChange(outChange, false, false, 0U, 0U, 0U);
    if (self->_isInCallback)
    {
        return CallbackError();
    }
    if (limits == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextEdit_SetLimits: limits must not be NULL.");
    }
    if (!UITextEditLimits_IsValid(limits))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITextEdit_SetLimits: the limits are not valid.");
    }
    self->_limits = *limits;
    size_t Caret = self->_caret;
    size_t Anchor = self->_anchor;
    size_t OldLength = UITextEdit_GetLength(self);
    // The text through the new limits as anything that comes in; the validator does not judge text already there.
    UITextEditValidator Validator = self->_limits.Validator;
    self->_limits.Validator = NULL;
    // Copied out first: the filter writes into scratch, not the text, but the source must not move under it.
    GenericBuffer_Clear(&self->_candidate);
    bool IsCopied = GenericBuffer_AppendRangeBytes(&self->_candidate, self->_text._data, OldLength);
    Error Result = IsCopied ? Error_CreateSuccess() : GrowthError();
    if (Result.Code == ErrorCode_Success)
    {
        // The budget of a whole replacement under the new limits: every byte of room.
        EditBudget Budget = { .Bytes = limits->MaxBytes, .HasCharacters = limits->HasMaxCharacters, .Characters = limits->MaxCharacters,
            .HasNewlines = limits->HasMaxLines, .Newlines = limits->HasMaxLines ? (limits->MaxLines - 1U) : 0U };
        bool IsRefused = false;
        Result = FilterIncoming(self, self->_candidate._data, OldLength, Budget, &IsRefused);
        bool IsSame = (Result.Code == ErrorCode_Success) && (self->_incoming._count == OldLength)
            && AreBytesEqual(self->_incoming._data, self->_candidate._data, OldLength);
        if ((Result.Code == ErrorCode_Success) && !IsSame)
        {
            Result = RawReplace(self, 0U, OldLength, self->_incoming._data, self->_incoming._count);
        }
        SetChange(outChange, !IsSame, IsRefused, 0U, OldLength, UITextEdit_GetLength(self));
    }
    self->_limits.Validator = Validator;
    UITextEdit_ClearUndo(self);
    SetCaretAndAnchor(self, Caret, Anchor);
    return Result;
}

Error UITextEdit_SetUndoOptions(UITextEdit* self, const UITextEditUndoOptions* options)
{
    if (self->_isInCallback)
    {
        return CallbackError();
    }
    if (options == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextEdit_SetUndoOptions: options must not be NULL.");
    }
    if (!UITextEditUndoOptions_IsValid(options))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITextEdit_SetUndoOptions: the options are not valid.");
    }
    self->_undoOptions = *options;
    UITextEdit_ClearUndo(self);
    return Error_CreateSuccess();
}


// Public functions: edits.
Error UITextEdit_Replace(UITextEdit* self, size_t start, size_t end, const unsigned char* text, size_t length, UITextEditKind kind,
    double time, UITextEditChange* outChange)
{
    SetChange(outChange, false, false, 0U, 0U, 0U);
    if (self->_isInCallback)
    {
        return CallbackError();
    }
    if (((text == NULL) && (length > 0U)) || (end < start))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextEdit_Replace: a NULL text with a length, or an end before the start.");
    }
    if (((int32_t)kind < (int32_t)UITextEditKind_Typing) || ((int32_t)kind > (int32_t)UITextEditKind_Whole))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITextEdit_Replace: unknown kind of edit.");
    }
    const unsigned char* Current = UITextEdit_GetText(self);
    size_t Length = UITextEdit_GetLength(self);
    size_t Start = UITextEdit_SnapToBoundary(Current, Length, start);
    size_t End = UITextEdit_SnapToBoundary(Current, Length, end);
    bool IsRefused = false;
    Error Result = FilterIncoming(self, text, length, GetBudget(self, Start, End), &IsRefused);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Inserted = self->_incoming._count;
    // Text came in and none of it is left: the edit changes nothing (typing a codepoint that does not fit never deletes what it was
    // to replace).
    bool HasLostEverything = (length > 0U) && (Inserted == 0U);
    bool IsSame = HasLostEverything || ((Inserted == (End - Start)) && AreBytesEqual(self->_incoming._data, Current + Start, Inserted));
    if (IsSame)
    {
        SetChange(outChange, false, IsRefused, Start, 0U, 0U);
        return Error_CreateSuccess();
    }
    bool IsAcceptedResult = true;
    Result = IsAccepted(self, Start, End, &IsAcceptedResult);
    if ((Result.Code != ErrorCode_Success) || !IsAcceptedResult)
    {
        SetChange(outChange, false, true, Start, 0U, 0U);
        return Result;
    }
    Result = RecordEdit(self, kind, Start, End - Start, self->_incoming._data, Inserted, time);
    if (Result.Code == ErrorCode_Success)
    {
        Result = RawReplace(self, Start, End - Start, self->_incoming._data, Inserted);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_caret = Start + Inserted;
    self->_anchor = self->_caret;
    EnforceUndoLimits(self);
    SetChange(outChange, true, IsRefused, Start, End - Start, Inserted);
    return Error_CreateSuccess();
}

Error UITextEdit_ReplaceSelection(UITextEdit* self, const unsigned char* text, size_t length, UITextEditKind kind, double time,
    UITextEditChange* outChange)
{
    size_t Start = 0U;
    size_t End = 0U;
    UITextEdit_GetSelection(self, &Start, &End);
    return UITextEdit_Replace(self, Start, End, text, length, kind, time, outChange);
}

Error UITextEdit_TypeCodepoint(UITextEdit* self, uint32_t codepoint, bool isOverwriting, double time, UITextEditChange* outChange)
{
    SetChange(outChange, false, false, 0U, 0U, 0U);
    unsigned char Bytes[4];
    size_t Count = CharUTF8_WriteCodePoint(Bytes, ToCodePoint(codepoint));
    if (Count == 0U)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITextEdit_TypeCodepoint: the value is no codepoint.");
    }
    UITextEditKind Kind = isOverwriting ? UITextEditKind_Overwriting : UITextEditKind_Typing;
    // A line break typed where none may go (Enter at the line limit) is refused, not typed as the space text that comes in becomes.
    if (codepoint == (uint32_t)'\n')
    {
        size_t Start = 0U;
        size_t End = 0U;
        UITextEdit_GetSelection(self, &Start, &End);
        EditBudget Budget = GetBudget(self, Start, End);
        if (!self->_limits.AllowsNewlines || (Budget.HasNewlines && (Budget.Newlines == 0U)))
        {
            SetChange(outChange, false, true, Start, 0U, 0U);
            return Error_CreateSuccess();
        }
    }
    if (UITextEdit_HasSelection(self))
    {
        return UITextEdit_ReplaceSelection(self, Bytes, Count, Kind, time, outChange);
    }
    const unsigned char* Text = UITextEdit_GetText(self);
    size_t Length = UITextEdit_GetLength(self);
    size_t End = self->_caret;
    // Overwriting replaces the character after the caret, never a line break, and nothing at the end.
    if (isOverwriting && (self->_caret < Length) && (Text[self->_caret] != (unsigned char)'\n'))
    {
        End = UITextEdit_GetNextBoundary(Text, Length, self->_caret);
    }
    return UITextEdit_Replace(self, self->_caret, End, Bytes, Count, Kind, time, outChange);
}

Error UITextEdit_DeleteBackward(UITextEdit* self, bool isByWord, bool isMasked, double time, UITextEditChange* outChange)
{
    if (UITextEdit_HasSelection(self))
    {
        return UITextEdit_ReplaceSelection(self, NULL, 0U, UITextEditKind_DeletingBackward, time, outChange);
    }
    const unsigned char* Text = UITextEdit_GetText(self);
    size_t Length = UITextEdit_GetLength(self);
    size_t Start = isByWord ? UITextEdit_FindWordStart(Text, Length, self->_caret, isMasked)
        : UITextEdit_GetPreviousBoundary(Text, Length, self->_caret);
    return UITextEdit_Replace(self, Start, self->_caret, NULL, 0U, isByWord ? UITextEditKind_Whole : UITextEditKind_DeletingBackward, time,
        outChange);
}

Error UITextEdit_DeleteForward(UITextEdit* self, bool isByWord, bool isMasked, double time, UITextEditChange* outChange)
{
    if (UITextEdit_HasSelection(self))
    {
        return UITextEdit_ReplaceSelection(self, NULL, 0U, UITextEditKind_DeletingForward, time, outChange);
    }
    const unsigned char* Text = UITextEdit_GetText(self);
    size_t Length = UITextEdit_GetLength(self);
    size_t End = isByWord ? UITextEdit_FindWordEnd(Text, Length, self->_caret, isMasked)
        : UITextEdit_GetNextBoundary(Text, Length, self->_caret);
    return UITextEdit_Replace(self, self->_caret, End, NULL, 0U, isByWord ? UITextEditKind_Whole : UITextEditKind_DeletingForward, time,
        outChange);
}


// Public functions: the caret and the selection.
void UITextEdit_SetCaret(UITextEdit* self, size_t offset, bool isExtending)
{
    SetCaretAndAnchor(self, offset, isExtending ? self->_anchor : offset);
    self->_isGroupOpen = false;
}

void UITextEdit_Select(UITextEdit* self, size_t anchor, size_t caret)
{
    SetCaretAndAnchor(self, caret, anchor);
    self->_isGroupOpen = false;
}

void UITextEdit_SelectAll(UITextEdit* self)
{
    UITextEdit_Select(self, 0U, UITextEdit_GetLength(self));
}

void UITextEdit_MoveLeft(UITextEdit* self, bool isByWord, bool isExtending, bool isMasked)
{
    const unsigned char* Text = UITextEdit_GetText(self);
    size_t Length = UITextEdit_GetLength(self);
    size_t Start = 0U;
    size_t End = 0U;
    UITextEdit_GetSelection(self, &Start, &End);
    size_t From = (UITextEdit_HasSelection(self) && !isExtending) ? Start : self->_caret;
    size_t Target = From;
    if (isByWord)
    {
        Target = UITextEdit_FindWordStart(Text, Length, From, isMasked);
    }
    else if (!UITextEdit_HasSelection(self) || isExtending)
    {
        Target = UITextEdit_GetPreviousBoundary(Text, Length, From);
    }
    UITextEdit_SetCaret(self, Target, isExtending);
}

void UITextEdit_MoveRight(UITextEdit* self, bool isByWord, bool isExtending, bool isMasked)
{
    const unsigned char* Text = UITextEdit_GetText(self);
    size_t Length = UITextEdit_GetLength(self);
    size_t Start = 0U;
    size_t End = 0U;
    UITextEdit_GetSelection(self, &Start, &End);
    size_t From = (UITextEdit_HasSelection(self) && !isExtending) ? End : self->_caret;
    size_t Target = From;
    if (isByWord)
    {
        Target = UITextEdit_FindWordEnd(Text, Length, From, isMasked);
    }
    else if (!UITextEdit_HasSelection(self) || isExtending)
    {
        Target = UITextEdit_GetNextBoundary(Text, Length, From);
    }
    UITextEdit_SetCaret(self, Target, isExtending);
}


// Public functions: undo.
Error UITextEdit_Undo(UITextEdit* self, UITextEditChange* outChange)
{
    if (self->_isInCallback)
    {
        SetChange(outChange, false, false, 0U, 0U, 0U);
        return CallbackError();
    }
    self->_isGroupOpen = false;
    if (self->_undoCount == 0U)
    {
        SetChange(outChange, false, true, 0U, 0U, 0U);
        return Error_CreateSuccess();
    }
    UITextEditStep Step = *GetStep(self, self->_undoCount - 1U);
    Error Result = RawReplace(self, Step.Start, Step.InsertedLength, GetUndoBytes(self, Step.BytesOffset), Step.RemovedLength);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_undoCount--;
    SetCaretAndAnchor(self, Step.CaretBefore, Step.AnchorBefore);
    SetChange(outChange, true, false, Step.Start, Step.InsertedLength, Step.RemovedLength);
    return Error_CreateSuccess();
}

Error UITextEdit_Redo(UITextEdit* self, UITextEditChange* outChange)
{
    if (self->_isInCallback)
    {
        SetChange(outChange, false, false, 0U, 0U, 0U);
        return CallbackError();
    }
    self->_isGroupOpen = false;
    if (self->_undoCount >= self->_steps._count)
    {
        SetChange(outChange, false, true, 0U, 0U, 0U);
        return Error_CreateSuccess();
    }
    UITextEditStep Step = *GetStep(self, self->_undoCount);
    Error Result = RawReplace(self, Step.Start, Step.RemovedLength, GetUndoBytes(self, Step.BytesOffset + Step.RemovedLength),
        Step.InsertedLength);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_undoCount++;
    SetCaretAndAnchor(self, Step.CaretAfter, Step.AnchorAfter);
    SetChange(outChange, true, false, Step.Start, Step.RemovedLength, Step.InsertedLength);
    return Error_CreateSuccess();
}

void UITextEdit_ClearUndo(UITextEdit* self)
{
    GenericBuffer_Clear(&self->_steps);
    GenericBuffer_Clear(&self->_undoBytes);
    self->_undoCount = 0U;
    self->_isGroupOpen = false;
    self->_hasMark = false;
}

void UITextEdit_SetMark(UITextEdit* self)
{
    self->_hasMark = true;
    self->_mark = self->_undoCount;
    self->_isGroupOpen = false;
}

Error UITextEdit_RevertToMark(UITextEdit* self, const unsigned char* fallback, size_t length, UITextEditObserver observer,
    const UserData* userData, bool* outHasChanged)
{
    if (outHasChanged != NULL)
    {
        *outHasChanged = false;
    }
    if (self->_isInCallback)
    {
        return CallbackError();
    }
    if ((fallback == NULL) && (length > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextEdit_RevertToMark: fallback must not be NULL with a length.");
    }
    bool HasChanged = false;
    Error Result = Error_CreateSuccess();
    while ((Result.Code == ErrorCode_Success) && self->_hasMark && (self->_undoCount != self->_mark))
    {
        UITextEditChange Change;
        bool IsBack = self->_undoCount > self->_mark;
        if (!IsBack && !UITextEdit_CanRedo(self))
        {
            break;
        }
        Result = IsBack ? UITextEdit_Undo(self, &Change) : UITextEdit_Redo(self, &Change);
        HasChanged = HasChanged || Change.HasChanged;
        if ((Result.Code == ErrorCode_Success) && (observer != NULL))
        {
            observer(&Change, userData);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsAtFallback = (UITextEdit_GetLength(self) == length) && AreBytesEqual(UITextEdit_GetText(self), fallback, length);
    if (!IsAtFallback)
    {
        UITextEditChange Change;
        Result = UITextEdit_SetText(self, fallback, length, &Change);
        HasChanged = HasChanged || Change.HasChanged;
        if ((Result.Code == ErrorCode_Success) && (observer != NULL))
        {
            observer(&Change, userData);
        }
    }
    if (outHasChanged != NULL)
    {
        *outHasChanged = HasChanged;
    }
    return Result;
}


// Public functions: text helpers.
UITextEditCharacterClass UITextEdit_GetCharacterClass(uint32_t codepoint)
{
    if (codepoint == (uint32_t)'\n')
    {
        return UITextEditCharacterClass_LineBreak;
    }
    if ((codepoint == (uint32_t)' ') || (codepoint == (uint32_t)'\t'))
    {
        return UITextEditCharacterClass_Space;
    }
    if (codepoint < 0x80U)
    {
        bool IsLetterOrDigit = ((codepoint >= (uint32_t)'a') && (codepoint <= (uint32_t)'z')) || ((codepoint >= (uint32_t)'A') && (codepoint <= (uint32_t)'Z'))
            || ((codepoint >= (uint32_t)'0') && (codepoint <= (uint32_t)'9')) || (codepoint == (uint32_t)'_');
        return IsLetterOrDigit ? UITextEditCharacterClass_Word : UITextEditCharacterClass_Symbol;
    }
    // Spaces outside ASCII: the no-break space, the typographic spaces, the narrow and the mathematical ones, the ideographic one.
    bool IsSpace = (codepoint == 0xA0U) || ((codepoint >= 0x2000U) && (codepoint <= 0x200AU)) || (codepoint == 0x202FU)
        || (codepoint == 0x205FU) || (codepoint == 0x3000U);
    if (IsSpace)
    {
        return UITextEditCharacterClass_Space;
    }
    // Punctuation and symbol blocks: Latin-1's, × and ÷, General Punctuation, the symbol blocks from arrows to dingbats and
    // miscellaneous symbols, CJK punctuation, the full-width ASCII punctuation, and the replacement and object characters.
    bool IsSymbol = ((codepoint >= 0xA1U) && (codepoint <= 0xBFU)) || (codepoint == 0xD7U) || (codepoint == 0xF7U)
        || ((codepoint >= 0x2010U) && (codepoint <= 0x2027U)) || ((codepoint >= 0x2030U) && (codepoint <= 0x205EU))
        || ((codepoint >= 0x2190U) && (codepoint <= 0x2BFFU)) || ((codepoint >= 0x3001U) && (codepoint <= 0x3003U))
        || ((codepoint >= 0x3008U) && (codepoint <= 0x3011U)) || ((codepoint >= 0xFF01U) && (codepoint <= 0xFF0FU))
        || ((codepoint >= 0xFF1AU) && (codepoint <= 0xFF20U)) || ((codepoint >= 0xFF3BU) && (codepoint <= 0xFF40U))
        || ((codepoint >= 0xFF5BU) && (codepoint <= 0xFF65U)) || (codepoint == 0xFFFCU) || (codepoint == 0xFFFDU);
    return IsSymbol ? UITextEditCharacterClass_Symbol : UITextEditCharacterClass_Word;
}

size_t UITextEdit_SnapToBoundary(const unsigned char* text, size_t length, size_t offset)
{
    size_t Offset = (offset < length) ? offset : length;
    while ((Offset > 0U) && (Offset < length) && IsContinuationByte(text[Offset]))
    {
        Offset--;
    }
    return Offset;
}

size_t UITextEdit_GetPreviousBoundary(const unsigned char* text, size_t length, size_t offset)
{
    size_t Offset = UITextEdit_SnapToBoundary(text, length, offset);
    if (Offset == 0U)
    {
        return 0U;
    }
    Offset--;
    while ((Offset > 0U) && IsContinuationByte(text[Offset]))
    {
        Offset--;
    }
    return Offset;
}

size_t UITextEdit_GetNextBoundary(const unsigned char* text, size_t length, size_t offset)
{
    size_t Offset = UITextEdit_SnapToBoundary(text, length, offset);
    if (Offset >= length)
    {
        return length;
    }
    Offset++;
    while ((Offset < length) && IsContinuationByte(text[Offset]))
    {
        Offset++;
    }
    return Offset;
}

/* The class of the codepoint that starts at @p offset (a boundary inside the text). */
static UITextEditCharacterClass GetClassAt(const unsigned char* text, size_t length, size_t offset)
{
    uint32_t Codepoint = 0U;
    size_t Count = 0U;
    DecodeAt(text + offset, length - offset, &Codepoint, &Count);
    return UITextEdit_GetCharacterClass(Codepoint);
}

static bool IsSkipped(UITextEditCharacterClass characterClass)
{
    return (characterClass == UITextEditCharacterClass_Space) || (characterClass == UITextEditCharacterClass_LineBreak);
}

size_t UITextEdit_FindWordStart(const unsigned char* text, size_t length, size_t offset, bool isMasked)
{
    size_t Offset = UITextEdit_SnapToBoundary(text, length, offset);
    if (isMasked)
    {
        return 0U;
    }
    while ((Offset > 0U) && IsSkipped(GetClassAt(text, length, UITextEdit_GetPreviousBoundary(text, length, Offset))))
    {
        Offset = UITextEdit_GetPreviousBoundary(text, length, Offset);
    }
    if (Offset == 0U)
    {
        return 0U;
    }
    UITextEditCharacterClass Run = GetClassAt(text, length, UITextEdit_GetPreviousBoundary(text, length, Offset));
    while ((Offset > 0U) && (GetClassAt(text, length, UITextEdit_GetPreviousBoundary(text, length, Offset)) == Run))
    {
        Offset = UITextEdit_GetPreviousBoundary(text, length, Offset);
    }
    return Offset;
}

size_t UITextEdit_FindWordEnd(const unsigned char* text, size_t length, size_t offset, bool isMasked)
{
    size_t Offset = UITextEdit_SnapToBoundary(text, length, offset);
    if (isMasked)
    {
        return length;
    }
    while ((Offset < length) && IsSkipped(GetClassAt(text, length, Offset)))
    {
        Offset = UITextEdit_GetNextBoundary(text, length, Offset);
    }
    if (Offset >= length)
    {
        return length;
    }
    UITextEditCharacterClass Run = GetClassAt(text, length, Offset);
    while ((Offset < length) && (GetClassAt(text, length, Offset) == Run))
    {
        Offset = UITextEdit_GetNextBoundary(text, length, Offset);
    }
    return Offset;
}

void UITextEdit_GetWordAt(const unsigned char* text, size_t length, size_t offset, bool isMasked, size_t* outStart, size_t* outEnd)
{
    size_t Offset = UITextEdit_SnapToBoundary(text, length, offset);
    if (isMasked)
    {
        *outStart = 0U;
        *outEnd = length;
        return;
    }
    *outStart = Offset;
    *outEnd = Offset;
    // The character at the offset, or at a run's end (the text's end, a line break) the one before it.
    bool IsAtBreak = (Offset >= length) || (GetClassAt(text, length, Offset) == UITextEditCharacterClass_LineBreak);
    size_t Probe = Offset;
    if (IsAtBreak)
    {
        if (Offset == 0U)
        {
            return;
        }
        Probe = UITextEdit_GetPreviousBoundary(text, length, Offset);
    }
    UITextEditCharacterClass Run = GetClassAt(text, length, Probe);
    if (Run == UITextEditCharacterClass_LineBreak)
    {
        return;
    }
    size_t Start = Probe;
    while ((Start > 0U) && (GetClassAt(text, length, UITextEdit_GetPreviousBoundary(text, length, Start)) == Run))
    {
        Start = UITextEdit_GetPreviousBoundary(text, length, Start);
    }
    size_t End = Probe;
    while ((End < length) && (GetClassAt(text, length, End) == Run))
    {
        End = UITextEdit_GetNextBoundary(text, length, End);
    }
    *outStart = Start;
    *outEnd = End;
}

void UITextEdit_GetLineAt(const unsigned char* text, size_t length, size_t offset, size_t* outStart, size_t* outEnd)
{
    size_t Offset = (offset < length) ? offset : length;
    size_t Start = Offset;
    while ((Start > 0U) && (text[Start - 1U] != (unsigned char)'\n'))
    {
        Start--;
    }
    size_t End = Offset;
    while ((End < length) && (text[End] != (unsigned char)'\n'))
    {
        End++;
    }
    *outStart = Start;
    *outEnd = End;
}
