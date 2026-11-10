#include "Logger.h"
#include "UIChoice.h"
#include "UIDropdown.h"
#include "UILayouts.h"
#include "UIScreen.h"
#include "UIWindowTypes.h"
#include "UIWorkspace.h"
#include "UIWorkspaceInternal.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


/*
 * A workspace's layouts: capturing its arrangement into a layout by window type keys, showing a layout again (reusing the windows of the
 * same types, closing those it lacks, opening those it has), switching, the named layouts' commands, saving once the arrangement settles,
 * and the switcher.
 */


// Macros.
/** The name a layout gets when there is no other: the default's. */
#define DEFAULT_LAYOUT_NAME ((const unsigned char*)u8"Default")
/** What a new layout's name starts with. */
#define NEW_LAYOUT_NAME ((const unsigned char*)u8"Layout")
/** FNV-1a's numbers. */
#define FINGERPRINT_OFFSET 14695981039346656037ULL
#define FINGERPRINT_PRIME 1099511628211ULL


// Types.
/* Which window of a capture stands for which item of a layout's dock. */
typedef struct ItemMapStruct
{
    const uint64_t* From;
    const uint64_t* To;
    size_t Count;
} ItemMap;


// Static functions: helpers.
static uint64_t Mix(uint64_t hash, const void* bytes, size_t count)
{
    const unsigned char* Data = bytes;
    for (size_t Index = 0U; Index < count; Index++)
    {
        hash = (hash ^ (uint64_t)Data[Index]) * FINGERPRINT_PRIME;
    }
    return hash;
}

static UserData MakeIdData(uint64_t id)
{
    UserData Data = UserData_CreateEmpty();
    Memory_Copy(&id, Data.Bytes, sizeof(id));
    return Data;
}

static uint64_t ReadIdData(const UserData* data)
{
    uint64_t Id = 0U;
    Memory_Copy(data->Bytes, &Id, sizeof(Id));
    return Id;
}

static uint64_t MapItem(const ItemMap* map, uint64_t item)
{
    for (size_t Index = 0U; Index < map->Count; Index++)
    {
        if (map->From[Index] == item)
        {
            return map->To[Index];
        }
    }
    return UI_DOCK_ITEM_NONE;
}

static void LogWarning(UIScreen* screen, const char* format, const Error* problem)
{
    Logger* Log = UIScreen_GetLogger(screen);
    if (Log == NULL)
    {
        return;
    }
    Error Result = Logger_LogWarningFormatted(Log, (const unsigned char*)format,
        ((problem != NULL) && (problem->Message != NULL)) ? (const char*)problem->Message : "no reason given");
    Error_Deconstruct(&Result);
}

/* Copies a dock's node and what lies under it into another tree, each item through a map (an item it does not map leaves its tab out). */
static Error CopyNode(const UIDockTree* source, int32_t index, size_t depth, UIDockTree* destination, int32_t parent, int32_t place,
    const ItemMap* map)
{
    const UIDockNode* Node = UIDockTree_GetNode(source, index);
    if ((Node == NULL) || (depth > UI_DOCK_TREE_MAX_DEPTH))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace: a dock that does not hold together cannot be copied.");
    }
    int32_t Made = UI_DOCK_NODE_NONE;
    Error Result = UIDockTree_BuildNode(destination, parent, place, Node->Kind, Node->Split, Node->Ratio, &Made);
    if ((Result.Code == ErrorCode_Success) && (Node->Kind == UIDockNodeKind_Split))
    {
        Result = CopyNode(source, Node->Children[0], depth + 1U, destination, Made, 0, map);
        if (Result.Code == ErrorCode_Success)
        {
            Result = CopyNode(source, Node->Children[1], depth + 1U, destination, Made, 1, map);
        }
        return Result;
    }
    for (size_t Tab = 0U; (Result.Code == ErrorCode_Success) && (Tab < UIDockTree_GetTabCount(source, index)); Tab++)
    {
        uint64_t Item = MapItem(map, UIDockTree_GetTabAt(source, index, Tab));
        Result = (Item != UI_DOCK_ITEM_NONE) ? UIDockTree_AddTab(destination, Made, Item, UI_DOCK_TREE_MAX_TABS) : Error_CreateSuccess();
    }
    uint64_t Selected = MapItem(map, Node->Selected);
    if ((Result.Code == ErrorCode_Success) && (Selected != UI_DOCK_ITEM_NONE))
    {
        Result = UIDockTree_SelectTab(destination, Selected);
    }
    return Result;
}

/* A whole dock copied through a map, its areas left empty taken away. */
static Error CopyTree(const UIDockTree* source, UIDockTree* destination, const ItemMap* map)
{
    UIDockTree_Construct(destination);
    Error Result = UIDockTree_IsEmpty(source) ? Error_CreateSuccess()
        : CopyNode(source, UIDockTree_GetRoot(source), 0U, destination, UI_DOCK_NODE_NONE, 0, map);
    if (Result.Code != ErrorCode_Success)
    {
        UIDockTree_Construct(destination);
        return Result;
    }
    UIDockTree_CollapseEmpty(destination);
    return Error_CreateSuccess();
}

/* The window type a window was opened as, when it is one of the workspace's types. */
static bool GetOwnType(UIScreen* screen, const UIWorkspaceCapability* self, uint64_t windowId, uint32_t* outIndex)
{
    UIWindowTypes* Types = NULL;
    Error Result = UIWindow_GetType(screen, windowId, &Types, outIndex);
    bool IsOwn = (Result.Code == ErrorCode_Success) && (Types != NULL) && (Types == self->_types) && (UIWindowTypes_Get(Types, *outIndex) != NULL);
    Error_Deconstruct(&Result);
    return IsOwn;
}


// Static functions: capturing and showing.
/* The workspace's arrangement as a layout, by its windows' types (a window of no type of its own is left out), named as the active layout. */
static Error Capture(UIScreen* screen, uint64_t workspaceId, UILayout* outLayout)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if (Self == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace: the workspace is gone.");
    }
    UILayout_Construct(outLayout);
    const UILayout* Active = (Self->_layouts != NULL) ? UILayouts_Get(Self->_layouts, UILayouts_GetActiveIndex(Self->_layouts)) : NULL;
    Error Result = UILayout_SetName(outLayout, (Active != NULL) ? Active->Name : DEFAULT_LAYOUT_NAME);
    uint64_t From[UI_WORKSPACE_MAX_WINDOWS];
    uint64_t To[UI_WORKSPACE_MAX_WINDOWS];
    size_t Count = 0U;
    for (size_t Index = 0U; (Index < Self->_windowCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        const UIWorkspaceWindow* Record = &Self->_windows[Index];
        uint32_t TypeIndex = 0U;
        if (!GetOwnType(screen, Self, Record->WindowId, &TypeIndex))
        {
            continue;
        }
        Rectangle Box = { 0 };
        bool IsMinimized = false;
        bool IsMaximized = false;
        Result = UIWindow_GetBox(screen, Record->WindowId, &Box);
        if (Result.Code == ErrorCode_Success)
        {
            Result = UIWindow_GetState(screen, Record->WindowId, &IsMinimized, &IsMaximized);
        }
        bool IsFloating = Record->Place == UIWorkspacePlace_Floating;
        size_t LayoutIndex = 0U;
        if (Result.Code == ErrorCode_Success)
        {
            Result = UILayout_AddWindow(outLayout, UIWindowTypes_GetKeyId(Self->_types, TypeIndex), Box, IsFloating && IsMinimized,
                IsFloating ? IsMaximized : (Record->Place == UIWorkspacePlace_Maximized), &LayoutIndex);
        }
        if (Result.Code == ErrorCode_Success)
        {
            From[Count] = Record->WindowId;
            To[Count] = (uint64_t)LayoutIndex + 1U;
            Count++;
        }
    }
    ItemMap Map = { .From = From, .To = To, .Count = Count };
    if (Result.Code == ErrorCode_Success)
    {
        Result = CopyTree(&Self->_tree, &outLayout->Tree, &Map);
    }
    for (uint32_t Type = 0U; (Result.Code == ErrorCode_Success) && (Type < UIWindowTypes_GetCount(Self->_types)); Type++)
    {
        Rectangle Box;
        if ((UIWindowTypes_Get(Self->_types, Type) != NULL) && UIWindowTypes_GetRememberedBox(Self->_types, Type, &Box))
        {
            Result = UILayout_SetPlace(outLayout, UIWindowTypes_GetKeyId(Self->_types, Type), Box);
            if ((Result.Code == ErrorCode_InvalidOperation) || (Result.Code == ErrorCode_ArgumentOutOfRange))
            {
                // More types than a layout keeps places of, or a box no layout keeps: those open centred.
                bool IsFull = Result.Code == ErrorCode_InvalidOperation;
                Error_Deconstruct(&Result);
                Result = Error_CreateSuccess();
                if (IsFull)
                {
                    break;
                }
            }
        }
    }
    return Result;
}

/* Puts a window's state as a layout says: minimized or maximized floating, or neither. */
static Error RestoreState(UIScreen* screen, uint64_t windowId)
{
    for (int32_t Step = 0; Step < 2; Step++)
    {
        bool IsMinimized = false;
        bool IsMaximized = false;
        Error Result = UIWindow_GetState(screen, windowId, &IsMinimized, &IsMaximized);
        if ((Result.Code != ErrorCode_Success) || (!IsMinimized && !IsMaximized))
        {
            return Result;
        }
        Result = UIWindow_Restore(screen, windowId);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* Shows a layout: the windows of the same types it keeps are reused in order, those the layout lacks closed (each asked: one that stays
 * floats), those it has opened; then their boxes, the dock, their states and the types' places. */
static Error ApplyLayout(UIScreen* screen, uint64_t workspaceId, const UILayout* layout)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || (Self->_types == NULL))
    {
        return Error_CreateSuccess();
    }
    uint32_t TypeIndices[UI_LAYOUT_MAX_WINDOWS];
    uint64_t Chosen[UI_LAYOUT_MAX_WINDOWS];
    bool IsUsed[UI_WORKSPACE_MAX_WINDOWS] = { false };
    for (size_t Index = 0U; Index < layout->WindowCount; Index++)
    {
        Chosen[Index] = WIDGET_ID_INVALID;
        bool IsWanted = UIWindowTypes_FindByKeyId(Self->_types, layout->Windows[Index].TypeKeyId, &TypeIndices[Index]);
        for (size_t Record = 0U; IsWanted && (Record < Self->_windowCount); Record++)
        {
            uint32_t Own = 0U;
            if (!IsUsed[Record] && GetOwnType(screen, Self, Self->_windows[Record].WindowId, &Own) && (Own == TypeIndices[Index]))
            {
                IsUsed[Record] = true;
                Chosen[Index] = Self->_windows[Record].WindowId;
                break;
            }
        }
        TypeIndices[Index] = IsWanted ? TypeIndices[Index] : UINT32_MAX;
    }
    uint64_t Closing[UI_WORKSPACE_MAX_WINDOWS];
    size_t ClosingCount = 0U;
    for (size_t Record = 0U; Record < Self->_windowCount; Record++)
    {
        uint32_t Own = 0U;
        if (!IsUsed[Record] && GetOwnType(screen, Self, Self->_windows[Record].WindowId, &Own))
        {
            Closing[ClosingCount++] = Self->_windows[Record].WindowId;
        }
    }
    // Every window out of the dock first: the new dock is built whole.
    UIDockTree_Construct(&Self->_tree);
    for (size_t Record = 0U; Record < Self->_windowCount; Record++)
    {
        Self->_windows[Record].Place = UIWorkspacePlace_Floating;
    }
    Error Result = UIWorkspace_InternalReconcile(screen, workspaceId);
    for (size_t Index = 0U; (Index < ClosingCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = UIScreen_IsWidgetAlive(screen, Closing[Index]) ? UIWindow_Close(screen, Closing[Index], NULL) : Error_CreateSuccess();
    }
    for (size_t Index = 0U; (Index < layout->WindowCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        if ((TypeIndices[Index] == UINT32_MAX) || (Chosen[Index] != WIDGET_ID_INVALID))
        {
            continue;
        }
        Error OpenResult = UIWorkspace_InternalOpenNew(screen, workspaceId, TypeIndices[Index], &Chosen[Index]);
        if (OpenResult.Code != ErrorCode_Success)
        {
            LogWarning(screen, "UI workspace: a layout's window could not open (%s); the layout shows without it.", &OpenResult);
            Error_Deconstruct(&OpenResult);
            Chosen[Index] = WIDGET_ID_INVALID;
        }
    }
    for (size_t Index = 0U; (Index < layout->WindowCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        if (Chosen[Index] == WIDGET_ID_INVALID)
        {
            continue;
        }
        Result = RestoreState(screen, Chosen[Index]);
        if (Result.Code == ErrorCode_Success)
        {
            Result = UIWindow_SetBox(screen, Chosen[Index], layout->Windows[Index].Box);
        }
    }
    uint64_t Items[UI_LAYOUT_MAX_WINDOWS];
    for (size_t Index = 0U; Index < layout->WindowCount; Index++)
    {
        Items[Index] = (uint64_t)Index + 1U;
    }
    ItemMap Map = { .From = Items, .To = Chosen, .Count = layout->WindowCount };
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Result.Code == ErrorCode_Success) && (Self != NULL))
    {
        Result = CopyTree(&layout->Tree, &Self->_tree, &Map);
    }
    for (size_t Record = 0U; (Result.Code == ErrorCode_Success) && (Self != NULL) && (Record < Self->_windowCount); Record++)
    {
        bool IsDocked = UIDockTree_FindTab(&Self->_tree, Self->_windows[Record].WindowId, NULL, NULL);
        Self->_windows[Record].Place = IsDocked ? UIWorkspacePlace_Docked : UIWorkspacePlace_Floating;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_InternalReconcile(screen, workspaceId);
    }
    // The minimized windows first (one maximized and then minimized keeps that it was), then the one that shows maximized: maximizing
    // restores any other that shows so, not one minimized.
    for (int32_t Pass = 0; Pass < 2; Pass++)
    {
        for (size_t Index = 0U; (Index < layout->WindowCount) && (Result.Code == ErrorCode_Success); Index++)
        {
            const UILayoutWindow* Window = &layout->Windows[Index];
            UIWorkspacePlace Place = UIWorkspacePlace_Floating;
            if ((Chosen[Index] == WIDGET_ID_INVALID) || !UIWorkspace_GetPlace(screen, workspaceId, Chosen[Index], &Place))
            {
                continue;
            }
            bool IsMinimizedHere = (Place == UIWorkspacePlace_Floating) && Window->IsMinimized;
            if ((Pass == 0) && IsMinimizedHere)
            {
                Result = Window->IsMaximized ? UIWindow_Maximize(screen, Chosen[Index]) : Error_CreateSuccess();
                if (Result.Code == ErrorCode_Success)
                {
                    Result = UIWindow_Minimize(screen, Chosen[Index]);
                }
            }
            else if ((Pass == 1) && !IsMinimizedHere && Window->IsMaximized)
            {
                Result = (Place == UIWorkspacePlace_Docked) ? UIWorkspace_MaximizeDocked(screen, workspaceId, Chosen[Index])
                    : UIWindow_Maximize(screen, Chosen[Index]);
            }
        }
    }
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    for (size_t Index = 0U; (Self != NULL) && (Result.Code == ErrorCode_Success) && (Index < layout->PlaceCount); Index++)
    {
        uint32_t Type = 0U;
        if (UIWindowTypes_FindByKeyId(Self->_types, layout->Places[Index].TypeKeyId, &Type))
        {
            UIWindowTypes_RememberBox(Self->_types, Type, layout->Places[Index].Box);
        }
    }
    if ((Result.Code == ErrorCode_Success) && (Self != NULL))
    {
        Self->_fingerprint = UIWorkspace_InternalFingerprint(screen, Self);
        Self->_hasFingerprint = true;
    }
    return Result;
}

/* The default, built on the live workspace by the host's function; a failure leaves what it made, logged, and is returned. */
static Error BuildDefault(UIScreen* screen, uint64_t workspaceId)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || (Self->_defaultLayout == NULL))
    {
        return Error_CreateSuccess();
    }
    UserData Data = Self->_defaultLayoutData;
    UIWorkspaceLayoutBuilder Builder = Self->_defaultLayout;
    Error Result = Builder(screen, workspaceId, &Data);
    if (Result.Code != ErrorCode_Success)
    {
        LogWarning(screen, "UI workspace: the default layout could not be built whole (%s); what was built stays.", &Result);
    }
    return Result;
}

/* Keeps the arrangement in the active layout. */
static Error StoreActive(UIScreen* screen, uint64_t workspaceId)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || (Self->_layouts == NULL) || (UILayouts_GetCount(Self->_layouts) == 0U))
    {
        return Error_CreateSuccess();
    }
    UILayout* Layout = Memory_Allocate(sizeof(UILayout));
    Error Result = Capture(screen, workspaceId, Layout);
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Result.Code == ErrorCode_Success) && (Self != NULL) && (Self->_layouts != NULL))
    {
        Result = UILayouts_Store(Self->_layouts, UILayouts_GetActiveIndex(Self->_layouts), Layout);
    }
    Memory_Free(Layout);
    // A failure keeps what the layout had: logged once, until a store succeeds again.
    if ((Self != NULL) && (Result.Code != ErrorCode_Success) && !Self->_isStoreFailureLogged)
    {
        Self->_isStoreFailureLogged = true;
        LogWarning(screen, "UI workspace: the arrangement could not be kept in the active layout (%s); the layout keeps what it had.", &Result);
    }
    else if ((Self != NULL) && (Result.Code == ErrorCode_Success))
    {
        Self->_isStoreFailureLogged = false;
    }
    return Result;
}

/* Keeps the arrangement in the active layout and writes the file: also when keeping failed (the layouts as they were are written, and the
 * change waiting is done with, so a failure is not tried again every tick). */
static Error SaveNow(UIScreen* screen, uint64_t workspaceId)
{
    Error Stored = StoreActive(screen, workspaceId);
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || (Self->_layouts == NULL))
    {
        return Stored;
    }
    Error Saved = UILayouts_Save(Self->_layouts);
    if (Stored.Code == ErrorCode_Success)
    {
        return Saved;
    }
    Error_Deconstruct(&Saved);
    return Stored;
}

static Error SwitchNow(UIScreen* screen, uint64_t workspaceId, size_t index)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || (Self->_layouts == NULL) || (index == UILayouts_GetActiveIndex(Self->_layouts)))
    {
        return Error_CreateSuccess();
    }
    if (index >= UILayouts_GetCount(Self->_layouts))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWorkspace_SwitchLayout: no layout at that index.");
    }
    Error Stored = StoreActive(screen, workspaceId);
    Error_Deconstruct(&Stored); // Logged: the layout left keeps what it had, and the switch goes on.
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || (Self->_layouts == NULL))
    {
        return Error_CreateSuccess();
    }
    Error Result = UILayouts_SetActiveIndex(Self->_layouts, index);
    Self->_layoutsRevision++;
    Self->_isSaveWanted = true;
    // The layout copied out first: showing it runs hooks, which could change the layouts.
    UILayout* Layout = Memory_Allocate(sizeof(UILayout));
    Memory_Copy(UILayouts_Get(Self->_layouts, index), Layout, sizeof(UILayout));
    if (Result.Code == ErrorCode_Success)
    {
        Result = ApplyLayout(screen, workspaceId, Layout);
    }
    Memory_Free(Layout);
    return Result;
}

static Error ResolveWithLayouts(UIScreen* screen, uint64_t workspaceId, UIWorkspaceCapability** outSelf)
{
    *outSelf = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((*outSelf == NULL) || ((*outSelf)->_layouts == NULL))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace: the widget is gone, is no workspace, or has no layouts.");
    }
    return Error_CreateSuccess();
}


// Static functions: the switcher.
static size_t CountLayouts(UIScreen* screen, uint64_t widgetId, const UserData* userData)
{
    (void)widgetId;
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, ReadIdData(userData), NULL);
    return ((Self != NULL) && (Self->_layouts != NULL)) ? UILayouts_GetCount(Self->_layouts) : 0U;
}

static Error WriteLayoutName(UIScreen* screen, uint64_t widgetId, const UserData* userData, size_t index, GenericBuffer* outText)
{
    (void)widgetId;
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, ReadIdData(userData), NULL);
    const UILayout* Layout = ((Self != NULL) && (Self->_layouts != NULL)) ? UILayouts_Get(Self->_layouts, index) : NULL;
    if ((Layout != NULL) && !GenericBuffer_AppendString(outText, Layout->Name))
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "UIWorkspace: a layout's name could not be written.");
    }
    return Error_CreateSuccess();
}

/* A layout chosen: switched to at the workspace's next update (the choice is told at the end of an update, after its layout). */
static Error OnSwitcherEvent(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    if ((event->WidgetEventId != (int32_t)UIElementEvent_Committed) || !event->HasValue || (event->Value.Type != UIPropertyType_Double))
    {
        return Error_CreateSuccess();
    }
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, ReadIdData(userData), NULL);
    double Index = event->Value.Data.DoubleValue;
    if ((Self != NULL) && (Index >= 0.0) && (Index < (double)UI_LAYOUTS_MAX_COUNT))
    {
        Self->_hasPendingSwitch = true;
        Self->_pendingSwitch = (size_t)Index;
    }
    return Error_CreateSuccess();
}


// Internal functions (declared in UIWorkspaceInternal.h).
uint64_t UIWorkspace_InternalDockDigest(const UIDockTree* tree)
{
    uint64_t Hash = FINGERPRINT_OFFSET;
    for (int32_t Node = 0; Node < (int32_t)UI_DOCK_TREE_MAX_NODES; Node++)
    {
        const UIDockNode* Found = UIDockTree_GetNode(tree, Node);
        if (Found == NULL)
        {
            continue;
        }
        int32_t Fields[5] = { Node, (int32_t)Found->Kind, (int32_t)Found->Split, Found->Children[0], Found->Children[1] };
        Hash = Mix(Hash, Fields, sizeof(Fields));
        Hash = Mix(Hash, &Found->Ratio, sizeof(Found->Ratio));
    }
    for (size_t Tab = 0U; Tab < UIDockTree_GetTotalTabCount(tree); Tab++)
    {
        uint64_t Item = UI_DOCK_ITEM_NONE;
        int32_t Leaf = UI_DOCK_NODE_NONE;
        (void)UIDockTree_GetTab(tree, Tab, &Item, &Leaf);
        Hash = Mix(Hash, &Item, sizeof(Item));
        Hash = Mix(Hash, &Leaf, sizeof(Leaf));
    }
    return Hash;
}

uint64_t UIWorkspace_InternalFingerprint(UIScreen* screen, const UIWorkspaceCapability* self)
{
    uint64_t Hash = FINGERPRINT_OFFSET;
    size_t Active = (self->_layouts != NULL) ? UILayouts_GetActiveIndex(self->_layouts) : 0U;
    Hash = Mix(Hash, &Active, sizeof(Active));
    for (size_t Index = 0U; Index < self->_windowCount; Index++)
    {
        const UIWorkspaceWindow* Record = &self->_windows[Index];
        Rectangle Box = { 0 };
        bool States[2] = { false, false };
        Error Result = UIWindow_GetBox(screen, Record->WindowId, &Box);
        Error_Deconstruct(&Result); // Gone: it leaves at the next update anyway.
        Result = UIWindow_GetState(screen, Record->WindowId, &States[0], &States[1]);
        Error_Deconstruct(&Result);
        int32_t Place = (int32_t)Record->Place;
        Hash = Mix(Hash, &Record->WindowId, sizeof(Record->WindowId));
        Hash = Mix(Hash, &Place, sizeof(Place));
        float Numbers[4] = { Box.x, Box.y, Box.width, Box.height };
        Hash = Mix(Hash, Numbers, sizeof(Numbers));
        Hash = Mix(Hash, States, sizeof(States));
    }
    for (int32_t Node = 0; Node < (int32_t)UI_DOCK_TREE_MAX_NODES; Node++)
    {
        const UIDockNode* Found = UIDockTree_GetNode(&self->_tree, Node);
        if (Found == NULL)
        {
            continue;
        }
        int32_t Fields[5] = { Node, (int32_t)Found->Kind, (int32_t)Found->Split, Found->Children[0], Found->Children[1] };
        Hash = Mix(Hash, Fields, sizeof(Fields));
        Hash = Mix(Hash, &Found->Ratio, sizeof(Found->Ratio));
        Hash = Mix(Hash, &Found->Selected, sizeof(Found->Selected));
    }
    for (size_t Tab = 0U; Tab < UIDockTree_GetTotalTabCount(&self->_tree); Tab++)
    {
        uint64_t Item = UI_DOCK_ITEM_NONE;
        int32_t Leaf = UI_DOCK_NODE_NONE;
        (void)UIDockTree_GetTab(&self->_tree, Tab, &Item, &Leaf);
        Hash = Mix(Hash, &Item, sizeof(Item));
        Hash = Mix(Hash, &Leaf, sizeof(Leaf));
    }
    return Hash;
}

Error UIWorkspace_InternalUpdateLayouts(UIScreen* screen, uint64_t workspaceId, double timeSeconds)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || (Self->_layouts == NULL))
    {
        return Error_CreateSuccess();
    }
    Error Result = Error_CreateSuccess();
    if (Self->_hasPendingSwitch)
    {
        Self->_hasPendingSwitch = false;
        Result = SwitchNow(screen, workspaceId, Self->_pendingSwitch);
        if (Result.Code == ErrorCode_ArgumentOutOfRange)
        {
            Error_Deconstruct(&Result); // A layout the switcher listed went meanwhile.
        }
    }
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Result.Code != ErrorCode_Success) || (Self == NULL) || (Self->_layouts == NULL))
    {
        return Result;
    }
    uint64_t Fingerprint = UIWorkspace_InternalFingerprint(screen, Self);
    if (!Self->_hasFingerprint || (Fingerprint != Self->_fingerprint) || Self->_isSaveWanted)
    {
        Self->_fingerprint = Fingerprint;
        Self->_hasFingerprint = true;
        Self->_isSaveWanted = false;
        UILayouts_NoteChange(Self->_layouts, timeSeconds);
    }
    if (UILayouts_IsSaveDue(Self->_layouts, timeSeconds))
    {
        Error SaveResult = SaveNow(screen, workspaceId);
        Error_Deconstruct(&SaveResult); // Logged by the layouts: a full disk does not stop the UI.
    }
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || (Self->_layouts == NULL) || (Self->_switcherRevision == Self->_layoutsRevision)
        || !UIScreen_IsWidgetAlive(screen, Self->_switcherId))
    {
        return Error_CreateSuccess();
    }
    Self->_switcherRevision = Self->_layoutsRevision;
    uint64_t SwitcherId = Self->_switcherId;
    size_t Active = UILayouts_GetActiveIndex(Self->_layouts);
    Result = UIChoice_Refresh(screen, SwitcherId);
    return (Result.Code == ErrorCode_Success) ? UIChoice_SetSelected(screen, SwitcherId, true, Active) : Result;
}


// Public functions.
Error UIWorkspace_SetLayouts(UIScreen* screen, uint64_t workspaceId, UILayouts* layouts, UIWorkspaceLayoutBuilder makeDefault, const UserData* userData)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || (Self->_types == NULL))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_SetLayouts: the widget is gone, is no workspace, or has no window types.");
    }
    Self->_layouts = layouts;
    Self->_defaultLayout = makeDefault;
    Self->_defaultLayoutData = (userData != NULL) ? *userData : UserData_CreateEmpty();
    Self->_layoutsRevision++;
    Self->_hasPendingSwitch = false;
    if (layouts == NULL)
    {
        return Error_CreateSuccess();
    }
    if (UILayouts_GetCount(layouts) > 0U)
    {
        UILayout* Layout = Memory_Allocate(sizeof(UILayout));
        Memory_Copy(UILayouts_Get(layouts, UILayouts_GetActiveIndex(layouts)), Layout, sizeof(UILayout));
        Error Result = ApplyLayout(screen, workspaceId, Layout);
        Memory_Free(Layout);
        return Result;
    }
    Error Built = BuildDefault(screen, workspaceId);
    UILayout* Layout = Memory_Allocate(sizeof(UILayout));
    Error Result = Capture(screen, workspaceId, Layout);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UILayout_SetName(Layout, DEFAULT_LAYOUT_NAME);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UILayouts_Add(layouts, Layout, NULL);
    }
    Memory_Free(Layout);
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Result.Code == ErrorCode_Success) && (Self != NULL))
    {
        Result = UILayouts_SetActiveIndex(layouts, UILayouts_GetCount(layouts) - 1U);
        Self->_isSaveWanted = true;
    }
    if (Result.Code == ErrorCode_Success)
    {
        return Built; // What the builder made is kept either way.
    }
    Error_Deconstruct(&Built);
    return Result;
}

Error UIWorkspace_SwitchLayout(UIScreen* screen, uint64_t workspaceId, size_t index)
{
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWithLayouts(screen, workspaceId, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (index >= UILayouts_GetCount(Self->_layouts))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWorkspace_SwitchLayout: no layout at that index.");
    }
    return SwitchNow(screen, workspaceId, index);
}

Error UIWorkspace_NewLayout(UIScreen* screen, uint64_t workspaceId, const unsigned char* name, size_t* outIndex)
{
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWithLayouts(screen, workspaceId, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (UILayouts_GetCount(Self->_layouts) >= UI_LAYOUTS_MAX_COUNT)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_NewLayout: there are as many layouts as there may be.");
    }
    unsigned char Name[UI_LAYOUT_NAME_CAPACITY];
    UILayouts_MakeName(Self->_layouts, NEW_LAYOUT_NAME, Name);
    size_t Taken = 0U;
    if ((name != NULL) && (!UILayout_IsNameValid(name) || UILayouts_FindByName(Self->_layouts, name, &Taken)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWorkspace_NewLayout: the name is not valid, or another layout has it.");
    }
    // The layout being left keeps the arrangement as it is now, as a switch away from it would (a failure is logged and goes no further).
    Error Stored = StoreActive(screen, workspaceId);
    Error_Deconstruct(&Stored);
    UILayout* Layout = Memory_Allocate(sizeof(UILayout));
    Result = Capture(screen, workspaceId, Layout);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UILayout_SetName(Layout, (name != NULL) ? name : Name);
    }
    size_t Index = 0U;
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Result.Code == ErrorCode_Success) && (Self != NULL) && (Self->_layouts != NULL))
    {
        Result = UILayouts_Add(Self->_layouts, Layout, &Index);
    }
    Memory_Free(Layout);
    if ((Result.Code == ErrorCode_Success) && (Self != NULL) && (Self->_layouts != NULL))
    {
        Result = UILayouts_SetActiveIndex(Self->_layouts, Index);
        Self->_layoutsRevision++;
        Self->_isSaveWanted = true;
    }
    if ((Result.Code == ErrorCode_Success) && (outIndex != NULL))
    {
        *outIndex = Index;
    }
    return Result;
}

Error UIWorkspace_RenameLayout(UIScreen* screen, uint64_t workspaceId, size_t index, const unsigned char* name)
{
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWithLayouts(screen, workspaceId, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UILayouts_Rename(Self->_layouts, index, name);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Self->_layoutsRevision++;
        Self->_isSaveWanted = true;
    }
    return Result;
}

Error UIWorkspace_DeleteLayout(UIScreen* screen, uint64_t workspaceId, size_t index)
{
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWithLayouts(screen, workspaceId, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Count = UILayouts_GetCount(Self->_layouts);
    if (index >= Count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWorkspace_DeleteLayout: no layout at that index.");
    }
    if (Count <= 1U)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_DeleteLayout: the last layout stays.");
    }
    if (index == UILayouts_GetActiveIndex(Self->_layouts))
    {
        Result = SwitchNow(screen, workspaceId, (index > 0U) ? (index - 1U) : 1U);
    }
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Result.Code != ErrorCode_Success) || (Self == NULL) || (Self->_layouts == NULL))
    {
        return Result;
    }
    Result = UILayouts_Remove(Self->_layouts, index);
    Self->_layoutsRevision++;
    Self->_isSaveWanted = true;
    return Result;
}

Error UIWorkspace_ResetLayout(UIScreen* screen, uint64_t workspaceId)
{
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWithLayouts(screen, workspaceId, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t Closing[UI_WORKSPACE_MAX_WINDOWS];
    size_t Count = Self->_windowCount;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Closing[Index] = Self->_windows[Index].WindowId;
    }
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = UIScreen_IsWidgetAlive(screen, Closing[Index]) ? UIWindow_Close(screen, Closing[Index], NULL) : Error_CreateSuccess();
    }
    // What refused to close floats, the dock empty, so the default is built whole.
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Result.Code == ErrorCode_Success) && (Self != NULL))
    {
        UIDockTree_Construct(&Self->_tree);
        for (size_t Index = 0U; Index < Self->_windowCount; Index++)
        {
            Self->_windows[Index].Place = UIWorkspacePlace_Floating;
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_InternalReconcile(screen, workspaceId);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Error Built = BuildDefault(screen, workspaceId);
    Result = StoreActive(screen, workspaceId);
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if (Self != NULL)
    {
        Self->_isSaveWanted = true;
    }
    if (Built.Code == ErrorCode_Success)
    {
        return Result;
    }
    Error_Deconstruct(&Result);
    return Built;
}

Error UIWorkspace_SaveLayouts(UIScreen* screen, uint64_t workspaceId)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if (Self == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_SaveLayouts: the widget is gone, or is no workspace.");
    }
    return (Self->_layouts != NULL) ? SaveNow(screen, workspaceId) : Error_CreateSuccess();
}

Error UIWorkspace_CreateLayoutSwitcher(UIKit* kit, UIScreen* screen, uint64_t parentId, uint64_t workspaceId, uint64_t* outId)
{
    if ((kit == NULL) || (screen == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWorkspace_CreateLayoutSwitcher: kit, screen and outId must not be NULL.");
    }
    *outId = WIDGET_ID_INVALID;
    if (!UIWorkspace_IsWorkspace(screen, workspaceId))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_CreateLayoutSwitcher: the workspace is gone.");
    }
    UIDropdownOptions Options = UIDropdownOptions_Create();
    Options.Direction = UIDropdownDirection_Auto;
    uint64_t Id = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateDropdown(kit, screen, parentId, &Options, &Id);
    UserData Data = MakeIdData(workspaceId);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIChoice_SetProvider(screen, Id, CountLayouts, WriteLayoutName, &Data);
    }
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_Subscribe(screen, Id, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnSwitcherEvent, &Data, &Subscription);
    }
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Result.Code == ErrorCode_Success) && (Self != NULL) && (Self->_layouts != NULL) && (UILayouts_GetCount(Self->_layouts) > 0U))
    {
        Result = UIChoice_SetSelected(screen, Id, true, UILayouts_GetActiveIndex(Self->_layouts));
    }
    Widget* Made = NULL;
    if (Result.Code != ErrorCode_Success)
    {
        if (UIScreen_TryGetWidgetById(screen, Id, &Made))
        {
            Error Cleanup = Widget_Destroy(Made);
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }
    if (Self != NULL)
    {
        Self->_switcherId = Id;
        Self->_switcherRevision = Self->_layoutsRevision;
    }
    *outId = Id;
    return Error_CreateSuccess();
}
