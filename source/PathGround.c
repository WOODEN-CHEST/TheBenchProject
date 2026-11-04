#include "PathGround.h"
#include "wr/WRMemory.h"


// Static functions.
/* The nearest hit the ray does not start inside: every hit, nearest first. Only when the first says it started inside. */
static bool FindFirstEnteredHit(WorldCollision* collision, Vector3Double origin, Vector3 direction, float reach,
    const WorldCollisionFilter* filter, WorldRayHit* outHit)
{
    GenericBuffer Hits;
    GenericBuffer_AllocateVariable(&Hits, 0U, sizeof(WorldRayHit));
    size_t Count = WorldCollision_RaycastAll(collision, origin, direction, reach, filter, &Hits);
    bool IsFound = false;
    for (size_t Index = 0U; (Index < Count) && !IsFound; Index++)
    {
        const WorldRayHit* Hit = (const WorldRayHit*)Hits._data + Index;
        if (!Hit->IsStartInside)
        {
            *outHit = *Hit;
            IsFound = true;
        }
    }
    Memory_Free(Hits._data);
    return IsFound;
}


// Public functions.
bool PathGround_Project(WorldCollision* collision, Vector3Double point, const PathGroundSettings* settings,
    PathGroundHit* outHit)
{
    float Reach = settings->SearchHeight + settings->SearchDepth;
    if (!(Reach > 0.0f))
    {
        return false;
    }
    bool IsTerrainOnly = settings->Target == PathGroundTarget_Terrain;
    WorldCollisionFilter Filter = WorldCollisionFilter_Create(IsTerrainOnly ? COLLISION_LAYER_MASK_ALL : settings->LayerMask,
        WorldCollisionTriggerMode_Ignore);
    Filter.IsTerrainOnly = IsTerrainOnly;
    Filter.IgnoredObjectId = settings->IgnoredObjectId;
    Filter.IsIgnoredSubtree = settings->IgnoredObjectId != 0U;
    Vector3Double Origin = Vector3Double_Create(point.X, point.Y + (double)settings->SearchHeight, point.Z);
    Vector3 Down = { 0.0f, -1.0f, 0.0f };
    WorldRayHit Hit;
    if (!WorldCollision_Raycast(collision, Origin, Down, Reach, &Filter, &Hit))
    {
        return false;
    }
    if (Hit.IsStartInside && !FindFirstEnteredHit(collision, Origin, Down, Reach, &Filter, &Hit))
    {
        return false;
    }
    *outHit = (PathGroundHit){ .Ground = Hit.Point,
        .Position = Vector3Double_Create(Hit.Point.X, Hit.Point.Y + (double)settings->Offset, Hit.Point.Z),
        .Normal = Hit.Normal, .Shape = Hit.Shape };
    return true;
}
