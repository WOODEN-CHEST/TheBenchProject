// A CRT's picture, for #include "common/crt" (a fragment stage's: it takes screen derivatives): the curvature that maps a
// point of the glass to the point of the picture shown there, the power animation's squeeze, the scanlines, the mask,
// the vignette and the glow, and the CRT's uniforms (set per CRT draw by WorldRendererCrt.c). The C twin is
// source/CrtScreen.c (include/CrtScreen.h, its file comment explains the look); tests/ShaderSourceTests.c pins the
// constants and the curvature.

#include "common/math"

// The picture's shape: x the curvature (its corners come in by it, a share of its half width), y its aspect (width over
// height), z and w its width and height in pixels.
uniform vec4 crtShape;
// Its look: x the scanlines' strength (0: none), y the mask's strength, z the glow's share of the light, w the vignette.
uniform vec4 crtLook;
// Its power animation (CrtScreen_GetPowerShape): x and y the open part's half width and half height (shares of the
// picture's), z its brightness (the flicker in), w how white the squeezed beam is.
uniform vec4 crtPower;
// Its mask: CRT_MASK_* below; a tile's texture, its mean linear colour and whether it is sRGB.
uniform int crtMaskKind;
uniform sampler2D crtMaskTile;
uniform vec3 crtMaskMean;
uniform bool crtMaskIsSrgb;
// The pass: how many window pixels one of its pixels spans (1 at the window's resolution, the crisp overlay's).
uniform float crtWindowPixelScale;

// The least a scanline spans in the pass's pixels, and the last share (in log2 steps) of a level's range over which the
// coarser lines blend in (CRT_SCREEN_SCANLINE_MIN_PIXELS, CRT_SCREEN_SCANLINE_BLEND).
const float CRT_SCANLINE_MIN_PIXELS = 2.5;
const float CRT_SCANLINE_BLEND = 0.35;
// The scanlines and the mask fade out while the whole picture spans fewer window pixels than the end, to none at the start
// (CRT_SCREEN_DETAIL_FADE_START, _END).
const float CRT_DETAIL_FADE_START = 64.0;
const float CRT_DETAIL_FADE_END = 160.0;
// The procedural masks' period across, in window pixels (CRT_SCREEN_MASK_PERIOD); a mask fades in while that spans from
// this many of the pass's pixels to the period's own count (whole at one window pixel a pass pixel: the pixelated
// scene's are too coarse for it).
const float CRT_MASK_PERIOD = 3.0;
const float CRT_MASK_MIN_PIXELS = 2.0;
// The glow is read at the mipmap level where the picture spans about this many texels across.
const float CRT_GLOW_TEXELS = 48.0;
// The masks (CrtMaskKind).
const int CRT_MASK_APERTURE_GRILLE = 0;
const int CRT_MASK_SHADOW_MASK = 1;
const int CRT_MASK_TEXTURE = 3;

// A point of the glass (its texture coordinates, (0, 0) the top-left) to the point of the picture shown there
// (CrtScreen_Distort): x becomes x (1 + curvature y^2) and y becomes y (1 + curvature aspect x^2) about the centre.
vec2 CrtDistort(vec2 uv, float curvature, float aspect)
{
    vec2 c = uv*2.0 - 1.0;
    vec2 bent = vec2(c.x*(1.0 + curvature*c.y*c.y), c.y*(1.0 + curvature*aspect*c.x*c.x));
    return bent*0.5 + 0.5;
}

// How much of a pass pixel at a point of the picture lies on it (0 beyond it): its edge drawn over the pixel's footprint
// (footprint: the picture coordinates one pixel spans).
float CrtInside(vec2 picture, vec2 footprint)
{
    vec2 inside = clamp(min(picture, 1.0 - picture)/max(footprint, vec2(1e-6)) + 0.5, 0.0, 1.0);
    return inside.x*inside.y;
}

// The scanlines' light factor at a row of the picture (rows from its top), for how many rows one of the pass's pixels
// spans (CrtScreen_GetScanlineLevels: lines of the fewest powers of two rows that span CRT_SCANLINE_MIN_PIXELS, the next,
// coarser lines blending in over a level's last stretch): a cosine around 1 at each level, so the picture keeps its mean
// light.
float CrtScanlines(float row, float rowsPerPixel, float strength)
{
    float level = log2(max(rowsPerPixel*CRT_SCANLINE_MIN_PIXELS, 1e-6));
    float drawn = max(ceil(level), 0.0);
    float rowsPerLine = exp2(drawn);
    float coarse = smoothstep(drawn - CRT_SCANLINE_BLEND, drawn, level);
    float fine = cos(2.0*PI*(row/rowsPerLine - 0.5));
    float wide = cos(2.0*PI*(row/(2.0*rowsPerLine) - 0.5));
    return 1.0 + strength*mix(fine, wide, coarse);
}

// The mask's light factor at a window pixel: red, green and blue columns (the aperture grille), the same in staggered
// rows of bright and dim (the shadow mask), or the tile's colours divided by their mean (a texture), each keeping the
// picture's mean light; mixed with white by its strength.
vec3 CrtMask(vec2 windowPixel, int kind, float strength)
{
    vec3 mask = vec3(1.0);
    float column = mod(floor(windowPixel.x), CRT_MASK_PERIOD);
    vec3 phosphor = vec3(float(column < 0.5), float((column > 0.5) && (column < 1.5)), float(column > 1.5))*3.0;
    if (kind == CRT_MASK_APERTURE_GRILLE)
    {
        mask = phosphor;
    }
    else if (kind == CRT_MASK_SHADOW_MASK)
    {
        float group = mod(floor(windowPixel.x/CRT_MASK_PERIOD), 2.0);
        float row = mod(floor(windowPixel.y) + group, 2.0);
        mask = phosphor*((row < 0.5) ? 0.5 : 1.5);
    }
    else if (kind == CRT_MASK_TEXTURE)
    {
        ivec2 size = max(textureSize(crtMaskTile, 0), ivec2(1));
        vec3 tile = texelFetch(crtMaskTile, ivec2(mod(floor(windowPixel), vec2(size))), 0).rgb;
        tile = crtMaskIsSrgb ? pow(max(tile, vec3(0.0)), vec3(2.2)) : tile;
        mask = tile/max(crtMaskMean, vec3(1e-3));
    }
    return mix(vec3(1.0), mask, clamp(strength, 0.0, 1.0));
}

// The vignette's light factor at a point of the picture: 1 over most of it, darkening toward its edges and most into its
// corners, by the strength.
float CrtVignette(vec2 picture, float strength)
{
    vec2 p = clamp(picture, 0.0, 1.0);
    float centre = 16.0*p.x*p.y*(1.0 - p.x)*(1.0 - p.y);
    return 1.0 - strength*(1.0 - pow(centre, 0.25));
}

// The picture blurred into its glow (linear): five taps a texel apart at the mipmap level where it spans about
// CRT_GLOW_TEXELS across (without mipmaps the taps read the picture itself, a sharper glow).
vec3 CrtBlurred(sampler2D picture, vec2 uv)
{
    vec2 size = vec2(textureSize(picture, 0));
    float level = max(log2(max(size.x, size.y)/CRT_GLOW_TEXELS), 0.0);
    vec2 texel = exp2(level)/max(size, vec2(1.0));
    vec3 sum = textureLod(picture, uv, level).rgb*0.4
        + (textureLod(picture, uv + vec2(texel.x, texel.y), level).rgb
        + textureLod(picture, uv + vec2(-texel.x, texel.y), level).rgb
        + textureLod(picture, uv + vec2(texel.x, -texel.y), level).rgb
        + textureLod(picture, uv - texel, level).rgb)*0.15;
    return pow(max(sum, vec3(0.0)), vec3(2.2));
}

// The light a CRT's glass gives off at its point uv (its texture coordinates), per unit of its emission: its picture
// (sRGB, read with v flipped while isBottomUp) through the curvature, squeezed into the open part of its power animation
// and whitened there, with its scanlines and mask (faded out while the picture is small on screen or a pass's pixels
// are too coarse for them), its glow (the share crtLook.z of its light spread), its vignette and brightness; dark
// beyond the picture. Call it in uniform control flow: it takes screen derivatives.
vec3 CrtGlow(sampler2D picture, bool isBottomUp, vec2 uv)
{
    vec2 position = CrtDistort(uv, crtShape.x, crtShape.y);
    vec2 footprint = fwidth(position);
    vec2 centred = position*2.0 - 1.0;
    vec2 open = max(crtPower.xy, vec2(1e-4));
    vec2 squeezed = (centred/open)*0.5 + 0.5;
    vec2 sampled = isBottomUp ? vec2(squeezed.x, 1.0 - squeezed.y) : squeezed;
    vec3 sharp = pow(max(texture(picture, sampled).rgb, vec3(0.0)), vec3(2.2));
    vec3 blurred = CrtBlurred(picture, sampled);

    // The scanlines and the mask, by how large the picture is seen (in window pixels) and how fine the pass's pixels are.
    float detail = smoothstep(CRT_DETAIL_FADE_START, CRT_DETAIL_FADE_END, crtWindowPixelScale/max(footprint.y, 1e-6));
    float lines = CrtScanlines(position.y*crtShape.w, footprint.y*crtShape.w, crtLook.x*detail);
    float maskShare = detail*smoothstep(CRT_MASK_MIN_PIXELS, CRT_MASK_PERIOD,
        CRT_MASK_PERIOD/max(crtWindowPixelScale, 1e-3));
    vec3 mask = CrtMask(gl_FragCoord.xy*crtWindowPixelScale, crtMaskKind, crtLook.y*maskShare);

    float glow = clamp(crtLook.z, 0.0, 1.0);
    vec3 light = (sharp*lines*mask + blurred*glow)/(1.0 + glow);
    light = mix(light, vec3(1.0), clamp(crtPower.w, 0.0, 1.0));
    vec2 openInside = clamp((open - abs(centred))/max(2.0*footprint, vec2(1e-6)) + 0.5, 0.0, 1.0);
    return light*crtPower.z*CrtVignette(position, crtLook.w)*CrtInside(position, footprint)*openInside.x*openInside.y;
}
