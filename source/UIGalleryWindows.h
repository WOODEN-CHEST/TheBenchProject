#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"


/*
 * The UI gallery's windows page, private to the gallery (UIGalleryFrame.c): four window types registered with the program's window types
 * while the gallery runs (a window that fits both ways, one whose content scrolls, a fixed one of its own scale, and notes whose close asks
 * first), opened at their places the first time the page shows and hidden while another page shows, and a Window menu listing every type.
 */


// Forward declarations.
typedef struct UIKitStruct UIKit;
typedef struct UIScreenStruct UIScreen;
typedef struct UIWindowTypesStruct UIWindowTypes;
typedef struct KeyRegistryStruct KeyRegistry;


// Macros.
/** How many window types the page registers. */
#define GALLERY_WINDOW_TYPE_COUNT ((size_t)4)


// Types.
/** Tells the gallery what happened, for its status line and its log. */
typedef Error (*GalleryWindowsReport)(void* context, const char* text);

/** The page's state. */
typedef struct GalleryWindowsStruct
{
    /** The program's window types, borrowed; NULL when the program has none (the page says so). */
    UIWindowTypes* Types;
    /** The gallery's kit and screen, borrowed. */
    UIKit* Kit;
    UIScreen* Screen;
    /** Where it reports. */
    GalleryWindowsReport Report;
    void* ReportContext;
    /** The source the types are registered under, and their indices, once registered. */
    uint32_t SourceKeyId;
    uint32_t TypeIndices[GALLERY_WINDOW_TYPE_COUNT];
    bool IsRegistered;
    /** Whether the windows were opened (the first time the page showed). */
    bool HasOpened;
    /** The dialog the notes ask with before closing, and the notes window asking; WIDGET_ID_INVALID while none asks. */
    uint64_t AskingDialogId;
    uint64_t AskingWindowId;
    /** The page's menu bar: the workspace starts under it. WIDGET_ID_INVALID until the page is built. */
    uint64_t MenuBarId;
} GalleryWindows;


// Functions.
/** Registers the page's window types with the program's (none without them). */
Error GalleryWindows_Construct(GalleryWindows* self, UIWindowTypes* types, KeyRegistry* keys, UIKit* kit, UIScreen* screen,
    GalleryWindowsReport report, void* reportContext);

/** Withdraws the page's window types. Call once the screen and its windows are gone. */
void GalleryWindows_Deconstruct(GalleryWindows* self);

/** Fills the page's column: what to try, and a menu bar whose Window menu lists the types. */
Error GalleryWindows_BuildPage(GalleryWindows* self, uint64_t columnId);

/** Shows every window of the screen while the page shows (opening the page's the first time) and hides them while another page shows; the
 *  screen's workspace is the whole surface again then. */
Error GalleryWindows_SetShown(GalleryWindows* self, bool isShown);

/** Keeps the screen's workspace between the page's menu bar and a line under it (the page's status line) while the page shows: a maximized
 *  window leaves them. Call each update while the page shows. */
Error GalleryWindows_KeepWorkspace(GalleryWindows* self, uint64_t bottomLineId);
