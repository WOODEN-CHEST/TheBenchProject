#include <math.h>
#include "WorldLightCulling.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldLight.h"
#include "raylib/raymath.h"


// Static functions.
/* Inserts a (light, influence) pair into the descending-influence top list held in @p outLights /
 * @p influences (currently @p count entries, max @p capacity). Returns the new count. When the list is full
 * the weakest entry is dropped if the newcomer beats it; otherwise the newcomer is ignored. */
static size_t InsertRanked(const WorldLight** outLights, float* influences, size_t count, size_t capacity,
    const WorldLight* light, float influence)
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
        outLights[Index] = outLights[Index - 1];
        influences[Index] = influences[Index - 1];
    }

    outLights[InsertAt] = light;
    influences[InsertAt] = influence;
    return (count < capacity) ? (count + 1) : count;
}


// Public functions.
size_t WorldLightCulling_SelectForSphere(World* world, Vector3 center, float radius,
    const WorldLight** outLights, size_t capacity)
{
    if ((world == NULL) || (outLights == NULL) || (capacity == 0))
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

    size_t ObjectCount = World_GetObjectCount(world);
    for (size_t Index = 0; Index < ObjectCount; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&GetResult);
            continue;
        }
        if (WorldObject_GetType(Object) != WorldObjectType_Light)
        {
            continue;
        }

        WorldLight* Light = (WorldLight*)Object;
        float Range = WorldLight_GetSize(Light);
        float Intensity = WorldLight_GetIntensity(Light);
        if ((Range <= 0.0f) || (Intensity <= 0.0f))
        {
            continue;
        }

        Vector3 Position = WorldObject_GetPosition(Object);
        float SurfaceDistance = Vector3Distance(Position, center) - radius;
        if (SurfaceDistance < 0.0f)
        {
            SurfaceDistance = 0.0f; // light reaches into (or past) the sphere centre
        }
        if (SurfaceDistance >= Range)
        {
            continue; // the light's reach does not touch the object's sphere
        }

        // Influence: brighter lights and lights whose reach penetrates deeper into the sphere rank higher.
        float Penetration = 1.0f - (SurfaceDistance / Range); // (0, 1]
        float Influence = Intensity * Penetration;
        Count = InsertRanked(outLights, Influences, Count, capacity, Light, Influence);
    }

    return Count;
}
