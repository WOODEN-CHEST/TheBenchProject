#version 330

// The reflection pass (world_reflections, source/WorldRendererReflections.c): for every pixel whose surface handed over
// a reflection (common/reflection_output: its weight, what it mirrors where the screen has no answer, the direction it
// mirrors along and the screen's share), the reflected ray marched over the scene's depth block by block
// (source/WorldReflections.h's MARCH, over the block pass's nearest surfaces: shaders/reflections/blocks.frag;
// WorldReflections_March is this march's C mirror: keep them in step), and the light the surface mirrors: the screen's
// answer (a surface the ray hit, or the sky's pixel at its vanishing point) mixed with the sky the surface handed over by
// the answer's confidence and the screen's share, times the weight. The renderer adds it to the scene's colour. A pixel
// of this pass stands for the scene pixel at its corner (resolutionDivisor scene pixels a side at low quality). An answer
// on a surface that mirrors too takes along the sky that surface handed over (a reflection in a reflection mirrors the
// sky alone). Its inputs sit on units of their own (WorldRendererTextureUnit_Reflection*), drawn as a rectangle.

uniform sampler2D reflectionWeightMap;    // reflectionWeight
uniform sampler2D reflectionSkyMap;       // reflectionSky
uniform sampler2D reflectionDirectionMap; // reflectionDirection
uniform sampler2D sceneColor;             // the scene's colour, linear HDR, as the opaque world left it
uniform sampler2D sceneDepth;             // the scene's depth (common/depth's convention)
uniform sampler2D reflectionBlocks;       // each block's nearest surface's depth (the block pass)
uniform mat4 viewProjection;              // render space -> clip (clip w: the distance in front of the camera)
uniform mat4 invViewProjection;           // clip -> render space
uniform vec3 cameraForward;               // unit, render space
uniform vec2 nearFar;                     // the projection's near plane, and its far plane (standard depth only)
uniform ivec2 sceneSize;                  // the scene's pixels
uniform ivec2 blockCount;                 // the block image's size
uniform int resolutionDivisor;            // scene pixels a side of this pass's pixel
uniform int maxBlocks;                    // the quality's most blocks a ray visits (at most REFLECTION_MAX_BLOCKS)
uniform vec3 sunDirection;                // unit, toward the sun
uniform float sunDiscCos;                 // the cosine of the sun's disc's radius and its margin
uniform vec3 moonDirection;               // unit, toward the moon
uniform float moonDiscCos;                // the same for the moon's disc (above 1 while it is not drawn)
uniform int reflectionView;               // the debug view: 0 off, 1 the weights, 2 the answers
uniform float viewScale;                  // the debug view's colours are drawn this bright (1 over the exposure)

out vec4 finalColor;

#include "common/depth"
#include "common/reflections"

// WorldReflections.h's constants (tests/ShaderSourceTests.c pins them).
const float REFLECTION_EDGE_FADE = 0.08;
const float REFLECTION_FACING_FADE_START = -0.35;
const float REFLECTION_FACING_FADE_END = -0.05;
const float REFLECTION_THICKNESS = 0.3;
const float REFLECTION_THICKNESS_SHARE = 0.03;
const float REFLECTION_RAY_LENGTH = 100000.0;
const float REFLECTION_NEAR_MARGIN = 1.5;
const float REFLECTION_START_OFFSET = 1.5;
const int REFLECTION_BLOCK_SIZE = 4;
const float REFLECTION_BLOCK_NUDGE = 0.01;
const int REFLECTION_BLOCK_WALK = 8;
const int REFLECTION_MAX_BLOCKS = 256;
// The distance the sky stands for (nothing drawn: no distance).
const float REFLECTION_SKY_DISTANCE = 1.0e30;

// What the march found (WorldReflectionsAnswer).
struct ReflectionAnswer
{
    bool isFound;
    bool isSky;
    ivec2 pixel;
    float confidence;
};

// The distance in front of the camera a stored depth stands for (WorldReflections_GetDistance).
float ReflectionDistance(float depth)
{
    if (depthReversed > 0.5)
    {
        return (depth > 0.0) ? (nearFar.x/depth) : REFLECTION_SKY_DISTANCE;
    }
    if (!(depth < 0.9999))
    {
        return REFLECTION_SKY_DISTANCE;
    }
    float ndc = depth*2.0 - 1.0;
    return 2.0*nearFar.y*nearFar.x/((nearFar.y + nearFar.x) - ndc*(nearFar.y - nearFar.x));
}

// The pixel a screen point lies in, kept on the screen.
ivec2 ReflectionPixel(vec2 point)
{
    return clamp(ivec2(floor(point)), ivec2(0), sceneSize - 1);
}

// The scene's distance at a screen point.
float ReflectionSceneDistance(vec2 point)
{
    return ReflectionDistance(texelFetch(sceneDepth, ReflectionPixel(point), 0).r);
}

// An answer's fades: the screen's edges (WorldReflections_GetEdgeFade) and the ray's facing
// (WorldReflections_GetFacingFade).
float ReflectionConfidence(ivec2 pixel, float facing)
{
    vec2 uv = (vec2(pixel) + 0.5)/vec2(sceneSize);
    float nearest = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float edge = clamp(nearest/REFLECTION_EDGE_FADE, 0.0, 1.0);
    return edge*smoothstep(REFLECTION_FACING_FADE_START, REFLECTION_FACING_FADE_END, facing);
}

// The largest share of a screen segment from p along d whose point stays on the screen, on one axis
// (WorldReflections.c's ClipToScreenAxis).
float ReflectionClipAxis(float p, float d, float size, float end)
{
    if ((d > 0.0) && ((p + d*end) > size))
    {
        return (size - p)/d;
    }
    if ((d < 0.0) && ((p + d*end) < 0.0))
    {
        return -p/d;
    }
    return end;
}

// The share where the ray from p0 along d (pixels) leaves the square cell of a side at the cell coordinates, no farther
// than end (WorldReflections.c's CellExit).
float ReflectionCellExit(vec2 p0, vec2 d, vec2 cell, float side, float end)
{
    float leave = end;
    if (d.x > 0.0)
    {
        leave = min(leave, ((cell.x + 1.0)*side - p0.x)/d.x);
    }
    else if (d.x < 0.0)
    {
        leave = min(leave, (cell.x*side - p0.x)/d.x);
    }
    if (d.y > 0.0)
    {
        leave = min(leave, ((cell.y + 1.0)*side - p0.y)/d.y);
    }
    else if (d.y < 0.0)
    {
        leave = min(leave, (cell.y*side - p0.y)/d.y);
    }
    return leave;
}

// The distance to a block's nearest surface (the sky's for a block off the image).
float ReflectionBlockDistance(vec2 cell)
{
    if (any(lessThan(cell, vec2(0.0))) || any(greaterThanEqual(cell, vec2(blockCount))))
    {
        return REFLECTION_SKY_DISTANCE;
    }
    return ReflectionDistance(texelFetch(reflectionBlocks, ivec2(cell), 0).r);
}

// Marches the reflected ray from origin along direction (render space) over the scene's depth, block by block
// (WorldReflections_March).
ReflectionAnswer ReflectionMarch(vec3 origin, vec3 direction)
{
    ReflectionAnswer none;
    none.isFound = false;
    none.isSky = false;
    none.pixel = ivec2(0);
    none.confidence = 0.0;

    float facing = dot(direction, cameraForward);
    vec4 start = viewProjection*vec4(origin, 1.0);
    if (!(start.w > 0.0))
    {
        return none;
    }
    float rayLength = REFLECTION_RAY_LENGTH;
    bool isToInfinity = true;
    if (facing < 0.0)
    {
        float toNear = (start.w - nearFar.x*REFLECTION_NEAR_MARGIN)/-facing;
        if (toNear < rayLength)
        {
            rayLength = toNear;
            isToInfinity = false;
        }
    }
    if (!(rayLength > 0.0))
    {
        return none;
    }
    vec4 end = viewProjection*vec4(origin + direction*rayLength, 1.0);

    // One over the distance in front of the camera is linear along the screen.
    float k0 = 1.0/start.w;
    float k1 = 1.0/end.w;
    vec2 size = vec2(sceneSize);
    vec2 p0 = (start.xy*k0*0.5 + 0.5)*size;
    vec2 p1 = (end.xy*k1*0.5 + 0.5)*size;
    vec2 d = p1 - p0;
    float lengthPixels = length(d);
    if (!(lengthPixels > REFLECTION_START_OFFSET))
    {
        return none;
    }
    float endShare = ReflectionClipAxis(p0.x, d.x, size.x, 1.0);
    endShare = ReflectionClipAxis(p0.y, d.y, size.y, endShare);
    bool isEndOnScreen = endShare >= 1.0;
    float pixelShare = 1.0/lengthPixels;
    float share = REFLECTION_START_OFFSET*pixelShare;
    float block = float(REFLECTION_BLOCK_SIZE);

    for (int visit = 0; visit < REFLECTION_MAX_BLOCKS; visit++)
    {
        if (!(share < endShare) || (visit >= maxBlocks))
        {
            break;
        }
        vec2 blockCell = floor((p0 + d*share)/block);
        float leaveBlock = ReflectionCellExit(p0, d, blockCell, block, endShare);
        float rayFar = max(1.0/(k0 + (k1 - k0)*share), 1.0/(k0 + (k1 - k0)*leaveBlock));
        if (rayFar > ReflectionBlockDistance(blockCell))
        {
            // Something in the block may be in front of the ray: every pixel its line crosses there, each a hit where
            // the ray's distances across it reach behind its surface and not past its thickness.
            float walk = share;
            for (int step = 0; step <= REFLECTION_BLOCK_WALK; step++)
            {
                if (!(walk < leaveBlock))
                {
                    break;
                }
                vec2 cell = floor(p0 + d*walk);
                float leave = ReflectionCellExit(p0, d, cell, 1.0, leaveBlock);
                float enter = 1.0/(k0 + (k1 - k0)*walk);
                float leaving = 1.0/(k0 + (k1 - k0)*leave);
                ivec2 pixel = ReflectionPixel(cell + 0.5);
                float surface = ReflectionDistance(texelFetch(sceneDepth, pixel, 0).r);
                if ((max(enter, leaving) > surface)
                    && (min(enter, leaving) < (surface + REFLECTION_THICKNESS + REFLECTION_THICKNESS_SHARE*surface)))
                {
                    ReflectionAnswer hit;
                    hit.isFound = true;
                    hit.isSky = false;
                    hit.pixel = pixel;
                    hit.confidence = ReflectionConfidence(pixel, facing);
                    return hit;
                }
                walk = max(leave, walk) + REFLECTION_BLOCK_NUDGE*pixelShare;
            }
        }
        share = max(leaveBlock, share) + REFLECTION_BLOCK_NUDGE*pixelShare;
    }

    // Behind nothing all the way to its vanishing point: the sky there answers.
    if (isToInfinity && isEndOnScreen && (share >= endShare))
    {
        ivec2 pixel = ReflectionPixel(p1);
        if (depthIsSky(texelFetch(sceneDepth, pixel, 0).r))
        {
            ReflectionAnswer sky;
            sky.isFound = true;
            sky.isSky = true;
            sky.pixel = pixel;
            sky.confidence = ReflectionConfidence(pixel, facing);
            return sky;
        }
    }
    return none;
}

void main()
{
    ivec2 outPixel = ivec2(gl_FragCoord.xy);
    ivec2 pixel = min(outPixel*resolutionDivisor, sceneSize - 1);
    vec3 weight = texelFetch(reflectionWeightMap, pixel, 0).rgb;
    if (!any(greaterThan(weight, vec3(0.0))))
    {
        finalColor = vec4(0.0);
        return;
    }
    vec3 sky = texelFetch(reflectionSkyMap, pixel, 0).rgb;
    vec4 directionData = texelFetch(reflectionDirectionMap, pixel, 0);
    float screenShare = directionData.z;

    ReflectionAnswer answer;
    answer.isFound = false;
    answer.isSky = false;
    answer.pixel = ivec2(0);
    answer.confidence = 0.0;
    if (screenShare > 0.0)
    {
        // The surface's point from its depth, through the inverse view-projection.
        float depth = texelFetch(sceneDepth, pixel, 0).r;
        vec2 ndc = (vec2(pixel) + 0.5)/vec2(sceneSize)*2.0 - 1.0;
        vec4 point = invViewProjection*vec4(ndc, depthToNdc(depth), 1.0);
        vec3 direction = ReflectionDecodeDirection(directionData.xy);
        answer = ReflectionMarch(point.xyz/point.w, direction);
        // The discs are the lit shaders' glints already: the sky's answer leaves them out.
        if (answer.isSky && ((dot(direction, sunDirection) > sunDiscCos) || (dot(direction, moonDirection) > moonDiscCos)))
        {
            answer.isFound = false;
        }
    }

    vec3 mirrored = sky;
    float share = answer.isFound ? (answer.confidence*screenShare) : 0.0;
    if (share > 0.0)
    {
        // What the answer's pixel shows; where it mirrors too, with the sky it handed over (its own screen answer is this
        // pass's, not in the scene's colour: a mirror seen in a mirror shows the sky in it, not a dull surface).
        vec3 answered = texelFetch(sceneColor, answer.pixel, 0).rgb
            + texelFetch(reflectionWeightMap, answer.pixel, 0).rgb*texelFetch(reflectionSkyMap, answer.pixel, 0).rgb;
        mirrored = mix(sky, answered, share);
    }
    vec3 result = weight*mirrored;

    // The debug views: the weights in white; the answers in colour (green a surface, blue the sky, red none), each by its
    // share of the reflection.
    if (reflectionView == 1)
    {
        result = vec3(clamp(max(weight.r, max(weight.g, weight.b)), 0.0, 1.0))*viewScale;
    }
    else if (reflectionView == 2)
    {
        vec3 answerColor = answer.isFound ? (answer.isSky ? vec3(0.1, 0.3, 1.0) : vec3(0.1, 1.0, 0.2)) : vec3(1.0, 0.1, 0.1);
        result = mix(vec3(1.0, 0.1, 0.1), answerColor, answer.isFound ? share : 1.0)*viewScale;
    }
    finalColor = vec4(result, 0.0);
}
