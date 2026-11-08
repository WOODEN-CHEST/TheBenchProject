#include "UIContextMenu.h"
#include "UIInternal.h"
#include "UIScreen.h"
#include "UIWidget.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Types.
/* One widget's provider. */
typedef struct ContextMenuEntryStruct
{
    uint64_t WidgetId;
    UIContextMenuProvider Provider;
    UserData Data;
} ContextMenuEntry;


// Static functions.
static ContextMenuEntry* FindEntry(const UIScreen* self, uint64_t widgetId, size_t* outIndex)
{
    for (size_t Index = 0; Index < self->_contextMenus._count; Index++)
    {
        ContextMenuEntry* Entry = GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_contextMenus, Index);
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


// Internal functions (declared in UIInternal.h).
void UIScreen_InternalInitializeContextMenus(UIScreen* self)
{
    GenericBuffer_AllocateVariable(&self->_contextMenus, 0U, sizeof(ContextMenuEntry));
}

void UIScreen_InternalReleaseContextMenus(UIScreen* self)
{
    Memory_Free(self->_contextMenus._data);
    Memory_Zero(&self->_contextMenus, sizeof(self->_contextMenus));
}

void UIScreen_InternalRemoveWidgetContextMenu(UIScreen* self, uint64_t widgetId)
{
    size_t Index = 0;
    if (FindEntry(self, widgetId, &Index) != NULL)
    {
        GenericBuffer_RemoveAt(&self->_contextMenus, Index);
    }
}


// Public functions.
Error UIContextMenu_SetProvider(UIScreen* screen, uint64_t widgetId, UIContextMenuProvider provider, const UserData* userData)
{
    if ((screen == NULL) || (provider == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIContextMenu_SetProvider: screen and provider must not be NULL.");
    }
    if (UIScreen_InternalResolve(screen, widgetId) == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIContextMenu_SetProvider: the widget is not alive.");
    }
    ContextMenuEntry* Entry = FindEntry(screen, widgetId, NULL);
    if (Entry == NULL)
    {
        ContextMenuEntry New = { .WidgetId = widgetId, .Provider = provider, .Data = UserData_CreateEmpty() };
        if (!GenericBuffer_AddLast(&screen->_contextMenus, &New))
        {
            return Error_Construct2(ErrorCode_BufferTooLarge, "UIContextMenu_SetProvider: the table could not grow.");
        }
        Entry = GenericBuffer_GetPointerToElement(&screen->_contextMenus, screen->_contextMenus._count - 1U);
    }
    Entry->Provider = provider;
    Entry->Data = (userData != NULL) ? *userData : UserData_CreateEmpty();
    return Error_CreateSuccess();
}

Error UIContextMenu_Clear(UIScreen* screen, uint64_t widgetId)
{
    if (screen == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIContextMenu_Clear: screen must not be NULL.");
    }
    UIScreen_InternalRemoveWidgetContextMenu(screen, widgetId);
    return Error_CreateSuccess();
}

bool UIContextMenu_HasProvider(const UIScreen* screen, uint64_t widgetId)
{
    return (screen != NULL) && (FindEntry(screen, widgetId, NULL) != NULL);
}

Error UIContextMenu_Request(UIScreen* screen, uint64_t widgetId, Vector2 screenPosition, bool isFromKey, bool* outIsAnswered)
{
    if (outIsAnswered != NULL)
    {
        *outIsAnswered = false;
    }
    if (screen == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIContextMenu_Request: screen must not be NULL.");
    }
    if (Math_IsNaNFloat(screenPosition.x) || Math_IsNaNFloat(screenPosition.y) || Math_IsInfinityFloat(screenPosition.x)
        || Math_IsInfinityFloat(screenPosition.y))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIContextMenu_Request: the position must be finite.");
    }
    for (Widget* Current = UIScreen_InternalResolve(screen, widgetId); Current != NULL; Current = Widget_GetParent(Current))
    {
        const ContextMenuEntry* Entry = FindEntry(screen, Widget_GetId(Current), NULL);
        if (Entry == NULL)
        {
            continue;
        }
        // Copied out: what the provider does cannot move them under the call.
        UIContextMenuProvider Provider = Entry->Provider;
        UserData Data = Entry->Data;
        UIContextMenuRequest Request = { .ProviderWidgetId = Widget_GetId(Current), .TargetWidgetId = widgetId, .ScreenPosition = screenPosition,
            .IsFromKey = isFromKey };
        if (outIsAnswered != NULL)
        {
            *outIsAnswered = true;
        }
        return Provider(screen, &Request, &Data);
    }
    return Error_CreateSuccess();
}
