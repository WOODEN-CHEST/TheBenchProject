#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "TerrainMeshInternal.h"
#include "GltfChecks.h"
#include "Logger.h"
#include "NamespacedKey.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include "wr/WRVectorMath.h"

/*
 * A terrain imported from glTF (TerrainMesh_CreateFromGltf, TerrainMesh.h): what Blender writes. The file is
 * hostile: source/GltfChecks.h checks its structure before cgltf trusts a number, and this reader checks every count,
 * index, attribute shape and transform before it reads one.
 *
 * Two passes over the default scene's nodes (an explicit stack, MAX_NODE_DEPTH levels at most): the first counts every
 * triangle primitive's vertices and triangles against the limits and checks its slot and attributes, the second reads
 * them into arrays sized by the count. Then vertices with the same position on the grid (and, with imported normals, the
 * same normal) are welded: their weights and densities are averaged where they differ (a seam in the file), and
 * triangles left with two corners at one position are dropped (both logged once, with counts). TerrainMesh_Create then
 * checks the mesh's own limits and cuts the chunks.
 *
 * TRANSFORMS. Each node's world transform is composed in double (glTF's T * R * S, its parents' after), each position
 * goes through it in double and is rounded once onto the grid. Normals go through its inverse transpose. A node whose
 * transform mirrors (a negative determinant) has its triangles' winding turned back, as glTF says.
 */


// Macros.
/** The deepest node tree read (rule 5; cgltf refused cycles). */
#define MAX_NODE_DEPTH 256U
/** The most vertices the file's primitives may have together before they are welded (Blender splits a vertex wherever
 *  a face corner's attribute differs). */
#define MAX_INPUT_VERTICES ((uint64_t)TERRAIN_MESH_MAX_VERTICES * 4U)
/** The material slot that makes holes. */
#define HOLE_SLOT_NAME ((const unsigned char*)u8"hole")
/** The default slot name of the painted weights. */
#define DEFAULT_BLEND_SLOT_NAME ((const unsigned char*)u8"blend")
/** Room for a description, and a name from the file in printable form. */
#define DESCRIPTION_CAPACITY ((size_t)192)
#define NAME_PRINT_CAPACITY ((size_t)129)
/** How long a quaternion may be off unit before it is refused, and the smallest determinant a transform may have. */
#define MIN_QUATERNION_LENGTH 1e-6
#define MIN_DETERMINANT 1e-24
/** The first capacity of the welding table. */
#define MIN_TABLE_CAPACITY ((size_t)16)


// Types.
/* What a primitive's material slot makes of its faces. */
typedef enum SlotKindEnum
{
    SlotKind_Blend,
    SlotKind_Hole,
    SlotKind_Material
} SlotKind;

/* One node on the walk's stack, with its world transform. */
typedef struct NodeVisitStruct
{
    const cgltf_node* Node;
    Matrix4Double ParentWorld;
    uint32_t Depth;
} NodeVisit;

/* The input before welding: per vertex its position (double, in the terrain's space), its normal, weights and
 * densities; per triangle its corners, flags and material. */
typedef struct InputStruct
{
    Vector3Double* Positions;
    Vector3* Normals;
    uint8_t* Weights;
    uint8_t* Densities;
    uint32_t VertexCount;
    uint32_t* Indices;
    uint8_t* Flags;
    uint8_t* Materials;
    uint32_t TriangleCount;
} Input;

/* One read. */
typedef struct ImportStruct
{
    const TerrainGltfOptions* Options;
    char Description[DESCRIPTION_CAPACITY];
    const unsigned char* BlendSlotName;
    cgltf_data* Data;
    uint64_t VertexCount;
    uint64_t TriangleCount;
    Input In;
} Import;


// Static functions: messages.
static Error Fail(const Import* import, ErrorCode code, const char* format, ...)
{
    char Text[TERRAIN_MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        Text[0] = '\0';
    }
    return Error_Construct4(code, "%s: %s", import->Description, Text);
}

static void Warn(const Import* import, const char* format, ...)
{
    if (import->Options->Logger == NULL)
    {
        return;
    }
    char Text[TERRAIN_MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        Text[0] = '\0';
    }
    Error LogResult = Logger_LogWarningFormatted(import->Options->Logger, (const unsigned char*)u8"%s: %s",
        import->Description, Text);
    Error_Deconstruct(&LogResult);
}

static bool AreNamesEqual(const unsigned char* a, const char* b)
{
    if ((a == NULL) || (b == NULL))
    {
        return false;
    }
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == strlen(b)) && Memory_IsEqual(a, b, Length);
}

/* The full form of an asset name as written ("dirt" is "tbp:dirt") into @p storage (NAMESPACED_KEY_BUFFER_SIZE bytes);
 * false when it is not one. */
static bool ToFullName(const unsigned char* name, size_t byteCount, unsigned char* storage)
{
    GenericBuffer FullName;
    GenericBuffer_CreateConstant(&FullName, storage, NAMESPACED_KEY_BUFFER_SIZE, sizeof(unsigned char), 0U);
    Error Result = NamespacedKey_Parse(name, byteCount, NULL, &FullName);
    bool IsName = Result.Code == ErrorCode_Success;
    Error_Deconstruct(&Result);
    return IsName;
}

/* Whether a material slot's name names a palette material, both read as asset names. */
static bool IsSlotOf(const unsigned char* materialName, const char* slotName)
{
    unsigned char FullMaterial[NAMESPACED_KEY_BUFFER_SIZE];
    unsigned char FullSlot[NAMESPACED_KEY_BUFFER_SIZE];
    return ToFullName(materialName, StringUTF8_GetByteLength(materialName), FullMaterial)
        && ToFullName((const unsigned char*)slotName, strlen(slotName), FullSlot)
        && AreNamesEqual(FullMaterial, (const char*)FullSlot);
}


// Static functions: options.
static Error CheckMappings(const Import* import, const TerrainAttributeMapping* mappings, size_t count, uint32_t limit,
    const char* what)
{
    if ((mappings == NULL) && (count > 0U))
    {
        return Fail(import, ErrorCode_IllegalArgument, "the %s mappings are missing.", what);
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (mappings[Index].Attribute == NULL)
        {
            return Fail(import, ErrorCode_IllegalArgument, "%s mapping %" PRIu64 " names no attribute.", what,
                (uint64_t)Index);
        }
        for (size_t Channel = 0U; Channel < 4U; Channel++)
        {
            int32_t Target = mappings[Index].Targets[Channel];
            if ((Target < -1) || ((Target >= 0) && ((uint32_t)Target >= limit)))
            {
                return Fail(import, ErrorCode_IllegalArgument, "%s mapping %" PRIu64 " feeds %" PRId32 ", past the %"
                    PRIu32 " there are.", what, (uint64_t)Index, Target, limit);
            }
        }
    }
    return Error_CreateSuccess();
}

static Error CheckOptions(const Import* import)
{
    const TerrainGltfOptions* Options = import->Options;
    if ((Options->MaterialCount == 0U) || (Options->MaterialCount > TERRAIN_MAX_MATERIALS)
        || (Options->MaterialNames == NULL) || (Options->DensityChannelCount > TERRAIN_MAX_DENSITY_CHANNELS))
    {
        return Fail(import, ErrorCode_IllegalArgument, "the palette (1 to %u names) or the density channel count (at "
            "most %u) is out of range.", TERRAIN_MAX_MATERIALS, TERRAIN_MAX_DENSITY_CHANNELS);
    }
    for (uint32_t Entry = 0U; Entry < Options->MaterialCount; Entry++)
    {
        if (Options->MaterialNames[Entry] == NULL)
        {
            return Fail(import, ErrorCode_IllegalArgument, "palette entry %" PRIu32 " has no name.", Entry);
        }
        // The slots "hole" and the blend slot are read first (GetSlot): a material of either name would never apply.
        if (AreNamesEqual(Options->MaterialNames[Entry], (const char*)HOLE_SLOT_NAME)
            || AreNamesEqual(Options->MaterialNames[Entry], (const char*)import->BlendSlotName))
        {
            char Name[NAME_PRINT_CAPACITY];
            GltfChecks_Printable((const char*)Options->MaterialNames[Entry], Name, sizeof(Name));
            return Fail(import, ErrorCode_IllegalArgument, "palette entry %" PRIu32 " is \"%s\", a material slot's own "
                "name (holes or the painted weights).", Entry, Name);
        }
    }
    Error Result = CheckMappings(import, Options->Weights, Options->WeightMappingCount, Options->MaterialCount, "weight");
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckMappings(import, Options->Densities, Options->DensityMappingCount, Options->DensityChannelCount,
            "density");
    }
    return Result;
}


// Static functions: transforms.
/* A node's local transform in double: its matrix, or T * R * S (glTF's order: scale, then rotation, then translation). */
static Error GetLocalTransform(const Import* import, const cgltf_node* node, Matrix4Double* outLocal)
{
    *outLocal = Matrix4Double_Identity();
    char Name[NAME_PRINT_CAPACITY];
    if (node->has_matrix)
    {
        for (size_t Index = 0U; Index < 16U; Index++)
        {
            if (!isfinite(node->matrix[Index]))
            {
                return Fail(import, ErrorCode_InvalidAssetData, "node \"%s\" has a matrix that is not finite.",
                    GltfChecks_Printable(node->name, Name, sizeof(Name)));
            }
            outLocal->M[Index] = (double)node->matrix[Index];
        }
        return Error_CreateSuccess();
    }
    double T[3] = { 0.0, 0.0, 0.0 };
    double Q[4] = { 0.0, 0.0, 0.0, 1.0 };
    double S[3] = { 1.0, 1.0, 1.0 };
    for (size_t Axis = 0U; Axis < 3U; Axis++)
    {
        T[Axis] = node->has_translation ? (double)node->translation[Axis] : 0.0;
        S[Axis] = node->has_scale ? (double)node->scale[Axis] : 1.0;
    }
    for (size_t Component = 0U; (Component < 4U) && node->has_rotation; Component++)
    {
        Q[Component] = (double)node->rotation[Component];
    }
    double Length = sqrt((Q[0] * Q[0]) + (Q[1] * Q[1]) + (Q[2] * Q[2]) + (Q[3] * Q[3]));
    bool IsFinite = isfinite(Length) && isfinite(T[0]) && isfinite(T[1]) && isfinite(T[2]) && isfinite(S[0])
        && isfinite(S[1]) && isfinite(S[2]);
    if (!IsFinite || !(Length > MIN_QUATERNION_LENGTH))
    {
        return Fail(import, ErrorCode_InvalidAssetData, "node \"%s\" has a transform that is not finite or a rotation of "
            "length 0.", GltfChecks_Printable(node->name, Name, sizeof(Name)));
    }
    double X = Q[0] / Length;
    double Y = Q[1] / Length;
    double Z = Q[2] / Length;
    double W = Q[3] / Length;
    // Column-major: M[c * 4 + r] is row r of column c; each column of the rotation times its axis's scale.
    const double Rotation[3][3] =
    {
        { 1.0 - (2.0 * ((Y * Y) + (Z * Z))), 2.0 * ((X * Y) + (W * Z)), 2.0 * ((X * Z) - (W * Y)) },
        { 2.0 * ((X * Y) - (W * Z)), 1.0 - (2.0 * ((X * X) + (Z * Z))), 2.0 * ((Y * Z) + (W * X)) },
        { 2.0 * ((X * Z) + (W * Y)), 2.0 * ((Y * Z) - (W * X)), 1.0 - (2.0 * ((X * X) + (Y * Y))) }
    };
    Matrix4Double Local = Matrix4Double_Identity();
    for (size_t Column = 0U; Column < 3U; Column++)
    {
        for (size_t Row = 0U; Row < 3U; Row++)
        {
            Local.M[(Column * 4U) + Row] = Rotation[Column][Row] * S[Column];
        }
    }
    Local.M[12] = T[0];
    Local.M[13] = T[1];
    Local.M[14] = T[2];
    *outLocal = Local;
    return Error_CreateSuccess();
}

/* The determinant of a transform's linear part. */
static double GetDeterminant(const Matrix4Double* m)
{
    const double* M = m->M;
    return (M[0] * ((M[5] * M[10]) - (M[9] * M[6]))) - (M[4] * ((M[1] * M[10]) - (M[9] * M[2])))
        + (M[8] * ((M[1] * M[6]) - (M[5] * M[2])));
}

/* The inverse transpose of a transform's linear part, for normals (rows of the result in @p out[r][c]). */
static void GetNormalMatrix(const Matrix4Double* m, double determinant, double out[3][3])
{
    const double* M = m->M;
    // a[r][c] = M[c * 4 + r]; the inverse transpose is the cofactor matrix over the determinant.
    double A[3][3];
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        for (size_t Column = 0U; Column < 3U; Column++)
        {
            A[Row][Column] = M[(Column * 4U) + Row];
        }
    }
    out[0][0] = ((A[1][1] * A[2][2]) - (A[1][2] * A[2][1])) / determinant;
    out[0][1] = ((A[1][2] * A[2][0]) - (A[1][0] * A[2][2])) / determinant;
    out[0][2] = ((A[1][0] * A[2][1]) - (A[1][1] * A[2][0])) / determinant;
    out[1][0] = ((A[0][2] * A[2][1]) - (A[0][1] * A[2][2])) / determinant;
    out[1][1] = ((A[0][0] * A[2][2]) - (A[0][2] * A[2][0])) / determinant;
    out[1][2] = ((A[0][1] * A[2][0]) - (A[0][0] * A[2][1])) / determinant;
    out[2][0] = ((A[0][1] * A[1][2]) - (A[0][2] * A[1][1])) / determinant;
    out[2][1] = ((A[0][2] * A[1][0]) - (A[0][0] * A[1][2])) / determinant;
    out[2][2] = ((A[0][0] * A[1][1]) - (A[0][1] * A[1][0])) / determinant;
}


// Static functions: primitives.
/* What a primitive's material slot makes of its faces; a slot the terrain does not know is refused, named. */
static Error GetSlot(const Import* import, const cgltf_primitive* primitive, const cgltf_mesh* mesh, SlotKind* outKind,
    uint8_t* outMaterial)
{
    *outMaterial = 0U;
    const char* Name = (primitive->material != NULL) ? primitive->material->name : NULL;
    if ((Name == NULL) || AreNamesEqual(import->BlendSlotName, Name))
    {
        *outKind = SlotKind_Blend;
        return Error_CreateSuccess();
    }
    if (AreNamesEqual(HOLE_SLOT_NAME, Name))
    {
        *outKind = SlotKind_Hole;
        return Error_CreateSuccess();
    }
    for (uint32_t Entry = 0U; Entry < import->Options->MaterialCount; Entry++)
    {
        if (IsSlotOf(import->Options->MaterialNames[Entry], Name))
        {
            *outKind = SlotKind_Material;
            *outMaterial = (uint8_t)Entry;
            return Error_CreateSuccess();
        }
    }
    char Printable[NAME_PRINT_CAPACITY];
    char MeshName[NAME_PRINT_CAPACITY];
    char Blend[NAME_PRINT_CAPACITY];
    return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\" has the material slot \"%s\": a terrain's slots are "
        "\"%s\" (the painted weights), \"hole\", or a material of its palette.",
        GltfChecks_Printable(mesh->name, MeshName, sizeof(MeshName)), GltfChecks_Printable(Name, Printable,
        sizeof(Printable)), GltfChecks_Printable((const char*)import->BlendSlotName, Blend, sizeof(Blend)));
}

static const cgltf_accessor* FindAttribute(const cgltf_primitive* primitive, const unsigned char* name)
{
    for (cgltf_size Index = 0U; Index < primitive->attributes_count; Index++)
    {
        if (AreNamesEqual(name, primitive->attributes[Index].name))
        {
            return primitive->attributes[Index].data;
        }
    }
    return NULL;
}

/* An accessor's channel count when it is one the terrain reads for a mapped attribute: a scalar, RGB or RGBA of floats or
 * normalized unsigned bytes or shorts; 0 otherwise. */
static uint32_t GetValueChannels(const cgltf_accessor* accessor)
{
    bool IsComponentRead = (accessor->component_type == cgltf_component_type_r_32f)
        || (accessor->normalized && ((accessor->component_type == cgltf_component_type_r_8u)
            || (accessor->component_type == cgltf_component_type_r_16u)));
    if (!IsComponentRead)
    {
        return 0U;
    }
    switch (accessor->type)
    {
        case cgltf_type_scalar: return 1U;
        case cgltf_type_vec3: return 3U;
        case cgltf_type_vec4: return 4U;
        case cgltf_type_invalid:
        case cgltf_type_vec2:
        case cgltf_type_mat2:
        case cgltf_type_mat3:
        case cgltf_type_mat4:
        case cgltf_type_max_enum:
            break;
    }
    return 0U;
}

/* Checks a mapped attribute of a primitive: it is there, with the vertex count, a shape the terrain reads, and every
 * channel the mapping feeds. */
/* A terrain reads only what the file stores: an accessor without a buffer view is zeros (and maybe a sparse set of
 * replacements) of any count, which would cost memory the file never paid for. */
static Error CheckStored(const Import* import, const cgltf_accessor* accessor, const char* meshName, const char* what)
{
    if ((accessor == NULL) || (accessor->buffer_view != NULL))
    {
        return Error_CreateSuccess();
    }
    return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\"'s %s are not stored in the file (an accessor without a "
        "buffer view); a terrain reads only stored data.", meshName, what);
}

static Error CheckMappedAttribute(const Import* import, const cgltf_primitive* primitive, const cgltf_mesh* mesh,
    const TerrainAttributeMapping* mapping, cgltf_size vertexCount)
{
    char MeshName[NAME_PRINT_CAPACITY];
    char Attribute[NAME_PRINT_CAPACITY];
    GltfChecks_Printable(mesh->name, MeshName, sizeof(MeshName));
    GltfChecks_Printable((const char*)mapping->Attribute, Attribute, sizeof(Attribute));
    const cgltf_accessor* Accessor = FindAttribute(primitive, mapping->Attribute);
    if (Accessor == NULL)
    {
        return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\" has a primitive without the attribute \"%s\" the "
            "definition maps.", MeshName, Attribute);
    }
    uint32_t Channels = GetValueChannels(Accessor);
    if ((Channels == 0U) || (Accessor->count != vertexCount))
    {
        return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\"'s attribute \"%s\" must be one value per vertex: a "
            "scalar, RGB or RGBA of floats or normalized unsigned bytes or shorts.", MeshName, Attribute);
    }
    char What[NAME_PRINT_CAPACITY + 24U];
    snprintf(What, sizeof(What), "values of \"%s\"", Attribute);
    Error Stored = CheckStored(import, Accessor, MeshName, What);
    if (Stored.Code != ErrorCode_Success)
    {
        return Stored;
    }
    for (uint32_t Channel = Channels; Channel < 4U; Channel++)
    {
        if (mapping->Targets[Channel] >= 0)
        {
            return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\"'s attribute \"%s\" has %" PRIu32 " channels; "
                "the definition maps channel %" PRIu32 ".", MeshName, Attribute, Channels, Channel + 1U);
        }
    }
    return Error_CreateSuccess();
}

/* A primitive's glTF mode in words, with its number (cgltf's values are one above glTF's). */
static const char* DescribeMode(cgltf_primitive_type type)
{
    switch (type)
    {
        case cgltf_primitive_type_points: return "points (mode 0)";
        case cgltf_primitive_type_lines: return "lines (mode 1)";
        case cgltf_primitive_type_line_loop: return "a line loop (mode 2)";
        case cgltf_primitive_type_line_strip: return "a line strip (mode 3)";
        case cgltf_primitive_type_triangles: return "triangles (mode 4)";
        case cgltf_primitive_type_triangle_strip: return "a triangle strip (mode 5)";
        case cgltf_primitive_type_triangle_fan: return "a triangle fan (mode 6)";
        case cgltf_primitive_type_invalid:
        case cgltf_primitive_type_max_enum:
            break;
    }
    return "an unknown mode";
}

/* Checks one primitive and counts its vertices and triangles. */
static Error CountPrimitive(Import* import, const cgltf_primitive* primitive, const cgltf_mesh* mesh)
{
    char MeshName[NAME_PRINT_CAPACITY];
    GltfChecks_Printable(mesh->name, MeshName, sizeof(MeshName));
    if (primitive->type != cgltf_primitive_type_triangles)
    {
        return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\" has a primitive of %s, not triangles; a terrain "
            "reads triangles only (mode 4).", MeshName, DescribeMode(primitive->type));
    }
    if (primitive->has_draco_mesh_compression)
    {
        return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\" is Draco-compressed, which the game does not read "
            "(export without compression).", MeshName);
    }
    SlotKind Kind = SlotKind_Blend;
    uint8_t SlotMaterial = 0U;
    Error Result = GetSlot(import, primitive, mesh, &Kind, &SlotMaterial);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const cgltf_accessor* Positions = FindAttribute(primitive, (const unsigned char*)u8"POSITION");
    if ((Positions == NULL) || (Positions->type != cgltf_type_vec3)
        || (Positions->component_type != cgltf_component_type_r_32f) || (Positions->count == 0U))
    {
        return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\" has a primitive without float positions.", MeshName);
    }
    Result = CheckStored(import, Positions, MeshName, "positions");
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckStored(import, primitive->indices, MeshName, "indices");
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (import->Options->NormalMode == TerrainNormalMode_Imported)
    {
        const cgltf_accessor* Normals = FindAttribute(primitive, (const unsigned char*)u8"NORMAL");
        if ((Normals == NULL) || (Normals->type != cgltf_type_vec3)
            || (Normals->component_type != cgltf_component_type_r_32f) || (Normals->count != Positions->count))
        {
            return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\" has a primitive without float normals, which "
                "\"normals\": \"imported\" needs.", MeshName);
        }
        Result = CheckStored(import, Normals, MeshName, "normals");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    for (size_t Index = 0U; Index < import->Options->WeightMappingCount; Index++)
    {
        Result = CheckMappedAttribute(import, primitive, mesh, &import->Options->Weights[Index], Positions->count);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    for (size_t Index = 0U; Index < import->Options->DensityMappingCount; Index++)
    {
        Result = CheckMappedAttribute(import, primitive, mesh, &import->Options->Densities[Index], Positions->count);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    cgltf_size Corners = (primitive->indices != NULL) ? primitive->indices->count : Positions->count;
    if ((Corners % 3U) != 0U)
    {
        return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\" has a primitive whose %" PRIu64 " corners are not "
            "whole triangles.", MeshName, (uint64_t)Corners);
    }
    // Each count is checked before it is added, so the sums never pass the limits (nor overflow).
    if ((Positions->count > (MAX_INPUT_VERTICES - import->VertexCount))
        || ((Corners / 3U) > ((uint64_t)TERRAIN_MESH_MAX_TRIANGLES - import->TriangleCount)))
    {
        return Fail(import, ErrorCode_InvalidAssetData, "its primitives hold more than %" PRIu64 " vertices or %" PRIu32
            " triangles.", MAX_INPUT_VERTICES, TERRAIN_MESH_MAX_TRIANGLES);
    }
    import->VertexCount += Positions->count;
    import->TriangleCount += Corners / 3U;
    return Error_CreateSuccess();
}

/* Reads one mapped attribute's values into the vertices from @p first (bytes, clamped to 0..1, at each target). */
static void ReadMappedAttribute(const cgltf_accessor* accessor, const TerrainAttributeMapping* mapping, uint32_t first,
    uint8_t* records, size_t stride)
{
    uint32_t Channels = GetValueChannels(accessor);
    for (cgltf_size Vertex = 0U; Vertex < accessor->count; Vertex++)
    {
        float Values[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        cgltf_accessor_read_float(accessor, Vertex, Values, Channels);
        for (uint32_t Channel = 0U; Channel < Channels; Channel++)
        {
            int32_t Target = mapping->Targets[Channel];
            if (Target < 0)
            {
                continue;
            }
            float Value = isfinite(Values[Channel]) ? fminf(fmaxf(Values[Channel], 0.0f), 1.0f) : 0.0f;
            records[((size_t)(first + Vertex) * stride) + (size_t)Target] = (uint8_t)lroundf(Value * 255.0f);
        }
    }
}

/* Reads one primitive (checked by CountPrimitive) into the input, through its node's world transform. */
static Error ReadPrimitive(Import* import, const cgltf_primitive* primitive, const cgltf_mesh* mesh,
    const Matrix4Double* world)
{
    Input* In = &import->In;
    SlotKind Kind = SlotKind_Blend;
    uint8_t SlotMaterial = 0U;
    Error Result = GetSlot(import, primitive, mesh, &Kind, &SlotMaterial);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    double Determinant = GetDeterminant(world);
    if (!isfinite(Determinant) || !(fabs(Determinant) > MIN_DETERMINANT))
    {
        char MeshName[NAME_PRINT_CAPACITY];
        return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\" is placed by a transform that flattens it (or is not "
            "finite).", GltfChecks_Printable(mesh->name, MeshName, sizeof(MeshName)));
    }
    double NormalMatrix[3][3];
    GetNormalMatrix(world, Determinant, NormalMatrix);

    uint32_t First = In->VertexCount;
    const cgltf_accessor* Positions = FindAttribute(primitive, (const unsigned char*)u8"POSITION");
    const cgltf_accessor* Normals = (import->Options->NormalMode == TerrainNormalMode_Imported)
        ? FindAttribute(primitive, (const unsigned char*)u8"NORMAL") : NULL;
    for (cgltf_size Vertex = 0U; Vertex < Positions->count; Vertex++)
    {
        float Local[3] = { 0.0f, 0.0f, 0.0f };
        cgltf_accessor_read_float(Positions, Vertex, Local, 3U);
        Vector3Double Point = Matrix4Double_TransformPoint(world, (Vector3Double){ .X = (double)Local[0],
            .Y = (double)Local[1], .Z = (double)Local[2] });
        In->Positions[First + Vertex] = Point;
        if (Normals != NULL)
        {
            float N[3] = { 0.0f, 1.0f, 0.0f };
            cgltf_accessor_read_float(Normals, Vertex, N, 3U);
            double Turned[3];
            for (size_t Row = 0U; Row < 3U; Row++)
            {
                Turned[Row] = (NormalMatrix[Row][0] * (double)N[0]) + (NormalMatrix[Row][1] * (double)N[1])
                    + (NormalMatrix[Row][2] * (double)N[2]);
            }
            In->Normals[First + Vertex] = (Vector3){ (float)Turned[0], (float)Turned[1], (float)Turned[2] };
        }
    }
    for (size_t Index = 0U; Index < import->Options->WeightMappingCount; Index++)
    {
        const TerrainAttributeMapping* Mapping = &import->Options->Weights[Index];
        ReadMappedAttribute(FindAttribute(primitive, Mapping->Attribute), Mapping, First, In->Weights,
            TERRAIN_MAX_MATERIALS);
    }
    for (size_t Index = 0U; Index < import->Options->DensityMappingCount; Index++)
    {
        const TerrainAttributeMapping* Mapping = &import->Options->Densities[Index];
        ReadMappedAttribute(FindAttribute(primitive, Mapping->Attribute), Mapping, First, In->Densities,
            TERRAIN_MAX_DENSITY_CHANNELS);
    }
    In->VertexCount += (uint32_t)Positions->count;

    // A mirroring transform turns the winding: glTF's front is then clockwise, so two corners swap back.
    bool IsMirrored = Determinant < 0.0;
    cgltf_size Corners = (primitive->indices != NULL) ? primitive->indices->count : Positions->count;
    for (cgltf_size Triangle = 0U; Triangle < (Corners / 3U); Triangle++)
    {
        uint32_t Local[3];
        for (cgltf_size Corner = 0U; Corner < 3U; Corner++)
        {
            cgltf_size Source = (Triangle * 3U) + Corner;
            cgltf_size Index = (primitive->indices != NULL) ? cgltf_accessor_read_index(primitive->indices, Source)
                : Source;
            if (Index >= Positions->count)
            {
                char MeshName[NAME_PRINT_CAPACITY];
                return Fail(import, ErrorCode_InvalidAssetData, "mesh \"%s\" has a triangle naming vertex %" PRIu64 " of "
                    "%" PRIu64 ".", GltfChecks_Printable(mesh->name, MeshName, sizeof(MeshName)), (uint64_t)Index,
                    (uint64_t)Positions->count);
            }
            Local[Corner] = First + (uint32_t)Index;
        }
        uint32_t* Target = &In->Indices[(size_t)In->TriangleCount * 3U];
        Target[0] = Local[0];
        Target[1] = IsMirrored ? Local[2] : Local[1];
        Target[2] = IsMirrored ? Local[1] : Local[2];
        In->Flags[In->TriangleCount] = (Kind == SlotKind_Hole) ? TERRAIN_TRIANGLE_FLAG_HOLE
            : ((Kind == SlotKind_Material) ? TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL : 0U);
        In->Materials[In->TriangleCount] = SlotMaterial;
        In->TriangleCount++;
    }
    return Error_CreateSuccess();
}

/* Walks the default scene's node trees (a stack, MAX_NODE_DEPTH levels at most), each mesh through its node's world
 * transform: counting (@p isReading false) or reading. */
static Error WalkScene(Import* import, bool isReading)
{
    const cgltf_data* Data = import->Data;
    const cgltf_scene* Scene = (Data->scene != NULL) ? Data->scene : ((Data->scenes_count > 0U) ? &Data->scenes[0] : NULL);
    if (Scene == NULL)
    {
        return Fail(import, ErrorCode_InvalidAssetData, "the file has no scene.");
    }
    GenericBuffer Stack;
    GenericBuffer_AllocateVariable(&Stack, 16U, sizeof(NodeVisit));
    Error Result = Error_CreateSuccess();
    for (cgltf_size Root = Scene->nodes_count; Root > 0U; Root--)
    {
        NodeVisit Visit = { .Node = Scene->nodes[Root - 1U], .ParentWorld = Matrix4Double_Identity(), .Depth = 1U };
        GenericBuffer_AddLast(&Stack, &Visit);
    }
    while ((Stack._count > 0U) && (Result.Code == ErrorCode_Success))
    {
        NodeVisit Visit = ((const NodeVisit*)(const void*)Stack._data)[Stack._count - 1U];
        GenericBuffer_RemoveLast(&Stack);
        if (Visit.Depth > MAX_NODE_DEPTH)
        {
            Result = Fail(import, ErrorCode_InvalidAssetData, "its node tree is deeper than %u levels.", MAX_NODE_DEPTH);
            break;
        }
        Matrix4Double Local;
        Result = GetLocalTransform(import, Visit.Node, &Local);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        Matrix4Double World = Matrix4Double_Multiply(&Local, &Visit.ParentWorld);
        const cgltf_mesh* NodeMesh = Visit.Node->mesh;
        for (cgltf_size Primitive = 0U; (NodeMesh != NULL) && (Primitive < NodeMesh->primitives_count)
            && (Result.Code == ErrorCode_Success); Primitive++)
        {
            Result = isReading ? ReadPrimitive(import, &NodeMesh->primitives[Primitive], NodeMesh, &World)
                : CountPrimitive(import, &NodeMesh->primitives[Primitive], NodeMesh);
        }
        for (cgltf_size Child = Visit.Node->children_count; (Child > 0U) && (Result.Code == ErrorCode_Success); Child--)
        {
            NodeVisit Next = { .Node = Visit.Node->children[Child - 1U], .ParentWorld = World, .Depth = Visit.Depth + 1U };
            GenericBuffer_AddLast(&Stack, &Next);
        }
    }
    Memory_Free(Stack._data);
    return Result;
}


// Static functions: welding.
static uint32_t HashFloats(const float* values, size_t count)
{
    uint32_t Hash = 2166136261U;
    for (size_t Index = 0U; Index < count; Index++)
    {
        uint32_t Bits = 0U;
        float Value = (values[Index] == 0.0f) ? 0.0f : values[Index];
        Memory_Copy(&Value, &Bits, sizeof(Bits));
        Hash = (Hash ^ Bits) * 16777619U;
        Hash ^= Hash >> 15U;
    }
    return Hash;
}

/* The welded mesh: per input vertex its welded index; the welded positions, normals and the sums of weights and
 * densities, with how many input vertices each joined and whether they differed. */
typedef struct WeldStruct
{
    uint32_t* Remap;
    Vector3* Positions;
    Vector3* Normals;
    uint32_t* WeightSums;
    uint32_t* DensitySums;
    uint32_t* JoinCounts;
    uint8_t* FirstWeights;
    uint8_t* FirstDensities;
    bool* IsSeam;
    uint32_t Count;
} Weld;

static bool AreFloatKeysEqual(const float* a, const float* b, size_t count)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (a[Index] != b[Index])
        {
            return false;
        }
    }
    return true;
}

/* Welds the input's vertices: one per position on the grid (and normal, while imported). Two passes: the first finds
 * each input vertex's welded vertex (the table, the keys and the remap: memory per input vertex), the second fills the
 * welded vertices in input order (their first vertex's position and normal and weights, the sums, the seams: memory per
 * welded vertex, allocated once their count is known and allowed). */
static Error WeldVertices(Import* import, Weld* outWeld)
{
    const Input* In = &import->In;
    bool IsImported = import->Options->NormalMode == TerrainNormalMode_Imported;
    size_t KeySize = IsImported ? 6U : 3U;
    Weld Result;
    Memory_Zero(&Result, sizeof(Result));
    size_t Capacity = MIN_TABLE_CAPACITY;
    while (Capacity < ((size_t)In->VertexCount * 2U))
    {
        Capacity *= 2U;
    }
    uint32_t* Table = Memory_Allocate(Capacity * sizeof(uint32_t));
    for (size_t Slot = 0U; Slot < Capacity; Slot++)
    {
        Table[Slot] = TERRAIN_NONE;
    }
    float* Keys = Memory_Allocate((size_t)In->VertexCount * KeySize * sizeof(float));
    Result.Remap = Memory_Allocate((size_t)In->VertexCount * sizeof(uint32_t));
    Error Failure = Error_CreateSuccess();
    for (uint32_t Vertex = 0U; (Vertex < In->VertexCount) && (Failure.Code == ErrorCode_Success); Vertex++)
    {
        Vector3Double P = In->Positions[Vertex];
        if (!TerrainMeshInternal_IsInExtent(P.X) || !TerrainMeshInternal_IsInExtent(P.Y)
            || !TerrainMeshInternal_IsInExtent(P.Z))
        {
            Failure = Fail(import, ErrorCode_InvalidAssetData, "a vertex lands at (%g, %g, %g) through its node; "
                "positions must be finite and within %g m of the terrain's origin on every axis.", P.X, P.Y, P.Z,
                TERRAIN_MESH_EXTENT);
            break;
        }
        float* Key = &Keys[(size_t)Vertex * KeySize];
        Key[0] = TerrainMeshInternal_Snap(P.X);
        Key[1] = TerrainMeshInternal_Snap(P.Y);
        Key[2] = TerrainMeshInternal_Snap(P.Z);
        if (IsImported)
        {
            Key[3] = In->Normals[Vertex].x;
            Key[4] = In->Normals[Vertex].y;
            Key[5] = In->Normals[Vertex].z;
        }
        size_t Slot = HashFloats(Key, KeySize) & (Capacity - 1U);
        uint32_t Welded = TERRAIN_NONE;
        while (Table[Slot] != TERRAIN_NONE)
        {
            if (AreFloatKeysEqual(&Keys[(size_t)Table[Slot] * KeySize], Key, KeySize))
            {
                Welded = Result.Remap[Table[Slot]];
                break;
            }
            Slot = (Slot + 1U) & (Capacity - 1U);
        }
        if (Welded == TERRAIN_NONE)
        {
            Table[Slot] = Vertex;
            Welded = Result.Count++;
        }
        Result.Remap[Vertex] = Welded;
    }
    Memory_Free(Table);
    if ((Failure.Code == ErrorCode_Success) && (Result.Count > TERRAIN_MESH_MAX_VERTICES))
    {
        Failure = Fail(import, ErrorCode_InvalidAssetData, "it has %" PRIu32 " vertices once welded; a terrain has at "
            "most %" PRIu32 ".", Result.Count, TERRAIN_MESH_MAX_VERTICES);
    }
    if (Failure.Code != ErrorCode_Success)
    {
        Memory_Free(Keys);
        *outWeld = Result;
        return Failure;
    }
    size_t Vertices = (Result.Count > 0U) ? (size_t)Result.Count : 1U;
    Result.Positions = Memory_Allocate(Vertices * sizeof(Vector3));
    Result.Normals = Memory_Allocate(Vertices * sizeof(Vector3));
    Result.WeightSums = Memory_Allocate(Vertices * TERRAIN_MAX_MATERIALS * sizeof(uint32_t));
    Result.DensitySums = Memory_Allocate(Vertices * TERRAIN_MAX_DENSITY_CHANNELS * sizeof(uint32_t));
    Result.JoinCounts = Memory_Allocate(Vertices * sizeof(uint32_t));
    Result.FirstWeights = Memory_Allocate(Vertices * TERRAIN_MAX_MATERIALS);
    Result.FirstDensities = Memory_Allocate(Vertices * TERRAIN_MAX_DENSITY_CHANNELS);
    Result.IsSeam = Memory_Allocate(Vertices * sizeof(bool));
    Memory_Zero(Result.JoinCounts, Vertices * sizeof(uint32_t));
    for (uint32_t Vertex = 0U; Vertex < In->VertexCount; Vertex++)
    {
        uint32_t Welded = Result.Remap[Vertex];
        const float* Key = &Keys[(size_t)Vertex * KeySize];
        const uint8_t* Weights = &In->Weights[(size_t)Vertex * TERRAIN_MAX_MATERIALS];
        const uint8_t* Densities = &In->Densities[(size_t)Vertex * TERRAIN_MAX_DENSITY_CHANNELS];
        if (Result.JoinCounts[Welded] == 0U)
        {
            // The welded vertex's first input vertex.
            Result.Positions[Welded] = (Vector3){ Key[0], Key[1], Key[2] };
            Result.Normals[Welded] = IsImported ? In->Normals[Vertex] : (Vector3){ 0.0f, 1.0f, 0.0f };
            Result.IsSeam[Welded] = false;
            Memory_Zero(&Result.WeightSums[(size_t)Welded * TERRAIN_MAX_MATERIALS],
                TERRAIN_MAX_MATERIALS * sizeof(uint32_t));
            Memory_Zero(&Result.DensitySums[(size_t)Welded * TERRAIN_MAX_DENSITY_CHANNELS],
                TERRAIN_MAX_DENSITY_CHANNELS * sizeof(uint32_t));
            Memory_Copy(Weights, &Result.FirstWeights[(size_t)Welded * TERRAIN_MAX_MATERIALS], TERRAIN_MAX_MATERIALS);
            Memory_Copy(Densities, &Result.FirstDensities[(size_t)Welded * TERRAIN_MAX_DENSITY_CHANNELS],
                TERRAIN_MAX_DENSITY_CHANNELS);
        }
        else if (!Memory_IsEqual(Weights, &Result.FirstWeights[(size_t)Welded * TERRAIN_MAX_MATERIALS],
            TERRAIN_MAX_MATERIALS) || !Memory_IsEqual(Densities,
            &Result.FirstDensities[(size_t)Welded * TERRAIN_MAX_DENSITY_CHANNELS], TERRAIN_MAX_DENSITY_CHANNELS))
        {
            Result.IsSeam[Welded] = true;
        }
        Result.JoinCounts[Welded]++;
        for (size_t Channel = 0U; Channel < TERRAIN_MAX_MATERIALS; Channel++)
        {
            Result.WeightSums[((size_t)Welded * TERRAIN_MAX_MATERIALS) + Channel] += Weights[Channel];
        }
        for (size_t Channel = 0U; Channel < TERRAIN_MAX_DENSITY_CHANNELS; Channel++)
        {
            Result.DensitySums[((size_t)Welded * TERRAIN_MAX_DENSITY_CHANNELS) + Channel] += Densities[Channel];
        }
    }
    Memory_Free(Keys);
    *outWeld = Result;
    return Error_CreateSuccess();
}

static void DestroyWeld(Weld* weld)
{
    Memory_Free(weld->Remap);
    Memory_Free(weld->Positions);
    Memory_Free(weld->Normals);
    Memory_Free(weld->WeightSums);
    Memory_Free(weld->DensitySums);
    Memory_Free(weld->JoinCounts);
    Memory_Free(weld->FirstWeights);
    Memory_Free(weld->FirstDensities);
    Memory_Free(weld->IsSeam);
}

static bool ArePositionsEqual(Vector3 a, Vector3 b)
{
    return (a.x == b.x) && (a.y == b.y) && (a.z == b.z);
}

/* Builds the mesh from the welded vertices and the triangles that keep three positions. */
static Error BuildMesh(Import* import, const Weld* weld, TerrainMesh** outMesh)
{
    const Input* In = &import->In;
    uint8_t* Weights = Memory_Allocate((size_t)weld->Count * TERRAIN_MAX_MATERIALS);
    uint8_t* Densities = Memory_Allocate((size_t)weld->Count * TERRAIN_MAX_DENSITY_CHANNELS);
    uint32_t SeamCount = 0U;
    for (uint32_t Vertex = 0U; Vertex < weld->Count; Vertex++)
    {
        uint32_t Joined = weld->JoinCounts[Vertex];
        SeamCount += weld->IsSeam[Vertex] ? 1U : 0U;
        for (size_t Channel = 0U; Channel < TERRAIN_MAX_MATERIALS; Channel++)
        {
            uint32_t Sum = weld->WeightSums[((size_t)Vertex * TERRAIN_MAX_MATERIALS) + Channel];
            Weights[((size_t)Vertex * TERRAIN_MAX_MATERIALS) + Channel] = (uint8_t)((Sum + (Joined / 2U)) / Joined);
        }
        for (size_t Channel = 0U; Channel < TERRAIN_MAX_DENSITY_CHANNELS; Channel++)
        {
            uint32_t Sum = weld->DensitySums[((size_t)Vertex * TERRAIN_MAX_DENSITY_CHANNELS) + Channel];
            Densities[((size_t)Vertex * TERRAIN_MAX_DENSITY_CHANNELS) + Channel] =
                (uint8_t)((Sum + (Joined / 2U)) / Joined);
        }
    }
    uint32_t* Indices = Memory_Allocate(((size_t)In->TriangleCount + 1U) * 3U * sizeof(uint32_t));
    uint8_t* Flags = Memory_Allocate((size_t)In->TriangleCount + 1U);
    uint8_t* Materials = Memory_Allocate((size_t)In->TriangleCount + 1U);
    uint32_t Kept = 0U;
    for (uint32_t Triangle = 0U; Triangle < In->TriangleCount; Triangle++)
    {
        uint32_t A = weld->Remap[In->Indices[(size_t)Triangle * 3U]];
        uint32_t B = weld->Remap[In->Indices[((size_t)Triangle * 3U) + 1U]];
        uint32_t C = weld->Remap[In->Indices[((size_t)Triangle * 3U) + 2U]];
        if (ArePositionsEqual(weld->Positions[A], weld->Positions[B])
            || ArePositionsEqual(weld->Positions[B], weld->Positions[C])
            || ArePositionsEqual(weld->Positions[C], weld->Positions[A]))
        {
            continue;
        }
        Indices[(size_t)Kept * 3U] = A;
        Indices[((size_t)Kept * 3U) + 1U] = B;
        Indices[((size_t)Kept * 3U) + 2U] = C;
        Flags[Kept] = In->Flags[Triangle];
        Materials[Kept] = In->Materials[Triangle];
        Kept++;
    }
    if (SeamCount > 0U)
    {
        Warn(import, "%" PRIu32 " of its vertices are welded from vertices of the file with other weights or densities at "
            "the same position (a seam in the file); each takes their average.", SeamCount);
    }
    if (Kept < In->TriangleCount)
    {
        Warn(import, "%" PRIu32 " of its triangles are dropped: two of their corners lie at one position (on the %g m "
            "grid).", In->TriangleCount - Kept, TERRAIN_POSITION_QUANTUM);
    }
    TerrainMeshData Data =
    {
        .Positions = weld->Positions,
        .Normals = weld->Normals,
        .Weights = Weights,
        .Densities = Densities,
        .VertexCount = weld->Count,
        .Indices = Indices,
        .TriangleFlags = Flags,
        .TriangleMaterials = Materials,
        .TriangleCount = Kept,
        .MaterialCount = import->Options->MaterialCount,
        .DensityChannelCount = import->Options->DensityChannelCount,
        .NormalMode = import->Options->NormalMode,
        .ChunkSize = import->Options->ChunkSize
    };
    Error Result = (Kept == 0U)
        ? Fail(import, ErrorCode_InvalidAssetData, "it has no triangle with three corners at different positions.")
        : TerrainMesh_Create(&Data, (const unsigned char*)import->Description, outMesh);
    Memory_Free(Weights);
    Memory_Free(Densities);
    Memory_Free(Indices);
    Memory_Free(Flags);
    Memory_Free(Materials);
    return Result;
}

static void AllocateInput(Import* import)
{
    Input* In = &import->In;
    size_t Vertices = (size_t)((import->VertexCount > 0U) ? import->VertexCount : 1U);
    size_t Triangles = (size_t)((import->TriangleCount > 0U) ? import->TriangleCount : 1U);
    In->Positions = Memory_Allocate(Vertices * sizeof(Vector3Double));
    // Normals are read only when they are imported.
    In->Normals = (import->Options->NormalMode == TerrainNormalMode_Imported)
        ? Memory_Allocate(Vertices * sizeof(Vector3)) : NULL;
    In->Weights = Memory_Allocate(Vertices * TERRAIN_MAX_MATERIALS);
    In->Densities = Memory_Allocate(Vertices * TERRAIN_MAX_DENSITY_CHANNELS);
    Memory_Zero(In->Weights, Vertices * TERRAIN_MAX_MATERIALS);
    Memory_Zero(In->Densities, Vertices * TERRAIN_MAX_DENSITY_CHANNELS);
    In->Indices = Memory_Allocate(Triangles * 3U * sizeof(uint32_t));
    In->Flags = Memory_Allocate(Triangles);
    In->Materials = Memory_Allocate(Triangles);
}

static void DestroyImport(Import* import)
{
    if (import->Data != NULL)
    {
        cgltf_free(import->Data);
    }
    Memory_Free(import->In.Positions);
    Memory_Free(import->In.Normals);
    Memory_Free(import->In.Weights);
    Memory_Free(import->In.Densities);
    Memory_Free(import->In.Indices);
    Memory_Free(import->In.Flags);
    Memory_Free(import->In.Materials);
}


// Public functions.
Error TerrainMesh_CreateFromGltf(const void* data, size_t byteCount, const TerrainGltfOptions* options,
    TerrainMesh** outMesh)
{
    if ((outMesh == NULL) || (options == NULL) || ((data == NULL) && (byteCount > 0U)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TerrainMesh_CreateFromGltf: outMesh, options and the data must not be NULL.");
    }
    *outMesh = NULL;
    Import Self;
    Memory_Zero(&Self, sizeof(Self));
    Self.Options = options;
    Self.BlendSlotName = (options->BlendSlotName != NULL) ? options->BlendSlotName : DEFAULT_BLEND_SLOT_NAME;
    char Name[NAME_PRINT_CAPACITY];
    snprintf(Self.Description, sizeof(Self.Description), "%s", (options->Description != NULL)
        ? GltfChecks_Printable((const char*)options->Description, Name, sizeof(Name)) : "The terrain");
    Error Result = CheckOptions(&Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (byteCount == 0U)
    {
        return Fail(&Self, ErrorCode_InvalidAssetData, "the file is empty.");
    }
    if (byteCount > TERRAIN_GLTF_MAX_FILE_BYTES)
    {
        return Fail(&Self, ErrorCode_BufferTooLarge, "the file has %" PRIu64 " bytes; at most %" PRIu64 " are read.",
            (uint64_t)byteCount, (uint64_t)TERRAIN_GLTF_MAX_FILE_BYTES);
    }
    GltfReadContext Context = { .Description = Self.Description, .MaxBytes = TERRAIN_GLTF_MAX_FILE_BYTES,
        .FilePath = options->FilePath };
    Result = GltfChecks_Parse(&Context, data, byteCount, &Self.Data);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WalkScene(&Self, false);
    }
    if ((Result.Code == ErrorCode_Success) && (Self.TriangleCount == 0U))
    {
        Result = Fail(&Self, ErrorCode_InvalidAssetData, "its default scene holds no triangles.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        AllocateInput(&Self);
        Result = WalkScene(&Self, true);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Weld Welded;
        Result = WeldVertices(&Self, &Welded);
        if (Result.Code == ErrorCode_Success)
        {
            Result = BuildMesh(&Self, &Welded, outMesh);
        }
        DestroyWeld(&Welded);
    }
    DestroyImport(&Self);
    return Result;
}

Error TerrainMesh_CreateFromGltfFile(const unsigned char* path, const TerrainGltfOptions* options,
    TerrainMesh** outMesh)
{
    if ((path == NULL) || (options == NULL) || (outMesh == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TerrainMesh_CreateFromGltfFile: path, options and outMesh must not be NULL.");
    }
    *outMesh = NULL;
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    Error Result = GltfChecks_ReadLimitedFile(path, TERRAIN_GLTF_MAX_FILE_BYTES, &Bytes);
    if (Result.Code == ErrorCode_Success)
    {
        TerrainGltfOptions Options = *options;
        if (Options.FilePath == NULL)
        {
            Options.FilePath = path;
        }
        Result = TerrainMesh_CreateFromGltf(Bytes._data, Bytes._count, &Options, outMesh);
    }
    Memory_Free(Bytes._data);
    return Result;
}
