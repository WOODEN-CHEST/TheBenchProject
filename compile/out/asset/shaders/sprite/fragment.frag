#version 330

// World sprites, lit like the models around them (common/lighting: the sun, the moon, point lights, shadows, the
// ambient, the haze, the eye's night vision) or unlit (their colours shown as emitted light, still hazed). The sprite's art
// is sRGB and is linearized here; the scene buffer is linear HDR, tonemapped at the end.
//
// The surface is a rough dielectric (roughness 0.9, metallic 0, occlusion 1). A fixed sprite or decal is a two-sided
// card: lit with its quad's normal turned toward the viewer. A billboard or axis billboard stands in for a round thing
// seen from the front: its normal faces the camera and its light wraps around it (lightWrap), so a sprite lit from the
// side is half lit instead of black.
//
// Alpha modes (WorldSpriteAlphaMode): cutout discards texels below alphaCutoff and writes the rest opaque (alpha 1, depth
// written); blend writes premultiplied colour and alpha, blended (ONE, ONE_MINUS_SRC_ALPHA) over what is behind; additive
// writes premultiplied colour with alpha 0, added (ONE, ONE) without touching the scene's alpha.
//
// Fades (common/dither): a cutout sprite dithers by its fade (its draw distance, the object's opacity), like a model. A
// blended or additive sprite is transparent already: the renderer folds its draw-distance fade into its tint's alpha and
// leaves its dither fade at 0.

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec3 fragShadowPosition;
in vec3 fragShadowNormal;
in vec3 fragPointShadowPosition;
in vec3 fragPointShadowNormal;

uniform sampler2D texture0;   // the animation frame's texture
uniform vec4 colDiffuse;      // the object's tint (sRGB); its alpha, the object's opacity, fades blended sprites
uniform int alphaMode;        // 0 cutout, 1 blend, 2 additive
uniform float alphaCutoff;    // cutout threshold on the texture's alpha
uniform bool isLit;           // false: the colours are emitted light
uniform float emission;       // an unlit sprite's brightness multiplier
uniform float lightWrap;      // how far light wraps around the sprite: 0 for cards (fixed sprites, decals)
uniform bool isCard;          // a card (a fixed sprite or decal), not a billboard
uniform bool useScreenAo;     // whether the screen-space AO at this pixel is this sprite's (a cutout sprite, a decal)

out vec4 finalColor;

#include "common/lighting"
#include "common/dither"

const int ALPHA_MODE_CUTOUT = 0;
const int ALPHA_MODE_BLEND = 1;
const int ALPHA_MODE_ADDITIVE = 2;
// The sprite surface: a rough dielectric.
const float SPRITE_ROUGHNESS = 0.9;
const float SPRITE_METALLIC = 0.0;
const vec3 SPRITE_F0 = vec3(0.04);

// The normal turned toward a point: a card is lit and shadowed on the side that faces it.
vec3 FacingNormal(vec3 n, vec3 toward)
{
    return (dot(n, toward) < 0.0) ? -n : n;
}

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    if (((alphaMode == ALPHA_MODE_CUTOUT) && (texel.a < alphaCutoff)) || DitherDiscard())
    {
        discard;
    }
    vec3 albedo = pow(max(texel.rgb*colDiffuse.rgb, vec3(0.0)), vec3(2.2)); // sRGB -> linear
    float alpha = (alphaMode == ALPHA_MODE_CUTOUT) ? 1.0 : texel.a*colDiffuse.a;

    vec3 color;
    // Where interior lighting reads the sprite's sky and zones from: a card by its own facing; a billboard, whose quad
    // turns with the camera, as a thing standing there (facing up), so its light does not change as the camera moves.
    vec3 placeNormal = vec3(0.0, 1.0, 0.0);
    float skyVisibility = 1.0;
    if (isLit)
    {
        vec3 v = normalize(-fragPosition); // render space: the camera at the origin
        vec3 n = FacingNormal(normalize(fragNormal), v);
        placeNormal = isCard ? n : placeNormal;

        // The cascades are read on the sprite as their light sees it, off that quad's face toward the light; the cube
        // shadow likewise on the quad facing its light.
        vec3 toShadowLight = normalize(shadowLightDirection);
        vec3 shadowFace = FacingNormal(normalize(fragShadowNormal), toShadowLight);
        float shadowAmount = SkyShadowAmount(fragShadowPosition, shadowFace, dot(shadowFace, toShadowLight));
        vec3 pointShadowNormal = FacingNormal(fragPointShadowNormal, pointShadowLightPos - fragPointShadowPosition);

        SkyLightValues light = SkyLightAt(fragPosition);
        vec3 direct = SkyLightWrapped(light, n, v, shadowAmount, lightWrap, albedo, SPRITE_F0, SPRITE_ROUGHNESS,
            SPRITE_METALLIC);
        vec3 pointTotal = PointLightsWrapped(n, v, fragPosition, fragPointShadowPosition, pointShadowNormal, lightWrap,
            albedo, SPRITE_F0, SPRITE_ROUGHNESS, SPRITE_METALLIC);
        // A card the pre-pass saw (a cutout card, a decal on its surface) takes its sky visibility; a billboard stands for
        // a thing seen from all round (the pre-pass saw its quad facing the camera) and a blended sprite is not in it:
        // they read what lies straight above them.
        skyVisibility = (useScreenAo && isCard && aoMapActive && skyVisibilityOnScreen && drawInPrepass)
            ? ScreenSkyVisibility()
            : InteriorSkyOverhead(fragPosition, placeNormal);
        vec3 ambient = AmbientLight(light, n, fragPosition, placeNormal, skyVisibility)*albedo
            *(useScreenAo ? ScreenAo() : 1.0);

        // Reflected light goes through the eye's night vision, as the models' does.
        color = NightVisionOf(ambient + direct + pointTotal);
    }
    else
    {
        // Emitted light keeps its colour (no night vision), like the models' emissive part.
        color = albedo*max(emission, 0.0);
    }

    if (alphaMode == ALPHA_MODE_ADDITIVE)
    {
        // An emitter seen through the haze: the air in front dims it, and the haze's own glow is already in what lies
        // behind, so it is dimmed, not mixed toward the sky.
        color *= AerialTransmittance(fragPosition);
        finalColor = vec4(color*alpha, 0.0);
        return;
    }

    color = ApplyAerialPerspective(color, fragPosition);
    if (isLit)
    {
        color = InteriorViewColor(color, fragPosition, placeNormal, skyVisibility);
    }
    finalColor = (alphaMode == ALPHA_MODE_BLEND) ? vec4(color*alpha, alpha) : vec4(color, 1.0);
}
