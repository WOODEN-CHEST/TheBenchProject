#include <math.h>
#include "GameCamera.h"
#include "raylib/raymath.h"


// Fields.
/* The world up direction (+Y), per the game's coordinate convention. */
static const Vector3 WorldUp = { .x = 0.0f, .y = 1.0f, .z = 0.0f };


// Public functions.
GameCamera GameCamera_Create(Vector3 position, float fovYDegrees)
{
    return (GameCamera)
    {
        .Position = position,
        .FovY = fovYDegrees,
        .Yaw = 0.0f,
        .Pitch = 0.0f,
        .Roll = 0.0f
    };
}

Vector3 GameCamera_GetForward(const GameCamera* self)
{
    float CosPitch = cosf(self->Pitch);
    Vector3 Forward =
    {
        .x = sinf(self->Yaw) * CosPitch,
        .y = sinf(self->Pitch),
        .z = cosf(self->Yaw) * CosPitch
    };
    return Vector3Normalize(Forward);
}

Vector3 GameCamera_GetRight(const GameCamera* self)
{
    // Forward x WorldUp gives the screen-right axis under Raylib's right-handed view (where, with +Z
    // forward and +Y up, world +X falls on the screen's left); WorldUp x Forward would point left.
    return Vector3Normalize(Vector3CrossProduct(GameCamera_GetForward(self), WorldUp));
}

Vector3 GameCamera_GetUp(const GameCamera* self)
{
    return Vector3RotateByAxisAngle(WorldUp, GameCamera_GetForward(self), self->Roll);
}

Camera3D GameCamera_ToRaylibCamera(const GameCamera* self)
{
    Vector3 Forward = GameCamera_GetForward(self);
    return (Camera3D)
    {
        .position = self->Position,
        .target = Vector3Add(self->Position, Forward),
        .up = Vector3RotateByAxisAngle(WorldUp, Forward, self->Roll),
        .fovy = self->FovY,
        .projection = CAMERA_PERSPECTIVE
    };
}
