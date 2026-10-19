#version 330

// The vertex stage of every world sprite shader (world_sprite, world_normal_sprite, world_sprite_depth,
// world_sprite_cube_depth). A sprite is one unit quad: x and y in [0, 1] (the image from left to right and bottom to
// top), z 0, normal +Z. The renderer draws it with the sprite's matrix for the pass (source/SpriteTransform.h): its
// orientation (facing the camera, or in a shadow pass the light), size, pivot and flips. The matrix's third column is
// the quad's unit normal, so no inverse transpose is needed.

in vec3 vertexPosition;
in vec2 vertexTexCoord;   // (x, 1 - y): v is 0 at the image's top row, as raylib textures are stored
in vec3 vertexNormal;     // (0, 0, 1)

// Supplied by raylib's DrawMesh.
uniform mat4 mvp;
uniform mat4 matModel;      // the sprite's matrix for this pass
uniform mat4 matView;
uniform mat4 matProjection;

uniform vec4 uvRect;        // the animation frame's area in its texture, normalized: x, y (top left), width, height

// Lit pass only (world_sprite): the sprite as the cascades' light and as the cube shadow's light see it. A billboard's
// shadow-map entry faces the light, so its shadow is read on that light-facing quad, at the point matching this one;
// the sprite then never shadows itself where its two quads cross.
uniform mat4 matShadowModel;
uniform mat4 matPointShadowModel;
// Lit pass only: metres a decal is pushed off its surface along its normal, toward the side the camera is on (0 for
// every other sprite).
uniform float decalOffset;
uniform vec3 viewPos;       // the camera's world position (shared with the fragment stage's lighting)

out vec3 fragPosition;            // world position (with a decal's push)
out vec2 fragTexCoord;
out vec3 fragNormal;              // world normal of the quad (not yet turned toward the viewer)
out vec3 fragShadowPosition;      // where the cascades are read
out vec3 fragShadowNormal;
out vec3 fragPointShadowPosition; // where the cube shadow is read
out vec3 fragPointShadowNormal;

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

    if (decalOffset > 0.0)
    {
        // A plane has the camera on one side for all of its points, so the four corners move together.
        float side = (dot(normal, viewPos - world) >= 0.0) ? 1.0 : -1.0;
        world += normal*(side*decalOffset);
        fragPosition = world;
        gl_Position = matProjection*(matView*vec4(world, 1.0));
    }
    else
    {
        fragPosition = world;
        gl_Position = mvp*local;
    }
}
