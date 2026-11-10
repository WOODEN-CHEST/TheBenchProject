#include "UIWindowTypes.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "NamespacedKey.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** How many times a new window steps down and to the right looking for a place no other window's top-left takes. */
#define MAX_CASCADE_STEPS 32
/** How close two top-lefts are to count as the same place, in UI pixels. */
#define SAME_PLACE_DISTANCE 1.0f


// Static functions: helpers.
static void DeconstructType(void* element, const UserData* userData)
{
    (void)userData;
    UIWindowType* Type = element;
    Memory_Free(Type->_displayName);
    Type->_displayName = NULL;
}

static bool IsFiniteFloat(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

static bool IsDefinitionValid(const UIWindowTypeDefinition* definition)
{
    if ((definition->DisplayName == NULL) || (definition->Fill == NULL) || !StringUTF8_IsEncodingValid(definition->DisplayName))
    {
        return false;
    }
    size_t NameBytes = StringUTF8_GetByteLength(definition->DisplayName);
    bool IsNameValid = (NameBytes > 0U) && (NameBytes <= UI_WINDOW_TYPES_MAX_NAME_BYTES);
    bool IsIconValid = (definition->Icon.Kind == UIElementIconKind_None) || UIElementIcon_IsValid(&definition->Icon);
    bool AreFitsValid = ((definition->FitAcross == UIWindowFit_Fit) || (definition->FitAcross == UIWindowFit_Scroll))
        && ((definition->FitDown == UIWindowFit_Fit) || (definition->FitDown == UIWindowFit_Scroll));
    bool IsSizeValid = UILayoutLength_IsValid(definition->DefaultSize.x) && UILayoutLength_IsValid(definition->DefaultSize.y)
        && (definition->DefaultSize.x > 0.0f) && (definition->DefaultSize.y > 0.0f);
    bool IsMinimumValid = UILayoutLength_IsValid(definition->MinimumSize.x) && UILayoutLength_IsValid(definition->MinimumSize.y);
    // A variant's name as the theme writes it, the rule its windows are created with (a name they refuse would fail every opening).
    size_t VariantBytes = (definition->Variant != NULL) ? StringUTF8_GetByteLength(definition->Variant) : 0U;
    bool IsVariantValid = (definition->Variant == NULL)
        || ((VariantBytes < UI_THEME_VARIANT_NAME_CAPACITY) && NamespacedKey_IsValidLocalName(definition->Variant));
    return IsNameValid && IsIconValid && AreFitsValid && IsSizeValid && IsMinimumValid && IsVariantValid;
}

/* Each slot of the registry has a place: new slots get an empty one. */
static bool EnsurePlaces(UIWindowTypes* self)
{
    size_t Count = (size_t)Registry_GetCount(&self->_types);
    UIWindowTypePlace Empty = { .HasBox = false, .Box = { .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f } };
    while (self->_places._count < Count)
    {
        if (!GenericBuffer_AddLast(&self->_places, &Empty))
        {
            return false;
        }
    }
    return true;
}

/* The first window of a type in a subtree, depth first; bounded by the tree's depth limit. */
static bool FindInTree(UIScreen* screen, Widget* widget, uint64_t capabilityId, const UIWindowTypes* types, uint32_t index, size_t depth,
    uint64_t* outWindowId)
{
    if ((widget == NULL) || (depth > WIDGET_MAX_TREE_DEPTH))
    {
        return false;
    }
    const UIWindowCapability* Window = Widget_GetCapability(widget, capabilityId);
    if ((Window != NULL) && (Window->_types == types) && (Window->_typeIndex == index))
    {
        *outWindowId = Widget_GetId(widget);
        return true;
    }
    for (size_t Child = 0U; Child < Widget_GetSubWidgetCount(widget); Child++)
    {
        if (FindInTree(screen, Widget_GetSubWidgetAt(widget, Child), capabilityId, types, index, depth + 1U, outWindowId))
        {
            return true;
        }
    }
    return false;
}

/* Whether a window of the screen stands with its top-left just there. */
static bool IsPlaceTaken(UIScreen* screen, uint64_t capabilityId, float x, float y)
{
    for (size_t Index = 0U; Index < UIScreen_GetRootCount(screen); Index++)
    {
        Widget* Root = UIScreen_GetRootAt(screen, Index);
        const UIWindowCapability* Window = (Root != NULL) ? Widget_GetCapability(Root, capabilityId) : NULL;
        if ((Window != NULL) && (Math_AbsFloat(Window->_floatingBox.x - x) < SAME_PLACE_DISTANCE)
            && (Math_AbsFloat(Window->_floatingBox.y - y) < SAME_PLACE_DISTANCE))
        {
            return true;
        }
    }
    return false;
}

/* Where a new window of a size opens with no place remembered: centred on the workspace. */
static Vector2 FindCentredPlace(UIScreen* screen, Vector2 size)
{
    Rectangle Workspace = UIScreen_GetWorkspaceBox(screen);
    return (Vector2)
    {
        .x = Math_FloorFloat(Workspace.x + ((Workspace.width - size.x) * 0.5f)),
        .y = Math_FloorFloat(Workspace.y + ((Workspace.height - size.y) * 0.5f))
    };
}

/* A place stepped down and to the right while another window's top-left is there, so a new window never hides one just under it. */
static Vector2 StepAside(UIScreen* screen, uint64_t capabilityId, Vector2 place)
{
    Vector2 Place = place;
    for (int32_t Step = 0; (Step < MAX_CASCADE_STEPS) && IsPlaceTaken(screen, capabilityId, Place.x, Place.y); Step++)
    {
        Place.x += UI_WINDOW_TYPES_CASCADE_STEP;
        Place.y += UI_WINDOW_TYPES_CASCADE_STEP;
    }
    return Place;
}

/* Brings a single type's open window back: restored if minimized, to the front and focused. */
static Error BringBack(UIScreen* screen, uint64_t windowId)
{
    bool IsMinimized = false;
    Error Result = UIWindow_GetState(screen, windowId, &IsMinimized, NULL);
    if ((Result.Code == ErrorCode_Success) && IsMinimized)
    {
        Result = UIWindow_Restore(screen, windowId);
    }
    return (Result.Code == ErrorCode_Success) ? UIWindow_Focus(screen, windowId) : Result;
}


// Public functions: lifecycle.
Error UIWindowTypes_Construct(UIWindowTypes* self, KeyRegistry* keys, RegistryRoot* root)
{
    if ((self == NULL) || (keys == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWindowTypes_Construct: self and keys must not be NULL.");
    }
    Memory_Zero(self, sizeof(*self));
    uint32_t KeyId = NAMESPACED_KEY_ID_NONE;
    Error Result = KeyRegistry_Intern(keys, UI_WINDOW_TYPES_REGISTRY_KEY, &KeyId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    RegistryConstructOptions Options = RegistryConstructOptions_CreateDefault(keys, KeyId, sizeof(UIWindowType), UI_WINDOW_TYPES_MAX_COUNT);
    Options.DeconstructElement = DeconstructType;
    Result = Registry_Construct(&self->_types, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GenericBuffer_AllocateVariable(&self->_places, 8U, sizeof(UIWindowTypePlace));
    self->_keys = keys;
    if (root != NULL)
    {
        Result = RegistryRoot_Add(root, &self->_types);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(self->_places._data);
            Registry_Deconstruct(&self->_types);
            Memory_Zero(self, sizeof(*self));
            return Result;
        }
        self->_root = root;
    }
    return Error_CreateSuccess();
}

void UIWindowTypes_Deconstruct(UIWindowTypes* self)
{
    if (self == NULL)
    {
        return;
    }
    if (self->_root != NULL)
    {
        Error Result = RegistryRoot_Withdraw(self->_root, &self->_types);
        Error_Deconstruct(&Result); // Only fails for a registry the root does not hold: then there is nothing to withdraw.
        self->_root = NULL;
    }
    Registry_Deconstruct(&self->_types);
    Memory_Free(self->_places._data);
    Memory_Zero(&self->_places, sizeof(self->_places));
}


// Public functions: types.
Error UIWindowTypes_Register(UIWindowTypes* self, const unsigned char* key, const UIWindowTypeDefinition* definition, uint32_t sourceKeyId,
    uint32_t* outIndex)
{
    if ((self == NULL) || (key == NULL) || (definition == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWindowTypes_Register: self, key and definition must not be NULL.");
    }
    if (!IsDefinitionValid(definition))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWindowTypes_Register: the definition needs a name of 1 to "
            "UI_WINDOW_TYPES_MAX_NAME_BYTES bytes of UTF-8, a Fill, known fits, a size above 0, a least size >= 0, a valid icon and no variant "
            "or one of 1 to 31 of a-z, 0-9, _, . and -, starting with a letter.");
    }
    uint32_t KeyId = NAMESPACED_KEY_ID_NONE;
    Error Result = KeyRegistry_InternText(self->_keys, key, StringUTF8_GetByteLength(key), NULL, &KeyId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t NameBytes = StringUTF8_GetByteLength(definition->DisplayName) + 1U;
    UIWindowType Type =
    {
        ._displayName = Memory_Allocate(NameBytes),
        ._icon = definition->Icon,
        ._fill = definition->Fill,
        ._data = definition->Data,
        ._fitAcross = definition->FitAcross,
        ._fitDown = definition->FitDown,
        ._defaultSize = definition->DefaultSize,
        ._minimumSize = definition->MinimumSize,
        ._isSingle = definition->IsSingle,
        ._hasVariant = definition->Variant != NULL
    };
    Memory_Copy(definition->DisplayName, Type._displayName, NameBytes);
    if (Type._hasVariant)
    {
        Memory_Copy(definition->Variant, Type._variant, StringUTF8_GetByteLength(definition->Variant) + 1U);
    }
    uint32_t Index = REGISTRY_INDEX_NONE;
    Result = Registry_Register(&self->_types, KeyId, &Type, sourceKeyId, &Index);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Type._displayName); // Not moved in: still the caller's to release.
        return Result;
    }
    if (!EnsurePlaces(self))
    {
        Error Withdraw = Registry_Withdraw(&self->_types, Index);
        Error_Deconstruct(&Withdraw);
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIWindowTypes_Register: the places could not grow.");
    }
    if (outIndex != NULL)
    {
        *outIndex = Index;
    }
    return Error_CreateSuccess();
}

Error UIWindowTypes_Withdraw(UIWindowTypes* self, uint32_t index)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWindowTypes_Withdraw: self must not be NULL.");
    }
    return Registry_Withdraw(&self->_types, index);
}

uint32_t UIWindowTypes_WithdrawSource(UIWindowTypes* self, uint32_t sourceKeyId)
{
    return (self != NULL) ? Registry_WithdrawSource(&self->_types, sourceKeyId) : 0U;
}

bool UIWindowTypes_Find(UIWindowTypes* self, const unsigned char* key, uint32_t* outIndex)
{
    if ((self == NULL) || (key == NULL) || (outIndex == NULL))
    {
        return false;
    }
    // A key as people write it, perhaps without its namespace, in its full form first.
    unsigned char Storage[NAMESPACED_KEY_BUFFER_SIZE];
    GenericBuffer FullKey;
    GenericBuffer_CreateConstant(&FullKey, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    Error Result = NamespacedKey_Parse(key, StringUTF8_GetByteLength(key), NULL, &FullKey);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return false; // No key: no type has it.
    }
    return Registry_FindText(&self->_types, Storage, outIndex);
}

bool UIWindowTypes_FindByKeyId(UIWindowTypes* self, uint32_t keyId, uint32_t* outIndex)
{
    return (self != NULL) && (outIndex != NULL) && (keyId != NAMESPACED_KEY_ID_NONE) && Registry_Find(&self->_types, keyId, outIndex);
}

uint32_t UIWindowTypes_GetKeyId(const UIWindowTypes* self, uint32_t index)
{
    return ((self != NULL) && (index < Registry_GetCount(&self->_types))) ? Registry_GetKey(&self->_types, index) : NAMESPACED_KEY_ID_NONE;
}

const UIWindowType* UIWindowTypes_Get(const UIWindowTypes* self, uint32_t index)
{
    return ((self != NULL) && Registry_IsActive(&self->_types, index)) ? Registry_Get(&self->_types, index) : NULL;
}


// Public functions: opening.
Error UIWindowTypes_Open(UIWindowTypes* self, UIScreen* screen, const unsigned char* key, uint64_t* outWindowId)
{
    if ((self == NULL) || (screen == NULL) || (key == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWindowTypes_Open: self, screen and key must not be NULL.");
    }
    uint32_t Index = REGISTRY_INDEX_NONE;
    if (!UIWindowTypes_Find(self, key, &Index))
    {
        if (outWindowId != NULL)
        {
            *outWindowId = WIDGET_ID_INVALID;
        }
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWindowTypes_Open: no window type has that key.");
    }
    return UIWindowTypes_OpenAt(self, screen, Index, outWindowId);
}

Error UIWindowTypes_OpenAt(UIWindowTypes* self, UIScreen* screen, uint32_t index, uint64_t* outWindowId)
{
    if (outWindowId != NULL)
    {
        *outWindowId = WIDGET_ID_INVALID;
    }
    if ((self == NULL) || (screen == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWindowTypes_OpenAt: self and screen must not be NULL.");
    }
    const UIWindowType* Type = UIWindowTypes_Get(self, index);
    UIKit* Kit = UIKit_FromScreen(screen);
    if ((Type == NULL) || (Kit == NULL))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWindowTypes_OpenAt: no active window type at that index, or a screen with no kit.");
    }
    uint64_t WindowId = WIDGET_ID_INVALID;
    if (Type->_isSingle && UIWindowTypes_FindOpen(self, screen, index, &WindowId))
    {
        Error Result = BringBack(screen, WindowId);
        if ((Result.Code == ErrorCode_Success) && (outWindowId != NULL))
        {
            *outWindowId = WindowId;
        }
        return Result;
    }
    // Copied first: making the window and filling it run hooks, which could withdraw the type.
    UIWindowTypeFill Fill = Type->_fill;
    UserData Data = Type->_data;
    UIWindowOptions Options = UIWindowOptions_Create(Type->_displayName);
    unsigned char Variant[UI_THEME_VARIANT_NAME_CAPACITY];
    Memory_Copy(Type->_variant, Variant, sizeof(Variant));
    Options.Element.Variant = Type->_hasVariant ? Variant : NULL;
    Options.Icon = Type->_icon;
    Options.FitAcross = Type->_fitAcross;
    Options.FitDown = Type->_fitDown;
    Options.Size = Type->_defaultSize;
    Options.MinimumSize = Type->_minimumSize;
    Rectangle Remembered;
    if (UIWindowTypes_GetRememberedBox(self, index, &Remembered))
    {
        Options.Position = (Vector2){ .x = Remembered.x, .y = Remembered.y };
        Options.Size = (Vector2){ .x = Remembered.width, .y = Remembered.height };
    }
    else
    {
        Options.Position = FindCentredPlace(screen, Options.Size);
    }
    Options.Position = StepAside(screen, UIKit_GetCapabilityId(Kit, UIElementCapability_Window), Options.Position);
    Options.HasPosition = true;
    uint64_t ContentId = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateWindow(Kit, screen, &Options, &WindowId, &ContentId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIWindow_SetType(screen, WindowId, self, index);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Fill(screen, WindowId, ContentId, &Data);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_IsWidgetAlive(screen, WindowId) ? UIWindow_Focus(screen, WindowId) : Error_CreateSuccess();
    }
    Widget* Made = NULL;
    if (Result.Code != ErrorCode_Success)
    {
        if (UIScreen_TryGetWidgetById(screen, WindowId, &Made))
        {
            Error Cleanup = Widget_Destroy(Made);
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }
    if ((outWindowId != NULL) && UIScreen_IsWidgetAlive(screen, WindowId))
    {
        *outWindowId = WindowId;
    }
    return Error_CreateSuccess();
}

bool UIWindowTypes_FindOpen(UIWindowTypes* self, UIScreen* screen, uint32_t index, uint64_t* outWindowId)
{
    UIKit* Kit = ((self != NULL) && (screen != NULL)) ? UIKit_FromScreen(screen) : NULL;
    if ((Kit == NULL) || (outWindowId == NULL))
    {
        return false;
    }
    uint64_t Capability = UIKit_GetCapabilityId(Kit, UIElementCapability_Window);
    for (size_t Root = 0U; Root < UIScreen_GetRootCount(screen); Root++)
    {
        if (FindInTree(screen, UIScreen_GetRootAt(screen, Root), Capability, self, index, 0U, outWindowId))
        {
            return true;
        }
    }
    return false;
}

void UIWindowTypes_RememberBox(UIWindowTypes* self, uint32_t index, Rectangle box)
{
    if ((self == NULL) || ((size_t)index >= self->_places._count))
    {
        return;
    }
    UIWindowTypePlace* Place = GenericBuffer_GetPointerToElement(&self->_places, (size_t)index);
    Place->HasBox = IsFiniteFloat(box.x) && IsFiniteFloat(box.y) && (Math_AbsFloat(box.x) <= UI_LAYOUT_MAX_LENGTH)
        && (Math_AbsFloat(box.y) <= UI_LAYOUT_MAX_LENGTH) && UILayoutLength_IsValid(box.width) && UILayoutLength_IsValid(box.height)
        && (box.width > 0.0f) && (box.height > 0.0f);
    Place->Box = box;
}

bool UIWindowTypes_GetRememberedBox(const UIWindowTypes* self, uint32_t index, Rectangle* outBox)
{
    if ((self == NULL) || (outBox == NULL) || ((size_t)index >= self->_places._count))
    {
        return false;
    }
    const UIWindowTypePlace* Place = GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_places, (size_t)index);
    if (Place->HasBox)
    {
        *outBox = Place->Box;
    }
    return Place->HasBox;
}


// Public functions: a Window menu.
Error UIWindowTypes_WriteMenuItems(const UIWindowTypes* self, uint32_t firstItemId, UIMenuItem* outItems, size_t capacity, size_t* outCount)
{
    if ((self == NULL) || (outCount == NULL) || ((outItems == NULL) && (capacity > 0U)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWindowTypes_WriteMenuItems: self, outCount and the items must not be NULL.");
    }
    *outCount = 0U;
    uint32_t Count = Registry_GetCount(&self->_types);
    if ((Count > 0U) && ((uint32_t)(UINT32_MAX - firstItemId) < (Count - 1U)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWindowTypes_WriteMenuItems: the items' ids would pass UINT32_MAX.");
    }
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        const UIWindowType* Type = UIWindowTypes_Get(self, Index);
        if (Type == NULL)
        {
            continue;
        }
        if (*outCount >= capacity)
        {
            return Error_Construct2(ErrorCode_BufferTooSmall, "UIWindowTypes_WriteMenuItems: more window types than the items hold.");
        }
        UIMenuItem Item = UIMenuItem_Create(firstItemId + Index, Type->_displayName);
        Item.Icon = Type->_icon;
        outItems[*outCount] = Item;
        (*outCount)++;
    }
    return Error_CreateSuccess();
}
