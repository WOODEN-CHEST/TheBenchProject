#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "wr/WRError.h"
// GenericBuffer backs the capability and type registries (held by value); UIPropertyType types the property
// descriptors. Both are needed here.
#include "wr/WRMemory.h"
#include "UIAnimation.h"


/**
 * @file UIWidgetFactory.h
 * @brief Registry and producer of widgets: registers capabilities and widget types, then creates (and recycles)
 *        widget instances from per-type pools.
 *
 * The factory is a single program-wide object shared by every screen (a screen borrows it); the target screen is
 * passed to UIWidgetFactory_CreateWidget. It solves three problems:
 *   - Type identity without a giant central enum. Capabilities are registered first, each minting a capability id.
 *     Widget types are registered next from a WidgetTypeDescriptor (struct size, vtable, constructor, capabilities,
 *     typed properties, default focusability), each minting a type id. Both ids start at 1 (0 is invalid).
 *   - Creation that cannot half-fail. The factory builds the widget base itself: it borrows a pool slot, initializes
 *     the base, reserves the widget's id with the screen, runs the type's constructor for the concrete part and only
 *     then makes the widget alive. A failing constructor leaves nothing registered.
 *   - Allocation-light instancing. Each type owns an object pool sized to its struct; a destroyed widget's slot is
 *     reused. The factory counts each type's live widgets.
 *
 * Construct with UIWidgetFactory_Construct and release with UIWidgetFactory_Deconstruct, after every screen that uses
 * it. Not thread-safe.
 */


// Forward declarations (referenced only by pointer here; full types live in their own headers).
/** @brief The abstract widget base; full type in UIWidget.h. */
typedef struct WidgetStruct Widget;
/** @brief A widget type's behavior table; full type in UIWidget.h. */
typedef struct WidgetVTableStruct WidgetVTable;
/** @brief A UI screen; full type in UIScreen.h. Borrowed. */
typedef struct UIScreenStruct UIScreen;


// Macros.
/** @brief Invalid widget type id (a real type id is >= 1). */
#define WIDGET_TYPE_ID_INVALID ((uint64_t)0)
/** @brief Invalid capability id (a real capability id is >= 1). */
#define WIDGET_CAPABILITY_ID_INVALID ((uint64_t)0)


// Types.
/**
 * @brief Resolves a capability struct pointer from a concrete widget instance.
 *
 * Registered per (type, capability) because the capability struct lives at a type-specific offset inside the concrete
 * widget. The returned pointer is only as stable as the widget itself.
 * @param widget The concrete widget instance (a Widget*, passed as void*); never NULL when called.
 * @returns A pointer to the capability struct embedded in @p widget.
 */
typedef void* (*WidgetCapabilityResolver)(void* widget);

/**
 * @brief Constructs the concrete part of a widget, in place, after the factory initialized its base.
 *
 * The base is already set up (vtable, screen, type, defaults; base setters may be called) but the widget is not alive
 * yet: it must not create other widgets, use its id or subscribe to anything (do that in the Initialize hook). On
 * failure it must release whatever it acquired itself; the factory then releases the base and the slot, and nothing
 * stays registered.
 * @param widget Pooled storage for the concrete widget (zero-filled apart from the base), at least the type's struct
 *        size. Never NULL.
 * @param args The caller's type-specific argument struct passed to UIWidgetFactory_CreateWidget; may be NULL.
 * @returns Success once the concrete part is constructed, or a non-success Error to abort the creation.
 */
typedef Error (*WidgetConstructor)(void* widget, void* args);

/**
 * @brief One capability a widget type implements: the capability id plus the resolver for that type.
 */
typedef struct WidgetCapabilityEntryStruct
{
    /** @brief The registered capability id this type implements. */
    uint64_t CapabilityId;
    /** @brief The resolver returning this type's instance of the capability struct; must not be NULL. */
    WidgetCapabilityResolver Resolver;
} WidgetCapabilityEntry;

/**
 * @brief One typed property a widget type declares (in addition to the WidgetBaseProperty ids every widget has).
 */
typedef struct WidgetPropertyDescriptorStruct
{
    /** @brief The property id; in [0, WIDGET_BASE_PROPERTY_START) and unique within the type. */
    int32_t Id;
    /** @brief The property's value type; values of any other type are rejected. */
    UIPropertyType Type;
    /** @brief The property's name for messages and tooling (static UTF-8, e.g. u8"tint"); may be NULL. */
    const unsigned char* Name;
} WidgetPropertyDescriptor;

/**
 * @brief Everything the factory needs to know about a widget type. Passed to UIWidgetFactory_RegisterType, which copies
 *        it (the arrays included); the vtable and the strings must be static.
 */
typedef struct WidgetTypeDescriptorStruct
{
    /** @brief The type's name for messages (static UTF-8, e.g. u8"Label"); may be NULL. */
    const unsigned char* Name;
    /** @brief Size in bytes of the concrete widget struct; at least sizeof(Widget). */
    size_t StructSize;
    /** @brief The type's behavior table (static storage); must not be NULL. */
    const WidgetVTable* VTable;
    /** @brief Constructs the concrete part; NULL when the type needs nothing beyond the base. */
    WidgetConstructor Constructor;
    /** @brief The capabilities the type implements; may be NULL only when CapabilityCount is 0. */
    const WidgetCapabilityEntry* Capabilities;
    /** @brief Number of entries in Capabilities. */
    size_t CapabilityCount;
    /** @brief The typed properties the type declares; may be NULL only when PropertyCount is 0. */
    const WidgetPropertyDescriptor* Properties;
    /** @brief Number of entries in Properties. */
    size_t PropertyCount;
    /** @brief Whether new widgets of the type can take keyboard focus (Widget_SetIsFocusable changes it per widget). */
    bool IsFocusable;
} WidgetTypeDescriptor;

/**
 * @brief Registry of capabilities and widget types, and producer of pooled widget instances.
 *
 * Underscore-prefixed fields are internal. Construct with UIWidgetFactory_Construct, release with
 * UIWidgetFactory_Deconstruct.
 */
typedef struct UIWidgetFactoryStruct
{
    /** @brief Registry of capabilities (one record per capability id; id is index + 1). */
    GenericBuffer _capabilities;
    /** @brief Registry of widget types (one record per type id; id is index + 1). Records hold each type's pool. */
    GenericBuffer _types;
} UIWidgetFactory;


// Functions.
/**
 * @brief Initializes an empty factory.
 * @param self The factory to initialize; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error UIWidgetFactory_Construct(UIWidgetFactory* self);

/**
 * @brief Releases the factory: every type's pool and registry storage.
 *
 * Deconstruct every screen first (that destroys their widgets properly). Widgets still alive are released with their
 * pools without any teardown (their resources leak), which is reported as an error. Best-effort: the first error is
 * returned and later ones are released. Safe on NULL.
 * @param self The factory to deconstruct, or NULL.
 * @returns Success (including the NULL case); ErrorCode_InvalidOperation if widgets were still alive; or the first
 *          other error encountered.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIWidgetFactory_Deconstruct(UIWidgetFactory* self);

/**
 * @brief Registers a new capability, minting its id.
 *
 * Capabilities must be registered before the widget types that reference them.
 * @param self The factory; must not be NULL.
 * @param outCapabilityId [out] Receives the new capability id (>= 1). Must not be NULL.
 * @returns Success with *outCapabilityId set; ErrorCode_IllegalArgument if @p self or @p outCapabilityId is NULL;
 *          ErrorCode_BufferTooLarge if the registry could not grow.
 */
Error UIWidgetFactory_RegisterCapability(UIWidgetFactory* self, uint64_t* outCapabilityId);

/**
 * @brief Unregisters a capability id, so it is no longer considered valid.
 *
 * Does not touch types that referenced it; those keep their resolver entries. Intended for teardown of
 * dynamically-registered capabilities.
 * @param self The factory; must not be NULL.
 * @param capabilityId The capability id to unregister; must be a currently-registered id.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p capabilityId is
 *          out of range; ErrorCode_InvalidOperation if it is not registered.
 */
Error UIWidgetFactory_UnregisterCapability(UIWidgetFactory* self, uint64_t capabilityId);

/**
 * @brief Registers a new widget type, minting its id and creating its instance pool.
 * @param self The factory; must not be NULL.
 * @param descriptor The type; must not be NULL. Copied.
 * @param outTypeId [out] Receives the new type id (>= 1). Must not be NULL.
 * @returns Success with *outTypeId set; ErrorCode_IllegalArgument if @p self, @p descriptor, its VTable or
 *          @p outTypeId is NULL, an array is NULL with a non-zero count, a capability entry has a NULL resolver, or a
 *          property has an invalid type or a duplicate id; ErrorCode_ArgumentOutOfRange if StructSize is smaller than
 *          a Widget, a capability is not registered or a property id is outside [0, WIDGET_BASE_PROPERTY_START);
 *          ErrorCode_BufferTooLarge if a registry could not grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIWidgetFactory_RegisterType(UIWidgetFactory* self, const WidgetTypeDescriptor* descriptor, uint64_t* outTypeId);

/**
 * @brief Unregisters a widget type, deconstructing its pool. Refused while widgets of the type are alive (see
 *        UIWidgetFactory_GetLiveWidgetCount), so also from inside the type's own constructor.
 * @param self The factory; must not be NULL.
 * @param typeId The type id to unregister; must be a currently-registered id.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p typeId is out of
 *          range; ErrorCode_InvalidOperation if it is not registered or widgets of it are alive.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIWidgetFactory_UnregisterType(UIWidgetFactory* self, uint64_t typeId);

/**
 * @brief Creates a widget of the given type in a screen.
 *
 * Borrows a slot from the type's pool, initializes the widget base (defaults: position and size 0, z 0, white tint,
 * rendered, updated and input-enabled, the type's focusability), reserves the widget's id with @p screen, runs the
 * type's constructor with @p args, and makes the widget alive. On a failure nothing stays registered and the slot
 * returns to the pool. The widget is created detached and uninitialized: initialize it (Widget_InitializeWidget) and
 * add it to the screen or a parent.
 * @param self The factory; must not be NULL and must be @p screen's factory.
 * @param screen The screen the widget belongs to; must not be NULL.
 * @param typeId The registered type to create.
 * @param args The type-specific argument struct for the constructor; may be NULL if the type takes none.
 * @param outWidget [out] Receives the widget on success, NULL on failure. Must not be NULL.
 * @returns Success with *outWidget set; ErrorCode_IllegalArgument if @p self, @p screen or @p outWidget is NULL or
 *          @p self is not the screen's factory; ErrorCode_ArgumentOutOfRange if @p typeId is out of range;
 *          ErrorCode_InvalidOperation if the type is not registered or the screen is being torn down; otherwise the
 *          constructor's, the pool's or the screen's error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIWidgetFactory_CreateWidget(UIWidgetFactory* self, UIScreen* screen, uint64_t typeId, void* args, Widget** outWidget);

/**
 * @brief Returns how many widgets of a type are alive (created and not yet torn down; a widget counts from the moment
 *        its constructor starts, and dying widgets count until their screen's flush).
 * @param self The factory; must not be NULL.
 * @param typeId The type id.
 * @returns The count; 0 for an unknown type.
 */
size_t UIWidgetFactory_GetLiveWidgetCount(const UIWidgetFactory* self, uint64_t typeId);

/**
 * @brief Returns how many widgets of all types are alive.
 * @param self The factory; must not be NULL.
 * @returns The count.
 */
size_t UIWidgetFactory_GetTotalLiveWidgetCount(const UIWidgetFactory* self);

/**
 * @brief Returns a type's name from its descriptor.
 * @param self The factory; must not be NULL.
 * @param typeId The type id.
 * @returns The name, or NULL for an unknown type or a type registered without one.
 */
const unsigned char* UIWidgetFactory_GetTypeName(const UIWidgetFactory* self, uint64_t typeId);

/**
 * @brief Looks up a property a type declares (base properties are not included; see Widget_TryGetPropertyType).
 * @param self The factory; must not be NULL.
 * @param typeId The type id.
 * @param propertyId The property id.
 * @param outDescriptor [out] Receives a copy of the descriptor when found; must not be NULL.
 * @returns true if the type is registered and declares the property.
 */
bool UIWidgetFactory_TryGetPropertyDescriptor(const UIWidgetFactory* self, uint64_t typeId, int32_t propertyId,
    WidgetPropertyDescriptor* outDescriptor);

/**
 * @brief Reports whether a widget type supports a capability.
 * @param self The factory; must not be NULL.
 * @param typeId The type id to query.
 * @param capabilityId The capability id to look for.
 * @returns true if @p typeId is registered and implements @p capabilityId, false otherwise.
 */
bool UIWidgetFactory_IsCapabilitySupported(const UIWidgetFactory* self, uint64_t typeId, uint64_t capabilityId);

/**
 * @brief Appends the capability ids supported by a widget type to a buffer (append semantics).
 * @param self The factory; must not be NULL.
 * @param typeId The type id to query; must be a currently-registered id.
 * @param outCapabilityIds [out] A uint64_t buffer to append ids to; must not be NULL and must have an element size of
 *        sizeof(uint64_t).
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outCapabilityIds is NULL or the buffer's element size
 *          is wrong; ErrorCode_ArgumentOutOfRange if @p typeId is out of range; ErrorCode_InvalidOperation if the type
 *          is not registered; ErrorCode_BufferTooLarge if the buffer could not grow.
 */
Error UIWidgetFactory_GetSupportedCapabilities(const UIWidgetFactory* self, uint64_t typeId, GenericBuffer* outCapabilityIds);

/**
 * @brief Resolves a capability struct pointer for a widget of the given type.
 * @param self The factory; must not be NULL.
 * @param typeId The widget's type id.
 * @param capabilityId The capability to resolve.
 * @param widget The widget instance whose capability struct is wanted; must not be NULL.
 * @returns A pointer to the widget's capability struct, or NULL if @p typeId is not registered or does not implement
 *          @p capabilityId.
 */
void* UIWidgetFactory_ResolveCapability(const UIWidgetFactory* self, uint64_t typeId, uint64_t capabilityId, void* widget);
