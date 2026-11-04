// Meshes a generator made while the game runs: see WorldGeneratedModel.h.

#include <float.h>
#include <math.h>
#include "WorldGeneratedModel.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Static functions.
static void FreeMeshArrays(WorldGeneratedMesh* mesh)
{
    Memory_Free(mesh->Positions);
    Memory_Free(mesh->Normals);
    Memory_Free(mesh->Tangents);
    Memory_Free(mesh->Texcoords);
    Memory_Free(mesh->Texcoords2);
    Memory_Free(mesh->Colors);
}

/* Whether two optional arrays hold the same bytes. */
static bool AreArraysEqual(const void* a, const void* b, size_t byteCount)
{
    if ((a == NULL) || (b == NULL))
    {
        return a == b;
    }
    return Memory_IsEqual(a, b, byteCount);
}

static bool AreMeshesEqual(const WorldGeneratedMesh* a, const WorldGeneratedMesh* b)
{
    if ((a->MaterialSlot != b->MaterialSlot) || (a->VertexCount != b->VertexCount))
    {
        return false;
    }
    size_t Count = a->VertexCount;
    return AreArraysEqual(a->Positions, b->Positions, Count * 3U * sizeof(float))
        && AreArraysEqual(a->Normals, b->Normals, Count * 3U * sizeof(float))
        && AreArraysEqual(a->Tangents, b->Tangents, Count * 4U * sizeof(float))
        && AreArraysEqual(a->Texcoords, b->Texcoords, Count * 2U * sizeof(float))
        && AreArraysEqual(a->Texcoords2, b->Texcoords2, Count * 2U * sizeof(float))
        && AreArraysEqual(a->Colors, b->Colors, Count * 4U);
}


// Functions.
WorldGeneratedModel* WorldGeneratedModel_Create(size_t lodCount)
{
    WorldGeneratedModel* Self = Memory_Allocate(sizeof(WorldGeneratedModel));
    Memory_Zero(Self, sizeof(*Self));
    Self->LodCount = (lodCount < 1U) ? 1U : ((lodCount > WORLD_MODEL_MAX_LODS) ? WORLD_MODEL_MAX_LODS : lodCount);
    return Self;
}

void WorldGeneratedModel_Destroy(WorldGeneratedModel* self)
{
    if (self == NULL)
    {
        return;
    }
    for (size_t Lod = 0U; Lod < self->LodCount; Lod++)
    {
        for (uint32_t MeshIndex = 0U; MeshIndex < self->Lods[Lod].MeshCount; MeshIndex++)
        {
            FreeMeshArrays(&self->Lods[Lod].Meshes[MeshIndex]);
        }
        Memory_Free(self->Lods[Lod].Meshes);
    }
    Memory_Free(self);
}

bool WorldGeneratedModel_AddMesh(WorldGeneratedModel* self, size_t lod, const WorldGeneratedMesh* mesh)
{
    if (lod >= self->LodCount)
    {
        return false;
    }
    WorldGeneratedLod* Lod = &self->Lods[lod];
    WorldGeneratedMesh* Grown = Memory_Allocate(((size_t)Lod->MeshCount + 1U) * sizeof(WorldGeneratedMesh));
    if (Lod->MeshCount > 0U)
    {
        Memory_Copy(Lod->Meshes, Grown, (size_t)Lod->MeshCount * sizeof(WorldGeneratedMesh));
    }
    Grown[Lod->MeshCount] = *mesh;
    Memory_Free(Lod->Meshes);
    Lod->Meshes = Grown;
    Lod->MeshCount++;
    return true;
}

void WorldGeneratedModel_UpdateBounds(WorldGeneratedModel* self)
{
    Vector3 Min = { FLT_MAX, FLT_MAX, FLT_MAX };
    Vector3 Max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t Lod = 0U; Lod < self->LodCount; Lod++)
    {
        for (uint32_t MeshIndex = 0U; MeshIndex < self->Lods[Lod].MeshCount; MeshIndex++)
        {
            const WorldGeneratedMesh* Held = &self->Lods[Lod].Meshes[MeshIndex];
            for (uint32_t Vertex = 0U; Vertex < Held->VertexCount; Vertex++)
            {
                const float* Position = &Held->Positions[(size_t)Vertex * 3U];
                Vector3 Point = { Position[0], Position[1], Position[2] };
                Min = Vector3Min(Min, Point);
                Max = Vector3Max(Max, Point);
            }
        }
        if (Min.x <= Max.x)
        {
            break; // the finest LOD with anything gives the bounds
        }
    }
    if (Min.x > Max.x)
    {
        Min = (Vector3){ 0.0f, 0.0f, 0.0f };
        Max = Min;
    }
    self->Box = (BoundingBox){ Min, Max };
    self->Center = Vector3Scale(Vector3Add(Min, Max), 0.5f);
    self->Radius = Vector3Distance(self->Center, Max);
}

bool WorldGeneratedModel_Equals(const WorldGeneratedModel* a, const WorldGeneratedModel* b)
{
    if ((a == NULL) || (b == NULL))
    {
        return a == b;
    }
    if (a->LodCount != b->LodCount)
    {
        return false;
    }
    for (size_t Lod = 0U; Lod < a->LodCount; Lod++)
    {
        if (a->Lods[Lod].MeshCount != b->Lods[Lod].MeshCount)
        {
            return false;
        }
        for (uint32_t MeshIndex = 0U; MeshIndex < a->Lods[Lod].MeshCount; MeshIndex++)
        {
            if (!AreMeshesEqual(&a->Lods[Lod].Meshes[MeshIndex], &b->Lods[Lod].Meshes[MeshIndex]))
            {
                return false;
            }
        }
    }
    return true;
}
