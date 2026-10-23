#include "TestAnimation.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "raylib/raymath.h"


// Macros.
/** Room for one formatted piece of JSON. */
#define PIECE_CAPACITY 1024
/** raylib's vertex buffer slots for the bone indices and weights (GameSkeleton.c counts a mesh with ids there as
 *  uploaded), and how many slots a GPU-skinning raylib gives a mesh. */
#define FAKE_BONE_INDEX_SLOT 7
#define FAKE_BONE_WEIGHT_SLOT 8
#define FAKE_VERTEX_BUFFER_COUNT 9


// Fields.
static const char Base64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


// Static functions.
static void Append(GenericBuffer* out, const char* format, ...)
{
    char Piece[PIECE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Piece, sizeof(Piece), format, Arguments);
    va_end(Arguments);
    if (Length > 0)
    {
        GenericBuffer_AppendString(out, (const unsigned char*)Piece);
    }
}

static void AppendBase64(GenericBuffer* out, const unsigned char* bytes, size_t count)
{
    for (size_t Index = 0U; Index < count; Index += 3U)
    {
        uint32_t Group = (uint32_t)bytes[Index] << 16U;
        Group |= (Index + 1U < count) ? ((uint32_t)bytes[Index + 1U] << 8U) : 0U;
        Group |= (Index + 2U < count) ? (uint32_t)bytes[Index + 2U] : 0U;
        char Quad[5] = { Base64Alphabet[(Group >> 18U) & 63U], Base64Alphabet[(Group >> 12U) & 63U],
            (Index + 1U < count) ? Base64Alphabet[(Group >> 6U) & 63U] : '=',
            (Index + 2U < count) ? Base64Alphabet[Group & 63U] : '=', '\0' };
        GenericBuffer_AppendString(out, (const unsigned char*)Quad);
    }
}

static void AppendBytes(GenericBuffer* binary, const void* bytes, size_t count)
{
    while ((binary->_count % 4U) != 0U)
    {
        GenericBuffer_AppendByte(binary, 0U);
    }
    // The append only reads the bytes; its parameter is not const.
    GenericBuffer_AppendRangeBytes(binary, (unsigned char*)bytes, count);
}

static void AppendNode(GenericBuffer* out, const TestGltfNode* nodes, uint32_t nodeCount, uint32_t index)
{
    const TestGltfNode* Node = &nodes[index];
    Append(out, "%s{", (index > 0U) ? "," : "");
    if (Node->Name != NULL)
    {
        Append(out, "\"name\":\"%s\",", Node->Name);
    }
    if (Node->HasMatrix)
    {
        const Matrix* M = &Node->Matrix;
        Append(out, "\"matrix\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]",
            (double)M->m0, (double)M->m1, (double)M->m2, (double)M->m3, (double)M->m4, (double)M->m5, (double)M->m6,
            (double)M->m7, (double)M->m8, (double)M->m9, (double)M->m10, (double)M->m11, (double)M->m12,
            (double)M->m13, (double)M->m14, (double)M->m15);
    }
    else
    {
        const Transform* R = &Node->Rest;
        Append(out, "\"translation\":[%.9g,%.9g,%.9g],\"rotation\":[%.9g,%.9g,%.9g,%.9g],\"scale\":[%.9g,%.9g,%.9g]",
            (double)R->translation.x, (double)R->translation.y, (double)R->translation.z, (double)R->rotation.x,
            (double)R->rotation.y, (double)R->rotation.z, (double)R->rotation.w, (double)R->scale.x,
            (double)R->scale.y, (double)R->scale.z);
    }
    bool HasChildren = false;
    for (uint32_t Child = 0U; Child < nodeCount; Child++)
    {
        if (nodes[Child].Parent == (int)index)
        {
            Append(out, "%s%u", HasChildren ? "," : ",\"children\":[", Child);
            HasChildren = true;
        }
    }
    Append(out, "%s}", HasChildren ? "]" : "");
}


// Public functions.
Transform TestAnimation_Identity(void)
{
    return (Transform){ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };
}

Transform TestAnimation_Translation(float x, float y, float z)
{
    Transform Result = TestAnimation_Identity();
    Result.translation = (Vector3){ x, y, z };
    return Result;
}

/* Writes the file: with the buffer as a data URI, or naming @p bufferUri with its bytes in @p outBinary. */
static void WriteGltf(const TestGltfNode* nodes, uint32_t nodeCount, const TestGltfClip* clips, uint32_t clipCount,
    const char* bufferUri, GenericBuffer* out, GenericBuffer* outBinary)
{
    GenericBuffer_AllocateVariable(out, 4096U, sizeof(unsigned char));
    GenericBuffer Binary;
    GenericBuffer_AllocateVariable(&Binary, 1024U, sizeof(unsigned char));
    GenericBuffer Views;
    GenericBuffer_AllocateVariable(&Views, 1024U, sizeof(unsigned char));
    GenericBuffer Accessors;
    GenericBuffer_AllocateVariable(&Accessors, 1024U, sizeof(unsigned char));
    GenericBuffer Animations;
    GenericBuffer_AllocateVariable(&Animations, 1024U, sizeof(unsigned char));

    uint32_t AccessorCount = 0U;
    for (uint32_t ClipIndex = 0U; ClipIndex < clipCount; ClipIndex++)
    {
        const TestGltfClip* Clip = &clips[ClipIndex];
        Append(&Animations, "%s{", (ClipIndex > 0U) ? "," : "");
        if (Clip->Name != NULL)
        {
            Append(&Animations, "\"name\":\"%s\",", Clip->Name);
        }
        GenericBuffer Samplers;
        GenericBuffer_AllocateVariable(&Samplers, 256U, sizeof(unsigned char));
        GenericBuffer Channels;
        GenericBuffer_AllocateVariable(&Channels, 256U, sizeof(unsigned char));
        for (uint32_t ChannelIndex = 0U; ChannelIndex < Clip->ChannelCount; ChannelIndex++)
        {
            const TestGltfChannel* Channel = &Clip->Channels[ChannelIndex];
            uint32_t PerKey = (strcmp(Channel->Interpolation, "CUBICSPLINE") == 0) ? 3U : 1U;
            uint32_t ValueCount = Channel->KeyCount * PerKey;

            size_t TimesOffset = (Binary._count + 3U) & ~(size_t)3U;
            AppendBytes(&Binary, Channel->Times, Channel->KeyCount * sizeof(float));
            float First = (Channel->KeyCount > 0U) ? Channel->Times[0] : 0.0f;
            float Last = (Channel->KeyCount > 0U) ? Channel->Times[Channel->KeyCount - 1U] : 0.0f;
            Append(&Views, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", (AccessorCount > 0U) ? "," : "",
                TimesOffset, (size_t)Channel->KeyCount * sizeof(float));
            Append(&Accessors, "%s{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,\"type\":\"SCALAR\","
                "\"min\":[%.9g],\"max\":[%.9g]}", (AccessorCount > 0U) ? "," : "", AccessorCount, Channel->KeyCount,
                (double)First, (double)Last);
            uint32_t TimesAccessor = AccessorCount++;

            size_t ValuesOffset = (Binary._count + 3U) & ~(size_t)3U;
            size_t FloatCount = (size_t)ValueCount * Channel->ValueComponents;
            size_t ValueBytes = 0U;
            if (Channel->AreValuesShorts)
            {
                int16_t* Shorts = Memory_Allocate((FloatCount > 0U) ? FloatCount * sizeof(int16_t) : 1U);
                for (size_t Index = 0U; Index < FloatCount; Index++)
                {
                    Shorts[Index] = (int16_t)lroundf(Channel->Values[Index] * 32767.0f);
                }
                ValueBytes = FloatCount * sizeof(int16_t);
                AppendBytes(&Binary, Shorts, ValueBytes);
                Memory_Free(Shorts);
            }
            else
            {
                ValueBytes = FloatCount * sizeof(float);
                AppendBytes(&Binary, Channel->Values, ValueBytes);
            }
            const char* Type = (Channel->ValueComponents == 4U) ? "VEC4"
                : ((Channel->ValueComponents == 3U) ? "VEC3" : "SCALAR");
            Append(&Views, ",{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", ValuesOffset, ValueBytes);
            Append(&Accessors, ",{\"bufferView\":%u,\"componentType\":%d,%s\"count\":%u,\"type\":\"%s\"}",
                AccessorCount, Channel->AreValuesShorts ? 5122 : 5126,
                Channel->AreValuesShorts ? "\"normalized\":true," : "", ValueCount, Type);
            uint32_t ValuesAccessor = AccessorCount++;

            Append(&Samplers, "%s{\"input\":%u,\"output\":%u,\"interpolation\":\"%s\"}", (ChannelIndex > 0U) ? "," : "",
                TimesAccessor, ValuesAccessor, Channel->Interpolation);
            if (Channel->Node >= 0)
            {
                Append(&Channels, "%s{\"sampler\":%u,\"target\":{\"node\":%d,\"path\":\"%s\"}}",
                    (ChannelIndex > 0U) ? "," : "", ChannelIndex, Channel->Node, Channel->Path);
            }
            else
            {
                Append(&Channels, "%s{\"sampler\":%u,\"target\":{\"path\":\"%s\"}}", (ChannelIndex > 0U) ? "," : "",
                    ChannelIndex, Channel->Path);
            }
        }
        GenericBuffer_NullTerminate(&Samplers);
        GenericBuffer_NullTerminate(&Channels);
        Append(&Animations, "\"samplers\":[");
        GenericBuffer_AppendString(&Animations, Samplers._data);
        Append(&Animations, "],\"channels\":[");
        GenericBuffer_AppendString(&Animations, Channels._data);
        Append(&Animations, "]}");
        Memory_Free(Samplers._data);
        Memory_Free(Channels._data);
    }

    Append(out, "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[");
    bool HasRoot = false;
    for (uint32_t Index = 0U; Index < nodeCount; Index++)
    {
        if (nodes[Index].Parent < 0)
        {
            Append(out, "%s%u", HasRoot ? "," : "", Index);
            HasRoot = true;
        }
    }
    Append(out, "]}],\"nodes\":[");
    for (uint32_t Index = 0U; Index < nodeCount; Index++)
    {
        AppendNode(out, nodes, nodeCount, Index);
    }
    Append(out, "]");
    while ((Binary._count % 4U) != 0U)
    {
        GenericBuffer_AppendByte(&Binary, 0U);
    }
    if (AccessorCount > 0U)
    {
        if (bufferUri != NULL)
        {
            Append(out, ",\"buffers\":[{\"byteLength\":%zu,\"uri\":\"%s", Binary._count, bufferUri);
        }
        else
        {
            Append(out, ",\"buffers\":[{\"byteLength\":%zu,\"uri\":\"data:application/octet-stream;base64,",
                Binary._count);
            AppendBase64(out, Binary._data, Binary._count);
        }
        Append(out, "\"}],\"bufferViews\":[");
        GenericBuffer_NullTerminate(&Views);
        GenericBuffer_AppendString(out, Views._data);
        Append(out, "],\"accessors\":[");
        GenericBuffer_NullTerminate(&Accessors);
        GenericBuffer_AppendString(out, Accessors._data);
        Append(out, "],\"animations\":[");
        GenericBuffer_NullTerminate(&Animations);
        GenericBuffer_AppendString(out, Animations._data);
        Append(out, "]");
    }
    Append(out, "}");
    GenericBuffer_NullTerminate(out);
    if (outBinary != NULL)
    {
        *outBinary = Binary;
    }
    else
    {
        Memory_Free(Binary._data);
    }
    Memory_Free(Views._data);
    Memory_Free(Accessors._data);
    Memory_Free(Animations._data);
}

void TestGltf_Write(const TestGltfNode* nodes, uint32_t nodeCount, const TestGltfClip* clips, uint32_t clipCount,
    GenericBuffer* out)
{
    WriteGltf(nodes, nodeCount, clips, clipCount, NULL, out, NULL);
}

void TestGltf_WriteWithBufferFile(const TestGltfNode* nodes, uint32_t nodeCount, const TestGltfClip* clips,
    uint32_t clipCount, const char* bufferUri, GenericBuffer* out, GenericBuffer* outBinary)
{
    WriteGltf(nodes, nodeCount, clips, clipCount, bufferUri, out, outBinary);
}

bool TestGltf_Replace(GenericBuffer* text, const char* from, const char* to)
{
    const char* Text = (const char*)text->_data;
    const char* Found = strstr(Text, from);
    if (Found == NULL)
    {
        return false;
    }
    size_t Before = (size_t)(Found - Text);
    size_t FromLength = strlen(from);
    GenericBuffer Result;
    GenericBuffer_AllocateVariable(&Result, text->_count + strlen(to), sizeof(unsigned char));
    GenericBuffer_AppendRangeBytes(&Result, text->_data, Before);
    GenericBuffer_AppendString(&Result, (const unsigned char*)to);
    GenericBuffer_AppendString(&Result, (const unsigned char*)(Found + FromLength));
    GenericBuffer_NullTerminate(&Result);
    Memory_Free(text->_data);
    *text = Result;
    return true;
}

Model TestSkeletonModel_Create(const TestSkeletonBone* bones, int boneCount, float boxSize)
{
    Model Result;
    Memory_Zero(&Result, sizeof(Result));
    Result.transform = MatrixIdentity();
    Result.skeleton.boneCount = boneCount;
    Result.skeleton.bones = Memory_Allocate((size_t)boneCount * sizeof(BoneInfo));
    Result.skeleton.bindPose = Memory_Allocate((size_t)boneCount * sizeof(Transform));
    Memory_Zero(Result.skeleton.bones, (size_t)boneCount * sizeof(BoneInfo));
    for (int Index = 0; Index < boneCount; Index++)
    {
        snprintf(Result.skeleton.bones[Index].name, sizeof(Result.skeleton.bones[Index].name), "%s", bones[Index].Name);
        Result.skeleton.bones[Index].parent = bones[Index].Parent;
        Result.skeleton.bindPose[Index] = bones[Index].Bind;
    }

    Mesh* Built = Memory_Allocate(sizeof(Mesh));
    Memory_Zero(Built, sizeof(*Built));
    int VertexCount = boneCount * 8;
    Built->vertexCount = VertexCount;
    Built->vertices = Memory_Allocate((size_t)VertexCount * 3U * sizeof(float));
    Built->boneIndices = Memory_Allocate((size_t)VertexCount * 4U);
    Built->boneWeights = Memory_Allocate((size_t)VertexCount * 4U * sizeof(float));
    Memory_Zero(Built->boneIndices, (size_t)VertexCount * 4U);
    Memory_Zero(Built->boneWeights, (size_t)VertexCount * 4U * sizeof(float));
    float Half = boxSize * 0.25f;
    for (int Bone = 0; Bone < boneCount; Bone++)
    {
        Vector3 Center = bones[Bone].Bind.translation;
        for (int Corner = 0; Corner < 8; Corner++)
        {
            int Vertex = (Bone * 8) + Corner;
            Built->vertices[(Vertex * 3) + 0] = Center.x + (((Corner & 1) != 0) ? Half : -Half);
            Built->vertices[(Vertex * 3) + 1] = Center.y + (((Corner & 2) != 0) ? Half : -Half);
            Built->vertices[(Vertex * 3) + 2] = Center.z + (((Corner & 4) != 0) ? Half : -Half);
            Built->boneIndices[Vertex * 4] = (unsigned char)Bone;
            Built->boneWeights[Vertex * 4] = 1.0f;
        }
    }
    Built->vboId = Memory_Allocate(FAKE_VERTEX_BUFFER_COUNT * sizeof(unsigned int));
    Memory_Zero(Built->vboId, FAKE_VERTEX_BUFFER_COUNT * sizeof(unsigned int));
    Built->vboId[FAKE_BONE_INDEX_SLOT] = 1U;
    Built->vboId[FAKE_BONE_WEIGHT_SLOT] = 2U;
    Result.meshes = Built;
    Result.meshCount = 1;
    Result.meshMaterial = Memory_Allocate(sizeof(int));
    Result.meshMaterial[0] = 0;
    return Result;
}

void TestSkeletonModel_Destroy(Model* model)
{
    for (int Index = 0; Index < model->meshCount; Index++)
    {
        Memory_Free(model->meshes[Index].vertices);
        Memory_Free(model->meshes[Index].animVertices);
        Memory_Free(model->meshes[Index].boneIndices);
        Memory_Free(model->meshes[Index].boneWeights);
        Memory_Free(model->meshes[Index].vboId);
    }
    Memory_Free(model->meshes);
    Memory_Free(model->meshMaterial);
    Memory_Free(model->skeleton.bones);
    Memory_Free(model->skeleton.bindPose);
    Memory_Zero(model, sizeof(*model));
}
