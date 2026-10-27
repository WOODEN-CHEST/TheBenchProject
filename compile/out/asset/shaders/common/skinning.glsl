// Skinning for the world's skinned vertex stages, for #include "common/skinning": a vertex follows up to four bones
// of its model's skeleton, each by its weight, through the bone palette the renderer uploads per object. Included files
// never declare #version: the main file does.
//
// THE PALETTE. Per bone, the first three rows of its skinning matrix: the bone's inverse bind matrix, then its posed
// matrix, both in the model's mesh space (source/SkeletalAnimation.c, SkeletalPose_ComputePalette). Row r of bone b is
// bonePalette[b*3 + r], and a point p goes to (dot(row0, p), dot(row1, p), dot(row2, p)) with p = (x, y, z, 1). A
// vertex's rows are its bones' rows blended by its weights (linear blend skinning: blending the rows blends the
// matrices); the blended rows move its position and turn its normal in mesh space, and the stage goes on as a rigid
// one does (matModel, mvp). The normal turns by the same rows: exact for turns and even scales, which is what a rig's
// bones do; a bone scaled unevenly would need the inverse transpose.
//
// THE ATTRIBUTES. raylib binds vertexBoneIndices to attribute 7 and vertexBoneWeights to 8 in every program it links,
// and a skinned mesh's vertex array feeds them from its bone buffers (raylib built with SUPPORT_GPU_SKINNING,
// BUILDING.md): four bone indices (bytes, read as the floats 0 to 255) and four weights. The weights are divided by
// their sum, since a file's need not add up to 1 exactly; a vertex without weights keeps its bind position. The
// renderer draws a mesh with these stages only when every index is below its skeleton's bone count
// (GameSkeleton_IsMeshSkinned) and the skeleton fits the palette (it reads the array's size from the program).

// How many bones the palette holds; a skeleton with more draws its skinned meshes in their bind pose.
#define SKINNING_MAX_BONES 128

in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;

uniform vec4 bonePalette[SKINNING_MAX_BONES*3];

// The rows of this vertex's skinning matrix: its bones' rows blended by its weights.
void Skinning_GetRows(out vec4 row0, out vec4 row1, out vec4 row2)
{
    float total = vertexBoneWeights.x + vertexBoneWeights.y + vertexBoneWeights.z + vertexBoneWeights.w;
    if (!(total > 0.0))
    {
        row0 = vec4(1.0, 0.0, 0.0, 0.0);
        row1 = vec4(0.0, 1.0, 0.0, 0.0);
        row2 = vec4(0.0, 0.0, 1.0, 0.0);
        return;
    }
    row0 = vec4(0.0);
    row1 = vec4(0.0);
    row2 = vec4(0.0);
    for (int influence = 0; influence < 4; influence++)
    {
        float weight = vertexBoneWeights[influence]/total;
        int bone = clamp(int(vertexBoneIndices[influence] + 0.5), 0, SKINNING_MAX_BONES - 1);
        row0 += bonePalette[bone*3]*weight;
        row1 += bonePalette[bone*3 + 1]*weight;
        row2 += bonePalette[bone*3 + 2]*weight;
    }
}

// A point moved by the rows.
vec3 Skinning_MovePoint(vec4 row0, vec4 row1, vec4 row2, vec3 point)
{
    vec4 p = vec4(point, 1.0);
    return vec3(dot(row0, p), dot(row1, p), dot(row2, p));
}

// A direction turned by the rows (not normalized: the stage normalizes it after its own matrix).
vec3 Skinning_TurnVector(vec4 row0, vec4 row1, vec4 row2, vec3 direction)
{
    return vec3(dot(row0.xyz, direction), dot(row1.xyz, direction), dot(row2.xyz, direction));
}
