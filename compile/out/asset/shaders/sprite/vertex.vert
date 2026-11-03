#version 330

// The vertex stage of every world sprite shader (world_sprite, world_normal_sprite, world_sprite_depth,
// world_sprite_cube_depth). A sprite is one unit quad: x and y in [0, 1] (the image from left to right and bottom to
// top), z 0, normal +Z. The renderer draws it with the sprite's matrix for the pass (source/SpriteTransform.h): its
// orientation (facing the camera, or in a shadow pass the light), size, pivot and flips. The matrix's third column is
// the quad's unit normal, so no inverse transpose is needed. In the camera's passes the picture curves with the planet
// (common/curvature; fragPosition stays the flat world's); the shadow passes' programs never set its scale, so their
// maps hold the flat world.

#include "common/curvature"

in vec3 vertexPosition;
in vec2 vertexTexCoord;   // (x, 1 - y): v is 0 at the image's top row, as raylib textures are stored
in vec3 vertexNormal;     // (0, 0, 1)

// Supplied by raylib's DrawMesh.
uniform mat4 mvp;
uniform mat4 matModel;      // the sprite's matrix for this pass
uniform mat4 matView;
uniform mat4 matProjection;

uniform vec4 uvRect;        // the animation frame's area in its texture, normalized: x, y (top left), width, height

// Lit pass only (world_sprite): the sprite as the shadow levels' light, the cube shadow's light and each shadowed spot
// see it. A billboard's shadow-map entry faces the light, so its shadow is read on that light-facing quad, at the point
// matching this one; the sprite then never shadows itself where its two quads cross. MAX_SPOT_SHADOWS is common/lighting's
// (the lit fragment stage reads this many).
#define MAX_SPOT_SHADOWS 2
uniform mat4 matShadowModel;
uniform mat4 matPointShadowModel;
uniform mat4 matSpotShadowModels[MAX_SPOT_SHADOWS];
// Lit pass only: metres a decal is pushed off its surface along its normal, toward the side the camera is on (0 for
// every other sprite). The camera sits at the origin of render space, the space every position here is in.
uniform float decalOffset;

out vec3 fragPosition;            // render-space position (with a decal's push)
out vec2 fragTexCoord;
out vec3 fragNormal;              // world normal of the quad (not yet turned toward the viewer)
out vec3 fragShadowPosition;      // where the shadow levels are read
out vec3 fragShadowNormal;
out vec3 fragPointShadowPosition; // where the cube shadow is read
out vec3 fragPointShadowNormal;
out vec3 fragSpotShadowPositions[MAX_SPOT_SHADOWS]; // where each shadowed spot's map is read
out vec3 fragSpotShadowNormals[MAX_SPOT_SHADOWS];

void main()
{
    vec4 local = vec4(vertexPosition, 1.0);
    vec3 world = (matModel*local).xyz;
    vec3 normal = normalize(mat3(matModel)*vertexNormal);

    fragTexCoord = uvRect.xy + vertexTexCoord*uvRect.zw;
    fragNormal = normal;
    fragShadowPosition = (matShadowModel*local).xyz;
    fragShadowNormal = normalize(mat3(matShadowModel)*vertexNormal);
    fragPointShadowPosition = (matPointShadowModel*local).xyz;
    fragPointShadowNormal = normalize(mat3(matPointShadowModel)*vertexNormal);
    for (int i = 0; i < MAX_SPOT_SHADOWS; i++)
    {
        fragSpotShadowPositions[i] = (matSpotShadowModels[i]*local).xyz;
        fragSpotShadowNormals[i] = normalize(mat3(matSpotShadowModels[i])*vertexNormal);
    }

    if (decalOffset > 0.0)
    {
        // A plane has the camera on one side for all of its points, so the four corners move together.
        float side = (dot(normal, -world) >= 0.0) ? 1.0 : -1.0;
        world += normal*(side*decalOffset);
        fragPosition = world;
        gl_Position = CurvedClipPosition(matProjection*(matView*vec4(world, 1.0)), world);
    }
    else
    {
        fragPosition = world;
        gl_Position = CurvedClipPosition(mvp*local, world);
    }
}
