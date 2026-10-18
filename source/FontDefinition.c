#include <inttypes.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "FontCodepoints.h"
#include "GameFont.h"
#include "Logger.h"
#include "wr/WRMemory.h"
#include "wr/WRJSON.h"
#include "wr/WRString.h"


// Macros.
/* Pixel height glyphs are rasterized at when the definition has no "size"; drawing scales from it. */
#define FONT_DEFAULT_PIXEL_SIZE (32)
/* Accepted "size" range, in pixels. */
#define FONT_PIXEL_SIZE_MIN (8)
#define FONT_PIXEL_SIZE_MAX (256)
/* How many missing codepoints the coverage warning lists. */
#define FONT_MISSING_LISTED_MAX ((size_t)8)
/* What this loader describes, for parse-time messages. */
#define FONT_ASSET_KIND ((const unsigned char*)u8"font")


// Types.
typedef struct FontDefinitionStruct
{
    AssetDefinition Base;
    OwnedAssetLocation Location;
    unsigned char* Format; // owned; validated extension: narrows the file choice, names a reference's type
    int PixelSize;         // rasterization height in pixels
    int* Codepoints;       // owned; ascending and unique, '?' always included (LoadFontEx takes an int array)
    int CodepointCount;
    int Filter;            // a TEXTURE_FILTER_* value
} FontDefinition;

typedef struct FontLoadedStruct
{
    GameFont Font; // Asset points here
} FontLoaded;


// Static functions: parsing.
static Error ReadPixelSize(JSONCompound* root, int* outPixelSize)
{
    int64_t PixelSize = 0;
    Error Result = AssetJSON_ReadOptionalInteger(root, (const unsigned char*)u8"size", FONT_DEFAULT_PIXEL_SIZE, &PixelSize);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((PixelSize < FONT_PIXEL_SIZE_MIN) || (PixelSize > FONT_PIXEL_SIZE_MAX))
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"\"size\" is %" PRId64 u8"; it must be within [%d, %d].",
            PixelSize, FONT_PIXEL_SIZE_MIN, FONT_PIXEL_SIZE_MAX);
    }
    *outPixelSize = (int)PixelSize;
    return Error_CreateSuccess();
}


// Static functions: loading.
// Logs once, at load time, which requested codepoints the font file lacks (they draw as '?').
static void ReportMissingCodepoints(AssetManager* manager, const FontDefinition* definition, const GameFont* font)
{
    Logger* Log = AssetManager_GetLogger(manager);
    if (Log == NULL)
    {
        return;
    }

    GenericBuffer Listed;
    GenericBuffer_AllocateVariable(&Listed, 0U, sizeof(unsigned char));
    int MissingCount = 0;
    for (int Index = 0; Index < definition->CodepointCount; Index++)
    {
        CodePoint Requested = (CodePoint)definition->Codepoints[Index];
        if (GameFont_HasGlyph(font, Requested))
        {
            continue;
        }
        MissingCount++;
        if ((size_t)MissingCount <= FONT_MISSING_LISTED_MAX)
        {
            Error FormatResult = StringUTF8_Format(u8" U+%04" PRIX32, &Listed, (uint32_t)Requested);
            Error_Deconstruct(&FormatResult);
        }
    }

    if (MissingCount > 0)
    {
        Error LogResult = Logger_LogWarningFormatted(Log,
            u8"Font \"%s\": %d of the %d requested codepoints are not in its file and draw as '?' (first:%s%s).",
            (const char*)definition->Base.Name, MissingCount, definition->CodepointCount,
            (Listed._data != NULL) ? (const char*)Listed._data : "",
            ((size_t)MissingCount > FONT_MISSING_LISTED_MAX) ? " ..." : "");
        Error_Deconstruct(&LogResult);
    }
    Memory_Free(Listed._data);
}

static void FontLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    FontLoaded* Loaded = self->DestroyContext;
    UnloadFont(Loaded->Font._rayFont);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable FontLoadedVTable = { FontLoaded_Destroy };

static Error FontDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    FontDefinition* Definition = self;

    const unsigned char* FormatExtension = Definition->Format;
    AssetExtensionFilter Extensions = AssetFormat_SelectFilter(&ASSET_EXTENSIONS_FONT, &FormatExtension);
    AssetLocation Location = OwnedAssetLocation_View(&Definition->Location);
    AssetResourcePath* PathHandle = NULL;
    Error PathResult = AssetManager_AcquireResourcePath(manager, Definition->Base.Type, &Location, &Extensions,
        Definition->Format, &PathHandle);
    if (PathResult.Code != ErrorCode_Success) { return PathResult; }

    Font RayFont = LoadFontEx((const char*)AssetResourcePath_Get(PathHandle), Definition->PixelSize, Definition->Codepoints,
        Definition->CodepointCount);
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, PathHandle);
    Error_Deconstruct(&ReleaseResult);

    if (RayFont.texture.id == 0)
    {
        UnloadFont(RayFont);
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to load font \"%s\".", Definition->Base.Name);
    }
    SetTextureFilter(RayFont.texture, Definition->Filter);

    FontLoaded* Loaded = Memory_Allocate(sizeof(FontLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Font = GameFont_Wrap(RayFont);
    ReportMissingCodepoints(manager, Definition, &Loaded->Font);

    outLoaded->Asset = &Loaded->Font;
    outLoaded->VTable = &FontLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void FontDefinition_Destroy(void* self)
{
    FontDefinition* Definition = self;
    OwnedAssetLocation_Deconstruct(&Definition->Location);
    Memory_Free(Definition->Format);
    Memory_Free(Definition->Codepoints);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable FontDefinitionVTable = { FontDefinition_LoadAsset, FontDefinition_Destroy };


// Public functions.
Error FontDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    FontDefinition* Definition = Memory_Allocate(sizeof(FontDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &FontDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadLocation(RootCompound, (const unsigned char*)u8"location", sourceDescription, &Definition->Location);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalFormat(RootCompound, &ASSET_EXTENSIONS_FONT, FONT_ASSET_KIND, &Definition->Format);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = OwnedAssetLocation_ValidateReferenceFormat(&Definition->Location, Definition->Format,
            &ASSET_EXTENSIONS_FONT, FONT_ASSET_KIND);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadPixelSize(RootCompound, &Definition->PixelSize);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = FontCodepoints_Read(RootCompound, Definition->PixelSize, &Definition->Codepoints,
            &Definition->CodepointCount);
    }
    if (Result.Code == ErrorCode_Success)
    {
        TextureProperties Properties;
        Result = AssetJSON_ReadTextureProperties(RootCompound, (const unsigned char*)u8"texture_properties", &Properties);
        Definition->Filter = Properties.Filter;
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        FontDefinition_Destroy(Definition);
        return Result;
    }

    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
