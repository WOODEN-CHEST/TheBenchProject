#include "UITooltip.h"
#include "UIInternal.h"
#include "UIScreen.h"
#include "UITheme.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "wr/WRChar.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Types.
/* One widget's hover text: by value or from a provider, with its own delay or the theme's. */
typedef struct TooltipEntryStruct
{
    uint64_t WidgetId;
    /* The text, owned (NULL with a provider). */
    unsigned char* Text;
    UITooltipProvider Provider;
    UserData Data;
    bool HasDelay;
    float DelaySeconds;
} TooltipEntry;


// Static functions.
static TooltipEntry* GetEntryAt(UIScreen* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_tooltips, index);
}

static TooltipEntry* FindEntry(UIScreen* self, uint64_t widgetId, size_t* outIndex)
{
    for (size_t Index = 0; Index < self->_tooltips._count; Index++)
    {
        TooltipEntry* Entry = GetEntryAt(self, Index);
        if (Entry->WidgetId == widgetId)
        {
            if (outIndex != NULL)
            {
                *outIndex = Index;
            }
            return Entry;
        }
    }
    return NULL;
}

/* The entry of a widget, made empty if it had none. NULL when the table could not grow. */
static TooltipEntry* FindOrAddEntry(UIScreen* self, uint64_t widgetId)
{
    TooltipEntry* Existing = FindEntry(self, widgetId, NULL);
    if (Existing != NULL)
    {
        return Existing;
    }
    TooltipEntry Entry = { .WidgetId = widgetId, .Text = NULL, .Provider = NULL, .Data = UserData_CreateEmpty(), .HasDelay = false,
        .DelaySeconds = 0.0f };
    if (!GenericBuffer_AddLast(&self->_tooltips, &Entry))
    {
        return NULL;
    }
    return GetEntryAt(self, self->_tooltips._count - 1U);
}

/* The widget whose hover text the pointer rests on: the hovered widget, or the nearest ancestor with one; NULL for none. */
static TooltipEntry* FindRestingEntry(UIScreen* self)
{
    if (!self->_hasPointer || (self->_tooltips._count == 0U))
    {
        return NULL;
    }
    for (Widget* Current = UIScreen_InternalResolve(self, self->_hoveredWidgetId); Current != NULL; Current = Widget_GetParent(Current))
    {
        TooltipEntry* Entry = FindEntry(self, Widget_GetId(Current), NULL);
        if (Entry != NULL)
        {
            return Entry;
        }
    }
    return NULL;
}

/* Ends the current text in place: what fits of it, up to a NUL a provider wrote, cut at a whole character, NUL-terminated; empty
 * when it is not valid UTF-8. */
static void FinishCurrentText(UIScreen* self)
{
    size_t Count = 0U;
    while ((Count < self->_tooltipText._count) && (self->_tooltipText._data[Count] != 0U))
    {
        Count++; // What follows a NUL is no part of the text.
    }
    GenericBuffer_SetCount(&self->_tooltipText, Count);
    if (Count > UI_TOOLTIP_MAX_TEXT_BYTES)
    {
        Count = UI_TOOLTIP_MAX_TEXT_BYTES;
        while ((Count > 0U) && ((self->_tooltipText._data[Count] & 0xC0U) == 0x80U))
        {
            Count--; // Not inside a character.
        }
        GenericBuffer_SetCount(&self->_tooltipText, Count);
    }
    if (!GenericBuffer_NullTerminate(&self->_tooltipText) || !StringUTF8_IsEncodingValid(self->_tooltipText._data))
    {
        GenericBuffer_Clear(&self->_tooltipText);
        GenericBuffer_NullTerminate(&self->_tooltipText);
    }
}

/* Sets the current text to a copy of @p text. */
static void SetCurrentText(UIScreen* self, const unsigned char* text, size_t byteCount)
{
    GenericBuffer_Clear(&self->_tooltipText);
    // The cast drops a const the framework's signature lacks: the bytes are only read.
    if (!GenericBuffer_AppendRangeBytes(&self->_tooltipText, (unsigned char*)text, byteCount))
    {
        GenericBuffer_Clear(&self->_tooltipText);
    }
    FinishCurrentText(self);
}

/* The text of an entry: its own, or what its provider writes, straight into the screen's buffer (nothing allocated per tick). */
static Error ReadEntryText(UIScreen* self, const TooltipEntry* entry)
{
    if (entry->Provider == NULL)
    {
        SetCurrentText(self, entry->Text, StringUTF8_GetByteLength(entry->Text));
        return Error_CreateSuccess();
    }
    UITooltipProvider Provider = entry->Provider;
    UserData Data = entry->Data; // Copied out: what the provider does cannot move it under the call.
    uint64_t WidgetId = entry->WidgetId;
    GenericBuffer_Clear(&self->_tooltipText);
    Error Result = Provider(self, WidgetId, &Data, &self->_tooltipText);
    if (Result.Code != ErrorCode_Success)
    {
        GenericBuffer_Clear(&self->_tooltipText);
    }
    FinishCurrentText(self);
    return Result;
}

static bool HasPressedButton(const GameInput* input)
{
    for (int32_t Button = (int32_t)GameKey_MouseLeft; Button <= (int32_t)GameKey_MouseBack; Button++)
    {
        if ((GameInput_GetRawKeyState(input, (GameKey)Button) & GAME_INPUT_KEY_STATE_PRESSED) != 0U)
        {
            return true;
        }
    }
    return false;
}

/* Makes the widget that shows hover text, once, of the type the factory names: a root in the overlay layer. */
static Error EnsureDisplay(UIScreen* self)
{
    uint64_t TypeId = UIWidgetFactory_GetTooltipType(self->_factory);
    if ((TypeId == WIDGET_TYPE_ID_INVALID) || (UIScreen_InternalResolve(self, self->_tooltipDisplayId) != NULL))
    {
        return Error_CreateSuccess();
    }
    Widget* Display = NULL;
    Error Result = UIWidgetFactory_CreateWidget(self->_factory, self, TypeId, NULL, &Display);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t Id = Widget_GetId(Display);
    Result = Widget_InitializeWidget(Display);
    if ((Result.Code == ErrorCode_Success) && ((Display = UIScreen_InternalResolve(self, Id)) != NULL))
    {
        Result = UIScreen_AddWidget(self, Display);
    }
    if ((Result.Code == ErrorCode_Success) && ((Display = UIScreen_InternalResolve(self, Id)) != NULL))
    {
        Result = UIScreen_SetRootLayer(self, Display, UIScreenLayer_Overlay);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Widget* Made = UIScreen_InternalResolve(self, Id);
        if (Made != NULL)
        {
            Error Cleanup = Widget_Destroy(Made);
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }
    self->_tooltipDisplayId = Id;
    return Error_CreateSuccess();
}


// Internal functions (declared in UIInternal.h).
void UIScreen_InternalInitializeTooltips(UIScreen* self)
{
    GenericBuffer_AllocateVariable(&self->_tooltips, 0U, sizeof(TooltipEntry));
    GenericBuffer_AllocateVariable(&self->_tooltipText, 64U, sizeof(unsigned char));
    GenericBuffer_NullTerminate(&self->_tooltipText);
}

void UIScreen_InternalReleaseTooltips(UIScreen* self)
{
    for (size_t Index = 0; Index < self->_tooltips._count; Index++)
    {
        Memory_Free(GetEntryAt(self, Index)->Text);
    }
    Memory_Free(self->_tooltips._data);
    Memory_Free(self->_tooltipText._data);
    Memory_Zero(&self->_tooltips, sizeof(self->_tooltips));
    Memory_Zero(&self->_tooltipText, sizeof(self->_tooltipText));
}

Error UIScreen_InternalUpdateTooltip(UIScreen* self, ProgramTime time)
{
    TooltipEntry* Entry = FindRestingEntry(self);
    uint64_t WidgetId = (Entry != NULL) ? Entry->WidgetId : WIDGET_ID_INVALID;
    if (WidgetId != self->_tooltipWidgetId)
    {
        // Somewhere else: it starts again.
        self->_tooltipWidgetId = WidgetId;
        self->_tooltipRestSeconds = 0.0;
        self->_isTooltipShown = false;
        self->_isTooltipSuppressed = false;
    }
    else if (Entry != NULL)
    {
        self->_tooltipRestSeconds += time.PassedTime;
    }
    if (Entry == NULL)
    {
        SetCurrentText(self, (const unsigned char*)u8"", 0U);
        return Error_CreateSuccess();
    }
    if ((self->_input != NULL) && HasPressedButton(self->_input))
    {
        self->_isTooltipSuppressed = true;
    }
    float Delay = Entry->HasDelay ? Entry->DelaySeconds : UITheme_GetTooltipDelaySeconds(self->_theme);
    Error Result = ReadEntryText(self, Entry); // The provider may have moved the table: the entry is not used past here.
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool HasText = self->_tooltipText._count > 1U;
    self->_isTooltipShown = HasText && !self->_isTooltipSuppressed && (self->_tooltipRestSeconds >= (double)Delay);
    return self->_isTooltipShown ? EnsureDisplay(self) : Error_CreateSuccess();
}

void UIScreen_InternalRemoveWidgetTooltip(UIScreen* self, uint64_t widgetId)
{
    size_t Index = 0;
    TooltipEntry* Entry = FindEntry(self, widgetId, &Index);
    if (Entry != NULL)
    {
        Memory_Free(Entry->Text);
        GenericBuffer_RemoveAt(&self->_tooltips, Index);
    }
    if ((widgetId != WIDGET_ID_INVALID) && (widgetId == self->_tooltipWidgetId))
    {
        UIScreen_InternalForgetCurrentTooltip(self);
    }
}

void UIScreen_InternalForgetCurrentTooltip(UIScreen* self)
{
    self->_tooltipWidgetId = WIDGET_ID_INVALID;
    self->_tooltipRestSeconds = 0.0;
    self->_isTooltipShown = false;
    self->_isTooltipSuppressed = false;
    SetCurrentText(self, (const unsigned char*)u8"", 0U);
}

uint64_t UIScreen_InternalGetCurrentTooltipWidgetId(const UIScreen* self)
{
    return self->_tooltipWidgetId;
}


// Public functions.
Error UITooltip_Set(UIScreen* screen, uint64_t widgetId, const unsigned char* text)
{
    if ((screen == NULL) || (text == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITooltip_Set: screen and text must not be NULL.");
    }
    if (UIScreen_InternalResolve(screen, widgetId) == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UITooltip_Set: the widget is not alive.");
    }
    size_t Length = StringUTF8_GetByteLength(text);
    if (Length > UI_TOOLTIP_MAX_TEXT_BYTES)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITooltip_Set: the text is longer than UI_TOOLTIP_MAX_TEXT_BYTES.");
    }
    if (!StringUTF8_IsEncodingValid(text))
    {
        return Error_Construct2(ErrorCode_InvalidTextEncoding, "UITooltip_Set: the text is not valid UTF-8.");
    }
    TooltipEntry* Entry = FindOrAddEntry(screen, widgetId);
    if (Entry == NULL)
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UITooltip_Set: the table could not grow.");
    }
    unsigned char* Copy = Memory_Allocate(Length + 1U);
    Memory_Copy(text, Copy, Length + 1U);
    Memory_Free(Entry->Text);
    Entry->Text = Copy;
    Entry->Provider = NULL;
    Entry->Data = UserData_CreateEmpty();
    return Error_CreateSuccess();
}

Error UITooltip_SetProvider(UIScreen* screen, uint64_t widgetId, UITooltipProvider provider, const UserData* userData)
{
    if ((screen == NULL) || (provider == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITooltip_SetProvider: screen and provider must not be NULL.");
    }
    if (UIScreen_InternalResolve(screen, widgetId) == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UITooltip_SetProvider: the widget is not alive.");
    }
    TooltipEntry* Entry = FindOrAddEntry(screen, widgetId);
    if (Entry == NULL)
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UITooltip_SetProvider: the table could not grow.");
    }
    Memory_Free(Entry->Text);
    Entry->Text = NULL;
    Entry->Provider = provider;
    Entry->Data = (userData != NULL) ? *userData : UserData_CreateEmpty();
    return Error_CreateSuccess();
}

Error UITooltip_SetDelay(UIScreen* screen, uint64_t widgetId, bool hasDelay, float delaySeconds)
{
    if (screen == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITooltip_SetDelay: screen must not be NULL.");
    }
    TooltipEntry* Entry = FindEntry(screen, widgetId, NULL);
    if (Entry == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UITooltip_SetDelay: the widget has no hover text.");
    }
    if (hasDelay && (!(delaySeconds >= 0.0f) || !(delaySeconds <= UI_TOOLTIP_MAX_DELAY_SECONDS)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITooltip_SetDelay: the delay must be within [0, UI_TOOLTIP_MAX_DELAY_SECONDS].");
    }
    Entry->HasDelay = hasDelay;
    Entry->DelaySeconds = hasDelay ? delaySeconds : 0.0f;
    return Error_CreateSuccess();
}

Error UITooltip_Clear(UIScreen* screen, uint64_t widgetId)
{
    if (screen == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITooltip_Clear: screen must not be NULL.");
    }
    UIScreen_InternalRemoveWidgetTooltip(screen, widgetId);
    return Error_CreateSuccess();
}

bool UITooltip_GetCurrent(const UIScreen* screen, UITooltipState* outState)
{
    if ((screen == NULL) || (outState == NULL) || (screen->_tooltipWidgetId == WIDGET_ID_INVALID) || (screen->_tooltipText._count <= 1U))
    {
        return false;
    }
    *outState = (UITooltipState)
    {
        .WidgetId = screen->_tooltipWidgetId,
        .Text = screen->_tooltipText._data,
        .IsShown = screen->_isTooltipShown,
        .PointerPosition = screen->_pointerPosition
    };
    return true;
}
