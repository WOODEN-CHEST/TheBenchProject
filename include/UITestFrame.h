#pragma once
#include "wr/WRError.h"
#include "GameFrame.h"


/**
 * @file UITestFrame.h
 * @brief A debug game frame that exercises the UI framework on top of the world test.
 *
 * It owns a widget factory, a UIScreen and a few labels: a hint at the bottom left, and a focusable "menu" label at
 * the top right with three focusable item labels inside it. It runs as its own frame above WorldTestFrame, so it gets
 * the input first (frames update top-down) and shows how input is shared: keys and clicks the UI uses are consumed,
 * everything else reaches the world below.
 *
 * What it exercises: hover and click focus (M in the world test frees the mouse), keyboard navigation (Tab / arrows
 * move, Enter enters the menu, Escape leaves it and then lets go of the UI, so a further Escape reaches the world test,
 * which quits), queued notifications (hover and focus change the labels' tint, and gaining focus starts a size
 * animation), and deferred destruction (Delete destroys the focused item, R rebuilds the menu).
 *
 * Create with UITestFrame_Create and hand it to the GameFrameManager, which owns it from then on.
 */


// Forward declarations.
/** @brief The shared services bundle; full type in Services.h. Borrowed. */
typedef struct ServicesStruct Services;


// Functions.
/**
 * @brief Creates the UI test frame (heap-allocated; the frame manager destroys it).
 * @param services The shared services; borrowed, must outlive the frame. Its Input, FrameManager and Assets are used
 *        (Assets may be NULL: the labels then use raylib's built-in font). Must not be NULL.
 * @param outFrame [out] Receives the frame; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise an error from building the UI.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UITestFrame_Create(Services* services, GameFrame** outFrame);
