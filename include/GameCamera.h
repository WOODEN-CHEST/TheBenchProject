#pragma once
// Vector3 and Camera3D are Raylib types that make up this camera's public API (its directions and the converted
// render camera), so this include is unavoidable here.
#include "raylib/raylib.h"
#include "wr/WRVectorMath.h"


/**
 * @file GameCamera.h
 * @brief The game's 3D camera: a position plus field-of-view and yaw/pitch/roll orientation.
 *
 * GameCamera is the engine-facing camera abstraction. It is a plain value type describing where the
 * viewer is and where they look, in the game's coordinate convention: +Y is up and +Z is forward (so at
 * yaw = pitch = roll = 0 the camera looks toward +Z). Yaw rotates around the world up (+Y), pitch tilts
 * up/down, and roll spins around the forward axis.
 *
 * Its position is a double (WRVectorMath's Vector3Double): far from the world's origin a float holds a position only to
 * millimetres or worse (a float's step is 1 mm past 8 km), which would shake the whole view. Its directions stay floats.
 * The renderer draws relative to the camera (render space: the world's orientation with the camera at (0, 0, 0)), so
 * the raylib camera made from it, GameCamera_ToRenderCamera, sits at the origin and only turns.
 *
 * It is deliberately decoupled from Raylib's Camera3D: game code drives this struct, and only at render
 * time is it converted with GameCamera_ToRenderCamera. The camera is instance/runtime data and is NOT
 * part of any saved world.
 *
 * The struct owns no memory and is trivially copyable (like ProgramTime / Int32Vector), so it has no
 * Construct/Deconstruct pair — build one with GameCamera_Create and mutate its public fields directly.
 */


// Macros.
/** @brief Default vertical field of view, in degrees, used by GameCamera_Create. */
#define GAME_CAMERA_DEFAULT_FOV_Y 60.0f
/** @brief Largest absolute pitch (radians) a caller should clamp to, to avoid gimbal flip at straight up/down. */
#define GAME_CAMERA_MAX_PITCH 1.5533f


// Types.
/**
 * @brief A 3D camera described by position, vertical FOV, and yaw/pitch/roll (radians).
 *
 * Public fields are mutated directly by game code (movement/look). Convert to a Raylib Camera3D with
 * GameCamera_ToRenderCamera only when rendering.
 */
typedef struct GameCameraStruct
{
    /** @brief World-space position of the camera, in double: the renderer's origin each frame. */
    Vector3Double Position;
    /** @brief Vertical field of view, in degrees. */
    float FovY;
    /** @brief Yaw around the world up (+Y), in radians; 0 looks toward +Z. */
    float Yaw;
    /** @brief Pitch (look up/down), in radians; positive looks up (+Y). */
    float Pitch;
    /** @brief Roll around the forward axis, in radians; 0 keeps the horizon level. */
    float Roll;
} GameCamera;


// Functions.
/**
 * @brief Builds a camera at a position with a given vertical FOV, looking toward +Z (all angles 0).
 * @param position World-space position.
 * @param fovYDegrees Vertical field of view in degrees.
 * @returns A GameCamera with the given position/FOV and zero yaw/pitch/roll.
 */
GameCamera GameCamera_Create(Vector3Double position, float fovYDegrees);

/**
 * @brief Returns the camera's unit forward direction from its yaw and pitch.
 * @param self The camera; must not be NULL.
 * @returns The normalized forward vector (toward +Z at yaw = pitch = 0).
 */
Vector3 GameCamera_GetForward(const GameCamera* self);

/**
 * @brief Returns the camera's unit right direction (horizontal, from forward and world up).
 *
 * Computed from the forward vector and world up (+Y); independent of roll. Useful for strafing movement.
 * @param self The camera; must not be NULL.
 * @returns The normalized right vector.
 */
Vector3 GameCamera_GetRight(const GameCamera* self);

/**
 * @brief Returns the camera's up vector (world up rotated by roll around the forward axis).
 * @param self The camera; must not be NULL.
 * @returns The up vector used for the render camera.
 */
Vector3 GameCamera_GetUp(const GameCamera* self);

/**
 * @brief Converts the game camera into the Raylib perspective Camera3D the renderer draws with: the camera in render
 *        space, at (0, 0, 0).
 *
 * Sets the position to (0, 0, 0), the target to the forward direction, the roll-adjusted up vector, the vertical FOV,
 * and a perspective projection, so BeginMode3D loads a view matrix that is a pure rotation (no translation, however far
 * the camera is from the world's origin). The renderer subtracts the camera's Position from everything it draws. This
 * is the single point where a Raylib camera is made.
 * @param self The camera; must not be NULL.
 * @returns The render camera (CAMERA_PERSPECTIVE).
 */
Camera3D GameCamera_ToRenderCamera(const GameCamera* self);
