#version 330

// Fragment stage for the point-light shadow pass: stores the LINEAR distance from the light to this fragment,
// normalized by the light's far range into [0,1] and PACKED into RGBA8 (the cube map is an 8-bit colour cube,
// as rlgl cannot create empty float cube maps). The PBR shader unpacks this and compares it to a fragment's
// own distance from the light to decide if it is shadowed. Cleared to white (packed 1.0 = "no occluder = far").

in vec3 fragWorldPos;

uniform vec3 lightPosition; // the shadow-casting point light's world position
uniform float lightFar;     // the light's reach; distances are normalized by this into [0,1]

out vec4 finalColor;

// Standard 32-bit-in-RGBA8 pack (matches unpackDistance in the PBR shader).
vec4 packDistance(float v)
{
    v = clamp(v, 0.0, 1.0);
    vec4 enc = vec4(1.0, 255.0, 65025.0, 16581375.0) * v;
    enc = fract(enc);
    enc -= enc.yzww * vec4(1.0/255.0, 1.0/255.0, 1.0/255.0, 0.0);
    return enc;
}

void main()
{
    float dist = length(fragWorldPos - lightPosition) / max(lightFar, 1e-4);
    finalColor = packDistance(dist);
}
