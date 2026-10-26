// The eye's dim-light vision, for #include "common/night_vision": in dim light the rods take over from the cones, and
// colour fades into a grey with a faint blue cast. The sky pass applies it to the sky's glow (the air's in-scatter, the
// skyglow, the Milky Way), the lit shaders to reflected light and the haze's in-scatter; emitters (the sun's and the
// moon's discs, stars, emissive materials) are left out: the game draws them far dimmer than they are, and real emitters
// that bright are seen in colour. The model and its calibration are explained in source/NightVision.h; NightVision_GetRodShare is
// the CPU twin of nightVisionRodShare, which the tests check.

// Above (level / halfLuminance)^steepness of this the rods' share is below 1/1024 and the colour is left exactly as it
// is (NIGHT_VISION_FULL_COLOUR_RATIO in source/NightVision.h): by day nothing changes.
const float NIGHT_VISION_FULL_COLOUR_RATIO = 1023.0;

// The rods' share of vision for a pixel's luminance, given the field luminance (the scene luminance the sky's light
// gives), the calibration on the game's light scale (halfLuminance, steepness) and the strength (0 none .. 1 the eye):
// 0 by day, near 1 on a moonless night.
float nightVisionRodShare(float luminance, float fieldLuminance, float halfLuminance, float steepness, float strength)
{
    float level = max(max(luminance, fieldLuminance), 0.0);
    float ratio = pow(level/halfLuminance, steepness);
    if ((strength <= 0.0) || (ratio >= NIGHT_VISION_FULL_COLOUR_RATIO))
    {
        return 0.0;
    }
    return clamp(strength, 0.0, 1.0)/(1.0 + ratio);
}

// What the eye sees of a dim, linear colour: blended toward its luminance in the rods' grey (rodTint, of luminance 1:
// NightVision_GetRodTint) by the rods' share. Returns the colour itself (not a recomputed copy) whenever the share is 0,
// so bright scenes stay bit for bit the same.
vec3 nightVision(vec3 color, float fieldLuminance, float halfLuminance, float steepness, float strength, vec3 rodTint)
{
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float rods = nightVisionRodShare(luminance, fieldLuminance, halfLuminance, steepness, strength);
    if (rods <= 0.0)
    {
        return color;
    }
    return mix(color, rodTint*luminance, rods);
}
