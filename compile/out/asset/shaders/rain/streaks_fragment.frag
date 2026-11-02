#version 330

// The rain's streaks (streaks_vertex.vert): each pixel the streak texture's alpha along it, cut where the rain map
// covers it (no rain under roofs), lit as a raindrop is (common/lighting's RainDropLight), seen through the eye's night
// vision and the haze, blended premultiplied over what lies behind (writing no depth).

in vec3 fragPosition;
in vec2 fragTexCoord;
in float fragAlpha;

uniform sampler2D texture0; // the streak texture: its alpha along the streak, the top row at the drop
uniform vec3 rainColor;     // the rain's colour times its brightness, linear

out vec4 finalColor;

#include "common/lighting"

void main()
{
    float alpha = fragAlpha*texture(texture0, vec2(0.5, fragTexCoord.y)).a*RainOpennessAt(fragPosition);
    if (!(alpha > 0.002))
    {
        discard;
    }
    vec3 color = NightVisionOf(rainColor*RainDropLight(fragPosition));
    color = ApplyAerialPerspective(color, fragPosition);
    finalColor = vec4(color*alpha, alpha);
}
