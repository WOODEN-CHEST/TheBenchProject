#version 330

// The rain's splashes (the pure maths source/WorldRain.h, "THE SPLASHES"): a grid of cells around the camera, fixed in
// the world; each cell splashes now and then at a random place in it, where a drop falling through there lands: the
// first surface the rain map finds on the way down its line along the map's own fall (RainLanding, a bisection whose C
// mirror is WorldRain_FindLanding). A splash is a small quad facing the camera, standing on that surface, playing its
// flipbook once.

in vec3 vertexPosition; // x, z: the splash's cell in the grid (0 .. WORLD_RAIN_SPLASH_GRID - 1); y unused
in vec2 vertexTexCoord; // the quad's corner, (0, 0) its bottom left .. (1, 1)

uniform mat4 rainViewProjection; // render space -> the scene's clip space
uniform vec2 rainSplashCell;     // the grid's first cell's hash coordinates (WorldRain_GetSplashGrid)
uniform vec2 rainSplashCorner;   // that cell's corner, render space x and z
uniform float rainSplashCycles;  // how many cycles the splashes have run (WorldRainDrift.SplashCycles, wrapped by
                                 // RAIN_CYCLE_WRAP)
uniform float rainSplashShare;   // the share of the cells that splash in a cycle
uniform float rainSplashPeriod;  // a cell's cycle now, seconds
uniform vec2 rainSlant;          // the rain map's fall: its step per metre up (WorldRain_GetSlant)
uniform vec3 rainCameraRight;    // the camera's right and up, world space (the quads face it)
uniform vec3 rainCameraUp;

out vec3 fragPosition; // render space
out vec2 fragTexCoord;
out float fragAlpha;

#include "common/rain"

// The cells' width, where their hash wraps, how far above and below the camera a landing is sought and in how many
// halvings (WORLD_RAIN_SPLASH_CELL, WORLD_RAIN_SPLASH_HASH_PERIOD, WORLD_RAIN_SPLASH_REACH, WORLD_RAIN_SPLASH_STEPS); a
// splash's life and its flipbook's frames (WORLD_RAIN_SPLASH_LIFE, WORLD_RAIN_SPLASH_FRAMES).
const float RAIN_SPLASH_CELL = 1.0;
const float RAIN_SPLASH_HASH_PERIOD = 4096.0;
const float RAIN_SPLASH_REACH = 12.0;
const int RAIN_SPLASH_STEPS = 12;
const float RAIN_SPLASH_LIFE = 0.2;
const float RAIN_SPLASH_FRAMES = 4.0;
// A splash's size (metres, its random part on top) and how far below the surface its quad starts (it stands on it).
const float RAIN_SPLASH_SIZE = 0.22;
const float RAIN_SPLASH_SIZE_SPREAD = 0.12;
const float RAIN_SPLASH_SINK = 0.02;

// Nothing to draw: a vertex of no area outside the view.
void Skip()
{
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    fragPosition = vec3(0.0);
    fragTexCoord = vertexTexCoord;
    fragAlpha = 0.0;
}

// The splashes' line through render-space (x, 0, z) along the rain map's fall, at a height.
vec3 RainLinePoint(float x, float z, float height)
{
    return vec3(x + rainSlant.x*height, height, z + rainSlant.y*height);
}

// Where a drop falling down the line through render-space (x, 0, z) lands: between RAIN_SPLASH_REACH above and below the
// camera, the first point the rain map covers (open while its openness is at least a half): along the map's own fall
// that is the first surface; or false when the top is covered or the bottom open (nothing there to land on, or under
// cover all the way).
bool RainLanding(float x, float z, out vec3 landing)
{
    float high = RAIN_SPLASH_REACH;
    float low = -RAIN_SPLASH_REACH;
    landing = vec3(0.0);
    if ((RainOpennessAt(RainLinePoint(x, z, high)) < 0.5) || (RainOpennessAt(RainLinePoint(x, z, low)) >= 0.5))
    {
        return false;
    }
    for (int step = 0; step < RAIN_SPLASH_STEPS; step++)
    {
        float middle = 0.5*(high + low);
        if (RainOpennessAt(RainLinePoint(x, z, middle)) >= 0.5)
        {
            high = middle;
        }
        else
        {
            low = middle;
        }
    }
    landing = RainLinePoint(x, z, 0.5*(high + low));
    return true;
}

void main()
{
    // The cell's own phase, then this cycle's: whether it splashes, where in the cell, how large.
    uvec2 whole = uvec2(mod(rainSplashCell + vertexPosition.xz, RAIN_SPLASH_HASH_PERIOD));
    float phase = rainHashPair(rainHash(uvec3(whole, 3u))).x;
    float cycle = rainSplashCycles + phase;
    float age = fract(cycle)*rainSplashPeriod;
    uint draw = rainHash(uvec3(whole, uint(mod(floor(cycle), RAIN_CYCLE_WRAP)) + 4u));
    vec2 chance = rainHashPair(draw);
    if (!(age < RAIN_SPLASH_LIFE) || !(chance.x < rainSplashShare))
    {
        Skip();
        return;
    }
    vec2 place = rainHashPair(rainHash(uvec3(whole, draw)));
    float x = rainSplashCorner.x + (vertexPosition.x + place.x)*RAIN_SPLASH_CELL;
    float z = rainSplashCorner.y + (vertexPosition.z + place.y)*RAIN_SPLASH_CELL;
    vec3 landing;
    if (!RainLanding(x, z, landing))
    {
        Skip();
        return;
    }

    float size = RAIN_SPLASH_SIZE + RAIN_SPLASH_SIZE_SPREAD*chance.y;
    vec3 position = landing - vec3(0.0, RAIN_SPLASH_SINK, 0.0) + rainCameraRight*((vertexTexCoord.x - 0.5)*size)
        + rainCameraUp*(vertexTexCoord.y*size);
    float frame = min(floor(age/RAIN_SPLASH_LIFE*RAIN_SPLASH_FRAMES), RAIN_SPLASH_FRAMES - 1.0);
    gl_Position = rainViewProjection*vec4(position, 1.0);
    fragPosition = position;
    fragTexCoord = vec2((frame + vertexTexCoord.x)/RAIN_SPLASH_FRAMES, 1.0 - vertexTexCoord.y);
    fragAlpha = 1.0;
}
