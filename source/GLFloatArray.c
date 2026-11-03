#include "GLFloatArray.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"

/*
 * Float colour texture arrays through the GL functions raylib's loader resolves (GLFloatArray.h): direct OpenGL calls,
 * as GLTextureArray.c's.
 */


// Macros.
/* The GL enumerants used here (the OpenGL 3.3 core specification's values). */
#define GL_TEXTURE_2D_ARRAY 0x8C1AU
#define GL_TEXTURE_MIN_FILTER 0x2801U
#define GL_TEXTURE_MAG_FILTER 0x2800U
#define GL_TEXTURE_WRAP_S 0x2802U
#define GL_TEXTURE_WRAP_T 0x2803U
#define GL_TEXTURE_BASE_LEVEL 0x813CU
#define GL_TEXTURE_MAX_LEVEL 0x813DU
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_R32F 0x822E
#define GL_RG32F 0x8230
#define GL_RED 0x1903U
#define GL_RG 0x8227U
#define GL_FLOAT 0x1406U
#define GL_FRAMEBUFFER 0x8D40U
#define GL_RENDERBUFFER 0x8D41U
#define GL_COLOR_ATTACHMENT0 0x8CE0U
#define GL_DEPTH_ATTACHMENT 0x8D00U
#define GL_DEPTH_COMPONENT24 0x81A6U
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5U
#define GL_COLOR 0x1800U
#define GL_DEPTH 0x1801U
#define GL_MAX_TEXTURE_SIZE 0x0D33U
#define GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FFU
#define GL_NO_ERROR 0U
#define GL_OUT_OF_MEMORY 0x0505U
/** How many queued GL errors are read off before a creation (a lost context could report one forever). */
#define MAX_DRAINED_ERRORS 32


// Types.
/* Reads a loader's object pointer as a function pointer: ISO C has no cast between the two, a union reads the same bytes
 * (every target platform's function pointers are object-pointer sized: checked below). */
typedef union GLFloatArrayAddressUnion
{
    void* Object;
    GLFloatArrayGenFunction Gen;
    GLFloatArrayDeleteFunction Delete;
    GLFloatArrayBindFunction Bind;
    GLFloatArrayTexImage3DFunction TexImage3D;
    GLFloatArrayTexParameteriFunction TexParameteri;
    GLFloatArrayFramebufferTextureLayerFunction FramebufferTextureLayer;
    GLFloatArrayRenderbufferStorageFunction RenderbufferStorage;
    GLFloatArrayFramebufferRenderbufferFunction FramebufferRenderbuffer;
    GLFloatArrayCheckFramebufferStatusFunction CheckFramebufferStatus;
    GLFloatArrayClearBufferfvFunction ClearBufferfv;
    GLFloatArrayGetIntegervFunction GetIntegerv;
    GLFloatArrayGetErrorFunction GetError;
} GLFloatArrayAddress;

_Static_assert(sizeof(void*) == sizeof(GLFloatArrayGetErrorFunction), "GL functions are read through object pointers");


// Static functions.
/* One function's address through raylib's loader; the first one missing is remembered for the message. */
static GLFloatArrayAddress FindFunction(const char* name, const char** ioMissing)
{
    GLFloatArrayAddress Address = { .Object = rlGetProcAddress(name) };
    if ((Address.Object == NULL) && (*ioMissing == NULL))
    {
        *ioMissing = name;
    }
    return Address;
}

/* Reads off errors queued by earlier calls, so the next check sees only its own. */
static void DrainErrors(const GLFloatArrayApi* api)
{
    for (int Count = 0; (Count < MAX_DRAINED_ERRORS) && (api->_getError() != GL_NO_ERROR); Count++)
    {
    }
}

/* Checks what a creation needs: an available API, and a size, a layer count and a channel count within this module's
 * and the GPU's limits. */
static Error CheckCreation(const GLFloatArrayApi* api, int size, int layerCount, int channelCount)
{
    if (!api->_isAvailable)
    {
        return Error_Construct4(ErrorCode_InvalidState, "Float texture arrays are unavailable (%s is missing).",
            (api->_missing != NULL) ? api->_missing : "a GL function");
    }
    int MaxSize = 0;
    int MaxLayers = 0;
    api->_getIntegerv(GL_MAX_TEXTURE_SIZE, &MaxSize);
    api->_getIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &MaxLayers);
    if ((size < 1) || (size > GL_FLOAT_ARRAY_MAX_SIZE) || (size > MaxSize))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "A float texture array of %d texels a side is outside the "
            "limits (1 to %d, and the GPU's %d).", size, GL_FLOAT_ARRAY_MAX_SIZE, MaxSize);
    }
    if ((layerCount < 1) || (layerCount > GL_FLOAT_ARRAY_MAX_LAYERS) || (layerCount > MaxLayers))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "A float texture array of %d layers is outside the limits "
            "(1 to %d, and the GPU's %d).", layerCount, GL_FLOAT_ARRAY_MAX_LAYERS, MaxLayers);
    }
    if ((channelCount != 1) && (channelCount != 2))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "A float texture array has 1 or 2 channels, not %d.",
            channelCount);
    }
    return Error_CreateSuccess();
}

/* Makes the texture: every layer's storage, linear filtering, clamped, no mipmaps. Leaves unit 0's array target unbound.
 * Returns GL's error after it (GL_NO_ERROR when made). */
static unsigned int MakeTexture(const GLFloatArrayApi* api, int size, int layerCount, int channelCount,
    unsigned int* outTexture)
{
    rlActiveTextureSlot(0);
    DrainErrors(api);
    unsigned int Texture = 0U;
    api->_genTextures(1, &Texture);
    api->_bindTexture(GL_TEXTURE_2D_ARRAY, Texture);
    bool IsPair = (channelCount == 2);
    api->_texImage3D(GL_TEXTURE_2D_ARRAY, 0, IsPair ? GL_RG32F : GL_R32F, size, size, layerCount, 0,
        IsPair ? GL_RG : GL_RED, GL_FLOAT, NULL);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
    unsigned int Status = api->_getError();
    api->_bindTexture(GL_TEXTURE_2D_ARRAY, 0U);
    *outTexture = Texture;
    return Status;
}

/* Makes the framebuffer with the texture's first layer as its colour and, when asked, a depth renderbuffer of the
 * layers' size; checks it complete. Leaves no framebuffer or renderbuffer bound. Returns whether it is complete, with
 * GL's status and error for the message. */
static bool MakeFramebuffer(const GLFloatArrayApi* api, unsigned int texture, int size, bool hasDepth,
    unsigned int* outFramebuffer, unsigned int* outDepth, unsigned int* outCompleteness, unsigned int* outError)
{
    unsigned int Framebuffer = 0U;
    unsigned int Depth = 0U;
    api->_genFramebuffers(1, &Framebuffer);
    api->_bindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
    api->_framebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, 0, 0);
    if (hasDepth)
    {
        api->_genRenderbuffers(1, &Depth);
        api->_bindRenderbuffer(GL_RENDERBUFFER, Depth);
        api->_renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
        api->_bindRenderbuffer(GL_RENDERBUFFER, 0U);
        api->_framebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, Depth);
    }
    *outCompleteness = api->_checkFramebufferStatus(GL_FRAMEBUFFER);
    *outError = api->_getError();
    api->_bindFramebuffer(GL_FRAMEBUFFER, 0U);
    *outFramebuffer = Framebuffer;
    *outDepth = Depth;
    return (*outCompleteness == GL_FRAMEBUFFER_COMPLETE) && (*outError == GL_NO_ERROR);
}


// Internal functions (declared in GLFloatArray.h).
bool GLFloatArray_LoadApi(GLFloatArrayApi* outApi)
{
    Memory_Zero(outApi, sizeof(*outApi));
    const char* Missing = NULL;
    outApi->_genTextures = FindFunction("glGenTextures", &Missing).Gen;
    outApi->_deleteTextures = FindFunction("glDeleteTextures", &Missing).Delete;
    outApi->_bindTexture = FindFunction("glBindTexture", &Missing).Bind;
    outApi->_texImage3D = FindFunction("glTexImage3D", &Missing).TexImage3D;
    outApi->_texParameteri = FindFunction("glTexParameteri", &Missing).TexParameteri;
    outApi->_genFramebuffers = FindFunction("glGenFramebuffers", &Missing).Gen;
    outApi->_deleteFramebuffers = FindFunction("glDeleteFramebuffers", &Missing).Delete;
    outApi->_bindFramebuffer = FindFunction("glBindFramebuffer", &Missing).Bind;
    outApi->_framebufferTextureLayer = FindFunction("glFramebufferTextureLayer", &Missing).FramebufferTextureLayer;
    outApi->_genRenderbuffers = FindFunction("glGenRenderbuffers", &Missing).Gen;
    outApi->_deleteRenderbuffers = FindFunction("glDeleteRenderbuffers", &Missing).Delete;
    outApi->_bindRenderbuffer = FindFunction("glBindRenderbuffer", &Missing).Bind;
    outApi->_renderbufferStorage = FindFunction("glRenderbufferStorage", &Missing).RenderbufferStorage;
    outApi->_framebufferRenderbuffer = FindFunction("glFramebufferRenderbuffer", &Missing).FramebufferRenderbuffer;
    outApi->_checkFramebufferStatus = FindFunction("glCheckFramebufferStatus", &Missing).CheckFramebufferStatus;
    outApi->_clearBufferfv = FindFunction("glClearBufferfv", &Missing).ClearBufferfv;
    outApi->_getIntegerv = FindFunction("glGetIntegerv", &Missing).GetIntegerv;
    outApi->_getError = FindFunction("glGetError", &Missing).GetError;
    outApi->_missing = Missing;
    outApi->_isAvailable = Missing == NULL;
    return outApi->_isAvailable;
}

Error GLFloatArray_Create(const GLFloatArrayApi* api, int size, int layerCount, int channelCount, bool hasDepth,
    GLFloatArray* outArray)
{
    Memory_Zero(outArray, sizeof(*outArray));
    Error Check = CheckCreation(api, size, layerCount, channelCount);
    if (Check.Code != ErrorCode_Success)
    {
        return Check;
    }

    unsigned int Texture = 0U;
    unsigned int Status = MakeTexture(api, size, layerCount, channelCount, &Texture);
    if (Status != GL_NO_ERROR)
    {
        api->_deleteTextures(1, &Texture);
        if (Status == GL_OUT_OF_MEMORY)
        {
            return Error_Construct4(ErrorCode_InvalidState, "The GPU has no room for %d float maps of %d x %d texels.",
                layerCount, size, size);
        }
        return Error_Construct4(ErrorCode_InvalidState, "GL refused %d float maps of %d x %d texels (error 0x%04X).",
            layerCount, size, size, Status);
    }

    unsigned int Framebuffer = 0U;
    unsigned int Depth = 0U;
    unsigned int Completeness = 0U;
    if (!MakeFramebuffer(api, Texture, size, hasDepth, &Framebuffer, &Depth, &Completeness, &Status))
    {
        api->_deleteFramebuffers(1, &Framebuffer);
        if (Depth != 0U)
        {
            api->_deleteRenderbuffers(1, &Depth);
        }
        api->_deleteTextures(1, &Texture);
        return Error_Construct4(ErrorCode_InvalidState, "The float maps' framebuffer is incomplete (status 0x%04X, "
            "error 0x%04X).", Completeness, Status);
    }

    outArray->_texture = Texture;
    outArray->_framebuffer = Framebuffer;
    outArray->_depth = Depth;
    outArray->_size = size;
    outArray->_layerCount = layerCount;
    outArray->_channelCount = channelCount;
    return Error_CreateSuccess();
}

void GLFloatArray_Destroy(const GLFloatArrayApi* api, GLFloatArray* array)
{
    if (array->_framebuffer != 0U)
    {
        api->_bindFramebuffer(GL_FRAMEBUFFER, 0U);
        api->_deleteFramebuffers(1, &array->_framebuffer);
    }
    if (array->_depth != 0U)
    {
        api->_deleteRenderbuffers(1, &array->_depth);
    }
    if (array->_texture != 0U)
    {
        api->_deleteTextures(1, &array->_texture);
    }
    Memory_Zero(array, sizeof(*array));
}

void GLFloatArray_AttachLayer(const GLFloatArrayApi* api, const GLFloatArray* array, int layer)
{
    rlDrawRenderBatchActive();
    api->_bindFramebuffer(GL_FRAMEBUFFER, array->_framebuffer);
    api->_framebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, array->_texture, 0, layer);
    api->_bindFramebuffer(GL_FRAMEBUFFER, 0U);
}

void GLFloatArray_Clear(const GLFloatArrayApi* api, const GLFloatArray* array, float red, float green)
{
    rlDrawRenderBatchActive();
    float Color[4] = { red, green, 0.0f, 1.0f };
    api->_clearBufferfv(GL_COLOR, 0, Color);
    if (array->_depth != 0U)
    {
        float Far = 1.0f;
        api->_clearBufferfv(GL_DEPTH, 0, &Far);
    }
}

void GLFloatArray_Bind(const GLFloatArrayApi* api, const GLFloatArray* array, int unit)
{
    rlActiveTextureSlot(unit);
    api->_bindTexture(GL_TEXTURE_2D_ARRAY, array->_texture);
}
