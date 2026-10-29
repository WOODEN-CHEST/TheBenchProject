#version 330

// The ambient pre-pass, run before the scene's colour pass from the low-resolution normal/flag G-buffer's samplable
// depth and surface flag. Per pixel it writes what the lit passes take off their light: in R the screen-space ambient
// occlusion, a multiplier of the ambient term (1 fully open, < 1 occluded), never of the sun, the moon or lamps; in B
// the contact light, what the contact shadows leave of the light the shadow cascades were rendered from (1 lit, 0 in a
// contact shadow); in G the sky's visibility (common/interior: what keeps the sky out around the surface, its march turned
// by the pixel's pattern), 1 while the occupancy volume is not drawn. The pass after it (ao_blur) blurs R and G into the
// texture the lit passes read and passes B through.
//
// THE OCCLUSION is horizon-based (ground-truth AO, Jimenez et al. 2016): in AO_SLICES slices through the view vector a
// pixel, the highest horizon on either side of the pixel within AO_RADIUS metres, clamped to the surface's hemisphere,
// gives the slice's cosine-weighted visibility in closed form. Occluders are weighed down to nothing toward the radius, so a thing
// far in front of a surface does not darken it (no halos behind silhouettes), and samples on the surface's own plane
// rise exactly to its tangent, so a face seen edge-on does not darken itself. The normal comes from the depth itself,
// along each axis from the neighbour whose depth steps less, so it never mixes two surfaces at a silhouette and the
// darkest place is the contact. The slices turn, and their steps shift, with a fixed 4 x 4 pattern on the pixel grid;
// the blur after averages exactly one period of it at every pixel, so the pattern leaves no noise and nothing crawls.
//
// THE CONTACT SHADOWS: from each pixel, pixel by pixel along the line toward the shadow light on screen and over
// contactLength metres, whether what the camera sees lies in front of that line (within CONTACT_THICKNESS of it). They
// are exact at the scene's pixels where the cascades' texels and filter leave a seam: at the foot of every caster, where
// a wall meets a roof. Only what the camera sees casts them, so they add to the cascades and never replace them.
//
// All sampling uses gl_FragCoord, as the other post passes do.

in vec2 fragTexCoord;   // unused (see the gl_FragCoord note); kept so the blit has an output
in vec4 fragColor;

uniform sampler2D texture0;      // the G-buffer colour, bound by the blit; unused here but needed for the draw
uniform vec4 colDiffuse;
uniform sampler2D depthTexture;  // the G-buffer's samplable depth (same geometry + projection as the scene pass)
uniform sampler2D normalTexture; // the normal/flag G-buffer (A = surface flag: 0 = sky/grid, >= 0.5 = surface)

uniform vec2 resolution;         // G-buffer size in pixels
uniform mat4 invProjection;      // inverse of the perspective projection used by the scene pass
uniform mat4 projection;         // the perspective projection used by the scene pass (its scale gives pixels a metre)
uniform float aoStrength;        // effective AO strength (config x world); 0 disables (R stays 1 = open)
uniform vec3 contactLightDirection; // unit, view space: toward the light the shadow cascades were rendered from
uniform float contactLength;     // how far the contact shadows' march reaches, metres; 0 disables (B stays 1 = lit)
uniform mat4 viewToRender;       // view space -> render space (the view's rotation undone), for the sky's visibility
#include "common/depth"
#include "common/interior"

out vec4 finalColor;

const float SURFACE_ON = 0.25;   // G-buffer alpha above this = a solid world-model surface (0 = sky / grid)
const float AO_PI = 3.14159265;
const float AO_HALF_PI = 1.57079633;

// ---- The occlusion's tuning ----
const int   AO_SLICES = 1;        // slices per pixel (the pattern turns it through 16 orientations)
const int   AO_STEPS = 6;         // samples on each side of a slice
const float AO_RADIUS = 0.5;      // how far occluders count, metres
const float AO_FALLOFF = 0.6;     // the outer share of the radius over which an occluder's weight falls to 0
const float AO_MAX_PIXELS = 64.0; // the radius on screen at most (up close the radius shrinks to it)
const float AO_MIN_PIXELS = 1.0;  // below this many pixels on screen no occlusion is taken (far away) ...
const float AO_FULL_PIXELS = 3.0; // ... and from this many all of it, faded between
const float AO_MAX = 0.6;         // clamp so AO never fully blackens the ambient term

// ---- The contact shadows' tuning ----
const float CONTACT_MAX_PIXELS = 32.0; // the march's steps at most, one a pixel
const float CONTACT_THICKNESS = 0.25;  // how thick a thing in front of the line is taken to be, metres
const float CONTACT_LIFT = 0.002;      // the march starts this far off the surface, per metre of distance
const float CONTACT_BIAS = 0.003;      // how far in front of the line a surface must lie, per metre of distance
const float CONTACT_NEAR = 0.05;       // the line is cut where it would come nearer the camera than this, metres

// The 4 x 4 ordered pattern on the pixel grid (common/dither's Bayer matrix, the first row at the bottom): the slices'
// turn at a pixel, and, read a quarter period over, their steps' shift.
const float AO_PATTERN[16] = float[16](
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0);

float PatternAt(ivec2 pixel)
{
    ivec2 cell = pixel & 3;
    return (AO_PATTERN[cell.y*4 + cell.x] + 0.5)/16.0;
}

bool IsSurfaceAt(ivec2 pixel)
{
    return texelFetch(normalTexture, pixel, 0).a > SURFACE_ON;
}

// The view-space position of the G-buffer pixel from its stored depth (at the pixel's centre).
vec3 ViewPosAt(ivec2 pixel)
{
    float d = texelFetch(depthTexture, pixel, 0).r;
    vec2 uv = (vec2(pixel) + 0.5)/resolution;
    vec4 ndc = vec4(uv*2.0 - 1.0, depthToNdc(d), 1.0);
    vec4 view = invProjection*ndc;
    return view.xyz/view.w;
}

// The surface's normal from its depth: along each axis the step to the neighbour (a surface, inside the target) whose
// depth differs less, so a silhouette's two sides never mix; turned toward the camera.
vec3 NormalAt(ivec2 pixel, vec3 center)
{
    ivec2 last = ivec2(resolution) - 1;
    vec3 steps[2];
    for (int axis = 0; axis < 2; axis++)
    {
        ivec2 offset = (axis == 0) ? ivec2(1, 0) : ivec2(0, 1);
        ivec2 before = pixel - offset;
        ivec2 after = pixel + offset;
        bool hasBefore = all(greaterThanEqual(before, ivec2(0))) && IsSurfaceAt(before);
        bool hasAfter = all(lessThanEqual(after, last)) && IsSurfaceAt(after);
        vec3 toAfter = hasAfter ? (ViewPosAt(after) - center) : vec3(0.0);
        vec3 fromBefore = hasBefore ? (center - ViewPosAt(before)) : vec3(0.0);
        bool useAfter = hasAfter && (!hasBefore || (abs(toAfter.z) < abs(fromBefore.z)));
        steps[axis] = useAfter ? toAfter : fromBefore;
    }
    vec3 n = cross(steps[0], steps[1]);
    float lengthSquared = dot(n, n);
    n = (lengthSquared > 0.0) ? n*inversesqrt(lengthSquared) : normalize(-center);
    return (dot(n, -center) < 0.0) ? -n : n;
}

// The cosine-weighted share of the surface's hemisphere left open by what lies within AO_RADIUS of it on screen, in
// [0, 1] (1 with nothing around). rotation and shift (in [0, 1)) are the pattern's at the pixel.
float Visibility(ivec2 pixel, vec3 position, vec3 normal, float rotation, float shift)
{
    float pixelsPerMetre = 0.5*resolution.y*projection[1][1]/max(-position.z, 1e-4);
    float radiusPixels = min(AO_RADIUS*pixelsPerMetre, AO_MAX_PIXELS);
    if (radiusPixels < AO_MIN_PIXELS)
    {
        return 1.0;
    }
    float radius = radiusPixels/pixelsPerMetre;
    float falloffRange = AO_FALLOFF*radius;
    float falloffMul = -1.0/falloffRange;
    float falloffAdd = radius/falloffRange;
    vec3 view = normalize(-position);
    vec2 center = vec2(pixel) + 0.5;
    ivec2 last = ivec2(resolution) - 1;

    float visibility = 0.0;
    float open = 0.0;
    for (int slice = 0; slice < AO_SLICES; slice++)
    {
        float phi = (float(slice) + rotation)*(AO_PI/float(AO_SLICES));
        vec2 omega = vec2(cos(phi), sin(phi));
        vec3 direction = vec3(omega, 0.0);
        vec3 orthoDirection = direction - dot(direction, view)*view;
        vec3 axis = normalize(cross(orthoDirection, view));
        vec3 projectedNormal = normal - axis*dot(normal, axis);
        float projectedLength = length(projectedNormal);
        float cosNormal = clamp(dot(projectedNormal, view)/max(projectedLength, 1e-6), 0.0, 1.0);
        float n = sign(dot(orthoDirection, projectedNormal))*acos(cosNormal);
        // Each side's highest occluder, as its elevation above the surface's tangent plane (0: nothing above it). Read
        // against the plane in 3D, a sample on the surface's own plane is exactly at 0 however its pixel rounds off the
        // slice, so flat ground keeps all of its sky at every slant.
        float elevation0 = 0.0;
        float elevation1 = 0.0;
        for (int sampleStep = 0; sampleStep < AO_STEPS; sampleStep++)
        {
            // Denser near the pixel; at least a pixel further each step.
            float s = (float(sampleStep) + shift)/float(AO_STEPS);
            vec2 offset = omega*max(s*s*radiusPixels, float(sampleStep) + 1.0);
            for (int side = 0; side < 2; side++)
            {
                ivec2 samplePixel = ivec2(floor(center + ((side == 0) ? offset : -offset)));
                if (any(lessThan(samplePixel, ivec2(0))) || any(greaterThan(samplePixel, last))
                    || !IsSurfaceAt(samplePixel))
                {
                    continue;
                }
                vec3 delta = ViewPosAt(samplePixel) - position;
                float sampleDistance = length(delta);
                if (!(sampleDistance > 1e-6))
                {
                    continue;
                }
                float weight = clamp(sampleDistance*falloffMul + falloffAdd, 0.0, 1.0);
                float elevation = asin(clamp(dot(delta, normal)/sampleDistance, 0.0, 1.0))*weight;
                if (side == 0)
                {
                    elevation0 = max(elevation0, elevation);
                }
                else
                {
                    elevation1 = max(elevation1, elevation);
                }
            }
        }
        // The horizons' angles from the view vector in the slice, the + side's (h1) and the - side's (h0), lowered from
        // the tangent plane toward the normal by their occluders, never past the view vector (the pixel is seen).
        float h1 = max(n + AO_HALF_PI - elevation0, 0.0);
        float h0 = min(n - AO_HALF_PI + elevation1, 0.0);
        float arc0 = (cosNormal + 2.0*h0*sin(n) - cos(2.0*h0 - n))*0.25;
        float arc1 = (cosNormal + 2.0*h1*sin(n) - cos(2.0*h1 - n))*0.25;
        visibility += projectedLength*(arc0 + arc1);
        // The same slice with both horizons on the tangent plane: what nothing around would leave open.
        open += projectedLength*(cosNormal + n*sin(n));
    }
    // A share of what the same slices would see with nothing around, so open ground is exactly 1 whatever the slices'
    // turn (two slices alone estimate the hemisphere's integral only on average).
    visibility = (open > 1e-6) ? clamp(visibility/open, 0.0, 1.0) : 1.0;
    // Faded in over the first pixels of radius on screen, so the far edge of where AO is taken shows no line.
    return mix(1.0, visibility, smoothstep(AO_MIN_PIXELS, AO_FULL_PIXELS, radiusPixels));
}

// Where a view-space point lands on the target, in pixels.
vec2 PixelOf(vec3 position)
{
    vec4 clip = projection*vec4(position, 1.0);
    return (clip.xy/clip.w*0.5 + 0.5)*resolution;
}

// What the contact shadows leave of the shadow light at a surface (1 lit, 0 in one): along the line toward the light,
// one pixel a step, whether a surface the camera sees lies in front of it (nearer the camera than the line, by more than
// a bias and less than CONTACT_THICKNESS). A surface turned away from the light is left to its own shadow.
float ContactLight(ivec2 pixel, vec3 position, vec3 normal)
{
    if (!(contactLength > 0.0) || (dot(normal, contactLightDirection) <= 0.0))
    {
        return 1.0;
    }
    float distanceToCamera = -position.z;
    vec3 start = position + normal*(CONTACT_LIFT*distanceToCamera);
    float reach = contactLength;
    // Cut the line where it would pass the camera's near side (a light behind the camera, the line coming toward it).
    if (contactLightDirection.z > 0.0)
    {
        reach = min(reach, max(-CONTACT_NEAR - start.z, 0.0)/contactLightDirection.z);
    }
    vec3 end = start + contactLightDirection*reach;
    vec2 startPixel = PixelOf(start);
    vec2 endPixel = PixelOf(end);
    int steps = int(min(ceil(length(endPixel - startPixel)), CONTACT_MAX_PIXELS));
    ivec2 last = ivec2(resolution) - 1;
    float inverseStartZ = 1.0/start.z;
    float inverseEndZ = 1.0/end.z;
    for (int i = 1; i <= steps; i++)
    {
        float t = float(i)/float(steps);
        ivec2 samplePixel = ivec2(floor(mix(startPixel, endPixel, t)));
        if (any(lessThan(samplePixel, ivec2(0))) || any(greaterThan(samplePixel, last)))
        {
            break; // off the screen: nothing known there
        }
        if ((samplePixel == pixel) || !IsSurfaceAt(samplePixel))
        {
            continue;
        }
        // The line's view depth there, perspective-correct (1/z is linear on screen).
        float lineZ = 1.0/mix(inverseStartZ, inverseEndZ, t);
        float inFront = ViewPosAt(samplePixel).z - lineZ;
        if ((inFront > CONTACT_BIAS*(-lineZ)) && (inFront < CONTACT_THICKNESS))
        {
            return 0.0;
        }
    }
    return 1.0;
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float aoMul = 1.0;        // sky / grid / no AO => fully open (the ambient is unchanged there)
    float contactLight = 1.0; // sky / no contact shadows => lit
    float skyVisibility = 1.0; // sky / no occupancy volume => the whole sky
    if (IsSurfaceAt(pixel))
    {
        vec3 position = ViewPosAt(pixel);
        vec3 normal = NormalAt(pixel, position);
        if (skyVisibilityActive)
        {
            // Read in render space, placed by the surface's geometric normal (the G-buffer's), its directions turned by
            // the pattern (the blur averages them).
            vec3 surfaceNormal = normalize(texelFetch(normalTexture, pixel, 0).rgb*2.0 - 1.0);
            skyVisibility = InteriorSkyVisibilityTurned((viewToRender*vec4(position, 1.0)).xyz,
                normalize(mat3(viewToRender)*surfaceNormal), PatternAt(pixel + ivec2(1, 2)),
                INTERIOR_SKY_PATTERN_AZIMUTHS);
        }
        if (aoStrength > 0.0)
        {
            float rotation = PatternAt(pixel);
            float shift = PatternAt(pixel + ivec2(2, 1));
            float occlusion = 1.0 - Visibility(pixel, position, normal, rotation, shift);
            aoMul = 1.0 - clamp(occlusion*aoStrength, 0.0, AO_MAX);
        }
        contactLight = ContactLight(pixel, position, normal);
    }
    finalColor = vec4(aoMul, skyVisibility, contactLight, 1.0);
}
