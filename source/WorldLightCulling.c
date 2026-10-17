#include "WorldLightCulling.h"
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


// Public functions.
size_t WorldLightCulling_SelectForSphere(const WorldCullingLight* lights, size_t lightCount, Vector3 center,
    float radius, size_t* outIndices, size_t capacity)
{
    if ((lights == NULL) || (outIndices == NULL) || (capacity == 0))
    {
        return 0;
    }
    if (capacity > WORLD_MAX_FORWARD_LIGHTS)
    {
        capacity = WORLD_MAX_FORWARD_LIGHTS;
    }
    if (radius < 0.0f)
    {
        radius = 0.0f;
    }

    float Influences[WORLD_MAX_FORWARD_LIGHTS];
    size_t Count = 0;
    for (size_t Index = 0; Index < lightCount; Index++)
    {
        const WorldCullingLight* Light = &lights[Index];
        if ((Light->Range <= 0.0f) || (Light->Intensity <= 0.0f))
        {
            continue;
        }

        float SurfaceDistance = Vector3Distance(Light->Position, center) - radius;
        if (SurfaceDistance < 0.0f)
        {
            SurfaceDistance = 0.0f; // light reaches into (or past) the sphere centre
        }
        if (SurfaceDistance >= Light->Range)
        {
            continue; // the light's reach does not touch the object's sphere
        }

        // Influence: brighter lights and lights whose reach penetrates deeper into the sphere rank higher.
        float Penetration = 1.0f - (SurfaceDistance / Light->Range); // (0, 1]
        float Influence = Light->Intensity * Penetration;
        Count = InsertRanked(outIndices, Influences, Count, capacity, Index, Influence);
    }

    return Count;
}
