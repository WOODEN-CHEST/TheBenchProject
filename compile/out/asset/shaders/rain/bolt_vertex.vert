#version 330

// A lightning bolt's channel (the pure maths source/WorldRain.h, "THE BOLT"): a quad standing from the ground to the
// clouds' base toward the strike, turned about the vertical to face the camera, showing one of the bolt texture's bolts.

in vec3 vertexPosition; // the unit quad: x, y in [0, 1]
in vec2 vertexTexCoord;

uniform mat4 rainViewProjection; // render space -> the scene's clip space
uniform vec3 boltFoot;           // render space: its foot on the ground
uniform vec3 boltTop;            // and its top at the clouds' base
uniform float boltWidth;         // metres
uniform vec2 boltColumns;        // x: where its bolt starts across the texture, y: its share of it (negative: mirrored)

out vec3 fragPosition; // render space
out vec2 fragTexCoord;

void main()
{
    vec3 toCamera = vec3(-boltFoot.x, 0.0, -boltFoot.z);
    vec3 right = (dot(toCamera, toCamera) > 1e-6) ? normalize(cross(vec3(0.0, 1.0, 0.0), toCamera)) : vec3(1.0, 0.0, 0.0);
    vec3 position = boltFoot + (boltTop - boltFoot)*vertexPosition.y + right*((vertexPosition.x - 0.5)*boltWidth);
    gl_Position = rainViewProjection*vec4(position, 1.0);
    fragPosition = position;
    // The texture's top row is the clouds' end.
    fragTexCoord = vec2(boltColumns.x + vertexPosition.x*boltColumns.y, 1.0 - vertexPosition.y);
}
