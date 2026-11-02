#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "WorldTestScreens.h"
#include "CharacterAsset.h"
#include "GameInput.h"
#include "GameKey.h"
#include "LabelWidget.h"
#include "Logger.h"
#include "Renderer.h"
#include "TextComponent.h"
#include "UIRenderContext.h"
#include "UIWidget.h"
#include "WorldCharacterComponent.h"
#include "WorldObject.h"
#include "WorldScreenComponent.h"
#include "wr/WRChar.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** The test monitor's object name in the test world. */
#define MONITOR_OBJECT_NAME ((const unsigned char*)u8"monitor")
/** The labels' font (the world test's text sample's). */
#define LABEL_FONT_NAME ((const unsigned char*)u8"noto_sans")
/** Where the labels stand (the picture's [0;1] space) and how tall their text is (a share of the picture's height). */
#define LABEL_LEFT 0.06f
#define TITLE_TOP 0.06f
#define TITLE_SIZE 0.085f
#define LINE_FIRST_TOP 0.24f
#define LINE_STEP 0.12f
#define LINE_SIZE 0.06f
#define HINT_TOP 0.86f
#define HINT_SIZE 0.045f
/** The picture's resolution until the monitor's surface says otherwise. */
#define DEFAULT_PICTURE_WIDTH 640
#define DEFAULT_PICTURE_HEIGHT 480
/** Room for one label's text (the typed line's every byte with its prefix). */
#define LINE_CAPACITY ((size_t)256)
/** Hundredths: the pointer's position is shown to two decimals. */
#define HUNDREDTHS 100.0f
/** Minutes in an hour, for the clock label. */
#define MINUTES_PER_HOUR 60
/** The pointer's mark: a cross this long on each side of the pointer (the picture's [0;1] space), its lines this many
 *  pixels thick. */
#define POINTER_MARK_HALF_WIDTH 0.02f
#define POINTER_MARK_THICKNESS 2.0f
/** How many backgrounds the picture steps through, one per click (the screen's light takes their colour). */
#define BACKGROUND_COUNT 4U


// Types.
/* The desk: a focusable widget covering the whole picture, under the labels. Clicks on the picture land on it (the
 * labels take no input), typed text reaches it while it has the UI's focus, and it marks where the player points. */
typedef struct WorldTestDeskStruct
{
    /** The widget base (must be first). */
    Widget Base;
    /** The module that owns the screen (borrowed: it outlives the screen). */
    WorldTestScreens* Owner;
} WorldTestDesk;


// Fields.
/* The picture's backgrounds, one per click in turn: the starting blue, a green, an amber and a white. */
static const Color Backgrounds[BACKGROUND_COUNT] =
{
    { 14, 26, 52, 255 }, { 16, 70, 30, 255 }, { 110, 64, 10, 255 }, { 210, 210, 205, 255 }
};
/* The masks the mask key steps through after the monitor's own: every CRT showing one, then its own again. */
static const CrtMaskKind MaskSteps[] = { CrtMaskKind_ApertureGrille, CrtMaskKind_ShadowMask, CrtMaskKind_None };


// Static functions: the desk widget.
static Error ConstructDesk(void* widget, void* args)
{
    WorldTestDesk* Desk = widget;
    Desk->Owner = args;
    return Error_CreateSuccess();
}

/* A left press on the picture counts. */
static Error OnDeskMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    WorldTestDesk* Desk = self;
    if ((args->Type == UIMouseInputType_ButtonPress) && (args->Button == GameKey_MouseLeft))
    {
        Desk->Owner->_clickCount++;
        Desk->Owner->_lastClick = args->ScreenPosition;
        Desk->Owner->_isTextChanged = true;
    }
    return Error_CreateSuccess();
}

/* Where the player points on the picture, a small cross (it shows the pointer following the aim or the free cursor). */
static Error RenderDesk(void* self, UIRenderContext* context)
{
    const WorldTestScreens* Owner = ((const WorldTestDesk*)self)->Owner;
    if (!WorldScreenHost_IsPointed(&Owner->_host))
    {
        return Error_CreateSuccess();
    }
    Vector2 At = WorldScreenHost_GetPointer(&Owner->_host);
    RenderColor Mark = RenderColor_White();
    Mark.Tint = (Color){ 255, 214, 120, 255 };
    RenderFloat Thickness = RenderFloat_Window(POINTER_MARK_THICKNESS);
    Error Result = UIRenderContext_RenderLine(context, (Vector2){ At.x - POINTER_MARK_HALF_WIDTH, At.y },
        (Vector2){ At.x + POINTER_MARK_HALF_WIDTH, At.y }, Thickness, Mark);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float HalfHeight = POINTER_MARK_HALF_WIDTH * ((float)Owner->_target.texture.width
        / (float)((Owner->_target.texture.height > 0) ? Owner->_target.texture.height : 1));
    return UIRenderContext_RenderLine(context, (Vector2){ At.x, At.y - HalfHeight }, (Vector2){ At.x, At.y + HalfHeight },
        Thickness, Mark);
}

/* Typed text goes into the line (up to WORLD_TEST_SCREEN_TYPED_CAPACITY codepoints); Backspace takes the last one off. */
static Error OnDeskKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    WorldTestScreens* Owner = ((WorldTestDesk*)self)->Owner;
    switch (args->Type)
    {
        case UIKeyboardInputType_Text:
            *outIsHandled = true;
            if ((Owner->_typedCount < WORLD_TEST_SCREEN_TYPED_CAPACITY) && (args->Codepoint <= (uint32_t)INT32_MAX)
                && CharUTF8_IsCodePointValid((CodePoint)args->Codepoint))
            {
                Owner->_typedByteCount += CharUTF8_WriteCodePoint(&Owner->_typed[Owner->_typedByteCount],
                    (CodePoint)args->Codepoint);
                Owner->_typed[Owner->_typedByteCount] = 0U;
                Owner->_typedCount++;
                Owner->_isTextChanged = true;
            }
            break;
        case UIKeyboardInputType_KeyPress:
        case UIKeyboardInputType_KeyRepeat:
            if ((args->Key == GameKey_Backspace) && (Owner->_typedCount > 0U))
            {
                *outIsHandled = true;
                Owner->_typedByteCount -= CharUTF8_GetByteCountCharFromEnd(&Owner->_typed[Owner->_typedByteCount - 1U],
                    Owner->_typedByteCount - 1U);
                Owner->_typed[Owner->_typedByteCount] = 0U;
                Owner->_typedCount--;
                Owner->_isTextChanged = true;
            }
            break;
        case UIKeyboardInputType_KeyRelease:
            break;
    }
    return Error_CreateSuccess();
}

static const WidgetVTable DeskVTable =
{
    .Render = RenderDesk,
    .OnMouseInput = OnDeskMouseInput,
    .OnKeyboardInput = OnDeskKeyboardInput
};

static const WidgetTypeDescriptor DeskDescriptor =
{
    .Name = u8"WorldTestDesk", .StructSize = sizeof(WorldTestDesk), .VTable = &DeskVTable, .Constructor = ConstructDesk,
    .Capabilities = NULL, .CapabilityCount = 0U, .Properties = NULL, .PropertyCount = 0U, .IsFocusable = true
};


// Static functions: helpers.
static void Log(const WorldTestScreens* self, const char* message)
{
    if (self->_services->Logger != NULL)
    {
        Error LogResult = Logger_LogInfoFormatted(self->_services->Logger, (const unsigned char*)u8"WorldTest: %s",
            message);
        Error_Deconstruct(&LogResult);
    }
}

/* The id of the first object named @p name, or 0. */
static uint64_t FindObjectId(World* world, const unsigned char* name)
{
    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        bool IsFound = false;
        Error Result = World_GetObjectByIndex(world, Index, &Object);
        if ((Result.Code == ErrorCode_Success) && (WorldObject_GetName(Object) != NULL))
        {
            Result = StringUTF8_EqualsExact(WorldObject_GetName(Object), name, &IsFound);
        }
        Error_Deconstruct(&Result);
        if (IsFound)
        {
            return WorldObject_GetId(Object);
        }
    }
    return 0U;
}

/* The picture's size: the monitor's surface's resolution once resolved. */
static void GetPictureSize(const WorldTestScreens* self, int32_t* outWidth, int32_t* outHeight)
{
    WorldScreenInfo Info;
    bool IsResolved = WorldScreens_TryGetScreen(self->_screens, self->_monitorId, &Info);
    *outWidth = IsResolved ? WorldModelSurface_GetWidth(Info.Surface) : DEFAULT_PICTURE_WIDTH;
    *outHeight = IsResolved ? WorldModelSurface_GetHeight(Info.Surface) : DEFAULT_PICTURE_HEIGHT;
}

/* Replaces a label's text (its one string component's) with @p text. */
static Error SetLabelText(WorldTestScreens* self, uint64_t labelId, const char* text)
{
    Widget* Label = NULL;
    if (!UIScreen_TryGetWidgetById(&self->_screen, labelId, &Label))
    {
        return Error_CreateSuccess();
    }
    TextComponent* Root = LabelWidget_GetText((LabelWidget*)Label);
    StringComponent* String = (Root != NULL) ? TextComponent_AsStringComponent(Root) : NULL;
    return (String != NULL) ? StringComponent_SetText(String, (const unsigned char*)text) : Error_CreateSuccess();
}


// Static functions: building the UI.
/* Destroys a widget that was never added, if it can still be found. */
static void DestroyUnadded(WorldTestScreens* self, uint64_t id)
{
    Widget* Found = NULL;
    if (UIScreen_TryGetWidgetById(&self->_screen, id, &Found))
    {
        Error DestroyResult = Widget_Destroy(Found);
        Error_Deconstruct(&DestroyResult);
    }
}

/* Creates an initialized, input-disabled label of one string under the desk; its id into *outId. Widgets are found by id
 * again after every call that can run hooks (AGENTS.md rule 2). */
static Error AddLabel(WorldTestScreens* self, const char* text, float size, float top, RenderColor color, uint64_t* outId)
{
    StringComponent* String = NULL;
    Error Result = TextComponentFactory_CreateString(&self->_textFactory, (const unsigned char*)text, &String);
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetSize(String, size);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetColor(String, color);
    }
    LabelWidget* Label = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = LabelWidget_Create(&self->_screen, self->_labelType, &self->_textFactory, self->_services->Assets,
            self->_services->Logger, StringComponent_AsComponent(String), &Label);
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (String != NULL)
        {
            Error ReturnResult = TextComponentFactory_ReturnTree(&self->_textFactory, StringComponent_AsComponent(String));
            Error_Deconstruct(&ReturnResult);
        }
        return Result;
    }
    Widget* Base = LabelWidget_AsWidget(Label);
    uint64_t Id = Widget_GetId(Base);
    Result = TextDocument_SetDefaultFontName(LabelWidget_GetDocument(Label), LABEL_FONT_NAME);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_SetPosition(Base, (Vector2){ LABEL_LEFT, top });
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_SetIsInputEnabled(Base, false);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_InitializeWidget(Base);
    }
    Widget* Desk = NULL;
    if ((Result.Code == ErrorCode_Success) && (!UIScreen_TryGetWidgetById(&self->_screen, self->_deskId, &Desk)
        || !UIScreen_TryGetWidgetById(&self->_screen, Id, &Base)))
    {
        Result = Error_Construct2(ErrorCode_InvalidState, "WorldTestScreens: the desk or a new label is gone.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_AddSubWidget(Desk, Base);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyUnadded(self, Id);
        return Result;
    }
    *outId = Id;
    return Error_CreateSuccess();
}

/* The desk over the whole picture, then the labels on it. */
static Error BuildUi(WorldTestScreens* self)
{
    Widget* Desk = NULL;
    Error Result = UIWidgetFactory_CreateWidget(&self->_widgetFactory, &self->_screen, self->_deskType, self, &Desk);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t DeskId = Widget_GetId(Desk);
    Result = Widget_SetSize(Desk, (Vector2){ 1.0f, 1.0f });
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_InitializeWidget(Desk);
    }
    if ((Result.Code == ErrorCode_Success) && !UIScreen_TryGetWidgetById(&self->_screen, DeskId, &Desk))
    {
        Result = Error_Construct2(ErrorCode_InvalidState, "WorldTestScreens: the desk widget is gone.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_AddWidget(&self->_screen, Desk);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyUnadded(self, DeskId);
        return Result;
    }
    self->_deskId = DeskId;

    RenderColor Title = RenderColor_White();
    Title.Tint = (Color){ 140, 230, 160, 255 };
    RenderColor Line = RenderColor_White();
    Line.Tint = (Color){ 225, 232, 240, 255 };
    RenderColor Hint = RenderColor_White();
    Hint.Tint = (Color){ 150, 165, 190, 255 };
    // The labels' ids are kept (AddLabel finds the desk by its id).
    uint64_t TitleId = WIDGET_ID_INVALID;
    Result = AddLabel(self, "TheBench workstation", TITLE_SIZE, TITLE_TOP, Title, &TitleId);
    struct { uint64_t* Id; const char* Text; } Lines[] =
    {
        { &self->_clockId, "Clock: --" }, { &self->_pointerId, "Pointer: off the screen" },
        { &self->_clicksId, "Clicks: 0" }, { &self->_typedId, "Typed: _" }
    };
    for (size_t Index = 0U; (Index < (sizeof(Lines) / sizeof(Lines[0]))) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = AddLabel(self, Lines[Index].Text, LINE_SIZE, LINE_FIRST_TOP + ((float)Index * LINE_STEP), Line,
            Lines[Index].Id);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabel(self, "E or a click: use it. Escape or looking away: let go.", HINT_SIZE, HINT_TOP, Hint,
            &self->_hintId);
    }
    return Result;
}

static Error ConstructUi(WorldTestScreens* self)
{
    Error Result = TextComponentFactory_Construct(&self->_textFactory);
    self->_isTextFactoryConstructed = (Result.Code == ErrorCode_Success);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWidgetFactory_Construct(&self->_widgetFactory);
        self->_isWidgetFactoryConstructed = (Result.Code == ErrorCode_Success);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = LabelWidget_RegisterType(&self->_widgetFactory, &self->_labelType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWidgetFactory_RegisterType(&self->_widgetFactory, &DeskDescriptor, &self->_deskType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_Construct(&self->_screen, &self->_widgetFactory, self->_services->Logger);
        self->_isScreenConstructed = (Result.Code == ErrorCode_Success);
    }
    if (Result.Code == ErrorCode_Success)
    {
        int32_t Width = 0;
        int32_t Height = 0;
        GetPictureSize(self, &Width, &Height);
        UIViewport Viewport = UIViewport_Create((Vector2){ (float)Width, (float)Height }, (float)Width / (float)Height,
            (Vector2){ 0.5f, 0.5f });
        Result = UIScreen_SetViewport(&self->_screen, &Viewport);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = BuildUi(self);
    }
    self->_isBuilt = (Result.Code == ErrorCode_Success);
    return Result;
}


// Static functions: the tick.
/* Where the player points at a screen: the camera's aim (or the free cursor's ray), out to the walking player's reach
 * (its own body left out) or the flycam's. */
static bool Point(WorldTestScreens* self, GameInput* input, const GameCamera* camera, uint64_t playerId,
    bool isCursorCaptured, WorldScreenHit* outHit)
{
    float Reach = WORLD_TEST_SCREEN_FLY_REACH;
    uint32_t Layers = COLLISION_LAYER_MASK_ALL;
    WorldComponent* Component = NULL;
    if ((playerId != 0U) && World_TryGetComponent(self->_world, playerId, WorldComponentType_Character, &Component))
    {
        CharacterSettings Settings = WorldCharacterComponent_GetSettings(WorldCharacterComponent_FromComponent(Component));
        Reach = Settings.InteractionReach;
        Layers = Settings.InteractionLayers;
    }
    WorldCollisionFilter Filter = WorldCollisionFilter_Create(Layers & ~COLLISION_LAYER_BIT(CollisionLayer_Trigger),
        WorldCollisionTriggerMode_Ignore);
    Filter.IgnoredObjectId = playerId;
    Filter.IsIgnoredSubtree = true;
    Vector3 Direction = GameCamera_GetForward(camera);
    if (!isCursorCaptured)
    {
        Vector2 Window = GameInput_GetWindowSize(input);
        if (!(Window.x >= 1.0f) || !(Window.y >= 1.0f))
        {
            return false;
        }
        Ray CursorRay = GetScreenToWorldRayEx(GameInput_GetMousePosition(input), GameCamera_ToRenderCamera(camera),
            (int)Window.x, (int)Window.y);
        Direction = CursorRay.direction;
    }
    return WorldScreens_Raycast(self->_screens, camera->Position, Direction, Reach, self->_collision, &Filter, outHit);
}

/* While the screen is in use the desk has the UI's focus: given it when the use starts, and the use ends when the UI
 * lets go of it (Escape), so one Escape gives control back to the world. Logs the use starting and ending. */
static Error KeepDeskFocus(WorldTestScreens* self)
{
    Widget* Desk = NULL;
    bool IsInUse = WorldScreenHost_IsFocused(&self->_host);
    bool IsDeskFocused = (UIScreen_GetFocusedWidgetId(&self->_screen) == self->_deskId);
    if (IsInUse && !IsDeskFocused && self->_wasInUse && self->_wasDeskFocused)
    {
        WorldScreenHost_Release(&self->_host);
        IsInUse = false;
    }
    else if (IsInUse && !IsDeskFocused && UIScreen_TryGetWidgetById(&self->_screen, self->_deskId, &Desk))
    {
        Error Result = UIScreen_SetFocusedWidget(&self->_screen, Desk);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        IsDeskFocused = (UIScreen_GetFocusedWidgetId(&self->_screen) == self->_deskId);
    }
    if (IsInUse != self->_wasInUse)
    {
        Log(self, IsInUse ? "the monitor is in use: it has the keyboard and the clicks (Escape or looking away lets go)."
            : "let go of the monitor: the keyboard is the world's again.");
    }
    self->_wasInUse = IsInUse;
    self->_wasDeskFocused = IsDeskFocused;
    return Error_CreateSuccess();
}

/* Writes the labels whose values changed: the clock (each game minute), the pointer (each hundredth), the clicks and the
 * typed line, and the hint (with the use). */
static Error RefreshLabels(WorldTestScreens* self, const GameClock* clock, bool wasInUse)
{
    char Text[LINE_CAPACITY];
    Error Result = Error_CreateSuccess();
    int32_t Hour = 0;
    int32_t Minute = 0;
    int32_t Second = 0;
    GameClock_GetHourMinuteSecond(clock, &Hour, &Minute, &Second);
    int64_t Minutes = (GameClock_GetDay(clock) * 24 * MINUTES_PER_HOUR) + (Hour * MINUTES_PER_HOUR) + Minute;
    if (Minutes != self->_shownMinute)
    {
        GameDate Date = GameClock_GetDate(clock);
        snprintf(Text, sizeof(Text), "Clock: %04d-%02d-%02d %02d:%02d", (int)Date.Year, (int)Date.Month, (int)Date.Day,
            (int)Hour, (int)Minute);
        Result = SetLabelText(self, self->_clockId, Text);
        self->_shownMinute = Minutes;
    }
    bool IsPointed = WorldScreenHost_IsPointed(&self->_host);
    Vector2 Pointer = WorldScreenHost_GetPointer(&self->_host);
    int32_t PointerX = IsPointed ? (int32_t)lroundf(Pointer.x * HUNDREDTHS) : -1;
    int32_t PointerY = IsPointed ? (int32_t)lroundf(Pointer.y * HUNDREDTHS) : -1;
    if ((Result.Code == ErrorCode_Success) && ((PointerX != self->_shownPointerX) || (PointerY != self->_shownPointerY)))
    {
        if (IsPointed)
        {
            snprintf(Text, sizeof(Text), "Pointer: %.2f, %.2f", (double)Pointer.x, (double)Pointer.y);
        }
        else
        {
            snprintf(Text, sizeof(Text), "Pointer: off the screen");
        }
        Result = SetLabelText(self, self->_pointerId, Text);
        self->_shownPointerX = PointerX;
        self->_shownPointerY = PointerY;
    }
    if ((Result.Code == ErrorCode_Success) && self->_isTextChanged)
    {
        if (self->_clickCount > 0U)
        {
            snprintf(Text, sizeof(Text), "Clicks: %" PRIu32 " (the last at %.2f, %.2f)", self->_clickCount,
                (double)self->_lastClick.x, (double)self->_lastClick.y);
        }
        else
        {
            snprintf(Text, sizeof(Text), "Clicks: 0");
        }
        Result = SetLabelText(self, self->_clicksId, Text);
        if (Result.Code == ErrorCode_Success)
        {
            snprintf(Text, sizeof(Text), "Typed: %s_", (const char*)self->_typed);
            Result = SetLabelText(self, self->_typedId, Text);
        }
        self->_isTextChanged = false;
    }
    if ((Result.Code == ErrorCode_Success) && (wasInUse != self->_wasInUse))
    {
        Result = SetLabelText(self, self->_hintId, self->_wasInUse ? "In use: type away, Backspace deletes. Escape lets go."
            : "E or a click: use it. Escape or looking away: let go.");
    }
    return Result;
}


// Functions (declared in WorldTestScreens.h).
Error WorldTestScreens_Construct(WorldTestScreens* self, Services* services, World* world, WorldCollision* collision,
    WorldScreens* screens)
{
    Memory_Zero(self, sizeof(*self));
    self->_services = services;
    self->_world = world;
    self->_collision = collision;
    self->_screens = screens;
    self->_shownMinute = -1;
    self->_shownPointerX = -1;
    self->_shownPointerY = -1;
    self->_deskId = WIDGET_ID_INVALID;
    self->_monitorId = FindObjectId(world, MONITOR_OBJECT_NAME);
    WorldScreenHost_Construct(&self->_host, self->_monitorId);
    if (self->_monitorId == 0U)
    {
        Log(self, "the test world has no monitor: no screen to use.");
        return Error_CreateSuccess();
    }
    return ConstructUi(self);
}

void WorldTestScreens_Deconstruct(WorldTestScreens* self)
{
    if (self->_hasTarget)
    {
        UnloadRenderTexture(self->_target);
        self->_hasTarget = false;
    }
    if (self->_isScreenConstructed)
    {
        Error Result = UIScreen_Deconstruct(&self->_screen);
        Error_Deconstruct(&Result);
        self->_isScreenConstructed = false;
    }
    if (self->_isWidgetFactoryConstructed)
    {
        Error Result = UIWidgetFactory_Deconstruct(&self->_widgetFactory);
        Error_Deconstruct(&Result);
        self->_isWidgetFactoryConstructed = false;
    }
    if (self->_isTextFactoryConstructed)
    {
        Error Result = TextComponentFactory_Deconstruct(&self->_textFactory);
        Error_Deconstruct(&Result);
        self->_isTextFactoryConstructed = false;
    }
    WorldScreenHost_Deconstruct(&self->_host);
}

Error WorldTestScreens_Update(WorldTestScreens* self, ProgramTime time, const GameCamera* camera, uint64_t playerId,
    bool isCursorCaptured, const GameClock* clock)
{
    if (!self->_isBuilt)
    {
        return Error_CreateSuccess();
    }
    GameInput* Input = self->_services->Input;
    WorldScreenHit Hit;
    bool IsPointing = (Input != NULL) && Point(self, Input, camera, playerId, isCursorCaptured, &Hit);
    Error Result = WorldScreenHost_Update(&self->_host, &self->_screen, time, Input, IsPointing ? &Hit : NULL);
    bool WasInUse = self->_wasInUse;
    if (Result.Code == ErrorCode_Success)
    {
        Result = KeepDeskFocus(self);
    }
    return (Result.Code == ErrorCode_Success) ? RefreshLabels(self, clock, WasInUse) : Result;
}

Error WorldTestScreens_Render(WorldTestScreens* self, WorldRenderer* renderer)
{
    WorldScreenInfo Info;
    if (!self->_isBuilt || !WorldScreens_TryGetScreen(self->_screens, self->_monitorId, &Info))
    {
        return Error_CreateSuccess(); // a screen that cannot show anything is given nothing (it shows no picture)
    }
    int32_t Width = WorldModelSurface_GetWidth(Info.Surface);
    int32_t Height = WorldModelSurface_GetHeight(Info.Surface);
    if (self->_hasTarget && ((self->_target.texture.width != Width) || (self->_target.texture.height != Height)))
    {
        UnloadRenderTexture(self->_target);
        self->_hasTarget = false;
    }
    if (!self->_hasTarget)
    {
        self->_target = LoadRenderTexture(Width, Height);
        self->_hasTarget = (self->_target.id != 0U) && (self->_target.texture.id != 0U);
        if (!self->_hasTarget)
        {
            if (!self->_isTargetFailureReported)
            {
                self->_isTargetFailureReported = true;
                Log(self, "the monitor's picture could not be made (no render target): it shows nothing.");
            }
            return Error_CreateSuccess();
        }
    }

    RenderContext Context;
    RenderContext_Create(&Context, &self->_target, (float)Width / (float)Height, (Vector2){ 0.5f, 0.5f });
    RenderContext_BeginRendering(&Context);
    ClearBackground(Backgrounds[self->_clickCount % BACKGROUND_COUNT]);
    Error Result = UIScreen_Render(&self->_screen, &Context);
    RenderContext_EndRendering(&Context);
    RenderContext_Deconstruct(&Context);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // Smaller copies of the picture, so it stays calm when the monitor is seen small or at an angle.
    GenTextureMipmaps(&self->_target.texture);
    SetTextureFilter(self->_target.texture, TEXTURE_FILTER_TRILINEAR);
    return WorldRenderer_SetSurfaceTexture(renderer, self->_monitorId, WorldModelSurface_GetName(Info.Surface),
        self->_target.texture, true);
}

void WorldTestScreens_HandleInput(WorldTestScreens* self, GameInput* input, WorldRenderer* renderer)
{
    if ((self->_monitorId == 0U) || !GameInput_IsKeyPressed(input, GameKey_Grave))
    {
        return;
    }
    char Text[LINE_CAPACITY];
    if (GameInput_IsKeyDown(input, GameKey_LeftShift) || GameInput_IsKeyDown(input, GameKey_RightShift))
    {
        // The next mask for every CRT, then each one's own again.
        size_t StepCount = sizeof(MaskSteps) / sizeof(MaskSteps[0]);
        size_t Next = 0U;
        if (WorldRenderer_IsCrtMaskOverridden(renderer))
        {
            CrtMaskKind Shown = WorldRenderer_GetCrtMaskOverride(renderer);
            while ((Next < StepCount) && (MaskSteps[Next] != Shown))
            {
                Next++;
            }
            Next++;
        }
        bool IsOverridden = Next < StepCount;
        WorldRenderer_SetCrtMaskOverride(renderer, IsOverridden, IsOverridden ? MaskSteps[Next] : CrtMaskKind_None);
        snprintf(Text, sizeof(Text), "every CRT shows %s%s.", IsOverridden ? "the mask " : "its own mask",
            IsOverridden ? (const char*)CrtMaskKind_GetName(MaskSteps[Next]) : "");
        Log(self, Text);
        return;
    }
    WorldObject* Monitor = NULL;
    WorldScreenComponent* Screen = World_TryGetObject(self->_world, self->_monitorId, &Monitor)
        ? WorldScreenComponent_FromComponent(WorldObject_GetComponent(Monitor, WorldComponentType_Screen)) : NULL;
    if (Screen == NULL)
    {
        return;
    }
    bool IsPowered = !WorldScreenComponent_IsPowered(Screen);
    Error Result = WorldScreenComponent_SetIsPowered(Screen, IsPowered);
    Error_Deconstruct(&Result);
    Log(self, IsPowered ? "the monitor turns on." : "the monitor turns off.");
}
