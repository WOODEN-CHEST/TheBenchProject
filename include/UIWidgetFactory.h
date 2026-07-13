#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "wr/WRError.h"
// GenericBuffer backs the capability and type registries (held by value); its full type is needed here.
#include "wr/WRMemory.h"


/**
 * @file UIWidgetFactory.h
 * @brief Registry and producer of widgets: registers capabilities and widget types, then constructs
 *        (and recycles) widget instances from per-type pools.
 *
 * The factory is a single program-wide object shared by every screen (a screen borrows it rather than
 * owning it); the target screen is supplied per widget to UIWidgetFactory_ConstructWidget. It solves two
 * problems:
 *   - Type identity without a giant central enum. Capabilities are registered first, each minting a
 *     capability id (a small interface identity). Widget types are registered next, each supplying its
 *     concrete struct size, a constructor, and the (capabilityId, resolver) pairs it implements, and each
 *     minting a type id. Capability and type ids both start at 1 (0 is invalid) and increase by 1.
 *   - Allocation-light instancing. Each registered type owns an object pool sized to its struct. Building
 *     a widget borrows a slot from that pool and runs the type's constructor into it; destroying a widget
 *     returns the slot to the pool for reuse (see UIWidgetFactory_ReturnWidget, driven by Widget_Destroy).
 *
 * Capability resolution is keyed by (type, capability): a widget resolves a capability by asking the
 * factory for its own type's resolver, which returns a pointer to the capability struct embedded in that
 * widget (no stability guarantee — it points inside the live widget).
 *
 * Construct with UIWidgetFactory_Construct and release with UIWidgetFactory_Deconstruct. Not thread-safe.
 */


// Forward declarations (referenced only by pointer here; full types live in their own headers).
/** @brief The abstract widget base; full type in UIWidget.h. */
typedef struct WidgetStruct Widget;
/** @brief The UI screen/context that owns this factory; full type in UIScreen.h. Borrowed. */
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
 * Registered per (type, capability) because the capability struct lives at a type-specific offset inside
 * the concrete widget. Given the widget, it returns a pointer to that widget's instance of the capability
 * struct. The returned pointer is only as stable as the widget itself.
 * @param widget The concrete widget instance (a Widget*, passed as void*); never NULL when called.
 * @returns A pointer to the capability struct embedded in @p widget.
 */
typedef void* (*WidgetCapabilityResolver)(void* widget);

/**
 * @brief Constructs a widget in place into factory-provided (pooled) storage.
 *
 * Supplied at type registration and invoked by UIWidgetFactory_ConstructWidget after a pool slot has been
 * borrowed. The implementation must initialize the widget — including calling Widget_Construct on the
 * embedded base with @p screen and @p typeId — and interpret @p args as its own argument struct. It must
 * NOT register or unregister factory types (that could invalidate the in-flight registry).
 * @param widgetMemory Pooled storage for the concrete widget, at least the type's registered struct size,
 *        suitably aligned. Never NULL.
 * @param screen The screen the widget belongs to; never NULL.
 * @param typeId The widget's registered type id.
 * @param args The caller's type-specific argument struct passed to UIWidgetFactory_ConstructWidget; may
 *        be NULL if the type takes no arguments.
 * @returns Success once the widget is fully constructed, or a non-success Error to abort the construction
 *          (the pool slot is then returned automatically).
 */
typedef Error (*WidgetConstructor)(void* widgetMemory, UIScreen* screen, uint64_t typeId, void* args);

/**
 * @brief One capability a widget type implements: the capability id plus the resolver for that type.
 *
 * Passed as an array to UIWidgetFactory_RegisterType. Each CapabilityId must already be registered.
 */
typedef struct WidgetCapabilityEntryStruct
{
    /** @brief The registered capability id this type implements. */
    uint64_t CapabilityId;
    /** @brief The resolver returning this type's instance of the capability struct; must not be NULL. */
    WidgetCapabilityResolver Resolver;
} WidgetCapabilityEntry;

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
 *
 * The factory is screen-independent: one factory is shared across every screen in the program, and the
 * target screen is supplied per widget via UIWidgetFactory_ConstructWidget.
 * @param self The factory to initialize; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error UIWidgetFactory_Construct(UIWidgetFactory* self);

/**
 * @brief Releases the factory: deconstructs every type's pool and registry storage.
 *
 * Best-effort teardown: the first error is returned and every later error is released so none leak. Any
 * widgets still borrowed from a type's pool are released with the pool; deconstruct the widgets first if
 * they own resources. Safe on NULL.
 * @param self The factory to deconstruct, or NULL.
 * @returns Success (including the NULL case), or the first non-success Error encountered.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error UIWidgetFactory_Deconstruct(UIWidgetFactory* self);

/**
 * @brief Registers a new capability, minting its id.
 *
 * Capabilities must be registered before the widget types that reference them.
 * @param self The factory; must not be NULL.
 * @param outCapabilityId [out] Receives the new capability id (>= 1). Must not be NULL.
 * @returns Success with *outCapabilityId set; ErrorCode_IllegalArgument if @p self or @p outCapabilityId
 *          is NULL; ErrorCode_BufferTooLarge if the registry could not grow.
 */
Error UIWidgetFactory_RegisterCapability(UIWidgetFactory* self, uint64_t* outCapabilityId);

/**
 * @brief Unregisters a capability id, so it is no longer considered valid.
 *
 * Does not touch types that referenced it; those keep their resolver entries. Intended for teardown of
 * dynamically-registered capabilities.
 * @param self The factory; must not be NULL.
 * @param capabilityId The capability id to unregister; must be a currently-registered id.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p capabilityId is out of range; ErrorCode_InvalidOperation if it is not registered.
 */
Error UIWidgetFactory_UnregisterCapability(UIWidgetFactory* self, uint64_t capabilityId);

/**
 * @brief Registers a new widget type, minting its id and creating its instance pool.
 *
 * @param self The factory; must not be NULL.
 * @param widgetStructSize Size in bytes of the concrete widget struct; must be > 0 and at least
 *        sizeof(Widget) (the base must fit). The type's object pool uses this element size.
 * @param constructor The in-place constructor for the type; must not be NULL.
 * @param capabilities Array of (capabilityId, resolver) pairs the type implements; may be NULL only when
 *        @p capabilityCount is 0. Each CapabilityId must be registered and each Resolver non-NULL.
 * @param capabilityCount Number of entries in @p capabilities.
 * @param outTypeId [out] Receives the new type id (>= 1). Must not be NULL.
 * @returns Success with *outTypeId set; ErrorCode_IllegalArgument if @p self, @p constructor or
 *          @p outTypeId is NULL, or an entry has a NULL resolver; ErrorCode_ArgumentOutOfRange if
 *          @p widgetStructSize is 0 or an entry references an unregistered capability;
 *          ErrorCode_BufferTooLarge if a registry could not grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error UIWidgetFactory_RegisterType(UIWidgetFactory* self,
    size_t widgetStructSize,
    WidgetConstructor constructor,
    const WidgetCapabilityEntry* capabilities,
    size_t capabilityCount,
    uint64_t* outTypeId);

/**
 * @brief Unregisters a widget type, deconstructing its pool.
 *
 * Requires that no instances of the type are currently alive (their storage lives in the pool being
 * released). Intended for teardown of dynamically-registered types.
 * @param self The factory; must not be NULL.
 * @param typeId The type id to unregister; must be a currently-registered id.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p typeId is out of range; ErrorCode_InvalidOperation if it is not registered.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error UIWidgetFactory_UnregisterType(UIWidgetFactory* self, uint64_t typeId);

/**
 * @brief Constructs a widget of the given type from its pool, into the given screen.
 *
 * Borrows a slot from the type's object pool and runs the type's constructor into it (passing @p screen,
 * the type id, and @p args). On constructor failure the slot is returned to the pool and the error is
 * propagated.
 * @param self The factory; must not be NULL.
 * @param screen The screen the widget is being built into; borrowed, passed to the constructor. Must not
 *        be NULL.
 * @param typeId The registered type to construct; must be a currently-registered id.
 * @param args The type-specific argument struct for the constructor; may be NULL if the type takes none.
 * @param outWidget [out] Receives the constructed widget on success, NULL on failure. Must not be NULL.
 * @returns Success with *outWidget set; ErrorCode_IllegalArgument if @p self, @p screen or @p outWidget is
 *          NULL; ErrorCode_ArgumentOutOfRange if @p typeId is out of range; ErrorCode_InvalidOperation if
 *          the type is not registered; otherwise the constructor's or pool's error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error UIWidgetFactory_ConstructWidget(UIWidgetFactory* self, UIScreen* screen, uint64_t typeId, void* args, Widget** outWidget);

/**
 * @brief Returns a widget's storage to its type's pool for reuse.
 *
 * Called by Widget_Destroy after the widget has been deconstructed. The widget must have been produced by
 * this factory for @p typeId and must not be used afterwards.
 * @param self The factory; must not be NULL.
 * @param typeId The widget's type id; must be a currently-registered id.
 * @param widget The widget storage to return; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p widget is NULL; ErrorCode_ArgumentOutOfRange
 *          if @p typeId is out of range; ErrorCode_InvalidOperation if the type is not registered;
 *          otherwise the pool's error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error UIWidgetFactory_ReturnWidget(UIWidgetFactory* self, uint64_t typeId, void* widget);

/**
 * @brief Reports whether a widget type supports a capability.
 * @param self The factory; must not be NULL.
 * @param typeId The type id to query.
 * @param capabilityId The capability id to look for.
 * @returns true if @p typeId is registered and implements @p capabilityId, false otherwise.
 */
bool UIWidgetFactory_IsCapabilitySupported(UIWidgetFactory* self, uint64_t typeId, uint64_t capabilityId);

/**
 * @brief Appends the capability ids supported by a widget type to a buffer.
 *
 * Does not clear @p outCapabilityIds first (append semantics); the caller clears it if a fresh list is
 * wanted.
 * @param self The factory; must not be NULL.
 * @param typeId The type id to query; must be a currently-registered id.
 * @param outCapabilityIds [out] A uint64_t buffer to append ids to; must not be NULL and must have an
 *        element size of sizeof(uint64_t).
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outCapabilityIds is NULL or the buffer's
 *          element size is wrong; ErrorCode_ArgumentOutOfRange if @p typeId is out of range;
 *          ErrorCode_InvalidOperation if the type is not registered; ErrorCode_BufferTooLarge if the
 *          buffer could not grow.
 */
Error UIWidgetFactory_GetSupportedCapabilities(UIWidgetFactory* self, uint64_t typeId, GenericBuffer* outCapabilityIds);

/**
 * @brief Resolves a capability struct pointer for a widget of the given type.
 * @param self The factory; must not be NULL.
 * @param typeId The widget's type id.
 * @param capabilityId The capability to resolve.
 * @param widget The widget instance whose capability struct is wanted; must not be NULL.
 * @returns A pointer to the widget's capability struct, or NULL if @p typeId is not registered or does not
 *          implement @p capabilityId.
 */
void* UIWidgetFactory_ResolveCapability(UIWidgetFactory* self, uint64_t typeId, uint64_t capabilityId, void* widget);
