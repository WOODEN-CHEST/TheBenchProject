#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRInt32Vector.h"
#include "UILayouts.h"
#include "raylib/raylib.h"


/*
 * The UI gallery's workspace page, private to the gallery (UIGalleryFrame.c): a workspace with a menu bar and a status bar, five window
 * types it registers with the program's window types while the gallery runs (an outline, a view, an inspector, a log and notes), their
 * layouts kept in the gallery's own file in the game's folder (GALLERY_WORKSPACE_FILE_NAME), the default one docking them all; a Window
 * menu opening each type, a Layout menu (new, rename, delete, reset) and the layout switcher in the status bar.
 *
 * The view's window holds a render view the gallery is the host of, as a world's editor is: a moving pattern drawn each frame at the view's
 * size (made again at every new size), nothing while the view does not show, and the view's record read once a tick (the left button puts
 * a marker, the right button pans the pattern with the cursor hidden, the wheel sets the stripes' speed), each thing it does reported.
 */


// Forward declarations.
typedef struct UIKitStruct UIKit;
typedef struct UIScreenStruct UIScreen;
typedef struct UIWindowTypesStruct UIWindowTypes;
typedef struct KeyRegistryStruct KeyRegistry;
typedef struct LoggerStruct Logger;
typedef struct GameInputStruct GameInput;


// Macros.
/** How many window types the page registers. */
#define GALLERY_WORKSPACE_TYPE_COUNT ((size_t)5)
/** The page's layouts file, in the game's folder. */
#define GALLERY_WORKSPACE_FILE_NAME ((const unsigned char*)u8"ui_gallery_layouts.json")


// Types.
/** Tells the gallery what happened, for its log. */
typedef Error (*GalleryWorkspaceReport)(void* context, const char* text);

/** The picture the page's render view shows, and what moves it. */
typedef struct GalleryPictureStruct
{
    /** The view (WIDGET_ID_INVALID while its window is closed), the picture its size, the view it was handed to, whether it shows. */
    uint64_t ViewId;
    RenderTexture2D Target;
    bool HasTarget;
    uint64_t TargetViewId;
    bool IsShown;
    /** The size a picture could not be made at, while HasFailedSize: not tried again until the view's size changes. */
    bool HasFailedSize;
    Int32Vector FailedSize;
    /** The pattern's clock and the stripes' speed (the wheel's), in pixels a second. */
    double Seconds;
    float Speed;
    /** How far the right button's drag panned the pattern, in pixels. */
    Vector2 Pan;
    /** Where the left button put the marker, in [0;1] of the view, once it did. */
    Vector2 Marker;
    bool HasMarker;
} GalleryPicture;

/** The page's state. */
typedef struct GalleryWorkspaceStruct
{
    /** The program's window types, borrowed; NULL when the program has none (the page says so). */
    UIWindowTypes* Types;
    /** The gallery's kit and screen, borrowed. */
    UIKit* Kit;
    UIScreen* Screen;
    /** Where it reports. */
    GalleryWorkspaceReport Report;
    void* ReportContext;
    /** The source the types are registered under, and their indices, once registered. */
    uint32_t SourceKeyId;
    uint32_t TypeIndices[GALLERY_WORKSPACE_TYPE_COUNT];
    bool IsRegistered;
    /** The layouts, once constructed. */
    UILayouts Layouts;
    bool HasLayouts;
    /** The workspace, its status bar and the switcher in it; WIDGET_ID_INVALID until the page is built. */
    uint64_t WorkspaceId;
    uint64_t StatusBarId;
    uint64_t SwitcherId;
    /** The rename dialog and its name box while one asks; WIDGET_ID_INVALID otherwise. */
    uint64_t RenameDialogId;
    uint64_t RenameBoxId;
    /** The view's picture. */
    GalleryPicture Picture;
} GalleryWorkspace;


// Functions.
/** Registers the page's window types with the program's (none without them) and reads its layouts file from @p baseDirectory (none
 *  without one: the default layout shows, and nothing is kept). */
Error GalleryWorkspace_Construct(GalleryWorkspace* self, UIWindowTypes* types, KeyRegistry* keys, UIKit* kit, UIScreen* screen, Logger* logger,
    const unsigned char* baseDirectory, GalleryWorkspaceReport report, void* reportContext);

/** Saves the layouts and lets the workspace go of them. Call before the screen goes. */
Error GalleryWorkspace_Save(GalleryWorkspace* self);

/** Withdraws the page's window types and releases the layouts. Call once the screen and its windows are gone. */
void GalleryWorkspace_Deconstruct(GalleryWorkspace* self);

/** Makes the workspace (hidden, a root of the screen) with its menus and switcher, and shows the active layout in it. */
Error GalleryWorkspace_Build(GalleryWorkspace* self, uint64_t* outWorkspaceId);

/** Shows the workspace and its windows while the page shows, and hides them while another page shows (the screen's workspace is the whole
 *  surface again then). */
Error GalleryWorkspace_SetShown(GalleryWorkspace* self, bool isShown);

/** Shows a status in the workspace's status bar. */
Error GalleryWorkspace_SetStatus(GalleryWorkspace* self, const char* text);

/** Reads the view's record (once a tick, after the screen's update) and moves the pattern by it, reporting what it did; while the view is
 *  entered, Escape (which the view keeps) lets go of it and is consumed. */
Error GalleryWorkspace_UpdatePicture(GalleryWorkspace* self, GameInput* input, double passedSeconds);

/** The update side's last step before a render: the picture the view's size (made again at every new size; the view lets go of the old one
 *  first), handed to the view; none drawn while the view does not show. A size no picture could be made at is not tried again until the
 *  view's size changes. */
void GalleryWorkspace_PreparePicture(GalleryWorkspace* self);

/** Draws the pattern into the picture, before the screen renders; nothing while the view does not show. */
void GalleryWorkspace_RenderPicture(const GalleryWorkspace* self);

/** Unloads the picture. Call once the screen (and the view) are gone. */
void GalleryWorkspace_ReleasePicture(GalleryWorkspace* self);
