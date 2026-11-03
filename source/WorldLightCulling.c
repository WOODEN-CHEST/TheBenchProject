#include "WorldLightCulling.h"
#include <math.h>
#include "raylib/raymath.h"


// Static functions.
/* Inserts a (light index, influence) pair into the descending-influence top list held in @p outIndices /
 * @p influences (currently @p count entries, max @p capacity). Returns the new count. When the list is full
 * the weakest entry is dropped if the newcomer beats it; otherwise the newcomer is ignored. */
static size_t InsertRanked(size_t* outIndices, float* influences, size_t count, size_t capacity,
    size_t lightIndex, float influence)
{
    // Full and not better than the current weakest: nothing to do.
    if ((count >= capacity) && (influence <= influences[capacity - 1]))
    {
        return count;
    }

    // Find the insertion point (first slot whose influence is smaller than the newcomer's).
    size_t InsertAt = (count < capacity) ? count : (capacity - 1);
    while ((InsertAt > 0) && (influences[InsertAt - 1] < influence))
    {
        InsertAt--;
    }

    // Shift the tail down by one to open a slot, staying within capacity (the last entry falls off if full).
    size_t Last = (count < capacity) ? count : (capacity - 1);
    for (size_t Index = Last; Index > InsertAt; Index--)
    {
        outIndices[Index] = outIndices[Index - 1];
        influences[Index] = influences[Index - 1];
    }

    outIndices[InsertAt] = lightIndex;
    influences[InsertAt] = influence;
    return (count < capacity) ? (count + 1) : count;
}

/* Whether a sphere reaches into a spot's cone and in front of its apex. In the cone's frame (the apex at the origin, the
 * axis along `along`, `across` the distance from the axis), the sphere's centre lies outside the infinite cone by
 * across cos(a) - along sin(a), which never exceeds its true distance (behind the apex the apex itself is nearer than
 * the cone's surface), so the test may keep a sphere near the apex but never drops one the cone reaches; a sphere wholly
 * behind the apex's plane is dropped (the cone opens less than 90 degrees, so nothing behind that plane is in it). */
static bool DoesConeReachSphere(const WorldCullingLight* light, Vector3 toCenter, float distanceSquared, float radius)
{
    float Along = Vector3DotProduct(toCenter, light->Direction);
    if (Along < -radius)
    {
        return false;
    }
    float Across = sqrtf(fmaxf(distanceSquared - (Along * Along), 0.0f));
    float CosOuter = fminf(fmaxf(light->CosOuter, 0.0f), 1.0f);
    float SinOuter = sqrtf(fmaxf(1.0f - (CosOuter * CosOuter), 0.0f));
    return ((Across * CosOuter) - (Along * SinOuter)) <= radius;
}


// Public functions.
bool WorldLightCulling_DoesLightReachSphere(const WorldCullingLight* light, Vector3 center, float radius)
{
    if (!(light->Range > 0.0f))
    {
        return false;
    }
    if (!(radius > 0.0f))
    {
        radius = 0.0f;
    }

    Vector3 ToCenter = Vector3Subtract(center, light->Position);
    float DistanceSquared = Vector3DotProduct(ToCenter, ToCenter);
    float SurfaceDistance = sqrtf(DistanceSquared) - radius;
    if (SurfaceDistance >= light->Range)
    {
        return false; // the light's reach does not touch the object's sphere
    }
    return !light->IsSpot || DoesConeReachSphere(light, ToCenter, DistanceSquared, radius);
}

size_t WorldLightCulling_SelectForSphere(const WorldCullingLight* lights, size_t lightCount, Vector3 center,
    float radius, size_t* outIndices, size_t capacity, size_t* outReachingCount)
{
    if (outReachingCount != NULL)
    {
        *outReachingCount = 0U;
    }
    if ((lights == NULL) || (outIndices == NULL))
    {
        return 0;
    }
    if (capacity > WORLD_MAX_FORWARD_LIGHTS)
    {
        capacity = WORLD_MAX_FORWARD_LIGHTS;
    }
    if (!(radius > 0.0f))
    {
        radius = 0.0f; // negative or not a number
    }

    float Influences[WORLD_MAX_FORWARD_LIGHTS];
    size_t Count = 0;
    size_t Reaching = 0;
    for (size_t Index = 0; Index < lightCount; Index++)
    {
        const WorldCullingLight* Light = &lights[Index];
        if ((Light->Range <= 0.0f) || (Light->Intensity <= 0.0f)
            || !WorldLightCulling_DoesLightReachSphere(Light, center, radius))
        {
            continue;
        }
        Reaching++;
        if (capacity == 0)
        {
            continue;
        }

        float SurfaceDistance = Vector3Distance(Light->Position, center) - radius;
        if (SurfaceDistance < 0.0f)
        {
            SurfaceDistance = 0.0f; // light reaches into (or past) the sphere centre
        }
        // Influence: brighter lights and lights whose reach penetrates deeper into the sphere rank higher.
        float Penetration = 1.0f - (SurfaceDistance / Light->Range); // (0, 1]
        float Influence = Light->Intensity * Penetration;
        Count = InsertRanked(outIndices, Influences, Count, capacity, Index, Influence);
    }

    if (outReachingCount != NULL)
    {
        *outReachingCount = Reaching;
    }
    return Count;
}
