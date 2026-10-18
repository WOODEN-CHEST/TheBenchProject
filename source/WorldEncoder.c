// TODO: Saves are write-only so far: there is no decoder (GHDF -> WorldDTO) and no stream read/write. The environment
// already reads back through its property table (Properties_ReadGHDF); the objects move onto tables in T05, and the
// world files (JSON and GHDF load and save) come with T09.
#include "WorldEncoder.h"
#include "wr/WRMemory.h"


// Static functions: scalar field writers (no borrowed sub-resource).
static Error PutFloat(GHDFCompound* compound, GHDFEntryID id, float value)
{
    GHDFObjectValue Value = GHDFObjectValue_CreateFloat(value);
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_Float), &Value);
}

static Error PutBool(GHDFCompound* compound, GHDFEntryID id, bool value)
{
    GHDFObjectValue Value = GHDFObjectValue_CreateBoolean(value);
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_Boolean), &Value);
}

static Error PutUInt8(GHDFCompound* compound, GHDFEntryID id, uint8_t value)
{
    GHDFObjectValue Value = GHDFObjectValue_CreateUInt8(value);
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_UInt8), &Value);
}

static Error PutUInt32(GHDFCompound* compound, GHDFEntryID id, uint32_t value)
{
    GHDFObjectValue Value = GHDFObjectValue_CreateUInt32(value);
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_UInt32), &Value);
}

static Error PutUInt64(GHDFCompound* compound, GHDFEntryID id, uint64_t value)
{
    GHDFObjectValue Value = GHDFObjectValue_CreateUInt64(value);
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_UInt64), &Value);
}


// Static functions: field writers that borrow a sub-resource (string/array).
/* Writes a string entry, or nothing when @p string is NULL. */
static Error PutString(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, const unsigned char* string)
{
    if (string == NULL)
    {
        return Error_CreateSuccess();
    }

    GenericBuffer* Buffer = NULL;
    Error Result = GHDFObjectPool_BorrowString(pool, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (!GenericBuffer_AppendString(Buffer, string) || !GenericBuffer_NullTerminate(Buffer))
    {
        Error ReturnResult = GHDFObjectPool_ReturnString(pool, Buffer);
        Error_Deconstruct(&ReturnResult);
        return Error_Construct2(ErrorCode_EncodeError, "WorldEncoder: failed to build a string buffer.");
    }

    GHDFObjectValue Value = GHDFObjectValue_CreateString(Buffer);
    Result = GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_String), &Value);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnString(pool, Buffer);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    return Error_CreateSuccess();
}

/* Writes a fixed-length float array entry from the components of a vector. */
static Error PutVector3(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, Vector3 vector)
{
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_Float, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    const float Components[3] = { vector.x, vector.y, vector.z };
    for (size_t Index = 0; Index < 3; Index++)
    {
        GHDFObjectValue Value = GHDFObjectValue_CreateFloat(Components[Index]);
        Result = GHDFArray_AddValue(Array, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
            Error_Deconstruct(&ReturnResult);
            return Result;
        }
    }

    GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(Array, GHDFValueType_Float);
    Result = GHDFCompound_SetValue(compound, id, GHDF_CreateArrayType(GHDFValueType_Float), &ArrayValue);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    return Error_CreateSuccess();
}

/* Writes an RGBA color as a 4-element uint8 array entry. */
static Error PutColor(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, Color color)
{
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_UInt8, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    const unsigned char Components[4] = { color.r, color.g, color.b, color.a };
    for (size_t Index = 0; Index < 4; Index++)
    {
        GHDFObjectValue Value = GHDFObjectValue_CreateUInt8(Components[Index]);
        Result = GHDFArray_AddValue(Array, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
            Error_Deconstruct(&ReturnResult);
            return Result;
        }
    }

    GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(Array, GHDFValueType_UInt8);
    Result = GHDFCompound_SetValue(compound, id, GHDF_CreateArrayType(GHDFValueType_UInt8), &ArrayValue);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    return Error_CreateSuccess();
}


// Static functions: sub-tree encoders.
static Error EncodeObject(GHDFObjectPool* pool, GHDFCompound* compound, const WorldObjectDTO* record)
{
    Error Result = PutUInt8(compound, WorldEncoderObjectField_Type, (uint8_t)record->Type);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = PutUInt64(compound, WorldEncoderObjectField_Id, record->Id);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = PutString(pool, compound, WorldEncoderObjectField_Name, record->Name);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = PutVector3(pool, compound, WorldEncoderObjectField_Position, record->Position);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = PutVector3(pool, compound, WorldEncoderObjectField_Rotation, record->Rotation);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = PutVector3(pool, compound, WorldEncoderObjectField_Scale, record->Scale);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = PutColor(pool, compound, WorldEncoderObjectField_TintColor, record->Tint.Tint);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = PutFloat(compound, WorldEncoderObjectField_TintBrightness, record->Tint.Brightness);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = PutFloat(compound, WorldEncoderObjectField_TintOpacity, record->Tint.Opacity);
    if (Result.Code != ErrorCode_Success) { return Result; }

    switch (record->Type)
    {
        case WorldObjectType_Model:
            Result = PutString(pool, compound, WorldEncoderObjectField_AssetName, record->Data.Model.ModelAssetName);
            if (Result.Code != ErrorCode_Success) { return Result; }
            Result = PutBool(compound, WorldEncoderObjectField_HasOutline, record->Data.Model.HasOutline);
            if (Result.Code != ErrorCode_Success) { return Result; }
            Result = PutBool(compound, WorldEncoderObjectField_OmitPixelation, record->Data.Model.OmitPixelation);
            if (Result.Code != ErrorCode_Success) { return Result; }
            return PutUInt8(compound, WorldEncoderObjectField_ShadowTier, (uint8_t)record->Data.Model.ShadowTier);

        case WorldObjectType_Sprite:
            Result = PutString(pool, compound, WorldEncoderObjectField_AssetName,
                record->Data.Sprite.SpriteAnimationAssetName);
            if (Result.Code != ErrorCode_Success) { return Result; }
            Result = PutBool(compound, WorldEncoderObjectField_HasOutline, record->Data.Sprite.HasOutline);
            if (Result.Code != ErrorCode_Success) { return Result; }
            return PutBool(compound, WorldEncoderObjectField_OmitPixelation, record->Data.Sprite.OmitPixelation);

        case WorldObjectType_Light:
            Result = PutColor(pool, compound, WorldEncoderObjectField_LightColor, record->Data.Light.Color);
            if (Result.Code != ErrorCode_Success) { return Result; }
            Result = PutFloat(compound, WorldEncoderObjectField_LightIntensity, record->Data.Light.Intensity);
            if (Result.Code != ErrorCode_Success) { return Result; }
            Result = PutFloat(compound, WorldEncoderObjectField_LightSize, record->Data.Light.Size);
            if (Result.Code != ErrorCode_Success) { return Result; }
            return PutBool(compound, WorldEncoderObjectField_LightCastsShadows, record->Data.Light.CastsShadows);
    }
    // No case matched: the record carries a value outside WorldObjectType (corrupted data).
    return Error_Construct2(ErrorCode_EncodeError, "WorldEncoder: unknown object type.");
}

static Error EncodeObjects(GHDFObjectPool* pool, GHDFArray* array, const WorldDTO* dto)
{
    for (size_t Index = 0; Index < dto->_objects._count; Index++)
    {
        const WorldObjectDTO* Record = GenericBuffer_GetPointerToElement((GenericBuffer*)&dto->_objects, Index);

        GHDFCompound* ObjectCompound = NULL;
        Error Result = GHDFObjectPool_BorrowCompound(pool, &ObjectCompound);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = EncodeObject(pool, ObjectCompound, Record);
        if (Result.Code != ErrorCode_Success)
        {
            Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, ObjectCompound, true);
            Error_Deconstruct(&ReturnResult);
            return Result;
        }

        GHDFObjectValue Value = GHDFObjectValue_CreateCompound(ObjectCompound);
        Result = GHDFArray_AddValue(array, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, ObjectCompound, true);
            Error_Deconstruct(&ReturnResult);
            return Result;
        }
    }
    return Error_CreateSuccess();
}

static Error EncodeRoot(GHDFObjectPool* pool, GHDFCompound* root, const WorldDTO* dto)
{
    Error Result = PutUInt32(root, WorldEncoderRootField_FormatVersion, WORLD_ENCODER_FORMAT_VERSION);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = PutString(pool, root, WorldEncoderRootField_Name, dto->Name);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = PutUInt64(root, WorldEncoderRootField_NextObjectId, dto->NextObjectId);
    if (Result.Code != ErrorCode_Success) { return Result; }

    // Environment sub-compound.
    GHDFCompound* EnvironmentCompound = NULL;
    Result = GHDFObjectPool_BorrowCompound(pool, &EnvironmentCompound);
    if (Result.Code != ErrorCode_Success) { return Result; }
    // Every setting, defaults included, under the table's wire ids: the same entries the hand-written encoder wrote.
    PropertyWriteOptions EnvironmentOptions = PropertyWriteOptions_CreateDefault();
    EnvironmentOptions.AreDefaultsWritten = true;
    Result = Properties_WriteGHDF(WorldEnvironment_GetPropertyTable(), &dto->Environment, pool, EnvironmentCompound,
        &EnvironmentOptions);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, EnvironmentCompound, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    GHDFObjectValue EnvironmentValue = GHDFObjectValue_CreateCompound(EnvironmentCompound);
    Result = GHDFCompound_SetValue(root, WorldEncoderRootField_Environment,
        GHDF_CreateRegularType(GHDFValueType_Compound), &EnvironmentValue);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, EnvironmentCompound, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    // Objects array.
    GHDFArray* ObjectsArray = NULL;
    Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_Compound, &ObjectsArray);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = EncodeObjects(pool, ObjectsArray, dto);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, ObjectsArray, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    GHDFObjectValue ObjectsValue = GHDFObjectValue_CreateArray(ObjectsArray, GHDFValueType_Compound);
    Result = GHDFCompound_SetValue(root, WorldEncoderRootField_Objects,
        GHDF_CreateArrayType(GHDFValueType_Compound), &ObjectsValue);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, ObjectsArray, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    return Error_CreateSuccess();
}


// Public functions.
Error WorldEncoder_EncodeToCompound(const WorldDTO* dto, GHDFObjectPool* pool, GHDFCompound** outCompound)
{
    if ((dto == NULL) || (pool == NULL) || (outCompound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldEncoder_EncodeToCompound: dto, pool and outCompound must not be NULL.");
    }
    *outCompound = NULL;

    GHDFCompound* Root = NULL;
    Error Result = GHDFObjectPool_BorrowCompound(pool, &Root);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = EncodeRoot(pool, Root, dto);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, Root, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    *outCompound = Root;
    return Error_CreateSuccess();
}
