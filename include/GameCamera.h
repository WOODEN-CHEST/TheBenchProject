#pragma once
// Vector3 and Camera3D are Raylib types that make up this camera's public API (position and the
// converted render camera), so this include is unavoidable here.
#include "raylib/raylib.h"


/**
 * @file GameCamera.h
 * @brief The game's 3D camera: a position plus field-of-view and yaw/pitch/roll orientation.
 *
 * GameCamera is the engine-facing camera abstraction. It is a plain value type describing where the
 * viewer is and where they look, in the game's coordinate convention: +Y is up and +Z is forward (so at
 * yaw = pitch = roll = 0 the camera looks toward +Z). Yaw rotates around the world up (+Y), pitch tilts
 * up/down, and roll spins around the forward axis.
 *
 * It is deliberately decoupled from Raylib's Camera3D: game code drives this struct, and only at render
 * time is it converted with GameCamera_ToRaylibCamera. The camera is instance/runtime data and is NOT
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
 * GameCamera_ToRaylibCamera only when rendering.
 */
typedef struct GameCameraStruct
{
    /** @brief World-space position of the camera. */
    Vector3 Position;
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
GameCamera GameCamera_Create(Vector3 position, float fovYDegrees);

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
 * @brief Converts the game camera into a Raylib perspective Camera3D for rendering.
 *
 * Sets position, target (position + forward), the roll-adjusted up vector, the vertical FOV, and a
 * perspective projection. This is the single point where the Raylib camera detail is exposed.
 * @param self The camera; must not be NULL.
 * @returns The equivalent Raylib Camera3D (CAMERA_PERSPECTIVE).
 */
Camera3D GameCamera_ToRaylibCamera(const GameCamera* self);
