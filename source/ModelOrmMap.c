#include "ModelOrmMap.h"
#include "wr/WRMemory.h"


// Macros.
/** The packed image's bytes per texel (R8G8B8A8). */
#define MODEL_ORM_MAP_TEXEL_SIZE ((size_t)4)
/** The channels the maps fill: red, green and blue, in ModelOrmSources' order. */
#define MODEL_ORM_MAP_CHANNEL_COUNT ((size_t)3)


// Static functions.
/* The bytes a pixel of @p format takes when it is an 8-bit format whose first byte is its red (or its grey), else 0. */
static size_t GetRedStride(int format)
{
    switch (format)
    {
        case PIXELFORMAT_UNCOMPRESSED_GRAYSCALE: return 1U;
        case PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA: return 2U;
        case PIXELFORMAT_UNCOMPRESSED_R8G8B8: return 3U;
        case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8: return 4U;
        default: return 0U;
    }
}

/* Checks one given map: it has pixels, in a format whose red is read, at most MODEL_ORM_MAP_MAX_SIDE a side. */
static Error CheckMap(const Image* map, const char* name)
{
    if ((map->data == NULL) || (map->width <= 0) || (map->height <= 0))
    {
        return Error_Construct4(ErrorCode_InvalidAssetData, "The %s map has no pixels.", name);
    }
    if (GetRedStride(map->format) == 0U)
    {
        return Error_Construct4(ErrorCode_InvalidAssetData,
            "The %s map's pixel format (%d) is not 8-bit grey, grey with alpha, RGB or RGBA.", name, map->format);
    }
    if ((map->width > MODEL_ORM_MAP_MAX_SIDE) || (map->height > MODEL_ORM_MAP_MAX_SIDE))
    {
        return Error_Construct4(ErrorCode_InvalidAssetData, "The %s map is %dx%d pixels: at most %d a side.", name,
            map->width, map->height, MODEL_ORM_MAP_MAX_SIDE);
    }
    return Error_CreateSuccess();
}

/* Copies a checked map's red channel into a grey image in raylib's memory (UnloadImage), resampled to @p width x
 * @p height when its own size differs. */
static Error CopyRed(const Image* map, int width, int height, Image* outRed)
{
    size_t Stride = GetRedStride(map->format);
    size_t PixelCount = (size_t)map->width * (size_t)map->height;
    unsigned char* Red = MemAlloc((unsigned int)PixelCount);
    if (Red == NULL)
    {
        return Error_Construct4(ErrorCode_BufferTooLarge, "A %dx%d grey copy could not be allocated.", map->width,
            map->height);
    }
    const unsigned char* Pixels = map->data;
    for (size_t Pixel = 0U; Pixel < PixelCount; Pixel++)
    {
        Red[Pixel] = Pixels[Pixel * Stride];
    }
    *outRed = (Image){ .data = Red, .width = map->width, .height = map->height, .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE };
    if ((map->width == width) && (map->height == height))
    {
        return Error_CreateSuccess();
    }
    ImageResize(outRed, width, height);
    if (outRed->data == NULL)
    {
        return Error_Construct4(ErrorCode_BufferTooLarge, "A %dx%d grey copy could not be allocated.", width, height);
    }
    return Error_CreateSuccess();
}


// Public functions.
Error ModelOrmMap_Build(const ModelOrmSources* sources, Image* outImage)
{
    Memory_Zero(outImage, sizeof(*outImage));
    const Image* const Maps[MODEL_ORM_MAP_CHANNEL_COUNT] = { sources->Occlusion, sources->Roughness, sources->Metallic };
    static const char* const Names[MODEL_ORM_MAP_CHANNEL_COUNT] = { "occlusion", "roughness", "metallic" };
    const Image* SizeSource = (sources->Roughness != NULL) ? sources->Roughness
        : ((sources->Metallic != NULL) ? sources->Metallic : sources->Occlusion);
    if (SizeSource == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "There is no map to pack.");
    }
    for (size_t Channel = 0U; Channel < MODEL_ORM_MAP_CHANNEL_COUNT; Channel++)
    {
        if (Maps[Channel] == NULL)
        {
            continue;
        }
        Error CheckResult = CheckMap(Maps[Channel], Names[Channel]);
        if (CheckResult.Code != ErrorCode_Success)
        {
            return CheckResult;
        }
    }

    // White: a missing map's channel reads 255, and alpha stays 255.
    int Width = SizeSource->width;
    int Height = SizeSource->height;
    Image Packed = GenImageColor(Width, Height, WHITE);
    unsigned char* Texels = Packed.data;
    size_t TexelCount = (size_t)Width * (size_t)Height;
    for (size_t Channel = 0U; Channel < MODEL_ORM_MAP_CHANNEL_COUNT; Channel++)
    {
        if (Maps[Channel] == NULL)
        {
            continue;
        }
        Image Red;
        Error CopyResult = CopyRed(Maps[Channel], Width, Height, &Red);
        if (CopyResult.Code != ErrorCode_Success)
        {
            UnloadImage(Packed);
            return CopyResult;
        }
        const unsigned char* Values = Red.data;
        for (size_t Texel = 0U; Texel < TexelCount; Texel++)
        {
            Texels[(Texel * MODEL_ORM_MAP_TEXEL_SIZE) + Channel] = Values[Texel];
        }
        UnloadImage(Red);
    }
    *outImage = Packed;
    return Error_CreateSuccess();
}
