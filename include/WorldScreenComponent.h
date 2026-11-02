#pragma once
#include <stdbool.h>
#include "wr/WRError.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"


/**
 * @file WorldScreenComponent.h
 * @brief The screen component: which surface of its object's world model shows a picture (WorldModel.h, "surfaces"),
 *        whether the screen is on, and how bright it is.
 *
 * A world model names its surfaces (a monitor's glass, a page); a placement with a screen component picks one by name
 * and shows on it whatever the code that owns the content hands the renderer each frame (WorldRenderer_SetSurfaceTexture;
 * a UI drawn into a render target). The content is game state, not world data: the component only says where it goes
 * and how it looks. The object also needs a model component naming that world model. The screens system
 * (WorldScreens.h) finds where on a screen the player points.
 *
 * A screen that is off shows no picture: the surface's own material (a screen's dark glass). A screen surface's picture
 * glows at the surface's emission times the component's brightness; a paper surface's picture is lit like the rest and
 * takes neither.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Screen, &component); reach its fields with
 * WorldScreenComponent_FromComponent. Its properties are the rows of its table ("screen_component"): surface,
 * is_powered and brightness. Every change advances the component's revision (WorldComponent_GetRevision).
 */


// Macros.
/** @brief The surface a new screen component shows on: a world model's surface usually named so. */
#define WORLD_SCREEN_COMPONENT_DEFAULT_SURFACE ((const unsigned char*)u8"screen")
/** @brief The largest brightness a screen component may give (it multiplies the surface's emission). */
#define WORLD_SCREEN_COMPONENT_MAX_BRIGHTNESS 1000.0f


// Types.
/**
 * @brief The screen component. Underscore-prefixed fields are read-only: use the accessors and setters (or the table).
 */
typedef struct WorldScreenComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Which surface of the object's world model it shows on (owned; [a-z][a-z0-9_.-]*, at most
     *  WORLD_MODEL_SURFACE_NAME_MAX_LENGTH bytes; default WORLD_SCREEN_COMPONENT_DEFAULT_SURFACE). */
    unsigned char* _surface;
    /** @brief Whether the screen is on (default true): off, it shows no picture. */
    bool _isPowered;
    /** @brief Times the surface's emission, 0 to WORLD_SCREEN_COMPONENT_MAX_BRIGHTNESS (default 1). */
    float _brightness;
} WorldScreenComponent;


// Functions.
/**
 * @brief Returns the screen component's registry entry (WorldComponentType_Screen, "screen").
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldScreenComponent_GetTypeInfo(void);

/**
 * @brief Returns the screen component's property table ("screen_component").
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldScreenComponent_GetPropertyTable(void);

/**
 * @brief Downcasts a component to a screen component.
 * @param component The component, or NULL.
 * @returns The screen component, or NULL when @p component is NULL or of another type.
 */
static inline WorldScreenComponent* WorldScreenComponent_FromComponent(WorldComponent* component)
{
    return ((component != NULL) && (component->_type == WorldComponentType_Screen)) ? (WorldScreenComponent*)component
        : NULL;
}

/**
 * @brief Upcasts a screen component to its base.
 * @param self The screen component; must not be NULL.
 * @returns The embedded base.
 */
static inline WorldComponent* WorldScreenComponent_AsComponent(WorldScreenComponent* self)
{
    return &self->Base;
}

/**
 * @brief Returns the name of the surface the screen shows on.
 * @param self The screen component; must not be NULL.
 * @returns The name (borrowed: valid until it changes).
 */
static inline const unsigned char* WorldScreenComponent_GetSurface(const WorldScreenComponent* self)
{
    return self->_surface;
}

/**
 * @brief Sets the surface the screen shows on.
 * @param self The screen component; must not be NULL.
 * @param surface The surface's name ([a-z][a-z0-9_.-]*, at most WORLD_MODEL_SURFACE_NAME_MAX_LENGTH bytes); copied.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; the table's error (naming the rule) for NULL or a name
 *          that is not a valid surface name. Refused, nothing changes.
 */
Error WorldScreenComponent_SetSurface(WorldScreenComponent* self, const unsigned char* surface);

/**
 * @brief Returns whether the screen is on.
 * @param self The screen component; must not be NULL.
 * @returns true when it is on.
 */
static inline bool WorldScreenComponent_IsPowered(const WorldScreenComponent* self)
{
    return self->_isPowered;
}

/**
 * @brief Turns the screen on or off (off, it shows no picture).
 * @param self The screen component; must not be NULL.
 * @param isPowered Whether it is on.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldScreenComponent_SetIsPowered(WorldScreenComponent* self, bool isPowered);

/**
 * @brief Returns how bright the screen is: the factor on its surface's emission.
 * @param self The screen component; must not be NULL.
 * @returns 0 to WORLD_SCREEN_COMPONENT_MAX_BRIGHTNESS.
 */
static inline float WorldScreenComponent_GetBrightness(const WorldScreenComponent* self)
{
    return self->_brightness;
}

/**
 * @brief Sets how bright the screen is.
 * @param self The screen component; must not be NULL.
 * @param brightness The factor on the surface's emission, finite, 0 to WORLD_SCREEN_COMPONENT_MAX_BRIGHTNESS.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a value outside the
 *          range or not finite.
 */
Error WorldScreenComponent_SetBrightness(WorldScreenComponent* self, float brightness);
