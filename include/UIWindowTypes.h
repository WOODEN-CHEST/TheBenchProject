#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"
#include "NamespacedKey.h"
#include "Registry.h"
#include "UIElement.h"
#include "UIMenu.h"
#include "UIWindow.h"


/**
 * @file UIWindowTypes.h
 * @brief Window types: the windows a UI opens by key (tbp:editor_outliner), each registered with what makes one, so a Window menu lists
 *        them, a layout can open them again, and packs and scripts can add their own.
 *
 * A TYPE is a namespaced key, a display name, an icon, a function that fills a new window's content, the window's fits, its default size
 * and its least size, and whether only one of it may be open on a screen. UIWindowTypes holds them in a Registry under
 * UI_WINDOW_TYPES_REGISTRY_KEY (Registry.md: stable indices, a source per entry, withdrawn entries keeping their slots), added to the
 * program's root registry, where tools find it. The program makes one (main.c) and Services carries it; a frame registers the types it
 * offers while it runs, and withdraws them (by its source) when it goes.
 *
 * OPENING (UIWindowTypes_Open): a single type's window that is open on the screen already is restored if it is minimized, brought to the
 * front and given the focus; otherwise a new window is made (UIKit_CreateWindow) where the type's last window was when it closed, or with
 * the type's default size centred on the workspace (either moved down and to the right while another window's top-left stands just
 * there), filled by the type's function, and given the focus (UIWindow_Focus: what the fill focused inside keeps it, else the window's
 * default focus target, else the window). Each type remembers the floating box of its last window that closed (UIWindow_Close); the
 * record belongs to the type, so it holds for any screen. A fill may close its own window: the open succeeds with no window then.
 *
 * A Window menu lists the types (UIWindowTypes_WriteMenuItems), one item per active type, its id the type's index after a first id the
 * caller picks; the menu's handler opens the type of the item it is told (UIWindowTypes_OpenAt).
 *
 * Main thread only, as the registries are.
 */


// Macros.
/** @brief The window types' registry's key, under which the root lists it. */
#define UI_WINDOW_TYPES_REGISTRY_KEY ((const unsigned char*)u8"tbp:ui_window_type")
/** @brief The most window types, withdrawn ones included. */
#define UI_WINDOW_TYPES_MAX_COUNT ((uint32_t)1024)
/** @brief The longest display name, in bytes. */
#define UI_WINDOW_TYPES_MAX_NAME_BYTES ((size_t)128)
/** @brief How far a new window is moved down and to the right while another stands just where it would open, in the screen's UI pixels. */
#define UI_WINDOW_TYPES_CASCADE_STEP 24.0f


// Types.
/**
 * @brief Fills a new window of a type: what calling code puts in its content. Called once, right after the window is made, as a hook is.
 * @param screen The window's screen.
 * @param windowId The window (UIWindow_SetDefaultFocus, UIWindow_SetCloseQuery and the rest work on it).
 * @param contentId The window's content column, where its elements go.
 * @param userData The type's data (its stored copy).
 * @returns Success, or an error: the window is then destroyed and UIWindowTypes_Open returns it.
 */
typedef Error (*UIWindowTypeFill)(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData);

/**
 * @brief What a window type is registered with. A plain value; build one with UIWindowTypeDefinition_Create.
 */
typedef struct UIWindowTypeDefinitionStruct
{
    /** @brief Its name in menus and on its windows' title bars: UTF-8, at most UI_WINDOW_TYPES_MAX_NAME_BYTES bytes, not empty. Copied. */
    const unsigned char* DisplayName;
    /** @brief Its icon, on its windows' title bars and in menus; none by default. */
    UIElementIcon Icon;
    /** @brief Fills a new window's content; must not be NULL. */
    UIWindowTypeFill Fill;
    /** @brief Handed to Fill; copied. */
    UserData Data;
    /** @brief How its windows' content takes the width and the height. */
    UIWindowFit FitAcross;
    UIWindowFit FitDown;
    /** @brief A new window's size, in the screen's UI pixels; each finite and above 0. */
    Vector2 DefaultSize;
    /** @brief Its windows' least size, in their own UI pixels; each finite, >= 0 and at most UI_LAYOUT_MAX_LENGTH. */
    Vector2 MinimumSize;
    /** @brief Whether only one of it may be open on a screen: opening it again brings that one to the front. */
    bool IsSingle;
} UIWindowTypeDefinition;

/**
 * @brief A registered window type, as the registry holds it. Read-only outside the module: read it through UIWindowTypes_Get.
 */
typedef struct UIWindowTypeStruct
{
    /** @brief The display name, NUL-terminated UTF-8; owned. */
    unsigned char* _displayName;
    /** @brief The icon. */
    UIElementIcon _icon;
    /** @brief Fills a new window's content, with its data. */
    UIWindowTypeFill _fill;
    UserData _data;
    /** @brief Its windows' fits. */
    UIWindowFit _fitAcross;
    UIWindowFit _fitDown;
    /** @brief A new window's size, the screen's UI pixels, and its windows' least size, their own. */
    Vector2 _defaultSize;
    Vector2 _minimumSize;
    /** @brief Whether only one may be open on a screen. */
    bool _isSingle;
} UIWindowType;

/**
 * @brief Where a type's last window was when it closed. Internal to the registry.
 */
typedef struct UIWindowTypePlaceStruct
{
    /** @brief Whether a window of the type closed, so Box means something. */
    bool HasBox;
    /** @brief Its floating box, in its screen's UI pixels. */
    Rectangle Box;
} UIWindowTypePlace;

/**
 * @brief The window types. Construct in place with UIWindowTypes_Construct and release with UIWindowTypes_Deconstruct, after every window
 *        opened through it is gone. Its fields are internal.
 */
typedef struct UIWindowTypesStruct
{
    /** @brief The types (UIWindowType), under UI_WINDOW_TYPES_REGISTRY_KEY. */
    Registry _types;
    /** @brief Each type's place (UIWindowTypePlace), by its index. Owned. */
    GenericBuffer _places;
    /** @brief The program's key registry, borrowed. */
    KeyRegistry* _keys;
    /** @brief The root the registry was added to, borrowed; NULL for none. */
    RegistryRoot* _root;
} UIWindowTypes;


// Helpers.
/**
 * @brief A definition with a name and what fills its windows: no icon, no data, fitting across and scrolling down, 360 x 260 UI pixels, no
 *        least size of its own, any number open.
 * @param displayName The name (borrowed until it is registered).
 * @param fill What fills a new window.
 * @returns The definition.
 */
static inline UIWindowTypeDefinition UIWindowTypeDefinition_Create(const unsigned char* displayName, UIWindowTypeFill fill)
{
    UIWindowTypeDefinition Definition =
    {
        .DisplayName = displayName,
        .Fill = fill,
        .Data = UserData_CreateEmpty(),
        .FitAcross = UIWindowFit_Fit,
        .FitDown = UIWindowFit_Scroll,
        .DefaultSize = { .x = 360.0f, .y = 260.0f },
        .MinimumSize = { .x = 0.0f, .y = 0.0f },
        .IsSingle = false
    };
    Definition.Icon = UIElementIcon_None();
    return Definition;
}


// Lifecycle.
/**
 * @brief Constructs the registry, empty, and adds it to a root registry.
 * @param self The registry; must not be NULL, and must not move afterwards (the root holds its address).
 * @param keys The program's key registry; borrowed, it must outlive the registry. Must not be NULL.
 * @param root The root registry to add it to, borrowed until UIWindowTypes_Deconstruct; NULL for none (tests).
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL; ErrorCode_InvalidOperation when the root holds a registry under
 *          the key already; otherwise the registry's error. On a failure nothing stays.
 */
Error UIWindowTypes_Construct(UIWindowTypes* self, KeyRegistry* keys, RegistryRoot* root);

/**
 * @brief Withdraws the registry from its root and releases it, with every type.
 * @param self The registry; must not be NULL.
 */
void UIWindowTypes_Deconstruct(UIWindowTypes* self);

/**
 * @brief Returns the registry itself, for tools that list or search the keys (Registry.h).
 * @param self The window types; must not be NULL.
 * @returns The registry.
 */
static inline const Registry* UIWindowTypes_AsRegistry(const UIWindowTypes* self)
{
    return &self->_types;
}


// Types.
/**
 * @brief Registers a window type.
 * @param self The registry; must not be NULL.
 * @param key Its key as people write it (`tbp:editor_outliner`; one without a namespace is tbp's); must not be NULL.
 * @param definition What it is; must not be NULL. Read for the call (its name is copied).
 * @param sourceKeyId Who registers it (UIWindowTypes_WithdrawSource withdraws them together): NAMESPACED_KEY_ID_NONE for the engine, or a
 *        key id of the registry's key registry.
 * @param outIndex [out] Receives its index; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_ArgumentOutOfRange for a key or a definition that is not
 *          valid (an empty or too long name, no Fill, an unknown fit, a size not above 0, a least size below 0, a number not finite, an icon
 *          not valid); ErrorCode_InvalidOperation for a key that is registered and active already, or a full registry; otherwise the key
 *          registry's error.
 */
Error UIWindowTypes_Register(UIWindowTypes* self, const unsigned char* key, const UIWindowTypeDefinition* definition, uint32_t sourceKeyId,
    uint32_t* outIndex);

/**
 * @brief Withdraws a type: it is not listed or opened any more, and its index stays its own (registering the key again revives it). Its
 *        windows that are open stay as they are.
 * @param self The registry; must not be NULL.
 * @param index The type's index.
 * @returns Success; the registry's error for an index it does not have or one withdrawn already.
 */
Error UIWindowTypes_Withdraw(UIWindowTypes* self, uint32_t index);

/**
 * @brief Withdraws every active type a source registered.
 * @param self The registry; must not be NULL.
 * @param sourceKeyId The source.
 * @returns How many were withdrawn.
 */
uint32_t UIWindowTypes_WithdrawSource(UIWindowTypes* self, uint32_t sourceKeyId);

/**
 * @brief Finds an active type by its key, without interning it.
 * @param self The registry; must not be NULL.
 * @param key The key as people write it; must not be NULL.
 * @param outIndex [out] Receives its index. Must not be NULL.
 * @returns true when found.
 */
bool UIWindowTypes_Find(UIWindowTypes* self, const unsigned char* key, uint32_t* outIndex);

/**
 * @brief Reads an active type.
 * @param self The registry; must not be NULL.
 * @param index The type's index.
 * @returns The type, valid until it is withdrawn or the registry goes; NULL for an index that is not active.
 */
const UIWindowType* UIWindowTypes_Get(const UIWindowTypes* self, uint32_t index);

/**
 * @brief Returns how many slots the registry has, withdrawn ones included: indices run below it.
 * @param self The registry; must not be NULL.
 * @returns The count.
 */
static inline uint32_t UIWindowTypes_GetCount(const UIWindowTypes* self)
{
    return Registry_GetCount(&self->_types);
}


// Opening.
/**
 * @brief Opens a window of a type by its key (above, OPENING).
 * @param self The registry; must not be NULL.
 * @param screen The screen; must not be NULL. Its factory must have a kit.
 * @param key The type's key; must not be NULL.
 * @param outWindowId [out] Receives the window (the one open already for a single type), or WIDGET_ID_INVALID when the fill closed it; may
 *        be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL; ErrorCode_InvalidOperation for a key no active type has or a
 *          screen with no kit; otherwise the error of making the window or of the type's Fill (the window is gone then).
 */
Error UIWindowTypes_Open(UIWindowTypes* self, UIScreen* screen, const unsigned char* key, uint64_t* outWindowId);

/**
 * @brief Opens a window of a type by its index (above, OPENING): what a Window menu's handler calls.
 * @param self The registry; must not be NULL.
 * @param screen The screen; must not be NULL.
 * @param index The type's index.
 * @param outWindowId [out] Receives the window; may be NULL.
 * @returns As UIWindowTypes_Open, ErrorCode_InvalidOperation for an index that is not active.
 */
Error UIWindowTypes_OpenAt(UIWindowTypes* self, UIScreen* screen, uint32_t index, uint64_t* outWindowId);

/**
 * @brief Finds a window of a type open on a screen: anywhere in its tree (floating, maximized, minimized, or inside another widget).
 * @param self The registry; must not be NULL.
 * @param screen The screen; must not be NULL.
 * @param index The type's index.
 * @param outWindowId [out] Receives the first one found. Must not be NULL.
 * @returns true when one is open.
 */
bool UIWindowTypes_FindOpen(UIWindowTypes* self, UIScreen* screen, uint32_t index, uint64_t* outWindowId);

/**
 * @brief Remembers where a type's window was when it closed: the next one opens there. UIWindow_CloseNow calls it.
 * @param self The registry; must not be NULL.
 * @param index The type's index; one the registry does not have is ignored.
 * @param box The window's floating box, in its screen's UI pixels.
 */
void UIWindowTypes_RememberBox(UIWindowTypes* self, uint32_t index, Rectangle box);

/**
 * @brief Reads where a type's last window was when it closed.
 * @param self The registry; must not be NULL.
 * @param index The type's index.
 * @param outBox [out] Receives the box when there is one. Must not be NULL.
 * @returns true when a window of the type closed.
 */
bool UIWindowTypes_GetRememberedBox(const UIWindowTypes* self, uint32_t index, Rectangle* outBox);


// A Window menu.
/**
 * @brief Writes a menu item for each active type, in index order: its display name, its icon, and the id @p firstItemId plus its index. The
 *        labels are the registry's, valid while the types stay registered (a menu copies them when it opens).
 * @param self The registry; must not be NULL.
 * @param firstItemId The id of index 0's item.
 * @param outItems [out] Receives the items; may be NULL only when @p capacity is 0.
 * @param capacity How many items it holds.
 * @param outCount [out] Receives how many were written. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL; ErrorCode_BufferTooSmall when there are more types than
 *          @p capacity (the first @p capacity are written); ErrorCode_ArgumentOutOfRange when an item's id would pass UINT32_MAX.
 */
Error UIWindowTypes_WriteMenuItems(const UIWindowTypes* self, uint32_t firstItemId, UIMenuItem* outItems, size_t capacity, size_t* outCount);
