#pragma once
#include <stdbool.h>
#include <stdint.h>
// Vector2 is a raylib type of the pointer's position; this include is unavoidable here.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "GameInput.h"
#include "ProgramTime.h"
#include "UIScreen.h"
#include "WorldScreens.h"


/**
 * @file WorldScreenHost.h
 * @brief Hosts a UI (UIScreen.h) shown on a screen in the world: its pointer where the player points, the keyboard
 *        while the player uses the screen, and letting go of it.
 *
 * The code that owns a screen's content (the office computer, a test's monitor) keeps a UIScreen, draws it into a render
 * target the renderer shows on the screen's surface (WorldRenderer_SetSurfaceTexture), and drives it through a host,
 * once a tick in its Update, before the game's own controls (the screen reads first, as a UI above the game does):
 * - THE POINTER is where the player points at the screen's picture this tick (WorldScreens_Raycast: the aim from the
 *   eyes or the camera, or the free cursor's ray): where on the picture the hit is (a CRT's through its curvature, the
 *   point of its picture the glass shows there) is the UI's own [0;1] position, (0, 0) the picture's top-left. Off the
 *   picture, at another screen or hidden, there is no pointer (and none while UI
 *   above claimed the pointer). Buttons go to the UI under the pointer, and a press on the picture is the screen's even
 *   where no widget takes it: the game below never sees it.
 * - USING IT: the use action (default E) or a click on the picture gives the screen the keyboard (it is FOCUSED). The
 *   press of use that focuses it is consumed with the tick's typed text, and while its key stays held the text of each
 *   tick is dropped too (the key's repeats), so it neither acts in the game nor types.
 * - THE KEYBOARD: while focused the screen gets every key and the typed text first within its frame (as a UI with the
 *   keyboard does; frames above it read before it, frames update top-down), and then the host takes the whole keyboard
 *   and the text: nothing below acts on a key while the player types (the movement actions included). The mouse still
 *   turns the view, so the pointer follows the aim.
 * - LETTING GO: an Escape the UI leaves unused (ui_back or pause) releases the screen (and is consumed: it does not
 *   pause the game), and so does pointing away from its picture: a screen the player no longer points at gets no input.
 *   A ui_back or pause press in a tick whose typed text the UI took is typing, not letting go (pause bound to a letter
 *   types it into a field; that tick's Escape, rare, needs pressing again).
 *
 * A plain value type: construct it in place, one per hosted screen; it owns nothing.
 */


// Types.
/**
 * @brief The host of one world screen's UI. Fields are read-only (underscore-prefixed): use the accessors.
 */
typedef struct WorldScreenHostStruct
{
    /** @brief The screen's object (0: none: it is never pointed at). */
    uint64_t _objectId;
    /** @brief Whether the player uses the screen: the keyboard is its. */
    bool _isFocused;
    /** @brief Whether the key of the use press that focused the screen is still held (its repeats type nothing). */
    bool _isUseHeld;
    /** @brief Whether the pointer was on its picture in the last update, and where (the UI's [0;1] space). */
    bool _isPointed;
    Vector2 _pointer;
} WorldScreenHost;


// Functions.
/**
 * @brief Constructs a host for one screen, not focused and not pointed at.
 * @param self The host; must not be NULL.
 * @param objectId The screen's object (0: none).
 */
void WorldScreenHost_Construct(WorldScreenHost* self, uint64_t objectId);

/**
 * @brief Deconstructs a host (it owns nothing).
 * @param self The host, or NULL.
 */
void WorldScreenHost_Deconstruct(WorldScreenHost* self);

/**
 * @brief One tick of the screen's UI (see the file comment): the pointer from @p pointed, focus by use or a click,
 *        UIScreen_Update with that input, then release by Escape or pointing away, or the whole keyboard taken while
 *        focused.
 * @param self The host; must not be NULL.
 * @param screen The UI shown on the screen; must not be NULL.
 * @param time The tick's time, for UIScreen_Update.
 * @param input The tick's input (consumption is recorded in it), or NULL: the screen gets no input this tick (no pointer:
 *        not pointed at) and keeps its focus.
 * @param pointed Where the player points this tick (WorldScreens_Raycast's hit), or NULL for nowhere.
 * @returns Success, or the error of UIScreen_Update (a broken invariant: see AGENTS.md rule 7).
 */
Error WorldScreenHost_Update(WorldScreenHost* self, UIScreen* screen, ProgramTime time, GameInput* input,
    const WorldScreenHit* pointed);

/**
 * @brief Lets go of the screen at once: no keyboard from the next update on (the screen's object went away, the player
 *        was moved elsewhere).
 * @param self The host; must not be NULL.
 */
void WorldScreenHost_Release(WorldScreenHost* self);

/** @brief Returns the screen's object (0: none). @param self The host; must not be NULL. */
static inline uint64_t WorldScreenHost_GetObjectId(const WorldScreenHost* self)
{
    return self->_objectId;
}

/** @brief Returns whether the player uses the screen (it has the keyboard). @param self The host; must not be NULL. */
static inline bool WorldScreenHost_IsFocused(const WorldScreenHost* self)
{
    return self->_isFocused;
}

/** @brief Returns whether the pointer was on the screen's picture in the last update. @param self The host; must not be
 *  NULL. */
static inline bool WorldScreenHost_IsPointed(const WorldScreenHost* self)
{
    return self->_isPointed;
}

/** @brief Returns where the pointer was on the picture in the last update ([0;1], (0, 0) the top-left; meaningful while
 *  WorldScreenHost_IsPointed). @param self The host; must not be NULL. */
static inline Vector2 WorldScreenHost_GetPointer(const WorldScreenHost* self)
{
    return self->_pointer;
}
