#version 330

// Cutout sprites in the point-light shadow pass: like world_cube_depth, stores the linear distance from the light,
// normalized by its reach and packed into RGBA8, but discards texels below the sprite's alpha cutoff, so the shadow has
// the sprite's shape. The renderer turns billboards toward the light.

in vec3 fragPosition;
in vec2 fragTexCoord;

uniform sampler2D texture0;   // the animation frame's texture
uniform float alphaCutoff;
uniform vec3 lightPosition;   // the shadow-casting point light's world position
uniform float lightFar;       // the light's reach; distances are normalized by this into [0,1]

out vec4 finalColor;

#include "common/distance_packing"

void main()
{
    if (texture(texture0, fragTexCoord).a < alphaCutoff)
    {
        discard;
    }
    float dist = length(fragPosition - lightPosition) / max(lightFar, 1e-4);
    finalColor = packDistance(dist);
}
