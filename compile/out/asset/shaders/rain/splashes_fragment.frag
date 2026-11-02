#version 330

// The rain's splashes (splashes_vertex.vert): the flipbook's frame, lit as raindrops are (common/lighting's
// RainDropLight), seen through the eye's night vision and the haze, blended premultiplied over what lies behind
// (writing no depth).

in vec3 fragPosition;
in vec2 fragTexCoord;
in float fragAlpha;

uniform sampler2D texture0; // the splash's flipbook: its frames side by side
uniform vec3 rainColor;     // the rain's colour times its brightness, linear

out vec4 finalColor;

#include "common/lighting"

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    float alpha = texel.a*fragAlpha;
    if (!(alpha > 0.002))
    {
        discard;
    }
    vec3 albedo = pow(max(texel.rgb, vec3(0.0)), vec3(2.2)); // sRGB -> linear
    vec3 color = NightVisionOf(rainColor*albedo*RainDropLight(fragPosition));
    color = ApplyAerialPerspective(color, fragPosition);
    finalColor = vec4(color*alpha, alpha);
}
