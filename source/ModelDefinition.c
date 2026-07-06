#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "GameModel.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include "wr/WRNumber.h"
#include "wr/WRJSON.h"

/*
 * Static-model loader: loads meshes + materials, bakes an optional import transform into the model's
 * transform matrix, seeds sensible PBR scalar defaults, and applies optional material overrides (rebinding a
 * slot's albedo / normal / ORM (occlusion-roughness-metallic) / emissive textures and its tint + metallic /
 * roughness / emissive scalar factors). Skeletal animation and material addressing by name are out of scope
 * for now (slots are addressed by index). NOTE: an override replaces a material slot's texture; if that slot
 * already carried an embedded texture (rather than the default), the replaced texture is not separately
 * unloaded.
 */


// Macros.
/** Default PBR scalar factors applied to every loaded material so un-overridden slots shade sensibly through
 *  the renderer's per-material path. Only degenerate (<= 0) roughness/occlusion values are replaced, so an
 *  importer that already authored PBR factors (e.g. GLTF) keeps them; a bare OBJ (which leaves them 0) gets
 *  these. Metallic 0 (dielectric) is a fine default and is left as the importer set it. */
#define MODEL_DEFAULT_ROUGHNESS 0.5f
#define MODEL_DEFAULT_AO 1.0f


// Types.
typedef struct ModelMaterialOverrideStruct
{
    int64_t Slot;
    bool HasAlbedo;
    OwnedAssetLocation Albedo;   // base colour texture
    bool HasNormal;
    OwnedAssetLocation Normal;   // tangent-space normal map
    bool HasMra;
    OwnedAssetLocation Mra;      // packed occlusion(R)/roughness(G)/metallic(B) map
    bool HasEmissiveMap;
    OwnedAssetLocation EmissiveMap;
    bool HasTint;
    Color Tint;
    bool HasMetallic;
    float Metallic;
    bool HasRoughness;
    float Roughness;
    bool HasEmissiveColor;
    Color EmissiveColor;
    bool HasEmissiveIntensity;
    float EmissiveIntensity;
    int Filter;
} ModelMaterialOverride;

typedef struct ModelDefinitionStruct
{
    AssetDefinition Base;
    OwnedAssetLocation Location;
    unsigned char* Format; // owned; extension hint for reference locations, else NULL
    bool HasTransform;
    Matrix Transform;
    ModelMaterialOverride* Overrides; // owned array
    size_t OverrideCount;
} ModelDefinition;

typedef struct ModelLoadedStruct
{
    GameModel Model; // Asset points here; owns its meshes/materials/override textures via UnloadModel
} ModelLoaded;


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

/* Reads an OPTIONAL asset-location key ("albedo"/"normal"/...) into @p outLocation, setting @p outHas. Absent
 * key -> outHas false and no location parsed (leave the default). */
static Error ReadOptionalLocationKey(JSONCompound* compound, const unsigned char* key, bool* outHas, OwnedAssetLocation* outLocation)
{
    *outHas = false;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }
    Error LocationResult = AssetJSON_ParseLocation(&Value, NULL, outLocation);
    if (LocationResult.Code != ErrorCode_Success) { return LocationResult; }
    *outHas = true;
    return Error_CreateSuccess();
}

/* Reads an OPTIONAL decimal-number key into @p outValue, setting @p outHas. Absent key -> outHas false. */
static Error ReadOptionalFloatKey(JSONCompound* compound, const unsigned char* key, bool* outHas, float* outValue)
{
    *outHas = false;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }
    Error FloatResult = ReadFloatFromValue(&Value, outValue);
    if (FloatResult.Code != ErrorCode_Success) { return FloatResult; }
    *outHas = true;
    return Error_CreateSuccess();
}

/* Reads an OPTIONAL colour key into @p outColor, setting @p outHas. Absent key -> outHas false. */
static Error ReadOptionalColorKey(JSONCompound* compound, const unsigned char* key, bool* outHas, Color* outColor)
{
    *outHas = false;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }
    Error ColorResult = AssetJSON_ParseColor(&Value, outColor);
    if (ColorResult.Code != ErrorCode_Success) { return ColorResult; }
    *outHas = true;
    return Error_CreateSuccess();
}

static Error ParseMaterialOverride(JSONCompound* compound, ModelMaterialOverride* outOverride)
{
    Memory_Zero(outOverride, sizeof(*outOverride));
    outOverride->Tint = WHITE;
    outOverride->EmissiveColor = WHITE;
    outOverride->Filter = TEXTURE_FILTER_BILINEAR;

    int64_t Slot = -1;
    Error SlotResult = AssetJSON_ReadOptionalInteger(compound, (const unsigned char*)u8"slot", -1, &Slot);
    if (SlotResult.Code != ErrorCode_Success) { return SlotResult; }
    if (Slot < 0)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"A material override requires a non-negative \"slot\".");
    }
    outOverride->Slot = Slot;

    Error Result = ReadOptionalLocationKey(compound, (const unsigned char*)u8"albedo", &outOverride->HasAlbedo, &outOverride->Albedo);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ReadOptionalLocationKey(compound, (const unsigned char*)u8"normal", &outOverride->HasNormal, &outOverride->Normal);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ReadOptionalLocationKey(compound, (const unsigned char*)u8"mra", &outOverride->HasMra, &outOverride->Mra);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ReadOptionalLocationKey(compound, (const unsigned char*)u8"emissive", &outOverride->HasEmissiveMap, &outOverride->EmissiveMap);
    if (Result.Code != ErrorCode_Success) { return Result; }

    Result = ReadOptionalColorKey(compound, (const unsigned char*)u8"tint", &outOverride->HasTint, &outOverride->Tint);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ReadOptionalColorKey(compound, (const unsigned char*)u8"emissive_color", &outOverride->HasEmissiveColor, &outOverride->EmissiveColor);
    if (Result.Code != ErrorCode_Success) { return Result; }

    Result = ReadOptionalFloatKey(compound, (const unsigned char*)u8"metallic", &outOverride->HasMetallic, &outOverride->Metallic);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ReadOptionalFloatKey(compound, (const unsigned char*)u8"roughness", &outOverride->HasRoughness, &outOverride->Roughness);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ReadOptionalFloatKey(compound, (const unsigned char*)u8"emissive_intensity", &outOverride->HasEmissiveIntensity, &outOverride->EmissiveIntensity);
    if (Result.Code != ErrorCode_Success) { return Result; }

    TextureProperties Properties;
    Error PropertiesResult = AssetJSON_ReadTextureProperties(compound, (const unsigned char*)u8"texture_properties", &Properties);
    if (PropertiesResult.Code != ErrorCode_Success) { return PropertiesResult; }
    outOverride->Filter = Properties.Filter;
    return Error_CreateSuccess();
}

static void ModelLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    ModelLoaded* Loaded = self->DestroyContext;
    UnloadModel(Loaded->Model._rayModel);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable ModelLoadedVTable = { ModelLoaded_Destroy };

/* Loads a material-override texture (resolved against the model asset-type dir, like the albedo override),
 * applies @p filter, and returns it by value in @p outTexture. On failure returns the error and does not touch
 * outTexture. The returned texture is owned by the model's Material slot and freed by UnloadModel. */
static Error LoadMaterialTexture(AssetManager* manager, AssetTypeID type, const OwnedAssetLocation* location,
    int filter, const unsigned char* modelName, Texture2D* outTexture)
{
    AssetLocation Loc = OwnedAssetLocation_View(location);
    AssetResourcePath* Handle = NULL;
    Error PathResult = AssetManager_AcquireResourcePath(manager, type, &Loc, NULL, &Handle);
    if (PathResult.Code != ErrorCode_Success) { return PathResult; }
    Texture2D Texture = LoadTexture((const char*)AssetResourcePath_Get(Handle));
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, Handle);
    Error_Deconstruct(&ReleaseResult);
    if (Texture.id == 0)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to load a material texture for model \"%s\".", modelName);
    }
    SetTextureFilter(Texture, filter);
    *outTexture = Texture;
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
 * and sets tint + metallic/roughness/emissive scalar factors. Textures load against the model asset-type dir.
 * On the first texture load failure the error is returned (the caller unloads the whole model). */
static Error ApplyMaterialOverride(AssetManager* manager, AssetTypeID type, const unsigned char* modelName,
    const ModelMaterialOverride* override, Material* material)
{
    if (override->HasAlbedo)
    {
        Texture2D Texture;
        Error Result = LoadMaterialTexture(manager, type, &override->Albedo, override->Filter, modelName, &Texture);
        if (Result.Code != ErrorCode_Success) { return Result; }
        material->maps[MATERIAL_MAP_ALBEDO].texture = Texture;
    }
    if (override->HasNormal)
    {
        Texture2D Texture;
        Error Result = LoadMaterialTexture(manager, type, &override->Normal, override->Filter, modelName, &Texture);
        if (Result.Code != ErrorCode_Success) { return Result; }
        material->maps[MATERIAL_MAP_NORMAL].texture = Texture;
    }
    if (override->HasMra)
    {
        Texture2D Texture;
        Error Result = LoadMaterialTexture(manager, type, &override->Mra, override->Filter, modelName, &Texture);
        if (Result.Code != ErrorCode_Success) { return Result; }
        material->maps[MATERIAL_MAP_METALNESS].texture = Texture; // METALNESS slot carries the packed ORM map
    }
    if (override->HasEmissiveMap)
    {
        Texture2D Texture;
        Error Result = LoadMaterialTexture(manager, type, &override->EmissiveMap, override->Filter, modelName, &Texture);
        if (Result.Code != ErrorCode_Success) { return Result; }
        material->maps[MATERIAL_MAP_EMISSION].texture = Texture;
    }

    if (override->HasTint)
    {
        material->maps[MATERIAL_MAP_ALBEDO].color = override->Tint;
    }
    if (override->HasMetallic)
    {
        material->maps[MATERIAL_MAP_METALNESS].value = override->Metallic;
    }
    if (override->HasRoughness)
    {
        material->maps[MATERIAL_MAP_ROUGHNESS].value = override->Roughness;
    }

    // Emissive: colour + intensity. An emissive MAP with no explicit colour/intensity implies white * 1 so the
    // map is actually visible (otherwise the default intensity 0 would suppress it).
    if (override->HasEmissiveColor)
    {
        material->maps[MATERIAL_MAP_EMISSION].color = override->EmissiveColor;
    }
    else if (override->HasEmissiveMap)
    {
        material->maps[MATERIAL_MAP_EMISSION].color = WHITE;
    }
    if (override->HasEmissiveIntensity)
    {
        material->maps[MATERIAL_MAP_EMISSION].value = override->EmissiveIntensity;
    }
    else if (override->HasEmissiveMap)
    {
        material->maps[MATERIAL_MAP_EMISSION].value = 1.0f;
    }
    return Error_CreateSuccess();
}

static Error ModelDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    ModelDefinition* Definition = self;

    AssetLocation Location = OwnedAssetLocation_View(&Definition->Location);
    AssetResourcePath* PathHandle = NULL;
    Error PathResult = AssetManager_AcquireResourcePath(manager, Definition->Base.Type, &Location, Definition->Format, &PathHandle);
    if (PathResult.Code != ErrorCode_Success) { return PathResult; }

    Model RayModel = LoadModel((const char*)AssetResourcePath_Get(PathHandle));
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, PathHandle);
    Error_Deconstruct(&ReleaseResult);

    if (RayModel.meshCount == 0)
    {
        UnloadModel(RayModel);
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to load model \"%s\".", Definition->Base.Name);
    }

    if (Definition->HasTransform)
    {
        RayModel.transform = Definition->Transform;
    }

    // Seed every material with sensible PBR scalar defaults so un-overridden slots shade correctly through the
    // renderer's per-material path (a bare OBJ leaves roughness/occlusion at 0, which would read as a mirror /
    // black ambient).
    for (int i = 0; i < RayModel.materialCount; i++)
    {
        SeedMaterialDefaults(&RayModel.materials[i]);
    }

    for (size_t i = 0; i < Definition->OverrideCount; i++)
    {
        ModelMaterialOverride* Override = &Definition->Overrides[i];
        if ((Override->Slot < 0) || (Override->Slot >= RayModel.materialCount))
        {
            continue;
        }
        Error ApplyResult = ApplyMaterialOverride(manager, Definition->Base.Type, Definition->Base.Name,
            Override, &RayModel.materials[(int)Override->Slot]);
        if (ApplyResult.Code != ErrorCode_Success)
        {
            UnloadModel(RayModel);
            return ApplyResult;
        }
    }

    ModelLoaded* Loaded = Memory_Allocate(sizeof(ModelLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Model._rayModel = RayModel;

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
        Error OverrideResult = ParseMaterialOverride(Element.Value.Compound, &definition->Overrides[i]);
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
        Result = AssetJSON_ReadOptionalOwnedString(RootCompound, (const unsigned char*)u8"format", &Definition->Format, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseTransform(RootCompound, &Definition->HasTransform, &Definition->Transform);
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
