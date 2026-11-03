// The screen-space reflections as pure maths (WorldReflections.h): the march's settings by quality, the screen share by
// roughness, the direction's packing, the answers' fades, the distance a stored depth stands for and the march itself;
// the C mirror of shaders/common/reflections.glsl and shaders/reflections/.

#include <math.h>
#include "WorldReflections.h"
#include "Config.h"


// Static functions.
/* GLSL's smoothstep: 0 below edge0, 1 above edge1, a cubic between (edge0 < edge1). */
static float SmoothStep(float edge0, float edge1, float x)
{
    float T = fminf(fmaxf((x - edge0) / (edge1 - edge0), 0.0f), 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

/* 1 for a value at or above 0, -1 below (the octahedral map's sign: zero counts as positive). */
static float SignNotZero(float value)
{
    return (value >= 0.0f) ? 1.0f : -1.0f;
}

/* A render-space point through a matrix in raymath's layout (column-major: m0 m1 m2 m3 the first column), as the
 * shader's mat4 * vec4(point, 1.0). */
static void TransformToClip(const Matrix* matrix, Vector3 point, float outClip[4])
{
    outClip[0] = matrix->m0 * point.x + matrix->m4 * point.y + matrix->m8 * point.z + matrix->m12;
    outClip[1] = matrix->m1 * point.x + matrix->m5 * point.y + matrix->m9 * point.z + matrix->m13;
    outClip[2] = matrix->m2 * point.x + matrix->m6 * point.y + matrix->m10 * point.z + matrix->m14;
    outClip[3] = matrix->m3 * point.x + matrix->m7 * point.y + matrix->m11 * point.z + matrix->m15;
}

/* The largest share f in [0, 1] of a screen segment from p (inside [0, size]) along d whose point stays inside, on one
 * axis. */
static float ClipToScreenAxis(float p, float d, float size, float end)
{
    if ((d > 0.0f) && ((p + d * end) > size))
    {
        return (size - p) / d;
    }
    if ((d < 0.0f) && ((p + d * end) < 0.0f))
    {
        return -p / d;
    }
    return end;
}

/* The share where a ray along the screen from p (both axes) with direction d leaves the square cell of a side at the
 * given cell coordinates, no farther than @p end. */
static float CellExit(float p0x, float p0y, float dx, float dy, float cellX, float cellY, float side, float end)
{
    float Exit = end;
    if (dx > 0.0f)
    {
        Exit = fminf(Exit, ((cellX + 1.0f) * side - p0x) / dx);
    }
    else if (dx < 0.0f)
    {
        Exit = fminf(Exit, (cellX * side - p0x) / dx);
    }
    if (dy > 0.0f)
    {
        Exit = fminf(Exit, ((cellY + 1.0f) * side - p0y) / dy);
    }
    else if (dy < 0.0f)
    {
        Exit = fminf(Exit, (cellY * side - p0y) / dy);
    }
    return Exit;
}

/* The pixel a screen point lies in, kept on the screen. */
static int32_t PixelOf(float coordinate, int32_t size)
{
    float Floor = floorf(coordinate);
    if (!(Floor >= 0.0f))
    {
        return 0;
    }
    return (Floor >= (float)(size - 1)) ? (size - 1) : (int32_t)Floor;
}

/* How much an answer at a pixel counts: its distance to the screen's edges and the ray's facing. */
static float AnswerConfidence(const WorldReflectionsView* view, int32_t x, int32_t y, float facing)
{
    float U = ((float)x + 0.5f) / (float)view->Width;
    float V = ((float)y + 0.5f) / (float)view->Height;
    return WorldReflections_GetEdgeFade(U, V) * WorldReflections_GetFacingFade(facing);
}


// Public functions.
WorldReflectionsMarchSettings WorldReflections_GetMarchSettings(int32_t quality)
{
    if (quality == CONFIG_REFLECTION_QUALITY_LOW)
    {
        return (WorldReflectionsMarchSettings)
        {
            .MaxBlocks = WORLD_REFLECTIONS_LOW_BLOCKS, .ResolutionDivisor = WORLD_REFLECTIONS_LOW_DIVISOR
        };
    }
    return (WorldReflectionsMarchSettings)
    {
        .MaxBlocks = WORLD_REFLECTIONS_HIGH_BLOCKS, .ResolutionDivisor = WORLD_REFLECTIONS_HIGH_DIVISOR
    };
}

float WorldReflections_GetScreenShare(float roughness)
{
    if (isnan(roughness))
    {
        return 0.0f;
    }
    return 1.0f - SmoothStep(WORLD_REFLECTIONS_SHARP_ROUGHNESS, WORLD_REFLECTIONS_ROUGH_ROUGHNESS, roughness);
}

Vector2 WorldReflections_EncodeDirection(Vector3 direction)
{
    float Sum = fabsf(direction.x) + fabsf(direction.y) + fabsf(direction.z);
    if (!(Sum > 0.0f))
    {
        return (Vector2){ 0.0f, 0.0f };
    }
    float X = direction.x / Sum;
    float Y = direction.y / Sum;
    float Z = direction.z / Sum;
    if (Z >= 0.0f)
    {
        return (Vector2){ X, Y };
    }
    return (Vector2){ (1.0f - fabsf(Y)) * SignNotZero(X), (1.0f - fabsf(X)) * SignNotZero(Y) };
}

Vector3 WorldReflections_DecodeDirection(Vector2 encoded)
{
    Vector3 Direction = { encoded.x, encoded.y, 1.0f - fabsf(encoded.x) - fabsf(encoded.y) };
    if (Direction.z < 0.0f)
    {
        Direction.x = (1.0f - fabsf(encoded.y)) * SignNotZero(encoded.x);
        Direction.y = (1.0f - fabsf(encoded.x)) * SignNotZero(encoded.y);
    }
    float Length = sqrtf(Direction.x * Direction.x + Direction.y * Direction.y + Direction.z * Direction.z);
    return (Vector3){ Direction.x / Length, Direction.y / Length, Direction.z / Length };
}

float WorldReflections_GetEdgeFade(float u, float v)
{
    float Nearest = fminf(fminf(u, 1.0f - u), fminf(v, 1.0f - v));
    return fminf(fmaxf(Nearest / WORLD_REFLECTIONS_EDGE_FADE, 0.0f), 1.0f);
}

float WorldReflections_GetFacingFade(float cosine)
{
    return SmoothStep(WORLD_REFLECTIONS_FACING_FADE_START, WORLD_REFLECTIONS_FACING_FADE_END, cosine);
}

float WorldReflections_GetThickness(float distance)
{
    return WORLD_REFLECTIONS_THICKNESS + WORLD_REFLECTIONS_THICKNESS_SHARE * distance;
}

float WorldReflections_GetDistance(const WorldReflectionsView* view, float depth)
{
    if (view->IsDepthReversed)
    {
        return (depth > 0.0f) ? (view->NearPlane / depth) : INFINITY;
    }
    if (!(depth < 0.9999f))
    {
        return INFINITY;
    }
    float Near = view->NearPlane;
    float Far = view->FarPlane;
    float Ndc = depth * 2.0f - 1.0f;
    return 2.0f * Far * Near / ((Far + Near) - Ndc * (Far - Near));
}

float WorldReflections_GetBlockNearest(const WorldReflectionsView* view, int32_t blockX, int32_t blockY,
    WorldReflectionsDepthReader read, const void* context)
{
    float Nearest = view->IsDepthReversed ? 0.0f : 1.0f;
    for (int32_t Row = 0; Row < WORLD_REFLECTIONS_BLOCK_SIZE; Row++)
    {
        int32_t Y = blockY * WORLD_REFLECTIONS_BLOCK_SIZE + Row;
        for (int32_t Column = 0; Column < WORLD_REFLECTIONS_BLOCK_SIZE; Column++)
        {
            int32_t X = blockX * WORLD_REFLECTIONS_BLOCK_SIZE + Column;
            if ((X < 0) || (Y < 0) || (X >= view->Width) || (Y >= view->Height))
            {
                continue;
            }
            float Depth = read(context, X, Y);
            Nearest = view->IsDepthReversed ? fmaxf(Nearest, Depth) : fminf(Nearest, Depth);
        }
    }
    return Nearest;
}

WorldReflectionsAnswer WorldReflections_March(const WorldReflectionsView* view,
    const WorldReflectionsMarchSettings* settings, Vector3 origin, Vector3 direction, WorldReflectionsDepthReader read,
    const void* context)
{
    WorldReflectionsAnswer None = { 0 };
    float Facing = direction.x * view->Forward.x + direction.y * view->Forward.y + direction.z * view->Forward.z;
    float Start[4];
    TransformToClip(&view->ViewProjection, origin, Start);
    if (!(Start[3] > 0.0f))
    {
        return None;
    }

    // The ray's end: as good as infinity away from the camera, just short of the near plane toward it.
    float Length = WORLD_REFLECTIONS_RAY_LENGTH;
    bool IsToInfinity = true;
    if (Facing < 0.0f)
    {
        float ToNear = (Start[3] - view->NearPlane * WORLD_REFLECTIONS_NEAR_MARGIN) / -Facing;
        if (ToNear < Length)
        {
            Length = ToNear;
            IsToInfinity = false;
        }
    }
    if (!(Length > 0.0f))
    {
        return None;
    }
    Vector3 EndPoint =
    {
        origin.x + direction.x * Length, origin.y + direction.y * Length, origin.z + direction.z * Length
    };
    float End[4];
    TransformToClip(&view->ViewProjection, EndPoint, End);

    // One over the distance in front of the camera is linear along the screen.
    float K0 = 1.0f / Start[3];
    float K1 = 1.0f / End[3];
    float Width = (float)view->Width;
    float Height = (float)view->Height;
    float P0x = (Start[0] * K0 * 0.5f + 0.5f) * Width;
    float P0y = (Start[1] * K0 * 0.5f + 0.5f) * Height;
    float P1x = (End[0] * K1 * 0.5f + 0.5f) * Width;
    float P1y = (End[1] * K1 * 0.5f + 0.5f) * Height;
    float Dx = P1x - P0x;
    float Dy = P1y - P0y;
    float LengthPixels = sqrtf(Dx * Dx + Dy * Dy);
    if (!(LengthPixels > WORLD_REFLECTIONS_START_OFFSET))
    {
        return None; // along the view: nothing on the screen to walk
    }
    float EndShare = ClipToScreenAxis(P0x, Dx, Width, 1.0f);
    EndShare = ClipToScreenAxis(P0y, Dy, Height, EndShare);
    bool IsEndOnScreen = EndShare >= 1.0f;
    float PixelShare = 1.0f / LengthPixels;
    float Share = WORLD_REFLECTIONS_START_OFFSET * PixelShare;
    float Block = (float)WORLD_REFLECTIONS_BLOCK_SIZE;

    int32_t Visits = 0;
    while ((Share < EndShare) && (Visits < settings->MaxBlocks))
    {
        Visits++;
        float Px = P0x + Dx * Share;
        float Py = P0y + Dy * Share;
        float BlockX = floorf(Px / Block);
        float BlockY = floorf(Py / Block);
        float Exit = CellExit(P0x, P0y, Dx, Dy, BlockX, BlockY, Block, EndShare);
        float RayFar = fmaxf(1.0f / (K0 + (K1 - K0) * Share), 1.0f / (K0 + (K1 - K0) * Exit));
        float Nearest = WorldReflections_GetDistance(view, WorldReflections_GetBlockNearest(view, (int32_t)BlockX,
            (int32_t)BlockY, read, context));
        if (RayFar > Nearest)
        {
            // Something in the block may be in front of the ray: every pixel its line crosses there (at most
            // WORLD_REFLECTIONS_BLOCK_WALK more after the first), each a hit where the ray's distances across it reach
            // behind its surface and not past its thickness.
            float Walk = Share;
            for (int32_t Step = 0; (Step <= WORLD_REFLECTIONS_BLOCK_WALK) && (Walk < Exit); Step++)
            {
                float CellX = floorf(P0x + Dx * Walk);
                float CellY = floorf(P0y + Dy * Walk);
                float Leave = CellExit(P0x, P0y, Dx, Dy, CellX, CellY, 1.0f, Exit);
                float Enter = 1.0f / (K0 + (K1 - K0) * Walk);
                float Leaving = 1.0f / (K0 + (K1 - K0) * Leave);
                int32_t X = PixelOf(CellX + 0.5f, view->Width);
                int32_t Y = PixelOf(CellY + 0.5f, view->Height);
                float SceneDistance = WorldReflections_GetDistance(view, read(context, X, Y));
                if ((fmaxf(Enter, Leaving) > SceneDistance)
                    && (fminf(Enter, Leaving) < (SceneDistance + WorldReflections_GetThickness(SceneDistance))))
                {
                    return (WorldReflectionsAnswer)
                    {
                        .IsFound = true, .IsSky = false, .X = X, .Y = Y,
                        .Confidence = AnswerConfidence(view, X, Y, Facing)
                    };
                }
                Walk = fmaxf(Leave, Walk) + WORLD_REFLECTIONS_BLOCK_NUDGE * PixelShare;
            }
        }
        Share = fmaxf(Exit, Share) + WORLD_REFLECTIONS_BLOCK_NUDGE * PixelShare;
    }

    // Behind nothing all the way to its vanishing point: the sky there answers.
    if (IsToInfinity && IsEndOnScreen && (Share >= EndShare))
    {
        int32_t X = PixelOf(P1x, view->Width);
        int32_t Y = PixelOf(P1y, view->Height);
        if (isinf(WorldReflections_GetDistance(view, read(context, X, Y))))
        {
            return (WorldReflectionsAnswer)
            {
                .IsFound = true, .IsSky = true, .X = X, .Y = Y, .Confidence = AnswerConfidence(view, X, Y, Facing)
            };
        }
    }
    return None;
}
