#version 330

// Fragment stage for the point-light shadow pass: stores the LINEAR distance from the light to this fragment,
// normalized by the light's far range into [0,1] and PACKED into RGBA8 (the cube map is an 8-bit colour cube,
// as rlgl cannot create empty float cube maps). The PBR shader unpacks this and compares it to a fragment's
// own distance from the light to decide if it is shadowed. Cleared to white (packed 1.0 = "no occluder = far"). A
// fading object is dithered on the faces' texel grid, so its shadow thins out with it.

in vec3 fragWorldPos;

uniform vec3 lightPosition; // the shadow-casting point light's world position
uniform float lightFar;     // the light's reach; distances are normalized by this into [0,1]

out vec4 finalColor;

#include "common/distance_packing"
#include "common/dither"

void main()
{
    if (DitherDiscard())
    {
        discard;
    }
    float dist = length(fragWorldPos - lightPosition) / max(lightFar, 1e-4);
    finalColor = packDistance(dist);
}
