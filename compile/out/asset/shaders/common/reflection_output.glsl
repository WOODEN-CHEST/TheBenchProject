// What a lit surface in the scene pass's opaque part hands the reflection pass, for #include "common/reflection_output"
// (after "common/lighting"): its three outputs and the functions that write them. The scene target's three reflection
// attachments take them while the pass runs this frame (reflectionsDeferred); in every other draw, and every other target,
// only the colour is drawn and they are left out. Each output's alpha is 1, so the scene pass's alpha blending writes them
// as they are.
//
//   reflectionWeight     rgb: the weight, the share of the mirrored light that reaches the eye (the reflectance times the
//                        share that mirrors, times the haze's transmittance in front of the surface); 0: no reflection
//   reflectionSky        rgb: what the surface mirrors where the screen has no answer (the sky and the ground's bounce,
//                        linear, through the eye's night vision)
//   reflectionDirection  xy: the direction it mirrors along (render space, packed: ReflectionEncodeDirection), z: the
//                        share of its reflection the screen may answer (ReflectionScreenShare)
//
// The pure maths and the constants' C twins are source/WorldReflections.h.

layout(location = 1) out vec4 reflectionWeight;
layout(location = 2) out vec4 reflectionSky;
layout(location = 3) out vec4 reflectionDirection;

// Hands a surface's reflection (common/lighting's SurfaceReflection) at the render-space point worldPos to the pass; a
// surface that does not mirror, or a draw while the pass does not run, hands over none.
void WriteReflection(SurfaceReflection reflection, vec3 worldPos)
{
    if (!reflectionsDeferred || !any(greaterThan(reflection.weight, vec3(0.0))))
    {
        reflectionWeight = vec4(0.0, 0.0, 0.0, 1.0);
        reflectionSky = vec4(0.0, 0.0, 0.0, 1.0);
        reflectionDirection = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    reflectionWeight = vec4(reflection.weight*AerialTransmittance(worldPos), 1.0);
    reflectionSky = vec4(NightVisionOf(reflection.sky), 1.0);
    reflectionDirection = vec4(ReflectionEncodeDirection(reflection.direction), clamp(reflection.screenShare, 0.0, 1.0),
        1.0);
}

// Hands over no reflection (a surface that never mirrors: a cutout sprite).
void WriteNoReflection()
{
    reflectionWeight = vec4(0.0, 0.0, 0.0, 1.0);
    reflectionSky = vec4(0.0, 0.0, 0.0, 1.0);
    reflectionDirection = vec4(0.0, 0.0, 0.0, 1.0);
}
