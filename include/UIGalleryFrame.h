#pragma once
#include "wr/WRError.h"
#include "GameFrame.h"


/**
 * @file UIGalleryFrame.h
 * @brief A game frame showing every element of the UI kit in every state, over a plain background with no world.
 *
 * The game runs it instead of the world test when started with --ui-gallery. Everything is laid out in stacks that show
 * content, fixed and filling sizes, so a change to the kit, a theme or the art can be looked at on its own.
 *
 * Its keys: T switches the theme (the same elements, flat boxes and nine-slice art, with no element's code changing), + and -
 * scale the UI, L stops and starts logging each element's events, Escape quits.
 *
 * Create with UIGalleryFrame_Create and hand it to the GameFrameManager, which owns it from then on.
 */


// Forward declarations.
/** @brief The shared services bundle; full type in Services.h. Borrowed. */
typedef struct ServicesStruct Services;


// Functions.
/**
 * @brief Creates the gallery (heap-allocated; the frame manager destroys it).
 * @param services The shared services; borrowed, must outlive the frame. Its Input, FrameManager, Assets, Config and Logger are
 *        used. Must not be NULL.
 * @param outFrame [out] Receives the frame; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise an error from building the UI.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIGalleryFrame_Create(Services* services, GameFrame** outFrame);
