#include "GLTexture3D.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"

/*
 * 3D textures through the GL functions raylib's loader resolves (GLTexture3D.h): direct OpenGL calls (GLTextureArray.c,
 * GLReadback.c, GLDepth.c, GLSliceMap.c and GLShadowArray.c hold the only others).
 */


// Macros.
/* The GL enumerants used here (the OpenGL 3.3 core specification's values). */
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_R8 0x8229
#define GL_RGBA8 0x8058
#define GL_RED 0x1903
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_NO_ERROR 0U
#define GL_OUT_OF_MEMORY 0x0505U
/** The largest side Create accepts (GL 3.3 guarantees 256; a driver refusing more fails the creation with a GL error,
 *  which is reported). */
#define MAX_SIDE 2048
/** How many queued GL errors are read off before a creation (a lost context could report one forever). */
#define MAX_DRAINED_ERRORS 32


// Types.
/* Reads a loader's object pointer as a function pointer: ISO C has no cast between the two, a union reads the same bytes
 * (every target platform's function pointers are object-pointer sized: checked below). */
typedef union GLFunctionAddressUnion
{
    void* Object;
    GLTexture3DGenTexturesFunction GenTextures;
    GLTexture3DDeleteTexturesFunction DeleteTextures;
    GLTexture3DBindTextureFunction BindTexture;
    GLTexture3DTexImage3DFunction TexImage3D;
    GLTexture3DGenerateMipmapFunction GenerateMipmap;
    GLTexture3DTexParameteriFunction TexParameteri;
    GLTexture3DPixelStoreiFunction PixelStorei;
    GLTexture3DGetIntegervFunction GetIntegerv;
    GLTexture3DGetErrorFunction GetError;
} GLFunctionAddress;

_Static_assert(sizeof(void*) == sizeof(GLTexture3DGetErrorFunction), "GL functions are read through object pointers");


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
static void DrainErrors(const GLTexture3DApi* api)
{
    for (int32_t Count = 0; (Count < MAX_DRAINED_ERRORS) && (api->_getError() != GL_NO_ERROR); Count++)
    {
    }
}


// Internal functions (declared in GLTexture3D.h).
bool GLTexture3D_LoadApi(GLTexture3DApi* outApi)
{
    Memory_Zero(outApi, sizeof(*outApi));
    const char* Missing = NULL;
    outApi->_genTextures = FindFunction("glGenTextures", &Missing).GenTextures;
    outApi->_deleteTextures = FindFunction("glDeleteTextures", &Missing).DeleteTextures;
    outApi->_bindTexture = FindFunction("glBindTexture", &Missing).BindTexture;
    outApi->_texImage3D = FindFunction("glTexImage3D", &Missing).TexImage3D;
    outApi->_generateMipmap = FindFunction("glGenerateMipmap", &Missing).GenerateMipmap;
    outApi->_texParameteri = FindFunction("glTexParameteri", &Missing).TexParameteri;
    outApi->_pixelStorei = FindFunction("glPixelStorei", &Missing).PixelStorei;
    outApi->_getIntegerv = FindFunction("glGetIntegerv", &Missing).GetIntegerv;
    outApi->_getError = FindFunction("glGetError", &Missing).GetError;
    outApi->_missing = Missing;
    outApi->_isAvailable = Missing == NULL;
    return outApi->_isAvailable;
}

Error GLTexture3D_Create(const GLTexture3DApi* api, GLTexture3DFormat format, int32_t width, int32_t height,
    int32_t depth, const uint8_t* texels, unsigned int* outId)
{
    *outId = 0U;
    if (!api->_isAvailable)
    {
        return Error_Construct4(ErrorCode_InvalidState, "3D textures are unavailable (%s is missing).",
            (api->_missing != NULL) ? api->_missing : "a GL function");
    }
    if ((width < 1) || (height < 1) || (depth < 1) || (width > MAX_SIDE) || (height > MAX_SIDE) || (depth > MAX_SIDE))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "A 3D texture of %d x %d x %d texels is outside the "
            "limits (1 to %d per side).", (int)width, (int)height, (int)depth, MAX_SIDE);
    }
    bool IsRgba = format == GLTexture3DFormat_Rgba8;

    // Unit 0's 3D target is never one a draw reads, so making it active keeps the textures bound for drawing intact.
    rlActiveTextureSlot(0);
    DrainErrors(api);
    int PreviousAlignment = 4;
    api->_getIntegerv(GL_UNPACK_ALIGNMENT, &PreviousAlignment);
    api->_pixelStorei(GL_UNPACK_ALIGNMENT, 1);
    unsigned int Id = 0U;
    api->_genTextures(1, &Id);
    api->_bindTexture(GL_TEXTURE_3D, Id);
    api->_texImage3D(GL_TEXTURE_3D, 0, IsRgba ? GL_RGBA8 : GL_R8, (int)width, (int)height, (int)depth, 0,
        IsRgba ? (unsigned int)GL_RGBA : (unsigned int)GL_RED, GL_UNSIGNED_BYTE, texels);
    api->_generateMipmap(GL_TEXTURE_3D);
    api->_texParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    api->_texParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    api->_texParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    api->_texParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    api->_texParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    unsigned int Status = api->_getError();
    api->_bindTexture(GL_TEXTURE_3D, 0U);
    api->_pixelStorei(GL_UNPACK_ALIGNMENT, PreviousAlignment);
    if (Status != GL_NO_ERROR)
    {
        api->_deleteTextures(1, &Id);
        if (Status == GL_OUT_OF_MEMORY)
        {
            return Error_Construct4(ErrorCode_InvalidState, "The GPU has no room for a 3D texture of %d x %d x %d "
                "texels.", (int)width, (int)height, (int)depth);
        }
        return Error_Construct4(ErrorCode_InvalidState, "GL refused a 3D texture of %d x %d x %d texels (error "
            "0x%04X).", (int)width, (int)height, (int)depth, Status);
    }
    *outId = Id;
    return Error_CreateSuccess();
}

void GLTexture3D_Bind(const GLTexture3DApi* api, int32_t unit, unsigned int id)
{
    rlActiveTextureSlot((int)unit);
    api->_bindTexture(GL_TEXTURE_3D, id);
    rlActiveTextureSlot(0);
}

void GLTexture3D_Delete(const GLTexture3DApi* api, unsigned int id)
{
    if (id != 0U)
    {
        api->_deleteTextures(1, &id);
    }
}
