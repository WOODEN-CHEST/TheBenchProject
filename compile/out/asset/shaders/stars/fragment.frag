#version 330

// The star's radiance, added onto the sky (WorldRendererStars.c draws with (ONE, ONE) blending), hidden by the drawn
// clouds in front of it (their share of the sky behind them that shows, at the star's pixel). Alpha 0 leaves the
// scene's alpha, which the tonemap passes on to the frame target, untouched.

flat in vec3 fragRadiance;

uniform vec2 resolution;           // the scene target's size in pixels
uniform bool cloudsActive;         // the clouds are drawn: their target holds them
uniform sampler2D cloudLayer;      // the cloud target: A the share of the sky behind them that shows (-1 under geometry)
uniform vec2 cloudLayerResolution; // its size in texels

out vec4 finalColor;

// The share of the sky behind the drawn clouds that shows at this pixel (1 without them): the cloud target's texels around
// it weighed bilinearly, the march's skipped ones left out (the sky pass's cloudsHere, its alpha).
float cloudsTransmittanceHere()
{
    if (!cloudsActive)
    {
        return 1.0;
    }
    vec2 position = gl_FragCoord.xy/resolution*cloudLayerResolution - 0.5;
    vec2 base = floor(position);
    vec2 share = position - base;
    ivec2 last = ivec2(cloudLayerResolution) - 1;
    float sum = 0.0;
    float weightSum = 0.0;
    for (int corner = 0; corner < 4; corner++)
    {
        ivec2 offset = ivec2(corner & 1, corner >> 1);
        float value = texelFetch(cloudLayer, clamp(ivec2(base) + offset, ivec2(0), last), 0).a;
        float weight = ((offset.x == 1) ? share.x : 1.0 - share.x)*((offset.y == 1) ? share.y : 1.0 - share.y);
        if ((value >= 0.0) && (weight > 0.0))
        {
            sum += value*weight;
            weightSum += weight;
        }
    }
    return (weightSum > 0.0) ? sum/weightSum : 1.0;
}

void main()
{
    finalColor = vec4(fragRadiance*cloudsTransmittanceHere(), 0.0);
}
