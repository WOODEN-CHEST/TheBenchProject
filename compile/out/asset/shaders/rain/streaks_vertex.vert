#version 330

// The rain's streaks (the pure maths source/WorldRain.h, "THE STREAKS"): every streak one pixel wide, from a drop's place
// back along its motion over the eye's exposure. The mesh holds each streak's seed, its place in a lattice that repeats
// every box in the world; this stage puts the seed's copy in the box around the camera, moved by the rain's drift
// (RainStreakPosition, whose C mirror is WorldRain_GetStreakPosition), and widens the streak to one pixel on screen.

in vec3 vertexPosition;  // the streak's seed, each in [0, 1)
in vec2 vertexTexCoord;  // x: its side (0 or 1), y: along it (0 the drop, 1 the end of its trail)
in vec3 vertexNormal;    // x: its speed class (0, 1, 2), y: its rank (drawn while below the frame's share), z: its
                         // brightness

uniform mat4 rainViewProjection; // render space -> the scene's clip space
uniform vec2 rainResolution;     // the target's size in pixels
uniform vec3 rainAnchor;         // the streaks' anchor (WorldRain_GetStreakAnchor): the origin less the wind's drift
uniform float rainFall;          // the fall's drift, wrapped by WORLD_RAIN_FALL_WRAP_BOXES boxes
uniform vec3 rainWind;           // the wind now, metres per second (the drops drift with it)
uniform float rainFallSpeed;     // the middle class's fall, metres per second
uniform float rainExposure;      // the seconds a streak spans
uniform float rainStreakShare;   // the share of the streaks drawn

out vec3 fragPosition; // render space
out vec2 fragTexCoord;
out float fragAlpha;   // how much it shows: its brightness, the box's fade and its distance's

// The box around the camera and how far inside its faces the streaks fade (WORLD_RAIN_BOX_SIZE, WORLD_RAIN_BOX_FADE),
// the slowest class's quarters of the fall speed (WORLD_RAIN_SLOWEST_QUARTERS).
const float RAIN_BOX = 30.0;
const float RAIN_BOX_FADE = 3.0;
const float RAIN_SLOWEST_QUARTERS = 3.0;
// An end nearer the eye than this (clip w) is drawn from there: the streak is cut where it passes the eye.
const float RAIN_NEAR = 0.1;
// A streak's alpha up to RAIN_FULL_DISTANCE metres away; farther, its drop covers less of its pixel (1 / distance).
const float RAIN_ALPHA = 0.8;
const float RAIN_FULL_DISTANCE = 6.0;

// A seed's copy in the box around the camera, for a speed class's share of the fall.
vec3 RainStreakPosition(vec3 seed, float speedFactor)
{
    vec3 anchor = vec3(rainAnchor.x, rainAnchor.y + speedFactor*rainFall, rainAnchor.z);
    return mod(seed*RAIN_BOX - anchor + 0.5*RAIN_BOX, RAIN_BOX) - 0.5*RAIN_BOX;
}

// Nothing to draw: a vertex of no area outside the view.
void Skip()
{
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    fragPosition = vec3(0.0);
    fragTexCoord = vertexTexCoord;
    fragAlpha = 0.0;
}

void main()
{
    if (!(vertexNormal.y < rainStreakShare) || !(rainExposure > 0.0))
    {
        Skip();
        return;
    }
    float speedFactor = (RAIN_SLOWEST_QUARTERS + floor(vertexNormal.x + 0.5))/4.0;
    vec3 head = RainStreakPosition(vertexPosition, speedFactor);
    vec3 velocity = vec3(rainWind.x, -speedFactor*rainFallSpeed, rainWind.z);
    vec3 tail = head - velocity*rainExposure;

    // Both ends on screen, the one behind the eye moved along the streak to where it passes the near distance.
    vec4 clipHead = rainViewProjection*vec4(head, 1.0);
    vec4 clipTail = rainViewProjection*vec4(tail, 1.0);
    if ((clipHead.w < RAIN_NEAR) && (clipTail.w < RAIN_NEAR))
    {
        Skip();
        return;
    }
    float along = vertexTexCoord.y;
    if (clipHead.w < RAIN_NEAR)
    {
        float cut = (RAIN_NEAR - clipHead.w)/(clipTail.w - clipHead.w);
        clipHead = mix(clipHead, clipTail, cut);
        head = mix(head, tail, cut);
    }
    else if (clipTail.w < RAIN_NEAR)
    {
        float cut = (RAIN_NEAR - clipTail.w)/(clipHead.w - clipTail.w);
        clipTail = mix(clipTail, clipHead, cut);
        tail = mix(tail, head, cut);
    }

    // One pixel wide: each side half a pixel off the streak's line on screen (straight down when it covers no length).
    vec2 onScreen = (clipTail.xy/clipTail.w - clipHead.xy/clipHead.w)*rainResolution;
    vec2 direction = (dot(onScreen, onScreen) > 1e-8) ? normalize(onScreen) : vec2(0.0, 1.0);
    vec2 across = vec2(-direction.y, direction.x)/rainResolution;
    vec4 clip = mix(clipHead, clipTail, along);
    clip.xy += across*(vertexTexCoord.x*2.0 - 1.0)*clip.w;
    gl_Position = clip;

    vec3 position = mix(head, tail, along);
    float largest = max(max(abs(head.x), abs(head.y)), abs(head.z));
    float boxFade = 1.0 - smoothstep(0.5*RAIN_BOX - RAIN_BOX_FADE, 0.5*RAIN_BOX, largest);
    fragPosition = position;
    fragTexCoord = vertexTexCoord;
    fragAlpha = RAIN_ALPHA*vertexNormal.z*boxFade*min(1.0, RAIN_FULL_DISTANCE/max(length(position), 1e-3));
}
