#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "SpriteSheet.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include "wr/WRJSON.h"

/*
 * Loads a sprite sheet: each source image is loaded, given its per-image padding border, packed into one
 * atlas texture with a simple shelf packer, and recorded as a named region. Padding modes: "transparent"
 * fills the border transparent; "nearest_pixel" replicates the edge pixels (the default). NOTE:
 * "nearest_pixel_transparent" (edge color with zero alpha) is currently approximated by "nearest_pixel";
 * true zero-alpha edge replication needs per-pixel work and is a TODO.
 */


// Macros.
/* Maximum atlas row width before the shelf packer wraps to a new row. */
#define SPRITE_SHEET_MAX_ATLAS_WIDTH 2048


// Types.
typedef enum SpriteSheetPaddingColorEnum
{
    SpriteSheetPaddingColor_NearestPixel,
    SpriteSheetPaddingColor_Transparent,
    SpriteSheetPaddingColor_NearestPixelTransparent
} SpriteSheetPaddingColor;

typedef struct SpriteSheetImageStruct
{
    unsigned char* Name; // owned
    OwnedAssetLocation Location;
} SpriteSheetImage;

typedef struct SpriteSheetDefinitionStruct
{
    AssetDefinition Base;
    SpriteSheetImage* Images; // owned array
    size_t ImageCount;
    int PaddingX;
    int PaddingY;
    SpriteSheetPaddingColor PaddingColor;
    int Filter;
} SpriteSheetDefinition;

typedef struct SpriteSheetLoadedStruct
{
    SpriteSheet Sheet;      // Asset points here
    Texture2D Texture;      // owned
    GenericBuffer* Entries; // borrowed from the manager buffer pool (SpriteSheetEntry elements)
} SpriteSheetLoaded;

/* Per-image state while packing. */
typedef struct PackItemStruct
{
    Image Padded;
    int OriginalWidth;
    int OriginalHeight;
    int X;
    int Y;
} PackItem;


// Static functions.
static bool StringsEqual(const unsigned char* a, const unsigned char* b)
{
    bool Equal = false;
    Error Result = StringUTF8_EqualsExact(a, b, &Equal);
    if (Result.Code != ErrorCode_Success) { Error_Deconstruct(&Result); return false; }
    return Equal;
}

static Image BuildPaddedImage(Image original, int padX, int padY, SpriteSheetPaddingColor mode)
{
    int OriginalWidth = original.width;
    int OriginalHeight = original.height;
    Color Transparent = { 0, 0, 0, 0 };
    Image Canvas = GenImageColor(OriginalWidth + (2 * padX), OriginalHeight + (2 * padY), Transparent);

    Rectangle Source = { 0.0f, 0.0f, (float)OriginalWidth, (float)OriginalHeight };
    Rectangle Destination = { (float)padX, (float)padY, (float)OriginalWidth, (float)OriginalHeight };
    ImageDraw(&Canvas, original, Source, Destination, WHITE);

    bool ReplicateEdges = (mode == SpriteSheetPaddingColor_NearestPixel) || (mode == SpriteSheetPaddingColor_NearestPixelTransparent);
    if (ReplicateEdges && (padX > 0))
    {
        Rectangle LeftSource = { 0.0f, 0.0f, 1.0f, (float)OriginalHeight };
        Rectangle LeftDestination = { 0.0f, (float)padY, (float)padX, (float)OriginalHeight };
        ImageDraw(&Canvas, original, LeftSource, LeftDestination, WHITE);
        Rectangle RightSource = { (float)(OriginalWidth - 1), 0.0f, 1.0f, (float)OriginalHeight };
        Rectangle RightDestination = { (float)(padX + OriginalWidth), (float)padY, (float)padX, (float)OriginalHeight };
        ImageDraw(&Canvas, original, RightSource, RightDestination, WHITE);
    }
    if (ReplicateEdges && (padY > 0))
    {
        Rectangle TopSource = { 0.0f, 0.0f, (float)OriginalWidth, 1.0f };
        Rectangle TopDestination = { (float)padX, 0.0f, (float)OriginalWidth, (float)padY };
        ImageDraw(&Canvas, original, TopSource, TopDestination, WHITE);
        Rectangle BottomSource = { 0.0f, (float)(OriginalHeight - 1), (float)OriginalWidth, 1.0f };
        Rectangle BottomDestination = { (float)padX, (float)(padY + OriginalHeight), (float)OriginalWidth, (float)padY };
        ImageDraw(&Canvas, original, BottomSource, BottomDestination, WHITE);
    }
    if (ReplicateEdges && (padX > 0) && (padY > 0))
    {
        Rectangle Corners[4] = {
            { 0.0f, 0.0f, 1.0f, 1.0f },
            { (float)(OriginalWidth - 1), 0.0f, 1.0f, 1.0f },
            { 0.0f, (float)(OriginalHeight - 1), 1.0f, 1.0f },
            { (float)(OriginalWidth - 1), (float)(OriginalHeight - 1), 1.0f, 1.0f },
        };
        Rectangle CornerDestinations[4] = {
            { 0.0f, 0.0f, (float)padX, (float)padY },
            { (float)(padX + OriginalWidth), 0.0f, (float)padX, (float)padY },
            { 0.0f, (float)(padY + OriginalHeight), (float)padX, (float)padY },
            { (float)(padX + OriginalWidth), (float)(padY + OriginalHeight), (float)padX, (float)padY },
        };
        for (size_t i = 0; i < 4U; i++)
        {
            ImageDraw(&Canvas, original, Corners[i], CornerDestinations[i], WHITE);
        }
    }
    return Canvas;
}

static void UnloadPackedImages(PackItem* items, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        UnloadImage(items[i].Padded);
    }
}

static void SpriteSheetLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    SpriteSheetLoaded* Loaded = self->DestroyContext;
    UnloadTexture(Loaded->Texture);
    Error DeconstructResult = SpriteSheet_Deconstruct(&Loaded->Sheet);
    Error_Deconstruct(&DeconstructResult);
    Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Loaded->Entries);
    Error_Deconstruct(&ReturnResult);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable SpriteSheetLoadedVTable = { SpriteSheetLoaded_Destroy };

/* Loads every source image (with padding) into @p items. On failure unloads what it loaded. */
static Error LoadSourceImages(SpriteSheetDefinition* definition, AssetManager* manager, PackItem* items)
{
    for (size_t i = 0; i < definition->ImageCount; i++)
    {
        SpriteSheetImage* SourceImage = &definition->Images[i];
        AssetLocation Location = OwnedAssetLocation_View(&SourceImage->Location);
        AssetResourcePath* PathHandle = NULL;
        Error PathResult = AssetManager_AcquireResourcePath(manager, definition->Base.Type, &Location, NULL, &PathHandle);
        if (PathResult.Code != ErrorCode_Success)
        {
            UnloadPackedImages(items, i);
            return PathResult;
        }

        Image Original = LoadImage((const char*)AssetResourcePath_Get(PathHandle));
        Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, PathHandle);
        Error_Deconstruct(&ReleaseResult);

        if ((Original.data == NULL) || (Original.width <= 0) || (Original.height <= 0))
        {
            UnloadImage(Original);
            UnloadPackedImages(items, i);
            return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to load sprite sheet image \"%s\".", SourceImage->Name);
        }

        items[i].OriginalWidth = Original.width;
        items[i].OriginalHeight = Original.height;
        items[i].Padded = BuildPaddedImage(Original, definition->PaddingX, definition->PaddingY, definition->PaddingColor);
        UnloadImage(Original);
    }
    return Error_CreateSuccess();
}

/* Shelf-packs @p items and writes the resulting atlas dimensions. */
static void PackImages(PackItem* items, size_t count, int* outAtlasWidth, int* outAtlasHeight)
{
    int X = 0;
    int Y = 0;
    int RowHeight = 0;
    int AtlasWidth = 0;
    for (size_t i = 0; i < count; i++)
    {
        int Width = items[i].Padded.width;
        int Height = items[i].Padded.height;
        if ((X > 0) && ((X + Width) > SPRITE_SHEET_MAX_ATLAS_WIDTH))
        {
            X = 0;
            Y += RowHeight;
            RowHeight = 0;
        }
        items[i].X = X;
        items[i].Y = Y;
        X += Width;
        if (X > AtlasWidth) { AtlasWidth = X; }
        if (Height > RowHeight) { RowHeight = Height; }
    }
    *outAtlasWidth = (AtlasWidth > 0) ? AtlasWidth : 1;
    *outAtlasHeight = ((Y + RowHeight) > 0) ? (Y + RowHeight) : 1;
}

static Error BuildEntries(SpriteSheetDefinition* definition, PackItem* items, GenericBuffer* entries)
{
    for (size_t i = 0; i < definition->ImageCount; i++)
    {
        SpriteSheetImage* SourceImage = &definition->Images[i];
        size_t NameLength = StringUTF8_GetByteLength(SourceImage->Name);
        if (NameLength >= (size_t)SPRITE_SHEET_ENTRY_MAX_NAME_LENGTH)
        {
            return Error_Construct3(ErrorCode_InvalidAssetData, u8"Sprite image name \"%s\" is too long.", SourceImage->Name);
        }

        SpriteSheetEntry Entry;
        Memory_Zero(&Entry, sizeof(Entry));
        Memory_Copy(SourceImage->Name, Entry._name, NameLength + 1U);
        Entry._textureArea.x = (float)(items[i].X + definition->PaddingX);
        Entry._textureArea.y = (float)(items[i].Y + definition->PaddingY);
        Entry._textureArea.width = (float)items[i].OriginalWidth;
        Entry._textureArea.height = (float)items[i].OriginalHeight;

        if (!GenericBuffer_AddLast(entries, &Entry))
        {
            return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to store sprite sheet entry.");
        }
    }
    return Error_CreateSuccess();
}

static Error SpriteSheetDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    SpriteSheetDefinition* Definition = self;

    size_t ItemsByteCount = 0;
    if (!Memory_TryMultiplySize(Definition->ImageCount, sizeof(PackItem), &ItemsByteCount))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Too many sprite sheet images.");
    }
    PackItem* Items = Memory_Allocate(ItemsByteCount);
    Memory_Zero(Items, ItemsByteCount);

    Error Result = LoadSourceImages(Definition, manager, Items);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Items);
        return Result;
    }

    int AtlasWidth = 0;
    int AtlasHeight = 0;
    PackImages(Items, Definition->ImageCount, &AtlasWidth, &AtlasHeight);

    Color Transparent = { 0, 0, 0, 0 };
    Image Atlas = GenImageColor(AtlasWidth, AtlasHeight, Transparent);
    for (size_t i = 0; i < Definition->ImageCount; i++)
    {
        Rectangle Source = { 0.0f, 0.0f, (float)Items[i].Padded.width, (float)Items[i].Padded.height };
        Rectangle Destination = { (float)Items[i].X, (float)Items[i].Y, (float)Items[i].Padded.width, (float)Items[i].Padded.height };
        ImageDraw(&Atlas, Items[i].Padded, Source, Destination, WHITE);
    }

    GenericBuffer* Entries = NULL;
    Error BorrowResult = AssetManager_BorrowGenericBuffer(manager, sizeof(SpriteSheetEntry), &Entries);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        UnloadImage(Atlas);
        UnloadPackedImages(Items, Definition->ImageCount);
        Memory_Free(Items);
        return BorrowResult;
    }

    Error EntriesResult = BuildEntries(Definition, Items, Entries);
    if (EntriesResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Entries);
        Error_Deconstruct(&ReturnResult);
        UnloadImage(Atlas);
        UnloadPackedImages(Items, Definition->ImageCount);
        Memory_Free(Items);
        return EntriesResult;
    }

    Texture2D Texture = LoadTextureFromImage(Atlas);
    SetTextureFilter(Texture, Definition->Filter);
    UnloadImage(Atlas);
    UnloadPackedImages(Items, Definition->ImageCount);
    Memory_Free(Items);

    if (Texture.id == 0)
    {
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Entries);
        Error_Deconstruct(&ReturnResult);
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to upload sprite sheet texture \"%s\".", Definition->Base.Name);
    }

    SpriteSheetLoaded* Loaded = Memory_Allocate(sizeof(SpriteSheetLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Texture = Texture;
    Loaded->Entries = Entries;
    Error ConstructResult = SpriteSheet_Construct1(&Loaded->Sheet, Texture, Entries);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        UnloadTexture(Texture);
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Entries);
        Error_Deconstruct(&ReturnResult);
        Memory_Free(Loaded);
        return ConstructResult;
    }

    outLoaded->Asset = &Loaded->Sheet;
    outLoaded->VTable = &SpriteSheetLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void SpriteSheetDefinition_Destroy(void* self)
{
    SpriteSheetDefinition* Definition = self;
    for (size_t i = 0; i < Definition->ImageCount; i++)
    {
        Memory_Free(Definition->Images[i].Name);
        OwnedAssetLocation_Deconstruct(&Definition->Images[i].Location);
    }
    Memory_Free(Definition->Images);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable SpriteSheetDefinitionVTable = { SpriteSheetDefinition_LoadAsset, SpriteSheetDefinition_Destroy };

static Error ParseImage(JSONCompound* compound, const unsigned char* sourceDescription, SpriteSheetImage* outImage)
{
    JSONObjectValue NameValue;
    Error NameResult = JSONCompound_GetVerified(compound, (const unsigned char*)u8"name", JSONValueType_String, &NameValue);
    if (NameResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&NameResult);
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"A sprite sheet image requires a string \"name\".");
    }
    Error NameCopyResult = AssetJSON_ValueToOwnedString(&NameValue, &outImage->Name);
    if (NameCopyResult.Code != ErrorCode_Success) { return NameCopyResult; }

    return AssetJSON_ReadLocation(compound, (const unsigned char*)u8"location", sourceDescription, &outImage->Location);
}

static Error ParsePaddingColor(JSONCompound* root, SpriteSheetPaddingColor* outColor)
{
    *outColor = SpriteSheetPaddingColor_NearestPixel;
    unsigned char* Text = NULL;
    bool Found = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(root, (const unsigned char*)u8"padding_color", &Text, &Found);
    if (Result.Code != ErrorCode_Success) { return Result; }
    if (!Found) { return Error_CreateSuccess(); }

    Error MatchResult = Error_CreateSuccess();
    if (StringsEqual(Text, (const unsigned char*)u8"nearest_pixel"))
    {
        *outColor = SpriteSheetPaddingColor_NearestPixel;
    }
    else if (StringsEqual(Text, (const unsigned char*)u8"transparent"))
    {
        *outColor = SpriteSheetPaddingColor_Transparent;
    }
    else if (StringsEqual(Text, (const unsigned char*)u8"nearest_pixel_transparent"))
    {
        *outColor = SpriteSheetPaddingColor_NearestPixelTransparent;
    }
    else
    {
        MatchResult = Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Invalid padding_color \"%s\".", Text);
    }
    Memory_Free(Text);
    return MatchResult;
}

static Error ParseImages(SpriteSheetDefinition* definition, JSONCompound* root, const unsigned char* sourceDescription)
{
    JSONObjectValue ImagesValue;
    Error GetResult = JSONCompound_GetVerified(root, (const unsigned char*)u8"images", JSONValueType_Array, &ImagesValue);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"A sprite sheet requires an \"images\" array.");
    }

    JSONArray* Array = ImagesValue.Value.Array;
    size_t Count = JSONArray_GetElementCount(Array);
    if (Count == 0U)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"A sprite sheet requires at least one image.");
    }

    size_t ByteCount = 0;
    if (!Memory_TryMultiplySize(Count, sizeof(SpriteSheetImage), &ByteCount))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Too many sprite sheet images.");
    }
    definition->Images = Memory_Allocate(ByteCount);
    Memory_Zero(definition->Images, ByteCount);
    definition->ImageCount = Count;

    for (size_t i = 0; i < Count; i++)
    {
        JSONObjectValue Element;
        Error ElementResult = JSONArray_GetVerified(Array, i, JSONValueType_Compound, &Element);
        if (ElementResult.Code != ErrorCode_Success) { return ElementResult; }
        Error ImageResult = ParseImage(Element.Value.Compound, sourceDescription, &definition->Images[i]);
        if (ImageResult.Code != ErrorCode_Success) { return ImageResult; }
    }
    return Error_CreateSuccess();
}


// Public functions.
Error SpriteSheetDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    SpriteSheetDefinition* Definition = Memory_Allocate(sizeof(SpriteSheetDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &SpriteSheetDefinitionVTable;
    Definition->Filter = TEXTURE_FILTER_BILINEAR;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseImages(Definition, RootCompound, sourceDescription);
    }
    if (Result.Code == ErrorCode_Success)
    {
        int64_t PaddingX = 0;
        int64_t PaddingY = 0;
        Result = AssetJSON_ReadOptionalVectorInt(RootCompound, (const unsigned char*)u8"padding_pixels", 0, 0, &PaddingX, &PaddingY);
        if (Result.Code == ErrorCode_Success)
        {
            Definition->PaddingX = (PaddingX > 0) ? (int)PaddingX : 0;
            Definition->PaddingY = (PaddingY > 0) ? (int)PaddingY : 0;
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParsePaddingColor(RootCompound, &Definition->PaddingColor);
    }
    if (Result.Code == ErrorCode_Success)
    {
        TextureProperties Properties;
        Result = AssetJSON_ReadTextureProperties(RootCompound, (const unsigned char*)u8"texture_properties", &Properties);
        if (Result.Code == ErrorCode_Success) { Definition->Filter = Properties.Filter; }
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        SpriteSheetDefinition_Destroy(Definition);
        return Result;
    }

    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
