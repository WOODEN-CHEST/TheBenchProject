#version 330

// A lightning bolt's channel (bolt_vertex.vert): emitted light, its texture's colour times its alpha times the bolt's
// radiance, dimmed by the air in front of it (the haze's transmittance alone: its own light is in what lies behind),
// added over the scene (writing no depth).

in vec3 fragPosition;
in vec2 fragTexCoord;

uniform sampler2D texture0; // the bolt texture: its bolts side by side, white on a clear background
uniform vec3 boltRadiance;  // the bolt's colour times its radiance on screen (the eye's exposure divided out), linear

out vec4 finalColor;

#include "common/lighting"

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    if (!(texel.a > 0.002))
    {
        discard;
    }
    vec3 color = boltRadiance*pow(max(texel.rgb, vec3(0.0)), vec3(2.2))*texel.a*AerialTransmittance(fragPosition);
    finalColor = vec4(color, 0.0);
}
