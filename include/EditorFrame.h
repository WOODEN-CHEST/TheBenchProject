#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "GameCamera.h"
#include "GameFrame.h"
#include "UIWindowHost.h"


/**
 * @file EditorFrame.h
 * @brief The world editor's frame: a world shown in a dockable window of a workspace, a fly camera, and the shell later tools plug into.
 *
 * The game starts it alone (`--editor [world name]`): no other frame runs beside it. It opens worlds/<name>.world.json from the game's folder,
 * or an empty world without a name (a name that is no plain file name, a missing file or a broken one is logged and leaves an empty world
 * too). It owns the World, a WorldCollision that places rather than moves (no swept motions, no warning when a static object moves), the
 * generators that build what prefabs and path objects make (so the world shows as it is), a WorldRenderer, the GameCamera and its fly
 * controls (EditorCamera), and its UI: a screen keeping its pixels (UIScalePolicy_ConstantPixels) with the test theme, a workspace with a menu
 * bar (File, Window, Layout) and a status bar (where the camera is and how fast it flies, messages, the layout switcher), and the window
 * types it registers while it runs: tbp:editor_world (the world's window: one at a time, closable, reopened from the Window menu), and two
 * plain ones, tbp:editor_controls and tbp:editor_notes. Its layouts are kept in editor_layouts.json in the game's folder.
 *
 * THE WORLD'S WINDOW holds a render view (UIRenderView.h) the frame is the host of: each shown frame it keeps a render target the view's
 * size (made again at every new size, also while an edge or a splitter is dragged, so the picture stays real), draws the world into it
 * before the UI renders, and draws nothing while the view does not show (a tab not selected, a minimized or closed window).
 *
 * EACH TICK: the screen's update through its UIWindowHost; then, with the view's record, the camera (a press of the right button in the view
 * enters it and hides the cursor: the pointer's motion turns the camera, and while it is held the editor's fly actions move it; the wheel
 * over the view steps the speed; keys count only while the view is entered); then the generators, World_UpdateTransforms and
 * WorldCollision_Sync. Nothing simulates: no character, no physics, no clock. The sky shows the time of day the world's environment holds.
 */


// Forward declarations.
/** @brief The shared services bundle; full type in Services.h. Borrowed. */
typedef struct ServicesStruct Services;
/** @brief A world; full type in World.h. */
typedef struct WorldStruct World;
/** @brief A UI screen; full type in UIScreen.h. */
typedef struct UIScreenStruct UIScreen;
/** @brief A collision world; full type in WorldCollision.h. */
typedef struct WorldCollisionStruct WorldCollision;


// Functions.
/**
 * @brief Creates the editor (heap-allocated; the frame manager destroys it), with its world loaded and its windows arranged.
 * @param services The shared services; borrowed, must outlive the frame. Its Assets, Keys and WindowTypes are required; its Logger, Config,
 *        Input, FrameManager and BaseDirectory are used when there (no layouts are kept, and no world file is opened, without a base
 *        directory). Must not be NULL.
 * @param worldName The world to open, a file name without its ending (worlds/<name>.world.json), or NULL for an empty world. Copied.
 * @param outFrame [out] Receives the frame; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL or a required service is missing; otherwise an error from building the
 *          world's systems or the UI (a world that does not load is no error: the world is empty then).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error EditorFrame_Create(Services* services, const unsigned char* worldName, GameFrame** outFrame);

/**
 * @brief Returns whether a frame is an editor frame (made by EditorFrame_Create).
 * @param frame The frame, or NULL.
 * @returns true for an editor frame.
 */
bool EditorFrame_IsEditorFrame(const GameFrame* frame);

/**
 * @brief Returns the world an editor frame edits.
 * @param frame An editor frame; must not be NULL.
 * @returns The world, owned by the frame; NULL for a frame that is no editor frame.
 */
World* EditorFrame_GetWorld(GameFrame* frame);

/**
 * @brief Returns the editor's collision world: every collider where it is as of the last tick, for picking.
 * @param frame An editor frame; must not be NULL.
 * @returns The collision world, owned by the frame; NULL for a frame that is no editor frame.
 */
WorldCollision* EditorFrame_GetCollision(GameFrame* frame);

/**
 * @brief Returns whether the world came from its file (false: the world is empty, there being no name, no file or a broken one).
 * @param frame An editor frame; must not be NULL.
 * @returns The answer; false for a frame that is no editor frame.
 */
bool EditorFrame_IsWorldLoaded(const GameFrame* frame);

/**
 * @brief Returns the editor's camera.
 * @param frame An editor frame; must not be NULL.
 * @returns The camera, owned by the frame; NULL for a frame that is no editor frame.
 */
GameCamera* EditorFrame_GetCamera(GameFrame* frame);

/**
 * @brief Returns the editor's fly speed (the wheel's steps; the fast key not counted), in metres a second.
 * @param frame An editor frame; must not be NULL.
 * @returns The speed; 0 for a frame that is no editor frame.
 */
float EditorFrame_GetFlySpeed(const GameFrame* frame);

/**
 * @brief Returns the editor's UI screen.
 * @param frame An editor frame; must not be NULL.
 * @returns The screen, owned by the frame; NULL for a frame that is no editor frame.
 */
UIScreen* EditorFrame_GetScreen(GameFrame* frame);

/**
 * @brief Returns the editor's workspace: its windows open through it (UIWorkspace_OpenTypeByKey).
 * @param frame An editor frame; must not be NULL.
 * @returns Its id; WIDGET_ID_INVALID for a frame that is no editor frame.
 */
uint64_t EditorFrame_GetWorkspaceId(const GameFrame* frame);

/**
 * @brief Returns the world's window's render view.
 * @param frame An editor frame; must not be NULL.
 * @returns Its id, or WIDGET_ID_INVALID while the world's window is closed (and for a frame that is no editor frame).
 */
uint64_t EditorFrame_GetViewId(const GameFrame* frame);

/**
 * @brief Returns how many times the editor drew its world: a view that does not show draws nothing.
 * @param frame An editor frame; must not be NULL.
 * @returns The count; 0 for a frame that is no editor frame.
 */
uint64_t EditorFrame_GetWorldRenderCount(const GameFrame* frame);

/**
 * @brief Puts another cursor in place of the real one the editor's UI hides, shows and shapes (UIWindowHost_SetCursor): how tests and
 *        harnesses watch it, with no window.
 * @param frame An editor frame; must not be NULL. Nothing happens for another frame.
 * @param cursor The cursor; copied. Must not be NULL.
 */
void EditorFrame_SetHostCursor(GameFrame* frame, const UIWindowHostCursor* cursor);
