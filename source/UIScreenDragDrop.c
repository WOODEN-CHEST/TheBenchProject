#include "UIDragDrop.h"
#include "UIInternal.h"
#include "UIScreen.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRNumber.h"
#include "wr/WRString.h"


// Types.
/* One widget's source or target entry: its provider or its acceptor, and what it is handed. */
typedef struct DragEntryStruct
{
    uint64_t WidgetId;
    UIDragSourceProvider Provider;
    UIDragAcceptor Acceptor;
    UserData Data;
} DragEntry;


// Static functions: the tables.
static DragEntry* FindEntry(const GenericBuffer* table, uint64_t widgetId, size_t* outIndex)
{
    for (size_t Index = 0; Index < table->_count; Index++)
    {
        DragEntry* Entry = GenericBuffer_GetPointerToElement((GenericBuffer*)table, Index);
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

static void RemoveEntry(GenericBuffer* table, uint64_t widgetId)
{
    size_t Index = 0;
    if (FindEntry(table, widgetId, &Index) != NULL)
    {
        GenericBuffer_RemoveAt(table, Index);
    }
}

static Error SetEntry(UIScreen* screen, GenericBuffer* table, uint64_t widgetId, const DragEntry* entry, const char* name)
{
    if (UIScreen_InternalResolve(screen, widgetId) == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, name);
    }
    DragEntry* Found = FindEntry(table, widgetId, NULL);
    if (Found != NULL)
    {
        *Found = *entry;
        return Error_CreateSuccess();
    }
    if (!GenericBuffer_AddLast(table, (void*)entry))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIDragDrop: the table could not grow.");
    }
    return Error_CreateSuccess();
}


// Static functions: the drag.
static GameKey GetDragButton(const UIScreen* self)
{
    return (GameKey)((int32_t)GameKey_MouseLeft + (int32_t)self->_drag.ButtonIndex);
}

/* Whether the source is still there to have given what is dragged: alive, attached and shown. */
static bool IsSourcePresent(const UIScreen* self)
{
    Widget* Source = UIScreen_InternalResolve(self, self->_drag.SourceId);
    return (Source != NULL) && UIScreen_InternalIsAttached(Source) && UIScreen_InternalIsShown(Source);
}

static UIDragRecord MakeRecord(const UIScreen* self)
{
    return (UIDragRecord){ .Kind = self->_drag.Payload.Kind, .Value = self->_drag.Payload.Value, .SourceId = self->_drag.SourceId,
        .TargetId = self->_drag.TargetId, .ScreenPosition = self->_drag.ScreenPosition, .Modifiers = self->_drag.Modifiers };
}

static Error PostDragEvent(UIScreen* self, uint64_t widgetId, UIDragEvent event, const UIDragRecord* record)
{
    UIEvent Event =
    {
        .Kind = UIEventKind_Drag,
        .WidgetId = widgetId,
        .WidgetTypeId = WIDGET_TYPE_ID_INVALID,
        .IsSet = false,
        .WidgetEventId = (int32_t)event,
        .HasValue = false,
        .HasMouse = false,
        .HasIndex = false,
        .HasDrag = true,
        .Drag = *record
    };
    Widget* Found = UIScreen_InternalResolve(self, widgetId);
    if (Found != NULL)
    {
        Event.WidgetTypeId = Widget_GetTypeId(Found);
    }
    if (!UIScreen_InternalPostEvent(self, &Event))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIDragDrop: the notification queue could not grow.");
    }
    return Error_CreateSuccess();
}

/* Ends the drag, its press with it; nothing is posted. */
static void EndDrag(UIScreen* self)
{
    size_t ButtonIndex = self->_drag.ButtonIndex;
    self->_drag.IsActive = false;
    self->_drag.TargetId = WIDGET_ID_INVALID;
    self->_drag.TargetState = UIDragTargetState_None;
    if (self->_buttons[ButtonIndex].IsPressed && (self->_buttons[ButtonIndex].TargetWidgetId == WIDGET_ID_INVALID))
    {
        UIScreen_InternalEndPress(self, ButtonIndex);
    }
}

static Error CancelDrag(UIScreen* self)
{
    UIDragRecord Record = MakeRecord(self);
    Record.TargetId = WIDGET_ID_INVALID;
    uint64_t SourceId = self->_drag.SourceId;
    EndDrag(self);
    return PostDragEvent(self, SourceId, UIDragEvent_Cancelled, &Record);
}

/* Looks the target up again where the pointer is: the top-most widget there, or its nearest ancestor, that declared itself a target,
 * asked whether it would take the payload. */
static void RefreshTarget(UIScreen* self)
{
    self->_drag.TargetId = WIDGET_ID_INVALID;
    self->_drag.TargetState = UIDragTargetState_None;
    Widget* Top = NULL;
    if (!self->_hasPointer || !UIScreen_TryGetTopmostAt(self, self->_pointerPosition, &Top))
    {
        return;
    }
    for (Widget* Current = Top; Current != NULL; Current = Widget_GetParent(Current))
    {
        const DragEntry* Entry = FindEntry(&self->_dragTargets, Widget_GetId(Current), NULL);
        if (Entry == NULL)
        {
            continue;
        }
        // Copied out: the acceptor cannot move them under the call.
        UIDragAcceptor Acceptor = Entry->Acceptor;
        UserData Data = Entry->Data;
        UIDragPayload Payload = self->_drag.Payload;
        UIDragQuery Query = { .Payload = &Payload, .SourceId = self->_drag.SourceId, .TargetId = Widget_GetId(Current),
            .ScreenPosition = self->_pointerPosition, .Modifiers = self->_drag.Modifiers };
        bool IsAccepted = Acceptor(self, &Query, &Data);
        self->_drag.TargetId = Query.TargetId;
        self->_drag.TargetState = IsAccepted ? UIDragTargetState_Accepting : UIDragTargetState_Refusing;
        return;
    }
}

/* Makes the widget that shows the drag, once, of the type the factory names: a root in the overlay layer. */
static Error EnsureDisplay(UIScreen* self)
{
    uint64_t TypeId = UIWidgetFactory_GetDragGhostType(self->_factory);
    if ((TypeId == WIDGET_TYPE_ID_INVALID) || (UIScreen_InternalResolve(self, self->_drag.DisplayId) != NULL))
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
    self->_drag.DisplayId = Id;
    return Error_CreateSuccess();
}

/* Writes at most @p capacity - 1 bytes of UTF-8 into @p out, cut at a codepoint, NUL-terminated; text that is not valid UTF-8 is left
 * out. */
static void CopyCutText(unsigned char* out, size_t capacity, const unsigned char* text, size_t byteCount)
{
    out[0] = 0U;
    if (text == NULL)
    {
        return;
    }
    size_t Length = 0U;
    while ((Length < byteCount) && (text[Length] != 0U))
    {
        Length++;
    }
    if (Length >= capacity)
    {
        Length = capacity - 1U;
        while ((Length > 0U) && ((text[Length] & 0xC0U) == 0x80U))
        {
            Length--;
        }
    }
    Memory_Copy(text, out, Length);
    out[Length] = 0U;
    if (!StringUTF8_IsEncodingValid(out))
    {
        out[0] = 0U;
    }
}


// Internal functions (declared in UIInternal.h).
void UIScreen_InternalInitializeDrag(UIScreen* self)
{
    GenericBuffer_AllocateVariable(&self->_dragSources, 0U, sizeof(DragEntry));
    GenericBuffer_AllocateVariable(&self->_dragTargets, 0U, sizeof(DragEntry));
    Memory_Zero(&self->_drag, sizeof(self->_drag));
    self->_drag.DisplayId = WIDGET_ID_INVALID;
}

void UIScreen_InternalReleaseDrag(UIScreen* self)
{
    Memory_Free(self->_dragSources._data);
    Memory_Free(self->_dragTargets._data);
    Memory_Zero(&self->_dragSources, sizeof(self->_dragSources));
    Memory_Zero(&self->_dragTargets, sizeof(self->_dragTargets));
}

void UIScreen_InternalRemoveWidgetDrag(UIScreen* self, uint64_t widgetId)
{
    RemoveEntry(&self->_dragSources, widgetId);
    RemoveEntry(&self->_dragTargets, widgetId);
}

bool UIScreen_InternalIsDragButton(const UIScreen* self, size_t buttonIndex)
{
    return self->_drag.IsActive && (self->_drag.ButtonIndex == buttonIndex);
}

Error UIScreen_InternalTryStartDrag(UIScreen* self, size_t buttonIndex, bool* outIsStarted)
{
    *outIsStarted = false;
    UIScreenButtonTracking* Press = &self->_buttons[buttonIndex];
    if (self->_drag.IsActive || !Press->IsPressed || Press->IsDragRefused || !Press->HasReachedWidget || (self->_dragSources._count == 0U)
        || (self->_pointerCaptureWidgetId != WIDGET_ID_INVALID) || !self->_isPointerKnown)
    {
        return Error_CreateSuccess();
    }
    float MovedX = ((self->_pointerPosition.x - Press->StartScreenPosition.x) * self->_viewport.SizePixels.x) / self->_scale;
    float MovedY = ((self->_pointerPosition.y - Press->StartScreenPosition.y) * self->_viewport.SizePixels.y) / self->_scale;
    if (((MovedX * MovedX) + (MovedY * MovedY)) < (UI_DRAG_START_DISTANCE * UI_DRAG_START_DISTANCE))
    {
        return Error_CreateSuccess();
    }
    // The source: the press's widget, or its nearest ancestor that declared itself one.
    const DragEntry* Entry = NULL;
    for (Widget* Current = UIScreen_InternalResolve(self, Press->TargetWidgetId); (Current != NULL) && (Entry == NULL);
        Current = Widget_GetParent(Current))
    {
        Entry = FindEntry(&self->_dragSources, Widget_GetId(Current), NULL);
    }
    Press->IsDragRefused = true; // Asked once per press: what it would not drag now it will not later.
    if (Entry == NULL)
    {
        return Error_CreateSuccess();
    }
    uint64_t SourceId = Entry->WidgetId;
    UIDragSourceProvider Provider = Entry->Provider;
    UserData Data = Entry->Data;
    UIDragPayload Payload;
    Memory_Zero(&Payload, sizeof(Payload));
    bool IsDragged = false;
    Error Result = Provider(self, SourceId, Press->StartScreenPosition, &Data, &Payload, &IsDragged);
    if ((Result.Code != ErrorCode_Success) || !IsDragged || !self->_buttons[buttonIndex].IsPressed)
    {
        return Result;
    }
    Payload.Text[UI_DRAG_MAX_TEXT_BYTES - 1U] = 0U;
    if (!StringUTF8_IsEncodingValid(Payload.Text))
    {
        Payload.Text[0] = 0U;
    }
    // The drag takes the press over: its widget lost it.
    Press = &self->_buttons[buttonIndex];
    uint64_t HolderId = Press->TargetWidgetId;
    bool HadReachedIt = Press->HasReachedWidget;
    Press->TargetWidgetId = WIDGET_ID_INVALID;
    Press->HasReachedWidget = false;
    uint64_t DisplayId = self->_drag.DisplayId;
    self->_drag = (UIScreenDrag){ .IsActive = true, .Payload = Payload, .SourceId = SourceId, .TargetId = WIDGET_ID_INVALID,
        .TargetState = UIDragTargetState_None, .ButtonIndex = buttonIndex, .ScreenPosition = self->_pointerPosition,
        .Modifiers = GameInput_GetHeldModifiers(self->_input), .DisplayId = DisplayId };
    *outIsStarted = true;
    Widget* Holder = UIScreen_InternalResolve(self, HolderId);
    if ((Holder != NULL) && HadReachedIt)
    {
        Result = Widget_OnPressLost(Holder, GetDragButton(self));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    if (self->_drag.IsActive)
    {
        RefreshTarget(self);
    }
    return Error_CreateSuccess();
}

Error UIScreen_InternalFollowDrag(UIScreen* self, bool isReleased)
{
    if (!self->_drag.IsActive)
    {
        return Error_CreateSuccess();
    }
    if (!IsSourcePresent(self))
    {
        return CancelDrag(self);
    }
    self->_drag.ScreenPosition = self->_pointerPosition;
    self->_drag.Modifiers = GameInput_GetHeldModifiers(self->_input);
    RefreshTarget(self);
    if (!isReleased || !self->_drag.IsActive)
    {
        return Error_CreateSuccess();
    }
    if ((self->_drag.TargetState != UIDragTargetState_Accepting) || (UIScreen_InternalResolve(self, self->_drag.TargetId) == NULL))
    {
        return CancelDrag(self);
    }
    UIDragRecord Record = MakeRecord(self);
    EndDrag(self);
    Error Result = PostDragEvent(self, Record.TargetId, UIDragEvent_Dropped, &Record);
    return (Result.Code == ErrorCode_Success) ? PostDragEvent(self, Record.SourceId, UIDragEvent_Delivered, &Record) : Result;
}

Error UIScreen_InternalUpdateDrag(UIScreen* self)
{
    if (!self->_drag.IsActive)
    {
        return Error_CreateSuccess();
    }
    if (!IsSourcePresent(self))
    {
        return CancelDrag(self);
    }
    RefreshTarget(self);
    return EnsureDisplay(self);
}

Error UIScreen_InternalRouteDragEscape(UIScreen* self)
{
    if (!self->_drag.IsActive || (self->_input == NULL) || !GameInput_IsActionPressed(self->_input, GameAction_UIBack))
    {
        return Error_CreateSuccess();
    }
    GameInput_ConsumeAction(self->_input, GameAction_UIBack);
    return CancelDrag(self);
}

bool UIScreen_InternalTryGetDragCursorShape(const UIScreen* self, UICursorShape* outShape)
{
    if (!self->_drag.IsActive)
    {
        return false;
    }
    *outShape = (self->_drag.TargetState == UIDragTargetState_Accepting) ? UICursorShape_Arrow : UICursorShape_NotAllowed;
    return true;
}


// Public functions.
Error UIDragPayload_SetText(UIDragPayload* self, const unsigned char* text, size_t byteCount)
{
    CopyCutText(self->Text, UI_DRAG_MAX_TEXT_BYTES, text, byteCount);
    return Error_CreateSuccess();
}

Error UIDragPayload_SetTextCount(UIDragPayload* self, size_t count, const unsigned char* noun)
{
    unsigned char Bytes[UI_DRAG_MAX_TEXT_BYTES];
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, Bytes, sizeof(Bytes), sizeof(unsigned char), 0U);
    Error Result = Number_UInt64ToString((uint64_t)count, 10, false, &Text);
    Error_Deconstruct(&Result);
    if (GenericBuffer_AppendByte(&Text, (unsigned char)' '))
    {
        size_t Room = sizeof(Bytes) - Text._count;
        size_t Length = Math_MinSizeT(StringUTF8_GetByteLength(noun), (Room > 0U) ? (Room - 1U) : 0U);
        // The cast drops a const the framework's signature lacks: the bytes are only read.
        GenericBuffer_AppendRangeBytes(&Text, (unsigned char*)noun, Length);
    }
    return UIDragPayload_SetText(self, Bytes, Text._count);
}

Error UIDragDrop_SetSource(UIScreen* screen, uint64_t widgetId, UIDragSourceProvider provider, const UserData* userData)
{
    if ((screen == NULL) || (provider == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIDragDrop_SetSource: screen and provider must not be NULL.");
    }
    DragEntry Entry = { .WidgetId = widgetId, .Provider = provider, .Acceptor = NULL, .Data = (userData != NULL) ? *userData : UserData_CreateEmpty() };
    return SetEntry(screen, &screen->_dragSources, widgetId, &Entry, "UIDragDrop_SetSource: the widget is not alive.");
}

void UIDragDrop_ClearSource(UIScreen* screen, uint64_t widgetId)
{
    RemoveEntry(&screen->_dragSources, widgetId);
}

Error UIDragDrop_SetTarget(UIScreen* screen, uint64_t widgetId, UIDragAcceptor acceptor, const UserData* userData)
{
    if ((screen == NULL) || (acceptor == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIDragDrop_SetTarget: screen and acceptor must not be NULL.");
    }
    DragEntry Entry = { .WidgetId = widgetId, .Provider = NULL, .Acceptor = acceptor, .Data = (userData != NULL) ? *userData : UserData_CreateEmpty() };
    return SetEntry(screen, &screen->_dragTargets, widgetId, &Entry, "UIDragDrop_SetTarget: the widget is not alive.");
}

void UIDragDrop_ClearTarget(UIScreen* screen, uint64_t widgetId)
{
    RemoveEntry(&screen->_dragTargets, widgetId);
    if (screen->_drag.IsActive && (screen->_drag.TargetId == widgetId))
    {
        screen->_drag.TargetId = WIDGET_ID_INVALID;
        screen->_drag.TargetState = UIDragTargetState_None;
    }
}

bool UIDragDrop_IsDragging(const UIScreen* screen)
{
    return screen->_drag.IsActive;
}

bool UIDragDrop_GetSession(const UIScreen* screen, UIDragSession* outSession)
{
    if (!screen->_drag.IsActive)
    {
        return false;
    }
    *outSession = (UIDragSession){ .Payload = screen->_drag.Payload, .SourceId = screen->_drag.SourceId, .TargetId = screen->_drag.TargetId,
        .TargetState = screen->_drag.TargetState, .ScreenPosition = screen->_drag.ScreenPosition, .Button = GetDragButton(screen),
        .Modifiers = screen->_drag.Modifiers };
    return true;
}

UIDragTargetState UIDragDrop_GetTargetState(const UIScreen* screen, uint64_t widgetId)
{
    if (!screen->_drag.IsActive || (screen->_drag.TargetId != widgetId) || (widgetId == WIDGET_ID_INVALID))
    {
        return UIDragTargetState_None;
    }
    return screen->_drag.TargetState;
}

Error UIDragDrop_Cancel(UIScreen* screen)
{
    return screen->_drag.IsActive ? CancelDrag(screen) : Error_CreateSuccess();
}
