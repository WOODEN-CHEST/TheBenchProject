#include "GLShadowArray.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"

/*
 * The shadow levels' maps through the GL functions raylib's loader resolves (GLShadowArray.h): direct OpenGL calls, as
 * GLTextureArray.c's.
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
#define GL_TEXTURE_COMPARE_MODE 0x884CU
#define GL_TEXTURE_COMPARE_FUNC 0x884DU
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_LEQUAL 0x0203
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_DEPTH_COMPONENT 0x1902U
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_FLOAT 0x1406U
#define GL_FRAMEBUFFER 0x8D40U
#define GL_DEPTH_ATTACHMENT 0x8D00U
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5U
#define GL_NONE 0U
#define GL_DEPTH_CLAMP 0x864FU
#define GL_MAX_TEXTURE_SIZE 0x0D33U
#define GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FFU
#define GL_NO_ERROR 0U
#define GL_OUT_OF_MEMORY 0x0505U
/** How many queued GL errors are read off before a creation (a lost context could report one forever). */
#define MAX_DRAINED_ERRORS 32


// Types.
/* Reads a loader's object pointer as a function pointer: ISO C has no cast between the two, a union reads the same bytes
 * (every target platform's function pointers are object-pointer sized: checked below). */
typedef union GLShadowArrayAddressUnion
{
    void* Object;
    GLShadowArrayGenFunction Gen;
    GLShadowArrayDeleteFunction Delete;
    GLShadowArrayBindFunction Bind;
    GLShadowArrayTexImage3DFunction TexImage3D;
    GLShadowArrayTexParameteriFunction TexParameteri;
    GLShadowArrayFramebufferTextureLayerFunction FramebufferTextureLayer;
    GLShadowArrayBufferFunction Buffer;
    GLShadowArrayCheckFramebufferStatusFunction CheckFramebufferStatus;
    GLShadowArrayCapabilityFunction Capability;
    GLShadowArrayGetIntegervFunction GetIntegerv;
    GLShadowArrayGetErrorFunction GetError;
} GLShadowArrayAddress;

_Static_assert(sizeof(void*) == sizeof(GLShadowArrayGetErrorFunction), "GL functions are read through object pointers");


// Static functions.
/* One function's address through raylib's loader; the first one missing is remembered for the message. */
static GLShadowArrayAddress FindFunction(const char* name, const char** ioMissing)
{
    GLShadowArrayAddress Address = { .Object = rlGetProcAddress(name) };
    if ((Address.Object == NULL) && (*ioMissing == NULL))
    {
        *ioMissing = name;
    }
    return Address;
}

/* Reads off errors queued by earlier calls, so the next check sees only its own. */
static void DrainErrors(const GLShadowArrayApi* api)
{
    for (int Count = 0; (Count < MAX_DRAINED_ERRORS) && (api->_getError() != GL_NO_ERROR); Count++)
    {
    }
}

/* Checks what a creation needs: an available API, and a size and a layer count within this module's and the GPU's
 * limits. */
static Error CheckCreation(const GLShadowArrayApi* api, int size, int layerCount)
{
    if (!api->_isAvailable)
    {
        return Error_Construct4(ErrorCode_InvalidState, "Shadow map arrays are unavailable (%s is missing).",
            (api->_missing != NULL) ? api->_missing : "a GL function");
    }
    int MaxSize = 0;
    int MaxLayers = 0;
    api->_getIntegerv(GL_MAX_TEXTURE_SIZE, &MaxSize);
    api->_getIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &MaxLayers);
    if ((size < 1) || (size > GL_SHADOW_ARRAY_MAX_SIZE) || (size > MaxSize))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "A shadow map of %d texels a side is outside the limits "
            "(1 to %d, and the GPU's %d).", size, GL_SHADOW_ARRAY_MAX_SIZE, MaxSize);
    }
    if ((layerCount < 1) || (layerCount > GL_SHADOW_ARRAY_MAX_LAYERS) || (layerCount > MaxLayers))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "A shadow map array of %d layers is outside the limits (1 "
            "to %d, and the GPU's %d).", layerCount, GL_SHADOW_ARRAY_MAX_LAYERS, MaxLayers);
    }
    return Error_CreateSuccess();
}

/* Makes the texture: every layer's storage, linear filtering with the depth comparison, clamped, no mipmaps. Leaves
 * unit 0's array target unbound. Returns GL's error after it (GL_NO_ERROR when made). */
static unsigned int MakeTexture(const GLShadowArrayApi* api, int size, int layerCount, unsigned int* outTexture)
{
    rlActiveTextureSlot(0);
    DrainErrors(api);
    unsigned int Texture = 0U;
    api->_genTextures(1, &Texture);
    api->_bindTexture(GL_TEXTURE_2D_ARRAY, Texture);
    api->_texImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, size, size, layerCount, 0, GL_DEPTH_COMPONENT,
        GL_FLOAT, NULL);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    api->_texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    unsigned int Status = api->_getError();
    api->_bindTexture(GL_TEXTURE_2D_ARRAY, 0U);
    *outTexture = Texture;
    return Status;
}


// Internal functions (declared in GLShadowArray.h).
bool GLShadowArray_LoadApi(GLShadowArrayApi* outApi)
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
    outApi->_drawBuffer = FindFunction("glDrawBuffer", &Missing).Buffer;
    outApi->_readBuffer = FindFunction("glReadBuffer", &Missing).Buffer;
    outApi->_checkFramebufferStatus = FindFunction("glCheckFramebufferStatus", &Missing).CheckFramebufferStatus;
    outApi->_enable = FindFunction("glEnable", &Missing).Capability;
    outApi->_disable = FindFunction("glDisable", &Missing).Capability;
    outApi->_getIntegerv = FindFunction("glGetIntegerv", &Missing).GetIntegerv;
    outApi->_getError = FindFunction("glGetError", &Missing).GetError;
    outApi->_missing = Missing;
    outApi->_isAvailable = Missing == NULL;
    return outApi->_isAvailable;
}

Error GLShadowArray_Create(const GLShadowArrayApi* api, int size, int layerCount, GLShadowArray* outArray)
{
    Memory_Zero(outArray, sizeof(*outArray));
    Error Check = CheckCreation(api, size, layerCount);
    if (Check.Code != ErrorCode_Success)
    {
        return Check;
    }

    unsigned int Texture = 0U;
    unsigned int Status = MakeTexture(api, size, layerCount, &Texture);
    if (Status != GL_NO_ERROR)
    {
        api->_deleteTextures(1, &Texture);
        if (Status == GL_OUT_OF_MEMORY)
        {
            return Error_Construct4(ErrorCode_InvalidState, "The GPU has no room for %d shadow maps of %d x %d texels.",
                layerCount, size, size);
        }
        return Error_Construct4(ErrorCode_InvalidState, "GL refused %d shadow maps of %d x %d texels (error 0x%04X).",
            layerCount, size, size, Status);
    }

    // A framebuffer of depth alone: no colour is drawn or read.
    unsigned int Framebuffer = 0U;
    api->_genFramebuffers(1, &Framebuffer);
    api->_bindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
    api->_framebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, Texture, 0, 0);
    api->_drawBuffer(GL_NONE);
    api->_readBuffer(GL_NONE);
    unsigned int Completeness = api->_checkFramebufferStatus(GL_FRAMEBUFFER);
    Status = api->_getError();
    api->_bindFramebuffer(GL_FRAMEBUFFER, 0U);
    if ((Completeness != GL_FRAMEBUFFER_COMPLETE) || (Status != GL_NO_ERROR))
    {
        api->_deleteFramebuffers(1, &Framebuffer);
        api->_deleteTextures(1, &Texture);
        return Error_Construct4(ErrorCode_InvalidState, "The shadow maps' framebuffer is incomplete (status 0x%04X, "
            "error 0x%04X).", Completeness, Status);
    }

    outArray->_texture = Texture;
    outArray->_framebuffer = Framebuffer;
    outArray->_size = size;
    outArray->_layerCount = layerCount;
    return Error_CreateSuccess();
}

void GLShadowArray_Destroy(const GLShadowArrayApi* api, GLShadowArray* array)
{
    if (array->_framebuffer != 0U)
    {
        api->_bindFramebuffer(GL_FRAMEBUFFER, 0U);
        api->_deleteFramebuffers(1, &array->_framebuffer);
    }
    if (array->_texture != 0U)
    {
        api->_deleteTextures(1, &array->_texture);
    }
    Memory_Zero(array, sizeof(*array));
}

void GLShadowArray_AttachLayer(const GLShadowArrayApi* api, const GLShadowArray* array, int layer)
{
    rlDrawRenderBatchActive();
    api->_bindFramebuffer(GL_FRAMEBUFFER, array->_framebuffer);
    api->_framebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, array->_texture, 0, layer);
    api->_bindFramebuffer(GL_FRAMEBUFFER, 0U);
}

void GLShadowArray_BeginDepthClamp(const GLShadowArrayApi* api)
{
    rlDrawRenderBatchActive();
    api->_enable(GL_DEPTH_CLAMP);
}

void GLShadowArray_EndDepthClamp(const GLShadowArrayApi* api)
{
    rlDrawRenderBatchActive();
    api->_disable(GL_DEPTH_CLAMP);
}

void GLShadowArray_Bind(const GLShadowArrayApi* api, const GLShadowArray* array, int unit)
{
    rlActiveTextureSlot(unit);
    api->_bindTexture(GL_TEXTURE_2D_ARRAY, array->_texture);
}
