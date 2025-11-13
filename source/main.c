#include <stdio.h>
#include "WRError.h"
#include "WRUnicode.h"
#include "WRUnicodeLoader.h"
#include "WRChar.h"
#include "WRNumber.h"
#include "raylib.h"
#include "raymath.h"

// Functions.
int main()
{
    DisableEventWaiting();
    InitWindow(1280, 720, "test");

    Vector3 Pos = (Vector3){ 1.0f, 1.0f, 1.0f };

    Camera3D Camera;
    Camera.fovy = 70.0f,
    Camera.projection = CAMERA_PERSPECTIVE;
    Camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    Camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    Camera.position = Pos;

    

    while (!WindowShouldClose())
    {
        PollInputEvents();



        BeginDrawing();
        ClearBackground(BLANK);

        BeginMode3D(Camera);

        DrawSphereEx((Vector3) { 0, 0, 0}, 0.25f, 12, 24, WHITE);

        EndMode3D();

        EndDrawing();
    }

    return 0;
}