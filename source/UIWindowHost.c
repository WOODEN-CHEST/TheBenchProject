#include "UIWindowHost.h"
#include "wr/WRCompile.h"


// Public functions.
void UIWindowHost_Construct(UIWindowHost* self, UIScalePolicy policy, float playerScale, float targetAspectRatio,
    Vector2 targetAreaPosition)
{
    self->_policy = policy;
    self->_playerScale = playerScale;
    self->_targetAspectRatio = targetAspectRatio;
    self->_targetAreaPosition = targetAreaPosition;
}

void UIWindowHost_Deconstruct(UIWindowHost* self)
{
    UNUSED(self);
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
    return UIScreen_Update(screen, time, &ScreenInput);
}
