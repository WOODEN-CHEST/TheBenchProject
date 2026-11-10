#pragma once
#include <stdbool.h>
#include <stdint.h>
// Vector2 (the pointer's motion) is a raylib type; GameCamera.h includes raylib anyway.
#include "raylib/raylib.h"
#include "GameCamera.h"


/**
 * @file EditorCamera.h
 * @brief The editor's fly camera as pure maths: what one tick of controls does to a GameCamera.
 *
 * Its host (the editor frame) fills EditorCameraControls each tick from its world view's record and the input actions, and applies them:
 * while flying (the right button held in the view) the pointer's motion turns the camera and the movement axes fly it along where it looks
 * (forward and back), across (left and right) and up and down the world's +Y; the wheel steps the speed up or down in whole notches,
 * doubling every EDITOR_CAMERA_STEPS_PER_DOUBLING notches between EDITOR_CAMERA_MIN_STEP and EDITOR_CAMERA_MAX_STEP. No input is read here,
 * so every rule is tested without a window. The camera's position stays a double, so flying works anywhere in a large world.
 *
 * A plain value type: construct it with EditorCamera_Construct and mutate it only through these functions.
 */


// Macros.
/** @brief The flying speed at step 0, in metres a second. */
#define EDITOR_CAMERA_BASE_SPEED 6.0f
/** @brief How many wheel notches double the speed. */
#define EDITOR_CAMERA_STEPS_PER_DOUBLING 3
/** @brief The slowest step: 6 m/s halved four times, 0.375 m/s. */
#define EDITOR_CAMERA_MIN_STEP (-12)
/** @brief The fastest step: 6 m/s doubled ten times, about 6 km/s, to cross a large world. */
#define EDITOR_CAMERA_MAX_STEP 30
/** @brief How much faster the fast key flies. */
#define EDITOR_CAMERA_FAST_FACTOR 3.0f
/** @brief How far the camera turns per surface pixel of the pointer's motion, in radians. */
#define EDITOR_CAMERA_RADIANS_PER_PIXEL 0.0025f


// Types.
/**
 * @brief One tick of controls for the camera. A plain value; zero it for a tick that does nothing.
 */
typedef struct EditorCameraControlsStruct
{
    /** @brief Whether the camera flies this tick (the right button held in the view): only then do LookPixels and the axes count. */
    bool IsFlying;
    /** @brief The pointer's motion this tick, in surface pixels: right turns right, down looks down. Non-finite components count as 0. */
    Vector2 LookPixels;
    /** @brief Forward less back, within [-1, 1]: along where the camera looks. Clamped; non-finite counts as 0. */
    float Forward;
    /** @brief Right less left, within [-1, 1]. Clamped; non-finite counts as 0. */
    float Right;
    /** @brief Up less down, within [-1, 1]: along the world's +Y. Clamped; non-finite counts as 0. */
    float Up;
    /** @brief Whether the fast key is held: EDITOR_CAMERA_FAST_FACTOR times the speed. */
    bool IsFast;
    /** @brief Wheel notches this tick, up is faster; counted flying or not. A fraction waits for the rest of its notch. Non-finite counts
     *  as 0. */
    float WheelNotches;
} EditorCameraControls;

/**
 * @brief The fly camera's own state: its speed step and the wheel's part notch. Read-only outside the module.
 */
typedef struct EditorCameraStruct
{
    /** @brief The speed step, within [EDITOR_CAMERA_MIN_STEP, EDITOR_CAMERA_MAX_STEP]. */
    int32_t _speedStep;
    /** @brief The wheel's movement not yet a whole notch, within (-1, 1). */
    float _wheelRemainder;
} EditorCamera;

/**
 * @brief What one application changed. A plain value.
 */
typedef struct EditorCameraChangeStruct
{
    /** @brief The camera turned. */
    bool HasTurned;
    /** @brief The camera moved. */
    bool HasMoved;
    /** @brief The speed step changed. */
    bool HasSpeedChanged;
} EditorCameraChange;


// Functions.
/**
 * @brief Constructs the state at step 0 (EDITOR_CAMERA_BASE_SPEED).
 * @param self The state; must not be NULL.
 */
void EditorCamera_Construct(EditorCamera* self);

/**
 * @brief Deconstructs the state; it owns nothing.
 * @param self The state; must not be NULL.
 */
void EditorCamera_Deconstruct(EditorCamera* self);

/**
 * @brief Returns the speed step.
 * @param self The state; must not be NULL.
 * @returns The step, within [EDITOR_CAMERA_MIN_STEP, EDITOR_CAMERA_MAX_STEP].
 */
static inline int32_t EditorCamera_GetSpeedStep(const EditorCamera* self)
{
    return self->_speedStep;
}

/**
 * @brief Returns the flying speed without the fast key: EDITOR_CAMERA_BASE_SPEED doubled every EDITOR_CAMERA_STEPS_PER_DOUBLING steps.
 * @param self The state; must not be NULL.
 * @returns The speed, in metres a second.
 */
float EditorCamera_GetSpeed(const EditorCamera* self);

/**
 * @brief Applies one tick of controls to a camera: the wheel's whole notches step the speed (flying or not); while flying, the pointer's
 *        motion turns it (the pitch kept within GAME_CAMERA_MAX_PITCH) and the axes move it, normalized when several are held, at the speed
 *        (times EDITOR_CAMERA_FAST_FACTOR when fast) for @p deltaSeconds.
 * @param self The state; must not be NULL.
 * @param camera The camera to move; must not be NULL. Its position stays a double.
 * @param controls The tick's controls; must not be NULL.
 * @param deltaSeconds The tick's length; a value that is not a finite number above 0 moves nothing (the camera may still turn).
 * @returns What changed.
 */
EditorCameraChange EditorCamera_Apply(EditorCamera* self, GameCamera* camera, const EditorCameraControls* controls, double deltaSeconds);
