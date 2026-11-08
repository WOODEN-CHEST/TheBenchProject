#include "UIChoice.h"
#include "UIElement.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "UIWidget.h"
#include "wr/WRChar.h"
#include "wr/WRString.h"


// Static functions.
static Error ResolveChoice(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIChoiceCapability** outChoice)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIChoice: the widget is gone, or its screen has no kit.");
    }
    UIChoiceCapability* Choice = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Choice));
    if (Choice == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIChoice: the widget has no choice.");
    }
    *outWidget = Target;
    *outChoice = Choice;
    return Error_CreateSuccess();
}

static Error PostIndex(Widget* widget, UIElementEvent event, size_t index)
{
    UIPropertyValue Value = UIPropertyValue_FromDouble((double)index);
    return UIScreen_PostWidgetEvent(Widget_GetScreen(widget), Widget_GetId(widget), event, &Value);
}

/* Checks a list of texts and copies it into one block with its offsets. Nothing is allocated on a failure. */
static Error CopyTexts(const unsigned char* const* texts, size_t count, unsigned char** outTexts, size_t** outOffsets)
{
    *outTexts = NULL;
    *outOffsets = NULL;
    if (count > UI_CHOICE_MAX_OPTIONS)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIChoice: more options than UI_CHOICE_MAX_OPTIONS.");
    }
    if ((count > 0U) && (texts == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIChoice: the texts must not be NULL.");
    }
    size_t Total = 0U;
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (texts[Index] == NULL)
        {
            return Error_Construct2(ErrorCode_IllegalArgument, "UIChoice: an option's text must not be NULL.");
        }
        size_t Length = StringUTF8_GetByteLength(texts[Index]);
        if (Length > UI_CHOICE_MAX_TEXT_BYTES)
        {
            return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIChoice: an option's text is longer than UI_CHOICE_MAX_TEXT_BYTES.");
        }
        if (!StringUTF8_IsEncodingValid(texts[Index]))
        {
            return Error_Construct2(ErrorCode_InvalidTextEncoding, "UIChoice: an option's text is not valid UTF-8.");
        }
        Total += Length + 1U; // At most UI_CHOICE_MAX_OPTIONS times UI_CHOICE_MAX_TEXT_BYTES + 1: no overflow.
    }
    if (count == 0U)
    {
        return Error_CreateSuccess();
    }
    unsigned char* Block = Memory_Allocate(Total);
    size_t* Offsets = Memory_Allocate(count * sizeof(size_t));
    size_t At = 0U;
    for (size_t Index = 0U; Index < count; Index++)
    {
        size_t Length = StringUTF8_GetByteLength(texts[Index]);
        Offsets[Index] = At;
        Memory_Copy(texts[Index], Block + At, Length + 1U);
        At += Length + 1U;
    }
    *outTexts = Block;
    *outOffsets = Offsets;
    return Error_CreateSuccess();
}

/* Ends what was appended from @p start: up to a NUL inside it, at most UI_CHOICE_MAX_TEXT_BYTES cut at a whole character, nothing when it
 * is not valid UTF-8; then NUL-terminated. */
static Error FinishText(GenericBuffer* text, size_t start)
{
    size_t End = start;
    while ((End < text->_count) && (text->_data[End] != 0U))
    {
        End++;
    }
    if ((End - start) > UI_CHOICE_MAX_TEXT_BYTES)
    {
        End = start + UI_CHOICE_MAX_TEXT_BYTES;
        while ((End > start) && ((text->_data[End] & 0xC0U) == 0x80U))
        {
            End--; // Not inside a character.
        }
    }
    // Its own terminator, also after a text that ended with one: an empty text is a terminator of its own.
    unsigned char Terminator = 0U;
    GenericBuffer_SetCount(text, End);
    if (!GenericBuffer_AddLast(text, &Terminator))
    {
        GenericBuffer_SetCount(text, start);
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIChoice: the text buffer could not grow.");
    }
    if (!StringUTF8_IsEncodingValid(text->_data + start))
    {
        GenericBuffer_SetCount(text, start);
        GenericBuffer_AddLast(text, &Terminator); // Room for it is there: it just held more.
    }
    return Error_CreateSuccess();
}

static uint32_t FoldLatin(uint32_t codepoint)
{
    return ((codepoint >= (uint32_t)'A') && (codepoint <= (uint32_t)'Z')) ? (codepoint + 32U) : codepoint;
}


// Public functions: calling code.
Error UIChoice_SetOptions(UIScreen* screen, uint64_t widgetId, const unsigned char* const* texts, size_t count)
{
    Widget* Target = NULL;
    UIChoiceCapability* Choice = NULL;
    Error Result = ResolveChoice(screen, widgetId, &Target, &Choice);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    unsigned char* Texts = NULL;
    size_t* Offsets = NULL;
    Result = CopyTexts(texts, count, &Texts, &Offsets);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Memory_Free(Choice->_texts);
    Memory_Free(Choice->_offsets);
    Choice->_texts = Texts;
    Choice->_offsets = Offsets;
    Choice->_count = count;
    Choice->_countProvider = NULL;
    Choice->_textProvider = NULL;
    Choice->_providerData = UserData_CreateEmpty();
    if (Choice->_hasSelection && (Choice->_selected >= count))
    {
        Choice->_hasSelection = false;
    }
    Choice->_revision++;
    Widget_InvalidateMeasure(Target);
    return Error_CreateSuccess();
}

Error UIChoice_SetProvider(UIScreen* screen, uint64_t widgetId, UIChoiceCountProvider countProvider, UIChoiceTextProvider textProvider,
    const UserData* userData)
{
    if ((countProvider == NULL) || (textProvider == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIChoice_SetProvider: the providers must not be NULL.");
    }
    Widget* Target = NULL;
    UIChoiceCapability* Choice = NULL;
    Error Result = ResolveChoice(screen, widgetId, &Target, &Choice);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Memory_Free(Choice->_texts);
    Memory_Free(Choice->_offsets);
    Choice->_texts = NULL;
    Choice->_offsets = NULL;
    Choice->_count = 0U;
    Choice->_countProvider = countProvider;
    Choice->_textProvider = textProvider;
    Choice->_providerData = (userData != NULL) ? *userData : UserData_CreateEmpty();
    if (Choice->_hasSelection && (Choice->_selected >= UIChoiceCapability_GetCount(Choice, Target)))
    {
        Choice->_hasSelection = false;
    }
    Choice->_revision++;
    Widget_InvalidateMeasure(Target);
    return Error_CreateSuccess();
}

Error UIChoice_Refresh(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIChoiceCapability* Choice = NULL;
    Error Result = ResolveChoice(screen, widgetId, &Target, &Choice);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Choice->_hasSelection && (Choice->_selected >= UIChoiceCapability_GetCount(Choice, Target)))
    {
        Choice->_hasSelection = false;
    }
    Choice->_revision++;
    Widget_InvalidateMeasure(Target);
    return Error_CreateSuccess();
}

Error UIChoice_GetCount(UIScreen* screen, uint64_t widgetId, size_t* outCount)
{
    Widget* Target = NULL;
    UIChoiceCapability* Choice = NULL;
    Error Result = ResolveChoice(screen, widgetId, &Target, &Choice);
    if (Result.Code == ErrorCode_Success)
    {
        *outCount = UIChoiceCapability_GetCount(Choice, Target);
    }
    return Result;
}

Error UIChoice_GetText(UIScreen* screen, uint64_t widgetId, size_t index, GenericBuffer* outText)
{
    Widget* Target = NULL;
    UIChoiceCapability* Choice = NULL;
    Error Result = ResolveChoice(screen, widgetId, &Target, &Choice);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (index >= UIChoiceCapability_GetCount(Choice, Target))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIChoice_GetText: no such option.");
    }
    return UIChoiceCapability_AppendText(Choice, Target, index, outText);
}

Error UIChoice_SetSelected(UIScreen* screen, uint64_t widgetId, bool hasSelection, size_t index)
{
    Widget* Target = NULL;
    UIChoiceCapability* Choice = NULL;
    Error Result = ResolveChoice(screen, widgetId, &Target, &Choice);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (hasSelection && (index >= UIChoiceCapability_GetCount(Choice, Target)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIChoice_SetSelected: no such option.");
    }
    Choice->_hasSelection = hasSelection;
    Choice->_selected = hasSelection ? index : 0U;
    Choice->_isMixed = false;
    Choice->_revision++;
    Widget_InvalidateMeasure(Target);
    return Error_CreateSuccess();
}

Error UIChoice_GetSelected(UIScreen* screen, uint64_t widgetId, bool* outHasSelection, size_t* outIndex)
{
    Widget* Target = NULL;
    UIChoiceCapability* Choice = NULL;
    Error Result = ResolveChoice(screen, widgetId, &Target, &Choice);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outHasSelection = Choice->_hasSelection;
    if (outIndex != NULL)
    {
        *outIndex = Choice->_selected;
    }
    return Error_CreateSuccess();
}

Error UIChoice_SetMixed(UIScreen* screen, uint64_t widgetId, bool isMixed)
{
    Widget* Target = NULL;
    UIChoiceCapability* Choice = NULL;
    Error Result = ResolveChoice(screen, widgetId, &Target, &Choice);
    if ((Result.Code == ErrorCode_Success) && (Choice->_isMixed != isMixed))
    {
        Choice->_isMixed = isMixed;
        Choice->_revision++;
        Widget_InvalidateMeasure(Target);
    }
    return Result;
}

Error UIChoice_IsMixed(UIScreen* screen, uint64_t widgetId, bool* outIsMixed)
{
    Widget* Target = NULL;
    UIChoiceCapability* Choice = NULL;
    Error Result = ResolveChoice(screen, widgetId, &Target, &Choice);
    if (Result.Code == ErrorCode_Success)
    {
        *outIsMixed = Choice->_isMixed;
    }
    return Result;
}


// Public functions: the capability.
Error UIChoiceCapability_Construct(UIChoiceCapability* self, const unsigned char* const* texts, size_t count, bool hasSelection, size_t selected)
{
    if (hasSelection && (selected >= count))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIChoice: the selection is past the options.");
    }
    Error Result = CopyTexts(texts, count, &self->_texts, &self->_offsets);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_count = count;
    self->_countProvider = NULL;
    self->_textProvider = NULL;
    self->_providerData = UserData_CreateEmpty();
    self->_hasSelection = hasSelection;
    self->_selected = hasSelection ? selected : 0U;
    self->_isMixed = false;
    self->_revision = 1U;
    return Error_CreateSuccess();
}

void UIChoiceCapability_Deconstruct(UIChoiceCapability* self)
{
    Memory_Free(self->_texts);
    Memory_Free(self->_offsets);
    self->_texts = NULL;
    self->_offsets = NULL;
    self->_count = 0U;
}

size_t UIChoiceCapability_GetCount(const UIChoiceCapability* self, const Widget* widget)
{
    if (self->_countProvider == NULL)
    {
        return self->_count;
    }
    size_t Count = self->_countProvider(Widget_GetScreen(widget), Widget_GetId(widget), &self->_providerData);
    return (Count < UI_CHOICE_MAX_OPTIONS) ? Count : UI_CHOICE_MAX_OPTIONS;
}

Error UIChoiceCapability_AppendText(const UIChoiceCapability* self, const Widget* widget, size_t index, GenericBuffer* outText)
{
    size_t Start = outText->_count;
    if (self->_textProvider == NULL)
    {
        if (index < self->_count)
        {
            const unsigned char* Text = self->_texts + self->_offsets[index];
            // The cast drops a const the framework's signature lacks: the bytes are only read.
            if (!GenericBuffer_AppendRangeBytes(outText, (unsigned char*)Text, StringUTF8_GetByteLength(Text)))
            {
                return Error_Construct2(ErrorCode_BufferTooLarge, "UIChoice: the text buffer could not grow.");
            }
        }
        return FinishText(outText, Start);
    }
    if (index >= UIChoiceCapability_GetCount(self, widget))
    {
        return FinishText(outText, Start);
    }
    Error Result = self->_textProvider(Widget_GetScreen(widget), Widget_GetId(widget), &self->_providerData, index, outText);
    if (Result.Code != ErrorCode_Success)
    {
        GenericBuffer_SetCount(outText, Start);
        return Result;
    }
    return FinishText(outText, Start);
}

Error UIChoiceCapability_Choose(UIChoiceCapability* self, Widget* widget, size_t index, bool* outHasChanged)
{
    bool HasChanged = (index < UIChoiceCapability_GetCount(self, widget))
        && (!self->_hasSelection || (self->_selected != index) || self->_isMixed);
    if (outHasChanged != NULL)
    {
        *outHasChanged = HasChanged;
    }
    if (!HasChanged)
    {
        return Error_CreateSuccess();
    }
    self->_hasSelection = true;
    self->_selected = index;
    self->_isMixed = false;
    self->_revision++;
    Widget_InvalidateMeasure(widget);
    Error Result = PostIndex(widget, UIElementEvent_Changed, index);
    return (Result.Code == ErrorCode_Success) ? PostIndex(widget, UIElementEvent_Committed, index) : Result;
}

Error UIChoiceCapability_FindByLetter(const UIChoiceCapability* self, const Widget* widget, uint32_t codepoint, bool hasFrom, size_t from,
    GenericBuffer* scratch, size_t* outIndex, bool* outIsFound)
{
    *outIsFound = false;
    size_t Count = UIChoiceCapability_GetCount(self, widget);
    uint32_t Wanted = FoldLatin(codepoint);
    for (size_t Step = 1U; Step <= Count; Step++)
    {
        size_t Index = hasFrom ? ((from + Step) % Count) : (Step - 1U);
        GenericBuffer_Clear(scratch);
        Error Result = UIChoiceCapability_AppendText(self, widget, Index, scratch);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if ((scratch->_count > 1U) && (FoldLatin((uint32_t)CharUTF8_GetCodePoint(scratch->_data)) == Wanted))
        {
            *outIndex = Index;
            *outIsFound = true;
            return Error_CreateSuccess();
        }
    }
    return Error_CreateSuccess();
}
