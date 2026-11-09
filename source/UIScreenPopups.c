#include "UIPopup.h"
#include "UIInternal.h"
#include "UIScreen.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "Renderer.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Types.
/* One open popup: its root, the backdrop under it when it is modal, how it opened, and where the focus goes back to. */
typedef struct PopupEntryStruct
{
    uint64_t RootId;
    uint64_t BackdropId;
    UIPopupOptions Options;
    uint64_t ReturnFocusId;
    bool WasReturnEntered;
} PopupEntry;


// Static functions: helpers.
static void KeepFirstError(Error* first, Error* candidate)
{
    if ((candidate->Code != ErrorCode_Success) && (first->Code == ErrorCode_Success))
    {
        *first = *candidate;
    }
    else
    {
        Error_Deconstruct(candidate);
    }
}

static bool IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

static PopupEntry* GetEntryAt(const UIScreen* self, size_t index)
{
    return GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_popups, index);
}

static size_t FindEntry(const UIScreen* self, uint64_t rootId)
{
    for (size_t Index = 0; Index < self->_popups._count; Index++)
    {
        if (GetEntryAt(self, Index)->RootId == rootId)
        {
            return Index;
        }
    }
    return GENERIC_BUFFER_INDEX_INVALID;
}

static Widget* GetRootOf(Widget* widget)
{
    Widget* Root = widget;
    while (Widget_GetParent(Root) != NULL)
    {
        Root = Widget_GetParent(Root);
    }
    return Root;
}

static bool IsSideValid(UIPopupSide side)
{
    return ((int32_t)side >= (int32_t)UIPopupSide_Below) && ((int32_t)side <= (int32_t)UIPopupSide_Center);
}

static bool AreOptionsValid(const UIPopupOptions* options)
{
    Rectangle Box = options->AnchorBox;
    bool IsBoxValid = IsFloatFinite(Box.x) && IsFloatFinite(Box.y) && IsFloatFinite(Box.width) && IsFloatFinite(Box.height)
        && (Box.width >= 0.0f) && (Box.height >= 0.0f);
    return IsBoxValid && IsSideValid(options->Side) && IsFloatFinite(options->MinimumLength) && (options->MinimumLength >= 0.0f);
}

/* Whether a widget is there for a popup to belong to: alive, attached and shown. */
static bool IsPresent(const UIScreen* self, uint64_t widgetId)
{
    const Widget* Found = UIScreen_InternalResolve(self, widgetId);
    return (Found != NULL) && UIScreen_InternalIsAttached(Found) && UIScreen_InternalIsShown(Found);
}

static Error PostClosed(UIScreen* self, uint64_t ownerId, UIPopupCloseReason reason)
{
    Widget* Owner = UIScreen_InternalResolve(self, ownerId);
    if (Owner == NULL)
    {
        return Error_CreateSuccess();
    }
    UIEvent Event =
    {
        .Kind = UIEventKind_PopupClosed,
        .WidgetId = ownerId,
        .WidgetTypeId = Widget_GetTypeId(Owner),
        .IsSet = false,
        .WidgetEventId = (int32_t)reason,
        .HasValue = false,
        .Value = UIPropertyValue_FromFloat(0.0f),
        .HasMouse = false,
        .Mouse = (UIEventMouse){ .Button = GameKey_Unknown }
    };
    if (!UIScreen_InternalPostEvent(self, &Event))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen: the notification queue could not grow.");
    }
    return Error_CreateSuccess();
}

/* Gives the focus back to the widget that had it when a popup opened, entered again if it was; nothing focused when it cannot take it
 * any more. */
static Error ReturnFocus(UIScreen* self, const PopupEntry* entry)
{
    Widget* Return = UIScreen_InternalResolve(self, entry->ReturnFocusId);
    if (!UIScreen_InternalCanFocus(Return))
    {
        return UIScreen_InternalSetFocus(self, WIDGET_ID_INVALID);
    }
    Error Result = UIScreen_InternalSetFocus(self, entry->ReturnFocusId);
    if ((Result.Code != ErrorCode_Success) || !entry->WasReturnEntered)
    {
        return Result;
    }
    return UIScreen_InternalEnter(self, entry->ReturnFocusId, UIEnterCause_Code); // Nothing if a hook moved the focus on.
}

static void DestroyIfAlive(UIScreen* self, uint64_t widgetId)
{
    Widget* Found = UIScreen_InternalResolve(self, widgetId);
    if (Found != NULL)
    {
        Error Result = Widget_Destroy(Found);
        Error_Deconstruct(&Result); // Only fails for a widget still being constructed, which a popup's never is.
    }
}

/* Closes one popup whose entry is already off the list: its cue, an element entered inside it left keeping, the focus back when it was
 * inside it or nowhere (unless a press is about to put the focus where it lands: @p returnsFocus false), its owner told, its widgets
 * destroyed. */
static Error CloseEntry(UIScreen* self, const PopupEntry* entry, UIPopupCloseReason reason, bool returnsFocus)
{
    Error FirstError = Error_CreateSuccess();
    Widget* Root = UIScreen_InternalResolve(self, entry->RootId);
    if (Root != NULL)
    {
        Error CueResult = UIScreen_PostCue(self, entry->RootId, UIElementCue_Close);
        KeepFirstError(&FirstError, &CueResult);
    }
    // An element entered inside it is left keeping what was done, as focus moving away leaves it (Escape left it with a cancel already):
    // what was typed in a popup's field is not lost to its closing.
    Widget* Entered = UIScreen_InternalResolve(self, self->_enteredWidgetId);
    Root = UIScreen_InternalResolve(self, entry->RootId);
    if ((Entered != NULL) && (Root != NULL) && UIScreen_InternalIsInSubtree(Entered, Root))
    {
        Error LeaveResult = UIScreen_InternalLeave(self, UILeaveReason_Keep);
        KeepFirstError(&FirstError, &LeaveResult);
    }
    Widget* Focused = UIScreen_InternalResolve(self, self->_focusedWidgetId);
    Root = UIScreen_InternalResolve(self, entry->RootId);
    if (returnsFocus && ((Focused == NULL) || ((Root != NULL) && UIScreen_InternalIsInSubtree(Focused, Root))))
    {
        Error FocusResult = ReturnFocus(self, entry);
        KeepFirstError(&FirstError, &FocusResult);
    }
    Error PostResult = PostClosed(self, entry->Options.OwnerId, reason);
    KeepFirstError(&FirstError, &PostResult);
    DestroyIfAlive(self, entry->RootId);
    DestroyIfAlive(self, entry->BackdropId);
    return FirstError;
}

/* Closes the popup at @p index and every one above it, from the top down. Their roots are taken first, so a popup a hook opens or closes
 * meanwhile is left alone (or is no longer there to close). */
static Error CloseFrom(UIScreen* self, size_t index, UIPopupCloseReason reason, bool returnsFocus)
{
    uint64_t Roots[UI_SCREEN_MAX_POPUPS];
    size_t Count = 0U;
    for (size_t Index = index; (Index < self->_popups._count) && (Count < UI_SCREEN_MAX_POPUPS); Index++)
    {
        Roots[Count++] = GetEntryAt(self, Index)->RootId;
    }
    Error FirstError = Error_CreateSuccess();
    for (size_t Step = Count; Step-- > 0U;)
    {
        size_t At = FindEntry(self, Roots[Step]);
        if (At == GENERIC_BUFFER_INDEX_INVALID)
        {
            continue;
        }
        PopupEntry Entry = *GetEntryAt(self, At);
        GenericBuffer_RemoveAt(&self->_popups, At);
        Error Result = CloseEntry(self, &Entry, reason, returnsFocus);
        KeepFirstError(&FirstError, &Result);
    }
    return FirstError;
}

/* The index of the topmost modal popup, or GENERIC_BUFFER_INDEX_INVALID. */
static size_t FindTopModal(const UIScreen* self)
{
    for (size_t Index = self->_popups._count; Index-- > 0U;)
    {
        if (GetEntryAt(self, Index)->Options.IsModal)
        {
            return Index;
        }
    }
    return GENERIC_BUFFER_INDEX_INVALID;
}

/* Makes a modal popup's backdrop: a root of the popup layer over the whole surface. */
static Error CreateBackdrop(UIScreen* self, uint64_t* outId)
{
    *outId = WIDGET_ID_INVALID;
    Widget* Backdrop = NULL;
    Error Result = UIWidgetFactory_CreateWidget(self->_factory, self, UIWidgetFactory_GetBackdropType(self->_factory), NULL, &Backdrop);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t Id = Widget_GetId(Backdrop);
    UIPlacement Everywhere = UIPlacement_Stretch(UIInsets_All(0.0f));
    Result = Widget_SetPlacement(Backdrop, &Everywhere);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_InitializeWidget(Backdrop);
    }
    if ((Result.Code == ErrorCode_Success) && ((Backdrop = UIScreen_InternalResolve(self, Id)) != NULL))
    {
        Result = UIScreen_AddWidget(self, Backdrop);
    }
    if ((Result.Code == ErrorCode_Success) && ((Backdrop = UIScreen_InternalResolve(self, Id)) != NULL))
    {
        Result = UIScreen_SetRootLayer(self, Backdrop, UIScreenLayer_Popup);
    }
    if ((Result.Code == ErrorCode_Success) && (UIScreen_InternalResolve(self, Id) == NULL))
    {
        Result = Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_OpenPopup: a hook took the backdrop away.");
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyIfAlive(self, Id);
        return Result;
    }
    *outId = Id;
    return Error_CreateSuccess();
}

/* Makes the popup's root a root of the popup layer, last: above every popup and backdrop before it. */
static Error RaiseAsPopupRoot(UIScreen* self, uint64_t rootId)
{
    Widget* Root = UIScreen_InternalResolve(self, rootId);
    Error Result = Error_CreateSuccess();
    if ((Root != NULL) && Widget_IsScreenRoot(Root))
    {
        Result = UIScreen_RemoveWidget(self, Root); // Added again below, so it comes after its backdrop.
    }
    if ((Result.Code == ErrorCode_Success) && ((Root = UIScreen_InternalResolve(self, rootId)) != NULL))
    {
        Result = UIScreen_AddWidget(self, Root);
    }
    if ((Result.Code == ErrorCode_Success) && ((Root = UIScreen_InternalResolve(self, rootId)) != NULL))
    {
        Result = UIScreen_SetRootLayer(self, Root, UIScreenLayer_Popup);
    }
    if ((Result.Code == ErrorCode_Success) && (UIScreen_InternalResolve(self, rootId) == NULL))
    {
        Result = Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_OpenPopup: a hook took the popup away.");
    }
    return Result;
}

/* Moves the focus into a popup that opened, as its options say. */
static Error MoveFocusIn(UIScreen* self, const UIPopupOptions* options)
{
    if (!options->MovesFocus)
    {
        return Error_CreateSuccess();
    }
    Widget* Target = UIScreen_InternalResolve(self, options->FocusId);
    if (!UIScreen_InternalCanFocus(Target))
    {
        return UIScreen_InternalSetFocus(self, WIDGET_ID_INVALID);
    }
    Error Result = UIScreen_InternalSetFocus(self, options->FocusId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIScreen_InternalEnter(self, options->FocusId, UIEnterCause_Code); // Nothing for what is no edit element.
}

/* The anchor's box in surface pixels: its widget's while that is alive and attached (remembered as the box from then on). */
static Rectangle GetAnchorPixels(UIScreen* self, PopupEntry* entry)
{
    Vector2 Surface = self->_viewport.SizePixels;
    Widget* Anchor = UIScreen_InternalResolve(self, entry->Options.AnchorWidgetId);
    if ((Anchor != NULL) && UIScreen_InternalIsAttached(Anchor))
    {
        Rectangle Box = UIScreen_GetWidgetPixelBox(self, Anchor);
        entry->Options.AnchorBox = (Rectangle){ .x = Box.x / Surface.x, .y = Box.y / Surface.y, .width = Box.width / Surface.x,
            .height = Box.height / Surface.y };
        return Box;
    }
    Rectangle Box = entry->Options.AnchorBox;
    return (Rectangle){ .x = Box.x * Surface.x, .y = Box.y * Surface.y, .width = Box.width * Surface.x, .height = Box.height * Surface.y };
}

/* Places one popup's root against its anchor, by its preferred size. */
static Error PlaceEntry(UIScreen* self, PopupEntry* entry, const UIMeasureContext* sizing)
{
    Widget* Root = UIScreen_InternalResolve(self, entry->RootId);
    if (Root == NULL)
    {
        return Error_CreateSuccess();
    }
    // Scaled like its owner: a popup of a zoomed element is zoomed with it.
    Widget* Owner = UIScreen_InternalResolve(self, entry->Options.OwnerId);
    if (entry->Options.IsScaledLikeOwner && (Owner != NULL))
    {
        float OwnScale = Math_ClampFloat(Widget_GetEffectiveScale(Owner) / self->_scale, WIDGET_MIN_SCALE, WIDGET_MAX_SCALE);
        Error ScaleResult = (Widget_GetScale(Root) != OwnScale) ? Widget_SetScale(Root, OwnScale) : Error_CreateSuccess();
        if (ScaleResult.Code != ErrorCode_Success)
        {
            return ScaleResult;
        }
    }
    Vector2 Size = { .x = 0.0f, .y = 0.0f };
    Error Result = Widget_Measure(Root, sizing, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Rectangle Anchor = GetAnchorPixels(self, entry);
    float Scale = self->_scale; // A root's placement counts in the screen's scale.
    Vector2 SizePixels = { .x = Size.x * Scale, .y = Size.y * Scale };
    bool IsVertical = (entry->Options.Side == UIPopupSide_Below) || (entry->Options.Side == UIPopupSide_Above);
    if (entry->Options.MatchesAnchorWidth && IsVertical)
    {
        SizePixels.x = Math_MaxFloat(SizePixels.x, Anchor.width);
    }
    UIPopupPlacementRules Rules = { .Side = entry->Options.Side, .Flips = entry->Options.Flips, .AllowsShrinking = entry->Options.AllowsShrinking,
        .MinimumLength = entry->Options.MinimumLength * Scale };
    Rectangle Box = UIPopupPlacement_Resolve(Anchor, SizePixels, self->_viewport.SizePixels, &Rules);
    if ((Box.width + 0.5f) < SizePixels.x)
    {
        // Narrower than it asked (the surface is): asked again at that width, so what wraps is as tall as it will be.
        Result = Widget_Measure(Root, sizing, Box.width / Scale, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        SizePixels = (Vector2){ .x = Box.width, .y = Size.y * Scale };
        Box = UIPopupPlacement_Resolve(Anchor, SizePixels, self->_viewport.SizePixels, &Rules);
    }
    UIPlacement Placement = UIPlacement_FromTopLeft(Box.x / Scale, Box.y / Scale, Box.width / Scale, Box.height / Scale);
    return Widget_SetPlacement(Root, &Placement);
}


// Internal functions (declared in UIInternal.h).
void UIScreen_InternalInitializePopups(UIScreen* self)
{
    GenericBuffer_AllocateVariable(&self->_popups, 0U, sizeof(PopupEntry));
}

void UIScreen_InternalReleasePopups(UIScreen* self)
{
    Memory_Free(self->_popups._data);
    Memory_Zero(&self->_popups, sizeof(self->_popups));
}

Error UIScreen_InternalPlacePopups(UIScreen* self)
{
    // A popup whose owner or root is gone closes, with every one above it.
    for (size_t Index = 0; Index < self->_popups._count; Index++)
    {
        const PopupEntry* Entry = GetEntryAt(self, Index);
        bool IsOwnerGone = (Entry->Options.OwnerId != WIDGET_ID_INVALID) && !IsPresent(self, Entry->Options.OwnerId);
        bool IsRootGone = UIScreen_InternalResolve(self, Entry->RootId) == NULL;
        if (IsOwnerGone || IsRootGone)
        {
            Error Result = CloseFrom(self, Index, IsOwnerGone ? UIPopupCloseReason_OwnerGone : UIPopupCloseReason_Code, true);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            break;
        }
    }
    if (self->_popups._count == 0U)
    {
        return Error_CreateSuccess();
    }
    RenderContext Measure;
    RenderContext_CreateMeasuring(&Measure, self->_viewport.SizePixels, self->_viewport.TargetAspectRatio,
        self->_viewport.TargetRelativePosition);
    UIMeasureContext Sizing = { .Measure = &Measure };
    for (size_t Index = 0; Index < self->_popups._count; Index++)
    {
        Error Result = PlaceEntry(self, GetEntryAt(self, Index), &Sizing);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

Error UIScreen_InternalClosePopupsForPress(UIScreen* self, uint64_t targetId, bool* outIsSwallowed, bool* outIsBlocked)
{
    *outIsSwallowed = false;
    *outIsBlocked = false;
    Widget* Target = UIScreen_InternalResolve(self, targetId);
    Widget* TargetRoot = (Target != NULL) ? GetRootOf(Target) : NULL;
    size_t Keep = GENERIC_BUFFER_INDEX_INVALID;
    for (size_t Index = self->_popups._count; (Index-- > 0U) && (Target != NULL);)
    {
        const PopupEntry* Entry = GetEntryAt(self, Index);
        Widget* Owner = UIScreen_InternalResolve(self, Entry->Options.OwnerId);
        bool IsInRoot = Widget_GetId(TargetRoot) == Entry->RootId;
        bool IsOnBackdrop = (Entry->BackdropId != WIDGET_ID_INVALID) && (Widget_GetId(TargetRoot) == Entry->BackdropId);
        bool IsOnOwner = Entry->Options.IsOwnerInside && (Owner != NULL) && UIScreen_InternalIsInSubtree(Target, Owner);
        if (IsInRoot || IsOnBackdrop || IsOnOwner)
        {
            Keep = Index;
            *outIsSwallowed = IsOnBackdrop;
            break;
        }
    }
    size_t Modal = FindTopModal(self);
    if ((Modal != GENERIC_BUFFER_INDEX_INVALID) && ((Keep == GENERIC_BUFFER_INDEX_INVALID) || (Keep < Modal)))
    {
        // A modal popup stays whatever is pressed, and nothing behind it takes the press: one on a widget there (its backdrop has no box
        // before its first layout) is swallowed, one on no widget is not this screen's.
        Keep = Modal;
        *outIsSwallowed = Target != NULL;
        *outIsBlocked = Target == NULL;
    }
    // A press that goes on puts the focus where it lands: the popups it closes give theirs back only when it goes no further.
    size_t From = (Keep == GENERIC_BUFFER_INDEX_INVALID) ? 0U : (Keep + 1U);
    bool ReturnsFocus = *outIsSwallowed || *outIsBlocked;
    return (From < self->_popups._count) ? CloseFrom(self, From, UIPopupCloseReason_PressOutside, ReturnsFocus) : Error_CreateSuccess();
}

Error UIScreen_InternalRoutePopupEscape(UIScreen* self, bool* outIsActed)
{
    *outIsActed = false;
    size_t Top = self->_popups._count;
    if ((Top == 0U) || !GetEntryAt(self, Top - 1U)->Options.ClosesOnEscape)
    {
        return Error_CreateSuccess(); // Escape closes the topmost popup or none: one that refuses it keeps those under it too.
    }
    PopupEntry* Entry = GetEntryAt(self, Top - 1U);
    Widget* Root = UIScreen_InternalResolve(self, Entry->RootId);
    Widget* Entered = UIScreen_InternalResolve(self, self->_enteredWidgetId);
    bool IsEnteredInside = (Entered != NULL) && (Root != NULL) && UIScreen_InternalIsInSubtree(Entered, Root);
    if ((Entered != NULL) && Entered->_keepsKeysWhenEntered && IsEnteredInside)
    {
        return Error_CreateSuccess(); // Its keys are its reader's.
    }
    *outIsActed = true;
    uint64_t RootId = Entry->RootId;
    if (IsEnteredInside)
    {
        Error Result = UIScreen_InternalLeave(self, UILeaveReason_Cancel);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    size_t At = FindEntry(self, RootId); // The leave hook may have changed the popups.
    return (At != GENERIC_BUFFER_INDEX_INVALID) ? CloseFrom(self, At, UIPopupCloseReason_Escape, true) : Error_CreateSuccess();
}

Widget* UIScreen_InternalGetNavigationRoot(const UIScreen* self)
{
    for (size_t Index = self->_popups._count; Index-- > 0U;)
    {
        Widget* Root = UIScreen_InternalResolve(self, GetEntryAt(self, Index)->RootId);
        if (Root != NULL)
        {
            return Root;
        }
    }
    return NULL;
}

bool UIScreen_InternalHasPopups(const UIScreen* self)
{
    return self->_popups._count > 0U;
}


// Public functions.
Error UIScreen_OpenPopup(UIScreen* screen, uint64_t rootWidgetId, const UIPopupOptions* options)
{
    if ((screen == NULL) || (options == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_OpenPopup: screen and options must not be NULL.");
    }
    if (!AreOptionsValid(options))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIScreen_OpenPopup: a known side, a finite anchor box of a size >= 0 and a minimum length that is a finite number >= 0 are required.");
    }
    Widget* Root = UIScreen_InternalResolve(screen, rootWidgetId);
    if ((Root == NULL) || !Widget_IsInitialized(Root) || (Widget_GetParent(Root) != NULL) || UIScreen_IsPopupOpen(screen, rootWidgetId))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIScreen_OpenPopup: the popup must be an alive, initialized widget that is no child and not open already.");
    }
    if (screen->_popups._count >= UI_SCREEN_MAX_POPUPS)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_OpenPopup: UI_SCREEN_MAX_POPUPS popups are open.");
    }
    if (options->IsModal && (UIWidgetFactory_GetBackdropType(screen->_factory) == WIDGET_TYPE_ID_INVALID))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_OpenPopup: a modal popup needs the factory's backdrop type.");
    }
    if (!GenericBuffer_EnsureTotalCapacity(&screen->_popups, screen->_popups._count + 1U))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_OpenPopup: the screen could not record the popup.");
    }

    PopupEntry Entry =
    {
        .RootId = rootWidgetId,
        .BackdropId = WIDGET_ID_INVALID,
        .Options = *options,
        .ReturnFocusId = screen->_focusedWidgetId,
        .WasReturnEntered = (screen->_enteredWidgetId != WIDGET_ID_INVALID) && (screen->_enteredWidgetId == screen->_focusedWidgetId)
    };
    Error Result = options->IsModal ? CreateBackdrop(screen, &Entry.BackdropId) : Error_CreateSuccess();
    if (Result.Code == ErrorCode_Success)
    {
        Result = RaiseAsPopupRoot(screen, rootWidgetId);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyIfAlive(screen, Entry.BackdropId);
        return Result;
    }
    GenericBuffer_AddLast(&screen->_popups, &Entry); // Cannot fail: the room is there.
    Result = UIScreen_PostCue(screen, rootWidgetId, UIElementCue_Open);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return MoveFocusIn(screen, options);
}

Error UIScreen_ClosePopup(UIScreen* screen, uint64_t rootWidgetId)
{
    if (screen == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_ClosePopup: screen must not be NULL.");
    }
    size_t Index = FindEntry(screen, rootWidgetId);
    if (Index == GENERIC_BUFFER_INDEX_INVALID)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_ClosePopup: no open popup has that root.");
    }
    return CloseFrom(screen, Index, UIPopupCloseReason_Code, true);
}

Error UIScreen_ClosePopupAndItsOwn(UIScreen* screen, uint64_t rootWidgetId)
{
    if (screen == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_ClosePopupAndItsOwn: screen must not be NULL.");
    }
    size_t Index = FindEntry(screen, rootWidgetId);
    if (Index == GENERIC_BUFFER_INDEX_INVALID)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_ClosePopupAndItsOwn: no open popup has that root.");
    }
    // Its own: the popup, then each above it whose owner lies in one already counted. Taken first, so a hook meanwhile changes nothing.
    uint64_t Roots[UI_SCREEN_MAX_POPUPS];
    size_t Count = 0U;
    Roots[Count++] = rootWidgetId;
    for (size_t Above = Index + 1U; (Above < screen->_popups._count) && (Count < UI_SCREEN_MAX_POPUPS); Above++)
    {
        const PopupEntry* Entry = GetEntryAt(screen, Above);
        const Widget* Owner = UIScreen_InternalResolve(screen, Entry->Options.OwnerId);
        bool IsOwn = false;
        for (size_t Known = 0U; (Known < Count) && !IsOwn && (Owner != NULL); Known++)
        {
            const Widget* Root = UIScreen_InternalResolve(screen, Roots[Known]);
            IsOwn = (Root != NULL) && UIScreen_InternalIsInSubtree(Owner, Root);
        }
        if (IsOwn)
        {
            Roots[Count++] = Entry->RootId;
        }
    }
    Error FirstError = Error_CreateSuccess();
    for (size_t Step = Count; Step-- > 0U;)
    {
        size_t At = FindEntry(screen, Roots[Step]);
        if (At == GENERIC_BUFFER_INDEX_INVALID)
        {
            continue;
        }
        PopupEntry Closing = *GetEntryAt(screen, At);
        GenericBuffer_RemoveAt(&screen->_popups, At);
        // A popup left open above it whose focus would go back inside it gives it back where this one would have.
        const Widget* ClosingRoot = UIScreen_InternalResolve(screen, Closing.RootId);
        for (size_t Left = At; (Left < screen->_popups._count) && (ClosingRoot != NULL); Left++)
        {
            PopupEntry* Entry = GetEntryAt(screen, Left);
            const Widget* Return = UIScreen_InternalResolve(screen, Entry->ReturnFocusId);
            if ((Return != NULL) && UIScreen_InternalIsInSubtree(Return, ClosingRoot))
            {
                Entry->ReturnFocusId = Closing.ReturnFocusId;
                Entry->WasReturnEntered = Closing.WasReturnEntered;
            }
        }
        Error Result = CloseEntry(screen, &Closing, UIPopupCloseReason_Code, true);
        KeepFirstError(&FirstError, &Result);
    }
    return FirstError;
}

Error UIScreen_CloseAllPopups(UIScreen* screen)
{
    if (screen == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_CloseAllPopups: screen must not be NULL.");
    }
    return (screen->_popups._count > 0U) ? CloseFrom(screen, 0U, UIPopupCloseReason_Code, true) : Error_CreateSuccess();
}

bool UIScreen_IsPopupOpen(const UIScreen* screen, uint64_t rootWidgetId)
{
    return (screen != NULL) && (rootWidgetId != WIDGET_ID_INVALID) && (FindEntry(screen, rootWidgetId) != GENERIC_BUFFER_INDEX_INVALID);
}

size_t UIScreen_GetPopupCount(const UIScreen* screen)
{
    return screen->_popups._count;
}

uint64_t UIScreen_GetPopupAt(const UIScreen* screen, size_t index)
{
    return (index < screen->_popups._count) ? GetEntryAt(screen, index)->RootId : WIDGET_ID_INVALID;
}

uint64_t UIScreen_FindPopupOf(const UIScreen* screen, uint64_t widgetId)
{
    Widget* Found = UIScreen_InternalResolve(screen, widgetId);
    if (Found == NULL)
    {
        return WIDGET_ID_INVALID;
    }
    uint64_t RootId = Widget_GetId(GetRootOf(Found));
    return (FindEntry(screen, RootId) != GENERIC_BUFFER_INDEX_INVALID) ? RootId : WIDGET_ID_INVALID;
}

/* Centred on the anchor, no larger than the surface and kept on it, in whole pixels. */
static Rectangle ResolveCentred(Rectangle anchor, float width, float height, Vector2 surface)
{
    float Width = Math_MinFloat(width, Math_MaxFloat(surface.x, 0.0f));
    float Height = Math_MinFloat(height, Math_MaxFloat(surface.y, 0.0f));
    float X = Math_ClampFloat(anchor.x + ((anchor.width - Width) * 0.5f), 0.0f, Math_MaxFloat(0.0f, surface.x - Width));
    float Y = Math_ClampFloat(anchor.y + ((anchor.height - Height) * 0.5f), 0.0f, Math_MaxFloat(0.0f, surface.y - Height));
    float Left = Math_FloorFloat(X + 0.5f);
    float Top = Math_FloorFloat(Y + 0.5f);
    return (Rectangle){ .x = Left, .y = Top, .width = Math_FloorFloat(X + Width + 0.5f) - Left, .height = Math_FloorFloat(Y + Height + 0.5f) - Top };
}

Rectangle UIPopupPlacement_Resolve(Rectangle anchor, Vector2 size, Vector2 surface, const UIPopupPlacementRules* rules)
{
    float Width = (size.x > 0.0f) ? size.x : 0.0f;
    float Height = (size.y > 0.0f) ? size.y : 0.0f;
    if (!IsFloatFinite(Width))
    {
        Width = 0.0f;
    }
    if (!IsFloatFinite(Height))
    {
        Height = 0.0f;
    }
    if (rules->Side == UIPopupSide_Center)
    {
        return ResolveCentred(anchor, Width, Height, surface);
    }
    bool IsVertical = (rules->Side == UIPopupSide_Below) || (rules->Side == UIPopupSide_Above);
    float Along = IsVertical ? Height : Width;
    float RoomAfter = Math_MaxFloat(0.0f, IsVertical ? (surface.y - (anchor.y + anchor.height)) : (surface.x - (anchor.x + anchor.width)));
    float RoomBefore = Math_MaxFloat(0.0f, IsVertical ? anchor.y : anchor.x);
    bool IsAfter = (rules->Side == UIPopupSide_Below) || (rules->Side == UIPopupSide_Right);
    float Room = IsAfter ? RoomAfter : RoomBefore;
    float OtherRoom = IsAfter ? RoomBefore : RoomAfter;
    if (rules->Flips && (Along > Room) && ((Along <= OtherRoom) || (OtherRoom > Room)))
    {
        IsAfter = !IsAfter;
        Room = OtherRoom;
    }
    if (rules->AllowsShrinking && (Along > Room))
    {
        float Minimum = Math_MinFloat(Math_MaxFloat(rules->MinimumLength, 0.0f), Along);
        Along = Math_MaxFloat(Room, Minimum);
    }
    float SurfaceAlong = IsVertical ? surface.y : surface.x;
    float SurfaceAcross = IsVertical ? surface.x : surface.y;
    float Across = Math_MinFloat(IsVertical ? Width : Height, SurfaceAcross);
    Along = Math_MinFloat(Along, SurfaceAlong);
    float AnchorStart = IsVertical ? anchor.y : anchor.x;
    float AnchorEnd = IsVertical ? (anchor.y + anchor.height) : (anchor.x + anchor.width);
    float Start = IsAfter ? AnchorEnd : (AnchorStart - Along);
    float CrossStart = IsVertical ? anchor.x : anchor.y;
    Start = Math_ClampFloat(Start, 0.0f, Math_MaxFloat(0.0f, SurfaceAlong - Along));
    CrossStart = Math_ClampFloat(CrossStart, 0.0f, Math_MaxFloat(0.0f, SurfaceAcross - Across));
    float StartPixel = Math_FloorFloat(Start + 0.5f);
    float CrossPixel = Math_FloorFloat(CrossStart + 0.5f);
    float AlongPixels = Math_FloorFloat(Start + Along + 0.5f) - StartPixel;
    float AcrossPixels = Math_FloorFloat(CrossStart + Across + 0.5f) - CrossPixel;
    if (IsVertical)
    {
        return (Rectangle){ .x = CrossPixel, .y = StartPixel, .width = AcrossPixels, .height = AlongPixels };
    }
    return (Rectangle){ .x = StartPixel, .y = CrossPixel, .width = AlongPixels, .height = AcrossPixels };
}
