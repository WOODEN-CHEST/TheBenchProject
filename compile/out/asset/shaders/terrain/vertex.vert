#version 330

// The terrain's vertex stage (B01, B02): rigid. A chunk's vertices are relative to its centre, which its render-space
// matrix places. The palette's sixteen weights come four normalized bytes at a time at attribute locations 9 to 12:
// raylib's instance-matrix locations, free here because a terrain is never instanced nor skinned (agents/WorldRuntime.md,
// "Vertex attributes"); location 13 carries the vertex's flags (B02). A face material's vertices carry that material's
// weight alone and the face-material flag, so its faces take it outright and the rules leave them as they are.

in vec3 vertexPosition;
in vec3 vertexNormal;
layout(location = 9) in vec4 vertexWeights0;   // palette materials 0..3
layout(location = 10) in vec4 vertexWeights1;  // 4..7
layout(location = 11) in vec4 vertexWeights2;  // 8..11
layout(location = 12) in vec4 vertexWeights3;  // 12..15
layout(location = 13) in vec4 vertexFlags;     // x: 1 on a face material's vertices, 0 on painted ones; y, z, w: 0

// Supplied by raylib's DrawMesh path.
uniform mat4 mvp;        // model-view-projection
uniform mat4 matModel;   // the chunk's matrix into render space (the world's orientation, the camera at the origin)
uniform mat4 matNormal;  // transpose(inverse(matModel))

out vec3 fragPosition;       // render-space surface position (relative to the camera)
out vec3 fragNormal;         // world-space surface normal
out vec3 fragTerrainOffset;  // the position relative to the chunk's centre in the terrain's own space, exact (the maps and
                             // the rules' noise add the centre, wrapped by their periods on the CPU in double)
out vec3 fragTerrainNormal;  // the surface normal in the terrain's own space (the maps' projections)
out float fragHeightOffset;  // the height above the chunk's centre in the world (the rules' height adds the centre's)
out vec4 fragWeights0;
out vec4 fragWeights1;
out vec4 fragWeights2;
out vec4 fragWeights3;
out float fragFaceMaterial;

void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));
    fragTerrainOffset = vertexPosition;
    fragTerrainNormal = vertexNormal;
    fragHeightOffset = (mat3(matModel)*vertexPosition).y;
    fragWeights0 = vertexWeights0;
    fragWeights1 = vertexWeights1;
    fragWeights2 = vertexWeights2;
    fragWeights3 = vertexWeights3;
    fragFaceMaterial = vertexFlags.x;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
