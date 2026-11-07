#include "UIWindowHost.h"
#include "wr/WRCompile.h"


// Static functions: the standard cursor.
static void CaptureStandard(void* context, GameInput* input)
{
    UNUSED(context);
    GameInput_CaptureCursor(input);
}

static void ReleaseStandard(void* context, GameInput* input, Vector2 position)
{
    UNUSED(context);
    GameInput_ReleaseCursor(input, position);
}

static void SetShapeStandard(void* context, UICursorShape shape)
{
    UNUSED(context);
    int RaylibShape = MOUSE_CURSOR_DEFAULT;
    switch (shape)
    {
        case UICursorShape_Arrow:              RaylibShape = MOUSE_CURSOR_DEFAULT; break;
        case UICursorShape_IBeam:              RaylibShape = MOUSE_CURSOR_IBEAM; break;
        case UICursorShape_ResizeHorizontal:   RaylibShape = MOUSE_CURSOR_RESIZE_EW; break;
        case UICursorShape_ResizeVertical:     RaylibShape = MOUSE_CURSOR_RESIZE_NS; break;
        case UICursorShape_ResizeDiagonalDown: RaylibShape = MOUSE_CURSOR_RESIZE_NWSE; break;
        case UICursorShape_ResizeDiagonalUp:   RaylibShape = MOUSE_CURSOR_RESIZE_NESW; break;
        case UICursorShape_Move:               RaylibShape = MOUSE_CURSOR_RESIZE_ALL; break;
        case UICursorShape_Hand:               RaylibShape = MOUSE_CURSOR_POINTING_HAND; break;
        case UICursorShape_NotAllowed:         RaylibShape = MOUSE_CURSOR_NOT_ALLOWED; break;
        case UICursorShape_Count:              break;
    }
    if (IsWindowReady())
    {
        SetMouseCursor(RaylibShape);
    }
}


// Static functions.
/* Does what the screen asks of the cursor, after its update. */
static void ApplyCursor(UIWindowHost* self, const UIScreen* screen, GameInput* input)
{
    bool IsWanted = UIScreen_IsPointerCaptured(screen);
    if (IsWanted && (self->_capturedInput == NULL))
    {
        self->_captureStart = GameInput_GetMousePosition(input);
        self->_capturedInput = input;
        self->_cursor.Capture(self->_cursor.Context, input);
    }
    else if (!IsWanted && (self->_capturedInput != NULL))
    {
        self->_cursor.Release(self->_cursor.Context, self->_capturedInput, self->_captureStart);
        self->_capturedInput = NULL;
    }
    UICursorShape Shape = UIScreen_GetCursorShape(screen);
    if (Shape != self->_shownShape)
    {
        self->_shownShape = Shape;
        self->_cursor.SetShape(self->_cursor.Context, Shape);
    }
}


// Public functions.
UIWindowHostCursor UIWindowHostCursor_CreateStandard(void)
{
    return (UIWindowHostCursor){ .Capture = CaptureStandard, .Release = ReleaseStandard, .SetShape = SetShapeStandard, .Context = NULL };
}

void UIWindowHost_Construct(UIWindowHost* self, UIScalePolicy policy, float playerScale, float targetAspectRatio,
    Vector2 targetAreaPosition)
{
    self->_policy = policy;
    self->_playerScale = playerScale;
    self->_targetAspectRatio = targetAspectRatio;
    self->_targetAreaPosition = targetAreaPosition;
    self->_cursor = UIWindowHostCursor_CreateStandard();
    self->_capturedInput = NULL;
    self->_captureStart = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_shownShape = UICursorShape_Arrow;
}

void UIWindowHost_Deconstruct(UIWindowHost* self)
{
    if (self == NULL)
    {
        return;
    }
    if (self->_capturedInput != NULL)
    {
        self->_cursor.Release(self->_cursor.Context, self->_capturedInput, self->_captureStart);
        self->_capturedInput = NULL;
    }
    if (self->_shownShape != UICursorShape_Arrow)
    {
        self->_shownShape = UICursorShape_Arrow;
        self->_cursor.SetShape(self->_cursor.Context, UICursorShape_Arrow);
    }
}

void UIWindowHost_SetCursor(UIWindowHost* self, const UIWindowHostCursor* cursor)
{
    self->_cursor = *cursor;
}

Error UIWindowHost_Update(UIWindowHost* self, UIScreen* screen, ProgramTime time, GameInput* input)
{
    Vector2 WindowSize = { .x = 0.0f, .y = 0.0f };
    Vector2 MonitorSize = { .x = 0.0f, .y = 0.0f };
    if (input != NULL)
    {
        WindowSize = GameInput_GetWindowSize(input);
        MonitorSize = GameInput_GetMonitorSize(input);
    }
    if ((WindowSize.x > 0.0f) && (WindowSize.y > 0.0f))
    {
        UIViewport Viewport = UIViewport_Create(WindowSize, self->_targetAspectRatio, self->_targetAreaPosition);
        Error ViewportResult = UIScreen_SetViewport(screen, &Viewport);
        if (ViewportResult.Code != ErrorCode_Success)
        {
            return ViewportResult;
        }
    }
    Error ScaleResult = UIScreen_SetScale(screen, UIScalePolicy_GetScale(&self->_policy, self->_playerScale, WindowSize.y, MonitorSize.y));
    if (ScaleResult.Code != ErrorCode_Success)
    {
        return ScaleResult;
    }
    UIScreenInput ScreenInput = UIScreenInput_CreateFromWindow(input);
    if ((self->_capturedInput != NULL) && (input != NULL))
    {
        ScreenInput.IsPointerRelative = true;
        ScreenInput.PointerDeltaPixels = GameInput_GetMouseDelta(input);
    }
    Error Result = UIScreen_Update(screen, time, &ScreenInput);
    if ((Result.Code == ErrorCode_Success) && (input != NULL))
    {
        ApplyCursor(self, screen, input);
    }
    return Result;
}
