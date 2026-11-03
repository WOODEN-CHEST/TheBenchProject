#include "WorldLocalShadows.h"
#include <math.h>
#include "raylib/raymath.h"


// Macros.
/** A spot whose axis lies this close to straight up or down (the cosine) takes +Z as its map's up instead of world up. */
#define UP_SWITCH_COSINE 0.99f
/** A quarter turn: below it a spot's bounding sphere passes through its apex, above it around its rim. */
#define QUARTER_PI 0.78539816f
/** The narrowest cone a spot's map is made for, radians: a sliver keeps a field of view the projection can hold. */
#define MIN_SPOT_ANGLE 0.001f


// Static functions.
/* Whether a light can cast a shadow this frame: on, casting shadows, and lighting anything. */
static bool CanCastShadow(const WorldRenderLight* light)
{
    return light->IsOn && light->CastsShadows && (light->Range > 0.0f) && (light->Intensity > 0.0f);
}

/* Whether @p objectId is among the spots shadowed last frame. */
static bool WasShadowed(uint64_t objectId, const uint64_t* previousSpotIds)
{
    if ((previousSpotIds == NULL) || (objectId == 0U))
    {
        return false;
    }
    for (uint32_t Index = 0U; Index < WORLD_LOCAL_SHADOWS_MAX_SPOTS; Index++)
    {
        if (previousSpotIds[Index] == objectId)
        {
            return true;
        }
    }
    return false;
}

/* Puts spot @p index of strength @p score among the choice's spots, strongest first (an equal one stays behind those
 * before it), dropping the weakest past @p maxSpots. */
static void RankSpot(WorldLocalShadowChoice* choice, float* scores, uint32_t maxSpots, size_t index, float score)
{
    uint32_t At = choice->SpotCount;
    while ((At > 0U) && (scores[At - 1U] < score))
    {
        At--;
    }
    if (At >= maxSpots)
    {
        return;
    }
    uint32_t Last = (choice->SpotCount < maxSpots) ? choice->SpotCount : (maxSpots - 1U);
    for (uint32_t Slot = Last; Slot > At; Slot--)
    {
        choice->Spots[Slot] = choice->Spots[Slot - 1U];
        scores[Slot] = scores[Slot - 1U];
    }
    choice->Spots[At] = index;
    scores[At] = score;
    choice->SpotCount = (choice->SpotCount < maxSpots) ? (choice->SpotCount + 1U) : maxSpots;
}


// Public functions.
WorldLocalShadowChoice WorldLocalShadows_Choose(const WorldRenderLight* lights, size_t lightCount,
    const WorldFrustum* cameraFrustum, const uint64_t* previousSpotIds, uint32_t maxSpots, bool isPointWanted)
{
    WorldLocalShadowChoice Choice = { .PointLight = SIZE_MAX, .SpotCount = 0U };
    float Scores[WORLD_LOCAL_SHADOWS_MAX_SPOTS] = { 0.0f };
    if (maxSpots > WORLD_LOCAL_SHADOWS_MAX_SPOTS)
    {
        maxSpots = WORLD_LOCAL_SHADOWS_MAX_SPOTS;
    }
    float NearestPoint = 0.0f;
    for (size_t Index = 0; Index < lightCount; Index++)
    {
        const WorldRenderLight* Light = &lights[Index];
        if (!CanCastShadow(Light))
        {
            continue;
        }
        if (Light->Type != WorldLightType_Spot)
        {
            // The camera sits at the origin of render space.
            float Distance = Vector3Length(Light->RenderPosition);
            if (isPointWanted && ((Choice.PointLight == SIZE_MAX) || (Distance < NearestPoint)))
            {
                Choice.PointLight = Index;
                NearestPoint = Distance;
            }
            continue;
        }
        if (maxSpots == 0U)
        {
            continue;
        }
        if (cameraFrustum != NULL)
        {
            Vector3 Center;
            float Radius;
            WorldLocalShadows_GetSpotBounds(Light, &Center, &Radius);
            if (!WorldVisibility_IsSphereInFrustum(cameraFrustum, Center, Radius))
            {
                continue; // its light reaches nothing the camera sees
            }
        }
        float Score = Light->Intensity / fmaxf(Vector3LengthSqr(Light->RenderPosition), 1.0f);
        if (WasShadowed(Light->ObjectId, previousSpotIds))
        {
            Score *= WORLD_LOCAL_SHADOWS_SPOT_HYSTERESIS;
        }
        RankSpot(&Choice, Scores, maxSpots, Index, Score);
    }
    return Choice;
}

void WorldLocalShadows_GetSpotBounds(const WorldRenderLight* light, Vector3* outCenter, float* outRadius)
{
    float Angle = fminf(fmaxf(light->Cone.OuterAngle, 0.0f), 1.5707963f);
    float Range = fmaxf(light->Range, 0.0f);
    float Along;
    if (Angle < QUARTER_PI)
    {
        // Through the apex and the rim: its centre on the axis as far from the apex as from the rim.
        *outRadius = Range / (2.0f * cosf(Angle));
        Along = *outRadius;
    }
    else
    {
        // Around the rim's circle (the apex and the cap's tip lie within it).
        *outRadius = Range * sinf(Angle);
        Along = Range * cosf(Angle);
    }
    *outCenter = Vector3Add(light->RenderPosition, Vector3Scale(light->Direction, Along));
}

WorldSpotShadowFrame WorldLocalShadows_GetSpotFrame(Vector3 position, Vector3 direction, float outerAngle, float range,
    int mapSize)
{
    WorldSpotShadowFrame Frame;
    float Size = (float)((mapSize > 0) ? mapSize : 1);
    float Outer = fminf(fmaxf(outerAngle, MIN_SPOT_ANGLE), WORLD_LOCAL_SHADOWS_MAX_SPOT_HALF_ANGLE);
    float HalfTangent = tanf(Outer) * (1.0f + ((2.0f * WORLD_LOCAL_SHADOWS_SPOT_MARGIN_TEXELS) / Size));
    Frame.HalfAngle = fminf(atanf(HalfTangent), WORLD_LOCAL_SHADOWS_MAX_SPOT_HALF_ANGLE);
    Frame.Near = WORLD_LOCAL_SHADOWS_SPOT_NEAR;
    Frame.Far = fmaxf(range, 2.0f * WORLD_LOCAL_SHADOWS_SPOT_NEAR);

    Vector3 Up = (fabsf(direction.y) > UP_SWITCH_COSINE) ? (Vector3){ 0.0f, 0.0f, 1.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };
    Frame.View = MatrixLookAt(position, Vector3Add(position, direction), Up);
    Frame.Projection = MatrixPerspective(2.0 * (double)Frame.HalfAngle, 1.0, (double)Frame.Near, (double)Frame.Far);
    Frame.ViewProjection = MatrixMultiply(Frame.View, Frame.Projection);

    Frame.TexelScale = (2.0f * tanf(Frame.HalfAngle)) / Size;
    Frame.NormalOffsetScale = WORLD_LOCAL_SHADOWS_NORMAL_OFFSET_TEXELS * Frame.TexelScale;
    Frame.DepthBiasScale = (WORLD_LOCAL_SHADOWS_DEPTH_BIAS_TEXELS * Frame.TexelScale * Frame.Far * Frame.Near)
        / (Frame.Far - Frame.Near);
    return Frame;
}

bool WorldLocalShadows_ProjectSpot(const WorldSpotShadowFrame* frame, Vector3 point, Vector3* outMap)
{
    const Matrix* M = &frame->ViewProjection;
    float X = (M->m0 * point.x) + (M->m4 * point.y) + (M->m8 * point.z) + M->m12;
    float Y = (M->m1 * point.x) + (M->m5 * point.y) + (M->m9 * point.z) + M->m13;
    float Z = (M->m2 * point.x) + (M->m6 * point.y) + (M->m10 * point.z) + M->m14;
    float W = (M->m3 * point.x) + (M->m7 * point.y) + (M->m11 * point.z) + M->m15;
    if (!(W > 0.0f))
    {
        return false;
    }
    *outMap = (Vector3){ ((X / W) * 0.5f) + 0.5f, ((Y / W) * 0.5f) + 0.5f, ((Z / W) * 0.5f) + 0.5f };
    return true;
}
