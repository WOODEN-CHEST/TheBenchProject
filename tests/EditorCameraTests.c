#include "TestSuites.h"
#include "EditorCamera.h"
#include "GameCamera.h"
#include <math.h>


// Static functions: helpers.
static GameCamera MakeCamera(void)
{
    return GameCamera_Create(Vector3Double_Create(0.0, 2.0, 0.0), GAME_CAMERA_DEFAULT_FOV_Y);
}

static EditorCameraControls Flying(void)
{
    return (EditorCameraControls){ .IsFlying = true, .LookPixels = { 0.0f, 0.0f }, .Forward = 0.0f, .Right = 0.0f, .Up = 0.0f, .IsFast = false,
        .WheelNotches = 0.0f };
}

static bool IsNear(double a, double b, double tolerance)
{
    return fabs(a - b) <= tolerance;
}

static double Distance(Vector3Double a, Vector3Double b)
{
    return Vector3Double_Length(Vector3Double_Subtract(a, b));
}


// Tests.
static void ItStartsAtTheBaseSpeed(TestRun* run)
{
    EditorCamera Flight;
    EditorCamera_Construct(&Flight);
    TEST_CHECK(run, (EditorCamera_GetSpeedStep(&Flight) == 0) && IsNear(EditorCamera_GetSpeed(&Flight), EDITOR_CAMERA_BASE_SPEED, 1e-6));
    EditorCamera_Deconstruct(&Flight);
}

static void TheWheelStepsTheSpeedInWholeNotches(TestRun* run)
{
    EditorCamera Flight;
    EditorCamera_Construct(&Flight);
    GameCamera Eye = MakeCamera();
    EditorCameraControls Controls = { .IsFlying = false, .WheelNotches = 3.0f };
    EditorCameraChange Change = EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01);
    TEST_CHECK(run, Change.HasSpeedChanged && !Change.HasMoved && !Change.HasTurned);
    TEST_CHECK(run, IsNear(EditorCamera_GetSpeed(&Flight), 2.0 * EDITOR_CAMERA_BASE_SPEED, 1e-4)); // Flying or not.
    // Half notches wait for the rest of theirs.
    Controls.WheelNotches = -0.5f;
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01).HasSpeedChanged && (EditorCamera_GetSpeedStep(&Flight) == 3));
    TEST_CHECK(run, EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01).HasSpeedChanged && (EditorCamera_GetSpeedStep(&Flight) == 2));
    // The limits hold, and a notch past them changes nothing.
    Controls.WheelNotches = 1000.0f;
    (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01);
    TEST_CHECK(run, EditorCamera_GetSpeedStep(&Flight) == EDITOR_CAMERA_MAX_STEP);
    Controls.WheelNotches = 1.0f;
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01).HasSpeedChanged);
    Controls.WheelNotches = -1e30f;
    (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01);
    (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01);
    TEST_CHECK(run, (EditorCamera_GetSpeedStep(&Flight) == EDITOR_CAMERA_MIN_STEP)
        && IsNear(EditorCamera_GetSpeed(&Flight), EDITOR_CAMERA_BASE_SPEED / 16.0, 1e-6));
    // What is no number moves nothing.
    Controls.WheelNotches = NAN;
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01).HasSpeedChanged && (EditorCamera_GetSpeedStep(&Flight) == EDITOR_CAMERA_MIN_STEP));
    Controls.WheelNotches = INFINITY;
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01).HasSpeedChanged);
    EditorCamera_Deconstruct(&Flight);
}

static void LookingTurnsTheCameraTheWayThePointerGoes(TestRun* run)
{
    EditorCamera Flight;
    EditorCamera_Construct(&Flight);
    GameCamera Eye = MakeCamera();
    EditorCameraControls Controls = Flying();
    Controls.LookPixels = (Vector2){ .x = 40.0f, .y = 0.0f };
    TEST_CHECK(run, EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01).HasTurned);
    // Right turns right: the forward direction swings toward the camera's right.
    TEST_CHECK(run, IsNear(Eye.Yaw, -40.0 * EDITOR_CAMERA_RADIANS_PER_PIXEL, 1e-6) && (GameCamera_GetForward(&Eye).x < 0.0f));
    Controls.LookPixels = (Vector2){ .x = 0.0f, .y = 20.0f };
    (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01);
    TEST_CHECK(run, IsNear(Eye.Pitch, -20.0 * EDITOR_CAMERA_RADIANS_PER_PIXEL, 1e-6)); // Down looks down.
    Controls.LookPixels = (Vector2){ .x = 0.0f, .y = -1e6f };
    (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01);
    TEST_CHECK(run, IsNear(Eye.Pitch, GAME_CAMERA_MAX_PITCH, 1e-6));
    // Not flying, the pointer turns nothing; what is no number neither.
    float Yaw = Eye.Yaw;
    Controls.IsFlying = false;
    Controls.LookPixels = (Vector2){ .x = 100.0f, .y = 100.0f };
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01).HasTurned && (Eye.Yaw == Yaw));
    Controls.IsFlying = true;
    Controls.LookPixels = (Vector2){ .x = NAN, .y = INFINITY };
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, 0.01).HasTurned && (Eye.Yaw == Yaw)
        && IsNear(Eye.Pitch, GAME_CAMERA_MAX_PITCH, 1e-6));
    EditorCamera_Deconstruct(&Flight);
}

static void FlyingMovesAlongWhereItLooks(TestRun* run)
{
    EditorCamera Flight;
    EditorCamera_Construct(&Flight);
    GameCamera Eye = MakeCamera();
    Vector3Double Start = Eye.Position;
    EditorCameraControls Controls = Flying();
    Controls.Forward = 1.0f;
    TEST_CHECK(run, EditorCamera_Apply(&Flight, &Eye, &Controls, 0.5).HasMoved);
    TEST_CHECK(run, IsNear(Eye.Position.Z - Start.Z, 3.0, 1e-5) && IsNear(Eye.Position.X, 0.0, 1e-9) && IsNear(Eye.Position.Y, 2.0, 1e-9));
    // Across: along the camera's right.
    Vector3 Right = GameCamera_GetRight(&Eye);
    Start = Eye.Position;
    Controls.Forward = 0.0f;
    Controls.Right = 1.0f;
    (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 0.5);
    TEST_CHECK(run, IsNear(Eye.Position.X - Start.X, 3.0 * (double)Right.x, 1e-5) && IsNear(Eye.Position.Z - Start.Z, 3.0 * (double)Right.z, 1e-5));
    // Up along the world's +Y whatever the pitch, and E up, Q down.
    Eye.Pitch = -1.0f;
    Start = Eye.Position;
    Controls.Right = 0.0f;
    Controls.Up = -1.0f;
    (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 0.5);
    TEST_CHECK(run, IsNear(Eye.Position.Y - Start.Y, -3.0, 1e-5) && IsNear(Distance(Eye.Position, Start), 3.0, 1e-5));
    // Several axes go no faster than one, and the fast key flies three times as fast.
    Start = Eye.Position;
    Controls.Forward = 1.0f;
    Controls.Right = -1.0f;
    Controls.Up = 1.0f;
    (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 0.5);
    TEST_CHECK(run, IsNear(Distance(Eye.Position, Start), 3.0, 1e-5));
    Start = Eye.Position;
    Controls.IsFast = true;
    (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 0.5);
    TEST_CHECK(run, IsNear(Distance(Eye.Position, Start), 3.0 * EDITOR_CAMERA_FAST_FACTOR, 1e-4));
    // Axes past one are one: five forward and one across go the way one and one do.
    Eye.Pitch = 0.0f;
    Start = Eye.Position;
    Controls = Flying();
    Controls.Forward = 5.0f;
    Controls.Right = 1.0f;
    (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 0.5);
    Vector3Double Clamped = Vector3Double_Subtract(Eye.Position, Start);
    Start = Eye.Position;
    Controls.Forward = 1.0f;
    (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 0.5);
    Vector3Double Even = Vector3Double_Subtract(Eye.Position, Start);
    TEST_CHECK(run, IsNear(Clamped.X, Even.X, 1e-5) && IsNear(Clamped.Z, Even.Z, 1e-5) && IsNear(Vector3Double_Length(Clamped), 3.0, 1e-5));
    EditorCamera_Deconstruct(&Flight);
}

static void NothingMovesWithoutFlyingOrTime(TestRun* run)
{
    EditorCamera Flight;
    EditorCamera_Construct(&Flight);
    GameCamera Eye = MakeCamera();
    Vector3Double Start = Eye.Position;
    EditorCameraControls Controls = Flying();
    Controls.Forward = 1.0f;
    Controls.IsFlying = false;
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, 0.5).HasMoved);
    Controls.IsFlying = true;
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, 0.0).HasMoved);
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, -1.0).HasMoved);
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, NAN).HasMoved);
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, INFINITY).HasMoved);
    Controls.Forward = NAN;
    TEST_CHECK(run, !EditorCamera_Apply(&Flight, &Eye, &Controls, 0.5).HasMoved);
    TEST_CHECK(run, Distance(Eye.Position, Start) == 0.0);
    EditorCamera_Deconstruct(&Flight);
}

static void FlyingFarAwayKeepsItsSteps(TestRun* run)
{
    EditorCamera Flight;
    EditorCamera_Construct(&Flight);
    GameCamera Eye = GameCamera_Create(Vector3Double_Create(1.0e7, 0.0, -2.0e7), GAME_CAMERA_DEFAULT_FOV_Y);
    EditorCameraControls Controls = Flying();
    Controls.Forward = 1.0f;
    for (int Tick = 0; Tick < 480; Tick++)
    {
        (void)EditorCamera_Apply(&Flight, &Eye, &Controls, 1.0 / 480.0);
    }
    // 10 000 km out, a second of flight is its 6 m to the millimetre: the steps are added in double.
    TEST_CHECK(run, IsNear(Eye.Position.Z - (-2.0e7), EDITOR_CAMERA_BASE_SPEED, 1e-3) && (Eye.Position.X == 1.0e7));
    EditorCamera_Deconstruct(&Flight);
}


// Public functions.
void EditorCameraTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "it_starts_at_the_base_speed", ItStartsAtTheBaseSpeed },
        { "the_wheel_steps_the_speed_in_whole_notches", TheWheelStepsTheSpeedInWholeNotches },
        { "looking_turns_the_camera_the_way_the_pointer_goes", LookingTurnsTheCameraTheWayThePointerGoes },
        { "flying_moves_along_where_it_looks", FlyingMovesAlongWhereItLooks },
        { "nothing_moves_without_flying_or_time", NothingMovesWithoutFlyingOrTime },
        { "flying_far_away_keeps_its_steps", FlyingFarAwayKeepsItsSteps },
    };
    TestRun_RunSuite(run, "EditorCamera", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
