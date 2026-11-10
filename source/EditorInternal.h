#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRInt32Vector.h"
#include "EditorCamera.h"
#include "GameCamera.h"
#include "GameFrame.h"
#include "GameInput.h"
#include "TextComponentFactory.h"
#include "UIKit.h"
#include "UILayouts.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "UIWindowHost.h"
#include "UIWindowTypes.h"
#include "World.h"
#include "raylib/raylib.h"


/*
 * Private to the editor (EditorFrame.c, EditorShell.c): the frame's state, and the shell's part of it, its UI: the screen with its kit and
 * host, the workspace with its bars, the window types the editor offers (the world's and two plain ones), the menus, the layouts kept in
 * EDITOR_LAYOUTS_FILE_NAME and the status line. Tests reach the frame's picture through the seam at the end.
 */


// Forward declarations.
typedef struct ServicesStruct Services;
typedef struct WorldCollisionStruct WorldCollision;
typedef struct WorldPrefabsStruct WorldPrefabs;
typedef struct WorldPathObjectsStruct WorldPathObjects;
typedef struct WorldRendererStruct WorldRenderer;


// Macros.
/** The layouts file, in the game's folder: the player's arrangement of the editor's windows. */
#define EDITOR_LAYOUTS_FILE_NAME ((const unsigned char*)u8"editor_layouts.json")
/** The status line's place text's room. */
#define EDITOR_PLACE_TEXT_CAPACITY ((size_t)96)


// Types.
/** The window types the editor offers, in the order they register. */
typedef enum EditorWindowSlotEnum
{
    EditorWindowSlot_World,
    EditorWindowSlot_Controls,
    EditorWindowSlot_Notes,
    EditorWindowSlot_Count
} EditorWindowSlot;

/** The editor's fly actions, in the order they are looked up. */
typedef enum EditorFlyActionEnum
{
    EditorFlyAction_Forward,
    EditorFlyAction_Back,
    EditorFlyAction_Left,
    EditorFlyAction_Right,
    EditorFlyAction_Up,
    EditorFlyAction_Down,
    EditorFlyAction_Fast,
    EditorFlyAction_Count
} EditorFlyAction;

/** How the frame makes and unloads the view's picture: by default render targets GL completed (one it could not is unloaded and given as
 *  all zero); tests, which have no GL, set stand-ins (EditorFrame_InternalSetTargetFunctions). Load gives a target whose id is 0 when none
 *  could be made. */
typedef struct EditorTargetFunctionsStruct
{
    RenderTexture2D (*Load)(int width, int height);
    void (*Unload)(RenderTexture2D target);
} EditorTargetFunctions;

/** The editor's UI. */
typedef struct EditorShellStruct
{
    TextComponentFactory TextFactory;
    UIWidgetFactory WidgetFactory;
    UIKit Kit;
    UIScreen Screen;
    UIWindowHost Host;
    bool IsTextFactoryConstructed;
    bool IsWidgetFactoryConstructed;
    bool IsKitConstructed;
    bool IsScreenConstructed;
    /** The program's window types (borrowed), the source the editor's are registered under, and each one's index. */
    UIWindowTypes* Types;
    uint32_t SourceKeyId;
    uint32_t TypeIndices[EditorWindowSlot_Count];
    bool AreTypesRegistered;
    /** The layouts and their file; HasLayouts once constructed. */
    UILayouts Layouts;
    bool HasLayouts;
    uint64_t WorkspaceId;
    uint64_t StatusBarId;
    /** The label in the status bar's left section: where the camera is and how fast it flies. */
    uint64_t PlaceLabelId;
    unsigned char PlaceText[EDITOR_PLACE_TEXT_CAPACITY];
    /** The world window's render view, WIDGET_ID_INVALID while it is closed. */
    uint64_t ViewId;
} EditorShell;

/** The editor frame. */
typedef struct EditorFrameStruct
{
    /** The base; first. */
    GameFrame Base;
    /** The shared services; borrowed. */
    Services* Services;
    /** The world's name as it was given, NUL-terminated (owned), or NULL when none was. */
    unsigned char* WorldName;
    /** Whether the world came from a file (else it is empty). */
    bool IsWorldLoaded;
    World World;
    bool IsWorldConstructed;
    WorldCollision* Collision;
    WorldPrefabs* Prefabs;
    WorldPathObjects* PathObjects;
    WorldRenderer* Renderer;
    GameCamera Camera;
    EditorCamera Flight;
    /** The fly actions' ids, and whether each was found (an action of a pack that did not load is not). */
    GameActionId FlyActions[EditorFlyAction_Count];
    bool HasFlyAction[EditorFlyAction_Count];
    EditorShell Shell;
    /** How the view's picture is made and unloaded. */
    EditorTargetFunctions TargetFunctions;
    /** The view's picture: a render target its size, the view it was handed to, and whether it shows this render. */
    RenderTexture2D ViewTarget;
    bool HasViewTarget;
    uint64_t TargetViewId;
    bool IsViewShown;
    /** Whether a picture that could not be made was logged (once until one is made). */
    bool IsTargetFailureLogged;
    /** The size the last picture could not be made at, while HasFailedTargetSize: it is not tried again until the view's size changes. */
    bool HasFailedTargetSize;
    Int32Vector FailedTargetSize;
    /** How many times the world was rendered, for checks: a hidden view renders nothing. */
    uint64_t WorldRenderCount;
    /** The place text is old, and when it was last set (the update's time). */
    bool IsPlaceTextOld;
    double PlaceTextTime;
    double Time;
} EditorFrame;


// Functions: the shell (EditorShell.c).
/** Builds the UI: the factories, the kit, the screen and its host and theme, the window types, the layouts, the workspace with its menus and
 *  status bar, and the arrangement (the active layout, or the default). @p frame is the editor's, borrowed for the windows' data. */
Error EditorShell_Construct(EditorShell* self, EditorFrame* frame);
/** Keeps the layouts, then tears the UI down: the screen (its windows), the types, the kit, the factories. Safe on a part-built shell. */
void EditorShell_Deconstruct(EditorShell* self);
/** Sets the status bar's place text when it changed. */
Error EditorShell_SetPlaceText(EditorShell* self, const char* text);


// Functions: for tests (EditorFrame.c).
/** Replaces how the frame makes and unloads the view's picture, before its first render (a picture it holds is unloaded first, the old way).
 *  Nothing happens for a frame that is no editor frame, or with NULL functions. */
void EditorFrame_InternalSetTargetFunctions(GameFrame* frame, const EditorTargetFunctions* functions);
