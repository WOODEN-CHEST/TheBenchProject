#version 330

// The atmosphere's transmittance table (T15): per texel, the share of light that crosses the air from a point to the top
// of the atmosphere, by the point's height and the ray's zenith angle (common/atmosphere's layout). Drawn as a full-screen
// pass into the table's target whenever the air changes (source/WorldRendererAtmosphere.c).

out vec4 finalColor;

#include "common/atmosphere"

// Steps along each ray: the table's rays run up to 2270 km (the horizon from the top of its rows), most of it in thin
// air; the steps grow with the square of their index.
const int TRANSMITTANCE_STEPS = 40;

void main()
{
    vec2 uv = gl_FragCoord.xy/vec2(ATMOSPHERE_TRANSMITTANCE_WIDTH, ATMOSPHERE_TRANSMITTANCE_HEIGHT);
    float height;
    float cosZenith;
    atmosphereTransmittanceRay(uv, height, cosZenith);
    finalColor = vec4(exp(-atmosphereOpticalDepth(height, cosZenith, TRANSMITTANCE_STEPS)), 1.0);
}
