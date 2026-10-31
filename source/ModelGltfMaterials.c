#include <inttypes.h>
#include <math.h>
#include "ModelGltfMaterials.h"
#include "GltfChecks.h"
#include "raylib/cgltf.h"
#include "wr/WRMemory.h"


// Macros.
/** Room for a name from the file in printable form. */
#define NAME_PRINT_CAPACITY ((size_t)129)


// Static functions.
/* Whether @p value is in [0, 1] (NaN is not). */
static bool IsUnitValue(float value)
{
    return (value >= 0.0f) && (value <= 1.0f);
}

/* A value clamped to [0, 1], NaN as 0. */
static float ClampUnit(float value)
{
    return (value > 0.0f) ? ((value < 1.0f) ? value : 1.0f) : 0.0f;
}

/* A value in [0, 1] as the nearest byte. */
static unsigned char ToByte(float unit)
{
    return (unsigned char)((unit * 255.0f) + 0.5f);
}

/* Whether a material has textures raylib's importer leaves out: it reads a material's textures only inside its
 * pbrMetallicRoughness block. */
static bool HasSkippedTextures(const cgltf_material* material)
{
    if (material->has_pbr_metallic_roughness)
    {
        return false;
    }
    return (material->normal_texture.texture != NULL) || (material->occlusion_texture.texture != NULL)
        || (material->emissive_texture.texture != NULL)
        || (material->pbr_specular_glossiness.diffuse_texture.texture != NULL)
        || (material->pbr_specular_glossiness.specular_glossiness_texture.texture != NULL);
}

/* The first value of a material that breaks glTF's ranges (factors and the occlusion strength in [0, 1], the emission's
 * strength finite and at least 0), named as the file writes it; NULL when there is none. */
static const char* FindValueOutOfRange(const cgltf_material* material)
{
    const cgltf_pbr_metallic_roughness* Pbr = &material->pbr_metallic_roughness;
    for (size_t Channel = 0U; Channel < 4U; Channel++)
    {
        if (!IsUnitValue(Pbr->base_color_factor[Channel]))
        {
            return "baseColorFactor";
        }
    }
    if (!IsUnitValue(Pbr->metallic_factor))
    {
        return "metallicFactor";
    }
    if (!IsUnitValue(Pbr->roughness_factor))
    {
        return "roughnessFactor";
    }
    for (size_t Channel = 0U; Channel < 3U; Channel++)
    {
        if (!IsUnitValue(material->emissive_factor[Channel]))
        {
            return "emissiveFactor";
        }
    }
    if ((material->occlusion_texture.texture != NULL) && !IsUnitValue(material->occlusion_texture.scale))
    {
        return "occlusionTexture.strength";
    }
    if (material->has_emissive_strength && !(isfinite(material->emissive_strength.emissive_strength)
        && (material->emissive_strength.emissive_strength >= 0.0f)))
    {
        return "KHR_materials_emissive_strength.emissiveStrength";
    }
    return NULL;
}

/* Copies every material of parsed, checked data into @p outMaterials (one allocation), refusing a value out of range. */
static Error CopyMaterials(const char* description, const cgltf_data* data, ModelGltfMaterials* outMaterials)
{
    if (data->materials_count == 0U)
    {
        return Error_CreateSuccess();
    }
    size_t ByteCount = 0U;
    if (!Memory_TryMultiplySize(data->materials_count, sizeof(ModelGltfMaterial), &ByteCount))
    {
        return GltfChecks_Fail(description, ErrorCode_BufferTooLarge, "it lists too many materials.");
    }
    outMaterials->Materials = Memory_Allocate(ByteCount);
    outMaterials->Count = data->materials_count;
    for (size_t Index = 0U; Index < data->materials_count; Index++)
    {
        const cgltf_material* Source = &data->materials[Index];
        const char* Broken = FindValueOutOfRange(Source);
        if (Broken != NULL)
        {
            char Name[NAME_PRINT_CAPACITY];
            return GltfChecks_Fail(description, ErrorCode_InvalidAssetData,
                "material %" PRIu64 " (\"%s\"): its %s is outside glTF's range.", (uint64_t)Index,
                GltfChecks_Printable(Source->name, Name, sizeof(Name)), Broken);
        }
        ModelGltfMaterial* Target = &outMaterials->Materials[Index];
        Memory_Zero(Target, sizeof(*Target));
        for (size_t Channel = 0U; Channel < 4U; Channel++)
        {
            Target->BaseColor[Channel] = Source->pbr_metallic_roughness.base_color_factor[Channel];
        }
        Target->Metallic = Source->pbr_metallic_roughness.metallic_factor;
        Target->Roughness = Source->pbr_metallic_roughness.roughness_factor;
        for (size_t Channel = 0U; Channel < 3U; Channel++)
        {
            Target->Emissive[Channel] = Source->emissive_factor[Channel];
        }
        // cgltf leaves the strength 0 for a material without the extension and the scale 0 without an occlusion
        // texture: glTF's default is 1 for both.
        Target->EmissiveStrength = Source->has_emissive_strength ? Source->emissive_strength.emissive_strength : 1.0f;
        Target->OcclusionStrength = (Source->occlusion_texture.texture != NULL) ? Source->occlusion_texture.scale : 1.0f;
        Target->HasSkippedTextures = HasSkippedTextures(Source);
    }
    return Error_CreateSuccess();
}


// Public functions.
Error ModelGltfMaterials_ReadFile(const unsigned char* path, const char* description, ModelGltfMaterials* outMaterials)
{
    Memory_Zero(outMaterials, sizeof(*outMaterials));
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    Error Result = GltfChecks_ReadLimitedFile(path, MODEL_GLTF_MAX_FILE_BYTES, &Bytes);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ModelGltfMaterials_Read(Bytes._data, Bytes._count, description, path, outMaterials);
    }
    Memory_Free(Bytes._data);
    return Result;
}

Error ModelGltfMaterials_Read(const void* data, size_t byteCount, const char* description,
    const unsigned char* filePath, ModelGltfMaterials* outMaterials)
{
    Memory_Zero(outMaterials, sizeof(*outMaterials));
    GltfReadContext Context = { .Description = description, .MaxBytes = MODEL_GLTF_MAX_FILE_BYTES, .FilePath = filePath };
    cgltf_data* Parsed = NULL;
    Error Result = GltfChecks_Parse(&Context, data, byteCount, &Parsed);
    if (Result.Code == ErrorCode_Success)
    {
        Result = CopyMaterials(description, Parsed, outMaterials);
    }
    if (Parsed != NULL)
    {
        cgltf_free(Parsed);
    }
    if (Result.Code != ErrorCode_Success)
    {
        ModelGltfMaterials_Deconstruct(outMaterials);
    }
    return Result;
}

void ModelGltfMaterials_Deconstruct(ModelGltfMaterials* self)
{
    Memory_Free(self->Materials);
    self->Materials = NULL;
    self->Count = 0U;
}

Color ModelGltfMaterials_ToColor(const float linear[3], float alpha)
{
    float Encoded[3];
    for (size_t Channel = 0U; Channel < 3U; Channel++)
    {
        Encoded[Channel] = powf(ClampUnit(linear[Channel]), 1.0f / MODEL_GLTF_COLOR_GAMMA);
    }
    return (Color){ .r = ToByte(Encoded[0]), .g = ToByte(Encoded[1]), .b = ToByte(Encoded[2]),
        .a = ToByte(ClampUnit(alpha)) };
}
