#include <inttypes.h>
#include <stdio.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "GameModel.h"
#include "Logger.h"
#include "ModelGltfMaterials.h"
#include "ModelOrmMap.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"
#include "wr/WRNumber.h"
#include "wr/WRJSON.h"

/*
 * Static-model loader: loads meshes + materials, packs a glTF file's occlusion, roughness and metallic maps into the
 * one map the PBR shader reads, bakes an optional import transform into the model's transform matrix, seeds sensible
 * PBR scalar defaults, and applies optional material overrides (rebinding a slot's albedo / normal / ORM
 * (occlusion-roughness-metallic) / emissive textures and its tint + metallic / roughness / emissive scalar factors). A
 * model with bones also gets its skeleton (GameSkeleton.h); the clips it plays are animation library assets of their
 * own. Material addressing by name is out of scope for now (slots are addressed by index).
 *
 * GLTF MATERIALS. The loader reads a glTF file's materials itself first (ModelGltfMaterials.h: checked as hostile, so
 * raylib's importer, which trusts every number, only reads files that passed), then lets raylib load it. raylib puts the
 * file's material i in slot i + 1 (slot 0 is its default material, which no mesh of the file uses). It splits a
 * metallicRoughness texture into a grey roughness texture (ROUGHNESS) and a grey metallic texture (METALNESS) and keeps
 * the occlusion texture apart (OCCLUSION), while the PBR shader reads one packed map from the METALNESS slot (red
 * occlusion, green roughness, blue metallic). So the loader reads the three back and packs them (ModelOrmMap.h, with
 * the file's occlusion strength), then gives the materials the file's factors raylib drops (its colours sRGB-encoded,
 * the emission's strength as its intensity), all before the overrides apply: an override still wins.
 *
 * TEXTURE PROPERTIES. A definition's "texture_properties" (by default trilinear over mipmaps, repeating) apply to every
 * map the model file brings and to the packed maps; an override's apply to the maps it loads, the keys it leaves out
 * taking the model's. The file's own samplers are not read.
 *
 * TEXTURE OWNERSHIP. raylib's UnloadModel only unloads the meshes and frees each material's map array; it
 * never unloads textures or shaders. The loaded asset therefore lists the GPU texture ids it owns: every
 * texture the importer loaded (OBJ/MTL: up to 4 per material, glTF: up to 6), every packed map and every override
 * texture, skipping raylib's default texture and duplicates. Importer textures a packed map or an override replaced are
 * unloaded as soon as the overrides are applied. Unloading the asset, and every failed load, unloads the listed textures
 * and then calls UnloadModel. UnloadMaterial is never used: it unloads each material's textures on their own
 * (a texture shared by two materials would be unloaded twice) and a material shader the asset never owns.
 */


// Macros.
/** Default PBR scalar factors applied to every loaded material so un-overridden slots shade sensibly through
 *  the renderer's per-material path. Only degenerate (<= 0) roughness/occlusion values are replaced, so an
 *  importer that already authored PBR factors (e.g. GLTF) keeps them; a bare OBJ (which leaves them 0) gets
 *  these. Metallic 0 (dielectric) is a fine default and is left as the importer set it. */
#define MODEL_DEFAULT_ROUGHNESS 0.5f
#define MODEL_DEFAULT_AO 1.0f
/** First and last material map scanned for texture ids (the full MaterialMapIndex range; raylib does not
 *  export MAX_MATERIAL_MAPS). */
#define MODEL_FIRST_MATERIAL_MAP MATERIAL_MAP_ALBEDO
#define MODEL_LAST_MATERIAL_MAP MATERIAL_MAP_BRDF
/** What this loader describes, for parse-time messages. */
#define MODEL_ASSET_KIND ((const unsigned char*)u8"model")
/** What a material override map points at, for parse-time messages. */
#define MODEL_TEXTURE_KIND ((const unsigned char*)u8"material texture")
/** Room for a model's name made printable in load messages (AssetJSON_CopyPrintable), its NUL included. */
#define MODEL_PRINTABLE_NAME_CAPACITY ((size_t)129)
/** The maps a packed occlusion/roughness/metallic map is built from. */
#define MODEL_ORM_SOURCE_COUNT ((size_t)3)


// Types.
typedef struct ModelMaterialOverrideStruct
{
    int32_t Slot;
    bool HasAlbedo;
    OwnedAssetLocation Albedo;   // base colour texture
    bool HasNormal;
    OwnedAssetLocation Normal;   // tangent-space normal map
    bool HasMra;
    OwnedAssetLocation Mra;      // packed occlusion(R)/roughness(G)/metallic(B) map
    bool HasEmissiveMap;
    OwnedAssetLocation EmissiveMap;
    GameMaterialFactors Factors;  // tint, metallic, roughness, emissive colour and intensity (the shared parser)
    TextureProperties Properties; // filter, wrap and mipmaps of every map this override loads (keys it leaves out: the
                                  // model's)
} ModelMaterialOverride;

typedef struct ModelDefinitionStruct
{
    AssetDefinition Base;
    OwnedAssetLocation Location;
    unsigned char* Format; // owned; validated extension: narrows the file choice, names a reference's type
    bool HasTransform;
    Matrix Transform;
    TextureProperties Properties; // filter, wrap and mipmaps of the model's maps: its file's own and the packed ones
    ModelMaterialOverride* Overrides; // owned array
    size_t OverrideCount;
} ModelDefinition;

typedef struct ModelLoadedStruct
{
    GameModel Model;                // Asset points here; meshes + material map arrays freed by UnloadModel
    GenericBuffer* OwnedTextureIds; // borrowed from the manager pool (unsigned int elements); textures to unload
} ModelLoaded;


// Fields.
/** A model's texture properties where its definition gives none: smooth at every distance (trilinear over mipmaps). */
static const TextureProperties ModelDefaultTextureProperties =
{
    .Filter = TEXTURE_FILTER_TRILINEAR, .Wrap = TEXTURE_WRAP_REPEAT, .AreMipmapsEnabled = true
};
/** The material maps a packed map is built from, in ModelOrmSources' order, and their names for messages. */
static const int ModelOrmSourceMaps[MODEL_ORM_SOURCE_COUNT] =
{
    MATERIAL_MAP_OCCLUSION, MATERIAL_MAP_ROUGHNESS, MATERIAL_MAP_METALNESS
};
static const char* const ModelOrmSourceNames[MODEL_ORM_SOURCE_COUNT] = { "occlusion", "roughness", "metallic" };


// Static functions.
static Error ReadFloatFromValue(const JSONObjectValue* value, float* outValue)
{
    if (value->Type == JSONValueType_Integer) { *outValue = (float)value->Value.Integer; return Error_CreateSuccess(); }
    if (value->Type == JSONValueType_RealNumber) { *outValue = (float)value->Value.RealNumber; return Error_CreateSuccess(); }
    if (value->Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Error TextResult = AssetJSON_ValueToOwnedString(value, &Text);
        if (TextResult.Code != ErrorCode_Success) { return TextResult; }
        float Parsed = 0.0f;
        Error ParseResult = Number_FloatFromString(Text, &Parsed, DecimalSeparator_Period);
        Memory_Free(Text);
        if (ParseResult.Code != ErrorCode_Success) { return ParseResult; }
        *outValue = Parsed;
        return Error_CreateSuccess();
    }
    return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Expected a number value.");
}

static Error ReadVector3FromValue(const JSONObjectValue* value, Vector3* outVector)
{
    if (value->Type == JSONValueType_Compound)
    {
        JSONCompound* Compound = value->Value.Compound;
        const unsigned char* Keys[3] = { (const unsigned char*)u8"x", (const unsigned char*)u8"y", (const unsigned char*)u8"z" };
        float* Components[3] = { &outVector->x, &outVector->y, &outVector->z };
        for (size_t i = 0; i < 3U; i++)
        {
            JSONObjectValue Component;
            Error GetResult = JSONCompound_Get(Compound, Keys[i], &Component);
            if (GetResult.Code != ErrorCode_Success)
            {
                Error_Deconstruct(&GetResult);
                return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Vector3 compound requires x, y and z.");
            }
            Error FloatResult = ReadFloatFromValue(&Component, Components[i]);
            if (FloatResult.Code != ErrorCode_Success) { return FloatResult; }
        }
        return Error_CreateSuccess();
    }
    if (value->Type == JSONValueType_Array)
    {
        JSONArray* Array = value->Value.Array;
        if (JSONArray_GetElementCount(Array) < 3U)
        {
            return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Vector3 array requires three elements.");
        }
        float* Components[3] = { &outVector->x, &outVector->y, &outVector->z };
        for (size_t i = 0; i < 3U; i++)
        {
            JSONObjectValue Element;
            Error GetResult = JSONArray_Get(Array, i, &Element);
            if (GetResult.Code != ErrorCode_Success) { return GetResult; }
            Error FloatResult = ReadFloatFromValue(&Element, Components[i]);
            if (FloatResult.Code != ErrorCode_Success) { return FloatResult; }
        }
        return Error_CreateSuccess();
    }
    return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Expected a vector3.");
}

static Error ReadVector3Key(JSONCompound* compound, const unsigned char* key, Vector3 defaultVector, Vector3* outVector)
{
    *outVector = defaultVector;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }
    return ReadVector3FromValue(&Value, outVector);
}

static Error ParseTransform(JSONCompound* root, bool* outHas, Matrix* outMatrix)
{
    *outHas = false;
    JSONObjectValue TransformValue;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptionalVerified(root, (const unsigned char*)u8"transform", JSONValueType_Compound, &TransformValue, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }

    JSONCompound* Compound = TransformValue.Value.Compound;
    Vector3 Scale = { 1.0f, 1.0f, 1.0f };
    Vector3 Rotation = { 0.0f, 0.0f, 0.0f };
    Vector3 Translation = { 0.0f, 0.0f, 0.0f };

    JSONObjectValue ScaleValue;
    bool ScaleFound = false;
    Error ScaleResult = JSONCompound_GetOptional(Compound, (const unsigned char*)u8"scale", &ScaleValue, &ScaleFound);
    if (ScaleResult.Code != ErrorCode_Success) { return ScaleResult; }
    if (ScaleFound)
    {
        if ((ScaleValue.Type == JSONValueType_Compound) || (ScaleValue.Type == JSONValueType_Array))
        {
            Error VectorResult = ReadVector3FromValue(&ScaleValue, &Scale);
            if (VectorResult.Code != ErrorCode_Success) { return VectorResult; }
        }
        else
        {
            float Uniform = 1.0f;
            Error FloatResult = ReadFloatFromValue(&ScaleValue, &Uniform);
            if (FloatResult.Code != ErrorCode_Success) { return FloatResult; }
            Scale.x = Uniform;
            Scale.y = Uniform;
            Scale.z = Uniform;
        }
    }

    Error RotationResult = ReadVector3Key(Compound, (const unsigned char*)u8"rotation_euler", Rotation, &Rotation);
    if (RotationResult.Code != ErrorCode_Success) { return RotationResult; }
    Error TranslationResult = ReadVector3Key(Compound, (const unsigned char*)u8"translation", Translation, &Translation);
    if (TranslationResult.Code != ErrorCode_Success) { return TranslationResult; }

    Matrix ScaleMatrix = MatrixScale(Scale.x, Scale.y, Scale.z);
    Vector3 RotationRadians = { Rotation.x * DEG2RAD, Rotation.y * DEG2RAD, Rotation.z * DEG2RAD };
    Matrix RotationMatrix = MatrixRotateXYZ(RotationRadians);
    Matrix TranslationMatrix = MatrixTranslate(Translation.x, Translation.y, Translation.z);
    *outMatrix = MatrixMultiply(MatrixMultiply(ScaleMatrix, RotationMatrix), TranslationMatrix);
    *outHas = true;
    return Error_CreateSuccess();
}

/* Reads an OPTIONAL texture-location key ("albedo"/"normal"/...) into @p outLocation, setting @p outHas.
 * Absent key -> outHas false and no location parsed. A texture location must be a file path. */
static Error ReadOptionalTextureLocationKey(JSONCompound* compound, const unsigned char* key, bool* outHas, OwnedAssetLocation* outLocation)
{
    *outHas = false;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }
    Error LocationResult = AssetJSON_ParseLocation(&Value, NULL, outLocation);
    if (LocationResult.Code != ErrorCode_Success) { return LocationResult; }
    Error FileResult = OwnedAssetLocation_RequireFile(outLocation, MODEL_TEXTURE_KIND);
    if (FileResult.Code != ErrorCode_Success) { return FileResult; }
    *outHas = true;
    return Error_CreateSuccess();
}

/* Reads one material override; the texture properties it leaves out are @p modelProperties' (the model's). */
static Error ParseMaterialOverride(JSONCompound* compound, const TextureProperties* modelProperties,
    ModelMaterialOverride* outOverride)
{
    Memory_Zero(outOverride, sizeof(*outOverride));

    Error Result = AssetJSON_ReadMaterialSlot(compound, &outOverride->Slot);
    if (Result.Code != ErrorCode_Success) { return Result; }

    Result = ReadOptionalTextureLocationKey(compound, (const unsigned char*)u8"albedo", &outOverride->HasAlbedo,
        &outOverride->Albedo);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ReadOptionalTextureLocationKey(compound, (const unsigned char*)u8"normal", &outOverride->HasNormal, &outOverride->Normal);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ReadOptionalTextureLocationKey(compound, (const unsigned char*)u8"mra", &outOverride->HasMra, &outOverride->Mra);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ReadOptionalTextureLocationKey(compound, (const unsigned char*)u8"emissive", &outOverride->HasEmissiveMap, &outOverride->EmissiveMap);
    if (Result.Code != ErrorCode_Success) { return Result; }

    Result = AssetJSON_ReadMaterialFactors(compound, &outOverride->Factors);
    if (Result.Code != ErrorCode_Success) { return Result; }

    return AssetJSON_ReadTexturePropertiesWithDefaults(compound, (const unsigned char*)u8"texture_properties", true,
        modelProperties, &outOverride->Properties, NULL);
}

/* Lists @p textureId as owned by the model unless it is 0, raylib's default texture, or already listed. */
static Error AddOwnedTextureId(GenericBuffer* ownedTextureIds, unsigned int textureId)
{
    if ((textureId == 0U) || (textureId == rlGetTextureIdDefault()))
    {
        return Error_CreateSuccess();
    }
    for (size_t i = 0; i < ownedTextureIds->_count; i++)
    {
        unsigned int OwnedId = 0U;
        if (GenericBuffer_GetAt(ownedTextureIds, i, &OwnedId) && (OwnedId == textureId))
        {
            return Error_CreateSuccess();
        }
    }
    if (!GenericBuffer_AddLast(ownedTextureIds, &textureId))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to track a model texture.");
    }
    return Error_CreateSuccess();
}

/* Lists every texture any material map of @p model references (right after LoadModel: the importer's). */
static Error CollectMaterialTextureIds(const Model* model, GenericBuffer* ownedTextureIds)
{
    if (model->materials == NULL)
    {
        return Error_CreateSuccess();
    }
    for (int materialIndex = 0; materialIndex < model->materialCount; materialIndex++)
    {
        const Material* ModelMaterial = &model->materials[materialIndex];
        if (ModelMaterial->maps == NULL)
        {
            continue;
        }
        for (int mapIndex = MODEL_FIRST_MATERIAL_MAP; mapIndex <= MODEL_LAST_MATERIAL_MAP; mapIndex++)
        {
            Error AddResult = AddOwnedTextureId(ownedTextureIds, ModelMaterial->maps[mapIndex].texture.id);
            if (AddResult.Code != ErrorCode_Success)
            {
                return AddResult;
            }
        }
    }
    return Error_CreateSuccess();
}

static bool IsTextureIdReferenced(const Model* model, unsigned int textureId)
{
    if (model->materials == NULL)
    {
        return false;
    }
    for (int materialIndex = 0; materialIndex < model->materialCount; materialIndex++)
    {
        const Material* ModelMaterial = &model->materials[materialIndex];
        if (ModelMaterial->maps == NULL)
        {
            continue;
        }
        for (int mapIndex = MODEL_FIRST_MATERIAL_MAP; mapIndex <= MODEL_LAST_MATERIAL_MAP; mapIndex++)
        {
            if (ModelMaterial->maps[mapIndex].texture.id == textureId)
            {
                return true;
            }
        }
    }
    return false;
}

/* Unloads and forgets every listed texture that no material references any more (importer textures an
 * override replaced). A texture is only unloaded once it is off the list, so it can never be unloaded twice. */
static void PruneUnreferencedTextures(const Model* model, GenericBuffer* ownedTextureIds)
{
    for (size_t i = ownedTextureIds->_count; i > 0U; i--)
    {
        size_t Index = i - 1U;
        unsigned int OwnedId = 0U;
        if (!GenericBuffer_GetAt(ownedTextureIds, Index, &OwnedId) || IsTextureIdReferenced(model, OwnedId))
        {
            continue;
        }
        if (GenericBuffer_RemoveAt(ownedTextureIds, Index))
        {
            rlUnloadTexture(OwnedId);
        }
    }
}

/* Unloads every listed texture and empties the list. */
static void UnloadOwnedTextures(GenericBuffer* ownedTextureIds)
{
    for (size_t i = 0; i < ownedTextureIds->_count; i++)
    {
        unsigned int OwnedId = 0U;
        if (GenericBuffer_GetAt(ownedTextureIds, i, &OwnedId))
        {
            rlUnloadTexture(OwnedId);
        }
    }
    GenericBuffer_Clear(ownedTextureIds);
}

/* Releases a loaded model completely: its owned textures first, then meshes and material map arrays. */
static void DiscardModel(Model* model, GenericBuffer* ownedTextureIds)
{
    UnloadOwnedTextures(ownedTextureIds);
    UnloadModel(*model);
}

static void ModelLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    ModelLoaded* Loaded = self->DestroyContext;
    GameSkeleton_Destroy(Loaded->Model._skeleton);
    DiscardModel(&Loaded->Model._rayModel, Loaded->OwnedTextureIds);
    Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Loaded->OwnedTextureIds);
    Error_Deconstruct(&ReturnResult);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable ModelLoadedVTable = { ModelLoaded_Destroy };

/* Loads a material-override texture (resolved against the model asset-type dir), lists it as owned by the
 * model immediately, applies @p properties (mipmaps, wrap, filter) and writes it to @p outTexture. On failure
 * @p outTexture is untouched, and a texture that loaded stays listed, so the caller's failure path unloads it. */
static Error LoadMaterialTexture(AssetManager* manager, AssetTypeID type, const OwnedAssetLocation* location,
    const TextureProperties* properties, const unsigned char* modelName, GenericBuffer* ownedTextureIds,
    Texture2D* outTexture)
{
    AssetLocation Location = OwnedAssetLocation_View(location);
    AssetResourcePath* Handle = NULL;
    Error PathResult = AssetManager_AcquireResourcePath(manager, type, &Location, &ASSET_EXTENSIONS_TEXTURE, NULL, &Handle);
    if (PathResult.Code != ErrorCode_Success) { return PathResult; }
    Texture2D MaterialTexture = LoadTexture((const char*)AssetResourcePath_Get(Handle));
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, Handle);
    Error_Deconstruct(&ReleaseResult);
    if (MaterialTexture.id == 0)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to load material texture \"%s\" for model \"%s\".",
            location->Value, modelName);
    }

    Error TrackResult = AddOwnedTextureId(ownedTextureIds, MaterialTexture.id);
    if (TrackResult.Code != ErrorCode_Success)
    {
        UnloadTexture(MaterialTexture);
        return TrackResult;
    }
    Error PropertiesResult = TextureProperties_Apply(properties, &MaterialTexture, location->Value);
    if (PropertiesResult.Code != ErrorCode_Success)
    {
        return PropertiesResult;
    }
    *outTexture = MaterialTexture;
    return Error_CreateSuccess();
}

/* Whether a material map holds a texture of its own: not none, and not raylib's default texture. */
static bool HasOwnTexture(const MaterialMap* map)
{
    return (map->texture.id != 0U) && (map->texture.id != rlGetTextureIdDefault());
}

/* Reads a material map's texture back from the GPU into @p outImage (raylib memory: UnloadImage), or leaves it zeroed
 * when the map has no texture of its own. */
static Error ReadMapImage(const MaterialMap* map, const char* name, Image* outImage)
{
    Memory_Zero(outImage, sizeof(*outImage));
    if (!HasOwnTexture(map))
    {
        return Error_CreateSuccess();
    }
    *outImage = LoadImageFromTexture(map->texture);
    if (outImage->data == NULL)
    {
        return Error_Construct4(ErrorCode_InvalidAssetData, "The %s map could not be read back (pixel format %d).",
            name, map->texture.format);
    }
    return Error_CreateSuccess();
}

/* Packs one material's occlusion, roughness and metallic textures into one texture in its METALNESS slot, where the PBR
 * shader reads the packed map, and takes the three off the material. The new texture is listed as owned at once; the
 * three stay listed until PruneUnreferencedTextures unloads them (no material references them any more). A material
 * with none of the three is left as it is. @p occlusionStrength is the file's (ModelOrmMap_Build). */
static Error PackMaterialOrmMap(Material* material, float occlusionStrength, GenericBuffer* ownedTextureIds)
{
    MaterialMap* Maps = material->maps;
    bool HasAnySource = false;
    for (size_t Index = 0U; Index < MODEL_ORM_SOURCE_COUNT; Index++)
    {
        HasAnySource = HasAnySource || HasOwnTexture(&Maps[ModelOrmSourceMaps[Index]]);
    }
    if (!HasAnySource)
    {
        return Error_CreateSuccess();
    }

    Image Images[MODEL_ORM_SOURCE_COUNT];
    Memory_Zero(Images, sizeof(Images));
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < MODEL_ORM_SOURCE_COUNT) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = ReadMapImage(&Maps[ModelOrmSourceMaps[Index]], ModelOrmSourceNames[Index], &Images[Index]);
    }
    Image Packed;
    Memory_Zero(&Packed, sizeof(Packed));
    if (Result.Code == ErrorCode_Success)
    {
        ModelOrmSources Sources =
        {
            .Occlusion = (Images[0].data != NULL) ? &Images[0] : NULL,
            .Roughness = (Images[1].data != NULL) ? &Images[1] : NULL,
            .Metallic = (Images[2].data != NULL) ? &Images[2] : NULL,
        };
        Result = ModelOrmMap_Build(&Sources, occlusionStrength, &Packed);
    }
    for (size_t Index = 0U; Index < MODEL_ORM_SOURCE_COUNT; Index++)
    {
        UnloadImage(Images[Index]);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Texture2D PackedTexture = LoadTextureFromImage(Packed);
    UnloadImage(Packed);
    if (PackedTexture.id == 0U)
    {
        return Error_Construct2(ErrorCode_InvalidAssetData, "The packed map could not be uploaded.");
    }
    Error TrackResult = AddOwnedTextureId(ownedTextureIds, PackedTexture.id);
    if (TrackResult.Code != ErrorCode_Success)
    {
        UnloadTexture(PackedTexture);
        return TrackResult;
    }
    Maps[MATERIAL_MAP_METALNESS].texture = PackedTexture;
    Maps[MATERIAL_MAP_ROUGHNESS].texture = (Texture2D){ 0 };
    Maps[MATERIAL_MAP_OCCLUSION].texture = (Texture2D){ 0 };
    return Error_CreateSuccess();
}

/* Gives every material of a glTF file that has an occlusion, roughness or metallic map the packed map the PBR shader
 * reads (raylib's importer leaves the three apart: ModelOrmMap.h), with the file's occlusion strength. @p materials are
 * the file's, one fewer than raylib's (its slot 0 is its default material); @p description names the model. */
static Error PackGltfOrmMaps(Model* model, const ModelGltfMaterials* materials, const char* description,
    GenericBuffer* ownedTextureIds)
{
    for (int MaterialIndex = 0; MaterialIndex < model->materialCount; MaterialIndex++)
    {
        if (model->materials[MaterialIndex].maps == NULL)
        {
            continue;
        }
        float OcclusionStrength = (MaterialIndex > 0)
            ? materials->Materials[MaterialIndex - 1].OcclusionStrength : 1.0f;
        Error Result = PackMaterialOrmMap(&model->materials[MaterialIndex], OcclusionStrength, ownedTextureIds);
        if (Result.Code != ErrorCode_Success)
        {
            Error Wrapped = Error_Construct4(Result.Code,
                "%s, material %d: its packed occlusion/roughness/metallic map could not be built: %s", description,
                MaterialIndex, (Result.Message != NULL) ? (const char*)Result.Message : "an error.");
            Error_Deconstruct(&Result);
            return Wrapped;
        }
    }
    return Error_CreateSuccess();
}

/* Whether raylib read the glTF file's materials: its own default one in slot 0, then each of the file's. */
static bool IsGltfMaterialCountMatching(const Model* model, const ModelGltfMaterials* materials)
{
    return (model->materialCount >= 1) && (((size_t)model->materialCount - 1U) == materials->Count);
}

/* Gives raylib's materials what the glTF file says and raylib drops (ModelGltfMaterials.h): the factors, the colours
 * encoded as the game's material colours are, and the emission's strength as its intensity. The file's material i is
 * slot i + 1 (IsGltfMaterialCountMatching). A material whose textures raylib left out is logged, once per load. */
static void ApplyGltfMaterials(Model* model, const ModelGltfMaterials* materials, const char* description,
    Logger* logger)
{
    for (size_t Index = 0U; Index < materials->Count; Index++)
    {
        MaterialMap* Maps = model->materials[Index + 1U].maps;
        const ModelGltfMaterial* Source = &materials->Materials[Index];
        if (Maps == NULL)
        {
            continue;
        }
        Maps[MATERIAL_MAP_ALBEDO].color = ModelGltfMaterials_ToColor(Source->BaseColor, Source->BaseColor[3]);
        Maps[MATERIAL_MAP_METALNESS].value = Source->Metallic;
        Maps[MATERIAL_MAP_ROUGHNESS].value = Source->Roughness;
        Maps[MATERIAL_MAP_EMISSION].color = ModelGltfMaterials_ToColor(Source->Emissive, 1.0f);
        Maps[MATERIAL_MAP_EMISSION].value = Source->EmissiveStrength;
        if (Source->HasSkippedTextures && (logger != NULL))
        {
            // TODO: load the textures of a material without a pbrMetallicRoughness block here (raylib's glTF image
            // loading is private to it); Blender writes the block for every Principled BSDF.
            Error LogResult = Logger_LogWarningFormatted(logger, (const unsigned char*)u8"%s: material %" PRIu64
                u8" has no pbrMetallicRoughness block, so its textures are not loaded (raylib reads a glTF material's "
                u8"textures only inside one).", description, (uint64_t)Index);
            Error_Deconstruct(&LogResult);
        }
    }
}

/* Applies the model's texture properties (mipmaps, wrap, filtering) to every texture it owns so far: its file's and the
 * packed maps (an override's maps get its own when they load). A texture several maps share is set up once, and each of
 * those maps gets its updated copy (the mip count). */
static Error ApplyFileTextureProperties(Model* model, const TextureProperties* properties, GenericBuffer* ownedTextureIds,
    const char* description)
{
    for (size_t Owned = 0U; Owned < ownedTextureIds->_count; Owned++)
    {
        unsigned int TextureId = 0U;
        if (!GenericBuffer_GetAt(ownedTextureIds, Owned, &TextureId))
        {
            continue;
        }
        const Texture2D* Applied = NULL;
        for (int MaterialIndex = 0; MaterialIndex < model->materialCount; MaterialIndex++)
        {
            MaterialMap* Maps = model->materials[MaterialIndex].maps;
            for (int MapIndex = MODEL_FIRST_MATERIAL_MAP; (Maps != NULL) && (MapIndex <= MODEL_LAST_MATERIAL_MAP);
                MapIndex++)
            {
                if (Maps[MapIndex].texture.id != TextureId)
                {
                    continue;
                }
                if (Applied != NULL)
                {
                    Maps[MapIndex].texture = *Applied;
                    continue;
                }
                Error Result = TextureProperties_Apply(properties, &Maps[MapIndex].texture,
                    (const unsigned char*)description);
                if (Result.Code != ErrorCode_Success)
                {
                    return Result;
                }
                Applied = &Maps[MapIndex].texture;
            }
        }
    }
    return Error_CreateSuccess();
}

/* Seeds a loaded material with sensible PBR scalar defaults: only degenerate (<= 0) roughness / occlusion
 * factors are replaced (so an importer that authored them keeps them; a bare OBJ, which leaves them 0, gets a
 * usable 0.5 roughness / 1.0 occlusion). Metallic 0 is a valid dielectric default and is left as loaded. */
static void SeedMaterialDefaults(Material* material)
{
    if (material->maps[MATERIAL_MAP_ROUGHNESS].value <= 0.0f)
    {
        material->maps[MATERIAL_MAP_ROUGHNESS].value = MODEL_DEFAULT_ROUGHNESS;
    }
    if (material->maps[MATERIAL_MAP_OCCLUSION].value <= 0.0f)
    {
        material->maps[MATERIAL_MAP_OCCLUSION].value = MODEL_DEFAULT_AO;
    }
}

/* Applies one material override to the model's material @p slot: rebinds albedo/normal/mra/emissive textures
 * and sets tint + metallic/roughness/emissive scalar factors. Textures load against the model asset-type dir
 * and are listed in @p ownedTextureIds as they load, so the caller can unload them all if a later one fails. */
static Error ApplyMaterialOverride(AssetManager* manager, AssetTypeID type, const unsigned char* modelName,
    const ModelMaterialOverride* override, GenericBuffer* ownedTextureIds, Material* material)
{
    if (override->HasAlbedo)
    {
        Error Result = LoadMaterialTexture(manager, type, &override->Albedo, &override->Properties, modelName,
            ownedTextureIds, &material->maps[MATERIAL_MAP_ALBEDO].texture);
        if (Result.Code != ErrorCode_Success) { return Result; }
    }
    if (override->HasNormal)
    {
        Error Result = LoadMaterialTexture(manager, type, &override->Normal, &override->Properties, modelName,
            ownedTextureIds, &material->maps[MATERIAL_MAP_NORMAL].texture);
        if (Result.Code != ErrorCode_Success) { return Result; }
    }
    if (override->HasMra)
    {
        // The METALNESS slot carries the packed ORM map.
        Error Result = LoadMaterialTexture(manager, type, &override->Mra, &override->Properties, modelName,
            ownedTextureIds, &material->maps[MATERIAL_MAP_METALNESS].texture);
        if (Result.Code != ErrorCode_Success) { return Result; }
    }
    if (override->HasEmissiveMap)
    {
        Error Result = LoadMaterialTexture(manager, type, &override->EmissiveMap, &override->Properties, modelName,
            ownedTextureIds, &material->maps[MATERIAL_MAP_EMISSION].texture);
        if (Result.Code != ErrorCode_Success) { return Result; }
    }

    // Tint, metallic, roughness and emissive colour + intensity (an emissive map alone shows at white x 1).
    GameMaterialFactors_Apply(&override->Factors, override->HasEmissiveMap, material->maps);
    return Error_CreateSuccess();
}

/* Returns the bounds of every mesh in mesh space: GetModelBoundingBox walks every vertex and applies
 * model.transform, so it is called once here, with an identity transform, rather than by each user. */
static BoundingBox ComputeLocalBounds(Model model)
{
    Model MeshSpaceModel = model;
    MeshSpaceModel.transform = MatrixIdentity();
    return GetModelBoundingBox(MeshSpaceModel);
}

/* Resolves the model file (narrowed to "format" when the definition has one) and loads it with raylib. A glTF file
 * (raylib picks its importer by the extension, as here; @p outIsGltf tells) is read with ModelGltfMaterials first, which
 * checks it as hostile: raylib's importer trusts every number, so it only reads a file those checks passed.
 * @p outGltfMaterials receives the file's materials (zeroed for other formats; the caller deconstructs them). */
static Error LoadModelFile(const ModelDefinition* definition, AssetManager* manager, const char* description,
    Model* outModel, bool* outIsGltf, ModelGltfMaterials* outGltfMaterials)
{
    Memory_Zero(outGltfMaterials, sizeof(*outGltfMaterials));
    *outIsGltf = false;
    const unsigned char* FormatExtension = definition->Format;
    AssetExtensionFilter Extensions = AssetFormat_SelectFilter(&ASSET_EXTENSIONS_MODEL, &FormatExtension);
    AssetLocation Location = OwnedAssetLocation_View(&definition->Location);
    AssetResourcePath* PathHandle = NULL;
    Error PathResult = AssetManager_AcquireResourcePath(manager, definition->Base.Type, &Location, &Extensions,
        definition->Format, &PathHandle);
    if (PathResult.Code != ErrorCode_Success) { return PathResult; }

    const char* Path = (const char*)AssetResourcePath_Get(PathHandle);
    *outIsGltf = IsFileExtension(Path, ".gltf;.glb");
    Error Result = Error_CreateSuccess();
    if (*outIsGltf)
    {
        Result = ModelGltfMaterials_ReadFile((const unsigned char*)Path, description, outGltfMaterials);
    }
    if (Result.Code == ErrorCode_Success)
    {
        *outModel = LoadModel(Path);
    }
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, PathHandle);
    Error_Deconstruct(&ReleaseResult);
    return Result;
}

/* Applies the definition to a freshly loaded model: transform, PBR defaults, a glTF file's own material values
 * (@p gltfMaterials; NULL for other formats), the texture properties of the file's maps and the material overrides. */
static Error ApplyDefinitionToModel(const ModelDefinition* definition, AssetManager* manager,
    const ModelGltfMaterials* gltfMaterials, const char* description, Model* model, GenericBuffer* ownedTextureIds)
{
    if (definition->HasTransform)
    {
        model->transform = definition->Transform;
    }

    // Seed every material with sensible PBR scalar defaults so un-overridden slots shade correctly through the
    // renderer's per-material path (a bare OBJ leaves roughness/occlusion at 0, which would read as a mirror /
    // black ambient).
    for (int i = 0; i < model->materialCount; i++)
    {
        SeedMaterialDefaults(&model->materials[i]);
    }

    // A glTF file's own values win over the defaults; the overrides win over both.
    if (gltfMaterials != NULL)
    {
        ApplyGltfMaterials(model, gltfMaterials, description, AssetManager_GetLogger(manager));
    }
    Error PropertiesResult = ApplyFileTextureProperties(model, &definition->Properties, ownedTextureIds, description);
    if (PropertiesResult.Code != ErrorCode_Success)
    {
        return PropertiesResult;
    }

    for (size_t i = 0; i < definition->OverrideCount; i++)
    {
        const ModelMaterialOverride* Override = &definition->Overrides[i];
        if (Override->Slot >= model->materialCount)
        {
            continue;
        }
        Error ApplyResult = ApplyMaterialOverride(manager, definition->Base.Type, definition->Base.Name,
            Override, ownedTextureIds, &model->materials[Override->Slot]);
        if (ApplyResult.Code != ErrorCode_Success)
        {
            return ApplyResult;
        }
    }
    return Error_CreateSuccess();
}

static Error ModelDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    ModelDefinition* Definition = self;

    GenericBuffer* OwnedTextureIds = NULL;
    Error BorrowResult = AssetManager_BorrowGenericBuffer(manager, sizeof(unsigned int), &OwnedTextureIds);
    if (BorrowResult.Code != ErrorCode_Success) { return BorrowResult; }

    // What messages call the model.
    char PrintableName[MODEL_PRINTABLE_NAME_CAPACITY];
    AssetJSON_CopyPrintable(Definition->Base.Name, PrintableName, sizeof(PrintableName));
    char Description[MODEL_PRINTABLE_NAME_CAPACITY + 16U];
    snprintf(Description, sizeof(Description), "Model \"%s\"", PrintableName);

    Model RayModel;
    Memory_Zero(&RayModel, sizeof(RayModel));
    bool IsGltf = false;
    ModelGltfMaterials GltfMaterials;
    Error Result = LoadModelFile(Definition, manager, Description, &RayModel, &IsGltf, &GltfMaterials);
    if (Result.Code != ErrorCode_Success)
    {
        ModelGltfMaterials_Deconstruct(&GltfMaterials);
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, OwnedTextureIds);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    // From here on every failure must unload whatever the model references; list the importer's textures
    // first (even a mesh-less result may carry material textures).
    Result = CollectMaterialTextureIds(&RayModel, OwnedTextureIds);
    if ((Result.Code == ErrorCode_Success) && (RayModel.meshCount == 0))
    {
        Result = Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to load model \"%s\".", Definition->Base.Name);
    }
    if ((Result.Code == ErrorCode_Success) && IsGltf && !IsGltfMaterialCountMatching(&RayModel, &GltfMaterials))
    {
        Result = Error_Construct4(ErrorCode_InvalidAssetData, "%s: raylib read %d materials where the file lists %"
            PRIu64 " (and raylib's own).", Description, RayModel.materialCount, (uint64_t)GltfMaterials.Count);
    }
    if ((Result.Code == ErrorCode_Success) && IsGltf)
    {
        Result = PackGltfOrmMaps(&RayModel, &GltfMaterials, Description, OwnedTextureIds);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ApplyDefinitionToModel(Definition, manager, IsGltf ? &GltfMaterials : NULL, Description, &RayModel,
            OwnedTextureIds);
    }
    ModelGltfMaterials_Deconstruct(&GltfMaterials);
    if (Result.Code != ErrorCode_Success)
    {
        DiscardModel(&RayModel, OwnedTextureIds);
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, OwnedTextureIds);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    // Importer textures replaced by a packed map or an override are referenced by no material any more.
    PruneUnreferencedTextures(&RayModel, OwnedTextureIds);

    // A skinned model's skeleton (none without bones). A broken skeleton fails the model, as a broken mesh would.
    GameSkeleton* Skeleton = NULL;
    Result = GameSkeleton_Create(&RayModel, (const unsigned char*)Description, AssetManager_GetLogger(manager),
        &Skeleton);
    if (Result.Code != ErrorCode_Success)
    {
        DiscardModel(&RayModel, OwnedTextureIds);
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, OwnedTextureIds);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    ModelLoaded* Loaded = Memory_Allocate(sizeof(ModelLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Model._rayModel = RayModel;
    Loaded->Model._localBounds = ComputeLocalBounds(RayModel);
    Loaded->Model._skeleton = Skeleton;
    Loaded->OwnedTextureIds = OwnedTextureIds;

    outLoaded->Asset = &Loaded->Model;
    outLoaded->VTable = &ModelLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void ModelDefinition_Destroy(void* self)
{
    ModelDefinition* Definition = self;
    OwnedAssetLocation_Deconstruct(&Definition->Location);
    Memory_Free(Definition->Format);
    for (size_t i = 0; i < Definition->OverrideCount; i++)
    {
        OwnedAssetLocation_Deconstruct(&Definition->Overrides[i].Albedo);
        OwnedAssetLocation_Deconstruct(&Definition->Overrides[i].Normal);
        OwnedAssetLocation_Deconstruct(&Definition->Overrides[i].Mra);
        OwnedAssetLocation_Deconstruct(&Definition->Overrides[i].EmissiveMap);
    }
    Memory_Free(Definition->Overrides);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable ModelDefinitionVTable = { ModelDefinition_LoadAsset, ModelDefinition_Destroy };

static Error ParseOverrides(ModelDefinition* definition, JSONCompound* root)
{
    JSONObjectValue OverridesValue;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptionalVerified(root, (const unsigned char*)u8"material_overrides", JSONValueType_Array, &OverridesValue, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }

    JSONArray* Array = OverridesValue.Value.Array;
    size_t Count = JSONArray_GetElementCount(Array);
    if (Count == 0U) { return Error_CreateSuccess(); }

    size_t ByteCount = 0;
    if (!Memory_TryMultiplySize(Count, sizeof(ModelMaterialOverride), &ByteCount))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Too many material overrides.");
    }
    definition->Overrides = Memory_Allocate(ByteCount);
    Memory_Zero(definition->Overrides, ByteCount);
    definition->OverrideCount = Count;

    for (size_t i = 0; i < Count; i++)
    {
        JSONObjectValue Element;
        Error ElementResult = JSONArray_GetVerified(Array, i, JSONValueType_Compound, &Element);
        if (ElementResult.Code != ErrorCode_Success) { return ElementResult; }
        Error OverrideResult = ParseMaterialOverride(Element.Value.Compound, &definition->Properties,
            &definition->Overrides[i]);
        if (OverrideResult.Code != ErrorCode_Success) { return OverrideResult; }
    }
    return Error_CreateSuccess();
}


// Public functions.
Error ModelDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
    const unsigned char* sourceDescription, AssetDefinition** outDefinition)
{
    (void)manager;
    *outDefinition = NULL;

    JSONObjectPool* Pool = UserData_GetPointer(userData);
    JSONObjectValue Root;
    JSONCompound* RootCompound = NULL;
    Error Result = AssetJSON_DeserializeRoot(Pool, rawData, sourceDescription, &Root, &RootCompound);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    ModelDefinition* Definition = Memory_Allocate(sizeof(ModelDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &ModelDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadLocation(RootCompound, (const unsigned char*)u8"location", sourceDescription, &Definition->Location);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalFormat(RootCompound, &ASSET_EXTENSIONS_MODEL, MODEL_ASSET_KIND, &Definition->Format);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = OwnedAssetLocation_ValidateReferenceFormat(&Definition->Location, Definition->Format,
            &ASSET_EXTENSIONS_MODEL, MODEL_ASSET_KIND);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseTransform(RootCompound, &Definition->HasTransform, &Definition->Transform);
    }
    if (Result.Code == ErrorCode_Success)
    {
        // Before the overrides: their texture properties default to the model's.
        Result = AssetJSON_ReadTexturePropertiesWithDefaults(RootCompound, (const unsigned char*)u8"texture_properties",
            true, &ModelDefaultTextureProperties, &Definition->Properties, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseOverrides(Definition, RootCompound);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        ModelDefinition_Destroy(Definition);
        return Result;
    }

    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
