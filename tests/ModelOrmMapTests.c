#include "TestSuites.h"
#include "ModelOrmMap.h"
#include "wr/WRMemory.h"
#include <string.h>


// Static functions: helpers.
/* An image of @p format holding a copy of @p bytes (GetPixelDataSize of them), in raylib's memory (UnloadImage). */
static Image MakeImage(int width, int height, int format, const unsigned char* bytes)
{
    int Size = GetPixelDataSize(width, height, format);
    unsigned char* Data = MemAlloc((unsigned int)Size);
    Memory_Copy(bytes, Data, (size_t)Size);
    return (Image){ .data = Data, .width = width, .height = height, .mipmaps = 1, .format = format };
}

/* A grey image of @p width x @p height whose every pixel is @p value. */
static Image MakeUniformGrey(int width, int height, unsigned char value)
{
    unsigned char Bytes[64];
    memset(Bytes, value, sizeof(Bytes));
    return MakeImage(width, height, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, Bytes);
}

/* One channel (0 red to 3 alpha) of a packed image's pixel. */
static unsigned char GetChannel(const Image* image, int x, int y, int channel)
{
    const unsigned char* Texels = image->data;
    return Texels[((((size_t)y * (size_t)image->width) + (size_t)x) * 4U) + (size_t)channel];
}

/* Whether every pixel of a packed image has @p value in @p channel. */
static bool IsChannelUniform(const Image* image, int channel, unsigned char value)
{
    for (int Y = 0; Y < image->height; Y++)
    {
        for (int X = 0; X < image->width; X++)
        {
            if (GetChannel(image, X, Y, channel) != value)
            {
                return false;
            }
        }
    }
    return true;
}

static bool IsPackedImage(const Image* image, int width, int height)
{
    return (image->data != NULL) && (image->width == width) && (image->height == height) && (image->mipmaps == 1)
        && (image->format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
}

/* Records that @p result carries @p code and a message holding @p text, then deconstructs it. */
static void CheckRefused(TestRun* run, Error result, ErrorCode code, const char* text)
{
    TEST_CHECK(run, result.Code == code);
    TEST_CHECK(run, (result.Message != NULL) && (strstr((const char*)result.Message, text) != NULL));
    Error_Deconstruct(&result);
}


// Tests.
static void EachMapLandsInItsChannel(TestRun* run)
{
    // Three 4x2 grey maps of distinct values: red the occlusion, green the roughness, blue the metallic, alpha 255.
    unsigned char OcclusionBytes[8];
    unsigned char RoughnessBytes[8];
    unsigned char MetallicBytes[8];
    for (unsigned char Index = 0U; Index < 8U; Index++)
    {
        OcclusionBytes[Index] = (unsigned char)(10U + Index);
        RoughnessBytes[Index] = (unsigned char)(100U + Index);
        MetallicBytes[Index] = (unsigned char)(200U + Index);
    }
    Image Occlusion = MakeImage(4, 2, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, OcclusionBytes);
    Image Roughness = MakeImage(4, 2, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, RoughnessBytes);
    Image Metallic = MakeImage(4, 2, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, MetallicBytes);
    ModelOrmSources Sources = { .Occlusion = &Occlusion, .Roughness = &Roughness, .Metallic = &Metallic };
    Image Packed;
    if (TEST_CHECK_SUCCESS(run, ModelOrmMap_Build(&Sources, &Packed)) && TEST_CHECK(run, IsPackedImage(&Packed, 4, 2)))
    {
        bool IsEveryPixelRight = true;
        for (int Y = 0; Y < 2; Y++)
        {
            for (int X = 0; X < 4; X++)
            {
                unsigned char Index = (unsigned char)((Y * 4) + X);
                IsEveryPixelRight = IsEveryPixelRight && (GetChannel(&Packed, X, Y, 0) == OcclusionBytes[Index])
                    && (GetChannel(&Packed, X, Y, 1) == RoughnessBytes[Index])
                    && (GetChannel(&Packed, X, Y, 2) == MetallicBytes[Index]) && (GetChannel(&Packed, X, Y, 3) == 255U);
            }
        }
        TEST_CHECK(run, IsEveryPixelRight);
    }
    UnloadImage(Packed);
    UnloadImage(Occlusion);
    UnloadImage(Roughness);
    UnloadImage(Metallic);
}

static void OnlyTheRedOfEachMapIsRead(TestRun* run)
{
    // An occlusion map in colour (as glTF's occlusion texture, often the image the metallicRoughness one shares): its
    // red is read in every 8-bit layout, never its green, blue or brightness.
    static const unsigned char Rgb[6] = { 77, 1, 2, 78, 250, 251 };
    static const unsigned char Rgba[8] = { 77, 1, 2, 3, 78, 250, 251, 0 };
    static const unsigned char GreyAlpha[4] = { 77, 9, 78, 0 };
    static const int Formats[3] =
    {
        PIXELFORMAT_UNCOMPRESSED_R8G8B8, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA
    };
    const unsigned char* const Bytes[3] = { Rgb, Rgba, GreyAlpha };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Image Occlusion = MakeImage(2, 1, Formats[Index], Bytes[Index]);
        ModelOrmSources Sources = { .Occlusion = &Occlusion, .Roughness = NULL, .Metallic = NULL };
        Image Packed;
        if (TEST_CHECK_SUCCESS(run, ModelOrmMap_Build(&Sources, &Packed)) && TEST_CHECK(run, IsPackedImage(&Packed, 2, 1)))
        {
            TEST_CHECK(run, (GetChannel(&Packed, 0, 0, 0) == 77U) && (GetChannel(&Packed, 1, 0, 0) == 78U));
        }
        UnloadImage(Packed);
        UnloadImage(Occlusion);
    }
}

static void AMissingMapReads255(TestRun* run)
{
    // The metallic alone: red and green 255, so the occlusion and roughness factors apply alone.
    Image Metallic = MakeUniformGrey(2, 2, 90U);
    ModelOrmSources MetallicOnly = { .Occlusion = NULL, .Roughness = NULL, .Metallic = &Metallic };
    Image Packed;
    if (TEST_CHECK_SUCCESS(run, ModelOrmMap_Build(&MetallicOnly, &Packed)) && TEST_CHECK(run, IsPackedImage(&Packed, 2, 2)))
    {
        TEST_CHECK(run, IsChannelUniform(&Packed, 0, 255U) && IsChannelUniform(&Packed, 1, 255U)
            && IsChannelUniform(&Packed, 2, 90U) && IsChannelUniform(&Packed, 3, 255U));
    }
    UnloadImage(Packed);

    // The occlusion alone: the image is its size, green and blue 255.
    Image Occlusion = MakeUniformGrey(3, 5, 40U);
    ModelOrmSources OcclusionOnly = { .Occlusion = &Occlusion, .Roughness = NULL, .Metallic = NULL };
    if (TEST_CHECK_SUCCESS(run, ModelOrmMap_Build(&OcclusionOnly, &Packed)) && TEST_CHECK(run, IsPackedImage(&Packed, 3, 5)))
    {
        TEST_CHECK(run, IsChannelUniform(&Packed, 0, 40U) && IsChannelUniform(&Packed, 1, 255U)
            && IsChannelUniform(&Packed, 2, 255U) && IsChannelUniform(&Packed, 3, 255U));
    }
    UnloadImage(Packed);
    UnloadImage(Metallic);
    UnloadImage(Occlusion);
}

static void TheSizeIsTheRoughnessMapsThenTheMetallicMaps(TestRun* run)
{
    // Three sizes: the roughness map's wins, and the others are resampled to it (a uniform map stays uniform).
    Image Occlusion = MakeUniformGrey(8, 4, 77U);
    Image Roughness = MakeUniformGrey(4, 2, 60U);
    Image Metallic = MakeUniformGrey(2, 1, 90U);
    ModelOrmSources All = { .Occlusion = &Occlusion, .Roughness = &Roughness, .Metallic = &Metallic };
    Image Packed;
    if (TEST_CHECK_SUCCESS(run, ModelOrmMap_Build(&All, &Packed)) && TEST_CHECK(run, IsPackedImage(&Packed, 4, 2)))
    {
        TEST_CHECK(run, IsChannelUniform(&Packed, 0, 77U) && IsChannelUniform(&Packed, 1, 60U)
            && IsChannelUniform(&Packed, 2, 90U));
    }
    UnloadImage(Packed);

    // Without a roughness map, the metallic map's size.
    ModelOrmSources NoRoughness = { .Occlusion = &Occlusion, .Roughness = NULL, .Metallic = &Metallic };
    if (TEST_CHECK_SUCCESS(run, ModelOrmMap_Build(&NoRoughness, &Packed)) && TEST_CHECK(run, IsPackedImage(&Packed, 2, 1)))
    {
        TEST_CHECK(run, IsChannelUniform(&Packed, 0, 77U) && IsChannelUniform(&Packed, 1, 255U)
            && IsChannelUniform(&Packed, 2, 90U));
    }
    UnloadImage(Packed);
    UnloadImage(Occlusion);
    UnloadImage(Roughness);
    UnloadImage(Metallic);
}

static void AResampledMapKeepsItsGradient(TestRun* run)
{
    // An occlusion ramp twice as wide as the roughness map, and a two-texel metallic ramp twice as narrow: both keep
    // their order from dark to light and their ends (an upscale may overshoot them a little).
    static const unsigned char Ramp[8] = { 0, 32, 64, 96, 128, 160, 192, 224 };
    static const unsigned char Short[2] = { 20, 220 };
    Image Occlusion = MakeImage(8, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, Ramp);
    Image Roughness = MakeUniformGrey(4, 1, 128U);
    Image Metallic = MakeImage(2, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, Short);
    ModelOrmSources All = { .Occlusion = &Occlusion, .Roughness = &Roughness, .Metallic = &Metallic };
    Image Packed;
    if (TEST_CHECK_SUCCESS(run, ModelOrmMap_Build(&All, &Packed)) && TEST_CHECK(run, IsPackedImage(&Packed, 4, 1)))
    {
        bool IsRising = true;
        for (int X = 1; X < 4; X++)
        {
            IsRising = IsRising && (GetChannel(&Packed, X, 0, 0) > GetChannel(&Packed, X - 1, 0, 0))
                && (GetChannel(&Packed, X, 0, 2) >= GetChannel(&Packed, X - 1, 0, 2));
        }
        TEST_CHECK(run, IsRising);
        TEST_CHECK(run, (GetChannel(&Packed, 0, 0, 0) <= 32U) && (GetChannel(&Packed, 3, 0, 0) >= 192U));
        TEST_CHECK(run, (GetChannel(&Packed, 0, 0, 2) <= 40U) && (GetChannel(&Packed, 3, 0, 2) >= 200U));
        TEST_CHECK(run, IsChannelUniform(&Packed, 1, 128U));
    }
    UnloadImage(Packed);
    UnloadImage(Occlusion);
    UnloadImage(Roughness);
    UnloadImage(Metallic);
}

static void MapsItCannotReadAreRefused(TestRun* run)
{
    Image Packed;
    ModelOrmSources None = { .Occlusion = NULL, .Roughness = NULL, .Metallic = NULL };
    TEST_CHECK_ERROR(run, ModelOrmMap_Build(&None, &Packed), ErrorCode_IllegalArgument);
    TEST_CHECK(run, Packed.data == NULL);

    // No pixels, a float format, a compressed one, a side past the limit (checked before a pixel is read).
    unsigned char Bytes[16] = { 0 };
    Image Empty = { .data = NULL, .width = 4, .height = 4, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE };
    Image Float = { .data = Bytes, .width = 2, .height = 2, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R32 };
    Image Compressed = { .data = Bytes, .width = 4, .height = 4, .mipmaps = 1,
        .format = PIXELFORMAT_COMPRESSED_DXT1_RGB };
    Image Huge = { .data = Bytes, .width = MODEL_ORM_MAP_MAX_SIDE + 1, .height = 1, .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE };
    ModelOrmSources EmptyRoughness = { .Occlusion = NULL, .Roughness = &Empty, .Metallic = NULL };
    CheckRefused(run, ModelOrmMap_Build(&EmptyRoughness, &Packed), ErrorCode_InvalidAssetData, "roughness map has no");
    ModelOrmSources FloatOcclusion = { .Occlusion = &Float, .Roughness = NULL, .Metallic = NULL };
    CheckRefused(run, ModelOrmMap_Build(&FloatOcclusion, &Packed), ErrorCode_InvalidAssetData,
        "occlusion map's pixel format");
    ModelOrmSources CompressedMetallic = { .Occlusion = NULL, .Roughness = NULL, .Metallic = &Compressed };
    CheckRefused(run, ModelOrmMap_Build(&CompressedMetallic, &Packed), ErrorCode_InvalidAssetData,
        "metallic map's pixel format");
    ModelOrmSources HugeOcclusion = { .Occlusion = &Huge, .Roughness = NULL, .Metallic = NULL };
    CheckRefused(run, ModelOrmMap_Build(&HugeOcclusion, &Packed), ErrorCode_InvalidAssetData, "8193x1");
    TEST_CHECK(run, Packed.data == NULL);

    // A bad map fails the whole image, even beside good ones.
    Image Roughness = MakeUniformGrey(2, 2, 60U);
    ModelOrmSources Mixed = { .Occlusion = &Float, .Roughness = &Roughness, .Metallic = NULL };
    TEST_CHECK_ERROR(run, ModelOrmMap_Build(&Mixed, &Packed), ErrorCode_InvalidAssetData);
    TEST_CHECK(run, Packed.data == NULL);
    UnloadImage(Roughness);

    // The limit itself is allowed.
    unsigned char* WidestBytes = MemAlloc(MODEL_ORM_MAP_MAX_SIDE);
    memset(WidestBytes, 33, MODEL_ORM_MAP_MAX_SIDE);
    Image Widest = { .data = WidestBytes, .width = MODEL_ORM_MAP_MAX_SIDE, .height = 1, .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE };
    ModelOrmSources WidestMetallic = { .Occlusion = NULL, .Roughness = NULL, .Metallic = &Widest };
    if (TEST_CHECK_SUCCESS(run, ModelOrmMap_Build(&WidestMetallic, &Packed))
        && TEST_CHECK(run, IsPackedImage(&Packed, MODEL_ORM_MAP_MAX_SIDE, 1)))
    {
        TEST_CHECK(run, IsChannelUniform(&Packed, 2, 33U) && IsChannelUniform(&Packed, 0, 255U));
    }
    UnloadImage(Packed);
    UnloadImage(Widest);
}


// Public functions.
void ModelOrmMapTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "each_map_lands_in_its_channel", EachMapLandsInItsChannel },
        { "only_the_red_of_each_map_is_read", OnlyTheRedOfEachMapIsRead },
        { "a_missing_map_reads_255", AMissingMapReads255 },
        { "the_size_is_the_roughness_maps_then_the_metallic_maps", TheSizeIsTheRoughnessMapsThenTheMetallicMaps },
        { "a_resampled_map_keeps_its_gradient", AResampledMapKeepsItsGradient },
        { "maps_it_cannot_read_are_refused", MapsItCannotReadAreRefused },
    };
    TestRun_RunSuite(run, "ModelOrmMap", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
