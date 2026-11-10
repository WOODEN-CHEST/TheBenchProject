#include <math.h>
#include "EditorCamera.h"
#include "GameMath.h"
#include "raylib/raymath.h"


// Static functions.
static float CleanAxis(float value)
{
    return isfinite(value) ? Clamp(value, -1.0f, 1.0f) : 0.0f;
}

static float CleanFinite(float value)
{
    return isfinite(value) ? value : 0.0f;
}

/* The wheel's whole notches step the speed between the limits; a fraction waits for the rest of its notch. */
static bool StepSpeed(EditorCamera* self, float notches)
{
    self->_wheelRemainder += CleanFinite(notches);
    float Whole = truncf(self->_wheelRemainder);
    self->_wheelRemainder -= Whole;
    float Range = (float)(EDITOR_CAMERA_MAX_STEP - EDITOR_CAMERA_MIN_STEP);
    int32_t Step = self->_speedStep + (int32_t)Clamp(Whole, -Range, Range);
    Step = (Step < EDITOR_CAMERA_MIN_STEP) ? EDITOR_CAMERA_MIN_STEP : ((Step > EDITOR_CAMERA_MAX_STEP) ? EDITOR_CAMERA_MAX_STEP : Step);
    bool HasChanged = Step != self->_speedStep;
    self->_speedStep = Step;
    return HasChanged;
}

/* The pointer moving right turns the view right: a larger yaw turns forward toward +X, which shows on the screen's left under the
 * right-handed view, so moving right lowers the yaw; moving down looks down. */
static bool Turn(GameCamera* camera, Vector2 lookPixels)
{
    float DeltaX = CleanFinite(lookPixels.x);
    float DeltaY = CleanFinite(lookPixels.y);
    if ((DeltaX == 0.0f) && (DeltaY == 0.0f))
    {
        return false;
    }
    camera->Yaw -= DeltaX * EDITOR_CAMERA_RADIANS_PER_PIXEL;
    camera->Pitch = Clamp(camera->Pitch - (DeltaY * EDITOR_CAMERA_RADIANS_PER_PIXEL), -GAME_CAMERA_MAX_PITCH, GAME_CAMERA_MAX_PITCH);
    return true;
}

static bool Move(const EditorCamera* self, GameCamera* camera, const EditorCameraControls* controls, double deltaSeconds)
{
    if (!isfinite(deltaSeconds) || !(deltaSeconds > 0.0))
    {
        return false;
    }
    Vector3 Forward = GameCamera_GetForward(camera);
    Vector3 Right = GameCamera_GetRight(camera);
    Vector3 Direction = Vector3Add(Vector3Add(Vector3Scale(Forward, CleanAxis(controls->Forward)), Vector3Scale(Right, CleanAxis(controls->Right))),
        (Vector3){ .x = 0.0f, .y = CleanAxis(controls->Up), .z = 0.0f });
    float Length = Vector3Length(Direction);
    if (!(Length > 0.0f))
    {
        return false;
    }
    double Speed = (double)EditorCamera_GetSpeed(self) * (controls->IsFast ? (double)EDITOR_CAMERA_FAST_FACTOR : 1.0);
    // Several axes held go no faster than one: the direction is made a unit long, in double for the step.
    Vector3Double Step = Vector3Double_Scale(GameMath_FromVector3(Vector3Scale(Direction, 1.0f / Length)), Speed * deltaSeconds);
    camera->Position = Vector3Double_Add(camera->Position, Step);
    return true;
}


// Public functions.
void EditorCamera_Construct(EditorCamera* self)
{
    self->_speedStep = 0;
    self->_wheelRemainder = 0.0f;
}

void EditorCamera_Deconstruct(EditorCamera* self)
{
    (void)self;
}

float EditorCamera_GetSpeed(const EditorCamera* self)
{
    return EDITOR_CAMERA_BASE_SPEED * exp2f((float)self->_speedStep / (float)EDITOR_CAMERA_STEPS_PER_DOUBLING);
}

EditorCameraChange EditorCamera_Apply(EditorCamera* self, GameCamera* camera, const EditorCameraControls* controls, double deltaSeconds)
{
    EditorCameraChange Change = { .HasTurned = false, .HasMoved = false, .HasSpeedChanged = StepSpeed(self, controls->WheelNotches) };
    if (!controls->IsFlying)
    {
        return Change;
    }
    Change.HasTurned = Turn(camera, controls->LookPixels);
    Change.HasMoved = Move(self, camera, controls, deltaSeconds);
    return Change;
}
