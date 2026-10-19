// A distance in [0, 1] packed into the four bytes of an RGBA8 texel and back, for #include "common/distance_packing".
// The point-light shadow cube is an 8-bit colour cube (rlgl cannot create empty float cube maps), so the cube-depth
// shaders store the light's linear distance this way and the lit shaders unpack it.

// Standard 32-bit-in-RGBA8 pack (unpackDistance reverses it).
vec4 packDistance(float v)
{
    v = clamp(v, 0.0, 1.0);
    vec4 enc = vec4(1.0, 255.0, 65025.0, 16581375.0) * v;
    enc = fract(enc);
    enc -= enc.yzww * vec4(1.0/255.0, 1.0/255.0, 1.0/255.0, 0.0);
    return enc;
}

// Unpacks a distance packed by packDistance (RGBA8 -> [0,1]).
float unpackDistance(vec4 rgba)
{
    return dot(rgba, vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0));
}
