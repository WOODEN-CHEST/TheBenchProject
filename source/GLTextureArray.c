#include "GLTextureArray.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"

/*
 * 2D texture arrays through the GL functions raylib's loader resolves (GLTextureArray.h): the project's only direct
 * OpenGL calls (decision W7).
 */


// Macros.
/* The GL enumerants used here (the OpenGL 3.3 core specification's values; GL_*_ANISOTROPY is the
 * EXT_texture_filter_anisotropic extension's, core since 4.6). */
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_RGBA8 0x8058
#define GL_RG8 0x822B
#define GL_RGBA 0x1908
#define GL_RG 0x8227
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_NO_ERROR 0U
#define GL_OUT_OF_MEMORY 0x0505U
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
/** The largest layer side and layer count Create accepts (GL 3.3 guarantees 1024 per side and 256 layers; a driver
 *  refusing more fails the creation with a GL error, which is reported). */
#define MAX_SIDE 16384
#define MAX_LAYERS 256
/** The most anisotropy the arrays get: beyond 8 the pixelated picture shows no difference. */
#define MAX_ANISOTROPY 8.0f
/** How many queued GL errors are read off before a creation (a lost context could report one forever). */
#define MAX_DRAINED_ERRORS 32


// Types.
/* Reads a loader's object pointer as a function pointer: ISO C has no cast between the two, a union reads the same bytes
 * (every target platform's function pointers are object-pointer sized: checked below). */
typedef union GLFunctionAddressUnion
{
    void* Object;
    GLGenTexturesFunction GenTextures;
    GLDeleteTexturesFunction DeleteTextures;
    GLBindTextureFunction BindTexture;
    GLTexImage3DFunction TexImage3D;
    GLTexSubImage3DFunction TexSubImage3D;
    GLGenerateMipmapFunction GenerateMipmap;
    GLTexParameteriFunction TexParameteri;
    GLTexParameterfFunction TexParameterf;
    GLPixelStoreiFunction PixelStorei;
    GLGetIntegervFunction GetIntegerv;
    GLGetFloatvFunction GetFloatv;
    GLGetErrorFunction GetError;
} GLFunctionAddress;

_Static_assert(sizeof(void*) == sizeof(GLGetErrorFunction), "GL functions are read through object pointers");


// Static functions.
/* One function's address through raylib's loader; the first one missing is remembered for the message. */
static GLFunctionAddress FindFunction(const char* name, const char** ioMissing)
{
    GLFunctionAddress Address = { .Object = rlGetProcAddress(name) };
    if ((Address.Object == NULL) && (*ioMissing == NULL))
    {
        *ioMissing = name;
    }
    return Address;
}

/* Reads off errors queued by earlier calls, so the next check sees only its own. */
static void DrainErrors(const GLTextureArrayApi* api)
{
    for (int32_t Count = 0; (Count < MAX_DRAINED_ERRORS) && (api->_getError() != GL_NO_ERROR); Count++)
    {
    }
}


// Internal functions (declared in GLTextureArray.h).
bool GLTextureArray_LoadApi(GLTextureArrayApi* outApi)
{
    Memory_Zero(outApi, sizeof(*outApi));
    outApi->_anisotropy = 1.0f;
    const char* Missing = NULL;
    outApi->_genTextures = FindFunction("glGenTextures", &Missing).GenTextures;
    outApi->_deleteTextures = FindFunction("glDeleteTextures", &Missing).DeleteTextures;
    outApi->_bindTexture = FindFunction("glBindTexture", &Missing).BindTexture;
    outApi->_texImage3D = FindFunction("glTexImage3D", &Missing).TexImage3D;
    outApi->_texSubImage3D = FindFunction("glTexSubImage3D", &Missing).TexSubImage3D;
    outApi->_generateMipmap = FindFunction("glGenerateMipmap", &Missing).GenerateMipmap;
    outApi->_texParameteri = FindFunction("glTexParameteri", &Missing).TexParameteri;
    outApi->_texParameterf = FindFunction("glTexParameterf", &Missing).TexParameterf;
    outApi->_pixelStorei = FindFunction("glPixelStorei", &Missing).PixelStorei;
    outApi->_getIntegerv = FindFunction("glGetIntegerv", &Missing).GetIntegerv;
    outApi->_getFloatv = FindFunction("glGetFloatv", &Missing).GetFloatv;
    outApi->_getError = FindFunction("glGetError", &Missing).GetError;
    outApi->_missing = Missing;
    outApi->_isAvailable = Missing == NULL;
    if (!outApi->_isAvailable)
    {
        return false;
    }
    // Anisotropic filtering is an extension (core since GL 4.6): a driver without it reports an error for the query and
    // leaves the value alone, and the arrays filter trilinearly only.
    DrainErrors(outApi);
    float Largest = 0.0f;
    outApi->_getFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &Largest);
    if ((outApi->_getError() == GL_NO_ERROR) && (Largest > 1.0f))
    {
        outApi->_anisotropy = (Largest < MAX_ANISOTROPY) ? Largest : MAX_ANISOTROPY;
    }
    return true;
}

Error GLTextureArray_Create(const GLTextureArrayApi* api, GLTextureArrayFormat format, int32_t width, int32_t height,
    int32_t layerCount, const uint8_t* const* layers, const uint8_t* fillTexel, unsigned int* outId)
{
    *outId = 0U;
    if (!api->_isAvailable)
    {
        return Error_Construct4(ErrorCode_InvalidState, "Texture arrays are unavailable (%s is missing).",
            (api->_missing != NULL) ? api->_missing : "a GL function");
    }
    if ((width < 1) || (height < 1) || (width > MAX_SIDE) || (height > MAX_SIDE) || (layerCount < 1)
        || (layerCount > MAX_LAYERS))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "A texture array of %d x %d texels and %d layers is "
            "outside the limits (1 to %d per side, 1 to %d layers).", (int)width, (int)height, (int)layerCount,
            MAX_SIDE, MAX_LAYERS);
    }
    bool IsRgba = format == GLTextureArrayFormat_Rgba8;
    size_t TexelSize = IsRgba ? 4U : 2U;
    size_t TexelCount = (size_t)width * (size_t)height;
    uint8_t* Fill = NULL;
    for (int32_t Layer = 0; Layer < layerCount; Layer++)
    {
        if (layers[Layer] != NULL)
        {
            continue;
        }
        if (fillTexel == NULL)
        {
            Memory_Free(Fill);
            return Error_Construct4(ErrorCode_IllegalArgument, "Layer %d of a texture array has no texels and no fill.",
                (int)Layer);
        }
        if (Fill == NULL)
        {
            Fill = Memory_Allocate(TexelCount * TexelSize);
            for (size_t Texel = 0U; Texel < TexelCount; Texel++)
            {
                Memory_Copy(fillTexel, &Fill[Texel * TexelSize], TexelSize);
            }
        }
    }

    // Unit 0's array target is never one a draw reads, so making it active keeps the arrays bound for drawing intact.
    rlActiveTextureSlot(0);
    DrainErrors(api);
    int PreviousAlignment = 4;
    api->_getIntegerv(GL_UNPACK_ALIGNMENT, &PreviousAlignment);
    api->_pixelStorei(GL_UNPACK_ALIGNMENT, 1);
    unsigned int Id = 0U;
    api->_genTextures(1, &Id);
    api->_bindTexture(GL_TEXTURE_2D_ARRAY, Id);
    int InternalFormat = IsRgba ? GL_RGBA8 : GL_RG8;
    unsigned int ExternalFormat = IsRgba ? (unsigned int)GL_RGBA : (unsigned int)GL_RG;
    api->_texImage3D(GL_TEXTURE_2D_ARRAY, 0, InternalFormat, (int)width, (int)height, (int)layerCount, 0, ExternalFormat,
        GL_UNSIGNED_BYTE, NULL);
    for (int32_t Layer = 0; Layer < layerCount; Layer++)
    {
        const uint8_t* Texels = (layers[Layer] != NULL) ? layers[Layer] : Fill;
        api->_texSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, (int)Layer, (int)width, (int)height, 1, ExternalFormat,
            GL_UNSIGNED_BYTE, Texels);
    }
    api->_generateMipmap(GL_TEXTURE_2D_ARRAY);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (api->_anisotropy > 1.0f)
    {
        api->_texParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, api->_anisotropy);
    }
    unsigned int Status = api->_getError();
    api->_bindTexture(GL_TEXTURE_2D_ARRAY, 0U);
    api->_pixelStorei(GL_UNPACK_ALIGNMENT, PreviousAlignment);
    Memory_Free(Fill);
    if (Status != GL_NO_ERROR)
    {
        api->_deleteTextures(1, &Id);
        if (Status == GL_OUT_OF_MEMORY)
        {
            return Error_Construct4(ErrorCode_InvalidState, "The GPU has no room for a texture array of %d x %d texels "
                "and %d layers.", (int)width, (int)height, (int)layerCount);
        }
        return Error_Construct4(ErrorCode_InvalidState, "GL refused a texture array of %d x %d texels and %d layers "
            "(error 0x%04X).", (int)width, (int)height, (int)layerCount, Status);
    }
    *outId = Id;
    return Error_CreateSuccess();
}

void GLTextureArray_Bind(const GLTextureArrayApi* api, int32_t unit, unsigned int id)
{
    rlActiveTextureSlot((int)unit);
    api->_bindTexture(GL_TEXTURE_2D_ARRAY, id);
    rlActiveTextureSlot(0);
}

void GLTextureArray_Delete(const GLTextureArrayApi* api, unsigned int id)
{
    if (id != 0U)
    {
        api->_deleteTextures(1, &id);
    }
}
