#version 330

// A lightning bolt's channel (the pure maths source/WorldRain.h, "THE BOLT"): a quad standing from the ground to the
// clouds' base toward the strike, turned about the vertical to face the camera, showing one frame of the bolt animation
// (its area in its texture; mirrored by a negative width). It drops with the planet's curve as the ground under it
// does (common/curvature).

#include "common/curvature"

in vec3 vertexPosition; // the unit quad: x, y in [0, 1]
in vec2 vertexTexCoord;

uniform mat4 rainViewProjection; // render space -> the scene's clip space
uniform vec3 boltFoot;           // render space: its foot on the ground
uniform vec3 boltTop;            // and its top at the clouds' base
uniform float boltWidth;         // metres
uniform vec4 boltArea;           // its frame's area in its texture: x and y from its top left, width (negative: mirrored),
                                 // height (0 .. 1)

out vec3 fragPosition; // render space
out vec2 fragTexCoord;

void main()
{
    vec3 toCamera = vec3(-boltFoot.x, 0.0, -boltFoot.z);
    vec3 right = (dot(toCamera, toCamera) > 1e-6) ? normalize(cross(vec3(0.0, 1.0, 0.0), toCamera)) : vec3(1.0, 0.0, 0.0);
    vec3 position = boltFoot + (boltTop - boltFoot)*vertexPosition.y + right*((vertexPosition.x - 0.5)*boltWidth);
    gl_Position = CurvedClipPosition(rainViewProjection*vec4(position, 1.0), position);
    fragPosition = position;
    // The frame's top row is the clouds' end.
    fragTexCoord = boltArea.xy + vec2(vertexPosition.x, 1.0 - vertexPosition.y)*boltArea.zw;
}
